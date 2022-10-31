#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <arpa/inet.h>

#include <string>
#include <memory>
#include <map>

#include "chip8.h"
#include "io.h"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"

// ------------------------------------------------------------------------

#define ERROR {errer();}
#define STEP {program_counter+=2;}
#define X uint8_t x = (op >> 8) & 0xf
#define Y uint8_t y = (op >> 4) & 0xf
#define OP uint16_t op = ntohs(((uint16_t *)memory)[program_counter>>1]);
#define IMMEDIATE4 uint8_t immediate = op & 0xf
#define IMMEDIATE8 uint8_t immediate = op & 0xff
#define IMMEDIATE12 uint16_t immediate = op & 0xfff
#define NANOS_PER_TICK (16666666) // ~60 Hz clock
#define TICKS_PER_SECOND (60) // ~60 Hz clock
#define INPUT_TICKS (10) // roughly 1/6 second window for input

// ------------------------------------------------------------------------

#define JIT_LOC(var) var ## _loc
#define JIT_PTR(var) var ## _ptr
#define JIT_VALUE(var) var ## _value

#define JIT_CALL(host_fn_name) \
    auto fn = module->getOrInsertFunction(host_fn_name, fn_type).getCallee(); \
    builder->CreateCall(fn_type, fn);

#define JIT_GETPTR16(var) \
  auto JIT_LOC(var) = llvm::ConstantInt::get(*context, llvm::APInt(sizeof(uint16_t *)*8, reinterpret_cast<uint64_t>(&var), false)); \
  auto JIT_PTR(var) = builder->CreateIntToPtr(JIT_LOC(var), llvm::Type::getInt16PtrTy(*context));
#define JIT_LOAD16(var) \
  auto JIT_VALUE(var) = builder->CreateLoad(llvm::Type::getInt16Ty(*context), JIT_PTR(var));

#define JIT_GETPTRREG(x) \
  auto JIT_LOC(x) = llvm::ConstantInt::get(*context, llvm::APInt(sizeof(uint8_t *)*8, reinterpret_cast<uint64_t>(&(regs[x])), false)); \
  auto JIT_PTR(x) = builder->CreateIntToPtr(JIT_LOC(x), llvm::Type::getInt8PtrTy(*context));
#define JIT_LOADREG(x) \
  auto JIT_VALUE(x) = builder->CreateLoad(llvm::Type::getInt8Ty(*context), JIT_PTR(x));

#define JIT_IMM16 \
  auto JIT_VALUE(immediate) = builder->getInt16(immediate);
#define JIT_IMM8 \
  auto JIT_VALUE(immediate) = builder->getInt8(immediate);

#define JIT_STEP \
  JIT_GETPTR16(program_counter); \
  JIT_LOAD16(program_counter); \
  auto two = builder->getInt16(2); \
  auto pc_plus_two = builder->CreateAdd(JIT_VALUE(program_counter), two); \
  builder->CreateStore(pc_plus_two, JIT_PTR(program_counter)); \
  continue;
#define JIT_RETURN builder->CreateRet(nullptr);
#define JIT_DONE goto end_of_block;

// ------------------------------------------------------------------------

int last_tick = 0;
int8_t delay_timer = 0;
int8_t sound_timer = 0;
uint16_t op = 0;
uint32_t keys_down[INPUT_TICKS];
int interrupt_count = 0;

typedef void (*code)(void);  // function pointer typedef
std::unique_ptr<std::map<uint16_t, code>> trace_cache;
llvm::ExitOnError ExitOnErr;

// ------------------------------------------------------------------------

