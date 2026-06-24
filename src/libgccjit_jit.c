#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <arpa/inet.h>

#include <libgccjit.h>

#include "chip8.h"
#include "io.h"

// ------------------------------------------------------------------------
// Opcode decode helpers (compile-time, mirror the LLVM backend)

#define X uint8_t x = (op >> 8) & 0xf
#define Y uint8_t y = (op >> 4) & 0xf
#define OP_AT(pc) (ntohs(((uint16_t *)memory)[(pc)>>1]))
#define IMMEDIATE4 uint8_t immediate = op & 0xf
#define IMMEDIATE8 uint8_t immediate = op & 0xff
#define IMMEDIATE12 uint16_t immediate = op & 0xfff
#define NANOS_PER_TICK (16666666) // ~60 Hz clock
#define TICKS_PER_SECOND (60) // ~60 Hz clock
#define INPUT_TICKS (10) // roughly 1/6 second window for input

// ------------------------------------------------------------------------
// VM bookkeeping state owned by this backend (chip8.c owns the rest)

int last_tick = 0;
int8_t delay_timer = 0;
int8_t sound_timer = 0;
uint16_t op = 0;
uint32_t keys_down[INPUT_TICKS];
int interrupt_count = 0;
int program_over = 0;

typedef void (*code)(void); // pointer to a compiled trace

// ------------------------------------------------------------------------
// Host helper routines. The JIT emits *calls* to these for the opcodes that
// are awkward to express as straight-line IR (control flow, I/O, blocking
// input, register/memory bulk moves). They must remain visible in the
// process' dynamic symbol table so libgccjit can resolve them at compile
// time -- the Makefile links chip8-libgccjit with -rdynamic for that reason.
//
// Each routine reads its own opcode back out of memory at program_counter,
// exactly as the interpreter does; the JIT guarantees program_counter holds
// the address of the calling instruction by emitting an inline "pc += 2"
// after every non-control instruction in the trace.

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
    errer();
}

void call()
{
  op = OP_AT(program_counter);
  IMMEDIATE12;

  interrupt();
  if (stack_pointer < STACK_SIZE)
    {
      stack[stack_pointer++] = program_counter + 2;
      program_counter = immediate;
    }
  else
    errer();
}

void random_byte()
{
  op = OP_AT(program_counter);
  X;
  IMMEDIATE8;

  regs[x] = rand() & 0xff & immediate;
}

void store_bcd()
{
  op = OP_AT(program_counter);
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
}

void skip_key_x(int up)
{
  op = OP_AT(program_counter);
  X;

  if (((all_keys_down() & (1<<(regs[x]))) != 0) ^ up)
    {
      clear_key(regs[x]);
      program_counter += 2;
    }
  program_counter += 2;
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
  op = OP_AT(program_counter);
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
      program_over = 1;
    }
  else
    {
      program_counter += 2;
    }
}

void draw()
{
  op = OP_AT(program_counter);
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
}

void save_registers()
{
  op = OP_AT(program_counter);
  X;

  for (int i = 0; i <= x; ++i)
    {
      memory[addr+i] = regs[i];
    }
}

void restore_registers()
{
  op = OP_AT(program_counter);
  X;

  for (int i = 0; i <= x; ++i)
    {
      regs[i] = memory[addr+i];
    }
}

// ------------------------------------------------------------------------
// libgccjit codegen helpers

// An lvalue denoting the storage at an absolute host address. Because every
// register / address the JIT touches is known at codegen time (x, y and the
// fixed globals), we can embed the concrete pointer as a constant and
// dereference it, rather than importing arrays and doing array-access IR.
static gcc_jit_lvalue *
mem(gcc_jit_context *ctx, gcc_jit_type *ptr_type, void *host_addr)
{
  gcc_jit_rvalue *p = gcc_jit_context_new_rvalue_from_ptr(ctx, ptr_type, host_addr);
  return gcc_jit_rvalue_dereference(p, NULL);
}

// Read an 8-bit register once into a fresh local, so later assignments to the
// same (or aliasing) storage cannot disturb the value -- this reproduces the
// SSA "load once" semantics that the LLVM backend gets for free.
static gcc_jit_rvalue *
snapshot(gcc_jit_context *ctx, gcc_jit_function *fn, gcc_jit_block *blk,
         gcc_jit_type *u8, gcc_jit_type *u8p, void *host_addr, int id)
{
  char name[24];
  snprintf(name, sizeof(name), "snap_%d", id);
  gcc_jit_lvalue *local = gcc_jit_function_new_local(fn, NULL, u8, name);
  gcc_jit_block_add_assignment(blk, NULL, local,
                               gcc_jit_lvalue_as_rvalue(mem(ctx, u8p, host_addr)));
  return gcc_jit_lvalue_as_rvalue(local);
}

