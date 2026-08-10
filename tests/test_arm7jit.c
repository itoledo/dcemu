/****************************************************************************

	Pruebas del traductor de bloques del ARM7 (arm7jit.c).

	La suite es el A/B del plan: cada programa corre dos veces por
	arm7_ejecutar() -- una con el emisor desinstalado (el lazo en C, que ya
	esta probado contra el interprete puro) y otra con el codigo emitido --
	y los dos estados finales tienen que ser identicos: los dieciseis
	registros, CPSR, SPSR, el banco, los ciclos que quedaron y las
	instrucciones contadas, mas la RAM de onda entera (los STR estan ahi).

	Los programas cubren cada plantilla y cada borde que la emision resuelve
	distinto que el lazo: las banderas armadas desde las del anfitrion (la C
	invertida de las restas), los casos de cantidad cero del desplazador, la
	rotacion de la carga desalineada, el PC bakeado como constante (leerlo
	como operando, cargar relativo a el, guardarlo con +12), la llamada de
	respaldo al manejador dentro de un bloque emitido, y la salida lateral
	cuando un acceso cae en el archivo de registros.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "arm7.h"
#include "arm7jit.h"
#include "aica.h"

/* ------------------------------------------------------------------------ */

#define ONDA_PRUEBA		0x30000

static struct arm7_estado	estado_a, estado_b;
static unsigned char		onda_a[ONDA_PRUEBA], onda_b[ONDA_PRUEBA];

/* Deja el programa en 0 con el reset soltado. Sin el censo de filas: con
   arm7_cobertura puesto, arm7.c corre los bloques por el lazo en C y la
   suite no probaria el codigo emitido. */
static void cargar_jit(const DWORD * programa, int n)
{
	DWORD cero = 0;
	int   i;

	arm7_cobertura = 0;

	aica_reset();
	arm7_reset();

	memset(sound_mem, 0, ONDA_PRUEBA);

	for (i = 0; i < n; i++)
	{
		sound_mem[i * 4 + 0] = (unsigned char) programa[i];
		sound_mem[i * 4 + 1] = (unsigned char) (programa[i] >> 8);
		sound_mem[i * 4 + 2] = (unsigned char) (programa[i] >> 16);
		sound_mem[i * 4 + 3] = (unsigned char) (programa[i] >> 24);
	}

	/* Soltar ARMRST vuelve a resetear el ARM: PC en 0, listo. */
	aica_escribir(AICA_REG_BASE + AICA_ARMRST, &cero, sizeof(cero));
}

/* Las dos corridas y la comparacion entera. */
static void comparar(const DWORD * programa, int n, long ciclos)
{
	arm7_blq_instalar_emisor(NULL);
	cargar_jit(programa, n);
	arm7_ejecutar(ciclos);
	estado_a = arm7;
	memcpy(onda_a, sound_mem, ONDA_PRUEBA);

	arm7jit_iniciar();
	cargar_jit(programa, n);
	arm7_ejecutar(ciclos);
	estado_b = arm7;
	memcpy(onda_b, sound_mem, ONDA_PRUEBA);

	arm7_blq_instalar_emisor(NULL);

	ESPERAR_I32(memcmp(estado_a.r, estado_b.r, sizeof(estado_a.r)) == 0, 1);
	ESPERAR_I32(estado_a.cpsr == estado_b.cpsr, 1);
	ESPERAR_I32(estado_a.spsr == estado_b.spsr, 1);
	ESPERAR_I32(estado_a.banco == estado_b.banco, 1);
	ESPERAR_I32(estado_a.ciclos == estado_b.ciclos, 1);
	ESPERAR_I32(estado_a.instrucciones == estado_b.instrucciones, 1);
	ESPERAR_I32(memcmp(onda_a, onda_b, ONDA_PRUEBA) == 0, 1);
}

/* ------------------------------------------------------------------------ */

