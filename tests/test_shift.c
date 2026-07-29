/****************************************************************************

	Pruebas de shift.c -- instrucciones de desplazamiento y rotacion

	Casos borde que cubre esta suite:

	  - ROTL/ROTR/ROTCL/ROTCR: el bit que sale por un extremo entra por el
		otro (rotacion) o por T (rotacion con acarreo). Se prueban los dos
		valores de T de entrada, porque ROTCL/ROTCR lo leen y lo escriben en
		la misma instruccion.
	  - SHAD/SHLD: los tres tramos del rango de Rm. Rm >= 0 desplaza a la
		izquierda; Rm < 0 con Rm[4:0] != 0 desplaza a la derecha
		(32 - Rm[4:0] posiciones); Rm < 0 con Rm[4:0] == 0 es el desplazamiento
		de 32, que el manual define como caso aparte: SHAD deja el signo
		replicado (0 o 0xFFFFFFFF) y SHLD deja cero.
	  - Rm = 0 en SHAD/SHLD: no es "no hacer nada" por casualidad, es el
		desplazamiento a la izquierda de cero posiciones.
	  - Rm = 32 (0x20): Rm[4:0] vale 0 y Rm es positivo, asi que tampoco
		desplaza.
	  - SHLL2/8/16 y SHLR2/8/16 no tocan T. Se verifica con T=1 de entrada,
		que es donde un bug se notaria.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* ------------------------------------------------------------------ ROTL */

static void rotl_saca_msb_por_t(void)
{
	arnes_reset();

	R(3) = 0x80000001;
	ejecutar(instr_n(0x4004, 3));	/* ROTL R3 */

	ESPERAR_U32(R(3), 0x00000003);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void rotl_sin_acarreo(void)
{
	arnes_reset();

	SR_T = 1;					/* T de entrada no participa en ROTL */
	R(0) = 0x40000000;
	ejecutar(instr_n(0x4004, 0));

	ESPERAR_U32(R(0), 0x80000000);
	ESPERAR_T(0);
}

/* ------------------------------------------------------------------ ROTR */

static void rotr_saca_lsb_por_t(void)
{
	arnes_reset();

	R(5) = 0x00000001;
	ejecutar(instr_n(0x4005, 5));	/* ROTR R5 */

	ESPERAR_U32(R(5), 0x80000000);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void rotr_sin_acarreo(void)
{
	arnes_reset();

	SR_T = 1;
	R(5) = 0x00000002;
	ejecutar(instr_n(0x4005, 5));

	ESPERAR_U32(R(5), 0x00000001);
	ESPERAR_T(0);
}

/* ----------------------------------------------------------------- ROTCL */

static void rotcl_mete_t_por_abajo(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0x80000000;
	ejecutar(instr_n(0x4024, 1));	/* ROTCL R1 */

	ESPERAR_U32(R(1), 0x00000001);	/* el T viejo entra por el bit 0 */
	ESPERAR_T(1);					/* el MSB viejo sale a T */
	ESPERAR_PC_SIGUIENTE();
}

static void rotcl_con_t_en_cero(void)
{
	arnes_reset();

	SR_T = 0;
	R(1) = 0xC0000000;
	ejecutar(instr_n(0x4024, 1));

	ESPERAR_U32(R(1), 0x80000000);
	ESPERAR_T(1);
}

/* ----------------------------------------------------------------- ROTCR */

static void rotcr_mete_t_por_arriba(void)
{
	arnes_reset();

	SR_T = 1;
	R(2) = 0x00000001;
	ejecutar(instr_n(0x4025, 2));	/* ROTCR R2 */

	ESPERAR_U32(R(2), 0x80000000);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void rotcr_con_t_en_cero(void)
{
	arnes_reset();

	SR_T = 0;
	R(2) = 0x00000003;
	ejecutar(instr_n(0x4025, 2));

	ESPERAR_U32(R(2), 0x00000001);
	ESPERAR_T(1);
}

/* ------------------------------------------------------------------ SHAD */

static void shad_izquierda(void)
{
	arnes_reset();

	R(1) = 0x00000001;	/* Rn */
	R(2) = 4;			/* Rm >= 0: desplaza a la izquierda */
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0x00000010);
	ESPERAR_PC_SIGUIENTE();
}

static void shad_derecha_conserva_signo(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = (DWORD) -4;	/* desplaza 4 a la derecha, aritmetico */
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0xF8000000);
}

static void shad_32_negativo_deja_todo_unos(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = (DWORD) -32;	/* Rm < 0 con Rm[4:0] == 0 */
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0xFFFFFFFF);
}