// Names of the host helpers the JIT may call. Declared as imported functions
// in every trace context (unused declarations are harmless).
static const char *HOST_FNS[] = {
  "interrupt", "clearscreen_io", "retern", "call", "random_byte", "draw",
  "skip_key_x_down", "skip_key_x_up", "load_on_key", "store_bcd",
  "save_registers", "restore_registers",
};
#define N_HOST_FNS ((int)(sizeof(HOST_FNS)/sizeof(HOST_FNS[0])))

static gcc_jit_function *
host_fn(gcc_jit_function **tbl, const char *name)
{
  for (int i = 0; i < N_HOST_FNS; ++i)
    if (strcmp(HOST_FNS[i], name) == 0)
      return tbl[i];
  return NULL; // unreachable for the names used below
}

// ------------------------------------------------------------------------
// Compile a single trace beginning at program_counter and return a pointer
// to the native code. Straight-line opcodes are emitted inline; the trace is
// extended across unconditional jumps (bounded) and the taken side of skips,
// matching the LLVM backend. Control-flow / IO opcodes terminate the trace.

code codegen(void)
{
  char fn_name[16];
  snprintf(fn_name, sizeof(fn_name), "ADDR%04X", program_counter);

  gcc_jit_context *ctx = gcc_jit_context_acquire();
  gcc_jit_context_set_int_option(ctx, GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, 2);

  gcc_jit_type *t_void = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_VOID);
  gcc_jit_type *t_u8   = gcc_jit_context_get_int_type(ctx, 1, 0);
  gcc_jit_type *t_u16  = gcc_jit_context_get_int_type(ctx, 2, 0);
  gcc_jit_type *t_u8p  = gcc_jit_type_get_pointer(t_u8);
  gcc_jit_type *t_u16p = gcc_jit_type_get_pointer(t_u16);

  // The trace itself: void ADDRxxxx(void).
  gcc_jit_function *function =
    gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_EXPORTED,
                                 t_void, fn_name, 0, NULL, 0);

  // Imported host helpers.
  gcc_jit_function *host[N_HOST_FNS];
  for (int i = 0; i < N_HOST_FNS; ++i)
    host[i] = gcc_jit_context_new_function(ctx, NULL, GCC_JIT_FUNCTION_IMPORTED,
                                           t_void, HOST_FNS[i], 0, NULL, 0);

  gcc_jit_block *blk = gcc_jit_function_new_block(function, NULL);

  // Convenience constants.
  gcc_jit_rvalue *two16  = gcc_jit_context_new_rvalue_from_int(ctx, t_u16, 2);
  gcc_jit_rvalue *four16 = gcc_jit_context_new_rvalue_from_int(ctx, t_u16, 4);

  // The lvalue for the VM program counter is reused constantly.
  #define PC_LVAL  mem(ctx, t_u16p, &program_counter)
  // Emit "program_counter += 2" then continue compiling the next opcode.
  // NB: no do/while wrapper -- the bare `continue` must target the for loop.
  #define STEP_AND_CONTINUE \
    gcc_jit_block_add_assignment_op(blk, NULL, PC_LVAL, \
      GCC_JIT_BINARY_OP_PLUS, two16); \
    continue
  // Emit a "call <name>()" for side effects.
  #define CALL_HOST(nm) \
    gcc_jit_block_add_eval(blk, NULL, \
      gcc_jit_context_new_call(ctx, NULL, host_fn(host, nm), 0, NULL))
  #define BAIL_ERRER  do { gcc_jit_context_release(ctx); return errer; } while (0)

  int local_id = 0;
  int block_id = 0;

  for (uint16_t pc = program_counter, op_count = 0; ; pc += 2, ++op_count)
    {
      op = OP_AT(pc);

      if (op == 0x00e0)
        { // clear screen
          CALL_HOST("clearscreen_io");
          STEP_AND_CONTINUE;
        }
      else if (op == 0x00ee)
        { // return
          CALL_HOST("retern");
          goto end_of_trace;
        }

      switch ((op & 0xf000) >> 12)
        {
        case 0x0:
        case 0x1:
          { // jump
            IMMEDIATE12;
            CALL_HOST("interrupt");
            gcc_jit_block_add_assignment(blk, NULL, PC_LVAL,
              gcc_jit_context_new_rvalue_from_int(ctx, t_u16, immediate));
            if ((pc != immediate) && (op_count < (1<<8)))
              {
                pc = immediate - 2; // continue the trace at the jump target
                continue;
              }
            goto end_of_trace; // self-loop or trace too long: bounce to dispatch
          }
        case 0x2:
          { // call
            CALL_HOST("call");
            goto end_of_trace;
          }
        case 0x3: // skip if Vx == imm
        case 0x4: // skip if Vx != imm
        case 0x5: // skip if Vx == Vy
        case 0x9: // skip if Vx != Vy
          {
            char tname[24], ename[24];
            snprintf(tname, sizeof(tname), "skip_%d", block_id);
            snprintf(ename, sizeof(ename), "noskip_%d", block_id);
            ++block_id;
            gcc_jit_block *then_blk = gcc_jit_function_new_block(function, tname);
            gcc_jit_block *else_blk = gcc_jit_function_new_block(function, ename);

            enum gcc_jit_comparison cmp =
              (((op & 0xf000) >> 12) == 0x3 || ((op & 0xf000) >> 12) == 0x5)
                ? GCC_JIT_COMPARISON_EQ : GCC_JIT_COMPARISON_NE;

            gcc_jit_rvalue *cond;
            {
              X;
              gcc_jit_rvalue *vx = gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[x]));
              gcc_jit_rvalue *rhs;
              if (((op & 0xf000) >> 12) == 0x3 || ((op & 0xf000) >> 12) == 0x4)
                {
                  IMMEDIATE8;
                  rhs = gcc_jit_context_new_rvalue_from_int(ctx, t_u8, immediate);
                }
              else
                {
                  Y;
                  rhs = gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[y]));
                }
              cond = gcc_jit_context_new_comparison(ctx, NULL, cmp, vx, rhs);
            }
            gcc_jit_block_end_with_conditional(blk, NULL, cond, then_blk, else_blk);

            // "don't skip": advance one instruction and bounce to dispatch.
            gcc_jit_block_add_assignment_op(else_blk, NULL,
              mem(ctx, t_u16p, &program_counter), GCC_JIT_BINARY_OP_PLUS, two16);
            gcc_jit_block_end_with_void_return(else_blk, NULL);

            // "skip": advance two instructions and keep compiling inline.
            blk = then_blk;
            gcc_jit_block_add_assignment_op(blk, NULL,
              mem(ctx, t_u16p, &program_counter), GCC_JIT_BINARY_OP_PLUS, four16);
            pc += 2; // skip the next opcode (loop adds the other +2)
            continue;
          }
        case 0x6:
          { // Vx = imm
            X; IMMEDIATE8;
            gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[x]),
              gcc_jit_context_new_rvalue_from_int(ctx, t_u8, immediate));
            STEP_AND_CONTINUE;
          }
        case 0x7:
          { // Vx += imm
            X; IMMEDIATE8;
            gcc_jit_block_add_assignment_op(blk, NULL, mem(ctx, t_u8p, &regs[x]),
              GCC_JIT_BINARY_OP_PLUS,
              gcc_jit_context_new_rvalue_from_int(ctx, t_u8, immediate));
            STEP_AND_CONTINUE;
          }
        case 0x8:
          {
            X; Y;
            gcc_jit_lvalue *rx = mem(ctx, t_u8p, &regs[x]);
            switch (op & 0x000f)
              {
              case 0x0: // Vx = Vy
                gcc_jit_block_add_assignment(blk, NULL, rx,
                  gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[y])));
                STEP_AND_CONTINUE;
              case 0x1: // Vx |= Vy
                gcc_jit_block_add_assignment_op(blk, NULL, rx, GCC_JIT_BINARY_OP_BITWISE_OR,
                  gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[y])));
                STEP_AND_CONTINUE;
              case 0x2: // Vx &= Vy
                gcc_jit_block_add_assignment_op(blk, NULL, rx, GCC_JIT_BINARY_OP_BITWISE_AND,
                  gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[y])));
                STEP_AND_CONTINUE;
              case 0x3: // Vx ^= Vy
                gcc_jit_block_add_assignment_op(blk, NULL, rx, GCC_JIT_BINARY_OP_BITWISE_XOR,
                  gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[y])));
                STEP_AND_CONTINUE;
              case 0x4:
                { // Vx += Vy, VF = carry
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *vy = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[y], local_id++);
                  gcc_jit_rvalue *sx = gcc_jit_context_new_cast(ctx, NULL, vx, t_u16);
                  gcc_jit_rvalue *sy = gcc_jit_context_new_cast(ctx, NULL, vy, t_u16);
                  gcc_jit_rvalue *sum16 = gcc_jit_context_new_binary_op(ctx, NULL,
                    GCC_JIT_BINARY_OP_PLUS, t_u16, sx, sy);
                  gcc_jit_rvalue *ff = gcc_jit_context_new_rvalue_from_int(ctx, t_u16, 0xff);
                  gcc_jit_rvalue *carry = gcc_jit_context_new_comparison(ctx, NULL,
                    GCC_JIT_COMPARISON_GT, sum16, ff);
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_cast(ctx, NULL, carry, t_u8));
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_cast(ctx, NULL, sum16, t_u8));
                  STEP_AND_CONTINUE;
                }
              case 0x5:
                { // Vx -= Vy, VF = NOT borrow
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *vy = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[y], local_id++);
                  gcc_jit_rvalue *gt = gcc_jit_context_new_comparison(ctx, NULL,
                    GCC_JIT_COMPARISON_GT, vx, vy);
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_cast(ctx, NULL, gt, t_u8));
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MINUS, t_u8, vx, vy));
                  STEP_AND_CONTINUE;
                }
              case 0x6:
                { // Vx >>= 1, VF = lost low bit
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *one = gcc_jit_context_new_rvalue_from_int(ctx, t_u8, 1);
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_BITWISE_AND, t_u8, vx, one));
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_RSHIFT, t_u8, vx, one));
                  STEP_AND_CONTINUE;
                }
              case 0x7:
                { // Vx = Vy - Vx, VF = NOT borrow
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *vy = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[y], local_id++);
                  gcc_jit_rvalue *gt = gcc_jit_context_new_comparison(ctx, NULL,
                    GCC_JIT_COMPARISON_GT, vy, vx);
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_cast(ctx, NULL, gt, t_u8));
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MINUS, t_u8, vy, vx));
                  STEP_AND_CONTINUE;
                }
              case 0xe:
                { // Vx <<= 1, VF = lost high bit
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *m80 = gcc_jit_context_new_rvalue_from_int(ctx, t_u8, 0x80);
                  gcc_jit_rvalue *seven = gcc_jit_context_new_rvalue_from_int(ctx, t_u8, 7);
                  gcc_jit_rvalue *one = gcc_jit_context_new_rvalue_from_int(ctx, t_u8, 1);
                  gcc_jit_rvalue *hi = gcc_jit_context_new_binary_op(ctx, NULL,
                    GCC_JIT_BINARY_OP_BITWISE_AND, t_u8, vx, m80);
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_RSHIFT, t_u8, hi, seven));
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_LSHIFT, t_u8, vx, one));
                  STEP_AND_CONTINUE;
                }
              default:
                BAIL_ERRER;
              }
          }
        case 0xa:
          { // addr = imm
            IMMEDIATE12;
            gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u16p, &addr),
              gcc_jit_context_new_rvalue_from_int(ctx, t_u16, immediate));
            STEP_AND_CONTINUE;
          }
        case 0xb:
          { // jump to imm + V0
            IMMEDIATE12;
            gcc_jit_rvalue *v0 = gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[0]));
            gcc_jit_rvalue *v0_16 = gcc_jit_context_new_cast(ctx, NULL, v0, t_u16);
            gcc_jit_rvalue *target = gcc_jit_context_new_binary_op(ctx, NULL,
              GCC_JIT_BINARY_OP_PLUS, t_u16,
              gcc_jit_context_new_rvalue_from_int(ctx, t_u16, immediate), v0_16);
            gcc_jit_block_add_assignment(blk, NULL, PC_LVAL, target);
            goto end_of_trace;
          }
        case 0xc:
          { // Vx = rand() & imm
            CALL_HOST("random_byte");
            STEP_AND_CONTINUE;
          }
        case 0xd:
          { // draw
            CALL_HOST("draw");
            STEP_AND_CONTINUE;
          }
        case 0xe:
          {
            switch (op & 0x00ff)
              {
              case 0x9e:
                CALL_HOST("skip_key_x_down");
                goto end_of_trace;
              case 0xa1:
                CALL_HOST("skip_key_x_up");
                goto end_of_trace;
              default:
                BAIL_ERRER;
              }
          }
        case 0xf:
          {
            switch (op & 0x00ff)
              {
              case 0x07:
                { // Vx = delay_timer
                  X;
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[x]),
                    gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &delay_timer)));
                  STEP_AND_CONTINUE;
                }
              case 0x0a:
                { // Vx = blocking key read
                  CALL_HOST("load_on_key");
                  goto end_of_trace; // end trace to observe program_over / new pc
                }
              case 0x15:
                { // delay_timer = Vx
                  X;
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &delay_timer),
                    gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[x])));
                  STEP_AND_CONTINUE;
                }
              case 0x18:
                { // sound_timer = Vx
                  X;
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &sound_timer),
                    gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[x])));
                  STEP_AND_CONTINUE;
                }
              case 0x1e:
                { // addr += Vx
                  X;
                  gcc_jit_rvalue *vx = gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[x]));
                  gcc_jit_block_add_assignment_op(blk, NULL, mem(ctx, t_u16p, &addr),
                    GCC_JIT_BINARY_OP_PLUS,
                    gcc_jit_context_new_cast(ctx, NULL, vx, t_u16));
                  STEP_AND_CONTINUE;
                }
              case 0x29:
                { // addr = Vx * 5 (font sprite)
                  X;
                  gcc_jit_rvalue *vx = gcc_jit_lvalue_as_rvalue(mem(ctx, t_u8p, &regs[x]));
                  gcc_jit_rvalue *vx16 = gcc_jit_context_new_cast(ctx, NULL, vx, t_u16);
                  gcc_jit_rvalue *five = gcc_jit_context_new_rvalue_from_int(ctx, t_u16, 5);
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u16p, &addr),
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MULT, t_u16, vx16, five));
                  STEP_AND_CONTINUE;
                }
              case 0x33:
                CALL_HOST("store_bcd");
                STEP_AND_CONTINUE;
              case 0x55:
                CALL_HOST("save_registers");
                STEP_AND_CONTINUE;
              case 0x65:
                CALL_HOST("restore_registers");
                STEP_AND_CONTINUE;
              default:
                BAIL_ERRER;
              }
          }
        default:
          BAIL_ERRER;
        }
    }

 end_of_trace:
  gcc_jit_block_end_with_void_return(blk, NULL);

  { const char *e = gcc_jit_context_get_first_error(ctx);
    if (e) fprintf(stderr, "JITERR @ %s: %s\n", fn_name, e); }
  gcc_jit_result *result = gcc_jit_context_compile(ctx);
  gcc_jit_context_release(ctx); // the result owns the code independently
  if (!result)
    return errer;
  code fn = (code)gcc_jit_result_get_code(result, fn_name);
  return fn ? fn : errer;

  #undef PC_LVAL
  #undef STEP_AND_CONTINUE
  #undef CALL_HOST
  #undef BAIL_ERRER
}

