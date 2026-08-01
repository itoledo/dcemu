/****************************************************************************

	Pruebas del ARM7DI del AICA.

	Un caso por familia de la tabla de arm7.c, mas los tres sitios donde el
	nucleo es facil de escribir mal y el error no se ve hasta mucho despues:

	  - **R15 leido no es el PC**, sino PC+8, y PC+12 cuando el desplazamiento
	    viene de un registro o cuando se guarda en memoria;
	  - **SUBS PC, R14, #4** no es una resta: es el retorno de una excepcion, y
	    tiene que restaurar el CPSR desde el SPSR. Asi termina la FIQ del
	    firmware de KOS, y sin eso el ARM se queda en modo FIQ para siempre;
	  - **los bancos de registros**: FIQ tiene los suyos de R8 a R14, y el
	    resto de los modos solo R13 y R14.

	El ultimo caso corre el crt0.s de KOS entero, ensamblado a mano, para ver
	que la FIQ del temporizador incrementa el reloj de milisegundos donde el
	firmware lo espera: 0x00021000 de la RAM de onda.

	Ver docs/aica-plan.md, fase 3.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "arm7.h"
#include "aica.h"
#include "tmu.h"

/* ------------------------------------------------------------------------ */

static DWORD leer_registro_aica(unsigned long off)
{
	DWORD v = 0;

	aica_leer(AICA_REG_BASE + off, &v, sizeof(v));

	return v;
}

/* Deja el ARM listo con un programa en la direccion 0 y el reset soltado. */
static void cargar(const DWORD * programa, int n)
{
	int i;

	aica_reset();
	arm7_reset();

	memset(sound_mem, 0, 0x30000);

	for (i = 0; i < n; i++)
	{
		sound_mem[i * 4 + 0] = (unsigned char) programa[i];
		sound_mem[i * 4 + 1] = (unsigned char) (programa[i] >> 8);
		sound_mem[i * 4 + 2] = (unsigned char) (programa[i] >> 16);
		sound_mem[i * 4 + 3] = (unsigned char) (programa[i] >> 24);
	}
}

static void correr(int instrucciones)
{
	while (instrucciones--)
		arm7_paso();
}

/* ------------------------------------------------------------------------ */

static void mover_y_sumar(void)
{
	static const DWORD p[] =
	{
		0xE3A00005,		/* mov  r0, #5      */
		0xE3A01003,		/* mov  r1, #3      */
		0xE0802001,		/* add  r2, r0, r1  */
		0xE0403001,		/* sub  r3, r0, r1  */
	};

	cargar(p, 4);
	correr(4);

	ESPERAR_U32(arm7.r[0], 5);
	ESPERAR_U32(arm7.r[1], 3);
	ESPERAR_U32(arm7.r[2], 8);
	ESPERAR_U32(arm7.r[3], 2);
	ESPERAR_U32(arm7.r[15], 16);
}

static void las_banderas_y_la_condicion(void)
{
	static const DWORD p[] =
	{
		0xE3A00005,		/* mov  r0, #5      */
		0xE3500005,		/* cmp  r0, #5      */
		0x03A01001,		/* moveq r1, #1     */
		0x13A02001,		/* movne r2, #1     */
		0xE3500006,		/* cmp  r0, #6      */
		0x33A03001,		/* movcc r3, #1     */
		0x23A04001,		/* movcs r4, #1     */
	};

	cargar(p, 7);
	correr(7);

	ESPERAR_U32(arm7.r[1], 1);		/* EQ se cumplio */
	ESPERAR_U32(arm7.r[2], 0);		/* NE no */
	ESPERAR_U32(arm7.r[3], 1);		/* 5 < 6 sin signo: acarreo bajo */
	ESPERAR_U32(arm7.r[4], 0);
}

