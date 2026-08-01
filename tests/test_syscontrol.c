/****************************************************************************

	Pruebas de syscontrol.c -- control del sistema

	Casos borde que cubre esta suite:

	  - LDC Rm,SR filtra el valor con la mascara de bits validos del SH-4
		(0x700083F3): los bits reservados no se pueden escribir.
	  - Cambiar RB en SR intercambia R0-R7 con el banco. Se prueba que el
		intercambio ocurra y que sea el juego completo.
	  - Las variantes .L con @Rm+ / @-Rn mueven el puntero ademas del dato, y
		lo hacen en direcciones opuestas.
	  - TRAPA arma la entrada a excepcion completa: SPC, SSR, TRA (el
		inmediato corrido 2 bits), EXPEVT = 0x160, MD/RB/BL en 1 y salto a
		VBR + 0x100.
	  - RTE restaura SR desde SSR antes de la ranura de retardo, y vuelve a
		SPC despues.
	  - PREF sobre una direccion de store queue (0xE0000000-0xEFFFFFFC) vuelca
		los 32 bytes de la cola. Que cola y a que direccion depende del bit 5
		de Rn y de QACR0/QACR1; cuando el destino cae en la FIFO del TA,
		ademas despacha segun el tipo de parametro.
	  - PREF sobre una direccion normal no hace nada: es solo una pista de
		cache.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* -------------------------------------------------------------- T y NOP */

static void clrt_apaga_t(void)
{
	arnes_reset();

	SR_T = 1;
	ejecutar(0x0008);					/* CLRT */

	ESPERAR_T(0);
	ESPERAR_PC_SIGUIENTE();
}

