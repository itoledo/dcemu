#ifndef _SH4_H_
#define _SH4_H_

#include <windows.h>
#include "log.h"
#include "mem.h"

typedef short bool;
#define true 1
#define false 0

// __fastcall void ejecutar_instruccion(DWORD instr);
void ejecutar_instruccion(DWORD instr);
void UpdateSR(DWORD newSR);
void UpdateFPSCR(DWORD newFPSCR);

// CPU Registers
extern float float_registers[32];
extern DWORD registers[];
extern unsigned long SR; 	// Status Register
extern unsigned long SSR; 	// Saved Status Register
extern unsigned long SPC; 	// Saved Program Counter
extern unsigned long GBR; 	// Global Base Register
extern unsigned long VBR; 	// Vector Base Register
extern unsigned long SGR; 	// Saved General Register 15
extern unsigned long DBR; 	// Debug Base Register
							
extern DWORD *	PTEL;		// Page Table Entry Low register
							
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

extern unsigned long MACH; // Multiply-And-Accumulate register High
extern unsigned long MACL; // Multiply-And-Accumulate register Low
extern DWORD FPUL;
// extern float FPUL;
extern unsigned long PR;   // Procedure Register
extern unsigned long PC;   // Program Counter
extern unsigned long FPSCR; // Floating Point Status/Control Register
extern unsigned long delayslot;
extern unsigned long NEXTPC;

/*
#define R(n)		(registers[((n) > 7) ? ((n) + 8) : ((IS_SET(SR, SR_MD) && IS_SET(SR, SR_RB)) ? ((n) + 8) : (n))])
#define R_BANK(n)	(registers[((n) > 7) ? ((n) + 8) : ((IS_SET(SR, SR_MD) && IS_SET(SR, SR_RB)) ? ((n)) : (n + 8))])
#define FR(n)		(float_registers[(FPSCR & FPSCR_FR) ? ((n) + 16) : (n)])
#define FR_index(n) ((FPSCR & FPSCR_FR) ? ((n) + 16) : (n))
#define DR(n) (FR((n)*2))
#define DR_index(z) (FR_index((z)*2))
#define XF(n) (float_registers[(FPSCR & FPSCR_FR) ? (n) : ((n) + 16)])
#define XF_index(n) ((FPSCR & FPSCR_FR) ? (n) : ((n) + 16))
#define XD(n) (XF((n)*2))
#define XD_index(n) (XF_index((n)*2))
*/

#define	R(n)		(registers[n])
#define R_BANK(n)	(registers[n+16])
#define FR(n)		(float_registers[n])
#define FR_index(n)	(n)
#define DR(n)		(FR((n)*2))
#define DR_index(z) (FR_index((z)*2))
#define XF(n)		(float_registers[n+16])
#define XF_index(n)	((n)+16)
#define XD(n)		(XF((n)*2))
#define XD_index(n)	(XF_index((n)*2))

// FPSCR
#define FPSCR_PR        (1<<19)
#define FPSCR_SZ        (1<<20)
#define FPSCR_FR        (1<<21)

#define REG_SET_BIT(reg, bit) {*reg |= bit;}
#define SET_BIT(reg, bit) (reg |= (bit))
#define REMOVE_BIT(reg, bit) (reg &= ~(bit))
#define IS_SET(val, bit) ((val) & (bit))
#define IS_SET_REG(reg, bit) ((*reg) & (bit))

#define SET_T {SR = SR | SR_T;}
#define UNSET_T {SR = SR & ~SR_T;}

#define IS_SR_MD()	(SR & SR_MD)
#define IS_SR_T() (SR & SR_T)
#define IS_SR_Q() (SR & SR_Q)
#define IS_SR_M() (SR & SR_M)

#define SR_MD (1 << 30)
#define SR_RB (1 << 29)
#define SR_BL (1 << 28)
#define SR_FD (1 << 15)
#define SR_M  (1 << 9)
#define SR_Q  (1 << 8)
#define SR_IMASK (0x000000F0)
#define SR_S  (1 << 1)
#define SR_T  (1 << 0)

#define flunpack(x) (*(float *)x)
#define lunpack(x) (*(DWORD *)x)
#define wunpack(x) (*(WORD *)x)
#define SignExtend8(c) (((c) & 0x80) ? (0xFFFFFF00 | (c)) : (c))
#define SignExtend16(c) (((c) & 0x8000) ? (0xFFFF0000 | (c)) : (c))
#define SignExtend12(c) (((c) & 0x0800) ? (0xFFFFF000 | (c)) : (c))

// #define OPCODE(instr) __fastcall void instr (WORD arg)
#define OPCODE(instr) void instr (WORD arg)
#define COPY_REG(a, b) ((DWORD) (a) = (DWORD) (b))

typedef void PC_f(void);
extern PC_f * PC_func;
void PC_f_normal(void);
void PC_f_delayslot(void);
void PC_f_nextpc(void);

#endif // _SH4_H_
