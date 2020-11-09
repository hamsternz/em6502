#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>

/************************************
* Memory contents 
************************************/
static uint8_t ram[1024*16];
static uint8_t rom1[1024*8];
static uint8_t rom2[1024*8];
static uint8_t rom3[1024*4];
static uint8_t vic[16];
static uint8_t colour[1024];

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
uint32_t last_display = 0;
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
//static int trace_level = TRACE_OP|TRACE_RD;
//static int trace_level = TRACE_OP|TRACE_WR|TRACE_RD;
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
  uint8_t  rtn = mem_fetch(state.pc);
  trace_num = rtn;
  rtn += state.x;
  return rtn;
}

static uint8_t addr_zpg_y(void) {
  uint8_t  rtn = mem_fetch(state.pc);
  trace_num = rtn;
  rtn += state.y;
  return rtn;
}

static uint8_t addr_zpg_x_ind(void) {
  uint8_t  z = mem_fetch(state.pc);
  trace_num = z;
  z += state.x;
  return mem_read(z+state.x) | (mem_read(z+state.x+1)<<8);
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

static void op05(void) {  // ORA zpg
  state.a |= mem_read(addr_zpg());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("ORA zeropage %02X");
}

static void op06(void) {  // ASL zpg
  uint16_t a = addr_zpg();
  uint16_t t = mem_read(a);
  if(t&0x80) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  t = t<<1;
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 5; 
  trace("ASL zeropage %02X");
  mem_write(a,t);
}

static void op08(void) {  // PHP
  mem_write(0x100+state.sp,   state.flags);
  state.sp    -= 1; 
  state.cycle += 3; 
  trace("PHP");
}

static void op09(void) {  // ORA #
  state.a |= immediate();
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("ORA #%02X");
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

static void op15(void) {  // ORA zpg, X
  state.a |= mem_read(addr_zpg_x());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("ORA zeropage %02X, X");
}

static void op16(void) {  // ASL zpg, X
  uint16_t a = addr_zpg_x();
  uint16_t t = mem_read(a);
  if(t&1) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  t = (t>>1) | (t&0x80);
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 5; 
  trace("ASL zeropage %02X, X");
  mem_write(a,t);
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

static void op24(void) {  // BIT zeropage
  uint8_t t = mem_read(addr_zpg());
  if((t & state.a) == 0) state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80)            state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x40)            state.flags |= FLAG_V;  else state.flags &= ~FLAG_V;
  state.cycle += 3; 
  trace("BIT zeropage %02X");
}

static void op25(void) {  // AND zpg
  state.a &= mem_read(addr_zpg());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND zeropage %02X");
}

static void op26(void) {  // ROL zpg
  uint16_t a = addr_zpg();
  uint16_t t = mem_read(a);

  t = (t<<1);
  if(state.flags & FLAG_C) 
    t |= 0x1;
  if(t&0x100) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 5; 
  mem_write(a,t);
  trace("ROL zeropage %02X");
}

static void op28(void) {  // PLP
  state.sp    += 1; 
  state.flags = mem_read(0x100+state.sp);
  state.cycle += 3;   // TODO: Fix up debug flags
  trace("PLP");
}


static void op29(void) {  // AND #
  state.a &= immediate();
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND #%02X");
}

static void op2A(void) {  // ROL A
  uint16_t t = state.a;
  t = (t<<1);
  if(state.flags & FLAG_C) 
    t |= 0x1;
  if(t&0x100) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.a = t;
  state.cycle += 2; 
  trace("ROL A");
}

static void op2C(void) {  // BIT abs
  uint8_t t = mem_read(addr_absolute());
  if((t & state.a) == 0) state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80)            state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x40)            state.flags |= FLAG_V;  else state.flags &= ~FLAG_V;
  state.cycle += 3; 
  trace("BIT zeropage %02X");
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

static void op35(void) {  // AND zpg, X
  state.a &= mem_read(addr_zpg_x());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("AND zeropage %02X, X");
}

static void op36(void) {  // ROL zpg, X
  uint16_t a = addr_zpg_x();
  uint16_t t = mem_read(a);

  t = t<<1;
  if(state.flags & FLAG_C) 
    t |= 0x1;
  if(t&0x100) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 5; 
  trace("ROL zeropage %02X, X");
  mem_write(a,t);
}

