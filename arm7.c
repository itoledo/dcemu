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
	1 cuando el ultimo acceso de datos cayo en el archivo de registros del
	AICA de una forma que pudo mover la FIQ. Es la salida lateral de los
	bloques (ver abajo): el bloque termina en esa frontera -- que es
	exactamente donde el interprete habria mirado. Lo pone el camino frio de
	arm7_escribir(); lo limpia el bloque al entrar. Exportado: la salida
	lateral emitida lo mira por direccion absoluta.

	La LECTURA del archivo ya no lo pone (2026-08-19, el teorema 4 refinado):
	dentro de un lote la FIQ pendiente solo cambia por escrituras del ARM --
	aica_tick() y el SH-4 corren entre lotes, y leer_registro() no toca ni
	int_nivel ni los pendientes (su unico efecto de lado, el dio_la_vuelta
	del monitor EG, es un bit de monitoreo). Con eso los lazos de sondeo --
	el 35 % de los pasos de CT, que la memoizacion no puede reponer porque lo
	leido cambia por muestra -- caben enteros en un bloque con cola y dan la
	vuelta en el lugar. DCEMU_SIN_SONDEO_ARM=1 es la conducta anterior:
	la lectura vuelve a cortar.
*/
int arm7_toco_reg = 0;

/* Lo que la lectura del archivo escribe en arm7_toco_reg: 0 por omision
   (no corta), 1 con DCEMU_SIN_SONDEO_ARM (la conducta anterior). */
static int arm7_lectura_corta = 0;

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
		arm7_toco_reg = arm7_lectura_corta;

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

/*
	La verificacion por generacion de onda: el memcmp que valida las palabras
	de un bloque vale mientras nadie haya escrito sus paginas, y quien dice
	"nadie escribio" es onda_gen[] -- el contador por pagina de 1 KB que TODO
	escritor de la RAM de onda mantiene por contrato (aica.h: el ARM, el SH-4
	y el G2-DMA por mem.c, el DMA interno del AICA, y el DSP), el mismo del
	que ya depende la memoizacion de barridos. Un bloque cubre a lo sumo dos
	paginas; el sello guarda sus dos generaciones y la verificacion es dos
	comparaciones en vez de un memcmp de hasta 52 bytes. Como las paginas con
	codigo casi nunca se escriben, el sello sobrevive a los lotes -- la misma
	separacion codigo/datos que la rejilla fina le dio al jit del SH-4.

	El limite heredado del contrato: una escritura a sound_mem que no marque
	(la suite lo hace) no invalida -- igual que con la memoizacion, y con la
	misma baranda (las suites y el .wav). Y el mismo agujero teorico de todo
	contador que envuelve: 2^32 escrituras exactas de una pagina entre dos
	visitas al bloque. DCEMU_SIN_VERIF_ONDA=1 vuelve al memcmp en cada salto.
*/
static int					arm7_verif_onda = 1;
static unsigned long long	arm7_verif_memcmp   = 0;	/* perfil */
static unsigned long long	arm7_verif_elididas = 0;	/* perfil */

