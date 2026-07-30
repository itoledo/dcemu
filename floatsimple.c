#include "main.h"
#include "excepciones.h"
#include "floatsimple.h"
#include "floatgraph.h"
#include <math.h>
#include <float.h>
#include <SIMDx86/math.h>


double regN;
double regM;


DC_INLINE void extract_double(double *  i,short idx)
{
	// damos vuelta los bytes
	DWORD * p =  (DWORD *) i;
	
	*(p++) =  DR_INT(idx) [1];

	*p = DR_INT(idx) [0];
}

DC_INLINE void put_double(short idx, double * src)
{
	// damos vuelta los bytes
	DWORD * d = (DWORD *) src;

	 DR_INT(idx)[1] = *(d++);
	 DR_INT(idx)[0] = *d;
}


/* --------------------------------------------- excepciones de la FPU ------
   El SH-4 rearma el campo Cause de FPSCR en cada operacion de la FPU y acumula
   las mismas causas en el campo Flag, que solo se limpia escribiendo FPSCR.
   Aqui se calculan las causas a partir de los operandos y del resultado, sin
   tocar el entorno de punto flotante del anfitrion: <fenv.h> obligaria a
   recargar MXCSR antes de cada instruccion emulada, que es lo mas caro que se
   puede hacer en un interprete.

   Que se detecta y que no esta en docs/sh4-conformidad.md. En resumen: V, Z, O
   y U salen bien; I solo aparece acompanando a O y a U, y un NaN de senal que
   entra no levanta V. */

/* Cause se reemplaza, Flag se acumula. No pasa por UpdateFPSCR() porque ninguno
   de los dos campos toca PR, SZ ni FR, que es lo unico que obliga a repuntar la
   tabla de opcodes o a cambiar de banco. */
static DC_INLINE void fpu_causa(DWORD causas)
{
	/* Si la causa coincide con su bit de Enable, el SH-4 no escribe el registro
	   destino ni el campo Flag: entra a la excepcion y la instruccion queda
	   para reejecutar. Salir por excepcion_abortar() hace que main_loop()
	   restaure la instantanea, que es exactamente eso; Cause lo repone despues
	   excepcion_reponer(), porque la instantanea tambien se lo lleva. */
	if (causas & FPU_ENABLE_A_CAUSA(FPSCR))
	{
		excepcion_fpu_cause = causas;
		excepcion_abortar(EXC_FPU_OPERACION, EXC_VEC_GENERAL);
		return;			/* solo se llega aca sin salto armado */
	}

	FPSCR = (FPSCR & ~FPU_CAUSA_TODAS) | causas | FPU_CAUSA_A_FLAG(causas);
}

/* isnan() e isinf() son de C99 y Makefile.win apunta al gcc de Dev-C++, que es
   de 2004: mejor no depender de ellas. La comparacion consigo mismo es la forma
   canonica de reconocer un NaN, y HUGE_VAL es el infinito de <math.h>. */
static DC_INLINE int fpu_es_nan(double x)
{
	return x != x;
}

static DC_INLINE int fpu_es_inf(double x)
{
	return x >= HUGE_VAL || x <= -HUGE_VAL;
}

/* Causas de una operacion aritmetica, mirando entradas y resultado.

   - Invalida: el resultado salio NaN sin que ninguna entrada lo fuera. Eso
     cubre inf-inf, 0*inf, 0/0, inf/inf y la raiz de un negativo. Un NaN que
     entra y sale se propaga sin senalar nada, como corresponde a un NaN
     silencioso.
   - Desbordamiento: el resultado es infinito y ninguna entrada lo era.
   - Subdesbordamiento: el resultado quedo por debajo del menor normal del
     formato -- `minimo` es FLT_MIN o DBL_MIN -- con las dos entradas no nulas.
     `subdesborda` esta en cero para la suma y la resta: cuando su resultado cae
     bajo el minimo normal es exacto, y sin inexactitud no hay
     subdesbordamiento.

   O y U arrastran I porque un resultado que se sale del rango es siempre
   inexacto. Para las operaciones de una sola entrada se pasa el mismo valor en
   `a` y en `b`. */
static DWORD fpu_causas(double a, double b, double r, double minimo,
						int subdesborda)
{
	if (fpu_es_nan(r))
		return (fpu_es_nan(a) || fpu_es_nan(b)) ? 0 : FPU_CAUSA_V;

	if (fpu_es_inf(r) && !fpu_es_inf(a) && !fpu_es_inf(b))
		return FPU_CAUSA_O | FPU_CAUSA_I;

	if (subdesborda && a != 0.0 && b != 0.0 && fabs(r) < minimo)
		return FPU_CAUSA_U | FPU_CAUSA_I;

	return 0;
}

