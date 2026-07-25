# CHIP-8

A small CHIP-8 emulator with several interchangeable execution backends and a
disassembler. The VM state and terminal I/O are shared; each backend executes
the loaded ROM a different way.

## Backends

| Target            | Description                          |
| ----------------- | ------------------------------------ |
| `chip8-interp`    | Tree-walking interpreter.            |
| `chip8-llvm`      | Trace JIT built on LLVM ORC.         |
| `chip8-libgccjit` | Trace JIT built on libgccjit.        |
| `chip8-disas`     | Static disassembler.                 |
| `chip8-asm`       | Two-pass CHIP-8 assembler.           |

Both JITs compile one native function per entry PC, cache it, and extend traces
across unconditional jumps and the taken side of skips; awkward opcodes (control
flow, I/O, blocking input) are emitted as calls to shared C helper routines.

Timers and input in the JIT backends are driven asynchronously: a POSIX
interval timer raises `SIGALRM` several hundred times a second and its handler
sets a flag; compiled traces contain lightweight safepoints (a volatile load
plus a conditional call) at jump back-edges and every 32 straight-line
instructions, which service the flag by polling the keyboard and decrementing
the 60 Hz timers. This bounds input latency regardless of the shape of the
compiled code.

## Build

Requirements: `gcc`/`g++`, LLVM 20 development files (`llvm-config-20`),
libgccjit (`libgccjit-*-dev`), and ncurses.

The libgccjit dev package installs its header (`libgccjit.h`) and link stub
(`libgccjit.so`) under a GCC-version-specific directory such as
`/usr/lib/gcc/x86_64-linux-gnu/14/`, which is **not** on the compiler's default
search path. The Makefile does not add it automatically, so on Debian/Ubuntu you
must point `CPPFLAGS` and `LDFLAGS` at that directory. The one-liner to properly invoke `make -C src` (with libgccjit flags, including cleanest) is:

```sh
gccjit_dir=$(dirname $(find /usr/lib/gcc -name libgccjit.so 2>/dev/null | head -1)); CPPFLAGS="-I$gccjit_dir/include" LDFLAGS="-L$gccjit_dir" make -C src
```

If you only need the interpreter, LLVM, disassembler, or assembler target, the
libgccjit path is not required and a plain `make chip8-interp` (etc.) will work without it.

```sh
cd src
make                 # builds all five targets (needs the libgccjit flags above)
make chip8-interp    # or build a single target
make chip8-asm
```

Override the LLVM config binary if needed, e.g. `make LLVM_CONFIG=llvm-config`.

## Usage

```sh
./chip8-interp    path/to/rom.ch8
./chip8-llvm      path/to/rom.ch8
./chip8-libgccjit path/to/rom.ch8
```

The display is rendered with ncurses (64x32). The 16 CHIP-8 keys are mapped to
the hex keys `0`-`9` and `a`-`f`; press `q` or `Escape` to quit. On exit the
register file, program counter, address register, and timers are dumped to
stderr.

## Assembling ROMs

`chip8-asm` reads source from a file and writes the raw ROM payload to standard
output. ROM addresses start at `0x200`; the output contains bytes from that
address onward.

```sh
./chip8-asm program.asm > program.ch8
./chip8-interp program.ch8
```

The assembler accepts case-insensitive conventional CHIP-8 mnemonics:
`CLS`, `RET`, `JP`, `CALL`, `SE`, `SNE`, `LD`, `ADD`, `OR`, `AND`, `XOR`,
`SUB`, `SHR`, `SUBN`, `SHL`, `RND`, `DRW`, `SKP`, and `SKNP`. `SHR` and `SHL`
may include an optional second register operand to preserve its encoded `y`
nibble when round-tripping a ROM. Registers are
written `V0` through `VF`; numeric literals use decimal or C-style prefixes
such as `0xFF`. `LD` supports the standard special operands `I`, `DT`, `ST`,
`K`, `F`, `B`, and `[I]` (for example, `LD B, V3` and `LD V3, [I]`).

Labels end with `:` and may be used before or after their definition. `;` and
`#` start comments. The data directives are `.byte`, `.word` (big-endian), and
`.org`; `.org` can move forward within `0x200` through `0xFFF` and fills the
intervening output with zeroes. Example:

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

The disassembler retains its human-readable prose output by default. Pass
`--asm` (or `-a`) to emit assembler source accepted by `chip8-asm`; addresses
are omitted, and unrecognized opcodes are preserved as `.word` directives.

```sh
./chip8-disas --asm program.ch8 > program.asm
./chip8-asm program.asm > rebuilt.ch8
```

## Testing

Two helper scripts in `scripts/` drive the engines under a pty and capture
results, since ROMs do not terminate on their own and the display is
ncurses. Both send `q` to quit and SIGKILL the child as a safety net so the
tool always returns. The assembler's byte-level regression suite can be run with:

```sh
make -C src test
```

- **`run_dump.py <engine> <rom>`** captures the stderr state dump
  (registers, PC, `I`, timers) while discarding the ncurses screen. Use it
  for deterministic cross-engine comparisons of final machine state, e.g.
  after a crafted micro-ROM.

- **`tools/chip8opt.py`** is the deliberately conservative front-end for the
  planned optimizer.  It currently analyzes assembler-compatible source and
  canonicalizes it without changing its assembled bytes:

  ```sh
  src/chip8-disas --asm roms/PONG > pong.asm
  python3 tools/chip8opt.py analyze --json pong.asm
  python3 tools/chip8opt.py canonicalize pong.asm -o pong.canonical.asm
  src/chip8-asm pong.canonical.asm > pong.ch8
  # `optimize` deletes bytes only from safely relocatable, symbolic source.
  python3 tools/chip8opt.py optimize source.asm -o compact.asm
  ```

  Analysis reports reachable instructions, CFG leaders/edges, declared data,
  statically known `I`-relative reads/writes, and hazards.  A hazard is a
  refusal to prove safety—not evidence that the ROM is broken.  In particular,
  computed `JP V0`, dynamic `I`, and writes into the ROM payload are retained
  for later optimizer passes to gate on.  `optimize` currently implements
  byte-removing peepholes (no-op removal, dead pure loads, and `LD`/`ADD`
  constant folding).  Direct numeric in-ROM `JP`, `CALL`, and `LD I` operands
  are converted to generated labels before compaction, allowing their targets
  to relocate safely.  It still refuses fixed-layout source (`.ORG`) and
  hazards whose target cannot yet be relocated safely.

- **`screen_dump.py <engine> <rom> [--keys KEYS] [--ticks N | --hold SECS]`**
  is the complement: it captures stdout, replays it through a small vt100
  renderer so the CHIP-8 font glyphs land at their real screen positions,
  and prints the readable screen plus the stderr state dump. Use it to read
  a test ROM's on-screen verdict (e.g. the Timendus suite's `OK`/`FAIL`).
  `--keys` feeds CHIP-8 keypresses to dismiss a splash screen; `--ticks N`
  waits `N/60` s of settle time (the 60 Hz timer rate, accurate for
  frame-drawing ROMs; use `--hold` for spin loops, which aren't tick-paced).

```sh
python3 scripts/run_dump.py src/chip8-interp roms/eq_sub.ch8          # register dump only
python3 scripts/screen_dump.py src/chip8-interp roms/test-roms/4-flags.ch8 --keys 0 --ticks 180
```

## Quirks / Compatibility Notes

CHIP-8 has several well-known compatibility splits. This emulator makes explicit
choices for interoperability with modern test ROMs (especially the Timendus
suite). All three execution backends agree on these:

| Opcode / area | Choice | Notes |
|---|---|---|
| `8xy6` / `8xyE` (shift) | **Modern / SCHIP**: shift `Vx` in place, ignore `Vy` | Original COSMAC VIP copied `Vy` into `Vx` then shifted (`regs[x]=regs[y]; regs[x]>>=1`). The interpreter comment `/* Y; */` and both JIT backends intentionally implement the modern quirk. Documented as a choice, not a bug. |
| `Dxyn` (draw) | **Wrap-start + clip**: `x %= 64`, `y %= 32`, then clip sprite pixels that would go off the right / bottom edge | Old behavior wrapped every pixel (`x2=(x+i)%W, y2=(y+j)%H`). Quirks suite `5-quirks` expects clipping; now fixed in `draw_io` shared by all backends. |
| `8xy4/5/6/7/E` with `x==F` | **Snapshot then result-then-flag**: operands snapshotted, result stored to `Vx`, `VF` stored last so flag wins | Required by `4-flags.ch8`. |
| `8xy5/8xy7` borrow | **NOT-borrow**: `VF = Vx >= Vy` (or `Vy >= Vx`) | Many ROMs expect `>=`, not strict `>`. |
| `Fx55/Fx65`, `Fx33`, `Dxyn` + `Annn`/`Fx1E` | **12-bit wrap via `MEM_AT`**: address masked with `0xFFF` | COSMAC VIP 12-bit wrap; also prevents OOB host access. |
| `Bnnn` | `PC = nnn + V0` with zero-extend | LLVM backend previously failed to extend `V0`. |
| Timers | `uint8_t`, 0..255, decrement only when >0 | Signed timers previously hung on values >127. |

## Layout

All source lives in `src/`. See `TODO.md` for known issues and unfinished work,
and `LICENSE.md` for licensing (BSD-style, (c) 2021 James McClain).
