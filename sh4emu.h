#ifndef _sh4emu_
#define _sh4emu_

#include <SIMDx86/matrix.h> 
#include <SIMDx86/vector.h>
#include <stdlib.h>
#include "lnxdefs.h"

#include "options.h"
#include "opcodes.h"
#ifdef WIN32
#include <windows.h>
#elif (__GNUC__)
#include <string.h>
#endif
#include "log.h"
#include "mem.h"

// data format on the sh4
							
/* Registros de la MMU. Ver docs/mmu-plan.md, fase 1.1. */
extern DWORD *	PTEH;		// Page Table Entry High register
extern DWORD *	PTEL;		// Page Table Entry Low register
extern DWORD *	TTB;		// Translation Table Base register
extern DWORD *	TEA;		// TLB Exception Address register
extern DWORD *	MMUCR;		// MMU Control Register
extern DWORD *	PTEA;		// Page Table Entry Assistance register

extern DWORD *	CCR;		// Cache Control Register
extern WORD *	SCFSR2;		
extern DWORD *	QACR0;		// Queue Address Control Register 0
extern DWORD *	QACR1;		// Queue Address Control Register 0
extern DWORD    SQ0[8];     // Store Queue 0
extern DWORD    SQ1[8];     // Store Queue 1
extern DWORD *	INTEVT;		// Interrupt Event Register
extern DWORD *	EXPEVT;		// Exception Event Register
extern DWORD *	TRA;		// TRAPA exception register
							
extern DWORD *	CHCR2;		// DMA Channel Control Register 2

extern WORD *	ICR;		// Interrupt Control Register
extern WORD *	IPRA;		// Interrupt Priority Register A
extern WORD *	IPRB;		// Interrupt Priority Register B
extern WORD *	IPRC;		// Interrupt Priority Register C

extern DWORD *	PCTRA;		// Port Control Register A
extern WORD *	PDTRA;		// Port Data Register A
extern DWORD *	PCTRB;		// Port Control Register B
extern WORD *	PDTRB;		// Port data Register B

/*** Serial Communications Interface ***/
extern WORD *	SCSMR2;			// Serial Mode Register
extern BYTE *	SCBRR2;			// Bit Rate Register
extern WORD *	SCSCR2;			// Serial Control Register
extern BYTE *	SCFTDR2;		// Transmit FIFO Data Register
extern WORD *	SCFSR2;			// Serial Status Register
extern BYTE *	SCFRDR2;		// Receive FIFO Data Register
extern WORD *	SCFCR2;			// FIFO Control Register
extern WORD *	SCFDR2;			// FIFO Data Count Register
extern WORD *	SCSPTR2;		// Serial Port Register
extern WORD *	SCLSR2;			// Line Status Register

// BITFIELDS
// SCSMR2
#define CHR		(0x0040)
#define PE		(0x0020)
#define PM		(0x0010)
#define STOP	(0x0008)
#define CKS1	(0x0002)
#define CKS0	(0x0000)

// SCSCR2
#define TIE		(0x0080)
#define RIE		(0x0040)
#define TE		(0x0020)
#define RE		(0x0010)
#define REIE	(0x0008)
#define CKE1	(0x0002)

// SCFSR2
#define SCFSR2_PER3	(0x8000)
#define SCFSR2_PER2	(0x4000)
#define SCFSR2_PER1	(0x2000)
#define SCFSR2_PER0	(0x1000)
#define SCFSR2_FER3	(0x0800)
#define SCFSR2_FER2	(0x0400)
#define SCFSR2_FER1	(0x0200)
#define SCFSR2_FER0	(0x0100)
#define SCFSR2_ER	(0x0080)
#define SCFSR2_TEND	(0x0040)
#define SCFSR2_TDFE	(0x0020)
#define SCFSR2_BRK	(0x0010)
#define SCFSR2_FER	(0x0008)
#define SCFSR2_PER	(0x0004)
#define SCFSR2_RDF	(0x0002)
#define SCFSR2_DR	(0x0001)
/*** FIN Serial Communications Interface ***/

/*** DMA ***/
extern	DWORD *	SAR0;		// DMA source address register 0
extern	DWORD *	DAR0;		// DMA destination address register 0
extern	DWORD *	DMATCR0;	// DMA transfer count register 0
extern	DWORD *	CHCR0;		// DMA channel control register 0
extern	DWORD *	SAR1;		// DMA source address register 0
extern	DWORD *	DAR1;		// DMA destination address register 0
extern	DWORD *	DMATCR1;	// DMA transfer count register 0
extern	DWORD *	CHCR1;		// DMA channel control register 0
extern	DWORD *	SAR2;		// DMA source address register 0
extern	DWORD *	DAR2;		// DMA destination address register 0
extern	DWORD *	DMATCR2;	// DMA transfer count register 0
extern	DWORD *	CHCR2;		// DMA channel control register 0
extern	DWORD *	SAR3;		// DMA source address register 0
extern	DWORD *	DAR3;		// DMA destination address register 0
extern	DWORD *	DMATCR3;	// DMA transfer count register 0
extern	DWORD *	CHCR3;		// DMA channel control register 0
extern	DWORD *	DMAOR;		// DMA operation register