static void sett_prende_t(void)
{
	arnes_reset();

	SR_T = 0;
	ejecutar(0x0018);					/* SETT */

	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void nop_solo_avanza(void)
{
	arnes_reset();

	SR = 0x12345678 & 0x700083F3;
	R(1) = 0xDEADBEEF;
	ejecutar(0x0009);					/* NOP */

	ESPERAR_U32(R(1), 0xDEADBEEF);
	ESPERAR_PC_SIGUIENTE();
}

static void sleep_solo_avanza(void)
{
	arnes_reset();

	ejecutar(0x001B);					/* SLEEP */

	ESPERAR_PC_SIGUIENTE();
}

static void ocbi_solo_avanza(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	escribir_l(PRUEBA_DATOS, 0x12345678);

	ejecutar(instr_n(0x0093, 1));		/* OCBI @R1 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

static void ocbwb_solo_avanza(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	ejecutar(instr_n(0x00B3, 1));		/* OCBWB @R1 */

	ESPERAR_PC_SIGUIENTE();
}

static void ocbp_solo_avanza(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	escribir_l(PRUEBA_DATOS, 0x12345678);

	ejecutar(instr_n(0x00A3, 1));		/* OCBP @R1 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

/* LDTLB carga la entrada de la UTLB que apunta MMUCR.URC desde PTEH, PTEL y
   PTEA. No toca registros generales. El detalle del formato se prueba en la
   suite mmu; aca alcanza con ver que la instruccion llega a la TLB. */
static void ldtlb_carga_la_tlb(void)
{
	arnes_reset();
	mmu_reset();

	*PTEH  = 0x12345000 | 0x2A;			/* VPN y ASID */
	*PTEL  = 0x0C001000 | MMU_BIT_V;	/* PPN y valida */
	*PTEA  = 0;
	*MMUCR = 7ul << 10;					/* URC = 7 */

	R(1) = 0x12345678;
	ejecutar(0x0038);					/* LDTLB */

	ESPERAR_U32(mmu_utlb_dir[7] & 0xFFFFFC00, 0x12345000);
	ESPERAR_U32(mmu_utlb_dir[7] & 0xFF, 0x2A);
	ESPERAR_U32(mmu_utlb_dir[7] & MMU_BIT_V, MMU_BIT_V);
	ESPERAR_U32(mmu_utlb_dat1[7] & 0x1FFFFC00, 0x0C001000);

	/* Y no eligio otra entrada. */
	ESPERAR_U32(mmu_utlb_dir[6], 0);

	ESPERAR_U32(R(1), 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------------ S y MAC */

static void clrmac_borra_los_dos_registros(void)
{
	arnes_reset();

	MACH = 0x12345678;
	MACL = 0x9ABCDEF0;

	ejecutar(0x0028);					/* CLRMAC */

	ESPERAR_U32(MACH, 0);
	ESPERAR_U32(MACL, 0);
	ESPERAR_PC_SIGUIENTE();
}

static void clrs_apaga_s(void)
{
	arnes_reset();

	SR_S = 1;
	SR_T = 1;
	ejecutar(0x0048);					/* CLRS */

	ESPERAR_U32(SR_S, 0);
	ESPERAR_T(1);						/* no toca T */
	ESPERAR_PC_SIGUIENTE();
}

static void sets_prende_s(void)
{
	arnes_reset();

	SR_S = 0;
	ejecutar(0x0058);					/* SETS */

	ESPERAR_U32(SR_S, 1);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------------- LDC / STC */

static void ldc_sr_filtra_los_bits_reservados(void)
{
	arnes_reset();

	R(8) = 0xFFFFFFFF;
	ejecutar(instr_n(0x400E, 8));		/* LDC R8, SR */

	ESPERAR_U32(SR, 0x700083F3);
	ESPERAR_PC_SIGUIENTE();
}

static void ldc_sr_cambia_el_banco_de_registros(void)
{
	arnes_reset();

	R(0)  = 0x0000AAAA;
	R(7)  = 0x0007AAAA;
	R(16) = 0x0000BBBB;					/* R0_BANK */
	R(23) = 0x0007BBBB;					/* R7_BANK */
	R(8)  = 0x20000000;					/* solo RB */

	ejecutar(instr_n(0x400E, 8));		/* LDC R8, SR */

	ESPERAR_U32(SR_RB, 1);
	ESPERAR_U32(R(0),  0x0000BBBB);
	ESPERAR_U32(R(7),  0x0007BBBB);
	ESPERAR_U32(R(16), 0x0000AAAA);
	ESPERAR_U32(R(23), 0x0007AAAA);
}

static void stc_sr_copia_el_registro_completo(void)
{
	arnes_reset();

	SR = 0x400000F1;
	ejecutar(instr_n(0x0002, 9));		/* STC SR, R9 */

	ESPERAR_U32(R(9), 0x400000F1);
	ESPERAR_PC_SIGUIENTE();
}

static void ldc_vbr(void)
{
	arnes_reset();

	R(2) = 0x8C030000;
	ejecutar(instr_n(0x402E, 2));		/* LDC R2, VBR */

	ESPERAR_U32(VBR, 0x8C030000);
	ESPERAR_PC_SIGUIENTE();
}

static void stc_vbr(void)
{
	arnes_reset();

	VBR = 0x8C030000;
	ejecutar(instr_n(0x0022, 2));		/* STC VBR, R2 */

	ESPERAR_U32(R(2), 0x8C030000);
}

static void ldc_ssr(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_n(0x403E, 2));		/* LDC R2, SSR */

	ESPERAR_U32(SSR, 0x12345678);
}

static void stc_ssr(void)
{
	arnes_reset();

	SSR = 0x12345678;
	ejecutar(instr_n(0x0032, 2));		/* STC SSR, R2 */

	ESPERAR_U32(R(2), 0x12345678);
}

static void stc_gbr(void)
{
	arnes_reset();

	GBR = 0x8C040000;
	ejecutar(instr_n(0x0012, 2));		/* STC GBR, R2 */

	ESPERAR_U32(R(2), 0x8C040000);
}

static void stc_dbr(void)
{
	arnes_reset();

	DBR = 0x12345678;
	ejecutar(instr_n(0x00FA, 2));		/* STC DBR, R2 */

	ESPERAR_U32(R(2), 0x12345678);
}

/* LDC Rm,DBR mueve un registro a DBR. La variante que lee de memoria es
   LDC.L @Rm+,DBR, que es otra instruccion. */
static void ldc_dbr_copia_el_registro(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	escribir_l(PRUEBA_DATOS, 0xDEADBEEF);

	ejecutar(instr_n(0x40FA, 2));		/* LDC R2, DBR */

	ESPERAR_U32(DBR, 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

static void ldc_gbr(void)
{
	arnes_reset();

	R(2) = 0x8C040000;
	ejecutar(instr_n(0x401E, 2));		/* LDC R2, GBR */

	ESPERAR_U32(GBR, 0x8C040000);
	ESPERAR_PC_SIGUIENTE();
}

static void ldc_spc(void)
{
	arnes_reset();

	R(2) = 0x8C050000;
	ejecutar(instr_n(0x404E, 2));		/* LDC R2, SPC */

	ESPERAR_U32(SPC, 0x8C050000);
	ESPERAR_PC_SIGUIENTE();
}

static void ldc_sgr(void)
{
	arnes_reset();

	R(2) = 0x8C0FFFF0;
	ejecutar(instr_n(0x403A, 2));		/* LDC R2, SGR */

	ESPERAR_U32(SGR, 0x8C0FFFF0);
	ESPERAR_PC_SIGUIENTE();
}

static void stc_spc(void)
{
	arnes_reset();

	SPC = 0x8C050000;
	ejecutar(instr_n(0x0042, 2));		/* STC SPC, R2 */

	ESPERAR_U32(R(2), 0x8C050000);
	ESPERAR_PC_SIGUIENTE();
}

static void stc_sgr(void)
{
	arnes_reset();

	SGR = 0x8C0FFFF0;
	ejecutar(instr_n(0x003A, 2));		/* STC SGR, R2 */

	ESPERAR_U32(R(2), 0x8C0FFFF0);
	ESPERAR_PC_SIGUIENTE();
}

static void ldc_rn_bank(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_nm(0x408E, 2, 3));	/* LDC R2, R3_BANK */

	ESPERAR_U32(R(16 + 3), 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

static void stc_rm_bank(void)
{
	arnes_reset();

	R(16 + 5) = 0x12345678;
	ejecutar(instr_nm(0x0082, 2, 5));	/* STC R5_BANK, R2 */

	ESPERAR_U32(R(2), 0x12345678);
}

/* ----------------------------------------------------------- LDC.L / STC.L */

static void ldcl_sr_avanza_el_puntero(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x400000F1);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4007, 2));		/* LDC.L @R2+, SR */

	ESPERAR_U32(SR, 0x400000F1);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
	ESPERAR_PC_SIGUIENTE();
}

static void ldcl_gbr(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x8C040000);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4017, 2));		/* LDC.L @R2+, GBR */

	ESPERAR_U32(GBR, 0x8C040000);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldcl_vbr(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x8C030000);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4027, 2));		/* LDC.L @R2+, VBR */

	ESPERAR_U32(VBR, 0x8C030000);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldcl_ssr(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x12345678);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4037, 2));		/* LDC.L @R2+, SSR */

	ESPERAR_U32(SSR, 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldcl_spc(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x8C050000);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4047, 2));		/* LDC.L @R2+, SPC */

	ESPERAR_U32(SPC, 0x8C050000);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldcl_sgr(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x12345678);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4036, 2));		/* LDC.L @R2+, SGR */

	ESPERAR_U32(SGR, 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldcl_dbr(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x12345678);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x40F6, 2));		/* LDC.L @R2+, DBR */

	ESPERAR_U32(DBR, 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldcl_rn_bank(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x12345678);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x4087, 2, 3));	/* LDC.L @R2+, R3_BANK */

	ESPERAR_U32(R(16 + 3), 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void stcl_sr_retrocede_el_puntero(void)
{
	arnes_reset();

	SR = 0x400000F1;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4003, 2));		/* STC.L SR, @-R2 */

	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x400000F1);
	ESPERAR_PC_SIGUIENTE();
}

