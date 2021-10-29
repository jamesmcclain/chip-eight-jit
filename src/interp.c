#include <string.h>
#include <arpa/inet.h>
#include "chip8.h"
#include "io.h"

#define ERROR {return (op << 16) | program_counter;}
#define STEP {program_counter++; return 0;}
#define X uint8_t x = (op >> 8) & 0xf
#define Y uint8_t y = (op >> 4) & 0xf
#define IMMEDIATE4 uint8_t immediate = op & 0xf
#define IMMEDIATE8 uint8_t immediate = op & 0xff
#define IMMEDIATE12 uint16_t immediate = op & 0xfff

int8_t delay_timer = 0;
int8_t sound_timer = 0;

uint8_t op;


uint32_t clearscreen()
{
  clearscreen_io();
  STEP;
}

uint32_t jump()
{
  program_counter = 0x0fff & op;
  return 0;
}

uint32_t retern()
{
  if (stack_pointer > 0)
    {
      program_counter = stack[stack_pointer--];
      return 0;
    }
  else
    ERROR;
}

uint32_t call()
{
  if (stack_pointer + 1 < STACK_SIZE)
    {
      stack[++stack_pointer] = program_counter;
      program_counter = 0x0fff & op;
      return 0;
    }
  else
    ERROR;
}

uint32_t skip_eq_immediate()
{
  X; IMMEDIATE8;
  if (regs[x] == immediate)
    program_counter++;
  STEP;
}

uint32_t skip_neq_immediate()
{
  X; IMMEDIATE8;
  if (regs[x] != immediate)
    program_counter++;
  STEP;
}

uint32_t skip_eq_register()
{
  X; Y;
  if (regs[x] == regs[y])
    program_counter++;
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
  uint16_t tmp = regs[x];
  tmp += regs[y];
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
  X; Y;
  FLAGS = regs[x] & 0x1;
  regs[x] >>= regs[y];
  STEP;
}

uint32_t subn_register()
{
  X; Y;
  FLAGS = (regs[y] > regs[x]) ? 1 : 0;
  regs[x] = regs[y] - regs[y];
  STEP;
}

uint32_t shift_left()
{
  X; Y;
  FLAGS = regs[x] & 0x8;
  regs[x] <<= regs[y];
  STEP;
}

uint32_t skip_neq_register()
{
  X; Y;
  if (regs[x] == regs[y])
    program_counter++;
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

uint32_t random()
{
  X; IMMEDIATE8;
  regs[x] = immediate;
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
  delay_timer = regs[x];
  STEP;
}

uint32_t add_addr_immediate()
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
  uint16_t keys_down;

  keys_down = read_keys_io();
  if ((keys_down & (1<<(regs[x]))) ^ up)
    {
      program_counter++;
    }
  STEP;
}

uint32_t load_on_key()
{
  X;
  uint16_t keys_down = 0;

  while (!keys_down)
    {
      keys_down = read_keys_io();
    }
  for (int i = 0; i < 0xf; ++i)
    {
      if (keys_down & (1<<i))
        {
          regs[x] = i;
        }
    }
  STEP;
}

uint32_t draw()
{
  X; Y; IMMEDIATE4;
  FLAGS = draw_io(regs[x], regs[y], immediate, &(memory[addr]));
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
  op = ntohs(memory[program_counter]);

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
      return random();
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
            return add_addr_immediate();
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