/* La division agrega su causa propia: divisor cero con un dividendo finito
   distinto de cero. 0/0 e inf/0 no son division por cero -- el primero es
   invalida y el segundo no es nada -- asi que los dos quedan afuera. */
static DWORD fpu_causas_division(double a, double b, double r, double minimo)
{
	if (b == 0.0 && a != 0.0 && !fpu_es_nan(a) && !fpu_es_inf(a))
		return FPU_CAUSA_Z;

	return fpu_causas(a, b, r, minimo, 1);
}

/* FCMP/EQ y FCMP/GT: el manual del SH-4 las da por invalidas con cualquier NaN
   de entrada, sin distinguir si es de senal o silencioso. */
static DC_INLINE DWORD fpu_causas_comparacion(double a, double b)
{
	return (fpu_es_nan(a) || fpu_es_nan(b)) ? FPU_CAUSA_V : 0;
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

	core.context.cycles += 2;

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

	core.context.cycles += 1;

#ifdef DEBUG_FLOAT_SIMPLE
	logmsg("fmovs174: r[%d]=%x, FR[%d]=%x\r\n", m, R(m), n, float_to_dword(FR(n)));
#endif
}

OPCODE(fmovs175) // FMOV.S @Rm+, FRn (1111nnnn mmmm1001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	
	memread(R(m), &FR(n), sizeof(float));

	R(m) += 4;
	
	PC += 2;

	core.context.cycles += 2;

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

	core.context.cycles += 1;

		#ifdef DEBUG_FLOAT_SIMPLE
		logmsg("fmovs176: FR[%d]=%x, r[%d]=%x\r\n",m, float_to_dword(FR(m)), n, R(n));
		#endif
}

OPCODE(fmovs177) // FMOV.S FRm, @-Rn (1111nnnn mmmm1011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) -= 4;

	WriteMemoryF(R(n), &FR(m));

	PC += 2;

	core.context.cycles += 1;

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

	core.context.cycles += 1;

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

	core.context.cycles += 2;
// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("fmov180: r[%d]=%x, n=%d\r\n", m, R(m), n);
// #endif
}

OPCODE(fmov182) // FMOV @Rm+, DRn (1111nnn0 mmmm1001)
{
   	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;

	memread(R(m), &DR(n), sizeof(DWORD)*2);

  	  R(m) += 8;

	PC += 2;

	core.context.cycles += 2;

// #ifdef DEBUG_FLOAT_GRAPH
// 	logmsg("fmov182: r[%d]=%x, n=%d\r\n", m, R(m), n);
// #endif
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

	memwrite(R(n), &DR(m), sizeof(DWORD) * 2);	// el par entero: 8 bytes

	PC += 2;

	core.context.cycles += 1;

// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("fmov184: m=%d, r[%d]=%x\r\n", m, n, R(n));
// #endif
}

OPCODE(fmov185) // FMOV DRm, @(R0, Rn) (1111nnnn mmm00111)
{
   	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;
	DWORD addr = R(0) + R(n);
		
//	doublewrite(addr, DR_index(m));
	memwrite(addr, &DR(m), sizeof(DWORD)*2);

	PC += 2;

	core.context.cycles += 2;

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

// #ifdef DEBUG_FLOAT_SIMPLE
// // 	logmsg("flds186: FR%d=%f (no conversion) FPUL=%x\r\n", m, FR(m), FPUL);
// #endif
}

OPCODE(fsts187) // FSTS FPUL, FRn (1111nnnn 00001101)
{
	short n = (arg >> 8) & 0x0F;

//	FR(n) = FPUL;
	memcpy(&FR(n), &FPUL, sizeof(float));

	PC += 2;

// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("fsts187: FPUL=%x (no conversion) FR%d=%x,%f\r\n", FPUL, n, FR(n),FR(n));
// #endif
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

	{
		float a = FR(n), b = FR(m);

		FR(n) = a + b;

		fpu_causa(fpu_causas(a, b, FR(n), FLT_MIN, 0));
	}

	PC += 2;

	core.context.cycles += 4;

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

	fpu_causa(fpu_causas_comparacion(FR(m), FR(n)));

	PC += 2;

	core.context.cycles += 2;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fcmpeq190: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
// #endif
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

	fpu_causa(fpu_causas_comparacion(FR(m), FR(n)));

	PC += 2;

	core.context.cycles += 2;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fcmpgt191: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
// #endif
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
	
	/* Dividir por cero no es un caso especial: con la excepcion de division
	   por cero deshabilitada -- que es como arranca FPSCR -- IEEE 754 define
	   x/0 como infinito con signo y 0/0 como NaN, que es justo lo que entrega
	   el host. Lo que si hay que dejar anotado es la causa. */
	{
		float a = FR(n), b = FR(m);

		FR(n) = a / b;

		fpu_causa(fpu_causas_division(a, b, FR(n), FLT_MIN));
	}

	PC += 2;

	core.context.cycles += 12;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fdiv192: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
// #endif
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

	/* Un entero de 32 bits siempre cabe en el rango de un float. Lo unico que
	   puede perder es precision, o sea la causa I, que no se detecta. */
	fpu_causa(0);

	PC += 2;

	core.context.cycles += 3;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("float193: FPUL=%x (CONVERSION FROM SIGNED LONG TO FLOAT) FR(%d)=%f,%x\r\n", FPUL, n, FR(n), float_to_dword(FR(n)));
// #endif
}