#define DME		1
#define DE		1
/*** FIN DMA ***/

/*** TMU ***/
extern  BYTE *	TOCR;
extern  BYTE *	TSTR;
extern  DWORD *	TCOR0;
extern  DWORD *	TCNT0;
extern  WORD *	TCR0;
extern  DWORD *	TCOR1;
extern  DWORD *	TCNT1;
extern  WORD *	TCR1;
extern  DWORD *	TCOR2;
extern  DWORD *	TCNT2;
extern  WORD *	TCR2;
extern  DWORD *	TCPR2;

#define TMU_TCR_UNF		(1 << 8)	// 0x0100
#define TMU_TCR_UNIE	(1 << 5)
/*** FIN TMU ***/



#define SWITCH_FLOAT_REG_BANKS(){\
FPR_BANK * backup;\
backup = core.context.XF_BANK; \
core.context.XF_BANK = core.context.FR_BANK; \
core.context.FR_BANK = backup;\
}

void UpdateSR(DWORD newSR);
void UpdateFPSCR(DWORD newFPSCR);

extern unsigned long delayslot;
extern unsigned long NEXTPC;

typedef union FPR_BANK FPR_BANK;

 union u64
{
	unsigned int part [2];
	double db;
};

union FPR_BANK
{
  union
  {
    SIMDx86Matrix XMTRX;
    float vector [4] [4];
    union u64 dreg [8];
  } FP;
  union 
  {
    SIMDx86Matrix XMTRX;
    FLOAT pair [8][2];
  }XFP;
};

// some needed declarations

typedef union SR SR_t;

	struct SR_BIT_t {
		unsigned T:1;
		unsigned S:1;
		unsigned reserved0:2;
		unsigned IMASK:4;
		unsigned Q:1;
		unsigned M:1;
		unsigned reserved1:5;
		unsigned FD:1;
		unsigned reserved2:12;
		unsigned BL:1;
		unsigned RB:1;
		unsigned MD:1;
		unsigned reserved3:1;
	};

union SR
{
	struct SR_BIT_t SR_BITS;
	unsigned long SR_ALL;
};

typedef union FPSCR FPSCR_t;

struct FPSCR_REG_BITS_t
{
	unsigned RM_BF:2;
	unsigned FLAG_BF:5;
	unsigned ENABLE_BF:5;
	unsigned Cause_BF:6;
	unsigned DN_BF:1;
	unsigned PR_BF:1;
	unsigned SZ_BF:1;
	unsigned FR_BF:1;
	unsigned reserved0:10;
};

union FPSCR
{
	struct FPSCR_REG_BITS_t FPSCR_REG_BITS;
	unsigned long FPSCR_ALL;
}; // floating-point status control register

// Estos dos se leen y escriben tambien como DWORD (SR_ALL / FPSCR_ALL).
DC_ASSERT_SIZE(sr_bits, struct SR_BIT_t, 4);
DC_ASSERT_SIZE(fpscr_bits, struct FPSCR_REG_BITS_t, 4);

typedef struct context_t context_t;

struct context_t
{
	DWORD cycles; // cycles count
	// Sin usar desde la fase 2 de docs/clock-plan.md: el barrido compara contra
	// una marca de reloj_total y ya no necesita acumuladores. Se dejan para no
	// cambiar el tamano del contexto, que main_loop() copia entero para
	// reejecutar instrucciones que fallan por MMU.
	DWORD cycles_v_int; // sin usar
	DWORD cycles_v_int_total; // sin usar, nunca se leyo
	DWORD registers [24]; // GENERAL PURPOSE REGISTERS
	FPSCR_t FPSCR_REG; 
	unsigned long SSR_REG;
	unsigned long SPC_REG;
	unsigned long GBR_REG;
	unsigned long VBR_REG;
	unsigned long SGR_REG;
	unsigned long DBR_REG;
  // system registers
	unsigned long MACH_REG; // multiply and accumulate high
	unsigned long MACL_REG; // multiply and acumulate low
	unsigned long PR_REG; // Procedure register
	unsigned long PC_REG; // Program Counter
	SR_t SR_REG;
	DWORD FPUL_REG;
  // floating point registers
	FPR_BANK * FR_BANK;
	FPR_BANK * XF_BANK;
};

// bool SH4_SLEEPING=false; // by default the processor is doing something




#define OPCODE(instr)  void   instr (WORD arg)
#define COPY_REG(a, b) ((a) =(b))


// sign extend macros

