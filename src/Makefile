CFLAGS ?= -Wall -Werror -Og -ggdb3
CC = clang


all: chip8.o interp.o ncurses_io.o

chip8: chip8.o interp.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o

cleaner: clean

cleanest: cleaner