static void shad_32_positivo_deja_cero(void)
{
	arnes_reset();

	R(1) = 0x7FFFFFFF;
	R(2) = (DWORD) -32;
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0x00000000);
}

static void shad_con_rm_cero_no_desplaza(void)
{
	arnes_reset();

	R(1) = 0x12345678;
	R(2) = 0;
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0x12345678);
}

static void shad_con_rm_32_no_desplaza(void)
{
	arnes_reset();

	R(1) = 0x12345678;
	R(2) = 32;			/* positivo, Rm[4:0] == 0 */
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0x12345678);
}

static void shad_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0x80000000;
	R(2) = (DWORD) -1;
	ejecutar(instr_nm(0x400C, 1, 2));

	ESPERAR_U32(R(1), 0xC0000000);
	ESPERAR_T(1);
}

/* ------------------------------------------------------------------ SHAL */

/* SHAL y SHLL son la misma operacion con dos encodings distintos: desplazar a
   la izquierda con signo o sin signo da lo mismo. */
static void shal_saca_msb_a_t(void)
{
	arnes_reset();

	R(4) = 0x80000001;
	ejecutar(instr_n(0x4020, 4));	/* SHAL R4 */

	ESPERAR_U32(R(4), 0x00000002);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void shal_da_lo_mismo_que_shll(void)
{
	DWORD con_shal;

	arnes_reset();
	R(4) = 0x40000000;
	ejecutar(instr_n(0x4020, 4));
	con_shal = R(4);

	arnes_reset();
	R(4) = 0x40000000;
	ejecutar(instr_n(0x4000, 4));	/* SHLL R4 */

	ESPERAR_U32(con_shal, R(4));
}

/* ------------------------------------------------------------------ SHAR */

static void shar_baja_uno_y_conserva_signo(void)
{
	arnes_reset();

	R(4) = 0x80000001;
	ejecutar(instr_n(0x4021, 4));	/* SHAR R4 */

	ESPERAR_U32(R(4), 0xC0000000);
	ESPERAR_T(1);					/* el bit 0 sale a T */
	ESPERAR_PC_SIGUIENTE();
}

static void shar_positivo(void)
{
	arnes_reset();

	R(4) = 0x7FFFFFFE;
	ejecutar(instr_n(0x4021, 4));

	ESPERAR_U32(R(4), 0x3FFFFFFF);
	ESPERAR_T(0);
}

/* ------------------------------------------------------------------ SHLD */

static void shld_izquierda(void)
{
	arnes_reset();

	R(1) = 0x0FFFFFFF;
	R(2) = 4;
	ejecutar(instr_nm(0x400D, 1, 2));

	ESPERAR_U32(R(1), 0xFFFFFFF0);
	ESPERAR_PC_SIGUIENTE();
}

static void shld_derecha_es_logico(void)
{
	arnes_reset();

	R(1) = 0x80000000;	/* con SHAD daria 0xF8000000 */
	R(2) = (DWORD) -4;
	ejecutar(instr_nm(0x400D, 1, 2));

	ESPERAR_U32(R(1), 0x08000000);
}

static void shld_32_deja_cero(void)
{
	arnes_reset();

	R(1) = 0xFFFFFFFF;
	R(2) = (DWORD) -32;
	ejecutar(instr_nm(0x400D, 1, 2));

	ESPERAR_U32(R(1), 0x00000000);
}

static void shld_con_rm_cero_no_desplaza(void)
{
	arnes_reset();

	R(1) = 0x12345678;
	R(2) = 0;
	ejecutar(instr_nm(0x400D, 1, 2));

	ESPERAR_U32(R(1), 0x12345678);
}

/* ------------------------------------------------------- SHLL/SHLR de 1 */

static void shll_saca_msb_a_t(void)
{
	arnes_reset();

	R(7) = 0x80000001;
	ejecutar(instr_n(0x4000, 7));	/* SHLL R7 */

	ESPERAR_U32(R(7), 0x00000002);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void shlr_saca_lsb_a_t(void)
{
	arnes_reset();

	R(7) = 0x80000001;
	ejecutar(instr_n(0x4001, 7));	/* SHLR R7 */

	ESPERAR_U32(R(7), 0x40000000);	/* logico: no replica el signo */
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------ SHLL/SHLR de 2, 8 y 16 */

static void shll2_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(6) = 0xC0000001;
	ejecutar(instr_n(0x4008, 6));	/* SHLL2 R6 */

	ESPERAR_U32(R(6), 0x00000004);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void shlr2_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(6) = 0xC0000007;
	ejecutar(instr_n(0x4009, 6));	/* SHLR2 R6 */

	ESPERAR_U32(R(6), 0x30000001);
	ESPERAR_T(1);
}

static void shll8_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(6) = 0x00FF00FF;
	ejecutar(instr_n(0x4018, 6));	/* SHLL8 R6 */

	ESPERAR_U32(R(6), 0xFF00FF00);
	ESPERAR_T(1);
}

static void shlr8_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(6) = 0xFF00FF00;
	ejecutar(instr_n(0x4019, 6));	/* SHLR8 R6 */

	ESPERAR_U32(R(6), 0x00FF00FF);
	ESPERAR_T(1);
}

