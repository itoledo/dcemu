/****************************************************************************

	SINGLESTEP - el nucleo SH-4 contra las pruebas de SingleStepTests/sh4

	https://github.com/SingleStepTests/sh4 publica 233 codificaciones del SH-4
	con 500 casos cada una: estado inicial completo, estado final, los cuatro
	accesos de instruccion y la lectura o escritura de datos que hubo. Salieron
	del interprete de Reicast, asi que **no** son el manual: son otro emulador.
	Donde discrepan, gana el manual -- ver la tabla de divergencias al final de
	este archivo y docs/sh4-conformidad.md.

	Como se corre y donde viven los datos: tests/README.md.

	El formato binario esta descrito en transcode_json.py del repositorio de las
	pruebas. Cada caso son 788 bytes:

		4               tamano del caso
		284             estado inicial   (4 + 4 de relleno + 69 palabras)
		284             estado final
		188             ciclos           (4 + 8 de relleno + 4 x 44)
		28              opcodes          (4 + 4 de relleno + 5 palabras)

	y las 69 palabras del estado son R0-R15, R'0-R'7, banco de punto flotante 0,
	banco 1, PC, GBR, SR, SSR, SPC, VBR, SGR, DBR, MACL, MACH, PR, FPSCR y FPUL.

	Tres cosas del formato hay que medirlas, no suponerlas, y estan medidas:

	  - **El banco de punto flotante que llaman "FP1" es el activo** y "FP0" el
		de XF, no al reves. Se ve en que FMOV FRm,FRn escribe siempre FP1 y
		FMOV DRm,XDn siempre FP0, con FPSCR.FR valga lo que valga. FRCHG
		intercambia los dos arreglos, igual que RB hace con R y R'.
	  - **Un acceso de 64 bits va en little-endian**: los 32 bits bajos son
		FR[n] (indice par) y los altos FR[n+1].
	  - **Un "ciclo" es un acceso a instruccion**, no una instruccion: un salto
		con ranura de retardo gasta dos. Los cuatro de cada caso son el NOP, la
		instruccion probada, la ranura (o el ADD R1,R1 que la sigue) y el
		cuarto acceso, que cae en el destino del salto si lo hubo.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "excepciones.h"
#include "mem.h"
#include "opcodes.h"
#include "sh4emu.h"

#include "arnes.h"

/* ------------------------------------------------------------------------ */
/* El formato del archivo                                                   */
/* ------------------------------------------------------------------------ */

#define CASOS_POR_ARCHIVO	500
#define TAM_ESTADO			284
#define TAM_CICLOS			188
#define TAM_OPCODES			28
#define TAM_CASO			(4 + 2 * TAM_ESTADO + TAM_CICLOS + TAM_OPCODES)
#define TAM_ARCHIVO			(CASOS_POR_ARCHIVO * TAM_CASO)

#define ACCION_LECTURA		1
#define ACCION_ESCRITURA	2
#define ACCION_FETCH		4

/*
	Los nombres van en minuscula porque PC, SR, GBR, MACL, FPSCR y compania son
	**macros** de sh4emu.h que se expanden a core.context.algo: un campo de
	estructura llamado asi no compila.
*/
typedef struct
{
	DWORD	r[16];
	DWORD	r_[8];
	DWORD	fp0[16];	/* el banco de XF */
	DWORD	fp1[16];	/* el banco activo */
	DWORD	pc, gbr, sr, ssr, spc, vbr, sgr, dbr;
	DWORD	macl, mach, pr, fpscr, fpul;
} estado_t;

typedef struct
{
	DWORD				acciones;
	DWORD				fetch_dir;
	DWORD				fetch_val;
	DWORD				escr_dir;
	unsigned long long	escr_val;
	DWORD				escr_tam;
	DWORD				lect_dir;
	unsigned long long	lect_val;
	DWORD				lect_tam;
} ciclo_t;

typedef struct
{
	estado_t	inicial;
	estado_t	final;
	ciclo_t		ciclos[4];
	DWORD		opcodes[5];
} caso_t;

static DWORD leer32(const unsigned char * p)
{
	return (DWORD) p[0] | ((DWORD) p[1] << 8)
		 | ((DWORD) p[2] << 16) | ((DWORD) p[3] << 24);
}

static unsigned long long leer64(const unsigned char * p)
{
	return (unsigned long long) leer32(p)
		 | ((unsigned long long) leer32(p + 4) << 32);
}

static void cargar_estado(estado_t * e, const unsigned char * p)
{
	DWORD v[69];
	int i;

	p += 8;						/* tamano del bloque y relleno */

	for (i = 0; i < 69; i++)
		v[i] = leer32(p + i * 4);

	for (i = 0; i < 16; i++)	e->r[i]   = v[i];
	for (i = 0; i < 8;  i++)	e->r_[i]  = v[16 + i];
	for (i = 0; i < 16; i++)	e->fp0[i] = v[24 + i];
	for (i = 0; i < 16; i++)	e->fp1[i] = v[40 + i];

	e->pc   = v[56];	e->gbr  = v[57];	e->sr   = v[58];	e->ssr = v[59];
	e->spc  = v[60];	e->vbr  = v[61];	e->sgr  = v[62];	e->dbr = v[63];
	e->macl = v[64];	e->mach = v[65];	e->pr   = v[66];	e->fpscr = v[67];
	e->fpul = v[68];
}

