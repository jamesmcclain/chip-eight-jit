#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include "io.h"

WINDOW * window;

uint8_t __attribute__((aligned(0x1000))) display[DISPLAY_SIZE];
int width = 0;
int height = 0;


void init_io(int _width, int _height)
{
  width = _width;
  height = _height;

  initscr();
  cbreak();
  noecho();

  curs_set(0);
  window = newwin(height+2, width+2, 0, 0);
  wtimeout(window, 0);
  box(window, 0, 0);
  wrefresh(window);
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
          mvwaddch(window, y2+1, x2+1, (new_display_bit? ACS_CKBOARD : ACS_BULLET));
        }
    }
  return vf;
}

void clearscreen_io()
{
  memset(display, 0, DISPLAY_SIZE * sizeof(uint8_t));
  wclear(window);
}

uint16_t read_keys_io()
{
  int key;
  uint16_t keys_down = 0;

  while ((key = wgetch(window)) != ERR)
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
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
          keys_down |= (1<<(key - 'a' + 0xa));
          break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
          keys_down |= (1<<(key - 'A' + 0xa));
          break;
        }
    }
  return keys_down;
}

void refresh_io()
{
  wrefresh(window);
}
