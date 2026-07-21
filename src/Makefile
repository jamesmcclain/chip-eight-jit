DEPFLAGS = -MMD -MP
CFLAGS ?= -Wall -O -g $(DEPFLAGS)
CXXFLAGS += $(DEPFLAGS)
CC = gcc
CXX = g++
CXXSTD = -std=c++17
LLVM_CONFIG ?= llvm-config-20
LLVM_LIBS = $(shell $(LLVM_CONFIG) --ldflags --system-libs --libs core orcjit native)
GCCJIT_LIBS = -lgccjit

all: chip8-interp chip8-llvm chip8-libgccjit chip8-disas

chip8-llvm: llvm_jit.o chip8.o ncurses_io.o
	$(CXX) $^ $(LLVM_LIBS) -Xlinker -E $(LDFLAGS) -lncurses -o $@

chip8-libgccjit: libgccjit_jit.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -rdynamic -lncurses $(GCCJIT_LIBS) -o $@

chip8-interp: interp.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -lncurses -o $@

chip8-disas: disas.o
	$(CC) $^ $(LDFLAGS) -o $@

llvm_jit.o: llvm_jit.cpp
	$(CXX) $(CFLAGS) $(CXXSTD) $(CXXFLAGS) $(shell $(LLVM_CONFIG) --cxxflags) $< -c -o $@

libgccjit_jit.o: libgccjit_jit.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -c -o $@

clean:
	rm -f *.o *.d

cleanest: clean
	rm -f chip8-interp chip8-llvm chip8-libgccjit chip8-disas

-include $(wildcard *.d)
