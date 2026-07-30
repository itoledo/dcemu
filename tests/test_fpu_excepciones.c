/****************************************************************************

	Pruebas de los campos Cause y Flag de FPSCR

	El SH-4 rearma el campo Cause en cada operacion de la FPU y acumula las
	mismas causas en el campo Flag, que solo se limpia escribiendo FPSCR. Las
	causas que dcemu detecta son V (invalida), Z (division por cero), O
	(desbordamiento) y U (subdesbordamiento); I (inexacto) solo aparece
	acompanando a O y a U. El porque esta en docs/sh4-conformidad.md.

	Casos borde que cubre esta suite:

	  - inf/0 no es division por cero: el resultado es infinito y esta bien
		definido. Solo lo es un dividendo finito distinto de cero.
	  - 0/0 e inf-inf si son invalidas, y se reconocen porque el resultado sale
		NaN sin que ninguna entrada lo fuera.
	  - Un NaN que entra y sale se propaga sin levantar nada.
	  - Las sumas y las restas nunca subdesbordan: cuando su resultado cae bajo
		el minimo normal es exacto.
	  - Los dobles muy grandes y muy chicos se arman elevando al cuadrado, que
		es la unica forma de salir del rango del float con FCNVSD.

*****************************************************************************/

#include <math.h>

#include "arnes.h"
#include "excepciones.h"
#include "suites.h"

#define BIT_SZ	0x00100000u
#define BIT_PR	0x00080000u

/* Los dos campos llevan las mismas causas en el mismo orden, asi que un solo
   juego de constantes sirve para leer Cause y Flag. */
#define C_I		0x01u
#define C_U		0x02u
#define C_O		0x04u
#define C_Z		0x08u
#define C_V		0x10u

/* Patrones de bits de float, para no depender de constantes del compilador. */
#define F_CERO		0x00000000u
#define F_UNO		0x3F800000u
#define F_DOS		0x40000000u
#define F_INF		0x7F800000u
#define F_INF_NEG	0xFF800000u
#define F_NAN		0x7FC00000u
#define F_MAX		0x7F7FFFFFu		/* FLT_MAX,  ~2^128 */
#define F_MIN		0x00800000u		/* FLT_MIN,  2^-126, el menor normal */
#define F_SUBNORMAL	0x00400000u		/* FLT_MIN/2, exacto y por debajo del normal */

static void modo_pr1(void)
{
	UpdateFPSCR((FPSCR & ~BIT_SZ) | BIT_PR);
}

static float f(DWORD bits)
{
	float v;

	memcpy(&v, &bits, sizeof(v));

	return v;
}

/* Instruccion de doble precision: 1111nnn0 mmm0.... */
static WORD instr_dd(WORD base, int n, int m)
{
	return (WORD) (base | ((n & 7) << 9) | ((m & 7) << 5));
}

/* Mete un float en DRn con FCNVSD, que es la via limpia: floatsimple.c guarda
   las dos mitades del double invertidas respecto del host. */
static void poner_double(int n, DWORD bits_float)
{
	FPUL = bits_float;
	ejecutar((WORD) (0xF0AD | ((n & 7) << 9)));		/* FCNVSD FPUL, DRn */
}

/* --------------------------------------------------------------- arranque */

static void el_reset_deja_cause_y_flag_en_cero(void)
{
	arnes_reset();

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
}

/* ------------------------------------------------- division por cero (Z) */

static void fdiv_entre_cero_levanta_z(void)
{
	arnes_reset();

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 : FR1 / FR2 */

	ESPERAR_U32(FPSCR_CAUSE, C_Z);
	ESPERAR_U32(FPSCR_FLAG, C_Z);
	ESPERAR(isinf(FR(1)));
}

static void fdiv_cero_entre_cero_es_invalida_no_division_por_cero(void)
{
	arnes_reset();

	FR(1) = f(F_CERO);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR(isnan(FR(1)));
}

