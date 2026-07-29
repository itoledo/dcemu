#include "main.h"
#include <math.h>
#include <SIMDx86/math.h>


OPCODE(fmov221) // FMOV DRm, XDn (1111nnn1 mmm01100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&XD(n), &DR(m), sizeof(float)*2);

	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov221: FMOV DR%d, XD%d\r\n", m, n);
#endif
}

OPCODE(fmov222) // FMOV XDm, DRn (1111nnn0 mmm11100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&DR(n), &XD(m), sizeof(float)*2);

	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov222: FMOV XD%d, DR%d\r\n", m, n);
#endif
}

OPCODE(fmov223) // FMOV XDm, XDn (1111nnn1 mmm11100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&XD(n), &XD(m), sizeof(float)*2);

	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov223: FMOV XD%d, XD%d\r\n", m, n);
#endif
}

OPCODE(fmov224) // FMOV @Rm, XDn (1111nnn1 mmmm1000)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;

/*	memread(R(m), &float_registers[XD_index(n)], sizeof(float));
	memread(registers[m+4], &float_registers[XD_index(n)+1], sizeof(float)); */
	
	memread(R(m), &XD(n), sizeof(float)*2);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov224: FMOV @R%d = %x, XD%d", m, R(m), n);
#endif
}

OPCODE(fmov225) // FMOV @Rm+, XDn (1111nnn1 mmmm1001)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;

	memread(R(m), &XD(n), sizeof(DWORD) * 2);

    	R(m) += 8;

	PC += 2;

	core.context.cycles += 1;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov225: r[%d]=%x - 8 n=%d\r\n", m, R(m), n);
#endif
}

OPCODE(fmov226) // FMOV @(R0, Rm), XDn		(1111nnn1 mmmm0110)
{
	/* n son los bits 9-11 (el bit 8 marca que el destino es XD) y m ocupa los
	   cuatro bits 4-7, como en las demas variantes con @Rm. */
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;
	DWORD addr = R(0) + R(m);

	memread(addr, &XD(n), sizeof(DWORD)*2);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov226: FMOV (R0 + R%d = %x), XD%d\r\n", m, addr, n);
#endif
}

OPCODE(fmov227) // FMOV XDm, @Rn (1111nnnn mmm11010)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;

	memwrite(R(n), &XD(m), sizeof(DWORD) * 2);

	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov227: FMOV XD%d, @R[%d]=%x\r\n", m, n, R(n));
#endif

	core.context.cycles += 1;
}
	
OPCODE(fmov228) // FMOV XDm, @-Rn (1111nnnn mmm11011)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;

    R(n) -= 8;
    
    memwrite(R(n), &XD(m), sizeof(DWORD)*2);

	PC += 2;

	core.context.cycles += 1;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov228: FMOV XD%d, @-R%d = %x", m, n, R(n));
#endif
}

OPCODE(fmov229) // FMOV XDm, @(R0, Rn) (1111nnnn mmm10111)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;
	DWORD addr = R(0) + R(n);

	memwrite(addr, &XD(m), sizeof(DWORD)*2);

	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov229: XD%d, (R0 + R%d = %x)\r\n", m, n, addr);
#endif
	core.context.cycles += 1;
}

OPCODE(fipr) // FIPR FVm, FVn (1111nnmm 11101101)
{
	short n = (arg >> 10) & 0x3;
	short m = (arg >> 8) & 0x3;

	/* n y m numeran vectores de cuatro registros: FVn son FR[4n]..FR[4n+3], y
	   el producto punto queda en el ultimo, FR[4n+3]. */
 #ifndef X86_OPT
	FR(4*n+3) =	FR(4*m+0) * FR(4*n+0) +
				FR(4*m+1) * FR(4*n+1) +
				FR(4*m+2) * FR(4*n+2) +
				FR(4*m+3) * FR(4*n+3);
#else
 FR(4*n+3) = SIMDx86Vector_Dot4(Vector(m),Vector(n));
#endif
	PC += 2;

	core.context.cycles += 4;
}

OPCODE(ftrv) // FTRV XMTRX, FVn (1111nn01 11111101)
{
	short n = (arg >> 10) & 0x3;
#ifndef X86_OPT
float v1, v2, v3, v4;
/* matriz:
	XF0		XF4		XF8		XF12
	XF1		XF5		XF9		XF13
	XF2		XF6		XF10	XF14
	XF3		XF7		XF11	XF15 */

	v1	=	XF(0) * FR(4*n+0) +
				XF(4) * FR(4*n+1) +
				XF(8) * FR(4*n+2) +
				XF(12) * FR(4*n+3);
	v2 =	XF(1) * FR(4*n+0) +
				XF(5) * FR(4*n+1) +
				XF(9) * FR(4*n+2) +
				XF(13) * FR(4*n+3);
	v3 =	XF(2) * FR(4*n+0) +
				XF(6) * FR(4*n+1) +
				XF(10) * FR(4*n+2) +
				XF(14) * FR(4*n+3);
	v4 =	XF(3) * FR(4*n+0) +
				XF(7) * FR(4*n+1) +
				XF(11) * FR(4*n+2) +
				XF(15) * FR(4*n+3);

	FR(4*n+0) = v1;
	FR(4*n+1) = v2;
	FR(4*n+2) = v3;
	FR(4*n+3) = v4;
#else
SIMDx86Matrix_Vector4Multiply(Vector(n),XFMTRX);
#endif

	PC += 2;

	core.context.cycles += 4;
}

OPCODE(fsrra) // FSRRA FRn (1111nnnn 01111101)
{
   	short n = (arg >> 8) & 0x0F;
	#ifndef X86_OPT
	FR(n) = (float) 1.0 / (float) sqrt(FR(n));
	#else
	FR(n) = SIMDx86_rsqrtf(FR(n));
	#endif
	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fsrra: FR(%d)=%x,%f\r\n", n, float_to_dword(FR(n)), (float) FR(n));
#endif

	core.context.cycles += 4;
}

OPCODE(frchg232) // FRCHG (1111101111111101)
{
    	FPSCR_FR = ~FPSCR_FR;
	SWITCH_FLOAT_REG_BANKS();
	PC += 2;

	core.context.cycles += 1;
#ifdef DEBUG_FLOAT_GRAPH
	logmsg("frchg232: fr=%d\r\n", FPSCR_FR);
#endif
}

OPCODE(fschg233) // FSCHG (11110011 11111101)
{
#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fschg233:Antes sz=%d\r\n", FPSCR_SZ_BIT);
#endif
	FPSCR_t fpscr = core.context.FPSCR_REG;
	
	fpscr.FPSCR_REG_BITS.SZ_BF = ~fpscr.FPSCR_REG_BITS.SZ_BF;
//	FPSCR_SZ_BIT = ~FPSCR_SZ_BIT ;
	UpdateFPSCR(fpscr.FPSCR_ALL);
	PC += 2;

	core.context.cycles += 1;
#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fschg233:Depois sz=%d\r\n", FPSCR_SZ_BIT);
#endif
}

