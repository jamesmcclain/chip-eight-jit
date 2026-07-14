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

- [x] **Ignored `fread` result.** `interp.c` and `llvm_jit.cpp` ignore the return
      value of `fread` when loading the ROM (the compiler warns); short reads and
      missing ROMs go undetected. `libgccjit_jit.c` now checks it.
      *Fixed.* Added the same `== 0` check and `Could not read ROM` diagnostic
      that `libgccjit_jit.c` already used to `interp.c`, `llvm_jit.cpp`, and
      `disas.c` (which had the identical ignored-result warning). All four
      backends now exit non-zero on a zero-length read instead of running on
      an empty memory image. Note: a *missing* ROM path still segfaults before
      `fread` runs because `fopen` returns `NULL` unchecked -- that is the
      separate "`fopen` result is never checked" item below and is out of
      scope here.

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

## Correctness

- [x] **`clear_key` erases the quit bit (and all high bits).** In all three
      engines, `clear_key` does `keys_down[i] &= (0xffff ^ (1<<key))`. For any
      `key < 16` the mask's upper 16 bits are zero, so the AND wipes bits
      16-31 of every slot -- including the `1<<31` "quit" flag. Pressing a
      CHIP-8 key that gets consumed by `Ex9E/ExA1/Fx0A` can silently cancel a
      pending `q`/Escape. The mask should be `~(1u << key)`.
      *Fixed.* Changed the mask to `~(1u << key)` in all three engines
      (`interp.c`, `llvm_jit.cpp`, `libgccjit_jit.c`). The new mask has its
      upper bits set, so only the target key bit is cleared and the `1<<31`
      quit flag (and every other high bit) is preserved across key-consume
      paths. Verified by rebuild: all four targets compile clean.
- [x] **Timers are `int8_t` and can get stuck.** ~~`delay_timer`/`sound_timer`
      are signed 8-bit, but `Fx15/Fx18` load them from a full 8-bit register.
      Setting a timer to any value > 127 stores a negative number; `interrupt()`
      only decrements when `> 0`, so the timer never counts down and a ROM
      spin-waiting on `Fx07 == 0` hangs forever. Make the timers `uint8_t`
      (and adjust the `> 0` checks / stderr dump accordingly).~~ *Fixed.*
      Changed `delay_timer`/`sound_timer` from `int8_t` to `uint8_t` in the
      `extern` declarations (`chip8.h`) and all three definitions (`interp.c`,
      `llvm_jit.cpp`, `libgccjit_jit.c`), so `Fx15/Fx18` values in 128..255 are
      stored as-is instead of wrapping negative. The `interrupt()` `> 0` guards
      stay correct and warning-free under unsigned (0 is the only non-decrement
      case), and the `stderr` dumps (`%d`, via integer promotion) now print the
      true 0..255 value rather than a spurious negative. The `Fx07` read paths
      were already byte-verbatim copies into the 8-bit register, so no IR
      change was needed in the JITs. Verified with a decrement model: an
      `int8_t` timer set to 200 never reaches 0 (spin-wait hangs); the
      `uint8_t` timer counts down to 0 in 200 ticks.
- [x] **Unbounded `addr` allows out-of-bounds memory access.** ~~`Annn` sets
      `addr` up to 0xFFF and `Fx1E` can push it to 0xFFFE, but `store_bcd`,
      `save_registers`, `restore_registers`, and `draw` index `memory[addr+i]`
      with no mask or bounds check (only opcode fetch uses `OPCODE_AT`'s
      masking). A hostile or buggy ROM can read/write past the 4 KiB `memory`
      array -- a real out-of-bounds write in the host process. Mask with
      `(MEMORY_SIZE - 1)` or clamp. Affects the interpreter and both JIT
      host-helper layers.~~ *Fixed.* Added a `MEM_AT(a)` macro in `chip8.h`
      that masks with `(MEMORY_SIZE - 1)`, mirroring the existing `OPCODE_AT`
      wrap and matching the COSMAC VIP's 12-bit address wrap (authentic, not a
      clamp). All four data sites now use it in the interpreter and both JIT
      host-helper layers: `store_bcd`, `save_registers`, `restore_registers`,
      and `draw` -- the last gathers the sprite rows into a small local buffer
      with masked indices before handing a pointer to `draw_io`, since the read
      loop lives inside `draw_io`. Verified with AddressSanitizer: crafted ROMs
      that push `addr` to 0x0FFF/0x0FFE before `Fx55`/`Dxyn` report a
      global-buffer-overflow write/read on the unfixed build and are clean
      after; a `Fx65` wrap test confirms the read lands at `memory[0]` (wrap,
      not drop). Both JIT binaries execute the masked `Fx55` path without
      faulting.
