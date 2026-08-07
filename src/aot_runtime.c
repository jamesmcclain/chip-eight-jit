/* Runtime for modules emitted by chip8-aot.  Link this file with interp built
   as CHIP8_RUNTIME, chip8.c, ncurses_io.c, and one generated .ll module. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chip8.h"
#include "io.h"

extern uint16_t op;
extern int last_tick;
extern uint32_t basic_block(void);
extern uint32_t all_keys_down(void);
extern void interrupt(void);
extern int tick(void);
extern void chip8_aot_run(void);
extern const uint8_t chip8_aot_rom[];
extern const uint32_t chip8_aot_rom_size;

static unsigned retired;

uint16_t chip8_aot_program_counter(void)
{
  return program_counter;
}

/* The generated module supplies the opcode.  The comparison turns an attempt
   to use mutable program memory into a clean stop instead of stale execution. */
uint32_t chip8_aot_execute(uint16_t expected)
{
  if (OPCODE_AT(program_counter) != expected)
    {
      fprintf(stderr, "AOT code changed at 0x%03X\n", program_counter);
      return 1;
    }
  ++retired;
  if ((retired & 31u) == 0)
    interrupt();
  if (all_keys_down() & (1u << 31))
    return 1;
  return basic_block();
}

int main(void)
{
  last_tick = tick();
  init_chip8();
  if (chip8_aot_rom_size > MEMORY_SIZE - ENTRYPOINT)
    {
      fprintf(stderr, "AOT ROM is too large\n");
      return 1;
    }
  memcpy(memory + ENTRYPOINT, chip8_aot_rom, chip8_aot_rom_size);
  init_io(64, 32);
  program_counter = ENTRYPOINT;
  chip8_aot_run();
  deinit_io();
  deinit_chip8();
  dump_chip8_state("AOT instructions", (int)retired);
  return 0;
}
