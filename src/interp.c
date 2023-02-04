#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <arpa/inet.h>

#include "chip8.h"
#include "io.h"

#define ERROR {return (op << 16) | program_counter;}
#define STEP {program_counter+=2; return 0;}
#define X uint8_t x = (op >> 8) & 0xf
#define Y uint8_t y = (op >> 4) & 0xf
#define IMMEDIATE4 uint8_t immediate = op & 0xf
#define IMMEDIATE8 uint8_t immediate = op & 0xff
#define IMMEDIATE12 uint16_t immediate = op & 0xfff
#define NANOS_PER_TICK (16666666) // ~60 Hz clock
#define TICKS_PER_SECOND (60) // ~60 Hz clock

int last_tick = 0;

int8_t delay_timer = 0;
int8_t sound_timer = 0;
uint16_t op = 0;

#define INPUT_TICKS (10) // roughly 1/6 window for input
uint32_t keys_down[INPUT_TICKS];
int interrupt_count = 0;


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

uint32_t clearscreen()
{
  clearscreen_io();
  STEP;
}

uint32_t jump()
{
  IMMEDIATE12;

  interrupt();
  program_counter = immediate;
  return 0;
}

uint32_t retern()
{
  interrupt();
  if (stack_pointer > 0)
    {
      program_counter = stack[--stack_pointer];
      return 0;
    }
  else
    ERROR;
}

uint32_t call()
{
  IMMEDIATE12;

  interrupt();
  if (stack_pointer + 1 < STACK_SIZE)
    {
      stack[stack_pointer++] = program_counter + 2;
      program_counter = immediate;
      return 0;
    }
  else
    ERROR;
}

uint32_t skip_eq_immediate()
{
  X; IMMEDIATE8;

  if (regs[x] == immediate)
    {
      program_counter+=2;
    }
  STEP;
}

uint32_t skip_neq_immediate()
{
  X; IMMEDIATE8;

  if (regs[x] != immediate)
    {
      program_counter+=2;
    }
  STEP;
}

uint32_t skip_eq_register()
{
  X; Y;

  if (regs[x] == regs[y])
    {
      program_counter+=2;
    }
  STEP;
}

uint32_t load_immediate()
{
  X; IMMEDIATE8;

  regs[x] = immediate;
  STEP;
}

uint32_t add_immediate()
{
  X; IMMEDIATE8;

  regs[x] += immediate;
  STEP;
}

uint32_t move()
{
  X; Y;

  regs[x] = regs[y];
  STEP;
}

uint32_t or()
{
  X; Y;

  regs[x] |= regs[y];
  STEP;
}

uint32_t and()
{
  X; Y;

  regs[x] &= regs[y];
  STEP;
}

uint32_t xor()
{
  X; Y;

  regs[x] ^= regs[y];
  STEP;
}

uint32_t add_register()
{
  X; Y;

  uint16_t tmp = regs[x] + regs[y];
  FLAGS = (tmp > 0xff) ? 1 : 0;
  regs[x] = tmp;
  STEP;
}

uint32_t sub_register()
{
  X; Y;

  FLAGS = (regs[x] > regs[y]) ? 1 : 0;
  regs[x] -= regs[y];
  STEP;
}

uint32_t shift_right()
{
  X;
  /* Y; */

  FLAGS = regs[x] & 0x01;
  /* regs[x] >>= regs[y]; */
  regs[x] >>= 1;
  STEP;
}

uint32_t subn_register()
{
  X; Y;

  FLAGS = (regs[y] > regs[x]) ? 1 : 0;
  regs[x] = regs[y] - regs[x];
  STEP;
}

uint32_t shift_left()
{
  X;
  /* Y; */

  FLAGS = regs[x] & 0x80;
  /* regs[x] <<= regs[y]; */
  regs[x] <<= 1;
  STEP;
}

uint32_t skip_neq_register()
{
  X; Y;

  if (regs[x] != regs[y])
    {
      program_counter+=2;
    }
  STEP;
}

uint32_t load_addr_immediate()
{
  IMMEDIATE12;

  addr = immediate;
  STEP;
}

uint32_t branch()
{
  IMMEDIATE12;

  program_counter = immediate + regs[0];
  return 0;
}

uint32_t random_byte()
{
  X; IMMEDIATE8;

  regs[x] = rand() & 0xff & immediate;
  STEP;
}

uint32_t get_delay_timer()
{
  X;

  regs[x] = delay_timer;
  STEP;
}

uint32_t set_delay_timer()
{
  X;

  delay_timer = regs[x];
  STEP;
}

