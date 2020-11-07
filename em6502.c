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
static uint8_t mem_fetch(uint16_t addr);
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
static void trace(char *msg);
static uint16_t trace_addr;
static uint8_t trace_opcode;
static int32_t trace_num;
static uint8_t trace_fetch_len;
#define TRACE_OFF 0
#define TRACE_OP  1
#define TRACE_RD  2
#define TRACE_WR  4
#define TRACE_FETCH  8

//static int trace_level = TRACE_OP|TRACE_RD|TRACE_WR|TRACE_FETCH;
//static int trace_level = TRACE_OP|TRACE_WR;
//static int trace_level = TRACE_OP;
static int trace_level = TRACE_OFF;


/********************************************************************************/
/*************** START OF ALL THE OPCODE IMPLEMENTATOINS ************************/
/********************************************************************************/

static uint16_t addr_absolute(void) {
  uint16_t rtn = mem_fetch(state.pc) | (mem_fetch(state.pc)<<8);
  trace_num = rtn;
  return rtn;
}

static uint16_t addr_absolute_x(void) {
  uint16_t rtn = mem_fetch(state.pc) | (mem_fetch(state.pc)<<8);
  trace_num = rtn;
  return rtn+state.x;
}

static uint16_t addr_absolute_y(void) {
  uint16_t rtn = mem_fetch(state.pc) | (mem_fetch(state.pc)<<8);
  trace_num = rtn;
  return rtn+state.y;
}

static uint16_t addr_zpg(void) {
  uint16_t rtn = mem_fetch(state.pc);
  trace_num = rtn;
  return rtn;
}

static uint16_t addr_zpg_ind_y(void) {
  uint16_t z = mem_fetch(state.pc);
  trace_num = z;
  uint16_t rtn = mem_read(z) | (mem_read(z+1)<<8);
  return rtn+state.y;
}

static uint8_t immediate(void) {
  uint8_t rtn = mem_fetch(state.pc);
  trace_num = rtn;
  return rtn;
}

static int8_t relative(void) {
  uint8_t rtn = mem_fetch(state.pc);
  trace_num = (int8_t)rtn;
  return (int8_t)rtn;
}

static uint8_t addr_zpg_x(void) {
  uint8_t  rtn = mem_fetch(state.pc)+state.x;
  trace_num = rtn;
  return rtn;
}

static uint8_t addr_zpg_x_ind(void) {
  uint8_t  z = mem_fetch(state.pc)+state.x;
  uint16_t rtn = mem_read(z) | (mem_read(z+1)<<8);
  trace_num = rtn;
  return rtn;
}

/******************************************************************************/
static void op00(void) {  // BRK     
   trace("BRK");
   mem_write(0x100+state.sp,   state.pc>>8);
   mem_write(0x100+state.sp-1, state.pc&0xFF);
   mem_write(0x100+state.sp-2, state.flags);    // TODO: SET THE BREAK BITS appropriately
   state.sp    -= 3; 
   state.pc     = mem_read(0xFFFC);
   state.pc    |= mem_read(0xFFFD)<<8;   
   state.flags |= FLAG_I;
   trace("BRK");
}

static void op01(void) {  // ORA (zpg, X)
  state.a  |= mem_read(addr_zpg_x_ind());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6;
  trace("ORA (zeropage %02X, X)");
}

static void op05(void) {  // AND zpg
  state.a |= mem_read(addr_zpg());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND zeropage %02X");
}

static void op08(void) {  // PHP
  mem_write(0x100+state.sp,   state.flags);
  state.sp    -= 1; 
  state.cycle += 3; 
  trace("PHP");
}

static void op09(void) {  // AND #
  state.a |= immediate();
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND #%02X");
}

static void op0A(void) {  // ASL A
  uint16_t t = state.a;
  if(t&1) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  state.a = (t>>1) | (t&0x80);
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 2; 
  trace("ASL A");
}

static void op0D(void) {  // ORA abs
  state.a      |= mem_read(addr_absolute());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDA #%04X");
}

static void op10(void) {  // BPL rel
  int8_t offset = relative();
  if(state.flags & FLAG_N) {
    state.cycle += 2; 
  } else {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  }
  trace("BPL %02i");
}