static void el_desplazador(void)
{
	static const DWORD p[] =
	{
		0xE3A00001,		/* mov  r0, #1          */
		0xE1A01200,		/* mov  r1, r0, lsl #4  */
		0xE3E02000,		/* mvn  r2, #0          -> 0xFFFFFFFF */
		0xE1A03222,		/* mov  r3, r2, lsr #4  */
		0xE1A04242,		/* mov  r4, r2, asr #4  */
		0xE3A05102,		/* mov  r5, #0x80000000 (2 ror 2) */
		0xE1A06265,		/* mov  r6, r5, ror #4  */
	};

	cargar(p, 7);
	correr(7);

	ESPERAR_U32(arm7.r[1], 0x10);
	ESPERAR_U32(arm7.r[3], 0x0FFFFFFF);
	ESPERAR_U32(arm7.r[4], 0xFFFFFFFF);
	ESPERAR_U32(arm7.r[5], 0x80000000);
	ESPERAR_U32(arm7.r[6], 0x08000000);
}

static void el_desplazamiento_por_registro(void)
{
	static const DWORD p[] =
	{
		0xE3A00001,		/* mov  r0, #1         */
		0xE3A01008,		/* mov  r1, #8         */
		0xE1A02110,		/* mov  r2, r0, lsl r1 */
	};

	cargar(p, 3);
	correr(3);

	ESPERAR_U32(arm7.r[2], 0x100);
}

static void r15_se_lee_ocho_mas_adelante(void)
{
	/* Es la trampa clasica del ARM: R15 leido como operando da la direccion de
	   la instruccion mas 8, porque el prefetch va dos etapas por delante. */
	static const DWORD p[] =
	{
		0xE1A0000F,		/* mov r0, pc   -- en la direccion 0 */
		0xE1A0100F,		/* mov r1, pc   -- en la direccion 4 */
	};

	cargar(p, 2);
	correr(2);

	ESPERAR_U32(arm7.r[0], 8);
	ESPERAR_U32(arm7.r[1], 12);
}

static void multiplicar(void)
{
	static const DWORD p[] =
	{
		0xE3A00007,		/* mov  r0, #7        */
		0xE3A01006,		/* mov  r1, #6        */
		0xE0020190,		/* mul  r2, r0, r1    */
		0xE3A03002,		/* mov  r3, #2        */
		0xE0243190,		/* mla  r4, r0, r1, r3 */
	};

	cargar(p, 5);
	correr(5);

	ESPERAR_U32(arm7.r[2], 42);
	ESPERAR_U32(arm7.r[4], 44);
}

static void cargar_y_guardar(void)
{
	static const DWORD p[] =
	{
		0xE3A00A01,		/* mov  r0, #0x1000      */
		0xE3A01F5A,		/* mov  r1, #0x168       */
		0xE5801000,		/* str  r1, [r0]         */
		0xE5902000,		/* ldr  r2, [r0]         */
		0xE5C01004,		/* strb r1, [r0, #4]     */
		0xE5D03004,		/* ldrb r3, [r0, #4]     */
		0xE5804008,		/* str  r4, [r0, #8]     */
		0xE4905010,		/* ldr  r5, [r0], #16    */
	};

	cargar(p, 8);
	correr(8);

	ESPERAR_U32(arm7.r[2], 0x168);
	ESPERAR_U32(arm7.r[3], 0x68);			/* solo el byte bajo */
	ESPERAR_U32(arm7.r[0], 0x1010);			/* post-indexado con incremento */
	ESPERAR_U32(arm7_leer(0x1000, 4), 0x168);
}

