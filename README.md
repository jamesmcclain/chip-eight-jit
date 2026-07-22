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

If you only need the interpreter, LLVM, or disassembler targets, the libgccjit
path is not required and a plain `make chip8-interp` (etc.) will work without it.

```sh
cd src
make                 # builds all four targets (needs the libgccjit flags above)
make chip8-interp    # or build a single target
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

## Testing

Two helper scripts in `scripts/` drive the engines under a pty and capture
results, since ROMs do not terminate on their own and the display is
ncurses. Both send `q` to quit and SIGKILL the child as a safety net so the
tool always returns.

- **`run_dump.py <engine> <rom>`** captures the stderr state dump
  (registers, PC, `I`, timers) while discarding the ncurses screen. Use it
  for deterministic cross-engine comparisons of final machine state, e.g.
  after a crafted micro-ROM.

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

## Layout

All source lives in `src/`. See `TODO.md` for known issues and unfinished work,
and `LICENSE.md` for licensing (BSD-style, (c) 2021 James McClain).
