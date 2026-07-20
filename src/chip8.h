#include <stdint.h>

#ifndef __CHIP8_H__
#define __CHIP8_H__

#define STACK_SIZE (0x10)
#define MEMORY_SIZE (0x1000)
#define REGFILE_SIZE (0x10)
#define FLAGS regs[15]
#define ENTRYPOINT (0x200)

#define OPCODE_AT(pc)                                       \
  ((uint16_t)((memory[(pc) & (MEMORY_SIZE - 1)] << 8) |     \
              memory[((pc) + 1) & (MEMORY_SIZE - 1)]))

/* Wrapping data access into the 4 KiB address space. On the COSMAC VIP the
   12-bit address register wraps at 0x0FFF, so masking is both the authentic
   behavior and the fix for out-of-bounds host reads/writes when `addr` is
   pushed near the top of memory (via Annn/Fx1E) before Fx33/Fx55/Fx65/Dxyn. */
#define MEM_AT(a) memory[(a) & (MEMORY_SIZE - 1)]

#if defined(__cplusplus)
extern "C" {
#endif

extern uint16_t stack[];
extern uint8_t memory[];
extern uint8_t regs[];

extern uint8_t delay_timer;
extern uint8_t sound_timer;
extern int8_t stack_pointer;
extern uint16_t addr;
extern uint16_t program_counter;

void init_chip8();
void deinit_chip8();
void dump_chip8_state(const char *counter_label, int counter);

#if defined(__cplusplus)
}
#endif

#endif
