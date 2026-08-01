/****************************************************************************

	Pruebas de mov.c -- transferencia de datos

	Casos borde que cubre esta suite:

	  - La extension de signo es lo que separa a MOV.B/MOV.W de MOV.L: cargar
		un byte 0x80 tiene que dejar 0xFFFFFF80 en el registro. Cada carga de
		byte y de word se prueba con el bit alto prendido.
	  - MOV #imm tambien extiende signo (0xFF es -1), al reves que los
		inmediatos de AND/OR/XOR/TST.
	  - Los modos @Rm+ y @-Rn modifican el registro puntero: se verifica el
		valor final del puntero, no solo el dato.
	  - @Rm+ con n == m: el manual dice que gana el dato leido y el incremento
		se descarta.
	  - Las cargas relativas a PC suman 4 (el pipeline) y, en el caso de
		MOV.L, alinean PC a 4 antes de sumar. Se prueba con PC en base+2, que
		es donde el enmascarado se nota.
	  - Los desplazamientos escalan segun el tamano: disp*1 en byte, disp*2 en
		word y disp*4 en long.
	  - SWAP.B intercambia los dos bytes bajos y deja los altos; SWAP.W
		intercambia las dos mitades; XTRCT saca los 32 bits del medio del par
		Rm:Rn.
	  - La zona de la direccion elige el handler de memoria: se prueba una
		escritura a RAM de video y otra a una zona sin mapear.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* ------------------------------------------------------------ inmediatos */

static void mov_inmediato_extiende_signo(void)
{
	arnes_reset();

	ejecutar(instr_ni(0xE000, 3, 0xFF));	/* MOV #-1, R3 */

	ESPERAR_U32(R(3), 0xFFFFFFFF);
	ESPERAR_PC_SIGUIENTE();
}

static void mov_inmediato_positivo(void)
{
	arnes_reset();

	ejecutar(instr_ni(0xE000, 3, 0x7F));	/* MOV #127, R3 */

	ESPERAR_U32(R(3), 0x0000007F);
}

static void mov_rm_rn(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_nm(0x6003, 1, 2));		/* MOV R2, R1 */

	ESPERAR_U32(R(1), 0x12345678);
	ESPERAR_U32(R(2), 0x12345678);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------- relativas a PC */

static void movw_relativo_a_pc_extiende_signo(void)
{
	arnes_reset();

	/* direccion = PC + 4 + disp*2 */
	escribir_w(PRUEBA_PC + 4 + 2 * 2, 0x8000);

	ejecutar(instr_ni(0x9000, 5, 2));		/* MOV.W @(2, PC), R5 */

	ESPERAR_U32(R(5), 0xFFFF8000);
	ESPERAR_PC_SIGUIENTE();
}

static void movl_relativo_a_pc(void)
{
	arnes_reset();

	/* direccion = (PC & ~3) + 4 + disp*4 */
	escribir_l(PRUEBA_PC + 4 + 1 * 4, 0xDEADBEEF);

	ejecutar(instr_ni(0xD000, 5, 1));		/* MOV.L @(1, PC), R5 */

	ESPERAR_U32(R(5), 0xDEADBEEF);
	ESPERAR_PC_SIGUIENTE();
}

static void movl_relativo_a_pc_alinea_a_cuatro(void)
{
	arnes_reset();

	/* Con PC en base+2, (PC & ~3) vuelve a base: la direccion es la misma que
	   en el caso anterior. Es el motivo de que exista el enmascarado. */
	PC = PRUEBA_PC + 2;
	escribir_l(PRUEBA_PC + 4 + 1 * 4, 0xCAFEBABE);

	ejecutar(instr_ni(0xD000, 5, 1));

	ESPERAR_U32(R(5), 0xCAFEBABE);
	ESPERAR_U32(PC, PRUEBA_PC + 4);
}

static void mova_deja_la_direccion_en_r0(void)
{
	arnes_reset();

	ejecutar(instr_i(0xC700, 3));			/* MOVA @(3, PC), R0 */

	ESPERAR_U32(R(0), PRUEBA_PC + 4 + 3 * 4);
	ESPERAR_PC_SIGUIENTE();
}

static void mova_alinea_a_cuatro(void)
{
	arnes_reset();

	PC = PRUEBA_PC + 2;
	ejecutar(instr_i(0xC700, 0));

	ESPERAR_U32(R(0), PRUEBA_PC + 4);
}

