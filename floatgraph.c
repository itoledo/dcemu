#include "main.h"
#include <math.h>

OPCODE(fmov221) // FMOV DRm, XDn (1111nnn1 mmm01100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&XD(n), &DR(m), sizeof(float)*2);

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov221: FMOV DR%d, XD%d\r\n", m, n);
#endif
}

OPCODE(fmov222) // FMOV XDm, DRn (1111nnn0 mmm11100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&DR(n), &XD(m), sizeof(float)*2);

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov222: FMOV XD%d, DR%d\r\n", m, n);
#endif
}

OPCODE(fmov223) // FMOV XDm, XDn (1111nnn1 mmm11100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&XD(n), &XD(m), sizeof(float)*2);

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

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov225: r[%d]=%x - 8 n=%d\r\n", m, R(m), n);
#endif
}

OPCODE(fmov226) // FMOV @(R0, Rm), XDn		(1111nnn1 mmmm0110)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;
	DWORD addr = R(0) + R(m);

	memread(addr, &XD(n), sizeof(DWORD)*2);

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov226: FMOV (R0 + R%d = %x), XD%d\r\n", m, addr, n);
#endif
}

OPCODE(fmov227) // FMOV XDm, @Rn (1111nnnn mmm11010)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;

	memwrite(R(n), &XD(m), sizeof(DWORD) * 2);

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov227: FMOV XD%d, @R[%d]=%x\r\n", m, n, R(n));
#endif
}
	
OPCODE(fmov228) // FMOV XDm, @-Rn (1111nnnn mmm11011)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;

    R(n) -= 8;
    
    memwrite(R(n), &XD(m), sizeof(DWORD)*2);

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

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov229: XD%d, (R0 + R%d = %x)\r\n", m, n, addr);
#endif
}

OPCODE(fipr) // FIPR FVm, FVn (1111nnmm 11101101)
{
	short n = (arg >> 10) & 0x3;
	short m = (arg >> 8) & 0x3;
	
	FR(n+3) =	FR(m+0) * FR(n+0) +
				FR(m+1) * FR(n+1) +
				FR(m+2) * FR(n+2) +
				FR(m+3) * FR(n+3);

}

OPCODE(ftrv) // FTRV XMTRX, FVn (1111nn01 11111101)
{
	short n = (arg >> 10) & 0x3;
	float v1, v2, v3, v4;

/* matriz:
	XF0		XF4		XF8		XF12
	XF1		XF5		XF9		XF13
	XF2		XF6		XF10	XF14
	XF3		XF7		XF11	XF15 */

	v1	=	float_registers[XF_index(0)] * FR(4*n+0) +
				float_registers[XF_index(4)] * FR(4*n+1) +
				float_registers[XF_index(8)] * FR(4*n+2) +
				float_registers[XF_index(12)] * FR(4*n+3);
	v2 =	float_registers[XF_index(1)] * FR(4*n+0) +
				float_registers[XF_index(5)] * FR(4*n+1) +
				float_registers[XF_index(9)] * FR(4*n+2) +
				float_registers[XF_index(13)] * FR(4*n+3);
	v3 =	float_registers[XF_index(2)] * FR(4*n+0) +
				float_registers[XF_index(6)] * FR(4*n+1) +
				float_registers[XF_index(10)] * FR(4*n+2) +
				float_registers[XF_index(14)] * FR(4*n+3);
	v4 =	float_registers[XF_index(3)] * FR(4*n+0) +
				float_registers[XF_index(7)] * FR(4*n+1) +
				float_registers[XF_index(11)] * FR(4*n+2) +
				float_registers[XF_index(15)] * FR(4*n+3);

	FR(4*n+0) = v1;
	FR(4*n+1) = v2;
	FR(4*n+2) = v3;
	FR(4*n+3) = v4;
}

OPCODE(fsrra) // FSRRA FRn (1111nnnn 01111101)
{
   	short n = (arg >> 8) & 0x0F;
	float f = (float) 1.0 / (float) sqrt(FR(n));

	FR(n) = f;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fsrra: FR(%d)=%x,%f\r\n", n, float_to_dword(FR(n)), (float) FR(n));
#endif
}

OPCODE(frchg232) // FRCHG (1111101111111101)
{
/*    if (IS_SET(FPSCR, FPSCR_FR))
        REMOVE_BIT(FPSCR, FPSCR_FR);
    else
        SET_BIT(FPSCR, FPSCR_FR); */
    UpdateFPSCR(FPSCR ^ FPSCR_FR);

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("frchg232: fr=%d\r\n", IS_SET(FPSCR, FPSCR_FR) ? 1 : 0);
#endif
}

OPCODE(fschg233) // FSCHG (11110011 11111101)
{
	if (IS_SET(FPSCR, FPSCR_SZ))
		REMOVE_BIT(FPSCR, FPSCR_SZ);
	else
		SET_BIT(FPSCR, FPSCR_SZ);

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fschg233: sz=%d\r\n", IS_SET(FPSCR, FPSCR_SZ) ? 1 : 0);
#endif
}
