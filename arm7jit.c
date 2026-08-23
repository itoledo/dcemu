/****************************************************************************

	ARM7JIT - el traductor de bloques del ARM7 a x86-64

	El escalon final de la fase 4 de docs/estado-del-arte-plan.md. Emite, con
	el emisor jit_x64.c del otro nucleo, el codigo de un bloque recto ya
	descubierto, clasificado y verificado por arm7.c: aqui no hay
	descubrimiento, ni validez, ni presupuesto, ni FIQ -- todo eso vive en
	arm7_blq_intentar() y sus cuatro teoremas. Lo que este archivo agrega es
	lo que el lazo en C no puede quitar: la llamada indirecta por
	instruccion, los operandos leidos de la entrada en vez de bakeados,
	LEER_R con su ternario del PC, y el PC+8 como constante por instruccion
	(por eso arm7.c solo entra al codigo emitido con arm7.r[15] == base).

	Plantillas por forma (arm7_deco_forma): la ALU con inmediato en sus dos
	sabores de S, la ALU con desplazamiento inmediato sin S, LDR/STR con
	inmediato y MRS. Lo demas -- LDM/STM, los tres con acarreo de entrada
	con S, la forma con desplazamiento por registro -- se emite como llamada
	al manejador de la entrada (e->fn), que es exactamente lo que el lazo en
	C hacia: mas lento pero identico por construccion.

	Convenciones del codigo emitido (ABI de Windows x64):

	  - RBX: ciclos acumulados; RSI: &arm7; RDI: la direccion del acceso en
	    curso (sobrevive la llamada a arm7_leer/arm7_escribir).
	  - la funcion es `int fn(void)`: devuelve los ciclos, deja
	    arm7.r[15], arm7.instrucciones y arm7_blq_ult_pasos escritos.
	  - las salidas laterales (arm7_toco_reg tras un acceso) son talones al
	    final del bloque con las constantes de su frontera.

	Las banderas del ARM se arman desde las del anfitrion: N=SF, Z=ZF, V=OF,
	y C es CF en las sumas y **CF invertido** en las restas (el prestamo del
	x86 es el acarreo negado del ARM). El acarreo de entrada se materializa
	con `shl ecx, 3` sobre el CPSR: el ultimo bit expulsado es el 29, o sea
	C, y queda en CF listo para un adc.

	DCEMU_SIN_JIT_ARM=1 no instala el traductor: los bloques quedan en el
	lazo en C, que es el A/B.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* Como en jit.c: DWORD lo trae <windows.h> y aca no hay nada de SDL. */
#include <windows.h>

#include "arm7.h"
#include "arm7jit.h"
#include "jit_x64.h"

/* ------------------------------------------------------------------------ */

#define AJ_ARENA_TAM	(16u * 1024 * 1024)
#define AJ_MARGEN		8192			/* peor bloque posible, con aire */

#define AJ_R(i)			((int) (offsetof(struct arm7_estado, r) + 4 * (i)))
#define AJ_CPSR			((int) offsetof(struct arm7_estado, cpsr))
#define AJ_SPSR			((int) offsetof(struct arm7_estado, spsr))
#define AJ_INSTR		((int) offsetof(struct arm7_estado, instrucciones))
#define AJ_CICLOS		((int) offsetof(struct arm7_estado, ciclos))

#define ARM7_N_BIT		0x80000000u
#define ARM7_Z_BIT		0x40000000u
#define ARM7_C_BIT		0x20000000u
#define ARM7_V_BIT		0x10000000u

static unsigned char *	aj_arena  = NULL;
static unsigned			aj_usado  = 0;

static unsigned long long	aj_emitidos   = 0;
static unsigned long long	aj_declinados = 0;	/* desborde o arena llena */

/* Una salida lateral pendiente: el parche del jcc y cuantos pasos lleva
   ejecutados la frontera donde salta. */
typedef struct
{
	x64_parche	p;
	int			pasos;
} aj_salida;

/* ------------------------------------------------------------------------ */
/* Piezas                                                                   */
/* ------------------------------------------------------------------------ */

/* CALL a un destino absoluto: rel32 si alcanza, y si no por R10 -- que no
   esta vivo en ningun sitio de llamada de este traductor. */
static void aj_llamar(x64_emisor * e, const void * destino)
{
	if (!jit_x64_call_directo(e, destino))
	{
		jit_x64_mov64_ri(e, X64_R10, (unsigned long long) (size_t) destino);
		jit_x64_call_r(e, X64_R10);
	}
}

/* Carga el registro `reg` del ARM en `dst`, con la semantica del PC: leido
   como operando vale la constante que el llamador bakeo. */
static void aj_cargar(x64_emisor * e, x64_reg dst, int reg, DWORD pc_leido)
{
	if (reg == 15)
		jit_x64_mov_ri(e, dst, pc_leido);
	else
		jit_x64_mov_rm(e, dst, X64_RSI, AJ_R(reg));
}

/* Deja CF = C del CPSR, pisando ECX: shl 3 expulsa el bit 29 al acarreo. */
static void aj_acarreo_entrada(x64_emisor * e)
{
	jit_x64_mov_rm(e, X64_RCX, X64_RSI, AJ_CPSR);
	jit_x64_shift_ri(e, X64_SHL, X64_RCX, 3);
}