OPCODE(fmac194) // FMAC FR0, FRm, FRn (1111nnnn mmmm1110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	/* Son dos operaciones y cada una tiene sus causas: el producto puede
	   desbordar o subdesbordar por su cuenta antes de que la suma lo vea. */
	{
		float a = FR(0), b = FR(m), c = FR(n);
		float p = a * b;
		DWORD causas = fpu_causas(a, b, p, FLT_MIN, 1);

		FR(n) = c + p;

		fpu_causa(causas | fpu_causas(c, p, FR(n), FLT_MIN, 0));
	}

	PC += 2;

	core.context.cycles += 3;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fmac194: FR(%d)=%x, FR(%d)=%x\r\n", m, float_to_dword(FR(m)), n, float_to_dword(FR(n)));
// #endif
}

OPCODE(fmul195) // FMUL FRn * FRm -> FRn (1111nnnn mmmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	{
		float a = FR(n), b = FR(m);

		FR(n) = a * b;

		fpu_causa(fpu_causas(a, b, FR(n), FLT_MIN, 1));
	}

	PC += 2;

	core.context.cycles += 3;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fmul195: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
// #endif
}

OPCODE(fneg196) // FNEG FRn (1111nnnn 01001101)
{
	short n = (arg >> 8) & 0x0F;
	DWORD bits;

	/* El manual define FNEG como "invertir el bit de signo", no como una
	   multiplicacion: asi tambien queda bien con ceros y NaN. */
	memcpy(&bits, &FR(n), sizeof(bits));
	bits ^= 0x80000000;
	memcpy(&FR(n), &bits, sizeof(bits));

	PC += 2;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fneg196: FR(%d)=%f, FR(%d)=%f\r\n", n, FR(n));
// #endif
}

OPCODE(fsqrt197) // FSQRT FRn (1111nnnn 01101101)
{
	short n = (arg >> 8) & 0x0F;
	float a = FR(n);

	#ifndef X86_OPT
	FR(n) = sqrt(FR(n));
	#else
 	FR(n) = SIMDx86_sqrtf(FR(n));
 	#endif

	/* La raiz de un negativo es la unica causa posible: no desborda ni
	   subdesborda desde ningun origen representable. */
	fpu_causa(fpu_causas(a, a, FR(n), FLT_MIN, 0));

	PC += 2;

	core.context.cycles += 12;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fsqrt197: FR(%d)=%f, FR(%d)=%f\r\n", n,f,n,FR(n));
// #endif
}

OPCODE(fsub198) // FSUB FRm, FRn (1111nnnn mmmm0001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	{
		float a = FR(n), b = FR(m);

		FR(n) = a - b;

		fpu_causa(fpu_causas(a, b, FR(n), FLT_MIN, 0));
	}

	PC += 2;

	core.context.cycles += 3;

// #ifdef DEBUG_FLOAT_SIMPLE
//     logmsg("fsub198: FR(%d)=%f, FR(%d)=%f\r\n", m, FR(m), n, FR(n));
// #endif
}