static void fdiv_infinito_entre_cero_no_levanta_nada(void)
{
	arnes_reset();

	/* Infinito partido por cero es infinito: esta definido y no es una
	   division por cero en el sentido de IEEE 754. */
	FR(1) = f(F_INF);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR(isinf(FR(1)));
}

/* ------------------------------------------------- operacion invalida (V) */

static void fadd_de_infinitos_opuestos_levanta_v(void)
{
	arnes_reset();

	FR(1) = f(F_INF);
	FR(2) = f(F_INF_NEG);
	ejecutar(instr_nm(0xF000, 1, 2));		/* FADD FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR(isnan(FR(1)));
}

static void fmul_de_cero_por_infinito_levanta_v(void)
{
	arnes_reset();

	FR(1) = f(F_CERO);
	FR(2) = f(F_INF);
	ejecutar(instr_nm(0xF002, 1, 2));		/* FMUL FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
}

static void un_nan_que_entra_y_sale_no_levanta_nada(void)
{
	arnes_reset();

	/* La causa invalida se reconoce porque el resultado sale NaN sin que
	   ninguna entrada lo fuera. Un NaN silencioso se propaga y ya. */
	FR(1) = f(F_NAN);
	FR(2) = f(F_UNO);
	ejecutar(instr_nm(0xF000, 1, 2));		/* FADD FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR(isnan(FR(1)));
}

static void fsqrt_de_negativo_levanta_v(void)
{
	arnes_reset();

	FR(3) = -4.0f;
	ejecutar(instr_n(0xF06D, 3));			/* FSQRT FR3 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
}

static void fsqrt_de_positivo_no_levanta_nada(void)
{
	arnes_reset();

	FR(3) = 9.0f;
	ejecutar(instr_n(0xF06D, 3));			/* FSQRT FR3 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR_F32(FR(3), 3.0f);
}

static void fcmpgt_con_nan_levanta_v(void)
{
	arnes_reset();

	/* El manual del SH-4 da las dos comparaciones por invalidas con cualquier
	   NaN, sin distinguir si es de senal o silencioso. */
	FR(1) = f(F_NAN);
	FR(2) = f(F_UNO);
	ejecutar(instr_nm(0xF005, 1, 2));		/* FCMP/GT FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR_T(0);
}

static void fcmpeq_con_nan_levanta_v(void)
{
	arnes_reset();

	FR(1) = f(F_UNO);
	FR(2) = f(F_NAN);
	ejecutar(instr_nm(0xF004, 1, 2));		/* FCMP/EQ FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR_T(0);
}

static void ftrc_de_nan_levanta_v(void)
{
	arnes_reset();

	FR(5) = f(F_NAN);
	ejecutar(instr_n(0xF03D, 5));			/* FTRC FR5, FPUL */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR_U32(FPUL, 0x80000000);
}

static void ftrc_fuera_de_rango_levanta_v(void)
{
	arnes_reset();

	FR(5) = 4.0e9f;							/* no entra en un entero con signo */
	ejecutar(instr_n(0xF03D, 5));			/* FTRC FR5, FPUL */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR_U32(FPUL, 0x7FFFFFFF);
}

static void ftrc_dentro_de_rango_no_levanta_nada(void)
{
	arnes_reset();

	FR(5) = -1.5f;
	ejecutar(instr_n(0xF03D, 5));			/* FTRC FR5, FPUL */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR_I32((long) FPUL, -1);
}

/* -------------------------------------------------- desbordamiento (O e I) */

