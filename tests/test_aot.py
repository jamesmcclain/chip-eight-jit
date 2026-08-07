#!/usr/bin/env python3
"""Smoke test for the textual LLVM IR AOT frontend."""
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
AOT = ROOT / "src" / "chip8-aot"
ROM = ROOT / "roms" / "PONG"


def main():
    subprocess.run(["make", "-C", str(ROOT / "src"), "chip8-aot"], check=True)
    llvm_as = shutil.which("llvm-as-20") or shutil.which("llvm-as")
    if llvm_as is None:
        print("missing llvm-as", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as directory:
        module = pathlib.Path(directory) / "pong.ll"
        bitcode = pathlib.Path(directory) / "pong.bc"
        subprocess.run([str(AOT), str(ROM), "-o", str(module)], check=True)
        text = module.read_text()
        assert "define void @chip8_aot_run()" in text
        assert "@chip8_aot_execute(i16" in text
        assert "@chip8_aot_rom = constant" in text
        subprocess.run([llvm_as, str(module), "-o", str(bitcode)], check=True)
    print("AOT LLVM IR test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
