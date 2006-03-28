#include "main.h"
#include "floatsimple.h"
#include "floatgraph.h"
#include <math.h>
#include <SIMDx86/math.h>


double regN;
double regM;

__inline__ void extract_double(double *  i,short idx)
{
	// damos vuelta los bytes
	DWORD * p =  (DWORD *) i;
	
	*(p++) =  DR_INT(idx) [1];

	*p = DR_INT(idx) [0];
}

__inline__ void put_double(short idx, double * src)
{
	// damos vuelta los bytes
	DWORD * d = (DWORD *) src;

	 DR_INT(idx)[1] = *(d++);
	 DR_INT(idx)[0] = *d;
}


OPCODE(fldi0170) // FLDI0 FRn (1111nnnn 10001101)
{
	short n = (arg >> 8) & 0x0F;

	FR(n) = (float) 0.0;

	PC += 2;
}

OPCODE(fldi1171) // FLDI1 FRn (1111nnnn 10011101)
{
	short n = (arg >> 8) & 0x0F;

	FR(n) = (float) 1.0;

	PC += 2;
}

OPCODE(fmov172) // FMOV FRm, FRn : FRm -> FRn (1111nnnn mmmm1100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

//	FR(n) = FR(m);
	memcpy(&FR(n), &FR(m), sizeof(float));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs172: m:%d, n:%d, FR[m]=%x, FR[n]=%f\r\n", m, n, FR(m), FR(n));
#endif
}

OPCODE(fmovs173) // FMOV.S @Rm, FRn : (Rm) -> FRn (1111nnnn mmmm1000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	
	memread(R(m), &FR(n), sizeof(DWORD));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs173: r[%d]=%x, FR(%d)=%x\r\n", m, R(m), n, float_to_dword(FR(n)));
#endif
}

OPCODE(fmovs174) // FMOV.S @(R0, Rm), FRn
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	
	memread(R(m) + R(0), &FR(n), sizeof(float));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs174: r[%d]=%x, FR[%d]=%x\r\n", m, R(m), n, float_to_dword(FR(n)));
#endif
}

OPCODE(fmovs175) // FMOV.S @Rm+, FRn (1111nnnn mmmm1001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
/*	float f;

	ReadMemoryL(R(m), &f);
	
	FR(n) = (float) f; */
	
	memread(R(m), &FR(n), sizeof(float));

	R(m) += 4;
	
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs175: r[%d]=%x, FR[%d]=%x\r\n", m, R(m), n, float_to_dword(FR(n)));
#endif
}

OPCODE(fmovs176) // FMOV.S FRm, @Rn : FRm -> (Rn) (1111nnnn mmmm1010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	WriteMemoryF(R(n), &FR(m));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs176: FR[%d]=%x, r[%d]=%x\r\n", m, float_to_dword(FR(m)), n, R(n));
#endif
}

OPCODE(fmovs177) // FMOV.S FRm, @-Rn (1111nnnn mmmm1011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) -= 4;

	WriteMemoryF(R(n), &FR(m));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs177: FR[%d]=%x, R[%d]=%x\r\n", m, float_to_dword(FR(m)), n, R(n));
#endif
}

OPCODE(fmovs178) // FMOV.S FRm, @(R0, Rn) (1111nnnn mmmm0111)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	WriteMemoryF(R(0) + R(n), &FR(m));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs178: FR[%d]=%x, R[0]+R[%d]=%x\r\n", m, float_to_dword(FR(m)), n, R(0) + R(n));
#endif
}

OPCODE(fmov179) // FMOV DRm, DRn (1111nnn0 mmm01100)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	memcpy(&DR(n), &DR(m), sizeof(double));

	PC += 2;
}

OPCODE(fmov180) // FMOV @Rm, DRn (1111nnn0 mmmm1000)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;

	memread(R(m), &DR(n), sizeof(DWORD) * 2);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmov180: r[%d]=%x, n=%d\r\n", m, R(m), n);
#endif
}

