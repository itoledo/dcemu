/****************************************************************************

	JIT - el recompilador dinamico, fase 0. Ver jit.h.

	Los dos bloques que emite son los de fusion.c, instruccion por instruccion,
	con los mismos ciclos, los mismos cortes y las mismas salidas. El
	desensamblado de cada uno esta en fusion.c y no se repite aca; lo que se
	comenta aca es lo que la traduccion agrega: el mapa de registros, la
	convencion de llamada y la informacion de desenrollado.

	**El mapa de registros del anfitrion**, fijo para los dos bloques:

	  rbx  el contexto (&core.context). Todo lo estatico que el codigo emitido
		   toca -- el estado del JIT, intc_sh4_reintentar -- se direcciona
		   tambien desde rbx con un desplazamiento calculado al emitir. Asi no
		   hacen falta inmediatos de 64 bits en el camino caliente ni que el
		   arena caiga a menos de 2 GB del segmento de datos.
	  rbp  los ciclos acumulados (el `cyc` de las sondas).
	  rsi  las instrucciones desde el ultimo volcado (el `n` de las sondas).
	  rdi, r12, r13, r14, r15   R0..R4 del SH-4.

	Los dos bloques usan R0..R4; el bloque CE toca ademas R5, R6 y R7, que se
	dejan en el contexto y se operan con operandos de memoria (una instruccion
	x64 por instruccion del SH-4, y de paso ya estan volcados cuando hay que
	sincronizar), y lee R8..R11, que nunca escribe.

	**Por que hay informacion de desenrollado.** El bloque CE sale por longjmp
	cada vez que un acceso falta -- 850 557 veces en el banco de DCDoom --, y
	en Windows x64 longjmp desenrolla la pila de verdad: le pregunta a
	RtlLookupFunctionEntry por cada marco. Un marco sin entrada se toma por
	hoja (RIP en [RSP], RSP += 8), que no es este: este empuja ocho registros y
	baja rsp. Sin la tabla, la primera falta se lleva el proceso. Se instala
	con RtlAddFunctionTable sobre el arena, describiendo el prologo -- que es
	byte a byte el mismo en los dos bloques --, y de paso es lo que le permite
	a traza_caida_instalar() cruzar el codigo emitido.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DCEMU_JIT

#include <stddef.h>

/* Antes que jit.h: en Windows DWORD lo trae <windows.h>, y aca no hay ningun
   otro encabezado del arbol que lo arrastre. */
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "jit.h"
#include "sh4emu.h"
#include "mem.h"
#include "mmu.h"		/* MMU_FETCH_PUNTERO */
#include "intc.h"		/* intc_sh4_reintentar */
#include "tmu.h"		/* RELOJ_GRANO */
#include "perf.h"
#include "jit_x64.h"

/* Los manejadores reales, que es de donde las plantillas copian y contra
   quienes se identifican. */
#include "opcodes.h"
#include "mov.h"
#include "arith.h"
#include "logic.h"
#include "shift.h"
#include "branch.h"
#include "syscontrol.h"

/* El emisor produce x86-64 y el contexto se direcciona por desplazamiento, asi
   que estas dos suposiciones son parte del contrato y no del ambiente. */
typedef char jit_assert_64[(sizeof(void *) == 8) ? 1 : -1];
typedef char jit_assert_ul[(sizeof(unsigned long) == 4) ? 1 : -1];

int jit_activo = 0;
unsigned char jit_mapa[8192];

/* La epoca del codigo traducido; ver jit.h. */
int				jit_vigila_codigo = 0;
unsigned		jit_epoca = 1;
unsigned		jit_md_visto = 0;
unsigned		jit_validez = 2;
unsigned		jit_ep_escritura = 0;
unsigned		jit_ep_mapeo = 0;
unsigned		jit_ep_modo = 0;
unsigned		jit_fpu_visto = 0;
unsigned		jit_ep_fpu = 0;
unsigned char	jit_pag_codigo[0x10000];

/* ------------------------------------------------------------------------ */
/* El estado que el codigo emitido toca                                     */
/* ------------------------------------------------------------------------ */

/*
	Un solo bloque de datos alcanzable desde rbx. El contador de instrucciones
	se vuelca antes de cada acceso (ver SINCRONIZAR en fusion.c: contarlo a la
	salida pierde las entradas que terminan en falta).

	Los punteros a los ayudantes existen solo como respaldo: si el arena cae a
	mas de 2 GB del codigo del emulador, el CALL directo no alcanza y se emite
	la forma por memoria.
*/
typedef struct
{
	unsigned long long		instr;
	void *					h_leer32;
	void *					h_leer8s;
	void *					h_leer16s;
	void *					h_escribir8;
	void *					h_escribir16;
	void *					h_escribir32;
	void *					h_leer32f;
	void *					h_leer8sf;
	void *					h_leer16sf;
	void *					h_escribir16f;
	void *					h_escribir8f;
	void *					h_escribir32f;
	/* PTEH y MMUCR viven adentro de regmem, que es un calloc de 16 MB: en
	   Windows una reserva de ese tamano no sale del monton chico y puede caer
	   a terabytes de la imagen, con lo cual no hay desplazamiento de 32 bits
	   desde el contexto que los alcance. Se guardan los punteros aca -- una
	   carga mas por acceso, sobre esta misma linea de cache -- en vez de
	   hornear la direccion. Lo descubrio el emisor negandose a emitir el
	   bloque, que es exactamente para lo que existe esa comprobacion. */
	void *					p_pteh;
	void *					p_mmucr;
	void *					entrada;		/* el cuerpo del bloque a entrar */
	void *					h_busqueda;		/* jit_busqueda_puente() */
} jit_estado_t;

static jit_estado_t jit_estado;

/* Solo desde C: el despachador las lleva, el codigo emitido no las toca. */
static unsigned long long jit_entradas  = 0;
static unsigned long long jit_rechazos  = 0;

/* ------------------------------------------------------------------------ */
/* Los ayudantes de memoria                                                 */
/* ------------------------------------------------------------------------ */

/*
	Envuelven los macros reales de mem.h, con lo cual el codigo emitido hereda
	gratis la alineacion, la traduccion con su avance de URC, los watchpoints,
	el UBC de operandos y el camino lento. **Son la semantica**: el camino
	rapido en linea de gen_rapido_inicio() cubre solo el caso plano y todo lo
	demas cae aca, asi que no hay una segunda copia de las reglas de mem.h.

	El plan daba por sentado que la llamada no era el costo. La fase 0 lo midio
	y no es asi -- ver el comentario de gen_rapido_inicio() --, por eso existe
	el camino en linea; pero el ayudante sigue siendo el que decide todo lo que
	no es el caso rapido.

	No son estaticas y se les toma la direccion, asi que LTCG tiene que
	dejarlas como funciones de verdad con la convencion del anfitrion.
*/
DWORD jit_leer32(DWORD dir)
{
	DWORD v;

	ReadMemoryL(dir, &v);

	return v;
}

DWORD jit_leer8s(DWORD dir)
{
	BYTE b;

	ReadMemoryB(dir, &b);

	return (DWORD) SignExtend8(b);
}

DWORD jit_leer16s(DWORD dir)
{
	WORD w;

	ReadMemoryW(dir, &w);

	return (DWORD) SignExtend16(w);
}

void jit_escribir8(DWORD dir, DWORD valor)
{
	BYTE b = (BYTE) (valor & 0xFF);

	WriteMemoryB(dir, &b);
}

void jit_escribir16(DWORD dir, DWORD valor)
{
	WORD w = (WORD) (valor & 0xFFFF);

	WriteMemoryW(dir, &w);
}

void jit_escribir32(DWORD dir, DWORD valor)
{
	DWORD v = valor;

	WriteMemoryL(dir, &v);
}

/*
	Los mismos, pero **con la direccion ya traducida**.

	Existen por un error que costo la exactitud del traductor. El camino rapido
	con MMU emite la traduccion en linea, y traducir **tiene efecto colateral**:
	avanza MMUCR.URC, que es lo que decide que entrada de la UTLB reemplaza el
	LDTLB del guest, o sea su camino de ejecucion. Si despues de traducir la
	zona resulta no tener base directa --registros, PVR, GD-ROM, AICA, colas de
	almacenamiento-- y se cae al ayudante de siempre, ese vuelve a traducir y
	**URC avanza dos veces**. Los bloques escritos a mano de la fase 0 no lo
	mostraron porque solo tocan RAM plana; el traductor toca de todo.

	La alineacion y el UBC de operandos ya los comprobo el camino rapido, asi
	que lo unico que falta es el despacho por zona, que es justo lo que hacen
	memread_fisico()/memwrite_fisico() -- con sus watchpoints incluidos.
*/
DWORD jit_leer32_fis(DWORD fisica)
{
	DWORD v;

	memread_fisico(fisica, &v, sizeof(DWORD));

	return v;
}

DWORD jit_leer8s_fis(DWORD fisica)
{
	BYTE b;

	memread_fisico(fisica, &b, sizeof(BYTE));

	return (DWORD) SignExtend8(b);
}

DWORD jit_leer16s_fis(DWORD fisica)
{
	WORD w;

	memread_fisico(fisica, &w, sizeof(WORD));

	return (DWORD) SignExtend16(w);
}

void jit_escribir8_fis(DWORD fisica, DWORD valor)
{
	BYTE b = (BYTE) (valor & 0xFF);

	memwrite_fisico(fisica, &b, sizeof(BYTE));
}

void jit_escribir16_fis(DWORD fisica, DWORD valor)
{
	WORD w = (WORD) (valor & 0xFFFF);

	memwrite_fisico(fisica, &w, sizeof(WORD));
}

void jit_escribir32_fis(DWORD fisica, DWORD valor)
{
	DWORD v = valor;

	memwrite_fisico(fisica, &v, sizeof(DWORD));
}

/*
	La busqueda de instruccion que un puente entre paginas reproduce.

	El despachador, antes de correr un bloque, busca su primera instruccion; esa
	busqueda avanza URC cuando falla la pagina vigente, repuebla la cache de
	busqueda y puede no volver (falta de TLB del lado de instrucciones). El salto
	encadenado se la saltea, y por eso el enlace directo quedo restringido a la
	pagina vigente. El talon del puente la hace de verdad: se llama con el
	contexto ya sincronizado --PC en el destino, registros y ciclos volcados, el
	contador recien volcado--, que es exactamente el estado con el que el
	interprete llega a la cabecera de su bucle, asi que una falta aqui sale por
	el longjmp con el mismo estado que alli.

	No hay nada que comprobar a la vuelta: la guarda de la clave ya paso --el
	mapeo del sucesor no cambio desde que se verifico--, asi que el puntero que
	esta busqueda resuelve es el mismo contra el que el sucesor se verifico.
*/
void jit_busqueda_puente(void)
{
	(void) MMU_FETCH_PUNTERO(PC);
}

/* ------------------------------------------------------------------------ */
/* El arena                                                                 */
/* ------------------------------------------------------------------------ */

/*
	Los topes vienen de la corrida de verificacion del puente, no de una
	corazonada: Crazy Taxi saturo los 16 384 bloques con 5269 candidatos
	calientes sin lugar --un tercio de su volumen sigue interpretado por falta
	de capacidad, no de plantillas-- y DCDoom dejo el arena al 98 % (15,68 de
	16 MB). El tope de bloques es el maximo que el `short` de la tabla hash
	direcciona.
*/
/* 64 MB: la segunda tanda de plantillas dejo a DCDoom con el arena de 32 al
   98,8 % y 1314 emisiones fallidas -- bloques de 15,6 instrucciones que ya no
   cupieron. El arena es lo unico que hoy le pone tope a su cobertura. */
#define JIT_ARENA_TAM		(64u * 1024u * 1024u)
#define JIT_MAX_BLOQUES		32768
#define JIT_MAX_INSTR		64

static unsigned char *	jit_arena     = NULL;
static unsigned char *	jit_codigo    = NULL;	/* donde empieza el codigo */
static unsigned			jit_codigo_us = 0;		/* cuanto se lleva emitido */
static unsigned			jit_codigo_tam = 0;

#define JIT_MAX_ENLACES		12

/*
	Un enlace: el salto directo de un bloque al siguiente, sin volver a C.

	**Lo que lo hace seguro es la epoca** (ver jit.h). El bloque sucesor guarda
	la epoca con la que se verifico entero; si la global no se movio desde
	entonces, sus palabras son las mismas y su pagina sigue mapeada donde
	estaba, porque la epoca se mueve con cualquiera de las dos cosas. Asi que la
	guarda del salto es **una comparacion** de la epoca global contra el campo
	del sucesor: ni busqueda en la tabla, ni comparacion de palabras, ni llamada
	indirecta.

	Un cambio de modo (SR.MD) tambien cambia el mapeo y no mueve la epoca --
	pero escribir SR no tiene plantilla y una excepcion sale por longjmp: las dos
	terminan la cadena, asi que dentro de una cadena el modo no cambia.

	Se parchean dos sitios: el desplazamiento del `cmp`, que pasa a apuntar al
	campo `epoca` del sucesor, y el rel32 del `jmp`. Sin parchear, el `cmp` mira
	`jit_nunca`, que vale cero y nunca iguala a la epoca --que arranca en 1 y
	solo sube--, asi que la guarda falla y el bloque sale como salia.
*/
typedef struct
{
	unsigned char *	sitio_cmp;		/* el disp32 del cmp de la guarda */
	unsigned char *	sitio_jmp;		/* el rel32 del salto */
	/*
		Los saltos indirectos --JSR, JMP, RTS-- no tienen sucesor constante, asi
		que su enlace lleva ademas una **guarda de destino visto**: un inmediato
		contra el que se compara el PC calculado. El destino no se sabe al
		traducir; lo aprende el despachador la primera vez que el bloque sale
		por ahi, y entonces parchea los tres sitios. Si el sitio resulta
		polimorfico, la guarda falla y se sale a C como siempre; el contador de
		parcheos evita que dos destinos se turnen para siempre.
	*/
	unsigned char *	sitio_pc;		/* el imm32 del cmp del destino, o NULL */
	/*
		**El puente entre paginas.** Un salto encadenado se saltea la busqueda de
		instruccion del despachador, y `traducir_busqueda()` avanza URC: por eso
		el enlace directo solo vale cuando saltearla no habria hecho nada, o sea
		cuando la busqueda habria acertado la pagina vigente. Para todo lo demas
		cada salida con MMU lleva un **talon**: volcar el contador, llamar a la
		busqueda real --con su avance de URC, su repoblado de la cache y su
		falta de TLB-- y recien entonces saltar al sucesor. Es exacto por
		construccion porque es la misma funcion que corre el despachador.

		El talon es NULL en los bloques sin MMU: alli no hay busqueda que
		reproducir y el salto directo siempre vale.
	*/
	unsigned char *	talon;			/* el comienzo del talon, o NULL */
	unsigned char *	talon_jmp;		/* el rel32 del salto final del talon */
	int				veces;
	DWORD			pc;				/* a que PC quiere saltar (0 si es dinamico) */
} jit_enlace;

static const unsigned jit_nunca = 0;

typedef struct
{
	DWORD			pc;					/* la entrada, en el espacio del guest */
	void			(* codigo)(void);
	const WORD *	palabras;			/* el tramo contiguo original */
	int				n_palabras;
	/* Palabras sueltas fuera del tramo (el callback del JSR en Crazy Taxi). */
	const DWORD *	extra_dir;
	const WORD *	extra_palabra;
	int				n_extra;
	/* Con que estado de la MMU se emitio: decide si sus accesos llevan la
	   traduccion en linea o la forma plana, asi que si cambia el bloque no
	   corre. En los escritos a mano es -1, "no importa". */
	int				mmu;
	WORD			copia[JIT_MAX_INSTR];	/* solo los traducidos */
	unsigned long long veces;
	/* La epoca con la que se verifico entero. Mientras la global no se mueva,
	   sus palabras son las mismas y su pagina sigue donde estaba. */
	unsigned		epoca;		/* la clave de validez con la que se verifico */
	const WORD *	ptr;		/* lo que devolvio la busqueda al verificarlo */
	jit_enlace		enlace[JIT_MAX_ENLACES];
	int				n_enlaces;
} jit_bloque;

static jit_bloque	jit_bloques[JIT_MAX_BLOQUES];
static int			jit_n_bloques = 0;

/*
	La tabla de bloques por PC. El mapa de bits dice "puede haber algo" y esta
	dice que -- y **tiene que estar indexada por el PC entero**, no por
	(PC >> 1) & 0xFFFF como el filtro: ese recorte distingue 128 KB de espacio
	de PC, y DCDoom ejecuta en cuatro ventanas a la vez (0x8C..., 0xAC...,
	0x0000..., 0x01E7...). Con mapeo directo recortado se pisaban entre ellas
	--16 146 reemplazos sobre 16 384 bloques-- y la cobertura se quedaba en el
	0,25 % con el censo ya seco: los bloques existian y no se encontraban.

	Direccionamiento abierto con sondeo lineal corto: si en ocho sitios no
	entra, ese bloque no se indexa y su PC sigue interpretado.

	El tamano viene de una medida: con 16 384 bloques sobre 32 768 ranuras
	--carga del 50 %-- Crazy Taxi dejaba 5269 inserciones sin lugar en ocho
	sondeos, o sea bloques ya emitidos que nadie podia encontrar. A carga del
	25 % la cola del sondeo lineal se corta; la tabla son shorts, 256 KB.
*/
#define JIT_HASH_BITS	17
#define JIT_HASH_N		(1u << JIT_HASH_BITS)
#define JIT_HASH_SONDEO	8

static short				jit_hash[JIT_HASH_N];
static unsigned long long	jit_colisiones = 0;

/* El sitio de salto indirecto por el que salio el ultimo bloque, o -1. Lo pone
   el codigo emitido y lo consume el despachador para aprender el destino. */
static int					jit_ult_sitio = -1;

/* Los bits **altos** del producto, que es donde el hash multiplicativo mezcla:
   tomar los bajos deja una permutacion de los bits de abajo del PC, o sea
   exactamente el recorte que este cambio venia a sacar. Costo 16 123 de 16 384
   bloques sin lugar en la tabla, con la cobertura clavada. El corrimiento se
   deriva de JIT_HASH_BITS: un literal desincronizado dejaria media tabla
   inalcanzable sin que nada lo reporte. */
static unsigned jit_hash_de(DWORD pc)
{
	unsigned h = (unsigned) ((pc >> 1) * 2654435761u);

	return (h >> (32 - JIT_HASH_BITS)) & (JIT_HASH_N - 1);
}

