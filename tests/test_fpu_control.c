/****************************************************************************

	Pruebas de floatcontrol.c -- acceso a FPSCR y FPUL

	Casos borde que cubre esta suite:

	  - FPSCR solo tiene 22 bits utiles: tanto LDS como STS filtran con
		0x003FFFFF, asi que escribir 0xFFFFFFFF y leerlo de vuelta no devuelve
		lo mismo.
	  - Escribir el bit FR de FPSCR intercambia los dos bancos de 16 registros
		float. Es la unica forma de llegar al banco XF sin FRCHG.
	  - Escribir PR o SZ cambia la tabla de despacho: el mismo patron de bits
		pasa a significar otra instruccion. Aqui se verifica el bit; el efecto
		sobre el despacho esta en la suite de decodificacion.
	  - FPUL es un registro de 32 bits sin interpretacion: LDS y STS copian el
		patron de bits tal cual.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

#define MASCARA_FPSCR	0x003FFFFFu
#define BIT_SZ		0x00100000u
#define BIT_PR		0x00080000u
#define BIT_FR		0x00200000u

/* --------------------------------------------------------------- FPSCR */

static void lds_fpscr_filtra_los_bits_altos(void)
{
	arnes_reset();

	R(2) = 0xFFFFFFFF;
	ejecutar(instr_n(0x406A, 2));		/* LDS R2, FPSCR */

	ESPERAR_U32(FPSCR, MASCARA_FPSCR);
	ESPERAR_PC_SIGUIENTE();
}

static void sts_fpscr_filtra_los_bits_altos(void)
{
	arnes_reset();

	FPSCR = 0xFFFFFFFF;
	R(2) = 0;
	ejecutar(instr_n(0x006A, 2));		/* STS FPSCR, R2 */

	ESPERAR_U32(R(2), MASCARA_FPSCR);
	ESPERAR_PC_SIGUIENTE();
}

static void lds_fpscr_prende_sz(void)
{
	arnes_reset();

	R(2) = BIT_SZ;
	ejecutar(instr_n(0x406A, 2));

	ESPERAR_U32(FPSCR_SZ_BIT, 1);
	ESPERAR_U32(FPSCR_PR_BIT, 0);
}

static void lds_fpscr_prende_pr(void)
{
	arnes_reset();

	R(2) = BIT_PR;
	ejecutar(instr_n(0x406A, 2));

	ESPERAR_U32(FPSCR_PR_BIT, 1);
	ESPERAR_U32(FPSCR_SZ_BIT, 0);
}

static void lds_fpscr_con_fr_cambia_de_banco(void)
{
	arnes_reset();

	FR(0) = 1.5f;						/* banco activo */
	XF(0) = 2.5f;						/* banco alternativo */

	R(2) = BIT_FR;
	ejecutar(instr_n(0x406A, 2));		/* LDS R2, FPSCR */

	ESPERAR_F32(FR(0), 2.5f);
	ESPERAR_F32(XF(0), 1.5f);
}

static void ldsl_fpscr_avanza_el_puntero(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, BIT_SZ);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4066, 2));		/* LDS.L @R2+, FPSCR */

	ESPERAR_U32(FPSCR, BIT_SZ);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
	ESPERAR_PC_SIGUIENTE();
}

static void stsl_fpscr_retrocede_el_puntero(void)
{
	arnes_reset();

	FPSCR = 0x000C0001;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4062, 2));		/* STS.L FPSCR, @-R2 */

	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x000C0001);
	ESPERAR_PC_SIGUIENTE();
}

static void fpscr_guardado_y_restaurado_es_reversible(void)
{
	arnes_reset();

	UpdateFPSCR(BIT_PR | 0x00000001);
	R(15) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4062, 15));		/* STS.L FPSCR, @-R15 */
	UpdateFPSCR(BIT_SZ);				/* alguien lo cambia */
	ejecutar(instr_n(0x4066, 15));		/* LDS.L @R15+, FPSCR */

	ESPERAR_U32(FPSCR, BIT_PR | 0x00000001);
	ESPERAR_U32(R(15), PRUEBA_DATOS);
}

/* ---------------------------------------------------------------- FPUL */

static void lds_fpul_copia_los_bits(void)
{
	arnes_reset();

	R(2) = 0xDEADBEEF;
	ejecutar(instr_n(0x405A, 2));		/* LDS R2, FPUL */

	ESPERAR_U32(FPUL, 0xDEADBEEF);
	ESPERAR_PC_SIGUIENTE();
}

static void sts_fpul_copia_los_bits(void)
{
	arnes_reset();

	FPUL = 0xDEADBEEF;
	ejecutar(instr_n(0x005A, 2));		/* STS FPUL, R2 */

	ESPERAR_U32(R(2), 0xDEADBEEF);
	ESPERAR_PC_SIGUIENTE();
}

static void ldsl_fpul_avanza_el_puntero(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0xDEADBEEF);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4056, 2));		/* LDS.L @R2+, FPUL */

	ESPERAR_U32(FPUL, 0xDEADBEEF);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void stsl_fpul_retrocede_el_puntero(void)
{
	arnes_reset();

	FPUL = 0xDEADBEEF;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4052, 2));		/* STS.L FPUL, @-R2 */

	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0xDEADBEEF);
}

/* FPUL es el unico puente entre los registros generales y los float: el par
   LDS/FSTS mueve un patron de bits de un lado al otro sin convertirlo. */
static void fpul_es_el_puente_entre_gpr_y_fpu(void)
{
	arnes_reset();

	R(2) = 0x40490FDB;					/* pi como float */
	ejecutar(instr_n(0x405A, 2));		/* LDS R2, FPUL */
	ejecutar(instr_n(0xF00D, 5));		/* FSTS FPUL, FR5 */

	ESPERAR_F32_APROX(FR(5), 3.14159265f, 1e-6f);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(lds_fpscr_filtra_los_bits_altos),
	CASO(sts_fpscr_filtra_los_bits_altos),
	CASO(lds_fpscr_prende_sz),
	CASO(lds_fpscr_prende_pr),
	CASO(lds_fpscr_con_fr_cambia_de_banco),
	CASO(ldsl_fpscr_avanza_el_puntero),
	CASO(stsl_fpscr_retrocede_el_puntero),
	CASO(fpscr_guardado_y_restaurado_es_reversible),
	CASO(lds_fpul_copia_los_bits),
	CASO(sts_fpul_copia_los_bits),
	CASO(ldsl_fpul_avanza_el_puntero),
	CASO(stsl_fpul_retrocede_el_puntero),
	CASO(fpul_es_el_puente_entre_gpr_y_fpu),
};

const dc_suite suite_fpu_control = DEFINIR_SUITE("fpu-control", casos);