/* El marco afuera y el retorno: RBX (los ciclos no comprometidos) a EAX. */
static void aj_marco_fuera(x64_emisor * e)
{
	jit_x64_mov_rr(e, X64_RAX, X64_RBX);
	jit_x64_add64_ri(e, X64_RSP, 32);
	jit_x64_pop(e, X64_RDI);
	jit_x64_pop(e, X64_RSI);
	jit_x64_pop(e, X64_RBX);
	jit_x64_ret(e);
}

/* La salida comun: las constantes de la frontera y el marco afuera. Los
   pasos SUMAN sobre arm7_blq_ult_pasos -- el prologo lo puso en cero, y con
   el encadenado emitido una llamada corre varios tramos que se acumulan. */
static void aj_salir(x64_emisor * e, DWORD dir, int pasos)
{
	jit_x64_mov_mi(e, X64_RSI, AJ_R(15), dir + 4u * (unsigned) pasos);
	jit_x64_add64_mi(e, X64_RSI, AJ_INSTR, pasos);

	jit_x64_mov64_ri(e, X64_RCX,
		(unsigned long long) (size_t) &arm7_blq_ult_pasos);
	jit_x64_alu_mi(e, X64_ADD, X64_RCX, 0, pasos);

	aj_marco_fuera(e);
}

/* La comprobacion del teorema 4 tras un acceso a memoria: si el acceso cayo
   en el archivo de registros, a la salida lateral de esta frontera. */
static void aj_toco(x64_emisor * e, aj_salida * sal, int * ns, int pasos)
{
	jit_x64_mov64_ri(e, X64_RCX,
		(unsigned long long) (size_t) &arm7_toco_reg);
	jit_x64_cmp_mi(e, X64_RCX, 0, 0);

	sal[*ns].p     = jit_x64_jcc(e, X64_NE);
	sal[*ns].pasos = pasos;
	(*ns)++;
}

/* ------------------------------------------------------------------------ */
/* La condicion                                                             */
/* ------------------------------------------------------------------------ */

/* Emite la prueba de la condicion sobre el CPSR y devuelve hasta dos parches
   que saltan al camino de "no se cumple". AL no llega aca; NV lo maneja el
   llamador (no emite cuerpo). */
typedef struct
{
	x64_parche	p[2];
	int			np;
} aj_cond;

static aj_cond aj_condicion(x64_emisor * e, unsigned cond)
{
	aj_cond s;

	s.np = 0;

	jit_x64_mov_rm(e, X64_RAX, X64_RSI, AJ_CPSR);

	switch (cond)
	{
	case 0x0:									/* EQ: Z puesto */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_Z_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_E);
		break;

	case 0x1:									/* NE */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_Z_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_NE);
		break;

	case 0x2:									/* CS */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_C_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_E);
		break;

	case 0x3:									/* CC */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_C_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_NE);
		break;

	case 0x4:									/* MI */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_N_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_E);
		break;

	case 0x5:									/* PL */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_N_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_NE);
		break;

	case 0x6:									/* VS */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_V_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_E);
		break;

	case 0x7:									/* VC */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_V_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_NE);
		break;

	case 0x8:									/* HI: C y no Z */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_C_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_E);
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_Z_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_NE);
		break;

	case 0x9:									/* LS: no C, o Z */
		{
			x64_parche va;

			jit_x64_test_ri(e, X64_RAX, (int) ARM7_C_BIT);
			va = jit_x64_jcc_corto(e, X64_E);
			jit_x64_test_ri(e, X64_RAX, (int) ARM7_Z_BIT);
			s.p[s.np++] = jit_x64_jcc(e, X64_E);
			jit_x64_fijar(e, va);
		}
		break;

	case 0xA:									/* GE: N == V */
	case 0xB:									/* LT: N != V */
		jit_x64_mov_rr(e, X64_RCX, X64_RAX);
		jit_x64_shift_ri(e, X64_SHL, X64_RCX, 3);	/* V al bit 31 */
		jit_x64_xor_rr(e, X64_RCX, X64_RAX);		/* bit 31 = N ^ V */
		jit_x64_test_rr(e, X64_RCX, X64_RCX);
		s.p[s.np++] = jit_x64_jcc(e, cond == 0xA ? X64_S : X64_NS);
		break;

	case 0xC:									/* GT: no Z, y N == V */
		jit_x64_test_ri(e, X64_RAX, (int) ARM7_Z_BIT);
		s.p[s.np++] = jit_x64_jcc(e, X64_NE);
		jit_x64_mov_rr(e, X64_RCX, X64_RAX);
		jit_x64_shift_ri(e, X64_SHL, X64_RCX, 3);
		jit_x64_xor_rr(e, X64_RCX, X64_RAX);
		jit_x64_test_rr(e, X64_RCX, X64_RCX);
		s.p[s.np++] = jit_x64_jcc(e, X64_S);
		break;

	default:									/* LE: Z, o N != V */
		{
			x64_parche va;

			jit_x64_test_ri(e, X64_RAX, (int) ARM7_Z_BIT);
			va = jit_x64_jcc_corto(e, X64_NE);
			jit_x64_mov_rr(e, X64_RCX, X64_RAX);
			jit_x64_shift_ri(e, X64_SHL, X64_RCX, 3);
			jit_x64_xor_rr(e, X64_RCX, X64_RAX);
			jit_x64_test_rr(e, X64_RCX, X64_RCX);
			s.p[s.np++] = jit_x64_jcc(e, X64_NS);
			jit_x64_fijar(e, va);
		}
		break;
	}

	return s;
}

/* ------------------------------------------------------------------------ */
/* Las banderas                                                             */
/* ------------------------------------------------------------------------ */

