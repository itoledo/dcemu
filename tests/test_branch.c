/****************************************************************************

	Pruebas de branch.c -- saltos

	Casos borde que cubre esta suite:

	  - El destino siempre se calcula sobre PC + 4, no sobre PC: el "+4" es el
		pipeline del SH-4. Todos los casos verifican la direccion exacta.
	  - Desplazamientos negativos: los inmediatos de 8 y 12 bits van con signo,
		asi que un salto hacia atras tiene que llegar antes del propio salto.
	  - Ranura de retardo (delay slot): BF y BT no la tienen, BF/S, BT/S, BRA,
		BSR, BRAF, BSRF, JMP, JSR y RTS si. Cada caso pone un MOV #imm en
		PC+2 y mira si se ejecuto: es la unica forma de distinguirlos.
	  - El destino se calcula antes de ejecutar la ranura de retardo. Se prueba
		con JMP @Rn cuya ranura pisa Rn: el salto tiene que ir al valor viejo.
	  - PR se carga antes de la ranura de retardo, asi que un STS PR,Rn ahi
		adentro ya ve la direccion de retorno.
	  - BF/S y BT/S con la condicion falsa no ejecutan la ranura: solo avanzan.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* MOV #0x55, R1: la instruccion testigo que se pone en la ranura de retardo.
   Si R1 vale 0x55 al final, la ranura se ejecuto. */
#define TESTIGO			0xE155
#define TESTIGO_VALOR	0x55

static void poner_testigo(void)
{
	poner_instr(PRUEBA_PC + 2, TESTIGO);
}

/* --------------------------------------------------------------- BF / BT */

static void bf_salta_con_t_apagado(void)
{
	arnes_reset();
	poner_testigo();

	SR_T = 0;
	ejecutar(instr_i(0x8B00, 2));		/* BF +2 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 2 * 2);
	ESPERAR_U32(R(1), 0);				/* BF no tiene ranura de retardo */
}

static void bf_no_salta_con_t_prendido(void)
{
	arnes_reset();

	SR_T = 1;
	ejecutar(instr_i(0x8B00, 2));

	ESPERAR_PC_SIGUIENTE();
}

static void bf_hacia_atras(void)
{
	arnes_reset();

	SR_T = 0;
	ejecutar(instr_i(0x8B00, 0xFA));	/* BF -6 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 - 6 * 2);
}

static void bt_salta_con_t_prendido(void)
{
	arnes_reset();
	poner_testigo();

	SR_T = 1;
	ejecutar(instr_i(0x8900, 2));		/* BT +2 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 2 * 2);
	ESPERAR_U32(R(1), 0);				/* BT tampoco tiene ranura */
}

static void bt_no_salta_con_t_apagado(void)
{
	arnes_reset();

	SR_T = 0;
	ejecutar(instr_i(0x8900, 2));

	ESPERAR_PC_SIGUIENTE();
}

/* ----------------------------------------------------------- BF/S y BT/S */

