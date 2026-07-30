/* Headless I/O backend and virtual clock for -DBENCH builds.
 *
 * Stands in for ncurses_io.c: same framebuffer, same draw and collision
 * semantics, no terminal. Also owns the virtual clock and synthetic keyboard
 * that make a run reproducible; see bench.h for the rationale. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "io.h"
#include "chip8.h"
#include "bench.h"

uint8_t __attribute__((aligned(0x1000))) display[DISPLAY_SIZE];
int width = 0;
int height = 0;

long long bench_retired = 0;
long long bench_poll_clock = 0;
long long bench_budget = BENCH_DEFAULT_INSTRUCTIONS;
long long bench_clock_cap = BENCH_DEFAULT_INSTRUCTIONS * BENCH_CLOCK_CAP_FACTOR;
long long bench_compiled = 0;
long long bench_flushes = 0;
unsigned  bench_seed = BENCH_DEFAULT_SEED;
int       bench_key_mode = BENCH_KEYS_ROTATE;

static struct timespec bench_start;

void init_io(int _width, int _height)
{
  width = _width;
  height = _height;
  memset(display, 0, sizeof(display));
}

void deinit_io()
{
}

/* Identical to the ncurses backend's pixel logic: wrap the origin, clip the
   sprite at the edges, XOR, and report collision. Only the rendering is
   dropped, so the framebuffer (and VF) match an interactive run exactly. */
int draw_io(int x, int y, int n, uint8_t * mem)
{
  int vf = 0;

  x %= width;
  y %= height;

  for (int j = 0; j < n; ++j)
    {
      int y2 = j + y;
      if (y2 >= height)
        continue;
      uint8_t byte = mem[j];

      for (int i = 0; i < 8; ++i)
        {
          int x2 = i + x;
          if (x2 >= width)
            continue;
          int bit = (byte & (0x80>>i)) ? 1: 0;
          int old_pixel = display[x2 + y2*width];
          int new_pixel = old_pixel ^ bit;

          display[x2 + y2*width] = new_pixel;
          if (old_pixel && bit)
            {
              vf |= 1;
            }
        }
    }

  return vf;
}

void clearscreen_io()
{
  memset(display, 0, DISPLAY_SIZE * sizeof(uint8_t));
}

void refresh_io()
{
}

uint32_t read_keys_io()
{
  return bench_poll_keys();
}

// ------------------------------------------------------------------------

/* Monotonic, unlike the wall-clock tick() it replaces: callers subtract two
   readings to learn how many 60 Hz ticks of work have elapsed, and wrapping
   at 60 would corrupt that whenever a run outpaces one virtual second
   between polls. */
int bench_tick(void)
{
  return (int)(bench_now() / BENCH_INSTR_PER_TICK);
}

/* Synthetic keyboard. Every key is pressed and released in turn, held for
   half of each four-tick slot, so ROMs that block on Fx0A or gate on
   Ex9E/ExA1 make progress instead of parking forever. The pattern depends
   only on the virtual clock, so it replays identically.
 *
 * Ex9E/ExA1 and Fx0A consume a key by clearing it; that clear lasts for the
 * remainder of the current tick. Keeping it tick-scoped is what makes the
 * visible key state a pure function of the clock rather than of a per-engine
 * ring of past polls -- the engines poll at wildly different rates, and a
 * ring would make them disagree. */
static uint32_t bench_cleared = 0;
static long long bench_cleared_tick = -1;

static void bench_roll_tick(long long t)
{
  if (t != bench_cleared_tick)
    {
      bench_cleared_tick = t;
      bench_cleared = 0;
    }
}

uint32_t bench_keys_now(void)
{
  long long t = bench_now() / BENCH_INSTR_PER_TICK;

  bench_roll_tick(t);

  if (bench_key_mode == BENCH_KEYS_NONE)
    {
      return 0;
    }
  if ((t % 4) >= 2)
    {
      return 0;
    }
  return (1u << (unsigned)((t / 4) % 16)) & ~bench_cleared;
}

void bench_clear_key(uint8_t key)
{
  bench_roll_tick(bench_now() / BENCH_INSTR_PER_TICK);
  bench_cleared |= (1u << key);
}