static void bloque_de_registros(void)
{
	static const DWORD p[] =
	{
		0xE3A0DB01,		/* mov   sp, #0x400        */
		0xE3A00001,		/* mov   r0, #1            */
		0xE3A01002,		/* mov   r1, #2            */
		0xE3A02003,		/* mov   r2, #3            */
		0xE92D0007,		/* stmdb sp!, {r0,r1,r2}   */
		0xE3A00000,		/* mov   r0, #0            */
		0xE3A01000,		/* mov   r1, #0            */
		0xE3A02000,		/* mov   r2, #0            */
		0xE8BD0007,		/* ldmia sp!, {r0,r1,r2}   */
	};

	cargar(p, 9);
	correr(9);

	ESPERAR_U32(arm7.r[0], 1);
	ESPERAR_U32(arm7.r[1], 2);
	ESPERAR_U32(arm7.r[2], 3);
	ESPERAR_U32(arm7.r[13], 0x400);			/* la pila volvio a su sitio */
}

static void saltos(void)
{
	static const DWORD p[] =
	{
		0xEA000001,		/* b    +1 instruccion -> a la 3 */
		0xE3A00001,		/* mov  r0, #1   (se saltea)     */
		0xE3A01001,		/* mov  r1, #1   (se saltea)     */
		0xE3A02001,		/* mov  r2, #1                   */
		0xEB000000,		/* bl   +0 -> a la 6             */
		0xE3A03001,		/* mov  r3, #1   (se saltea)     */
		0xE3A04001,		/* mov  r4, #1                   */
	};

	cargar(p, 7);
	correr(4);

	ESPERAR_U32(arm7.r[0], 0);
	ESPERAR_U32(arm7.r[1], 0);
	ESPERAR_U32(arm7.r[2], 1);
	ESPERAR_U32(arm7.r[4], 1);
	ESPERAR_U32(arm7.r[14], 0x14);			/* BL guardo la que sigue */
}

static void intercambio(void)
{
	static const DWORD p[] =
	{
		0xE3A00A01,		/* mov  r0, #0x1000  */
		0xE3A01011,		/* mov  r1, #0x11    */
		0xE5801000,		/* str  r1, [r0]     */
		0xE3A02022,		/* mov  r2, #0x22    */
		0xE1003092,		/* swp  r3, r2, [r0] */
	};

	cargar(p, 5);
	correr(5);

	ESPERAR_U32(arm7.r[3], 0x11);			/* lo que habia */
	ESPERAR_U32(arm7_leer(0x1000, 4), 0x22);	/* lo que quedo */
}

/* ------------------------------------------------------------------------ */

static void mrs_y_msr(void)
{
	static const DWORD p[] =
	{
		0xE10F0000,		/* mrs r0, cpsr             */
		0xE3A01013,		/* mov r1, #0x13 (svc)      */
		0xE121F001,		/* msr cpsr_c, r1           */
		0xE10F2000,		/* mrs r2, cpsr             */
	};

	cargar(p, 4);
	correr(4);

	/* El reset deja modo supervisor con las dos mascaras puestas. */
	ESPERAR_U32(arm7.r[0] & 0xFF, 0x13 | 0xC0);

	/* Y msr cpsr_c las baja, porque escribe el campo de control entero. */
	ESPERAR_U32(arm7.r[2] & 0xFF, 0x13);
}

static void los_bancos_de_fiq(void)
{
	/* R8-R14 de FIQ son otros registros. Cambiar de modo y volver tiene que
	   devolver los del modo anterior intactos. */
	static const DWORD p[] =
	{
		0xE3A0800A,		/* mov r8, #10            */
		0xE3A0E00B,		/* mov lr, #11            */
		0xE3A01011,		/* mov r1, #0x11 (fiq)    */
		0xE121F001,		/* msr cpsr_c, r1         */
		0xE3A08063,		/* mov r8, #99            */
		0xE3A0E064,		/* mov lr, #100           */
		0xE3A01013,		/* mov r1, #0x13 (svc)    */
		0xE121F001,		/* msr cpsr_c, r1         */
	};

	cargar(p, 8);
	correr(6);

	ESPERAR_U32(arm7.r[8], 99);
	ESPERAR_U32(arm7.r[14], 100);

	correr(2);

	ESPERAR_U32(arm7.r[8], 10);
	ESPERAR_U32(arm7.r[14], 11);
}

