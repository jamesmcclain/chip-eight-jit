CFLAGS ?= -Wall -O -g
CC = clang
CXX = clang++


all: chip8-interp chip8-llvm chip8-disas

chip8-llvm: llvm_jit.o chip8.o ncurses_io.o
	$(CXX) $^ $(shell llvm-config --ldflags --system-libs --libs core orcjit native) -Xlinker -E $(LDFLAGS) -lncurses -o $@

chip8-interp: interp.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -lncurses -o $@

chip8-disas: disas.o
	$(CC) $^ $(LDFLAGS) -o $@

llvm_jit.o: llvm_jit.cpp
	$(CXX) $(CFLAGS) $(shell llvm-config --cxxflags) $< -c -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o

cleaner: clean

cleanest: cleaner
	rm -f chip8-interp chip8-llvm chip8-disas
