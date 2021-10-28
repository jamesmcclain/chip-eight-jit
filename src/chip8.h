#include <stdint.h>

#ifndef __CHIP8_H__
#define __CHIP8_H__

#define DISPLAY_SIZE (128*64)
#define STACK_SIZE (0x10)
#define MEMORY_SIZE (0x1000)
#define REGFILE_SIZE (0x10)
#define FLAGS regs[15]

extern uint16_t stack[];
extern uint8_t display[];
extern uint8_t memory[];
extern uint8_t regs[];

extern int8_t delay_timer;
extern int8_t sound_timer;
extern int8_t stack_pointer;
extern uint16_t addr;
extern uint16_t program_counter;

extern int width;
extern int height;

#endif