/*
	Captura NZCV de las banderas vivas del anfitrion y las escribe en el
	CPSR. R8-R11 tienen que venir en cero (los xor van ANTES de la operacion
	que produce las banderas, porque xor las pisa). `invertir_c` es 1 en la
	clase de las restas.
*/
static void aj_banderas_arit(x64_emisor * e, int invertir_c)
{
	jit_x64_setcc(e, X64_S, X64_R8);			/* N */
	jit_x64_setcc(e, X64_E, X64_R9);			/* Z */
	jit_x64_setcc(e, X64_B, X64_R10);			/* CF */
	jit_x64_setcc(e, X64_O, X64_R11);			/* V */

	if (invertir_c)
		jit_x64_xor_ri(e, X64_R10, 1);

	jit_x64_shift_ri(e, X64_SHL, X64_R8, 31);
	jit_x64_shift_ri(e, X64_SHL, X64_R9, 30);
	jit_x64_shift_ri(e, X64_SHL, X64_R10, 29);
	jit_x64_shift_ri(e, X64_SHL, X64_R11, 28);
	jit_x64_alu_rr(e, X64_OR, X64_R8, X64_R9);
	jit_x64_alu_rr(e, X64_OR, X64_R8, X64_R10);
	jit_x64_alu_rr(e, X64_OR, X64_R8, X64_R11);

	jit_x64_mov_rm(e, X64_RCX, X64_RSI, AJ_CPSR);
	jit_x64_and_ri(e, X64_RCX, 0x0FFFFFFF);
	jit_x64_alu_rr(e, X64_OR, X64_RCX, X64_R8);
	jit_x64_mov_mr(e, X64_RSI, AJ_CPSR, X64_RCX);
}

static void aj_cero(x64_emisor * e, x64_reg r)
{
	jit_x64_xor_rr(e, r, r);
}

/* ------------------------------------------------------------------------ */
/* Plantillas                                                               */
/* ------------------------------------------------------------------------ */

/* La llamada al manejador de la entrada: identico al lazo en C por
   construccion. `pc_actual` porque los manejadores leen arm7.r[15]. */
static void aj_fallback(x64_emisor * e, const arm7_deco * d, DWORD pc_actual)
{
	jit_x64_mov_mi(e, X64_RSI, AJ_R(15), pc_actual);

	jit_x64_mov64_ri(e, X64_RCX,
		(unsigned long long) (size_t) &arm7_ciclos_op);
	jit_x64_mov_mi(e, X64_RCX, 0, 1);

	jit_x64_mov64_ri(e, X64_RCX, (unsigned long long) (size_t) d);
	aj_llamar(e, (const void *) d->fn);

	jit_x64_mov64_ri(e, X64_RCX,
		(unsigned long long) (size_t) &arm7_ciclos_op);
	jit_x64_add_rm(e, X64_RBX, X64_RCX, 0);
}

/* ALU con inmediato, S=0. Devuelve 0 si la emitio. */
static int aj_alu_imm_s0(x64_emisor * e, const arm7_deco * d, DWORD pc8)
{
	int   codigo = d->b0 & 0xF;
	int   rn     = d->b1;
	int   rd     = d->b2;
	DWORD imm    = d->imm;

	switch (codigo)
	{
	case 0xD:									/* MOV */
		jit_x64_mov_mi(e, X64_RSI, AJ_R(rd), imm);
		break;

	case 0xF:									/* MVN */
		jit_x64_mov_mi(e, X64_RSI, AJ_R(rd), ~imm);
		break;

	default:
		aj_cargar(e, X64_RAX, rn, pc8);

		switch (codigo)
		{
		case 0x0:	jit_x64_alu_ri(e, X64_AND, X64_RAX, (int) imm);	break;
		case 0x1:	jit_x64_alu_ri(e, X64_XOR, X64_RAX, (int) imm);	break;
		case 0x2:	jit_x64_alu_ri(e, X64_SUB, X64_RAX, (int) imm);	break;

		case 0x3:									/* RSB: imm - a */
			jit_x64_neg_r(e, X64_RAX);
			jit_x64_alu_ri(e, X64_ADD, X64_RAX, (int) imm);
			break;

		case 0x4:	jit_x64_alu_ri(e, X64_ADD, X64_RAX, (int) imm);	break;

		case 0x5:									/* ADC */
			aj_acarreo_entrada(e);
			jit_x64_alu_ri(e, X64_ADC, X64_RAX, (int) imm);
			break;

		case 0x6:									/* SBC: a - imm - 1 + C */
			jit_x64_alu_ri(e, X64_SUB, X64_RAX, (int) (imm + 1));
			aj_acarreo_entrada(e);
			jit_x64_alu_ri(e, X64_ADC, X64_RAX, 0);
			break;

		case 0x7:									/* RSC: imm - a - 1 + C */
			jit_x64_not_r(e, X64_RAX);
			jit_x64_alu_ri(e, X64_ADD, X64_RAX, (int) imm);
			aj_acarreo_entrada(e);
			jit_x64_alu_ri(e, X64_ADC, X64_RAX, 0);
			break;

		case 0xC:	jit_x64_alu_ri(e, X64_OR, X64_RAX, (int) imm);	break;
		case 0xE:	jit_x64_alu_ri(e, X64_AND, X64_RAX, (int) ~imm);	break;

		default:
			return 1;							/* 8-11 no existen con S=0 */
		}

		jit_x64_mov_mr(e, X64_RSI, AJ_R(rd), X64_RAX);
		break;
	}

	jit_x64_add_ri(e, X64_RBX, 1);

	return 0;
}

