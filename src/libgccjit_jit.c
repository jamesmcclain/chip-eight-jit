#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <libgccjit.h>

#include "chip8.h"
#include "io.h"

// ------------------------------------------------------------------------
// Opcode decode helpers (compile-time, mirror the LLVM backend)

#define X uint8_t x = (op >> 8) & 0xf
#define Y uint8_t y = (op >> 4) & 0xf
#define OP_AT(pc) (OPCODE_AT(pc))
#define IMMEDIATE4 uint8_t immediate = op & 0xf
#define IMMEDIATE8 uint8_t immediate = op & 0xff
#define IMMEDIATE12 uint16_t immediate = op & 0xfff
#define NANOS_PER_TICK (16666666) // ~60 Hz clock
#define TICKS_PER_SECOND (60) // ~60 Hz clock
#define INPUT_TICKS (10) // roughly 1/6 second window for input
#define SAFEPOINT_INTERVAL (32) // straight-line ops between emitted safepoints

// ------------------------------------------------------------------------
// VM bookkeeping state owned by this backend (chip8.c owns the rest)

int last_tick = 0;
uint8_t delay_timer = 0;
uint8_t sound_timer = 0;
uint16_t op = 0;
uint32_t keys_down[INPUT_TICKS];
int interrupt_count = 0;
int program_over = 0;
volatile sig_atomic_t smc_pending = 0;

// Asynchronous interrupt source. A POSIX interval timer raises SIGALRM
// INPUT_TICKS times per 60 Hz tick; the handler only sets this flag (ncurses
// is not async-signal-safe, so the actual polling happens synchronously in
// check_interrupt() at safepoints emitted into the JITed traces).
volatile sig_atomic_t interrupt_pending = 0;
static timer_t interrupt_timer;

static void alarm_handler(int signum)
{
  (void)signum;
  interrupt_pending = 1;
}

static void init_interrupt_timer(void)
{
  struct sigaction sa;
  struct sigevent sev;
  struct itimerspec its;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = alarm_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &sa, NULL);

  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = SIGALRM;
  timer_create(CLOCK_MONOTONIC, &sev, &interrupt_timer);

  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = NANOS_PER_TICK / INPUT_TICKS;
  its.it_value = its.it_interval;
  timer_settime(interrupt_timer, 0, &its, NULL);
}

static void deinit_interrupt_timer(void)
{
  timer_delete(interrupt_timer);
}

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
      keys_down[i] &= ~(1u << key);
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

// Slow path of a safepoint: service timers and input iff the asynchronous
// timer has fired since the last check. Cheap enough to call from the
// dispatch loop and from any host helper. Must stay visible in the dynamic
// symbol table (see HOST_FNS) so JITed traces can call it.
void check_interrupt()
{
  if (interrupt_pending)
    {
      interrupt_pending = 0;
      interrupt();
    }
}

void errer()
{
  fprintf(stderr, "Error: op=%04x pc=%04x\n", op, program_counter);
  exit(-1);
}

void retern()
{
  check_interrupt();
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

  check_interrupt();
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
  smc_pending = 1;
  op = OP_AT(program_counter);
  X;
  uint8_t tmp = regs[x];
  uint8_t hundreds, tens;

  hundreds = tmp / 100;
  tmp %= 100;
  tens = tmp / 10;
  tmp %= 10;
  MEM_AT(addr+0) = hundreds;
  MEM_AT(addr+1) = tens;
  MEM_AT(addr+2) = tmp;
  program_counter += 2;
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
      errer();
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

  check_interrupt();
  uint8_t sprite[16];
  for (int i = 0; i < immediate; ++i)
    {
      sprite[i] = MEM_AT(addr+i);
    }
  FLAGS = draw_io(regs[x], regs[y], immediate, sprite);
  while((current_tick = tick()) == last_tick)
    {
      usleep(NANOS_PER_TICK>>10);
    }
  last_tick = current_tick;
  refresh_io();
}

void save_registers()
{
  smc_pending = 1;
  op = OP_AT(program_counter);
  X;

  for (int i = 0; i <= x; ++i)
    {
      MEM_AT(addr+i) = regs[i];
    }
  program_counter += 2;
}

