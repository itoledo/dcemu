/****************************************************************************

	ARITH - Arithmetic Opcodes for SH-4 Processor

*****************************************************************************/

#include "sh4.h"

OPCODE(add39) // ADD Rm, Rn : Rn + Rm -> Rn (0011nnnn mmmm1100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
/*	signed long rm, rn;

	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n));
	rn += rm;
	COPY_REG(R(n), rn); */
	
	R(n) += R(m);

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("add39: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        n, R(n), (signed) R(n),
        m, R(m), (signed) R(m));
#endif
}

OPCODE(add40) // ADD #imm, Rn
{
	short n = (arg >> 8) & 0x0F;
	signed long s = SignExtend8(arg & 0xFF);
/*	signed long rn;

	COPY_REG(rn, R(n));
	rn += s;
	COPY_REG(R(n), rn); */

	R(n) += s;

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("add40: r[%d]=%x,%d\r\n",
        n, R(n), (signed) R(n));
#endif
}

OPCODE(addc41) // ADDC Rm, Rn (0011nnnn mmmm1110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	unsigned long tmp0, tmp1;
	
	tmp1 = R(n) + R(m);
	tmp0 = R(n);
	
	R(n) = tmp1 + (IS_SR_T() ? 1 : 0);
	
	if (tmp0 > tmp1)
		SET_BIT(SR, SR_T);
	else
		REMOVE_BIT(SR, SR_T);

	if (tmp1 > R(n))
		SET_BIT(SR, SR_T);

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("addc41: r[%d]=%x,%d r[%d]=%x,%d tmp0=%x,%d, tmp1=%x,%d\r\n",
        m, R(m), (signed) R(m),
        n, R(n), (signed) R(n),
        tmp0, tmp0, tmp1, tmp1);
#endif
}

OPCODE(cmpeq43) // CMP/EQ #imm, R0 (10001000 iiiiiiii)
{
	signed long i = SignExtend8(arg & 0xFF);
//	signed long r0;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/eq: r[0]=%x,%d,'%c' == #%x,%d,'%c' ?\r\n",
        (signed) R(0), (signed) R(0), (signed) R(0),
        (signed) i, (signed) i, (signed) i);
#endif

/*	COPY_REG(r0, R(0));

	if (r0 == i) */
	if (R(0) == i)
		SET_T
	else
		UNSET_T

	PC += 2;

}

OPCODE(cmpeq44) // CMP/EQ Rm, Rn (0011nnnn mmmm0000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	signed long rm, rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/eq: r[%d]=%x,%d,'%c' == r[%d]=%x,%d,'%c' ?\r\n",
        n, (signed) R(n), (signed) R(n), (signed) R(n),
        m, (signed) R(m), (signed) R(m), (signed) R(m));
#endif

/*	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n)); */

	if ((signed) R(m) == (signed) R(n))
//	if (rm == rn)
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmphs45) // CMP/HS Rm, Rn (0011nnnn mmmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	unsigned long rm, rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/hs: r[%d]=%x >= r[%d]=%x ?\r\n", n, (unsigned long) R(n), m, (unsigned long) R(m));
#endif

/*	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n));

	if (rn >= rm) */
	if ((unsigned long) R(n) >= (unsigned long) R(m))
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmpge46) // CMP/GE Rm, Rn (0011nnnn mmmm0011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	signed long rm, rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/ge: r[%d]=%x >= r[%d]=%x ?\r\n", n, (signed long) R(n), m, (signed long) R(m));
#endif

/*	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n));

	if (rn >= rm) */
	if ((signed long) R(n) >= (signed long) R(m))
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmphi47) // CMP/HI Rm, Rn (0011nnnn mmmm0110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	unsigned long rm, rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/hi: r[%d]=%x > r[%d]=%x ?\r\n", n, (unsigned long) R(n), m, (unsigned long) R(m));
#endif

/*	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n)); */

	if ((unsigned long) R(n) > (unsigned long) R(m))
//	if (rn > rm)
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmpgt48) // CMP/GT Rm, Rn (0011nnnn mmmm0111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	signed long rm, rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/gt: r[%d]=%x > r[%d]=%x ?\r\n", n, (signed long) R(n), m, (signed long) R(m));
#endif

/*	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n)); */

	if ((signed long) R(n) > (signed long) R(m))