static void * jit_arena_reservar(unsigned tam)
{
#ifdef _WIN32
	return VirtualAlloc(NULL, tam, MEM_COMMIT | MEM_RESERVE,
		PAGE_EXECUTE_READWRITE);
#else
	{
		void * p = mmap(NULL, tam, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		return (p == MAP_FAILED) ? NULL : p;
	}
#endif
}

/* ------------------------------------------------------------------------ */
/* La informacion de desenrollado                                           */
/* ------------------------------------------------------------------------ */

/* Los desplazamientos de byte del prologo, capturados al emitirlo. */
typedef struct
{
	unsigned char	tras_push[8];	/* offset despues de cada push */
	unsigned char	tras_sub;		/* offset despues del sub rsp */
	unsigned char	tam;			/* = tras_sub */
} jit_marco;

#ifdef _WIN32

#define UWOP_PUSH_NONVOL	0
#define UWOP_ALLOC_SMALL	2

static RUNTIME_FUNCTION *	jit_tabla_rt = NULL;
static unsigned char *		jit_unwind   = NULL;
static int					jit_unwind_puesto = 0;

/*
	UNWIND_INFO a mano: version 1, sin banderas, sin registro de marco, y los
	codigos en orden **descendente** de desplazamiento, que es como los lee el
	desenrollador. El arreglo se rellena a una cantidad par de entradas.
*/
static void jit_unwind_armar(unsigned char * ui, const jit_marco * m,
	const x64_reg * empujados, int n_empujados, int bytes_rsp)
{
	unsigned char * c;
	int i, n;

	ui[0] = 1;						/* Version = 1, Flags = 0 */
	ui[1] = m->tam;					/* SizeOfProlog */
	ui[3] = 0;						/* FrameRegister = 0, FrameOffset = 0 */

	c = ui + 4;
	n = 0;

	/* El sub rsp es el ultimo del prologo, asi que va primero. */
	c[0] = m->tras_sub;
	c[1] = (unsigned char) (UWOP_ALLOC_SMALL | (((bytes_rsp / 8) - 1) << 4));
	c += 2;
	n++;

	for (i = n_empujados - 1; i >= 0; i--)
	{
		c[0] = m->tras_push[i];
		c[1] = (unsigned char) (UWOP_PUSH_NONVOL
			| (((int) empujados[i] & 15) << 4));
		c += 2;
		n++;
	}

	ui[2] = (unsigned char) n;		/* CountOfCodes */

	if (n & 1)						/* relleno a par */
	{
		c[0] = 0;
		c[1] = 0;
	}
}

#endif /* _WIN32 */

static jit_bloque * jit_buscar(DWORD pc)
{
	unsigned h = jit_hash_de(pc);
	int i;

	for (i = 0; i < JIT_HASH_SONDEO; i++)
	{
		short b = jit_hash[(h + (unsigned) i) & (JIT_HASH_N - 1)];

		if (b < 0)
			return NULL;

		if (jit_bloques[b].pc == pc)
			return &jit_bloques[b];
	}

	return NULL;
}

static void jit_insertar(int idx)
{
	unsigned h = jit_hash_de(jit_bloques[idx].pc);
	int i;

	for (i = 0; i < JIT_HASH_SONDEO; i++)
	{
		unsigned k = (h + (unsigned) i) & (JIT_HASH_N - 1);

		if (jit_hash[k] < 0)
		{
			jit_hash[k] = (short) idx;
			return;
		}
	}

	jit_colisiones++;
}

/*
	La tabla de funciones del bloque recien emitido. Se registra bloque por
	bloque --RtlAddFunctionTable acepta varias tablas-- porque con el traductor
	los bloques nacen a lo largo de la corrida y no todos al arrancar. Con
	cientos de bloques lo que corresponde es RtlInstallFunctionTableCallback.
*/
/*
	La informacion de desenrollado es **una sola**: la del trampolin, cubriendo
	todo el arena. Los bloques no tocan rsp, asi que para el desenrollador
	cualquier PC de ahi adentro esta despues del prologo del trampolin y el
	marco que hay que deshacer es el suyo. Antes habia una por bloque, porque
	cada bloque tenia su propio marco.
*/
static void jit_registrar_marco(const jit_bloque * b, unsigned largo)
{
	(void) b; (void) largo;
}

/* El prologo es byte a byte el mismo en todos los bloques, porque comparten la
   informacion de desenrollado. Se mide una vez al arrancar. */
static jit_marco	jit_marco_comun;
static int			jit_marco_visto = 0;

/* ------------------------------------------------------------------------ */
/* Los desplazamientos desde rbx                                            */
/* ------------------------------------------------------------------------ */

#define CTX		X64_RBX
#define CYC		X64_RBP
#define N		X64_RSI

static const x64_reg jit_a[5] =
	{ X64_RDI, X64_R12, X64_R13, X64_R14, X64_R15 };	/* R0..R4 */

#define A(n)	(jit_a[(n)])

#define O_CYC	((int) offsetof(context_t, cycles))
#define O_PC	((int) offsetof(context_t, PC_REG))
#define O_SR	((int) offsetof(context_t, SR_REG))
#define O_PR	((int) offsetof(context_t, PR_REG))
#define O_MACL	((int) offsetof(context_t, MACL_REG))
#define O_R(n)	((int) (offsetof(context_t, registers) + 4 * (n)))

static int jit_disp_ok = 1;

/* Un estatico cualquiera, visto desde rbx. Cae comodo en 32 bits porque todo
   esto vive en el segmento de datos de la misma imagen; si algun dia no, el
   bloque no se emite y el guest sigue interpretado. */
static int D(const void * p)
{
	ptrdiff_t d = (const char *) p - (const char *) &core.context;

	if (d < -2147483647 || d > 2147483647)
	{
		jit_disp_ok = 0;
		return 0;
	}

	return (int) d;
}

#define D_INSTR		D(&jit_estado.instr)
#define D_PERF		D(&perf_instrucciones)
#define D_LEER32	D(&jit_estado.h_leer32)
#define D_LEER8S	D(&jit_estado.h_leer8s)
#define D_LEER16S	D(&jit_estado.h_leer16s)
#define D_LEER16SF	D(&jit_estado.h_leer16sf)
#define D_ESCR16	D(&jit_estado.h_escribir16)
#define D_ESCR16F	D(&jit_estado.h_escribir16f)
#define D_ESCR8		D(&jit_estado.h_escribir8)
#define D_ESCR32	D(&jit_estado.h_escribir32)
#define D_LEER32F	D(&jit_estado.h_leer32f)
#define D_LEER8SF	D(&jit_estado.h_leer8sf)
#define D_ESCR8F	D(&jit_estado.h_escribir8f)
#define D_ESCR32F	D(&jit_estado.h_escribir32f)
#define D_PAG_CODIGO	D(&jit_pag_codigo[0])
#define D_EPOCA		D(&jit_validez)
#define D_ENTRADA	D(&jit_estado.entrada)
#define D_BUSQUEDA	D(&jit_estado.h_busqueda)
#define D_ULT_SITIO	D(&jit_ult_sitio)
#define D_REINTENTO	D(&intc_sh4_reintentar)
#define D_MMU		D(&mmu_activa)
#define D_UBC_OP	D(&ubc_operando_activa)
#define D_BASE_LEC	D(&mem_base_lectura[0])
#define D_BASE_ESC	D(&mem_base_escritura[0])

/* Lo que la traduccion emitida en linea mira. PTEH y MMUCR son punteros a
   regmem, ligados una sola vez en regmem_setup() --que corre mucho antes que
   jit_iniciar()--, asi que lo que se hornea es la direccion a la que apuntan y
   no una segunda indireccion en el camino caliente. */
#define D_MACRO_PROBAR	D(&mmu_macro_probar)
#define D_DATOS_MASCARA	D(&mmu_datos_mascara)
#define D_MMU_DATOS		D(&mmu_datos[0])
#define D_UTLB_GEN		D(&mmu_utlb_gen[0])
#define D_P_PTEH		D(&jit_estado.p_pteh)
#define D_P_MMUCR		D(&jit_estado.p_mmucr)
#define D_PERF_TRADUCE	D(&perf_mmu_traduce)
#define D_PERF_ACIERTO	D(&perf_mmu_datos_acierto)

/* ------------------------------------------------------------------------ */
/* El generador                                                             */
/* ------------------------------------------------------------------------ */

#define JIT_MAX_SALIDAS		64
typedef struct
{
	x64_emisor	e;
	x64_parche	salidas[JIT_MAX_SALIDAS];
	int			n_salidas;
	jit_enlace	enlace[JIT_MAX_ENLACES];
	int			n_enlaces;
	jit_marco	marco;
} jit_gen;

/* Los ocho no volatiles que el bloque usa, en orden de empuje. */
static const x64_reg jit_empujados[8] =
{
	X64_RBX, X64_RBP, X64_RSI, X64_RDI, X64_R12, X64_R13, X64_R14, X64_R15
};

/*
	40 bytes: los 32 del espacio de sombra que la convencion de Windows exige
	al que llama, mas 8 para que rsp quede alineado a 16 en cada CALL. Con
	ocho empujes (64 bytes, multiplo de 16) rsp queda en 8 respecto de la
	frontera, y estos 40 la cierran.
*/
#define JIT_MARCO_RSP		40

/* El marco lo arma el trampolin: un bloque solo carga los registros del guest
   que mapea. El contexto, los ciclos y el contador ya vienen en registros. */
static void gen_prologo(jit_gen * g)
{
	int i;

	for (i = 0; i < 5; i++)
		jit_x64_mov_rm(&g->e, A(i), CTX, O_R(i));
}

/* El volcado de los registros cacheados y de los ciclos. Lo comparten el
   epilogo y --en el bloque con MMU-- cada sincronizacion previa a un acceso. */
static void gen_volcar_regs(jit_gen * g)
{
	int i;

	for (i = 0; i < 5; i++)
		jit_x64_mov_mr(&g->e, CTX, O_R(i), A(i));

	jit_x64_mov_mr(&g->e, CTX, O_CYC, CYC);
}

/*
	Los contadores. **Especializado en el momento de emitir**: `perf_activa` no
	puede cambiar durante la corrida --se lee una vez al arrancar y el bloque se
	emite despues--, asi que la suma a perf_instrucciones se emite o no se
	emite, en vez de decidirse en cada volcado.

	Y no es cosmetico: la primera version llevaba un puntero al destino (a
	perf_instrucciones o a un pozo) para evitar la rama, y pagaba **una carga
	dependiente y un segundo lee-modifica-escribe a una direccion fija en cada
	sincronizacion**. Con seis sincronizaciones por vuelta eso es una cadena
	serializada de unos 36 ciclos por vuelta que el C de la sonda no tiene,
	porque su `if (perf_activa)` es una rama perfectamente predicha. Medido en
	el A/B de la fase 0.
*/
static void gen_volcar_cuenta(jit_gen * g)
{
	jit_x64_add64_mr(&g->e, CTX, D_INSTR, N);

	if (perf_activa)
		jit_x64_add64_mr(&g->e, CTX, D_PERF, N);

	jit_x64_xor_rr(&g->e, N, N);
}

/* ------------------------------------------------------------------------ */
/* Los accesos a memoria                                                    */
/* ------------------------------------------------------------------------ */

/*
	La llamada al ayudante: directa si el arena cae en alcance (cinco bytes y
	sin carga) y por la tabla en memoria si no.
*/
static void gen_llamar(jit_gen * g, const void * destino, int disp_tabla)
{
	if (!jit_x64_call_directo(&g->e, destino))
		jit_x64_call_m(&g->e, CTX, disp_tabla);
}

/*
	El camino rapido de memread/memwrite, emitido en linea.

	**Existe porque la fase 0 lo midio.** El plan daba por sentado que la
	llamada al ayudante no era el costo; el A/B de los bloques dijo lo
	contrario: en Crazy Taxi --que no tiene MMU, ni sincronizaciones, ni
	registros fuera de los cacheados-- la unica diferencia estructural con el C
	de la sonda es que el C **expande el macro** y el JIT **llamaba**, y eso
	valia 2,2 ns (unos 9 ciclos) por acceso. Con 0,5 accesos por instruccion,
	la mitad de la ganancia del bloque.

	Lo que se emite es exactamente el caso rapido del macro y nada mas:
	alineacion, MMU apagada, UBC de operandos apagado, y la zona con base
	directa en mem_base_lectura/mem_base_escritura. **Cualquier otra cosa cae
	al ayudante**, que es el macro entero -- traduccion, watchpoints, UBC,
	camino lento, el error de direccion --, asi que la semantica sigue siendo
	la de mem.h y no hay una segunda copia de sus reglas que pueda derivar.

	Las tres pruebas de guarda son comparaciones contra cero perfectamente
	predichas y sobre lineas de cache calientes; el macro de mem.h hace las
	mismas dos ultimas.
*/
/* El valor de una escritura se materializa por callback; la definicion vive
   junto a gen_escribir(), pero gen_rapido_fin() ya la necesita. */
typedef void (* jit_valor_f)(jit_gen * g, void * ctx, x64_reg dst);

typedef struct
{
	x64_parche	lento[12];		/* al ayudante que traduce (direccion virtual) */
	int			n_lento;
	x64_parche	lento_fis[2];	/* al ayudante que NO traduce (ya es fisica) */
	int			n_lento_fis;
	int			corto;			/* los saltos al camino lento caben en rel8 */
	x64_reg		fis;			/* que registro lleva la direccion fisica */
	x64_parche	listo;
	x64_parche	listo2;
} jit_acceso;

/* Los tres modos de un acceso. PLANO y MMU son politica, no correccion: el
   ayudante siempre esta detras y decide todo lo que el camino rapido no cubre.
   Con la MMU encendida el camino plano nunca se tomaria, y con ella apagada la
   traduccion emitida seria codigo muerto -- por eso son modos y no uno solo.
   En la fase 1 el modo sale de la clave del bloque; aqui va a mano, que con dos
   bloques escritos a mano es lo mismo. */
#define JIT_ACC_LENTO	0
#define JIT_ACC_PLANO	1
#define JIT_ACC_MMU		2

static x64_parche gen_guarda(jit_gen * g, jit_acceso * a, x64_cond cc)
{
	return a->corto ? jit_x64_jcc_corto(&g->e, cc) : jit_x64_jcc(&g->e, cc);
}

/*
	La traduccion de datos emitida en linea: el acierto de mmu_datos[], que es
	MMU_TRADUCIR_EN_SITIO de mmu.h copiado condicion por condicion.

	**Lo que lo hace correcto y no una segunda implementacion de la MMU** es que
	solo se emite el ACIERTO: las cuatro comparaciones de la etiqueta, el
	permiso, la VPN y la generacion, el avance de URC y la composicion de la
	fisica. Cualquiera que falle cae al ayudante, que expande el macro entero y
	de ahi a mmu_traducir(), que decide todo lo demas -- fallos, permisos,
	primera escritura, P1/P2/P4 -- como siempre.

	Los dos efectos que **no** son el resultado y que igual hay que reproducir,
	porque de ellos depende que la ejecucion siga siendo la misma:

	 - **URC avanza tambien en el acierto.** De URC depende que entrada de la
	   UTLB reemplaza el LDTLB del guest, o sea su camino de ejecucion. Un
	   acierto que no lo avance no se nota en una captura: se nota mil millones
	   de instrucciones despues.
	 - **los contadores de --perf se incrementan igual**, o la verificacion de
	   trabajo entre corridas cambia de significado con el interruptor. Van
	   especializados al emitir, como el contador de instrucciones.

	La direccion virtual queda intacta en ECX --el camino lento la necesita-- y
	la fisica sale en R11D.
*/
static void gen_traducir_mmu(jit_gen * g, jit_acceso * a, unsigned permiso_bit)
{
	x64_parche sin_urb;
	const int  DAT = D_MMU_DATOS;

	/* if (!mmu_macro_probar) -> mmu_traducir() */
	jit_x64_cmp_mi(&g->e, CTX, D_MACRO_PROBAR, 0);
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_E);

	/* r9 = ((dir >> 12) & mmu_datos_mascara) * sizeof(mmu_datos_t), en bytes:
	   el elemento no mide una potencia de dos, asi que el indice viaja ya
	   multiplicado y la escala del SIB es 1. */
	jit_x64_mov_rr(&g->e, X64_RAX, X64_RCX);
	jit_x64_shr_ri(&g->e, X64_RAX, 12);
	jit_x64_and_rm(&g->e, X64_RAX, CTX, D_DATOS_MASCARA);
	jit_x64_imul_rri(&g->e, X64_RAX, X64_RAX, (int) sizeof(mmu_datos_t));
	jit_x64_mov_rr(&g->e, X64_R9, X64_RAX);

	/* edx = ASID_DE(*PTEH) | ((SR_MD == 0) << 8) | MMU_CACHE_VALIDA */
	jit_x64_mov64_rm(&g->e, X64_R8, CTX, D_P_PTEH);
	jit_x64_mov_rm(&g->e, X64_RDX, X64_R8, 0);
	jit_x64_and_ri(&g->e, X64_RDX, 0xFF);
	jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_SR);
	jit_x64_shr_ri(&g->e, X64_RAX, 30);			/* MD es el bit 30 */
	jit_x64_and_ri(&g->e, X64_RAX, 1);
	jit_x64_xor_ri(&g->e, X64_RAX, 1);
	jit_x64_shl_ri(&g->e, X64_RAX, 8);
	jit_x64_or_ri(&g->e, X64_RDX, (int) MMU_CACHE_VALIDA);
	jit_x64_add_rr(&g->e, X64_RDX, X64_RAX);	/* los bits son disjuntos */

	/* etiqueta */
	jit_x64_cmp_rm_idx(&g->e, X64_RDX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, etiqueta));
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_NE);

	/* permisos */
	jit_x64_test_mi_idx(&g->e, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, permisos), (int) permiso_bit);
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_E);

	/* (dir & ~mascara) == vpn */
	jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, mascara));
	jit_x64_not_r(&g->e, X64_RAX);
	jit_x64_and_rr(&g->e, X64_RAX, X64_RCX);
	jit_x64_cmp_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, vpn));
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_NE);

	/* mmu_utlb_gen[entrada] == gen */
	jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, entrada));
	jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_RAX, 4, D_UTLB_GEN);
	jit_x64_cmp_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, gen));
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_NE);

	/* --- MMU_URC_AVANZAR() --- */
	jit_x64_mov64_rm(&g->e, X64_R8, CTX, D_P_MMUCR);
	jit_x64_mov_rm(&g->e, X64_RAX, X64_R8, 0);
	jit_x64_mov_rr(&g->e, X64_RDX, X64_RAX);
	jit_x64_shr_ri(&g->e, X64_RDX, 10);
	jit_x64_and_ri(&g->e, X64_RDX, 0x3F);
	jit_x64_add_ri(&g->e, X64_RDX, 1);
	jit_x64_and_ri(&g->e, X64_RDX, 0x3F);		/* _urc */
	jit_x64_mov_rr(&g->e, X64_R10, X64_RAX);
	jit_x64_shr_ri(&g->e, X64_R10, 18);
	jit_x64_and_ri(&g->e, X64_R10, 0x3F);		/* URB */
	jit_x64_test_rr(&g->e, X64_R10, X64_R10);
	sin_urb = jit_x64_jcc_corto(&g->e, X64_E);
	jit_x64_cmp_rr(&g->e, X64_RDX, X64_R10);
	{
		x64_parche distinto = jit_x64_jcc_corto(&g->e, X64_NE);

		jit_x64_xor_rr(&g->e, X64_RDX, X64_RDX);
		jit_x64_fijar(&g->e, distinto);
	}
	jit_x64_fijar(&g->e, sin_urb);
	jit_x64_and_ri(&g->e, X64_RAX, (int) 0xFFFF03FFul);	/* ~0x0000FC00 */
	jit_x64_shl_ri(&g->e, X64_RDX, 10);
	jit_x64_add_rr(&g->e, X64_RAX, X64_RDX);
	jit_x64_mov_mr(&g->e, X64_R8, 0, X64_RAX);

	if (perf_activa)
	{
		jit_x64_add64_mi(&g->e, CTX, D_PERF_TRADUCE, 1);
		jit_x64_add64_mi(&g->e, CTX, D_PERF_ACIERTO, 1);
	}

	/* r11d = base | (dir & mascara) */
	jit_x64_mov_rm_idx(&g->e, X64_R11, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, mascara));
	jit_x64_and_rr(&g->e, X64_R11, X64_RCX);
	jit_x64_or_rm_idx(&g->e, X64_R11, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, base));
}

/*
	El camino rapido de memread/memwrite, emitido en linea.

	**Existe porque la fase 0 lo midio.** El plan daba por sentado que la
	llamada al ayudante no era el costo; el A/B de los bloques dijo lo
	contrario: en Crazy Taxi --que no tiene MMU, ni sincronizaciones, ni
	registros fuera de los cacheados-- la unica diferencia estructural con el C
	de la sonda es que el C **expande el macro** y el JIT **llamaba**, y eso
	valia 2,2 ns (unos 9 ciclos) por acceso. Con 0,5 accesos por instruccion,
	la mitad de la ganancia del bloque.

	Lo que se emite es exactamente el caso rapido del macro y nada mas:
	alineacion, UBC de operandos apagado, la traduccion si corresponde, y la
	zona con base directa en mem_base_lectura/mem_base_escritura. **Cualquier
	otra cosa cae al ayudante**, que es el macro entero -- watchpoints, UBC,
	camino lento, el error de direccion, mmu_traducir() --, asi que la semantica
	sigue siendo la de mem.h.

	Las pruebas de guarda son comparaciones contra cero perfectamente predichas
	y sobre lineas de cache calientes; el macro de mem.h hace las mismas.
*/
static void gen_rapido_inicio(jit_gen * g, jit_acceso * a, int disp_tabla,
	int alineacion, int modo)
{
	a->n_lento     = 0;
	a->n_lento_fis = 0;
	a->corto   = (modo != JIT_ACC_MMU);		/* con la traduccion no cabe rel8 */
	a->fis     = (modo == JIT_ACC_MMU) ? X64_R11 : X64_RCX;

	if (alineacion)
	{
		jit_x64_test_ri8(&g->e, X64_RCX, alineacion);
		a->lento[a->n_lento++] = gen_guarda(g, a, X64_NE);
	}

	jit_x64_cmp_mi(&g->e, CTX, D_MMU, 0);
	a->lento[a->n_lento++] = gen_guarda(g, a,
		(modo == JIT_ACC_MMU) ? X64_E : X64_NE);

	jit_x64_cmp_mi(&g->e, CTX, D_UBC_OP, 0);
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_NE);

	if (modo == JIT_ACC_MMU)
		gen_traducir_mmu(g, a,
			(disp_tabla == D_BASE_ESC) ? MMU_DATOS_ESCRIBIR : MMU_DATOS_LEER);

	/* rax = la base de la zona; r8 = el desplazamiento dentro de ella. */
	jit_x64_mov_rr(&g->e, X64_RAX, a->fis);
	jit_x64_shr_ri(&g->e, X64_RAX, 24);
	jit_x64_mov64_rm_idx(&g->e, X64_RAX, CTX, X64_RAX, 8, disp_tabla);
	jit_x64_test64_rr(&g->e, X64_RAX, X64_RAX);

	/* Aca la traduccion ya corrio y ya avanzo URC: volver por el ayudante que
	   traduce la avanzaria de nuevo, asi que esta salida usa la fisica. Sin
	   MMU no hay traduccion y da lo mismo, asi que va por el camino comun. */
	if (modo == JIT_ACC_MMU)
		a->lento_fis[a->n_lento_fis++] = gen_guarda(g, a, X64_E);
	else
		a->lento[a->n_lento++] = gen_guarda(g, a, X64_E);

	jit_x64_mov_rr(&g->e, X64_R8, a->fis);
	jit_x64_and_ri(&g->e, X64_R8, 0x00FFFFFF);
}

