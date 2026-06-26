# TODO / Known Issues

Things noticed while modernizing the JIT backends that have **not** yet been
addressed. Items already fixed (LLVM 20 API port, shift-left flag, carry
sign-extension, the call-stack off-by-one, the `$(C)` Makefile typo) are
deliberately omitted.

## Correctness

- [x] **LLVM `Bnnn` type mismatch.** `llvm_jit.cpp` (case `0xb`) adds the 16-bit
      immediate to the 8-bit `V0` value directly, producing a wrong (often odd)
      jump target. The libgccjit backend zero-extends `V0` first and is correct;
      port that fix back to the LLVM backend. Reproduces with any ROM that uses
      `Bnnn`.
- [x] **Odd program counters are misread.** Opcode fetch is
      `((uint16_t *)memory)[pc >> 1]`, which assumes 2-byte alignment. A jump or
      call to an odd address silently fetches a misaligned opcode instead of
      faulting. Affects all backends.

## JIT robustness

- [ ] **Self-modifying code is not handled by the JITs.** Traces are cached by
      entry PC and never invalidated, so writes into a code region (e.g. via
      `Fx55`) are not reflected in already-compiled traces. The interpreter
      handles SMC correctly because it re-decodes every instruction.
- [x] **Compiled code is intentionally leaked.** ~~LLVM modules and libgccjit
      `gcc_jit_result`s are never released and there is no JIT teardown. Fine for
      short runs; grows unbounded for long-lived or SMC-heavy programs.~~
      *Fixed.* Each backend now tracks the resources behind every compiled trace
      (an ORC `ResourceTracker` per IR module in `llvm_jit.cpp`, the owning
      `gcc_jit_result` in `libgccjit_jit.c`) and releases them in a `teardown_jit`
      step on the normal exit path, which also drops the LLJIT itself. The run
      loop replaces its `exit(0)` with teardown + `return 0` so the teardown
      actually runs. This bounds the steady-state leak to whatever traces are
      currently cached; reclaiming superseded traces during a run is the separate
      SMC item above.

## Semantics / quirks

- [ ] **`VF`-as-destination ordering.** For `8xy4/5/6/7/E` the flag is written to
      `VF` and then the result to `Vx`, so when `x == 0xF` the result clobbers the
      just-written flag. Preserved identically across all three engines; confirm
      this is the intended behavior.

## Build hygiene

- [ ] **Ignored `fread` result.** `interp.c` and `llvm_jit.cpp` ignore the return
      value of `fread` when loading the ROM (the compiler warns); short reads and
      missing ROMs go undetected. `libgccjit_jit.c` now checks it.

## Maintainability

- [x] **Triplicated opcode semantics.** The per-opcode behavior lives in three
      places: `interp.c`, and the host-helper layers of `llvm_jit.cpp` and
      `libgccjit_jit.c`. Behavioral fixes must be made in all three (this is how
      the shift-left and call bugs survived into the libgccjit scaffold).
      *Decision: won't fix.* Checked off as deliberately accepted, not resolved.
      The three layers are not actually interchangeable -- the interpreter walks
      one instruction at a time, while the JIT host helpers exist precisely
      because those opcodes are awkward to express as inline IR and re-read their
      own opcode from memory under a different control-flow contract. Hoisting
      them into a single shared helper set would couple the calling conventions
      of three engines for the handful of opcodes that already have the clearest,
      most localized semantics, and the straight-line opcodes (the ones most
      prone to silent divergence) can't be shared at all because each backend
      emits them as native IR rather than calls. The maintenance cost is a small,
      well-understood set of helpers; we accept it rather than take on a riskier
      abstraction. Revisit only if a fourth backend appears or the helper set
      grows substantially.
