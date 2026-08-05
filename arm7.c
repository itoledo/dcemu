/****************************************************************************

	ARM7DI - ver arm7.h.

	El despacho sigue el patron del arbol: una tabla maestra de
	{patron, mascara, nombre, manejador} que arm7_init() expande a un arreglo
	de punteros, como initopcodes() con opcodes[]. El indice son los bits que
	deciden en ARM -- 27-20 y 7-4, doce en total -- asi que la tabla expandida
	tiene 4096 entradas y no hay decodificacion en tiempo de ejecucion.

	Las filas no se pisan: arm7_init() lo comprueba y avisa. La unica sutileza
	esta en el espacio de proceso de datos, donde los codigos 8 a 11 con S=0
	**no** son TST/TEQ/CMP/CMN sino MRS y MSR; por eso el proceso de datos son
	tres filas por forma de operando en vez de una.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"			/* solo por los tipos; no se enlaza nada de SDL */
#include "arm7.h"
#include "aica.h"
#include "perf.h"

struct arm7_estado arm7;

/* ------------------------------------------------------------------------ */
/* Memoria                                                                  */
/* ------------------------------------------------------------------------ */

/*
	Lo que el ARM ve, y nada mas (tabla 4-8). Los 2 MB de RAM de onda se
	repiten hasta 0x007FFFFF porque esa es la ventana que el chip reserva para
	memoria; una consola de serie solo tiene los dos primeros.
*/
/*
	El bus del ARM son 24 bits: fuera de esos, la tabla 4-8 no define nada. Que
	la direccion se recorte y no se deje crecer importa por un caso concreto --
	spu_init() de KOS escribe 0xEAFFFFF8 en la direccion 0, que es un salto a
	PC-24, o sea a 0xFFFFFFE8. Sin el recorte esa direccion no cae en ninguna
	de las dos regiones; con el, el nucleo recorre ceros --que decodifican como
	un AND sin efecto-- y vuelve a dar la vuelta, que es el bucle infinito que
	el comentario de KOS promete.
*/
#define ARM7_BUS	0x00FFFFFFu

/*
	La palabra alineada, de una sola vez.

	Esto se armaba byte a byte --cuatro cargas, tres desplazamientos y tres
	OR-- por independencia del orden de bytes del anfitrion, y se paga en CADA
	instruccion: la busqueda pasa por aqui, y son dos mil millones de pasos en
	una corrida de tres minutos (medido: 18 141 ms, el 15,2 % del tiempo real).

	El ARM es little-endian y los anfitriones de este arbol tambien --x86 y
	ARM64--; el resto del arbol ya depende de eso en varios lugares (mem.c,
	los decodificadores de textura de graficos.c). Va por memcpy y no por un
	puntero convertido: MSVC y GCC lo compilan a un solo mov y no apoya nada en
	las reglas de aliasing que /O2 si usa, que es exactamente la salida limpia
	que pide docs/msvc-build-plan.md para el type-punning del arbol.
*/
/*
	DCEMU_ARM_POR_BYTES=1 vuelve al armado byte a byte. Es una sonda de
	medicion, no una opcion: existe para poder alternar las dos formas en el
	MISMO binario y en la misma tanda, que es la unica comparacion que resiste
	el ruido de esta maquina --perf_ns_arm varia un 10 % entre corridas
	identicas, asi que una corrida contra otra de otro binario no decide nada--.

	Los dos caminos pagan la misma rama, o sea que la diferencia medida
	subestima la ganancia real en lo que cueste esa rama. Mejor eso que un A/B
	entre binarios que uno no puede demostrar que sean distintos.
*/
static int arm7_por_bytes = 0;		/* el camino rapido es el de por omision */

static void arm7_forma_de_acceso(void)
{
	const char * e = getenv("DCEMU_ARM_POR_BYTES");

	arm7_por_bytes = (e != NULL && atoi(e) != 0);
}

static DWORD onda_leer32(DWORD a)
{
	a &= ~3u;

	if (arm7_por_bytes)
		return (DWORD) (sound_mem[a]
		              | (sound_mem[a + 1] << 8)
		              | (sound_mem[a + 2] << 16)
		              | ((DWORD) sound_mem[a + 3] << 24));

	{
		DWORD v;

		memcpy(&v, sound_mem + a, sizeof(v));

		return v;
	}
}

static DWORD onda_leer16(DWORD a)
{
	a &= ~1u;

	if (arm7_por_bytes)
		return (DWORD) (sound_mem[a] | (sound_mem[a + 1] << 8));

	{
		Uint16 v;

		memcpy(&v, sound_mem + a, sizeof(v));

		return v;
	}
}

/*
	La busqueda de instruccion, separada de arm7_leer() a proposito.

	El ARM ejecuta desde la misma RAM de onda que sondea, asi que si el censo de
	paginas contara tambien las busquedas taparia justo lo que se busca separar:
	que paginas se leen como **dato** y cuales se escriben. Ver perf.h.

	De paso es el camino corto que docs/arm7-plan.md pide en su punto 1.4: la
	busqueda siempre son 4 bytes y casi siempre en RAM de onda.
*/
static DWORD arm7_buscar(DWORD direccion)
{
	direccion &= ARM7_BUS;

	if (direccion & 0x00800000)
		return aica_arm_leer(direccion & (AICA_REG_SIZE - 1), 4);

	return onda_leer32(direccion & (AICA_ONDA_SIZE - 1));
}

DWORD arm7_leer(DWORD direccion, int tam)
{
	direccion &= ARM7_BUS;

	if (direccion & 0x00800000)
	{
		/* Un lazo que sondee el archivo de registros no se puede saltear: eso
		   cambia con cada muestra sin que nadie lo escriba, asi que ninguna
		   generacion lo cubre. Se cuenta aparte para saber si pasa. */
		if (perf_sonda_onda)
			perf_onda_arm_reg_lect++;

		arm7_memo_abortar_por(ARM7_MEMO_REGISTRO);

		return aica_arm_leer(direccion & (AICA_REG_SIZE - 1), tam);
	}

	PERF_ONDA_LECT(direccion);

	/* Lo que el barrido lee es lo que hay que vigilar para poder reponerlo. */
	if (arm7_memo_fin != ~0u)
		arm7_memo_pagina(direccion);

	{
		DWORD a = direccion & (AICA_ONDA_SIZE - 1);

		switch (tam)
		{
		case 1:		return sound_mem[a];
		case 2:		return onda_leer16(a);
		default:	return onda_leer32(a);
		}
	}
}