/*
	Cierra el camino rapido y abre los lentos: primero el fisico --la
	traduccion ya corrio, hay que entrar por la zona sin volver a traducir-- y
	despues el virtual, que es el macro entero.
*/
static void gen_rapido_fin(jit_gen * g, jit_acceso * a, int disp_fis,
	const void * ayudante_fis, jit_valor_f val, void * ctx)
{
	int i;

	a->listo  = jit_x64_jmp(&g->e);
	a->listo2 = a->listo;
	a->listo2.sitio = NULL;

	if (a->n_lento_fis)
	{
		for (i = 0; i < a->n_lento_fis; i++)
			jit_x64_fijar(&g->e, a->lento_fis[i]);

		jit_x64_mov_rr(&g->e, X64_RCX, a->fis);

		if (val != NULL)
			val(g, ctx, X64_RDX);

		gen_llamar(g, ayudante_fis, disp_fis);
		a->listo2 = jit_x64_jmp(&g->e);
	}

	for (i = 0; i < a->n_lento; i++)
		jit_x64_fijar(&g->e, a->lento[i]);
}

static void gen_acceso_cerrar(jit_gen * g, jit_acceso * a)
{
	jit_x64_fijar(&g->e, a->listo);

	if (a->listo2.sitio != NULL)
		jit_x64_fijar(&g->e, a->listo2);
}

/*
	Los tres accesos que los bloques de la fase 0 usan. La direccion virtual
	llega en ECX --y ahi se queda, porque el camino lento la necesita sin
	traducir-- y el resultado sale en EAX: los dos caminos convergen ahi, que es
	lo que permite que el punto de union sea uno solo.
*/
static void gen_leer32(jit_gen * g, x64_reg dst, int modo)
{
	jit_acceso a;

	if (modo != JIT_ACC_LENTO)
	{
		gen_rapido_inicio(g, &a, D_BASE_LEC, 3, modo);
		jit_x64_mov_rm_idx(&g->e, X64_RAX, X64_RAX, X64_R8, 1, 0);
		gen_rapido_fin(g, &a, D_LEER32F, (const void *) jit_leer32_fis,
			NULL, NULL);
	}

	gen_llamar(g, (const void *) jit_leer32, D_LEER32);

	if (modo != JIT_ACC_LENTO)
		gen_acceso_cerrar(g, &a);

	if (dst != X64_RAX)
		jit_x64_mov_rr(&g->e, dst, X64_RAX);
}

static void gen_leer8s(jit_gen * g, x64_reg dst, int modo)
{
	jit_acceso a;

	if (modo != JIT_ACC_LENTO)
	{
		gen_rapido_inicio(g, &a, D_BASE_LEC, 0, modo);
		jit_x64_movsx_b_rm_idx(&g->e, X64_RAX, X64_RAX, X64_R8, 1, 0);
		gen_rapido_fin(g, &a, D_LEER8SF, (const void *) jit_leer8s_fis,
			NULL, NULL);
	}

	gen_llamar(g, (const void *) jit_leer8s, D_LEER8S);

	if (modo != JIT_ACC_LENTO)
		gen_acceso_cerrar(g, &a);

	if (dst != X64_RAX)
		jit_x64_mov_rr(&g->e, dst, X64_RAX);
}

/* Como gen_leer8s() pero de 16 bits: la mascara de alineacion es 1 y el
   camino rapido extiende signo desde una palabra. Lo pidio el censo de los
   dos guests a la vez (MOV.W @Rm,Rn: 64 sitios en DCDoom, 66 en Crazy Taxi). */
static void gen_leer16s(jit_gen * g, x64_reg dst, int modo)
{
	jit_acceso a;

	if (modo != JIT_ACC_LENTO)
	{
		gen_rapido_inicio(g, &a, D_BASE_LEC, 1, modo);
		jit_x64_movsx_w_rm_idx(&g->e, X64_RAX, X64_RAX, X64_R8, 1, 0);
		gen_rapido_fin(g, &a, D_LEER16SF, (const void *) jit_leer16s_fis,
			NULL, NULL);
	}

	gen_llamar(g, (const void *) jit_leer16s, D_LEER16S);

	if (modo != JIT_ACC_LENTO)
		gen_acceso_cerrar(g, &a);

	if (dst != X64_RAX)
		jit_x64_mov_rr(&g->e, dst, X64_RAX);
}

/*
	El valor de una escritura se materializa **en el ultimo momento**, en EDX, y
	por callback: en los bloques escritos a mano vive en un registro del
	anfitrion, y en los traducidos puede vivir en el contexto. Va al final
	porque la traduccion de la MMU usa EDX para el avance de URC.
*/
/* El callback de la fase 0: el valor esta en un registro no volatil. */
static void jit_valor_reg(jit_gen * g, void * ctx, x64_reg dst)
{
	x64_reg fuente = *(const x64_reg *) ctx;

	if (fuente != dst)
		jit_x64_mov_rr(&g->e, dst, fuente);
}

static void gen_escribir(jit_gen * g, int modo, int ancho,
	jit_valor_f val, void * ctx)
{
	jit_acceso a;

	if (modo != JIT_ACC_LENTO)
	{
		gen_rapido_inicio(g, &a, D_BASE_ESC,
			ancho == 4 ? 3 : (ancho == 2 ? 1 : 0), modo);

		/*
			**La pagina que se escribe no puede tener codigo traducido**, o el
			camino rapido emitido escribiria sin mover la epoca -- y la epoca es
			lo que le dice al despacho que un bloque sigue valiendo. Si la tiene,
			el acceso baja al ayudante, que escribe por memwrite() y mueve la
			epoca como corresponde. Cuatro instrucciones por escritura emitida;
			el agujero costaba 61 568 instrucciones de divergencia.
		*/
		jit_x64_lea64_idx(&g->e, X64_R9, X64_RAX, X64_R8, 1, 0);
		jit_x64_shift64_ri(&g->e, X64_SHR, X64_R9, 12);
		jit_x64_alu_ri(&g->e, X64_AND, X64_R9, 0xFFFF);
		jit_x64_cmp8_mi_idx(&g->e, CTX, X64_R9, 1, D_PAG_CODIGO, 0);

		if (a.n_lento_fis)
			a.lento_fis[a.n_lento_fis++] = gen_guarda(g, &a, X64_NE);
		else
			a.lento[a.n_lento++] = gen_guarda(g, &a, X64_NE);

		val(g, ctx, X64_RDX);

		if (ancho == 4)
			jit_x64_mov_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, X64_RDX);
		else if (ancho == 2)
			jit_x64_mov16_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, X64_RDX);
		else
			jit_x64_mov8_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, X64_RDX);

		gen_rapido_fin(g, &a,
			ancho == 4 ? D_ESCR32F : (ancho == 2 ? D_ESCR16F : D_ESCR8F),
			ancho == 4 ? (const void *) jit_escribir32_fis
					   : (ancho == 2 ? (const void *) jit_escribir16_fis
									 : (const void *) jit_escribir8_fis),
			val, ctx);
	}

	val(g, ctx, X64_RDX);
	gen_llamar(g, ancho == 4 ? (const void *) jit_escribir32
							 : (ancho == 2 ? (const void *) jit_escribir16
										   : (const void *) jit_escribir8),
		ancho == 4 ? D_ESCR32 : (ancho == 2 ? D_ESCR16 : D_ESCR8));

	if (modo != JIT_ACC_LENTO)
		gen_acceso_cerrar(g, &a);
}

static void gen_escribir8_reg(jit_gen * g, x64_reg valor, int modo)
{
	x64_reg v = valor;

	gen_escribir(g, modo, 1, jit_valor_reg, &v);
}

/* ------------------------------------------------------------------------ */
/* El trampolin                                                             */
/* ------------------------------------------------------------------------ */

/*
	**Los bloques dejan de ser funciones de C.**

	Antes cada bloque empujaba ocho registros, armaba su marco, cargaba el
	contexto y al salir lo deshacia todo -- y un salto encadenado pagaba las
	ocho sacadas del que salia y los ocho empujes del que entraba. Con 1900
	millones de entradas eso es el costo por entrada, que es lo que la medicion
	viene senalando desde que el traductor empezo a andar.

	Ahora hay un trampolin: se entra al mundo emitido **una vez**, se arma el
	marco una vez, y los bloques son tramos de codigo que se saltan entre si.
	Lo que un bloque hace al entrar es cargar los registros del guest que
	mapeo; lo que hace al salir es volcarlos. El contexto (rbx), los ciclos
	(rbp) y el contador (rsi) viven en registros durante toda la cadena.

	La informacion de desenrollado pasa a ser **una sola**, la del trampolin,
	cubriendo todo el arena: los bloques no tocan rsp, asi que para el
	desenrollador cualquier PC de ahi adentro esta "despues del prologo" del
	trampolin y el marco que hay que deshacer es el suyo. Eso es lo que
	mantiene sano el longjmp de una falta.
*/
static unsigned char *	jit_tramp       = NULL;
static unsigned char *	jit_tramp_salir = NULL;

static int jit_emitir_trampolin(void)
{
	jit_gen	g;
	int		i;

	memset(&g, 0, sizeof(g));
	jit_x64_iniciar(&g.e, jit_codigo + jit_codigo_us,
		jit_codigo_tam - jit_codigo_us);

	jit_tramp = jit_x64_aqui(&g.e);

	for (i = 0; i < 8; i++)
	{
		jit_x64_push(&g.e, jit_empujados[i]);
		g.marco.tras_push[i] = (unsigned char) jit_x64_largo(&g.e);
	}

	jit_x64_sub64_ri(&g.e, X64_RSP, JIT_MARCO_RSP);
	g.marco.tras_sub = (unsigned char) jit_x64_largo(&g.e);
	g.marco.tam      = g.marco.tras_sub;

	jit_x64_mov64_ri(&g.e, CTX, (unsigned long long) (size_t) &core.context);
	jit_x64_mov_rm(&g.e, CYC, CTX, O_CYC);
	jit_x64_xor_rr(&g.e, N, N);

	/* Al bloque que el despachador dejo anotado. */
	jit_x64_mov64_rm(&g.e, X64_RAX, CTX, D_ENTRADA);
	jit_x64_jmp_r(&g.e, X64_RAX);

	/* La salida comun: aqui saltan todos los bloques cuando hay que volver. */
	jit_tramp_salir = jit_x64_aqui(&g.e);

	jit_x64_mov_mr(&g.e, CTX, O_CYC, CYC);
	gen_volcar_cuenta(&g);
	jit_x64_add64_ri(&g.e, X64_RSP, JIT_MARCO_RSP);

	for (i = 7; i >= 0; i--)
		jit_x64_pop(&g.e, jit_empujados[i]);

	jit_x64_ret(&g.e);

	if (g.e.desborde || !jit_disp_ok)
		return 0;

	jit_marco_comun = g.marco;
	jit_marco_visto = 1;
	jit_codigo_us  += jit_x64_largo(&g.e);
	jit_codigo_us   = (jit_codigo_us + 15u) & ~15u;

	return 1;
}

static void gen_salir_en(jit_gen * g, DWORD pc_sig)
{
	jit_x64_mov_mi(&g->e, CTX, O_PC, pc_sig);

	if (g->n_salidas < JIT_MAX_SALIDAS)
		g->salidas[g->n_salidas++] = jit_x64_jmp(&g->e);
	else
		g->e.desborde = 1;
}

/*
	El corte del bloque periodico, en la misma frontera en que el interprete lo
	evaluaria. Los dos saltos cortos caen dentro de los pocos bytes que separan
	las tres etiquetas, asi que no hay rel32 en el camino que no corta.
*/
static void gen_corte(jit_gen * g, DWORD pc_sig)
{
	x64_parche a_cortar, sigue;

	jit_x64_cmp_ri(&g->e, CYC, RELOJ_GRANO);
	a_cortar = jit_x64_jcc_corto(&g->e, X64_AE);
	jit_x64_cmp_mi(&g->e, CTX, D_REINTENTO, 0);
	sigue = jit_x64_jcc_corto(&g->e, X64_E);
	jit_x64_fijar(&g->e, a_cortar);
	gen_salir_en(g, pc_sig);
	jit_x64_fijar(&g->e, sigue);
}

/* Una instruccion de ALU cerrada: sus ciclos, su cuenta y su corte. */
static void gen_fin_alu(jit_gen * g, int ciclos, DWORD pc_sig)
{
	if (ciclos)
		jit_x64_add_ri(&g->e, CYC, ciclos);

	jit_x64_inc_r(&g->e, N);

	/* Sin ciclos nuevos la condicion del corte no puede haberse vuelto
	   cierta: se saltea, igual que en las sondas (la rareza de mov3). */
	if (ciclos)
		gen_corte(g, pc_sig);
}

/* SR.T = cc. Vive en el bit 0 de un campo de bits, asi que se toca el byte y
   no el registro: escribir SR entero pisaria S y el IMASK. */
static void gen_poner_t(jit_gen * g, x64_cond cc)
{
	jit_x64_setcc(&g->e, cc, X64_RAX);
	jit_x64_and_mi8(&g->e, CTX, O_SR, 0xFE);
	jit_x64_or_mr8(&g->e, CTX, O_SR, X64_RAX);
}

/*
	El volcado que hace reejecutable a la instruccion en curso: los registros
	cacheados, los ciclos acumulados hasta la anterior, el PC de la que va a
	ejecutar, y el contador **contando el intento que viene** -- igual que
	run(), que cuenta antes de despachar --, porque si el acceso falta el
	longjmp sale por encima del epilogo.
*/
static void gen_sync(jit_gen * g, DWORD pc_k)
{
	gen_volcar_regs(g);
	jit_x64_mov_mi(&g->e, CTX, O_PC, pc_k);
	jit_x64_inc_r(&g->e, N);
	gen_volcar_cuenta(g);
}

static void gen_epilogo(jit_gen * g)
{
	int i;

	for (i = 0; i < g->n_salidas; i++)
		jit_x64_fijar(&g->e, g->salidas[i]);

	gen_volcar_regs(g);
	jit_x64_jmp_a(&g->e, jit_tramp_salir);
}

/* ------------------------------------------------------------------------ */
/* El bloque de Crazy Taxi: el lazo de espera de 0c1583f8                    */
/* ------------------------------------------------------------------------ */

/* Las 20 palabras del tramo, mas las dos del callback. Ver fusion.c. */
static const WORD jit_ct_palabras[20] =
{
	0xD321, 0x6232, 0x420B, 0x5431, 0xD120, 0x6312, 0xD020, 0x6202,
	0xD11B, 0x323C, 0x7201, 0x6312, 0x3326, 0x8B01, 0xA004, 0x6CE3,
	0xD318, 0x6232, 0x2228, 0x8BEB,
};

static const DWORD jit_ct_extra_dir[2]     = { 0x0C156C30ul, 0x0C156C32ul };
static const WORD  jit_ct_extra_palabra[2] = { 0x000B, 0x0009 };

/* MOV.L @dir_constante, Rn. La direccion del literal de PC-relativo es una
   constante del bloque, que es uno de los dos pliegues que el plan permite sin
   IR (el *valor* se sigue leyendo: el literal es dato). */
static void gen_ct_leer_const(jit_gen * g, DWORD dir, int rn)
{
	jit_x64_mov_ri(&g->e, X64_RCX, dir);
	gen_leer32(g, A(rn), JIT_ACC_PLANO);
}

/* MOV.L @Rm, Rn, con la guarda de alineacion: una direccion desalineada
   vuelve al interprete ANTES de tocar nada, que es donde tiene que levantar
   su error de direccion por el camino de siempre. */
static void gen_ct_leer_ind(jit_gen * g, int rm, int rn, DWORD pc_esta)
{
	x64_parche ok;

	jit_x64_test_ri(&g->e, A(rm), 3);
	ok = jit_x64_jcc_corto(&g->e, X64_E);
	gen_salir_en(g, pc_esta);
	jit_x64_fijar(&g->e, ok);

	jit_x64_mov_rr(&g->e, X64_RCX, A(rm));
	gen_leer32(g, A(rn), JIT_ACC_PLANO);
}

static void gen_bloque_ct(jit_gen * g)
{
	unsigned char * por_vuelta;
	x64_parche      ok, no_t;

	gen_prologo(g);

	por_vuelta = jit_x64_aqui(&g->e);

	/* 0c1583f8: MOV.L @(c158480), R3 */
	gen_ct_leer_const(g, 0x0C158480ul, 3);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C1583FAul);

	/* 0c1583fa: MOV.L @R3, R2 */
	gen_ct_leer_ind(g, 3, 2, 0x0C1583FAul);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C1583FCul);

	/* 0c1583fc: JSR @R2 + ranura MOV.L @(4,R3), R4. Solo si el destino es el
	   RTS conocido; cualquier otro callback vuelve al interprete antes de
	   ejecutar nada del salto. La ranura comparte la alineacion de R3, ya
	   probada arriba. */
	jit_x64_cmp_ri(&g->e, A(2), (int) 0x0C156C30ul);
	ok = jit_x64_jcc_corto(&g->e, X64_E);
	gen_salir_en(g, 0x0C1583FCul);
	jit_x64_fijar(&g->e, ok);

	jit_x64_mov_mi(&g->e, CTX, O_PR, 0x0C158400ul);
	jit_x64_mov_rr(&g->e, X64_RCX, A(3));
	jit_x64_add_ri(&g->e, X64_RCX, 4);
	gen_leer32(g, A(4), JIT_ACC_PLANO);
	jit_x64_add_ri(&g->e, CYC, 3 + 1);
	jit_x64_add_ri(&g->e, N, 2);
	gen_corte(g, 0x0C156C30ul);

	/* 0c156c30: RTS + ranura NOP (que no suma ciclos: es el manejador nop). */
	jit_x64_add_ri(&g->e, CYC, 3);
	jit_x64_add_ri(&g->e, N, 2);
	gen_corte(g, 0x0C158400ul);

	/* 0c158400: MOV.L @(c158484), R1 */
	gen_ct_leer_const(g, 0x0C158484ul, 1);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C158402ul);

	/* 0c158402: MOV.L @R1, R3 */
	gen_ct_leer_ind(g, 1, 3, 0x0C158402ul);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C158404ul);

	/* 0c158404: MOV.L @(c158488), R0 */
	gen_ct_leer_const(g, 0x0C158488ul, 0);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C158406ul);

	/* 0c158406: MOV.L @R0, R2 */
	gen_ct_leer_ind(g, 0, 2, 0x0C158406ul);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C158408ul);

	/* 0c158408: MOV.L @(c158478), R1 */
	gen_ct_leer_const(g, 0x0C158478ul, 1);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C15840Aul);

	/* 0c15840a: ADD R3, R2 */
	jit_x64_add_rr(&g->e, A(2), A(3));
	gen_fin_alu(g, 1, 0x0C15840Cul);

	/* 0c15840c: ADD #1, R2 */
	jit_x64_add_ri(&g->e, A(2), 1);
	gen_fin_alu(g, 1, 0x0C15840Eul);

	/* 0c15840e: MOV.L @R1, R3 */
	gen_ct_leer_ind(g, 1, 3, 0x0C15840Eul);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C158410ul);

	/* 0c158410: CMP/HI R2, R3 -- T = R3 > R2 sin signo. */
	jit_x64_cmp_rr(&g->e, A(3), A(2));
	gen_poner_t(g, X64_A);
	gen_fin_alu(g, 1, 0x0C158412ul);

	/* 0c158412: BF c158418 -- con T puesto sigue en 0c158414, que ya no es
	   nuestro (el BRA de salida). */
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	no_t = jit_x64_jcc_corto(&g->e, X64_E);
	gen_salir_en(g, 0x0C158414ul);
	jit_x64_fijar(&g->e, no_t);
	gen_corte(g, 0x0C158418ul);

	/* 0c158418: MOV.L @(c15847c), R3 */
	gen_ct_leer_const(g, 0x0C15847Cul, 3);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C15841Aul);

	/* 0c15841a: MOV.L @R3, R2 */
	gen_ct_leer_ind(g, 3, 2, 0x0C15841Aul);
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	gen_corte(g, 0x0C15841Cul);

	/* 0c15841c: TST R2, R2 */
	jit_x64_cmp_ri(&g->e, A(2), 0);
	gen_poner_t(g, X64_E);
	gen_fin_alu(g, 1, 0x0C15841Eul);

	/* 0c15841e: BF c1583f8 -- con T puesto el lazo termina en 0c158420. */
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	no_t = jit_x64_jcc_corto(&g->e, X64_E);
	gen_salir_en(g, 0x0C158420ul);
	jit_x64_fijar(&g->e, no_t);
	gen_corte(g, JIT_CT_ENTRADA);

	jit_x64_jmp_a(&g->e, por_vuelta);

	gen_epilogo(g);
}

