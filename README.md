# CHIP-8

A small CHIP-8 emulator. It has several interchangeable execution backends and a disassembler. The VM state and terminal I/O are shared. Each backend executes the loaded ROM in a different way.

## Backends

| Target            | Description                          |
| ----------------- | ------------------------------------ |
| `chip8-interp`    | Tree-walking interpreter.            |
| `chip8-llvm`      | Trace JIT built on LLVM ORC.         |
| `chip8-libgccjit` | Trace JIT built on libgccjit.        |
| `chip8-disas`     | Static disassembler.                 |
| `chip8-asm`       | Two-pass CHIP-8 assembler.           |

Both JITs compile one native function per entry PC. The JIT caches the function. The JIT extends traces across unconditional jumps and the taken side of skips. Awkward opcodes call shared C helper routines. These opcodes include control flow, I/O, and blocking input.

A jump to an already compiled address closes as a branch to that code. A CHIP-8 loop runs as a native loop. The loop does not cost a dispatcher round trip or a trace-cache lookup per iteration. Each closed back-edge carries a safepoint. The safepoint tests `program_over`. The safepoint detects the end of the run, an error, and the quit key.

CHIP-8 keeps code and data in one 4 KiB address space. The stores (`Fx33`, `Fx55`) can modify their own code. A trace compiled from overwritten bytes is stale. Each backend records the bytes the code generator read. A store raises the invalidation flag only if it lands on one of those bytes. The dispatcher then discards the cache on the next trip. Most ROMs store a BCD score or spill registers to a scratch buffer. They never touch code. The difference is recompiling at frame rate or not recompiling at all.

Timers and input run asynchronously in the JIT backends. A POSIX interval timer raises `SIGALRM` several hundred times a second. Its handler sets a flag. Compiled traces contain lightweight safepoints at jump back-edges and every 32 straight-line instructions. The safepoint is a volatile load plus a conditional call. These safepoints poll the keyboard and decrement the 60 Hz timers. This design bounds input latency.

## Build

Install `gcc`/`g++`, LLVM 20 development files (`llvm-config-20`), libgccjit (`libgccjit-*-dev`), and ncurses.

The libgccjit dev package installs its header and link stub under a GCC-version-specific directory. This directory is not on the compiler's default search path. The Makefile does not add it. You must point `CPPFLAGS` and `LDFLAGS` at that directory on Debian/Ubuntu. Use this command to invoke `make -C src`:

```sh
gccjit_dir=$(dirname $(find /usr/lib/gcc -name libgccjit.so 2>/dev/null | head -1)); CPPFLAGS="-I$gccjit_dir/include" LDFLAGS="-L$gccjit_dir" make -C src
```

You do not need libgccjit for the interpreter, LLVM, disassembler, or assembler. A plain `make chip8-interp` will work.

```sh
cd src
make                 # builds all five targets (needs the libgccjit flags above)
make chip8-interp    # or build a single target
make chip8-asm
```

Override the LLVM config binary if needed. For example, use `make LLVM_CONFIG=llvm-config`.

## Usage

```sh
./chip8-interp    path/to/rom.ch8
./chip8-llvm      path/to/rom.ch8
./chip8-libgccjit path/to/rom.ch8
```

### AOT LLVM IR

`chip8-aot` converts an immutable CHIP-8 ROM into a textual LLVM module. The module contains a ROM-specific program-counter dispatcher. It links to a runtime for VM state, timers, input, and ncurses display output.

```sh
make -C src chip8-aot aot-runtime-objs
src/chip8-aot roms/PONG -o pong.ll
llvm-as-20 pong.ll -o pong.bc
clang pong.ll src/aot_runtime.o src/interp.runtime.o src/chip8.o src/ncurses_io.o -lncurses -o pong
./pong
```

The compiler rejects empty, odd-byte, and oversized ROMs. The runtime verifies that each opcode still matches the opcode embedded in `pong.ll`. It stops if code memory changed. Do not submit self-modifying ROMs.

The emulator renders the display with ncurses (64x32). The 16 CHIP-8 keys map to `0`-`9` and `a`-`f`. Press `q` or `Escape` to quit. On exit the emulator writes the register file, program counter, address register, and timers to stderr.