OPCODE(fmov182) // FMOV @Rm+, DRn (1111nnn0 mmmm1001)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;

	memread(R(m), &DR(n), sizeof(DWORD)*2);

  	  R(m) += 8;

	PC += 2;

#ifdef DEBUG_FLOAT_GRAPH
	logmsg("fmov182: r[%d]=%x, n=%d\r\n", m, R(m), n);
#endif
}

OPCODE(fmov184) // FMOV DRm, @-Rn (1111nnnn mmm01011)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;
/*	short offset;
	
    if (IS_SET(FPSCR, FPSCR_FR))
        offset = 16;
    else
        offset = 0;

    R(n) -= 8;
    
    memwrite(R(n), &float_registers[m*2 + offset], sizeof(float) * 2); */

	R(n) -= 8;

//	doublewrite(R(n), DR_index(m));
	memwrite(R(n), &DR(m), sizeof(DWORD));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmov184: m=%d, r[%d]=%x\r\n", m, n, R(n));
#endif
}

OPCODE(fmov185) // FMOV DRm, @(R0, Rn) (1111nnnn mmm00111)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;
	DWORD addr = R(0) + R(n);
		
//	doublewrite(addr, DR_index(m));
	memwrite(addr, &DR(m), sizeof(DWORD)*2);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmov185: DR%d -> (R0 + R%d = %x)\r\n", m, n, addr);
#endif
}

OPCODE(flds186) // FLDS FRm, FPUL (1111mmmm 00011101)
{
	short m = (arg >> 8) & 0x0F;

//	FPUL = FR(m);
	memcpy(&FPUL, &FR(m), sizeof(float));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("flds186: FR%d=%f (no conversion) FPUL=%x\r\n", m, FR(m), FPUL);
#endif
}

OPCODE(fsts187) // FSTS FPUL, FRn (1111nnnn 00001101)
{
	short n = (arg >> 8) & 0x0F;

//	FR(n) = FPUL;
	memcpy(&FR(n), &FPUL, sizeof(float));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fsts187: FPUL=%x (no conversion) FR%d=%x,%f\r\n", FPUL, n, FR(n),FR(n));
#endif
}

OPCODE(fadd189) // FADD FRm, FRn : FRn + FRm -> FRn (1111nnnn mmmm0000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

/*	if (IS_SET(FPSCR, FPSCR_PR))
	{
		if ((arg & 0x0100) == 0)
			fadd201(arg);
		else
			logmsg("ERROR: fadd arg");
		return;
	} */

	FR(n) += FR(m);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fadd: FR[%d]=%x, FR[%d]=%x\r\n", m, float_to_dword(FR(m)), n, float_to_dword(FR(n)));
#endif
}

OPCODE(fcmpeq190) // FCMP/EQ FRm, FRn (1111nnnn mmmm0100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	if (FR(m) == FR(n))
        SET_T
    else
        UNSET_T
        
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fcmpeq190: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
#endif
}

OPCODE(fcmpgt191) // FCMP/GT FRm, FRn (1111nnnn mmmm0101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

/*	if (IS_SET(FPSCR, FPSCR_PR))
	{
		if ((arg & 0x0110) == 0)
			fcmpgt203(arg);
		else
			logmsg("ERROR: fcmpgt191 arg\r\n");
		return;
	} */

	if (FR(n) > FR(m))
        SET_T
    else
        UNSET_T

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fcmpgt191: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
#endif
}

OPCODE(fdiv192) // FDIV FRm, FRn : FRn/FRm -> FRn (1111nnnn mmmm0011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

/*	if (IS_SET(FPSCR, FPSCR_PR))
	{
		if ((arg & 0x0110) == 0)
			fdiv204(arg);
		else
			logmsg("ERROR: fdiv arg\r\n");
		return;
	} */
	
	if (FR(m) == 0)
	{
		logmsg("EXCEPTION: float_R(m) = 0, n:%d, m:%d\r\n", n, m);
	}
	else
	{
#ifdef ASM_DEBUG
		fprintf(logfp, "fdiv192: n:%d, m:%d, fl_reg[n]=%f, fl_reg[m]=%f\r\n",
			n, m, FR(n), FR(m));
#endif
		FR(n) /= FR(m);
#ifdef ASM_DEBUG
		fprintf(logfp, "fdiv192: res=%f\r\n", FR(n));
#endif
	}

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fdiv192: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
#endif
}

