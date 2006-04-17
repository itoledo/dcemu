/****************************************************************************

	SHIFT - Shift Opcodes for SH-4 Processor

*****************************************************************************/

#include "sh4emu.h"

#ifdef _fast_interpreter_
#define st_n jumptable.jit_array[jumptable.current_pos].cache.n
#define st_m  jumptable.jit_array[jumptable.current_pos].cache.m
#else
short st_n=0;
short st_m=0;
#endif

OPCODE(rotl86) // ROTL Rn : T <- Rn <- MSB (0100nnnn 00000100)
{
 	st_n = (arg >> 8) & 0x0F;

	if (R(st_n) & 0x80000000)
		SET_T
	else
		UNSET_T

//	R(st_n) = ((R(st_n) << 1) & 0xFFFFFFFE) | (SR & REG_T);
	R(st_n) <<= 1;
	
	if (IS_SR_T())
		R(st_n) |= 0x00000001;
	else
		R(st_n) &= 0xFFFFFFFE;

	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("rotl: despues %x\r\n", R(st_n));
#endif
}

OPCODE(rotr87) // ROTR Rn (0100nnnn 00000101)
{
	st_n = (arg >> 8) & 0x0F;

	if (R(st_n) & 0x01)
		SET_T
	else
		UNSET_T

#ifdef DEBUG_SHIFT
    logmsg("rotr: antes %x\r\n", R(st_n));
#endif
//	R(st_n) >>= 1;
	R(st_n) >>= 1;

//	R(st_n) = ((R(st_n) & 0x01) << 31) | ((R(st_n) >> 1) & 0x7fffffff);
	if (IS_SR_T())
		R(st_n) |= 0x80000000;
	else
		R(st_n) &= 0x7FFFFFFF;

	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("rotr: despues %x\r\n", R(st_n));
#endif
}

OPCODE(rotcl88) // ROTCL Rn (0100nnnn 00100100)
{
	st_n = (arg >> 8) & 0x0F;
    int t;
    
    t = IS_SR_T() ? 1 : 0;
    
    if (R(st_n) & 0x80000000)
        SET_T
    else
        UNSET_T
        
    R(st_n) <<= 1;
    
    if (t)
        R(st_n) |= 0x1;
    else
        R(st_n) &= 0xFFFFFFFE;

	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("rotcl: r[%d]=%x\r\n", n, R(st_n));
#endif
}

OPCODE(rotcr89) // ROTCR Rn (0100nnnn 00100101)
{
	st_n = (arg >> 8) & 0x0F;
	int t;

	t = R(st_n) & 0x1;

	R(st_n) >>= 1;
	
	if (IS_SH4_REG_SET(SR_T))
		R(st_n) |= 0x80000000;
	else
		R(st_n) &= 0x7FFFFFFF;

	if (t)
		SET_SH4_BIT(SR_T);
	else
		REMOVE_SH4_BIT(SR_T);

	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("rotcr89: r[%d]=%x\r\n", n, R(st_n));
#endif
}

OPCODE(shad90) // SHAD Rm, Rn (0100nnnn mmmm1100)
{
 	st_n = (arg >> 8) & 0x0F;
 	st_m = (arg >> 4) & 0x0F;
//	signed long rm;
	long amount;

	PC += 2;

	if (R(st_m) == 0)
		return;

#ifdef DEBUG_SHIFT
    logmsg("shad: antes %x\r\n", R(st_n));
#endif

//	COPY_REG(rm, R(st_m));
	amount = R(st_m) & 0x1F;

	if ((signed) R(st_m) > 0) // left shift
	{
		R(st_n) <<= amount;
	}
	else // right shift
	if (amount != 0)
	{
		 R(st_n) = ((signed) R(st_n)) >> (32 - amount); // IMPORTANTE!!!!!!
	}
	else
	if ((signed) R(st_n) < 0)
	{
		R(st_n) = -1;
	}
	else
	{
		R(st_n) = 0;
	}

#ifdef DEBUG_SHIFT
    logmsg("shad: despues %x\r\n", R(st_n));
#endif
}

OPCODE(shar92) // SHAR Rn : MSB -> Rn -> T (0100nnnn 00100001)
{
	st_n = (arg >> 8) & 0x0F;
	long temp;
//	signed long rn;

#ifdef DEBUG_SHIFT
    logmsg("shar: antes %x\r\n", R(st_n));
#endif

//	COPY_REG(rn, R(st_n));

	if (R(st_n) & 0x01)
		SET_T
	else
		UNSET_T

	if ((R(st_n) & 0x80000000) == 0)
		temp = 0;
	else
		temp = 1;

	R(st_n) >>= 1;
	
	if (temp == 1)
		R(st_n) |= 0x80000000;
	else
		R(st_n) &= 0x7FFFFFFF;

//	COPY_REG(R(st_n), rn);

	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("shar: despues %x\r\n", R(st_n));
#endif
}