// ------------------------------------------------------------------------

code trace_cache[1<<16]; // indexed by program_counter; zero-initialized

int main(int argc, const char * argv[])
{
  FILE * fp;
  int trace_count = 0;

  if (argc <= 1)
    {
      fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
      exit(-1);
    }

  // Load program
  fp = fopen(argv[1], "r");
  if (fread(memory + ENTRYPOINT, sizeof(uint8_t), MEMORY_SIZE - ENTRYPOINT, fp) == 0)
    {
      fprintf(stderr, "Could not read ROM\n");
      exit(-1);
    }
  fclose(fp);

  // Initialize CHIP-8 state
  last_tick = tick();
  init_chip8();
  init_io(64, 32);

  // Run
  program_counter = ENTRYPOINT;
  while (1)
    {
      code c = trace_cache[program_counter];
      if (c == NULL)
        {
          trace_cache[program_counter] = codegen();
          continue;
        }
      c();

      if ((all_keys_down() & (1<<31)) || program_over)
        {
          break;
        }
      trace_count++;
    }

  deinit_io();
  deinit_chip8();

  for (int i = 0; i < REGFILE_SIZE; ++i)
    {
      fprintf(stderr, "V%02d = 0x%02X\n", i, regs[i]);
    }
  fprintf(stderr, "%d traces executed\n", trace_count);
  fprintf(stderr, "$pc = 0x%04X\n", program_counter);
  fprintf(stderr, "$addr = 0x%04X\n", addr);
  fprintf(stderr, "delay = %d\n", delay_timer);
  fprintf(stderr, "sound = %d\n", sound_timer);

  exit(0);
}