OPCODE(ftrc199) // FTRC FRm, FPUL : (long) FRm -> FPUL (1111mmmm00111101)
{
	short m = (arg >> 8) & 0x0F;
	float f = FR(m);

	/* Trunca hacia cero y satura: fuera de rango el manual pide el maximo o el
	   minimo entero con signo, y NaN da el minimo. El limite inferior se
	   compara con "<" porque -2147483649 no es representable en float y
	   redondearia justo al valor valido. */
	if (f != f)								// NaN
	{
		FPUL = 0x80000000;
		fpu_causa(FPU_CAUSA_V);				// NaN y fuera de rango son las dos
	}										// causas invalidas de FTRC
	else
	if (f >= 2147483648.0f)					// incluye +infinito
	{
		FPUL = 0x7FFFFFFF;
		fpu_causa(FPU_CAUSA_V);
	}
	else
	if (f < -2147483648.0f)					// incluye -infinito
	{
		FPUL = 0x80000000;
		fpu_causa(FPU_CAUSA_V);
	}
	else
	{
		FPUL = (DWORD) (signed long) f;		// el cast de C trunca hacia cero
		fpu_causa(0);
	}

	PC += 2;

	core.context.cycles += 3;

// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("ftrc199: FR%d=%f,%x (CONVERSION FROM FLOAT TO SIGNED LONG) FPUL=%d,%x\r\n", m, FR(m), float_to_dword(FR(m)), FPUL, FPUL);
// #endif
}


OPCODE(fabs188) // FABS FRn (1111nnnn 01011101)
{
	short n = (arg >> 8) & 0x0F;
	DWORD bits;

	/* Como FNEG: es una operacion sobre el bit de signo, no aritmetica. */
	memcpy(&bits, &FR(n), sizeof(bits));
	bits &= 0x7FFFFFFF;
	memcpy(&FR(n), &bits, sizeof(bits));

	PC += 2;
}

OPCODE(fmov181) // FMOV @(R0, Rm), DRn (1111nnn0 mmmm0110)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 4) & 0x0F;

	memread(R(0) + R(m), &DR(n), sizeof(DWORD) * 2);

	PC += 2;

	core.context.cycles += 2;
}

OPCODE(fmov183) // FMOV DRm, @Rn (1111nnnn mmm01010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 5) & 0x07;

	memwrite(R(n), &DR(m), sizeof(DWORD) * 2);

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(fabs200) // FABS DRn (1111nnn0 01011101)
{
	short n = (arg >> 9) & 0x07;

	/* La parte alta del par lleva el signo y el exponente. */
	DR_INT(n)[0] &= 0x7FFFFFFF;

	PC += 2;
}

OPCODE(fneg209) // FNEG DRn (1111nnn0 01001101)
{
	short n = (arg >> 9) & 0x07;

	DR_INT(n)[0] ^= 0x80000000;

	PC += 2;
}

OPCODE(fcmpeq202) // FCMP/EQ DRm, DRn (1111nnn0 mmm00100)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	extract_double(&regM, DR_index(m));
	extract_double(&regN, DR_index(n));

	if (regN == regM)
		SR_T = 1;
	else
		SR_T = 0;

	fpu_causa(fpu_causas_comparacion(regM, regN));

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(fmul208) // FMUL DRm, DRn : DRn * DRm -> DRn (1111nnn0 mmm00010)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	extract_double(&regN, DR_index(m));
	extract_double(&regM, DR_index(n));

	{
		double a = regM, b = regN;

		regM = a * b;

		fpu_causa(fpu_causas(a, b, regM, DBL_MIN, 1));
	}

	put_double(DR_index(n), &regM);

	PC += 2;

	core.context.cycles += 7;
}

OPCODE(fsub211) // FSUB DRm, DRn : DRn - DRm -> DRn (1111nnn0 mmm00001)
{
	short n = (arg >> 9) & 0x07;
	short m = (arg >> 5) & 0x07;

	extract_double(&regN, DR_index(m));
	extract_double(&regM, DR_index(n));

	{
		double a = regM, b = regN;

		regM = a - b;

		fpu_causa(fpu_causas(a, b, regM, DBL_MIN, 0));
	}

	put_double(DR_index(n), &regM);

	PC += 2;

	core.context.cycles += 7;
}

OPCODE(fcnvds205) // FCNVDS DRm, FPUL : (float) DRm -> FPUL (1111mmm0 10111101)
{
	short m = (arg >> 9) & 0x07;
	float f;

	extract_double(&regN, DR_index(m));

	f = (float) regN;

	/* Bajar de doble a simple es la unica conversion que puede desbordar y
	   subdesbordar: el rango de destino es mucho mas chico que el de origen. */
	fpu_causa(fpu_causas(regN, regN, f, FLT_MIN, 1));

	memcpy(&FPUL, &f, sizeof(float));

	PC += 2;

	core.context.cycles += 4;
}