extern "C"
{
  void clear_key(uint8_t key)
  {
    for (int i = 0; i < INPUT_TICKS; ++i)
      {
        keys_down[i] &= (0xffff ^ (1<<key));
      }
  }

  uint32_t all_keys_down()
  {
    uint32_t all_keys = 0;

    for (int i = 0; i < INPUT_TICKS; ++i)
      {
        all_keys |= keys_down[i];
      }
    return all_keys;
  }

  int tick()
  {
    struct timespec spec;

    clock_gettime(CLOCK_MONOTONIC, &spec);
    return ((spec.tv_nsec / NANOS_PER_TICK) % TICKS_PER_SECOND);
  }

  void interrupt()
  {
    int current_tick = tick();

    if (current_tick != last_tick)
      {
        if (delay_timer > 0)
          {
            --delay_timer;
          }
        if (sound_timer > 0)
          {
            --sound_timer;
          }
        last_tick = current_tick;
      }
    keys_down[interrupt_count] = read_keys_io();
    interrupt_count = (interrupt_count + 1) % INPUT_TICKS;
  }

  void errer()
  {
    fprintf(stderr, "Error: op=%04x pc=%04x\n", op, program_counter);
    exit(-1);
  }

  void retern()
  {
    interrupt();
    if (stack_pointer > 0)
      {
        program_counter = stack[--stack_pointer];
      }
    else
      ERROR;
  }

  void call()
  {
    OP;
    IMMEDIATE12;

    interrupt();
    if (stack_pointer + 1 < STACK_SIZE)
      {
        stack[stack_pointer++] = program_counter + 2;
        program_counter = immediate;
      }
    else
      ERROR;
  }

  void skip_eq_immediate()
  {
    OP;
    X;
    IMMEDIATE8;

    if (regs[x] == immediate)
      {
        program_counter+=2;
      }
    STEP;
  }

  void skip_neq_immediate()
  {
    OP;
    X;
    IMMEDIATE8;

    if (regs[x] != immediate)
      {
        program_counter+=2;
      }
    STEP;
  }

  void skip_eq_register()
  {
    OP;
    X;
    Y;

    if (regs[x] == regs[y])
      {
        program_counter+=2;
      }
    STEP;
  }

  void skip_neq_register()
  {
    OP;
    X;
    Y;

    if (regs[x] != regs[y])
      {
        program_counter+=2;
      }
    STEP;
  }

  void random_byte()
  {
    OP;
    X;
    IMMEDIATE8;

    regs[x] = rand() & 0xff & immediate;
    STEP;
  }

  void store_bcd()
  {
    OP;
    X;
    uint8_t tmp = regs[x];
    uint8_t hundreds, tens;

    hundreds = tmp / 100;
    tmp %= 100;
    tens = tmp / 10;
    tmp %= 10;
    memory[addr+0] = hundreds;
    memory[addr+1] = tens;
    memory[addr+2] = tmp;
    STEP;
  }

  void skip_key_x(int up)
  {
    OP;
    X;

    if (((all_keys_down() & (1<<(regs[x]))) != 0) ^ up)
      {
        clear_key(regs[x]);
        program_counter+=2;
      }
    STEP;
  }

  void skip_key_x_up()
  {
    skip_key_x(1);
  }

  void skip_key_x_down()
  {
    skip_key_x(0);
  }

  void load_on_key()
  {
    OP;
    X;
    uint32_t all_keys = 0;

    do
      {
        usleep(10);
        all_keys = read_keys_io();
      } while((all_keys) == 0);

    for (int i = 0; i < 16; ++i)
      {
        clear_key(i);
        if (all_keys & (1<<i))
          {
            regs[x] = i;
          }
      }

    if (all_keys & (1<<31))
      {
        ERROR;
      }
    else
      {
        STEP;
      }
  }

  void draw()
  {
    OP;
    X;
    Y;
    IMMEDIATE4;
    int current_tick;

    interrupt();
    FLAGS = draw_io(regs[x], regs[y], immediate, &(memory[addr]));
    while((current_tick = tick()) == last_tick)
      {
        usleep(NANOS_PER_TICK>>10);
      }
    last_tick = current_tick;
    refresh_io();
    STEP;
  }

  void save_registers()
  {
    OP;
    X;

    for (int i = 0; i <= x; ++i)
      {
        memory[addr+i] = regs[i];
      }
    STEP;
  }

  void restore_registers()
  {
    OP;
    X;

    for (int i = 0; i <= x; ++i)
      {
        regs[i] = memory[addr+i];
      }
    STEP;
  }

  void load_sprite_addr()
  {
    OP;
    X;

    addr = regs[x] * 5;
    STEP;
  }

  void load_addr_immediate()
  {
    OP; IMMEDIATE12;

    addr = immediate;
    STEP;
  }
}

// ------------------------------------------------------------------------

