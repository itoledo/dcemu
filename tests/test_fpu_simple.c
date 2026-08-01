/****************************************************************************

	Pruebas de floatsimple.c -- punto flotante de precision simple y doble

	El mismo patron de bits de instruccion significa cosas distintas segun los
	bits PR y SZ de FPSCR, porque initopcodes() arma cuatro tablas de despacho.
	Los casos de doble precision cambian FPSCR antes de ejecutar.

	Casos borde que cubre esta suite:

	  - FLDI0 y FLDI1 son las dos unicas constantes inmediatas de la FPU.
	  - FCMP/EQ con -0.0 y +0.0: en IEEE 754 son iguales aunque los patrones de
		bits difieran.
	  - FCMP/GT contra NaN: toda comparacion con NaN es falsa.
	  - FTRC trunca hacia cero, no hacia abajo: -1.5 da -1, no -2.
	  - FDIV entre cero: sin excepciones habilitadas el SH-4 entrega infinito.
	  - Con SZ=1 los FMOV mueven pares de 8 bytes; con SZ=0, floats de 4.
	  - Los valores de doble precision se arman y se leen con FLOAT y FTRC en
		vez de escribirlos a mano, porque floatsimple.c guarda las dos mitades
		del double invertidas respecto del host.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "suites.h"

#define BIT_SZ	0x00100000u
#define BIT_PR	0x00080000u

static void modo_sz1(void)
{
	UpdateFPSCR((FPSCR & ~BIT_PR) | BIT_SZ);
}

static void modo_pr1(void)
{
	UpdateFPSCR((FPSCR & ~BIT_SZ) | BIT_PR);
}

static float bits_a_float(DWORD bits)
{
	float f;

	memcpy(&f, &bits, sizeof(f));

	return f;
}

/* El camino de vuelta, para los casos que comparan patrones de bits exactos --
   redondeo y desnormalizados -- donde una comparacion de floats no distingue
   dos valores que difieren en el ultimo bit. */
static DWORD float_a_bits(float f)
{
	DWORD b;

	memcpy(&b, &f, sizeof(b));

	return b;
}

/* Arma DRn con un entero, usando las propias instrucciones del emulador:
   LDS Rm,FPUL y FLOAT FPUL,DRn. Evita depender de como guarda el double. */
static void poner_double(int n, long valor)
{
	R(14) = (DWORD) valor;
	ejecutar(instr_n(0x405A, 14));					/* LDS R14, FPUL */
	ejecutar((WORD) (0xF02D | ((n & 7) << 9)));		/* FLOAT FPUL, DRn */
}

/* La vuelta: FTRC DRm,FPUL y STS FPUL,Rn. */
static long leer_double_truncado(int m)
{
	ejecutar((WORD) (0xF03D | ((m & 7) << 9)));		/* FTRC DRm, FPUL */
	ejecutar(instr_n(0x005A, 13));					/* STS FPUL, R13 */

	return (long) R(13);
}

/* ------------------------------------------------------------- constantes */

static void fldi0_carga_cero(void)
{
	arnes_reset();

	FR(3) = 12345.0f;
	ejecutar(instr_n(0xF08D, 3));		/* FLDI0 FR3 */

	ESPERAR_F32(FR(3), 0.0f);
	ESPERAR_PC_SIGUIENTE();
}

static void fldi1_carga_uno(void)
{
	arnes_reset();

	ejecutar(instr_n(0xF09D, 3));		/* FLDI1 FR3 */

	ESPERAR_F32(FR(3), 1.0f);
	ESPERAR_PC_SIGUIENTE();
}

/* --------------------------------------------------------------- aritmetica */

static void fadd_simple(void)
{
	arnes_reset();

	FR(1) = 1.5f;
	FR(2) = 2.25f;
	ejecutar(instr_nm(0xF000, 1, 2));	/* FADD FR2, FR1 */

	ESPERAR_F32(FR(1), 3.75f);
	ESPERAR_F32(FR(2), 2.25f);
	ESPERAR_PC_SIGUIENTE();
}

static void fsub_simple(void)
{
	arnes_reset();

	FR(1) = 1.5f;
	FR(2) = 2.25f;
	ejecutar(instr_nm(0xF001, 1, 2));	/* FSUB FR2, FR1 */

	ESPERAR_F32(FR(1), -0.75f);
}

