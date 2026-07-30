#ifndef __BENCH_H__
#define __BENCH_H__

/* Deterministic benchmark / differential-test mode.
 *
 * A normal run of any engine is unrepeatable: the RNG is seeded from the wall
 * clock, the 60 Hz timers are driven by CLOCK_MONOTONIC (and, in the JITs, by
 * a SIGALRM interval timer), input arrives whenever the terminal delivers it,
 * and no ROM ever terminates. Two runs of the same binary on the same ROM do
 * not agree, so neither A/B measurement nor engine-vs-engine comparison is
 * possible.
 *
 * Building with -DBENCH replaces every one of those inputs with something
 * reproducible:
 *
 *   - the RNG is seeded from --seed (default BENCH_DEFAULT_SEED);
 *   - the 60 Hz tick is a pure function of a virtual clock rather than of
 *     wall-clock time, so timers advance with executed work;
 *   - the virtual clock is driven by retired CHIP-8 instructions, plus a
 *     fixed charge per input poll so that blocking loops (Fx0A, the draw
 *     frame-sync) still make progress and terminate;
 *   - keyboard input is synthesised from the virtual clock (see --keys);
 *   - the display is headless: bench_io.c keeps the same framebuffer and the
 *     same draw/collision semantics as ncurses_io.c but renders nothing;
 *   - the run stops after --instructions retired instructions.
 *
 * The engines then agree instruction-for-instruction, which is what makes the
 * final state and display hash usable as a differential test, and what makes
 * "instructions retired per second" a comparable number across builds.
 *
 * Accounting invariant: bench_retired counts *architectural* CHIP-8
 * instructions, not emitted operations. A JIT peephole that folds several
 * opcodes into one native sequence must still add the number of CHIP-8
 * instructions it stood in for, or its virtual clock will drift away from the
 * interpreter's and the comparison becomes meaningless.
 */

#ifdef BENCH

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Virtual 60 Hz tick length, in instruction-equivalents. Roughly the work a
   period-accurate CHIP-8 does in one tick, but its only real requirement is
   that every engine uses the same number. */
#define BENCH_INSTR_PER_TICK (2000)
/* The 60 Hz tick counter wraps exactly as the wall-clock tick() does. */
#define BENCH_TICKS_PER_SECOND (60)
/* Clock charged for one input poll, so spin loops that retire no instructions
   (Fx0A) still advance the virtual clock and eventually hit the cap. Kept at
   1 so that polling barely distorts the instructions-per-tick rate a ROM
   actually sees. */
#define BENCH_POLL_COST (1)
#define BENCH_DEFAULT_SEED (20240101u)
#define BENCH_DEFAULT_INSTRUCTIONS (50000000LL)
/* Hard stop, as a multiple of the instruction budget, applied to the virtual
   clock. Guards ROMs that park in Fx0A and retire almost nothing. */
#define BENCH_CLOCK_CAP_FACTOR (64)

enum { BENCH_KEYS_NONE = 0, BENCH_KEYS_ROTATE = 1 };

/* The virtual clock is bench_retired + bench_poll_clock. Splitting it in two
   keeps the JIT-emitted accounting down to a single increment per CHIP-8
   instruction: traces touch bench_retired only, and the poll charge is added
   by the I/O layer. */
extern long long bench_retired;    /* architectural CHIP-8 instructions */
extern long long bench_poll_clock; /* clock charged for input polls */
extern long long bench_budget;     /* stop once bench_retired reaches this */
extern long long bench_clock_cap;  /* stop once the virtual clock reaches this */

#define bench_now() (bench_retired + bench_poll_clock)
/* Traces compiled and trace-cache flushes: JIT compile pressure is a first
   class cost in an emulator, and self-modifying writes discard the whole
   cache, so both belong in any A/B comparison. */
extern long long bench_compiled;
extern long long bench_flushes;
extern unsigned  bench_seed;
extern int       bench_key_mode;

/* Parse the bench flags out of argv and store the ROM path in *rom.
   Returns 0 on success, non-zero if usage was wrong. */
int bench_parse_args(int argc, const char *argv[], const char **rom);

/* Non-zero once the instruction budget or the clock cap has been reached. */
int bench_done(void);

/* The virtual 60 Hz tick, replacing the wall-clock tick(). */
int bench_tick(void);

/* Charge the virtual clock for one input poll and return the synthesised
   key state for the current virtual tick. Used by the Fx0A spin loops, whose
   polling is the only thing keeping the clock moving while no instruction
   retires. */
uint32_t bench_poll_keys(void);

/* The key state for the current virtual tick, with any keys cleared during
   this tick masked off. A pure function of the virtual clock (and of the
   clears, which happen on the same instructions in every engine), so it does
   not matter how often an engine happens to poll -- which is what keeps the
   interpreter and the JITs in step. */
uint32_t bench_keys_now(void);
void bench_clear_key(uint8_t key);

/* FNV-1a over the visible framebuffer: a cheap whole-screen comparison that
   works without a terminal. */
uint64_t bench_display_hash(void);

/* Catch the 60 Hz timers up to the virtual clock. Defined by each engine,
   which owns delay_timer/sound_timer/last_tick; called from every timer
   access and from bench_report, so a reported timer is never stale by an
   engine-specific amount. */
void sync_timers(void);

/* Print the machine state plus the benchmark counters. */
void bench_report(const char *engine, const char *counter_label, int counter);

#if defined(__cplusplus)
}
#endif

#endif /* BENCH */
#endif