code codegen(std::unique_ptr<llvm::orc::LLJIT> & JIT)
{
  char fn_name[16];
  sprintf(fn_name, "ADDR%0X", program_counter);
  auto context = std::make_unique<llvm::LLVMContext>();
  auto module = std::make_unique<llvm::Module>("CHIP-8", *context);
  auto fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {}, false);
  auto builder = std::make_unique<llvm::IRBuilder<>>(*context);
  auto basic_block = llvm::BasicBlock::Create(*context);
  auto linkage = llvm::Function::ExternalLinkage;
  auto function = llvm::Function::Create(fn_type, linkage, fn_name, *module);

  function->getBasicBlockList().push_back(basic_block);
  builder->SetInsertPoint(basic_block);

  for(int pc_offset = 0; ; pc_offset+=2)
  {
    op = ntohs(((uint16_t *)memory)[(pc_offset+program_counter)>>1]);

    if (op == 0x00e0)
      { // clear
        JIT_CALL("clearscreen_io");
        JIT_STEP;
      }
    else if (op == 0x00ee)
      {
        JIT_CALL("retern");
        JIT_DONE;
      }

    switch ((op & 0xf000) >> 12)
      {
      case 0x0:
      case 0x1:
        { // jump
          IMMEDIATE12; JIT_IMM16;
          JIT_CALL("interrupt");
          JIT_GETPTR16(program_counter);
          builder->CreateStore(JIT_VALUE(immediate), JIT_PTR(program_counter));
          JIT_DONE;
        }
      case 0x2:
        {
          JIT_CALL("call");
          JIT_DONE;
        }
      case 0x3:
        {
          JIT_CALL("skip_eq_immediate");
          JIT_DONE;
        }
      case 0x4:
        {
          JIT_CALL("skip_neq_immediate");
          JIT_DONE;
        }
      case 0x5:
        {
          JIT_CALL("skip_eq_register");
          JIT_DONE;
        }
      case 0x6:
        { // load_immediate
          X; JIT_GETPTRREG(x);
          IMMEDIATE8; JIT_IMM8;
          builder->CreateStore(JIT_VALUE(immediate), JIT_PTR(x));
          JIT_STEP;
        }
      case 0x7:
        { // add_immediate
          X; JIT_GETPTRREG(x); JIT_LOADREG(x);
          IMMEDIATE8; JIT_IMM8;
          auto sum_value = builder->CreateAdd(JIT_VALUE(x), JIT_VALUE(immediate));
          builder->CreateStore(sum_value, JIT_PTR(x));
          JIT_STEP;
        }
      case 0x8:
        {
          switch (op & 0x000f)
            {
            case 0x0:
              { // move
                X; JIT_GETPTRREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                builder->CreateStore(JIT_VALUE(y), JIT_PTR(x));
                JIT_STEP;
              }
            case 0x1:
              { // or_op
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                auto or_value = builder->CreateOr(JIT_VALUE(x), JIT_VALUE(y));
                builder->CreateStore(or_value, JIT_PTR(x));
                JIT_STEP;
              }
            case 0x2:
              { // and_op
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                auto and_value = builder->CreateAnd(JIT_VALUE(x), JIT_VALUE(y));
                builder->CreateStore(and_value, JIT_PTR(x));
                JIT_STEP;
              }
            case 0x3:
              { // xor_op
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                auto xor_value = builder->CreateXor(JIT_VALUE(x), JIT_VALUE(y));
                builder->CreateStore(xor_value, JIT_PTR(x));
                JIT_STEP;
              }
            case 0x4:
              { // add_register
                auto int16ty = llvm::Type::getInt16Ty(*context);
                auto int8ty = llvm::Type::getInt8Ty(*context);
                int f = 0xf; JIT_GETPTRREG(f); // flags
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                auto x16_value = builder->CreateCast(llvm::CastInst::getCastOpcode(JIT_VALUE(x), true, int16ty, true), JIT_VALUE(x), int16ty);
                auto y16_value = builder->CreateCast(llvm::CastInst::getCastOpcode(JIT_VALUE(y), true, int16ty, true), JIT_VALUE(y), int16ty);
                auto eff_eff = builder->getInt16(0xff);
                auto sum16_value = builder->CreateAdd(x16_value, y16_value);
                auto cmp_value = builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, sum16_value, eff_eff);
                auto cmp8_value = builder->CreateCast(llvm::CastInst::getCastOpcode(cmp_value, false, int8ty, false), cmp_value, int8ty);
                builder->CreateStore(cmp8_value, JIT_PTR(f));
                auto sum8_value = builder->CreateCast(llvm::CastInst::getCastOpcode(sum16_value, false, int8ty, false), sum16_value, int8ty);
                builder->CreateStore(sum8_value, x_ptr);
                JIT_STEP;
              }
            case 0x5:
              { // sub_register;
                auto int8ty = llvm::Type::getInt8Ty(*context);
                int f = 0xf; JIT_GETPTRREG(f);
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                auto diff_value = builder->CreateSub(JIT_VALUE(x), JIT_VALUE(y));
                builder->CreateStore(diff_value, JIT_PTR(x));
                auto cmp_value = builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_UGT, JIT_VALUE(x), JIT_VALUE(y));
                auto cmp8_value = builder->CreateCast(llvm::CastInst::getCastOpcode(cmp_value, false, int8ty, false), cmp_value, int8ty);
                builder->CreateStore(cmp8_value, JIT_PTR(f));
                JIT_STEP;
              }
            case 0x6:
              { // shift_right
                auto int8ty = llvm::Type::getInt8Ty(*context);
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                int f = 0xf; JIT_GETPTRREG(f);
                auto andone_value = builder->CreateAnd(JIT_VALUE(x), 0x01);
                builder->CreateStore(andone_value, JIT_PTR(f));
                auto shr_value = builder->CreateLShr(JIT_VALUE(x), 1);
                auto shr8_value = builder->CreateCast(llvm::CastInst::getCastOpcode(shr_value, false, int8ty, false), shr_value, int8ty);
                builder->CreateStore(shr8_value, JIT_PTR(x));
                JIT_STEP;
              }
            case 0x7:
              { // subn_register
                auto int8ty = llvm::Type::getInt8Ty(*context);
                int f = 0xf; JIT_GETPTRREG(f);
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                Y; JIT_GETPTRREG(y); JIT_LOADREG(y);
                auto cmp_value = builder->CreateCmp(llvm::CmpInst::Predicate::ICMP_UGT, JIT_VALUE(y), JIT_VALUE(x));
                auto cmp8_value = builder->CreateCast(llvm::CastInst::getCastOpcode(cmp_value, false, int8ty, false), cmp_value, int8ty);
                builder->CreateStore(cmp8_value, JIT_PTR(f));
                auto diff_value = builder->CreateSub(JIT_VALUE(y), JIT_VALUE(x));
                builder->CreateStore(diff_value, JIT_PTR(x));
                JIT_STEP;
              }
            case 0xe:
              { // shift_left
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                int f = 0xf; JIT_GETPTRREG(f);
                auto andeight_value = builder->CreateAnd(JIT_VALUE(x), 0x80);
                builder->CreateStore(andeight_value, JIT_PTR(f));
                auto shl_value = builder->CreateShl(JIT_VALUE(x), 1);
                builder->CreateStore(shl_value, JIT_PTR(x));
                JIT_STEP;
              }
            default:
              return errer;
            }
        }
      case 0x9:
        {
          JIT_CALL("skip_neq_register");
          JIT_DONE;
        }
      case 0xa: // XXX
        { //load_addr_immediate
          IMMEDIATE12; JIT_IMM16;
          JIT_GETPTR16(addr);
          builder->CreateStore(JIT_VALUE(immediate), JIT_PTR(addr));
          JIT_STEP;
        }
      case 0xb:
        { // branch
          int z = 0; JIT_GETPTRREG(z); JIT_LOADREG(z);
          IMMEDIATE12; JIT_IMM16;
          JIT_GETPTR16(program_counter);
          auto pc_value = builder->CreateAdd(JIT_VALUE(immediate), JIT_VALUE(z));
          builder->CreateStore(pc_value, JIT_PTR(program_counter));
          JIT_DONE;
        }
      case 0xc:
        {
          JIT_CALL("random_byte");
          continue;
        }
      case 0xd:
        {
          JIT_CALL("draw");
          continue;
        }
      case 0xe:
        {
          switch (op & 0x00ff)
            {
            case 0x9e:
              {
                JIT_CALL("skip_key_x_down");
                JIT_DONE;
              }
            case 0xa1:
              {
                JIT_CALL("skip_key_x_up");
                JIT_DONE;
              }
            default:
              return errer;
            }
        }
      case 0xf:
        {
          switch (op & 0x00ff)
            {
            case 0x07:
              { // get_delay_timer
                X; JIT_GETPTRREG(x);
                auto JIT_LOC(delay_timer) = llvm::ConstantInt::get(*context, llvm::APInt(sizeof(uint8_t *)*8, reinterpret_cast<uint64_t>(&delay_timer), false));
                auto JIT_PTR(delay_timer) = builder->CreateIntToPtr(JIT_LOC(delay_timer), llvm::Type::getInt8PtrTy(*context));
                auto JIT_VALUE(delay_timer) = builder->CreateLoad(llvm::Type::getInt8Ty(*context), JIT_PTR(delay_timer));
                builder->CreateStore(JIT_VALUE(delay_timer), JIT_PTR(x));
                JIT_STEP;
              }
            case 0x0a:
              {
                JIT_CALL("load_on_keys");
                continue;
              }
            case 0x15:
              { // set_delay_timer
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                auto JIT_LOC(delay_timer) = llvm::ConstantInt::get(*context, llvm::APInt(sizeof(uint8_t *)*8, reinterpret_cast<uint64_t>(&delay_timer), false));
                auto JIT_PTR(delay_timer) = builder->CreateIntToPtr(JIT_LOC(delay_timer), llvm::Type::getInt8PtrTy(*context));
                builder->CreateStore(JIT_VALUE(x), JIT_PTR(delay_timer));
                JIT_STEP;
              }
            case 0x18:
              { // set_sound_timer
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                auto JIT_LOC(sound_timer) = llvm::ConstantInt::get(*context, llvm::APInt(sizeof(uint8_t *)*8, reinterpret_cast<uint64_t>(&sound_timer), false));
                auto JIT_PTR(sound_timer) = builder->CreateIntToPtr(JIT_LOC(sound_timer), llvm::Type::getInt8PtrTy(*context));
                builder->CreateStore(JIT_VALUE(x), JIT_PTR(sound_timer));
                JIT_STEP;
              }
            case 0x1e:
              { // add_addr
                auto int16ty = llvm::Type::getInt16Ty(*context);
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                JIT_GETPTR16(addr); JIT_LOAD16(addr);
                auto x16_value = builder->CreateCast(llvm::CastInst::getCastOpcode(JIT_VALUE(x), true, int16ty, true), JIT_VALUE(x), int16ty);
                auto sum_value = builder->CreateAdd(JIT_VALUE(addr), x16_value);
                builder->CreateStore(sum_value, JIT_PTR(addr));
                JIT_STEP;
              }
            case 0x29:
              { // load_sprite_addr
                auto int16ty = llvm::Type::getInt16Ty(*context);
                X; JIT_GETPTRREG(x); JIT_LOADREG(x);
                JIT_GETPTR16(addr);
                auto five_value = builder->getInt16(5);
                auto x16_value = builder->CreateCast(llvm::CastInst::getCastOpcode(JIT_VALUE(x), true, int16ty, true), JIT_VALUE(x), int16ty);
                auto prod_value = builder->CreateMul(x16_value, five_value);
                builder->CreateStore(prod_value, JIT_PTR(addr));
                JIT_STEP;
              }
            case 0x33:
              {
                JIT_CALL("store_bcd");
                continue;
              }
            case 0x55:
              {
                JIT_CALL("save_registers");
                continue;
              }
            case 0x65:
              {
                JIT_CALL("restore_registers");
                continue;
              }
            }
        }
      default:
        return errer;
      }
  }

 end_of_block:

  // Generate code
  JIT_RETURN;
  auto safe_module = llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
  ExitOnErr(JIT->addIRModule(std::move(safe_module)));

  // Return generated code
  auto sym = ExitOnErr(JIT->lookup(fn_name));
  return reinterpret_cast<code>(sym.getAddress());
}

