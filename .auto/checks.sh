#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make -C src chip8-interp-bench chip8-llvm-bench >/dev/null
python3 scripts/bench_diff.py --instructions 1000000 --engines llvm \
  roms/PONG roms/TETRIS roms/BLINKY