/* ------------------------------------------------------------ Rm -> @Rn */

static void movb_guarda_el_byte_bajo(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x123456AB;
	escribir_l(PRUEBA_DATOS, 0xFFFFFFFF);

	ejecutar(instr_nm(0x2000, 1, 2));		/* MOV.B R2, @R1 */

	ESPERAR_U32(leer_b(PRUEBA_DATOS), 0xAB);
	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0xFFFFFFAB);	/* no toca los otros 3 */
	ESPERAR_U32(R(1), PRUEBA_DATOS);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_guarda_la_word_baja(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x1234ABCD;
	escribir_l(PRUEBA_DATOS, 0xFFFFFFFF);

	ejecutar(instr_nm(0x2001, 1, 2));		/* MOV.W R2, @R1 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0xFFFFABCD);
}

static void movl_guarda_los_32_bits(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x89ABCDEF;

	ejecutar(instr_nm(0x2002, 1, 2));		/* MOV.L R2, @R1 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0x89ABCDEF);
}

/* ------------------------------------------------------------ @Rm -> Rn */

static void movb_carga_extiende_signo(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS, 0x80);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6000, 1, 2));		/* MOV.B @R2, R1 */

	ESPERAR_U32(R(1), 0xFFFFFF80);
	ESPERAR_PC_SIGUIENTE();
}

static void movb_carga_positiva(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS, 0x7F);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6000, 1, 2));

	ESPERAR_U32(R(1), 0x0000007F);
}

static void movw_carga_extiende_signo(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS, 0x8001);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6001, 1, 2));		/* MOV.W @R2, R1 */

	ESPERAR_U32(R(1), 0xFFFF8001);
}

static void movl_carga(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x89ABCDEF);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6002, 1, 2));		/* MOV.L @R2, R1 */

	ESPERAR_U32(R(1), 0x89ABCDEF);
}

/* -------------------------------------------------------- predecremento */

static void movb_predecremento(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x000000AB;

	ejecutar(instr_nm(0x2004, 1, 2));		/* MOV.B R2, @-R1 */

	ESPERAR_U32(R(1), PRUEBA_DATOS - 1);
	ESPERAR_U32(leer_b(PRUEBA_DATOS - 1), 0xAB);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_predecremento(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x0000ABCD;

	ejecutar(instr_nm(0x2005, 1, 2));		/* MOV.W R2, @-R1 */

	ESPERAR_U32(R(1), PRUEBA_DATOS - 2);
	ESPERAR_U32(leer_w(PRUEBA_DATOS - 2), 0xABCD);
}

static void movl_predecremento(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x89ABCDEF;

	ejecutar(instr_nm(0x2006, 1, 2));		/* MOV.L R2, @-R1 */

	ESPERAR_U32(R(1), PRUEBA_DATOS - 4);
	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), 0x89ABCDEF);
}

/* Los tres con n == m. El manual escribe "Write_Byte(R[n]-1, R[m]); R[n]-=1":
   **el valor que va a memoria es el de Rm antes de decrementar**, o sea el Rn
   original, y la direccion si es la decrementada.

   Estuvo al reves un tiempo -- se decrementaba y despues se leia Rm --, y lo
   marcaron las pruebas de SingleStepTests/sh4: 21 casos de 0010nnnnmmmm0100,
   30 de la version word y 34 de la long, justo los que caen con n == m. */
static void movl_predecremento_mismo_registro(void)
{
	arnes_reset();

	R(15) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x2006, 15, 15));		/* MOV.L R15, @-R15 */

	ESPERAR_U32(R(15), PRUEBA_DATOS - 4);
	ESPERAR_U32(leer_l(PRUEBA_DATOS - 4), PRUEBA_DATOS);
}

static void movb_predecremento_mismo_registro(void)
{
	arnes_reset();

	R(15) = PRUEBA_DATOS;					/* ...0000, decrementado ...FFFF */

	ejecutar(instr_nm(0x2004, 15, 15));		/* MOV.B R15, @-R15 */

	ESPERAR_U32(R(15), PRUEBA_DATOS - 1);
	ESPERAR_U32(leer_b(PRUEBA_DATOS - 1), PRUEBA_DATOS & 0xFF);
}

static void movw_predecremento_mismo_registro(void)
{
	arnes_reset();

	R(15) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x2005, 15, 15));		/* MOV.W R15, @-R15 */

	ESPERAR_U32(R(15), PRUEBA_DATOS - 2);
	ESPERAR_U32(leer_w(PRUEBA_DATOS - 2), PRUEBA_DATOS & 0xFFFF);
}

