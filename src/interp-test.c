#include <stdio.h>
#include "chip8.h"
#include "io.h"

uint32_t load_on_key();

int main()
{
  init_chip8();
  init_io(64, 32);
  draw_io(0, 0, 5, memory);
  load_on_key();
  draw_io(0, 0, 5, memory);
  load_on_key();
  draw_io(7, 4, 5, &memory[15]);
  load_on_key();
  deinit_io();
  deinit_chip8();
}