/* ------------------------------------------------------------------------ */
/* El bloque con MMU: el blit de columnas de DOOM, en 0002ef3e               */
/* ------------------------------------------------------------------------ */

static const WORD jit_ce_palabras[17] =
{
	0x6173, 0x63B2, 0x4129, 0x6282, 0x601F, 0xC97F, 0x033C, 0x603C,
	0x022C, 0x379C, 0x2420, 0x61A2, 0x341C, 0x6653, 0x2668, 0x8FEF,
	0x75FF,
};

static void gen_bloque_ce(jit_gen * g)
{
	unsigned char * vuelta;
	x64_parche      toma;

	gen_prologo(g);

	vuelta = jit_x64_aqui(&g->e);

	/* 0002ef3e: MOV R7, R1 -- mov3 no suma ciclos, y sin ciclos nuevos la
	   condicion del corte no puede haberse vuelto cierta: se saltea. */
	jit_x64_mov_rm(&g->e, A(1), CTX, O_R(7));
	jit_x64_inc_r(&g->e, N);

	/* 0002ef40: MOV.L @R11, R3 */
	gen_sync(g, 0x0002EF40ul);
	jit_x64_mov_rm(&g->e, X64_RCX, CTX, O_R(11));
	gen_leer32(g, A(3), JIT_ACC_MMU);
	jit_x64_add_ri(&g->e, CYC, 2);
	gen_corte(g, 0x0002EF42ul);

	/* 0002ef42: SHLR16 R1 */
	jit_x64_shr_ri(&g->e, A(1), 16);
	gen_fin_alu(g, 1, 0x0002EF44ul);

	/* 0002ef44: MOV.L @R8, R2 */
	gen_sync(g, 0x0002EF44ul);
	jit_x64_mov_rm(&g->e, X64_RCX, CTX, O_R(8));
	gen_leer32(g, A(2), JIT_ACC_MMU);
	jit_x64_add_ri(&g->e, CYC, 2);
	gen_corte(g, 0x0002EF46ul);

	/* 0002ef46: EXTS.W R1, R0 */
	jit_x64_movsx_w(&g->e, A(0), A(1));
	gen_fin_alu(g, 1, 0x0002EF48ul);

	/* 0002ef48: AND #7f, R0 */
	jit_x64_and_ri(&g->e, A(0), 0x7F);
	gen_fin_alu(g, 1, 0x0002EF4Aul);

	/* 0002ef4a: MOV.B @(R0, R3), R3 -- extiende signo. */
	gen_sync(g, 0x0002EF4Aul);
	jit_x64_mov_rr(&g->e, X64_RCX, A(0));
	jit_x64_add_rr(&g->e, X64_RCX, A(3));
	gen_leer8s(g, A(3), JIT_ACC_MMU);
	jit_x64_add_ri(&g->e, CYC, 2);
	gen_corte(g, 0x0002EF4Cul);

	/* 0002ef4c: EXTU.B R3, R0 */
	jit_x64_movzx_b(&g->e, A(0), A(3));
	gen_fin_alu(g, 1, 0x0002EF4Eul);

	/* 0002ef4e: MOV.B @(R0, R2), R2 */
	gen_sync(g, 0x0002EF4Eul);
	jit_x64_mov_rr(&g->e, X64_RCX, A(0));
	jit_x64_add_rr(&g->e, X64_RCX, A(2));
	gen_leer8s(g, A(2), JIT_ACC_MMU);
	jit_x64_add_ri(&g->e, CYC, 2);
	gen_corte(g, 0x0002EF50ul);

	/* 0002ef50: ADD R9, R7 */
	jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(9));
	jit_x64_add_mr(&g->e, CTX, O_R(7), X64_RAX);
	gen_fin_alu(g, 1, 0x0002EF52ul);

	/* 0002ef52: MOV.B R2, @R4 -- la escritura al framebuffer: puede levantar
	   la primera escritura de la pagina, y por eso el volcado va antes. */
	gen_sync(g, 0x0002EF52ul);
	jit_x64_mov_rr(&g->e, X64_RCX, A(4));
	gen_escribir8_reg(g, A(2), JIT_ACC_MMU);
	jit_x64_add_ri(&g->e, CYC, 2);
	gen_corte(g, 0x0002EF54ul);

	/* 0002ef54: MOV.L @R10, R1 */
	gen_sync(g, 0x0002EF54ul);
	jit_x64_mov_rm(&g->e, X64_RCX, CTX, O_R(10));
	gen_leer32(g, A(1), JIT_ACC_MMU);
	jit_x64_add_ri(&g->e, CYC, 2);
	gen_corte(g, 0x0002EF56ul);

	/* 0002ef56: ADD R1, R4 */
	jit_x64_add_rr(&g->e, A(4), A(1));
	gen_fin_alu(g, 1, 0x0002EF58ul);

	/* 0002ef58: MOV R5, R6 -- mov3 otra vez: sin ciclos, sin corte. */
	jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(5));
	jit_x64_mov_mr(&g->e, CTX, O_R(6), X64_RAX);
	jit_x64_inc_r(&g->e, N);

	/* 0002ef5a: TST R6, R6 -- el valor recien guardado sigue en EAX, asi que
	   se compara ahi y no releyendo la posicion que se acaba de escribir. */
	jit_x64_cmp_ri(&g->e, X64_RAX, 0);
	gen_poner_t(g, X64_E);
	gen_fin_alu(g, 1, 0x0002EF5Cul);

	/* 0002ef5c: BF/S 2ef3e -- la ranura (ADD #ff, R5) solo corre si toma; al
	   caer, 0002ef5e se ejecuta despues como instruccion normal, ya del
	   interprete. */
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	toma = jit_x64_jcc_corto(&g->e, X64_E);
	gen_salir_en(g, 0x0002EF5Eul);
	jit_x64_fijar(&g->e, toma);

	/* la ranura: ADD #-1, R5 */
	jit_x64_add_mi(&g->e, CTX, O_R(5), -1);
	gen_fin_alu(g, 1, JIT_CE_ENTRADA);

	jit_x64_jmp_a(&g->e, vuelta);

	gen_epilogo(g);
}


/* ======================================================================== */
/* El traductor automatico (fase 1 de docs/recompilador-plan.md)            */
/* ======================================================================== */

/*
	Traduce por **identidad de manejador**: cada palabra se resuelve por
	OP_HANDLER(oplist, instr) -- la expansion real de opcodes[] -- y se emite
	con la plantilla de ESE manejador, con los ciclos copiados de su cuerpo. No
	hay un segundo decodificador que pueda divergir del primero, y una fila mal
	puesta en la tabla es una plantilla que no se usa, nunca una instruccion mal
	decodificada. Una palabra sin plantilla termina el bloque y se anota en el
	censo: esa lista es la que dice cual escribir despues.

	Se enciende con DCEMU_JIT=2. Con DCEMU_JIT=1 corren los dos bloques escritos
	a mano de la fase 0, que siguen siendo el oraculo en el mismo binario.
*/

static void jit_marcar(DWORD pc);
static void jit_desmarcar(DWORD pc);

typedef struct jit_traduccion jit_traduccion;

static void gen_salir_enlazable(jit_gen * g, jit_traduccion * t, DWORD pc_sig);
static void gen_salir_dinamico(jit_gen * g, jit_traduccion * t);

#define JIT_SLOTS		5

/* El bloque no cruza una frontera de 1 KB, que es la pagina mas chica del
   SH-4: asi un solo puntero de busqueda cubre todas sus palabras y la
   verificacion por entrada no puede leer de otra pagina, sea cual sea el
   tamano con el que el guest la haya mapeado. */
#define JIT_LIMITE_PAG	0x400u

typedef struct jit_plantilla jit_plantilla;

struct jit_traduccion
{
	DWORD					pc0;
	WORD					palabra[JIT_MAX_INSTR];
	const jit_plantilla *	pl[JIT_MAX_INSTR];
	int						n;
	int						modo;			/* JIT_ACC_PLANO o JIT_ACC_MMU */

	signed char				slot[16];		/* indice en jit_a[], o -1 */

	unsigned char *			etiqueta[JIT_MAX_INSTR];
	x64_parche				adelante[JIT_MAX_INSTR];
	int						adelante_i[JIT_MAX_INSTR];
	int						n_adelante;
};

typedef void (* jit_emitir_f)(jit_gen * g, jit_traduccion * t, int i);

struct jit_plantilla
{
	opcode_f *		f;
	const char *	nombre;
	int				ciclos;
	unsigned char	accede;		/* toca memoria: hay que sincronizar antes */
	unsigned char	rama;		/* decide el flujo por su cuenta */
	unsigned char	ranura;		/* lleva ranura de retardo */
	jit_emitir_f	emitir;
};

#define TN(w)	(((w) >> 8) & 0x0F)
#define TM(w)	(((w) >> 4) & 0x0F)

/* ------------------------------------------------------------------------ */
/* Acceso a los registros del guest, cacheados o no                         */
/* ------------------------------------------------------------------------ */

static int tr_h(const jit_traduccion * t, int n)
{
	return (t->slot[n] < 0) ? -1 : (int) jit_a[t->slot[n]];
}

static void tr_cargar(jit_gen * g, jit_traduccion * t, x64_reg dst, int n)
{
	int hn = tr_h(t, n);

	if (hn < 0)
		jit_x64_mov_rm(&g->e, dst, CTX, O_R(n));
	else if ((x64_reg) hn != dst)
		jit_x64_mov_rr(&g->e, dst, (x64_reg) hn);
}

static void tr_mover(jit_gen * g, jit_traduccion * t, int n, int m)
{
	int hn = tr_h(t, n);

	if (hn >= 0)
		tr_cargar(g, t, (x64_reg) hn, m);
	else
	{
		tr_cargar(g, t, X64_RAX, m);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

/* R(n) op= R(m). Las tres formas de x86 evitan el rodeo por RAX cuando alguno
   de los dos esta cacheado, que es el caso normal. */
static void tr_alu_rr(jit_gen * g, jit_traduccion * t, x64_alu op, int n, int m)
{
	int hn = tr_h(t, n), hm = tr_h(t, m);

	if (hn >= 0 && hm >= 0)
		jit_x64_alu_rr(&g->e, op, (x64_reg) hn, (x64_reg) hm);
	else if (hn >= 0)
		jit_x64_alu_rm(&g->e, op, (x64_reg) hn, CTX, O_R(m));
	else
	{
		tr_cargar(g, t, X64_RAX, m);
		jit_x64_alu_mr(&g->e, op, CTX, O_R(n), X64_RAX);
	}
}

static void tr_alu_ri(jit_gen * g, jit_traduccion * t, x64_alu op, int n, int imm)
{
	int hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_alu_ri(&g->e, op, (x64_reg) hn, imm);
	else
		jit_x64_alu_mi(&g->e, op, CTX, O_R(n), imm);
}

static void tr_shift(jit_gen * g, jit_traduccion * t, x64_shift op, int n, int c)
{
	int hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_shift_ri(&g->e, op, (x64_reg) hn, c);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(n));
		jit_x64_shift_ri(&g->e, op, X64_RAX, c);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

/* ECX op= R(m), para componer una direccion. */
static void tr_ecx_alu(jit_gen * g, jit_traduccion * t, x64_alu op, int m)
{
	int hm = tr_h(t, m);

	if (hm >= 0)
		jit_x64_alu_rr(&g->e, op, X64_RCX, (x64_reg) hm);
	else
		jit_x64_alu_rm(&g->e, op, X64_RCX, CTX, O_R(m));
}

/* El destino de una lectura, cacheado o no. */
static void tr_leer_a(jit_gen * g, jit_traduccion * t, int n, int ancho)
{
	int		hn  = tr_h(t, n);
	x64_reg dst = (hn >= 0) ? (x64_reg) hn : X64_RAX;

	if (ancho == 4)
		gen_leer32(g, dst, t->modo);
	else if (ancho == 2)
		gen_leer16s(g, dst, t->modo);
	else
		gen_leer8s(g, dst, t->modo);

	if (hn < 0)
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
}

/* El valor de una escritura: R(m) del bloque en curso. */
typedef struct { jit_traduccion * t; int n; } jit_valor_tr;

static void tr_valor(jit_gen * g, void * ctx, x64_reg dst)
{
	jit_valor_tr * v = (jit_valor_tr *) ctx;

	tr_cargar(g, v->t, dst, v->n);
}

static void tr_escribir_de(jit_gen * g, jit_traduccion * t, int m, int ancho)
{
	jit_valor_tr v;

	v.t = t;
	v.n = m;

	gen_escribir(g, t->modo, ancho, tr_valor, &v);
}

/* ------------------------------------------------------------------------ */
/* Las plantillas                                                           */
/* ------------------------------------------------------------------------ */

static void pl_nop(jit_gen * g, jit_traduccion * t, int i)
{
	(void) g; (void) t; (void) i;
}

static void pl_mov0(jit_gen * g, jit_traduccion * t, int i)		/* MOV #imm,Rn */
{
	WORD w   = t->palabra[i];
	int  n   = TN(w);
	int  hn  = tr_h(t, n);
	int  imm = (int) (signed char) (w & 0xFF);

	if (hn >= 0)
		jit_x64_mov_ri(&g->e, (x64_reg) hn, (unsigned) imm);
	else
		jit_x64_mov_mi(&g->e, CTX, O_R(n), (unsigned) imm);
}

static void pl_mov3(jit_gen * g, jit_traduccion * t, int i)		/* MOV Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_mover(g, t, TN(w), TM(w));
}

/* MOV.L @(disp,PC),Rn -- la direccion es constante del bloque, que es uno de
   los dos pliegues que el plan permite sin IR. El valor se sigue leyendo. */
static void pl_movl2(jit_gen * g, jit_traduccion * t, int i)
{
	WORD  w   = t->palabra[i];
	DWORD pc  = t->pc0 + (DWORD) (2 * i);
	DWORD dir = (DWORD) (w & 0xFF) * 4 + (pc & 0xFFFFFFFCul) + 4;

	jit_x64_mov_ri(&g->e, X64_RCX, dir);
	tr_leer_a(g, t, TN(w), 4);
}

static void pl_movl9(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L @Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	tr_leer_a(g, t, TN(w), 4);
}

static void pl_movl21(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L @(d,Rm),Rn */
{
	WORD w = t->palabra[i];
	int  d = (int) (w & 0x0F) * 4;

	tr_cargar(g, t, X64_RCX, TM(w));

	if (d)
		jit_x64_add_ri(&g->e, X64_RCX, d);

	tr_leer_a(g, t, TN(w), 4);
}

static void pl_movb7(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B @Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	tr_leer_a(g, t, TN(w), 1);
}

static void pl_movb25(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B @(R0,Rm),Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TM(w));
	tr_leer_a(g, t, TN(w), 1);
}

static void pl_movl6(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L Rm,@Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	tr_escribir_de(g, t, TM(w), 4);
}

static void pl_movb4(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B Rm,@Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	tr_escribir_de(g, t, TM(w), 1);
}

static void pl_add39(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_alu_rr(g, t, X64_ADD, TN(w), TM(w));
}

static void pl_add40(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_alu_ri(g, t, X64_ADD, TN(w), (int) (signed char) (w & 0xFF));
}

static void pl_and72(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_alu_rr(g, t, X64_AND, TN(w), TM(w));
}

static void pl_and73(jit_gen * g, jit_traduccion * t, int i)	/* AND #imm,R0 */
{
	tr_alu_ri(g, t, X64_AND, 0, (int) (t->palabra[i] & 0xFF));
}

static void pl_not75(jit_gen * g, jit_traduccion * t, int i)	/* NOT Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);

	if (hn >= 0)
	{
		tr_cargar(g, t, (x64_reg) hn, TM(w));
		jit_x64_not_r(&g->e, (x64_reg) hn);
	}
	else
	{
		tr_cargar(g, t, X64_RAX, TM(w));
		jit_x64_not_r(&g->e, X64_RAX);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

static void tr_cmp_t(jit_gen * g, jit_traduccion * t, int n, int m, x64_cond cc)
{
	int hn = tr_h(t, n), hm = tr_h(t, m);

	if (hn >= 0 && hm >= 0)
		jit_x64_cmp_rr(&g->e, (x64_reg) hn, (x64_reg) hm);
	else if (hn >= 0)
		jit_x64_cmp_rm(&g->e, (x64_reg) hn, CTX, O_R(m));
	else if (hm >= 0)
	{
		tr_cargar(g, t, X64_RAX, n);
		jit_x64_cmp_rr(&g->e, X64_RAX, (x64_reg) hm);
	}
	else
	{
		tr_cargar(g, t, X64_RAX, n);
		jit_x64_cmp_rm(&g->e, X64_RAX, CTX, O_R(m));
	}

	gen_poner_t(g, cc);
}

static void pl_cmpeq44(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cmp_t(g, t, TN(w), TM(w), X64_E);
}

static void pl_cmphs45(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cmp_t(g, t, TN(w), TM(w), X64_AE);
}

static void pl_cmpge46(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cmp_t(g, t, TN(w), TM(w), X64_GE);
}

static void pl_cmphi47(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cmp_t(g, t, TN(w), TM(w), X64_A);
}

static void pl_cmpgt48(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cmp_t(g, t, TN(w), TM(w), X64_G);
}

/* TST Rm,Rn: T = ((Rn & Rm) == 0). */
static void pl_tst80(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w  = t->palabra[i];
	int  n  = TN(w), m = TM(w);
	int  hn = tr_h(t, n), hm = tr_h(t, m);

	if (n == m)
	{
		if (hn >= 0)
			jit_x64_test_rr(&g->e, (x64_reg) hn, (x64_reg) hn);
		else
			jit_x64_cmp_mi(&g->e, CTX, O_R(n), 0);
	}
	else
	{
		tr_cargar(g, t, X64_RAX, n);

		if (hm >= 0)
			jit_x64_test_rr(&g->e, X64_RAX, (x64_reg) hm);
		else
		{
			jit_x64_alu_rm(&g->e, X64_AND, X64_RAX, CTX, O_R(m));
			jit_x64_test_rr(&g->e, X64_RAX, X64_RAX);
		}
	}

	gen_poner_t(g, X64_E);
}

static void pl_tst81(jit_gen * g, jit_traduccion * t, int i)	/* TST #imm,R0 */
{
	tr_cargar(g, t, X64_RAX, 0);
	jit_x64_test_ri(&g->e, X64_RAX, (int) (t->palabra[i] & 0xFF));
	gen_poner_t(g, X64_E);
}

static void pl_extsw59(jit_gen * g, jit_traduccion * t, int i)	/* EXTS.W Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);
	int  hm = tr_h(t, TM(w));

	if (hm < 0)
	{
		tr_cargar(g, t, X64_RAX, TM(w));
		hm = (int) X64_RAX;
	}

	if (hn >= 0)
		jit_x64_movsx_w(&g->e, (x64_reg) hn, (x64_reg) hm);
	else
	{
		jit_x64_movsx_w(&g->e, X64_RAX, (x64_reg) hm);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

static void pl_extub60(jit_gen * g, jit_traduccion * t, int i)	/* EXTU.B Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);
	int  hm = tr_h(t, TM(w));

	if (hm < 0)
	{
		tr_cargar(g, t, X64_RAX, TM(w));
		hm = (int) X64_RAX;
	}

	if (hn >= 0)
		jit_x64_movzx_b(&g->e, (x64_reg) hn, (x64_reg) hm);
	else
	{
		jit_x64_movzx_b(&g->e, X64_RAX, (x64_reg) hm);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

static void pl_shll2(jit_gen * g, jit_traduccion * t, int i)
{
	tr_shift(g, t, X64_SHL, TN(t->palabra[i]), 2);
}

/* SHLR2 enmascara con 0x3FFFFFFF despues del corrimiento, como su cuerpo. */
static void pl_shlr2(jit_gen * g, jit_traduccion * t, int i)
{
	int n = TN(t->palabra[i]);

	tr_shift(g, t, X64_SHR, n, 2);
	tr_alu_ri(g, t, X64_AND, n, 0x3FFFFFFF);
}

static void pl_shlr16(jit_gen * g, jit_traduccion * t, int i)
{
	int n = TN(t->palabra[i]);

	tr_shift(g, t, X64_SHR, n, 16);
	tr_alu_ri(g, t, X64_AND, n, 0x0000FFFF);
}

/* ------------------------------------------------------------------------ */
/* Las ramas                                                                */
/* ------------------------------------------------------------------------ */

/* Sigue en `dest`: si cae adentro del bloque se salta a su etiqueta (hacia
   atras ya emitida, hacia adelante por parche pendiente); si no, se sale al
   interprete. En los dos casos el corte va antes, con el PC de destino. */
static void tr_seguir_en(jit_gen * g, jit_traduccion * t, DWORD dest)
{
	if (dest >= t->pc0 && dest < t->pc0 + (DWORD) (2 * t->n)
		&& ((dest - t->pc0) & 1) == 0)
	{
		int j = (int) ((dest - t->pc0) / 2);

		gen_corte(g, dest);

		if (t->etiqueta[j] != NULL)
			jit_x64_jmp_a(&g->e, t->etiqueta[j]);
		else if (t->n_adelante < JIT_MAX_INSTR)
		{
			t->adelante_i[t->n_adelante] = j;
			t->adelante[t->n_adelante]   = jit_x64_jmp(&g->e);
			t->n_adelante++;
		}
		else
			g->e.desborde = 1;

		return;
	}

	gen_salir_enlazable(g, t, dest);
}

static DWORD tr_destino8(const jit_traduccion * t, int i)
{
	WORD  w  = t->palabra[i];
	DWORD pc = t->pc0 + (DWORD) (2 * i);

	return (DWORD) (int) ((int) (signed char) (w & 0xFF) * 2) + pc + 4;
}

/* BF: salta si T == 0. Los dos caminos llevan su corte con el PC por donde
   siguen, que es lo que hace que la entrega de interrupciones no se corra ni
   un ciclo. */
static void pl_bf(jit_gen * g, jit_traduccion * t, int i)
{
	x64_parche no_toma;

	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);

	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	no_toma = jit_x64_jcc(&g->e, X64_NE);
	tr_seguir_en(g, t, tr_destino8(t, i));
	jit_x64_fijar(&g->e, no_toma);

	gen_corte(g, t->pc0 + (DWORD) (2 * i + 2));
}

/* BF/S: si toma, corre la ranura y salta; si no toma **la ranura no corre** y
   la palabra siguiente se ejecuta despues como instruccion normal, que es la
   que el conductor emite a continuacion. La ranura queda emitida dos veces,
   una por camino. */
static void pl_bfs(jit_gen * g, jit_traduccion * t, int i)
{
	const jit_plantilla * ranura = t->pl[i + 1];
	x64_parche no_toma;

	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);

	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	no_toma = jit_x64_jcc(&g->e, X64_NE);

	ranura->emitir(g, t, i + 1);

	if (ranura->ciclos)
		jit_x64_add_ri(&g->e, CYC, ranura->ciclos);

	jit_x64_inc_r(&g->e, N);

	tr_seguir_en(g, t, tr_destino8(t, i));
	jit_x64_fijar(&g->e, no_toma);

	gen_corte(g, t->pc0 + (DWORD) (2 * i + 2));
}