void arm7_escribir(DWORD direccion, int tam, DWORD valor)
{
	direccion &= ARM7_BUS;

	/* Un barrido que escribe no es un barrido: reponerlo se saltearia la
	   escritura. Y si es al archivo de registros, ademas cambia el AICA. */
	arm7_memo_abortar_por(ARM7_MEMO_ESCRITURA);

	if (direccion & 0x00800000)
	{
		aica_arm_escribir(direccion & (AICA_REG_SIZE - 1), tam, valor);
		return;
	}

	PERF_ONDA_ESCR(direccion, tam);
	onda_marcar_escritura(direccion, tam);

	{
		DWORD a = direccion & (AICA_ONDA_SIZE - 1);

		switch (tam)
		{
		case 1:
			sound_mem[a] = (unsigned char) valor;
			break;

		case 2:
			a &= ~1u;

			if (arm7_por_bytes)
			{
				sound_mem[a]     = (unsigned char) valor;
				sound_mem[a + 1] = (unsigned char) (valor >> 8);
			}
			else
			{
				Uint16 v = (Uint16) valor;

				memcpy(sound_mem + a, &v, sizeof(v));
			}
			break;

		default:
			a &= ~3u;

			if (arm7_por_bytes)
			{
				sound_mem[a]     = (unsigned char) valor;
				sound_mem[a + 1] = (unsigned char) (valor >> 8);
				sound_mem[a + 2] = (unsigned char) (valor >> 16);
				sound_mem[a + 3] = (unsigned char) (valor >> 24);
			}
			else
				memcpy(sound_mem + a, &valor, sizeof(valor));
			break;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Modos y bancos                                                           */
/* ------------------------------------------------------------------------ */

static int banco_de(DWORD modo)
{
	switch (modo & ARM7_MODO)
	{
	case ARM7_MODO_FIQ:		return ARM7_B_FIQ;
	case ARM7_MODO_IRQ:		return ARM7_B_IRQ;
	case ARM7_MODO_SVC:		return ARM7_B_SVC;
	case ARM7_MODO_ABT:		return ARM7_B_ABT;
	case ARM7_MODO_UND:		return ARM7_B_UND;
	default:				return ARM7_B_USR;	/* usuario y sistema comparten */
	}
}

/*
	Guarda el banco vigente y carga el del modo nuevo. R8-R12 solo se mueven
	entrando o saliendo de FIQ, que es lo que distingue a ese modo del resto.
*/
static void cambiar_banco(int nuevo)
{
	if (nuevo == arm7.banco)
		return;

	arm7.r13_14[arm7.banco][0]  = arm7.r[13];
	arm7.r13_14[arm7.banco][1]  = arm7.r[14];
	arm7.spsr_banco[arm7.banco] = arm7.spsr;

	if (arm7.banco == ARM7_B_FIQ)
		memcpy(arm7.r8_12_fiq, &arm7.r[8], sizeof(arm7.r8_12_fiq));
	else
		memcpy(arm7.r8_12_usr, &arm7.r[8], sizeof(arm7.r8_12_usr));

	if (nuevo == ARM7_B_FIQ)
		memcpy(&arm7.r[8], arm7.r8_12_fiq, sizeof(arm7.r8_12_fiq));
	else
		memcpy(&arm7.r[8], arm7.r8_12_usr, sizeof(arm7.r8_12_usr));

	arm7.r[13] = arm7.r13_14[nuevo][0];
	arm7.r[14] = arm7.r13_14[nuevo][1];
	arm7.spsr  = arm7.spsr_banco[nuevo];
	arm7.banco = nuevo;
}

static void poner_cpsr(DWORD valor)
{
	cambiar_banco(banco_de(valor));
	arm7.cpsr = valor;
}

/* Entrada a una excepcion: guarda el retorno y el CPSR, cambia de modo y
   salta al vector. */
static void excepcion(DWORD vector, DWORD modo, DWORD retorno, int mascara_f)
{
	DWORD viejo = arm7.cpsr;

	/* Una excepcion en medio de un barrido lo parte: ni el estado de salida ni
	   los ciclos serian los del barrido. Se declara aca arriba (arm7.h) porque
	   esto esta antes que el modulo de memoizacion. */
	arm7_memo_abortar_por(ARM7_MEMO_EXCEPCION);

	poner_cpsr((arm7.cpsr & ~ARM7_MODO) | modo);

	arm7.spsr  = viejo;
	arm7.r[14] = retorno;
	arm7.cpsr |= ARM7_I;

	if (mascara_f)
		arm7.cpsr |= ARM7_F;

	arm7.r[15] = vector;
}

/* ------------------------------------------------------------------------ */
/* Lectura y escritura de registros con la semantica del PC                 */
/* ------------------------------------------------------------------------ */

/*
	Leer R15 no da el PC: da el PC mas 8, porque en el ARM7 la instruccion se
	esta ejecutando dos etapas por detras del prefetch. Son mas 12 cuando el
	desplazamiento viene de un registro y cuando se guarda R15 en memoria: ahi
	hay un ciclo mas de por medio.
*/
#define LEER_R(n)		((n) == 15 ? arm7.r[15] + 8 : arm7.r[n])
#define LEER_R_12(n)	((n) == 15 ? arm7.r[15] + 12 : arm7.r[n])

/* 1 si la instruccion movio el PC por su cuenta; entonces no se avanza. */
static int pc_cambio;

static void poner_r(int n, DWORD v)
{
	if (n == 15)
	{
		/* El unico camino por el que el PC se mueve fuera de op_salto() y de
		   excepcion(). Un barrido que salga por aca no es el barrido que se
		   estaba grabando, asi que la grabacion se tira. */
		arm7_memo_abortar_por(ARM7_MEMO_PC);

		arm7.r[15] = v & ~3u & ARM7_BUS;
		pc_cambio  = 1;
	}
	else
		arm7.r[n] = v;
}

/* ------------------------------------------------------------------------ */
/* Desplazador                                                              */
/* ------------------------------------------------------------------------ */

/*
	El segundo operando del proceso de datos. Devuelve el valor y deja el
	acarreo en *c, que solo cambia cuando el desplazamiento lo produce.

	Los casos de cantidad cero son los del manual y no son simetricos: LSL #0
	no desplaza y no toca el acarreo, mientras que LSR #0 y ASR #0 significan
	32 y ROR #0 significa RRX.
*/
static DWORD desplazar(DWORD valor, int tipo, DWORD cant, int por_registro,
                       DWORD * c)
{
	if (por_registro)
	{
		cant &= 0xFF;

		if (cant == 0)
			return valor;
	}
	else
	if (cant == 0)
	{
		switch (tipo)
		{
		case 0:									/* LSL #0: nada */
			return valor;

		case 1:									/* LSR #0 = LSR #32 */
		case 2:									/* ASR #0 = ASR #32 */
			cant = 32;
			break;

		default:								/* ROR #0 = RRX */
			{
				DWORD entrada = *c;

				*c = valor & 1;
				return (valor >> 1) | (entrada << 31);
			}
		}
	}

	switch (tipo)
	{
	case 0:										/* LSL */
		if (cant > 32)		{ *c = 0; return 0; }
		if (cant == 32)		{ *c = valor & 1; return 0; }
		*c = (valor >> (32 - cant)) & 1;
		return valor << cant;

	case 1:										/* LSR */
		if (cant > 32)		{ *c = 0; return 0; }
		if (cant == 32)		{ *c = (valor >> 31) & 1; return 0; }
		*c = (valor >> (cant - 1)) & 1;
		return valor >> cant;

	case 2:										/* ASR */
		if (cant >= 32)
		{
			*c = (valor >> 31) & 1;
			return (valor & 0x80000000u) ? 0xFFFFFFFFu : 0;
		}
		*c = (DWORD) (((long) valor >> (cant - 1)) & 1);
		return (DWORD) ((long) valor >> cant);

	default:									/* ROR */
		cant &= 31;

		if (cant == 0)							/* ROR #32k: solo el acarreo */
		{
			*c = (valor >> 31) & 1;
			return valor;
		}

		*c = (valor >> (cant - 1)) & 1;
		return (valor >> cant) | (valor << (32 - cant));
	}
}

/* ------------------------------------------------------------------------ */
/* Condiciones                                                              */
/* ------------------------------------------------------------------------ */

static int condicion(DWORD op)
{
	DWORD cpsr = arm7.cpsr;
	int n = (cpsr & ARM7_N) != 0;
	int z = (cpsr & ARM7_Z) != 0;
	int c = (cpsr & ARM7_C) != 0;
	int v = (cpsr & ARM7_V) != 0;

	switch (op >> 28)
	{
	case 0x0:	return z;					/* EQ */
	case 0x1:	return !z;					/* NE */
	case 0x2:	return c;					/* CS */
	case 0x3:	return !c;					/* CC */
	case 0x4:	return n;					/* MI */
	case 0x5:	return !n;					/* PL */
	case 0x6:	return v;					/* VS */
	case 0x7:	return !v;					/* VC */
	case 0x8:	return c && !z;				/* HI */
	case 0x9:	return !c || z;				/* LS */
	case 0xA:	return n == v;				/* GE */
	case 0xB:	return n != v;				/* LT */
	case 0xC:	return !z && n == v;		/* GT */
	case 0xD:	return z || n != v;			/* LE */
	case 0xE:	return 1;					/* AL */
	default:	return 0;					/* NV: nunca, en ARMv3 */
	}
}

/* ------------------------------------------------------------------------ */
/* Manejadores                                                              */
/* ------------------------------------------------------------------------ */

/* Los ciclos que costo la instruccion en curso. El modelo es el del manual,
   simplificado: 1 por instruccion secuencial, mas los accesos a memoria. */
static int ciclos_op;

static void poner_nz(DWORD r)
{
	arm7.cpsr = (arm7.cpsr & ~(ARM7_N | ARM7_Z))
	          | (r & 0x80000000u)
	          | (r == 0 ? ARM7_Z : 0);
}

static void poner_c(DWORD c)
{
	arm7.cpsr = (arm7.cpsr & ~ARM7_C) | (c ? ARM7_C : 0);
}

static void poner_v(DWORD v)
{
	arm7.cpsr = (arm7.cpsr & ~ARM7_V) | (v ? ARM7_V : 0);
}

/*
	Proceso de datos. Un solo manejador para las nueve filas: el codigo de
	operacion esta en los bits 24-21 y no hace falta una fila por cada uno --
	lo que si hacia falta era separar los codigos 8 a 11 con S=0, que son otra
	instruccion.
*/
static void op_datos(DWORD op)
{
	int   codigo = (int) ((op >> 21) & 0xF);
	int   s      = (int) ((op >> 20) & 1);
	int   rn     = (int) ((op >> 16) & 0xF);
	int   rd     = (int) ((op >> 12) & 0xF);
	DWORD c      = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD a, b, r = 0;
	int   escribe = 1;
	int   aritmetica = 0;
	DWORD acarreo = 0, desborde = 0;

	if (op & 0x02000000)						/* operando inmediato */
	{
		DWORD imm = op & 0xFF;
		DWORD rot = ((op >> 8) & 0xF) * 2;

		if (rot)
		{
			b = (imm >> rot) | (imm << (32 - rot));
			c = (b >> 31) & 1;
		}
		else
			b = imm;

		a = LEER_R(rn);
	}
	else
	if (op & 0x10)								/* desplazamiento por registro */
	{
		DWORD cant = LEER_R((int) ((op >> 8) & 0xF));

		/* Con desplazamiento por registro, R15 se lee como PC+12: hay un
		   ciclo mas antes del uso. */
		a = LEER_R_12(rn);
		b = desplazar(LEER_R_12((int) (op & 0xF)), (int) ((op >> 5) & 3),
		              cant, 1, &c);
		ciclos_op++;
	}
	else										/* desplazamiento inmediato */
	{
		a = LEER_R(rn);
		b = desplazar(LEER_R((int) (op & 0xF)), (int) ((op >> 5) & 3),
		              (op >> 7) & 0x1F, 0, &c);
	}

	switch (codigo)
	{
	case 0x0:	r = a & b;					break;	/* AND */
	case 0x1:	r = a ^ b;					break;	/* EOR */
	case 0x2:	r = a - b;   aritmetica = 1;	break;	/* SUB */
	case 0x3:	r = b - a;   aritmetica = 2;	break;	/* RSB */
	case 0x4:	r = a + b;   aritmetica = 3;	break;	/* ADD */
	case 0x5:	r = a + b + ((arm7.cpsr & ARM7_C) ? 1 : 0); aritmetica = 4; break;	/* ADC */
	case 0x6:	r = a - b - ((arm7.cpsr & ARM7_C) ? 0 : 1); aritmetica = 5; break;	/* SBC */
	case 0x7:	r = b - a - ((arm7.cpsr & ARM7_C) ? 0 : 1); aritmetica = 6; break;	/* RSC */
	case 0x8:	r = a & b;   escribe = 0;	break;	/* TST */
	case 0x9:	r = a ^ b;   escribe = 0;	break;	/* TEQ */
	case 0xA:	r = a - b;   escribe = 0; aritmetica = 1;	break;	/* CMP */
	case 0xB:	r = a + b;   escribe = 0; aritmetica = 3;	break;	/* CMN */
	case 0xC:	r = a | b;					break;	/* ORR */
	case 0xD:	r = b;						break;	/* MOV */
	case 0xE:	r = a & ~b;					break;	/* BIC */
	default:	r = ~b;						break;	/* MVN */
	}

	if (aritmetica)
	{
		DWORD x, y;							/* los dos sumandos efectivos */
		DWORD llevada;

		switch (aritmetica)
		{
		case 1:		x = a; y = ~b; llevada = 1; break;					/* SUB, CMP */
		case 2:		x = b; y = ~a; llevada = 1; break;					/* RSB */
		case 3:		x = a; y =  b; llevada = 0; break;					/* ADD, CMN */
		case 4:		x = a; y =  b; llevada = (arm7.cpsr & ARM7_C) ? 1 : 0; break;
		case 5:		x = a; y = ~b; llevada = (arm7.cpsr & ARM7_C) ? 1 : 0; break;
		default:	x = b; y = ~a; llevada = (arm7.cpsr & ARM7_C) ? 1 : 0; break;
		}

		{
			unsigned long long suma =
				(unsigned long long) x + (unsigned long long) y + llevada;

			acarreo  = (DWORD) ((suma >> 32) & 1);
			desborde = (~(x ^ y) & (x ^ (DWORD) suma) & 0x80000000u) ? 1 : 0;
		}
	}

	if (escribe)
		poner_r(rd, r);

	if (s)
	{
		if (rd == 15 && escribe)
		{
			/* "SUBS PC, R14, #4": el retorno de una excepcion. Es lo que hace
			   el crt0.s de KOS al final de su FIQ. */
			poner_cpsr(arm7.spsr);
		}
		else
		{
			poner_nz(r);

			if (aritmetica)
			{
				poner_c(acarreo);
				poner_v(desborde);
			}
			else
				poner_c(c);
		}
	}

	if (rd == 15 && escribe)
		ciclos_op += 2;
}

/* MRS: del PSR a un registro. */
static void op_mrs(DWORD op)
{
	int rd = (int) ((op >> 12) & 0xF);

	poner_r(rd, (op & 0x00400000) ? arm7.spsr : arm7.cpsr);
}

/*
	MSR: de un registro o un inmediato al PSR. Los cuatro bits 19-16 dicen que
	campos se tocan; en modo usuario solo se puede escribir el de banderas,
	pero el firmware nunca corre en usuario asi que la distincion se respeta
	igual por no dejar un agujero.
*/
static void op_msr(DWORD op)
{
	int   spsr = (op & 0x00400000) != 0;
	DWORD campos = (op >> 16) & 0xF;
	DWORD valor;
	DWORD mascara = 0;
	DWORD destino;

	if (op & 0x02000000)
	{
		DWORD imm = op & 0xFF;
		DWORD rot = ((op >> 8) & 0xF) * 2;

		valor = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
	}
	else
		valor = LEER_R((int) (op & 0xF));

	if (campos & 1)		mascara |= 0x000000FFu;
	if (campos & 2)		mascara |= 0x0000FF00u;
	if (campos & 4)		mascara |= 0x00FF0000u;
	if (campos & 8)		mascara |= 0xFF000000u;

	/* En modo usuario el campo de control no se toca. */
	if (!spsr && (arm7.cpsr & ARM7_MODO) == ARM7_MODO_USR)
		mascara &= 0xFF000000u;

	if (spsr)
	{
		arm7.spsr = (arm7.spsr & ~mascara) | (valor & mascara);
		return;
	}

	destino = (arm7.cpsr & ~mascara) | (valor & mascara);

	poner_cpsr(destino);
}

/* MUL y MLA. */
static void op_multiplicar(DWORD op)
{
	int   rd = (int) ((op >> 16) & 0xF);
	int   rn = (int) ((op >> 12) & 0xF);
	int   rs = (int) ((op >> 8) & 0xF);
	int   rm = (int) (op & 0xF);
	DWORD r  = LEER_R(rm) * LEER_R(rs);

	if (op & 0x00200000)						/* A: acumula */
	{
		r += LEER_R(rn);
		ciclos_op++;
	}

	poner_r(rd, r);

	if (op & 0x00100000)						/* S */
	{
		poner_nz(r);
		/* El acarreo queda indefinido tras un MUL; se deja como estaba, que
		   es lo que hace el ARM7 en la practica. */
	}

	ciclos_op += 3;
}

/* SWP: leer, escribir, en un solo acceso indivisible. */
static void op_swap(DWORD op)
{
	int   rn  = (int) ((op >> 16) & 0xF);
	int   rd  = (int) ((op >> 12) & 0xF);
	int   rm  = (int) (op & 0xF);
	int   tam = (op & 0x00400000) ? 1 : 4;
	DWORD dir = LEER_R(rn);
	DWORD leido = arm7_leer(dir, tam);

	if (tam == 4 && (dir & 3))
	{
		DWORD rot = (dir & 3) * 8;

		leido = (leido >> rot) | (leido << (32 - rot));
	}

	arm7_escribir(dir, tam, LEER_R(rm));
	poner_r(rd, leido);

	ciclos_op += 3;
}

/*
	LDR y STR. La rotacion de una lectura desalineada es del ARM7 y no un
	detalle menor: el firmware no la usa, pero un guest que lea un byte con
	LDR la espera.
*/
static void op_transferencia(DWORD op)
{
	int   rn   = (int) ((op >> 16) & 0xF);
	int   rd   = (int) ((op >> 12) & 0xF);
	int   pre  = (op & 0x01000000) != 0;
	int   suma = (op & 0x00800000) != 0;
	int   byte = (op & 0x00400000) != 0;
	int   escr = (op & 0x00200000) != 0;		/* writeback */
	int   carga = (op & 0x00100000) != 0;
	DWORD base = LEER_R(rn);
	DWORD desp;
	DWORD dir;

	if (op & 0x02000000)						/* desplazamiento por registro */
	{
		DWORD c = (arm7.cpsr & ARM7_C) ? 1 : 0;

		desp = desplazar(LEER_R((int) (op & 0xF)), (int) ((op >> 5) & 3),
		                 (op >> 7) & 0x1F, 0, &c);
	}
	else
		desp = op & 0xFFF;

	dir = pre ? (suma ? base + desp : base - desp) : base;

	if (carga)
	{
		DWORD v = arm7_leer(dir, byte ? 1 : 4);

		if (!byte && (dir & 3))
		{
			DWORD rot = (dir & 3) * 8;

			v = (v >> rot) | (v << (32 - rot));
		}

		/* El writeback se aplica antes de cargar el destino: si son el mismo
		   registro gana el dato. */
		if (!pre || escr)
		{
			DWORD nueva = pre ? dir : (suma ? base + desp : base - desp);

			if (rn != rd)
				poner_r(rn, nueva);
		}

		poner_r(rd, v);
		ciclos_op += 2;
	}
	else
	{
		/* Guardar R15 da PC+12, no PC+8. */
		arm7_escribir(dir, byte ? 1 : 4, LEER_R_12(rd));

		if (!pre || escr)
			poner_r(rn, pre ? dir : (suma ? base + desp : base - desp));

		ciclos_op += 1;
	}
}

/*
	LDM y STM. Los registros se recorren siempre de menor a mayor y siempre
	hacia direcciones crecientes: el modo de direccionamiento solo cambia donde
	empieza el bloque, que es lo que hace que las cuatro variantes se puedan
	escribir una sola vez.
*/
static void op_bloque(DWORD op)
{
	int   rn    = (int) ((op >> 16) & 0xF);
	int   pre   = (op & 0x01000000) != 0;
	int   suma  = (op & 0x00800000) != 0;
	int   s     = (op & 0x00400000) != 0;
	int   escr  = (op & 0x00200000) != 0;
	int   carga = (op & 0x00100000) != 0;
	DWORD lista = op & 0xFFFF;
	DWORD base  = LEER_R(rn);
	int   n     = 0;
	int   i;
	DWORD dir, fin;
	int   banco_viejo = arm7.banco;
	int   forzar_usuario;

	for (i = 0; i < 16; i++)
		if (lista & (1u << i))
			n++;

	if (n == 0)								/* lista vacia: no existe en ARMv3 */
		return;

	if (suma)
	{
		dir = pre ? base + 4 : base;
		fin = base + (DWORD) n * 4;
	}
	else
	{
		fin = base - (DWORD) n * 4;
		dir = pre ? fin : fin + 4;
	}

	/*
		El bit S con R15 fuera de la lista significa "los registros del banco
		de usuario". Con R15 dentro y carga, significa ademas CPSR = SPSR.
	*/
	forzar_usuario = s && !(carga && (lista & 0x8000));

	if (forzar_usuario)
		cambiar_banco(ARM7_B_USR);

	for (i = 0; i < 16; i++)
	{
		if (!(lista & (1u << i)))
			continue;

		if (carga)
		{
			DWORD v = arm7_leer(dir, 4);

			if (i == 15)
				poner_r(15, v);
			else
				arm7.r[i] = v;
		}
		else
			arm7_escribir(dir, 4, (i == 15) ? arm7.r[15] + 12 : arm7.r[i]);

		dir += 4;
	}

	if (forzar_usuario)
		cambiar_banco(banco_viejo);

	if (escr && !(carga && (lista & (1u << rn))))
		arm7.r[rn] = fin;

	if (carga && s && (lista & 0x8000))
		poner_cpsr(arm7.spsr);

	ciclos_op += n + (carga ? 1 : 0);
}

/* ------------------------------------------------------------------------ */
/* Memoizacion de los barridos de sondeo                                    */
/* ------------------------------------------------------------------------ */

/*
	Cerca de la mitad de los 2161 millones de pasos del ARM son barridos de
	sondeo que casi nunca encuentran nada: recorrer una tabla mirando un byte por
	entrada. El lazo mas caro de DCDoom da 94 439 120 vueltas y **su cuerpo no se
	ejecuta ni una vez** en toda la corrida. Ver docs/arm7-plan.md.

	Un barrido asi es una funcion pura de (registros de entrada, memoria). Si el
	ARM vuelve a hacer el mismo barrido con los mismos registros y nadie escribio
	las paginas que el barrido lee, el resultado es identico por construccion y
	se puede reponer de una sola vez en vez de interpretar miles de
	instrucciones.

	**La clave es el salto hacia atras, no la entrada al lazo.** Se memoiza desde
	el borde de atras: cuando el ARM salta hacia atras a `cabecera` con un estado
	de registros que ya se vio, lo que falta del barrido esta determinado. Cuesta
	interpretar una vuelta y saltear las otras N-1, y a cambio la deteccion es un
	solo lugar --op_salto-- en vez de tener que reconocer cuando se entra al lazo
	desde afuera.

	Lo que **aborta** la grabacion, todo por el mismo motivo -- si la vuelta no es
	una funcion pura de la memoria que se vigila, no se puede reponer:

	  - cualquier escritura del ARM (arm7_escribir);
	  - cualquier acceso al archivo de registros del AICA, que cambia con cada
	    muestra sin que nadie lo "escriba", asi que ninguna generacion lo cubre;
	  - cualquier cambio de PC que no sea del propio lazo (poner_r sobre R15) y
	    cualquier excepcion, FIQ incluida;
	  - leer mas de MEMO_PAGS paginas distintas, o pasarse de MEMO_INSTR
	    instrucciones: las dos son barandas contra memoizar algo que no es un
	    barrido.

	Y dos condiciones mas en el momento de reponer:

	  - **no puede haber FIQ pendiente.** Reponer se salta las comprobaciones de
	    FIQ de cada instruccion; como el barrido no toca registros del AICA ni
	    escribe, el estado del AICA no puede cambiar durante el, asi que alcanza
	    con mirar una vez al principio;
	  - **el barrido tiene que caber en los ciclos que quedan.** Si no, el ARM se
	    adelantaria dentro de la muestra de audio y cambiaria la granularidad con
	    la que llegan las interrupciones.

	La baranda es el .wav de --captura-audio, determinista bit a bit. Y
	DCEMU_SIN_MEMO_ARM=1 lo apaga entero, que es el A/B.
*/
#define MEMO_RANURAS	512					/* directa, potencia de dos */
#define MEMO_PAGS		8					/* paginas distintas por barrido */
#define MEMO_INSTR		8192				/* tope de instrucciones grabadas */
#define MEMO_CUERPO		1024				/* tamano maximo del cuerpo, bytes */

typedef struct
{
	DWORD			cabecera;				/* destino del salto hacia atras */
	DWORD			r_ent[15];				/* R0-R14 al tomarlo */
	DWORD			cpsr_ent;
	DWORD			r_sal[15];
	DWORD			cpsr_sal;
	DWORD			pc_sal;
	long			ciclos;
	unsigned long	instr;
	int				banco;
	int				n_pags;
	unsigned short	pag[MEMO_PAGS];
	unsigned long	gen[MEMO_PAGS];
	int				lista;
} memo_t;

static memo_t	memo[MEMO_RANURAS];

/*
	El filtro de cabeceras, y **es lo que hace que el mecanismo pueda rendir**.

	Sin el, cada salto hacia atras del ARM --decenas de millones por corrida--
	pagaba mezclar quince registros para buscar en la tabla y, si no encontraba,
	copiar quince mas para empezar a grabar. Casi todos esos saltos son lazos
	comunes que escriben y que van a abortar la grabacion tres instrucciones
	despues: 4 989 019 grabaciones abortadas contra 664 130 reposiciones utiles
	en Crazy Taxi.

	El filtro es directo por PC de cabecera y cuesta dos cargas y una
	comparacion:

	  - una cabecera que se ve por primera vez solo se anota;
	  - recien despues de MEMO_UMBRAL vueltas se paga la busqueda cara;
	  - y una que aborto MEMO_FALLOS veces seguidas se envenena y no se vuelve a
	    intentar. Un lazo que escribe no va a dejar de escribir.

	El envenenamiento se levanta solo cuando la ranura la reclama otra cabecera,
	que es lo que hace que el filtro se adapte si el firmware cambia de fase.
*/
#define MEMO_CABS		1024				/* potencia de dos */
#define MEMO_UMBRAL		2					/* vueltas antes de grabar */
#define MEMO_FALLOS		8					/* abortos antes de envenenar */
#define MEMO_LISTO		254					/* ya grabo un barrido util */
#define MEMO_VENENO		255

static DWORD			memo_cab[MEMO_CABS];
static unsigned char	memo_cab_n[MEMO_CABS];
static unsigned char	memo_cab_fallos[MEMO_CABS];

int				arm7_memo_apagada = 0;		/* DCEMU_SIN_MEMO_ARM */

/* Estado de la grabacion en curso. */
static int		memo_grabando = 0;
static int		memo_ranura;
static int		memo_cab_ranura;			/* la del filtro, para castigarla */
static DWORD	memo_cabecera;
static DWORD	memo_r_ent[15];
static DWORD	memo_cpsr_ent;
static int		memo_banco;
static long		memo_ciclos;
static unsigned long memo_instr;
static int		memo_n_pags;
static unsigned short memo_pag[MEMO_PAGS];
static unsigned long  memo_gen[MEMO_PAGS];

/*
	El PC del salto hacia atras mientras se graba, y ~0 cuando no.

	El centinela existe para que arm7_paso() no tenga que preguntar si se esta
	grabando: la comprobacion de fin de barrido es una comparacion contra este
	valor, metida **dentro del if de pc_cambio que ya estaba**. Es la misma
	leccion que costo 8,5 % en intc_sh4_reintentar: una rama propia en el camino
	caliente se paga, doblada dentro de una que ya existe no.
*/
DWORD			arm7_memo_fin = ~0u;

unsigned long long arm7_memo_aciertos    = 0;
unsigned long long arm7_memo_pasos       = 0;	/* instrucciones no ejecutadas */
unsigned long long arm7_memo_grabados    = 0;
unsigned long long arm7_memo_abortados   = 0;
unsigned long long arm7_memo_sucios      = 0;

unsigned long long arm7_memo_motivo[ARM7_MEMO_MOTIVOS];

const char * const arm7_memo_motivo_nombre[ARM7_MEMO_MOTIVOS] =
{
	"escritura", "registro del AICA", "PC fuera del lazo", "excepcion",
	"lazo anidado", "demasiado largo", "demasiadas paginas"
};

void arm7_memo_abortar_real(int motivo)
{
	if (!memo_grabando)
		return;

	memo_grabando  = 0;
	arm7_memo_fin  = ~0u;
	arm7_memo_abortados++;
	arm7_memo_motivo[motivo]++;

	/* Un lazo que aborta seguido no va a dejar de hacerlo: el que escribe
	   escribe siempre. Se lo castiga hasta envenenarlo. */
	if (memo_cab_n[memo_cab_ranura] != MEMO_VENENO
	 && ++memo_cab_fallos[memo_cab_ranura] >= MEMO_FALLOS)
		memo_cab_n[memo_cab_ranura] = MEMO_VENENO;
}

/* Una pagina de RAM de onda que la vuelta leyo. Se guarda con la generacion que
   tenia: reponer solo vale si sigue siendo esa. */
void arm7_memo_pagina(DWORD direccion)
{
	unsigned long p = (direccion & (AICA_ONDA_SIZE - 1)) >> ONDA_PAG_BITS;
	int i;

	for (i = 0; i < memo_n_pags; i++)
		if (memo_pag[i] == (unsigned short) p)
			return;

	if (memo_n_pags >= MEMO_PAGS)
	{
		arm7_memo_abortar_por(ARM7_MEMO_PAGS);
		return;
	}

	memo_pag[memo_n_pags] = (unsigned short) p;
	memo_gen[memo_n_pags] = onda_gen[p];
	memo_n_pags++;
}

static unsigned arm7_memo_ranura(DWORD cabecera, const DWORD * r)
{
	/* Mezcla barata: la cabecera identifica el lazo y R0-R14 el punto del
	   barrido. No decide correccion --la entrada se compara entera antes de
	   reponer-- solo en que ranura cae. */
	unsigned h = (unsigned) (cabecera >> 2) * 2654435761u;
	int i;

	for (i = 0; i < 15; i++)
		h = h * 16777619u + (unsigned) r[i];

	return h & (MEMO_RANURAS - 1);
}

/* Cierra la grabacion: la vuelta salio del cuerpo del lazo por donde debia. */
void arm7_memo_terminar(void)
{
	memo_t * m = &memo[memo_ranura];
	int      i;

	memo_grabando = 0;
	arm7_memo_fin = ~0u;

	/* El barrido tiene que haber terminado en el mismo banco en que empezo: si
	   cambio de modo, los registros que se repondrian no son los mismos. */
	if (arm7.banco != memo_banco || (arm7.cpsr & ARM7_MODO) != (memo_cpsr_ent & ARM7_MODO))
	{
		arm7_memo_abortados++;
		return;
	}

	m->cabecera = memo_cabecera;
	memcpy(m->r_ent, memo_r_ent, sizeof(m->r_ent));
	m->cpsr_ent = memo_cpsr_ent;

	for (i = 0; i < 15; i++)
		m->r_sal[i] = arm7.r[i];

	m->cpsr_sal = arm7.cpsr;
	m->pc_sal   = arm7.r[15];
	m->ciclos   = memo_ciclos;
	m->instr    = memo_instr;
	m->banco    = memo_banco;
	m->n_pags   = memo_n_pags;
	memcpy(m->pag, memo_pag, sizeof(m->pag));
	memcpy(m->gen, memo_gen, sizeof(m->gen));
	m->lista    = 1;

	/* La cabecera demostro servir: se le da la ranura del filtro en propiedad y
	   se le perdonan los abortos acumulados. */
	memo_cab_n[memo_cab_ranura]      = MEMO_LISTO;
	memo_cab_fallos[memo_cab_ranura] = 0;

	arm7_memo_grabados++;
}

/* 1 si se pudo reponer un barrido entero desde `cabecera`. */
static int arm7_memo_reponer(DWORD cabecera)
{
	unsigned  ran = arm7_memo_ranura(cabecera, arm7.r);
	memo_t *  m   = &memo[ran];
	int       i;

	if (!m->lista || m->cabecera != cabecera || m->banco != arm7.banco
	 || m->cpsr_ent != arm7.cpsr)
		return 0;

	for (i = 0; i < 15; i++)
		if (m->r_ent[i] != arm7.r[i])
			return 0;

	/* Que no se haya escrito ninguna de las paginas que el barrido leyo. */
	for (i = 0; i < m->n_pags; i++)
		if (onda_gen[m->pag[i]] != m->gen[i])
		{
			arm7_memo_sucios++;
			return 0;
		}

	/* Las dos condiciones de tiempo. Ver el comentario de arriba. */
	if (m->ciclos > arm7.ciclos)
		return 0;

	if (!(arm7.cpsr & ARM7_F) && aica_fiq_pendiente())
		return 0;

	for (i = 0; i < 15; i++)
		arm7.r[i] = m->r_sal[i];

	arm7.cpsr   = m->cpsr_sal;
	arm7.r[15]  = m->pc_sal;
	pc_cambio   = 1;

	/*
		**Asignacion y no suma, y esto costo la primera version del mecanismo.**

		memo_ciclos empezo a contar en este mismo salto hacia atras, o sea que
		m->ciclos ya incluye los 3 ciclos que el salto cuesta. Sumarle el 1 con
		el que arm7_paso() arranco cobraba un ciclo de mas por reposicion. Con
		4603 reposiciones son 4603 ciclos en una corrida de tres minutos --nada--
		y sin embargo alcanzaba para correr una frontera de muestra y **cambiar
		el .wav**. La baranda de --captura-audio lo agarro; ninguna otra lo
		habria hecho.

		El salto de atras que cierra el barrido no entra en m->ciclos y se vuelve
		a ejecutar de verdad, porque pc_sal es su propia direccion: cobra su
		ciclo por su cuenta.
	*/
	ciclos_op   = (int) m->ciclos;

	arm7.instrucciones += m->instr;
	arm7_memo_aciertos++;
	arm7_memo_pasos    += m->instr;

	return 1;
}

/*
	El salto hacia atras: donde se decide todo. Devuelve 1 si repuso un barrido
	entero, y entonces op_salto() no tiene nada mas que hacer.
*/
static int arm7_memo_borde(DWORD destino, DWORD pc_salto)
{
	unsigned c;
	int      i;

	if (memo_grabando)
	{
		/* Otra vuelta del mismo lazo: se sigue grabando. Un salto hacia atras a
		   otra cabecera es un lazo anidado, y eso no se memoiza. */
		if (destino != memo_cabecera)
			arm7_memo_abortar_por(ARM7_MEMO_ANIDADO);
		else if (memo_instr > MEMO_INSTR)
			arm7_memo_abortar_por(ARM7_MEMO_LARGO);

		return 0;
	}

	if (arm7_memo_apagada || pc_salto - destino > MEMO_CUERPO)
		return 0;

	/* El filtro barato. Todo lo caro de aqui abajo pasa por el. */
	c = (destino >> 2) & (MEMO_CABS - 1);

	if (memo_cab[c] != destino)
	{
		/*
			**Una cabecera que ya sirvio no se deja desplazar**, y esto costo la
			ganancia entera una vez: con la ranura cediendosela a cualquier
			cabecera nueva, dos lazos separados por 4 KB se anulaban --cada uno
			reseteaba el contador del otro-- y ninguno llegaba nunca al umbral.
			La elision cayo de 8,3 % de los pasos del ARM a 0,03 % sin que nada
			mas cambiara.
		*/
		if (memo_cab_n[c] >= MEMO_LISTO)
			return 0;

		memo_cab[c]        = destino;
		memo_cab_n[c]      = 1;
		memo_cab_fallos[c] = 0;
		return 0;
	}

	if (memo_cab_n[c] == MEMO_VENENO)
		return 0;

	if (memo_cab_n[c] < MEMO_UMBRAL)
	{
		memo_cab_n[c]++;
		return 0;
	}

	if (arm7_memo_reponer(destino))
		return 1;

	/* No estaba: se graba este barrido desde aca. */
	memo_cab_ranura = (int) c;
	memo_cabecera = destino;
	memo_ranura   = (int) arm7_memo_ranura(destino, arm7.r);
	memo_cpsr_ent = arm7.cpsr;
	memo_banco    = arm7.banco;
	memo_ciclos   = 0;
	memo_instr    = 0;
	memo_n_pags   = 0;
	memo_grabando = 1;
	arm7_memo_fin = pc_salto;

	for (i = 0; i < 15; i++)
		memo_r_ent[i] = arm7.r[i];

	/* El cuerpo del lazo vive en la misma RAM de onda que el ARM sondea. Es
	   improbable que alguien lo reescriba, pero si pasara la reposicion seria
	   una instruccion vieja: se vigilan las dos puntas como cualquier otra
	   lectura. */
	arm7_memo_pagina(destino);
	arm7_memo_pagina(pc_salto);

	return 0;
}

void arm7_memo_reset(void)
{
	memset(memo, 0, sizeof(memo));
	memset(memo_cab, 0, sizeof(memo_cab));
	memset(memo_cab_n, 0, sizeof(memo_cab_n));
	memset(memo_cab_fallos, 0, sizeof(memo_cab_fallos));
	memo_grabando   = 0;
	memo_cab_ranura = 0;
	arm7_memo_fin   = ~0u;
}

/* B y BL. */
static void op_salto(DWORD op)
{
	long desp = (long) (op & 0x00FFFFFF);

	if (desp & 0x00800000)
		desp |= ~0x00FFFFFFL;					/* signo */

	if (op & 0x01000000)						/* BL: guarda el retorno */
		arm7.r[14] = arm7.r[15] + 4;

	{
		DWORD pc_salto = arm7.r[15];
		DWORD destino  = (DWORD) (arm7.r[15] + 8 + (desp << 2)) & ARM7_BUS;

		/* Salto hacia atras corto: el borde de un lazo. Un BL nunca lo es --
		   guarda retorno, o sea que es una llamada -- y memoizar a traves de una
		   llamada seria memoizar lo que hay del otro lado. */
		if (destino < pc_salto && !(op & 0x01000000)
		 && arm7_memo_borde(destino, pc_salto))
			return;								/* repuesto: PC y ciclos ya estan */

		arm7.r[15] = destino;
	}

	pc_cambio  = 1;
	ciclos_op += 2;
}

static void op_swi(DWORD op)
{
	(void) op;

	excepcion(ARM7_VEC_SWI, ARM7_MODO_SVC, arm7.r[15] + 4, 0);
	pc_cambio = 1;
	ciclos_op += 2;
}

/*
	Todo lo que no es una instruccion de ARMv3. Aqui caen, entre otras, las
	transferencias de media palabra y las de coprocesador: patrones validos en
	otros nucleos que en este son la excepcion de instruccion indefinida.
*/
static void op_indefinida(DWORD op)
{
	(void) op;

	arm7.indefinidas++;

	excepcion(ARM7_VEC_UNDEF, ARM7_MODO_UND, arm7.r[15] + 4, 0);
	pc_cambio = 1;
	ciclos_op += 2;
}

/* ------------------------------------------------------------------------ */
/* La tabla                                                                 */
/* ------------------------------------------------------------------------ */

struct arm7_fila
{
	DWORD			patron;			/* sobre los 12 bits del indice */
	DWORD			mascara;
	const char *	nombre;
	void		 (* manejador)(DWORD op);
};

/*
	El indice: bits 27-20 arriba y 7-4 abajo.

	  idx[11:4] = op[27:20]        idx[3:0] = op[7:4]

	El orden de las filas no importa -- no se pisan, y arm7_init() lo
	comprueba. Lo unico que hay que leer con cuidado es el bloque de proceso de
	datos: son tres formas de operando (desplazamiento inmediato, por registro,
	e inmediato rotado) por tres tramos de codigo de operacion, porque los
	codigos 8 a 11 con S=0 son MRS/MSR y hay que sacarlos del medio.
*/
static const struct arm7_fila filas[] =
{
	/* Multiplicacion: op[27:22]=000000, op[7:4]=1001. */
	{ 0x009, 0xFCF, "MUL/MLA",		op_multiplicar },

	/* Intercambio: op[27:23]=00010, op[21:20]=00, op[7:4]=1001. */
	{ 0x109, 0xFBF, "SWP",			op_swap },

	/* Transferencia de PSR. */
	{ 0x100, 0xFBF, "MRS",			op_mrs },
	{ 0x120, 0xFBF, "MSR",			op_msr },
	{ 0x320, 0xFB0, "MSR#",			op_msr },

	/* Proceso de datos, codigos 0-7 (idx[8]=0). */
	{ 0x000, 0xF01, "ALU",			op_datos },		/* desplazamiento inmediato */
	{ 0x001, 0xF09, "ALU Rs",		op_datos },		/* desplazamiento por registro */
	{ 0x200, 0xF00, "ALU #",		op_datos },		/* inmediato rotado */

	/* Proceso de datos, codigos 12-15 (idx[8]=1, idx[7]=1). */
	{ 0x180, 0xF81, "ALU2",			op_datos },
	{ 0x181, 0xF89, "ALU2 Rs",		op_datos },
	{ 0x380, 0xF80, "ALU2 #",		op_datos },

	/* Proceso de datos, codigos 8-11, solo con S=1: son las comparaciones. */
	{ 0x110, 0xF91, "CMP",			op_datos },
	{ 0x111, 0xF99, "CMP Rs",		op_datos },
	{ 0x310, 0xF90, "CMP #",		op_datos },

	/* Transferencia simple. La forma con registro exige op[4]=0; con op[4]=1
	   el patron esta indefinido en ARMv3. */
	{ 0x400, 0xE00, "LDR/STR #",	op_transferencia },
	{ 0x600, 0xE01, "LDR/STR R",	op_transferencia },
	{ 0x601, 0xE01, "indefinida",	op_indefinida },

	/* Transferencia de bloque, saltos, coprocesador y SWI. */
	{ 0x800, 0xE00, "LDM/STM",		op_bloque },
	{ 0xA00, 0xE00, "B/BL",			op_salto },
	{ 0xC00, 0xE00, "LDC/STC",		op_indefinida },
	{ 0xE00, 0xF00, "CDP/MRC/MCR",	op_indefinida },
	{ 0xF00, 0xF00, "SWI",			op_swi },
};

#define ARM7_FILAS	((int) (sizeof(filas) / sizeof(filas[0])))

/* La tabla expandida: un puntero y un indice de fila por cada patron. */
static void (* arm7_oplist[4096])(DWORD op);
static signed char arm7_opfila[4096];
static unsigned char arm7_usada[ARM7_FILAS < 64 ? 64 : ARM7_FILAS];

/* Lo enciende la suite (tests/test_arm7.c) para poder pedir el censo de filas
   al final. Apagado en el emulador, que es donde el ARM da miles de millones de
   pasos y nadie lee el resultado. */
int arm7_cobertura = 0;

#define ARM7_INDICE(op)		((((op) >> 16) & 0xFF0) | (((op) >> 4) & 0xF))

void arm7_init(void)
{
	int i, f;

	for (i = 0; i < 4096; i++)
	{
		arm7_oplist[i] = op_indefinida;
		arm7_opfila[i] = -1;
	}

	for (f = 0; f < ARM7_FILAS; f++)
	{
		for (i = 0; i < 4096; i++)
		{
			if (((DWORD) i & filas[f].mascara) != filas[f].patron)
				continue;

			/* Igual que initopcodes() con logs/repetidos.txt: dos filas que
			   cubran el mismo patron son un error de la tabla, no algo que
			   haya que resolver por orden. */
			if (arm7_opfila[i] >= 0)
				fprintf(stderr, "arm7_init: patron %03x repetido entre "
					"'%s' y '%s'\n", i, filas[arm7_opfila[i]].nombre,
					filas[f].nombre);

			arm7_oplist[i] = filas[f].manejador;
			arm7_opfila[i] = (signed char) f;
		}
	}

	memset(arm7_usada, 0, sizeof(arm7_usada));
}

int arm7_filas(void)					{ return ARM7_FILAS; }
int arm7_fila_usada(int i)				{ return arm7_usada[i]; }
const char * arm7_fila_nombre(int i)	{ return filas[i].nombre; }

/* ------------------------------------------------------------------------ */
/* Perfil (ver arm7.h)                                                       */
/* ------------------------------------------------------------------------ */

int arm7_perfil = 0;

/* Una cuenta por palabra de los 2 MB de RAM de onda. Es donde vive el codigo
   del ARM; lo que caiga en la ventana de registros --que no es codigo-- se
   pliega sobre el mismo arreglo y se ve como tal en el volcado. */
#define ARM7_PERFIL_PCS		(AICA_ONDA_SIZE / 4)

static unsigned int *		arm7_perfil_pc;
static unsigned long long	arm7_perfil_fila[ARM7_FILAS < 64 ? 64 : ARM7_FILAS];
static unsigned long long	arm7_perfil_pasos;

void arm7_perfil_inicio(void)
{
	const char * e = getenv("DCEMU_PERFIL_ARM");

	/* De paso, la otra sonda: se lee una vez, en el mismo lugar. */
	arm7_forma_de_acceso();

	if (e == NULL || atoi(e) == 0)
		return;

	arm7_perfil_pc = (unsigned int *)
		calloc(ARM7_PERFIL_PCS, sizeof(unsigned int));

	/* Sin el arreglo no se enciende: mejor no medir que medir a medias. */
	arm7_perfil = (arm7_perfil_pc != NULL);
}

void arm7_perfil_resumen(void)
{
	int i, j;

	if (!arm7_perfil || arm7_perfil_pasos == 0)
		return;

	fprintf(stderr, "\narm7: %llu pasos con perfil\n", arm7_perfil_pasos);

	fprintf(stderr, "arm7: por fila de la tabla de despacho\n");

	for (i = 0; i < ARM7_FILAS; i++)
		if (arm7_perfil_fila[i])
			fprintf(stderr, "arm7:   %-14s %12llu  %5.1f %%\n",
				filas[i].nombre, arm7_perfil_fila[i],
				100.0 * (double) arm7_perfil_fila[i]
				      / (double) arm7_perfil_pasos);

	/*
		Las veinte direcciones mas ejecutadas. Es la cifra que decide el rumbo:
		si unas pocas se llevan casi todo, el ARM esta en un lazo y lo que hay
		que hacer es no ejecutarlo; si el peso esta repartido, hay que acelerar
		el interprete.
	*/
	fprintf(stderr, "arm7: las 20 direcciones mas ejecutadas\n");

	for (j = 0; j < 20; j++)
	{
		unsigned int	mejor = 0;
		int				donde = -1;

		for (i = 0; i < ARM7_PERFIL_PCS; i++)
			if (arm7_perfil_pc[i] > mejor)
			{
				mejor = arm7_perfil_pc[i];
				donde = i;
			}

		if (donde < 0)
			break;

		{
			DWORD dir = (DWORD) donde * 4;
			DWORD op  = arm7_leer(dir, 4);
			int   fil = arm7_opfila[ARM7_INDICE(op)];

			fprintf(stderr, "arm7:   %06lx  %10u  %5.1f %%  %08lx  %s\n",
				(unsigned long) dir, mejor,
				100.0 * (double) mejor / (double) arm7_perfil_pasos,
				(unsigned long) op,
				(fil >= 0) ? filas[fil].nombre : "?");
		}

		arm7_perfil_pc[donde] = 0;		/* para que la vuelta siguiente vea otra */
	}

	/*
		Cuantas direcciones distintas concentran la mitad de los pasos. Una
		cifra sola que separa "lazo" de "programa": con el histograma ya
		gastado por el volcado de arriba no se puede, asi que se cuenta antes
		de imprimir nada... y por eso se recorre aqui sobre lo que quedo, que
		sigue sirviendo para la cola.
	*/
	{
		unsigned long long resto = 0;
		int distintas = 0;

		for (i = 0; i < ARM7_PERFIL_PCS; i++)
			if (arm7_perfil_pc[i])
			{
				resto += arm7_perfil_pc[i];
				distintas++;
			}

		fprintf(stderr, "arm7: %d direcciones distintas fuera de las 20, "
			"%llu pasos (%.1f %%)\n", distintas, resto,
			100.0 * (double) resto / (double) arm7_perfil_pasos);
	}
}

/* ------------------------------------------------------------------------ */
/* Ejecucion                                                                */
/* ------------------------------------------------------------------------ */

void arm7_reset(void)
{
	const char * e;

	memset(&arm7, 0, sizeof(arm7));

	arm7.banco = ARM7_B_SVC;
	arm7.cpsr  = ARM7_MODO_SVC | ARM7_I | ARM7_F;
	arm7.r[15] = ARM7_VEC_RESET;

	/* Los barridos grabados valen para el estado que habia: soltar el reset del
	   ARM puede dejar otro firmware. */
	arm7_memo_reset();

	/* Una vez al arrancar y nunca en el camino caliente, como el resto de las
	   sondas del arbol. */
	e = getenv("DCEMU_SIN_MEMO_ARM");
	arm7_memo_apagada = (e != NULL && atoi(e) != 0);
}

int arm7_paso(void)
{
	DWORD op;
	int   idx;

	ciclos_op = 1;
	pc_cambio = 0;

	/*
		La FIQ se mira en el limite de instruccion, antes de buscar. R14 queda
		en "la que sigue mas 4" porque el retorno es SUBS PC,R14,#4 -- que es
		exactamente como termina la FIQ del firmware de KOS.
	*/
	if (!(arm7.cpsr & ARM7_F) && aica_fiq_pendiente())
	{
		aica_fiq_tomada();
		excepcion(ARM7_VEC_FIQ, ARM7_MODO_FIQ, arm7.r[15] + 4, 1);
		return 3;
	}

	op = arm7_buscar(arm7.r[15]);
	arm7.instrucciones++;

	if (arm7_perfil)
	{
		DWORD p = (arm7.r[15] & ARM7_BUS) >> 2;

		arm7_perfil_pc[p % ARM7_PERFIL_PCS]++;
		arm7_perfil_pasos++;

		{
			int f = arm7_opfila[ARM7_INDICE(op)];

			/* Se cuenta la fila de lo que se FUE a ejecutar, aunque la
			   condicion despues lo descarte: una instruccion que no se cumple
			   igual se busco y se decodifico. */
			if (f >= 0)
				arm7_perfil_fila[f]++;
		}
	}

	/*
		AL --el campo de condicion en 0xE, "siempre"-- se atiende aqui y no en
		condicion(). En codigo ARM real es la enorme mayoria de las
		instrucciones, y condicion() extrae N, Z, C y V y entra a un switch de
		dieciseis casos para contestar que si. Es una comparacion contra una
		llamada.
	*/
	if ((op >> 28) == 0xE || condicion(op))
	{
		idx = (int) ARM7_INDICE(op);

		/*
			El censo de filas que la suite lee por arm7_fila_usada(). Es un
			instrumento de tests/ y estaba escribiendo en cada instruccion del
			ARM en una corrida normal -- dos cargas y un almacenamiento por
			paso, por un dato que en produccion nadie mira. Misma regla que el
			resto de los instrumentos: apagado, cuesta una comparacion.
		*/
		if (arm7_cobertura && arm7_opfila[idx] >= 0)
			arm7_usada[arm7_opfila[idx]] = 1;

		arm7_oplist[idx](op);
	}

	if (!pc_cambio)
	{
		/*
			Fin del barrido: el salto de atras del lazo se ejecuto y **no**
			salto, o sea que el lazo termino y cayo a la instruccion siguiente.

			La comparacion va doblada dentro de este `if`, que ya estaba, y
			contra un centinela que vale ~0 cuando no se graba -- asi no hace
			falta preguntar antes si se estaba grabando. Ver el comentario de
			arm7_memo_fin: una rama propia en el camino caliente se paga.
		*/
		if (arm7.r[15] == arm7_memo_fin)
			arm7_memo_terminar();

		arm7.r[15] += 4;
	}

	/* La contabilidad del barrido en curso. Cuelga del mismo centinela. */
	if (arm7_memo_fin != ~0u)
	{
		memo_ciclos += ciclos_op;
		memo_instr++;
	}

	return ciclos_op;
}

void arm7_ejecutar(long ciclos)
{
	if (aica_arm_en_reset())
	{
		/* Detenido: no acumula deuda. Soltar el reset no tiene que producir
		   una rafaga de todo lo que estuvo parado. */
		arm7.ciclos = 0;
		return;
	}

	arm7.ciclos += ciclos;

	/*
		Los dos lazos estan separados a proposito: la prueba de perf_activa
		estaba ADENTRO, o sea una rama por cada uno de los dos mil millones de
		pasos, por un instrumento que en una corrida normal esta apagado.
	*/
	if (perf_activa)
	{
		while (arm7.ciclos > 0)
		{
			/* Un salto a si mismo deja el PC donde estaba: es la forma que
			   tiene el ARM de esperar, y la que spu_init() de KOS deja puesta
			   en la direccion 0.

			   **Cero aqui no significa que el ARM trabaje**, y eso ya
			   confundio una vez: contra un juego da 0,0 % mientras la mitad de
			   los pasos se van en dos lazos de sondeo de cuatro y seis
			   instrucciones, que no mueven esta cifra y son igual de
			   salteables. Para eso esta DCEMU_PERFIL_ARM; ver arm7.h. */
			DWORD antes = arm7.r[15];

			arm7.ciclos -= arm7_paso();

			perf_arm_pasos++;

			if (arm7.r[15] == antes)
				perf_arm_ocioso++;
		}

		return;
	}

	while (arm7.ciclos > 0)
		arm7.ciclos -= arm7_paso();
}
