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
static bool report;
static bool assume_no_smc;
static bool decode_unreachable; /* decode dead regions as instructions (old behavior) */
static bool smc_possible;
static bool indirect_jump_seen;
static bool leader[MEMORY_SIZE]; /* address starts a basic block */
static unsigned char reach[MEMORY_SIZE]; /* byte is on a reachable code path */
static unsigned char class[MEMORY_SIZE]; /* 0 = unknown, 1 = code, 2 = data */

/* Whether the word is a valid, supported CHIP-8 instruction.  Data bytes
   encountered during descent fail this test and terminate the path. */
static bool valid_opcode(uint16_t op)
{
  unsigned kk = op & 0xff, n = op & 0xf;
  if (op == 0x00e0 || op == 0x00ee) return true;
  switch (op >> 12) {
  case 0x0: return false; /* SYS unsupported */
  case 0x5: case 0x9: return n == 0;
  case 0x8: return n <= 7 || n == 0xe;
  case 0xe: return kk == 0x9e || kk == 0xa1;
  case 0xf:
    switch (kk) {
    case 0x07: case 0x0a: case 0x15: case 0x18: case 0x1e:
    case 0x29: case 0x33: case 0x55: case 0x65: return true;
    }
    return false;
  }
  return true;
}

/* A label can only be referenced/emitted for an in-ROM, 2-byte aligned address. */
static bool labelable(unsigned addr, size_t size)
{
  return addr >= ENTRYPOINT && addr + 1 < ENTRYPOINT + size && !(addr & 1);
}

/* Classic leader analysis: entry point, static branch/call targets, and
   instructions following a control-flow transfer all start basic blocks.
   Bnnn (JP V0, nnn) is a computed jump; its targets cannot be found
   statically, so analysis may be incomplete for ROMs using jump tables. */
static void analyze(size_t size)
{
  unsigned end = ENTRYPOINT + size;
  leader[ENTRYPOINT] = true;
  for (unsigned pc = ENTRYPOINT; pc + 1 < end; pc += 2) {
    uint16_t op = (uint16_t)((memory[pc] << 8) | memory[pc + 1]);
    unsigned nnn = op & 0xfff, kk = op & 0xff, n = op & 0xf;
    bool skip = false, ends_block = false;
    switch (op >> 12) {
    case 0x0: if (op == 0x00ee) ends_block = true; break;
    case 0x1:
      if (labelable(nnn, size)) leader[nnn] = true;
      ends_block = true;
      break;
    case 0x2: if (labelable(nnn, size)) leader[nnn] = true; break;
    case 0x3: case 0x4: skip = true; break;
    case 0x5: case 0x9: if (n == 0) skip = true; break;
    case 0xb: ends_block = true; break; /* computed jump: targets unknown */
    case 0xe: if (kk == 0x9e || kk == 0xa1) skip = true; break;
    }
    if (skip) { /* fall-through and taken (pc+4) paths both start blocks */
      if (pc + 2 < end) leader[pc + 2] = true;
      if (pc + 4 < end) leader[pc + 4] = true;
    }
    if (ends_block && pc + 2 < end) leader[pc + 2] = true;
  }
}

static void mark_data(unsigned addr, unsigned len, size_t size)
{
  for (unsigned a = addr; a < addr + len && a < ENTRYPOINT + size; ++a)
    if (a < MEMORY_SIZE && class[a] != 1) class[a] = 2;
}

/* A reachable memory-write instruction (LD [I], Vx or LD B, Vx).  If I is
   dynamic or the written range covers known code, the ROM may modify its
   own instructions and static classification is unsound. */
static void note_write(bool have_ldi, unsigned addr, unsigned len)
{
  if (!have_ldi) { smc_possible = true; return; }
  for (unsigned a = addr; a < addr + len && a < MEMORY_SIZE; ++a)
    if (reach[a]) smc_possible = true;
}

/* Recursive descent from the entry point: follow every statically known
   control-flow path, marking executed bytes as code.  While walking, use
   the most recent LD I, nnn as a heuristic anchor: a following DRW n
   marks n sprite bytes, LD B marks 3 BCD bytes, LD [I]/LD V,[I] mark
   x+1 register-dump bytes.  Bnnn (JP V0) makes the analysis incomplete;
   its base is pushed as one possible entry (V0 == 0) and flagged. */