static void la_alu_con_inmediato_sin_banderas(void)
{
	static const DWORD p[] =
	{
		0xE3A01005,		/* MOV  r1, #5            */
		0xE3A0207B,		/* MOV  r2, #0x7B         */
		0xE2813003,		/* ADD  r3, r1, #3        */
		0xE204400F,		/* AND  r4, r2, #0x0F     */
		0xE22250FF,		/* EOR  r5, r2, #0xFF     */
		0xE241600A,		/* SUB  r6, r1, #10       */
		0xE2617064,		/* RSB  r7, r1, #100      */
		0xE2A18001,		/* ADC  r8, r1, #1        */
		0xE2C19001,		/* SBC  r9, r1, #1        */
		0xE2E1A000,		/* RSC  r10, r1, #0       */
		0xE382BC0F,		/* ORR  r11, r2, #0xF00   */
		0xE3C2C003,		/* BIC  r12, r2, #3       */
		0xE3E00000,		/* MVN  r0, #0            */
		0xEAFFFFFE,		/* B    .                 */
	};

	comparar(p, (int) (sizeof(p) / sizeof(p[0])), 200);
}

static void la_alu_con_inmediato_y_banderas(void)
{
	static const DWORD p[] =
	{
		0xE3B00000,		/* MOVS r0, #0            (Z)                    */
		0xE3B01102,		/* MOVS r1, #0x80000000   (rot: N y C)           */
		0xE0912001,		/* ADDS r2, r1, r1        (forma reg S=1: por el
						   manejador, adentro del bloque)                */
		0xE2503001,		/* SUBS r3, r0, #1        (N, sin acarreo)       */
		0xE35300FF,		/* CMP  r3, #0xFF                                */
		0xE3730001,		/* CMN  r3, #1            (Z y C)                */
		0xE3110102,		/* TST  r1, #0x80000000   (rot: C)               */
		0xE3310001,		/* TEQ  r1, #1                                   */
		0xE2934002,		/* ADDS r4, r3, #2        (acarreo de salida)    */
		0xE2735000,		/* RSBS r5, r3, #0                               */
		0xE39160FF,		/* ORRS r6, r1, #0xFF                            */
		0xE3D17002,		/* BICS r7, r1, #2                               */
		0xE3F08000,		/* MVNS r8, #0                                   */
		0xE2B09000,		/* ADCS r9, r0, #0        (por el manejador)     */
		0xEAFFFFFE,		/* B    .                                        */
	};

	comparar(p, (int) (sizeof(p) / sizeof(p[0])), 220);
}

static void la_alu_con_desplazamiento_inmediato(void)
{
	static const DWORD p[] =
	{
		0xE3A01081,		/* MOV  r1, #0x81                                */
		0xE1A02201,		/* MOV  r2, r1, LSL #4                           */
		0xE1A030A1,		/* MOV  r3, r1, LSR #1                           */
		0xE1A04041,		/* MOV  r4, r1, ASR #32   (cantidad 0)           */
		0xE1A05021,		/* MOV  r5, r1, LSR #32   (cantidad 0)           */
		0xE1A06461,		/* MOV  r6, r1, ROR #8                           */
		0xE1A07061,		/* MOV  r7, r1, RRX       (ROR #0)               */
		0xE0828081,		/* ADD  r8, r2, r1, LSL #1                       */
		0xE0429001,		/* SUB  r9, r2, r1                               */
		0xE0A1A002,		/* ADC  r10, r1, r2                              */
		0xE0C2B001,		/* SBC  r11, r2, r1                              */
		0xE0E2C001,		/* RSC  r12, r2, r1                              */
		0xE1C20001,		/* BIC  r0, r2, r1                               */
		0xEAFFFFFE,		/* B    .                                        */
	};

	comparar(p, (int) (sizeof(p) / sizeof(p[0])), 200);
}

