#ifndef __IO_H__
#define __IO_H__

#define DISPLAY_SIZE (128*64)

extern uint8_t display[];
extern int width;
extern int height;

void init_io(int width, int height);
void deinit_io();

int draw_io(int x, int y, int n, uint8_t * mem);
uint16_t read_keys_io();
void clearscreen_io();
void load_sprite_addr_io();

#endif
