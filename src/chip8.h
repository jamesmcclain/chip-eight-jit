#include <stdint.h>

#ifndef __CHIP8__
#define __CHIP8__

#define DISPLAY_SIZE (128*64)
#define STACK_SIZE (0x10)
#define MEMORY_SIZE (0x1000)
#define REGFILE_SIZE (0x10)

extern uint16_t stack[];
extern uint8_t display[];
extern uint8_t memory[];
extern uint8_t regs[];
extern uint8_t flags;

extern int8_t delay_timer;
extern int8_t sound_timer;
extern int8_t stack_pointer;
extern uint16_t addr;
extern uint16_t program_counter;

#endif
