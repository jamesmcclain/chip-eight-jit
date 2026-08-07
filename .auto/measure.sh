#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
make -C src chip8-interp-bench chip8-llvm-bench >/dev/null

roms=(PONG TETRIS BLINKY)
runs=5
declare -a geo_rates
for rom in "${roms[@]}"; do
  rates=()
  compiled=()
  flushes=()
  for ((i=0; i<runs; ++i)); do
    report=$(./src/chip8-llvm-bench "roms/$rom" --instructions 5000000 --seed 20240101 --keys rotate 2>&1 >/dev/null)
    rates+=("$(awk -F'[= ]+' '/^rate =/{print $3}' <<<"$report")")
    compiled+=("$(awk -F'= *' '/^compiled =/{print $2}' <<<"$report")")
    flushes+=("$(awk -F'= *' '/^flushes =/{print $2}' <<<"$report")")
  done
  rate=$(printf '%s\n' "${rates[@]}" | sort -n | sed -n '3p')
  echo "METRIC ${rom,,}_rate_minsn_s=$rate"
  echo "METRIC ${rom,,}_compiled=${compiled[2]}"
  echo "METRIC ${rom,,}_flushes=${flushes[2]}"
  geo_rates+=("$rate")
done
rate=$(printf '%s\n' "${geo_rates[@]}" | python3 -c 'import math,sys; x=[float(v) for v in sys.stdin]; print(math.prod(x)**(1/len(x)))')
echo "METRIC rate_minsn_s=$rate"
