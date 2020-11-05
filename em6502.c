#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/************************************
* Memory contents 
************************************/
uint8_t ram[1024*56];
uint8_t rom1[1024*8];
uint8_t rom2[1024*8];

uint8_t mem_read(uint16_t addr);
uint8_t mem_read_nolog(uint16_t addr);
void mem_write(uint16_t addr, uint8_t data);
/************************************
* CPU state
************************************/
#define FLAG_C 0x01
#define FLAG_Z 0x02
#define FLAG_I 0x04
#define FLAG_D 0x08
#define FLAG_B 0x10
#define FLAG_V 0x40
#define FLAG_N 0x80
static struct cpu_state {
  uint8_t  flags;
  uint8_t  a;
  uint8_t  x;
  uint8_t  y;
  uint8_t  sp;
  uint16_t pc;
  uint32_t cycle;
} state;

/**************************************
* For tracing execution
***************************************/
void trace(char *msg, uint16_t op);
#define TRACE_OFF 0
#define TRACE_OP  1
#define TRACE_MEM 2
//int trace_level = TRACE_OP|TRACE_MEM;
int trace_level = TRACE_OP;
uint16_t trace_addr;
uint8_t trace_opcode;

/********************************************************************************/
/*************** START OF ALL THE OPCODE IMPLEMENTATOINS ************************/
/********************************************************************************/

void op20(void) {  // JSR
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  state.pc    += 2;
  mem_write(0x100+state.sp,   state.pc>>8);
  mem_write(0x100+state.sp-1, state.pc&0xFF);
  state.sp    -= 2; 
  state.pc     = o;
  state.cycle += 6; 
  if(trace_level & TRACE_OP)
     trace("JSR #%04X", o);
}

void op60(void) {  // RTS
  uint16_t o = mem_read(0x100+state.sp+1) | (mem_read(0x100+state.sp+2)<<8);
  state.sp    += 2; 
  state.pc     = o+1;
  state.cycle += 6; 
  if(trace_level & TRACE_OP)
     trace("RTS", o);
}

void op78(void) {  // SEI
  state.flags |= FLAG_I;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("SEI", 0);
}

void op95(void) {  // STA zpg, X
  uint16_t z = mem_read(state.pc+1);
  uint16_t o = mem_read(z) | (mem_read(z+1)<<8);
  mem_write(o+state.x,state.a);
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STA zeropage %02X, X", z);
}


void op9A(void) {  // TXS
  state.sp     = state.x;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("TXS", 0);
}

void op9D(void) {  // STA abs, X
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  mem_write(o+state.x, state.a);
  state.pc    += 3;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STA %04X, X", o);
}

void opA2(void) {  // LDX #
  uint8_t o = mem_read(state.pc+1);
  state.x      = o;
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 2;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("LDX #%02X", o);
}

void opA6(void) {  // LDX zeropage
  uint8_t z = mem_read(state.pc+1);
  uint8_t o = mem_read(z);
  state.x      = o;
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 3; 
  if(trace_level & TRACE_OP)
     trace("LDX zz%02X", o);
}

void opA9(void) {  // LDA #
  uint8_t o = mem_read(state.pc+1);
  state.a      = o;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("LDA #%02X", o);
}

void opAA(void) {  // TAX
  state.x      = state.a;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("TAX", 0);
}

void opBD(void) {  // LDA abs, X
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  state.a      = mem_read(o+state.x);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 3;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(trace_level & TRACE_OP)
     trace("LDA %04X, X", o);
}

void opD0(void) {  // BNE rel
  uint8_t o = mem_read(state.pc+1);
  state.pc    += 2;
  if(state.flags & FLAG_Z) {
    state.cycle += 2; 
  } else {
    state.pc    += (signed char)o;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  }

  if(trace_level & TRACE_OP)
     trace("BNE %02i", (signed char)o);
}

void opD8(void) {  // CLD
  state.flags &= ~FLAG_D;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("CLD", 0);
}

void opDD(void) {  // CMP abs,X
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  uint8_t  d = mem_read(o+state.x);
  state.pc    += 3;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if((state.x-d) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x-d) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.x<d)          state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  
  if(trace_level & TRACE_OP)
     trace("CMP %04X, X", o);
}