//	if (rn > rm)
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmppz49) // CMP/PZ Rn (0100nnnn 00010001)
{
	short n = (arg >> 8) & 0x0F;
//	signed long rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/pz: r[%d]=%x >= 0 ?\r\n", n, (signed long) R(n));
#endif

//	COPY_REG(rn, R(n));

	if ((signed long) R(n) >= 0)
//	if (rn >= 0)
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmppl50) // CMP/PL Rn (0100nnnn 00010101)
{
	short n = (arg >> 8) & 0x0F;
//	signed long rn;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/pl: r[%d]=%x > 0 ?\r\n", n, (signed long) R(n));
#endif

//	COPY_REG(rn, R(n));

	if ((signed long) R(n) > 0)
//	if (rn > 0)
		SET_T
	else
		UNSET_T

	PC += 2;
}

OPCODE(cmpstr51) // CMP/STR Rm, Rn (0010nnnn mmmm1100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

/*	if ((R(m) & 0xFF000000) == (R(n) & 0xFF000000)
	||  (R(m) & 0x00FF0000) == (R(n) & 0x00FF0000)
	||  (R(m) & 0x0000FF00) == (R(n) & 0x0000FF00)
	||  (R(m) & 0x000000FF) == (R(n) & 0x000000FF))
	    SET_T
    else
        UNSET_T */
        
    unsigned long temp;
    long HH, HL, LH, LL;
    
    temp = R(n) ^ R(m);
    HH = (temp & 0xFF000000) >> 12;
    HL = (temp & 0x00FF0000) >> 8;
    LH = (temp & 0x0000FF00) >> 4;
    LL = (temp & 0x000000FF);

    HH = HH && HL && LH && LL;

    if (HH == 0)
    	SET_T
	else
		UNSET_T;

#ifdef DEBUG_ARITH_CMP
    logmsg("cmp/str: r[%d]=%x, r[%d]=%x\r\n", n, R(n), m, R(m));
#endif

	PC += 2;
}

OPCODE(div0s53) // DIV0S Rm, Rn (0010nnnn mmmm0111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	
	if (R(n) & 0x80000000)
		SET_BIT(SR, SR_Q);
    else
		REMOVE_BIT(SR, SR_Q);

	if (R(m) & 0x80000000)
		SET_BIT(SR, SR_M);
	else
		REMOVE_BIT(SR, SR_M);

	if ((IS_SR_Q() && IS_SR_M()) || (!IS_SR_Q() && !IS_SR_M()))
		REMOVE_BIT(SR, SR_T);
	else
		SET_BIT(SR, SR_T);

	PC += 2;
}

OPCODE(div1s52) // DIV1 Rm, Rn (0011nnnn mmmm0100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
    unsigned long tmp0;
    unsigned char old_q,tmp1;
    
    old_q=IS_SR_Q() ? 1 : 0;
    if (R(n) & 0x80000000)
        SET_BIT(SR, SR_Q);
    else
        REMOVE_BIT(SR, SR_Q);
//    Q=(unsigned char)((0x80000000 & R[n])!=0);
    R(n) <<= 1;

    if (IS_SR_T())
        R(n) |= 0x1;

    switch(old_q)
    {
        case 0:
        switch(IS_SR_M() ? 1 : 0)
        {
            case 0:
            tmp0=R(n);
            R(n)-=R(m);
            tmp1=(R(n) > tmp0);
            switch(IS_SR_Q() ? 1 : 0)
            {
                case 0:
                if (tmp1)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
//                Q=tmp1;
                break;

                case 1:
//                Q=(unsigned char)(tmp1==0);
                if (tmp1 == 0)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
                break;
            }
            break;

            case 1:
            tmp0=R(n);
            R(n)+=R(m);
            tmp1=(R(n)<tmp0);
            switch(IS_SR_Q() ? 1 : 0)
            {
                case 0:
//                Q=(unsigned char)(tmp1==0);
                if (tmp1 == 0)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
                break;

                case 1:
//                Q=tmp1;
                if (tmp1)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
                break;
            }
            break;
        }
        break;

        case 1:
        switch(IS_SR_M() ? 1 : 0)
        {
            case 0:
            tmp0=R(n);
            R(n)+=R(m);
            tmp1=(R(n)<tmp0);

            switch(IS_SR_Q() ? 1 : 0)
            {
                case 0:
//                Q=tmp1;
                if (tmp1)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
                break;

                case 1:
//                Q=(unsigned char)(tmp1==0);
                if (tmp1 == 0)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
                break;
            }
            break;

            case 1:
            tmp0=R(n);
            R(n)-=R(m);
            tmp1=(R(n)>tmp0);
            switch(IS_SR_Q() ? 1 : 0)
            {
                case 0:
//                Q=(unsigned char)(tmp1==0);
                if (tmp1 == 0)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
                break;

                case 1:
                if (tmp1)
                    SET_BIT(SR, SR_Q);
                else
                    REMOVE_BIT(SR, SR_Q);
//                Q=tmp1;
                break;
            }
            break;
        }
        break;
    }

    if (IS_SR_Q() && IS_SR_M())
        SET_BIT(SR, SR_T);
    else
        REMOVE_BIT(SR, SR_T);
