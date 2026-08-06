# Autoresearch: deterministic BENCH safepoints

## Objective
Remove the BENCH-only safepoint distortion in both trace JITs. In BENCH builds,
closed-loop and periodic safepoints must avoid an unconditional host call to
`check_interrupt()`. Replace the pinned `interrupt_pending` path with a cheap,
deterministic retired-instruction threshold check.

## Metrics
- **Primary**: `rate_mins` (Minsn/s, higher is better). This is the geometric
  mean of median rates for LLVM and libgccjit on MAZE, PONG, BLINKY, and TANK.
- **Secondary**: `llvm_rate_mins`, `libgccjit_rate_mins`, `min_rate_mins`,
  `compiled`, and `flushes`.

## How to Run
`./.auto/measure.sh` builds BENCH binaries, runs each selected ROM three times
per backend at 5,000,000 requested instructions, and emits metrics.

## Files in Scope
- `src/bench.h`: shared BENCH constants and declarations.
- `src/bench_io.c`: BENCH-owned counter state, argument setup, and reporting.
- `src/llvm_jit.cpp`: LLVM BENCH safepoint emission and helper behavior.
- `src/libgccjit_jit.c`: libgccjit BENCH safepoint emission and helper behavior.
- `README.md`: document changed BENCH behavior only if needed.

## Off Limits
- Interactive (non-BENCH) interrupt and input behavior.
- CHIP-8 opcode semantics, timer semantics, and architectural retirement
  accounting.
- Trace shape, cache invalidation, and the PC-store-sinking optimization.
- New dependencies and unrelated refactors.

## Constraints
- Both JITs must use equivalent BENCH semantics.
- BENCH stop/input/timer behavior must depend only on virtual work, never wall
  time.
- `bench_retired` remains the count of architectural CHIP-8 instructions.
- `sync_timers()` remains at every timer read/write.
- A JIT may stop after the requested budget only at a deterministic safepoint;
  `scripts/bench_diff.py` must exactly match the interpreter re-run at the
  actual retired count.
- A primary-metric win must be repeatable. Run full differential testing before
  keeping a substantive candidate.
- Maximum of 12 experiments.

## What's Been Tried
- Baseline: `rate_mins=22.105230`; LLVM geometric mean `60.241814`, libgccjit
  geometric mean `8.111330`. Three samples per ROM/backend at 5M requested
  instructions.
- Candidate 1: added a shared 64-instruction BENCH safepoint threshold. BENCH
  traces compare `bench_retired` with `bench_next_safepoint` and call a new
  backend-local `bench_safepoint()` only when due. The helper retains timer
  synchronization and budget detection, then advances the threshold. Full
  1M-instruction differential testing passed for both JITs. Measurement:
  `rate_mins=27.543192`; LLVM `84.164550`; libgccjit `9.013622`. Keep and use
  this as the new baseline. A later independent 64-interval measurement was
  `rate_mins=26.497352`, still about 20% above the original baseline despite
  substantial host noise.
- Candidate 2 (discarded): increased the threshold to 256. It passed the full
  differential suite, but two independent measurements gave `rate_mins`
  values of `27.868697` and `26.883782`; the apparent gain over 64 was below
  the observed noise. Restored 64 to retain tighter bounded stop latency.
- Candidate 3 (discarded): threshold 128 measured `rate_mins=25.639986`, below
  the repeated 64-interval result. No correctness run was needed: it changes
  only the same threshold schedule already differentially tested at 256.
- Candidate 4 (discarded): threshold 32 measured `rate_mins=26.377256`, also
  below the repeated 64-interval result. Restored 64.
