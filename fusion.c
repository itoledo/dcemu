/****************************************************************************

	FUSION - el prototipo desechable de bloques fusionados

	Ver fusion.h. El lazo que traduce, desensamblado y volcado del banco de
	Crazy Taxi el 2026-08-07 (docs/rendimiento-plan-2.md, fase 4):

	  0c1583f8: D321  MOV.L @(c158480), R3      ; +2
	  0c1583fa: 6232  MOV.L @R3, R2             ; +2
	  0c1583fc: 420B  JSR @R2                   ; +3, PR = 0c158400
	  0c1583fe: 5431    MOV.L @(4, R3), R4      ; +1 (ranura)
	  0c156c30: 000B  RTS                       ; +3 (el callback es esto solo)
	  0c156c32: 0009    NOP                     ; +0 (ranura)
	  0c158400: D120  MOV.L @(c158484), R1      ; +2
	  0c158402: 6312  MOV.L @R1, R3             ; +2
	  0c158404: D020  MOV.L @(c158488), R0      ; +2
	  0c158406: 6202  MOV.L @R0, R2             ; +2
	  0c158408: D11B  MOV.L @(c158478), R1      ; +2
	  0c15840a: 323C  ADD R3, R2                ; +1
	  0c15840c: 7201  ADD #1, R2                ; +1
	  0c15840e: 6312  MOV.L @R1, R3             ; +2
	  0c158410: 3326  CMP/HI R2, R3             ; +1, T = R3 > R2 (sin signo)
	  0c158412: 8B01  BF c158418                ; +2; con T sale a 0c158414
	  0c158418: D318  MOV.L @(c15847c), R3      ; +2
	  0c15841a: 6232  MOV.L @R3, R2             ; +2
	  0c15841c: 2228  TST R2, R2                ; +1, T = (R2 == 0)
	  0c15841e: 8BEB  BF c1583f8                ; +2; con T sale a 0c158420

	Es el lazo de espera del juego: sondea dos contadores y un puntero de
	callback que apunta a un RTS pelado. 535 millones de vueltas en 180 s
	emulados, 20 instrucciones y 35 ciclos por vuelta.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "fusion.h"

#ifdef DCEMU_FUSION

#include "sh4emu.h"
#include "mem.h"
#include "intc.h"		/* intc_sh4_reintentar */
#include "tmu.h"		/* RELOJ_GRANO */
#include "perf.h"

int fusion_activa = 0;

/* Cuantas instrucciones corrieron fusionadas y cuantas entradas hubo: sin
   esto el resultado no se puede interpretar (la fraccion cubierta es el
   denominador de la extrapolacion). */
static unsigned long long fusion_instr    = 0;
static unsigned long long fusion_entradas = 0;

static void fusion_resumen(void)
{
	if (fusion_entradas)
		fprintf(stderr, "fusion: %llu instrucciones fusionadas en %llu"
			" entradas (%.1f por entrada)\n",
			fusion_instr, fusion_entradas,
			(double) fusion_instr / (double) fusion_entradas);
}

void fusion_iniciar(void)
{
	const char * v = getenv("DCEMU_FUSION");

	fusion_activa = (v != NULL && atoi(v) != 0);

	if (fusion_activa)
	{
		fprintf(stderr, "fusion: el lazo de %08lx corre fusionado"
			" (DCEMU_FUSION=1)\n", (unsigned long) FUSION_CT_ENTRADA);
		atexit(fusion_resumen);
	}
}

/*
	El codigo que la fusion reproduce, palabra por palabra. Se verifica entero
	en la primera entrada -- si el juego cargado no es el del banco, la sonda
	simplemente no corre -- y la primera palabra en cada una, contra el codigo
	automodificado. Las dos palabras de 0c158414/16 no se ejecutan nunca pero
	se verifican igual: un parche del guest sobre la region es motivo para
	devolverle el lazo al interprete.
*/
static const struct { DWORD dir; WORD palabra; } fusion_region[] =
{
	{ 0x0C1583F8ul, 0xD321 }, { 0x0C1583FAul, 0x6232 },
	{ 0x0C1583FCul, 0x420B }, { 0x0C1583FEul, 0x5431 },
	{ 0x0C158400ul, 0xD120 }, { 0x0C158402ul, 0x6312 },
	{ 0x0C158404ul, 0xD020 }, { 0x0C158406ul, 0x6202 },
	{ 0x0C158408ul, 0xD11B }, { 0x0C15840Aul, 0x323C },
	{ 0x0C15840Cul, 0x7201 }, { 0x0C15840Eul, 0x6312 },
	{ 0x0C158410ul, 0x3326 }, { 0x0C158412ul, 0x8B01 },
	{ 0x0C158414ul, 0xA004 }, { 0x0C158416ul, 0x6CE3 },
	{ 0x0C158418ul, 0xD318 }, { 0x0C15841Aul, 0x6232 },
	{ 0x0C15841Cul, 0x2228 }, { 0x0C15841Eul, 0x8BEB },
	{ 0x0C156C30ul, 0x000B }, { 0x0C156C32ul, 0x0009 },
};

