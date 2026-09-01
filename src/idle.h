#ifndef __IDLE_H__
#define __IDLE_H__

#include <stdint.h>
#include "chip8.h"

/* Recognize the canonical CHIP-8 delay-timer poll loop:
 *
 *   L: Fx07 Vx      ; Vx := delay
 *      3x00         ; SE Vx, 0  -- skip the jump once the timer has expired
 *      1nnn         ; JP L
 *
 * This is the shape Octo emits for
 *
 *   loop  vf := delay  if vf != 0 then  again
 *
 * and it appears verbatim in many ROMs; see "The Art of CHIP-8", Timing.
 * The body has no effect other than loading Vx, so once the timer reaches
 * zero the loop falls through with Vx == 0 and the program counter at L + 6.
 * An engine that recognizes this can wait for the timer directly instead of
 * spinning a native loop that does nothing.
 *
 * Returns the loop length in bytes (6) and sets *reg to x when [pc] begins
 * such a loop; returns 0 otherwise.
 */
static inline int chip8_idle_delay_loop (uint16_t pc, uint8_t *reg)
{
  uint16_t a = OPCODE_AT (pc);
  uint16_t b = OPCODE_AT (pc + 2);
  uint16_t c = OPCODE_AT (pc + 4);
  uint8_t x = (a >> 8) & 0xf;

  if ((a & 0xf0ff) != 0xf007)	/* Fx07: Vx := delay */
    return 0;
  if ((b & 0xf0ff) != 0x3000 || ((b >> 8) & 0xf) != x)	/* 3x00: SE Vx, 0 */
    return 0;
  if ((c & 0xf000) != 0x1000 || (c & 0x0fff) != pc)	/* 1nnn: JP pc */
    return 0;
  *reg = x;
  return 6;
}

#endif
