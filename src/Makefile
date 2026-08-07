DEPFLAGS = -MMD -MP
CFLAGS ?= -Wall -O -g $(DEPFLAGS)
CXXFLAGS += $(DEPFLAGS)
CC = gcc
CXX = g++
CXXSTD = -std=c++17
LLVM_CONFIG ?= llvm-config-20
LLVM_LIBS = $(shell $(LLVM_CONFIG) --ldflags --system-libs --libs core orcjit native)
GCCJIT_LIBS = -lgccjit

all: chip8-interp chip8-llvm chip8-libgccjit chip8-disas chip8-asm chip8-aot

# Deterministic benchmark / differential-test builds: same engines, headless
# I/O, virtual clock, fixed seed, instruction budget. See bench.h.
bench: chip8-interp-bench chip8-llvm-bench chip8-libgccjit-bench

chip8-llvm: llvm_jit.o chip8.o ncurses_io.o
	$(CXX) $^ $(LLVM_LIBS) -Xlinker -E $(LDFLAGS) -lncurses -o $@

chip8-libgccjit: libgccjit_jit.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -rdynamic -lncurses $(GCCJIT_LIBS) -o $@

chip8-interp: interp.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -lncurses -o $@

chip8-disas: disas.o
	$(CC) $^ $(LDFLAGS) -o $@

chip8-asm: asm.o
	$(CC) $^ $(LDFLAGS) -o $@

# chip8-aot writes a textual LLVM module. Link generated code only with the
# AOT host services and VM/I/O state; interp.o is intentionally not involved.
# Example: clang pong.ll aot_runtime.o chip8.o ncurses_io.o -lncurses -o pong
chip8-aot: aot.o
	$(CC) $^ $(LDFLAGS) -o $@

aot-runtime-objs: aot_runtime.o chip8.o ncurses_io.o

llvm_jit.o: llvm_jit.cpp
	$(CXX) $(CFLAGS) $(CXXSTD) $(CXXFLAGS) $(shell $(LLVM_CONFIG) --cxxflags) $< -c -o $@

libgccjit_jit.o: libgccjit_jit.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -c -o $@

chip8-llvm-bench: llvm_jit.bench.o chip8.bench.o bench_io.bench.o
	$(CXX) $^ $(LLVM_LIBS) -Xlinker -E $(LDFLAGS) -o $@

chip8-libgccjit-bench: libgccjit_jit.bench.o chip8.bench.o bench_io.bench.o
	$(CC) $^ $(LDFLAGS) -rdynamic $(GCCJIT_LIBS) -o $@

chip8-interp-bench: interp.bench.o chip8.bench.o bench_io.bench.o
	$(CC) $^ $(LDFLAGS) -o $@

llvm_jit.bench.o: llvm_jit.cpp
	$(CXX) $(CFLAGS) $(CXXSTD) $(CXXFLAGS) -DBENCH $(shell $(LLVM_CONFIG) --cxxflags) $< -c -o $@

%.bench.o: %.c
	$(CC) $(CFLAGS) -DBENCH $(CPPFLAGS) $< -c -o $@

test: chip8-asm chip8-disas
	python3 ../tests/test_asm.py
	python3 ../tests/test_opt.py
	python3 ../tests/test_aot.py

clean:
	rm -f *.o *.d

cleanest: clean
	rm -f chip8-interp chip8-llvm chip8-libgccjit chip8-disas chip8-asm chip8-aot
	rm -f chip8-interp-bench chip8-llvm-bench chip8-libgccjit-bench

.PHONY: all bench test clean cleanest aot-runtime-objs

-include $(wildcard *.d)