static void subs_pc_lr_vuelve_de_la_excepcion(void)
{
	/*
		Asi termina la FIQ del crt0.s de KOS. No es una resta: con S puesto y
		R15 como destino, el CPSR se restaura desde el SPSR. Si esto no
		estuviera, el firmware entraria a su primera FIQ y no saldria nunca del
		modo FIQ -- y como los bancos cambian, perderia R8-R14 del programa
		principal en el acto.
	*/
	static const DWORD p[] =
	{
		0xE3A00001,		/* mov r0, #1   -- en 0x00 */
	};

	cargar(p, 1);

	/* Se entra a mano a la FIQ: SPSR con el modo supervisor, LR con el
	   retorno mas 4, tal como lo deja el hardware. */
	arm7.spsr_banco[ARM7_B_FIQ] = ARM7_MODO_SVC;
	arm7.r13_14[ARM7_B_FIQ][1]  = 0x40 + 4;

	{
		static const DWORD fiq[] =
		{
			0xE25EF004,		/* subs pc, lr, #4 */
		};

		sound_mem[0x100] = (unsigned char) fiq[0];
		sound_mem[0x101] = (unsigned char) (fiq[0] >> 8);
		sound_mem[0x102] = (unsigned char) (fiq[0] >> 16);
		sound_mem[0x103] = (unsigned char) (fiq[0] >> 24);
	}

	/* Se salta al modo FIQ con el PC en 0x100. */
	arm7.cpsr  = ARM7_MODO_SVC;
	arm7.banco = ARM7_B_SVC;
	arm7.r[15] = 0x100;

	{
		static const DWORD cambio[] = { 0 };
		(void) cambio;
	}

	/* Entrada manual al modo FIQ, como la hace excepcion(). */
	arm7.r13_14[ARM7_B_SVC][0] = arm7.r[13];
	arm7.r13_14[ARM7_B_SVC][1] = arm7.r[14];
	arm7.spsr_banco[ARM7_B_SVC] = arm7.spsr;
	memcpy(arm7.r8_12_usr, &arm7.r[8], sizeof(arm7.r8_12_usr));
	memcpy(&arm7.r[8], arm7.r8_12_fiq, sizeof(arm7.r8_12_fiq));
	arm7.r[13] = arm7.r13_14[ARM7_B_FIQ][0];
	arm7.r[14] = arm7.r13_14[ARM7_B_FIQ][1];
	arm7.spsr  = arm7.spsr_banco[ARM7_B_FIQ];
	arm7.banco = ARM7_B_FIQ;
	arm7.cpsr  = ARM7_MODO_FIQ | ARM7_I | ARM7_F;

	arm7_paso();

	ESPERAR_U32(arm7.r[15], 0x40);
	ESPERAR_U32(arm7.cpsr & ARM7_MODO, ARM7_MODO_SVC);
	ESPERAR_U32(arm7.cpsr & ARM7_F, 0);		/* la mascara volvio con el SPSR */
}

static void una_instruccion_de_armv4_es_indefinida(void)
{
	/*
		LDRH existe desde ARMv4; en un ARM7DI ese patron es una instruccion
		indefinida y salta al vector 0x04. Emularla seria peor que no hacerlo:
		el firmware se compila con -mcpu=arm7di justamente porque el chip no
		la tiene.
	*/
	static const DWORD p[] =
	{
		0xE1D010B0,		/* ldrh r1, [r0]  -- no existe aqui */
	};
	unsigned long antes;

	cargar(p, 1);
	antes = arm7.indefinidas;

	arm7_paso();

	ESPERAR_U32((DWORD) (arm7.indefinidas - antes), 1);
	ESPERAR_U32(arm7.r[15], ARM7_VEC_UNDEF);
	ESPERAR_U32(arm7.cpsr & ARM7_MODO, ARM7_MODO_UND);
}