static void stcl_gbr(void)
{
	arnes_reset();

	GBR = 0x8C040000;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4013, 2));		/* STC.L GBR, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x8C040000);
	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
}

static void stcl_vbr(void)
{
	arnes_reset();

	VBR = 0x8C030000;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4023, 2));		/* STC.L VBR, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x8C030000);
}

static void stcl_ssr(void)
{
	arnes_reset();

	SSR = 0x12345678;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4033, 2));		/* STC.L SSR, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x12345678);
}

static void stcl_spc(void)
{
	arnes_reset();

	SPC = 0x8C050000;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4043, 2));		/* STC.L SPC, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x8C050000);
}

static void stcl_sgr(void)
{
	arnes_reset();

	SGR = 0x12345678;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4032, 2));		/* STC.L SGR, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x12345678);
}

static void stcl_dbr(void)
{
	arnes_reset();

	DBR = 0x12345678;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x40F2, 2));		/* STC.L DBR, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x12345678);
}

static void stcl_rm_bank(void)
{
	arnes_reset();

	R(16 + 5) = 0x12345678;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x4083, 2, 5));	/* STC.L R5_BANK, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
}

/* ------------------------------------------------------------- LDS / STS */

static void lds_mach(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_n(0x400A, 2));		/* LDS R2, MACH */

	ESPERAR_U32(MACH, 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

static void lds_macl(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_n(0x401A, 2));		/* LDS R2, MACL */

	ESPERAR_U32(MACL, 0x12345678);
}