static void fmul_que_desborda_levanta_o_e_i(void)
{
	arnes_reset();

	FR(1) = f(F_MAX);
	FR(2) = f(F_DOS);
	ejecutar(instr_nm(0xF002, 1, 2));		/* FMUL FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_O | C_I);
	ESPERAR(isinf(FR(1)));
}

static void fadd_que_desborda_levanta_o_e_i(void)
{
	arnes_reset();

	FR(1) = f(F_MAX);
	FR(2) = f(F_MAX);
	ejecutar(instr_nm(0xF000, 1, 2));		/* FADD FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_O | C_I);
}

static void un_infinito_que_entra_y_sale_no_desborda(void)
{
	arnes_reset();

	FR(1) = f(F_INF);
	FR(2) = f(F_DOS);
	ejecutar(instr_nm(0xF002, 1, 2));		/* FMUL FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR(isinf(FR(1)));
}

/* ----------------------------------------------- subdesbordamiento (U e I) */

static void fmul_que_subdesborda_levanta_u_e_i(void)
{
	arnes_reset();

	FR(1) = 1.0e-30f;
	FR(2) = 1.0e-20f;
	ejecutar(instr_nm(0xF002, 1, 2));		/* FMUL FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_U | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_U | C_I);
	ESPERAR_F32(FR(1), 0.0f);
}

static void fmul_por_cero_no_subdesborda(void)
{
	arnes_reset();

	/* El resultado tambien es cero, pero por multiplicar por cero, no por
		irse abajo del rango. */
	FR(1) = 1.0e-30f;
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF002, 1, 2));		/* FMUL FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
}

static void fsub_con_resultado_subnormal_no_subdesborda(void)
{
	arnes_reset();

	/* FLT_MIN - FLT_MIN/2 da FLT_MIN/2: subnormal pero exacto. Sin
	   inexactitud no hay subdesbordamiento, y por eso las sumas y las restas
	   no lo levantan nunca. */
	FR(1) = f(F_MIN);
	FR(2) = f(F_SUBNORMAL);
	ejecutar(instr_nm(0xF001, 1, 2));		/* FSUB FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR_F32(FR(1), f(F_SUBNORMAL));
}

/* -------------------------------------------------------------- FMAC */

static void fmac_desborda_en_el_producto(void)
{
	arnes_reset();

	/* FMAC son dos operaciones y el producto tiene sus propias causas: aca
	   desborda antes de que la suma lo vea. */
	FR(0) = f(F_MAX);
	FR(2) = f(F_DOS);
	FR(1) = f(F_CERO);
	ejecutar(instr_nm(0xF00E, 1, 2));		/* FMAC FR0, FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_O | C_I);
}

static void fmac_limpio_no_levanta_nada(void)
{
	arnes_reset();

	FR(0) = 2.0f;
	FR(2) = 3.0f;
	FR(1) = 1.0f;
	ejecutar(instr_nm(0xF00E, 1, 2));		/* FMAC FR0, FR2, FR1 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR_F32(FR(1), 7.0f);
}

/* ------------------------------------------------------------ conversiones */

static void fcnvds_que_desborda_al_bajar_a_simple_levanta_o(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_MAX);
	poner_double(1, F_DOS);
	ejecutar(instr_dd(0xF002, 0, 1));		/* FMUL DR1, DR0 : ~2^129 en doble */

	ejecutar((WORD) (0xF0BD | (0 << 9)));	/* FCNVDS DR0, FPUL */

	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_O | C_I);
	ESPERAR_U32(FPUL, F_INF);
}

static void fcnvds_que_subdesborda_al_bajar_a_simple_levanta_u(void)
{
	arnes_reset();
	modo_pr1();

	/* 2^-126 al cuadrado es 2^-252: normal en doble, cero en simple. */
	poner_double(0, F_MIN);
	ejecutar(instr_dd(0xF002, 0, 0));		/* FMUL DR0, DR0 */

	ejecutar((WORD) (0xF0BD | (0 << 9)));	/* FCNVDS DR0, FPUL */

	ESPERAR_U32(FPSCR_CAUSE, C_U | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_U | C_I);
	ESPERAR_U32(FPUL, F_CERO);
}

static void float_limpia_cause_y_no_levanta_nada(void)
{
	arnes_reset();

	/* Un entero de 32 bits siempre entra en el rango de un float, asi que
	   FLOAT no puede levantar nada -- pero rearma Cause igual. */
	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 : deja Z */

	FPUL = 5;
	ejecutar(instr_n(0xF02D, 3));			/* FLOAT FPUL, FR3 */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, C_Z);
	ESPERAR_F32(FR(3), 5.0f);
}

