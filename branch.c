/****************************************************************************

	BRANCH - Branch Opcodes for SH-4 Processor

*****************************************************************************/

#include "sh4.h"

OPCODE(bf) // BF label (10001011 ssssssss)
{
//	DWORD s = SignExtend8(arg & 0x00FF)*2 + 4 + PC;

	if (!IS_SR_T())
	{
//		NEXTPC = s;
		NEXTPC = SignExtend8(arg & 0x00FF)*2 + 4 + PC;
		PC_func = PC_f_nextpc;
#ifdef DEBUG_BRANCH
		if (NEXTPC == 0)
		{
			logmsg("bf: nextpc = 0");
		}
#endif
	}
}

OPCODE(bra) // BRA label (1010dddd dddddddd)
{
	DWORD disp = SignExtend12(arg & 0x0FFF) * 2 + PC + 4;

	//	fprintf(fp, "Cambiando PC de %x a %x. arg %x\r\n", PC, disp, arg & 0x0FFF);
	delayslot = disp;
	PC_func = PC_f_delayslot;
#ifdef DEBUG_BRANCH
	logmsg("bra: dslot=%x\r\n", disp);
#endif
}

OPCODE(braf) // BRAF Rn (0000dddd 00100011)
{
	short n = (arg >> 8) & 0x0F;
	DWORD target = (DWORD) R(n) + PC + 4;

	delayslot = target;
	PC_func = PC_f_delayslot;
#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("braf: delayslot = 0");
	}
#endif
}

OPCODE(bsr108) // BSR label (1011dddd dddddddd)
{
	DWORD disp = SignExtend12(arg & 0x0FFF) * 2 + PC + 4;

	PR = PC + 4;

	delayslot = disp;
	PC_func = PC_f_delayslot;
#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("bsr: delayslot = 0");
	}
#endif
}

OPCODE(bsrf109) // BSRF Rn (0000nnnn 00000011)
{
	short n = (arg >> 8) & 0x0F;
	DWORD target = (DWORD) R(n) + PC + 4;

	PR = PC + 4;

	delayslot = target;
	PC_func = PC_f_delayslot;

#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("bsrf: delayslot = 0");
	}
#endif
}

OPCODE(jmp110) // JMP @Rn (0100nnnn 00101011)
{
	short n = (arg >> 8) & 0x0F;

	delayslot = R(n);
	PC_func = PC_f_delayslot;

#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("jmp: delayslot = 0");
	}
#endif
}

OPCODE(jsr111) // JSR @Rn (0100nnnn 00001011)
{
	short n = (arg >> 8) & 0x0F;

	PR = PC + 4;

	delayslot = R(n);
	PC_func = PC_f_delayslot;

#ifdef DEBUG_BRANCH_JSR
    logmsg("jsr: r[%d]=%x\r\n", n, R(n));
#endif
}

OPCODE(rts112) // RTS (00000000 00001011)
{
	delayslot = PR;
	PC_func = PC_f_delayslot;

#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("rts: delayslot = 0\r\n");
	}
#endif
}

OPCODE(bfs) // 10001111dddddddd
{
//	DWORD disp; 

	//	= ((~(arg & 0x00FF)) & 0xFF) + 1)*2 + PC + 4;

	if (!IS_SR_T())
	{
//		disp = SignExtend8(arg & 0x00FF)*2 + PC + 4;
		delayslot = SignExtend8(arg & 0x00FF)*2 + PC + 4;;
		PC_func = PC_f_delayslot;
#ifdef DEBUG_BRANCH
		if (delayslot == 0)
		{
			logmsg("bfs: delayslot = 0");
		}
#endif
	}
}

OPCODE(bt104) // 10001001 dddddddd
{
	if (IS_SR_T())
	{
		NEXTPC = SignExtend8(arg & 0xFF)*2 + PC + 4; // antes era disp = ...
		PC_func = PC_f_nextpc;
#ifdef DEBUG_BRANCH
		if (NEXTPC == 0)
		{
			logmsg("bt: nextpc = 0");
		}
#endif
	}
}

OPCODE(bts105) // 10001101 dddddddd
{
	if (IS_SR_T())
	{
		delayslot = SignExtend8(arg & 0xFF)*2 + PC + 4; // antes era disp = ...
		PC_func = PC_f_delayslot;
#ifdef DEBUG_BRANCH
		if (delayslot == 0)
		{
			logmsg("bts: delayslot = 0");
		}
#endif
	}
}