void arm7_escribir(DWORD direccion, int tam, DWORD valor)
{
	direccion &= ARM7_BUS;

	/* Un barrido que escribe no es un barrido: reponerlo se saltearia la
	   escritura. Y si es al archivo de registros, ademas cambia el AICA. */
	arm7_memo_abortar_por(ARM7_MEMO_ESCRITURA);

	if (direccion & 0x00800000)
	{
		arm7_toco_reg = 1;
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
   simplificado: 1 por instruccion secuencial, mas los accesos a memoria.
   Exportado con nombre propio porque el codigo emitido lo pone en 1 antes de
   llamar a un manejador y lo recoge despues; el define conserva el nombre
   corto en todo este archivo. */
int arm7_ciclos_op;
#define ciclos_op arm7_ciclos_op

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

/* La contabilidad de "el salto arranco una grabacion", para la cola emitida:
   el epilogo del lazo en C hace memo_ciclos += ciclos_op; memo_instr++; y el
   codigo emitido no alcanza estos estaticos. */
void arm7_memo_cola_contabilizar(int ciclos)
{
	memo_ciclos += ciclos;
	memo_instr++;
}

/*
	El salto hacia atras: donde se decide todo. Devuelve 1 si repuso un barrido
	entero, y entonces op_salto() no tiene nada mas que hacer. No estatico
	desde el encadenado emitido: la cola emitida lo llama igual que d_salto.
*/
int arm7_memo_borde(DWORD destino, DWORD pc_salto)
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

/* ------------------------------------------------------------------------ */
/* Predecodificacion                                                        */
/* ------------------------------------------------------------------------ */

/*
	La tabla de despacho evita decodificar el patron, pero cada manejador
	vuelve a extraer sus campos de la palabra en cada ejecucion: op_salto
	extiende el signo del desplazamiento 555 millones de veces por corrida,
	op_datos rota el inmediato y separa la forma del operando, y op_bloque
	cuenta los bits de la lista en un lazo de dieciseis vueltas. Nada de eso
	depende del estado: es funcion pura de la palabra, o sea que se puede
	hacer una vez y guardar.

	Una entrada por palabra de la RAM de onda guarda la palabra cruda, los
	campos ya extraidos y un manejador especializado por forma. La validez es
	comparar la palabra guardada contra la que la memoria tiene AHORA -- la
	misma regla que jit_verificar() en el otro nucleo, y la unica que aguanta
	a todos los que escriben RAM de onda sin pasar por arm7_escribir(): el
	DMA interno del AICA (aica.c), el DSP (aicadsp.c) y la suite de pruebas,
	que mete los programas con memcpy. La busqueda ya carga la palabra en
	cada paso, asi que validar cuesta una comparacion, no una carga extra.

	Los manejadores d_* son transcripciones de los op_* de arriba con los
	campos leidos de la entrada; el nucleo de la ALU esta factorizado en
	alu_nucleo() para que las tres formas no puedan derivar entre si. Lo que
	no tiene forma caliente cae en d_generico(), que despacha por la tabla
	vieja: MUL, SWP, SWI, las formas con desplazamiento por registro.

	Dos decisiones que no son de gusto:

	  - el salto guarda el desplazamiento RELATIVO y no el destino, porque
	    las entradas se indexan por palabra fisica y los 2 MB se repiten en
	    la ventana de 16: dos PC distintos pueden ejecutar la misma palabra;
	  - la tabla arranca entera como la decodificacion de la palabra 0 --
	    AND EQ R0,R0,R0, lo que esos bytes significan de verdad -- para que
	    una entrada fria nunca coincida por casualidad con un fn sin poner.
	    Y la palabra 0 se ejecuta en serio: el lazo de spu_init() que da la
	    vuelta al bus recorre ceros.

	DCEMU_SIN_PREDECO_ARM=1 lo apaga en el mismo binario, que es el A/B.
*/

/* La estructura de la entrada es publica (arm7.h): el traductor de bloques
   emite a partir de ella. Llenarla sigue siendo asunto exclusivo de
   arm7_decodificar(), aqui abajo. */

static arm7_deco	arm7_deco_tabla[AICA_ONDA_SIZE / 4];
static int			arm7_predeco = 0;
static unsigned long long arm7_deco_decodificadas = 0;

/* Las formas anchas (LDR/STR-R, ALU-Rs, MUL/MLA) y su admision en bloques.
   Dos palancas porque son dos efectos: DCEMU_SIN_FORMAS_ARM=1 las deja en
   d_generico (el interprete anterior); DCEMU_SIN_CABE_ARM=1 las decodifica
   pero los bloques no las admiten -- ni a ellas ni al STM con PC en la
   lista --, que es el brazo del medio del A/B. */
static int			arm7_formas_anchas = 0;
static int			arm7_cabe_ancho = 0;

/* Los bloques sobre la predecodificacion (la seccion vive mas abajo, tras el
   perfil). Necesitan la tabla de arriba: sin predecodificacion, sin bloques.
   Los contadores viven aca porque el resumen del perfil los imprime. */
static int			arm7_bloques = 0;
static int			arm7_blq_rama = 0;		/* la cola de salto (ver abajo) */
static int			arm7_blq_retorno = 0;	/* la cola generalizada: LDM al
											   PC como terminal, y la terminal
											   sola (ver abajo) */
static int			arm7_blq_cadena = 0;	/* el encadenado emitido: la cola
											   B/BL y el salto al sucesor en
											   x64 (DCEMU_SIN_CADENA_ARM) */
static unsigned long long	arm7_blq_corridos = 0;
static unsigned long long	arm7_blq_pasos    = 0;
static unsigned long long	arm7_blq_vueltas  = 0;	/* vueltas en el lugar */

/* El censo del giro puro (la pregunta de la fase C, nunca medida): cuando el
   encadenado cierra un ciclo -- vuelve a la base por la que correr() entro --,
   ¿los registros quedaron identicos a la vuelta anterior? Si la mayoria de
   las vueltas es pura, el saldo se podria consumir de un golpe; si los ciclos
   calientes mutan estado (el barrido de canales avanza su indice), la fase C
   no tiene material. Solo bajo perfil, como todo censo. */
static unsigned long long	arm7_giro_puras    = 0;
static unsigned long long	arm7_giro_impuras  = 0;
static unsigned long long	arm7_giro_pasos_puros = 0;
static unsigned long long	arm7_blq_encadenados = 0;	/* saltos bloque a bloque */

/* El censo de rechazos de arm7_blq_intentar(), solo bajo DCEMU_PERFIL_ARM:
   cada rechazo es un paso que corre interpretado, y el censo dice por que --
   la pregunta que quedo abierta cuando la cobertura se planto en 34,5 %. */
static unsigned long long	arm7_blq_rechazo[5];
static const char * const	arm7_blq_rechazo_nombre[5] =
	{ "ventana de registros", "fiq pendiente", "grabando",
	  "marca negativa", "presupuesto" };
static unsigned long long	arm7_blq_descubrimientos = 0;

/* La sonda de la marca negativa por PC: una cuenta por ranura, solo bajo
   perfil -- la ranura guarda su base, asi que el resumen puede decir DONDE
   se rechaza, que es la pregunta que el censo de arriba deja abierta. El
   informe (definido junto a los bloques, que necesita ver) imprime con
   prefijo "arm7 neg:" a proposito: las compuertas comparan las lineas
   `^arm7:` entre brazos y una sonda de mecanismo no entra en esa cuenta. */
#define ARM7_BLQ_RANURAS	4096			/* directa, potencia de dos */
static unsigned int			arm7_blq_neg[ARM7_BLQ_RANURAS];
static void					arm7_blq_neg_resumen(void);

/* Todo lo que no tiene manejador especializado: despacha por la tabla de
   siempre. Frio a proposito. */
static void d_generico(const arm7_deco * e)
{
	arm7_oplist[ARM7_INDICE(e->palabra)](e->palabra);
}

/* op_salto con el desplazamiento ya extendido: imm = (desp << 2) + 8, y en
   b0 si es BL. */
static void d_salto(const arm7_deco * e)
{
	if (e->b0)									/* BL: guarda el retorno */
		arm7.r[14] = arm7.r[15] + 4;

	{
		DWORD pc_salto = arm7.r[15];
		DWORD destino  = (arm7.r[15] + e->imm) & ARM7_BUS;

		if (destino < pc_salto && !e->b0
		 && arm7_memo_borde(destino, pc_salto))
			return;								/* repuesto: PC y ciclos ya estan */

		arm7.r[15] = destino;
	}

	pc_cambio  = 1;
	ciclos_op += 2;
}

/*
	El nucleo del proceso de datos: op_datos desde el switch para abajo, con
	los operandos ya resueltos por el prologo de cada forma. La unica
	diferencia deliberada es que el acarreo y el desborde solo se calculan
	con S=1, que es cuando alguien los consume.
*/
static void alu_nucleo(int codigo, int s, DWORD a, DWORD b, DWORD c, int rd)
{
	DWORD r = 0;
	int   escribe = 1;
	int   aritmetica = 0;
	DWORD acarreo = 0, desborde = 0;

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

	if (s && aritmetica)
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
			poner_cpsr(arm7.spsr);
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

/* Forma inmediata: imm ya rotado; en b0, el codigo y (bit 4) si la rotacion
   produjo acarreo. Con S=0 el acarreo no le importa a nadie. */
static void d_alu_imm_s0(const arm7_deco * e)
{
	alu_nucleo(e->b0 & 0xF, 0, LEER_R(e->b1), e->imm, 0, e->b2);
}

static void d_alu_imm_s1(const arm7_deco * e)
{
	DWORD c = (e->b0 & 0x10) ? (e->imm >> 31)
	                         : ((arm7.cpsr & ARM7_C) ? 1u : 0u);

	alu_nucleo(e->b0 & 0xF, 1, LEER_R(e->b1), e->imm, c, e->b2);
}

/* Forma con desplazamiento inmediato: rm, tipo y cantidad empacados en imm. */
static void d_alu_reg_s0(const arm7_deco * e)
{
	DWORD c = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD b = desplazar(LEER_R((int) (e->imm & 0xF)), (int) ((e->imm >> 8) & 3),
	                    (e->imm >> 16) & 0x1F, 0, &c);

	alu_nucleo(e->b0 & 0xF, 0, LEER_R(e->b1), b, c, e->b2);
}

static void d_alu_reg_s1(const arm7_deco * e)
{
	DWORD c = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD b = desplazar(LEER_R((int) (e->imm & 0xF)), (int) ((e->imm >> 8) & 3),
	                    (e->imm >> 16) & 0x1F, 0, &c);

	alu_nucleo(e->b0 & 0xF, 1, LEER_R(e->b1), b, c, e->b2);
}

/* op_transferencia con inmediato, partido por carga/almacenamiento. En b0:
   bit 0 pre, bit 1 suma, bit 2 byte, bit 3 writeback. */
static void d_ldr_imm(const arm7_deco * e)
{
	DWORD base = LEER_R(e->b1);
	DWORD dir  = (e->b0 & 1) ? ((e->b0 & 2) ? base + e->imm : base - e->imm)
	                         : base;
	DWORD v    = arm7_leer(dir, (e->b0 & 4) ? 1 : 4);

	if (!(e->b0 & 4) && (dir & 3))
	{
		DWORD rot = (dir & 3) * 8;

		v = (v >> rot) | (v << (32 - rot));
	}

	/* El writeback se aplica antes de cargar el destino: si son el mismo
	   registro gana el dato. */
	if (!(e->b0 & 1) || (e->b0 & 8))
	{
		DWORD nueva = (e->b0 & 1) ? dir
		            : ((e->b0 & 2) ? base + e->imm : base - e->imm);

		if (e->b1 != e->b2)
			poner_r(e->b1, nueva);
	}

	poner_r(e->b2, v);
	ciclos_op += 2;
}

static void d_str_imm(const arm7_deco * e)
{
	DWORD base = LEER_R(e->b1);
	DWORD dir  = (e->b0 & 1) ? ((e->b0 & 2) ? base + e->imm : base - e->imm)
	                         : base;

	/* Guardar R15 da PC+12, no PC+8. */
	arm7_escribir(dir, (e->b0 & 4) ? 1 : 4, LEER_R_12(e->b2));

	if (!(e->b0 & 1) || (e->b0 & 8))
		poner_r(e->b1, (e->b0 & 1) ? dir
		             : ((e->b0 & 2) ? base + e->imm : base - e->imm));

	ciclos_op += 1;
}

/*
	Las formas anchas (2026-08-20): la transferencia con desplazamiento por
	registro, la ALU con desplazamiento por registro (Rs) y MUL/MLA, que
	vivian en d_generico. Salieron del censo de marcas negativas por PC: el
	lazo caliente que los bloques no podian cruzar era exactamente un LDR-R,
	un MUL y seis STM. Transcripciones de op_transferencia, op_datos y
	op_multiplicar con los campos leidos de la entrada, como todas las d_*.
	DCEMU_SIN_FORMAS_ARM=1 las devuelve a d_generico en el mismo binario.
*/

/* Transferencia con desplazamiento por registro: rm, tipo y cantidad
   empacados en imm con el layout de d_alu_reg. El acarreo del desplazador
   se descarta, igual que en op_transferencia. */
static void d_ldr_reg(const arm7_deco * e)
{
	DWORD c    = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD desp = desplazar(LEER_R((int) (e->imm & 0xF)),
	                       (int) ((e->imm >> 8) & 3), (e->imm >> 16) & 0x1F,
	                       0, &c);
	DWORD base = LEER_R(e->b1);
	DWORD dir  = (e->b0 & 1) ? ((e->b0 & 2) ? base + desp : base - desp)
	                         : base;
	DWORD v    = arm7_leer(dir, (e->b0 & 4) ? 1 : 4);

	if (!(e->b0 & 4) && (dir & 3))
	{
		DWORD rot = (dir & 3) * 8;

		v = (v >> rot) | (v << (32 - rot));
	}

	if (!(e->b0 & 1) || (e->b0 & 8))
	{
		DWORD nueva = (e->b0 & 1) ? dir
		            : ((e->b0 & 2) ? base + desp : base - desp);

		if (e->b1 != e->b2)
			poner_r(e->b1, nueva);
	}

	poner_r(e->b2, v);
	ciclos_op += 2;
}

static void d_str_reg(const arm7_deco * e)
{
	DWORD c    = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD desp = desplazar(LEER_R((int) (e->imm & 0xF)),
	                       (int) ((e->imm >> 8) & 3), (e->imm >> 16) & 0x1F,
	                       0, &c);
	DWORD base = LEER_R(e->b1);
	DWORD dir  = (e->b0 & 1) ? ((e->b0 & 2) ? base + desp : base - desp)
	                         : base;

	arm7_escribir(dir, (e->b0 & 4) ? 1 : 4, LEER_R_12(e->b2));

	if (!(e->b0 & 1) || (e->b0 & 8))
		poner_r(e->b1, (e->b0 & 1) ? dir
		             : ((e->b0 & 2) ? base + desp : base - desp));

	ciclos_op += 1;
}

/* ALU con desplazamiento por registro (Rs): rm y tipo como d_alu_reg, rs en
   los bits 16-19 de imm. R15 se lee como PC+12 en rn y rm -- hay un ciclo
   mas antes del uso, y es el que se cobra aqui --, pero rs se lee normal,
   la misma asimetria de op_datos. */
static void d_alu_rr_s0(const arm7_deco * e)
{
	DWORD c    = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD cant = LEER_R((int) ((e->imm >> 16) & 0xF));
	DWORD b    = desplazar(LEER_R_12((int) (e->imm & 0xF)),
	                       (int) ((e->imm >> 8) & 3), cant, 1, &c);

	ciclos_op++;
	alu_nucleo(e->b0 & 0xF, 0, LEER_R_12(e->b1), b, c, e->b2);
}

static void d_alu_rr_s1(const arm7_deco * e)
{
	DWORD c    = (arm7.cpsr & ARM7_C) ? 1 : 0;
	DWORD cant = LEER_R((int) ((e->imm >> 16) & 0xF));
	DWORD b    = desplazar(LEER_R_12((int) (e->imm & 0xF)),
	                       (int) ((e->imm >> 8) & 3), cant, 1, &c);

	ciclos_op++;
	alu_nucleo(e->b0 & 0xF, 1, LEER_R_12(e->b1), b, c, e->b2);
}

/* MUL y MLA: rm, rs y rn empacados en imm; rd en b2, A y S en b0. El
   acarreo queda como estaba, igual que en op_multiplicar. */
static void d_mul(const arm7_deco * e)
{
	DWORD r = LEER_R((int) (e->imm & 0xF)) * LEER_R((int) ((e->imm >> 8) & 0xF));

	if (e->b0 & 1)								/* A: acumula */
	{
		r += LEER_R((int) ((e->imm >> 16) & 0xF));
		ciclos_op++;
	}

	poner_r(e->b2, r);

	if (e->b0 & 2)								/* S */
		poner_nz(r);

	ciclos_op += 3;
}

/* op_bloque con la lista ya contada (n en imm[20:16]) y las tres decisiones
   estaticas resueltas en b2: bit 0 banco de usuario, bit 1 escribir la base
   al final, bit 2 CPSR = SPSR al final. En b0: pre, suma, carga. */
static void d_bloque(const arm7_deco * e)
{
	DWORD lista = e->imm & 0xFFFF;
	int   n     = (int) (e->imm >> 16);
	int   carga = (e->b0 & 4) != 0;
	DWORD base  = LEER_R(e->b1);
	int   banco_viejo = arm7.banco;
	DWORD dir, fin;
	int   i;

	if (e->b0 & 2)
	{
		dir = (e->b0 & 1) ? base + 4 : base;
		fin = base + (DWORD) n * 4;
	}
	else
	{
		fin = base - (DWORD) n * 4;
		dir = (e->b0 & 1) ? fin : fin + 4;
	}

	if (e->b2 & 1)
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

	if (e->b2 & 1)
		cambiar_banco(banco_viejo);

	if (e->b2 & 2)
		arm7.r[e->b1] = fin;

	if (e->b2 & 4)
		poner_cpsr(arm7.spsr);

	ciclos_op += n + (carga ? 1 : 0);
}

static void d_mrs(const arm7_deco * e)
{
	poner_r(e->b1, e->b0 ? arm7.spsr : arm7.cpsr);
}

/* op_msr con la mascara de campos ya armada en imm2 y, en la forma
   inmediata, el valor ya rotado en imm. En b0: bit 0 inmediato, bit 1 SPSR.
   La restriccion de modo usuario depende del estado y se queda aqui. */
static void d_msr(const arm7_deco * e)
{
	DWORD valor   = (e->b0 & 1) ? e->imm : LEER_R(e->b1);
	DWORD mascara = e->imm2;

	if (!(e->b0 & 2) && (arm7.cpsr & ARM7_MODO) == ARM7_MODO_USR)
		mascara &= 0xFF000000u;

	if (e->b0 & 2)
	{
		arm7.spsr = (arm7.spsr & ~mascara) | (valor & mascara);
		return;
	}

	poner_cpsr((arm7.cpsr & ~mascara) | (valor & mascara));
}

/*
	La decodificacion: pura de la palabra, nunca del estado -- lo que dependa
	del estado (la restriccion de usuario de MSR, el acarreo de entrada) se
	resuelve en el manejador. La forma se elige por el manejador que la tabla
	expandida ya conoce, no repitiendo los patrones: asi no hay dos tablas
	que puedan derivar.
*/
static void arm7_decodificar(arm7_deco * e, DWORD op)
{
	void (* m)(DWORD palabra) = arm7_oplist[ARM7_INDICE(op)];

	arm7_deco_decodificadas++;

	e->palabra = op;
	e->cond    = (unsigned char) (op >> 28);
	e->b0 = e->b1 = e->b2 = 0;
	e->imm  = 0;
	e->imm2 = 0;
	e->fn   = d_generico;

	if (m == op_salto)
	{
		long desp = (long) (op & 0x00FFFFFF);

		if (desp & 0x00800000)
			desp |= ~0x00FFFFFFL;				/* signo */

		e->imm = (DWORD) ((desp << 2) + 8);
		e->b0  = (op & 0x01000000) != 0;
		e->fn  = d_salto;
	}
	else
	if (m == op_datos)
	{
		int codigo = (int) ((op >> 21) & 0xF);
		int s      = (int) ((op >> 20) & 1);

		e->b1 = (unsigned char) ((op >> 16) & 0xF);		/* rn */
		e->b2 = (unsigned char) ((op >> 12) & 0xF);		/* rd */

		if (op & 0x02000000)					/* inmediato rotado */
		{
			DWORD imm = op & 0xFF;
			DWORD rot = ((op >> 8) & 0xF) * 2;

			e->imm = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
			e->b0  = (unsigned char) (codigo | (rot ? 0x10 : 0));
			e->fn  = s ? d_alu_imm_s1 : d_alu_imm_s0;
		}
		else
		if (!(op & 0x10))						/* desplazamiento inmediato */
		{
			e->b0  = (unsigned char) codigo;
			e->imm = (op & 0xF)					/* rm */
			       | (((op >> 5) & 3) << 8)		/* tipo */
			       | (((op >> 7) & 0x1F) << 16);	/* cantidad */
			e->fn  = s ? d_alu_reg_s1 : d_alu_reg_s0;
		}
		else
		if (arm7_formas_anchas)					/* desplazamiento por registro */
		{
			e->b0  = (unsigned char) codigo;
			e->imm = (op & 0xF)					/* rm */
			       | (((op >> 5) & 3) << 8)		/* tipo */
			       | (((op >> 8) & 0xF) << 16);	/* rs */
			e->fn  = s ? d_alu_rr_s1 : d_alu_rr_s0;
		}
	}
	else
	if (m == op_transferencia)
	{
		e->b0 = (unsigned char) ((((op) >> 24) & 1)				/* pre */
		      | ((((op) >> 23) & 1) << 1)						/* suma */
		      | ((((op) >> 22) & 1) << 2)						/* byte */
		      | ((((op) >> 21) & 1) << 3));						/* writeback */
		e->b1 = (unsigned char) ((op >> 16) & 0xF);				/* rn */
		e->b2 = (unsigned char) ((op >> 12) & 0xF);				/* rd */

		if (!(op & 0x02000000))					/* forma inmediata */
		{
			e->imm = op & 0xFFF;
			e->fn  = (op & 0x00100000) ? d_ldr_imm : d_str_imm;
		}
		else
		if (arm7_formas_anchas)					/* desplazamiento por registro */
		{
			e->imm = (op & 0xF)					/* rm */
			       | (((op >> 5) & 3) << 8)		/* tipo */
			       | (((op >> 7) & 0x1F) << 16);	/* cantidad */
			e->fn  = (op & 0x00100000) ? d_ldr_reg : d_str_reg;
		}
		else
			e->b0 = e->b1 = e->b2 = 0;			/* d_generico: entrada limpia */
	}
	else
	if (m == op_bloque)
	{
		DWORD lista = op & 0xFFFF;
		int   n = 0;
		int   i;

		for (i = 0; i < 16; i++)
			if (lista & (1u << i))
				n++;

		if (n > 0)								/* lista vacia: d_generico */
		{
			int carga = (op & 0x00100000) != 0;
			int s     = (op & 0x00400000) != 0;
			int escr  = (op & 0x00200000) != 0;
			int rn    = (int) ((op >> 16) & 0xF);

			e->b0 = (unsigned char) ((((op) >> 24) & 1)			/* pre */
			      | ((((op) >> 23) & 1) << 1)					/* suma */
			      | (carga ? 4 : 0));
			e->b1 = (unsigned char) rn;
			e->b2 = (unsigned char)
			        ((s && !(carga && (lista & 0x8000)) ? 1 : 0)
			       | ((escr && !(carga && (lista & (1u << rn)))) ? 2 : 0)
			       | ((carga && s && (lista & 0x8000)) ? 4 : 0));
			e->imm = lista | ((DWORD) n << 16);
			e->fn  = d_bloque;
		}
	}
	else
	if (m == op_mrs)
	{
		e->b0 = (op & 0x00400000) != 0;
		e->b1 = (unsigned char) ((op >> 12) & 0xF);
		e->fn = d_mrs;
	}
	else
	if (m == op_msr)
	{
		DWORD campos  = (op >> 16) & 0xF;
		DWORD mascara = 0;

		if (campos & 1)		mascara |= 0x000000FFu;
		if (campos & 2)		mascara |= 0x0000FF00u;
		if (campos & 4)		mascara |= 0x00FF0000u;
		if (campos & 8)		mascara |= 0xFF000000u;

		e->imm2 = mascara;
		e->b0   = (unsigned char) (((op & 0x02000000) ? 1 : 0)
		        | ((op & 0x00400000) ? 2 : 0));

		if (op & 0x02000000)
		{
			DWORD imm = op & 0xFF;
			DWORD rot = ((op >> 8) & 0xF) * 2;

			e->imm = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
		}
		else
			e->b1 = (unsigned char) (op & 0xF);

		e->fn = d_msr;
	}
	else
	if (m == op_multiplicar && arm7_formas_anchas)
	{
		e->b0 = (unsigned char) ((((op >> 21) & 1))				/* A */
		      | (((op >> 20) & 1) << 1));						/* S */
		e->b2 = (unsigned char) ((op >> 16) & 0xF);				/* rd */
		e->imm = (op & 0xF)					/* rm */
		       | (((op >> 8) & 0xF) << 8)	/* rs */
		       | (((op >> 12) & 0xF) << 16);	/* rn */
		e->fn = d_mul;
	}
	/* SWP, SWI e indefinidas quedan en d_generico. */
}

/* La forma de una entrada, para el traductor -- que no puede comparar los
   manejadores porque son estaticos de este archivo a proposito. */
int arm7_deco_forma(const arm7_deco * e)
{
	if (e->fn == d_alu_imm_s0)	return ARM7_DF_ALU_IMM_S0;
	if (e->fn == d_alu_imm_s1)	return ARM7_DF_ALU_IMM_S1;
	if (e->fn == d_alu_reg_s0)	return ARM7_DF_ALU_REG_S0;
	if (e->fn == d_alu_reg_s1)	return ARM7_DF_ALU_REG_S1;
	if (e->fn == d_ldr_imm)		return ARM7_DF_LDR_IMM;
	if (e->fn == d_str_imm)		return ARM7_DF_STR_IMM;
	if (e->fn == d_bloque)		return ARM7_DF_BLOQUE;
	if (e->fn == d_mrs)			return ARM7_DF_MRS;
	if (e->fn == d_ldr_reg)		return ARM7_DF_LDR_REG;
	if (e->fn == d_str_reg)		return ARM7_DF_STR_REG;

	/* d_alu_rr y d_mul no tocan memoria: ARM7_DF_OTRA les da la llamada
	   generica, que es exactamente lo que necesitan. */
	return ARM7_DF_OTRA;
}

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

	/*
		La tabla de predecodificacion, entera como la palabra 0. Ver el
		comentario del bloque: la unica condicion de validez es
		palabra == memoria, y con este llenado vale tambien para una entrada
		fria que se encuentre con un cero de verdad. Una vez al arrancar,
		como todas las sondas del arbol; apagada no se toca ni una pagina.
	*/
	{
		const char * e = getenv("DCEMU_SIN_PREDECO_ARM");
		const char * b = getenv("DCEMU_SIN_BLOQUES_ARM");

		arm7_predeco = !(e != NULL && atoi(e) != 0);

		/* Los bloques ejecutan por las entradas predecodificadas: apagar la
		   predecodificacion los apaga tambien. */
		arm7_bloques = arm7_predeco && !(b != NULL && atoi(b) != 0);

		{
			const char * r = getenv("DCEMU_SIN_RAMA_ARM");
			const char * s = getenv("DCEMU_SIN_SONDEO_ARM");
			const char * f = getenv("DCEMU_SIN_FORMAS_ARM");
			const char * a = getenv("DCEMU_SIN_CABE_ARM");

			/* La cola de salto de los bloques; sin bloques, sin cola. */
			arm7_blq_rama = arm7_bloques && !(r != NULL && atoi(r) != 0);

			/* La lectura del archivo vuelve a cortar el bloque: la conducta
			   anterior del teorema 4, y el brazo del A/B. */
			arm7_lectura_corta = (s != NULL && atoi(s) != 0);

			/* Las formas anchas; sin predecodificacion, sin formas. Y la
			   admision ancha en bloques encima de ellas: sin formas (o sin
			   bloques), la admision anterior entera. */
			arm7_formas_anchas = arm7_predeco && !(f != NULL && atoi(f) != 0);
			arm7_cabe_ancho = arm7_formas_anchas && arm7_bloques
			               && !(a != NULL && atoi(a) != 0);

			{
				const char * t = getenv("DCEMU_SIN_RETORNO_ARM");

				/* La cola generalizada vive sobre la cola: sin rama, sin
				   retorno. No depende de las formas anchas -- es admision,
				   no decodificacion. */
				arm7_blq_retorno = arm7_blq_rama && !(t != NULL && atoi(t) != 0);
			}

			{
				const char * v = getenv("DCEMU_SIN_VERIF_ONDA");

				/* La verificacion por generacion de onda: solo pesa con
				   bloques; sin ellos nadie consulta los sellos. */
				arm7_verif_onda = !(v != NULL && atoi(v) != 0);
			}

			{
				const char * c = getenv("DCEMU_SIN_CADENA_ARM");

				/* El encadenado emitido vive sobre la cola de salto (y sobre
				   el emisor, que puede no estar instalado): sin rama, sin
				   cadena. */
				arm7_blq_cadena = arm7_blq_rama && !(c != NULL && atoi(c) != 0);
			}
		}

		if (arm7_predeco)
		{
			arm7_deco cero;

			arm7_decodificar(&cero, 0);

			for (i = 0; i < (int) (AICA_ONDA_SIZE / 4); i++)
				arm7_deco_tabla[i] = cero;

			arm7_deco_decodificadas = 0;
		}
	}
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

	if (arm7_predeco)
		fprintf(stderr, "arm7: %llu palabras decodificadas (el resto de los "
			"pasos reuso una entrada)\n", arm7_deco_decodificadas);

	if (arm7_bloques && arm7_blq_corridos)
	{
		fprintf(stderr, "arm7: %llu bloques corridos, %llu pasos en bloque "
			"(%.1f %% de los pasos, %.1f por corrida), %llu vueltas en el "
			"lugar, %llu encadenados\n",
			arm7_blq_corridos, arm7_blq_pasos,
			100.0 * (double) arm7_blq_pasos / (double) arm7_perfil_pasos,
			(double) arm7_blq_pasos / (double) arm7_blq_corridos,
			arm7_blq_vueltas, arm7_blq_encadenados);

		/* Cada rechazo de intentar() es un paso interpretado: el censo de
		   por que. Solo se cuenta bajo el perfil, como todo lo de aca. */
		for (i = 0; i < 5; i++)
			if (arm7_blq_rechazo[i])
				fprintf(stderr, "arm7:   rechazo por %-21s %12llu (%.1f %% de los pasos)\n",
					arm7_blq_rechazo_nombre[i], arm7_blq_rechazo[i],
					100.0 * (double) arm7_blq_rechazo[i] / (double) arm7_perfil_pasos);

		fprintf(stderr, "arm7:   %llu descubrimientos de bloque\n",
			arm7_blq_descubrimientos);

		/* Prefijo propio, como "arm7 neg:": es un contador del mecanismo y
		   las compuertas que comparan los histogramas ^arm7: no deben verlo. */
		if (arm7_verif_memcmp || arm7_verif_elididas)
			fprintf(stderr, "arm7 verif: %llu memcmp, %llu elididas por lote "
				"(%.1f %%)\n",
				arm7_verif_memcmp, arm7_verif_elididas,
				100.0 * (double) arm7_verif_elididas
					/ (double) (arm7_verif_memcmp + arm7_verif_elididas));

		/* El censo del giro puro (fase C): mismo trato de prefijo. */
		if (arm7_giro_puras || arm7_giro_impuras)
			fprintf(stderr, "arm7 giro: %llu vueltas puras (%llu pasos, "
				"%.1f %% de los pasos), %llu impuras\n",
				arm7_giro_puras, arm7_giro_pasos_puros,
				100.0 * (double) arm7_giro_pasos_puros
					/ (double) arm7_perfil_pasos,
				arm7_giro_impuras);

		if (arm7_blq_rechazo[3])
			arm7_blq_neg_resumen();
	}

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
/* Bloques sobre la predecodificacion                                       */
/* ------------------------------------------------------------------------ */

/*
	El escalon 2 de la fase 4 (docs/arm7-plan.md, "El diseno del escalon 2"):
	un tramo recto de instrucciones que no pueden tocar PC ni el modo,
	ejecutado con la verificacion hecha una vez, un solo chequeo de FIQ y el
	PC avanzando de a 4 sin preguntar. Lo que quita por instruccion respecto
	de arm7_paso(): el chequeo de FIQ, la busqueda con sus mascaras, la rama
	del avance de PC y los centinelas de la memoizacion.

	Los cuatro teoremas que lo hacen exacto estan en el plan; en resumen:

	  1. dentro de un lote, la FIQ solo cambia de estado si el ARM toca el
	     archivo de registros o escribe CPSR -- aica_tick() corre entre lotes;
	  2. se entra al bloque solo si sus ciclos maximos caben en el saldo, asi
	     el lote se detiene en la misma frontera que deteniendose paso a paso;
	  3. la memoizacion convive: no se corre bloque mientras se graba, y el
	     borde de atras -- donde se graba y repone -- es un salto, que siempre
	     ejecuta por el interprete;
	  4. un acceso con direccion dinamica puede caer en el archivo de
	     registros: arm7_toco_reg deja la marca y el bloque sale por el
	     costado en esa frontera, que es donde el interprete habria mirado.

	La cola de salto (2026-08-19, fase E de docs/jit-sota-plan.md): si lo que
	corto el tramo es un B/BL, entra como ultima entrada y corre dentro del
	bloque -- el analogo de los pares del SH-4. El censo de CT dijo por que:
	B/BL es el 25,1 % del despacho y solo el 36,8 % de los pasos corria en
	bloques (2,9 por bloque). La cola ejecuta por d_salto, asi que el borde
	de la memoizacion corre identico por construccion; si arranca una
	grabacion, el bloque sale y la contabilidad del barrido queda como la del
	interprete. Y si el salto vuelve a la propia cabecera, el bloque da la
	vuelta en el lugar sin pasar por intentar(): la FIQ no pudo cambiar
	(teorema 1: las rectas no tocaron el archivo ni el CPSR de control), y el
	presupuesto (teorema 2) y las palabras se re-verifican adentro igual que
	en intentar().

	DCEMU_SIN_BLOQUES_ARM=1 los apaga en el mismo binario, que es el A/B;
	DCEMU_SIN_RAMA_ARM=1 apaga solo la cola de salto.
*/

/* ARM7_BLQ_RANURAS vive junto a los contadores del perfil, que lo usan. */
#define ARM7_BLQ_MAX		12				/* instrucciones por bloque */
#define ARM7_BLQ_MIN		2				/* mas corto que esto no paga */

typedef struct
{
	DWORD			base;					/* direccion de bus de la palabra 0 */
	unsigned char	n;						/* entradas, cola incluida;
											   0: marca negativa (aca no conviene) */
	unsigned char	salto;					/* 1: la ultima entrada es un B/BL */
	unsigned char	relleno[2];
	int				ciclos_max;
	DWORD			palabras[ARM7_BLQ_MAX];

	/* La copia privada de las entradas: el traductor emite leyendo de aca, y
	   por eso el codigo emitido no depende de la tabla compartida -- que un
	   paso ajeno puede redecodificar. Inmutables entre emision y corrida. */
	arm7_deco		entradas[ARM7_BLQ_MAX];
	void *			codigo;					/* emitido, o NULL: el lazo en C */

	/* El sello de la verificacion por generacion de onda: las palabras estan
	   verificadas mientras las generaciones de sus (a lo sumo dos) paginas
	   coincidan con onda_gen[]. */
	unsigned long	verif_gen[2];

	/* Los punteros a onda_gen de esas paginas, fijados en descubrir: el
	   sello se compara contra *pgen[i], y son lo que el encadenado emitido
	   carga sin recomputar paginas. */
	unsigned long *	pgen[2];

	/* El encadenado emitido: la entrada interna post-prologo (adonde saltan
	   los encadenados de otros bloques), y si el epilogo de la cola quedo
	   emitido -- entonces codigo() lo hace todo y el lazo en C solo corta. */
	void *			cadena;
	unsigned char	cola_emitida;
} arm7_blq;

/* Las dos paginas de onda que cubren las palabras del bloque, por los
   punteros que descubrir dejo fijados (pgen es valido siempre que n > 0,
   que es lo que todo llamador ya comprobo). Con una sola pagina, pgen[1]
   apunta a la misma y las dos comparaciones son la misma. */
static int arm7_blq_verificado(const arm7_blq * b, DWORD dir)
{
	(void) dir;

	return b->verif_gen[0] == *b->pgen[0]
	    && b->verif_gen[1] == *b->pgen[1];
}

static void arm7_blq_sellar(arm7_blq * b, DWORD dir)
{
	(void) dir;

	b->verif_gen[0] = *b->pgen[0];
	b->verif_gen[1] = *b->pgen[1];
}

static arm7_blq	arm7_blqs[ARM7_BLQ_RANURAS];

/* El informe de la sonda de marcas negativas (declarada con los contadores
   del perfil): las doce ranuras mas golpeadas, con su base, su cabecera y la
   fila -- lo que dice QUE instruccion conviene admitir en arm7_blq_cabe(). */
static void arm7_blq_neg_resumen(void)
{
	int i, j;

	for (j = 0; j < 12; j++)
	{
		unsigned int	mejor = 0;
		int				donde = -1;

		for (i = 0; i < ARM7_BLQ_RANURAS; i++)
			if (arm7_blq_neg[i] > mejor)
			{
				mejor = arm7_blq_neg[i];
				donde = i;
			}

		if (donde < 0)
			break;

		{
			const arm7_blq * b  = &arm7_blqs[donde];
			DWORD			 op = b->palabras[0];
			int				 f  = arm7_opfila[ARM7_INDICE(op)];

			fprintf(stderr, "arm7 neg:   %06lx  %10u  %08lx  %s%s\n",
				(unsigned long) b->base, mejor, (unsigned long) op,
				(f >= 0) ? filas[f].nombre : "?",
				(b->n != 0) ? "  (la ranura ya no es negativa)" : "");
		}

		arm7_blq_neg[donde] = 0;
	}
}

int				arm7_blq_ult_pasos;			/* del ultimo bloque corrido; lo
											   escribe tambien el emitido */

/* El traductor instalado, o NULL: todo por el lazo en C. */
static void * (* arm7_blq_emitir)(const arm7_deco * entradas, int n,
                                  DWORD dir, const arm7_cola_emitir * cola,
                                  void ** cadena) = NULL;

void arm7_blq_instalar_emisor(void * (* emitir)(const arm7_deco * entradas,
                                                int n, DWORD dir,
                                                const arm7_cola_emitir * cola,
                                                void ** cadena))
{
	arm7_blq_emitir = emitir;

	/* Los bloques ya descubiertos quedaron sin codigo (o con codigo de un
	   emisor anterior): que se redescubran. */
	memset(arm7_blqs, 0, sizeof(arm7_blqs));
}

/*
	-1 si la instruccion no puede ir en un bloque; si puede, su costo maximo
	en ciclos. Todo estatico sobre la entrada predecodificada. Terminan el
	bloque: d_salto y d_msr (PC y CPSR), d_generico entero (MUL/SWP/SWI/
	indefinidas/formas Rs pueden escribir PC o levantar excepcion), y toda
	forma con destino o writeback sobre R15.
*/
static int arm7_blq_cabe(const arm7_deco * e)
{
	if (e->fn == d_alu_imm_s0 || e->fn == d_alu_imm_s1
	 || e->fn == d_alu_reg_s0 || e->fn == d_alu_reg_s1)
	{
		int codigo = e->b0 & 0xF;

		/* rd=15 con escritura es un salto; los codigos 8-11 no escriben. */
		if (e->b2 == 15 && !(codigo >= 0x8 && codigo <= 0xB))
			return -1;

		return 1;
	}

	if (e->fn == d_ldr_imm)
	{
		if (e->b2 == 15)					/* carga al PC */
			return -1;

		if (e->b1 == 15 && (!(e->b0 & 1) || (e->b0 & 8)))	/* writeback a R15 */
			return -1;

		return 3;
	}

	if (e->fn == d_str_imm)
	{
		if (e->b1 == 15 && (!(e->b0 & 1) || (e->b0 & 8)))
			return -1;

		return 2;
	}

	if (e->fn == d_bloque)
	{
		/* PC en la lista: CARGARLO es un salto y no entra; guardarlo no --
		   un STM escribe PC+12 y el bloque sigue derecho. La relajacion es
		   del cabe ancho: salio del censo (seis de las doce ranuras mas
		   golpeadas eran STMFD sp!,{pc}). */
		if ((e->imm & 0x8000) && ((e->b0 & 4) || !arm7_cabe_ancho))
			return -1;

		if (e->b1 == 15 && (e->b2 & 2))		/* fin escrito sobre R15 */
			return -1;

		return 1 + (int) (e->imm >> 16) + ((e->b0 & 4) ? 1 : 0);
	}

	if (e->fn == d_mrs)
		return (e->b1 == 15) ? -1 : 1;

	/* Las formas anchas, solo con la admision ancha encendida. Las guardas
	   son las de sus parientes: destino o writeback sobre R15, afuera. */
	if (arm7_cabe_ancho)
	{
		if (e->fn == d_alu_rr_s0 || e->fn == d_alu_rr_s1)
		{
			int codigo = e->b0 & 0xF;

			if (e->b2 == 15 && !(codigo >= 0x8 && codigo <= 0xB))
				return -1;

			return 2;						/* 1 + el ciclo del desplazador */
		}

		if (e->fn == d_mul)
			return (e->b2 == 15) ? -1 : (4 + (e->b0 & 1));

		if (e->fn == d_ldr_reg)
		{
			if (e->b2 == 15)				/* carga al PC */
				return -1;

			if (e->b1 == 15 && (!(e->b0 & 1) || (e->b0 & 8)))
				return -1;

			return 3;
		}

		if (e->fn == d_str_reg)
		{
			if (e->b1 == 15 && (!(e->b0 & 1) || (e->b0 & 8)))
				return -1;

			return 2;
		}
	}

	return -1;
}

/*
	Descubre el bloque que empieza en `dir` (bus, bit 23 en cero) y llena la
	ranura. El tramo no puede cruzar hacia el archivo de registros: el avance
	del PC es lineal y la ventana de onda termina en 0x00800000.
*/
static void arm7_blq_descubrir(arm7_blq * b, DWORD dir)
{
	int n = 0;
	int salto = 0;
	int ciclos = 0;

	/* Frio por diseno: si este contador sale caliente, dos bloques vivos
	   comparten ranura y se desalojan mutuamente. */
	arm7_blq_descubrimientos++;

	b->base = dir;

	/* Dos cotas ademas del largo: no cruzar hacia el archivo de registros
	   (la ventana de onda termina en 0x00800000) y no cruzar un espejo de
	   los 2 MB -- la verificacion es un memcmp lineal sobre sound_mem. */
	while (n < ARM7_BLQ_MAX && dir + (DWORD) n * 4 < 0x00800000u
	    && (dir & (AICA_ONDA_SIZE - 1)) + (DWORD) (n + 1) * 4 <= AICA_ONDA_SIZE)
	{
		DWORD fis = (dir + (DWORD) n * 4) & (AICA_ONDA_SIZE - 1);
		DWORD op  = onda_leer32(fis);
		arm7_deco * e = &arm7_deco_tabla[fis >> 2];
		int c;

		if (e->palabra != op)
			arm7_decodificar(e, op);

		c = arm7_blq_cabe(e);

		if (c < 0)
			break;

		b->palabras[n] = op;
		b->entradas[n] = *e;
		ciclos += c;
		n++;
	}

	/* La cola: si lo que corto el tramo es una TERMINAL, entra al bloque
	   como ultima entrada y corre aca adentro (el analogo de los pares del
	   SH-4). Dos terminales: el B/BL -- d_salto, con cualquier condicion;
	   ejecuta por su propio manejador, asi que el borde de la memoizacion
	   corre identico por construccion -- y, con el retorno encendido, el
	   LDM que carga el PC sin el bit S (con S escribe CPSR y cambia de
	   modo: ese no encadena). Con el retorno la terminal puede ademas
	   estar SOLA: las tres ranuras LDM del censo eran destinos de salto
	   directos, y una cola sola vale por el encadenado que sigue -- lo que
	   antes decia "un B a secas no gana nada" dejo de ser cierto cuando
	   aparecio el encadenado. Las mismas dos cotas del lazo de arriba. */
	if (arm7_blq_rama && (n >= 1 || arm7_blq_retorno) && n < ARM7_BLQ_MAX
	 && dir + (DWORD) n * 4 < 0x00800000u
	 && (dir & (AICA_ONDA_SIZE - 1)) + (DWORD) (n + 1) * 4 <= AICA_ONDA_SIZE)
	{
		DWORD fis = (dir + (DWORD) n * 4) & (AICA_ONDA_SIZE - 1);
		DWORD op  = onda_leer32(fis);
		arm7_deco * e = &arm7_deco_tabla[fis >> 2];

		if (e->palabra != op)
			arm7_decodificar(e, op);

		if (e->fn == d_salto)
		{
			b->palabras[n] = op;
			b->entradas[n] = *e;
			ciclos += 3;					/* 1 + 2 del salto tomado */
			n++;
			salto = 1;
		}
		else
		if (arm7_blq_retorno && e->fn == d_bloque
		 && (e->b0 & 4) && (e->imm & 0x8000)	/* carga que incluye al PC */
		 && !(e->b2 & 4)						/* sin CPSR = SPSR al final */
		 && !(e->b1 == 15 && (e->b2 & 2)))		/* sin fin sobre R15 */
		{
			b->palabras[n] = op;
			b->entradas[n] = *e;
			ciclos += 2 + (int) (e->imm >> 16);	/* 1 + n + 1 de la carga */
			n++;
			salto = 1;
		}
	}

	if (n < ARM7_BLQ_MIN && !salto)
	{
		/* La marca negativa guarda la palabra de cabecera: si alguien la
		   reescribe, la marca se invalida sola por la misma comparacion. */
		b->palabras[0] = onda_leer32(dir & (AICA_ONDA_SIZE - 1));
		b->n      = 0;
		b->salto  = 0;
		b->codigo = NULL;
		return;
	}

	b->n     = (unsigned char) n;
	b->salto = (unsigned char) salto;
	b->ciclos_max = ciclos;

	{
		DWORD a = dir & (AICA_ONDA_SIZE - 1);

		b->pgen[0] = &onda_gen[a >> ONDA_PAG_BITS];
		b->pgen[1] = &onda_gen[(a + (DWORD) n * 4 - 1) >> ONDA_PAG_BITS];
	}

	b->cadena       = NULL;
	b->cola_emitida = 0;

	/* Con traductor instalado, el bloque sale emitido -- las rectas, y si la
	   cola es un B/BL (d_salto) tambien el epilogo del encadenado: la cola y
	   el salto directo al sucesor, sin volver al lazo en C por cada salto.
	   El retorno (LDM al PC) y demas terminales siguen saliendo al C -- toda
	   salida al C cae en una frontera de instruccion con el estado entero
	   consistente, y el despachador sigue solo. NULL deja el lazo en C, que
	   es tambien el destino de todo bloque si el arena se llena. Una
	   terminal sola no tiene rectas y no emite nada. */
	if (arm7_blq_emitir != NULL && n - salto > 0)
	{
		arm7_cola_emitir		 ce;
		const arm7_cola_emitir * pce = NULL;
		const arm7_deco *		 e   = &b->entradas[n - 1];

		if (arm7_blq_cadena && salto && e->fn == d_salto)
		{
			DWORD pc_cola = dir + 4u * (unsigned) (n - 1);
			DWORD destino = (pc_cola + e->imm) & ARM7_BUS;
			DWORD caida   = (pc_cola + 4) & ARM7_BUS;

			ce.off_base       = (int) offsetof(arm7_blq, base);
			ce.off_n          = (int) offsetof(arm7_blq, n);
			ce.off_ciclos_max = (int) offsetof(arm7_blq, ciclos_max);
			ce.off_verif0     = (int) offsetof(arm7_blq, verif_gen[0]);
			ce.off_verif1     = (int) offsetof(arm7_blq, verif_gen[1]);
			ce.off_pgen0      = (int) offsetof(arm7_blq, pgen[0]);
			ce.off_pgen1      = (int) offsetof(arm7_blq, pgen[1]);
			ce.off_cadena     = (int) offsetof(arm7_blq, cadena);

			ce.pc_cola = pc_cola;
			ce.cond    = e->palabra >> 28;
			ce.bl      = (e->b0 != 0);
			ce.atras   = (destino < pc_cola && !ce.bl);
			ce.destino = destino;

			ce.salto.slot = &arm7_blqs[(destino >> 2) & (ARM7_BLQ_RANURAS - 1)];
			ce.salto.base = destino;
			ce.caida.slot = &arm7_blqs[(caida >> 2) & (ARM7_BLQ_RANURAS - 1)];
			ce.caida.base = caida;

			pce = &ce;
		}

		b->codigo = arm7_blq_emitir(b->entradas, n - salto, dir, pce,
			&b->cadena);
		b->cola_emitida = (unsigned char) (b->codigo != NULL && pce != NULL);

		if (b->codigo == NULL)
			b->cadena = NULL;
	}
	else
		b->codigo = NULL;
}

/* Corre el bloque entero (o hasta la salida lateral), la cola de salto si la
   hay, y si esa cola volvio a la propia cabecera, da la vuelta sin pasar por
   arm7_blq_intentar(). Devuelve los ciclos consumidos y deja en
   arm7_blq_ult_pasos las instrucciones ejecutadas. */
static int arm7_blq_correr(const arm7_blq * b)
{
	DWORD base    = b->base;
	int   rectas  = (int) b->n - (int) b->salto;
	int   gastado = 0;
	int   pasos   = 0;

	DWORD giro_base = b->base;			/* censo del giro puro (solo perfil) */
	DWORD giro_regs[15];
	DWORD giro_cpsr = 0;
	int   giro_marca = 0;

	if (arm7_perfil)
	{
		memcpy(giro_regs, arm7.r, sizeof(giro_regs));
		giro_cpsr = arm7.cpsr;
	}

	/*
		El camino emitido cubre las rectas. Tres condiciones ademas de tener
		codigo: el PC tiene que ser EXACTAMENTE la base (el emitido bakea PC+8
		como constante, y tras el tope del bus r15 puede traer bits altos de
		mas), y los dos instrumentos por paso -- el perfil y el censo de la
		suite -- corren por el lazo en C, que es el que lleva sus ganchos.
	*/
	int instrumentos = (arm7_perfil || arm7_cobertura);

	for (;;)
	{
		int i;

		if (!instrumentos && b->codigo != NULL && arm7.r[15] == base)
		{
			gastado += ((int (*)(void)) b->codigo)();
			i = arm7_blq_ult_pasos;

			/*
				El encadenado emitido: codigo() ya corrio la cola y siguio por
				los sucesores hasta donde pudo -- toda salida cae en una
				frontera de instruccion con el estado consistente, i es el
				total acumulado de todos los tramos y gastado lo NO
				comprometido. Aca solo se corta; el despachador sigue solo (y
				su chequeo de FIQ ve lo mismo que veria el lazo: los replays
				son de solo lectura y las escrituras salen por el costado).
			*/
			if (b->cola_emitida)
			{
				pasos += i;
				break;
			}
		}
		else
		{
			arm7_toco_reg = 0;

			for (i = 0; i < rectas; i++)
			{
				DWORD op = b->palabras[i];
				arm7_deco * e =
					&arm7_deco_tabla[((base + (DWORD) i * 4) & (AICA_ONDA_SIZE - 1)) >> 2];

				ciclos_op = 1;
				arm7.instrucciones++;

				if (arm7_perfil)
				{
					arm7_perfil_pc[((arm7.r[15] & ARM7_BUS) >> 2) % ARM7_PERFIL_PCS]++;
					arm7_perfil_pasos++;

					{
						int f = arm7_opfila[ARM7_INDICE(op)];

						if (f >= 0)
							arm7_perfil_fila[f]++;
					}
				}

				if ((op >> 28) == 0xE || condicion(op))
				{
					if (arm7_cobertura)
					{
						int f = arm7_opfila[ARM7_INDICE(op)];

						if (f >= 0)
							arm7_usada[f] = 1;
					}

					/* La entrada pudo quedar decodificada de otra palabra (se
					   comparte con el paso a paso): la palabra del bloque ya esta
					   verificada contra la memoria, asi que manda ella. */
					if (e->palabra != op)
						arm7_decodificar(e, op);

					e->fn(e);
				}

				arm7.r[15] += 4;
				gastado    += ciclos_op;

				/* Teorema 4: el acceso cayo en el archivo de registros y la FIQ pudo
				   cambiar. Se sale en esta frontera, que es donde el interprete
				   habria mirado. */
				if (arm7_toco_reg)
				{
					i++;
					break;
				}
			}
		}

		pasos += i;

		/*
			Con rectas incompletas el bloque termina aca. La salida lateral
			manda incluso con las rectas completas -- el acceso de la ultima
			pudo caer en el archivo --: la FIQ pudo cambiar, y lo que siga
			correria sin el chequeo que el interprete hace en el limite de
			cada instruccion.
		*/
		if (i < rectas || arm7_toco_reg)
			break;

		if (b->salto)
		{
			/*
				Antes de la cola se comprometen los ciclos ya gastados, y la
				funcion devuelve solo lo no comprometido. La reposicion del borde
				compara el costo del barrido contra arm7.ciclos (sus "dos
				condiciones de tiempo"), y el interprete llega al salto con el
				cuerpo ya cobrado: sin esto la cola le mostraba el saldo de la
				ENTRADA del bloque -- mas grande -- y aceptaba reposiciones que el
				paso a paso rechaza. El histograma de la compuerta lo cazo:
				177 004 pasos menos ejecutados en 30 s de CT, con la captura y el
				.wav intactos -- esta vez.
			*/
			arm7.ciclos -= gastado;
			gastado = 0;

			/*
				La cola de salto: la ultima entrada es un B/BL y corre aca
				adentro, con la semantica del limite de instruccion del
				interprete. Sin chequeo de FIQ: por el teorema 1 no pudo cambiar
				desde el que hizo intentar() -- las rectas no tocaron el archivo
				(recien verificado, y una lectura no mueve la FIQ) ni el CPSR de
				control (d_msr no entra en bloques, y la ALU con S solo escribe
				NZCV).
			*/
			{
				const arm7_deco * e = &b->entradas[rectas];
				DWORD op = b->palabras[rectas];

				ciclos_op = 1;
				pc_cambio = 0;
				arm7.instrucciones++;

				if (arm7_perfil)
				{
					arm7_perfil_pc[((arm7.r[15] & ARM7_BUS) >> 2) % ARM7_PERFIL_PCS]++;
					arm7_perfil_pasos++;

					{
						int f = arm7_opfila[ARM7_INDICE(op)];

						if (f >= 0)
							arm7_perfil_fila[f]++;
					}
				}

				if ((op >> 28) == 0xE || condicion(op))
				{
					if (arm7_cobertura)
					{
						int f = arm7_opfila[ARM7_INDICE(op)];

						if (f >= 0)
							arm7_usada[f] = 1;
					}

					/* La entrada privada, como el emisor: inmutable y decodificada
					   de esta palabra, que el memcmp de la entrada verifico. */
					e->fn(e);
				}

				pasos   += 1;
				gastado += ciclos_op;

				if (!pc_cambio)
				{
					/*
						Condicion no cumplida: cae a la siguiente, como el
						interprete. El `terminar` del borde no puede tocar aca:
						arm7_memo_fin era ~0 al entrar (teorema 3) y lo unico de
						aca adentro que lo arma es esta misma cola -- que entonces
						deja pc_cambio en 1.
					*/
					arm7.r[15] += 4;
				}
				else
				if (arm7_memo_fin != ~0u)
				{
					/* El salto arranco una grabacion: la contabilidad del
					   barrido es la del interprete -- y grabando no se corre
					   bloque. */
					memo_ciclos += ciclos_op;
					memo_instr++;
					break;
				}
			}

			/* La cola de retorno lee memoria (la pila) y pudo caer en el
			   archivo de registros: la salida lateral manda tambien aca,
			   antes de encadenar. Un B/BL no puede armarla, asi que para
			   la cola clasica el chequeo es un no-op predecible. */
			if (arm7_toco_reg)
				break;
		}

		/*
			El encadenado en el lugar: el PC quedo en una direccion que puede
			tener bloque ya descubierto -- la propia cabecera (la vuelta del
			lazo), la de otro bloque (el lazo caliente de sondeo de CT es un
			ciclo de DOS bloques, porque su salida de en medio lo parte), o
			la caida de una cola no tomada o de un tramo sin cola. Se sigue
			corriendo sin pasar por intentar(): la FIQ sigue cubierta por el
			teorema 1 por induccion -- una escritura al archivo corta por el
			costado antes de llegar aca, y la grabacion corta arriba --, y el
			presupuesto (teorema 2) y las palabras del proximo bloque se
			re-verifican igual que alla. Solo se encadena a bloques ya
			descubiertos: el descubrimiento queda en intentar(), detras de su
			chequeo de FIQ.
		*/
		{
			DWORD dir = arm7.r[15] & ARM7_BUS;
			arm7_blq * b2;

			/* La palanca de la cola manda sobre el encadenado entero: el
			   brazo viejo del A/B es la conducta anterior exacta. */
			if (!arm7_blq_rama)
				break;

			if (dir & 0x00800000)
				break;

			b2 = &arm7_blqs[(dir >> 2) & (ARM7_BLQ_RANURAS - 1)];

			if (b2->base != dir || b2->n == 0)
				break;

			if ((long) (gastado + b2->ciclos_max) > arm7.ciclos)
				break;

			/* La verificacion por generacion de onda: el memcmp corre solo si
			   alguna pagina del bloque se escribio desde el ultimo sello (ver
			   el comentario junto a arm7_verif_onda). Con la palanca apagada
			   se compara siempre, la conducta anterior. */
			if (!arm7_verif_onda || !arm7_blq_verificado(b2, dir))
			{
				if (arm7_perfil)
					arm7_verif_memcmp++;

				if (memcmp(b2->palabras, sound_mem + (dir & (AICA_ONDA_SIZE - 1)),
				           (size_t) b2->n * 4) != 0)
					break;

				arm7_blq_sellar(b2, dir);
			}
			else if (arm7_perfil)
				arm7_verif_elididas++;

			if (b2 == b)
				arm7_blq_vueltas++;
			else
				arm7_blq_encadenados++;

			/* El censo del giro puro: el ciclo se cierra al volver a la base
			   de entrada; pura = registros y CPSR identicos a la vuelta
			   anterior (r15 es la base en las dos). Tras una impura se toma
			   la instantanea nueva: un lazo con preambulo cierra impuro una
			   vez y puro las demas. */
			if (arm7_perfil && dir == giro_base)
			{
				if (memcmp(giro_regs, arm7.r, sizeof(giro_regs)) == 0
					&& giro_cpsr == arm7.cpsr)
				{
					arm7_giro_puras++;
					arm7_giro_pasos_puros +=
						(unsigned long long) (pasos - giro_marca);
				}
				else
				{
					arm7_giro_impuras++;
					memcpy(giro_regs, arm7.r, sizeof(giro_regs));
					giro_cpsr = arm7.cpsr;
				}

				giro_marca = pasos;
			}

			b      = b2;
			base   = dir;
			rectas = (int) b->n - (int) b->salto;
		}
	}

	arm7_blq_ult_pasos = pasos;
	arm7_blq_corridos++;
	arm7_blq_pasos += (unsigned long long) pasos;

	return gastado;
}

/*
	Intenta correr un bloque desde el PC. Devuelve los ciclos consumidos, o 0
	si aca no hay bloque que valga -- y entonces el lote da un paso normal,
	que es donde viven la FIQ, los saltos, la memoizacion y todo lo demas.
*/
static int arm7_blq_intentar(void)
{
	DWORD      dir = arm7.r[15] & ARM7_BUS;
	arm7_blq * b;

	if (dir & 0x00800000)
	{
		if (arm7_perfil)
			arm7_blq_rechazo[0]++;
		return 0;
	}

	/* Teorema 1: la FIQ del limite de instruccion se mira una vez aca; si
	   esta por entregarse, que la entregue arm7_paso(). */
	if (!(arm7.cpsr & ARM7_F) && aica_fiq_pendiente())
	{
		if (arm7_perfil)
			arm7_blq_rechazo[1]++;
		return 0;
	}

	/* Teorema 3: mientras se graba un barrido, todo va por el interprete. */
	if (arm7_memo_fin != ~0u)
	{
		if (arm7_perfil)
			arm7_blq_rechazo[2]++;
		return 0;
	}

	b = &arm7_blqs[(dir >> 2) & (ARM7_BLQ_RANURAS - 1)];

	if (b->base != dir)
		arm7_blq_descubrir(b, dir);
	else
	if (b->n == 0)
	{
		/* Marca negativa vigente mientras la cabecera no cambie. */
		if (b->palabras[0] == onda_leer32(dir & (AICA_ONDA_SIZE - 1)))
		{
			if (arm7_perfil)
			{
				arm7_blq_rechazo[3]++;
				arm7_blq_neg[b - arm7_blqs]++;
			}
			return 0;
		}

		arm7_blq_descubrir(b, dir);
	}
	else
	if (!arm7_verif_onda || !arm7_blq_verificado(b, dir))
	{
		if (arm7_perfil)
			arm7_verif_memcmp++;

		if (memcmp(b->palabras, sound_mem + (dir & (AICA_ONDA_SIZE - 1)),
		           (size_t) b->n * 4) != 0)
			arm7_blq_descubrir(b, dir);
	}
	else if (arm7_perfil)
		arm7_verif_elididas++;

	if (b->n == 0)
	{
		if (arm7_perfil)
		{
			arm7_blq_rechazo[3]++;
			arm7_blq_neg[b - arm7_blqs]++;
		}
		return 0;
	}

	/* Verificado -- o recien descubierto, que copia las palabras de la propia
	   memoria --: el sello guarda las generaciones vigentes de sus paginas. */
	arm7_blq_sellar(b, dir);

	/* Teorema 2: el bloque entero tiene que caber en el saldo. */
	if ((long) b->ciclos_max > arm7.ciclos)
	{
		if (arm7_perfil)
			arm7_blq_rechazo[4]++;
		return 0;
	}

	return arm7_blq_correr(b);
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
	   sondas del arbol.

	   Bajo los bloques con cola y encadenado la memoizacion pierde, y el
	   censo de rechazos dijo por que: el 44,7 % de los pasos de CT se
	   rechazaba por "grabando" -- el memo graba el barrido de canales que la
	   muestra siguiente invalida (repone solo 7,6 %), y mientras graba los
	   bloques estan apagados. El A/B sobre un solo binario (memo-ab.ps1,
	   2026-08-20): CT -1,7 % con rangos disjuntos, SR2 no distingue, DOOM no
	   elide nada. Apagada por omision cuando esos bloques corren;
	   DCEMU_MEMO_ARM=1 la fuerza (el brazo de vuelta del A/B) y
	   DCEMU_SIN_MEMO_ARM=1 la apaga tambien sin bloques. */
	e = getenv("DCEMU_SIN_MEMO_ARM");
	arm7_memo_apagada = (e != NULL && atoi(e) != 0);

	if (arm7_bloques && arm7_blq_rama && !arm7_memo_apagada)
	{
		e = getenv("DCEMU_MEMO_ARM");
		arm7_memo_apagada = !(e != NULL && atoi(e) != 0);
	}
}

int arm7_paso(void)
{
	DWORD op;
	DWORD dir;

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

	/*
		La busqueda, separada de arm7_leer() a proposito: el ARM ejecuta desde
		la misma RAM de onda que sondea, y si el censo de paginas contara las
		busquedas taparia justo lo que separa -- que paginas se leen como
		**dato**. Vive aqui y no en una funcion porque la direccion resuelta
		tambien es el indice de la tabla de predecodificacion.
	*/
	dir = arm7.r[15] & ARM7_BUS;
	op  = (dir & 0x00800000)
	    ? aica_arm_leer(dir & (AICA_REG_SIZE - 1), 4)
	    : onda_leer32(dir & (AICA_ONDA_SIZE - 1));

	arm7.instrucciones++;

	if (arm7_perfil)
	{
		arm7_perfil_pc[(dir >> 2) % ARM7_PERFIL_PCS]++;
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
		/*
			El censo de filas que la suite lee por arm7_fila_usada(). Es un
			instrumento de tests/ y estaba escribiendo en cada instruccion del
			ARM en una corrida normal -- dos cargas y un almacenamiento por
			paso, por un dato que en produccion nadie mira. Misma regla que el
			resto de los instrumentos: apagado, cuesta una comparacion.
		*/
		if (arm7_cobertura)
		{
			int f = arm7_opfila[ARM7_INDICE(op)];

			if (f >= 0)
				arm7_usada[f] = 1;
		}

		if (arm7_predeco && !(dir & 0x00800000))
		{
			arm7_deco * e = &arm7_deco_tabla[(dir & (AICA_ONDA_SIZE - 1)) >> 2];

			/* La validez es esta comparacion y nada mas: si la memoria ya no
			   tiene la palabra de la que se decodifico, se decodifica de
			   nuevo. Cubre el codigo automodificado, el DMA y a la suite. */
			if (e->palabra != op)
				arm7_decodificar(e, op);

			e->fn(e);
		}
		else
			arm7_oplist[ARM7_INDICE(op)](op);
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

			if (arm7_bloques)
			{
				int c = arm7_blq_intentar();

				if (c > 0)
				{
					arm7.ciclos    -= c;
					perf_arm_pasos += (unsigned long long) arm7_blq_ult_pasos;
					/* Los bloques no cuentan ociosos por diseno: un bloque
					   que vuelve a su cabecera (la cola de salto) esta
					   sondeando, no esperando. */
					continue;
				}
			}

			arm7.ciclos -= arm7_paso();

			perf_arm_pasos++;

			if (arm7.r[15] == antes)
				perf_arm_ocioso++;
		}

		return;
	}

	if (arm7_bloques)
	{
		while (arm7.ciclos > 0)
		{
			int c = arm7_blq_intentar();

			arm7.ciclos -= (c > 0) ? c : arm7_paso();
		}

		return;
	}

	while (arm7.ciclos > 0)
		arm7.ciclos -= arm7_paso();
}
