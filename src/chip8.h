#include <stdint.h>

#ifndef __CHIP8__
#define __CHIP8__

extern uint16_t stack[];
extern uint8_t display[];
extern uint8_t memory[];
extern uint8_t regs[];

extern int16_t delay_timer;
extern int16_t sound_timer;
extern int8_t stack_pointer;
extern uint16_t addr;
extern uint16_t program_counter;

#endif