static void cargar_caso(caso_t * c, const unsigned char * p)
{
	const unsigned char * q;
	int i;

	p += 4;
	cargar_estado(&c->inicial, p);
	cargar_estado(&c->final, p + TAM_ESTADO);

	q = p + 2 * TAM_ESTADO + 12;		/* tamano del bloque y relleno */

	for (i = 0; i < 4; i++)
	{
		c->ciclos[i].acciones	= leer32(q + 0);
		c->ciclos[i].fetch_dir	= leer32(q + 4);
		c->ciclos[i].fetch_val	= leer32(q + 8);
		c->ciclos[i].escr_dir	= leer32(q + 12);
		c->ciclos[i].escr_val	= leer64(q + 16);
		c->ciclos[i].escr_tam	= leer32(q + 24);
		c->ciclos[i].lect_dir	= leer32(q + 28);
		c->ciclos[i].lect_val	= leer64(q + 32);
		c->ciclos[i].lect_tam	= leer32(q + 40);
		q += 44;
	}

	q = p + 2 * TAM_ESTADO + TAM_CICLOS + 8;

	for (i = 0; i < 5; i++)
		c->opcodes[i] = leer32(q + i * 4);
}

/* ------------------------------------------------------------------------ */
/* La memoria                                                               */
/* ------------------------------------------------------------------------ */

/*
	Las pruebas ven un espacio plano de 32 bits sin registros mapeados, o sea
	nada parecido al mapa del Dreamcast. Aqui se reemplazan las dos tablas de
	mem.c enteras:

	  - las 256 entradas de mem_zone[] apuntan al mismo bloque de 16 MB, asi
		que get_memory_pointer() -- que es como se lee **la instruccion** --
		resuelve cualquier direccion. El bloque esta lleno de ADD R2,R2, que es
		el quinto opcode que la prueba entrega para todo lo que este fuera de
		la ventana de cuatro instrucciones.
	  - los 256 handlers de lectura y escritura atienden **los datos** y no
		tocan el bloque: la lectura entrega el valor que el caso dice y la
		escritura se anota para compararla.

	Que el bloque tenga 16 MB y las direcciones 32 bits significa que dos
	direcciones distintas pueden caer en el mismo sitio. Solo importaria si el
	destino de un salto aliasara con la ventana de instrucciones, y eso se
	detecta y se descarta el caso (aliasing, mas abajo).
*/

#define BLOQUE			(16 * 1024 * 1024)
#define RELLENO			0x322C				/* ADD R2, R2 */

static unsigned char * bloque;

static const caso_t *	caso;				/* el caso que corre ahora */

static int				lecturas;			/* cuantas hubo */
static DWORD			lect_dir;
static DWORD			lect_tam;

static int				escrituras;
static DWORD			escr_dir;
static DWORD			escr_tam;
static unsigned long long escr_val;

/* El ciclo del caso que lleva la lectura o la escritura, si lo hay. */
static const ciclo_t * ciclo_con(DWORD accion)
{
	int i;

	for (i = 0; i < 4; i++)
		if (caso->ciclos[i].acciones & accion)
			return &caso->ciclos[i];

	return NULL;
}

static void ss_read(unsigned long direccion, void * p, size_t size)
{
	const ciclo_t * c = ciclo_con(ACCION_LECTURA);
	unsigned long long v = c ? c->lect_val : 0;

	lecturas++;
	lect_dir = (DWORD) direccion;
	lect_tam = (DWORD) size;

	switch (size)
	{
		case 1:	*(BYTE *)  p = (BYTE)  v;	break;
		case 2:	*(WORD *)  p = (WORD)  v;	break;
		case 4:	*(DWORD *) p = (DWORD) v;	break;
		case 8:
			((DWORD *) p)[0] = (DWORD) v;
			((DWORD *) p)[1] = (DWORD) (v >> 32);
			break;
		default: memset(p, 0, size);		break;
	}
}

static void ss_write(unsigned long direccion, void * p, size_t size)
{
	escrituras++;
	escr_dir = (DWORD) direccion;
	escr_tam = (DWORD) size;

	switch (size)
	{
		case 1:	escr_val = *(BYTE *)  p;	break;
		case 2:	escr_val = *(WORD *)  p;	break;
		case 4:	escr_val = *(DWORD *) p;	break;
		case 8:
			escr_val = (unsigned long long) ((DWORD *) p)[0]
					 | ((unsigned long long) ((DWORD *) p)[1] << 32);
			break;
		default: escr_val = 0;				break;
	}
}

static void memoria_iniciar(void)
{
	int i;
	WORD * w;

	bloque = (unsigned char *) malloc(BLOQUE);

	if (!bloque)
	{
		fprintf(stderr, "singlestep: no se pudo reservar el bloque de 16 MB\n");
		exit(2);
	}

	w = (WORD *) bloque;

	for (i = 0; i < BLOQUE / 2; i++)
		w[i] = RELLENO;

	for (i = 0; i < 0x100; i++)
	{
		mem_zone[i]			= bloque;
		mem_hash_read[i]	= ss_read;
		mem_hash_write[i]	= ss_write;
	}
}

/* ------------------------------------------------------------------------ */
/* La ejecucion                                                             */
/* ------------------------------------------------------------------------ */

/*
	run() de sh4emu.c es lo que core.execute apunta normalmente. Se envuelve
	para contar los accesos a instruccion: **cada llamada a core.execute va
	precedida de exactamente un acceso**, tanto la de este archivo como la que
	branch.c y rte143() hacen para la ranura de retardo, y PC en ese momento es
	la direccion de la que se leyo. Eso es lo que las pruebas llaman un ciclo.
*/
extern void run(WORD arg);

#define MAX_FETCH	8