static void fmul_simple(void)
{
	arnes_reset();

	FR(1) = 1.5f;
	FR(2) = 4.0f;
	ejecutar(instr_nm(0xF002, 1, 2));	/* FMUL FR2, FR1 */

	ESPERAR_F32(FR(1), 6.0f);
}

static void fdiv_simple(void)
{
	arnes_reset();

	FR(1) = 6.0f;
	FR(2) = 4.0f;
	ejecutar(instr_nm(0xF003, 1, 2));	/* FDIV FR2, FR1 */

	ESPERAR_F32(FR(1), 1.5f);
}

static void fdiv_entre_cero_da_infinito(void)
{
	arnes_reset();

	FR(1) = 1.0f;
	FR(2) = 0.0f;
	ejecutar(instr_nm(0xF003, 1, 2));

	ESPERAR_U32(*(DWORD *) &FR(1), 0x7F800000);	/* +inf */
}

static void fmac_multiplica_y_acumula_con_fr0(void)
{
	arnes_reset();

	FR(0) = 2.0f;						/* FMAC siempre usa FR0 */
	FR(1) = 10.0f;
	FR(2) = 3.0f;
	ejecutar(instr_nm(0xF00E, 1, 2));	/* FMAC FR0, FR2, FR1 */

	ESPERAR_F32(FR(1), 16.0f);
	ESPERAR_PC_SIGUIENTE();
}