static void lds_pr(void)
{
	arnes_reset();

	R(2) = 0x8C050000;
	ejecutar(instr_n(0x402A, 2));		/* LDS R2, PR */

	ESPERAR_U32(PR, 0x8C050000);
}

static void sts_mach(void)
{
	arnes_reset();

	MACH = 0x12345678;
	ejecutar(instr_n(0x000A, 2));		/* STS MACH, R2 */

	ESPERAR_U32(R(2), 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

static void sts_macl(void)
{
	arnes_reset();

	MACL = 0x12345678;
	ejecutar(instr_n(0x001A, 2));		/* STS MACL, R2 */

	ESPERAR_U32(R(2), 0x12345678);
}

static void sts_pr(void)
{
	arnes_reset();

	PR = 0x8C050000;
	ejecutar(instr_n(0x002A, 2));		/* STS PR, R2 */

	ESPERAR_U32(R(2), 0x8C050000);
}

static void ldsl_mach(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x12345678);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4006, 2));		/* LDS.L @R2+, MACH */

	ESPERAR_U32(MACH, 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
	ESPERAR_PC_SIGUIENTE();
}

static void ldsl_macl(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x12345678);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4016, 2));		/* LDS.L @R2+, MACL */

	ESPERAR_U32(MACL, 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void ldsl_pr(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x8C050000);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4026, 2));		/* LDS.L @R2+, PR */

	ESPERAR_U32(PR, 0x8C050000);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void stsl_mach(void)
{
	arnes_reset();

	MACH = 0x12345678;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4002, 2));		/* STS.L MACH, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x12345678);
	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
	ESPERAR_PC_SIGUIENTE();
}

static void stsl_macl(void)
{
	arnes_reset();

	MACL = 0x12345678;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4012, 2));		/* STS.L MACL, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x12345678);
}

static void stsl_pr(void)
{
	arnes_reset();

	PR = 0x8C050000;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4022, 2));		/* STS.L PR, @-R2 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x8C050000);
	ESPERAR_U32(R(2), PRUEBA_DATOS - 4);
}

/* Guardar y restaurar PR alrededor de una llamada es el uso real de estas
   dos: el par tiene que ser reversible. */
static void stsl_y_ldsl_pr_son_reversibles(void)
{
	arnes_reset();

	PR = 0x8C050000;
	R(15) = PRUEBA_DATOS;

	ejecutar(instr_n(0x4022, 15));		/* STS.L PR, @-R15 */
	PR = 0xDEADBEEF;					/* la subrutina lo pisa */
	ejecutar(instr_n(0x4026, 15));		/* LDS.L @R15+, PR */

	ESPERAR_U32(PR, 0x8C050000);
	ESPERAR_U32(R(15), PRUEBA_DATOS);
}

/* ------------------------------------------------------------ TRAPA / RTE */