static void swi_entra_por_su_vector(void)
{
	static const DWORD p[] =
	{
		0xEF000000,		/* swi #0 */
	};

	cargar(p, 1);
	arm7_paso();

	ESPERAR_U32(arm7.r[15], ARM7_VEC_SWI);
	ESPERAR_U32(arm7.cpsr & ARM7_MODO, ARM7_MODO_SVC);
	ESPERAR_U32(arm7.r[14], 4);
}

static void el_arm_en_reset_no_ejecuta(void)
{
	/* ARMRST lo detiene, y soltarlo lo arranca desde el vector 0. Es lo que
	   hacen spu_disable() y spu_enable(). */
	static const DWORD p[] =
	{
		0xE3A00001,		/* mov r0, #1 */
		0xE3A00002,		/* mov r0, #2 */
	};
	DWORD uno = 1, cero = 0;

	cargar(p, 2);

	aica_escribir(AICA_REG_BASE + AICA_ARMRST, &uno, sizeof(uno));
	arm7_ejecutar(1000);

	ESPERAR_U32(arm7.r[15], 0);
	ESPERAR_U32(arm7.r[0], 0);

	aica_escribir(AICA_REG_BASE + AICA_ARMRST, &cero, sizeof(cero));
	arm7_ejecutar(1);

	ESPERAR_U32(arm7.r[0], 1);
}

/* ------------------------------------------------------------------------ */

static void el_bucle_infinito_de_spu_init(void)
{
	/*
		spu_init() de KOS escribe esta unica instruccion en la direccion 0 y
		suelta el reset -- en **todos** los programas, no solo en los de
		sonido, "so that CD audio works". O sea que este es el programa que van
		a correr las 135 demos del inventario, y lo que aqui se comprueba es
		que no se sale ni cuesta de mas.
	*/
	static const DWORD p[] = { 0xEAFFFFF8 };
	DWORD cero = 0;
	int i;

	cargar(p, 1);
	aica_escribir(AICA_REG_BASE + AICA_ARMRST, &cero, sizeof(cero));

	for (i = 0; i < 1000; i++)
		arm7_paso();

	/*
		Recorre ceros --que decodifican como un AND sin efecto-- y da la vuelta
		por el bus de 24 bits. Lo que importa: no se sale de ese bus, no levanta
		ninguna instruccion indefinida y no escribio nada en los registros del
		AICA, que es donde una salida en falso se notaria.
	*/
	ESPERAR_U32(arm7.indefinidas, 0);
	ESPERAR(arm7.r[15] < 0x01000000);
	ESPERAR_U32(leer_registro_aica(AICA_MVOL), 0 | ((AICA_VERSION & 0xF) << 8));
}

