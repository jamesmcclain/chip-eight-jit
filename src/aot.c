/* Ahead-of-time CHIP-8 LLVM IR emitter.  Each ROM word becomes native IR; the
   generated dispatcher selects a lowered block, never an interpreter opcode. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chip8.h"

static void die(const char *s) { fprintf(stderr, "chip8-aot: %s\n", s); exit(1); }
static unsigned ptr_mask, load_mask;
static void next(FILE *f, unsigned pc) { fprintf(f, "  store i16 %u, ptr @program_counter\n  br label %%dispatch\n\n", pc + 2); }
static void regptr(FILE *f, unsigned pc, unsigned r) { if (!(ptr_mask & (1u << r))) { fprintf(f, "  %%r%x_%x = getelementptr [16 x i8], ptr @regs, i32 0, i32 %u\n", pc, r, r); ptr_mask |= 1u << r; } }
static void loadreg(FILE *f, unsigned pc, unsigned r) { regptr(f,pc,r); if (!(load_mask & (1u << r))) { fprintf(f,"  %%v%x_%x = load i8, ptr %%r%x_%x\n",pc,r,pc,r); load_mask |= 1u << r; } }
static void block(FILE *f, unsigned pc, uint16_t op) {
  ptr_mask = load_mask = 0;
  unsigned x=(op>>8)&15,y=(op>>4)&15,n=op&15,kk=op&255,nnn=op&4095;
  fprintf(f,"pc%x:\n  %%stop%x = call i1 @chip8_aot_retire()\n  br i1 %%stop%x, label %%exit, label %%op%x\nop%x:\n",pc,pc,pc,pc,pc);
  if(op==0x00e0) { fprintf(f,"  call void @chip8_aot_clear()\n"); next(f,pc); return; }
  if(op==0x00ee) { fprintf(f,"  call void @chip8_aot_return()\n  br label %%dispatch\n\n"); return; }
  switch(op>>12) {
  case 0: case 1: fprintf(f,"  store i16 %u, ptr @program_counter\n  br label %%dispatch\n\n",nnn); break;
  case 2: fprintf(f,"  call void @chip8_aot_call(i16 %u, i16 %u)\n  br label %%dispatch\n\n",nnn,pc+2); break;
  case 3: case 4: loadreg(f,pc,x); fprintf(f,"  %%c%x = icmp %s i8 %%v%x_%x, %u\n  %%p%x = select i1 %%c%x, i16 %u, i16 %u\n  store i16 %%p%x, ptr @program_counter\n  br label %%dispatch\n\n",pc,(op>>12)==3?"eq":"ne",pc,x,kk,pc,pc,pc+4,pc+2,pc); break;
  case 5: case 9: loadreg(f,pc,x);loadreg(f,pc,y);fprintf(f,"  %%c%x = icmp %s i8 %%v%x_%x, %%v%x_%x\n  %%p%x = select i1 %%c%x, i16 %u, i16 %u\n  store i16 %%p%x, ptr @program_counter\n  br label %%dispatch\n\n",pc,(op>>12)==5?"eq":"ne",pc,x,pc,y,pc,pc,pc+4,pc+2,pc);break;
  case 6: regptr(f,pc,x);fprintf(f,"  store i8 %u, ptr %%r%x_%x\n",kk,pc,x);next(f,pc);break;
  case 7: loadreg(f,pc,x);fprintf(f,"  %%a%x = add i8 %%v%x_%x, %u\n",pc,pc,x,kk);regptr(f,pc,x);fprintf(f,"  store i8 %%a%x, ptr %%r%x_%x\n",pc,pc,x);next(f,pc);break;
  case 8: fprintf(f,"  call void @chip8_aot_alu(i8 %u, i8 %u, i8 %u)\n",x,y,n);next(f,pc);break;
  case 0xa: fprintf(f,"  store i16 %u, ptr @addr\n",nnn);next(f,pc);break;
  case 0xb: loadreg(f,pc,0);fprintf(f,"  %%z%x = zext i8 %%v%x_0 to i16\n  %%j%x = add i16 %u, %%z%x\n  store i16 %%j%x, ptr @program_counter\n  br label %%dispatch\n\n",pc,pc,pc,nnn,pc,pc);break;
  case 0xc: fprintf(f,"  call void @chip8_aot_random(i8 %u, i8 %u)\n",x,kk);next(f,pc);break;
  case 0xd: fprintf(f,"  call void @chip8_aot_draw(i8 %u, i8 %u, i8 %u)\n",x,y,n);next(f,pc);break;
  case 0xe: fprintf(f,"  call void @chip8_aot_key(i8 %u, i1 %u)\n  br label %%dispatch\n\n",x,(op&255)==0xa1);break;
  case 0xf: fprintf(f,"  call void @chip8_aot_f(i8 %u, i8 %u)\n",x,op&255);next(f,pc);break;
  default: fprintf(f,"  call void @chip8_aot_bad(i16 %u, i16 %u)\n  br label %%exit\n\n",op,pc);
  }
}
int main(int ac,char **av) {
  uint8_t rom[MEMORY_SIZE-ENTRYPOINT]; size_t z; FILE *in,*out;
  if(ac!=4||strcmp(av[2],"-o")){fprintf(stderr,"Usage: %s ROM -o OUTPUT.ll\n",av[0]);return 1;} if(!(in=fopen(av[1],"rb")))die(strerror(errno)); z=fread(rom,1,sizeof rom,in);if(ferror(in)||fgetc(in)!=EOF)die("invalid ROM");fclose(in);if(!z||(z&1))die("ROM size must be a nonzero even number");if(!(out=fopen(av[3],"w")))die(strerror(errno));
  fprintf(out,"; Generated AOT code -- no opcode dispatcher is linked.\n@regs = external global [16 x i8]\n@addr = external global i16\n@program_counter = external global i16\ndeclare i1 @chip8_aot_retire()\ndeclare void @chip8_aot_clear()\ndeclare void @chip8_aot_return()\ndeclare void @chip8_aot_call(i16,i16)\ndeclare void @chip8_aot_alu(i8,i8,i8)\ndeclare void @chip8_aot_random(i8,i8)\ndeclare void @chip8_aot_draw(i8,i8,i8)\ndeclare void @chip8_aot_key(i8,i1)\ndeclare void @chip8_aot_f(i8,i8)\ndeclare void @chip8_aot_bad(i16,i16)\n");
  fprintf(out,"@chip8_aot_rom = constant [%zu x i8] [",z);for(size_t i=0;i<z;i++)fprintf(out,"%si8 %u",i?", ":"",rom[i]);fprintf(out,"]\n@chip8_aot_rom_size = constant i32 %zu\ndefine void @chip8_aot_run() {\nentry: br label %%dispatch\ndispatch:\n  %%pc = load i16, ptr @program_counter\n  switch i16 %%pc, label %%exit [\n",z);for(size_t i=0;i<z;i+=2)fprintf(out,"    i16 %zu, label %%pc%zx\n",ENTRYPOINT+i,ENTRYPOINT+i);fprintf(out,"  ]\n\n");for(size_t i=0;i<z;i+=2)block(out,ENTRYPOINT+i,(rom[i]<<8)|rom[i+1]);fprintf(out,"exit: ret void\n}\n");if(fclose(out))die("cannot write LLVM IR");return 0;
}
