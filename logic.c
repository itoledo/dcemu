#include "sh4emu.h"


OPCODE(and72) // AND Rm, Rn (0010 nnnn mmmm 1001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

#ifdef ASM_DEBUG
	fprintf(logfp, "and72: n:%d, m:%d, reg[n]=%d, reg[m]=%d\r\n",
		n, m, R(n), R(m));
#endif
	
	R(n) &= R(m);

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "and72: res=%d\r\n", R(n));
#endif

	core.context.cycles += 1;
}

OPCODE(and73) // AND #imm, R0 (1100 1001 iiiiiiii)
{
   long imm = arg & 0xFF;

#ifdef DEBUG_LOGIC
	fprintf(fp, "and73: r[0]=%x, i=%x\r\n", R(0), i);
#endif
	
	R(0) &= imm;

	PC += 2;

#ifdef DEBUG_LOGIC
	fprintf(fp, "and73: res=%x\r\n", R(0));
#endif

	core.context.cycles += 1;
}

OPCODE(not75) // NOT Rm, Rn (0110nnnn mmmm0111)
{
	short  n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	
	R(n) = ~R(m);

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(or76) // OR Rm, Rn (0010 nnnn mmmm 1011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

#ifdef ASM_DEBUG
	fprintf(logfp, "or76: n:%d, m:%d, reg[n]=%x, reg[m]=%x\r\n",
		n, m, R(n), R(m));
#endif
	
	R(n) |= R(m);

	PC += 2;

	core.context.cycles += 1;

#ifdef ASM_DEBUG
	fprintf(logfp, "or76: res=%x\r\n", R(n));
#endif
}

OPCODE(or77) // OR #imm, R0 (11001011 iiiiiiii)
{
	long imm = arg & 0x00FF;

	R(0) |= imm;

	PC += 2;

	core.context.cycles += 5;
}

OPCODE(tasb79) // TAS.B @Rn (0100nnnn 00011011)
{
  short  n = (arg >> 8) & 0x0F;
    BYTE valor;
    
    ReadMemoryB(R(n), &valor);
    
    if (valor == 0)
        SET_T
    else
        UNSET_T

    valor |= 0x80;
    
    WriteMemoryB(R(n), &valor);

	PC += 2;

	core.context.cycles += 5;
}

OPCODE(tst80) // TST Rm, Rn (0010nnnn mmmm1000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	if (R(n) & R(m))
	 UNSET_T
    else
     SET_T

	PC += 2;

	core.context.cycles += 1;

#ifdef DEBUG_LOGIC
    logmsg("tst80: r[%d]=%x, r[%d]=%x\r\n", m, R(m), n, R(n));
#endif
}

OPCODE(tst81) // TST #imm, R0 (11001000 iiiiiiii)
{
	long imm = arg & 0xFF;

	if ((R(0) & imm))
	  UNSET_T
   else
      SET_T

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(xor83) // (0010 nnnn mmmm 1010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

/*	logmsg("xor83: antes: r[%d]=%x,%d r[%d]=%x,%d\r\n",
 		m, R(m), (signed long) R(m),
   		n, R(n), (signed long) R(m)); */

	R(n) ^= R(m);

/*	logmsg("xor83: despues: r[%d]=%x,%d r[%d]=%x,%d\r\n",
 		m, R(m), (signed long) R(m),
   		n, R(n), (signed long) R(m)); */

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(xor84) // XOR #imm, R0 (11001010 iiiiiiii)
{
   long imm = arg & 0xFF;

	R(0) ^= imm;

	PC += 2;

	core.context.cycles += 1;
}

