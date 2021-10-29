#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include "io.h"

WINDOW * window;

uint8_t __attribute__((aligned(0x1000))) display[DISPLAY_SIZE];
int width = 0;
int height = 0;


void init_io(int width, int height)
{
  initscr();

  cbreak();
  noecho();
  timeout(0);

  window = newwin(height+2, width+2, 0, 0);
  box(window, 0, 0);

  refresh(); // XXX
}

void deinit_io()
{
  endwin();
}

int draw_io(int x, int y, int n, uint8_t * mem)
{
  int vf = 0;

  for (int j = 0; j < n; ++j)
    {
      int y2 = y + j;
      uint8_t mem_byte = mem[j];

      for (int i = 0; i < 8; ++i)
        {
          int x2 = x + i;
          uint8_t mem_bit = (mem_byte >> (7-i)) & 0x1;
          uint8_t old_display_bit = display[x2 + y2*width];
          uint8_t new_display_bit = old_display_bit ^= mem_bit;

          display[x2 + y2*width] = new_display_bit;
          if (old_display_bit & !new_display_bit)
            {
              vf |= 1;
            }
          mvwaddch(window, y2+1, x2+1, (new_display_bit? 219: ' '));
        }
    }
  refresh(); // XXX
  return vf;
}

void clearscreen_io()
{
  memset(display, 0, DISPLAY_SIZE * sizeof(uint8_t));
  wclear(window);
  refresh(); // XXX
}

uint16_t read_keys_io()
{
  int key;
  uint16_t keys_down = 0;

  while ((key = getch()) != ERR)
    {
      switch(key)
        {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          keys_down |= (1<<(key - '0'));
          break;
        case 'a':
        case 'A':
          keys_down |= (1<<(0x0a));
          break;
        case 'b':
        case 'B':
          keys_down |= (1<<(0x0b));
          break;
        case 'c':
        case 'C':
          keys_down |= (1<<(0x0c));
          break;
        case 'd':
        case 'D':
          keys_down |= (1<<(0x0d));
          break;
        case 'e':
        case 'E':
          keys_down |= (1<<(0x0e));
          break;
        case 'f':
        case 'F':
          keys_down |= (1<<(0x0f));
          break;
        }
    }
  return keys_down;
}

void load_sprite_addr_io()
{
}
