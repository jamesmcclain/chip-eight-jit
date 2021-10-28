#include <ncurses.h>
#include "io.h"

WINDOW * window;

void init()
{
  window = initscr();
}

void deinit()
{
  delwin(window);
}

void draw_io()
{
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
