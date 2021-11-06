#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <ncurses.h>
#include "io.h"

WINDOW * window;

uint8_t __attribute__((aligned(0x1000))) display[DISPLAY_SIZE];
int width = 0;
int height = 0;

#define KEY_ESC (27)


void init_io(int _width, int _height)
{
  width = _width;
  height = _height;

  initscr();
  cbreak();
  noecho();

  curs_set(0);
  window = newpad(height+3, width+2);
  wtimeout(window, 0);
  box(window, 0, 0);
  refresh_io();
}

void deinit_io()
{
  delwin(window);
  endwin();
}

int draw_io(int x, int y, int n, uint8_t* mem)
{
  int vf = 0;

  for (int j = 0; j < n; ++j)
    {
      int y2 = (j + y) % height;
      uint8_t byte = mem[j];

      for (int i = 0; i < 8; ++i)
        {
          int x2 = (i + x) % width;
          int bit = (byte & (0x80>>i)) ? 1: 0;
          int old_pixel = display[x2 + y2*width];
          int new_pixel = old_pixel ^ bit;

          display[x2 + y2*width] = new_pixel;
          if (old_pixel && bit)
            {
              vf |= 1;
            }
          mvwaddch(window, y2+1, x2+1, (new_pixel? ACS_CKBOARD : ' '));
        }
    }

  return vf;
}

void clearscreen_io()
{
  memset(display, 0, DISPLAY_SIZE * sizeof(uint8_t));
  wclear(window);
}

uint32_t read_keys_io()
{
  int key;
  uint32_t keys_down = 0;

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
        case 'q':
        case 'Q':
        case KEY_ESC:
          keys_down |= (1<<31);
          break;
        }
    }
  return keys_down;
}

void refresh_io()
{
  prefresh(window, 0, 0, 0, 0, height+3, width+2);
}