static void op11(void) {  // ORA (zpg), Y
  state.a  |= mem_read(addr_zpg_ind_y());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6;
  trace("ORA (zeropage %02X), Y");
}

static void op18(void) {  // CLC
  state.flags &= ~FLAG_C;
  state.cycle += 2; 
  trace("CLC");
}

static void op20(void) {  // JSR
  uint16_t a = addr_absolute();
  mem_write(0x100+state.sp,   (state.pc-1)>>8);
  mem_write(0x100+state.sp-1, (state.pc-1)&0xFF);
  state.sp    -= 2; 
  state.pc     = a;
  state.cycle += 6; 
  trace("JSR #%04X");
}

static void op21(void) {  // AND (zpg, X)
  state.a  &= mem_read(addr_zpg_x_ind());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6;
  trace("AND (zeropage %02X, X)");
}

static void op25(void) {  // AND zpg
  state.a &= mem_read(addr_zpg());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND zeropage %02X");
}

static void op29(void) {  // AND #
  state.a &= immediate();
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND #%02X");
}

static void op30(void) {  // BMI rel
  int8_t offset = relative();
  if(state.flags & FLAG_N) {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  } else {
    state.cycle += 2; 
  }
  trace("BMI %02i");
}

static void op31(void) {  // AND (zpg), Y
  state.a  &= mem_read(addr_zpg_ind_y());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6;
  trace("AND (zeropage %02X), Y");
}

static void op40(void) {  // RTI
  uint16_t o;
  state.flags = mem_read(0x100+state.sp+1);
  o = mem_read(0x100+state.sp+2) | (mem_read(0x100+state.sp+3)<<8);
  state.sp    += 3; 
  state.pc     = o;
  state.cycle += 6; 
  trace("RTI");
}

static void op41(void) {  // EOR (zpg, X)
  state.a  ^= mem_read(addr_zpg_x_ind());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6;
  trace("EOR (zeropage %02X, X)");
}


static void op4C(void) {  // JMP
  uint16_t o = addr_absolute();
  state.pc     = o;
  state.cycle += 3; 
  trace("JMP #%04X");
}

static void op50(void) {  // BVC rel
  int8_t offset = relative();
  if(state.flags & FLAG_V) {
    state.cycle += 2; 
  } else {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  }
  trace("BVC %02i");
}

static void op51(void) {  // EOR (zpg), Y
  state.a  ^= mem_read(addr_zpg_ind_y());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6;
  trace("EOR (zeropage %02X), Y");
}

static void op58(void) {  // CLI
  state.flags &= ~FLAG_I;
  state.cycle += 2; 
  trace("CLI");
}

static void op60(void) {  // RTS
  uint16_t o = mem_read(0x100+state.sp+1) | (mem_read(0x100+state.sp+2)<<8);
  state.sp    += 2; 
  state.pc     = o+1;
  state.cycle += 6; 
  trace("RTS");
}

static void op61(void) {  // ADC (ind, X)
  uint16_t t = state.a;
  t += mem_read(addr_zpg_x_ind());
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC (%02X, X)");
}

static void op69(void) {  // ADC #
  uint16_t t = state.a;
  t += immediate();
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC #%02X");
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

  state.cycle += 2; 
  trace("ROR A");
}

static void op70(void) {  // BVS rel
  int8_t offset = relative();
  if(state.flags & FLAG_V) {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  } else {
    state.cycle += 2; 
  }
  trace("BVS %02i");
}

static void op71(void) {  // ADC (ind), Y
  uint16_t t = state.a;
  t += mem_read(addr_zpg_ind_y());
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC (%02X), Y");
}

static void op78(void) {  // SEI
  state.flags |= FLAG_I;
  state.cycle += 2; 
  trace("SEI");
}

static void op81(void) {  // STA (zpg, X)
  mem_write(addr_zpg_x_ind(),state.a);
  state.cycle += 6;
  trace("STA (zeropage %02X, X)");
}

static void op84(void) {  // STY zpg
  mem_write(addr_zpg(), state.y);
  state.cycle += 4;
  trace("STY zeropage %02X");
}

