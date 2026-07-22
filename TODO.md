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

- [x] **Self-modifying code is not handled by the JITs.** Trace caches are now
      invalidated after code-writing `Fx33`/`Fx55` operations. Those operations
      terminate their current trace, and both JITs release compiled resources,
      clear their caches, and re-decode from the updated memory before running
      further code. This uses conservative full-cache invalidation rather than
      range tracking; it is correct, though less efficient than selective
      invalidation.
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
- [x] **Failed codegen leaks and is cached as `errer`.** Unknown opcodes now
      fail code generation explicitly in both JITs, release their temporary
      compiler state, and return `NULL` instead of caching the host `errer`
      function as executable trace code. The run loops report the failure and
      stop without installing a crash-on-execute stub.
- [x] **`Fx0A` (`load_on_key`) quit path leaves the PC on the waiting
      instruction.** When quit is pressed during a blocked key wait, all three
      engines now report quit through the same `ERROR` path, keeping the PC on
      the waiting instruction and aligning diagnostic/exit behavior for
      cross-checking.

## Semantics / quirks

- [x] **`VF`-as-destination ordering.** For `8xy4/5/6/7/E` the flag is written to
      `VF` and then the result to `Vx`, so when `x == 0xF` the result clobbers the
      just-written flag. ~~Preserved identically across all three engines; confirm
      this is the intended behavior.~~ *Now aligned (snapshot semantics).* The
      interpreter previously diverged from both JITs for `x == 0xF` because it
      lacked the JITs' operand snapshot: it wrote the flag to `VF`, then
      `regs[x] -= regs[y]` (etc.) re-read the just-clobbered `VF`. The JITs
      snapshot `Vx`/`Vy` into SSA temporaries before either store, so the result
      is computed from the original operands and clobbers the flag.
      *Fixed.* Snapshot the operands into locals in the four affected
      interpreter opcodes -- `sub_register` (`8xy5`), `subn_register` (`8xy7`),
      `shift_right` (`8xy6`), `shift_left` (`8xyE`) -- before the flag write, and
      compute the result from the locals. (`add_register`/`8xy4` was already
      snapshot-equivalent via its precomputed `tmp`.) The store order is now
      flag-first in all three engines and all snapshot their operands, so
      `x == 0xF` uniformly yields "result wins." The change is a no-op for
      `x != 0xF`. Verified dynamically: the interpreter's `8F15/8F17/8F16/8F1E`
      results changed from `FE/03/00/02` to `02/FE/02/00`, now matching both
      JITs; the normal `8015` case and PONG/TETRIS/TANK/BRIX run clean.

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
- [x] **LLVM `8xy5` writes result before flag (divergence).** In
      `llvm_jit.cpp`, `sub_register` stores the difference to `Vx` *first* and
      the borrow flag to `VF` *second* -- the opposite order from the
      libgccjit backend (and from LLVM's own sibling opcodes `8xy4/6/7/E`,
      which are already flag-first). When `x == 0xF`, the LLVM engine leaves
      the flag in `VF` while libgccjit leaves the result.
      *Fixed.* Reordered the two `CreateStore`s in `case 0x5` so the flag
      store precedes the result store, mirroring `case 0x4`. The comparison's
      operands are SSA-loaded before either store, so the reorder only changes
      which value lands last in `VF` when `x == 0xF`; for `x != 0xF` it is a
      no-op. Verified dynamically with a 3-instruction ROM
      (`6F05 6103 8F15` + spin): LLVM went `VF=0x01`->`0x02`, now matching
      libgccjit (`0x02`); the normal case `8015` is unchanged across all three
      engines (`V0=0x02, VF=0x01`), and PONG runs/exits clean under LLVM.
      **Caveat discovered by the dynamic test:** the interpreter gives
      `VF=0xFE` on the `8F15` case, not `0x02`, because it lacks the JITs'
      operand snapshot -- it writes the flag to `VF`, then `regs[x] -= regs[y]`
      re-reads the just-clobbered `VF`. So the original item's claim that the
      interpreter and libgccjit "agree" was true only for `x != 0xF`; for
      `x == 0xF` the interpreter diverges from *both* JITs. That is a
      snapshot-vs-read-modify-write difference, orthogonal to store order, and
      is recorded under the `VF`-as-destination quirk below, where it has since
      been fixed (interpreter now snapshots its operands).
- [x] **`fopen` result is never checked.** All four `main`s call
      `fopen(argv[1], "r")` and pass the result straight to `fread` --
      a missing ROM path segfaults before any diagnostic. Check for `NULL`
      and print a usable error. (Related to, but distinct from, the existing
      "ignored `fread` result" item: `libgccjit_jit.c` checks `fread` but
      still not `fopen`.) Also open with `"rb"` for portability.
      *Fixed.* All four `main`s (`interp.c`, `llvm_jit.cpp`, `libgccjit_jit.c`,
      `disas.c`) now check `fp == NULL` right after `fopen`, printing
      `Could not open ROM <path>` and exiting non-zero before `fread` runs.
      The mode is also already `"rb"` everywhere, so the portability sub-point
      is satisfied too. The `libgccjit_jit.c` caveat no longer holds: it now
      checks `fopen` in addition to `fread`.