/* ------------------------------------------------------- doble precision */

static void fdiv_doble_entre_cero_levanta_z(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_UNO);
	poner_double(1, F_CERO);
	ejecutar(instr_dd(0xF003, 0, 1));		/* FDIV DR1, DR0 : DR0 / DR1 */

	ESPERAR_U32(FPSCR_CAUSE, C_Z);
	ESPERAR_U32(FPSCR_FLAG, C_Z);
}

static void fdiv_doble_cero_entre_cero_levanta_v(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_CERO);
	poner_double(1, F_CERO);
	ejecutar(instr_dd(0xF003, 0, 1));		/* FDIV DR1, DR0 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
}

static void fmul_doble_que_desborda_levanta_o_e_i(void)
{
	arnes_reset();
	modo_pr1();

	/* Por FCNVSD no entra nada mayor que un float, asi que hay que elevar al
	   cuadrado hasta salirse del rango del doble. FLT_MAX es 2^128 menos un
	   poco, y ese "menos un poco" alcanza para que el tercer cuadrado quede
	   justo por debajo de DBL_MAX en vez de desbordar. */
	poner_double(0, F_MAX);
	ejecutar(instr_dd(0xF002, 0, 0));		/* ~2^256 */
	ejecutar(instr_dd(0xF002, 0, 0));		/* ~2^512 */
	ejecutar(instr_dd(0xF002, 0, 0));		/* ~2^1024, todavia finito */

	ESPERAR_U32(FPSCR_CAUSE, 0);			/* hasta aca no desbordo nada */

	ejecutar(instr_dd(0xF002, 0, 0));		/* ~2^2048: infinito */

	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_O | C_I);
}

static void fmul_doble_que_subdesborda_levanta_u_e_i(void)
{
	arnes_reset();
	modo_pr1();

	/* La bajada: 2^-126 -> 2^-252 -> 2^-504 -> 2^-1008, todos normales en
	   doble. El siguiente cuadrado se va por debajo de todo. */
	poner_double(0, F_MIN);
	ejecutar(instr_dd(0xF002, 0, 0));		/* 2^-252 */
	ejecutar(instr_dd(0xF002, 0, 0));		/* 2^-504 */
	ejecutar(instr_dd(0xF002, 0, 0));		/* 2^-1008 */

	ESPERAR_U32(FPSCR_CAUSE, 0);			/* hasta aca sigue siendo normal */

	ejecutar(instr_dd(0xF002, 0, 0));		/* 2^-2016: cero */

	ESPERAR_U32(FPSCR_CAUSE, C_U | C_I);
	ESPERAR_U32(FPSCR_FLAG, C_U | C_I);
}

static void fadd_doble_de_infinitos_opuestos_levanta_v(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_INF);
	poner_double(1, F_INF_NEG);
	ejecutar(instr_dd(0xF000, 0, 1));		/* FADD DR1, DR0 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
}

static void fsqrt_doble_de_negativo_levanta_v(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_UNO | 0x80000000u);	/* -1.0f */
	ejecutar((WORD) (0xF06D | (0 << 9)));	/* FSQRT DR0 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
}

static void ftrc_doble_de_nan_levanta_v(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_NAN);
	ejecutar((WORD) (0xF03D | (0 << 9)));	/* FTRC DR0, FPUL */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR_U32(FPUL, 0x80000000);
}

static void fcmpgt_doble_con_nan_levanta_v(void)
{
	arnes_reset();
	modo_pr1();

	poner_double(0, F_NAN);
	poner_double(1, F_UNO);
	ejecutar(instr_dd(0xF005, 0, 1));		/* FCMP/GT DR1, DR0 */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_V);
	ESPERAR_T(0);
}

/* --------------------------------------------- acumulacion y limpieza */