static void descend(size_t size)
{
  unsigned end = ENTRYPOINT + size;
  unsigned stack[MEMORY_SIZE / 2];
  size_t sp = 0;
  stack[sp++] = ENTRYPOINT;
  while (sp) {
    unsigned pc = stack[--sp];
    unsigned last_ldi = 0;
    bool have_ldi = false;
    while (pc + 1 < end && !reach[pc]) {
      uint16_t op = (uint16_t)((memory[pc] << 8) | memory[pc + 1]);
      if (!valid_opcode(op)) break; /* walked into data */
      unsigned x = (op >> 8) & 0xf, kk = op & 0xff, n = op & 0xf, nnn = op & 0xfff;
      reach[pc] = reach[pc + 1] = 1;
      class[pc] = class[pc + 1] = 1;
      unsigned next = pc + 2;
      switch (op >> 12) {
      case 0x0: /* 00e0 or 00ee */
        if (op == 0x00ee) next = 0; /* RET: path ends */
        break;
      case 0x1:
        next = 0;
        if (labelable(nnn, size)) stack[sp++] = nnn;
        break;
      case 0x2:
        if (labelable(nnn, size)) stack[sp++] = nnn;
        break;
      case 0x3: case 0x4: case 0x5: case 0x9:
        if (pc + 4 < end) stack[sp++] = pc + 4; /* taken path; fall through */
        break;
      case 0xa:
        last_ldi = nnn; have_ldi = true;
        break;
      case 0xb:
        next = 0;
        indirect_jump_seen = true;
        if (labelable(nnn, size)) stack[sp++] = nnn; /* V0 == 0 entry */
        break;
      case 0xd:
        if (have_ldi) mark_data(last_ldi, n, size);
        break;
      case 0xe:
        if (pc + 4 < end) stack[sp++] = pc + 4;
        break;
      case 0xf:
        if (kk == 0x33) { note_write(have_ldi, last_ldi, 3); if (have_ldi) mark_data(last_ldi, 3, size); }
        if (kk == 0x55) { note_write(have_ldi, last_ldi, x + 1); if (have_ldi) mark_data(last_ldi, x + 1, size); }
        if (have_ldi && kk == 0x65) mark_data(last_ldi, x + 1, size);
        break;
      }
      if (!next) break;
      pc = next;
    }
  }
}

/* Glue unreachable basic blocks: within each maximal run of unreachable
   bytes, clear every leader except the one at the run's start.  Splits
   inside dead regions are speculative artifacts of context-free leader
   analysis (the bytes may not even be instructions).  This is sound
   because any target of reachable code is itself reachable, so no live
   reference can point into the middle of a glued run.  Indirect JP V0
   jumps can in principle land anywhere, which is why the report flags
   the analysis as incomplete when one is seen. */
static void glue_unreachable(size_t size)
{
  unsigned end = ENTRYPOINT + size;
  for (unsigned pc = ENTRYPOINT; pc < end;) {
    if (reach[pc]) { ++pc; continue; }
    unsigned start = pc;
    while (pc < end && !reach[pc]) ++pc;
    if (labelable(start, size)) leader[start] = true; /* labels only exist at aligned, in-ROM addresses */
    for (unsigned a = start + 1; a < pc; ++a) leader[a] = false;
  }
}

