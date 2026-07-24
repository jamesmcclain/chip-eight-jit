#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRYPOINT 0x200
#define MEMORY_SIZE 0x1000

static uint8_t memory[MEMORY_SIZE];
static bool assembly_output;

static void output(unsigned pc, const char *format, ...)
{
  va_list ap;
  if (assembly_output)
    fprintf(stdout, "        ");
  else
    fprintf(stdout, "0x%04X:\t", pc);
  va_start(ap, format);
  vfprintf(stdout, format, ap);
  va_end(ap);
  fputc('\n', stdout);
}

static void raw_word(unsigned pc, uint16_t op)
{
  if (assembly_output)
    output(pc, ".word 0x%04X ; unrecognized opcode", op);
  else
    output(pc, "op code %04X at pc 0x%04X", op, pc);
}

static void disassemble(unsigned pc, uint16_t op)
{
  unsigned x = (op >> 8) & 0xf, y = (op >> 4) & 0xf;
  unsigned n = op & 0xf, kk = op & 0xff, nnn = op & 0xfff;
#define BOTH(asm_format, prose_format, ...) \
  do { output(pc, assembly_output ? (asm_format) : (prose_format), ##__VA_ARGS__); return; } while (0)
  if (op == 0x00e0) BOTH("CLS", "clear");
  if (op == 0x00ee) BOTH("RET", "return");

  switch (op >> 12) {
  case 0x0:
    if (assembly_output) raw_word(pc, op); /* SYS is unsupported by this emulator. */
    else output(pc, "jump 0x%04X", nnn);
    return;
  case 0x1: BOTH("JP 0x%03X", "jump 0x%04X", nnn);
  case 0x2: BOTH("CALL 0x%03X", "call 0x%04X", nnn);
  case 0x3: BOTH("SE V%X, 0x%02X", "skip next if V%X == 0x%04X", x, kk);
  case 0x4: BOTH("SNE V%X, 0x%02X", "skip next if V%X != 0x%04X", x, kk);
  case 0x5:
    if (n == 0 || !assembly_output) BOTH("SE V%X, V%X", "skip next if V%X == V%X", x, y);
    raw_word(pc, op); return;
  case 0x6: BOTH("LD V%X, 0x%02X", "V%X = 0x%04X", x, kk);
  case 0x7: BOTH("ADD V%X, 0x%02X", "V%X += 0x%04X", x, kk);
  case 0x8:
    switch (n) {
    case 0: BOTH("LD V%X, V%X", "V%X = V%X", x, y);
    case 1: BOTH("OR V%X, V%X", "V%X |= V%X", x, y);
    case 2: BOTH("AND V%X, V%X", "V%X &= V%X", x, y);
    case 3: BOTH("XOR V%X, V%X", "V%X ^= V%X", x, y);
    case 4: BOTH("ADD V%X, V%X", "V%X += V%X", x, y);
    case 5: BOTH("SUB V%X, V%X", "V%X -= V%X", x, y);
    case 6: BOTH("SHR V%X, V%X", "V%X >>= V%X", x, y);
    case 7: BOTH("SUBN V%X, V%X", "V%X = V%X - V%X", x, y, x);
    case 0xe: BOTH("SHL V%X, V%X", "V%X <<= V%X", x, y);
    default: raw_word(pc, op); return;
    }
  case 0x9:
    if (n == 0 || !assembly_output) BOTH("SNE V%X, V%X", "skip next if V%X != V%X", x, y);
    raw_word(pc, op); return;
  case 0xa: BOTH("LD I, 0x%03X", "addr = 0x%04X", nnn);
  case 0xb: BOTH("JP V0, 0x%03X", "branch to 0x%04X + V0", nnn);
  case 0xc: BOTH("RND V%X, 0x%02X", "V%X = <random> & 0x%04X", x, kk);
  case 0xd: BOTH("DRW V%X, V%X, 0x%X", "draw sprite from addr of height %d at (V%X, V%X)", x, y, n);
  case 0xe:
    if (kk == 0x9e) BOTH("SKP V%X", "skip next if V%X is down", x);
    if (kk == 0xa1) BOTH("SKNP V%X", "skip next if V%X is up", x);
    raw_word(pc, op); return;
  case 0xf:
    switch (kk) {
    case 0x07: BOTH("LD V%X, DT", "V%X = <current delay timer>", x);
    case 0x0a: BOTH("LD V%X, K", "V%X = <next key pressed>", x);
    case 0x15: BOTH("LD DT, V%X", "<current delay timer> = V%X", x);
    case 0x18: BOTH("LD ST, V%X", "<current sound timer> = V%X", x);
    case 0x1e: BOTH("ADD I, V%X", "addr += V%X", x);
    case 0x29: BOTH("LD F, V%X", "addr = <address of hex sprite V%X>", x);
    case 0x33: BOTH("LD B, V%X", "store V%X in BCD starting at addr", x);
    case 0x55: BOTH("LD [I], V%X", "save registers V0 through V%X starting at addr", x);
    case 0x65: BOTH("LD V%X, [I]", "load registers V0 through V%X starting at addr", x);
    default: raw_word(pc, op); return;
    }
  }
#undef BOTH
}

int main(int argc, const char *argv[])
{
  const char *rom = NULL;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--asm")) assembly_output = true;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      fprintf(stderr, "Usage: %s [-a|--asm] <rom>\n", argv[0]); return 0;
    } else if (!rom) rom = argv[i];
    else { fprintf(stderr, "Usage: %s [-a|--asm] <rom>\n", argv[0]); return 2; }
  }
  if (!rom) { fprintf(stderr, "Usage: %s [-a|--asm] <rom>\n", argv[0]); return 2; }
  FILE *fp = fopen(rom, "rb");
  if (!fp) { fprintf(stderr, "Could not open ROM %s\n", rom); return 1; }
  size_t size = fread(memory + ENTRYPOINT, 1, MEMORY_SIZE - ENTRYPOINT, fp);
  if (ferror(fp)) { fprintf(stderr, "Could not read ROM\n"); fclose(fp); return 1; }
  fclose(fp);
  for (unsigned pc = ENTRYPOINT; pc + 1 < ENTRYPOINT + size; pc += 2) {
    uint16_t op = (uint16_t)((memory[pc] << 8) | memory[pc + 1]);
    if (op == 0) break;
    disassemble(pc, op);
  }
  return 0;
}