static void fneg_cambia_el_signo(void)
{
	arnes_reset();

	FR(3) = 2.5f;
	ejecutar(instr_n(0xF04D, 3));		/* FNEG FR3 */

	ESPERAR_F32(FR(3), -2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fsqrt_simple(void)
{
	arnes_reset();

	FR(3) = 16.0f;
	ejecutar(instr_n(0xF06D, 3));		/* FSQRT FR3 */

	ESPERAR_F32(FR(3), 4.0f);
	ESPERAR_PC_SIGUIENTE();
}

/* --------------------------------------------------------- comparaciones */

static void fcmpeq_iguales(void)
{
	arnes_reset();

	FR(1) = 2.5f;
	FR(2) = 2.5f;
	ejecutar(instr_nm(0xF004, 1, 2));	/* FCMP/EQ FR2, FR1 */

	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void fcmpeq_cero_negativo_igual_a_cero(void)
{
	arnes_reset();

	FR(1) = 0.0f;
	FR(2) = bits_a_float(0x80000000);	/* -0.0 */
	ejecutar(instr_nm(0xF004, 1, 2));

	ESPERAR_T(1);						/* distinto patron de bits, mismo valor */
}

static void fcmpgt_estricto(void)
{
	arnes_reset();

	FR(1) = 2.5f;
	FR(2) = 2.5f;
	ejecutar(instr_nm(0xF005, 1, 2));	/* FCMP/GT FR2, FR1 */

	ESPERAR_T(0);
}

static void fcmpgt_mayor(void)
{
	arnes_reset();

	FR(1) = 3.0f;
	FR(2) = 2.5f;
	ejecutar(instr_nm(0xF005, 1, 2));

	ESPERAR_T(1);
}

static void fcmpgt_con_nan_es_falso(void)
{
	arnes_reset();

	FR(1) = bits_a_float(0x7FC00000);	/* NaN */
	FR(2) = 2.5f;
	ejecutar(instr_nm(0xF005, 1, 2));

	ESPERAR_T(0);
}

/* --------------------------------------------------------- conversiones */

static void float_convierte_entero_con_signo(void)
{
	arnes_reset();

	FPUL = (DWORD) -3;
	ejecutar(instr_n(0xF02D, 5));		/* FLOAT FPUL, FR5 */

	ESPERAR_F32(FR(5), -3.0f);
	ESPERAR_PC_SIGUIENTE();
}

static void ftrc_trunca_hacia_cero(void)
{
	arnes_reset();

	FR(5) = -1.5f;
	ejecutar(instr_n(0xF03D, 5));		/* FTRC FR5, FPUL */

	ESPERAR_I32((signed long) FPUL, -1);	/* no -2 */
	ESPERAR_PC_SIGUIENTE();
}

static void ftrc_positivo(void)
{
	arnes_reset();

	FR(5) = 3.99f;
	ejecutar(instr_n(0xF03D, 5));

	ESPERAR_I32((signed long) FPUL, 3);
}

static void flds_copia_los_bits_a_fpul(void)
{
	arnes_reset();

	FR(5) = 1.0f;
	ejecutar(instr_n(0xF01D, 5));		/* FLDS FR5, FPUL */

	ESPERAR_U32(FPUL, 0x3F800000);		/* sin conversion: son los bits */
	ESPERAR_PC_SIGUIENTE();
}

static void fsts_copia_los_bits_desde_fpul(void)
{
	arnes_reset();

	FPUL = 0x40000000;					/* 2.0f */
	ejecutar(instr_n(0xF00D, 5));		/* FSTS FPUL, FR5 */

	ESPERAR_F32(FR(5), 2.0f);
	ESPERAR_PC_SIGUIENTE();
}

/* -------------------------------------------------- FMOV de 32 bits (SZ=0) */

static void fmov_registro_a_registro(void)
{
	arnes_reset();

	FR(2) = 2.5f;
	ejecutar(instr_nm(0xF00C, 1, 2));	/* FMOV FR2, FR1 */

	ESPERAR_F32(FR(1), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fmovs_carga_desde_memoria(void)
{
	arnes_reset();

	escribir_f(PRUEBA_DATOS, 2.5f);
	R(2) = PRUEBA_DATOS;
	ejecutar(instr_nm(0xF008, 1, 2));	/* FMOV.S @R2, FR1 */

	ESPERAR_F32(FR(1), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fmovs_carga_indexada(void)
{
	arnes_reset();

	escribir_f(PRUEBA_DATOS + 0x10, 2.5f);
	R(0) = 0x10;
	R(2) = PRUEBA_DATOS;
	ejecutar(instr_nm(0xF006, 1, 2));	/* FMOV.S @(R0, R2), FR1 */

	ESPERAR_F32(FR(1), 2.5f);
}

static void fmovs_carga_con_postincremento(void)
{
	arnes_reset();

	escribir_f(PRUEBA_DATOS, 2.5f);
	R(2) = PRUEBA_DATOS;
	ejecutar(instr_nm(0xF009, 1, 2));	/* FMOV.S @R2+, FR1 */

	ESPERAR_F32(FR(1), 2.5f);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void fmovs_guarda_en_memoria(void)
{
	arnes_reset();

	FR(2) = 2.5f;
	R(1) = PRUEBA_DATOS;
	ejecutar(instr_nm(0xF00A, 1, 2));	/* FMOV.S FR2, @R1 */

	ESPERAR_F32(leer_f(PRUEBA_DATOS), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fmovs_guarda_con_predecremento(void)
{
	arnes_reset();

	FR(2) = 2.5f;
	R(1) = PRUEBA_DATOS;
	ejecutar(instr_nm(0xF00B, 1, 2));	/* FMOV.S FR2, @-R1 */

	ESPERAR_U32(R(1), PRUEBA_DATOS - 4);
	ESPERAR_F32(leer_f(PRUEBA_DATOS - 4), 2.5f);
}

static void fmovs_guarda_indexado(void)
{
	arnes_reset();

	FR(2) = 2.5f;
	R(0) = 0x10;
	R(1) = PRUEBA_DATOS;
	ejecutar(instr_nm(0xF007, 1, 2));	/* FMOV.S FR2, @(R0, R1) */

	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0x10), 2.5f);
}

/* --------------------------------------------------- FMOV de pares (SZ=1) */

static void fmov_par_entre_registros(void)
{
	arnes_reset();
	modo_sz1();

	FR(4) = 1.5f;						/* DR2 = FR4:FR5 */
	FR(5) = 2.5f;

	ejecutar((WORD) (0xF00C | (0 << 9) | (2 << 5)));	/* FMOV DR2, DR0 */

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fmov_par_carga_desde_memoria(void)
{
	arnes_reset();
	modo_sz1();

	escribir_f(PRUEBA_DATOS + 0, 1.5f);
	escribir_f(PRUEBA_DATOS + 4, 2.5f);
	R(2) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF008 | (0 << 9) | (2 << 4)));	/* FMOV @R2, DR0 */

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 2.5f);
}

static void fmov_par_carga_con_postincremento(void)
{
	arnes_reset();
	modo_sz1();

	escribir_f(PRUEBA_DATOS + 0, 1.5f);
	escribir_f(PRUEBA_DATOS + 4, 2.5f);
	R(2) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF009 | (0 << 9) | (2 << 4)));	/* FMOV @R2+, DR0 */

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 2.5f);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 8);	/* avanza 8, no 4 */
}

static void fmov_par_guarda_indexado(void)
{
	arnes_reset();
	modo_sz1();

	FR(0) = 1.5f;
	FR(1) = 2.5f;
	R(0) = 0;							/* R0 es el indice */
	R(3) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF007 | (3 << 8) | (0 << 5)));	/* FMOV DR0, @(R0, R3) */

	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0), 1.5f);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 4), 2.5f);
}

