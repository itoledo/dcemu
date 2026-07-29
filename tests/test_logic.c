/****************************************************************************

	Pruebas de logic.c -- instrucciones logicas

	Casos borde que cubre esta suite:

	  - Los inmediatos de AND/OR/XOR/TST se extienden con ceros, no con signo:
		AND #0x0F, R0 deja R0 con 4 bits utiles como mucho. Es la diferencia
		con ADD #imm y CMP/EQ #imm, que si extienden signo.
	  - TST no guarda el resultado en ningun lado, solo mueve T, y T queda en 1
		cuando la conjuncion es cero (logica invertida respecto de lo que uno
		esperaria leyendo el nombre).
	  - TST Rn, Rn es el modismo para "Rn == 0".
	  - TAS.B lee, decide T con el valor previo y despues prende el bit 7. Se
		verifica el orden: con el byte ya en 0x80, T tiene que quedar en 0 y el
		byte no cambia.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* -------------------------------------------------------------------- AND */

static void and_rm_rn(void)
{
	arnes_reset();

	R(1) = 0xFF00FF00;
	R(2) = 0x0FF00FF0;
	ejecutar(instr_nm(0x2009, 1, 2));	/* AND R2, R1 */

	ESPERAR_U32(R(1), 0x0F000F00);
	ESPERAR_U32(R(2), 0x0FF00FF0);
	ESPERAR_PC_SIGUIENTE();
}

static void and_inmediato_extiende_con_ceros(void)
{
	arnes_reset();

	R(0) = 0xFFFFFFFF;
	ejecutar(instr_i(0xC900, 0x0F));	/* AND #0x0F, R0 */

	ESPERAR_U32(R(0), 0x0000000F);		/* no 0xFFFFFF0F */
	ESPERAR_PC_SIGUIENTE();
}

static void and_inmediato_con_bit_alto(void)
{
	arnes_reset();

	R(0) = 0xFFFFFFFF;
	ejecutar(instr_i(0xC900, 0xFF));	/* 0xFF no es -1 aqui */

	ESPERAR_U32(R(0), 0x000000FF);
}

/* -------------------------------------------------------------------- NOT */

static void not_rm_rn(void)
{
	arnes_reset();

	R(2) = 0x0F0F0F0F;
	ejecutar(instr_nm(0x6007, 1, 2));	/* NOT R2, R1 */

	ESPERAR_U32(R(1), 0xF0F0F0F0);
	ESPERAR_U32(R(2), 0x0F0F0F0F);
	ESPERAR_PC_SIGUIENTE();
}

static void not_sobre_si_mismo(void)
{
	arnes_reset();

	R(3) = 0x00000000;
	ejecutar(instr_nm(0x6007, 3, 3));	/* NOT R3, R3 */

	ESPERAR_U32(R(3), 0xFFFFFFFF);
}

/* --------------------------------------------------------------------- OR */

static void or_rm_rn(void)
{
	arnes_reset();

	R(1) = 0xF0F0F0F0;
	R(2) = 0x0F0F0F0F;
	ejecutar(instr_nm(0x200B, 1, 2));	/* OR R2, R1 */

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_PC_SIGUIENTE();
}

static void or_inmediato_extiende_con_ceros(void)
{
	arnes_reset();

	R(0) = 0x12345600;
	ejecutar(instr_i(0xCB00, 0xFF));	/* OR #0xFF, R0 */

	ESPERAR_U32(R(0), 0x123456FF);		/* solo toca el byte bajo */
	ESPERAR_PC_SIGUIENTE();
}

/* -------------------------------------------------------------------- XOR */

static void xor_rm_rn(void)
{
	arnes_reset();

	R(1) = 0xFFFF0000;
	R(2) = 0x00FF00FF;
	ejecutar(instr_nm(0x200A, 1, 2));	/* XOR R2, R1 */

	ESPERAR_U32(R(1), 0xFF0000FF);
	ESPERAR_PC_SIGUIENTE();
}

static void xor_sobre_si_mismo_da_cero(void)
{
	arnes_reset();

	R(3) = 0x12345678;
	ejecutar(instr_nm(0x200A, 3, 3));

	ESPERAR_U32(R(3), 0);
}

static void xor_inmediato_extiende_con_ceros(void)
{
	arnes_reset();

	R(0) = 0xFFFFFFFF;
	ejecutar(instr_i(0xCA00, 0xFF));	/* XOR #0xFF, R0 */

	ESPERAR_U32(R(0), 0xFFFFFF00);
	ESPERAR_PC_SIGUIENTE();
}

/* -------------------------------------------------------------------- TST */

static void tst_con_bits_en_comun(void)
{
	arnes_reset();

	R(1) = 0x000000FF;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x2008, 1, 2));	/* TST R2, R1 */

	ESPERAR_T(0);						/* hay coincidencia -> T = 0 */
	ESPERAR_U32(R(1), 0x000000FF);		/* TST no guarda el resultado */
	ESPERAR_PC_SIGUIENTE();
}

static void tst_sin_bits_en_comun(void)
{
	arnes_reset();

	R(1) = 0x000000F0;
	R(2) = 0x0000000F;
	ejecutar(instr_nm(0x2008, 1, 2));

	ESPERAR_T(1);
}

static void tst_mismo_registro_detecta_cero(void)
{
	arnes_reset();

	R(4) = 0;
	ejecutar(instr_nm(0x2008, 4, 4));	/* TST R4, R4 */

	ESPERAR_T(1);
}