static void cause_se_rearma_y_flag_se_acumula(void)
{
	arnes_reset();

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 : Z */

	ESPERAR_U32(FPSCR_CAUSE, C_Z);
	ESPERAR_U32(FPSCR_FLAG, C_Z);

	FR(3) = 2.0f;
	FR(4) = 3.0f;
	ejecutar(instr_nm(0xF000, 3, 4));		/* FADD FR4, FR3 : sin causa */

	ESPERAR_U32(FPSCR_CAUSE, 0);			/* Cause vale por instruccion */
	ESPERAR_U32(FPSCR_FLAG, C_Z);			/* Flag no se limpia sola */

	FR(5) = f(F_NAN);
	FR(6) = f(F_UNO);
	ejecutar(instr_nm(0xF005, 5, 6));		/* FCMP/GT FR6, FR5 : V */

	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, C_Z | C_V);		/* las dos causas juntas */
}

static void escribir_fpscr_limpia_flag(void)
{
	arnes_reset();

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 : Z */

	ESPERAR_U32(FPSCR_FLAG, C_Z);

	R(3) = 0x00040001;						/* DN=1, RM=01: el valor de reset */
	ejecutar(instr_n(0x406A, 3));			/* LDS R3, FPSCR */

	ESPERAR_U32(FPSCR_CAUSE, 0);
	ESPERAR_U32(FPSCR_FLAG, 0);
}

static void las_transferencias_no_tocan_cause(void)
{
	arnes_reset();

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);
	ejecutar(instr_nm(0xF003, 1, 2));		/* FDIV FR2, FR1 : Z */

	/* FMOV, FNEG, FABS y FLDI no son operaciones aritmeticas: el manual no les
	   asigna excepciones, asi que tampoco rearman Cause. */
	ejecutar(instr_nm(0xF00C, 7, 1));		/* FMOV FR1, FR7 */
	ejecutar(instr_n(0xF04D, 7));			/* FNEG FR7 */
	ejecutar(instr_n(0xF05D, 7));			/* FABS FR7 */
	ejecutar(instr_n(0xF09D, 7));			/* FLDI1 FR7 */

	ESPERAR_U32(FPSCR_CAUSE, C_Z);
	ESPERAR_U32(FPSCR_FLAG, C_Z);
}

/* --------------------------------------------- Enable y la trampa (0x120) */

/* Los bits de Enable: mismo orden que Cause y Flag, en los bits 11-7. */
#define E_I		0x00000080u
#define E_U		0x00000100u
#define E_O		0x00000200u
#define E_Z		0x00000400u
#define E_V		0x00000800u

#define VBR_PRUEBA	0x8C030000u

static void habilitar(DWORD enables)
{
	UpdateFPSCR(FPSCR | enables);
	VBR = VBR_PRUEBA;
}

static void sin_enable_la_division_por_cero_no_atrapa(void)
{
	arnes_reset();

	VBR = VBR_PRUEBA;
	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF003, 1, 2)), 0);

	ESPERAR_U32(FPSCR_FLAG, C_Z);
	ESPERAR_U32(PC, PRUEBA_PC + 2);
}

static void enable_z_atrapa_la_division_por_cero(void)
{
	arnes_reset();
	habilitar(E_Z);

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF003, 1, 2)), 1);

	ESPERAR_U32(*EXPEVT, EXC_FPU_OPERACION);
	ESPERAR_U32(PC, VBR_PRUEBA + EXC_VEC_GENERAL);
	ESPERAR_U32(SPC, PRUEBA_PC);			/* reejecucion: la misma instruccion */
	ESPERAR_U32(FPSCR_CAUSE, C_Z);
	ESPERAR_U32(FPSCR_FLAG, 0);				/* el manual: Flag no se actualiza */
	ESPERAR_F32(FR(1), 1.0f);				/* ni el registro destino */
}

