#include "sh4.h"
#include "opcodes.h"
#include <math.h>

#define PI 3.1415926535

OPCODE(fsca) // FSCA FPUL, DRn
{
//	long n = (arg >> 8) & 0x0F;
	long n = (arg >> 9) & 0x07;
	float x = (float) (2 * PI * (float) FPUL / 65536.0);
//	float x = (float) (PI * FPUL / 65536.0);

	FR(n*2) = sin(x);
	FR(n*2+1) = cos(x);
/*	float_R(n) = sin(FPUL);
	float_registers[n+1] = cos(FPUL); */

/*	float_R(n) = 0;
	float_registers[n+1] = 0; */

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "fsca: n:%d, FPUL=%f, fl_reg[n]=%f, fl_reg[n+1]=%f\r\n",
		n, (float) FPUL, FR(n), float_registers[n+1]);
#endif
}

OPCODE(NOIMP)
{
	logmsg("opcode no implementado: %s (%d)\r\n", opcodes[find_opcode(PC)].opdesc, find_opcode(PC));
	PC += 2;
}