static void trapa_arma_la_entrada_a_excepcion(void)
{
	arnes_reset();

	VBR = 0x8C030000;
	SR = 0x00000001;					/* solo T */

	ejecutar(instr_i(0xC300, 5));		/* TRAPA #5 */

	ESPERAR_U32(SPC, PRUEBA_PC + 2);
	ESPERAR_U32(SSR, 0x00000001);
	ESPERAR_U32(*TRA, 5 << 2);
	ESPERAR_U32(*EXPEVT, 0x160);
	ESPERAR_U32(SR_MD, 1);
	ESPERAR_U32(SR_RB, 1);
	ESPERAR_U32(SR_BL, 1);
	ESPERAR_U32(PC, 0x8C030000 + 0x100);
}

static void trapa_cambia_al_banco_de_excepcion(void)
{
	arnes_reset();

	VBR = 0x8C030000;
	R(0)  = 0x0000AAAA;
	R(16) = 0x0000BBBB;

	ejecutar(instr_i(0xC300, 0));		/* TRAPA #0 */

	ESPERAR_U32(R(0),  0x0000BBBB);
	ESPERAR_U32(R(16), 0x0000AAAA);
}

/*
	Y el gemelo, que es el que faltaba: con RB **ya** en 1 la entrada no cambia
	de banco. RB se pone a 1 igual, pero poner un bit que ya estaba no mueve
	nada -- el manual describe el banco por el valor de RB, no por la escritura.

	Es el caso de una excepcion tomada desde dentro de otra, que es lo que hace
	cualquier manejador que baje BL para permitir anidamiento. dcemu
	intercambiaba siempre, asi que el manejador anidado veia el banco del codigo
	normal y el interrumpido volvia del RTE con R0-R7 del otro banco. El RTE no
	lo deshacia, porque ese camino si compara y SSR.RB == SR.RB.

	Lo destapo Virtua Tennis: perdia el indice de una tabla de callbacks a
	traves de una interrupcion anidada, llamaba por un puntero nulo, caia en la
	direccion 0 --que es el boot ROM-- y terminaba ejecutando el bloque bajo del
	sistema hasta toparse con un TRAPA.
*/
static void trapa_desde_el_banco_1_no_vuelve_a_cambiar(void)
{
	arnes_reset();

	VBR = 0x8C030000;
	SR_MD = 1;
	SR_RB = 1;							/* ya estamos en el banco de excepcion */
	core.context.banco_activo = 1;

	R(0)  = 0x0000AAAA;
	R(16) = 0x0000BBBB;

	ejecutar(instr_i(0xC300, 0));		/* TRAPA #0 */

	ESPERAR_U32(SR_RB, 1);
	ESPERAR_U32(R(0),  0x0000AAAA);		/* sin intercambiar */
	ESPERAR_U32(R(16), 0x0000BBBB);
	ESPERAR_U32(core.context.banco_activo, 1);
}

/* La secuencia de excepcion del SH-4 guarda R15 en SGR. */
static void trapa_guarda_r15_en_sgr(void)
{
	arnes_reset();

	VBR = 0x8C030000;
	R(15) = 0x8C0FFFF0;

	ejecutar(instr_i(0xC300, 0));

	ESPERAR_U32(SGR, 0x8C0FFFF0);
}

static void rte_vuelve_a_spc_con_ranura_de_retardo(void)
{
	arnes_reset();

	SSR = 0x40000001;					/* MD y T, sin tocar RB */
	SPC = PRUEBA_DATOS;
	poner_instr(PRUEBA_PC + 2, 0xE155);	/* MOV #0x55, R1 */

	ejecutar(0x002B);					/* RTE */

	ESPERAR_U32(PC, PRUEBA_DATOS);
	ESPERAR_U32(SR, 0x40000001);
	ESPERAR_U32(R(1), 0x55);			/* la ranura se ejecuto */
	ESPERAR(inside_int == false);		/* la excepcion quedo cerrada */
}

/* SR se restaura antes de la ranura de retardo, asi que un STC SR ahi adentro
   ya ve el SR nuevo. */
static void rte_restaura_sr_antes_de_la_ranura(void)
{
	arnes_reset();

	SSR = 0x40000001;
	SPC = PRUEBA_DATOS;
	poner_instr(PRUEBA_PC + 2, instr_n(0x0002, 9));	/* STC SR, R9 */

	ejecutar(0x002B);

	ESPERAR_U32(R(9), 0x40000001);
}

/* ------------------------------------------------------------------ PREF */