/* --- las formas indexadas por R0 ---------------------------------------- */

static void pl_movl27(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L @(R0,Rm),Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TM(w));
	tr_leer_a(g, t, TN(w), 4);
}

static void pl_movl24(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L Rm,@(R0,Rn) */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TN(w));
	tr_escribir_de(g, t, TM(w), 4);
}

static void pl_movb22(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B Rm,@(R0,Rn) */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TN(w));
	tr_escribir_de(g, t, TM(w), 1);
}

/* --- post-incremento y pre-decremento ----------------------------------- */

/*
	@Rm+ : lee y **despues** incrementa, y no incrementa cuando n == m. Si la
	lectura falta, el contexto todavia tiene el R(m) viejo porque el volcado
	previo lo dejo asi -- la reejecucion sale exacta sin instantanea.
*/
static void tr_leer_mas(jit_gen * g, jit_traduccion * t, int i, int ancho)
{
	WORD w = t->palabra[i];
	int  n = TN(w), m = TM(w);

	tr_cargar(g, t, X64_RCX, m);
	tr_leer_a(g, t, n, ancho);

	if (n != m)
		tr_alu_ri(g, t, X64_ADD, m, ancho);
}

static void pl_movb13(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B @Rm+,Rn */
{
	tr_leer_mas(g, t, i, 1);
}

static void pl_movl15(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L @Rm+,Rn */
{
	tr_leer_mas(g, t, i, 4);
}

/*
	@-Rn : decrementa y escribe. El decremento se calcula en un registro suelto
	y **R(n) se compromete recien despues** de que la escritura volvio: si
	faltara, el contexto tiene que mostrar el R(n) de antes, y con R(n) en el
	contexto (no cacheado) decrementarlo antes lo arruinaria.
*/
static void pl_movl12(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L Rm,@-Rn */
{
	WORD w = t->palabra[i];
	int  n = TN(w);

	tr_cargar(g, t, X64_RCX, n);
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RCX, 4);
	tr_escribir_de(g, t, TM(w), 4);
	tr_alu_ri(g, t, X64_SUB, n, 4);
}

/* --- lo que el censo de Crazy Taxi pidio: el pushpop de PR y compania ---- */

/*
	STS.L PR,@-Rn (stsl168, 2 ciclos). El manejador decrementa R(n) antes de
	escribir y la instantanea del interprete repone; aca el contrato es que el
	contexto quede pre-instruccion ante una falta, asi que el decremento se
	calcula en RCX y R(n) se compromete despues de que la escritura volvio,
	como en pl_movl12. El valor sale del PR del contexto.
*/
static void tr_valor_pr(jit_gen * g, void * ctx, x64_reg dst)
{
	(void) ctx;
	jit_x64_mov_rm(&g->e, dst, CTX, O_PR);
}

static void pl_stsl168(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];
	int  n = TN(w);

	tr_cargar(g, t, X64_RCX, n);
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RCX, 4);
	gen_escribir(g, t->modo, 4, tr_valor_pr, NULL);
	tr_alu_ri(g, t, X64_SUB, n, 4);
}

/*
	LDS.L @Rm+,PR (ldsl135, que no suma ciclos -- no es un olvido de aca) y
	LDS.L @Rm+,MACL (ldsl134, 3). El destino no es un registro general: la
	lectura baja a RAX y de ahi al campo del contexto, y el incremento se
	compromete despues de que la lectura volvio.
*/
static void tr_ldsl_a(jit_gen * g, jit_traduccion * t, int i, int campo)
{
	WORD w = t->palabra[i];
	int  m = TN(w);		/* 0100mmmm: el registro va en los bits 8-11 */

	tr_cargar(g, t, X64_RCX, m);
	gen_leer32(g, X64_RAX, t->modo);
	jit_x64_mov_mr(&g->e, CTX, campo, X64_RAX);
	tr_alu_ri(g, t, X64_ADD, m, 4);
}

static void pl_ldsl135(jit_gen * g, jit_traduccion * t, int i)
{
	tr_ldsl_a(g, t, i, O_PR);
}

static void pl_ldsl134(jit_gen * g, jit_traduccion * t, int i)
{
	tr_ldsl_a(g, t, i, O_MACL);
}

static void pl_sts164(jit_gen * g, jit_traduccion * t, int i)	/* STS MACL,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_mov_rm(&g->e, (x64_reg) hn, CTX, O_MACL);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_MACL);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

static void pl_movt35(jit_gen * g, jit_traduccion * t, int i)	/* MOVT Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);
	x64_reg dst = (hn >= 0) ? (x64_reg) hn : X64_RAX;

	jit_x64_mov_rm(&g->e, dst, CTX, O_SR);
	jit_x64_alu_ri(&g->e, X64_AND, dst, 1);

	if (hn < 0)
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
}

static void pl_mull(jit_gen * g, jit_traduccion * t, int i)		/* MUL.L Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  hm = tr_h(t, TM(w));

	tr_cargar(g, t, X64_RAX, TN(w));

	if (hm >= 0)
		jit_x64_imul_rr(&g->e, X64_RAX, (x64_reg) hm);
	else
		jit_x64_imul_rm(&g->e, X64_RAX, CTX, O_R(TM(w)));

	jit_x64_mov_mr(&g->e, CTX, O_MACL, X64_RAX);
}

static void pl_cmppl50(jit_gen * g, jit_traduccion * t, int i)	/* CMP/PL Rn */
{
	WORD w  = t->palabra[i];
	int  hn = tr_h(t, TN(w));

	if (hn >= 0)
		jit_x64_alu_ri(&g->e, X64_CMP, (x64_reg) hn, 0);
	else
		jit_x64_alu_mi(&g->e, X64_CMP, CTX, O_R(TN(w)), 0);

	gen_poner_t(g, X64_G);
}

/*
	ROTCL (rotcl88, 1 ciclo): el T vigente entra por el acarreo --el bit 0 de
	SR sale con un SHR-- y x86 RCL por 1 hace exactamente la rotacion del
	SH-4, dejando el bit 31 viejo en CF. MOV no toca las banderas, asi que el
	camino sin slot puede cargar y guardar alrededor del RCL.
*/
static void pl_rotcl88(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);

	jit_x64_mov_rm(&g->e, X64_RDX, CTX, O_SR);
	jit_x64_shift_ri(&g->e, X64_SHR, X64_RDX, 1);

	if (hn >= 0)
		jit_x64_shift_ri(&g->e, X64_RCL, (x64_reg) hn, 1);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(n));
		jit_x64_shift_ri(&g->e, X64_RCL, X64_RAX, 1);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}

	gen_poner_t(g, X64_B);
}

static void tr_valor_macl(jit_gen * g, void * ctx, x64_reg dst)
{
	(void) ctx;
	jit_x64_mov_rm(&g->e, dst, CTX, O_MACL);
}

static void pl_stsl167(jit_gen * g, jit_traduccion * t, int i)	/* STS.L MACL,@-Rn */
{
	WORD w = t->palabra[i];
	int  n = TN(w);

	tr_cargar(g, t, X64_RCX, n);
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RCX, 4);
	gen_escribir(g, t->modo, 4, tr_valor_macl, NULL);
	tr_alu_ri(g, t, X64_SUB, n, 4);
}

static void pl_movl18(jit_gen * g, jit_traduccion * t, int i)	/* MOV.L Rm,@(d,Rn) */
{
	WORD w = t->palabra[i];
	int  d = (int) (w & 0x0F) * 4;

	tr_cargar(g, t, X64_RCX, TN(w));

	if (d)
		jit_x64_add_ri(&g->e, X64_RCX, d);

	tr_escribir_de(g, t, TM(w), 4);
}

static void pl_movb16(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B R0,@(d,Rn) */
{
	WORD w = t->palabra[i];
	int  d = (int) (w & 0x0F);

	tr_cargar(g, t, X64_RCX, TM(w));	/* aqui el registro va en los bits 4-7 */

	if (d)
		jit_x64_add_ri(&g->e, X64_RCX, d);

	tr_escribir_de(g, t, 0, 1);
}

static void pl_or76(jit_gen * g, jit_traduccion * t, int i)		/* OR Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_alu_rr(g, t, X64_OR, TN(w), TM(w));
}

static void pl_cmppz49(jit_gen * g, jit_traduccion * t, int i)	/* CMP/PZ Rn */
{
	WORD w  = t->palabra[i];
	int  hn = tr_h(t, TN(w));

	if (hn >= 0)
		jit_x64_alu_ri(&g->e, X64_CMP, (x64_reg) hn, 0);
	else
		jit_x64_alu_mi(&g->e, X64_CMP, CTX, O_R(TN(w)), 0);

	gen_poner_t(g, X64_GE);
}

static void pl_sub69(jit_gen * g, jit_traduccion * t, int i)	/* SUB Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_alu_rr(g, t, X64_SUB, TN(w), TM(w));
}

static void pl_movw8(jit_gen * g, jit_traduccion * t, int i)	/* MOV.W @Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	tr_leer_a(g, t, TN(w), 2);
}

/* MOV.W @(disp,PC),Rn -- como pl_movl2, la direccion es constante del bloque,
   y a diferencia del literal de 32 bits el PC no se enmascara. */
static void pl_movw1(jit_gen * g, jit_traduccion * t, int i)
{
	WORD  w   = t->palabra[i];
	DWORD pc  = t->pc0 + (DWORD) (2 * i);
	DWORD dir = (DWORD) (w & 0xFF) * 2 + pc + 4;

	jit_x64_mov_ri(&g->e, X64_RCX, dir);
	tr_leer_a(g, t, TN(w), 2);
}

/* --- aritmetica y corrimientos que el censo pidio ------------------------ */

static void pl_cmpeq43(jit_gen * g, jit_traduccion * t, int i)	/* CMP/EQ #imm,R0 */
{
	int imm = (int) (signed char) (t->palabra[i] & 0xFF);
	int h0  = tr_h(t, 0);

	if (h0 >= 0)
		jit_x64_alu_ri(&g->e, X64_CMP, (x64_reg) h0, imm);
	else
		jit_x64_alu_mi(&g->e, X64_CMP, CTX, O_R(0), imm);

	gen_poner_t(g, X64_E);
}

static void pl_extuw61(jit_gen * g, jit_traduccion * t, int i)	/* EXTU.W Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);
	int  hm = tr_h(t, TM(w));

	if (hm < 0)
	{
		tr_cargar(g, t, X64_RAX, TM(w));
		hm = (int) X64_RAX;
	}

	if (hn >= 0)
		jit_x64_movzx_w(&g->e, (x64_reg) hn, (x64_reg) hm);
	else
	{
		jit_x64_movzx_w(&g->e, X64_RAX, (x64_reg) hm);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

static void pl_extsb58(jit_gen * g, jit_traduccion * t, int i)	/* EXTS.B Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);
	int  hm = tr_h(t, TM(w));

	if (hm < 0)
	{
		tr_cargar(g, t, X64_RAX, TM(w));
		hm = (int) X64_RAX;
	}

	if (hn >= 0)
		jit_x64_movsx_b(&g->e, (x64_reg) hn, (x64_reg) hm);
	else
	{
		jit_x64_movsx_b(&g->e, X64_RAX, (x64_reg) hm);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

/* SHLL y SHLR de uno dejan el bit que sale en T; el corrimiento de x86 ya lo
   pone en CF, asi que el setcc sale del mismo flag. */
static void pl_shll94(jit_gen * g, jit_traduccion * t, int i)	/* SHLL Rn */
{
	int n  = TN(t->palabra[i]);
	int hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_shift_ri(&g->e, X64_SHL, (x64_reg) hn, 1);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(n));
		jit_x64_shift_ri(&g->e, X64_SHL, X64_RAX, 1);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}

	gen_poner_t(g, X64_B);		/* CF = el bit que salio */
}

static void pl_shlr95(jit_gen * g, jit_traduccion * t, int i)	/* SHLR Rn */
{
	int n  = TN(t->palabra[i]);
	int hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_shift_ri(&g->e, X64_SHR, (x64_reg) hn, 1);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(n));
		jit_x64_shift_ri(&g->e, X64_SHR, X64_RAX, 1);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}

	gen_poner_t(g, X64_B);
}

static void pl_shll8(jit_gen * g, jit_traduccion * t, int i)
{
	tr_shift(g, t, X64_SHL, TN(t->palabra[i]), 8);
}

static void pl_shlr8(jit_gen * g, jit_traduccion * t, int i)
{
	int n = TN(t->palabra[i]);

	tr_shift(g, t, X64_SHR, n, 8);
	tr_alu_ri(g, t, X64_AND, n, 0x00FFFFFF);
}

static void pl_shll16(jit_gen * g, jit_traduccion * t, int i)
{
	tr_shift(g, t, X64_SHL, TN(t->palabra[i]), 16);
}

/* --- las ramas que faltaban --------------------------------------------- */

/* BT: salta si T == 1. El espejo de BF. */
static void pl_bt104(jit_gen * g, jit_traduccion * t, int i)
{
	x64_parche no_toma;

	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);

	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	no_toma = jit_x64_jcc(&g->e, X64_E);
	tr_seguir_en(g, t, tr_destino8(t, i));
	jit_x64_fijar(&g->e, no_toma);

	gen_corte(g, t->pc0 + (DWORD) (2 * i + 2));
}

/* La ranura de una rama condicional, emitida adentro del camino que toma. */
static void tr_emitir_ranura(jit_gen * g, jit_traduccion * t, int i)
{
	const jit_plantilla * r = t->pl[i];

	r->emitir(g, t, i);

	if (r->ciclos)
		jit_x64_add_ri(&g->e, CYC, r->ciclos);

	jit_x64_inc_r(&g->e, N);
}

static void pl_bts105(jit_gen * g, jit_traduccion * t, int i)	/* BT/S */
{
	x64_parche no_toma;

	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);

	jit_x64_test_mi8(&g->e, CTX, O_SR, 1);
	no_toma = jit_x64_jcc(&g->e, X64_E);
	tr_emitir_ranura(g, t, i + 1);
	tr_seguir_en(g, t, tr_destino8(t, i));
	jit_x64_fijar(&g->e, no_toma);

	gen_corte(g, t->pc0 + (DWORD) (2 * i + 2));
}

static DWORD tr_destino12(const jit_traduccion * t, int i)
{
	WORD  w  = t->palabra[i];
	DWORD pc = t->pc0 + (DWORD) (2 * i);
	int   d  = (int) (w & 0x0FFF);

	if (d & 0x0800)
		d |= ~0x0FFF;

	return (DWORD) (d * 2) + pc + 4;
}

/* BRA / BSR: siempre saltan, con destino constante, asi que el salto puede
   plegarse como arista del bloque. */
static void pl_bra(jit_gen * g, jit_traduccion * t, int i)
{
	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	tr_emitir_ranura(g, t, i + 1);
	tr_seguir_en(g, t, tr_destino12(t, i));
}

static void pl_bsr108(jit_gen * g, jit_traduccion * t, int i)
{
	DWORD pc = t->pc0 + (DWORD) (2 * i);

	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	jit_x64_mov_mi(&g->e, CTX, O_PR, pc + 4);
	tr_emitir_ranura(g, t, i + 1);
	tr_seguir_en(g, t, tr_destino12(t, i));
}

/*
	JMP / JSR / RTS: el destino es dinamico, asi que el bloque termina siempre.
	**El destino se captura antes de la ranura** --que puede escribir el mismo
	registro-- y se guarda derecho en el PC del contexto, que hace de lugar
	seguro: ninguna plantilla de ranura lo toca, y ninguna puede acceder a
	memoria (tr_descubrir lo impide), asi que no hay sincronizacion que lo pise.
*/
static void tr_salto_dinamico(jit_gen * g, jit_traduccion * t, int i,
	int ciclos, int reg_destino, int guardar_pr)
{
	DWORD pc = t->pc0 + (DWORD) (2 * i);

	jit_x64_add_ri(&g->e, CYC, ciclos);
	jit_x64_inc_r(&g->e, N);

	if (reg_destino >= 0)
		tr_cargar(g, t, X64_RAX, reg_destino);
	else
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_PR);

	if (guardar_pr)
		jit_x64_mov_mi(&g->e, CTX, O_PR, pc + 4);

	jit_x64_mov_mr(&g->e, CTX, O_PC, X64_RAX);

	tr_emitir_ranura(g, t, i + 1);

	gen_salir_dinamico(g, t);
}

static void pl_jmp110(jit_gen * g, jit_traduccion * t, int i)
{
	tr_salto_dinamico(g, t, i, 3, TN(t->palabra[i]), 0);
}

static void pl_jsr111(jit_gen * g, jit_traduccion * t, int i)
{
	tr_salto_dinamico(g, t, i, 3, TN(t->palabra[i]), 1);
}

static void pl_rts112(jit_gen * g, jit_traduccion * t, int i)
{
	tr_salto_dinamico(g, t, i, 3, -1, 0);
}

/* BRAF/BSRF: como el salto dinamico pero el destino es R(n) + PC + 4, y se
   captura antes de la ranura por el mismo motivo. Los ciclos y el orden --PR
   antes de la ranura-- son los de braf/bsrf109. */
static void tr_salto_relativo(jit_gen * g, jit_traduccion * t, int i,
	int guardar_pr)
{
	DWORD pc = t->pc0 + (DWORD) (2 * i);

	jit_x64_add_ri(&g->e, CYC, 3);
	jit_x64_inc_r(&g->e, N);

	tr_cargar(g, t, X64_RAX, TN(t->palabra[i]));
	jit_x64_alu_ri(&g->e, X64_ADD, X64_RAX, (int) (pc + 4));

	if (guardar_pr)
		jit_x64_mov_mi(&g->e, CTX, O_PR, pc + 4);

	jit_x64_mov_mr(&g->e, CTX, O_PC, X64_RAX);

	tr_emitir_ranura(g, t, i + 1);

	gen_salir_dinamico(g, t);
}

static void pl_braf(jit_gen * g, jit_traduccion * t, int i)
{
	tr_salto_relativo(g, t, i, 0);
}

static void pl_bsrf109(jit_gen * g, jit_traduccion * t, int i)
{
	tr_salto_relativo(g, t, i, 1);
}

static void tr_prologo(jit_gen * g, jit_traduccion * t);

