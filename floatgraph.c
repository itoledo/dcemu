#include "main.h"
#include "floatsimple.h"	/* fpu_dn_s(): FPSCR.DN */
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

/*
	FIPR y FTRV generan valores, asi que les toca lo mismo que a la aritmetica:
	con FPSCR.DN los desnormalizados entran y salen aplastados a cero (manual
	del SH-4, 6.2.3). Se copian los operandos a locales para no tocar los
	registros de origen -- FIPR solo escribe FR[4n+3], y los otros siete tienen
	que quedar como estaban.

	La cuenta va en doble y se redondea una sola vez, aqui y en
	simdx86_stub.c; ver ahi por que.
*/
OPCODE(fipr) // FIPR FVm, FVn (1111nnmm 11101101)
{
	short n = (arg >> 10) & 0x3;
	short m = (arg >> 8) & 0x3;
	double suma = 0;
	int i;

	/* n y m numeran vectores de cuatro registros: FVn son FR[4n]..FR[4n+3], y
	   el producto punto queda en el ultimo, FR[4n+3]. */
	for (i = 0; i < 4; i++)
		suma += (double) fpu_dn_s(FR(4*m+i)) * (double) fpu_dn_s(FR(4*n+i));

	FR(4*n+3) = fpu_dn_s((float) suma);

	PC += 2;

	core.context.cycles += 4;
}

OPCODE(ftrv) // FTRV XMTRX, FVn (1111nn01 11111101)
{
	short n = (arg >> 10) & 0x3;
	float v[4];
	double r[4];
	int i, j;

/* matriz:
	XF0		XF4		XF8		XF12
	XF1		XF5		XF9		XF13
	XF2		XF6		XF10	XF14
	XF3		XF7		XF11	XF15 */

	for (i = 0; i < 4; i++)
		v[i] = fpu_dn_s(FR(4*n+i));

	for (i = 0; i < 4; i++)
	{
		r[i] = 0;

		for (j = 0; j < 4; j++)
			r[i] += (double) fpu_dn_s(XF(i + 4*j)) * (double) v[j];
	}

	for (i = 0; i < 4; i++)
		FR(4*n+i) = fpu_dn_s((float) r[i]);

	PC += 2;

	core.context.cycles += 4;
}

OPCODE(fsrra) // FSRRA FRn (1111nnnn 01111101)
{
   	short n = (arg >> 8) & 0x0F;
	float a = fpu_dn_s(FR(n));

	#ifndef X86_OPT
	FR(n) = fpu_dn_s((float) (1.0 / sqrt(a)));
	#else
	FR(n) = fpu_dn_s(SIMDx86_rsqrtf(a));
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

