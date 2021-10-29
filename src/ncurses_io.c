#include <ncurses.h>
#include "io.h"

WINDOW * window;

uint8_t __attribute__((aligned(0x1000))) display[DISPLAY_SIZE];
int width = 0;
int height = 0;


void init(int width, int height)
{
  initscr();
  cbreak();
  noecho();
  window = newwin(height+2, width+2, 0, 0);
  box(window, 0, 0);
  refresh();
}

void deinit()
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
          mvwaddch(window, y2+1, x2+1, new_display_bit? 219: ' ');
        }
    }
  refresh(); // XXX
  return vf;
}

void skip_key_down_io()
{
}

void skip_key_up_io()
{
}

void load_on_key_io()
{
}

void set_delay_timer_io()
{
}

void set_sound_timer_io()
{
}

void load_sprite_addr_io()
{
}
