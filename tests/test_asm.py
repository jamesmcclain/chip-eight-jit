#!/usr/bin/env python3
"""Small byte-level regression suite for chip8-asm."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASM = ROOT / "src" / "chip8-asm"
DISAS = ROOT / "src" / "chip8-disas"


def assemble(source, okay=True):
    with tempfile.NamedTemporaryFile("w", suffix=".asm") as f:
        f.write(source)
        f.flush()
        result = subprocess.run([ASM, f.name], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=False)
    if okay:
        assert result.returncode == 0, result.stderr.decode()
        return result.stdout
    assert result.returncode != 0, "invalid source assembled successfully"
    return result.stderr.decode()


def test_instructions():
    source = """\
CLS
RET
JP 0x345
JP V0, 0x456
CALL 0x567
SE V1, 0x89
SNE V2, 0xAB
SE V3, V4
SNE V5, V6
LD V7, 0xCD
ADD V8, 0xEF
LD V1, V2
OR V1, V2
AND V1, V2
XOR V1, V2
ADD V1, V2
SUB V1, V2
SHR V1
SUBN V1, V2
SHL V1
LD I, 0x123
RND VA, 0x55
DRW V1, V2, 5
SKP V3
SKNP V4
LD V5, DT
LD V6, K
LD DT, V7
LD ST, V8
ADD I, V9
LD F, VA
LD B, VB
LD [I], VC
LD VD, [I]
"""
    expected = bytes.fromhex("00e0 00ee 1345 b456 2567 3189 42ab 5340 9560 "
                             "67cd 78ef 8120 8121 8122 8123 8124 8125 8106 "
                             "8127 810e a123 ca55 d125 e39e e4a1 f507 f60a "
                             "f715 f818 f91e fa29 fb33 fc55 fd65")
    assert assemble(source) == expected


def test_labels_data_and_org():
    source = """\
.org 0x200
start: JP later
.byte 1, 0x02
.word 0xA0B0
.org 0x208
later: LD I, start
"""
    assert assemble(source) == bytes.fromhex("1208 0102 a0b0 0000 a200")


def test_disassembler_asm_round_trip():
    # Includes non-zero y nibbles in shifts, which must survive the round trip.
    original = bytes.fromhex("8126 8abe 00e0 f155 d345")
    with tempfile.NamedTemporaryFile("wb") as rom:
        rom.write(original)
        rom.flush()
        source = subprocess.check_output([DISAS, "--asm", rom.name])
    with tempfile.NamedTemporaryFile("wb", suffix=".asm") as text:
        text.write(source)
        text.flush()
        rebuilt = subprocess.check_output([ASM, text.name])
    assert rebuilt == original


def test_failures():
    assert "undefined symbol" in assemble("JP missing\n", okay=False)
    assert "duplicate label" in assemble("a: CLS\na: RET\n", okay=False)
    assert "out of range" in assemble("LD V0, 0x100\n", okay=False)
    assert "cannot move backwards" in assemble(".org 0x200\nCLS\n.org 0x200\n", okay=False)


if __name__ == "__main__":
    test_instructions()
    test_labels_data_and_org()
    test_disassembler_asm_round_trip()
    test_failures()
    print("chip8-asm tests: OK")
