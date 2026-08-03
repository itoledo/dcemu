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

DWORD arm7_leer(DWORD direccion, int tam)
{
	direccion &= ARM7_BUS;

	if (direccion & 0x00800000)
		return aica_arm_leer(direccion & (AICA_REG_SIZE - 1), tam);

	{
		DWORD a = direccion & (AICA_ONDA_SIZE - 1);

		switch (tam)
		{
		case 1:		return sound_mem[a];
		case 2:		return (DWORD) (sound_mem[a & ~1u]
					              | (sound_mem[(a & ~1u) + 1] << 8));
		default:
			a &= ~3u;
			return (DWORD) (sound_mem[a]
			              | (sound_mem[a + 1] << 8)
			              | (sound_mem[a + 2] << 16)
			              | ((DWORD) sound_mem[a + 3] << 24));
		}
	}
}

void arm7_escribir(DWORD direccion, int tam, DWORD valor)
{
	direccion &= ARM7_BUS;

	if (direccion & 0x00800000)
	{
		aica_arm_escribir(direccion & (AICA_REG_SIZE - 1), tam, valor);
		return;
	}

	{
		DWORD a = direccion & (AICA_ONDA_SIZE - 1);

		switch (tam)
		{
		case 1:
			sound_mem[a] = (unsigned char) valor;
			break;

		case 2:
			a &= ~1u;
			sound_mem[a]     = (unsigned char) valor;
			sound_mem[a + 1] = (unsigned char) (valor >> 8);
			break;

		default:
			a &= ~3u;
			sound_mem[a]     = (unsigned char) valor;
			sound_mem[a + 1] = (unsigned char) (valor >> 8);
			sound_mem[a + 2] = (unsigned char) (valor >> 16);
			sound_mem[a + 3] = (unsigned char) (valor >> 24);
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

/* B y BL. */
static void op_salto(DWORD op)
{
	long desp = (long) (op & 0x00FFFFFF);

	if (desp & 0x00800000)
		desp |= ~0x00FFFFFFL;					/* signo */

	if (op & 0x01000000)						/* BL: guarda el retorno */
		arm7.r[14] = arm7.r[15] + 4;

	arm7.r[15] = (DWORD) (arm7.r[15] + 8 + (desp << 2)) & ARM7_BUS;
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
/* Ejecucion                                                                */
/* ------------------------------------------------------------------------ */

void arm7_reset(void)
{
	memset(&arm7, 0, sizeof(arm7));

	arm7.banco = ARM7_B_SVC;
	arm7.cpsr  = ARM7_MODO_SVC | ARM7_I | ARM7_F;
	arm7.r[15] = ARM7_VEC_RESET;
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

	op = arm7_leer(arm7.r[15], 4);
	arm7.instrucciones++;

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
		arm7.r[15] += 4;

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

	while (arm7.ciclos > 0)
	{
		if (perf_activa)
		{
			/* Un salto a si mismo deja el PC donde estaba: es la forma que
			   tiene el ARM de esperar, y la que spu_init() de KOS deja puesta
			   en la direccion 0. Si domina, lo que hay que hacer no es mover
			   el ARM de hilo sino no ejecutarlo. */
			DWORD antes = arm7.r[15];

			arm7.ciclos -= arm7_paso();

			perf_arm_pasos++;

			if (arm7.r[15] == antes)
				perf_arm_ocioso++;

			continue;
		}

		arm7.ciclos -= arm7_paso();
	}
}
