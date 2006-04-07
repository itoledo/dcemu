/****************************************************************************

	FLOATCONTROL - Floating Point Control Opcodes for SH-4 Processor

*****************************************************************************/

#include "sh4emu.h"



#ifdef _fast_interpreter_
#define fc_n jumptable.jit_array[jumptable.current_pos].cache.n
#define fc_m  jumptable.jit_array[jumptable.current_pos].cache.m
#else
short fc_n=0;
short fc_m=0;
#endif

OPCODE(lds213) // LDS Rm, FPSCR : Rm -> FPSCR (0100mmmm 01101010)
{
	fc_m = (arg >> 8) & 0x0F;

//	FPSCR = R(m);
	FPSCR = R(fc_m);
	UpdateFPSCR(FPSCR);
//	memcpy(&FPSCR, &R(m), sizeof(DWORD));

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("lds213: FPSCR=%x, r[%d]=%x\r\n", FPSCR, m, R(m));
#endif
}

OPCODE(lds214) // LDS Rm, FPUL : Rm -> FPUL (0100mmmm 01011010)
{
	fc_m = (arg >> 8) & 0x0F;

//	FPUL = R(m);
	memcpy(&FPUL, &R(fc_m), sizeof(DWORD));
//	FPUL = f;

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("lds214: r[%d]=%x,%d (no conversion) FPUL=%x,%f\r\n", m, R(m), R(m), (DWORD) FPUL, (float) FPUL);
#endif
}

OPCODE(ldsl215) // LDS.L @Rm+, FPSCR : (Rm) -> FPSCR, Rm + 4 -> Rm (0100mmmm 01100110)
{
	fc_m = (arg >> 8) & 0x0F;
	DWORD fpscr;

	ReadMemoryL(R(fc_m), &fpscr);
	UpdateFPSCR(fpscr);

	R(fc_m) += 4;

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("lds215: FPSCR=%x, r[%d]=%x\r\n", FPSCR, m, R(m));
#endif
}

OPCODE(ldsl216) // LDS.L @Rm+, FPUL (0100mmmm 01010110)
{
	fc_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(fc_m), &FPUL);

	R(fc_m) += 4;

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("lds216: FPUL=%x, (no conversion) r[%d]=%x\r\n", FPUL, m, R(m));
#endif
}

OPCODE(sts217) // STS FPSCR, Rn (0000nnnn 01101010)
{
	fc_n = (arg >> 8) & 0x0F;

//	R(n) = (DWORD) FPSCR;
	memcpy(&R(fc_n), &FPSCR, sizeof(DWORD));

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("sts217: FPSCR=%x r[%d]=%x,%d\r\n", FPSCR, n, R(n), R(n));
#endif
}

OPCODE(sts218) // STS FPUL, Rn : FPUL -> Rn (0000nnnn 01011010)
{
	fc_n = (arg >> 8) & 0x0F;
//	signed long fpul = (signed long) FPUL;

// ESTA ESTO BIEN?
	R(fc_n) = (signed long) FPUL;
//	memcpy(&R(n), &fpul, sizeof(DWORD));

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("sts218: r[%d]=%d, FPUL=%x,%f\r\n", n, R(n), (DWORD) FPUL, (float) FPUL);
#endif
}

OPCODE(stsl219) // STS.L FPSCR, @-Rn : Rn - 4 -> Rn, FPSCR -> (Rn) (0100nnnn 01100010)
{
	fc_n = (arg >> 8) & 0x0F;

	R(fc_n) -= 4;
	
	WriteMemoryL(R(fc_n), (DWORD *) &FPSCR);

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("stsl219: FPSCR=%x, r[%d]=%x\r\n", FPSCR, n, R(n));
#endif
}

OPCODE(stsl220) // STS.L FPUL, @-Rn (0100nnnn 01010010)
{
	fc_n = (arg >> 8) & 0x0F;

	R(fc_n) -= 4;

	WriteMemoryL(R(fc_n), (DWORD *) &FPUL);

	PC += 2;

#ifdef DEBUG_FLOAT_CONTROL
	logmsg("stsl220: FPUL=%x,%f r[%d]=%x\r\n", (DWORD) FPUL, (float) FPUL, n, R(n));
#endif
}