static int fusion_verificada = 0;

/*
	El corte del bloque periodico, en la misma frontera en que el interprete
	lo evaluaria: despues de cada instruccion despachada (las ranuras van
	pegadas a su salto, como en branch.c). Si corresponde cortar, PC queda en
	la instruccion siguiente y el interprete retoma exactamente ahi.
*/
#define CORTE(pc_sig)													\
	do																	\
	{																	\
		if (cyc >= RELOJ_GRANO || intc_sh4_reintentar)					\
		{																\
			PC = (pc_sig);												\
			goto salir;													\
		}																\
	} while (0)

/* La salida al interprete sin cortar: la proxima instruccion no esta cubierta
   por la fusion (el callback desconocido, una salida del lazo, una direccion
   desalineada que tiene que levantar su error por el camino de siempre). */
#define SALIR_EN(pc_sig)												\
	do { PC = (pc_sig); goto salir; } while (0)

int fusion_lazo_ct(void)
{
	DWORD r0, r1, r2, r3, r4, pr;
	DWORD cyc;
	unsigned long n = 0;

	if (*(WORD *) get_memory_pointer(FUSION_CT_ENTRADA) != 0xD321)
		return 0;

	if (!fusion_verificada)
	{
		size_t i;

		for (i = 0; i < sizeof(fusion_region) / sizeof(fusion_region[0]); i++)
			if (*(WORD *) get_memory_pointer(fusion_region[i].dir)
				!= fusion_region[i].palabra)
			{
				fprintf(stderr, "fusion: la palabra de %08lx no es %04x;"
					" la sonda no corre\n",
					(unsigned long) fusion_region[i].dir,
					fusion_region[i].palabra);
				fusion_activa = 0;
				return 0;
			}

		fusion_verificada = 1;
	}

	r0  = R(0); r1 = R(1); r2 = R(2); r3 = R(3); r4 = R(4);
	pr  = PR;
	cyc = core.context.cycles;

por_vuelta:
	/* 0c1583f8: MOV.L @(c158480), R3 */
	ReadMemoryL(0x0C158480ul, &r3); cyc += 2; n++;
	CORTE(0x0C1583FAul);

	/* 0c1583fa: MOV.L @R3, R2 */
	if (r3 & 3)
		SALIR_EN(0x0C1583FAul);
	ReadMemoryL(r3, &r2); cyc += 2; n++;
	CORTE(0x0C1583FCul);

	/* 0c1583fc: JSR @R2 + ranura MOV.L @(4,R3), R4 -- solo si el destino es
	   el RTS conocido; cualquier otro callback vuelve al interprete ANTES de
	   ejecutar nada del salto. La ranura comparte la alineacion de R3, ya
	   probada. */
	if (r2 != 0x0C156C30ul)
		SALIR_EN(0x0C1583FCul);
	pr = 0x0C158400ul;
	ReadMemoryL(r3 + 4, &r4); cyc += 3 + 1; n += 2;
	CORTE(0x0C156C30ul);

	/* 0c156c30: RTS + ranura NOP (que no suma ciclos: es el manejador `nop`).
	   El destino es el PR que el JSR de arriba acaba de dejar. */
	cyc += 3; n += 2;
	CORTE(0x0C158400ul);

	/* 0c158400: MOV.L @(c158484), R1 */
	ReadMemoryL(0x0C158484ul, &r1); cyc += 2; n++;
	CORTE(0x0C158402ul);

	/* 0c158402: MOV.L @R1, R3 */
	if (r1 & 3)
		SALIR_EN(0x0C158402ul);
	ReadMemoryL(r1, &r3); cyc += 2; n++;
	CORTE(0x0C158404ul);

	/* 0c158404: MOV.L @(c158488), R0 */
	ReadMemoryL(0x0C158488ul, &r0); cyc += 2; n++;
	CORTE(0x0C158406ul);

	/* 0c158406: MOV.L @R0, R2 */
	if (r0 & 3)
		SALIR_EN(0x0C158406ul);
	ReadMemoryL(r0, &r2); cyc += 2; n++;
	CORTE(0x0C158408ul);

	/* 0c158408: MOV.L @(c158478), R1 */
	ReadMemoryL(0x0C158478ul, &r1); cyc += 2; n++;
	CORTE(0x0C15840Aul);

	/* 0c15840a: ADD R3, R2 */
	r2 += r3; cyc += 1; n++;
	CORTE(0x0C15840Cul);

	/* 0c15840c: ADD #1, R2 */
	r2 += 1; cyc += 1; n++;
	CORTE(0x0C15840Eul);

	/* 0c15840e: MOV.L @R1, R3 */
	if (r1 & 3)
		SALIR_EN(0x0C15840Eul);
	ReadMemoryL(r1, &r3); cyc += 2; n++;
	CORTE(0x0C158410ul);

	/* 0c158410: CMP/HI R2, R3 */
	SR_T = ((DWORD) r3 > (DWORD) r2) ? 1 : 0; cyc += 1; n++;
	CORTE(0x0C158412ul);

	/* 0c158412: BF c158418 -- con T puesto sigue en 0c158414, que ya no es
	   nuestro (el BRA de salida). */
	cyc += 2; n++;
	if (SR_T)
		SALIR_EN(0x0C158414ul);
	CORTE(0x0C158418ul);

	/* 0c158418: MOV.L @(c15847c), R3 */
	ReadMemoryL(0x0C15847Cul, &r3); cyc += 2; n++;
	CORTE(0x0C15841Aul);

	/* 0c15841a: MOV.L @R3, R2 */
	if (r3 & 3)
		SALIR_EN(0x0C15841Aul);
	ReadMemoryL(r3, &r2); cyc += 2; n++;
	CORTE(0x0C15841Cul);

	/* 0c15841c: TST R2, R2 */
	SR_T = (r2 == 0) ? 1 : 0; cyc += 1; n++;
	CORTE(0x0C15841Eul);

	/* 0c15841e: BF c1583f8 -- con T puesto el lazo termina en 0c158420. */
	cyc += 2; n++;
	if (SR_T)
		SALIR_EN(0x0C158420ul);
	CORTE(FUSION_CT_ENTRADA);

	goto por_vuelta;

salir:
	R(0) = r0; R(1) = r1; R(2) = r2; R(3) = r3; R(4) = r4;
	PR = pr;
	core.context.cycles = cyc;

	fusion_instr += n;
	fusion_entradas++;

	if (perf_activa)
		perf_instrucciones += n;

	return 1;
}