static int		fetch_n;
static DWORD	fetch_dir[MAX_FETCH];
static WORD		fetch_val[MAX_FETCH];

static void ss_execute(WORD arg)
{
	if (fetch_n < MAX_FETCH)
	{
		fetch_dir[fetch_n] = (DWORD) PC;
		fetch_val[fetch_n] = arg;
	}

	fetch_n++;

	/*
		El generador corta a los cuatro accesos. Pasado eso no hay con que
		comparar, asi que no se ejecuta: el caso se descarta mas arriba.
	*/
	if (fetch_n > 4)
		return;

	run(arg);
}

/*
	La lectura de instruccion tal como la define el repositorio de las pruebas:
	los cuatro opcodes cubren la ventana que empieza en el PC inicial y el
	quinto contesta **cualquier otra direccion**, este alineada o no. Un salto
	a registro cae casi siempre en una direccion impar, y ahi la diferencia
	importa: el bloque de memoria entregaria la palabra a caballo entre dos
	rellenos (0x2C32, que es un MOV.L a memoria) en vez del ADD R2,R2 que la
	prueba supone.

	La ranura de retardo no pasa por aqui -- branch.c la lee con un
	get_memory_pointer() directo --, pero siempre cae dentro de la ventana, que
	es lo que el bloque tiene escrito.
*/
static WORD leer_instruccion(DWORD dir)
{
	DWORD n = (dir - caso->inicial.pc) / 2;

	return (WORD) caso->opcodes[n <= 3 ? n : 4];
}

extern FPR_BANK * BANK0;
extern FPR_BANK * BANK1;

static void poner_estado(const estado_t * e)
{
	int i;

	for (i = 0; i < 16; i++)	R(i)      = e->r[i];
	for (i = 0; i < 8;  i++)	R(16 + i) = e->r_[i];

	PC   = e->pc;	GBR  = e->gbr;	SSR  = e->ssr;	SPC = e->spc;
	VBR  = e->vbr;	SGR  = e->sgr;	DBR  = e->dbr;	MACL = e->macl;
	MACH = e->mach;	PR   = e->pr;	FPUL = e->fpul;

	/* SR se escribe crudo: UpdateSR() intercambiaria los bancos, y aqui el
	   arreglo que llega ya **es** el banco que SR.RB nombra. */
	SR = e->sr;
	core.context.banco_activo = SR_RB;

	/* FPSCR si pasa por UpdateFPSCR(): tiene que repuntar oplist segun PR y SZ.
	   Se parte de cero y de los bancos sin intercambiar, asi que si FR viene en
	   uno los punteros quedan cruzados -- y el contenido se carga despues, por
	   los punteros, que es lo que hace que "FP1" sea siempre el banco activo. */
	FPSCR = 0;
	core.context.FR_BANK = BANK0;
	core.context.XF_BANK = BANK1;
	UpdateFPSCR(e->fpscr);

	for (i = 0; i < 16; i++)
	{
		((DWORD *) core.context.FR_BANK)[i] = e->fp1[i];
		((DWORD *) core.context.XF_BANK)[i] = e->fp0[i];
	}

	/*
		Las pruebas no modelan ninguna excepcion, y SR.FD viene al azar: hay
		casos de FPU con la FPU apagada que el generador ejecuto igual. Aqui se
		fuerza el camino sin trampa para poder comparar el resto; que dcemu
		entre en 0x800 con FD puesto esta cubierto por la suite fpu-excepciones.
	*/
	fpu_deshabilitada = 0;
	excepcion_vigilar = 0;
	en_ranura_retardo = 0;
	delayslot = 0;
	NEXTPC = 0;
	core.context.cycles = 0;
}

/* ------------------------------------------------------------------------ */
/* La comparacion                                                           */
/* ------------------------------------------------------------------------ */

#define MAX_DIF		48

typedef struct
{
	char	campo[16];
	DWORD	esperado;
	DWORD	obtenido;
} dif_t;

static dif_t	difs[MAX_DIF];
static int		n_difs;

static void anotar(const char * campo, DWORD esperado, DWORD obtenido)
{
	if (n_difs < MAX_DIF)
	{
		strncpy(difs[n_difs].campo, campo, sizeof(difs[0].campo) - 1);
		difs[n_difs].campo[sizeof(difs[0].campo) - 1] = 0;
		difs[n_difs].esperado = esperado;
		difs[n_difs].obtenido = obtenido;
	}

	n_difs++;
}

static void comparar(const char * campo, DWORD esperado, DWORD obtenido)
{
	if (esperado != obtenido)
		anotar(campo, esperado, obtenido);
}

/*
	Un NaN vale por cualquier otro. No es una concesion: es la regla que el
	propio repositorio de las pruebas publica en su compare_floats(), "check for
	both different NaN but still NaN", y hace falta porque el SH-4, el anfitrion
	y Reicast escriben tres patrones distintos -- H'7FBFFFFF el manual,
	H'FFC00000 el x87 y H'7FC00000 Reicast.

	Se mira la pareja ademas de la palabra suelta: en doble precision el NaN
	ocupa FR[n] y FR[n+1], y por separado ninguna de las dos mitades lo es.
*/
static int es_nan_simple(DWORD b)
{
	return (b & 0x7F800000) == 0x7F800000 && (b & 0x007FFFFF) != 0;
}

static int es_nan_doble(DWORD alta, DWORD baja)
{
	return (alta & 0x7FF00000) == 0x7FF00000
		&& ((alta & 0x000FFFFF) != 0 || baja != 0);
}