static void bfs_salta_y_ejecuta_la_ranura(void)
{
	arnes_reset();
	poner_testigo();

	SR_T = 0;
	ejecutar(instr_i(0x8F00, 3));		/* BF/S +3 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 3 * 2);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

static void bfs_sin_salto_no_ejecuta_la_ranura(void)
{
	arnes_reset();
	poner_testigo();

	SR_T = 1;
	ejecutar(instr_i(0x8F00, 3));

	ESPERAR_PC_SIGUIENTE();
	ESPERAR_U32(R(1), 0);
}

static void bts_salta_y_ejecuta_la_ranura(void)
{
	arnes_reset();
	poner_testigo();

	SR_T = 1;
	ejecutar(instr_i(0x8D00, 3));		/* BT/S +3 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 3 * 2);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

static void bts_sin_salto_no_ejecuta_la_ranura(void)
{
	arnes_reset();
	poner_testigo();

	SR_T = 0;
	ejecutar(instr_i(0x8D00, 3));

	ESPERAR_PC_SIGUIENTE();
	ESPERAR_U32(R(1), 0);
}

/* ---------------------------------------------------------------- BRA/BSR */

static void bra_con_desplazamiento_de_12_bits(void)
{
	arnes_reset();
	poner_testigo();

	ejecutar((WORD) (0xA000 | 0x100));	/* BRA +256 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 0x100 * 2);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

static void bra_hacia_atras(void)
{
	arnes_reset();
	poner_testigo();

	ejecutar((WORD) (0xA000 | 0xFFE));	/* BRA -2 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 - 2 * 2);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

static void bsr_guarda_el_retorno_en_pr(void)
{
	arnes_reset();
	poner_testigo();

	ejecutar((WORD) (0xB000 | 0x010));	/* BSR +16 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 0x10 * 2);
	ESPERAR_U32(PR, PRUEBA_PC + 4);		/* la instruccion tras la ranura */
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

/* PR se escribe antes de correr la ranura de retardo, asi que un STS PR ahi
   adentro ya lee la direccion de retorno. */
static void bsr_deja_pr_visible_desde_la_ranura(void)
{
	arnes_reset();

	poner_instr(PRUEBA_PC + 2, instr_n(0x002A, 2));	/* STS PR, R2 */

	ejecutar((WORD) (0xB000 | 0x010));

	ESPERAR_U32(R(2), PRUEBA_PC + 4);
}

/* ------------------------------------------------------------- BRAF/BSRF */

static void braf_suma_el_registro(void)
{
	arnes_reset();
	poner_testigo();

	R(3) = 0x40;
	ejecutar(instr_n(0x0023, 3));		/* BRAF R3 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 0x40);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

static void bsrf_suma_el_registro_y_guarda_pr(void)
{
	arnes_reset();
	poner_testigo();

	R(3) = 0x40;
	ejecutar(instr_n(0x0003, 3));		/* BSRF R3 */

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 0x40);
	ESPERAR_U32(PR, PRUEBA_PC + 4);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

/* ---------------------------------------------------------------- JMP/JSR */

static void jmp_salta_a_la_direccion_del_registro(void)
{
	arnes_reset();
	poner_testigo();

	R(3) = PRUEBA_DATOS;
	ejecutar(instr_n(0x402B, 3));		/* JMP @R3 */

	ESPERAR_U32(PC, PRUEBA_DATOS);		/* absoluto, sin sumar 4 */
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

/* El destino queda fijado antes de la ranura: si la ranura pisa Rn, el salto
   igual va al valor viejo. */
static void jmp_fija_el_destino_antes_de_la_ranura(void)
{
	arnes_reset();

	R(3) = PRUEBA_DATOS;
	poner_instr(PRUEBA_PC + 2, instr_ni(0xE000, 3, 0x7F));	/* MOV #0x7F, R3 */

	ejecutar(instr_n(0x402B, 3));		/* JMP @R3 */

	ESPERAR_U32(PC, PRUEBA_DATOS);
	ESPERAR_U32(R(3), 0x7F);			/* la ranura si corrio */
}

static void jsr_guarda_el_retorno_en_pr(void)
{
	arnes_reset();
	poner_testigo();

	R(3) = PRUEBA_DATOS;
	ejecutar(instr_n(0x400B, 3));		/* JSR @R3 */

	ESPERAR_U32(PC, PRUEBA_DATOS);
	ESPERAR_U32(PR, PRUEBA_PC + 4);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

/* -------------------------------------------------------------------- RTS */

static void rts_vuelve_a_pr(void)
{
	arnes_reset();
	poner_testigo();

	PR = PRUEBA_DATOS;
	ejecutar(0x000B);					/* RTS */

	ESPERAR_U32(PC, PRUEBA_DATOS);
	ESPERAR_U32(R(1), TESTIGO_VALOR);
}

/* Igual que JMP: el destino se toma antes de la ranura, asi que un LDS a PR
   en la ranura no cambia adonde vuelve este RTS. */
static void rts_fija_el_destino_antes_de_la_ranura(void)
{
	arnes_reset();

	PR = PRUEBA_DATOS;
	R(4) = 0x12345678;
	poner_instr(PRUEBA_PC + 2, instr_n(0x402A, 4));	/* LDS R4, PR */

	ejecutar(0x000B);

	ESPERAR_U32(PC, PRUEBA_DATOS);
	ESPERAR_U32(PR, 0x12345678);		/* la ranura si corrio */
}

/* Llamada y retorno completos, encadenando BSR y RTS. */
static void bsr_y_rts_van_y_vuelven(void)
{
	arnes_reset();

	/* BSR +8 con NOP en la ranura: se va a PRUEBA_PC + 4 + 16. */
	poner_instr(PRUEBA_PC + 2, 0x0009);				/* NOP */
	ejecutar((WORD) (0xB000 | 0x008));

	ESPERAR_U32(PC, PRUEBA_PC + 4 + 8 * 2);
	ESPERAR_U32(PR, PRUEBA_PC + 4);

	/* En el destino, un RTS con NOP en la ranura vuelve a PR. */
	poner_instr(PC + 2, 0x0009);
	ejecutar(0x000B);

	ESPERAR_U32(PC, PRUEBA_PC + 4);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(bf_salta_con_t_apagado),
	CASO(bf_no_salta_con_t_prendido),
	CASO(bf_hacia_atras),
	CASO(bt_salta_con_t_prendido),
	CASO(bt_no_salta_con_t_apagado),
	CASO(bfs_salta_y_ejecuta_la_ranura),
	CASO(bfs_sin_salto_no_ejecuta_la_ranura),
	CASO(bts_salta_y_ejecuta_la_ranura),
	CASO(bts_sin_salto_no_ejecuta_la_ranura),
	CASO(bra_con_desplazamiento_de_12_bits),
	CASO(bra_hacia_atras),
	CASO(bsr_guarda_el_retorno_en_pr),
	CASO(bsr_deja_pr_visible_desde_la_ranura),
	CASO(braf_suma_el_registro),
	CASO(bsrf_suma_el_registro_y_guarda_pr),
	CASO(jmp_salta_a_la_direccion_del_registro),
	CASO(jmp_fija_el_destino_antes_de_la_ranura),
	CASO(jsr_guarda_el_retorno_en_pr),
	CASO(rts_vuelve_a_pr),
	CASO(rts_fija_el_destino_antes_de_la_ranura),
	CASO(bsr_y_rts_van_y_vuelven),
};

const dc_suite suite_branch = DEFINIR_SUITE("branch", casos);