- [x] **`disas.c` error format string is malformed.** `#define ERROR
      fprintf(stdout, "op code %0x04X\n", op)` -- `%0x` consumes `op` and the
      `04X` prints literally (e.g. `op code ab04X`). Should be `%04X`. The
      unknown-opcode path also doesn't print the PC.
      *Fixed.* `#define ERROR` now reads
      `fprintf(stdout, "op code %04X at pc 0x%04X\n", op, program_counter)`:
      the `%0x04X` typo is corrected to `%04X`, and the unknown-opcode path
      prints the PC alongside the opcode.

- [x] **`8xy5/8xy7` borrow flag on equality.** All three engines set
      `VF = (Vx > Vy)` (strict), so `Vx == Vy` yields `VF = 0`. The
      conventional semantics is "VF = NOT borrow", i.e. `Vx >= Vy` sets
      `VF = 1`. Most test ROMs (e.g. the Timendus quirks suite) expect the
      `>=` behavior; verify against one and fix or document.
      *Fixed.* Changed the interpreter comparisons to `>=`, LLVM's unsigned
      predicates from `ICMP_UGT` to `ICMP_UGE`, and libgccjit's comparisons
      from `GCC_JIT_COMPARISON_GT` to `GCC_JIT_COMPARISON_GE`, for both
      `8xy5` and `8xy7`. Equality micro-ROMs (`roms/eq_sub.ch8` and
      `roms/eq_subn.ch8`) now yield `V0=0x00, VF=0x01` on all three engines.
      The Timendus `4-flags.ch8` screen and final machine state also agree
      across all three backends; only their expected instruction-vs-trace
      execution-counter line differs.
- [ ] **`Dxyn` wraps sprites instead of clipping.** `draw_io` applies
      `% width` / `% height` per pixel, so sprites that run off the right or
      bottom edge wrap around. The common quirk expectation is: wrap the
      *starting* coordinate, then clip the sprite at the edges. Decide which
      behavior is intended and note it in the README.
- [ ] **`8xy6/8xyE` ignore `Vy`.** The interpreter has the `Vy` variants
      commented out and both JITs shift `Vx` in place (the "modern"/SCHIP
      quirk). Fine as a choice, but it's undocumented; add a quirks section
      to the README so ROM incompatibilities are explainable.

- [x] **Header dependencies aren't tracked.** ~~The pattern rule
      `%.o: %.c %.h` only fires when a same-named header exists
      (`chip8.o`, `ncurses_io.o` -- and the latter matches `io.h` not at all:
      `ncurses_io.h` doesn't exist, so it falls back to the built-in rule).
      `interp.o`, `disas.o`, and the JIT objects don't rebuild when `chip8.h`
      or `io.h` change, which is exactly the kind of stale-build that hides
      cross-engine divergence. Use `-MMD -MP` generated deps or list headers
      explicitly.~~ *Fixed.* Added `DEPFLAGS = -MMD -MP` to `Makefile`,
      appended it to `CFLAGS`/`CXXFLAGS` so each compile emits a `*.d` file,
      removed the broken `%.o: %.c %.h` pattern rule (now using the built-in
      implicit rule with dep flags), and added `-include $(wildcard *.d)` at
      the bottom plus `*.d` to `clean`. Verified: `touch chip8.h && make -n`
      now rebuilds `chip8.o`, `interp.o`, `llvm_jit.o`, `libgccjit_jit.o`;
      `touch io.h` rebuilds `ncurses_io.o` and dependents. Full rebuild
      clean under both plain and libgccjit-flagged invocations.
- [x] **Dead/unused includes and warnings.** ~~`arpa/inet.h` and `signal.h` are
      included in all engines but unused (likely leftovers from an
      `htons`-based fetch). `io.h` uses `uint8_t`/`uint32_t` without including
      `<stdint.h>` itself, so it only compiles because every includer happens
      to pull `chip8.h` first. Compiling with `-Wextra` also flags a few
      sign-comparison nits worth cleaning.~~ *Fixed.* Removed dead
      `arpa/inet.h` from `interp.c`, `llvm_jit.cpp`, `libgccjit_jit.c`,
      `disas.c`; removed dead `signal.h` from `interp.c` (kept in both JITs
      where `sigaction(SIGALRM)` is actually used). Added `#include <stdint.h>`
      to `io.h` so it is self-contained (`echo '#include "io.h"' | gcc -c`
      now succeeds). Fixed `-Wimplicit-fallthrough` in `interp.c`, `disas.c`,
      `llvm_jit.cpp` by restructuring the outer `Fx` fallthrough to `ERROR` and
      annotating intentional inner-switch fallthroughs with
      `__attribute__((fallthrough))`. Verified `make` clean with `-Wall -Wextra`
      on project files (LLVM header noise excluded) and full 4-target rebuild
      with libgccjit flags.

## Cosmetics / consistency

- [x] **Register dump prints decimal indices.** `V%02d` in every engine's exit
      dump printed `V10`..`V15` instead of `VA`..`VF`; `disas.c` similarly
      printed `V%d`. *Fixed.* The shared exit-state dump and all disassembler
      register formats now use uppercase hexadecimal (`V%X`), matching CHIP-8
      register names.
- [x] **Exit dumps differ across engines.** The libgccjit backend omitted the
      `stack[...]` line the other two printed; the interpreter's
      `stack[stack_pointer]` also read one slot past the top of stack
      (index equals the count, not the last pushed entry). *Fixed.* A shared
      `dump_chip8_state` routine now supplies all three backends' state output.
      It prints the stack depth and, only for a nonempty stack, its top entry
      at `stack[stack_pointer - 1]`, avoiding both the empty-stack fiction and
      the full-stack out-of-bounds read.