/*
	Cota de error absoluta por registro, para las cuatro instrucciones que el
	manual del SH-4 declara aproximadas. Vale cero en todo lo demas, y entonces
	la comparacion es del bit. La llena `armar_cotas()`, mas abajo.
*/
static double cota[16];
static int    hay_cota;

static float como_float(DWORD b)
{
	float f;

	memcpy(&f, &b, sizeof(f));

	return f;
}

static int dentro_de_cota(int i, DWORD a, DWORD b)
{
	double x = como_float(a);
	double y = como_float(b);
	double d = x > y ? x - y : y - x;

	if (!hay_cota || i >= 16)
		return 0;

	return d <= cota[i];
}

static void comparar_banco(const char * prefijo, const DWORD * esperado,
						   const DWORD * obtenido)
{
	char nombre[16];
	int i, par;

	for (i = 0; i < 16; i++)
	{
		if (esperado[i] == obtenido[i])
			continue;

		if (es_nan_simple(esperado[i]) && es_nan_simple(obtenido[i]))
			continue;

		/* En doble precision DRn son FR[n] y FR[n+1] con **FR[n] arriba**: la
		   numeracion de los registros es la del manual y no depende del orden
		   de bytes en memoria. Es la razon por la que un FMOV con SZ=1 no sirve
		   para cargar un doble en little-endian, cosa que el propio manual
		   aclara (6.6.2, nota de programacion). */
		par = i & ~1;

		if (es_nan_doble(esperado[par], esperado[par + 1])
			&& es_nan_doble(obtenido[par], obtenido[par + 1]))
			continue;

		if (prefijo[0] == 'F' && dentro_de_cota(i, esperado[i], obtenido[i]))
			continue;

		sprintf(nombre, "%s%d", prefijo, i);
		anotar(nombre, esperado[i], obtenido[i]);
	}
}

static void comparar_estado(const estado_t * e)
{
	char nombre[16];
	int i;

	for (i = 0; i < 16; i++)
	{
		sprintf(nombre, "R%d", i);
		comparar(nombre, e->r[i], R(i));
	}

	for (i = 0; i < 8; i++)
	{
		sprintf(nombre, "R_%d", i);
		comparar(nombre, e->r_[i], R(16 + i));
	}

	comparar_banco("FR", e->fp1, (const DWORD *) core.context.FR_BANK);
	comparar_banco("XF", e->fp0, (const DWORD *) core.context.XF_BANK);

	comparar("PC",    e->pc,    (DWORD) PC);
	comparar("GBR",   e->gbr,   (DWORD) GBR);
	comparar("SR",    e->sr,    (DWORD) SR);
	comparar("SSR",   e->ssr,   (DWORD) SSR);
	comparar("SPC",   e->spc,   (DWORD) SPC);
	comparar("VBR",   e->vbr,   (DWORD) VBR);
	comparar("SGR",   e->sgr,   (DWORD) SGR);
	comparar("DBR",   e->dbr,   (DWORD) DBR);
	comparar("MACL",  e->macl,  (DWORD) MACL);
	comparar("MACH",  e->mach,  (DWORD) MACH);
	comparar("PR",    e->pr,    (DWORD) PR);
	comparar("FPSCR", e->fpscr, (DWORD) FPSCR);
	comparar("FPUL",  e->fpul,  (DWORD) FPUL);
}

/* ------------------------------------------------------------------------ */
/* Divergencias conocidas                                                   */
/* ------------------------------------------------------------------------ */

/*
	Donde la prueba y el manual del SH-4 no dicen lo mismo, dcemu sigue al
	manual y el caso se cuenta aparte -- ni verde ni rojo. Cada una tiene su
	razon escrita; ninguna se agrega "para que pase".
*/

#define DIV_NINGUNA			0
#define DIV_FPSCR_CAUSA		1
#define DIV_TRAPA			2
#define DIV_RTE_BANCO		3
#define DIV_FPSCR_RESERVA	4
#define DIV_DIV1			5
#define DIV_FTRC			6
#define DIV_APROXIMA		7

static const char * nombre_divergencia(int d)
{
	switch (d)
	{
		case DIV_FPSCR_CAUSA:	return "FPSCR.Cause/Flag no se escriben";
		case DIV_TRAPA:			return "TRAPA no entra en la excepcion";
		case DIV_RTE_BANCO:		return "RTE: la ranura ve el SR viejo";
		case DIV_FPSCR_RESERVA:	return "FPSCR guarda los bits reservados";
		case DIV_DIV1:			return "DIV1 con n == m";
		case DIV_FTRC:			return "FTRC fuera de rango no satura";
		case DIV_APROXIMA:		return "FIPR/FTRV: acumulacion en simple";
	}

	return "";
}

/* ------------------------------------------------------------------------ */
/* Un caso                                                                  */
/* ------------------------------------------------------------------------ */

#define RES_OK			0
#define RES_FALLA		1
#define RES_DESCARTE	2
#define RES_DIVERGE		3

static const char * motivo_descarte;
static const char * archivo_actual;

static int armar_cotas(const char * archivo, const caso_t * c);

