#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

/************************************
* Memory contents 
************************************/
static uint8_t ram[1024*56];
static uint8_t rom1[1024*8];
static uint8_t rom2[1024*8];

static uint8_t mem_read(uint16_t addr);
static uint8_t mem_read_nolog(uint16_t addr);
static void mem_write(uint16_t addr, uint8_t data);
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
static void cpu_dump(void);
/**************************************
* For tracing execution
***************************************/
static void trace(char *msg, int32_t op);
static uint16_t trace_addr;
static uint8_t trace_opcode;
#define TRACE_OFF 0
#define TRACE_OP  1
#define TRACE_RD  2
#define TRACE_WR  4

//static int trace_level = TRACE_OP|TRACE_RD|TRACE_WR;
//static int trace_level = TRACE_OP|TRACE_WR;
//static int trace_level = TRACE_OP;
static int trace_level = TRACE_OFF;


/********************************************************************************/
/*************** START OF ALL THE OPCODE IMPLEMENTATOINS ************************/
/********************************************************************************/

static void op05(void) {  // AND zpg
  uint16_t z = mem_read(state.pc+1);
  state.a |= mem_read(z);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("AND zeropage %02X", z);
}

static void op08(void) {  // PHP
  mem_write(0x100+state.sp,   state.flags);
  state.pc    += 1;
  state.sp    -= 1; 
  state.cycle += 3; 
  if(trace_level & TRACE_OP)
     trace("PHP", 0);
}

static void op18(void) {  // CLC
  state.flags &= ~FLAG_C;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("CLC", 0);
}

