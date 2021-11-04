#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <arpa/inet.h>

#define ENTRYPOINT (0x200)
#define MEMORY_SIZE (0x1000)

#define ERROR fprintf(stdout, "op code %0x04X\n", op)
#define PC fprintf(stdout, "0x%04X:\t", program_counter)
#define STEP return 0

#define X uint8_t x = (op >> 8) & 0xf
#define Y uint8_t y = (op >> 4) & 0xf
#define IMMEDIATE4 uint8_t immediate = op & 0xf
#define IMMEDIATE8 uint8_t immediate = op & 0xff
#define IMMEDIATE12 uint16_t immediate = op & 0xfff

uint8_t __attribute__((aligned(0x1000))) memory[MEMORY_SIZE];
uint16_t program_counter = 0;
uint16_t op = 0;


uint32_t clearscreen()
{
  PC; fprintf(stdout, "clear\n"); STEP;
}

uint32_t jump()
{
  IMMEDIATE12;

  PC; fprintf(stdout, "jump 0x%04X\n", immediate); STEP;
}

uint32_t retern()
{
  PC; fprintf(stdout, "return\n"); STEP;
}

uint32_t call()
{
  IMMEDIATE12;

  PC; fprintf(stdout, "call 0x%04X\n", immediate); STEP;
}

uint32_t skip_eq_immediate()
{
  X; IMMEDIATE8;

  PC; fprintf(stdout, "skip next if V%d == 0x%04X\n", x, immediate); STEP;
}

uint32_t skip_neq_immediate()
{
  X; IMMEDIATE8;

  PC; fprintf(stdout, "skip next if V%d != 0x%04X\n", x, immediate); STEP;
}

uint32_t skip_eq_register()
{
  X; Y;

  PC; fprintf(stdout, "skip next if V%d == V%d\n", x, y); STEP;
}

uint32_t load_immediate()
{
  X; IMMEDIATE8;

  PC; fprintf(stdout, "V%d = 0x%04X\n", x, immediate); STEP;
}

uint32_t add_immediate()
{
  X; IMMEDIATE8;

  PC; fprintf(stdout, "V%d += 0x%04X\n", x, immediate); STEP;
}

uint32_t move()
{
  X; Y;

  PC; fprintf(stdout, "V%d = V%d\n", x, y); STEP;
}

uint32_t or()
{
  X; Y;

  PC; fprintf(stdout, "V%d |= V%d\n", x, y); STEP;
}

uint32_t and()
{
  X; Y;

  PC; fprintf(stdout, "V%d &= V%d\n", x, y); STEP;
}

uint32_t xor()
{
  X; Y;

  PC; fprintf(stdout, "V%d ^= V%d\n", x, y); STEP;
}

uint32_t add_register()
{
  X; Y;

  PC; fprintf(stdout, "V%d += V%d\n", x, y); STEP;
}

uint32_t sub_register()
{
  X; Y;

  PC; fprintf(stdout, "V%d -= V%d\n", x, y); STEP;
}

uint32_t shift_right()
{
  X; Y;

  PC; fprintf(stdout, "V%d >>= V%d\n", x, y); STEP;
}

uint32_t subn_register()
{
  X; Y;

  PC; fprintf(stdout, "V%d = V%d - V%d\n", x, y, x); STEP;;
}

uint32_t shift_left()
{
  X; Y;

  PC; fprintf(stdout, "V%d <<= V%d\n", x, y); STEP;
}

uint32_t skip_neq_register()
{
  X; Y;

  PC; fprintf(stdout, "skip next if V%d != V%d\n", x, y); STEP;
}

uint32_t load_addr_immediate()
{
  IMMEDIATE12;

  PC; fprintf(stdout, "addr = 0x%04X\n", immediate); STEP;
}

uint32_t branch()
{
  IMMEDIATE12;

  PC; fprintf(stdout, "branch to 0x%04X + V0\n", immediate); STEP;
}

uint32_t random_byte()
{
  X; IMMEDIATE8;

  PC; fprintf(stdout, "V%d = <random> & 0x%04X\n", x, immediate); STEP;
}

uint32_t get_delay_timer()
{
  X;

  PC; fprintf(stdout, "V%d = <current delay timer>\n", x); STEP;
}

uint32_t set_delay_timer()
{
  X;

  PC; fprintf(stdout, "<current delay timer> = V%d\n", x); STEP;
}

uint32_t set_sound_timer()
{
  X;

  PC; fprintf(stdout, "<current sound timer> = V%d\n", x); STEP;
}

uint32_t add_addr()
{
  X;

  PC; fprintf(stdout, "addr += V%d\n", x); STEP;
}

uint32_t store_bcd()
{
  X;

  PC; fprintf(stdout, "store V%d in BCD starting at addr\n", x); STEP;
}

uint32_t skip_key_x(int up)
{
  X;

  PC; fprintf(stdout, "skip next if V%d is %s\n", x, up? "up" : "down"); STEP;
}

uint32_t load_on_key()
{
  X;

  PC; fprintf(stdout, "V%d = <next key pressed>\n", x); STEP;
}

uint32_t draw()
{
  X; Y; IMMEDIATE4;

  PC; fprintf(stdout, "draw sprite from addr of height %d at (V%d, V%d)\n", immediate, x, y); STEP;
}

uint32_t save_registers()
{
  X;

  PC; fprintf(stdout, "save registers V0 through V%d starting at addr\n", x); STEP;
}

uint32_t restore_registers()
{
  X;

  PC; fprintf(stdout, "load registers V0 through V%d starting at addr\n", x); STEP;
}

uint32_t load_sprite_addr()
{
  X;

  PC; fprintf(stdout, "addr = <address of hex sprite V%d>\n", x); STEP;
}

uint32_t basic_block()
{
  op = ntohs(((uint16_t *)memory)[program_counter>>1]);

  if (op == 0)
    {
      return 0;
    }

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
            PC; ERROR;
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
            PC; ERROR; STEP;
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
      PC; ERROR; STEP;
    }
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

  // load
  fp = fopen(argv[1], "r");
  fread(memory + ENTRYPOINT, sizeof(uint8_t), MEMORY_SIZE - ENTRYPOINT, fp);
  fclose(fp);

  for (program_counter = ENTRYPOINT; program_counter < MEMORY_SIZE; program_counter+=2)
    {
      basic_block();
    }

  exit(0);
}