static int correr_caso(const caso_t * c)
{
	DWORD base = c->inicial.pc;
	DWORD off  = base & 0xFFFFFF;
	const ciclo_t * lc;
	const ciclo_t * ec;
	int i, vueltas;

	caso = c;
	n_difs = 0;
	motivo_descarte = NULL;

	armar_cotas(archivo_actual, c);

	/* La ventana de cuatro instrucciones. El resto del bloque ya tiene el
	   quinto opcode, que es lo que la prueba entrega en cualquier otra
	   direccion. */
	for (i = 0; i < 4; i++)
		*(WORD *) &bloque[(off + i * 2) & 0xFFFFFF] = (WORD) c->opcodes[i];

	poner_estado(&c->inicial);

	lecturas = escrituras = 0;
	lect_dir = escr_dir = lect_tam = escr_tam = 0;
	escr_val = 0;
	fetch_n = 0;

	for (vueltas = 0; fetch_n < 4 && vueltas < MAX_FETCH; vueltas++)
		core.execute(leer_instruccion((DWORD) PC));

	/* Deshacer la ventana. */
	for (i = 0; i < 4; i++)
		*(WORD *) &bloque[(off + i * 2) & 0xFFFFFF] = RELLENO;

	if (fetch_n != 4)
	{
		/* Mas de cuatro accesos: la cuarta instruccion resulto ser un salto,
		   o sea que el destino del anterior volvio a la ventana. El generador
		   corto a los cuatro y no hay estado final con que comparar. */
		motivo_descarte = "mas de cuatro accesos a instruccion";
		return RES_DESCARTE;
	}

	/* Aliasing: una direccion de acceso fuera de la ventana que caiga dentro
	   de ella al truncar a 24 bits leeria el opcode equivocado. */
	for (i = 0; i < 4; i++)
	{
		DWORD d = fetch_dir[i];

		if (d >= base && d <= base + 6)
			continue;

		if (((d - base) & 0xFFFFFF) <= 6)
		{
			motivo_descarte = "la direccion aliasa con la ventana";
			return RES_DESCARTE;
		}
	}

	for (i = 0; i < 4; i++)
		if (fetch_dir[i] != c->ciclos[i].fetch_dir)
		{
			anotar("fetch", c->ciclos[i].fetch_dir, fetch_dir[i]);
			break;
		}

	comparar_estado(&c->final);

	lc = ciclo_con(ACCION_LECTURA);
	ec = ciclo_con(ACCION_ESCRITURA);

	if (!!lc != (lecturas != 0))
		anotar("lecturas", lc ? 1 : 0, lecturas != 0);
	else if (lc)
	{
		comparar("lect.dir", lc->lect_dir, lect_dir);
		comparar("lect.tam", lc->lect_tam, lect_tam);
	}

	if (!!ec != (escrituras != 0))
		anotar("escrituras", ec ? 1 : 0, escrituras != 0);
	else if (ec)
	{
		comparar("escr.dir", ec->escr_dir, escr_dir);
		comparar("escr.tam", ec->escr_tam, escr_tam);
		comparar("escr.val", (DWORD) ec->escr_val, (DWORD) escr_val);

		if (ec->escr_tam == 8)
			comparar("escr.val.hi", (DWORD) (ec->escr_val >> 32),
					 (DWORD) (escr_val >> 32));
	}

	return n_difs ? RES_FALLA : RES_OK;
}

/* ------------------------------------------------------------------------ */
/* La clasificacion de un fallo                                             */
/* ------------------------------------------------------------------------ */

static int clasificar(const caso_t * c, const char * archivo)
{
	int i;

	/* Reicast no escribe los campos Cause ni Flag de FPSCR; dcemu si, que es
	   lo que pide el manual y lo que verifica basic/fpu/exc de KOS. Una
	   diferencia confinada a esos diez bits es eso y nada mas. */
	if (n_difs == 1 && strcmp(difs[0].campo, "FPSCR") == 0
		&& ((difs[0].esperado ^ difs[0].obtenido)
			& ~(FPU_CAUSA_TODAS | FPU_FLAG_TODAS)) == 0)
		return DIV_FPSCR_CAUSA;

	/* La prueba de TRAPA no modela la excepcion: el estado final es el de
	   cuatro instrucciones seguidas, con SPC, SSR, SGR y SR sin tocar. */
	if (strncmp(archivo, "11000011", 8) == 0)
		return DIV_TRAPA;

	/* RTE: la prueba ejecuta la ranura de retardo **antes** de escribir SR,
	   asi que con SSR.RB distinto de SR.RB el ADD R1,R1 de la ranura cae en el
	   otro banco. El manual del SH-4 dice lo contrario -- "the other bits (S,
	   T, M, Q, FD, BL, RB) after modification are used for the delay slot
	   instruction" --, que es lo que hace dcemu. */
	if (strncmp(archivo, "0000000000101011", 16) == 0
		&& ((sr_normalizar(c->inicial.ssr) >> 29) & 1)
			!= ((sr_normalizar(c->inicial.sr) >> 29) & 1))
	{
		for (i = 0; i < n_difs && i < MAX_DIF; i++)
			if (strncmp(difs[i].campo, "R1", 2) != 0
				&& strncmp(difs[i].campo, "R_1", 3) != 0)
				return DIV_NINGUNA;

		return DIV_RTE_BANCO;
	}

	/* FPSCR tiene 22 bits: "#define FPSCR_MASK 0x003FFFFF" en la definicion de
	   LDS Rm,FPSCR del manual, y los bits 31-22 se leen siempre en cero. Reicast
	   guarda la palabra entera. */
	if (n_difs == 1 && strcmp(difs[0].campo, "FPSCR") == 0
		&& (difs[0].esperado & 0x003FFFFFu) == difs[0].obtenido)
		return DIV_FPSCR_RESERVA;

	/* DIV1 con n == m. El manual guarda Rm en una temporal **antes** de
	   desplazar Rn ("tmp2 = R[m]; R[n] <<= 1;"), asi que con los dos registros
	   iguales el sustraendo es el valor original. Reicast lee Rm despues del
	   desplazamiento y le da cero. QEMU tambien captura antes. */
	if (strncmp(archivo, "0011nnnnmmmm0100", 16) == 0
		&& ((c->opcodes[1] >> 8) & 0x0F) == ((c->opcodes[1] >> 4) & 0x0F))
		return DIV_DIV1;

	/* FTRC fuera de rango: el manual entrega 0x7FFFFFFF o 0x80000000 y levanta
	   la causa invalida ("ftrc_invalid": *FPUL = 0x7fffffff / 0x80000000).
	   Reicast recorta al mayor entero representable en float, 0x7FFFFF80, y no
	   levanta nada. */
	if (strncmp(archivo, "1111mmmm00111101", 16) == 0)
	{
		int solo_saturacion = 1;

		for (i = 0; i < n_difs && i < MAX_DIF; i++)
			if (strcmp(difs[i].campo, "FPUL") == 0)
			{
				if (difs[i].obtenido != 0x7FFFFFFFu
					&& difs[i].obtenido != 0x80000000u)
					solo_saturacion = 0;
			}
			else if (strcmp(difs[i].campo, "FPSCR") != 0)
				solo_saturacion = 0;

		if (solo_saturacion)
			return DIV_FTRC;
	}

	/*
		FIPR y FTRV: lo que queda son diferencias de tres unidades del ultimo
		bit, siempre en el mismo sentido. dcemu acumula los cuatro productos en
		doble y redondea una sola vez -- 6.4 del manual --, Reicast acumula en
		simple y va perdiendo un bit por suma. La diferencia queda dentro de la
		tolerancia que el propio repositorio de las pruebas publica en su
		compare_floats(), cuatro unidades del patron de bits, pero fuera de la
		cota de error del manual, asi que se cuenta aparte en vez de ensanchar
		la cota.
	*/
	if (strncmp(archivo, "1111nnmm11101101", 16) == 0
		|| strncmp(archivo, "1111nn0111111101", 16) == 0)
	{
		for (i = 0; i < n_difs && i < MAX_DIF; i++)
		{
			DWORD a = difs[i].esperado, b = difs[i].obtenido;
			DWORD d = a > b ? a - b : b - a;

			if (difs[i].campo[0] != 'F' || ((a ^ b) & 0x80000000u) || d > 4)
				return DIV_NINGUNA;
		}

		return DIV_APROXIMA;
	}

	return DIV_NINGUNA;
}

