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
- Baseline geometric mean: about 50 Minsn/s (PONG ~103, TETRIS ~52, BLINKY ~24), with appreciable host noise.
- Replacing all regs[] accesses with 16 local allocas and spilling/reloading all registers at every call and trace return was correct on the three-ROM differential suite but regressed to ~26 Minsn/s. Omitting barriers for safepoint, timer, clear, and return helpers improved only to ~32 Minsn/s. Discarded. The all-register exit barrier and/or generated code needs inspection before retrying that shape.
- Optimized IR inspection is complete: PONG hot trace ADDR212 has only 4 i8 loads and 8 i8 stores after O2, but benchmark retirement accounting emits an i64 load/add/store for every retired opcode. Calls make VM state observable. Whole-file spilling therefore costs more than it saves.
- Dirty-register experiment rejected: a generator-time mask is not path-sensitive at control-flow joins. Resetting it after a helper diverged on TETRIS; marking every register dirty after reload restored correctness but reached only 39.99 Minsn/s.
- Next: promotion needs runtime SSA/PHI state or a path-sensitive per-basic-block mask, plus helper-specific read/write summaries. Do not retry a single mutable generator mask.
