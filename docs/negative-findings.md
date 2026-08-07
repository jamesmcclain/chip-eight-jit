# LLVM Register-Promotion Negative Findings

## Scope

These experiments affect only `src/llvm_jit.cpp`. Correctness was gated by
`scripts/bench_diff.py --engines llvm` on PONG, TETRIS, and BLINKY at one
million retired instructions.

## Baseline

The LLVM benchmark geometric mean was approximately 50 Minsn/s. The host has
substantial measurement noise, so results below are directional rather than
fine-grained comparisons.

## 1. Promote all V0-VF through trace-local allocas

**Result:** rejected.

Sixteen `i8` allocas were initialized at trace entry and used for all inline
register accesses. LLVM O2 can promote those non-escaping allocas to SSA. The
implementation spilled all registers before VM-observing helpers and trace
returns, then reloaded them after helpers.

- Differential tests passed.
- Throughput fell to about 26 Minsn/s.
- Excluding known register-free helpers from barriers improved this only to
  about 32 Minsn/s.

The trace-exit and helper barriers cost more than the register memory traffic
they eliminate.

## 2. Inspect optimized IR before trying more promotion

**Result:** retained as diagnostic infrastructure.

`LLVM_JIT_DUMP_IR=<pc|*>` prints a selected generated trace before and after
O2. The PONG `ADDR212` hot trace had only four `i8` loads and eight `i8` stores
after O2. It also had an `i64` load/add/store for every retired CHIP-8
instruction in the BENCH configuration.

This confirms that the benchmark has limited removable register traffic and
that trace-local register promotion cannot amortize a broad exit spill.

## 3. Generator-time dirty-register mask

**Result:** rejected.

A second implementation marked a register dirty when inline code stored it,
then spilled only dirty slots at boundaries.

- The first form reset the mask after a helper reload. It diverged on TETRIS:
  generator-time metadata from one generated branch was incorrectly reused at
  a control-flow join.
- Marking every register dirty after helper reload restored correctness but
  measured about 40 Minsn/s, still below baseline.

A single mutable mask in the code generator is not path-sensitive. It cannot
model control-flow joins or helper effects safely.

## Do not retry

Do not retry whole-file alloca promotion or a single generator-side dirty mask.
Both are structurally unprofitable or unsound in this trace compiler.

## Remaining credible direction

The only interesting register-promotion follow-up is substantially larger:
represent V-register state as runtime SSA values per generated basic block,
merge it at joins with PHIs, and attach explicit per-helper read/write
summaries. Spill only registers required by that helper or trace exit. This is
a compiler refactor, not a small autoresearch iteration. It should begin with
an IR-level proof that selected trace exits actually remove enough memory
operations to pay for any required spills.