static void pref_fuera_de_la_store_queue_no_hace_nada(void)
{
	arnes_reset();

	SQ0[0] = 0x11111111;
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_n(0x0083, 1));		/* PREF @R1 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0);
	ESPERAR_PC_SIGUIENTE();
}

static void pref_vuelca_la_store_queue_0(void)
{
	int i;

	arnes_reset();

	/* QACR0 aporta los bits 26-28 del destino: 0x0C -> 0x0C000000, que es la
	   RAM del sistema (el mismo bloque que se ve desde 0x8C). */
	*QACR0 = 0x0C;

	for (i = 0; i < 8; i++)
		SQ0[i] = 0x11111100 + i;

	R(1) = 0xE0000000 | (PRUEBA_DATOS & 0x03FFFFC0);

	ejecutar(instr_n(0x0083, 1));		/* PREF @R1 */

	for (i = 0; i < 8; i++)
		ESPERAR_U32(leer_l(PRUEBA_DATOS + i * 4), 0x11111100 + i);

	ESPERAR_PC_SIGUIENTE();
}

static void pref_elige_la_store_queue_1_por_el_bit_5(void)
{
	int i;

	arnes_reset();

	*QACR0 = 0x0C;
	*QACR1 = 0x0C;

	for (i = 0; i < 8; i++)
	{
		SQ0[i] = 0xAAAAAA00 + i;
		SQ1[i] = 0xBBBBBB00 + i;
	}

	R(1) = 0xE0000020 | (PRUEBA_DATOS & 0x03FFFFC0);

	ejecutar(instr_n(0x0083, 1));

	/* La cola 1 se vuelca 32 bytes mas arriba, y la 0 no se toca. */
	for (i = 0; i < 8; i++)
		ESPERAR_U32(leer_l(PRUEBA_DATOS + 0x20 + i * 4), 0xBBBBBB00 + i);

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0);
}

/* Camino completo: el programa llena la cola con MOV.L a 0xE0000000 y despues
   la vuelca con PREF. */
static void escribir_en_la_cola_y_volcarla(void)
{
	arnes_reset();

	*QACR0 = 0x0C;

	R(1) = 0xE0000000;
	R(2) = 0x12345678;
	ejecutar(instr_nm(0x2002, 1, 2));	/* MOV.L R2, @R1 */

	R(1) = 0xE0000004;
	R(2) = 0x9ABCDEF0;
	ejecutar(instr_nm(0x2002, 1, 2));

	ESPERAR_U32(SQ0[0], 0x12345678);
	ESPERAR_U32(SQ0[1], 0x9ABCDEF0);

	R(1) = 0xE0000000 | (PRUEBA_DATOS & 0x03FFFFC0);
	ejecutar(instr_n(0x0083, 1));

	ESPERAR_U32(leer_l(PRUEBA_DATOS + 0), 0x12345678);
	ESPERAR_U32(leer_l(PRUEBA_DATOS + 4), 0x9ABCDEF0);
}

/* Cuando el destino cae en la FIFO del TA (0x10000000), PREF ademas mira los
   3 bits altos de la primera palabra y despacha al manejador que corresponde.
   Los dobles de graficos.c cuentan las llamadas. */
static void pref_despacha_a_la_fifo_del_ta(int para_type)
{
	arnes_reset();

	*QACR0 = 0x10;						/* 0x10 >> 2 = 4 -> bit 28 */

	SQ0[0] = (DWORD) para_type << 29;

	R(1) = 0xE0000000;
	ejecutar(instr_n(0x0083, 1));
}

static void pref_ta_lista_terminada(void)
{
	pref_despacha_a_la_fifo_del_ta(0);

	ESPERAR_U32(dobles_ta_list_end, 1);
	ESPERAR_U32(dobles_vertex_handler, 0);
}

static void pref_ta_recorte_de_usuario(void)
{
	pref_despacha_a_la_fifo_del_ta(1);

	ESPERAR_U32(dobles_user_clip, 1);
}

static void pref_ta_lista_de_objetos(void)
{
	pref_despacha_a_la_fifo_del_ta(2);

	ESPERAR_U32(dobles_object_list_set, 1);
}