/* ------------------------------------------------------------------------ */
/* El bloque con MMU: el blit de columnas de DOOM                           */
/* ------------------------------------------------------------------------ */

/*
	El bloque 2 de DCDoom (0002ef3e, 16,1 instrucciones de media, 8,9 % de las
	instrucciones), trazado en ejecucion el 2026-08-07:

	  0002ef3e: 6173  MOV R7, R1            ; +0 (la rareza de mov3)
	  0002ef40: 63B2  MOV.L @R11, R3        ; +2
	  0002ef42: 4129  SHLR16 R1             ; +1
	  0002ef44: 6282  MOV.L @R8, R2         ; +2
	  0002ef46: 601F  EXTS.W R1, R0         ; +1
	  0002ef48: C97F  AND #7f, R0           ; +1
	  0002ef4a: 033C  MOV.B @(R0,R3), R3    ; +2, extiende signo
	  0002ef4c: 603C  EXTU.B R3, R0         ; +1
	  0002ef4e: 022C  MOV.B @(R0,R2), R2    ; +2, extiende signo
	  0002ef50: 379C  ADD R9, R7            ; +1
	  0002ef52: 2420  MOV.B R2, @R4         ; +2
	  0002ef54: 61A2  MOV.L @R10, R1        ; +2
	  0002ef56: 341C  ADD R1, R4            ; +1
	  0002ef58: 6653  MOV R5, R6            ; +0
	  0002ef5a: 2668  TST R6, R6            ; +1
	  0002ef5c: 8FEF  BF/S 2ef3e            ; +2; la ranura solo si toma
	  0002ef5e: 75FF    ADD #ff, R5         ; +1 (ranura; de paso, la salida
	                                          del lazo cae aqui como
	                                          instruccion normal)

	La textura, el mapa de color y el framebuffer: seis accesos traducidos por
	vuelta de 17 instrucciones. **Esto es trabajo real** — la mitad que el lazo
	de Crazy Taxi no podia medir.

	La semantica de reejecucion, sin instantanea: antes de CADA acceso se
	vuelcan los locales mutados y el PC de esa instruccion al contexto
	(SINCRONIZAR), y la instantanea quedo invalidada a la entrada. Si el acceso
	falta -- fallo de TLB, proteccion, primera escritura, desalineacion -- el
	longjmp sale por adentro de esta funcion, falta_reponer() no restaura nada
	(instantanea_valida esta en cero) y el contexto YA ES el estado
	pre-instruccion exacto: la excepcion entra igual que en el interprete y la
	instruccion se reejecuta interpretada. Es la "salida 3" de la fase 5 de
	rendimiento-plan.md (deshacer en vez de copiar), lograda estaticamente.

	La validez del codigo: el bloque entero vive en una pagina minima (1 KB),
	asi que la traduccion de la busqueda de la entrada -- que main_loop() ya
	hizo, con su falta posible por el camino de siempre -- cubre las 17
	palabras, y se verifican todas contra la tabla en cada entrada, a traves
	del puntero de pagina que la fetch-cache ya tiene. Otro proceso en la misma
	VA, un parche, otra imagen: cualquiera de esos hace fallar la comparacion y
	el bloque vuelve al interprete sin tocar nada.
*/
static const WORD fusion_ce_palabras[17] =
{
	0x6173, 0x63B2, 0x4129, 0x6282, 0x601F, 0xC97F, 0x033C, 0x603C,
	0x022C, 0x379C, 0x2420, 0x61A2, 0x341C, 0x6653, 0x2668, 0x8FEF,
	0x75FF,
};