/* ALU con inmediato, S=1. ADC/SBC/RSC van por el manejador: el truco del
   inmediato corrido rompe las banderas que aqui son la mercancia. */
static int aj_alu_imm_s1(x64_emisor * e, const arm7_deco * d, DWORD pc8)
{
	int   codigo  = d->b0 & 0xF;
	int   rn      = d->b1;
	int   rd      = d->b2;
	DWORD imm     = d->imm;
	int   rot     = (d->b0 & 0x10) != 0;
	int   escribe = !(codigo >= 0x8 && codigo <= 0xB);

	if (codigo == 0x5 || codigo == 0x6 || codigo == 0x7)
		return 1;

	/* MOV y MVN: el resultado es el inmediato, o sea que N y Z (y C si la
	   rotacion la produjo) son constantes de emision. */
	if (codigo == 0xD || codigo == 0xF)
	{
		DWORD r    = (codigo == 0xD) ? imm : ~imm;
		DWORD alto = (r & ARM7_N_BIT) | (r == 0 ? ARM7_Z_BIT : 0);
		DWORD masc = rot ? 0x1FFFFFFF : 0x3FFFFFFF;

		if (rot && (imm & ARM7_N_BIT))
			alto |= ARM7_C_BIT;

		jit_x64_mov_rm(e, X64_RCX, X64_RSI, AJ_CPSR);
		jit_x64_and_ri(e, X64_RCX, (int) masc);

		if (alto)
			jit_x64_or_ri(e, X64_RCX, (int) alto);

		jit_x64_mov_mr(e, X64_RSI, AJ_CPSR, X64_RCX);
		jit_x64_mov_mi(e, X64_RSI, AJ_R(rd), r);
		jit_x64_add_ri(e, X64_RBX, 1);

		return 0;
	}

	if (codigo == 0x2 || codigo == 0x3 || codigo == 0x4
	 || codigo == 0xA || codigo == 0xB)
	{
		/* Aritmetica: las banderas del anfitrion son la fuente, asi que los
		   receptores se ponen en cero ANTES de la operacion. */
		aj_cero(e, X64_R8);
		aj_cero(e, X64_R9);
		aj_cero(e, X64_R10);
		aj_cero(e, X64_R11);

		if (codigo == 0x3)						/* RSB: imm - a */
		{
			jit_x64_mov_ri(e, X64_RAX, imm);

			if (rn == 15)
				jit_x64_alu_ri(e, X64_SUB, X64_RAX, (int) pc8);
			else
				jit_x64_alu_rm(e, X64_SUB, X64_RAX, X64_RSI, AJ_R(rn));

			aj_banderas_arit(e, 1);
			jit_x64_mov_mr(e, X64_RSI, AJ_R(rd), X64_RAX);
		}
		else
		{
			int resta = (codigo == 0x2 || codigo == 0xA);

			aj_cargar(e, X64_RAX, rn, pc8);

			if (codigo == 0xA)
				jit_x64_alu_ri(e, X64_CMP, X64_RAX, (int) imm);
			else
			if (resta)
				jit_x64_alu_ri(e, X64_SUB, X64_RAX, (int) imm);
			else
				jit_x64_alu_ri(e, X64_ADD, X64_RAX, (int) imm);

			aj_banderas_arit(e, resta);

			if (escribe)
				jit_x64_mov_mr(e, X64_RSI, AJ_R(rd), X64_RAX);
		}

		jit_x64_add_ri(e, X64_RBX, 1);

		return 0;
	}

	/* Logicas: AND/EOR/TST/TEQ/ORR/BIC. N y Z del resultado; C solo cambia
	   si la rotacion lo produjo, y entonces es constante; V no se toca. */
	{
		DWORD masc = rot ? 0x1FFFFFFF : 0x3FFFFFFF;

		aj_cargar(e, X64_RAX, rn, pc8);

		switch (codigo)
		{
		case 0x0:
		case 0x8:	jit_x64_alu_ri(e, X64_AND, X64_RAX, (int) imm);	break;
		case 0x1:
		case 0x9:	jit_x64_alu_ri(e, X64_XOR, X64_RAX, (int) imm);	break;
		case 0xC:	jit_x64_alu_ri(e, X64_OR, X64_RAX, (int) imm);	break;
		default:	jit_x64_alu_ri(e, X64_AND, X64_RAX, (int) ~imm);	break;	/* BIC */
		}

		aj_cero(e, X64_R8);
		aj_cero(e, X64_R9);
		jit_x64_test_rr(e, X64_RAX, X64_RAX);
		jit_x64_setcc(e, X64_S, X64_R8);
		jit_x64_setcc(e, X64_E, X64_R9);
		jit_x64_shift_ri(e, X64_SHL, X64_R8, 31);
		jit_x64_shift_ri(e, X64_SHL, X64_R9, 30);
		jit_x64_alu_rr(e, X64_OR, X64_R8, X64_R9);

		jit_x64_mov_rm(e, X64_RCX, X64_RSI, AJ_CPSR);
		jit_x64_and_ri(e, X64_RCX, (int) masc);
		jit_x64_alu_rr(e, X64_OR, X64_RCX, X64_R8);

		if (rot && (imm & ARM7_N_BIT))
			jit_x64_or_ri(e, X64_RCX, (int) ARM7_C_BIT);

		jit_x64_mov_mr(e, X64_RSI, AJ_CPSR, X64_RCX);

		if (escribe)
			jit_x64_mov_mr(e, X64_RSI, AJ_R(rd), X64_RAX);

		jit_x64_add_ri(e, X64_RBX, 1);

		return 0;
	}
}