static void la_trampa_no_agrega_nada_a_flag(void)
{
	arnes_reset();
	VBR = VBR_PRUEBA;

	/* Primero una causa sin trampa, para dejar Flag sucio. */
	FR(3) = f(F_NAN);
	FR(4) = f(F_UNO);
	ejecutar(instr_nm(0xF005, 3, 4));		/* FCMP/GT: deja Flag.V */

	ESPERAR_U32(FPSCR_FLAG, C_V);

	UpdateFPSCR(FPSCR | E_Z);

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);
	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF003, 1, 2)), 1);

	ESPERAR_U32(FPSCR_CAUSE, C_Z);
	ESPERAR_U32(FPSCR_FLAG, C_V);			/* lo de antes, sin la Z nueva */
}

static void un_enable_de_otra_causa_no_atrapa(void)
{
	arnes_reset();
	habilitar(E_O | E_U | E_V);				/* todos menos Z */

	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF003, 1, 2)), 0);

	ESPERAR_U32(FPSCR_FLAG, C_Z);
}

static void enable_v_atrapa_la_operacion_invalida(void)
{
	arnes_reset();
	habilitar(E_V);

	FR(1) = f(F_INF);
	FR(2) = f(F_INF_NEG);

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF000, 1, 2)), 1);

	ESPERAR_U32(*EXPEVT, EXC_FPU_OPERACION);
	ESPERAR_U32(FPSCR_CAUSE, C_V);
	ESPERAR_U32(FPSCR_FLAG, 0);
}

static void enable_o_atrapa_el_desbordamiento(void)
{
	arnes_reset();
	habilitar(E_O);

	FR(1) = f(F_MAX);
	FR(2) = f(F_DOS);

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF002, 1, 2)), 1);

	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR_F32(FR(1), f(F_MAX));			/* destino sin tocar */
}

static void enable_u_atrapa_el_subdesbordamiento(void)
{
	arnes_reset();
	habilitar(E_U);

	FR(1) = 1.0e-30f;
	FR(2) = 1.0e-20f;

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF002, 1, 2)), 1);

	ESPERAR_U32(FPSCR_CAUSE, C_U | C_I);
	ESPERAR_U32(FPSCR_FLAG, 0);
	ESPERAR_F32(FR(1), 1.0e-30f);
}

static void enable_i_solo_atrapa_cuando_viene_con_o_o_con_u(void)
{
	arnes_reset();
	habilitar(E_I);

	/* dcemu no detecta I por si sola, asi que una division exacta no atrapa. */
	FR(1) = 6.0f;
	FR(2) = 2.0f;
	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF003, 1, 2)), 0);
	ESPERAR_F32(FR(1), 3.0f);

	/* Pero un desbordamiento arrastra I, y con Enable.I puesto si atrapa. */
	FR(3) = f(F_MAX);
	FR(4) = f(F_DOS);
	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF002, 3, 4)), 1);
	ESPERAR_U32(FPSCR_CAUSE, C_O | C_I);
}

static void la_excepcion_guarda_ssr_spc_y_sgr(void)
{
	arnes_reset();
	habilitar(E_Z);

	R(15) = 0x8C05FFF0;
	FR(1) = f(F_UNO);
	FR(2) = f(F_CERO);

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF003, 1, 2)), 1);

	ESPERAR_U32(SPC, PRUEBA_PC);
	ESPERAR_U32(SGR, 0x8C05FFF0);
	ESPERAR_U32(SR_BL, 1);					/* la secuencia de excepcion */
	ESPERAR_U32(SR_MD, 1);
	ESPERAR_U32(SR_RB, 1);
}

/* ---------------------------------------- FPU deshabilitada (0x800 y 0x820) */

static void poner_fd(void)
{
	UpdateSR(SR | (1u << 15));				/* SR.FD */
	VBR = VBR_PRUEBA;
}

