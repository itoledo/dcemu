/****************************************************************************

	BRANCH - Branch Opcodes for SH-4 Processor

*****************************************************************************/

#include "sh4emu.h"
#include "excepciones.h"
#include "mem.h"

/*
	La ranura de retardo se ejecuta con un core.execute() anidado. La bandera
	solo la mira run(), para distinguir la excepcion de FPU deshabilitada
	general (0x800) de la de ranura (0x820). Si la instruccion aborta, el
	longjmp se salta el bajado: main_loop() la limpia al recibir el salto.
*/
#define EJECUTAR_RANURA()										\
	do															\
	{															\
		en_ranura_retardo = 1;									\
		core.execute(*(WORD *) get_memory_pointer(PC));			\
		en_ranura_retardo = 0;									\
	} while (0)

OPCODE(bf) // BF label (10001011 ssssssss)
{

	if (!IS_SR_T())
	{
//		NEXTPC = s;
/*		NEXTPC = SignExtend8(arg & 0x00FF)*2 + 4 + PC;
		PC_func = PC_f_nextpc; */
		PC =  SignExtend8(arg & 0x00FF)*2 + 4 + PC;
#ifdef DEBUG_BRANCH
		if (NEXTPC == 0)
		{
			logmsg("bf: nextpc = 0");
		}
#endif
	}
	else
		PC += 2;

	core.context.cycles += 2;
}

OPCODE(bra) // BRA label (1010dddd dddddddd)
{
 	DWORD ddisp = SignExtend12(arg & 0x0FFF) * 2 + PC + 4;

	//	fprintf(fp, "Cambiando PC de %x a %x. arg %x\r\n", PC, disp, arg & 0x0FFF);
/*	delayslot = disp;
	PC_func = PC_f_delayslot; */

	PC += 2;
	EJECUTAR_RANURA();

	PC = ddisp;

	core.context.cycles += 2;

#ifdef DEBUG_BRANCH
	logmsg("bra: dslot=%x\r\n", disp);
#endif
}

OPCODE(braf) // BRAF Rn (0000dddd 00100011)
{
 	short n = (arg >> 8) & 0x0F;
	DWORD target = (DWORD) R(n) + PC + 4;

/*	delayslot = target;
	PC_func = PC_f_delayslot; */
	
	PC += 2;
	EJECUTAR_RANURA();

	PC = target;

	core.context.cycles += 3;

#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("braf: delayslot = 0");
	}
#endif
}

OPCODE(bsr108) // BSR label (1011dddd dddddddd)
{
 	DWORD ddisp = SignExtend12(arg & 0x0FFF) * 2 + PC + 4;

	PR = PC + 4;

/*	delayslot = disp;
	PC_func = PC_f_delayslot; */

	PC += 2;

	EJECUTAR_RANURA();

	PC = ddisp;

	core.context.cycles += 2;

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

/*	delayslot = target;
	PC_func = PC_f_delayslot; */
	
	PC += 2;
	EJECUTAR_RANURA();

	PC = target;

	core.context.cycles += 3;

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
	DWORD target = R(n);

/*	delayslot = R(n);
	PC_func = PC_f_delayslot; */

	PC += 2;
	EJECUTAR_RANURA();

	PC = target;

#ifdef DEBUG_BRANCH
	if (delayslot == 0)
	{
		logmsg("jmp: delayslot = 0");
	}
#endif
	
	core.context.cycles += 3;
}

OPCODE(jsr111) // JSR @Rn (0100nnnn 00001011)
{
 	short n = (arg >> 8) & 0x0F;
	DWORD target = R(n);

	PR = PC + 4;

/*	delayslot = R(n);
	PC_func = PC_f_delayslot; */

	PC += 2;
	EJECUTAR_RANURA();

	PC = target;

	core.context.cycles += 3;

#ifdef DEBUG_BRANCH_JSR
    logmsg("jsr: r[%d]=%x\r\n", n, R(n));
#endif
}

OPCODE(rts112) // RTS (00000000 00001011)
{
/*	delayslot = PR;
	PC_func = PC_f_delayslot; */
	DWORD target = PR;

	PC += 2;
	EJECUTAR_RANURA();

	PC = target;

	core.context.cycles += 3;

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

/*		delayslot = SignExtend8(arg & 0x00FF)*2 + PC + 4;;
		PC_func = PC_f_delayslot; */
		DWORD ddisp =SignExtend8(arg & 0x00FF)*2 + PC + 4;
		PC += 2;
		EJECUTAR_RANURA();

		PC = ddisp;

#ifdef DEBUG_BRANCH
		if (delayslot == 0)
		{
			logmsg("bfs: delayslot = 0");
		}
#endif
	}
	else
		PC += 2;

	core.context.cycles += 2;
}

OPCODE(bt104) // 10001001 dddddddd
{
	if (IS_SR_T())
	{
/*		NEXTPC = SignExtend8(arg & 0xFF)*2 + PC + 4; // antes era disp = ...
		PC_func = PC_f_nextpc; */
		DWORD ddisp = SignExtend8(arg & 0xFF)*2 + PC + 4;
		
		PC = ddisp;

#ifdef DEBUG_BRANCH
		if (NEXTPC == 0)
		{
			logmsg("bt: nextpc = 0");
		}
#endif
	}
	else
		PC += 2;

	core.context.cycles += 2;
}

OPCODE(bts105) // 10001101 dddddddd
{
	if (IS_SR_T())
	{
/*		delayslot = SignExtend8(arg & 0xFF)*2 + PC + 4; // antes era disp = ...
		PC_func = PC_f_delayslot; */

		DWORD ddisp = SignExtend8(arg & 0xFF)*2 + PC + 4;

		PC += 2;
  		EJECUTAR_RANURA();
  		
  		PC = ddisp;

#ifdef DEBUG_BRANCH
		if (delayslot == 0)
		{
			logmsg("bts: delayslot = 0");
		}
#endif
	}
	else
		PC += 2;

	core.context.cycles += 2;
}

