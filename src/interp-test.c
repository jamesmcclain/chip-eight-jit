#include <stdio.h>
#include "chip8.h"
#include "io.h"

uint32_t load_on_key();

int main()
{
  init_chip8();
  init_io(64, 32);
  load_on_key();
  deinit_io();
  deinit_chip8();
}