static void fd_atrapa_una_instruccion_de_fpu(void)
{
	arnes_reset();
	poner_fd();

	FR(1) = 1.0f;
	FR(2) = 2.0f;

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0xF000, 1, 2)), 1);

	ESPERAR_U32(*EXPEVT, EXC_FPU_GENERAL);
	ESPERAR_U32(PC, VBR_PRUEBA + EXC_VEC_GENERAL);
	ESPERAR_U32(SPC, PRUEBA_PC);
	ESPERAR_F32(FR(1), 1.0f);				/* no llego a sumar */
}

static void fd_no_atrapa_una_instruccion_entera(void)
{
	arnes_reset();
	poner_fd();

	R(1) = 5;
	R(2) = 7;

	ESPERAR_I32(ejecutar_vigilado(instr_nm(0x300C, 1, 2)), 0);	/* ADD R2, R1 */

	ESPERAR_U32(R(1), 12);
}

static void fd_atrapa_las_transferencias_de_fpul_y_fpscr(void)
{
	/* No empiezan con 1111, pero el manual las lista con las de FPU porque
	   tocan sus registros. */
	static const WORD instrucciones[] =
	{
		0x405A,		/* LDS   R0, FPUL    */
		0x406A,		/* LDS   R0, FPSCR   */
		0x4056,		/* LDS.L @R0+, FPUL  */
		0x4066,		/* LDS.L @R0+, FPSCR */
		0x005A,		/* STS   FPUL, R0    */
		0x006A,		/* STS   FPSCR, R0   */
		0x4052,		/* STS.L FPUL, @-R0  */
		0x4062,		/* STS.L FPSCR, @-R0 */
	};
	int i;

	for (i = 0; i < (int) (sizeof(instrucciones) / sizeof(instrucciones[0])); i++)
	{
		arnes_reset();
		poner_fd();

		R(0) = PRUEBA_DATOS;

		ESPERAR_I32(ejecutar_vigilado(instrucciones[i]), 1);
		ESPERAR_U32(*EXPEVT, EXC_FPU_GENERAL);
	}
}

static void fd_en_la_ranura_de_retardo_da_el_codigo_de_ranura(void)
{
	arnes_reset();
	poner_fd();

	/* BRA con un FADD en la ranura. El codigo cambia a 0x820 y SPC apunta al
	   salto, no a la ranura: el longjmp desenrolla los dos niveles y la
	   instantanea deja PC donde empezo. */
	poner_instr(PRUEBA_PC + 2, instr_nm(0xF000, 1, 2));

	ESPERAR_I32(ejecutar_vigilado(0xA010), 1);		/* BRA +0x20 */

	ESPERAR_U32(*EXPEVT, EXC_FPU_RANURA);
	ESPERAR_U32(SPC, PRUEBA_PC);
	ESPERAR_U32(en_ranura_retardo, 0);				/* main_loop la limpia */
}

static void sin_fd_la_ranura_de_retardo_corre_normal(void)
{
	arnes_reset();
	VBR = VBR_PRUEBA;

	FR(1) = 1.0f;
	FR(2) = 2.0f;
	poner_instr(PRUEBA_PC + 2, instr_nm(0xF000, 1, 2));

	ESPERAR_I32(ejecutar_vigilado(0xA010), 0);		/* BRA +0x20 */

	ESPERAR_F32(FR(1), 3.0f);
	ESPERAR_U32(PC, PRUEBA_PC + 4 + 0x20);
}

/* ------------------------------------------------------------- vigilancia */

