/****************************************************************************

	BLOQUES - el cache de bloques predecodificados. Ver bloques.h por el que y
	el por que; aca esta el como.

	Sin SDL a proposito, como perf.c y aica.c.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloques.h"
#include "perf.h"

int   bloques_sonda    = 0;
DWORD bloques_esperado = 0;

const struct bloque_e * bloques_cursor     = NULL;
const struct bloque_e * bloques_cursor_fin = NULL;

/*
	El arena de entradas y la tabla de bloques.

	Las dos son de tamano fijo y cuando una se llena **se vacia todo**. Es lo
	correcto para una sonda y probablemente tambien para la version de verdad:
	con menos de 1300 bloques cubriendo el 99 % de las instrucciones
	(docs/interprete-plan.md), un arena de un millon de entradas no se llena
	nunca en los bancos de este arbol, y si se llenara, un vaciado cada tanto es
	mas barato que un desalojo con politica.
*/
#define ARENA_TAM		(1UL << 20)		/* 1 M de entradas, 16 MB */
#define TABLA_BITS		16
#define TABLA_TAM		(1UL << TABLA_BITS)
#define TABLA_MASC		(TABLA_TAM - 1)
#define SONDEOS			8

/* Un bloque grabado: donde empieza en el arena y cuantas entradas tiene. */
struct bloque
{
	DWORD			pc;
	const void *	variante;	/* que tabla de despacho: ver bloques.h */
	unsigned long	e0;
	unsigned long	n;
};

static struct bloque_e * arena  = NULL;
static struct bloque *   tabla  = NULL;
static unsigned long     arena_usado = 0;

/* La grabacion en curso. `bloques_grabando` es global porque BLOQUES_CORTAR()
   la apaga desde el bucle sin pagar una llamada. */
static DWORD         grab_pc  = 0;
static const void *  grab_var = NULL;
static unsigned long grab_n   = 0;
int                  bloques_grabando = 0;

unsigned long long bloques_altas      = 0;
unsigned long long bloques_vaciados   = 0;
unsigned long long bloques_desbordes  = 0;

/* Knuth multiplicativo sobre el PC en unidades de instruccion: los bloques
   estan alineados a 2 y usar el bit 0 desperdiciaria media tabla. */
static unsigned long hash(DWORD pc)
{
	unsigned int h = (unsigned int) (pc >> 1) * 2654435761u;

	return (unsigned long) (h >> (32 - TABLA_BITS));
}

static void vaciar(void)
{
	memset(tabla, 0, TABLA_TAM * sizeof(struct bloque));
	arena_usado = 0;
	bloques_grabando = 0;
	bloques_vaciados++;
}

void bloques_iniciar(void)
{
	const char * v = getenv("DCEMU_SONDA_BLOQUES");

	bloques_sonda = (v != NULL && atoi(v) != 0);

	if (!bloques_sonda)
		return;

	arena = (struct bloque_e *) malloc(ARENA_TAM * sizeof(struct bloque_e));
	tabla = (struct bloque *)   malloc(TABLA_TAM * sizeof(struct bloque));

	if (arena == NULL || tabla == NULL)
	{
		fprintf(stderr, "bloques: sin memoria, la sonda queda apagada\n");
		free(arena); free(tabla);
		arena = NULL; tabla = NULL;
		bloques_sonda = 0;
		return;
	}

	vaciar();
	bloques_vaciados = 0;

	fprintf(stderr, "sonda: bloques predecodificados encendidos\n");
}

/*
	Un bloque vive si tiene entradas. `n` en cero es la ranura vacia, y por eso
	un bloque nunca se guarda con cero.
*/
const struct bloque_e * bloques_buscar(DWORD pc, const void * variante,
	const struct bloque_e ** fin)
{
	unsigned long k = hash(pc), i;

	for (i = 0; i < SONDEOS; i++)
	{
		const struct bloque * b = &tabla[(k + i) & TABLA_MASC];

		if (b->n == 0)
			return NULL;

		if (b->pc == pc && b->variante == variante)
		{
			const struct bloque_e * e = &arena[b->e0];

			*fin = e + b->n;
			return e;
		}
	}

	return NULL;
}

/*
	Devuelve al arena lo que la grabacion abandonada habia escrito. Sin esto,
	cada corte filtra sus entradas: la primera version lo hacia y el arena
	crecia a 960 entradas por bloque grabado, cuando el bloque real de DCDoom
	tiene doce.
*/
void bloques_cortar(void)
{
	bloques_cursor = NULL;

	if (bloques_grabando)
	{
		arena_usado -= grab_n;
		bloques_grabando = 0;
	}
}

void bloques_cancelar(void)
{
	bloques_cortar();
}

void bloques_anotar(DWORD pc, const void * variante, WORD instr, opcode_f * f)
{
	if (!bloques_grabando)
	{
		/* Arranca una grabacion nueva en este PC. */
		if (arena_usado + 1 >= ARENA_TAM)
			vaciar();

		grab_pc  = pc;
		grab_var = variante;
		grab_n   = 0;
		bloques_grabando = 1;
	}

	if (arena_usado >= ARENA_TAM)
	{
		/* No cabe: se abandona esta grabacion, no se vacia a mitad de bloque
		   --el bucle tiene punteros vivos al arena solo cuando reproduce, pero
		   vaciar aca dejaria la grabacion apuntando a cualquier lado--. */
		bloques_grabando = 0;
		bloques_desbordes++;
		return;
	}

	arena[arena_usado].f     = f;
	arena[arena_usado].instr = instr;
	arena_usado++;
	grab_n++;
}

/*
	Cierra el bloque en curso y lo guarda. Lo llama el bucle cuando descubre
	que el PC dejo de ser el siguiente -- que es lo mismo que decir que el
	bloque termino.
*/
void bloques_cerrar(void)
{
	unsigned long k, i;

	if (!bloques_grabando || grab_n == 0)
	{
		bloques_grabando = 0;
		return;
	}

	/* Se guarda en la primera ranura libre de su sondeo; si no hay, el bloque
	   se pierde y se volvera a grabar la proxima vez. */
	bloques_grabando = 0;

	k = hash(grab_pc);

	for (i = 0; i < SONDEOS; i++)
	{
		struct bloque * b = &tabla[(k + i) & TABLA_MASC];

		if (b->n != 0 && (b->pc != grab_pc || b->variante != grab_var))
			continue;

		b->pc = grab_pc;
		b->variante = grab_var;
		b->e0 = arena_usado - grab_n;
		b->n  = grab_n;

		bloques_altas++;
		return;
	}

	bloques_desbordes++;
}

void bloques_resumen(void)
{
	if (!bloques_sonda)
		return;

	fprintf(stderr, "perf: bloques: %llu grabados, %llu entradas usadas de %lu,"
		" %llu vaciados, %llu perdidos\n",
		bloques_altas, (unsigned long long) arena_usado, ARENA_TAM,
		bloques_vaciados, bloques_desbordes);
}