uint32_t bench_poll_keys(void)
{
  bench_poll_clock += BENCH_POLL_COST;
  return bench_keys_now();
}

int bench_done(void)
{
  return (bench_retired >= bench_budget) || (bench_now() >= bench_clock_cap);
}

uint64_t bench_display_hash(void)
{
  uint64_t h = 1469598103934665603ULL;

  for (int i = 0; i < width * height; ++i)
    {
      h ^= (uint64_t)(display[i] ? 1u : 0u);
      h *= 1099511628211ULL;
    }
  return h;
}

static void usage(const char *prog)
{
  fprintf(stderr,
          "Usage: %s <rom> [--instructions N] [--seed S] [--keys none|rotate]\n"
          "\nDeterministic benchmark mode: headless, fixed seed, virtual 60 Hz\n"
          "clock driven by retired instructions. Runs until N instructions\n"
          "have retired, then prints machine state, counters and a display\n"
          "hash. Two runs of the same build agree exactly, and so do the\n"
          "interpreter and the JITs.\n", prog);
}

int bench_parse_args(int argc, const char *argv[], const char **rom)
{
  *rom = NULL;

  for (int i = 1; i < argc; ++i)
    {
      if (strcmp(argv[i], "--instructions") == 0 && (i + 1) < argc)
        {
          bench_budget = atoll(argv[++i]);
        }
      else if (strcmp(argv[i], "--seed") == 0 && (i + 1) < argc)
        {
          bench_seed = (unsigned)strtoul(argv[++i], NULL, 0);
        }
      else if (strcmp(argv[i], "--keys") == 0 && (i + 1) < argc)
        {
          ++i;
          if (strcmp(argv[i], "none") == 0)
            bench_key_mode = BENCH_KEYS_NONE;
          else if (strcmp(argv[i], "rotate") == 0)
            bench_key_mode = BENCH_KEYS_ROTATE;
          else
            {
              usage(argv[0]);
              return 1;
            }
        }
      else if (argv[i][0] == '-')
        {
          usage(argv[0]);
          return 1;
        }
      else if (*rom == NULL)
        {
          *rom = argv[i];
        }
      else
        {
          usage(argv[0]);
          return 1;
        }
    }

  if (*rom == NULL)
    {
      usage(argv[0]);
      return 1;
    }

  if (bench_budget <= 0)
    {
      fprintf(stderr, "--instructions must be positive\n");
      return 1;
    }
  bench_clock_cap = bench_budget * BENCH_CLOCK_CAP_FACTOR;

  clock_gettime(CLOCK_MONOTONIC, &bench_start);
  return 0;
}

void bench_report(const char *engine, const char *counter_label, int counter)
{
  struct timespec now;
  double elapsed;

  clock_gettime(CLOCK_MONOTONIC, &now);
  elapsed = (double)(now.tv_sec - bench_start.tv_sec)
          + (double)(now.tv_nsec - bench_start.tv_nsec) / 1e9;

  // The timers are caught up lazily, so the last sync happened at a different
  // point in each engine. Level them at the final clock before reporting.
  sync_timers();

  dump_chip8_state(counter_label, counter);

  fprintf(stderr, "engine = %s\n", engine);
  fprintf(stderr, "seed = %u\n", bench_seed);
  fprintf(stderr, "keys = %s\n",
          (bench_key_mode == BENCH_KEYS_NONE) ? "none" : "rotate");
  fprintf(stderr, "retired = %lld\n", bench_retired);
  fprintf(stderr, "compiled = %lld\n", bench_compiled);
  fprintf(stderr, "flushes = %lld\n", bench_flushes);
  fprintf(stderr, "clock = %lld\n", bench_now());
  fprintf(stderr, "display = 0x%016llx\n",
          (unsigned long long)bench_display_hash());
  fprintf(stderr, "elapsed = %.3f s\n", elapsed);
  if (elapsed > 0.0)
    {
      fprintf(stderr, "rate = %.2f Minsn/s\n",
              (double)bench_retired / elapsed / 1e6);
    }
}