//    T=(Q==M);

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("div1s: r[%d]=%x r[%d]=%x\r\n", m, R(m), n, R(n));
#endif
}

OPCODE(div0u54) // DIV0U (00000000 00011001)
{
    REMOVE_BIT(SR, SR_Q);
    REMOVE_BIT(SR, SR_M);
    REMOVE_BIT(SR, SR_T);

	PC += 2;
}

OPCODE(dmulsl55) // DMULS.L Rm, Rn (0011nnnn mmmm1101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	signed long long x;
	
	x = (signed long long) (signed long) R(n) * (signed long long) (signed long) R(m);

	MACL = (DWORD) (x & 0xFFFFFFFF);
	MACH = (DWORD) ((x >> 32) & 0xFFFFFFFF);

	PC += 2;

#ifdef DEBUG_ARITH_DMUL
    logmsg("dmuls.l: r[%d]=%x,%d, r[%d]=%x,%d, MACL=%x,%d, MACH=%x,%d\r\n",
        n, R(n), R(n),
        m, R(m), R(m),
        MACL, MACL,
        MACH, MACH);
#endif
}

OPCODE(dmulul56) // DMULU.L Rm, Rn (0011nnnn mmmm0101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	unsigned long long x;

	x = (unsigned long long) (unsigned long) R(n) * (unsigned long long) (unsigned long) R(m);

	MACL = (DWORD) (x & 0xFFFFFFFF);
	MACH = (DWORD) ((x >> 32) & 0xFFFFFFFF);

	PC += 2;

#ifdef DEBUG_ARITH_DMUL
    logmsg("dmulu.l: r[%d]=%x,%d, r[%d]=%x,%d, MACL=%x,%d, MACH=%x,%d\r\n",
        n, R(n), R(n),
        m, R(m), R(m),
        MACL, MACL,
        MACH, MACH);
#endif
}

OPCODE(dt) // DT Rn (0100nnnn 00010000)
{
	short n = (arg >> 8) & 0x0F;

#ifdef DEBUG_ARITH
	logmsg("dt: r[%d]=%x\r\n", n, R(n));
#endif

	if (--R(n))
		UNSET_T
	else
		SET_T

	PC += 2;
}

OPCODE(macl62)	// MAC.L @Rm+, @Rn+	(0000nnnn mmmm1111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	DWORD rm, rn;
	signed long long mul, mac, result;

	memread(R(m), &rm, sizeof(DWORD));
	memread(R(n), &rn, sizeof(DWORD));
	
	logmsg("macl62: R(%d)=%x->%x, R(%d)=%x->%x\n", m, R(m), rm, n, R(n), rn);

	R(m) += 4;
	R(n) += 4;
	
	mul = rm * rn;
	mac = ((unsigned long long) MACH << 32) + MACL;
	
	result = mac + mul;
	
	MACL = (DWORD) (result & 0xFFFFFFFF);
	MACH = (DWORD) ((result >> 32) & 0xFFFFFFFF);

	PC += 2;
}

OPCODE(neg67) // NEG Rm, Rn : 0 - Rm -> Rn (0110nnnn mmmm1011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
/*	signed long rm, rn;

	COPY_REG(rm, R(m));

	rn = 0 - rm; */

	R(n) = (signed) 0 - (signed) R(m);
//	COPY_REG(R(n), rn);

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("neg67: r[%d]=%x,%d\r\n", n, R(n), (signed) R(n));
#endif
}

OPCODE(negc68) // NEGC Rm, Rn (0110nnnn mmmm1010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
/*	signed long result;

	dump_registers();

	result = 0 - R(m) - (IS_SR_T() ? 1 : 0);
	R(n) = result;

	dump_registers(); */
	
	// FIXME
	unsigned long temp;
	
//	dump_registers();

	temp = 0 - R(m);