OPCODE(shld93) // SHLD Rm, Rn (0100nnnn mmmm1101)
{
	st_n = (arg >> 8) & 0x0F;
	st_m = (arg >> 4) & 0x0F;
//	signed long rm;

#ifdef DEBUG_SHIFT
    logmsg("shld: paso1: r[%d]=%x, r[%d]=%x\r\n", m, R(st_m), n, R(st_n));
#endif

//	COPY_REG(rm, R(st_m));

	PC += 2;

	if (R(st_m) == 0)
		return;

	if ((signed) R(st_m) > 0)
	{
#ifdef DEBUG_SHIFT
	   logmsg("shift left: %d\r\n", R(st_m) & 0x1F);
#endif
		R(st_n) <<= (R(st_m) & 0x1F);
	}
	else
	if ((signed) R(st_m) < 0)
	{
#ifdef DEBUG_SHIFT
        logmsg("shift right: %d\r\n", (32 - (R(st_m) & 0x1F)));
#endif
		R(st_n) >>= (32 - (R(st_m) & 0x1F)); // era con (DWORD) al comienzo
	}
	else
		R(st_n) = 0;

#ifdef DEBUG_SHIFT
    logmsg("shld: paso2: r[%d]=%x, r[%d]=%x\r\n", m, R(st_m), n, R(st_n));
#endif
}

OPCODE(shll94) // SHLL Rn : T <- Rn <- 0 (0100nnnn 00000000)
{
 	st_n = (arg >> 8) & 0x0F;

#ifdef DEBUG_SHIFT
    logmsg("shll: antes %x\r\n", R(st_n));
#endif

	if (R(st_n) & 0x80000000)
	{
		SET_T
	}
	else
	{
		UNSET_T
	}
	
#ifdef ASM_DEBUG
	fprintf(logfp, "shll: n:%d, reg[n]=%x\r\n", n, R(st_n));
#endif

	R(st_n) <<= 1;
	
	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("shll: despues %x\r\n", R(st_n));
#endif
}

OPCODE(shlr95) // SHLR Rn : 0 -> Rn -> T (0100nnnn 00000001)
{
	st_n = (arg >> 8) & 0x0F;

	if (R(st_n) & 0x0001)
	{
		SET_T
	}
	else
	{
		UNSET_T
	}
	
	R(st_n) >>= 1;
	
	// probando
	R(st_n) &= 0x7FFFFFFF;

	PC += 2;

#ifdef DEBUG_SHIFT
    logmsg("shlr: r[%d]=%x\r\n", n, R(st_n));
#endif
}

OPCODE(shll2) // SHLL2 Rn: Rn << 2 -> Rn (0100nnnn 00001000)
{
	st_n = (arg >> 8) & 0x0F;

	R(st_n) <<= 2;

	PC += 2;
}

OPCODE(shlr2) // SHLR2 Rn: Rn >> 2 -> Rn (0100nnnn 00001001)
{
 	st_n = (arg >> 8) & 0x0F;

	R(st_n) >>= 2;
	
	// probando!
	R(st_n) &= 0x3FFFFFFF;

	PC += 2;

#ifdef DEBUG_SHIFT
	logmsg("shlr2: r[%d]=%x\r\n", n, R(st_n));
#endif
}

OPCODE(shll8) // SHLL8 Rn (0100nnnn 00011000)
{
	st_n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	logmsg("shll8: n:%d, reg[n]=%d\r\n", n, R(st_n));
#endif

	R(st_n) <<= 8;

	PC += 2;

#ifdef ASM_DEBUG
	logmsg("shll8: res=%d\r\n", n, R(st_n));
#endif
}

OPCODE(shlr8) // SHLR8 Rn (0100nnnn 00011001)
{
	st_n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr8: n:%d, reg[n]=%d\r\n", n, R(st_n));
#endif

	R(st_n) >>= 8;

	// probando
	R(st_n) &= 0x00FFFFFF;

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr8: res=%d\r\n", n, R(st_n));
#endif
}

OPCODE(shll16) // SHLL16 Rn (0100nnnn 00101000)
{
 	st_n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	logmsg("shll16: n:%d, reg[n]=%d\r\n", n, R(st_n));
#endif

	R(st_n) <<= 16;
	
	PC += 2;

#ifdef ASM_DEBUG
	logmsg("shll16: res=%d\r\n", n, R(st_n));
#endif
}

OPCODE(shlr16) // SHLR16 Rn (0100nnnn 00101001)
{
	st_n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr16: n:%d, reg[n]=%d\r\n", n, R(st_n));
#endif

	R(st_n) >>= 16;

	// probando
	R(st_n) &= 0x0000FFFF;

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr16: res=%d\r\n", n, R(st_n));
#endif
}