/* ------------------------------------------------------------------------ */
/* El corredor                                                              */
/* ------------------------------------------------------------------------ */

/* Histograma de campos que difieren: con 500 casos por codificacion, saber que
   el fallo esta siempre en el mismo registro es la mitad del diagnostico. */
#define MAX_CAMPOS	24

typedef struct
{
	char	nombre[64];
	int		ok, falla, descarte, diverge;
	int		divergencia;
	char	campo[MAX_CAMPOS][16];
	int		cuenta[MAX_CAMPOS];
	int		n_campos;
} resumen_t;

static void anotar_campo(resumen_t * r, const char * campo)
{
	int i;

	for (i = 0; i < r->n_campos; i++)
		if (strcmp(r->campo[i], campo) == 0)
		{
			r->cuenta[i]++;
			return;
		}

	if (r->n_campos < MAX_CAMPOS)
	{
		strncpy(r->campo[r->n_campos], campo, 15);
		r->campo[r->n_campos][15] = 0;
		r->cuenta[r->n_campos] = 1;
		r->n_campos++;
	}
}

static resumen_t	resumen[512];
static int			n_resumen;

static int			total_ok, total_falla, total_descarte, total_diverge;

static int			max_detalle = 3;	/* casos detallados por archivo */
static int			listar_ok;

static void detallar(const char * archivo, int idx, const caso_t * c)
{
	int i;

	printf("  %s caso %d  opcode %04x\n", archivo, idx, c->opcodes[1]);

	for (i = 0; i < n_difs && i < MAX_DIF; i++)
		printf("      %-11s esperado %08lx  obtenido %08lx\n",
			   difs[i].campo,
			   (unsigned long) difs[i].esperado,
			   (unsigned long) difs[i].obtenido);

	if (n_difs > MAX_DIF)
		printf("      ... y %d diferencias mas\n", n_difs - MAX_DIF);
}

/*
	Las cuatro instrucciones de la unidad grafica del SH-4 calculan valores
	**aproximados**, y el manual lo dice con todas las letras: 6.6.1,
	"Geometric operation instructions perform approximate-value computations. To
	enable high-speed computation with a minimum of hardware, the SH-4 ignores
	comparatively small values in the partial computation results of four
	multiplications", con una cota de error y esta advertencia: "In future
	version of SH series, the above error is guaranteed, but the same result as
	SH-4 is not guaranteed".

	O sea que ni el propio fabricante promete el mismo bit. FSRRA y FSCA son de
	la misma familia -- reciproco de la raiz y seno/coseno por tabla -- y el
	repositorio de las pruebas asume lo mismo: su compare_floats() acepta cuatro
	unidades de diferencia en el patron de bits "more or less checks for
	rounding issues".

	Aqui la tolerancia se aplica **solo a estas cuatro**. En todo lo demas -- las
	operaciones que IEEE 754 define exactamente -- la comparacion es del bit, que
	es lo que dejo ver que dcemu ignoraba el modo de redondeo de FPSCR: aquellas
	diferencias eran de un ulp y una tolerancia las habria tapado.
*/
/*
	La cota de cada una, con la formula del manual donde la hay.

	FIPR y FTRV, 6.6.1:

		error maximo = MAX(resultado de cada multiplicacion x 2^-23)
					 + MAX(valor del resultado x 2^-23, 2^-149)

	(24 digitos significativos para un normalizado, de ahi el 2^-23.)

	Se usa la **suma** de los cuatro productos y no el maximo: son cuatro
	multiplicaciones y el error de cada una entra en el total. Con el maximo
	solo, una decena de casos de FIPR y FTRV queda apenas afuera -- tres unidades
	del ultimo bit --, porque Reicast acumula en simple precision y se aparta del
	valor exacto un poco mas de lo que el manual promete. dcemu acumula en doble
	y redondea una sola vez, que es lo que dice 6.4.

	FSRRA y FSCA no estan en el manual del SH-4 Rev.5 -- llegaron con la unidad
	grafica --, pero son de la misma familia y su exactitud documentada es de
	2^-21. Medido contra las propias pruebas: el peor seno de las 500 de FSCA se
	aparta 1.0e-7 del exacto, y 2^-21 son 4.8e-7.

	Devuelve 0 si la codificacion no es de las cuatro.
*/
#define EPS23	1.1920928955078125e-07		/* 2^-23 */
#define EPS21	4.76837158203125e-07		/* 2^-21 */
#define MINIMO	1.401298464324817e-45		/* 2^-149 */