OPCODE(float193) // FLOAT FPUL, FRn : (float) FPUL -> FRn (1111nnnn 00101101)
{
	short n = (arg >> 8) & 0x0F;
	signed long l = (signed long) FPUL;
/*	signed long l;
	float f; */

/*	if (IS_SET(FPSCR, FPSCR_PR))
	{
		float207(arg);
		return;
	} */

/*	COPY_REG(l, FPUL);
	f = (float) l; */

	FR(n) = (float) l;

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("float193: FPUL=%x (CONVERSION FROM SIGNED LONG TO FLOAT) FR(%d)=%f,%x\r\n", FPUL, n, FR(n), float_to_dword(FR(n)));
#endif
}

OPCODE(fmac194) // FMAC FR0, FRm, FRn (1111nnnn mmmm1110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	FR(n) += FR(0) * FR(m);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fmac194: FR(%d)=%x, FR(%d)=%x\r\n", m, float_to_dword(FR(m)), n, float_to_dword(FR(n)));
#endif
}

OPCODE(fmul195) // FMUL FRn * FRm -> FRn (1111nnnn mmmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	FR(n) *= FR(m);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fmul195: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
#endif
}

OPCODE(fneg196) // FNEG FRn (1111nnnn 01001101)
{
	short n = (arg >> 8) & 0x0F;

	FR(n) *= -1;

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fneg196: FR(%d)=%f, FR(%d)=%f\r\n", n, FR(n));
#endif
}

OPCODE(fsqrt197) // FSQRT FRn (1111nnnn 01101101)
{
	short n = (arg >> 8) & 0x0F;
	#ifndef X86_OPT
	FR(n) = sqrt(FR(n));
	#else
 	FR(n) = SIMDx86_sqrtf(FR(n));
 	#endif
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fsqrt197: FR(%d)=%f, FR(%d)=%f\r\n", n,f,n,FR(n));
#endif
}

OPCODE(fsub198) // FSUB FRm, FRn (1111nnnn mmmm0001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	FR(n) -= FR(m);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
    logmsg("fsub198: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
#endif
}

OPCODE(ftrc199) // FTRC FRm, FPUL : (long) FRm -> FPUL (1111mmmm00111101)
{
	short m = (arg >> 8) & 0x0F;
	signed long z;

/*	if (IS_SET(FPSCR, FPSCR_PR))
	{
		if ((arg & 0x0100) == 0)
			ftrc212(arg);
		else
			logmsg("ERROR: ftrc arg\r\n");
		return;
	} */

	/* if (float_R(m) - floorf(float_R(m)) > 0.5)
		FPUL = ceilf(float_R(m));
	else
		FPUL = floorf(float_R(m)); */

//	FPUL = (float) floor(FR(m));

	z = (signed long) FR(m);
	FPUL = z;

/*	logmsg("ftrc199: FPUL=%x,%d FR[%d]=%x,%f\r\n",
		(DWORD) FPUL, (signed long) FPUL,
		m, (DWORD) R(m), (float) R(m)); */

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("ftrc199: FR%d=%f,%x (CONVERSION FROM FLOAT TO SIGNED LONG) FPUL=%d,%x\r\n", m, FR(m), float_to_dword(FR(m)), FPUL, FPUL);
#endif
}


OPCODE(fadd201) // FADD DRm, DRn (1111nnn0 mmm00000)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;
// 	double x, y;

//	memcpy(&x, &DR(m), sizeof(double));
/*	memcpy(&x, &float_registers[DR_index(m)], sizeof(double)); */

	extract_double(&regN, DR_index(m));
	extract_double(&regM, DR_index(n));
//	memcpy(&y, &DR(n), sizeof(double));


	regM = regM + regN;

	put_double(DR_index(n), &regM);