## Benchmarking and differential testing

An ordinary run is not reproducible. The RNG uses the clock as seed. The 60 Hz timers follow `CLOCK_MONOTONIC`. The JITs use a `SIGALRM` interval timer. Input arrives when the terminal delivers it. No ROM terminates. Two runs never agree. You cannot compare timings or final state.

`make bench` builds a second copy of each engine with `-DBENCH`. This flag replaces every input with a reproducible value. It uses a fixed seed, a virtual 60 Hz clock driven by retired instructions, a synthetic keyboard derived from that clock, and headless I/O. The same framebuffer and collision semantics apply. The engine stops after a set number of instructions.

```sh
make -C src bench            # needs the libgccjit flags above
./src/chip8-llvm-bench roms/PONG --instructions 5000000
./src/chip8-llvm-bench roms/PONG --instructions 5000000 --seed 7 --keys none
```

The stderr report adds `retired` (architectural CHIP-8 instructions). It adds `compiled` and `flushes` (JIT compile pressure and self-modifying write discards). It adds a `display` hash of the framebuffer. It adds elapsed time and a `rate` in retired instructions per second.

`scripts/bench_diff.py` runs the engines against each other:

```sh
python3 scripts/bench_diff.py --instructions 1000000          # all stock ROMs
python3 scripts/bench_diff.py --engines llvm roms/BLINKY
```

A JIT can not stop on an exact instruction count. A trace notices the budget only at a safepoint. The script reads each JIT's actual retired count. It re-runs the interpreter to that exact count. Both engines execute the same instruction sequence from the same state. Every register, timer, PC, and display hash must agree exactly. Any difference is a real divergence.

Preserve two properties when you change a JIT. Timer values must depend only on the virtual clock. Every timer access calls `sync_timers()` because of this requirement. `bench_retired` counts architectural instructions, not emitted operations. A peephole that folds several opcodes into one native sequence must count all of them. The virtual clock stays aligned with the interpreter.

## Assembling ROMs

`chip8-asm` reads source from a file. It writes the raw ROM payload to standard output. ROM addresses start at `0x200`. The output contains bytes from that address onward.

```sh
./chip8-asm program.asm > program.ch8
./chip8-interp program.ch8
```

The assembler accepts these case-insensitive CHIP-8 mnemonics: `CLS`, `RET`, `JP`, `CALL`, `SE`, `SNE`, `LD`, `ADD`, `OR`, `AND`. It also accepts `XOR`, `SUB`, `SHR`, `SUBN`, `SHL`, `RND`, `DRW`, `SKP`, `SKNP`. `SHR` and `SHL` can include an optional second register operand. This operand preserves the encoded `y` nibble when you round-trip a ROM. Write registers as `V0` through `VF`. Numeric literals use decimal or C-style prefixes such as `0xFF`. `LD` supports the standard special operands `I`, `DT`, `ST`, `K`, `F`, `B`, and `[I]`. For example: `LD B, V3` and `LD V3, [I]`.

Labels end with `:`. You can use a label before or after its definition. `;` and `#` start comments. The data directives are `.byte`, `.word` (big-endian), and `.org`. `.org` moves forward within `0x200` through `0xFFF`. It fills the intervening output with zeroes. Example:

```asm
; clear, then spin forever
start:
    CLS
    LD V0, 0x2A
loop:
    ADD V0, 1
    JP loop

sprite: .byte 0xF0, 0x90, 0xF0, 0x90, 0x90
```

The disassembler uses human-readable prose output by default. Pass `--asm` (or `-a`) to emit assembler source for `chip8-asm`. Addresses are omitted. Unrecognized opcodes are preserved as `.word` directives.

```sh
./chip8-disas --asm program.ch8 > program.asm
./chip8-asm program.asm > rebuilt.ch8
```

## Testing

Two helper scripts in `scripts/` drive the engines under a pty. They capture results. ROMs do not terminate on their own. The display uses ncurses. Both scripts send `q` to quit. They send SIGKILL to the child as a safety net. The tool always returns. Run the assembler's byte-level regression suite with:

```sh
make -C src test
```

- **`run_dump.py <engine> <rom>`** captures the stderr state dump (registers, PC, `I`, timers). It discards the ncurses screen. Use it for deterministic cross-engine comparisons of final machine state.