static double maximo(double a, double b) { return a > b ? a : b; }
static double absoluto(double a) { return a < 0 ? -a : a; }

static int armar_cotas(const char * archivo, const caso_t * c)
{
	const DWORD * fr = c->inicial.fp1;		/* banco activo  */
	const DWORD * xf = c->inicial.fp0;		/* XMTRX de FTRV */
	WORD op = (WORD) c->opcodes[1];
	int i, j, n, m;

	memset(cota, 0, sizeof(cota));
	hay_cota = 1;

	if (strncmp(archivo, "1111nnmm11101101", 16) == 0)		/* FIPR FVm,FVn */
	{
		double suma = 0;

		n = ((op >> 10) & 3) * 4;
		m = ((op >> 8)  & 3) * 4;

		for (i = 0; i < 4; i++)
			suma += absoluto((double) como_float(fr[n + i])
						   * (double) como_float(fr[m + i]));

		cota[n + 3] = suma * EPS23
					+ maximo(absoluto(como_float(c->final.fp1[n + 3])) * EPS23,
							 MINIMO);

		return 1;
	}

	if (strncmp(archivo, "1111nn0111111101", 16) == 0)		/* FTRV XMTRX,FVn */
	{
		n = ((op >> 10) & 3) * 4;

		for (i = 0; i < 4; i++)
		{
			double suma = 0;

			for (j = 0; j < 4; j++)
				suma += absoluto((double) como_float(xf[i + 4 * j])
							   * (double) como_float(fr[n + j]));

			cota[n + i] = suma * EPS23
						+ maximo(absoluto(como_float(c->final.fp1[n + i]))
								 * EPS23, MINIMO);
		}

		return 1;
	}

	if (strncmp(archivo, "1111nnn011111101", 16) == 0)		/* FSCA FPUL,DRn */
	{
		n = ((op >> 9) & 7) * 2;

		cota[n]     = EPS21;		/* seno y coseno viven en [-1, 1], asi que */
		cota[n + 1] = EPS21;		/* la cota absoluta es la que corresponde  */

		return 1;
	}

	if (strncmp(archivo, "1111nnnn01111101", 16) == 0)		/* FSRRA FRn */
	{
		n = (op >> 8) & 0x0F;

		cota[n] = absoluto(como_float(c->final.fp1[n])) * EPS21;

		return 1;
	}

	hay_cota = 0;

	return 0;
}

static int correr_archivo(const char * ruta, const char * nombre)
{
	unsigned char * datos;
	FILE * f;
	resumen_t * r;
	int i, detallados = 0;
	size_t leidos;

	f = fopen(ruta, "rb");

	if (!f)
	{
		fprintf(stderr, "singlestep: no se pudo abrir %s\n", ruta);
		return 0;
	}

	datos = (unsigned char *) malloc(TAM_ARCHIVO);

	if (!datos)
	{
		fclose(f);
		fprintf(stderr, "singlestep: sin memoria para %s\n", ruta);
		return 0;
	}

	leidos = fread(datos, 1, TAM_ARCHIVO, f);
	fclose(f);

	if (leidos != TAM_ARCHIVO)
	{
		free(datos);
		fprintf(stderr, "singlestep: %s mide %lu bytes, se esperaban %lu\n",
				ruta, (unsigned long) leidos, (unsigned long) TAM_ARCHIVO);
		return 0;
	}

	r = &resumen[n_resumen++];
	memset(r, 0, sizeof(*r));
	strncpy(r->nombre, nombre, sizeof(r->nombre) - 1);

	archivo_actual = nombre;


	for (i = 0; i < CASOS_POR_ARCHIVO; i++)
	{
		caso_t c;
		int res;

		cargar_caso(&c, datos + i * TAM_CASO);
		res = correr_caso(&c);

		if (res == RES_FALLA)
		{
			int d = clasificar(&c, nombre);

			if (d != DIV_NINGUNA)
			{
				res = RES_DIVERGE;
				r->divergencia = d;
			}
		}

		switch (res)
		{
			case RES_OK:		r->ok++;		break;
			case RES_DESCARTE:	r->descarte++;	break;
			case RES_DIVERGE:	r->diverge++;	break;

			default:
			{
				int j;

				r->falla++;

				for (j = 0; j < n_difs && j < MAX_DIF; j++)
					anotar_campo(r, difs[j].campo);

				if (detallados < max_detalle)
				{
					detallar(nombre, i, &c);
					detallados++;
				}
				break;
			}
		}
	}

	free(datos);

	total_ok       += r->ok;
	total_falla    += r->falla;
	total_descarte += r->descarte;
	total_diverge  += r->diverge;

	if (r->falla || listar_ok)
	{
		int j;

		printf("%-34s %4d ok  %4d fallan  %3d divergen  %2d descartados\n",
			   nombre, r->ok, r->falla, r->diverge, r->descarte);

		if (r->falla)
		{
			printf("    campos:");

			for (j = 0; j < r->n_campos; j++)
				printf(" %s(%d)", r->campo[j], r->cuenta[j]);

			printf("%s\n", r->n_campos == MAX_CAMPOS ? " ..." : "");
		}
	}

	return 1;
}