static void op20(void) {  // JSR
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

static void op21(void) {  // AND (zpg, X)
  uint8_t  z = mem_read(state.pc+1)+state.x;
  uint16_t o = mem_read(z) | (mem_read(z+1)<<8);
  state.a  &= mem_read(o);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 2;
  state.cycle += 6;
  if(trace_level & TRACE_OP)
     trace("AND (zeropage %02X, X)", z);
}

static void op25(void) {  // AND zpg
  uint16_t z = mem_read(state.pc+1);
  state.a &= mem_read(z);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("AND zeropage %02X", z);
}

static void op4C(void) {  // JMP
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  state.pc     = o;
  state.cycle += 3; 
  if(trace_level & TRACE_OP)
     trace("JMP #%04X", o);
}

static void op60(void) {  // RTS
  uint16_t o = mem_read(0x100+state.sp+1) | (mem_read(0x100+state.sp+2)<<8);
  state.sp    += 2; 
  state.pc     = o+1;
  state.cycle += 6; 
  if(trace_level & TRACE_OP) {
     trace("RTS", o);
  }
//  printf("Returning to %04X\n",o+1);
}

static void op6A(void) {  // ROR A
  uint16_t t = state.a;
  if(state.flags & FLAG_C)
      t |= 0x100;
  if(t&1) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  state.a = t>>1;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("ROR A", 0);
}

static void op78(void) {  // SEI
  state.flags |= FLAG_I;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("SEI", 0);
}

static void op84(void) {  // STY zpg
  uint16_t z = mem_read(state.pc+1);
  mem_write(z, state.y);
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STY zeropage %02X", z);
}

static void op85(void) {  // STA zpg
  uint16_t z = mem_read(state.pc+1);
  mem_write(z, state.a);
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STA zeropage %02X", z);
}

static void op86(void) {  // STX zpg
  uint16_t z = mem_read(state.pc+1);
  mem_write(z, state.x);
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STX zeropage %02X", z);
}

static void op88(void) {  // DEY
  state.y     -= 1;
  if((state.y) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.y) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("DEY", 0);
}

static void op8A(void) {  // TXA
  state.a      = state.x;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("TXA", 0);
}

static void op8C(void) {  // STY abs
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  mem_write(o, state.y);
  state.pc    += 3;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STY %04X", o);
}


static void op8D(void) {  // STA abs
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  mem_write(o, state.a);
  state.pc    += 3;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STA %04X", o);
}

static void op8E(void) {  // STX abs
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  mem_write(o, state.x);
  state.pc    += 3;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STX %04X", o);
}

static void op90(void) {  // BCC rel
  uint8_t o = mem_read(state.pc+1);
  state.pc    += 2;
  if(state.flags & FLAG_C) {
    state.cycle += 2; 
  } else {
    state.pc    += (signed char)o;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  }

  if(trace_level & TRACE_OP)
     trace("BCC %02i", (signed char)o);
}

static void op91(void) {  // STA (zpg), y
  uint16_t z = mem_read(state.pc+1);
  uint16_t o = mem_read(z) | (mem_read(z+1)<<8);
  mem_write(o+state.y,state.a);
  state.pc    += 2;
  state.cycle += 6;
  if(trace_level & TRACE_OP)
     trace("STA (zeropage %02X), Y", z);
}

static void op95(void) {  // STA zpg, X
  uint16_t z = mem_read(state.pc+1);
  uint16_t o = mem_read(z) | (mem_read(z+1)<<8);
  mem_write(o+state.x,state.a);
  state.pc    += 2;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STA zeropage %02X, X", z);
}


static void op9A(void) {  // TXS
  state.sp     = state.x;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("TXS", 0);
}

static void op9D(void) {  // STA abs, X
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  mem_write(o+state.x, state.a);
  state.pc    += 3;
  state.cycle += 4;
  if(trace_level & TRACE_OP)
     trace("STA %04X, X", o);
}

static void opA0(void) {  // LDY #
  uint8_t o = mem_read(state.pc+1);
  state.y      = o;
  if(state.y == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.y &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 2;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("LDY #%02X", o);
}

static void opA2(void) {  // LDX #
  uint8_t o = mem_read(state.pc+1);
  state.x      = o;
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 2;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("LDX #%02X", o);
}

static void opA4(void) {  // LDY zeropage
  uint8_t z = mem_read(state.pc+1);
  uint8_t o = mem_read(z);
  state.y      = o;
  if(state.y == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.y &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 3; 
  if(trace_level & TRACE_OP)
     trace("LDA zeropage %02X", z);
}

static void opA5(void) {  // LDA zeropage
  uint8_t z = mem_read(state.pc+1);
  uint8_t o = mem_read(z);
  state.a      = o;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 3; 
  if(trace_level & TRACE_OP)
     trace("LDA zeropage %02X", z);
}

static void opA6(void) {  // LDX zeropage
  uint8_t z = mem_read(state.pc+1);
  uint8_t o = mem_read(z);
  state.x      = o;
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 3; 
  if(trace_level & TRACE_OP)
     trace("LDX zeropage %02X", z);
}

static void opA8(void) {  // TAY
  state.y      = state.a;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("TAY", 0);
}

static void opA9(void) {  // LDA #
  uint8_t o = mem_read(state.pc+1);
  state.a      = o;
  if(state.a == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("LDA #%02X", o);
}

static void opAA(void) {  // TAX
  state.x      = state.a;
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("TAX", 0);
}

static void opB0(void) {  // BCS rel
  uint8_t o = mem_read(state.pc+1);
  state.pc    += 2;
  if(state.flags & FLAG_C) {
    state.pc    += (signed char)o;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  } else {
    state.cycle += 2; 
  }

  if(trace_level & TRACE_OP)
     trace("BCS %02i", (signed char)o);
}

static void opB1(void) {  // LDA (zpg), y
  uint16_t z = mem_read(state.pc+1);
  uint16_t o = mem_read(z) | (mem_read(z+1)<<8);
  state.a = mem_read(o+state.y);
  state.pc    += 2;
  state.cycle += 6;
  if(trace_level & TRACE_OP)
     trace("LDA (zeropage %02X), Y", z);
}

static void opB9(void) {  // LDA abs, Y
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  state.a      = mem_read(o+state.y);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 3;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(trace_level & TRACE_OP)
     trace("LDA %04X, Y", o);
}

static void opBD(void) {  // LDA abs, X
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  state.a      = mem_read(o+state.x);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.pc    += 3;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(trace_level & TRACE_OP)
     trace("LDA %04X, X", o);
}

static void opC0(void) {  // CPY #
  uint16_t o = mem_read(state.pc+1);
  uint8_t  d = state.y - o;
  state.pc    += 2;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)       state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)     state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.y >= o) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
 
  if(trace_level & TRACE_OP)
     trace("CPY #%02X", o);
}

static void opC9(void) {  // CMP #
  uint8_t o = mem_read(state.pc+1);
  uint8_t d = state.a - o;

  if(d == 0)       state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)     state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= o) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;

  state.pc    += 2;
  state.cycle += 2; 
  if(trace_level & TRACE_OP) {
     trace("CMP #%02X", o);
  }
}