void opE8(void) {  // INX
  state.x     += 1;
  if((state.x) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("INX", 0);
}


void opEA(void) {  // NOP
  state.pc    += 1;
  state.cycle += 2;
  if(trace_level & TRACE_OP)
     trace("NOP",0);
}

/********************************************************************************/
/*************** END OF ALL THE OPCODE IMPLEMENTATOINS **************************/
/********************************************************************************/

uint8_t trace_len[256] = {
//           00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
/* 00 */       1,    1,    0,    0,    0,    2,    2,    0,    1,    2,    1,    0,    0,    3,    3,    0,
/* 10 */       2,    1,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 20 */       3,    1,    0,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* 30 */       2,    1,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 40 */       1,    1,    0,    0,    0,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* 50 */       2,    1,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 60 */       1,    1,    0,    0,    0,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* 70 */       2,    1,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 80 */       0,    1,    0,    0,    2,    2,    2,    0,    1,    0,    1,    0,    1,    3,    3,    0,
/* 90 */       2,    1,    0,    0,    2,    2,    2,    0,    1,    3,    1,    0,    0,    3,    0,    0,
/* A0 */       2,    1,    2,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* B0 */       2,    1,    0,    0,    2,    2,    2,    0,    1,    3,    1,    0,    1,    3,    3,    0,
/* C0 */       2,    1,    0,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* D0 */       2,    1,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* E0 */       2,    1,    0,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* F0 */       2,    1,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0};

void (*dispatch[256])(void) = {
//           00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
/* 00 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 10 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 20 */    op20, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 30 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 40 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 50 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 60 */    op60, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 70 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op78, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 80 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 90 */    NULL, NULL, NULL, NULL, NULL, op95, NULL, NULL, NULL, NULL, op9A, NULL, NULL, op9D, NULL, NULL,
/* A0 */    NULL, NULL, opA2, NULL, NULL, NULL, opA6, NULL, NULL, opA9, opAA, NULL, NULL, NULL, NULL, NULL,
/* B0 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, opBD, NULL, NULL,
/* C0 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* D0 */    opD0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, opD8, NULL, NULL, NULL, NULL, opDD, NULL, NULL,
/* E0 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, opE8, NULL, opEA, NULL, NULL, NULL, NULL, NULL,
/* F0 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

void trace(char *msg, uint16_t op) {
  int i;
  uint8_t inst;
  inst = mem_read_nolog(trace_addr);
  printf("%04X: %02X ", trace_addr, inst);

  for(i = 1; i < trace_len[inst]; i++) {
    printf("%02X ", mem_read_nolog(trace_addr+i));
  }
  while(i < 4) {
    printf("   ");
    i++;
  }
  printf(msg, op);
  printf("\n");
}

void logger_16(char *message, uint16_t data) {
  printf("%s %04x\n", message, data);
}

void logger_8(char *message, uint8_t data) {
  printf("%s %02x\n", message, data);
}

void logger_16_8(char *message, uint16_t data16, uint8_t data8) {
  printf("%s %04x %02x\n", message, data16, data8);
}

void dump(void) {
   printf("\n");
   printf("Fault at cycle %i\n",state.cycle);
   printf("PC:    %04x\n",state.pc);
   printf("flags: %02x ",state.flags);
   putchar(state.flags & FLAG_N ? 'N' : ' '); 
   putchar(state.flags & FLAG_V ? 'V' : ' '); 
   putchar(' '); 
   putchar(state.flags & FLAG_D ? 'D' : ' '); 
   putchar(state.flags & FLAG_I ? 'I' : ' '); 
   putchar(state.flags & FLAG_Z ? 'Z' : ' '); 
   putchar(state.flags & FLAG_C ? 'C' : ' '); 
   putchar('\n'); 

   printf("A:     %02x\n",state.a);
   printf("X:     %02x\n",state.x);
   printf("Y:     %02x\n",state.y);
   printf("SP:    %02x\n",state.sp);
}

uint8_t mem_read(uint16_t addr) {
  uint8_t rtn;

  if(addr < 0xC000) 
    rtn = ram[addr];
  else if(addr < 0xE000) 
    rtn =  rom1[addr-0xC000];
  else if(addr <= 0xFFFF)
    rtn =  rom2[addr-0xE000];
  else  {
    logger_16("Read from unmapped address",addr);
    return 0;
  }
  if(trace_level & TRACE_MEM)
    logger_16_8("  Read ",addr, rtn);
  return rtn;
}

uint8_t mem_read_nolog(uint16_t addr) {
  uint8_t rtn;

  if(addr < 0xC000) 
    rtn = ram[addr];
  else if(addr < 0xE000) 
    rtn =  rom1[addr-0xC000];
  else if(addr <= 0xFFFF)
    rtn =  rom2[addr-0xE000];
  else  {
    return 0;
  }
  return rtn;
}


void mem_write(uint16_t addr, uint8_t data) {
  if(trace_level & TRACE_MEM)
    logger_16_8("  Write", addr, data);

  if(addr < 0xE000)  {
      ram[addr] = data;
      return;
  }
  logger_16_8("Write to unmapped address", addr, data);
}

int cpu_run(void) {
   uint8_t inst = mem_read(state.pc);
   trace_addr   = state.pc; 
   trace_opcode = inst;
   if(dispatch[inst]==0) {
      logger_16_8("Unknown opcode at address",state.pc, inst);
      dump();
      return 0;
   }
   dispatch[inst]();
   return 1;
}

void cpu_reset(void) {
   trace("RESET triggerd",0);
   state.sp     = 0xFD;   
   state.pc     = mem_read(0xFFFC);
   state.pc    |= mem_read(0xFFFD)<<8;   
   state.flags |= FLAG_I;
   state.cycle  = 0;   
}

int rom1_load() {
   FILE *f = fopen("rom1.img","rb");
   if(f == NULL) {
      fprintf(stderr, "Unable to open 'rom.img'\n");
      return 0;
   }
   if(fread(rom1,8192,1,f) != 1) {
      fclose(f);
      return 0;
   }
   fclose(f);
   printf("ROM1 loaded\n");
   return 1;
}

int rom2_load() {
   FILE *f = fopen("rom2.img","rb");
   if(f == NULL) {
      fprintf(stderr, "Unable to open 'rom.img'\n");
      return 0;
   }
   if(fread(rom2,8192,1,f) != 1) {
      fclose(f);
      return 0;
   }
   fclose(f);
   printf("ROM2 loaded\n");
   return 1;
}

int main(int argc, char *argv[]) {
   if(rom1_load() && rom2_load()) {
      cpu_reset();
      while(cpu_run()) {
         ;
      }
   }
   return 0;
}