static void op38(void) {  // SEC
  state.flags |= FLAG_C;
  state.cycle += 2; 
  trace("SEC");
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

static void op45(void) {  // EOR zpg
  state.a ^= mem_read(addr_zpg());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("EOR zeropage %02X");
} 

static void op46(void) {  // LSR zpg
  uint16_t a = addr_zpg();
  uint16_t t = mem_read(a);
  if(state.flags & FLAG_C)
    t |= 0x100;
  if(t&0x100) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  t = t>>1;
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 5; 
  trace("LSR zeropage %02X");
  mem_write(a,t);
}

static void op48(void) {  // PHA
  mem_write(0x100+state.sp,   state.a);
  state.sp    -= 1; 
  state.cycle += 3; 
  trace("PHA");
}

static void op49(void) {  // EOR #
  state.a ^= immediate();
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("EOR #%02X");
}

static void op4A(void) {  // LSR A
  uint16_t t = state.a;
  if(state.flags & FLAG_C)
    t |= 0x100;
  if(t&0x1) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  t = t>>1;
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.a = t;

  state.cycle += 2; 
  trace("LSR A");
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

static void op55(void) {  // EOR zpg, X
  state.a ^= mem_read(addr_zpg_x());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 4;
  trace("EOR zeropage %02X, X");
} 

static void op56(void) {  // LSR zpg, X
  uint16_t a = addr_zpg_x();
  uint16_t t = mem_read(a);
  t = (t<<1);
  if(t&0x100) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;

  state.cycle += 5; 
  trace("LSR zeropage %02X, X");
  mem_write(a,t);
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
  // TODO - OVERFLOW FLAGS
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC (%02X, X)");
}

static void op65(void) {  // ADC zpg
  uint16_t t = state.a;
  t += mem_read(addr_zpg());
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  // TODO - OVERFLOW FLAGS
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC zeropage %02X");
}

static void op66(void) {  // ROR zpg
  uint16_t a = addr_zpg();
  uint16_t t = mem_read(a);
  if(state.flags & FLAG_C)
      t |= 0x100;
  if(t&1) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  t = t>>1;
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  mem_write(a,t);
  state.cycle += 5; 
  trace("ROR zpg %02X");
}

static void op68(void) {  // PLA
  state.sp    += 1; 
  state.a = mem_read(0x100+state.sp);
  state.cycle += 3; 
  trace("PLA");
}

static void op69(void) {  // ADC #
  uint16_t t = state.a;
  t += immediate();
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  // TODO - OVERFLOW FLAGS
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

static void op6C(void) {  // JMP (ind)
  uint16_t a = addr_absolute();
  if((a & 0xFF) == 0xFF) {
      a = mem_read(a) | (mem_read(a-0xFF)<<8);
  } else {
      a = mem_read(a) | (mem_read(a+1)<<8);
  } 
  state.pc = a;
  state.cycle += 5; 
  trace("JMP (%04X)");
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
  // TODO - OVERFLOW FLAGS
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC (%02X), Y");
}

static void op75(void) {  // ADC zpg, X
  uint16_t t = state.a;
  t += mem_read(addr_zpg_x());
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  // TODO - OVERFLOW FLAGS
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("ADC zeropage %02X, X");
}

static void op76(void) {  // ROR zpg, X
  uint16_t a = addr_zpg_x();
  uint16_t t = mem_read(a);
  if(state.flags & FLAG_C)
      t |= 0x100;
  if(t&1) { 
    state.flags |= FLAG_C;
  } else {
    state.flags &= ~FLAG_C;
  }
  t = t>>1;
  if(t == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  mem_write(a,t);
  state.cycle += 5; 
  trace("ROR zpg %02X, X");
}

static void op78(void) {  // SEI
  state.flags |= FLAG_I;
  state.cycle += 2; 
  trace("SEI");
}

static void op79(void) {  // ADC abs, Y
  uint16_t t = state.a;
  t += mem_read(addr_absolute_y());
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  // TODO - OVERFLOW FLAGS
  if(t & 0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 3;   // TODO Decimal mode
  trace("ADC %04X, Y");
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

static void op96(void) {  // STX zpg, Y
  mem_write(addr_zpg_y(), state.x);
  state.cycle += 4;
  trace("STX zeropage %02X, Y");
}

static void op98(void) {  // TYA
  state.a = state.y;
  state.cycle += 2; 
  trace("TYA");
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
  mem_write(addr_absolute_x(), state.a);
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

static void opAC(void) {  // LDY abs
  state.y      = mem_read(addr_absolute());
  if(state.y == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.y &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDY #%04X");
}

static void opAD(void) {  // LDA abs
  state.a      = mem_read(addr_absolute());
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDA #%04X");
}

static void opAE(void) {  // LDX abs
  state.x      = mem_read(addr_absolute());
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("LDX #%04X");
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
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
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
  trace("LDA zeropage %02X, X");
}

static void opB6(void) {  // LDX zeropage, Y
  state.x = mem_read(addr_zpg_y());
  if(state.x == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(state.x &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 3; 
  trace("LDX zeropage %02X, Y");
}


static void opB8(void) {  // CLV
  state.flags &= ~FLAG_V;
  state.cycle += 2; 
  trace("CLV");
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

static void opC1(void) {  // CMP (zpg, x)
  uint8_t  m = mem_read(addr_zpg_x_ind());
  uint8_t  d = state.a - m;
  state.cycle += 6;
  
  if(d == 0)       state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)     state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= m) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  
  trace("CMP (zeropage %02X, X)");
}

static void opC4(void) {  // CPY zpg
  uint16_t val = mem_read(addr_zpg());
  uint8_t  d = state.y - val;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.y >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  trace("CPX zeropage %02X");
}

static void opC5(void) {  // CMP zpg
  uint16_t val = mem_read(addr_zpg());
  uint8_t  d = state.a - val;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  trace("CMP zeropage %02X");
}

static void opC6(void) {  // DEC zeropage
  uint8_t z = addr_zpg();
  uint8_t t = mem_read(z);
  t--;
  mem_write(z,t);
  if(t == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 5; 
  trace("DEC zeropage %02X");
}

static void opC8(void) {  // INY
  state.y     += 1;
  if((state.y) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.y) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("INY");
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

static void opD5(void) {  // CMP zpg, X
  uint16_t val = mem_read(addr_zpg_x());
  uint8_t  d = state.a - val;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.a >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  trace("CMP zeropage %02X, X");
}

static void opD6(void) {  // DEC zeropage, X
  uint8_t z = addr_zpg_x();
  uint8_t t = mem_read(z);
  t--;
  mem_write(z,t);
  if(t == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6; 
  trace("DEC zeropage %02X");
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

static void opE1(void) {  // SBC zpg, X
  uint16_t t = state.a;
  t += mem_read(addr_zpg_x_ind()) ^ 0xFF;  // TODO - check Carry flags
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  // TODO - OVERFLOW FLAGS
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("SBC zeropage %02X, X");
}

static void opE4(void) {  // CPX zpg
  uint16_t val = mem_read(addr_zpg());
  uint8_t  d = state.x - val;
  if(d == 0)         state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(d & 0x80)       state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(state.x >= val) state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;  // TODO: +1 if boundary crossed
  trace("CPX zeropage #%02X");
}


static void opE5(void) {  // SBC zpg
  uint16_t t = state.a;
  uint16_t o = mem_read(addr_zpg());
  t += o ^ 0xFF;
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  // TODO - OVERFLOW FLAGS
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("SBC zeropage %02X");
}

static void opE8(void) {  // INX
  state.x     += 1;
  if((state.x) == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if((state.x) & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 2; 
  trace("INX");
}

static void opE9(void) {  // SBC # 
  uint16_t t = state.a;
  t += immediate() ^ 0xFF;
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  // TODO - OVERFLOW FLAGS
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("SBC #%02X");
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

static void opF1(void) {  // SBC zpg, Y
  uint16_t t = state.a;
  t += mem_read(addr_zpg_ind_y()) ^ 0xFF;
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  // TODO - OVERFLOW FLAGS
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("SBC zeropage %02X, Y");
}

static void opF5(void) {  // SBC zpg, X
  uint16_t t = state.a;
  t += mem_read(addr_zpg_x()) ^ 0xFF;
  t += (state.flags & FLAG_C ? 1 : 0);
  state.a = t;
  if(state.a == 0)  state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  // TODO - OVERFLOW FLAGS
  if(state.a &0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  if(t &0x100)      state.flags |= FLAG_C;  else state.flags &= ~FLAG_C;
  state.cycle += 4;   // TODO Decimal mode
  trace("SBC zeropage %02X, X");
}

static void opF6(void) {  // INC zeropage, X
  uint8_t z = addr_zpg_x();
  uint8_t t = mem_read(z);
  t++;
  mem_write(z,t);
  if(t == 0)   state.flags |= FLAG_Z;  else state.flags &= ~FLAG_Z;
  if(t & 0x80) state.flags |= FLAG_N;  else state.flags &= ~FLAG_N;
  state.cycle += 6; 
  trace("INC zeropage %02X");
}

static void opF8(void) {  // SED
  state.flags |= FLAG_D;
  state.cycle += 2; 
  printf("DECIMAL NODE NOT IMPLEMENTED YET!\n");
  trace("SED");
}

/********************************************************************************/
/*************** END OF ALL THE OPCODE IMPLEMENTATOINS **************************/
/********************************************************************************/

static void (*dispatch[256])(void) = {
//           00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
/* 00 */    op00, op01, NULL, NULL, NULL, op05, op06, NULL, op08, op09, op0A, NULL, NULL, op0D,    0, NULL,
/* 10 */    op10, op11, NULL, NULL, NULL, op15, op16, NULL, op18,    0, NULL, NULL, NULL,    0,    0, NULL,
/* 20 */    op20, op21, NULL, NULL, op24, op25, op26, NULL, op28, op29, op2A, NULL, op2C,    0,    0, NULL,
/* 30 */    op30, op31, NULL, NULL, NULL, op35, op36, NULL, op38,    0, NULL, NULL, NULL,    0,    0, NULL,
/* 40 */    op40, op41, NULL, NULL, NULL, op45, op46, NULL, op48, op49, op4A, NULL, op4C,    0,    0, NULL,
/* 50 */    op50, op51, NULL, NULL, NULL, op55, op56, NULL, op58,    0, NULL, NULL, NULL,    0,    0, NULL,
/* 60 */    op60, op61, NULL, NULL, NULL, op65, op66, NULL, op68, op69, op6A, NULL, op6C,    0,    0, NULL,
/* 70 */    op70, op71, NULL, NULL, NULL, op75, op76, NULL, op78, op79, NULL, NULL, NULL,    0,    0, NULL,
/* 80 */    NULL, op81, NULL, NULL, op84, op85, op86, NULL, op88, NULL, op8A, NULL, op8C, op8D, op8E, NULL,
/* 90 */    op90, op91, NULL, NULL, op94, op95, op96, NULL, op98, op99, op9A, NULL, NULL, op9D, NULL, NULL,
/* A0 */    opA0, opA1, opA2, NULL, opA4, opA5, opA6, NULL, opA8, opA9, opAA, NULL, opAC, opAD, opAE, NULL,
/* B0 */    opB0, opB1, NULL, NULL, opB4, opB5, opB6, NULL, opB8, opB9,    0, NULL,    0, opBD,    0, NULL,
/* C0 */    opC0, opC1, NULL, NULL, opC4, opC5, opC6, NULL, opC8, opC9, opCA, NULL,    0,    0,    0, NULL,
/* D0 */    opD0, opD1, NULL, NULL, NULL, opD5, opD6, NULL, opD8,    0, NULL, NULL, NULL, opDD,    0, NULL,
/* E0 */    opE0, opE1, NULL, NULL, opE4, opE5, opE6, NULL, opE8, opE9, opEA, NULL,    0,    0,    0, NULL,
/* F0 */    opF0, opF1, NULL, NULL, NULL, opF5, opF6, NULL, opF8,    0, NULL, NULL, NULL,    0,    0, NULL};

static uint8_t dispatched[256];
static uint8_t dispatched1[256];

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
   return vic[addr];
}
static void vic_write(uint16_t addr, uint8_t data) {
   printf("VIC write %04X %02X\n",addr,data);
   vic[addr] = data;
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

  if(addr < sizeof(ram)) 
    rtn = ram[addr];
  else if(addr >= 0x9000 && addr< 0x9010)
    rtn = vic_read(addr-0x9000);
  else if(addr >= 0x9110 && addr< 0x9120)
    rtn = via1_read(addr-0x9110);
  else if(addr >= 0x9120 && addr< 0x9130)
    rtn = via2_read(addr-0x9110);
  else if(addr >= 0x9400 && addr< 0x9800)
    rtn = colour[addr-0x9400];
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

  if(addr < sizeof(ram))  {
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

  if(addr >= 0x9400 && addr< 0x9800) {
    colour[addr-0x9400] = data;
    return;
  }

//  if(addr != 0x4000) {
    logger_16_8("Write to unmapped address", addr, data);
//    trace_level |= TRACE_OP;
//  }
}

static void trace(char *msg) {
  int i;
  uint8_t inst;
  if(!(trace_level & TRACE_OP))
     return;

  inst = mem_read_nolog(trace_addr);
  printf("%10i %04X: %02X ", state.cycle, trace_addr, inst);

  for(i = 1; i < trace_fetch_len; i++) {
    printf("%02X ", mem_read_nolog(trace_addr+i));
  }

  while(i < 4) {
    printf("   ");
    i++;
  }
#if 1
  printf("%02X %02X %02X ",state.a, state.x, state.y);
  if(state.flags & FLAG_N) 
    printf("N");
  else
    printf(" ");

  if(state.flags & FLAG_Z) 
    printf("Z");
  else
    printf(" ");

  if(state.flags & FLAG_C) 
    printf("C ");
  else
    printf("  ");
#endif

  printf(msg, trace_num);
  printf("\n");
}

void border_pixel(FILE *f) {
   putc(64,f);
   putc(64,f);
   putc(255,f);
}

uint8_t colours[16][3] = {
   {  0,  0,  0},   //BLACK            000
   {255,255,255},   //WHITE            001
   {255,  0,  0},   //RED              010
   {  0,255,255},   //CYAN             011
   {255,  0,255},   //PURPLE           100
   {  0,255,  0},   //GREEN            101
   {  0,  0,255},   //BLUE             110
   {255,255,  0},   //YELLOW           111
   {255,128,  0},   //    8 - 1000   Orange
   {255,192, 64},   //    9 - 1001   Light orange
   {255,128,128},   //    10 - 1010   Pink
   {128,255,255},   //    11 - 1011   Light cyan
   {255,128,255},   //    12 - 1100   Light purple
   {128,255,128},   //    13 - 1101   Light green
   {128,128,255},   //    14 - 1110   Light blue
   {255,255,128},   //    15 - 1111   Light yellow
};

uint16_t vram_lookup[32] = {
    0x8000, 0x8200, 0x8400, 0x8600, 0x8800, 0x8A00, 0x8C00, 0x8E00,
    0x9000, 0x9200, 0x9400, 0x9600, 0x9800, 0x9A00, 0x9C00, 0x9E00,
    0xE000, 0x0200, 0x0400, 0x0600, 0x0800, 0x0A00, 0x0C00, 0x0E00,
    0x1000, 0x1200, 0x1400, 0x1600, 0x1800, 0x1A00, 0x1C00, 0x1E00
};

void pixel(FILE *f, int colour) {
   putc(colours[colour][0],f);
   putc(colours[colour][1],f);
   putc(colours[colour][2],f);
}

void show_display(void) {
   int i, j;
   FILE *f = fopen("display.ppm", "wb");
   if(f == NULL)
     return;
   int width  = 12+22*8+12;
   int height = 38+23*8+38;
   fprintf(f,"P6\n%i %i\n255\n", width, height);

   uint16_t colour_ram_addr;
   uint16_t video_ram_addr;
   int bg_colour = mem_read_nolog(0x900F)>>4;
   int bd_colour = mem_read_nolog(0x900F)&0x7;
   int hoz_pos   = mem_read_nolog(0x9000)&0x7F; 
   int vert_pos  = mem_read_nolog(0x9001); 

   if(hoz_pos >= 24) hoz_pos = 24;

   video_ram_addr  = (mem_read_nolog(0x9005)&0xF0)>>3; // 4 bits
   video_ram_addr += (mem_read_nolog(0x9002)&0x80)>>7; // 1 bit
   video_ram_addr = vram_lookup[video_ram_addr];   
   video_ram_addr = 0x1000;  // TODO: Something odd with this = override computed value

   if(mem_read_nolog(0x9002) & 0x80) 
      colour_ram_addr = 0x9600;
   else 
      colour_ram_addr = 0x9400;
   
   for(i = 0; i < height; i++) {
      if(i < vert_pos || i >= vert_pos+23*8) {
         // Top or bottom frame
         for(j = 0; j < width; j++) {
            pixel(f, bd_colour);
         }
      } else {
         // Left frame
         for(j = 0; j < hoz_pos; j++) {
            pixel(f, bd_colour);
         }

         // Center
         for(j = 0; j < 22*8 && j+hoz_pos < width; j++) {
            int offset = ((i-vert_pos)>>3)*22+(j>>3);
            int line = (i-vert_pos)&7;
            int col  = j&7; 
            char glyph = mem_read_nolog(video_ram_addr+offset);
            int fg_colour = mem_read_nolog(colour_ram_addr+offset) & 0x7;
            uint8_t byte = rom3[glyph*8+line];
            uint8_t mask = 0x80 >> col;

            pixel(f, (byte&mask) ? fg_colour : bg_colour);
         }

         // Right frame
         for(j = 0; j < width-22*8-hoz_pos; j++) {
            pixel(f, bd_colour);
         }
      }
   } 
   fclose(f);
}

static void print_dispatched(void) {
  printf("  0123456789ABCDEF\n");
  for(int i = 0; i <256; i++) {
    if((i&0xf)==0) {
       printf("%1x ",i>>4);
    }
    putchar(dispatched[i] ^ dispatched1[i] ? '1' : '0');
    if((i&0xf) == 0xF)
      putchar('\n');
  }
  putchar('\n');
}

static int cpu_run(void) {
   uint8_t inst;
   if(state.pc == 0xE41C) {
      memcpy(dispatched1, dispatched, 256);
      trace_level = TRACE_OP;
   } else if(state.pc == 0xDEA5) {
      print_dispatched();
      trace_level = TRACE_OFF;
      sleep(1);
   }
   trace_addr   = state.pc; 
   trace_fetch_len    = 0;
   inst         = mem_fetch(state.pc);
   trace_opcode = inst;
   if(dispatch[inst]==0) {
      logger_16_8("Unknown opcode at address",trace_addr, inst);
      cpu_dump();
      show_display(); 
      return 0;
   }
   dispatched[inst] = 1;
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
      fprintf(stderr, "Unable to open 'rom1.img'\n");
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
      fprintf(stderr, "Unable to open 'rom2.img'\n");
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

static int rom3_load() {
   FILE *f = fopen("rom3.img","rb");
   if(f == NULL) {
      fprintf(stderr, "Unable to open 'rom3.img'\n");
      return 0;
   }
   if(fread(rom3,4096,1,f) != 1) {
      fclose(f);
      return 0;
   }
   fclose(f);
   printf("ROM3 loaded\n");
   return 1;
}

static void sighandler_usr1(int v) {
   int i;
   cpu_dump();

   printf("   ");
   for(i = 0; i < 16; i++) {
     printf(" %02X",i);
   }

   for(i = 0; i < 256; i++) {
     if((i&0xF)==0) {
       printf("\n%02X:",i);
     }
     printf(" %02X",ram[i]);
   }
   printf("\n");
}

int main(int argc, char *argv[]) {
   if(argc != 1 && argc != 3) {
      printf("Unknown opton\n");
      exit(1);
   }
   if(argc == 3)  {
      if(strcmp(argv[1],"-v")!=0) {
         printf("Unknown opton\n");
         exit(1);
      }
      trace_level = atoi(argv[2]);
   }
   signal(SIGUSR1, sighandler_usr1);

   if(rom1_load() && rom2_load() && rom3_load()) {
      cpu_reset();
      while(cpu_run()) {
         if(state.cycle - last_display > 3000000) {
            show_display();
            last_display = state.cycle;
         }
      }
      if(0)
        logger_8("remove warning for unused logger_8()",0);
   }
   return 0;
}