/* -------------------------------------------------------- postincremento */

static void movb_postincremento(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS, 0x80);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6004, 1, 2));		/* MOV.B @R2+, R1 */

	ESPERAR_U32(R(1), 0xFFFFFF80);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 1);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_postincremento(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS, 0x8001);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6005, 1, 2));		/* MOV.W @R2+, R1 */

	ESPERAR_U32(R(1), 0xFFFF8001);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 2);
}

static void movl_postincremento(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x89ABCDEF);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6006, 1, 2));		/* MOV.L @R2+, R1 */

	ESPERAR_U32(R(1), 0x89ABCDEF);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
}

static void movl_postincremento_mismo_registro(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0x89ABCDEF);
	R(3) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x6006, 3, 3));		/* MOV.L @R3+, R3 */

	/* Con n == m gana el dato leido: el incremento se descarta. */
	ESPERAR_U32(R(3), 0x89ABCDEF);
}

/* -------------------------------------------------- desplazamiento + Rn */

static void movb_a_disp_rn(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(0) = 0x000000AB;

	ejecutar(instr_nd(0x8000, 1, 5));		/* MOV.B R0, @(5, R1) */

	ESPERAR_U32(leer_b(PRUEBA_DATOS + 5), 0xAB);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_a_disp_rn_escala_por_dos(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(0) = 0x0000ABCD;

	ejecutar(instr_nd(0x8100, 1, 5));		/* MOV.W R0, @(5, R1) */

	ESPERAR_U32(leer_w(PRUEBA_DATOS + 10), 0xABCD);
}

static void movl_a_disp_rn_escala_por_cuatro(void)
{
	arnes_reset();

	R(1) = PRUEBA_DATOS;
	R(2) = 0x89ABCDEF;

	ejecutar(instr_nmd(0x1000, 1, 2, 5));	/* MOV.L R2, @(5, R1) */

	ESPERAR_U32(leer_l(PRUEBA_DATOS + 20), 0x89ABCDEF);
	ESPERAR_PC_SIGUIENTE();
}

static void movb_desde_disp_rm_extiende_signo(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS + 5, 0x80);
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_nd(0x8400, 1, 5));		/* MOV.B @(5, R1), R0 */

	ESPERAR_U32(R(0), 0xFFFFFF80);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_desde_disp_rm_extiende_signo(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS + 10, 0x8001);
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_nd(0x8500, 1, 5));		/* MOV.W @(5, R1), R0 */

	ESPERAR_U32(R(0), 0xFFFF8001);
}

static void movl_desde_disp_rm(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 20, 0x89ABCDEF);
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nmd(0x5000, 1, 2, 5));	/* MOV.L @(5, R2), R1 */

	ESPERAR_U32(R(1), 0x89ABCDEF);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------- indexado por R0 */

static void movb_indexado(void)
{
	arnes_reset();

	R(0) = 0x10;
	R(1) = PRUEBA_DATOS;
	R(2) = 0x000000AB;

	ejecutar(instr_nm(0x0004, 1, 2));		/* MOV.B R2, @(R0, R1) */

	ESPERAR_U32(leer_b(PRUEBA_DATOS + 0x10), 0xAB);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_indexado(void)
{
	arnes_reset();

	R(0) = 0x10;
	R(1) = PRUEBA_DATOS;
	R(2) = 0x0000ABCD;

	ejecutar(instr_nm(0x0005, 1, 2));		/* MOV.W R2, @(R0, R1) */

	ESPERAR_U32(leer_w(PRUEBA_DATOS + 0x10), 0xABCD);
}

static void movl_indexado(void)
{
	arnes_reset();

	R(0) = 0x10;
	R(1) = PRUEBA_DATOS;
	R(2) = 0x89ABCDEF;

	ejecutar(instr_nm(0x0006, 1, 2));		/* MOV.L R2, @(R0, R1) */

	ESPERAR_U32(leer_l(PRUEBA_DATOS + 0x10), 0x89ABCDEF);
}

static void movb_carga_indexada_extiende_signo(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS + 0x10, 0x80);
	R(0) = 0x10;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x000C, 1, 2));		/* MOV.B @(R0, R2), R1 */

	ESPERAR_U32(R(1), 0xFFFFFF80);
}