/*
	**La plantilla que llama al manejador real.** Para las instrucciones raras
	y complejas --DIV1 y su pareja de inicio, SHAD-- emitir la semantica a mano
	no paga: se sincroniza y se llama al manejador del interprete, que ES la
	semantica, igual que los ayudantes de memoria lo son de mem.h. El bloque
	sigue de largo en vez de cortarse, que es todo el punto.

	El contrato: el conductor ya sincronizo (accede=1: registros volcados, PC
	en la instruccion, el intento contado, ciclos en el contexto), el manejador
	suma sus propios ciclos ahi (la fila lleva 0) y avanza el PC del contexto
	(+2, que la proxima sincronizacion pisa). A la vuelta se recarga el reloj y
	los slots, porque el manejador pudo escribir cualquier registro. Y el corte
	del bloque periodico se emite aca, porque el conductor solo lo emite cuando
	la fila declara ciclos.

	**La lista blanca es estricta y el motivo es la falta.** Sin instantanea,
	el contrato del mundo emitido es que una falta deje el contexto
	pre-instruccion; un manejador que muta antes de poder fallar lo rompe.
	Entran manejadores que no toquen el PC mas alla del +2, ni SR.MD/RB (los
	bancos cambiarian bajo los slots), ni FPSCR (repuntaria oplist a mitad de
	bloque), y cuyas mutaciones vayan TODAS despues de su ultima posibilidad
	de falta. Los sin acceso a memoria lo cumplen gratis; MAC.L quedo afuera
	hasta que su manejador se reordeno --leia @Rn+, incrementaba, y recien
	entonces leia @Rm+: la segunda falta dejaba R(n) avanzado-- y hoy entra,
	con las dos lecturas antes de mutar y las mismas direcciones en el mismo
	orden.
*/
static void tr_manejador(jit_gen * g, jit_traduccion * t, int i, const void * f)
{
	jit_x64_mov_ri(&g->e, X64_RCX, (unsigned) t->palabra[i]);

	if (!jit_x64_call_directo(&g->e, f))
	{
		jit_x64_mov64_ri(&g->e, X64_RAX, (unsigned long long) (size_t) f);
		jit_x64_call_r(&g->e, X64_RAX);
	}

	jit_x64_mov_rm(&g->e, CYC, CTX, O_CYC);
	tr_prologo(g, t);

	if (i + 1 < t->n)
		gen_corte(g, t->pc0 + (DWORD) (2 * i) + 2);
}

static void pl_div1s52(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) div1s52);
}

static void pl_div0s53(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) div0s53);
}

static void pl_div0u54(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) div0u54);
}

static void pl_shad90(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) shad90);
}

static void pl_shld93(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) shld93);
}

static void pl_macl62(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) macl62);
}

static void pl_or77(jit_gen * g, jit_traduccion * t, int i)		/* OR #imm,R0 */
{
	int imm = (int) (t->palabra[i] & 0xFF);
	int h0  = tr_h(t, 0);

	if (h0 >= 0)
		jit_x64_alu_ri(&g->e, X64_OR, (x64_reg) h0, imm);
	else
		jit_x64_alu_mi(&g->e, X64_OR, CTX, O_R(0), imm);
}

/* MOVA @(d,PC),R0: el resultado es una constante del bloque, con la formula
   exacta del manejador (disp*4 + ((PC+4) & ~3)). */
static void pl_mova34(jit_gen * g, jit_traduccion * t, int i)
{
	WORD  w   = t->palabra[i];
	DWORD pc  = t->pc0 + (DWORD) (2 * i);
	DWORD val = (DWORD) (w & 0xFF) * 4 + ((pc + 4) & 0xFFFFFFFCul);
	int   h0  = tr_h(t, 0);

	if (h0 >= 0)
		jit_x64_mov_ri(&g->e, (x64_reg) h0, val);
	else
		jit_x64_mov_mi(&g->e, CTX, O_R(0), val);
}

static void pl_movw23(jit_gen * g, jit_traduccion * t, int i)	/* MOV.W Rm,@(R0,Rn) */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TN(w));
	tr_escribir_de(g, t, TM(w), 2);
}

/* ------------------------------------------------------------------------ */
/* La tabla de plantillas                                                   */
/* ------------------------------------------------------------------------ */

/*
	Una fila por manejador, con los ciclos copiados de su cuerpo. La fila no
	repite la codificacion: el manejador se resuelve por OP_HANDLER() sobre la
	tabla real. Los manejadores van en un arreglo aparte y en el mismo orden
	para que la tabla se lea como una lista.
*/
static jit_plantilla jit_plantillas[] =
{
	{ NULL, "NOP",                0, 0, 0, 0, pl_nop },
	{ NULL, "MOV #imm,Rn",        1, 0, 0, 0, pl_mov0 },
	{ NULL, "MOV Rm,Rn",          0, 0, 0, 0, pl_mov3 },
	{ NULL, "MOV.L @(d,PC),Rn",   2, 1, 0, 0, pl_movl2 },
	{ NULL, "MOV.L @Rm,Rn",       2, 1, 0, 0, pl_movl9 },
	{ NULL, "MOV.L Rm,@Rn",       2, 1, 0, 0, pl_movl6 },
	{ NULL, "MOV.L @(d,Rm),Rn",   1, 1, 0, 0, pl_movl21 },
	{ NULL, "MOV.B @Rm,Rn",       2, 1, 0, 0, pl_movb7 },
	{ NULL, "MOV.B Rm,@Rn",       2, 1, 0, 0, pl_movb4 },
	{ NULL, "MOV.B @(R0,Rm),Rn",  2, 1, 0, 0, pl_movb25 },
	{ NULL, "ADD Rm,Rn",          1, 0, 0, 0, pl_add39 },
	{ NULL, "ADD #imm,Rn",        1, 0, 0, 0, pl_add40 },
	{ NULL, "AND Rm,Rn",          1, 0, 0, 0, pl_and72 },
	{ NULL, "AND #imm,R0",        1, 0, 0, 0, pl_and73 },
	{ NULL, "NOT Rm,Rn",          1, 0, 0, 0, pl_not75 },
	{ NULL, "TST Rm,Rn",          1, 0, 0, 0, pl_tst80 },
	{ NULL, "TST #imm,R0",        1, 0, 0, 0, pl_tst81 },
	{ NULL, "CMP/EQ Rm,Rn",       1, 0, 0, 0, pl_cmpeq44 },
	{ NULL, "CMP/HS Rm,Rn",       1, 0, 0, 0, pl_cmphs45 },
	{ NULL, "CMP/GE Rm,Rn",       1, 0, 0, 0, pl_cmpge46 },
	{ NULL, "CMP/HI Rm,Rn",       1, 0, 0, 0, pl_cmphi47 },
	{ NULL, "CMP/GT Rm,Rn",       1, 0, 0, 0, pl_cmpgt48 },
	{ NULL, "EXTS.W Rm,Rn",       1, 0, 0, 0, pl_extsw59 },
	{ NULL, "EXTU.B Rm,Rn",       1, 0, 0, 0, pl_extub60 },
	{ NULL, "SHLL2 Rn",           1, 0, 0, 0, pl_shll2 },
	{ NULL, "SHLR2 Rn",           1, 0, 0, 0, pl_shlr2 },
	{ NULL, "SHLR16 Rn",          1, 0, 0, 0, pl_shlr16 },
	{ NULL, "BF",                 2, 0, 1, 0, pl_bf },
	{ NULL, "BF/S",               2, 0, 1, 1, pl_bfs },
	{ NULL, "MOV.L @(R0,Rm),Rn",  2, 1, 0, 0, pl_movl27 },
	{ NULL, "MOV.L Rm,@(R0,Rn)",  2, 1, 0, 0, pl_movl24 },
	{ NULL, "MOV.B Rm,@(R0,Rn)",  2, 1, 0, 0, pl_movb22 },
	{ NULL, "MOV.B @Rm+,Rn",      1, 1, 0, 0, pl_movb13 },
	{ NULL, "MOV.L @Rm+,Rn",      1, 1, 0, 0, pl_movl15 },
	{ NULL, "MOV.L Rm,@-Rn",      1, 1, 0, 0, pl_movl12 },
	{ NULL, "CMP/EQ #imm,R0",     1, 0, 0, 0, pl_cmpeq43 },
	{ NULL, "EXTU.W Rm,Rn",       1, 0, 0, 0, pl_extuw61 },
	{ NULL, "EXTS.B Rm,Rn",       1, 0, 0, 0, pl_extsb58 },
	{ NULL, "SHLL Rn",            1, 0, 0, 0, pl_shll94 },
	{ NULL, "SHLR Rn",            1, 0, 0, 0, pl_shlr95 },
	{ NULL, "SHLL8 Rn",           1, 0, 0, 0, pl_shll8 },
	{ NULL, "SHLR8 Rn",           1, 0, 0, 0, pl_shlr8 },
	{ NULL, "SHLL16 Rn",          1, 0, 0, 0, pl_shll16 },
	{ NULL, "BT",                 2, 0, 1, 0, pl_bt104 },
	{ NULL, "BT/S",               2, 0, 1, 1, pl_bts105 },
	{ NULL, "BRA",                2, 0, 1, 1, pl_bra },
	{ NULL, "BSR",                2, 0, 1, 1, pl_bsr108 },
	{ NULL, "JMP @Rn",            3, 0, 1, 1, pl_jmp110 },
	{ NULL, "JSR @Rn",            3, 0, 1, 1, pl_jsr111 },
	{ NULL, "RTS",                3, 0, 1, 1, pl_rts112 },
	/* Lo que el censo de Crazy Taxi pidio (2026-08-08): el pushpop de PR corta
	   todo prologo y epilogo de funcion del guest. Ciclos copiados de cada
	   manejador; el 0 de LDS.L @Rm+,PR es del manejador, no un olvido. */
	{ NULL, "STS.L PR,@-Rn",      2, 1, 0, 0, pl_stsl168 },
	{ NULL, "LDS.L @Rm+,PR",      0, 1, 0, 0, pl_ldsl135 },
	{ NULL, "LDS.L @Rm+,MACL",    3, 1, 0, 0, pl_ldsl134 },
	{ NULL, "STS MACL,Rn",        3, 0, 0, 0, pl_sts164 },
	{ NULL, "MOVT Rn",            1, 0, 0, 0, pl_movt35 },
	{ NULL, "MUL.L Rm,Rn",        4, 0, 0, 0, pl_mull },
	{ NULL, "CMP/PL Rn",          1, 0, 0, 0, pl_cmppl50 },
	{ NULL, "ROTCL Rn",           1, 0, 0, 0, pl_rotcl88 },
	/* La segunda tanda del censo (2026-08-08, ya sin el pushpop de PR): la
	   escritura con desplazamiento que le faltaba a la pareja de movl21, el
	   SUB que nunca tuvo fila, y los MOV.W que pidieron el ayudante de 16
	   bits. Ciclos copiados de cada manejador. */
	{ NULL, "STS.L MACL,@-Rn",    3, 1, 0, 0, pl_stsl167 },
	{ NULL, "MOV.L Rm,@(d,Rn)",   1, 1, 0, 0, pl_movl18 },
	{ NULL, "MOV.B R0,@(d,Rn)",   1, 1, 0, 0, pl_movb16 },
	{ NULL, "OR Rm,Rn",           1, 0, 0, 0, pl_or76 },
	{ NULL, "CMP/PZ Rn",          1, 0, 0, 0, pl_cmppz49 },
	{ NULL, "SUB Rm,Rn",          1, 0, 0, 0, pl_sub69 },
	{ NULL, "MOV.W @Rm,Rn",       2, 1, 0, 0, pl_movw8 },
	{ NULL, "MOV.W @(d,PC),Rn",   2, 1, 0, 0, pl_movw1 },
	/* El tercer lote: la division por el manejador real, y los saltos
	   relativos por registro. Las filas de manejador llevan accede=1 (la
	   sincronizacion es el contrato) y ciclos 0 (los suma el manejador). */
	{ NULL, "DIV1 Rm,Rn",         0, 1, 0, 0, pl_div1s52 },
	{ NULL, "DIV0S Rm,Rn",        0, 1, 0, 0, pl_div0s53 },
	{ NULL, "DIV0U",              0, 1, 0, 0, pl_div0u54 },
	{ NULL, "SHAD Rm,Rn",         0, 1, 0, 0, pl_shad90 },
	{ NULL, "BRAF Rn",            3, 0, 1, 1, pl_braf },
	{ NULL, "BSRF Rn",            3, 0, 1, 1, pl_bsrf109 },
	/* El cuarto lote: MAC.L por el manejador reordenado, SHLD, y lo que el
	   censo listo tras el tercero. El 5 de OR #imm es del manejador. */
	{ NULL, "MAC.L @Rm+,@Rn+",    0, 1, 0, 0, pl_macl62 },
	{ NULL, "SHLD Rm,Rn",         0, 1, 0, 0, pl_shld93 },
	{ NULL, "OR #imm,R0",         5, 0, 0, 0, pl_or77 },
	{ NULL, "MOVA @(d,PC),R0",    1, 0, 0, 0, pl_mova34 },
	{ NULL, "MOV.W Rm,@(R0,Rn)",  2, 1, 0, 0, pl_movw23 },
};

#define JIT_N_PLANTILLAS \
	((int) (sizeof(jit_plantillas) / sizeof(jit_plantillas[0])))

static opcode_f * const jit_manejadores[JIT_N_PLANTILLAS] =
{
	nop, mov0, mov3, movl2, movl9, movl6, movl21, movb7, movb4, movb25,
	add39, add40, and72, and73, not75, tst80, tst81,
	cmpeq44, cmphs45, cmpge46, cmphi47, cmpgt48, extsw59, extub60,
	shll2, shlr2, shlr16, bf, bfs,
	movl27, movl24, movb22, movb13, movl15, movl12,
	cmpeq43, extuw61, extsb58,
	shll94, shlr95, shll8, shlr8, shll16,
	bt104, bts105, bra, bsr108, jmp110, jsr111, rts112,
	stsl168, ldsl135, ldsl134, sts164, movt35, mull, cmppl50, rotcl88,
	stsl167, movl18, movb16, or76, cmppz49, sub69, movw8, movw1,
	div1s52, div0s53, div0u54, shad90, braf, bsrf109,
	macl62, shld93, or77, mova34, movw23,
};

/* Cuantas filas de la tabla estan en juego. DCEMU_JIT_PLANTILLAS=N la recorta
   para bisecar: una plantilla infiel se delata en la cuenta de instrucciones,
   pero la cuenta no dice cual, y probar de a una es la forma barata de
   averiguarlo. Por omision, todas. */
static int jit_n_activas = JIT_N_PLANTILLAS;

static const jit_plantilla * jit_plantilla_de(opcode_f * f)
{
	int i;

	for (i = 0; i < jit_n_activas; i++)
		if (jit_plantillas[i].f == f)
			return &jit_plantillas[i];

	return NULL;
}

/* ------------------------------------------------------------------------ */
/* El censo de lo que corta los bloques                                     */
/* ------------------------------------------------------------------------ */

/*
	Sin esto la cobertura degrada en silencio, que es lo que este arbol llama
	un tope callado. Cada palabra sin plantilla que termina un bloque se anota;
	el resumen lista las que mas cortaron, y esa lista es la que dice cual
	plantilla escribir despues en vez de adivinarlo.
*/
#define JIT_CENSO_N		48

static WORD					jit_censo_op[JIT_CENSO_N];
static unsigned long long	jit_censo_veces[JIT_CENSO_N];
static int					jit_censo_n = 0;

static void jit_censar(WORD instr)
{
	int i;

	for (i = 0; i < jit_censo_n; i++)
		if (jit_censo_op[i] == instr)
		{
			jit_censo_veces[i]++;
			return;
		}

	if (jit_censo_n < JIT_CENSO_N)
	{
		jit_censo_op[jit_censo_n]    = instr;
		jit_censo_veces[jit_censo_n] = 1;
		jit_censo_n++;
	}
}

/* ------------------------------------------------------------------------ */
/* Asignacion de registros                                                  */
/* ------------------------------------------------------------------------ */

/* Cuantas veces toca cada registro del guest. Es la cuenta barata: los campos
   n y m de cada palabra segun lo que su plantilla mira. Decide cuales cinco
   van a registros del anfitrion, que es lo que la fase 0 hizo a mano. */
static void tr_asignar_registros(jit_traduccion * t)
{
	int uso[16];
	int i, s;

	for (i = 0; i < 16; i++)
	{
		uso[i]     = 0;
		t->slot[i] = -1;
	}

	for (i = 0; i < t->n; i++)
	{
		WORD w = t->palabra[i];
		const jit_plantilla * p = t->pl[i];

		if (p->emitir == pl_jmp110 || p->emitir == pl_jsr111)
		{
			uso[TN(w)]++;
			continue;
		}

		if (p->rama || p->emitir == pl_nop)
			continue;

		if (p->emitir == pl_movl27 || p->emitir == pl_movl24
			|| p->emitir == pl_movb22)
			uso[0]++;

		if (p->emitir == pl_and73 || p->emitir == pl_tst81)
		{
			uso[0] += 2;
			continue;
		}

		if (p->emitir == pl_movb25)
			uso[0]++;

		uso[TN(w)]++;

		if (p->emitir != pl_mov0 && p->emitir != pl_movl2
			&& p->emitir != pl_add40 && p->emitir != pl_shll2
			&& p->emitir != pl_shlr2 && p->emitir != pl_shlr16)
			uso[TM(w)]++;
	}

	for (s = 0; s < JIT_SLOTS; s++)
	{
		int mejor = -1, mejor_uso = 0;

		for (i = 0; i < 16; i++)
			if (t->slot[i] < 0 && uso[i] > mejor_uso)
			{
				mejor     = i;
				mejor_uso = uso[i];
			}

		if (mejor < 0)
			break;

		t->slot[mejor] = (signed char) s;
	}
}

/* ------------------------------------------------------------------------ */
/* Prologo, sincronizacion y epilogo del traductor                          */
/* ------------------------------------------------------------------------ */

/* Como gen_prologo(): el marco es del trampolin, aqui solo se cargan los
   registros que este bloque mapea. Es tambien la entrada de un enlace. */
static void tr_prologo(jit_gen * g, jit_traduccion * t)
{
	int i;

	for (i = 0; i < 16; i++)
		if (t->slot[i] >= 0)
			jit_x64_mov_rm(&g->e, jit_a[t->slot[i]], CTX, O_R(i));
}

static void tr_volcar_regs(jit_gen * g, jit_traduccion * t)
{
	int i;

	for (i = 0; i < 16; i++)
		if (t->slot[i] >= 0)
			jit_x64_mov_mr(&g->e, CTX, O_R(i), jit_a[t->slot[i]]);

	jit_x64_mov_mr(&g->e, CTX, O_CYC, CYC);
}

static void tr_sync(jit_gen * g, jit_traduccion * t, DWORD pc_k)
{
	tr_volcar_regs(g, t);
	jit_x64_mov_mi(&g->e, CTX, O_PC, pc_k);
	jit_x64_inc_r(&g->e, N);
	gen_volcar_cuenta(g);
}

/*
	El talon del puente entre paginas, uno por salida enlazable de un bloque con
	MMU. Solo se llega aqui cuando el parche apunto sitio_jmp al talon en vez de
	al sucesor: la guarda de la clave ya paso y los registros ya estan volcados
	--el PC del contexto ya es el destino--, asi que falta volcar el contador,
	hacer la busqueda de verdad (jit_busqueda_puente, que puede no volver) y
	saltar al cuerpo del sucesor.

	Sin MMU no se emite: no hay busqueda que reproducir y el salto directo
	siempre vale. Sin parchear, su salto final cae en la salida comun -- aunque
	nadie deberia llegar sin parchear, porque sitio_jmp solo apunta aqui cuando
	el parche escribio los dos sitios juntos.
*/
/* Cuantas salidas quisieron un sitio de enlace y JIT_MAX_ENLACES ya no daba.
   Cuenta sitios al emitir, no cruces: es el diagnostico de si el tope de 12
   esta dejando cadenas sin atar, que hasta ahora nadie podia ver. */
static unsigned long long jit_enlaces_agotados = 0;

static void gen_talon_puente(jit_gen * g, jit_traduccion * t, jit_enlace * e)
{
	x64_parche fin;

	e->talon     = NULL;
	e->talon_jmp = NULL;

	if (t->modo != JIT_ACC_MMU)
		return;

	e->talon = jit_x64_aqui(&g->e);

	gen_volcar_cuenta(g);
	gen_llamar(g, (const void *) jit_busqueda_puente, D_BUSQUEDA);

	fin          = jit_x64_jmp(&g->e);
	e->talon_jmp = fin.sitio;

	jit_x64_fijar(&g->e, fin);
	jit_x64_jmp_a(&g->e, jit_tramp_salir);
}