/* ALU con desplazamiento inmediato, S=0: el desplazador se resuelve al
   emitir, casos de cantidad cero incluidos. */
static int aj_alu_reg_s0(x64_emisor * e, const arm7_deco * d, DWORD pc8)
{
	int codigo = d->b0 & 0xF;
	int rn     = d->b1;
	int rd     = d->b2;
	int rm     = (int) (d->imm & 0xF);
	int tipo   = (int) ((d->imm >> 8) & 3);
	int cant   = (int) ((d->imm >> 16) & 0x1F);
	x64_reg r  = X64_RAX;						/* donde queda el resultado */

	/* b, ya desplazado, en EAX. */
	aj_cargar(e, X64_RAX, rm, pc8);

	switch (tipo)
	{
	case 0:										/* LSL; #0 es identidad */
		if (cant)
			jit_x64_shift_ri(e, X64_SHL, X64_RAX, cant);
		break;

	case 1:										/* LSR; #0 significa 32 */
		if (cant)
			jit_x64_shift_ri(e, X64_SHR, X64_RAX, cant);
		else
			jit_x64_mov_ri(e, X64_RAX, 0);
		break;

	case 2:										/* ASR; #0 significa 32 */
		jit_x64_shift_ri(e, X64_SAR, X64_RAX, cant ? cant : 31);
		break;

	default:									/* ROR; #0 es RRX */
		if (cant)
			jit_x64_shift_ri(e, X64_ROR, X64_RAX, cant);
		else
		{
			aj_acarreo_entrada(e);
			jit_x64_shift_ri(e, X64_RCR, X64_RAX, 1);
		}
		break;
	}

	switch (codigo)
	{
	case 0x0:									/* AND */
		if (rn == 15)	jit_x64_alu_ri(e, X64_AND, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_AND, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0x1:									/* EOR */
		if (rn == 15)	jit_x64_alu_ri(e, X64_XOR, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_XOR, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0x2:									/* SUB: a - b */
		aj_cargar(e, X64_RCX, rn, pc8);
		jit_x64_alu_rr(e, X64_SUB, X64_RCX, X64_RAX);
		r = X64_RCX;
		break;

	case 0x3:									/* RSB: b - a */
		if (rn == 15)	jit_x64_alu_ri(e, X64_SUB, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_SUB, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0x4:									/* ADD */
		if (rn == 15)	jit_x64_alu_ri(e, X64_ADD, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_ADD, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0x5:									/* ADC: a + b + C */
		aj_acarreo_entrada(e);

		if (rn == 15)	jit_x64_alu_ri(e, X64_ADC, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_ADC, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0x6:									/* SBC: a - b - 1 + C */
		aj_cargar(e, X64_RDX, rn, pc8);
		jit_x64_alu_rr(e, X64_SUB, X64_RDX, X64_RAX);
		jit_x64_alu_ri(e, X64_SUB, X64_RDX, 1);
		aj_acarreo_entrada(e);
		jit_x64_alu_ri(e, X64_ADC, X64_RDX, 0);
		r = X64_RDX;
		break;

	case 0x7:									/* RSC: b - a - 1 + C */
		if (rn == 15)	jit_x64_alu_ri(e, X64_SUB, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_SUB, X64_RAX, X64_RSI, AJ_R(rn));

		jit_x64_alu_ri(e, X64_SUB, X64_RAX, 1);
		aj_acarreo_entrada(e);
		jit_x64_alu_ri(e, X64_ADC, X64_RAX, 0);
		break;

	case 0xC:									/* ORR */
		if (rn == 15)	jit_x64_alu_ri(e, X64_OR, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_OR, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0xD:									/* MOV */
		break;

	case 0xE:									/* BIC: a & ~b */
		jit_x64_not_r(e, X64_RAX);

		if (rn == 15)	jit_x64_alu_ri(e, X64_AND, X64_RAX, (int) pc8);
		else			jit_x64_alu_rm(e, X64_AND, X64_RAX, X64_RSI, AJ_R(rn));
		break;

	case 0xF:									/* MVN */
		jit_x64_not_r(e, X64_RAX);
		break;

	default:
		return 1;								/* 8-11 no existen con S=0 */
	}

	jit_x64_mov_mr(e, X64_RSI, AJ_R(rd), r);
	jit_x64_add_ri(e, X64_RBX, 1);

	return 0;
}

/* LDR con inmediato. En b0: bit 0 pre, 1 suma, 2 byte, 3 writeback. */
static void aj_ldr_imm(x64_emisor * e, const arm7_deco * d, DWORD pc8,
                       aj_salida * sal, int * ns, int pasos)
{
	int pre  = d->b0 & 1;
	int suma = d->b0 & 2;
	int byte = d->b0 & 4;
	int escr = d->b0 & 8;
	int rn   = d->b1;
	int rd   = d->b2;

	aj_cargar(e, X64_RDI, rn, pc8);				/* base */

	if (pre)
		jit_x64_alu_ri(e, suma ? X64_ADD : X64_SUB, X64_RDI, (int) d->imm);

	/*
		El writeback, ANTES de la llamada: arm7_leer() no mira arm7.r, asi que
		adelantarlo no cambia nada -- y deja que solo la direccion tenga que
		sobrevivir la llamada. La guarda rn != rd es la del interprete.
	*/
	if ((!pre || escr) && rn != rd)
	{
		if (pre)
			jit_x64_mov_mr(e, X64_RSI, AJ_R(rn), X64_RDI);
		else
		{
			jit_x64_mov_rr(e, X64_RAX, X64_RDI);
			jit_x64_alu_ri(e, suma ? X64_ADD : X64_SUB, X64_RAX, (int) d->imm);
			jit_x64_mov_mr(e, X64_RSI, AJ_R(rn), X64_RAX);
		}
	}

	jit_x64_mov_rr(e, X64_RCX, X64_RDI);
	jit_x64_mov_ri(e, X64_RDX, byte ? 1u : 4u);
	aj_llamar(e, (const void *) arm7_leer);

	if (!byte)
	{
		/* La rotacion de la carga desalineada: por CL, y con cuenta cero es
		   inocua, asi que va sin rama. */
		jit_x64_mov_rr(e, X64_RCX, X64_RDI);
		jit_x64_and_ri(e, X64_RCX, 3);
		jit_x64_shift_ri(e, X64_SHL, X64_RCX, 3);
		jit_x64_shift_cl(e, X64_ROR, X64_RAX);
	}

	jit_x64_mov_mr(e, X64_RSI, AJ_R(rd), X64_RAX);
	jit_x64_add_ri(e, X64_RBX, 3);

	aj_toco(e, sal, ns, pasos);
}

/* STR con inmediato. Guardar R15 vale PC+12. */
static void aj_str_imm(x64_emisor * e, const arm7_deco * d, DWORD pc8,
                       aj_salida * sal, int * ns, int pasos)
{
	int pre  = d->b0 & 1;
	int suma = d->b0 & 2;
	int byte = d->b0 & 4;
	int escr = d->b0 & 8;
	int rn   = d->b1;
	int rd   = d->b2;

	/* El valor primero: el writeback de rn == rd no debe pisarlo. */
	aj_cargar(e, X64_R8, rd, pc8 + 4);			/* R15 guardado: PC+12 */

	aj_cargar(e, X64_RDI, rn, pc8);

	if (pre)
		jit_x64_alu_ri(e, suma ? X64_ADD : X64_SUB, X64_RDI, (int) d->imm);

	if (!pre || escr)
	{
		if (pre)
			jit_x64_mov_mr(e, X64_RSI, AJ_R(rn), X64_RDI);
		else
		{
			jit_x64_mov_rr(e, X64_RAX, X64_RDI);
			jit_x64_alu_ri(e, suma ? X64_ADD : X64_SUB, X64_RAX, (int) d->imm);
			jit_x64_mov_mr(e, X64_RSI, AJ_R(rn), X64_RAX);
		}
	}

	jit_x64_mov_rr(e, X64_RCX, X64_RDI);
	jit_x64_mov_ri(e, X64_RDX, byte ? 1u : 4u);
	aj_llamar(e, (const void *) arm7_escribir);

	jit_x64_add_ri(e, X64_RBX, 2);

	aj_toco(e, sal, ns, pasos);
}

/* ------------------------------------------------------------------------ */
/* El epilogo del encadenado (la cola B/BL y el salto directo al sucesor)   */
/* ------------------------------------------------------------------------ */

/*
	La cadena hacia un slot: las mismas cinco comprobaciones del lazo en C
	(base, n, presupuesto con lo no comprometido, y el sello de onda por sus
	dos paginas), y el salto a la ENTRADA INTERNA del sucesor -- post-prologo,
	con RSI/RBX vivos y arm7_blq_ult_pasos acumulando -- o al talon crudo, que
	sale al C con el estado en una frontera de instruccion. r15 ya viene
	puesto por el camino que llega aca.
*/
static void aj_cadena(x64_emisor * e, const arm7_enlace * enl,
	const arm7_cola_emitir * c, x64_parche * crudo, int * nc)
{
	jit_x64_mov64_ri(e, X64_RCX, (unsigned long long) (size_t) enl->slot);

	jit_x64_cmp_mi(e, X64_RCX, c->off_base, (int) enl->base);
	crudo[(*nc)++] = jit_x64_jcc(e, X64_NE);

	jit_x64_test_mi8(e, X64_RCX, c->off_n, -1);
	crudo[(*nc)++] = jit_x64_jcc(e, X64_E);

	/* Teorema 2: (no comprometido + ciclos_max) > arm7.ciclos rechaza. */
	jit_x64_mov_rm(e, X64_RAX, X64_RCX, c->off_ciclos_max);
	jit_x64_add_rr(e, X64_RAX, X64_RBX);
	jit_x64_cmp_rm(e, X64_RAX, X64_RSI, AJ_CICLOS);
	crudo[(*nc)++] = jit_x64_jcc(e, X64_G);

	/* El sello de onda: verif_gen[i] == *pgen[i]. */
	jit_x64_mov64_rm(e, X64_RDX, X64_RCX, c->off_pgen0);
	jit_x64_mov_rm(e, X64_RAX, X64_RDX, 0);
	jit_x64_cmp_rm(e, X64_RAX, X64_RCX, c->off_verif0);
	crudo[(*nc)++] = jit_x64_jcc(e, X64_NE);

	jit_x64_mov64_rm(e, X64_RDX, X64_RCX, c->off_pgen1);
	jit_x64_mov_rm(e, X64_RAX, X64_RDX, 0);
	jit_x64_cmp_rm(e, X64_RAX, X64_RCX, c->off_verif1);
	crudo[(*nc)++] = jit_x64_jcc(e, X64_NE);

	jit_x64_mov64_rm(e, X64_RAX, X64_RCX, c->off_cadena);
	jit_x64_test64_rr(e, X64_RAX, X64_RAX);
	crudo[(*nc)++] = jit_x64_jcc(e, X64_E);

	jit_x64_jmp_r(e, X64_RAX);
}

/*
	La cola B/BL emitida, paso por paso el epilogo del lazo en C:

	1. el compromiso del cuerpo ANTES de la cola (arm7.ciclos -= RBX) -- la
	   regla del borde del memo, que compara contra arm7.ciclos;
	2. la contabilidad del tramo entero, cola incluida (el interprete la
	   cuenta aunque la condicion falle);
	3. la cola: condicion, r14 si BL, y en el B hacia atras el borde de la
	   memoizacion con sus tres desenlaces -- repuesto (PC y ciclos ya
	   estan: al C, el destino del replay es dinamico), grabacion armada
	   (contabilidad del memo y al C: grabando no se corre bloque), o el
	   salto normal;
	4. la cadena al sucesor que toque (destino o caida).

	El talon crudo comparte salida: r15, instrucciones y ult_pasos ya estan
	al dia en todo camino que llega, y RBX es lo no comprometido.
*/
static void aj_cola(x64_emisor * e, int rectas, const arm7_cola_emitir * c)
{
	x64_parche	crudo[16];
	int			nc = 0;
	aj_cond		cond = { { { NULL, 0 }, { NULL, 0 } }, 0 };
	x64_parche	nv   = { NULL, 0 };
	int			i;

	jit_x64_alu_mr(e, X64_SUB, X64_RSI, AJ_CICLOS, X64_RBX);
	aj_cero(e, X64_RBX);

	jit_x64_add64_mi(e, X64_RSI, AJ_INSTR, rectas + 1);
	jit_x64_mov64_ri(e, X64_RCX,
		(unsigned long long) (size_t) &arm7_blq_ult_pasos);
	jit_x64_alu_mi(e, X64_ADD, X64_RCX, 0, rectas + 1);

	if (c->cond == 0xF)
		nv = jit_x64_jmp(e);
	else if (c->cond != 0xE)
		cond = aj_condicion(e, c->cond);

	/* --- tomada --------------------------------------------------------- */
	if (c->cond != 0xF)
	{
		jit_x64_add_ri(e, X64_RBX, 1);			/* ciclos_op arranca en 1 */

		if (c->bl)
			jit_x64_mov_mi(e, X64_RSI, AJ_R(14), c->pc_cola + 4);

		if (c->atras)
		{
			jit_x64_mov_ri(e, X64_RCX, c->destino);
			jit_x64_mov_ri(e, X64_RDX, c->pc_cola);
			aj_llamar(e, (const void *) arm7_memo_borde);
			jit_x64_test_rr(e, X64_RAX, X64_RAX);
			crudo[nc++] = jit_x64_jcc(e, X64_NE);	/* repuesto */
		}

		jit_x64_add_ri(e, X64_RBX, 2);			/* el salto tomado: +2 */
		jit_x64_mov_mi(e, X64_RSI, AJ_R(15), c->destino);

		if (c->atras)
		{
			x64_parche sigue;

			jit_x64_mov64_ri(e, X64_RCX,
				(unsigned long long) (size_t) &arm7_memo_fin);
			jit_x64_cmp_mi(e, X64_RCX, 0, -1);
			sigue = jit_x64_jcc(e, X64_E);

			jit_x64_mov_ri(e, X64_RCX, 3);
			aj_llamar(e, (const void *) arm7_memo_cola_contabilizar);
			crudo[nc++] = jit_x64_jmp(e);

			jit_x64_fijar(e, sigue);
		}

		aj_cadena(e, &c->salto, c, crudo, &nc);
	}

	/* --- no tomada ------------------------------------------------------ */
	if (c->cond != 0xE)
	{
		for (i = 0; i < cond.np; i++)
			jit_x64_fijar(e, cond.p[i]);

		if (c->cond == 0xF)
			jit_x64_fijar(e, nv);

		jit_x64_add_ri(e, X64_RBX, 1);
		jit_x64_mov_mi(e, X64_RSI, AJ_R(15), c->pc_cola + 4);

		aj_cadena(e, &c->caida, c, crudo, &nc);
	}

	for (i = 0; i < nc; i++)
		jit_x64_fijar(e, crudo[i]);

	aj_marco_fuera(e);
}

/* ------------------------------------------------------------------------ */
/* El bloque                                                                */
/* ------------------------------------------------------------------------ */

static void * aj_emitir(const arm7_deco * ent, int n, DWORD dir,
	const arm7_cola_emitir * cola, void ** cadena)
{
	x64_emisor		e;
	unsigned char *	inicio;
	aj_salida		sal[16];
	int				ns = 0;
	int				i;

	if (cadena != NULL)
		*cadena = NULL;

	if (aj_arena == NULL || aj_usado + AJ_MARGEN > AJ_ARENA_TAM)
	{
		aj_declinados++;
		return NULL;
	}

	jit_x64_iniciar(&e, aj_arena + aj_usado, AJ_ARENA_TAM - aj_usado);
	inicio = jit_x64_aqui(&e);

	jit_x64_push(&e, X64_RBX);
	jit_x64_push(&e, X64_RSI);
	jit_x64_push(&e, X64_RDI);
	jit_x64_sub64_ri(&e, X64_RSP, 32);

	jit_x64_mov64_ri(&e, X64_RSI, (unsigned long long) (size_t) &arm7);
	aj_cero(&e, X64_RBX);

	jit_x64_mov64_ri(&e, X64_RCX,
		(unsigned long long) (size_t) &arm7_toco_reg);
	jit_x64_mov_mi(&e, X64_RCX, 0, 0);

	/* El acumulador de pasos arranca en cero SOLO en la entrada desde C: la
	   entrada interna (adonde saltan los encadenados) va despues, con RSI y
	   RBX vivos del que salta y los pasos previos acumulados. */
	jit_x64_mov64_ri(&e, X64_RCX,
		(unsigned long long) (size_t) &arm7_blq_ult_pasos);
	jit_x64_mov_mi(&e, X64_RCX, 0, 0);

	if (cadena != NULL)
		*cadena = jit_x64_aqui(&e);

	for (i = 0; i < n; i++)
	{
		const arm7_deco *	d      = &ent[i];
		DWORD				pc8    = dir + 4u * (unsigned) i + 8;
		unsigned			cond   = d->palabra >> 28;
		aj_cond				salto  = { { { NULL, 0 }, { NULL, 0 } }, 0 };
		x64_parche			fin    = { NULL, 0 };
		int					j;

		/* NV: no se ejecuta nunca en ARMv3. Un ciclo y nada mas. */
		if (cond == 0xF)
		{
			jit_x64_add_ri(&e, X64_RBX, 1);
			continue;
		}

		if (cond != 0xE)
			salto = aj_condicion(&e, cond);

		switch (arm7_deco_forma(d))
		{
		case ARM7_DF_ALU_IMM_S0:
			if (aj_alu_imm_s0(&e, d, pc8))
				aj_fallback(&e, d, pc8 - 8);
			break;

		case ARM7_DF_ALU_IMM_S1:
			if (aj_alu_imm_s1(&e, d, pc8))
				aj_fallback(&e, d, pc8 - 8);
			break;

		case ARM7_DF_ALU_REG_S0:
			if (aj_alu_reg_s0(&e, d, pc8))
				aj_fallback(&e, d, pc8 - 8);
			break;

		case ARM7_DF_LDR_IMM:
			aj_ldr_imm(&e, d, pc8, sal, &ns, i + 1);
			break;

		case ARM7_DF_STR_IMM:
			aj_str_imm(&e, d, pc8, sal, &ns, i + 1);
			break;

		case ARM7_DF_MRS:
			jit_x64_mov_rm(&e, X64_RAX, X64_RSI, d->b0 ? AJ_SPSR : AJ_CPSR);
			jit_x64_mov_mr(&e, X64_RSI, AJ_R(d->b1), X64_RAX);
			jit_x64_add_ri(&e, X64_RBX, 1);
			break;

		case ARM7_DF_BLOQUE:
			aj_fallback(&e, d, pc8 - 8);
			aj_toco(&e, sal, &ns, i + 1);
			break;

		case ARM7_DF_LDR_REG:
		case ARM7_DF_STR_REG:
			/* Tocan memoria: mismo trato que BLOQUE -- el acceso pudo caer
			   en el archivo de registros y el bloque sale por el costado. */
			aj_fallback(&e, d, pc8 - 8);
			aj_toco(&e, sal, &ns, i + 1);
			break;

		default:
			/* ALU_REG_S1 y lo que quede: por el manejador. Ninguno toca
			   memoria (los que si, tienen forma propia o son BLOQUE). */
			aj_fallback(&e, d, pc8 - 8);
			break;
		}

		if (salto.np)
		{
			fin = jit_x64_jmp(&e);

			for (j = 0; j < salto.np; j++)
				jit_x64_fijar(&e, salto.p[j]);

			jit_x64_add_ri(&e, X64_RBX, 1);		/* condicion fallada: 1 ciclo */
			jit_x64_fijar(&e, fin);
		}
	}

	if (cola != NULL)
		aj_cola(&e, n, cola);
	else
		aj_salir(&e, dir, n);

	/* Los talones de las salidas laterales, con las constantes de su
	   frontera. */
	for (i = 0; i < ns; i++)
	{
		jit_x64_fijar(&e, sal[i].p);
		aj_salir(&e, dir, sal[i].pasos);
	}

	if (e.desborde)
	{
		aj_declinados++;

		if (cadena != NULL)
			*cadena = NULL;

		return NULL;
	}

	aj_usado += jit_x64_largo(&e);
	aj_emitidos++;

	return inicio;
}

/* ------------------------------------------------------------------------ */

void arm7jit_iniciar(void)
{
	const char * e = getenv("DCEMU_SIN_JIT_ARM");

	if (e != NULL && atoi(e) != 0)
		return;

	if (aj_arena == NULL)
	{
		aj_arena = (unsigned char *) VirtualAlloc(NULL, AJ_ARENA_TAM,
			MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

		if (aj_arena == NULL)
			return;
	}

	arm7_blq_instalar_emisor(aj_emitir);
}

void arm7jit_resumen(void)
{
	if (aj_emitidos == 0 && aj_declinados == 0)
		return;

	fprintf(stderr, "arm7jit: %llu bloques emitidos (%u KB), %llu declinados\n",
		aj_emitidos, aj_usado / 1024, aj_declinados);
}