OPCODE(fcnvsd206) // FCNVSD FPUL, DRn : (double) FPUL -> DRn (1111nnn0 10101101)
{
	short n = (arg >> 9) & 0x07;
	float f;

	memcpy(&f, &FPUL, sizeof(float));

	regN = (double) f;

	/* Subir de simple a doble entra siempre exacto y dentro de rango. */
	fpu_causa(0);

	put_double(DR_index(n), &regN);

	PC += 2;

	core.context.cycles += 4;
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


	{
		double a = regM, b = regN;

		regM = a + b;

		fpu_causa(fpu_causas(a, b, regM, DBL_MIN, 0));
	}

	put_double(DR_index(n), &regM);

//    memcpy(&DR(n), &y, sizeof(float)*2);

	PC += 2;

	core.context.cycles += 7;

// #ifdef DEBUG_FLOAT_SIMPLE
// 		logmsg("fadd201: DR%d, DR%d total=%f Input X - %f , Y - %f.Before X - %f, Y - %f \r\n", m, n, y,x,i,bx,by);
// 	dump_registers();
// #endif
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

	fpu_causa(fpu_causas_comparacion(regM, regN));

	PC += 2;

	core.context.cycles += 3;

// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("fcmpgt203: DR%d > DR%d ?\r\n", m, n);
// 	dump_registers();
// #endif
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
 	logmsg("fdiv204: x=%f, y=%f\r\n", regN, regM);
#endif

	//	print_double(x);
//	print_double(y);
 
	{
		double a = regM, b = regN;

		regM = a / b;

		fpu_causa(fpu_causas_division(a, b, regM, DBL_MIN));
	}

//    memcpy(&DR(n), &y, sizeof(float)*2);
	put_double(DR_index(n), &regM);
	
	PC += 2;

			#ifdef DEBUG_FLOAT_SIMPLE
			logmsg("fdiv204: DR%d, DR%d, total=%f\r\n", m, n, regN);
			#endif	

	core.context.cycles += 24;
}

OPCODE(float207) // FLOAT FPUL, DRn (1111nnn0 00101101)
{
	short n = (arg >> 9) & 0x07;
// 	double x;
	signed long z = FPUL;
	
/*	if (FPUL & 0x80000000)
//		z = (FPUL & 0x7FFFFFFF) * -1;
		z = (~FPUL + 1) * -1;
	else */
		z = FPUL;

	regN = (double) z;

	/* Un entero de 32 bits entra exacto en un doble: no hay causa posible. */
	fpu_causa(0);

//	print_double(x);

//	memcpy(&DR(n), &x, sizeof(float)*2);
	put_double(DR_index(n), &regN);
//	print_double(*(double *) &DR(n));

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_FLOAT_SIMPLE
			logmsg("float207: FPUL=%x,%d (CONVERSION FROM SIGNED INT TO DOUBLE PREC FP) DR%d\r\n", FPUL, FPUL, n);
		//	dump_registers();
		#endif
}

OPCODE(fsqrt210) // FSQRT DRn (1111nnn0 00111101)
{
	short n = (arg >> 9) & 0x07;
	double a;

	extract_double(&regN, DR_index(n));
	a = regN;							// el origen, para clasificar despues
 	#ifndef X86_OPT
	regN = sqrt(regN);
	put_double(DR_index(n), &regN);
	#else
 	regN = SIMDx86_sqrt(regN);
	put_double(DR_index(n), &regN);
	#endif

	/* Como fsqrt197: solo la raiz de un negativo levanta algo. */
	fpu_causa(fpu_causas(a, a, regN, DBL_MIN, 0));

	PC += 2;

	core.context.cycles += 23;

// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("fsqrt210\n");
// #endif
}

OPCODE(ftrc212) // FTRC DRm, FPUL (1111mmm0 00011101)
{
	short m = (arg >> 9) & 0x07;
// 	double x;
	//signed long y;
	
	extract_double(&regN, DR_index(m));

	/* Igual que ftrc199: trunca hacia cero (no floor, que redondearia mal los
	   negativos) y satura fuera de rango. */
	if (regN != regN)						// NaN
	{
		FPUL = 0x80000000;
		fpu_causa(FPU_CAUSA_V);				// igual que ftrc199
	}
	else
	if (regN >= 2147483648.0)				// incluye +infinito
	{
		FPUL = 0x7FFFFFFF;
		fpu_causa(FPU_CAUSA_V);
	}
	else
	if (regN <= -2147483649.0)				// incluye -infinito
	{
		FPUL = 0x80000000;
		fpu_causa(FPU_CAUSA_V);
	}
	else
	{
		FPUL = (DWORD) (signed long) regN;	// el cast de C trunca hacia cero
		fpu_causa(0);
	}

	PC += 2;

	core.context.cycles += 4;

// #ifdef DEBUG_FLOAT_SIMPLE
// 	logmsg("ftrc212: (CONVERSION FROM DOUBLE FLOAT TO SIGNED LONG) FPUL=%x,%d\r\n", (DWORD) FPUL, (signed long) FPUL);
// #endif
}
