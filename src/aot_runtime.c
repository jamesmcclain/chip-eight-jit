/* Host services used by generated AOT code.  This file deliberately contains
   no fetch/decode/dispatch loop: opcode selection lives in the LLVM module. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "chip8.h"
#include "io.h"

uint8_t delay_timer, sound_timer;
static uint32_t retired;
static int last_tick;
extern void chip8_aot_run(void);
extern const uint8_t chip8_aot_rom[];
extern const uint32_t chip8_aot_rom_size;

static uint32_t keys(void) { return read_keys_io(); }
int chip8_aot_retire(void) { ++retired; if ((retired & 31u) == 0) { int now=(int)(clock()/(CLOCKS_PER_SEC/60)); if(now!=last_tick) { if(delay_timer)--delay_timer; if(sound_timer)--sound_timer; last_tick=now; } } return (keys() & (1u<<31)) != 0; }
void chip8_aot_clear(void) { clearscreen_io(); }
void chip8_aot_return(void) { if(stack_pointer) program_counter=stack[--stack_pointer]; else program_counter=0xffff; }
void chip8_aot_call(uint16_t dst,uint16_t ret) { if(stack_pointer < STACK_SIZE) { stack[stack_pointer++]=ret; program_counter=dst; } else program_counter=0xffff; }
void chip8_aot_alu(uint8_t x,uint8_t y,uint8_t n) { uint8_t a=regs[x],b=regs[y]; switch(n) { case 0:regs[x]=b;break;case 1:regs[x]|=b;break;case 2:regs[x]&=b;break;case 3:regs[x]^=b;break;case 4:{uint16_t s=a+b;regs[x]=s;FLAGS=s>255;break;}case 5:regs[x]=a-b;FLAGS=a>=b;break;case 6:regs[x]=a>>1;FLAGS=a&1;break;case 7:regs[x]=b-a;FLAGS=b>=a;break;case 14:regs[x]=a<<1;FLAGS=(a>>7)&1;break;default:program_counter=0xffff; } }
void chip8_aot_random(uint8_t x,uint8_t k) { regs[x]=(uint8_t)(rand()&255)&k; }
void chip8_aot_draw(uint8_t x,uint8_t y,uint8_t n) { uint8_t sprite[16];for(unsigned i=0;i<n;i++)sprite[i]=MEM_AT(addr+i);FLAGS=draw_io(regs[x],regs[y],n,sprite);refresh_io(); }
void chip8_aot_key(uint8_t x,int up) { int down=(keys()&(1u<<regs[x]))!=0;if((down^up))program_counter+=2;program_counter+=2; }
void chip8_aot_f(uint8_t x,uint8_t k) { unsigned i; switch(k) { case 7:regs[x]=delay_timer;break;case 10:while(!(keys()&0xffff));for(i=0;i<16;i++)if(keys()&(1u<<i))regs[x]=i;break;case 21:delay_timer=regs[x];break;case 24:sound_timer=regs[x];break;case 30:addr+=regs[x];break;case 41:addr=regs[x]*5;break;case 51:{uint8_t v=regs[x];MEM_AT(addr)=v/100;MEM_AT(addr+1)=(v/10)%10;MEM_AT(addr+2)=v%10;break;}case 85:for(i=0;i<=x;i++)MEM_AT(addr+i)=regs[i];break;case 101:for(i=0;i<=x;i++)regs[i]=MEM_AT(addr+i);break;default:program_counter=0xffff; } }
void chip8_aot_bad(uint16_t op,uint16_t pc) { fprintf(stderr,"Error: op=%04x pc=%04x\n",op,pc); }
int main(void) { init_chip8(); if(chip8_aot_rom_size>MEMORY_SIZE-ENTRYPOINT)return 1;memcpy(memory+ENTRYPOINT,chip8_aot_rom,chip8_aot_rom_size);init_io(64,32);program_counter=ENTRYPOINT;chip8_aot_run();deinit_io();deinit_chip8();dump_chip8_state("AOT instructions",(int)retired);return 0; }