/*
	Salida por un sucesor **constante**: la que puede encadenarse.

	El orden importa. El corte del bloque periodico va primero, porque si
	corresponde cortar hay que salir a main_loop pase lo que pase; despues la
	guarda de la epoca; y recien entonces el volcado, las sacadas de la pila y
	el salto.

	El salto es un **tail jump**: en ese punto rsp esta exactamente como al
	entrar al bloque, que es lo que el prologo del sucesor espera, y el epilogo
	del sucesor hara el `ret` que le corresponde a quien llamo. La pila queda
	balanceada y la informacion de desenrollado de cada bloque sigue
	describiendo su propio marco, sin cambiar nada de eso.
*/
static void gen_salir_enlazable(jit_gen * g, jit_traduccion * t, DWORD pc_sig)
{
	x64_parche sin_enlace[3];
	x64_parche fin;
	jit_enlace * e;
	int i;

	if (g->n_enlaces >= JIT_MAX_ENLACES)
	{
		jit_enlaces_agotados++;
		gen_salir_en(g, pc_sig);
		return;
	}

	jit_x64_mov_mi(&g->e, CTX, O_PC, pc_sig);

	jit_x64_cmp_ri(&g->e, CYC, RELOJ_GRANO);
	sin_enlace[0] = jit_x64_jcc(&g->e, X64_AE);
	jit_x64_cmp_mi(&g->e, CTX, D_REINTENTO, 0);
	sin_enlace[1] = jit_x64_jcc(&g->e, X64_NE);

	/* La guarda. El desplazamiento del cmp es lo que se parchea; `jit_nunca`
	   esta lejos del contexto, asi que se codifica como disp32 y son los
	   ultimos cuatro bytes emitidos. */
	jit_x64_mov_rm(&g->e, X64_RAX, CTX, D_EPOCA);
	jit_x64_cmp_rm32(&g->e, X64_RAX, CTX, D(&jit_nunca));

	e = &g->enlace[g->n_enlaces++];
	e->sitio_cmp = jit_x64_aqui(&g->e) - 4;
	e->pc        = pc_sig;

	sin_enlace[2] = jit_x64_jcc(&g->e, X64_NE);

	tr_volcar_regs(g, t);

	fin          = jit_x64_jmp(&g->e);
	e->sitio_jmp = fin.sitio;

	/* Sin parchear cae en la salida comun del trampolin. */
	jit_x64_fijar(&g->e, fin);
	jit_x64_jmp_a(&g->e, jit_tramp_salir);

	gen_talon_puente(g, t, e);

	for (i = 0; i < 3; i++)
		jit_x64_fijar(&g->e, sin_enlace[i]);

	/* Sin enlace: el PC ya esta puesto, solo hay que salir por el epilogo. */
	if (g->n_salidas < JIT_MAX_SALIDAS)
		g->salidas[g->n_salidas++] = jit_x64_jmp(&g->e);
	else
		g->e.desborde = 1;
}

/*
	Salida por un sucesor **dinamico**: la del JSR, el JMP y el RTS.

	Igual que la enlazable, con una guarda mas adelante: el PC calculado contra
	el destino que ese sitio vio la primera vez. El destino se aprende en
	tiempo de ejecucion --el despachador lo parchea cuando el bloque sale por
	aca-- y hasta entonces el inmediato vale 1, que ningun PC iguala porque
	todos son pares.

	El identificador del sitio se deja en `jit_ult_sitio` **solo por el camino
	que no enlaza**, que es exactamente cuando hay algo que aprender.
*/
static void gen_salir_dinamico(jit_gen * g, jit_traduccion * t)
{
	x64_parche sin_enlace[4];
	x64_parche fin;
	jit_enlace * e;
	int i;

	if (g->n_enlaces >= JIT_MAX_ENLACES)
	{
		jit_enlaces_agotados++;

		if (g->n_salidas < JIT_MAX_SALIDAS)
			g->salidas[g->n_salidas++] = jit_x64_jmp(&g->e);
		else
			g->e.desborde = 1;

		return;
	}

	e     = &g->enlace[g->n_enlaces];
	e->pc = 0;

	/* El destino quedo en el PC del contexto; la ranura pudo pisar EAX. */
	jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_PC);
	jit_x64_cmp_ri32(&g->e, X64_RAX, 1);
	e->sitio_pc = jit_x64_aqui(&g->e) - 4;
	sin_enlace[0] = jit_x64_jcc(&g->e, X64_NE);

	jit_x64_cmp_ri(&g->e, CYC, RELOJ_GRANO);
	sin_enlace[1] = jit_x64_jcc(&g->e, X64_AE);
	jit_x64_cmp_mi(&g->e, CTX, D_REINTENTO, 0);
	sin_enlace[2] = jit_x64_jcc(&g->e, X64_NE);

	jit_x64_mov_rm(&g->e, X64_RAX, CTX, D_EPOCA);
	jit_x64_cmp_rm32(&g->e, X64_RAX, CTX, D(&jit_nunca));
	e->sitio_cmp = jit_x64_aqui(&g->e) - 4;
	sin_enlace[3] = jit_x64_jcc(&g->e, X64_NE);

	tr_volcar_regs(g, t);

	fin          = jit_x64_jmp(&g->e);
	e->sitio_jmp = fin.sitio;
	jit_x64_fijar(&g->e, fin);
	jit_x64_jmp_a(&g->e, jit_tramp_salir);

	gen_talon_puente(g, t, e);

	for (i = 0; i < 4; i++)
		jit_x64_fijar(&g->e, sin_enlace[i]);

	/* Que el despachador sepa que sitio tiene que aprender. */
	jit_x64_mov_mi(&g->e, CTX, D_ULT_SITIO,
		(unsigned) (jit_n_bloques * JIT_MAX_ENLACES + g->n_enlaces));

	g->n_enlaces++;

	if (g->n_salidas < JIT_MAX_SALIDAS)
		g->salidas[g->n_salidas++] = jit_x64_jmp(&g->e);
	else
		g->e.desborde = 1;
}

static void tr_epilogo(jit_gen * g, jit_traduccion * t)
{
	int i;

	for (i = 0; i < g->n_salidas; i++)
		jit_x64_fijar(&g->e, g->salidas[i]);

	tr_volcar_regs(g, t);
	jit_x64_jmp_a(&g->e, jit_tramp_salir);
}


/* ------------------------------------------------------------------------ */
/* El conductor: descubrir, emitir, registrar                               */
/* ------------------------------------------------------------------------ */

static int					jit_traductor = 0;	/* DCEMU_JIT=2 */
static unsigned long long	jit_traducidos = 0;
static unsigned long long	jit_instr_bloque = 0;
static unsigned long long	jit_fallidos = 0;
static unsigned long long	jit_enlaces_atados = 0;
static unsigned long long	jit_enlaces_dinamicos = 0;

/*
	Descubrimiento: camina las palabras desde `pc` resolviendo cada una por
	OP_HANDLER() y parando cuando una no tiene plantilla, cuando se acaba la
	pagina de 1 KB o cuando se llega al tope de instrucciones.
*/
static int tr_descubrir(jit_traduccion * t, DWORD pc)
{
	const WORD *	codigo = (const WORD *) MMU_FETCH_PUNTERO(pc);
	unsigned		cabe   = (JIT_LIMITE_PAG - (pc & (JIT_LIMITE_PAG - 1))) / 2;
	int				max    = (int) (cabe < JIT_MAX_INSTR ? cabe : JIT_MAX_INSTR);
	int				i;

	t->pc0        = pc;
	t->n          = 0;
	t->n_adelante = 0;
	t->modo       = mmu_activa ? JIT_ACC_MMU : JIT_ACC_PLANO;

	for (i = 0; i < max; i++)
	{
		WORD		instr = codigo[i];
		opcode_f *	f     = OP_HANDLER(oplist, instr);
		const jit_plantilla * p = jit_plantilla_de(f);

		if (p == NULL)
		{
			jit_censar(instr);
			break;
		}

		t->palabra[i] = instr;
		t->pl[i]      = p;
		t->n          = i + 1;
	}

	/*
		La ranura de un BF/S se emite adentro del camino que toma, y ahi no hay
		sincronizacion previa: si tocara memoria, una falta saldria por longjmp
		con el PC de la rama y no el de la ranura, y el guest reejecutaria desde
		el lugar equivocado. **Costo 616 instrucciones de divergencia en la
		primera corrida del traductor**, con la captura y los cuadros iguales.
		Asi que un BF/S cuya ranura acceda a memoria --o sea otra rama, o no
		exista-- termina el bloque antes de el.
	*/
	for (i = 0; i < t->n; i++)
		if (t->pl[i]->ranura
			&& (i + 1 >= t->n || t->pl[i + 1]->accede || t->pl[i + 1]->rama))
		{
			t->n = i;
			break;
		}

	return t->n;
}

/*
	Emision. Cada instruccion lleva lo mismo que en los bloques escritos a
	mano: la sincronizacion previa si toca memoria, sus ciclos, su cuenta y su
	corte del bloque periodico en la frontera siguiente.
*/
static void tr_emitir_cuerpo(jit_gen * g, jit_traduccion * t)
{
	int i;

	tr_prologo(g, t);

	for (i = 0; i < t->n; i++)
	{
		const jit_plantilla * p = t->pl[i];
		DWORD pc_i   = t->pc0 + (DWORD) (2 * i);
		DWORD pc_sig = pc_i + 2;

		t->etiqueta[i] = jit_x64_aqui(&g->e);

		if (p->rama)
		{
			p->emitir(g, t, i);
			continue;
		}

		if (p->accede)
			tr_sync(g, t, pc_i);

		p->emitir(g, t, i);

		if (p->ciclos)
			jit_x64_add_ri(&g->e, CYC, p->ciclos);

		if (!p->accede)
			jit_x64_inc_r(&g->e, N);

		/* Sin ciclos nuevos la condicion del corte no pudo volverse cierta.
		   Y tras la ultima instruccion no hace falta: el bloque termina. */
		if (p->ciclos && i + 1 < t->n)
			gen_corte(g, pc_sig);
	}

	/* Los saltos internos hacia adelante, ahora que estan todas las etiquetas. */
	for (i = 0; i < t->n_adelante; i++)
	{
		unsigned char * destino = t->etiqueta[t->adelante_i[i]];
		unsigned char * fin     = jit_x64_aqui(&g->e);
		long long       rel;

		if (destino == NULL)
		{
			g->e.desborde = 1;
			continue;
		}

		rel = (long long) (destino - (t->adelante[i].sitio + 4));

		if (rel < -2147483647LL || rel > 2147483647LL)
		{
			g->e.desborde = 1;
			continue;
		}

		t->adelante[i].sitio[0] = (unsigned char) ((unsigned long long) rel & 0xFF);
		t->adelante[i].sitio[1] = (unsigned char) (((unsigned long long) rel >> 8) & 0xFF);
		t->adelante[i].sitio[2] = (unsigned char) (((unsigned long long) rel >> 16) & 0xFF);
		t->adelante[i].sitio[3] = (unsigned char) (((unsigned long long) rel >> 24) & 0xFF);

		(void) fin;
	}

	gen_salir_enlazable(g, t, t->pc0 + (DWORD) (2 * t->n));
	tr_epilogo(g, t);
}

/*
	Ata los enlaces del bloque nuevo con los que ya estaban: los suyos que
	apunten a un bloque existente, y los de cualquier otro que apuntaran a este.

	El recorrido es sobre todos los bloques y solo ocurre al **crear** uno --
	unos diez mil por corrida --, asi que su costo vive fuera del camino
	caliente.
*/
/* DCEMU_JIT_SIN_PUENTES=1: los enlaces entre paginas no se atan y el criterio
   vuelve a ser el de antes. Es la palanca de aislamiento del puente. */
static int jit_sin_puentes = 0;

static unsigned long long jit_puentes_atados = 0;

/*
	**Todos los sitios se escriben juntos o no se escribe ninguno.** El
	inmediato del destino, el desplazamiento de la guarda de epoca, el rel32
	del salto y el del talon describen un mismo enlace: si el destino se
	actualiza y el salto no, la guarda deja pasar un PC nuevo hacia el bloque
	viejo. Eso fue una divergencia de 815 millones de instrucciones y una
	captura distinta -- la primera de esta serie que se vio a simple vista.
*/
static void jit_parchear_enlace(jit_enlace * e, DWORD pc_fuente,
	const jit_bloque * destino)
{
	int          disp = D(&destino->epoca);
	unsigned char * salto;
	long long    rel, rel_talon = 0;
	int          puente;

	/*
		**Directo solo dentro de la misma ventana de 1 KB; el resto, por el
		talon.**

		El salto directo se saltea la busqueda de instruccion del despachador,
		que avanza URC cuando falla la pagina vigente. Saltearla solo es exacto
		cuando la busqueda habria acertado seguro -- y la unica medida de eso
		que no depende del mapeo vigente es la ventana de `JIT_LIMITE_PAG`
		(1 KB): la pagina mas chica del SH-4. Dos PC en la misma ventana estan
		en la misma pagina bajo **cualquier** tamano, para siempre.

		La version anterior media con `mmu_fetch_mascara`, la mascara de la
		pagina cacheada **en el momento de parchear**, con dos agujeros: en la
		direccion "los enlaces ajenos hacia el bloque nuevo" esa mascara es la
		del bloque nuevo y no la del que salta, y un parche hecho bajo un mapeo
		revive tras el cambio de epoca sin re-evaluarse -- la guarda del salto
		compara la clave, no el criterio con el que se parcheo. Con la ventana
		fija ninguna de las dos cosas puede pasar. WinCE mapea todo en paginas
		de 4 KB, asi que ninguno de los dos agujeros llego a morder; los cierra
		esta regla, no una correccion aparte.

		Todo lo que no cae en la ventana va por el talon, que hace la busqueda
		de verdad: exacto por construccion, URC y falta incluidos. Costo de no
		tener nada de esto: 7095 instrucciones de divergencia sobre 5433
		millones -- y la mitad de las salidas de DCDoom sin enlace.

		Sin MMU no hay busqueda que reproducir: directo siempre.
	*/
	{
		DWORD ventana = mmu_activa ? (JIT_LIMITE_PAG - 1u) : 0xFFFFFFFFul;

		puente = ((pc_fuente & ~ventana) != (destino->pc & ~ventana));
	}

	if (puente && (e->talon == NULL || jit_sin_puentes))
		return;

	salto = puente ? e->talon : (unsigned char *) (size_t) destino->codigo;
	rel   = (long long) (salto - (e->sitio_jmp + 4));

	if (!jit_disp_ok || rel < -2147483647LL || rel > 2147483647LL)
		return;

	if (e->talon != NULL)
	{
		rel_talon = (long long) ((const unsigned char *) destino->codigo
								 - (e->talon_jmp + 4));

		if (rel_talon < -2147483647LL || rel_talon > 2147483647LL)
			return;
	}

	/* El inmediato del destino, cuando el sitio es un salto indirecto. */
	if (e->sitio_pc != NULL)
	{
		e->sitio_pc[0] = (unsigned char) (destino->pc & 0xFF);
		e->sitio_pc[1] = (unsigned char) ((destino->pc >> 8) & 0xFF);
		e->sitio_pc[2] = (unsigned char) ((destino->pc >> 16) & 0xFF);
		e->sitio_pc[3] = (unsigned char) ((destino->pc >> 24) & 0xFF);
	}

	e->pc = destino->pc;

	e->sitio_cmp[0] = (unsigned char) ((unsigned) disp & 0xFF);
	e->sitio_cmp[1] = (unsigned char) (((unsigned) disp >> 8) & 0xFF);
	e->sitio_cmp[2] = (unsigned char) (((unsigned) disp >> 16) & 0xFF);
	e->sitio_cmp[3] = (unsigned char) (((unsigned) disp >> 24) & 0xFF);

	e->sitio_jmp[0] = (unsigned char) ((unsigned long long) rel & 0xFF);
	e->sitio_jmp[1] = (unsigned char) (((unsigned long long) rel >> 8) & 0xFF);
	e->sitio_jmp[2] = (unsigned char) (((unsigned long long) rel >> 16) & 0xFF);
	e->sitio_jmp[3] = (unsigned char) (((unsigned long long) rel >> 24) & 0xFF);

	/* El talon siempre apunta al cuerpo del sucesor vigente, aunque este
	   parche haya salido directo: sitio_jmp decide si se pasa por el, y un
	   reparcheo posterior puede cambiar de opinion sin dejar un talon rancio. */
	if (e->talon != NULL)
	{
		e->talon_jmp[0] = (unsigned char) ((unsigned long long) rel_talon & 0xFF);
		e->talon_jmp[1] = (unsigned char) (((unsigned long long) rel_talon >> 8) & 0xFF);
		e->talon_jmp[2] = (unsigned char) (((unsigned long long) rel_talon >> 16) & 0xFF);
		e->talon_jmp[3] = (unsigned char) (((unsigned long long) rel_talon >> 24) & 0xFF);
	}

	jit_enlaces_atados++;

	if (puente)
		jit_puentes_atados++;
}

static void jit_enlazar(jit_bloque * nuevo)
{
	int i, k;

	for (i = 0; i < nuevo->n_enlaces; i++)
	{
		const jit_bloque * d;

		if (nuevo->enlace[i].sitio_pc != NULL)
			continue;			/* dinamico: su destino se aprende corriendo */

		d = jit_buscar(nuevo->enlace[i].pc);

		if (d != NULL)
			jit_parchear_enlace(&nuevo->enlace[i], nuevo->pc, d);
	}

	for (k = 0; k < jit_n_bloques; k++)
	{
		jit_bloque * b = &jit_bloques[k];

		if (b == nuevo)
			continue;

		for (i = 0; i < b->n_enlaces; i++)
			if (b->enlace[i].sitio_pc == NULL
				&& b->enlace[i].pc == nuevo->pc)
				jit_parchear_enlace(&b->enlace[i], b->pc, nuevo);
	}
}

/*
	Le ensena a un sitio de salto indirecto el destino que acaba de tomar. Lo
	llama el despachador justo despues de que el bloque salio por ahi, que es
	el unico momento en que ese destino se conoce.

	El tope de parcheos existe por los sitios polimorficos: sin el, dos
	destinos que se alternan se turnarian para siempre reescribiendose el
	inmediato, que es trabajo puro.
*/
#define JIT_MAX_REPARCHEOS	4

/* DCEMU_JIT_SIN_INDIRECTOS=1: los sitios de salto indirecto no aprenden su
   destino. Es la palanca que separa "la guarda esta mal emitida" de "el
   parcheo esta mal hecho". */
static int jit_sin_indirectos = 0;

static void jit_aprender_destino(int sitio, DWORD destino)
{
	int bi = sitio / JIT_MAX_ENLACES;
	int k  = sitio % JIT_MAX_ENLACES;
	jit_bloque * b;
	jit_enlace * e;
	const jit_bloque * d;

	if (bi < 0 || bi >= jit_n_bloques || k < 0 || k >= JIT_MAX_ENLACES)
		return;

	b = &jit_bloques[bi];
	e = &b->enlace[k];

	if (jit_sin_indirectos == 1 || e->sitio_pc == NULL
		|| e->veces >= JIT_MAX_REPARCHEOS)
		return;

	d = jit_buscar(destino);

	if (d == NULL || d->pc != destino)
		return;

	/* Lo mismo que el despachador exige antes de correr un bloque, y que el
	   salto encadenado no comprueba: el modo con el que se emitio. */
	if (d->mmu >= 0 && d->mmu != (mmu_activa ? JIT_ACC_MMU : JIT_ACC_PLANO))
		return;

	e->veces++;

	if (jit_sin_indirectos != 2)
		jit_parchear_enlace(e, b->pc, d);

	jit_enlaces_dinamicos++;
}

