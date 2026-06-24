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

## Build

Requirements: `gcc`/`g++`, LLVM 20 development files (`llvm-config-20`),
libgccjit (`libgccjit-*-dev`), and ncurses.

```sh
cd src
make                 # builds all four targets
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

## Layout

All source lives in `src/`. See `TODO.md` for known issues and unfinished work,
and `LICENSE.md` for licensing (BSD-style, (c) 2021 James McClain).
