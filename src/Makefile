CFLAGS ?= -Wall -Werror -O0 -ggdb3
CC = clang


all: interp-test

interp-test: interp-test.o chip8.o interp.o ncurses_io.o
	$(CC) $^ $(LDFLAGS) -lncurses -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o

cleaner: clean

cleanest: cleaner
	rm -f interp-test
