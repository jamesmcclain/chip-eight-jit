#!/usr/bin/env python3
"""Cross-engine quirk checks.

Each case assembles a tiny ROM, runs it under every built engine through a
pty (scripts/run_dump.py), and asserts on the stderr state dump. The engines
must agree, and they must agree on the documented quirk choice.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASM = ROOT / "src" / "chip8-asm"
RUN_DUMP = ROOT / "scripts" / "run_dump.py"
ENGINES = ["chip8-interp", "chip8-llvm", "chip8-libgccjit"]


def assemble(source):
    with tempfile.NamedTemporaryFile("w", suffix=".asm") as f:
        f.write(source)
        f.flush()
        return subprocess.check_output([ASM, f.name])


def run(engine, rom_bytes, *extra):
    with tempfile.NamedTemporaryFile("wb", suffix=".ch8") as rom:
        rom.write(rom_bytes)
        rom.flush()
        out = subprocess.run(
            ["python3", str(RUN_DUMP), str(ROOT / "src" / engine), rom.name, *extra],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        ).stdout
    return out


def state(dump):
    values = {}
    for key, val in re.findall(r"\$(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", dump):
        values[key] = int(val, 16)
    return values


def built_engines():
    return [e for e in ENGINES if (ROOT / "src" / e).exists()]


def test_fx29_masks_to_low_nibble():
    """Fx29 with Vx > 15 must land on a real hex-digit sprite, not an
    undefined address. "The Art of CHIP-8" warns against Vx > 15; every
    engine masks Vx to its low nibble, so 0xFF selects digit 0xF at
    0xF * 5 = 0x4B."""
    rom = assemble("LD V0, 0xFF\nLD F, V0\nloop: JP loop\n")
    engines = built_engines()
    assert engines, "no engines built"
    for engine in engines:
        addr = state(run(engine, rom)).get("addr")
        assert addr == 0x4B, f"{engine}: $addr = {addr:#06x}, expected 0x004B"


def test_elide_idle_matches_the_spinning_loop():
    """--elide-idle waits out the canonical delay-timer poll loop instead of
    spinning it. The observable end state -- the polled register cleared,
    the timer at zero, control past the loop with V5 set -- must be exactly
    what the un-elided interpreter produces."""
    rom = assemble(
        "LD V0, 6\nLD DT, V0\n"
        "wait: LD V0, DT\nSE V0, 0\nJP wait\n"
        "LD V5, 0xAB\nend: JP end\n"
    )

    def final(dump):
        keep = ("V0 =", "V5 =", "$pc =", "delay =")
        return [line for line in dump.splitlines() if line.startswith(keep)]

    for engine in ("chip8-interp", "chip8-llvm", "chip8-libgccjit"):
        if not (ROOT / "src" / engine).exists():
            continue
        plain = final(run(engine, rom))
        elided = final(run(engine, rom, "--elide-idle"))
        assert plain == elided, (engine, plain, elided)
        assert "V5 = 0xAB" in plain and "delay = 0" in plain, (engine, plain)


if __name__ == "__main__":
    test_fx29_masks_to_low_nibble()
    test_elide_idle_matches_the_spinning_loop()
    print("chip8 quirk tests: OK")