//    memcpy(&DR(n), &y, sizeof(float)*2);

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
		logmsg("fadd201: DR%d, DR%d total=%f Input X - %f , Y - %f.Before X - %f, Y - %f \r\n", m, n, y,x,i,bx,by);
	dump_registers();
#endif
}

OPCODE(fcmpgt203) // FCMP/GT DRm, DRn (1111nnn0 mmm00101)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;
// 	double drm, drn;

	extract_double(&regM, DR_index(m));
	extract_double(&regN, DR_index(n));

	if (regN > regM)
		SR_T=1;
	else
		SR_T=0;
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fcmpgt203: DR%d > DR%d ?\r\n", m, n);
	dump_registers();
#endif
}

OPCODE(fdiv204) // FDIV DRm, DRn (1111nnn0 mmm00011)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;
// 	double x, y;
	
/*	memcpy(&x, &DR(m), sizeof(float)*2);
	memcpy(&y, &DR(n), sizeof(float)*2); */
	extract_double(&regN, DR_index(m));
	extract_double(&regM, DR_index(n));

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fdiv204: x=%f, y=%f\r\n", x, y);
#endif

	//	print_double(x);
//	print_double(y);
 
  	regM  /= regN;
   
//    memcpy(&DR(n), &y, sizeof(float)*2);
	put_double(DR_index(n), &regM);
	
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fdiv204: DR%d, DR%d, total=%f\r\n", m, n, y);
#endif
}

OPCODE(float207) // FLOAT FPUL, DRn (1111nnn0 00101101)
{
	int n = (arg >> 9) & 0x07;
// 	double x;
	signed long z = FPUL;
	
/*	if (FPUL & 0x80000000)
//		z = (FPUL & 0x7FFFFFFF) * -1;
		z = (~FPUL + 1) * -1;
	else */
		z = FPUL;

	regN = (double) z;

//	print_double(x);

//	memcpy(&DR(n), &x, sizeof(float)*2);
	put_double(DR_index(n), &regN);
//	print_double(*(double *) &DR(n));

	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("float207: FPUL=%x,%d (CONVERSION FROM SIGNED INT TO DOUBLE PREC FP) DR%d\r\n", FPUL, FPUL, n);
//	dump_registers();
#endif
}

OPCODE(fsqrt210) // FSQRT DRn (1111nnn0 00111101)
{
	short n = (arg >> 9) & 0x07;
// 	double x;
	extract_double(&regN, DR_index(n));
 	#ifndef X86_OPT
	x = sqrt(x);
	#else
 	regN = SIMDx86_sqrt(regN);
	put_double(DR_index(n), &regN);
	#endif
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fsqrt210\n");
#endif
}

OPCODE(ftrc212) // FTRC DRm, FPUL (1111mmm0 00011101)
{
	short m = (arg >> 9) & 0x07;
// 	double x;
	//signed long y;
	
//	memcpy(&x, &DR(m), sizeof(float)*2);
	extract_double(&regN, DR_index(m));

//	print_double(DR(m));
//	print_double(x);

//	y = (signed long) x;
	
/*	if ((x - floor(x)) >= .5)
		y = (signed long) ceil(x);
	else */
//	y = (signed long) floor(x);
 	FPUL = (signed long) floor(regN); // y;
	PC += 2;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("ftrc212: (CONVERSION FROM DOUBLE FLOAT TO SIGNED LONG) FPUL=%x,%d\r\n", (DWORD) FPUL, (signed long) FPUL);
#endif
}


OPCODE(fabs258)
{
	short n = (arg >> 8) & 0x0F;
	FR(n) = fabsf(FR(n));
	PC+=2;
}

// we don't need to retrieve the double in this case just fabsf the n+1 float
OPCODE(fabs244)
{
	short n = (arg >> 9) & 0x07;
	FR(n+1) = fabsf(FR(n+1));
	PC+=2;
}

OPCODE(fneg254)
{
	short n = (arg >> 9) & 0x07;
	FR(n+1) = -FR(n+1);
	PC+=2;
}