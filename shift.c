/****************************************************************************

	SHIFT - Shift Opcodes for SH-4 Processor

*****************************************************************************/

#include "sh4.h"

OPCODE(rotl86) // ROTL Rn : T <- Rn <- MSB (0100nnnn 00000100)
{
	short n = (arg >> 8) & 0x0F;

	if (R(n) & 0x80000000)
		SET_T
	else
		UNSET_T;

//	R(n) = ((R(n) << 1) & 0xFFFFFFFE) | (SR & REG_T);
	R(n) <<= 1;
	
	if (IS_SR_T())
		R(n) |= 0x00000001;
	else
		R(n) &= 0xFFFFFFFE;

#ifdef DEBUG_SHIFT
    logmsg("rotl: despues %x\r\n", R(n));
#endif
}

OPCODE(rotr87) // ROTR Rn (0100nnnn 00000101)
{
	short n = (arg >> 8) & 0x0F;

	if (R(n) & 0x01)
		SET_T
	else
		UNSET_T;

#ifdef DEBUG_SHIFT
    logmsg("rotr: antes %x\r\n", R(n));
#endif
//	R(n) >>= 1;
	R(n) >>= 1;

//	R(n) = ((R(n) & 0x01) << 31) | ((R(n) >> 1) & 0x7fffffff);
	if (IS_SR_T())
		R(n) |= 0x80000000;
	else
		R(n) &= 0x7FFFFFFF;

#ifdef DEBUG_SHIFT
    logmsg("rotr: despues %x\r\n", R(n));
#endif
}

OPCODE(rotcl88) // ROTCL Rn (0100nnnn 00100100)
{
	short n = (arg >> 8) & 0x0F;
    int t;
    
    t = IS_SR_T() ? 1 : 0;
    
    if (R(n) & 0x80000000)
        SET_T
    else
        UNSET_T
        
    R(n) <<= 1;
    
    if (t)
        R(n) |= 0x1;
    else
        R(n) &= 0xFFFFFFFE;

#ifdef DEBUG_SHIFT
    logmsg("rotcl: r[%d]=%x\r\n", n, R(n));
#endif
}

OPCODE(rotcr89) // ROTCR Rn (0100nnnn 00100101)
{
	short n = (arg >> 8) & 0x0F;
	int t;

	t = R(n) & 0x1;

	R(n) >>= 1;
	
	if (IS_SET(SR, SR_T))
		R(n) |= 0x80000000;
	else
		R(n) &= 0x7FFFFFFF;

	if (t)
		SET_BIT(SR, SR_T);
	else
		REMOVE_BIT(SR, SR_T);

#ifdef DEBUG_SHIFT
    logmsg("rotcr89: r[%d]=%x\r\n", n, R(n));
#endif
}

OPCODE(shad90) // SHAD Rm, Rn (0100nnnn mmmm1100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	signed long rm;
	long amount;

	if (R(m) == 0)
		return;

#ifdef DEBUG_SHIFT
    logmsg("shad: antes %x\r\n", R(n));
#endif

//	COPY_REG(rm, R(m));
	amount = R(m) & 0x1F;

	if ((signed) R(m) > 0) // left shift
	{
		R(n) <<= amount;
	}
	else // right shift
	if (amount != 0)
	{
		(signed) R(n) >>= (32 - amount); // IMPORTANTE!!!!!!
	}
	else
	if ((signed) R(n) < 0)
	{
		R(n) = -1;
	}
	else
	{
		R(n) = 0;
	}

#ifdef DEBUG_SHIFT
    logmsg("shad: despues %x\r\n", R(n));
#endif
}

OPCODE(shar92) // SHAR Rn : MSB -> Rn -> T (0100nnnn 00100001)
{
	short n = (arg >> 8) & 0x0F;
	long temp;
//	signed long rn;

#ifdef DEBUG_SHIFT
    logmsg("shar: antes %x\r\n", R(n));
#endif

//	COPY_REG(rn, R(n));

	if (R(n) & 0x01)
		SET_T
	else
		UNSET_T

	if ((R(n) & 0x80000000) == 0)
		temp = 0;
	else
		temp = 1;

	R(n) >>= 1;
	
	if (temp == 1)
		R(n) |= 0x80000000;
	else
		R(n) &= 0x7FFFFFFF;

//	COPY_REG(R(n), rn);

#ifdef DEBUG_SHIFT
    logmsg("shar: despues %x\r\n", R(n));
#endif
}