static void fmov_par_guarda_con_predecremento(void)
{
	arnes_reset();
	modo_sz1();

	FR(0) = 1.5f;
	FR(1) = 2.5f;
	R(3) = PRUEBA_DATOS + 8;

	ejecutar((WORD) (0xF00B | (3 << 8) | (0 << 5)));	/* FMOV DR0, @-R3 */

	ESPERAR_U32(R(3), PRUEBA_DATOS);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0), 1.5f);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 4), 2.5f);
}

/* ------------------------------------------------ doble precision (PR=1) */

static void fadd_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 3);					/* DR0 = 3.0 */
	poner_double(2, 4);					/* DR2 = 4.0 */

	ejecutar((WORD) (0xF000 | (2 << 9) | (0 << 5)));	/* FADD DR0, DR2 */

	ESPERAR_I32(leer_double_truncado(2), 7);
}

static void fdiv_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 4);
	poner_double(2, 10);

	ejecutar((WORD) (0xF003 | (2 << 9) | (0 << 5)));	/* FDIV DR0, DR2 */

	ESPERAR_I32(leer_double_truncado(2), 2);			/* 10 / 4 = 2.5 */
}

static void fsqrt_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 144);

	ejecutar((WORD) (0xF06D | (0 << 9)));			/* FSQRT DR0 */

	ESPERAR_I32(leer_double_truncado(0), 12);
}

static void fcmpgt_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 3);
	poner_double(2, 4);

	ejecutar((WORD) (0xF005 | (2 << 9) | (0 << 5)));	/* FCMP/GT DR0, DR2 */

	ESPERAR_T(1);						/* DR2 > DR0 */
}

static void fcmpgt_doble_al_reves(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 4);
	poner_double(2, 3);

	ejecutar((WORD) (0xF005 | (2 << 9) | (0 << 5)));

	ESPERAR_T(0);
}

static void float_doble_negativo(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, -7);

	ESPERAR_I32(leer_double_truncado(0), -7);
}

/* FTRC tiene que truncar hacia cero tambien en doble precision. */
static void ftrc_doble_trunca_hacia_cero(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 2);
	poner_double(2, -5);

	ejecutar((WORD) (0xF003 | (2 << 9) | (0 << 5)));	/* DR2 = -5 / 2 = -2.5 */

	ESPERAR_I32(leer_double_truncado(2), -2);		/* no -3 */
}

/* --------------------------------------------------------- FABS y FNEG */