static void movw_carga_indexada_extiende_signo(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS + 0x10, 0x8001);
	R(0) = 0x10;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x000D, 1, 2));		/* MOV.W @(R0, R2), R1 */

	ESPERAR_U32(R(1), 0xFFFF8001);
}

static void movl_carga_indexada(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 0x10, 0x89ABCDEF);
	R(0) = 0x10;
	R(2) = PRUEBA_DATOS;

	ejecutar(instr_nm(0x000E, 1, 2));		/* MOV.L @(R0, R2), R1 */

	ESPERAR_U32(R(1), 0x89ABCDEF);
}

/* ------------------------------------------------------- relativas a GBR */

static void movb_a_disp_gbr(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x000000AB;
	escribir_l(PRUEBA_DATOS + 4, 0);

	ejecutar(instr_i(0xC000, 4));			/* MOV.B R0, @(4, GBR) */

	ESPERAR_U32(leer_b(PRUEBA_DATOS + 4), 0xAB);
	ESPERAR_U32(R(0), 0x000000AB);			/* es una escritura, no una carga */
	ESPERAR_PC_SIGUIENTE();
}

static void movw_a_disp_gbr_escala_por_dos(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x0000ABCD;

	ejecutar(instr_i(0xC100, 4));			/* MOV.W R0, @(4, GBR) */

	ESPERAR_U32(leer_w(PRUEBA_DATOS + 8), 0xABCD);
	ESPERAR_PC_SIGUIENTE();
}

static void movl_a_disp_gbr_escala_por_cuatro(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x89ABCDEF;

	ejecutar(instr_i(0xC200, 4));			/* MOV.L R0, @(4, GBR) */

	ESPERAR_U32(leer_l(PRUEBA_DATOS + 16), 0x89ABCDEF);
}

static void movb_desde_disp_gbr_extiende_signo(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	escribir_b(PRUEBA_DATOS + 4, 0x80);

	ejecutar(instr_i(0xC400, 4));			/* MOV.B @(4, GBR), R0 */

	ESPERAR_U32(R(0), 0xFFFFFF80);
	ESPERAR_PC_SIGUIENTE();
}

static void movw_desde_disp_gbr_extiende_signo(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	escribir_w(PRUEBA_DATOS + 8, 0x8001);

	ejecutar(instr_i(0xC500, 4));			/* MOV.W @(4, GBR), R0 */

	ESPERAR_U32(R(0), 0xFFFF8001);
}

static void movl_desde_disp_gbr(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	escribir_l(PRUEBA_DATOS + 16, 0x89ABCDEF);

	ejecutar(instr_i(0xC600, 4));			/* MOV.L @(4, GBR), R0 */

	ESPERAR_U32(R(0), 0x89ABCDEF);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------------------ MOVT */

static void movt_con_t_prendido(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0xFFFFFFFF;

	ejecutar(instr_n(0x0029, 1));			/* MOVT R1 */

	ESPERAR_U32(R(1), 1);
	ESPERAR_PC_SIGUIENTE();
}

static void movt_con_t_apagado(void)
{
	arnes_reset();

	SR_T = 0;
	R(1) = 0xFFFFFFFF;

	ejecutar(instr_n(0x0029, 1));

	ESPERAR_U32(R(1), 0);
}

/* ------------------------------------------------------------ SWAP/XTRCT */

static void swapb_intercambia_los_bytes_bajos(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_nm(0x6008, 1, 2));		/* SWAP.B R2, R1 */

	ESPERAR_U32(R(1), 0x12347856);
	ESPERAR_PC_SIGUIENTE();
}

static void swapb_sobre_si_mismo(void)
{
	arnes_reset();

	R(3) = 0xAABBCCDD;
	ejecutar(instr_nm(0x6008, 3, 3));

	ESPERAR_U32(R(3), 0xAABBDDCC);
}

static void swapw_intercambia_las_mitades(void)
{
	arnes_reset();

	R(2) = 0x12345678;
	ejecutar(instr_nm(0x6009, 1, 2));		/* SWAP.W R2, R1 */

	ESPERAR_U32(R(1), 0x56781234);
}

static void xtrct_saca_los_32_bits_del_medio(void)
{
	arnes_reset();

	R(1) = 0xCCCCDDDD;						/* Rn aporta la mitad alta */
	R(2) = 0xAAAABBBB;						/* Rm aporta la mitad baja */
	ejecutar(instr_nm(0x200D, 1, 2));		/* XTRCT R2, R1 */

	ESPERAR_U32(R(1), 0xBBBBCCCC);
	ESPERAR_PC_SIGUIENTE();
}

