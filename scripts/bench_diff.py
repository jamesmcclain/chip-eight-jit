#!/usr/bin/env python3
"""Differential test and benchmark across the three engines.

Runs each engine's -DBENCH build on a ROM with the same seed, key schedule
and instruction budget, then compares final machine state.

The JIT builds cannot stop on an exact instruction count: a trace only
notices the budget at a safepoint, so it overshoots by a bounded amount.
Rather than loosen the comparison, this script reads each JIT's actual
retired count and re-runs the interpreter to exactly that count. Both engines
have then executed the same instruction sequence from the same initial state,
so every register, timer, the PC, and the display hash must agree exactly --
any difference is a real divergence.

Usage: bench_diff.py [--instructions N] [--seed S] [--keys none|rotate]
                     [--engines llvm,libgccjit] [rom ...]
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

# Fields that must match between two engines run to the same instruction
# count. The trace/instruction counter and the timing fields are engine
# specific and deliberately excluded.
STATE_FIELDS = (
    [f"V{i:X}" for i in range(16)]
    + ["$pc", "$addr", "stack depth", "delay", "sound", "display", "retired"]
)


def run(engine, rom, instructions, seed, keys):
    exe = SRC / f"chip8-{engine}-bench"
    if not exe.exists():
        sys.exit(f"missing {exe}; build it with `make -C src bench`")
    out = subprocess.run(
        [str(exe), str(rom), "--instructions", str(instructions),
         "--seed", str(seed), "--keys", keys],
        capture_output=True, text=True, timeout=1800,
    )
    state = {}
    for line in out.stderr.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            state[k.strip()] = v.strip()
        elif line.startswith("stack depth"):
            state["stack depth"] = line.split("=", 1)[-1].strip()
    if "retired" not in state:
        sys.exit(f"{engine} on {rom} produced no report:\n{out.stderr[-2000:]}")
    return state


def compare(ref, ref_name, other, other_name):
    diffs = []
    for field in STATE_FIELDS:
        if field in ref and field in other and ref[field] != other[field]:
            diffs.append(f"      {field}: {ref_name}={ref[field]} {other_name}={other[field]}")
    return diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roms", nargs="*", default=None)
    ap.add_argument("--instructions", type=int, default=5_000_000)
    ap.add_argument("--seed", type=int, default=20240101)
    ap.add_argument("--keys", default="rotate", choices=["none", "rotate"])
    ap.add_argument("--engines", default="llvm,libgccjit")
    args = ap.parse_args()

    roms = [Path(r) for r in args.roms] if args.roms else sorted(
        p for p in (ROOT / "roms").iterdir() if p.is_file() and p.name.isupper()
    )
    engines = args.engines.split(",")

    failures = 0
    for rom in roms:
        print(f"{rom.name}:")
        base = run("interp", rom, args.instructions, args.seed, args.keys)
        print(f"    interp      {float(base['rate'].split()[0]):8.2f} Minsn/s"
              f"  retired={base['retired']}")

        for engine in engines:
            got = run(engine, rom, args.instructions, args.seed, args.keys)
            rate = float(got["rate"].split()[0])
            print(f"    {engine:<11} {rate:8.2f} Minsn/s"
                  f"  retired={got['retired']}"
                  f"  compiled={got.get('compiled', '?')}"
                  f"  flushes={got.get('flushes', '?')}")

            # Re-run the interpreter to the JIT's exact stopping point.
            ref = base
            if got["retired"] != base["retired"]:
                ref = run("interp", rom, int(got["retired"]), args.seed, args.keys)
            diffs = compare(ref, "interp", got, engine)
            if diffs:
                failures += 1
                print(f"      DIVERGED from interp:")
                print("\n".join(diffs))
            else:
                print(f"      matches interp")

    print()
    print("all engines agree" if not failures else f"{failures} divergence(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