static void las_condicionales(void)
{
	static const DWORD p[] =
	{
		0xE3A00000,		/* MOV   r0, #0                                  */
		0xE3500000,		/* CMP   r0, #0          (Z=1, C=1)              */
		0x02800001,		/* ADDEQ r0, r0, #1      si                      */
		0x12800001,		/* ADDNE                 no                      */
		0x22800001,		/* ADDCS                 si                      */
		0x32800001,		/* ADDCC                 no                      */
		0x42800001,		/* ADDMI                 no                      */
		0x52800001,		/* ADDPL                 si                      */
		0x62800001,		/* ADDVS                 no                      */
		0x72800001,		/* ADDVC                 si                      */
		0x82800001,		/* ADDHI                 no (Z)                  */
		0x92800001,		/* ADDLS                 si                      */
		0xA2800001,		/* ADDGE                 si                      */
		0xB2800001,		/* ADDLT                 no                      */
		0xC2800001,		/* ADDGT                 no (Z)                  */
		0xD2800001,		/* ADDLE                 si                      */
		0xF2800001,		/* (NV)                  nunca                   */
		0xE3500064,		/* CMP   r0, #100        (N=1, C=0)              */
		0x42800001,		/* ADDMI                 si                      */
		0x22800001,		/* ADDCS                 no                      */
		0xB2800001,		/* ADDLT                 si                      */
		0xC2800001,		/* ADDGT                 no                      */
		0xEAFFFFFE,		/* B     .                                       */
	};

	comparar(p, (int) (sizeof(p) / sizeof(p[0])), 260);
}

static void cargas_almacenamientos_y_bloques(void)
{
	static const DWORD p[] =
	{
		0xE3A01801,		/* MOV   r1, #0x10000                            */
		0xE3A02012,		/* MOV   r2, #0x12                               */
		0xE3822C34,		/* ORR   r2, r2, #0x3400                         */
		0xE5812000,		/* STR   r2, [r1]                                */
		0xE5A12004,		/* STR   r2, [r1, #4]!    (pre con writeback)    */
		0xE5C12001,		/* STRB  r2, [r1, #1]                            */
		0xE4812008,		/* STR   r2, [r1], #8     (post)                 */
		0xE581F000,		/* STR   pc, [r1]         (guarda PC+12)         */
		0xE5113008,		/* LDR   r3, [r1, #-8]                           */
		0xE5514008,		/* LDRB  r4, [r1, #-8]                           */
		0xE5115007,		/* LDR   r5, [r1, #-7]    (desalineada: rota)    */
		0xE59F6004,		/* LDR   r6, [pc, #4]     (relativo al PC)       */
		0xE8A1001C,		/* STMIA r1!, {r2,r3,r4}                         */
		0xE9310380,		/* LDMDB r1!, {r7,r8,r9}                         */
		0xEAFFFFFE,		/* B     .                                       */
		0x00000000,		/*   (relleno)                                   */
		0xDEADBEEF,		/*   .word: lo que lee el LDR relativo al PC     */
	};

	comparar(p, (int) (sizeof(p) / sizeof(p[0])), 300);
}

static void la_salida_lateral_del_archivo_de_registros(void)
{
	static const DWORD p[] =
	{
		0xE3A06880,		/* MOV  r6, #0x00800000   (el archivo)           */
		0xE3A02007,		/* MOV  r2, #7                                   */
		0xE3A03000,		/* MOV  r3, #0                                   */
		0xE5862000,		/* STR  r2, [r6]          (toca el archivo)      */
		0xE2833001,		/* ADD  r3, r3, #1        (tras la frontera)     */
		0xE2833002,		/* ADD  r3, r3, #2                               */
		0xE5862004,		/* STR  r2, [r6, #4]                             */
		0xE2833004,		/* ADD  r3, r3, #4                               */
		0xEAFFFFFE,		/* B    .                                        */
	};

	comparar(p, (int) (sizeof(p) / sizeof(p[0])), 200);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(la_alu_con_inmediato_sin_banderas),
	CASO(la_alu_con_inmediato_y_banderas),
	CASO(la_alu_con_desplazamiento_inmediato),
	CASO(las_condicionales),
	CASO(cargas_almacenamientos_y_bloques),
	CASO(la_salida_lateral_del_archivo_de_registros),
};

const dc_suite suite_arm7jit = DEFINIR_SUITE("arm7jit", casos);