// ------------------------------------------------------------------------

int main(int argc, const char * argv[])
{
  FILE * fp;

  if (argc <= 1)
    {
      fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
      exit(-1);
    }

  // Load program
  fp = fopen(argv[1], "r");
  fread(memory + ENTRYPOINT, sizeof(uint8_t), MEMORY_SIZE - ENTRYPOINT, fp);
  fclose(fp);

  // Initialize CHIP-8 state
  last_tick = tick();
  init_chip8();
  init_io(64, 32);

  // Initialize LLVM
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  auto TheJIT = ExitOnErr(llvm::orc::LLJITBuilder().create());
  auto DL = TheJIT->getDataLayout();
  TheJIT->getMainJITDylib().addGenerator(cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));

  // Nuggets
  trace_cache = std::make_unique<std::map<uint16_t, code>>();

  // Transfer
  program_counter = ENTRYPOINT;
  while (1)
    {
      // Look for JITed code starting at current PC
      auto it = trace_cache->find(program_counter);

      // If no code found, generate some
      if (it == trace_cache->end())
        {
          trace_cache->operator[](program_counter) = codegen(TheJIT);
          continue;
        }
      // Otherwise run the code that has been found
      else
        {
          it->second();
        }

      // If escape or q(uit) has been pressed, exit
      if (all_keys_down() & (1<<31))
        {
          break;
        }
    }

  // Deinit CHIP-8
  deinit_io();
  deinit_chip8();

  for (int i = 0; i < REGFILE_SIZE; ++i)
    {
      fprintf(stderr, "V%02d = 0x%02X\n", i, regs[i]);
    }
  fprintf(stderr, "$pc = 0x%04X\n", program_counter);
  fprintf(stderr, "$addr = 0x%04X\n", addr);
  fprintf(stderr, "stack[%d] = 0x%04X\n", stack_pointer, stack[stack_pointer]);
  fprintf(stderr, "delay = %d\n", delay_timer);
  fprintf(stderr, "sound = %d\n", sound_timer);

  exit(0);
}