/*
	Traduce el bloque que empieza en `pc` y lo registra. Devuelve el bloque o
	NULL; en cualquier caso el guest sigue corriendo, interpretado si no hubo
	traduccion, que es la degradacion que el plan promete.

	**El crecimiento hacia atras**: si la ultima instruccion es una rama cuyo
	destino cae antes del comienzo y en la misma pagina, se retraduce desde
	ahi. Es lo que corrige que el muestreo caiga en mitad de un lazo en vez de
	en su cabeza, y sin eso el lazo saldria del bloque en cada vuelta.
*/
static jit_bloque * tr_traducir(DWORD pc)
{
	static jit_traduccion	t;			/* 3 KB: no va a la pila */
	jit_gen					g;
	unsigned				disponible;
	jit_bloque *			b;
	int						intento;
	int						i;

	if (jit_n_bloques >= JIT_MAX_BLOQUES)
		return NULL;

	for (intento = 0; intento < 2; intento++)
	{
		if (tr_descubrir(&t, pc) == 0)
			return NULL;

		if (intento == 0)
		{
			const jit_plantilla * ult = t.pl[t.n - 1];
			DWORD dest;

			if (!ult->rama)
				break;

			dest = tr_destino8(&t, t.n - 1);

			if (dest < t.pc0
				&& (dest & ~(JIT_LIMITE_PAG - 1)) == (t.pc0 & ~(JIT_LIMITE_PAG - 1)))
			{
				pc = dest;
				continue;
			}
		}

		break;
	}

	/* Puede que la cabeza del lazo ya este traducida: no duplicarla. */
	{
		jit_bloque * ya = jit_buscar(t.pc0);

		if (ya != NULL)
			return ya;
	}

	tr_asignar_registros(&t);

	for (i = 0; i < JIT_MAX_INSTR; i++)
		t.etiqueta[i] = NULL;

	jit_codigo_us = (jit_codigo_us + 15u) & ~15u;

	if (jit_codigo_us >= jit_codigo_tam)
		return NULL;

	disponible = jit_codigo_tam - jit_codigo_us;

	memset(&g, 0, sizeof(g));
	jit_x64_iniciar(&g.e, jit_codigo + jit_codigo_us, disponible);

	tr_emitir_cuerpo(&g, &t);

	if (g.e.desborde || !jit_disp_ok)
	{
		jit_fallidos++;
		return NULL;
	}

	b = &jit_bloques[jit_n_bloques++];

	memset(b, 0, sizeof(*b));
	b->pc         = t.pc0;
	b->codigo     = (void (*)(void)) (jit_codigo + jit_codigo_us);
	b->n_palabras = t.n;
	b->mmu        = t.modo;

	memcpy(b->copia, t.palabra, (size_t) t.n * sizeof(WORD));
	b->palabras = b->copia;

	memcpy(b->enlace, g.enlace, sizeof(b->enlace));
	b->n_enlaces = g.n_enlaces;

	jit_registrar_marco(b, jit_x64_largo(&g.e));

	jit_codigo_us += jit_x64_largo(&g.e);

	/* La pagina del anfitrion donde vive este bloque queda vigilada: una
	   escritura ahi mueve la epoca y obliga a verificarlo entero otra vez. */
	jit_pag_codigo[JIT_PAG_BIT(MMU_FETCH_PUNTERO(b->pc))] = 1;

	b->epoca = 0;			/* todavia sin verificar */
	b->ptr   = NULL;

	jit_marcar(b->pc);

	jit_insertar(jit_n_bloques - 1);

	jit_traducidos++;
	jit_instr_bloque += (unsigned long long) t.n;

	jit_enlazar(b);

	return b;
}

/* ------------------------------------------------------------------------ */
/* El muestreo: de donde salen los candidatos                               */
/* ------------------------------------------------------------------------ */

/*
	El bloque periodico de main_loop() corre cada RELOJ_GRANO ciclos --unas 130
	instrucciones-- asi que muestrear ahi no cuesta nada en el camino caliente.
	Un PC visto JIT_CALOR veces se marca como candidato, y la proxima vez que
	el despacho lo vea se traduce. La salida de cada bloque siembra ademas el
	PC siguiente, que es una frontera de bloque de verdad y no una muestra.
*/
#define JIT_CALOR	8

static unsigned char jit_calor[0x10000];

void jit_muestrear(DWORD pc)
{
	unsigned idx;

	if (!jit_traductor)
		return;

	idx = (pc >> 1) & 0xFFFFu;

	if (jit_calor[idx] < JIT_CALOR)
	{
		jit_calor[idx]++;

		if (jit_calor[idx] == JIT_CALOR)
			jit_marcar(pc);
	}
}

/* ------------------------------------------------------------------------ */
/* Emision, registro y despacho                                             */
/* ------------------------------------------------------------------------ */

static void jit_marcar(DWORD pc)
{
	unsigned i = ((pc >> 1) & 0xFFFFu);

	jit_mapa[i >> 3] |= (unsigned char) (1u << (i & 7u));
}

static void jit_desmarcar(DWORD pc)
{
	unsigned i = ((pc >> 1) & 0xFFFFu);

	jit_mapa[i >> 3] &= (unsigned char) ~(1u << (i & 7u));
}

/*
	Emite un bloque en el arena y lo registra. Devuelve 0 si el emisor se
	quejo (desborde del buffer, un salto corto fuera de alcance, un
	desplazamiento que no cabe): el bloque entero se descarta y ese PC sigue
	interpretado, que es la degradacion que el plan promete.
*/
static int jit_emitir(DWORD pc, void (* generar)(jit_gen *),
	const WORD * palabras, int n_palabras,
	const DWORD * extra_dir, const WORD * extra_palabra, int n_extra)
{
	jit_gen		g;
	unsigned	disponible;
	jit_bloque * b;

	if (jit_n_bloques >= JIT_MAX_BLOQUES)
		return 0;

	/* Cada bloque arranca alineado a 16. */
	jit_codigo_us = (jit_codigo_us + 15u) & ~15u;

	if (jit_codigo_us >= jit_codigo_tam)
		return 0;

	disponible = jit_codigo_tam - jit_codigo_us;

	memset(&g, 0, sizeof(g));
	jit_x64_iniciar(&g.e, jit_codigo + jit_codigo_us, disponible);

	generar(&g);

	if (g.e.desborde || !jit_disp_ok)
	{
		fprintf(stderr, "jit: el bloque %08lx no se emitio (desborde=%d,"
			" desplazamientos=%d, %u bytes de %u)\n",
			(unsigned long) pc, g.e.desborde, jit_disp_ok,
			jit_x64_largo(&g.e), disponible);
		return 0;
	}

	b = &jit_bloques[jit_n_bloques++];

	b->pc            = pc;
	b->codigo        = (void (*)(void)) (jit_codigo + jit_codigo_us);
	b->palabras      = palabras;
	b->n_palabras    = n_palabras;
	b->extra_dir     = extra_dir;
	b->extra_palabra = extra_palabra;
	b->n_extra       = n_extra;
	b->mmu           = -1;
	b->veces         = 0;

	jit_registrar_marco(b, jit_x64_largo(&g.e));

	jit_codigo_us += jit_x64_largo(&g.e);

	jit_marcar(pc);
	jit_insertar(jit_n_bloques - 1);

	return 1;
}

/* Las palabras del bloque, contra la copia que la traduccion guardo, por el
   puntero de pagina que la busqueda de main_loop() ya resolvio. Codigo
   automodificado, otro proceso en la misma VA, otra imagen: la comparacion
   falla, el bloque no corre y el guest sigue interpretado. */
static int jit_verificar(jit_bloque * b)
{
	const WORD * codigo;
	int n = b->n_palabras;
	int i;

	codigo = (const WORD *) MMU_FETCH_PUNTERO(b->pc);

	/*
		El camino normal: **dos comparaciones**.

		La primera es el puntero que devuelve la busqueda, que es lo que
		identifica el mapeo entero -- pagina, ASID y modo, porque MMU_FETCH_PUNTERO
		mira los tres --. Comparar solo la epoca no alcanzaba: un bloque
		traducido en modo privilegiado y reencontrado en modo usuario mapea a
		otro lado y la epoca no se entera. Costo 61 568 instrucciones de
		divergencia, con la captura y los cuadros intactos.

		La segunda es la epoca, que se mueve cuando alguien escribe sobre una
		pagina con codigo traducido (ver jit.h). Entre las dos: mismas palabras,
		mismo sitio.
	*/
	if (codigo == b->ptr && b->epoca == jit_validez)
		return 1;

	/*
		A mano y no con memcmp: esto corre **una vez por entrada al bloque** --
		434 millones de veces en el banco de DCDoom y 3393 en el de Crazy Taxi --
		y son 16 bytes de media. La llamada al memcmp de la biblioteca, con su
		despacho por tamano, cuesta mas que la comparacion.
	*/
	for (i = 0; i < n; i++)
		if (codigo[i] != b->palabras[i])
			return 0;

	for (i = 0; i < b->n_extra; i++)
		if (*(const WORD *) MMU_FETCH_PUNTERO(b->extra_dir[i])
			!= b->extra_palabra[i])
			return 0;

	b->epoca = jit_validez;
	b->ptr   = codigo;

	return 1;
}

/*
	Corre bloques **encadenados**: mientras el corte del bloque periodico no
	corresponda, el bloque siguiente se despacha aca mismo en vez de volver a
	main_loop.

	Es lo que la primera medicion del traductor pidio. Sus bloques hacen 4,3
	instrucciones por entrada contra las 219 del bloque escrito a mano, y lo que
	se paga por entrada --el viaje a main_loop con su busqueda de instruccion,
	el filtro, la tabla, la verificacion, el prologo y el epilogo-- salia unos
	46 ciclos. Encadenar quita de esos el viaje entero.

	**La condicion es exactamente la de main_loop**, evaluada donde main_loop la
	evaluaria: si el bloque dejo el reloj pasado de RELOJ_GRANO o hay una
	entrega pendiente, se sale y el bloque periodico corre antes de la
	instruccion siguiente. Si no, main_loop tampoco lo habria corrido, asi que
	seguir de largo es la misma ejecucion.
*/
int jit_despachar(DWORD pc)
{
	int corridos = 0;

	for (;;)
	{
		jit_bloque * b = jit_buscar(pc);

		if (b == NULL)
		{
			if (corridos || !jit_traductor)
				break;

			b = tr_traducir(pc);

			if (b == NULL)
			{
				/* Que no se reintente en cada instruccion: el bit se apaga y
				   ese PC vuelve al interprete hasta que el muestreo lo
				   reproponga. */
				jit_desmarcar(pc);
				break;
			}

			/*
				**El bloque puede no empezar donde se pidio.** El crecimiento
				hacia atras lo registra en la cabeza del lazo, que es lo que se
				queria; pero entonces este PC no es su entrada y correrlo seria
				ejecutar desde otro lado. Se deja traducido y esta instruccion
				la hace el interprete: el bloque se encuentra solo cuando el
				guest llegue a su entrada. Y este PC deja de proponerse, o cada
				visita volveria a traducir la misma cabeza.
			*/
			if (b->pc != pc)
			{
				jit_desmarcar(pc);
				break;
			}
		}

		/* El modo con el que se emitio tiene que seguir valiendo: con la MMU
		   encendida los accesos llevan la traduccion adentro. */
		if (b->mmu >= 0
			&& b->mmu != (mmu_activa ? JIT_ACC_MMU : JIT_ACC_PLANO))
		{
			jit_rechazos++;
			break;
		}

		if (!jit_verificar(b))
		{
			jit_rechazos++;
			break;
		}

		jit_entradas++;
		b->veces++;
		jit_ult_sitio = -1;
		jit_estado.entrada = (void *) b->codigo;
		((void (*)(void)) jit_tramp)();
		corridos = 1;

		/* Si salio por un salto indirecto, este es el unico momento en que se
		   sabe adonde fue: se le ensena al sitio. */
		if (jit_ult_sitio >= 0)
		{
			int sitio = jit_ult_sitio;

			jit_ult_sitio = -1;
			jit_aprender_destino(sitio, PC);
		}

		/*
			La salida es una frontera de bloque de verdad, asi que siembra la
			siguiente -- pero por el contador de calor y no derecho: sembrar
			cada salida gasta un bloque por PC visto una sola vez, y los bloques
			son un recurso finito.

			Y solo si el PC no esta ya marcado. El contador de calor es una
			tabla de 64 KB y se toca por entrada al bloque; el mapa de bits son
			8 KB y ya se consulto. Un PC marcado no tiene nada que ganar
			muestreandose: su contador ya llego al tope. Lo unico que se pierde
			es una siembra cuando el bit lo puso otro PC que aliasa, y eso solo
			retrasa un descubrimiento.
		*/
		if (!JIT_MARCADO(PC))
			jit_muestrear(PC);

		if (core.context.cycles >= RELOJ_GRANO || intc_sh4_reintentar)
			break;

		pc = PC;
	}

	return corridos;
}

/* ------------------------------------------------------------------------ */
/* Arranque                                                                 */
/* ------------------------------------------------------------------------ */

static void jit_resumen(void)
{
	int i, j;

	if (!jit_entradas && !jit_rechazos)
		return;

	fprintf(stderr, "jit: %llu instrucciones en %llu entradas"
		" (%.1f por entrada), %llu rechazos por verificacion\n",
		jit_estado.instr, jit_entradas,
		jit_entradas ? (double) jit_estado.instr / (double) jit_entradas : 0.0,
		jit_rechazos);

	if (!jit_traductor)
		return;

	fprintf(stderr, "jit: %llu bloques traducidos (%.1f instrucciones cada"
		" uno), %u bytes, %llu emisiones fallidas, %llu sin lugar en la tabla,"
		" %llu enlaces atados (%llu por puente), %llu indirectos aprendidos,"
		" %llu salidas con los enlaces agotados,"
		" %u movimientos de epoca (%u escritura, %u mapeo, %u modo),"
		" %u transiciones de PR/SZ/Enable\n",
		jit_traducidos,
		jit_traducidos ? (double) jit_instr_bloque / (double) jit_traducidos
					   : 0.0,
		jit_codigo_us, jit_fallidos, jit_colisiones, jit_enlaces_atados,
		jit_puentes_atados, jit_enlaces_dinamicos, jit_enlaces_agotados,
		jit_epoca - 1, jit_ep_escritura, jit_ep_mapeo, jit_ep_modo,
		jit_ep_fpu);

	/*
		El censo de lo que corto los bloques, de mayor a menor. **Es lo que
		dice cual plantilla escribir despues**: sin el, la cobertura degrada en
		silencio y nadie sabe donde se fue.
	*/
	if (jit_censo_n)
	{
		fprintf(stderr, "jit: lo que mas corto bloques (palabra, veces,"
			" mnemonico):\n");

		for (i = 0; i < 12 && i < jit_censo_n; i++)
		{
			int mejor = -1;

			for (j = 0; j < jit_censo_n; j++)
				if (jit_censo_veces[j] != 0
					&& (mejor < 0
						|| jit_censo_veces[j] > jit_censo_veces[mejor]))
					mejor = j;

			if (mejor < 0)
				break;

			fprintf(stderr, "jit:   %04X  %8llu  %s\n",
				jit_censo_op[mejor], jit_censo_veces[mejor],
				opcodes_mnemonico(jit_censo_op[mejor]));

			jit_censo_veces[mejor] = 0;
		}
	}

	/* Y los bloques mas ejecutados, para saber si el muestreo encontro lo que
	   habia que encontrar. */
	fprintf(stderr, "jit: bloques mas ejecutados:\n");

	for (i = 0; i < 8; i++)
	{
		int mejor = -1;

		for (j = 0; j < jit_n_bloques; j++)
			if (jit_bloques[j].veces != 0
				&& (mejor < 0
					|| jit_bloques[j].veces > jit_bloques[mejor].veces))
				mejor = j;

		if (mejor < 0)
			break;

		fprintf(stderr, "jit:   %08lx  %2d instr  %10llu veces\n",
			(unsigned long) jit_bloques[mejor].pc,
			jit_bloques[mejor].n_palabras, jit_bloques[mejor].veces);

		jit_bloques[mejor].veces = 0;
	}
}

static void jit_volcar(const char * archivo)
{
	FILE * f = fopen(archivo, "wb");
	int i;

	if (f == NULL)
		return;

	fwrite(jit_codigo, 1, jit_codigo_us, f);
	fclose(f);

	fprintf(stderr, "jit: %u bytes emitidos en %s, cargados en %p\n",
		jit_codigo_us, archivo, (void *) jit_codigo);

	for (i = 0; i < jit_n_bloques; i++)
		fprintf(stderr, "jit:   bloque %08lx en +%u\n",
			(unsigned long) jit_bloques[i].pc,
			(unsigned) ((unsigned char *) jit_bloques[i].codigo - jit_codigo));
}

void jit_iniciar(void)
{
	const char * v = getenv("DCEMU_JIT");
	unsigned	 desplazamiento;
	int			 i;

	if (v == NULL || atoi(v) == 0)
		return;

	jit_traductor = (atoi(v) >= 2);

	{
		const char * si = getenv("DCEMU_JIT_SIN_INDIRECTOS");

		jit_sin_indirectos = (si != NULL && atoi(si) != 0);
	}

	{
		const char * sp = getenv("DCEMU_JIT_SIN_PUENTES");

		jit_sin_puentes = (sp != NULL && atoi(sp) != 0);
	}

	{
		const char * n = getenv("DCEMU_JIT_PLANTILLAS");

		if (n != NULL && atoi(n) > 0 && atoi(n) < JIT_N_PLANTILLAS)
			jit_n_activas = atoi(n);
	}

	jit_arena = (unsigned char *) jit_arena_reservar(JIT_ARENA_TAM);

	if (jit_arena == NULL)
	{
		fprintf(stderr, "jit: no se pudo reservar el arena; sigue el"
			" interprete\n");
		return;
	}

	/* Delante del codigo van la tabla de funciones y el UNWIND_INFO, para que
	   sus direcciones sean desplazamientos de 32 bits desde el arena, que es
	   lo que RtlAddFunctionTable espera. */
	desplazamiento = 0;

#ifdef _WIN32
	jit_tabla_rt = (RUNTIME_FUNCTION *) jit_arena;
	desplazamiento += (unsigned) (sizeof(RUNTIME_FUNCTION) * JIT_MAX_BLOQUES);
	desplazamiento = (desplazamiento + 3u) & ~3u;

	jit_unwind = jit_arena + desplazamiento;
	desplazamiento += 32;
#endif

	desplazamiento = (desplazamiento + 15u) & ~15u;

	jit_codigo     = jit_arena + desplazamiento;
	jit_codigo_tam = JIT_ARENA_TAM - desplazamiento;
	jit_codigo_us  = 0;

	jit_estado.instr        = 0;
	jit_estado.h_leer32     = (void *) jit_leer32;
	jit_estado.h_leer8s     = (void *) jit_leer8s;
	jit_estado.h_leer16s    = (void *) jit_leer16s;
	jit_estado.h_leer16sf   = (void *) jit_leer16s_fis;
	jit_estado.h_escribir16 = (void *) jit_escribir16;
	jit_estado.h_escribir16f = (void *) jit_escribir16_fis;
	jit_estado.h_escribir8  = (void *) jit_escribir8;
	jit_estado.h_escribir32 = (void *) jit_escribir32;
	jit_estado.h_leer32f    = (void *) jit_leer32_fis;
	jit_estado.h_leer8sf    = (void *) jit_leer8s_fis;
	jit_estado.h_escribir8f = (void *) jit_escribir8_fis;
	jit_estado.h_escribir32f = (void *) jit_escribir32_fis;
	jit_estado.p_pteh       = (void *) PTEH;
	jit_estado.p_mmucr      = (void *) MMUCR;
	jit_estado.h_busqueda   = (void *) jit_busqueda_puente;

	for (i = 0; i < JIT_HASH_N; i++)
		jit_hash[i] = -1;

	/* Las filas de la tabla de plantillas se ligan a los manejadores reales
	   aca: la tabla se escribe como una lista y el orden de los dos arreglos
	   es lo unico que las une, asi que un desajuste seria una plantilla usada
	   para otra instruccion. La comprobacion de tamano lo impide. */
	for (i = 0; i < JIT_N_PLANTILLAS; i++)
		jit_plantillas[i].f = jit_manejadores[i];


	if (!jit_emitir_trampolin())
	{
		fprintf(stderr, "jit: no se pudo emitir el trampolin; sigue el"
			" interprete\n");
		return;
	}

#ifdef _WIN32
	jit_unwind_armar(jit_unwind, &jit_marco_comun, jit_empujados, 8,
		JIT_MARCO_RSP);
	jit_unwind_puesto = 1;

	jit_tabla_rt[0].BeginAddress      = (DWORD) (jit_tramp - jit_arena);
	jit_tabla_rt[0].EndAddress        = (DWORD) JIT_ARENA_TAM;
	jit_tabla_rt[0].UnwindInfoAddress = (DWORD) (jit_unwind - jit_arena);

	if (!RtlAddFunctionTable(jit_tabla_rt, 1, (DWORD64) (size_t) jit_arena))
	{
		fprintf(stderr, "jit: RtlAddFunctionTable fallo; sigue el"
			" interprete\n");
		return;
	}
#endif

	if (!jit_traductor
		&& (!jit_emitir(JIT_CT_ENTRADA, gen_bloque_ct,
				jit_ct_palabras, 20,
				jit_ct_extra_dir, jit_ct_extra_palabra, 2)
		 || !jit_emitir(JIT_CE_ENTRADA, gen_bloque_ce,
				jit_ce_palabras, 17, NULL, NULL, 0)))
	{
		fprintf(stderr, "jit: el emisor se quejo; sigue el interprete\n");
		jit_n_bloques = 0;
		memset(jit_mapa, 0, sizeof(jit_mapa));
		return;
	}

	jit_activo        = 1;
	jit_vigila_codigo = jit_traductor;

	if (jit_traductor)
		fprintf(stderr, "jit: traductor automatico, %d plantillas"
			" (DCEMU_JIT=2)\n", JIT_N_PLANTILLAS);
	else
		fprintf(stderr, "jit: %d bloques a mano, %u bytes (DCEMU_JIT=1)\n",
			jit_n_bloques, jit_codigo_us);

	v = getenv("DCEMU_JIT_VOLCADO");

	if (v != NULL && *v != '\0')
		jit_volcar(v);

	atexit(jit_resumen);
}

#endif /* DCEMU_JIT */