static void op85(void) {  // STA zpg
  mem_write(addr_zpg(), state.a);
  state.cycle += 4;
  trace("STA zeropage %02X");
}

static void op86(void) {  // STX zpg
  mem_write(addr_zpg(), state.x);
  state.cycle += 4;
  trace("STX zeropage %02X");
}

static void op88(void) {  // DEY
  state.y     -= 1;
  if((state.y) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.y) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("DEY");
}

static void op8A(void) {  // TXA
  state.a      = state.x;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("TXA");
}

static void op8C(void) {  // STY abs
  mem_write(addr_absolute(), state.y);
  state.cycle += 4;
  trace("STY %04X");
}


static void op8D(void) {  // STA abs
  mem_write(addr_absolute(), state.a);
  state.cycle += 4;
  trace("STA %04X");
}

static void op8E(void) {  // STX abs
  mem_write(addr_absolute(), state.x);
  state.cycle += 4;
  trace("STX %04X");
}

static void op90(void) {  // BCC rel
  int8_t offset = relative();
  if(state.flags & FLAG_C) {
    state.cycle += 2; 
  } else {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  }
  trace("BCC %02i");
}

static void op91(void) {  // STA (zpg), y
  mem_write(addr_zpg_ind_y(),state.a);
  state.cycle += 6;
  trace("STA (zeropage %02X), Y");
}

static void op94(void) {  // STY zpg, X
  mem_write(addr_zpg_x(),state.y);
  state.cycle += 4;
  trace("STY zeropage %02X, X");
}

static void op95(void) {  // STA zpg, X
  mem_write(addr_zpg_x(),state.a);
  state.cycle += 4;
  trace("STA zeropage %02X, X");
}

static void op99(void) {  // STA abs, Y
  mem_write(addr_absolute_y(), state.a);
  state.cycle += 4;  // TODO: +1 if boundary crossed
  trace("STA %04X, Y");
}


static void op9A(void) {  // TXS
  state.sp     = state.x;
  state.cycle += 2; 
  trace("TXS");
}

static void op9D(void) {  // STA abs, X
  mem_write(addr_absolute(), state.a);
  state.cycle += 4;
  trace("STA %04X, X");
}

