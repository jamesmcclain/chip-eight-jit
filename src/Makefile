CFLAGS ?= -Wall -Werror -O0 -ggdb3
CC = clang


all: interp disas

interp: interp.o chip8.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -lncurses -o $@

disas: disas.o
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o

cleaner: clean

cleanest: cleaner
	rm -f interp disas