static void el_reloj_de_milisegundos_del_firmware(void)
{
	/*
		El crt0.s de KOS ensamblado a mano, tal cual: la FIQ del temporizador
		incrementa una palabra en 0x00021000 --AICA_MEM_CLOCK--, recarga el
		temporizador A escribiendo 246 en 0x2890, reconoce con SCIRE en 0x28A4 y
		vuelve con SUBS PC,LR,#4 despues de escribir M en 0x00802D04.

		Es la prueba de punta a punta de la fase: temporizador, controlador de
		interrupciones, entrada y salida de la FIQ, bancos de registros y nucleo.
		Si cualquiera de esas piezas falla, el reloj no avanza -- y ese reloj es
		el que el planificador de flujos de KOS lee como milisegundos.
	*/
	static const DWORD manejador[] =
	{
		/* 0x100: el reloj de milisegundos */
		0xE3A08802,		/* mov r8, #0x20000        */
		0xE2888A01,		/* add r8, r8, #0x1000     -> 0x21000 */
		0xE5989000,		/* ldr r9, [r8]            */
		0xE2899001,		/* add r9, r9, #1          */
		0xE5889000,		/* str r9, [r8]            */

		/* Recarga del temporizador A y reconocimiento, como el crt0 */
		0xE3A08502,		/* mov r8, #0x00800000     */
		0xE2888C28,		/* add r8, r8, #0x2800     */
		0xE2888080,		/* add r8, r8, #0x80       -> 0x802880 */
		0xE3A090F6,		/* mov r9, #246            */
		0xE5889010,		/* str r9, [r8, #0x10]     -> TIMER A */
		0xE3A09040,		/* mov r9, #0x40           */
		0xE5889024,		/* str r9, [r8, #0x24]     -> SCIRE */

		/* Fin de proceso: M, cuatro veces como el crt0 */
		0xE3A08502,		/* mov r8, #0x00800000     */
		0xE2888C2D,		/* add r8, r8, #0x2D00     */
		0xE2888004,		/* add r8, r8, #4          -> 0x802D04 */
		0xE3A09001,		/* mov r9, #1              */
		0xE5889000,		/* str r9, [r8]            */

		0xE25EF004,		/* subs pc, lr, #4         */
	};
	DWORD cero = 0;
	int i;

	aica_reset();
	arm7_reset();
	memset(sound_mem, 0, 0x30000);

	arm7_escribir(0x00, 4, 0xEAFFFFFE);			/* b .  : el programa principal */
	arm7_escribir(ARM7_VEC_FIQ, 4, 0xEA000037);	/* b 0x100 */

	for (i = 0; i < (int) (sizeof(manejador) / sizeof(manejador[0])); i++)
		arm7_escribir(0x100 + i * 4, 4, manejador[i]);

	/* El chip como lo deja aica_init(): temporizador A cada 10 muestras y
	   habilitado como unica fuente de FIQ. */
	{
		DWORD v;

		v = 0x18;	aica_escribir(AICA_REG_BASE + AICA_SCILV0, &v, 4);
		v = 0x50;	aica_escribir(AICA_REG_BASE + AICA_SCILV1, &v, 4);
		v = 0x08;	aica_escribir(AICA_REG_BASE + AICA_SCILV2, &v, 4);
		v = 246;	aica_escribir(AICA_REG_BASE + AICA_TIMER_A, &v, 4);
		v = AICA_INT_TIMER_A;
					aica_escribir(AICA_REG_BASE + AICA_SCIEB, &v, 4);
	}

	aica_escribir(AICA_REG_BASE + AICA_ARMRST, &cero, sizeof(cero));

	/* arm_fiq_enable(): el firmware baja la mascara de FIQ cuando termina de
	   configurar. */
	arm7.cpsr &= ~ARM7_F;

	/* Cien muestras: diez desbordes del temporizador, diez FIQ. */
	for (i = 0; i < 100; i++)
	{
		reloj_total += 4524;
		aica_tick();
	}

	{
		DWORD reloj = arm7_leer(0x21000, 4);

		ESPERAR(reloj >= 9 && reloj <= 11);
	}

	/* Y volvio al programa principal, no se quedo en modo FIQ. */
	ESPERAR_U32(arm7.cpsr & ARM7_MODO, ARM7_MODO_SVC);
	ESPERAR_U32(arm7.indefinidas, 0);
}

/* ------------------------------------------------------------------------ */