- **`tools/chip8opt.py`** is the conservative front-end for the planned optimizer. It analyzes assembler-compatible source. It canonicalizes the source without changing its assembled bytes:

  ```sh
  src/chip8-disas --asm roms/PONG > pong.asm
  python3 tools/chip8opt.py analyze --json pong.asm
  python3 tools/chip8opt.py canonicalize pong.asm -o pong.canonical.asm
  src/chip8-asm pong.canonical.asm > pong.ch8
  # `optimize` deletes bytes only from safely relocatable, symbolic source.
  python3 tools/chip8opt.py optimize source.asm -o compact.asm
  ```

  The analysis reports reachable instructions, CFG leaders/edges, declared data, and statically known `I`-relative reads/writes. A hazard is a refusal to prove safety. It is not evidence that the ROM is broken. Computed `JP V0`, dynamic `I`, and writes into the ROM payload remain for later optimizer passes. `optimize` currently implements byte-removing peepholes. These peepholes remove no-ops, dead pure loads, and `LD`/`ADD` constant folding. Direct numeric in-ROM `JP`, `CALL`, and `LD I` operands convert to generated labels before compaction. Their targets relocate safely. The optimizer refuses fixed-layout source (`.ORG`) and hazards whose target can not relocate safely.

- **`screen_dump.py <engine> <rom> [--keys KEYS] [--ticks N | --hold SECS]`** captures stdout. It replays the output through a small vt100 renderer. The CHIP-8 font glyphs land at their real screen positions. It prints the readable screen and the stderr state dump. Use it to read a test ROM's on-screen verdict. The Timendus suite uses `OK`/`FAIL`. `--keys` feeds CHIP-8 keypresses to dismiss a splash screen. `--ticks N` waits `N/60` s of settle time. This rate matches the 60 Hz timer for frame-drawing ROMs. Use `--hold` for spin loops.

```sh
python3 scripts/run_dump.py src/chip8-interp roms/eq_sub.ch8          # register dump only
python3 scripts/screen_dump.py src/chip8-interp roms/test-roms/4-flags.ch8 --keys 0 --ticks 180
```

## Quirks / Compatibility Notes

CHIP-8 has several well-known compatibility splits. This emulator makes explicit choices for interoperability with modern test ROMs. The Timendus suite drives these choices. All three execution backends agree on these rules:

| Opcode / area | Choice | Notes |
|---|---|---|
| `8xy6` / `8xyE` (shift) | **Modern / SCHIP**: shift `Vx` in place, ignore `Vy` | Original COSMAC VIP copied `Vy` into `Vx` then shifted (`regs[x]=regs[y]; regs[x]>>=1`). The interpreter comment `/* Y; */` and both JIT backends intentionally implement the modern quirk. This is a choice, not a bug. |
| `Dxyn` (draw) | **Wrap-start + clip**: `x %= 64`, `y %= 32`, then clip sprite pixels that go off the right or bottom edge | Old behavior wrapped every pixel (`x2=(x+i)%W, y2=(y+j)%H`). Quirks suite `5-quirks` expects clipping. The fix is in `draw_io` shared by all backends. |
| `8xy4/5/6/7/E` with `x==F` | **Snapshot then result-then-flag**: operands snapshotted, result stored to `Vx`, `VF` stored last so flag wins | Required by `4-flags.ch8`. |
| `8xy5/8xy7` borrow | **NOT-borrow**: `VF = Vx >= Vy` (or `Vy >= Vx`) | Many ROMs expect `>=`, not strict `>`. |
| `Fx55/Fx65`, `Fx33`, `Dxyn` + `Annn`/`Fx1E` | **12-bit wrap via `MEM_AT`**: address masked with `0xFFF` | COSMAC VIP 12-bit wrap. This rule also prevents OOB host access. |
| `Bnnn` | `PC = nnn + V0` with zero-extend | LLVM backend previously failed to extend `V0`. |
| Timers | `uint8_t`, 0..255, decrement only when >0 | Signed timers previously hung on values >127. |

## Layout

All source lives in `src/`. Read `LICENSE.md` for licensing (BSD-style, (c) 2021 James McClain).