OPCODE(shld93) // SHLD Rm, Rn (0100nnnn mmmm1101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	signed long rm;

#ifdef DEBUG_SHIFT
    logmsg("shld: paso1: r[%d]=%x, r[%d]=%x\r\n", m, R(m), n, R(n));
#endif

//	COPY_REG(rm, R(m));

	if (R(m) == 0)
		return;

	if ((signed) R(m) > 0)
	{
#ifdef DEBUG_SHIFT
	   logmsg("shift left: %d\r\n", R(m) & 0x1F);
#endif
		R(n) <<= (R(m) & 0x1F);
	}
	else
	if ((signed) R(m) < 0)
	{
#ifdef DEBUG_SHIFT
        logmsg("shift right: %d\r\n", (32 - (R(m) & 0x1F)));
#endif
		R(n) >>= (32 - (R(m) & 0x1F)); // era con (DWORD) al comienzo
	}
	else
		R(n) = 0;

#ifdef DEBUG_SHIFT
    logmsg("shld: paso2: r[%d]=%x, r[%d]=%x\r\n", m, R(m), n, R(n));
#endif
}

OPCODE(shll94) // SHLL Rn : T <- Rn <- 0 (0100nnnn 00000000)
{
	short n = (arg >> 8) & 0x0F;

#ifdef DEBUG_SHIFT
    logmsg("shll: antes %x\r\n", R(n));
#endif

	if (R(n) & 0x80000000)
	{
		SET_T
	}
	else
	{
		UNSET_T
	}
	
#ifdef ASM_DEBUG
	fprintf(logfp, "shll: n:%d, reg[n]=%x\r\n", n, R(n));
#endif

	R(n) <<= 1;
	
#ifdef DEBUG_SHIFT
    logmsg("shll: despues %x\r\n", R(n));
#endif
}

OPCODE(shlr95) // SHLR Rn : 0 -> Rn -> T (0100nnnn 00000001)
{
	short n = (arg >> 8) & 0x0F;

	if (R(n) & 0x0001)
	{
		SET_T
	}
	else
	{
		UNSET_T
	}
	
	R(n) >>= 1;
	
	// probando
//	R(n) &= 0x7FFFFFFF;

#ifdef DEBUG_SHIFT
    logmsg("shlr: r[%d]=%x\r\n", n, R(n));
#endif
}

OPCODE(shll2) // SHLL2 Rn: Rn << 2 -> Rn (0100nnnn 00001000)
{
	short n = (arg >> 8) & 0x0F;

	R(n) <<= 2;
}

OPCODE(shlr2) // SHLR2 Rn: Rn >> 2 -> Rn (0100nnnn 00001001)
{
	short n = (arg >> 8) & 0x0F;

	R(n) >>= 2;
	
	// probando!
//	R(n) &= 0x3FFFFFFF;

#ifdef DEBUG_SHIFT
	logmsg("shlr2: r[%d]=%x\r\n", n, R(n));
#endif
}

OPCODE(shll8) // SHLL8 Rn (0100nnnn 00011000)
{
	short n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	logmsg("shll8: n:%d, reg[n]=%d\r\n", n, R(n));
#endif

	R(n) <<= 8;

#ifdef ASM_DEBUG
	logmsg("shll8: res=%d\r\n", n, R(n));
#endif
}

OPCODE(shlr8) // SHLR8 Rn (0100nnnn 00011001)
{
	short n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr8: n:%d, reg[n]=%d\r\n", n, R(n));
#endif

	R(n) >>= 8;

	// probando
//	R(n) &= 0x00FFFFFF;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr8: res=%d\r\n", n, R(n));
#endif
}

OPCODE(shll16) // SHLL16 Rn (0100nnnn 00101000)
{
	short n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	logmsg("shll16: n:%d, reg[n]=%d\r\n", n, R(n));
#endif

	R(n) <<= 16;
	
#ifdef ASM_DEBUG
	logmsg("shll16: res=%d\r\n", n, R(n));
#endif
}

OPCODE(shlr16) // SHLR16 Rn (0100nnnn 00101001)
{
	short n = (arg >> 8) & 0x0F;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr16: n:%d, reg[n]=%d\r\n", n, R(n));
#endif

	R(n) >>= 16;

	// probando
//	R(n) &= 0x0000FFFF;

#ifdef ASM_DEBUG
	fprintf(logfp, "shlr16: res=%d\r\n", n, R(n));
#endif
}