static void fabs_simple(void)
{
	arnes_reset();

	FR(3) = -2.5f;
	ejecutar(instr_n(0xF05D, 3));		/* FABS FR3 */

	ESPERAR_F32(FR(3), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

/* FABS y FNEG trabajan sobre el bit de signo, no son aritmetica: tienen que
   dar el resultado exacto tambien con el cero negativo. */
static void fabs_de_cero_negativo(void)
{
	arnes_reset();

	FR(3) = bits_a_float(0x80000000);	/* -0.0 */
	ejecutar(instr_n(0xF05D, 3));

	ESPERAR_U32(*(DWORD *) &FR(3), 0x00000000);
}

static void fneg_de_cero_positivo(void)
{
	arnes_reset();

	FR(3) = 0.0f;
	ejecutar(instr_n(0xF04D, 3));		/* FNEG FR3 */

	ESPERAR_U32(*(DWORD *) &FR(3), 0x80000000);
}

/* ------------------------------------------------ FTRC fuera de rango */

static void ftrc_satura_por_arriba(void)
{
	arnes_reset();

	FR(5) = 1e10f;
	ejecutar(instr_n(0xF03D, 5));		/* FTRC FR5, FPUL */

	ESPERAR_U32(FPUL, 0x7FFFFFFF);
}

static void ftrc_satura_por_abajo(void)
{
	arnes_reset();

	FR(5) = -1e10f;
	ejecutar(instr_n(0xF03D, 5));

	ESPERAR_U32(FPUL, 0x80000000);
}

static void ftrc_de_nan_da_el_minimo(void)
{
	arnes_reset();

	FR(5) = bits_a_float(0x7FC00000);
	ejecutar(instr_n(0xF03D, 5));

	ESPERAR_U32(FPUL, 0x80000000);
}

static void ftrc_en_el_limite_negativo_no_satura(void)
{
	arnes_reset();

	FR(5) = -2147483648.0f;				/* el minimo representable */
	ejecutar(instr_n(0xF03D, 5));

	ESPERAR_U32(FPUL, 0x80000000);		/* es el valor, no la saturacion */
}

/* --------------------------------------- FMOV de pares que faltaban (SZ=1) */

static void fmov_par_carga_indexada(void)
{
	arnes_reset();
	modo_sz1();

	escribir_f(PRUEBA_DATOS + 0x10, 1.5f);
	escribir_f(PRUEBA_DATOS + 0x14, 2.5f);
	R(0) = 0x10;
	R(2) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF006 | (0 << 9) | (2 << 4)));	/* FMOV @(R0, R2), DR0 */

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fmov_par_guarda_en_memoria(void)
{
	arnes_reset();
	modo_sz1();

	FR(0) = 1.5f;
	FR(1) = 2.5f;
	R(3) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF00A | (3 << 8) | (0 << 5)));	/* FMOV DR0, @R3 */

	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0), 1.5f);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 4), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------ doble precision que faltaba (PR=1) */

static void fmul_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 6);
	poner_double(2, 7);

	ejecutar((WORD) (0xF002 | (2 << 9) | (0 << 5)));	/* FMUL DR0, DR2 */

	ESPERAR_I32(leer_double_truncado(2), 42);
}

static void fsub_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 3);
	poner_double(2, 10);

	ejecutar((WORD) (0xF001 | (2 << 9) | (0 << 5)));	/* FSUB DR0, DR2 */

	ESPERAR_I32(leer_double_truncado(2), 7);
}

static void fabs_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, -7);

	ejecutar((WORD) (0xF05D | (0 << 9)));			/* FABS DR0 */

	ESPERAR_I32(leer_double_truncado(0), 7);
}

static void fneg_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 7);

	ejecutar((WORD) (0xF04D | (0 << 9)));			/* FNEG DR0 */

	ESPERAR_I32(leer_double_truncado(0), -7);
}

static void fcmpeq_doble(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 42);
	poner_double(2, 42);

	ejecutar((WORD) (0xF004 | (2 << 9) | (0 << 5)));	/* FCMP/EQ DR0, DR2 */

	ESPERAR_T(1);
}

static void fcmpeq_doble_distintos(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 42);
	poner_double(2, 43);

	ejecutar((WORD) (0xF004 | (2 << 9) | (0 << 5)));

	ESPERAR_T(0);
}

static void fcnvds_convierte_a_simple(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 3);

	ejecutar((WORD) (0xF0BD | (0 << 9)));			/* FCNVDS DR0, FPUL */

	ESPERAR_U32(FPUL, 0x40400000);					/* 3.0f */
}

static void fcnvsd_convierte_a_doble(void)
{
	arnes_reset();
	modo_pr1();

	FPUL = 0x40400000;								/* 3.0f */

	ejecutar((WORD) (0xF0AD | (0 << 9)));			/* FCNVSD FPUL, DR0 */

	ESPERAR_I32(leer_double_truncado(0), 3);
}