static void opA0(void) {  // LDY #
  state.y      = immediate();
  if(state.y == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.y &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDY #%02X");
}

static void opA1(void) {  // LDA (ind, X)
  state.a = mem_read(addr_zpg_x_ind());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 3; 
  trace("LDA zeropage %02X");
}

static void opA2(void) {  // LDX #
  state.x      = immediate();
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDX #%02X");
}

static void opA4(void) {  // LDY zeropage
  state.y = mem_read(addr_zpg());
  if(state.y == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.y &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 3; 
  trace("LDA zeropage %02X");
}

static void opA5(void) {  // LDA zeropage
  state.a = mem_read(addr_zpg());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 3; 
  trace("LDA zeropage %02X");
}

static void opA6(void) {  // LDX zeropage
  state.x = mem_read(addr_zpg());
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 3; 
  trace("LDX zeropage %02X");
}

static void opA8(void) {  // TAY
  state.y      = state.a;
  state.cycle += 2; 
  trace("TAY");
}

static void opA9(void) {  // LDA #
  state.a      = immediate();
  if(state.a == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDA #%02X");
}

static void opAA(void) {  // TAX
  state.x      = state.a;
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("TAX");
}

static void opAD(void) {  // LDA abs
  state.a      = mem_read(addr_absolute());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDA #%04X");
}

static void opB0(void) {  // BCS rel
  int8_t offset = relative();
  if(state.flags & FLAG_C) {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  } else {
    state.cycle += 2; 
  }
  trace("BCS %02i");
}

static void opB1(void) {  // LDA (zpg), y
  state.a = mem_read(addr_zpg_ind_y());
  state.cycle += 6;
  trace("LDA (zeropage %02X), Y");
}

static void opB4(void) {  // LDY zeropage, X
  state.y = mem_read(addr_zpg_x());
  if(state.y == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.y &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 3; 
  trace("LDY zeropage %02X, X");
}

static void opB5(void) {  // LDA zeropage, X
  state.a = mem_read(addr_zpg_x());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 3; 
  trace("LDA zeropage %02X");
}

static void opB9(void) {  // LDA abs, Y
  state.a      = mem_read(addr_absolute_y());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  trace("LDA %04X, Y");
}

static void opBD(void) {  // LDA abs, X
  state.a      = mem_read(addr_absolute_x());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  trace("LDA %04X, X");
}

static void opC0(void) {  // CPY #
  uint16_t val = immediate();
  uint8_t  d = state.y - val;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.y >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  trace("CPY #%02X");
}

static void opC8(void) {  // INY
  state.y     += 1;
  if((state.y) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.y) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("INX");
}

static void opC9(void) {  // CMP #
  uint8_t val = immediate();
  uint8_t d = state.a - val;
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 2; 
  trace("CMP #%02X");
}

static void opCA(void) {  // DEX
  state.x     -= 1;
  if((state.x) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("DEX");
}

static void opD0(void) {  // BNE rel
  int8_t offset = relative();
  if(state.flags & FLAG_Z) {
    state.cycle += 2; 
  } else {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  }
  trace("BNE %02i");
}

static void opD1(void) {  // CMP (zpg), y
  uint8_t  m = mem_read(addr_zpg_ind_y());
  uint8_t  d = state.a - m;
  state.cycle += 6;
  
  if(d == 0)       state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)     state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= m) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  
  trace("CMP (zeropage %02X), Y");
}

static void opD8(void) {  // CLD
  state.flags &= ~FLAG_D;
  state.cycle += 2; 
  trace("CLD");
}

static void opDD(void) {  // CMP abs,X
  uint8_t val = mem_read(addr_absolute_x());
  uint8_t d = state.a - val;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  trace("CMP %04X, X");
}

static void opE0(void) {  // CPX #
  uint16_t val = immediate();
  uint8_t  d = state.x - val;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.x >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  trace("CPX #%02X");
}


static void opE8(void) {  // INX
  state.x     += 1;
  if((state.x) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("INX");
}

static void opE6(void) {  // INC zeropage
  uint8_t z = addr_zpg();
  uint8_t t = mem_read(z);
  t++;
  mem_write(z,t);
  if(t == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 5; 
  trace("INC zeropage %02X");
}

static void opEA(void) {  // NOP
  state.cycle += 2;
  trace("NOP");
}

static void opF0(void) {  // BEQ rel
  int8_t offset = relative();
  if(state.flags & FLAG_Z) {
    state.pc    += offset;
    state.cycle += 3;  // TODO: +1 if boundary crossed
  } else {
    state.cycle += 2; 
  }
  trace("BEQ %02i");
}

/********************************************************************************/
/*************** END OF ALL THE OPCODE IMPLEMENTATOINS **************************/
/********************************************************************************/

static void (*dispatch[256])(void) = {
//           00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
/* 00 */    op00, op01, NULL, NULL, NULL, op05, NULL, NULL, op08, op09, op0A, NULL, NULL, op0D, NULL, NULL,
/* 10 */    op10, op11, NULL, NULL, NULL, NULL, NULL, NULL, op18, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 20 */    op20, op21, NULL, NULL, NULL, op25, NULL, NULL, NULL, op29, NULL, NULL, NULL, NULL, NULL, NULL,
/* 30 */    op30, op31, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 40 */    op40, op41, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op4C, NULL, NULL, NULL,
/* 50 */    op50, op51, NULL, NULL, NULL, NULL, NULL, NULL, op58, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 60 */    op60, op61, NULL, NULL, NULL, NULL, NULL, NULL, NULL, op69, op6A, NULL, NULL, NULL, NULL, NULL,
/* 70 */    op70, op71, NULL, NULL, NULL, NULL, NULL, NULL, op78, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
/* 80 */    NULL, op81, NULL, NULL, op84, op85, op86, NULL, op88, NULL, op8A, NULL, op8C, op8D, op8E, NULL,
/* 90 */    op90, op91, NULL, NULL, op94, op95, NULL, NULL, NULL, op99, op9A, NULL, NULL, op9D, NULL, NULL,
/* A0 */    opA0, opA1, opA2, NULL, opA4, opA5, opA6, NULL, opA8, opA9, opAA, NULL, NULL, opAD, NULL, NULL,
/* B0 */    opB0, opB1, NULL, NULL, opB4, opB5, NULL, NULL, NULL, opB9, NULL, NULL, NULL, opBD, NULL, NULL,
/* C0 */    opC0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, opC8, opC9, opCA, NULL, NULL, NULL, NULL, NULL,
/* D0 */    opD0, opD1, NULL, NULL, NULL, NULL, NULL, NULL, opD8, NULL, NULL, NULL, NULL, opDD, NULL, NULL,
/* E0 */    opE0, NULL, NULL, NULL, NULL, NULL, opE6, NULL, opE8, NULL, opEA, NULL, NULL, NULL, NULL, NULL,
/* F0 */    opF0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

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

/*****************************************************************/
static uint8_t via1_read(uint16_t addr) {
   assert(addr < 0x20);
   return 0;
}
static void via1_write(uint16_t addr, uint8_t data) {
   printf("VIA#1 write %04X %02X\n",addr,data);
   assert(addr < 0x20);
}
/*****************************************************************/
static uint8_t via2_read(uint16_t addr) {
   assert(addr < 0x20);
   return 0;
}
static void via2_write(uint16_t addr, uint8_t data) {
   printf("VIA#2 write %04X %02X\n",addr,data);
   assert(addr < 0x20);
}
/*****************************************************************/
static uint8_t mem_read_nolog(uint16_t addr) {
  uint8_t rtn;

  if(addr < 0x4000) 
    rtn = ram[addr];
  else if(addr >= 0x9000 && addr< 0x9010)
    rtn = vic_read(addr-0x9000);
  else if(addr >= 0x9110 && addr< 0x9120)
    rtn = via1_read(addr-0x9110);
  else if(addr >= 0x9120 && addr< 0x9130)
    rtn = via2_read(addr-0x9110);
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

static uint8_t mem_read(uint16_t addr) {
  uint8_t rtn = 0;
  rtn = mem_read_nolog(addr);
  if(trace_level & TRACE_RD)
    logger_16_8("  Read ",addr, rtn);
  return rtn;
}

static uint8_t mem_fetch(uint16_t addr) {
  uint8_t rtn = 0;
  rtn = mem_read_nolog(addr);
  state.pc++;
  trace_fetch_len++;
  if(trace_level & TRACE_FETCH)
    logger_16_8("  Fetch",addr, rtn);
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

  if(addr >= 0x9110 && addr< 0x9120) {
    via1_write(addr-0x9110, data);
    return;
  }

  if(addr >= 0x9120 && addr< 0x9130) {
    via2_write(addr-0x9110, data);
    return;
  }

//  if(addr != 0x4000) {
    logger_16_8("Write to unmapped address", addr, data);
    trace_level |= TRACE_OP;
//  }
}

static void trace(char *msg) {
  int i;
  uint8_t inst;
  if(!(trace_level & TRACE_OP))
     return;

  inst = mem_read_nolog(trace_addr);
  printf("%04X: %02X ", trace_addr, inst);

  for(i = 1; i < trace_fetch_len; i++) {
    printf("%02X ", mem_read_nolog(trace_addr+i));
  }

  while(i < 4) {
    printf("   ");
    i++;
  }
#if 0
  if(state.flags & FLAG_C) 
    printf("C ");
  else
    printf("  ");
#endif

  printf(msg, trace_num);
  printf("\n");
}


static int cpu_run(void) {
   uint8_t inst;
   trace_addr   = state.pc; 
   trace_fetch_len    = 0;
   inst         = mem_fetch(state.pc);
   trace_opcode = inst;
   if(dispatch[inst]==0) {
      logger_16_8("Unknown opcode at address",trace_addr, inst);
      cpu_dump();
      return 0;
   }
   dispatch[inst]();
   return 1;
}

static void cpu_reset(void) {
   trace("RESET triggerd");
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
