CFLAGS ?= -Wall -O -g
CC = gcc
CXX = g++
CXXSTD = -std=c++17
LLVM_CONFIG ?= llvm-config-20
LLVM_LIBS = $(shell $(LLVM_CONFIG) --ldflags --system-libs --libs core orcjit native)

all: chip8-interp chip8-llvm chip8-disas

chip8-llvm: llvm_jit.o chip8.o ncurses_io.o
	$(CXX) $^ $(LLVM_LIBS) -Xlinker -E $(LDFLAGS) -lncurses -o $@

chip8-interp: interp.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -lncurses -o $@

chip8-disas: disas.o
	$(CC) $^ $(LDFLAGS) -o $@

llvm_jit.o: llvm_jit.cpp
	$(CXX) $(CFLAGS) $(CXXSTD) $(CXXFLAGS) $(shell $(LLVM_CONFIG) --cxxflags) $< -c -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o

cleanest: clean
	rm -f chip8-interp chip8-llvm chip8-disas