static void opCA(void) {  // DEX
  state.x     -= 1;
  if((state.x) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("DEX", 0);
}

static void opD0(void) {  // BNE rel
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

static void opD1(void) {  // CMP (zpg), y
  uint16_t z = mem_read(state.pc+1);
  uint16_t o = mem_read(z) | (mem_read(z+1)<<8);
  uint8_t  m = mem_read(o+state.y);
  uint8_t  d = state.a - m;
  state.pc    += 2;
  state.cycle += 6;
  
  if(d == 0)       state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)     state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= m) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  
  if(trace_level & TRACE_OP)
     trace("CMP (zeropage %02X), Y", z);
}

static void opD8(void) {  // CLD
  state.flags &= ~FLAG_D;
  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("CLD", 0);
}

static void opDD(void) {  // CMP abs,X
  uint16_t o = mem_read(state.pc+1) | (mem_read(state.pc+2)<<8);
  uint8_t  m = mem_read(o+state.x);
  uint8_t  d = state.a - m;
  state.pc    += 3;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)       state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)     state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= m) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.a = d;
 
  if(trace_level & TRACE_OP)
     trace("CMP %04X, X", o);
}

static void opE8(void) {  // INX
  state.x     += 1;
  if((state.x) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 1;
  state.cycle += 2; 
  if(trace_level & TRACE_OP)
     trace("INX", 0);
}

static void opE6(void) {  // INC zeropage
  uint8_t z = mem_read(state.pc+1);
  uint8_t t = mem_read(z);
  t++;
  mem_write(z,t);
  if(t == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.pc    += 2;
  state.cycle += 5; 
  if(trace_level & TRACE_OP)
     trace("INC zeropage %02X", z);
}

static void opEA(void) {  // NOP
  state.pc    += 1;
  state.cycle += 2;
  if(trace_level & TRACE_OP)
     trace("NOP",0);
}

static void opF0(void) {  // BEQ rel
  uint8_t o = mem_read(state.pc+1);
  state.pc    += 2;
  if(state.flags & FLAG_Z) {
    state.pc    += (signed char)o;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  } else {
    state.cycle += 2; 
  }

  if(trace_level & TRACE_OP)
     trace("BEQ %02i", (signed char)o);
}

/********************************************************************************/
/*************** END OF ALL THE OPCODE IMPLEMENTATOINS **************************/
/********************************************************************************/

static uint8_t trace_len[256] = {
//           00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
/* 00 */       1,    2,    0,    0,    0,    2,    2,    0,    1,    2,    1,    0,    0,    3,    3,    0,
/* 10 */       2,    2,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 20 */       3,    2,    0,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* 30 */       2,    2,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 40 */       1,    2,    0,    0,    0,    2,    2,    0,    1,    2,    1,    0,    3,    3,    3,    0,
/* 50 */       2,    2,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 60 */       1,    2,    0,    0,    0,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* 70 */       2,    2,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* 80 */       0,    2,    0,    0,    2,    2,    2,    0,    1,    0,    1,    0,    3,    3,    3,    0,
/* 90 */       2,    2,    0,    0,    2,    2,    2,    0,    1,    3,    1,    0,    0,    3,    0,    0,
/* A0 */       2,    2,    2,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* B0 */       2,    2,    0,    0,    2,    2,    2,    0,    1,    3,    1,    0,    1,    3,    3,    0,
/* C0 */       2,    2,    0,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* D0 */       2,    2,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0,
/* E0 */       2,    2,    0,    0,    2,    2,    2,    0,    1,    2,    1,    0,    1,    3,    3,    0,
/* F0 */       2,    2,    0,    0,    0,    2,    2,    0,    1,    3,    0,    0,    0,    3,    3,    0};

static void (*dispatch[256])(void) = {
//           00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
/* 00 */    NULL, NULL, NULL, NULL, NULL, op05, NULL, NULL, op08, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 10 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op18, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 20 */    op20, op21, NULL, NULL, NULL, op25, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 30 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 40 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op4C, NULL, NULL, NULL,
/* 50 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 60 */    op60, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op6A, NULL, NULL, NULL, NULL, NULL,
/* 70 */    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op78, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 80 */    NULL, NULL, NULL, NULL, op84, op85, op86, NULL, op88, NULL, op8A, NULL, op8C, op8D, op8E, NULL,
/* 90 */    op90, op91, NULL, NULL, NULL, op95, NULL, NULL, NULL, NULL, op9A, NULL, NULL, op9D, NULL, NULL,
/* A0 */    opA0, NULL, opA2, NULL, opA4, opA5, opA6, NULL, opA8, opA9, opAA, NULL, NULL, NULL, NULL, NULL,
/* B0 */    opB0, opB1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, opB9, NULL, NULL, NULL, opBD, NULL, NULL,
/* C0 */    opC0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, opC9, opCA, NULL, NULL, NULL, NULL, NULL,
/* D0 */    opD0, opD1, NULL, NULL, NULL, NULL, NULL, NULL, opD8, NULL, NULL, NULL, NULL, opDD, NULL, NULL,
/* E0 */    NULL, NULL, NULL, NULL, NULL, NULL, opE6, NULL, opE8, NULL, opEA, NULL, NULL, NULL, NULL, NULL,
/* F0 */    opF0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

static void trace(char *msg, int32_t op) {
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
  if(state.flags & FLAG_C) 
    printf("C ");
  else
    printf("  ");

  printf(msg, op);
  printf("\n");
}

static void logger_16(char *message, uint16_t data) {
  printf("%s %04X\n", message, data);
}

static void logger_8(char *message, uint8_t data) {
  printf("%s %02X\n", message, data);
}

static void logger_16_8(char *message, uint16_t data16, uint8_t data8) {
  printf("%s %04X %02X\n", message, data16, data8);
}

static void cpu_dump(void) {
   printf("\n");
   printf("Fault at cycle %i\n",state.cycle);
   printf("PC:    %04x\n",state.pc);
   printf("flags: %02X ",state.flags);
   putchar(state.flags & FLAG_N ? 'N' : ' '); 
   putchar(state.flags & FLAG_V ? 'V' : ' '); 
   putchar(' '); 
   putchar(state.flags & FLAG_D ? 'D' : ' '); 
   putchar(state.flags & FLAG_I ? 'I' : ' '); 
   putchar(state.flags & FLAG_Z ? 'Z' : ' '); 
   putchar(state.flags & FLAG_C ? 'C' : ' '); 
   putchar('\n'); 

   printf("A:     %02X\n",state.a);
   printf("X:     %02X\n",state.x);
   printf("Y:     %02X\n",state.y);
   printf("SP:    %02X\n",state.sp);
}

#if 0
DETAILS OF THE VIC CHIP FROM
http://www.zimmers.net/cbmpics/cbm/vic/memorymap.txt

HEX       DECIMAL       DESCRIPTION

9000-900F    36864-36879   Address of VIC chip registers
9000     36864   bits 0-6 horizontal centering
                           bit 7 sets interlace scan
9001     36865   vertical centering
9002     36866   bits 0-6 set # of columns
                           bit 7 is part of video matrix address
9003     36867   bits 1-6 set # of rows
                           bit 0 sets 8x8 or 16x8 chars
9004     36868   TV raster beam line
9005     36869   bits 0-3 start of character memory
                           (default = 0)
                           bits 4-7 is rest of video address
                           (default= F)
                           BITS 3,2,1,0 CM startinq address
                                        HEX   DEC
                           0000   ROM   8000  32768
                           0001         8400  33792
                           0010         8800  34816
                           0011         8C00  35840
                           1000   RAM   0000  0000
                           1001 xxxx
                           1010 xxxx  unavail.
                           1011 xxxx
                           1100         1000  4096
                           1101         1400  5120
                           1110         1800  6144
                           1111         1C00  7168

9006     36870    horizontal position of light pen
9007     36871    vertical position of light pen
9008     36872    Digitized value of paddle X
9009     36873    Digitized value of paddle Y
900A     36874    Frequency for oscillator 1 (low)
                  (on: 128-255)
900B     36875    Frequency for oscillator 2 (medium)
                  (on: 128-255)
900C     36876    Frequency for oscillator 3 (high)
                  (on: 128-255)
900D     36877    Frequency of noise source
900E     36878    bit 0-3 sets volume of all sound
                  bits 4-7 are auxiliary color information
900F     36879    Screen and border color register
                   bits 4-7 select background color
                   bits 0-2 select border color
                   bit 3 selects inverted or normal mode

#endif
static uint8_t vic_read(uint16_t addr) {
   assert(addr < 0x10);
   return 0;
}
static void vic_write(uint16_t addr, uint8_t data) {
   printf("VIC write %04X %02X\n",addr,data);
   assert(addr < 0x10);
}

static uint8_t mem_read(uint16_t addr) {
  uint8_t rtn = 0;

  if(addr < 0x4000) 
    rtn = ram[addr];
  else if(addr >= 0x9000 && addr< 0x9010)
    rtn = vic_read(addr-0x9000);
  else if(addr == 0xA008) {
    logger_16("Expansion ROM probe at ",addr);
  }
  else if(addr < 0xC000) {
    if(addr != 0x4000) {
      logger_16("Read from unmapped address",addr);
      trace_level |= TRACE_OP;
    }
    rtn = 0;
  }
  else if(addr < 0xE000) 
    rtn =  rom1[addr-0xC000];
  else if(addr <= 0xFFFF)
    rtn =  rom2[addr-0xE000];
  else {
    logger_16("Read from unmapped address",addr);
  }
  if(trace_level & TRACE_RD)
    logger_16_8("  Read ",addr, rtn);
  return rtn;
}

static uint8_t mem_read_nolog(uint16_t addr) {
  uint8_t rtn;

  if(addr < 0x4000) 
    rtn = ram[addr];
  else if(addr >= 0x9000 && addr< 0x9010)
    rtn = vic_read(addr-0x9000);
  else if(addr < 0xC000) {
    rtn = 0;
  }
  else if(addr < 0xE000) 
    rtn =  rom1[addr-0xC000];
  else if(addr <= 0xFFFF)
    rtn =  rom2[addr-0xE000];
  else  {
    rtn = 0;
  }
  return rtn;
}


static void mem_write(uint16_t addr, uint8_t data) {
  if(trace_level & TRACE_WR)
    logger_16_8("  Write", addr, data);

  if(addr < 0x4000)  {
      ram[addr] = data;
      return;
  }
  if(addr >= 0x9000 && addr< 0x9010) {
    vic_write(addr-0x9000, data);
    return;
  }
  if(addr != 0x4000) {
    logger_16_8("Write to unmapped address", addr, data);
    trace_level |= TRACE_OP;
  }
}

static int cpu_run(void) {
   uint8_t inst = mem_read(state.pc);
   trace_addr   = state.pc; 
   trace_opcode = inst;
   if(dispatch[inst]==0) {
      logger_16_8("Unknown opcode at address",state.pc, inst);
      cpu_dump();
      return 0;
   }
   dispatch[inst]();
   return 1;
}

static void cpu_reset(void) {
   trace("RESET triggerd",0);
   state.sp     = 0xFD;   
   state.pc     = mem_read(0xFFFC);
   state.pc    |= mem_read(0xFFFD)<<8;   
   state.flags |= FLAG_I;
   state.cycle  = 0;   
}

static int rom1_load() {
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

static int rom2_load() {
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
      if(0)
        logger_8("remove warning for unused logger_8()",0);
   }
   return 0;
}