static void tst_inmediato_extiende_con_ceros(void)
{
	arnes_reset();

	R(0) = 0xFFFFFF00;
	ejecutar(instr_i(0xC800, 0xFF));	/* TST #0xFF, R0 */

	ESPERAR_T(1);						/* el byte bajo esta en cero */
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------------------ TAS.B */

static void tasb_sobre_cero_prende_el_bit_7(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS, 0x00);
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_n(0x401B, 1));		/* TAS.B @R1 */

	ESPERAR_T(1);						/* el valor previo era cero */
	ESPERAR_U32(leer_b(PRUEBA_DATOS), 0x80);
	ESPERAR_U32(R(1), PRUEBA_DATOS);	/* no modifica el puntero */
	ESPERAR_PC_SIGUIENTE();
}

static void tasb_sobre_ocupado_no_cambia_el_valor(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS, 0x80);
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_n(0x401B, 1));

	ESPERAR_T(0);
	ESPERAR_U32(leer_b(PRUEBA_DATOS), 0x80);
}

static void tasb_conserva_los_bits_bajos(void)
{
	arnes_reset();

	escribir_b(PRUEBA_DATOS, 0x01);
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_n(0x401B, 1));

	ESPERAR_T(0);
	ESPERAR_U32(leer_b(PRUEBA_DATOS), 0x81);
}

static void tasb_solo_toca_un_byte(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS, 0xFFFFFF00);
	R(1) = PRUEBA_DATOS;

	ejecutar(instr_n(0x401B, 1));

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0xFFFFFF80);
}

/* -------------------------------------------- variantes .B sobre memoria */

/* Las cuatro operan sobre el byte en (R0 + GBR), no sobre un registro. */

static void andb_combina_el_byte_de_memoria(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x10;
	escribir_b(PRUEBA_DATOS + 0x10, 0xFF);

	ejecutar(instr_i(0xCD00, 0x0F));	/* AND.B #0x0F, @(R0, GBR) */

	ESPERAR_U32(leer_b(PRUEBA_DATOS + 0x10), 0x0F);
	ESPERAR_PC_SIGUIENTE();
}

static void andb_no_toca_los_bytes_vecinos(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 1;
	escribir_l(PRUEBA_DATOS, 0xFFFFFFFF);

	ejecutar(instr_i(0xCD00, 0x00));	/* AND.B #0, @(R0, GBR) */

	ESPERAR_U32(leer_l(PRUEBA_DATOS), 0xFFFF00FF);
}

static void orb_combina_el_byte_de_memoria(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x10;
	escribir_b(PRUEBA_DATOS + 0x10, 0xF0);

	ejecutar(instr_i(0xCF00, 0x0F));	/* OR.B #0x0F, @(R0, GBR) */

	ESPERAR_U32(leer_b(PRUEBA_DATOS + 0x10), 0xFF);
	ESPERAR_PC_SIGUIENTE();
}

static void xorb_combina_el_byte_de_memoria(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x10;
	escribir_b(PRUEBA_DATOS + 0x10, 0xFF);

	ejecutar(instr_i(0xCE00, 0x0F));	/* XOR.B #0x0F, @(R0, GBR) */

	ESPERAR_U32(leer_b(PRUEBA_DATOS + 0x10), 0xF0);
	ESPERAR_PC_SIGUIENTE();
}

static void tstb_no_escribe_nada(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x10;
	escribir_b(PRUEBA_DATOS + 0x10, 0xF0);

	ejecutar(instr_i(0xCC00, 0x0F));	/* TST.B #0x0F, @(R0, GBR) */

	ESPERAR_T(1);						/* no hay bits en comun */
	ESPERAR_U32(leer_b(PRUEBA_DATOS + 0x10), 0xF0);
	ESPERAR_PC_SIGUIENTE();
}

static void tstb_con_bits_en_comun(void)
{
	arnes_reset();

	GBR = PRUEBA_DATOS;
	R(0) = 0x10;
	escribir_b(PRUEBA_DATOS + 0x10, 0xFF);

	ejecutar(instr_i(0xCC00, 0x01));

	ESPERAR_T(0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(and_rm_rn),
	CASO(and_inmediato_extiende_con_ceros),
	CASO(and_inmediato_con_bit_alto),
	CASO(not_rm_rn),
	CASO(not_sobre_si_mismo),
	CASO(or_rm_rn),
	CASO(or_inmediato_extiende_con_ceros),
	CASO(xor_rm_rn),
	CASO(xor_sobre_si_mismo_da_cero),
	CASO(xor_inmediato_extiende_con_ceros),
	CASO(tst_con_bits_en_comun),
	CASO(tst_sin_bits_en_comun),
	CASO(tst_mismo_registro_detecta_cero),
	CASO(tst_inmediato_extiende_con_ceros),
	CASO(tasb_sobre_cero_prende_el_bit_7),
	CASO(tasb_sobre_ocupado_no_cambia_el_valor),
	CASO(tasb_conserva_los_bits_bajos),
	CASO(tasb_solo_toca_un_byte),
	CASO(andb_combina_el_byte_de_memoria),
	CASO(andb_no_toca_los_bytes_vecinos),
	CASO(orb_combina_el_byte_de_memoria),
	CASO(xorb_combina_el_byte_de_memoria),
	CASO(tstb_no_escribe_nada),
	CASO(tstb_con_bits_en_comun),
};

const dc_suite suite_logic = DEFINIR_SUITE("logic", casos);
