CFLAGS ?= -Wall -Werror -Og -ggdb3
CC = clang


all: chip8.o

chip8: chip8.o
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c %.h
	$(CC) $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o

cleaner: clean

cleanest: cleaner