/* Print a code/data/unknown region map to stderr. */
static void print_report(size_t size)
{
  unsigned end = ENTRYPOINT + size;
  unsigned totals[3] = {0, 0, 0};
  static const char *const names[3] = {"unknown", "code", "data"};
  fprintf(stderr, "region map:%s\n",
          indirect_jump_seen ? " (incomplete: indirect JP V0 seen)" : "");
  for (unsigned pc = ENTRYPOINT; pc < end;) {
    unsigned c = class[pc], run = pc;
    while (run < end && class[run] == c) ++run;
    totals[c] += run - pc;
    fprintf(stderr, "  0x%03X-0x%03X  %-7s (%u bytes)\n", pc, run - 1,
            names[c], run - pc);
    pc = run;
  }
  fprintf(stderr, "totals: code %u, data %u, unknown %u of %u bytes\n",
          totals[1], totals[2], totals[0], (unsigned)size);
  if (smc_possible && !assume_no_smc)
    fprintf(stderr, "warning: reachable memory writes with dynamic or "
            "code-overlapping targets; self-modifying code may invalidate "
            "this map (re-run with --assume-no-smc to assert otherwise)\n");
  if (assume_no_smc)
    fprintf(stderr, "note: assuming no self-modifying code\n");
}

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
  if (assembly_output && pc < MEMORY_SIZE && !reach[pc])
    fputs(" ; unreachable", stdout);
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
  case 0x1:
    if (assembly_output && leader[nnn]) { output(pc, "JP block%03X", nnn); return; }
    BOTH("JP 0x%03X", "jump 0x%04X", nnn);
  case 0x2:
    if (assembly_output && leader[nnn]) { output(pc, "CALL block%03X", nnn); return; }
    BOTH("CALL 0x%03X", "call 0x%04X", nnn);
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
  case 0xa:
    if (assembly_output && leader[nnn]) { output(pc, "LD I, block%03X", nnn); return; }
    BOTH("LD I, 0x%03X", "addr = 0x%04X", nnn);
  case 0xb:
    if (assembly_output)
      output(pc, "JP V0, 0x%03X ; WARNING: indirect jump, block analysis may be incomplete", nnn);
    else output(pc, "branch to 0x%04X + V0", nnn);
    return;
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
    else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--report")) report = true;
    else if (!strcmp(argv[i], "--assume-no-smc")) assume_no_smc = true;
    else if (!strcmp(argv[i], "--decode-unreachable")) decode_unreachable = true;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      fprintf(stderr, "Usage: %s [-a|--asm] [-r|--report] [--assume-no-smc] [--decode-unreachable] <rom>\n", argv[0]); return 0;
    } else if (!rom) rom = argv[i];
    else { fprintf(stderr, "Usage: %s [-a|--asm] [-r|--report] [--assume-no-smc] [--decode-unreachable] <rom>\n", argv[0]); return 2; }
  }
  if (!rom) { fprintf(stderr, "Usage: %s [-a|--asm] [-r|--report] [--assume-no-smc] [--decode-unreachable] <rom>\n", argv[0]); return 2; }
  FILE *fp = fopen(rom, "rb");
  if (!fp) { fprintf(stderr, "Could not open ROM %s\n", rom); return 1; }
  size_t size = fread(memory + ENTRYPOINT, 1, MEMORY_SIZE - ENTRYPOINT, fp);
  if (ferror(fp)) { fprintf(stderr, "Could not read ROM\n"); fclose(fp); return 1; }
  fclose(fp);
  analyze(size);
  descend(size);
  glue_unreachable(size);
  /* By default, treat unreachable bytes as data: they cannot execute on
     any static path, so decoding them as instructions is speculative
     fiction.  --decode-unreachable recovers the old behavior of
     disassembling them anyway (they then classify as "unknown"). */
  if (!decode_unreachable)
    for (unsigned a = ENTRYPOINT; a < ENTRYPOINT + size && a < MEMORY_SIZE; ++a)
      if (class[a] == 0) class[a] = 2;
  if (report) print_report(size);
  for (unsigned pc = ENTRYPOINT; pc + 1 < ENTRYPOINT + size; pc += 2) {
    uint16_t op = (uint16_t)((memory[pc] << 8) | memory[pc + 1]);
    if (assembly_output && leader[pc]) fprintf(stdout, "block%03X:\n", pc);
    if (!decode_unreachable && !reach[pc]) { /* dead word: emit as data */
      if (assembly_output)
        fprintf(stdout, "        .word 0x%04X ; data (unreachable)\n", op);
      else output(pc, "data word %04X", op);
      continue;
    }
    disassemble(pc, op);
  }
  if (size & 1) { /* odd-sized ROM: preserve the trailing byte */
    unsigned pc = ENTRYPOINT + size - 1;
    if (assembly_output) {
      if (leader[pc]) fprintf(stdout, "block%03X:\n", pc);
      fprintf(stdout, "        .byte 0x%02X\n", memory[pc]);
    } else output(pc, "trailing byte %02X at pc 0x%04X", memory[pc], pc);
  }
  return 0;
}