void restore_registers()
{
  op = OP_AT(program_counter);
  X;

  for (int i = 0; i <= x; ++i)
    {
      regs[i] = MEM_AT(addr+i);
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
  "interrupt", "check_interrupt", "clearscreen_io", "retern", "call", "random_byte", "draw",
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

static gcc_jit_result **trace_results = NULL;
static size_t n_trace_results = 0;
static size_t cap_trace_results = 0;

static void remember_result(gcc_jit_result *result)
{
  if (n_trace_results == cap_trace_results)
    {
      size_t cap = cap_trace_results ? cap_trace_results * 2 : 64;
      gcc_jit_result **grown = realloc(trace_results, cap * sizeof(*grown));
      if (grown == NULL)
        {
          // Out of memory growing the bookkeeping list: we cannot track this
          // result for later release, but dropping the program is worse than
          // leaking one result, so keep running.
          return;
        }
      trace_results = grown;
      cap_trace_results = cap;
    }
  trace_results[n_trace_results++] = result;
}

static void teardown_jit(void)
{
  for (size_t i = 0; i < n_trace_results; ++i)
    {
      gcc_jit_result_release(trace_results[i]);
    }
  free(trace_results);
  trace_results = NULL;
  n_trace_results = 0;
  cap_trace_results = 0;
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
  gcc_jit_type *t_int  = gcc_jit_context_get_type(ctx, GCC_JIT_TYPE_INT);
  gcc_jit_type *t_vintp = gcc_jit_type_get_pointer(gcc_jit_type_get_volatile(t_int));

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
  #define BAIL_ERRER  do { fprintf(stderr, "JIT codegen failed at pc=%04x\n", program_counter); gcc_jit_context_release(ctx); return NULL; } while (0)
  // Emit a lightweight safepoint: a volatile load of interrupt_pending and a
  // conditional call to check_interrupt(). The fast (flag clear) path is a
  // single load-and-branch, so these can be sprinkled liberally through
  // traces without hurting throughput; the slow path services timers/input.
  #define SAFEPOINT() \
    do { \
      char sname[24], cname[24]; \
      snprintf(sname, sizeof(sname), "sp_slow_%d", block_id); \
      snprintf(cname, sizeof(cname), "sp_cont_%d", block_id); \
      ++block_id; \
      gcc_jit_block *slow_blk = gcc_jit_function_new_block(function, sname); \
      gcc_jit_block *cont_blk = gcc_jit_function_new_block(function, cname); \
      gcc_jit_rvalue *flag = gcc_jit_lvalue_as_rvalue( \
        mem(ctx, t_vintp, (void *)&interrupt_pending)); \
      gcc_jit_rvalue *pending = gcc_jit_context_new_comparison(ctx, NULL, \
        GCC_JIT_COMPARISON_NE, flag, \
        gcc_jit_context_new_rvalue_from_int(ctx, t_int, 0)); \
      gcc_jit_block_end_with_conditional(blk, NULL, pending, slow_blk, cont_blk); \
      gcc_jit_block_add_eval(slow_blk, NULL, \
        gcc_jit_context_new_call(ctx, NULL, host_fn(host, "check_interrupt"), 0, NULL)); \
      gcc_jit_block_end_with_jump(slow_blk, NULL, cont_blk); \
      blk = cont_blk; \
    } while (0)

  int local_id = 0;
  int block_id = 0;

  for (uint16_t pc = program_counter, op_count = 0; ; pc += 2, ++op_count)
    {
      op = OP_AT(pc);

      // Periodic safepoint: bound the number of straight-line (or taken-skip)
      // instructions that can execute between input/timer checks.
      if ((op_count != 0) && ((op_count % SAFEPOINT_INTERVAL) == 0))
        {
          SAFEPOINT();
        }

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
            SAFEPOINT();
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
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_cast(ctx, NULL, sum16, t_u8));
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_cast(ctx, NULL, carry, t_u8));
                  STEP_AND_CONTINUE;
                }
              case 0x5:
                { // Vx -= Vy, VF = NOT borrow
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *vy = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[y], local_id++);
                  gcc_jit_rvalue *gt = gcc_jit_context_new_comparison(ctx, NULL,
                    GCC_JIT_COMPARISON_GE, vx, vy);
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MINUS, t_u8, vx, vy));
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_cast(ctx, NULL, gt, t_u8));
                  STEP_AND_CONTINUE;
                }
              case 0x6:
                { // Vx >>= 1, VF = lost low bit
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *one = gcc_jit_context_new_rvalue_from_int(ctx, t_u8, 1);
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_RSHIFT, t_u8, vx, one));
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_BITWISE_AND, t_u8, vx, one));
                  STEP_AND_CONTINUE;
                }
              case 0x7:
                { // Vx = Vy - Vx, VF = NOT borrow
                  gcc_jit_rvalue *vx = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[x], local_id++);
                  gcc_jit_rvalue *vy = snapshot(ctx, function, blk, t_u8, t_u8p, &regs[y], local_id++);
                  gcc_jit_rvalue *gt = gcc_jit_context_new_comparison(ctx, NULL,
                    GCC_JIT_COMPARISON_GE, vy, vx);
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_MINUS, t_u8, vy, vx));
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_cast(ctx, NULL, gt, t_u8));
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
                  gcc_jit_block_add_assignment(blk, NULL, rx,
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_LSHIFT, t_u8, vx, one));
                  gcc_jit_block_add_assignment(blk, NULL, mem(ctx, t_u8p, &regs[0xf]),
                    gcc_jit_context_new_binary_op(ctx, NULL, GCC_JIT_BINARY_OP_RSHIFT, t_u8, hi, seven));
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
                goto end_of_trace;
              case 0x55:
                CALL_HOST("save_registers");
                goto end_of_trace;
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
    {
      fprintf(stderr, "JIT codegen failed at pc=%04x\n", program_counter);
      return NULL;
    }
  code fn = (code)gcc_jit_result_get_code(result, fn_name);
  if (!fn)
    {
      gcc_jit_result_release(result);
      fprintf(stderr, "JIT codegen failed at pc=%04x\n", program_counter);
      return NULL;
    }
  remember_result(result); // hold the result so teardown can free its code
  return fn;

  #undef SAFEPOINT
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
  fp = fopen(argv[1], "rb");
  if (fp == NULL)
    {
      fprintf(stderr, "Could not open ROM %s\n", argv[1]);
      exit(-1);
    }
  if (fread(memory + ENTRYPOINT, sizeof(uint8_t), MEMORY_SIZE - ENTRYPOINT, fp) == 0)
    {
      fprintf(stderr, "Could not read ROM\n");
      fclose(fp);
      exit(-1);
    }
  fclose(fp);

  // Initialize CHIP-8 state
  last_tick = tick();
  init_chip8();
  init_io(64, 32);
  init_interrupt_timer();

  // Run
  program_counter = ENTRYPOINT;
  while (1)
    {
      code c = trace_cache[program_counter];
      if (c == NULL)
        {
          code compiled = codegen();
          if (compiled == NULL)
            {
              break;
            }
          trace_cache[program_counter] = compiled;
          continue;
        }
      c();

      // Service any pending timer/input interrupt between traces; this also
      // keeps keys fresh for ROMs that spin on skip_key traces.
      check_interrupt();

      if (smc_pending)
        {
          for (size_t i = 0; i < n_trace_results; ++i)
            gcc_jit_result_release(trace_results[i]);
          free(trace_results);
          trace_results = NULL;
          n_trace_results = 0;
          cap_trace_results = 0;
          memset(trace_cache, 0, sizeof(trace_cache));
          smc_pending = 0;
        }

      if ((all_keys_down() & (1<<31)) || program_over)
        {
          break;
        }
      trace_count++;
    }

  deinit_interrupt_timer();
  deinit_io();
  deinit_chip8();

  dump_chip8_state("traces", trace_count);

  teardown_jit();

  return 0;
}
