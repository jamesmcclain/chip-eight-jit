#!/usr/bin/env python3
"""AOT IR must contain lowered ROM blocks, not an interpreter trampoline."""
import pathlib, shutil, subprocess, sys, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
AOT, ROM = ROOT / "src" / "chip8-aot", ROOT / "roms" / "PONG"
def main():
    subprocess.run(["make", "-C", str(ROOT / "src"), "chip8-aot", "aot-runtime-objs"], check=True)
    llvm_as = shutil.which("llvm-as-20") or shutil.which("llvm-as")
    clang = shutil.which("clang-20") or shutil.which("clang")
    if not llvm_as or not clang:
        print("missing LLVM tools", file=sys.stderr); return 1
    with tempfile.TemporaryDirectory() as directory:
        d = pathlib.Path(directory); module, bitcode, exe = d / "pong.ll", d / "pong.bc", d / "pong"
        subprocess.run([str(AOT), str(ROM), "-o", str(module)], check=True)
        text = module.read_text()
        assert "define void @chip8_aot_run()" in text
        assert "chip8_aot_execute" not in text
        assert "@chip8_aot_rom = constant" in text
        subprocess.run([llvm_as, str(module), "-o", str(bitcode)], check=True)
        subprocess.run([clang, str(module), str(ROOT/"src"/"aot_runtime.o"), str(ROOT/"src"/"chip8.o"), str(ROOT/"src"/"ncurses_io.o"), "-lncurses", "-o", str(exe)], check=True)
    print("AOT LLVM IR/link test passed")
    return 0
if __name__ == "__main__": raise SystemExit(main())