uint32_t set_sound_timer()
{
  X;

  sound_timer = regs[x];
  STEP;
}

uint32_t add_addr()
{
  X;

  addr += regs[x];
  STEP;
}

uint32_t store_bcd()
{
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

uint32_t skip_key_x(int up)
{
  X;

  if (((all_keys_down() & (1<<(regs[x]))) != 0) ^ up)
    {
      clear_key(regs[x]);
      program_counter+=2;
    }
  STEP;
}

uint32_t load_on_key()
{
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

uint32_t draw()
{
  X; Y; IMMEDIATE4;
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

uint32_t save_registers()
{
  X;

  for (int i = 0; i <= x; ++i)
    {
      memory[addr+i] = regs[i];
    }
  STEP;
}

uint32_t restore_registers()
{
  X;

  for (int i = 0; i <= x; ++i)
    {
      regs[i] = memory[addr+i];
    }
  STEP;
}

uint32_t load_sprite_addr()
{
  X;

  addr = regs[x] * 5;
  STEP;
}

uint32_t basic_block()
{
  op = ntohs(((uint16_t *)memory)[program_counter>>1]);

  if (op == 0x00e0)
    {
      return clearscreen();
    }
  else if (op == 0x00ee)
    {
      return retern();
    }

  switch ((op & 0xf000) >> 12)
    {
    case 0x0:
      return jump();
    case 0x1:
      return jump();
    case 0x2:
      return call();
    case 0x3:
      return skip_eq_immediate();
    case 0x4:
      return skip_neq_immediate();
    case 0x5:
      return skip_eq_register();
    case 0x6:
      return load_immediate();
    case 0x7:
      return add_immediate();
    case 0x8:
      {
        switch (op & 0x000f)
          {
          case 0x0:
            return move();
          case 0x1:
            return or();
          case 0x2:
            return and();
          case 0x3:
            return xor();
          case 0x4:
            return add_register();
          case 0x5:
            return sub_register();
          case 0x6:
            return shift_right();
          case 0x7:
            return subn_register();
          case 0xe:
            return shift_left();
          default:
            ERROR;
          }
      }
    case 0x9:
      return skip_neq_register();
    case 0xa:
      return load_addr_immediate();
    case 0xb:
      return branch();
    case 0xc:
      return random_byte();
    case 0xd:
      return draw();
    case 0xe:
      {
        switch (op & 0x00ff)
          {
          case 0x9e:
            return skip_key_x(0);
          case 0xa1:
            return skip_key_x(1);
          default:
            ERROR;
          }
      }
    case 0xf:
      {
        switch (op & 0x00ff)
          {
          case 0x07:
            return get_delay_timer();
          case 0x0a:
            return load_on_key();
          case 0x15:
            return set_delay_timer();
          case 0x18:
            return set_sound_timer();
          case 0x1e:
            return add_addr();
          case 0x29:
            return load_sprite_addr();
          case 0x33:
            return store_bcd();
          case 0x55:
            return save_registers();
          case 0x65:
            return restore_registers();
          }
      }
    default:
      ERROR;
    }
}

// ------------------------------------------------------------------------

int main(int argc, const char * argv[])
{
  FILE * fp;
  int inst_count = 0;

  if (argc <= 1)
    {
      fprintf(stderr, "Usage: %s <rom>\n", argv[0]);
      exit(-1);
    }

  // load
  fp = fopen(argv[1], "r");
  fread(memory + ENTRYPOINT, sizeof(uint8_t), MEMORY_SIZE - ENTRYPOINT, fp);
  fclose(fp);

  // initialize
  last_tick = tick();
  init_chip8();
  init_io(64, 32);

  // transfer
  program_counter = ENTRYPOINT;
  while (1)
    {
      /* interrupt(); */
      if (all_keys_down() & (1<<31))
        {
          break;
        }
      if (basic_block())
        {
          break;
        }
      inst_count++;
    }

  deinit_io();
  deinit_chip8();

  for (int i = 0; i < REGFILE_SIZE; ++i)
    {
      fprintf(stderr, "V%02d = 0x%02X\n", i, regs[i]);
    }
  fprintf(stderr, "%d chip-8 instructions executed\n", inst_count);
  fprintf(stderr, "$pc = 0x%04X\n", program_counter);
  fprintf(stderr, "$addr = 0x%04X\n", addr);
  fprintf(stderr, "stack[%d] = 0x%04X\n", stack_pointer, stack[stack_pointer]);
  fprintf(stderr, "delay = %d\n", delay_timer);
  fprintf(stderr, "sound = %d\n", sound_timer);

  exit(0);
}