/* --------------------------------------------------------------- MOVCA.L */

/* En el SH-4 reserva la linea de cache ademas de escribir. Sin cache emulada
   el efecto visible es el de un MOV.L R0, @Rn. */
static void movcal_guarda_r0(void)
{
	arnes_reset();

	R(0) = 0x89ABCDEF;
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_n(0x00C3, 1));			/* MOVCA.L R0, @R1 */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0x89ABCDEF);
	ESPERAR_U32(R(1), PRUEBA_DATOS);
	ESPERAR_PC_SIGUIENTE();
}

/* ----------------------------------------------------- despacho por zona */

static void movl_escribe_en_ram_de_video(void)
{
	arnes_reset();

	/* La direccion decide que handler corre: 0xA5 es RAM de video, no la RAM
	   del sistema. */
	R(1) = PRUEBA_VIDEO + 0x100;
	R(2) = 0x89ABCDEF;

	ejecutar(instr_nm(0x2002, 1, 2));		/* MOV.L R2, @R1 */

	ESPERAR_U32(leer_l(PRUEBA_VIDEO + 0x100), 0x89ABCDEF);
	ESPERAR_U32(prueba_accesos_invalidos, 0);
}

static void movl_a_zona_sin_mapear_no_escribe(void)
{
	arnes_reset();

	R(1) = 0x30000000;						/* zona sin mapear */
	R(2) = 0x89ABCDEF;

	ejecutar(instr_nm(0x2002, 1, 2));

	ESPERAR_U32(prueba_accesos_invalidos, 1);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(mov_inmediato_extiende_signo),
	CASO(mov_inmediato_positivo),
	CASO(mov_rm_rn),
	CASO(movw_relativo_a_pc_extiende_signo),
	CASO(movl_relativo_a_pc),
	CASO(movl_relativo_a_pc_alinea_a_cuatro),
	CASO(mova_deja_la_direccion_en_r0),
	CASO(mova_alinea_a_cuatro),
	CASO(movb_guarda_el_byte_bajo),
	CASO(movw_guarda_la_word_baja),
	CASO(movl_guarda_los_32_bits),
	CASO(movb_carga_extiende_signo),
	CASO(movb_carga_positiva),
	CASO(movw_carga_extiende_signo),
	CASO(movl_carga),
	CASO(movb_predecremento),
	CASO(movw_predecremento),
	CASO(movl_predecremento),
	CASO(movl_predecremento_mismo_registro),
	CASO(movb_predecremento_mismo_registro),
	CASO(movw_predecremento_mismo_registro),
	CASO(movb_postincremento),
	CASO(movw_postincremento),
	CASO(movl_postincremento),
	CASO(movl_postincremento_mismo_registro),
	CASO(movb_a_disp_rn),
	CASO(movw_a_disp_rn_escala_por_dos),
	CASO(movl_a_disp_rn_escala_por_cuatro),
	CASO(movb_desde_disp_rm_extiende_signo),
	CASO(movw_desde_disp_rm_extiende_signo),
	CASO(movl_desde_disp_rm),
	CASO(movb_indexado),
	CASO(movw_indexado),
	CASO(movl_indexado),
	CASO(movb_carga_indexada_extiende_signo),
	CASO(movw_carga_indexada_extiende_signo),
	CASO(movl_carga_indexada),
	CASO(movb_a_disp_gbr),
	CASO(movw_a_disp_gbr_escala_por_dos),
	CASO(movl_a_disp_gbr_escala_por_cuatro),
	CASO(movb_desde_disp_gbr_extiende_signo),
	CASO(movw_desde_disp_gbr_extiende_signo),
	CASO(movl_desde_disp_gbr),
	CASO(movt_con_t_prendido),
	CASO(movt_con_t_apagado),
	CASO(swapb_intercambia_los_bytes_bajos),
	CASO(swapb_sobre_si_mismo),
	CASO(swapw_intercambia_las_mitades),
	CASO(xtrct_saca_los_32_bits_del_medio),
	CASO(movcal_guarda_r0),
	CASO(movl_escribe_en_ram_de_video),
	CASO(movl_a_zona_sin_mapear_no_escribe),
};

const dc_suite suite_mov = DEFINIR_SUITE("mov", casos);