#ifdef DEBUG_ARITH
    logmsg("temp: %x\r\n", temp);
#endif
	R(n) = temp - (IS_SR_T() ? 1 : 0);

	if (0 < temp)
		SET_T
	else
		UNSET_T

	if (temp < R(n))
		SET_T

#ifdef DEBUG_ARITH
    logmsg("negc68: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        n, R(n), (signed) R(n),
        m, R(m), (signed) R(m));
#endif
//	dump_registers();

	PC += 2;
}

OPCODE(sub69) // SUB Rm, Rn : Rn - Rm -> Rn (0011nnnn mmmm1000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
//	signed long rm, rn;

//	(signed) R(n) -= (signed) R(m);
	R(n) -= R(m);
/*	COPY_REG(rm, R(m));
	COPY_REG(rn, R(n));
	
	rn -= rm;
	
	COPY_REG(R(n), rn); */

	PC += 2;
}

OPCODE(subc70) // SUB Rm, Rn : Rn - Rm -> Rn (0011nnnn mmmm1010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	unsigned long tmp0, tmp1;

//	R(n) = R(n) - R(m) - (IS_SR_T() ? 1 : 0);
//	tmp1 = (signed) R(n) - (signed) R(m);
	tmp1 = R(n) - R(m);
	tmp0 = R(n);
	R(n) = tmp1 - (IS_SR_T() ? 1 : 0);
	if (tmp0 < tmp1)
	{
		SET_T
	}
	else
	{
		UNSET_T;
	}
	if (tmp1 < R(n))
	{
		SET_T;
	}

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("subc70: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        m, R(m), (signed) R(m),
        n, R(n), (signed) R(n));
#endif
}

OPCODE(extsb58) // EXTS.B Rm, Rn (0110nnnn mmmm1110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) = SignExtend8(R(m) & 0x000000FF);

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("extsb58: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        n, R(n), (signed) R(n),
        m, R(m), (signed) R(m));
#endif
}

OPCODE(extsw59) // EXTS.W Rm, Rn (0110nnnn mmmm1111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) = SignExtend16(R(m) & 0x0000FFFF);

	PC += 2;

#ifdef DEBUG_ARITH
    logmsg("extsw59: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        n, R(n), (signed) R(n),
        m, R(m), (signed) R(m));
#endif
}

OPCODE(extub60) // EXTU.B Rm, Rn
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) = (R(m) & 0x000000FF);

	PC += 2;

#ifdef DEBUG_ARITH
	logmsg("extu.b: r[%d]=%x, r[%d]=%x\r\n", m, R(m), n, R(n));
#endif
}

OPCODE(extuw61) // EXTU.W Rm, Rn (0110nnnn mmmm1101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) = R(m) & 0x0000FFFF;

	PC += 2;
}
    
OPCODE(mull) // Rn x Rm -> MACL (0000nnnn mmmm0111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	//	MACL = (R(n) * R(m)) & 0xFFFFFFFF;
	signed long long x = (signed long long) R(n) * (signed long long) R(m);
	MACL = (DWORD) (x & 0xFFFFFFFF);

	PC += 2;

#ifdef DEBUG_ARITH_DMUL
    logmsg("mull: r[%d]=%x,%d, r[%d]=%x,%d, MACL=%x,%d\r\n",
        n, R(n), R(n),
        m, R(m), R(m),
        MACL, MACL);
#endif
}

OPCODE(mulsw65) // MULS.W Rm, Rn (0010nnnn mmmm1111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	MACL = ((signed) (R(n) & 0xFFFF) * (signed) (R(m) & 0xFFFF));

	PC += 2;

#ifdef DEBUG_ARITH_DMUL
    logmsg("mulsw65: r[%d]=%x,%d, r[%d]=%x,%d, MACL=%x,%d\r\n",
        n, R(n), R(n),
        m, R(m), R(m),
        MACL, MACL);
#endif
}

OPCODE(muluw66) // MULU.W Rm, Rn (0010nnnn mmmm1110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	MACL = ((unsigned) (R(n) & 0xFFFF) * (unsigned) (R(m) & 0xFFFF));

	PC += 2;

#ifdef DEBUG_ARITH_DMUL
    logmsg("muluw66: r[%d]=%x,%d, r[%d]=%x,%d, MACL=%x,%d\r\n",
        n, R(n), R(n),
        m, R(m), R(m),
        MACL, MACL);
#endif
}
