#include <stdint.h>
#include "chip8.h"

uint16_t stack[0x10];
uint8_t __attribute__((aligned(0x1000))) display[128*64];
uint8_t __attribute__((aligned(0x1000))) memory[0x1000];
uint8_t regs[16];

int16_t delay_timer = 0;
int16_t sound_timer = 0;
int8_t stack_pointer = 0;
uint16_t addr = 0;
uint16_t program_counter = 0;
