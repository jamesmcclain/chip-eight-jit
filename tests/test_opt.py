#!/usr/bin/env python3
"""Regression tests for the Python optimizer front-end."""
import json
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OPT = ROOT / "tools" / "chip8opt.py"
ASM = ROOT / "src" / "chip8-asm"
DISAS = ROOT / "src" / "chip8-disas"


def run(*args, text=False):
    return subprocess.check_output(args, text=text)


def test_canonicalize_round_trip_real_roms():
    for rom_name in ("PONG", "TETRIS"):
        original = (ROOT / "roms" / rom_name).read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            source = directory / "input.asm"
            canonical = directory / "canonical.asm"
            source.write_bytes(run(DISAS, "--asm", ROOT / "roms" / rom_name))
            subprocess.check_call([OPT, "canonicalize", source, "-o", canonical])
            rebuilt = run(ASM, canonical)
        assert rebuilt == original, rom_name


def test_analyzer_reports_static_rom_write_and_cfg():
    source = """\
start: LD I, scratch
LD B, V0
JP start
scratch: .byte 0, 0, 0
"""
    with tempfile.NamedTemporaryFile("w", suffix=".asm") as f:
        f.write(source)
        f.flush()
        data = json.loads(run(OPT, "analyze", "--json", f.name, text=True))
    assert data["reachable_instruction_count"] == 3
    assert data["accesses"] == [{"pc": "0x202", "kind": "write", "start": "0x206", "length": 3, "reason": "BCD store"}]
    assert data["hazards"] == ["0x202: write into ROM payload at 0x206"]
    assert ["0x204", "0x200"] in data["edges"]


def test_analyzer_marks_dynamic_control_flow():
    with tempfile.NamedTemporaryFile("w", suffix=".asm") as f:
        f.write("JP V0, 0x220\n")
        f.flush()
        data = json.loads(run(OPT, "analyze", "--json", f.name, text=True))
    assert "0x200: computed JP V0" in data["hazards"]


if __name__ == "__main__":
    test_canonicalize_round_trip_real_roms()
    test_analyzer_reports_static_rom_write_and_cfg()
    test_analyzer_marks_dynamic_control_flow()
    print("chip8opt tests: OK")
