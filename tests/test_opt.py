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


def test_peepholes_compact_relocatable_source():
    source = """\\
start: LD V1, 1
ADD V1, 2
LD V2, V2
LD V3, 4
LD V3, 5
JP done
done: RET
"""
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        original = directory / "input.asm"
        optimized = directory / "optimized.asm"
        original.write_text(source)
        subprocess.check_call([OPT, "optimize", original, "-o", optimized])
        assert run(ASM, optimized) == bytes.fromhex("6103 6305 1206 00ee")


def test_peepholes_do_not_remove_memory_stores():
    source = """\\
LD I, 0x700
LD [I], V0
LD [I], V1
RET
"""
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        original = directory / "input.asm"
        optimized = directory / "optimized.asm"
        original.write_text(source)
        subprocess.check_call([OPT, "optimize", original, "-o", optimized])
        assert run(ASM, optimized) == bytes.fromhex("a700 f055 f155 00ee")


def test_peepholes_reject_fixed_layout_or_dynamic_rom_access():
    source = """\\
LD I, 0x206
DRW V0, V1, 1
LD V2, V2
.byte 0
"""
    with tempfile.NamedTemporaryFile("w", suffix=".asm") as f:
        f.write(source)
        f.flush()
        result = subprocess.run([OPT, "optimize", f.name], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert result.returncode != 0
    assert "cannot compact safely" in result.stderr


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