static void fcnvds_y_fcnvsd_son_reversibles(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 12345);

	ejecutar((WORD) (0xF0BD | (0 << 9)));			/* DR0 -> FPUL */
	ejecutar((WORD) (0xF0AD | (2 << 9)));			/* FPUL -> DR2 */

	ESPERAR_I32(leer_double_truncado(2), 12345);
}

static void ftrc_doble_satura_por_arriba(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, 1000000);
	ejecutar((WORD) (0xF002 | (0 << 9) | (0 << 5)));	/* DR0 = 1e12 */

	ESPERAR_I32((signed long) leer_double_truncado(0), 0x7FFFFFFF);
}

static void ftrc_doble_satura_por_abajo(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, -1000000);
	poner_double(2, 1000000);
	ejecutar((WORD) (0xF002 | (2 << 9) | (0 << 5)));	/* DR2 = -1e12 */

	ESPERAR_U32((DWORD) leer_double_truncado(2), 0x80000000);
}

/* ------------------------------------------------------------------------ */

/* ------------------------------------------------ FPSCR.RM y FPSCR.DN ---- */

/* RM = 01 -- truncar hacia cero -- es el valor de reset del SH-4 y lo que deja
   KOS, o sea el modo en que corre todo. dcemu lo ignoraba y redondeaba siempre
   al mas cercano. 1/3 es el caso mas corto: al mas cercano da 0x3EAAAAAB y
   truncando 0x3EAAAAAA. */
static void rm_uno_trunca_hacia_cero(void)
{
	arnes_reset();				/* FPSCR de reset: RM = 01 */

	FR(1) = 1.0f;
	FR(2) = 3.0f;
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 */

	ESPERAR_U32(float_a_bits(FR(1)), 0x3EAAAAAAu);
}

static void rm_cero_redondea_al_mas_cercano(void)
{
	arnes_reset();

	UpdateFPSCR(FPSCR & ~3u);				/* RM = 00 */

	FR(1) = 1.0f;
	FR(2) = 3.0f;
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 */

	ESPERAR_U32(float_a_bits(FR(1)), 0x3EAAAAABu);
}

/* DN = 1: "a denormalized number (source operand or operation result) is
   always flushed to 0" -- manual, 6.2.3. Entra y sale. */
static void dn_aplasta_el_operando_desnormalizado(void)
{
	arnes_reset();				/* FPSCR de reset: DN = 1 */

	FR(1) = 1.0f;
	FR(2) = bits_a_float(0x00400000u);				/* FLT_MIN/2, subnormal */
	ejecutar(instr_nm(0xF000, 1, 2));		/* FADD FR2, FR1 */

	ESPERAR_F32(FR(1), 1.0f);				/* el subnormal valio cero */
}

static void dn_aplasta_el_resultado_desnormalizado(void)
{
	arnes_reset();

	FR(1) = bits_a_float(0x00800000u);					/* FLT_MIN */
	FR(2) = bits_a_float(0x3E800000u);					/* 0.25 */
	ejecutar(instr_nm(0xF002, 1, 2));		/* FMUL FR2, FR1 */

	ESPERAR_U32(float_a_bits(FR(1)), 0);			/* FLT_MIN/4 es subnormal */
}

static void sin_dn_el_desnormalizado_sobrevive(void)
{
	arnes_reset();

	UpdateFPSCR(FPSCR & ~0x00040000u);		/* DN = 0 */

	FR(1) = bits_a_float(0x00800000u);		/* FLT_MIN */
	FR(2) = bits_a_float(0x00400000u);		/* FLT_MIN/2, subnormal */
	ejecutar(instr_nm(0xF001, 1, 2));		/* FSUB FR2, FR1 */

	ESPERAR_U32(float_a_bits(FR(1)), 0x00400000u);
}

/* FMAC redondea una sola vez: "Rounding is performed once in FMAC, but twice
   in FADD, FSUB, and FMUL" (manual, 6.4). O sea que es un multiplicar-y-sumar
   fusionado y el producto no pasa por simple precision.

   (1 + 2^-23)^2 = 1 + 2^-22 + 2^-46. Redondeado a simple queda 1 + 2^-22
   justo, y restarle 1 + 2^-22 da cero; sin redondeo intermedio queda el
   2^-46. dcemu redondeaba dos veces, y eso costaba un ulp en 35 de las 500
   pruebas de 1111nnnnmmmm1110 de SingleStepTests. */
