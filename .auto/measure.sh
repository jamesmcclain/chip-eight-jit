#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
gccjit_dir=$(dirname "$(find /usr/lib/gcc -name libgccjit.so 2>/dev/null | head -1)")
: "${gccjit_dir:?libgccjit.so not found}"
export CPPFLAGS="-I$gccjit_dir/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$gccjit_dir${LDFLAGS:+ $LDFLAGS}"
make -C src bench >/dev/null

python3 - <<'PY'
import math
import re
import statistics
import subprocess

roms = ("MAZE", "PONG", "BLINKY", "TANK")
engines = ("llvm", "libgccjit")
runs = 3
rates = {engine: [] for engine in engines}
compiled = []
flushes = []

for engine in engines:
    for rom in roms:
        samples = []
        for _ in range(runs):
            result = subprocess.run(
                [f"src/chip8-{engine}-bench", f"roms/{rom}",
                 "--instructions", "5000000", "--seed", "20240101",
                 "--keys", "rotate"],
                capture_output=True, text=True, check=True, timeout=1800)
            rate = re.search(r"^rate = ([0-9.]+) Minsn/s$", result.stderr, re.M)
            comp = re.search(r"^compiled = ([0-9]+)$", result.stderr, re.M)
            flush = re.search(r"^flushes = ([0-9]+)$", result.stderr, re.M)
            if not rate or not comp or not flush:
                raise RuntimeError(f"incomplete report for {engine} {rom}:\n{result.stderr}")
            samples.append(float(rate.group(1)))
            compiled.append(int(comp.group(1)))
            flushes.append(int(flush.group(1)))
        median = statistics.median(samples)
        rates[engine].append(median)
        print(f"{engine} {rom}: median={median:.2f} Minsn/s samples={samples}")

llvm_geo = math.prod(rates["llvm"]) ** (1 / len(roms))
gcc_geo = math.prod(rates["libgccjit"]) ** (1 / len(roms))
all_geo = math.prod(rates["llvm"] + rates["libgccjit"]) ** (1 / (len(roms) * len(engines)))
print(f"METRIC rate_mins={all_geo:.6f}")
print(f"METRIC llvm_rate_mins={llvm_geo:.6f}")
print(f"METRIC libgccjit_rate_mins={gcc_geo:.6f}")
print(f"METRIC min_rate_mins={min(rates['llvm'] + rates['libgccjit']):.6f}")
print(f"METRIC compiled={statistics.median(compiled):.0f}")
print(f"METRIC flushes={statistics.median(flushes):.0f}")
PY