static void shll16_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(6) = 0x0000FFFF;
	ejecutar(instr_n(0x4028, 6));	/* SHLL16 R6 */

	ESPERAR_U32(R(6), 0xFFFF0000);
	ESPERAR_T(1);
}

static void shlr16_no_toca_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(6) = 0xFFFF0000;
	ejecutar(instr_n(0x4029, 6));	/* SHLR16 R6 */

	ESPERAR_U32(R(6), 0x0000FFFF);
	ESPERAR_T(1);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(rotl_saca_msb_por_t),
	CASO(rotl_sin_acarreo),
	CASO(rotr_saca_lsb_por_t),
	CASO(rotr_sin_acarreo),
	CASO(rotcl_mete_t_por_abajo),
	CASO(rotcl_con_t_en_cero),
	CASO(rotcr_mete_t_por_arriba),
	CASO(rotcr_con_t_en_cero),
	CASO(shad_izquierda),
	CASO(shad_derecha_conserva_signo),
	CASO(shad_32_negativo_deja_todo_unos),
	CASO(shad_32_positivo_deja_cero),
	CASO(shad_con_rm_cero_no_desplaza),
	CASO(shad_con_rm_32_no_desplaza),
	CASO(shad_no_toca_t),
	CASO(shal_saca_msb_a_t),
	CASO(shal_da_lo_mismo_que_shll),
	CASO(shar_baja_uno_y_conserva_signo),
	CASO(shar_positivo),
	CASO(shld_izquierda),
	CASO(shld_derecha_es_logico),
	CASO(shld_32_deja_cero),
	CASO(shld_con_rm_cero_no_desplaza),
	CASO(shll_saca_msb_a_t),
	CASO(shlr_saca_lsb_a_t),
	CASO(shll2_no_toca_t),
	CASO(shlr2_no_toca_t),
	CASO(shll8_no_toca_t),
	CASO(shlr8_no_toca_t),
	CASO(shll16_no_toca_t),
	CASO(shlr16_no_toca_t),
};

const dc_suite suite_shift = DEFINIR_SUITE("shift", casos);