static void fmac_redondea_una_sola_vez(void)
{
	arnes_reset();

	FR(0) = bits_a_float(0x3F800001u);		/* 1 + 2^-23 */
	FR(2) = bits_a_float(0x3F800001u);
	FR(1) = bits_a_float(0xBF800002u);		/* -(1 + 2^-22) */
	ejecutar(instr_nm(0xF00E, 1, 2));		/* FMAC FR0, FR2, FR1 */

	ESPERAR_U32(float_a_bits(FR(1)), 0x28800000u);		/* 2^-46 */
}

static const dc_caso casos[] =
{
	CASO(fldi0_carga_cero),
	CASO(fldi1_carga_uno),
	CASO(fadd_simple),
	CASO(fsub_simple),
	CASO(fmul_simple),
	CASO(fdiv_simple),
	CASO(fdiv_entre_cero_da_infinito),
	CASO(fmac_multiplica_y_acumula_con_fr0),
	CASO(fneg_cambia_el_signo),
	CASO(fsqrt_simple),
	CASO(fcmpeq_iguales),
	CASO(fcmpeq_cero_negativo_igual_a_cero),
	CASO(fcmpgt_estricto),
	CASO(fcmpgt_mayor),
	CASO(fcmpgt_con_nan_es_falso),
	CASO(float_convierte_entero_con_signo),
	CASO(ftrc_trunca_hacia_cero),
	CASO(ftrc_positivo),
	CASO(flds_copia_los_bits_a_fpul),
	CASO(fsts_copia_los_bits_desde_fpul),
	CASO(fmov_registro_a_registro),
	CASO(fmovs_carga_desde_memoria),
	CASO(fmovs_carga_indexada),
	CASO(fmovs_carga_con_postincremento),
	CASO(fmovs_guarda_en_memoria),
	CASO(fmovs_guarda_con_predecremento),
	CASO(fmovs_guarda_indexado),
	CASO(fmov_par_entre_registros),
	CASO(fmov_par_carga_desde_memoria),
	CASO(fmov_par_carga_con_postincremento),
	CASO(fmov_par_guarda_indexado),
	CASO(fmov_par_guarda_con_predecremento),
	CASO(fadd_doble),
	CASO(fdiv_doble),
	CASO(fsqrt_doble),
	CASO(fcmpgt_doble),
	CASO(fcmpgt_doble_al_reves),
	CASO(float_doble_negativo),
	CASO(ftrc_doble_trunca_hacia_cero),
	CASO(fabs_simple),
	CASO(fabs_de_cero_negativo),
	CASO(fneg_de_cero_positivo),
	CASO(ftrc_satura_por_arriba),
	CASO(ftrc_satura_por_abajo),
	CASO(ftrc_de_nan_da_el_minimo),
	CASO(ftrc_en_el_limite_negativo_no_satura),
	CASO(fmov_par_carga_indexada),
	CASO(fmov_par_guarda_en_memoria),
	CASO(fmul_doble),
	CASO(fsub_doble),
	CASO(fabs_doble),
	CASO(fneg_doble),
	CASO(fcmpeq_doble),
	CASO(fcmpeq_doble_distintos),
	CASO(fcnvds_convierte_a_simple),
	CASO(fcnvsd_convierte_a_doble),
	CASO(fcnvds_y_fcnvsd_son_reversibles),
	CASO(ftrc_doble_satura_por_arriba),
	CASO(ftrc_doble_satura_por_abajo),
	CASO(rm_uno_trunca_hacia_cero),
	CASO(rm_cero_redondea_al_mas_cercano),
	CASO(dn_aplasta_el_operando_desnormalizado),
	CASO(dn_aplasta_el_resultado_desnormalizado),
	CASO(sin_dn_el_desnormalizado_sobrevive),
	CASO(fmac_redondea_una_sola_vez),
};

const dc_suite suite_fpu_simple = DEFINIR_SUITE("fpu-simple", casos);