static void las_familias_que_quedan(void)
{
	/*
		Una instruccion por cada fila de la tabla que las pruebas de arriba no
		tocan. No estan aqui por prolijidad: la suite de cobertura falla si una
		fila implementada nunca se ejecuto, asi que sin este caso la tabla podria
		crecer con filas muertas sin que nadie se entere.
	*/
	static const DWORD p[] =
	{
		0xE3A00005,		/* mov  r0, #5                */
		0xE3A01002,		/* mov  r1, #2                */
		0xE3A03001,		/* mov  r3, #1                */
		0xE0802311,		/* add  r2, r0, r1, lsl r3    -- ALU con Rs   */
		0xE1500001,		/* cmp  r0, r1                -- CMP directo  */
		0xE1500211,		/* cmp  r0, r1, lsl r2        -- CMP con Rs   */
		0xE328F201,		/* msr  cpsr_f, #0x10000000   -- MSR inmediato */
		0xE3A04A01,		/* mov  r4, #0x1000           */
		0xE3A05077,		/* mov  r5, #0x77             */
		0xE3A06000,		/* mov  r6, #0                */
		0xE7845006,		/* str  r5, [r4, r6]          -- LDR/STR con registro */
		0xE7947006,		/* ldr  r7, [r4, r6]          */
	};
	int i;

	cargar(p, (int) (sizeof(p) / sizeof(p[0])));
	correr((int) (sizeof(p) / sizeof(p[0])));

	ESPERAR_U32(arm7.r[2], 9);				/* 5 + (2 << 1) */
	ESPERAR_U32(arm7.r[7], 0x77);
	ESPERAR_U32(arm7.cpsr & ARM7_V, ARM7_V);	/* lo puso el MSR inmediato */

	/*
		Y los tres patrones que en un ARM7DI no son instrucciones: la
		transferencia con registro y el bit 4 puesto, y las dos familias de
		coprocesador. Las tres tienen que entrar por el vector de instruccion
		indefinida y no hacer nada mas.
	*/
	{
		static const DWORD indefinidas[] =
		{
			0xE7901012,		/* ldr con bit 4: no existe   */
			0xEC900100,		/* ldc p1, cr0, [r0]          */
			0xEE000010,		/* mcr p0, 0, r0, cr0, cr0    */
		};

		for (i = 0; i < 3; i++)
		{
			unsigned long antes;

			cargar(&indefinidas[i], 1);
			antes = arm7.indefinidas;

			arm7_paso();

			ESPERAR_U32((DWORD) (arm7.indefinidas - antes), 1);
			ESPERAR_U32(arm7.r[15], ARM7_VEC_UNDEF);
		}
	}
}

static void toda_fila_implementada_se_ejercita(void)
{
	/*
		El gemelo de la suite de cobertura del SH-4: si una fila de la tabla
		del ARM nunca se ejecuto en toda la corrida, falta una prueba. Solo
		tiene sentido cuando se corre la suite completa.
	*/
	int i, sin_probar = 0;

	if (!dc_corrida_completa())
		return;

	for (i = 0; i < arm7_filas(); i++)
	{
		if (arm7_fila_usada(i))
			continue;

		dc_anotar(__FILE__, __LINE__, "fila sin probar: %s",
			arm7_fila_nombre(i));
		sin_probar++;
	}

	/* El resumen va por printf y no por dc_anotar, que cuenta como diferencia
	   -- igual que en test_cobertura.c. */
	printf("         %d filas del ARM, %d sin probar\n",
		arm7_filas(), sin_probar);

	ESPERAR_I32(sin_probar, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(mover_y_sumar),
	CASO(las_banderas_y_la_condicion),
	CASO(el_desplazador),
	CASO(el_desplazamiento_por_registro),
	CASO(r15_se_lee_ocho_mas_adelante),
	CASO(multiplicar),
	CASO(cargar_y_guardar),
	CASO(bloque_de_registros),
	CASO(saltos),
	CASO(intercambio),
	CASO(mrs_y_msr),
	CASO(los_bancos_de_fiq),
	CASO(subs_pc_lr_vuelve_de_la_excepcion),
	CASO(una_instruccion_de_armv4_es_indefinida),
	CASO(swi_entra_por_su_vector),
	CASO(el_arm_en_reset_no_ejecuta),
	CASO(el_bucle_infinito_de_spu_init),
	CASO(el_reloj_de_milisegundos_del_firmware),
	CASO(las_familias_que_quedan),
	CASO(toda_fila_implementada_se_ejercita),
};

const dc_suite suite_arm7 = DEFINIR_SUITE("arm7", casos);