static int fusion_ce_quejada = 0;

/* El volcado que hace reejecutable a la instruccion en curso: los registros
   que este bloque muta, los ciclos acumulados hasta la instruccion anterior,
   y el PC de la instruccion que va a ejecutar.

   El contador de instrucciones se vuelca aqui tambien, **contando el intento
   que viene** -- igual que run(), que cuenta antes de despachar --, porque si
   el acceso falta el longjmp sale por encima de salir_ce y lo acumulado se
   perderia. La primera version lo volcaba solo al salir y una corrida entera
   de DCDoom quedo 6,7 millones de instrucciones por debajo de la linea base
   con TODOS los demas contadores identicos al digito: el sintoma exacto de
   contar bien y volcar tarde. */
#define SINCRONIZAR(pc_k)												\
	do																	\
	{																	\
		R(0) = r0; R(1) = r1; R(2) = r2; R(3) = r3;						\
		R(4) = r4; R(5) = r5; R(6) = r6; R(7) = r7;						\
		core.context.cycles = cyc;										\
		PC = (pc_k);													\
		n++;															\
		fusion_instr += n;												\
		if (perf_activa)												\
			perf_instrucciones += n;									\
		n = 0;															\
	} while (0)

#define CORTE_CE(pc_sig)												\
	do																	\
	{																	\
		if (cyc >= RELOJ_GRANO || intc_sh4_reintentar)					\
		{																\
			PC = (pc_sig);												\
			goto salir_ce;												\
		}																\
	} while (0)