static void la_vigilancia_sigue_a_fd_y_a_enable(void)
{
	arnes_reset();

	/* Es lo que decide si main_loop() saca instantanea. En lo que corre hoy
	   -- sin MMU, sin FD y sin Enable -- tiene que valer cero. */
	ESPERAR_U32(excepcion_vigilar, 0);

	UpdateFPSCR(FPSCR | E_Z);
	ESPERAR_U32(excepcion_vigilar != 0, 1);

	UpdateFPSCR(FPSCR & ~FPU_ENABLE_TODAS);
	ESPERAR_U32(excepcion_vigilar, 0);

	UpdateSR(SR | (1u << 15));				/* SR.FD */
	ESPERAR_U32(excepcion_vigilar != 0, 1);
	ESPERAR_U32(fpu_deshabilitada != 0, 1);

	UpdateSR(SR & ~(1u << 15));
	ESPERAR_U32(excepcion_vigilar, 0);
	ESPERAR_U32(fpu_deshabilitada, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(el_reset_deja_cause_y_flag_en_cero),
	CASO(fdiv_entre_cero_levanta_z),
	CASO(fdiv_cero_entre_cero_es_invalida_no_division_por_cero),
	CASO(fdiv_infinito_entre_cero_no_levanta_nada),
	CASO(fadd_de_infinitos_opuestos_levanta_v),
	CASO(fmul_de_cero_por_infinito_levanta_v),
	CASO(un_nan_que_entra_y_sale_no_levanta_nada),
	CASO(fsqrt_de_negativo_levanta_v),
	CASO(fsqrt_de_positivo_no_levanta_nada),
	CASO(fcmpgt_con_nan_levanta_v),
	CASO(fcmpeq_con_nan_levanta_v),
	CASO(ftrc_de_nan_levanta_v),
	CASO(ftrc_fuera_de_rango_levanta_v),
	CASO(ftrc_dentro_de_rango_no_levanta_nada),
	CASO(fmul_que_desborda_levanta_o_e_i),
	CASO(fadd_que_desborda_levanta_o_e_i),
	CASO(un_infinito_que_entra_y_sale_no_desborda),
	CASO(fmul_que_subdesborda_levanta_u_e_i),
	CASO(fmul_por_cero_no_subdesborda),
	CASO(fsub_con_resultado_subnormal_no_subdesborda),
	CASO(fmac_desborda_en_el_producto),
	CASO(fmac_limpio_no_levanta_nada),
	CASO(fcnvds_que_desborda_al_bajar_a_simple_levanta_o),
	CASO(fcnvds_que_subdesborda_al_bajar_a_simple_levanta_u),
	CASO(float_limpia_cause_y_no_levanta_nada),
	CASO(fdiv_doble_entre_cero_levanta_z),
	CASO(fdiv_doble_cero_entre_cero_levanta_v),
	CASO(fmul_doble_que_desborda_levanta_o_e_i),
	CASO(fmul_doble_que_subdesborda_levanta_u_e_i),
	CASO(fadd_doble_de_infinitos_opuestos_levanta_v),
	CASO(fsqrt_doble_de_negativo_levanta_v),
	CASO(ftrc_doble_de_nan_levanta_v),
	CASO(fcmpgt_doble_con_nan_levanta_v),
	CASO(cause_se_rearma_y_flag_se_acumula),
	CASO(escribir_fpscr_limpia_flag),
	CASO(las_transferencias_no_tocan_cause),
	CASO(sin_enable_la_division_por_cero_no_atrapa),
	CASO(enable_z_atrapa_la_division_por_cero),
	CASO(la_trampa_no_agrega_nada_a_flag),
	CASO(un_enable_de_otra_causa_no_atrapa),
	CASO(enable_v_atrapa_la_operacion_invalida),
	CASO(enable_o_atrapa_el_desbordamiento),
	CASO(enable_u_atrapa_el_subdesbordamiento),
	CASO(enable_i_solo_atrapa_cuando_viene_con_o_o_con_u),
	CASO(la_excepcion_guarda_ssr_spc_y_sgr),
	CASO(fd_atrapa_una_instruccion_de_fpu),
	CASO(fd_no_atrapa_una_instruccion_entera),
	CASO(fd_atrapa_las_transferencias_de_fpul_y_fpscr),
	CASO(fd_en_la_ranura_de_retardo_da_el_codigo_de_ranura),
	CASO(sin_fd_la_ranura_de_retardo_corre_normal),
	CASO(la_vigilancia_sigue_a_fd_y_a_enable),
};

const dc_suite suite_fpu_excepciones = DEFINIR_SUITE("fpu-excepciones", casos);
