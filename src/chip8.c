#include <stdint.h>
#include "chip8.h"

uint16_t stack[STACK_SIZE];
uint8_t __attribute__((aligned(0x1000))) memory[MEMORY_SIZE];
uint8_t regs[REGFILE_SIZE];

int8_t stack_pointer = 0;
uint16_t addr = 0;
uint16_t program_counter = 0;

void init_chip8()
{
}

void deinit_chip8()
{
}