#define SignExtend8(c) (((c) & 0x80) ? (0xFFFFFF00 | (c)) : (c))
#define SignExtend16(c) (((c) & 0x8000) ? (0xFFFF0000 | (c)) : (c))
#define SignExtend12(c) (((c) & 0x0800) ? (0xFFFFF000 | (c)) : (c))


typedef struct
{
  context_t context;
  void (* execute )(WORD arg);
  void (* resetCpu)(void);
  void (*closeCpu) (void);
  void (*executeBlock)(void);
}sh4_cpu;

// some macros to acess some special registers
//FPSCR stuff
#define FPSCR_CAUSE core.context.FPSCR_REG.FPSCR_BITS.Cause_BF
#define FPSCR_PR_BIT core.context.FPSCR_REG.FPSCR_REG_BITS.PR_BF
#define FPSCR_SZ_BIT core.context.FPSCR_REG.FPSCR_REG_BITS.SZ_BF
#define FPSCR_FR core.context.FPSCR_REG.FPSCR_REG_BITS.FR_BF
#define FPSCR_DN core.context.FPSCR_REG.FPSCR_REG_BITS.DN_BF
#define FPSCR_FLAG core.context.FPSCR.FPSCR_REG_BITS.FLAG_BF
#define FPSCR_RM core.context.FPSCR_REG.FPSCR_REG_BITS.RM_BF
#define FPSCR_ENABLE core.context.FPSCR_REG.FPSCR_REG_BITS.ENABLE_BF
#define FPSCR core.context.FPSCR_REG.FPSCR_ALL

// register acess stuf

#define MACL core.context.MACL_REG
#define MACH core.context.MACH_REG
#define SSR core.context.SSR_REG
#define SPC core.context.SPC_REG
#define GBR core.context.GBR_REG
#define VBR core.context.VBR_REG
#define SGR core.context.SGR_REG
#define DBR core.context.DBR_REG

#define PR core.context.PR_REG
#define PC core.context.PC_REG

#define FPUL core.context.FPUL_REG
#define PC core.context.PC_REG


//SR
#define SR_T core.context.SR_REG.SR_BITS.T
#define SR_S core.context.SR_REG.SR_BITS.S
#define SR_BL core.context.SR_REG.SR_BITS.BL
#define SR_RB core.context.SR_REG.SR_BITS.RB
#define SR_MD core.context.SR_REG.SR_BITS.MD
#define SR_FD core.context.SR_REG.SR_BITS.FD
#define SR_M core.context.SR_REG.SR_BITS.M
#define SR_Q core.context.SR_REG.SR_BITS.Q
#define SR_IMASK core.context.SR_REG.SR_BITS.IMASK
#define SR core.context.SR_REG.SR_ALL

//floating register stuff
#define FR(x) core.context.FR_BANK->FP.XMTRX.m[x]
#define DR(x) core.context.FR_BANK->FP.dreg[x]
#define DR_INT(x) core.context.FR_BANK->FP.dreg[x].part
#define DR_index(x) (x)
#define Vector(x)  core.context.FR_BANK->FP.vector[x]
#define XD(x) core.context.XF_BANK->XFP.pair[x]
#define MTRX &core.context.FR_BANK->FP.XMTRX.m
#define XFMTRX &core.context.XF_BANK->XFP.XMTRX

#define XF(x) core.context.XF_BANK->XFP.XMTRX.m[x]

#define IS_SR_MD()(SR_MD == 1)
#define IS_SR_T() (SR_T == 1)
#define IS_SR_Q() (SR_Q == 1)
#define IS_SR_M() (SR_M == 1)
#define IS_SR_S() (SR_S == 1)

#define SET_T {SR_T = 1;}
#define UNSET_T {SR_T =0;}

// something from the old core just here till we don't rewrite the rest :)
#define REG_SET_BIT(reg, bit) {*reg |= bit;}
#define SET_BIT(reg, bit) (reg |= (bit))
#define REMOVE_BIT(reg, bit) (reg &= ~(bit))
#define IS_SET(val, bit) ((val) & (bit))
#define IS_SET_REG(reg, bit) ((*reg) & (bit))
#define IS_SH4_REG_SET(bit) (bit == 1)
#define SET_SH4_BIT(bit)(bit=1)
#define REMOVE_SH4_BIT(bit)(bit=0)

// this means SR_BL SR_MD SR_RB were set
#define SH4_SYSTEM_REGISTER_INTC_REWRITTEN -1

// macros to check FPSCR attributes on a regular int


// GPR acess stuff
#define R(n) core.context.registers[n]
// the sh4 context

#define R_BANK(arg)(R(arg+16))
extern sh4_cpu core;

void initCpuSubSystem();

typedef void PC_f(void);
extern PC_f * PC_func;
void PC_f_normal(void);
void PC_f_delayslot(void);
void PC_f_nextpc(void);

#endif