static void pref_ta_modificador_de_poligono(void)
{
	pref_despacha_a_la_fifo_del_ta(4);

	ESPERAR_U32(dobles_poly_modifier, 1);
}

static void pref_ta_vertice(void)
{
	pref_despacha_a_la_fifo_del_ta(7);

	ESPERAR_U32(dobles_vertex_handler, 1);
}

static void pref_ta_tipo_reservado_no_despacha(void)
{
	pref_despacha_a_la_fifo_del_ta(3);

	ESPERAR_U32(dobles_ta_list_end, 0);
	ESPERAR_U32(dobles_user_clip, 0);
	ESPERAR_U32(dobles_object_list_set, 0);
	ESPERAR_U32(dobles_poly_modifier, 0);
	ESPERAR_U32(dobles_vertex_handler, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(clrt_apaga_t),
	CASO(sett_prende_t),
	CASO(nop_solo_avanza),
	CASO(sleep_solo_avanza),
	CASO(ocbi_solo_avanza),
	CASO(ocbwb_solo_avanza),
	CASO(ocbp_solo_avanza),
	CASO(ldtlb_carga_la_tlb),
	CASO(clrmac_borra_los_dos_registros),
	CASO(clrs_apaga_s),
	CASO(sets_prende_s),
	CASO(ldc_sr_filtra_los_bits_reservados),
	CASO(ldc_sr_cambia_el_banco_de_registros),
	CASO(stc_sr_copia_el_registro_completo),
	CASO(ldc_vbr),
	CASO(stc_vbr),
	CASO(ldc_ssr),
	CASO(stc_ssr),
	CASO(stc_gbr),
	CASO(stc_dbr),
	CASO(ldc_dbr_copia_el_registro),
	CASO(ldc_gbr),
	CASO(ldc_spc),
	CASO(ldc_sgr),
	CASO(stc_spc),
	CASO(stc_sgr),
	CASO(ldc_rn_bank),
	CASO(stc_rm_bank),
	CASO(ldcl_sr_avanza_el_puntero),
	CASO(ldcl_gbr),
	CASO(ldcl_vbr),
	CASO(ldcl_ssr),
	CASO(ldcl_spc),
	CASO(ldcl_sgr),
	CASO(ldcl_dbr),
	CASO(ldcl_rn_bank),
	CASO(stcl_sr_retrocede_el_puntero),
	CASO(stcl_gbr),
	CASO(stcl_vbr),
	CASO(stcl_ssr),
	CASO(stcl_spc),
	CASO(stcl_sgr),
	CASO(stcl_dbr),
	CASO(stcl_rm_bank),
	CASO(lds_mach),
	CASO(lds_macl),
	CASO(lds_pr),
	CASO(sts_mach),
	CASO(sts_macl),
	CASO(sts_pr),
	CASO(ldsl_mach),
	CASO(ldsl_macl),
	CASO(ldsl_pr),
	CASO(stsl_mach),
	CASO(stsl_macl),
	CASO(stsl_pr),
	CASO(stsl_y_ldsl_pr_son_reversibles),
	CASO(trapa_arma_la_entrada_a_excepcion),
	CASO(trapa_cambia_al_banco_de_excepcion),
	CASO(trapa_desde_el_banco_1_no_vuelve_a_cambiar),
	CASO(trapa_guarda_r15_en_sgr),
	CASO(rte_vuelve_a_spc_con_ranura_de_retardo),
	CASO(rte_restaura_sr_antes_de_la_ranura),
	CASO(pref_fuera_de_la_store_queue_no_hace_nada),
	CASO(pref_vuelca_la_store_queue_0),
	CASO(pref_elige_la_store_queue_1_por_el_bit_5),
	CASO(escribir_en_la_cola_y_volcarla),
	CASO(pref_ta_lista_terminada),
	CASO(pref_ta_recorte_de_usuario),
	CASO(pref_ta_lista_de_objetos),
	CASO(pref_ta_modificador_de_poligono),
	CASO(pref_ta_vertice),
	CASO(pref_ta_tipo_reservado_no_despacha),
};

const dc_suite suite_syscontrol = DEFINIR_SUITE("syscontrol", casos);
