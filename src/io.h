#ifndef __IO_H__
#define __IO_H__

#define DISPLAY_SIZE (128*64)

extern uint8_t display[];
extern int width;
extern int height;

void init(int width, int height);
void deinit();

int draw_io(int x, int y, int n, uint8_t * mem);
void clearscreen_io();
void load_on_key_io();
void load_sprite_addr_io();
void set_delay_timer_io();
void set_sound_timer_io();
void skip_key_down_io();
void skip_key_up_io();

#endif