- [ ] **LLVM `8xy5` writes result before flag (divergence).** In
      `llvm_jit.cpp`, `sub_register` stores the difference to `Vx` *first* and
      the borrow flag to `VF` *second* -- the opposite order from the
      interpreter and the libgccjit backend. When `x == 0xF`, the LLVM engine
      leaves the flag in `VF` while the other two leave the result. This
      contradicts the "preserved identically across all three engines" claim
      in the `VF`-as-destination item above; pick one order and align all
      three.
- [ ] **`fopen` result is never checked.** All four `main`s call
      `fopen(argv[1], "r")` and pass the result straight to `fread` --
      a missing ROM path segfaults before any diagnostic. Check for `NULL`
      and print a usable error. (Related to, but distinct from, the existing
      "ignored `fread` result" item: `libgccjit_jit.c` checks `fread` but
      still not `fopen`.) Also open with `"rb"` for portability.
- [ ] **`disas.c` error format string is malformed.** `#define ERROR
      fprintf(stdout, "op code %0x04X\n", op)` -- `%0x` consumes `op` and the
      `04X` prints literally (e.g. `op code ab04X`). Should be `%04X`. The
      unknown-opcode path also doesn't print the PC.

## Semantics / quirks

- [ ] **`8xy5/8xy7` borrow flag on equality.** All three engines set
      `VF = (Vx > Vy)` (strict), so `Vx == Vy` yields `VF = 0`. The
      conventional semantics is "VF = NOT borrow", i.e. `Vx >= Vy` sets
      `VF = 1`. Most test ROMs (e.g. the Timendus quirks suite) expect the
      `>=` behavior; verify against one and fix or document.
- [ ] **`Dxyn` wraps sprites instead of clipping.** `draw_io` applies
      `% width` / `% height` per pixel, so sprites that run off the right or
      bottom edge wrap around. The common quirk expectation is: wrap the
      *starting* coordinate, then clip the sprite at the edges. Decide which
      behavior is intended and note it in the README.
- [ ] **`8xy6/8xyE` ignore `Vy`.** The interpreter has the `Vy` variants
      commented out and both JITs shift `Vx` in place (the "modern"/SCHIP
      quirk). Fine as a choice, but it's undocumented; add a quirks section
      to the README so ROM incompatibilities are explainable.

## JIT robustness

- [ ] **Failed codegen leaks and is cached as `errer`.** On an unknown opcode,
      `llvm_jit.cpp`'s `codegen` returns the host `errer` function mid-build,
      abandoning the partially built module/context (the libgccjit backend at
      least releases its context in `BAIL_ERRER`). In both backends the
      returned `errer` pointer is then stored in the trace cache as if it were
      a compiled trace. Consider failing more loudly at compile time instead
      of deferring to a cached crash-on-execute stub.
- [ ] **`Fx0A` (`load_on_key`) quit path leaves the PC on the waiting
      instruction.** When quit is pressed during a blocked key wait, both JIT
      helpers set `program_over` without stepping the PC, while the
      interpreter reports it via `ERROR` with a different exit status/message.
      Harmless, but the three engines' final register/PC dumps differ for the
      same input; worth unifying if the dumps are used for cross-checking.

## Build hygiene

- [ ] **Header dependencies aren't tracked.** The pattern rule
      `%.o: %.c %.h` only fires when a same-named header exists
      (`chip8.o`, `ncurses_io.o` -- and the latter matches `io.h` not at all:
      `ncurses_io.h` doesn't exist, so it falls back to the built-in rule).
      `interp.o`, `disas.o`, and the JIT objects don't rebuild when `chip8.h`
      or `io.h` change, which is exactly the kind of stale-build that hides
      cross-engine divergence. Use `-MMD -MP` generated deps or list headers
      explicitly.
- [ ] **Dead/unused includes and warnings.** `arpa/inet.h` and `signal.h` are
      included in all engines but unused (likely leftovers from an
      `htons`-based fetch). `io.h` uses `uint8_t`/`uint32_t` without including
      `<stdint.h>` itself, so it only compiles because every includer happens
      to pull `chip8.h` first. Compiling with `-Wextra` also flags a few
      sign-comparison nits worth cleaning.

## Cosmetics / consistency

- [ ] **Register dump prints decimal indices.** `V%02d` in every engine's exit
      dump prints `V10`..`V15` instead of `VA`..`VF`; `disas.c` similarly
      prints `V%d`. Hex register names would match every CHIP-8 reference.
- [ ] **Exit dumps differ across engines.** The libgccjit backend omits the
      `stack[...]` line the other two print; the interpreter's
      `stack[stack_pointer]` also reads one slot past the top of stack
      (index equals the count, not the last pushed entry).
