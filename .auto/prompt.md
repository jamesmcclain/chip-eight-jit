# Autoresearch: LLVM register promotion

## Objective
Keep CHIP-8 V0–VF in LLVM SSA/native registers for each generated trace. Improve
geometric-mean LLVM throughput on PONG, TETRIS, and BLINKY without changing VM
semantics.

## Metrics
- **Primary**: `rate_minsn_s` (million retired CHIP-8 instructions per second,
  higher is better).
- **Secondary**: per-ROM rate, traces compiled, and cache flushes.

## How to Run
`./.auto/measure.sh` builds LLVM/interpreter benchmark targets, runs five samples
per ROM, and reports median per-ROM rates plus their geometric mean.

## Files in Scope
- `src/llvm_jit.cpp`: LLVM trace code generator and register access barriers.
- `.auto/*`: benchmark, checks, and research record.

## Off Limits
- `src/libgccjit_jit.c` and all interpreter/VM semantics.

## Constraints
- `./.auto/checks.sh` must pass for retained changes.
- Materialize register memory before helpers that observe registers and before
  trace exits. Reload values after helpers that may modify registers.
- Preserve BENCH retired-count semantics.

## What's Been Tried
- Baseline pending.