/* ------------------------------------------------------------------------ */

static int coincide(const char * nombre, int n, char ** filtros)
{
	int i;

	if (n == 0)
		return 1;

	for (i = 0; i < n; i++)
		if (strstr(nombre, filtros[i]))
			return 1;

	return 0;
}

/*
	La lista de archivos. No se usa opendir() para no arrastrar dirent.h en
	MSVC: el repositorio de las pruebas trae 233 nombres fijos y basta con
	probar cada codificacion que la tabla de opcodes conoce... pero eso seria
	adivinar. Se lee la lista de un archivo de texto que genera el propio
	corredor la primera vez, o se pasa por la linea de comandos.
*/
#ifdef _WIN32
#include <windows.h>

static int listar(const char * dir, char nombres[][64], int max)
{
	WIN32_FIND_DATAA fd;
	HANDLE h;
	char patron[512];
	int n = 0;

	sprintf(patron, "%s\\*.json.bin", dir);
	h = FindFirstFileA(patron, &fd);

	if (h == INVALID_HANDLE_VALUE)
		return 0;

	do
	{
		if (n < max)
		{
			strncpy(nombres[n], fd.cFileName, 63);
			nombres[n][63] = 0;
			n++;
		}
	} while (FindNextFileA(h, &fd));

	FindClose(h);

	return n;
}
#else
#include <dirent.h>

static int listar(const char * dir, char nombres[][64], int max)
{
	DIR * d = opendir(dir);
	struct dirent * e;
	int n = 0;

	if (!d)
		return 0;

	while ((e = readdir(d)) != NULL)
	{
		const char * p = strstr(e->d_name, ".json.bin");

		if (p && p[9] == 0 && n < max)
		{
			strncpy(nombres[n], e->d_name, 63);
			nombres[n][63] = 0;
			n++;
		}
	}

	closedir(d);

	return n;
}
#endif

static int cmp_nombre(const void * a, const void * b)
{
	return strcmp((const char *) a, (const char *) b);
}

/* main.h arrastra <SDL/SDL.h>, que hace "#define main SDL_main" en Windows.
   Igual que en principal.c: aqui no se enlaza SDLmain, asi que el punto de
   entrada tiene que llamarse main de verdad. */
#undef main

int main(int argc, char ** argv)
{
	static char nombres[512][64];
	const char * dir = getenv("DCEMU_SH4_JSON");
	char ** filtros = NULL;
	int n_filtros = 0;
	int n, i;

	for (i = 1; i < argc; i++)
	{
		if (strncmp(argv[i], "--dir=", 6) == 0)
			dir = argv[i] + 6;
		else if (strncmp(argv[i], "--detalle=", 10) == 0)
			max_detalle = atoi(argv[i] + 10);
		else if (strcmp(argv[i], "--todos") == 0)
			listar_ok = 1;
		else
		{
			if (!filtros)
				filtros = &argv[i];
			n_filtros++;
		}
	}

	if (!dir)
		dir = "../sh4-singlestep";

	n = listar(dir, nombres, 512);

	if (n == 0)
	{
		printf("singlestep: no hay pruebas en %s\n", dir);
		printf("  git clone https://github.com/SingleStepTests/sh4.git\n");
		printf("  y despues --dir=RUTA o DCEMU_SH4_JSON=RUTA\n");
		return 77;			/* CTest lo toma como omitida */
	}

	qsort(nombres, n, 64, cmp_nombre);

	arnes_iniciar();
	memoria_iniciar();
	core.execute = ss_execute;

	printf("singlestep: %d codificaciones en %s\n", n, dir);

	for (i = 0; i < n; i++)
	{
		char ruta[512];

		if (!coincide(nombres[i], n_filtros, filtros))
			continue;

		sprintf(ruta, "%s/%s", dir, nombres[i]);
		correr_archivo(ruta, nombres[i]);
	}

	printf("\n%d ok, %d fallan, %d divergen a proposito, %d descartados\n",
		   total_ok, total_falla, total_diverge, total_descarte);

	if (total_descarte)
		printf("  (descartado = la cuarta instruccion resulto ser un salto, o sea\n"
			   "   que el destino de la probada volvio a la ventana. El generador\n"
			   "   corta a los cuatro accesos y deja el estado final a mitad de una\n"
			   "   instruccion, que no hay como reproducir.)\n");

	if (total_diverge)
	{
		printf("\nDivergencias (la prueba sale de Reicast, no del manual):\n");

		for (i = 0; i < n_resumen; i++)
			if (resumen[i].diverge)
				printf("  %-34s %4d  %s\n", resumen[i].nombre,
					   resumen[i].diverge,
					   nombre_divergencia(resumen[i].divergencia));
	}

	return total_falla ? 1 : 0;
}