int fusion_bloque_ce(void)
{
	DWORD r0, r1, r2, r3, r4, r5, r6, r7;
	DWORD r8, r9, r10, r11;
	DWORD cyc;
	unsigned long n = 0;
	BYTE b;

	/* Las 17 palabras, contra la tabla, por el puntero de la pagina que la
	   busqueda de main_loop() acaba de resolver. */
	{
		const WORD * codigo = (const WORD *) MMU_FETCH_PUNTERO(FUSION_CE_ENTRADA);
		int i;

		for (i = 0; i < 17; i++)
			if (codigo[i] != fusion_ce_palabras[i])
			{
				if (!fusion_ce_quejada)
				{
					fusion_ce_quejada = 1;
					fprintf(stderr, "fusion: la palabra %d del bloque CE es"
						" %04x y no %04x; el bloque vuelve al interprete\n",
						i, codigo[i], fusion_ce_palabras[i]);
				}
				return 0;
			}
	}

	r0  = R(0);  r1 = R(1);  r2 = R(2);  r3 = R(3);
	r4  = R(4);  r5 = R(5);  r6 = R(6);  r7 = R(7);
	r8  = R(8);  r9 = R(9);  r10 = R(10); r11 = R(11);
	cyc = core.context.cycles;

	/* Aqui y no al salir: una entrada que termina en falta tambien es una
	   entrada, y el longjmp no pasa por salir_ce. */
	fusion_entradas++;

vuelta_ce:
	/* 0002ef3e: MOV R7, R1 -- mov3 no suma ciclos, y sin ciclos nuevos la
	   condicion del corte no puede haberse vuelto cierta: se saltea. */
	r1 = r7; n++;

	/* 0002ef40: MOV.L @R11, R3 */
	SINCRONIZAR(0x0002EF40ul);
	ReadMemoryL(r11, &r3); cyc += 2;
	CORTE_CE(0x0002EF42ul);

	/* 0002ef42: SHLR16 R1 */
	r1 = (r1 >> 16) & 0x0000FFFFul; cyc += 1; n++;
	CORTE_CE(0x0002EF44ul);

	/* 0002ef44: MOV.L @R8, R2 */
	SINCRONIZAR(0x0002EF44ul);
	ReadMemoryL(r8, &r2); cyc += 2;
	CORTE_CE(0x0002EF46ul);

	/* 0002ef46: EXTS.W R1, R0 */
	r0 = (DWORD) SignExtend16(r1 & 0x0000FFFFul); cyc += 1; n++;
	CORTE_CE(0x0002EF48ul);

	/* 0002ef48: AND #7f, R0 */
	r0 &= 0x7Ful; cyc += 1; n++;
	CORTE_CE(0x0002EF4Aul);

	/* 0002ef4a: MOV.B @(R0, R3), R3 */
	SINCRONIZAR(0x0002EF4Aul);
	ReadMemoryB(r0 + r3, &b); r3 = (DWORD) SignExtend8(b); cyc += 2;
	CORTE_CE(0x0002EF4Cul);

	/* 0002ef4c: EXTU.B R3, R0 */
	r0 = r3 & 0xFFul; cyc += 1; n++;
	CORTE_CE(0x0002EF4Eul);

	/* 0002ef4e: MOV.B @(R0, R2), R2 */
	SINCRONIZAR(0x0002EF4Eul);
	ReadMemoryB(r0 + r2, &b); r2 = (DWORD) SignExtend8(b); cyc += 2;
	CORTE_CE(0x0002EF50ul);

	/* 0002ef50: ADD R9, R7 */
	r7 += r9; cyc += 1; n++;
	CORTE_CE(0x0002EF52ul);

	/* 0002ef52: MOV.B R2, @R4 -- la escritura al framebuffer: puede levantar
	   la primera escritura de la pagina, y por eso el volcado va antes. */
	SINCRONIZAR(0x0002EF52ul);
	{
		BYTE v = (BYTE) (r2 & 0xFFul);

		WriteMemoryB(r4, &v);
	}
	cyc += 2;
	CORTE_CE(0x0002EF54ul);

	/* 0002ef54: MOV.L @R10, R1 */
	SINCRONIZAR(0x0002EF54ul);
	ReadMemoryL(r10, &r1); cyc += 2;
	CORTE_CE(0x0002EF56ul);

	/* 0002ef56: ADD R1, R4 */
	r4 += r1; cyc += 1; n++;
	CORTE_CE(0x0002EF58ul);

	/* 0002ef58: MOV R5, R6 -- mov3 otra vez: sin ciclos, sin corte. */
	r6 = r5; n++;

	/* 0002ef5a: TST R6, R6 */
	SR_T = (r6 == 0) ? 1 : 0; cyc += 1; n++;
	CORTE_CE(0x0002EF5Cul);

	/* 0002ef5c: BF/S 2ef3e -- la ranura (ADD #ff, R5) solo corre si toma;
	   al caer, 0002ef5e se ejecuta despues como instruccion normal, ya del
	   interprete. */
	cyc += 2; n++;
	if (SR_T)
	{
		PC = 0x0002EF5Eul;
		goto salir_ce;
	}
	r5 += 0xFFFFFFFFul; cyc += 1; n++;	/* la ranura: ADD #-1, R5 */
	CORTE_CE(FUSION_CE_ENTRADA);

	goto vuelta_ce;

salir_ce:
	R(0) = r0; R(1) = r1; R(2) = r2; R(3) = r3;
	R(4) = r4; R(5) = r5; R(6) = r6; R(7) = r7;
	core.context.cycles = cyc;

	/* Lo que quedo sin volcar desde el ultimo SINCRONIZAR: las ALU del final
	   de la vuelta. La entrada ya se conto al entrar. */
	fusion_instr += n;

	if (perf_activa)
		perf_instrucciones += n;

	return 1;
}

#endif /* DCEMU_FUSION */
