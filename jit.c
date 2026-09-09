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
#include "floatsimple.h"
#include "floatcontrol.h"	/* lds214, sts218 */
#include "floatgraph.h"		/* fipr, ftrv, fsrra */
#include "dcopcodes.h"		/* fsca */
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
unsigned		jit_epoca_escr = 1;
unsigned long long	jit_ep_pag_vista = 0;
unsigned		jit_ep_mapeo = 0;
unsigned		jit_ep_modo = 0;
unsigned		jit_fpu_visto = 0;
unsigned		jit_ep_fpu = 0;
unsigned char	jit_pag_codigo[0x10000];
unsigned char	jit_lin_codigo[0x40000];

/*
	Una escritura de bloque contra la rejilla fina: 1 si toca alguna linea con
	codigo traducido. Recorre las lineas de verdad en vez de mirar los
	extremos, que es lo unico que hace bien la pregunta cuando el bloque cubre
	mas de una. Ver JIT_ESCRITURA_HOST en jit.h.
*/
int jit_escritura_bloque(const unsigned char * p, size_t tam)
{
	const unsigned char * fin = p + tam - 1;
	int                   vista = 0;

	for (; p <= fin; p += 64)
		if (jit_pag_codigo[JIT_PAG_BIT(p)])
		{
			vista = 1;

			if (jit_lin_codigo[JIT_LIN_BIT(p)])
			{
				jit_ep_pag_vista++;

				return 1;
			}
		}

	if (jit_pag_codigo[JIT_PAG_BIT(fin)])
	{
		vista = 1;

		if (jit_lin_codigo[JIT_LIN_BIT(fin)])
		{
			jit_ep_pag_vista++;

			return 1;
		}
	}

	jit_ep_pag_vista += (unsigned) vista;

	return 0;
}

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
	void *					h_leer_par;		/* jit_leer_par() */
	void *					h_escribir_par;	/* jit_escribir_par() */
	/* El destino de un salto dinamico cuya ranura accede a memoria. El PC del
	   contexto no sirve de lugar seguro ahi: tiene que quedar en el PC de la
	   RAMA hasta despues de la ranura, para que una falta reejecute desde la
	   rama (la semantica de la ranura de retardo). */
	DWORD					destino;
} jit_estado_t;

static jit_estado_t jit_estado;

/*
	La elision de lazos ociosos (docs/recompilador-plan.md, 'Lo que sigue').

	La generacion de impureza: la sube todo lo que puede hacer que dos vueltas
	de un lazo con los mismos registros NO sean identicas -- una escritura
	emitida, una llamada a manejador o a ayudante (una lectura por ayudante
	puede tener efectos: FIFO, RTC), y la entrada al despachador (entre dos
	entradas corrio el bloque periodico). Es un contador y no una bandera a
	proposito: cada arista guarda el valor con el que tomo su instantanea, asi
	que una arista no puede limpiarle la marca a otra. El codigo emitido la
	sube en linea (`add qword [gen], 1`), asi que es una global vista desde el
	contexto como las demas. De 64 bits para que no pueda dar la vuelta entre
	dos visitas a una misma arista: a diez millones de bumps por segundo, 32
	bits dan la vuelta en siete minutos.

	Y va **partida en clases**, que es el censo de la segunda vuelta: una
	sonda que falla sabe que la vuelta no fue pura, pero no POR QUE, y las
	cuatro causas piden cosas distintas de una v2 -- una escritura emitida
	pide un sello mas fino que 'hubo tienda', un acceso por ayudante pide
	separar la lectura pura del registro con efecto, un manejador pide mas
	plantillas, y una entrada al despachador pide enlazar mejor. Comparar
	cuatro palabras en vez de una no cambia la conducta --la vuelta es pura si
	y solo si NINGUNA se movio-- y no cuesta nada en lo emitido: cada fila
	sube la suya con el mismo `add qword [gen+k], 1` de siempre.
*/
#define JIT_IMP_ESCRITURA	0	/* tienda del camino rapido emitido */
#define JIT_IMP_ACCESO		1	/* gen_llamar: ayudantes, camino lento, PREF */
#define JIT_IMP_MANEJADOR	2	/* tr_manejador: la fila corre en C */
#define JIT_IMP_DESPACHO	3	/* entrada al despachador: cambio de grano */
#define JIT_IMP_N			4

static const char * const jit_imp_nombre[JIT_IMP_N] =
{
	"escritura", "acceso", "manejador", "despacho"
};

unsigned long long	jit_ocioso_gen[JIT_IMP_N] = { 1, 1, 1, 1 };

/* DCEMU_JIT_OCIOSOS: 0 apagada (emision identica a la anterior), 1 solo los
   bumps (mide su costo), 2 entera (la omision). */
static int		jit_ociosos = 2;

/* Solo desde C: el despachador las lleva, el codigo emitido no las toca. */
static unsigned long long jit_entradas  = 0;
static unsigned long long jit_rechazos  = 0;

/*
	El desglose de los rechazos, porque el total mezcla tres causas que piden
	correcciones distintas: el modo MMU de la emision, el modo FPU
	(PR/SZ/Enable) y las palabras (jit_verificar). Camino frio -- decenas o
	cientos de miles por segundo, no millones -- asi que el censo por PC cabe:
	64 ranuras por hash abierto alcanzan para nombrar a los reincidentes.
*/
static unsigned long long jit_rechazos_modo     = 0;
static unsigned long long jit_rechazos_fpu      = 0;
static unsigned long long jit_rechazos_palabras = 0;

/*
	La retraduccion por fallo de palabras: cuando jit_verificar dice que la
	memoria ya no es la traducida (los remapeos de WinCE: DOOM dejaba 2,5 M
	de entradas por 35 s corriendo interpretadas PARA SIEMPRE), el bloque
	viejo recibe una lapida en el pc y el lazo del despachador traduce el
	contenido vigente como a cualquier miss. El tope por PC (heredado de
	bloque en bloque) acota el ping-pong si dos contenidos alternan: el
	contador de retraducciones es el censo que decide si hace falta algo
	mejor (variantes por ASID). DCEMU_JIT_SIN_RETRADUCIR=1 vuelve a la
	conducta anterior.
*/
#define JIT_MAX_RETRAD	16

static int jit_retraducir = 1;

static unsigned long long	jit_retraducciones = 0;
static unsigned long long	jit_retrad_topes   = 0;	/* rechazos ya al tope */
static DWORD				jit_retrad_pc = 0;		/* la herencia pendiente */
static unsigned char		jit_retrad_n  = 0;

typedef struct
{
	DWORD				pc;
	unsigned long long	veces[3];		/* modo, fpu, palabras */
} jit_rechazo_sitio;

static jit_rechazo_sitio jit_rechazo_sitios[64];

static void jit_rechazo_censar(DWORD pc, int causa)
{
	unsigned i = (pc >> 1) & 63;
	unsigned k;

	for (k = 0; k < 8; k++, i = (i + 1) & 63)
	{
		jit_rechazo_sitio * s = &jit_rechazo_sitios[i];

		if (s->pc == pc || s->pc == 0)
		{
			s->pc = pc;
			s->veces[causa]++;
			return;
		}
	}
	/* Tabla llena en ese vecindario: el total ya lo cuenta. */
}


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

	La alineacion ya la comprobo el camino rapido, asi que lo unico que falta es
	el despacho por zona, que es justo lo que hacen memread_fisico() /
	memwrite_fisico() -- con sus watchpoints incluidos.

	**El break de operando del UBC no**, y esa es la unica cosa que estos
	ayudantes tienen que hacer de mas. Su guarda dejo de emitirse (se pliega en
	mem_base_lectura/escritura, como el watchpoint), asi que con el UBC armado
	toda zona plana baja por aqui -- y el gancho vive en el macro, que este
	camino se saltea. Compara la direccion **virtual**, que la fisica no
	sustituye, y por eso llega aparte en jit_ubc_virtual.
*/
static DWORD jit_ubc_virtual = 0;

static void jit_ubc_fis(const void * valor, size_t tam, int escritura)
{
	if (ubc_operando_activa)
		ubc_operando(jit_ubc_virtual, valor, tam, escritura);
}

DWORD jit_leer32_fis(DWORD fisica)
{
	DWORD v;

	memread_fisico(fisica, &v, sizeof(DWORD));
	jit_ubc_fis(&v, sizeof(DWORD), 0);

	return v;
}

DWORD jit_leer8s_fis(DWORD fisica)
{
	BYTE b;

	memread_fisico(fisica, &b, sizeof(BYTE));
	jit_ubc_fis(&b, sizeof(BYTE), 0);

	return (DWORD) SignExtend8(b);
}

DWORD jit_leer16s_fis(DWORD fisica)
{
	WORD w;

	memread_fisico(fisica, &w, sizeof(WORD));
	jit_ubc_fis(&w, sizeof(WORD), 0);

	return (DWORD) SignExtend16(w);
}

void jit_escribir8_fis(DWORD fisica, DWORD valor)
{
	BYTE b = (BYTE) (valor & 0xFF);

	memwrite_fisico(fisica, &b, sizeof(BYTE));
	jit_ubc_fis(&b, sizeof(BYTE), 1);
}

void jit_escribir16_fis(DWORD fisica, DWORD valor)
{
	WORD w = (WORD) (valor & 0xFFFF);

	memwrite_fisico(fisica, &w, sizeof(WORD));
	jit_ubc_fis(&w, sizeof(WORD), 1);
}

void jit_escribir32_fis(DWORD fisica, DWORD valor)
{
	DWORD v = valor;

	memwrite_fisico(fisica, &v, sizeof(DWORD));
	jit_ubc_fis(&v, sizeof(DWORD), 1);
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

/*
	El par de sz1 viaja como UN acceso de 8 bytes, igual que en los
	manejadores (memread/memwrite de sizeof(DWORD)*2): todo-o-nada ante una
	falta, sin orden interno que reproducir. El puntero de banco se
	desreferencia aca adentro, en el momento del acceso, asi que el
	intercambio por el bit FR no necesita guarda.
*/
void jit_leer_par(DWORD dir, DWORD desp)
{
	memread(dir, (unsigned char *) core.context.FR_BANK + desp,
		sizeof(DWORD) * 2);
}

void jit_escribir_par(DWORD dir, DWORD desp)
{
	memwrite(dir, (unsigned char *) core.context.FR_BANK + desp,
		sizeof(DWORD) * 2);
}

/* ------------------------------------------------------------------------ */
/* El arena                                                                 */
/* ------------------------------------------------------------------------ */

/*
	Los topes vienen de la corrida de verificacion del puente, no de una
	corazonada: Crazy Taxi saturo los 16 384 bloques con 5269 candidatos
	calientes sin lugar --un tercio de su volumen sigue interpretado por falta
	de capacidad, no de plantillas-- y DCDoom dejo el arena al 98 % (15,68 de
	16 MB). El tope de bloques era el maximo que el `short` de la tabla hash
	direccionaba; la tabla es de ints desde el tope de 65 536.
*/
/* 64 MB: la segunda tanda de plantillas dejo a DCDoom con el arena de 32 al
   98,8 % y 1314 emisiones fallidas -- bloques de 15,6 instrucciones que ya no
   cupieron. El arena es lo unico que hoy le pone tope a su cobertura. */
/* 128 MB: Sega Rally 2 lleno los 64 (DCDoom ya usaba 48,5 -- la traduccion
   MMU en linea pesa ~4 KB por bloque) y el desborde en el borde destapo el
   parche fuera del mapa que fijar() ahora anula. */
/* 192 MB: los bloques de hasta 96 instrucciones (superbloques, paso 1)
   subieron el bloque medio de SR2 a 27,8 instrucciones y dejaron los 128 al
   99,7 %. Es espacio de direcciones, no memoria tocada. */
/* 256 MB y 65 536 bloques (2026-08-30): con la fuga de la tabla arreglada,
   SR2 llena las 32 768 ranuras LEGITIMAMENTE a los 180 s -- 9577 propuestas
   rechazadas con la tabla llena, y peor: una lapida de retraduccion que no
   puede renacer pierde el bloque entero, asi que los sitios remapeados de
   WinCE degradan al interprete para siempre (37 973 rechazos por tope de
   retraduccion contra 12 019 a 60 s). El doble de bloques pide arena a
   juego: SR2 llevaba 103 MB de 192 con la tabla llena. DCEMU_JIT_BLOQUES=N
   recorta el tope en runtime -- 32768 es el brazo del A/B. */
#define JIT_ARENA_TAM		(256u * 1024u * 1024u)
#define JIT_MAX_BLOQUES		65536
/* El tope a 96 SE MIDIO Y PERDIO (superbloques, paso 1): la longitud extra
   se va a colas frias -- las entradas al despacho apenas bajaron 1-3 % --
   mientras el codigo emitido crece 13-34 % y dispersa lo caliente; SR2 paso
   a +9,4 % contra 64, mas lento que su interprete. Las fronteras calientes
   son cortes periodicos y aristas de llamada, y el largo estatico no las
   quita: un superbloque util tendra que seguir el flujo (BRA, retorno), no
   estirar el tramo. El expediente en el plan. */
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
	/* La arista de la elision de ociosos: indice + 1 en jit_aristas, 0 si el
	   enlace no tiene sonda. Lo pone jit_parchear_enlace al instalarla. */
	int				sonda;
} jit_enlace;

static const unsigned jit_nunca = 0;

typedef struct
{
	DWORD			pc;					/* la entrada, en el espacio del guest */
	void			(* codigo)(void);
	/* La entrada post-prologo y que registros del guest cachea (mascara de
	   bits): lo que una costura por arista necesita para cargar solo la
	   diferencia, y lo que el censo de uso pondera por veces. `canonicas` es
	   el subconjunto colocado en hogar canonico (elidible en una costura si
	   el que salta tambien lo cachea); `mapa` es la ranura de cada registro,
	   que la costura necesita para cargar los no canonicos donde va. */
	void			(* cuerpo)(void);
	unsigned		ranuras;
	unsigned		canonicas;
	signed char		mapa[16];
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
	/* Con que modo FPU se emitio ((PR<<2)|(SZ<<1)|algun-Enable), o -1 si no
	   tiene filas FPU. Lo chequea el despachador; los bloques con esto >= 0
	   no reciben enlaces, asi que el chequeo los cubre en toda entrada. La
	   sonda midio por que no puede ir en la clave: Crazy Taxi conmuta SZ
	   13,2 millones de veces por minuto alrededor de sus matrices. */
	int				fpu;
	WORD			copia[JIT_MAX_INSTR];	/* solo los traducidos */
	/* El PC de cada palabra de la traza. Solo se llena cuando la traza no es
	   contigua (flujo seguido): extra_dir apunta adentro y jit_verificar
	   compara lo no contiguo palabra a palabra, con el mecanismo que ya
	   tenian los bloques a mano. */
	DWORD			pcs[JIT_MAX_INSTR];
	/* En que termino el descubrimiento (JIT_FIN_*): el censo de la frontera,
	   ponderado por veces en el resumen. Es lo que dice DONDE esta el costo
	   de frontera de verdad, por peso de ejecucion y no por sitios. */
	unsigned char	fin;
	/* La palabra que corto el descubrimiento (0 si el fin no tiene palabra).
	   Con `veces` da el censo de la frontera POR PALABRA ponderado por
	   ejecucion: la lista sin ponderar de jit_censar() cuenta sitios de
	   traduccion, y un solo sitio caliente vale millones de entradas. */
	WORD			corte;
	/* Cuantas veces este PC ya se retradujo por fallo de palabras: la
	   herencia que acota el ping-pong de dos contenidos alternando (ver la
	   retraduccion en jit_despachar). Cabe en el relleno de alineacion. */
	unsigned char	retraducido;
	unsigned long long veces;
	/* La epoca con la que se verifico entero. Mientras la global no se mueva,
	   sus palabras son las mismas y su pagina sigue donde estaba. */
	unsigned		epoca;		/* la clave de validez con la que se verifico */
	/*
		**La epoca de ESCRITURA con la que se verifico, que es otra cosa.**

		`epoca` lleva la clave entera --epoca global y SR.MD-- porque es lo que
		compara el salto encadenado, que se saltea el despachador y por lo
		tanto no recomprueba nada mas. La verificacion por entrada SI
		recomprueba: calcula el puntero de busqueda y lo compara. Y ese
		puntero ya identifica el mapeo entero --pagina, ASID y modo--, asi que
		exigirle ademas la clave entera hace re-verificar palabra por palabra
		bloques que no pueden haber cambiado. Medido: **el 20-22 % de las
		entradas** de DCDoom y Sega Rally 2 caian al camino largo, y en SR2 el
		96,7 % de ellas ACERTABA -- veinte palabras comparadas para confirmar
		lo que el puntero ya decia.

		Lo unico que el puntero no cubre es que alguien haya escrito sobre las
		palabras, y para eso basta el contador de escrituras.
	*/
	unsigned		epoca_escr;
	const WORD *	ptr;		/* lo que devolvio la busqueda al verificarlo */
	jit_enlace		enlace[JIT_MAX_ENLACES];
	int				n_enlaces;
} jit_bloque;

static jit_bloque	jit_bloques[JIT_MAX_BLOQUES];
static int			jit_n_bloques = 0;

/* Los dos interruptores de la compuerta MMU+FPU (ver el plan). Se leen una
   vez al arrancar; deciden EMISION, asi que cuestan cero en caliente.

   La compuerta viene LEVANTADA: su motivo era el 0x800 del cambio perezoso
   de contexto FPU de WinCE, resuelto con FD en la clave (bit 3, jit.h) y el
   corte del descubrimiento con FD puesto -- el protocolo salio canonico en
   los seis escenarios. DCEMU_JIT_SIN_FPU_MMU=1 la cierra: es el interruptor
   de aislamiento y el que reproduce la linea base anterior.

   El corte de epoca tras escrituras (gen_corte_epoca) viene APAGADO: el
   agujero del orden de busqueda que motiva su diseno no se observa en
   ningun banco (los seis escenarios son exactos sin el; la razon empirica
   es que el guest no ejecuta accesos con avance entre la escritura
   invalidante y el fin del bloque), y cuesta 6-8 % de cobertura.
   DCEMU_JIT_CORTE_EPOCA=1 lo enciende si un guest futuro lo desmiente. */
static int			jit_fpu_mmu = 1;
static int			jit_corte_epoca = 0;

/* El buscador emitido (el despacho dentro del arena). Viene APAGADO: el A/B
   sobre un binario dio neutro en CT (93 655 contra 93 522 ms, dispersiones
   solapadas) aun sirviendo la salida dominante -- el viaje al despachador C
   ya no es el costo, medido asi por tercera vez --. DCEMU_JIT_BUSCADOR=1 lo
   enciende para rehacer el A/B sin rehacer la idea, como DCEMU_BLOQUES.
   NULL = apagado, y el epilogo salta a tramp_salir como siempre. */
static unsigned char *	jit_buscador = NULL;
static int				jit_buscador_activo = 0;

/*
	Los hogares canonicos y las costuras (la fase de registros persistentes,
	ver el plan). La SELECCION de que cachear sigue siendo la codiciosa por
	uso -- la calidad intra-bloque no cambia por construccion --; solo la
	COLOCACION se fija para los cinco universales del censo ponderado por
	veces (r0 74 %, r3 48 %, r2 46 %, r4 46 %, r15 35 % entre los tres
	guests), y los demas toman las ranuras que sobren. Con eso, el parche de
	un enlace directo puede coser: la interseccion canonica ya esta en los
	hogares (el volcado completo del que salta dejo el contexto al dia sin
	tocar los registros), y la costura carga solo la diferencia.
	DCEMU_JIT_SIN_HOGARES=1 vuelve a la colocacion secuencial de antes y
	apaga las costuras: aislamiento y linea base anterior.
*/
static const signed char	jit_canonico[16] =
{
	0, -1, 1, 2, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4
};

static int					jit_hogares = 1;
static int					jit_costuras_talones = 0;	/* DCEMU_JIT_COSTURAS=2 */
static unsigned long long	jit_costuras = 0;		/* talones emitidos */
static unsigned long long	jit_costuras_vacias = 0;	/* saltos directos al cuerpo */

/* La cuenta que decide la fase de registros persistentes (ver el plan):
   cuantas fronteras de bloque son cruces de enlace -- que una costura por
   arista dejaria casi gratis -- contra entradas por el despachador. Con
   DCEMU_JIT_SONDA_CRUCES=1 cada cabeza de bloque emite un contador;
   cruces = corridos - entradas. Cero costo apagada (decision de emision). */
static unsigned long long	jit_bloques_corridos = 0;
static int					jit_sonda_cruces = 0;

/* El superbloque por flujo: seguir la caminata a traves del BRA constante,
   del BSR (llamada en linea, con rastreo de PR) y del RTS con punto de
   retorno conocido. **Apagado por omision**: la tanda 2026-08-09 salio
   neutra a levemente negativa (DCDoom -21,2 % contra -21,7, CT -15,2 contra
   -14,8, SR2 -7,0 contra -8,3) -- las entradas al despachador bajaron 1-4 %
   y el tiempo no las siguio, la leccion de siempre: el viaje al despachador
   no es el costo. DCEMU_JIT_FLUJO=1 lo revive para rehacer el A/B; la
   exactitud esta probada al digito con capturas canonicas en los tres
   guests. Los seguidos se cuentan al traducir (frio). */
static int					jit_flujo = 0;
static unsigned long long	jit_flujo_seguidos = 0;	/* BRA */
static unsigned long long	jit_flujo_bsr = 0;
static unsigned long long	jit_flujo_rts = 0;

/* Los pares de rama: una rama cuya ranura accede a memoria ya no corta el
   bloque. El caso que lo inauguro es el epilogo estandar de Katana
   (rts; lds.l @r15+,pr): sin esto CADA retorno de llamada paga salida +
   despacho + dos instrucciones interpretadas. La emision sincroniza con el
   PC de la RAMA antes de tocar nada -- una falta en la ranura reejecuta
   desde la rama, como el interprete -- que es exactamente lo que faltaba en
   el expediente de las 616. DCEMU_JIT_SIN_PARES=1 los apaga todos;
   DCEMU_JIT_SIN_PARES_LLAMADA=1 solo BSR/JSR/BSRF, para su A/B. */
static int					jit_par_rts = 1;
static int					jit_par_llamadas = 1;
static unsigned long long	jit_pares_rts = 0;

/* Las dos mitades del lote B.2b (jit-sota-plan.md), cada una con su palanca:
   DCEMU_JIT_SIN_TERMINALES=1 apaga las filas terminales (vuelven a cortar como
   si no tuvieran plantilla) y DCEMU_JIT_SIN_RANURA_FPU=1 les devuelve a las
   filas FPU directas la prohibicion de ranura. */
static int					jit_terminales = 1;
static int					jit_ranura_fpu = 1;

/* En que termino el descubrimiento de una traza: el censo de la frontera. */
#define JIT_FIN_PLANTILLA	0	/* palabra sin plantilla (el censo la nombra) */
#define JIT_FIN_TOPE		1	/* JIT_MAX_INSTR */
#define JIT_FIN_VENTANA		2	/* el limite de 1 KB bajo MMU */
#define JIT_FIN_RANURA		3	/* rama/FPU en la ranura, o rama al final */
#define JIT_FIN_RANURA_MEM	4	/* ranura con memoria de una rama no-RTS */
#define JIT_FIN_PAR			5	/* par de rama */
#define JIT_FIN_LAZO		6	/* el destino ya estaba en la traza */
#define JIT_FIN_FPU			7	/* la compuerta o FD */
#define JIT_FIN_TERMINAL	8	/* fila terminal: el bloque termina en ella */
#define JIT_FIN_N			9

static const char * const jit_fin_nombre[JIT_FIN_N] =
{
	"sin plantilla", "tope de 64", "ventana de 1 KB", "rama/FPU en ranura",
	"ranura con memoria (sin par)", "par de rama", "lazo cerrado",
	"compuerta FPU", "fila terminal"
};

/*
	La tabla de bloques por PC. El mapa de bits dice "puede haber algo" y esta
	dice que -- y **tiene que estar indexada por el PC entero**, no por
	(PC >> 1) & 0xFFFF como el filtro: ese recorte distingue 128 KB de espacio
	de PC, y DCDoom ejecuta en cuatro ventanas a la vez (0x8C..., 0xAC...,
	0x0000..., 0x01E7...). Con mapeo directo recortado se pisaban entre ellas
	--16 146 reemplazos sobre 16 384 bloques-- y la cobertura se quedaba en el
	0,25 % con el censo ya seco: los bloques existian y no se encontraban.

	Direccionamiento abierto con sondeo lineal. Si el sondeo no encuentra
	lugar, el bloque no se indexa y su PC se desmarca -- vuelve al interprete
	hasta que el muestreo lo reproponga.

	El tamano viene de una medida: con 16 384 bloques sobre 32 768 ranuras
	--carga del 50 %-- Crazy Taxi dejaba 5269 inserciones sin lugar en ocho
	sondeos, o sea bloques ya emitidos que nadie podia encontrar. A carga del
	25 % la cola del sondeo lineal se corta; con el tope de 65 536 la tabla
	guarda esa carga con 18 bits, y son ints (1 MB) porque el indice ya no
	cabe en un short.

	El sondeo era 8 y el censo de sin-lugar mostro que no alcanza NI con la
	carga al 25 %: a esa carga un cumulo lleno de 8 es raro pero no imposible
	(esperados ~2 sobre 131 072 posiciones, y las lapidas de la retraduccion
	alargan las corridas), y un PC que cae en uno falla PARA SIEMPRE -- SR2
	tenia 9, y como la falla ademas retraducia en cada visita (el bug de
	abajo), quemo 12 904 ranuras de bloque y choco el tope global de 32 768 a
	mitad de corrida, apagando el traductor entero. Con 32 la falla al azar es
	~0.25^32: extinta. El costo en jit_buscar es cero en regimen -- el lazo
	sale en el primer vacio o en el match, y solo una busqueda que atraviesa
	un cumulo de mas de 8 ocupados paga sondeos extra, que es exactamente el
	caso raro que este numero existe para cubrir.
*/
#define JIT_HASH_BITS	18
#define JIT_HASH_N		(1u << JIT_HASH_BITS)
#define JIT_HASH_SONDEO	32

/* DCEMU_JIT_TABLA_VIEJA=1: sondeo de 8, sin reuso de lapidas y sin desmarcar
   al fallar -- la conducta anterior entera, con su fuga. Es el brazo del A/B:
   la emision no cambia con nada de esto, asi que los dos brazos corren sobre
   un solo binario. */
static int jit_tabla_vieja = 0;
static int jit_sondeo      = JIT_HASH_SONDEO;

static int					jit_hash[JIT_HASH_N];
static unsigned long long	jit_colisiones = 0;

/* DCEMU_JIT_BLOQUES=N: el tope de bloques en runtime (1..JIT_MAX_BLOQUES).
   32768 reproduce el tope anterior y es el brazo del A/B de la capacidad. */
static int					jit_max_bloques = JIT_MAX_BLOQUES;

/*
	El censo de las inserciones sin lugar, por PC. Existe por una cuenta que
	no cerraba sola: a carga <=25 % con sondeo de 8, una falla al azar es
	rarisima, y SR2 reporto 12 904 -- el censo mostro que eran 9 PCs en fuga.
	Un PC sin lugar no volvia al interprete y ya: el despachador lo
	RETRADUCIA en cada visita (buscar -> NULL -> tr_traducir, cuyo dedup usa
	el mismo buscar), quemando una ranura de jit_bloques[] y arena por visita
	hasta chocar el tope global -- y con el tope chocado el traductor entero
	dejaba de traducir, incluidas las retraducciones por remapeo. El arreglo
	son las dos cosas juntas porque son un solo mecanismo: el sondeo a 32
	extingue la falla, y el desmarcar de abajo hace verdad lo que el
	comentario de la tabla siempre prometio -- sin lugar = interpretado, no
	en fuga. El censo queda de guardia: cualquier numero aca es la proxima
	vez. Mismo patron que jit_rechazo_sitios: solo en el camino de falla.
*/
typedef struct
{
	DWORD				pc;
	unsigned long long	veces;
} jit_colision_sitio;

static jit_colision_sitio	jit_colision_sitios[64];
static unsigned				jit_colision_pcs = 0;	/* distintos censados */

/* Propuestas rechazadas con la tabla de bloques llena (ver tr_traducir). */
static unsigned long long	jit_tope_bloques = 0;

static void jit_desmarcar(DWORD pc);

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

/* DCEMU_JIT_SIN_VARIANTES_FPU=1: la busqueda vuelve a ser ciega al modo (la
   conducta anterior, donde el modo equivocado era un rechazo que mandaba el
   tramo al interprete). */
static int jit_variantes_fpu = 1;

static unsigned long long jit_fpu_variantes = 0;	/* traducciones hermanas */

static jit_bloque * jit_buscar(DWORD pc)
{
	unsigned h = jit_hash_de(pc);
	int i;

	for (i = 0; i < jit_sondeo; i++)
	{
		int b = jit_hash[(h + (unsigned) i) & (JIT_HASH_N - 1)];

		if (b < 0)
			return NULL;

		if (jit_bloques[b].pc == pc)
		{
			/*
				La variante por modo FPU: un bloque con filas FPU solo vale
				bajo el modo con el que se emitio (b->fpu), asi que el modo
				equivocado se SALTEA y el sondeo sigue -- el miss traduce la
				variante de este modo y las dos conviven en el hash abierto.
				Antes esto era un rechazo del despachador que mandaba el
				tramo al interprete: el desglose midio 3,9 M por minuto en
				SR2 (dos sitios con el 94 %) y 5,8 M en 35 s de DOOM. La
				premisa del comentario del rechazo --"el flip encierra la
				secuencia"-- vale para los sitios fmov de un bloque, no para
				un bloque cuya ENTRADA se visita bajo los dos modos.
			*/
			if (!jit_variantes_fpu
				|| jit_bloques[b].fpu < 0
				|| (unsigned) jit_bloques[b].fpu == jit_fpu_visto)
				return &jit_bloques[b];
		}
	}

	return NULL;
}

static void jit_insertar(int idx)
{
	unsigned h = jit_hash_de(jit_bloques[idx].pc);
	int i;

	for (i = 0; i < jit_sondeo; i++)
	{
		unsigned k = (h + (unsigned) i) & (JIT_HASH_N - 1);
		int      b = jit_hash[k];

		/*
			Una ranura de lapida (pc == 1, la retraduccion) se reusa: buscar la
			trata como ocupada-que-no-matchea, asi que pisarla no corta ninguna
			cadena de sondeo -- el reuso clasico de tumbas del direccionamiento
			abierto. Sin esto los cumulos crecian con cada retraduccion (las
			lapidas no salen nunca de la tabla) y eran LA causa de los
			sin-lugar de SR2: sus 9 PCs en fuga eran justamente sitios
			remapeados, cuyas propias lapidas les llenaban el vecindario.
		*/
		if (b < 0 || (!jit_tabla_vieja && jit_bloques[b].pc == 1))
		{
			jit_hash[k] = idx;
			return;
		}
	}

	jit_colisiones++;

	/* Sin lugar de verdad (extinto con el sondeo a 32 y el reuso): que el PC
	   vuelva al interprete en vez de retraducirse en cada visita. El bloque
	   ya emitido corre esta unica vez y se pierde -- una ranura, no una
	   fuga. */
	if (!jit_tabla_vieja)
		jit_desmarcar(jit_bloques[idx].pc);

	{
		DWORD    pc = jit_bloques[idx].pc;
		unsigned s  = (pc >> 1) & 63;
		unsigned k2;

		for (k2 = 0; k2 < 8; k2++, s = (s + 1) & 63)
		{
			jit_colision_sitio * c = &jit_colision_sitios[s];

			if (c->pc == pc)
			{
				c->veces++;
				return;
			}

			if (c->pc == 0)
			{
				c->pc    = pc;
				c->veces = 1;
				jit_colision_pcs++;
				return;
			}
		}
		/* Vecindario lleno: el total ya lo cuenta. */
	}
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
#define O_GBR	((int) offsetof(context_t, GBR_REG))
#define O_MACL	((int) offsetof(context_t, MACL_REG))

/* El banco FR es un PUNTERO en el contexto y los intercambios del bit FR lo
   permutan: el acceso emitido carga el puntero vivo y por eso sobrevive al
   intercambio sin guarda alguna. */
#define O_FRB		((int) offsetof(context_t, FR_BANK))
#define FR_DESP(x)	((int) offsetof(FPR_BANK, FP.XMTRX.m) + 4 * (x))
#define O_FPUL		((int) offsetof(context_t, FPUL_REG))
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
#define D_LIN_CODIGO	D(&jit_lin_codigo[0])
#define D_EPOCA		D(&jit_validez)
#define D_ENTRADA	D(&jit_estado.entrada)
#define D_BUSQUEDA	D(&jit_estado.h_busqueda)
#define D_LEER_PAR	D(&jit_estado.h_leer_par)
#define D_ESCR_PAR	D(&jit_estado.h_escribir_par)
#define D_ULT_SITIO	D(&jit_ult_sitio)
#define D_REINTENTO	D(&intc_sh4_reintentar)
#define D_LIMITE		D(&intc_corte_limite)
#define D_OCIOSO_GEN(k)	D(&jit_ocioso_gen[k])
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
#define D_ETIQUETA		D(&mmu_etiqueta)
#define D_URC_PEND		D(&mmu_urc_pend)
#define D_PERF_TRADUCE	D(&perf_mmu_traduce)
#define D_PERF_ACIERTO	D(&perf_mmu_datos_acierto)

/* ------------------------------------------------------------------------ */
/* El generador                                                             */
/* ------------------------------------------------------------------------ */

/* Escala con el tope de instrucciones: un corte por instruccion con ciclos,
   mas las salidas propias de ramas y el margen. Con 64 fijo, subir
   JIT_MAX_INSTR a 96 hacia desbordar la emision del bloque entero -- 799 y
   1084 emisiones fallidas contra 322 y 263 -- y el PC quedaba interpretado
   para siempre: la cobertura CAIA al permitir bloques mas largos. */
#define JIT_MAX_SALIDAS		(JIT_MAX_INSTR + JIT_MAX_INSTR / 2 + 16)
typedef struct jit_traduccion jit_traduccion;

typedef struct
{
	x64_emisor	e;
	x64_parche	salidas[JIT_MAX_SALIDAS];
	int			n_salidas;
	jit_enlace	enlace[JIT_MAX_ENLACES];
	int			n_enlaces;
	jit_marco	marco;

	/*
		La sincronizacion pendiente de la fila en curso: la instantanea que
		hace reejecutable al acceso que viene. **Se emite en el camino lento y
		no antes de la plantilla**, que es toda la fase: el camino rapido
		emitido no puede faltar --alineacion, modo, traduccion acertada y zona
		con base directa lo garantizan, y el UBC y el watchpoint estan plegados
		en mem_base_*-- asi que volcar los registros ahi era pagar cuarenta
		bytes por acceso para un longjmp que no ocurre.

		Lo que la hace exacta es que TODA salida a C pasa por gen_llamar() o
		por tr_manejador(), y las dos la emiten antes de la llamada. Y que las
		plantillas de acceso mutan los registros del guest **despues** del
		acceso (el post-incremento de @Rm+, el compromiso de @-Rn), asi que
		volcar tarde vuelca el mismo estado previo que volcar temprano.

		El contador de control es jit_sync_sin_consumir: una fila con accede
		que no llegue a ningun sitio de llamada seria una que puede faltar sin
		instantanea, asi que se descarta el bloque entero y se cuenta.
	*/
	jit_traduccion * sync_t;
	DWORD			 sync_pc;
	int				 sync_activa;
	int				 sync_usada;

	/*
		El talon de sincronizacion **por bloque**. El volcado son las mismas seis
		tiendas para todos los accesos del bloque --las cinco ranuras que ese
		bloque mapea, mas CYC--, asi que emitirlo entero en cada talon multiplica
		cuarenta bytes por acceso, y bajo MMU por DOS, que son los dos talones
		que abre cada acceso. Emitido una vez y llamado por rel32, cada talon
		queda en el `mov` del PC (que si es propio de la instruccion) y cinco
		bytes de `call`.

		El `call` es seguro adentro de un bloque aunque el arena tenga una sola
		informacion de desenrollado: el talon no llama a nadie ni toca la pila mas
		alla del retorno, asi que RSP vuelve a estar como lo espera la llamada al
		ayudante que sigue, y nada puede faltar mientras esta corrido.
	*/
	x64_parche		 sync_llam[JIT_MAX_INSTR * 2];
	int				 n_sync_llam;

	/*
		La direccion constante del acceso que viene, si la hay.

		Los literales de PC-relativo --`MOV.L @(d,PC),Rn` y su hermana de 16
		bits-- tienen la direccion decidida al traducir: es `(pc & ~3) + 4 +
		disp*4`. El valor se sigue leyendo, que el literal es dato; lo que se
		pliega es **el camino hasta el**. Con la direccion conocida, el indice de
		zona y el desplazamiento dentro de ella son constantes, y la guarda de
		alineacion sobra porque la formula ya alinea.

		Vale la pena por el censo: esa fila es el **13,87 % de las instrucciones
		de Crazy Taxi**, 6,21 % de DCDoom y 6,82 % de Sega Rally 2 sumando las
		dos anchuras.
	*/
	DWORD			 acc_dir;
	int				 acc_dir_valida;

	/* Emitir los bumps de la generacion de impureza (la elision de ociosos):
	   solo en bloques planos y con la palanca en 1 o 2. Los bloques MMU no los
	   llevan porque una cadena bajo MMU nunca llega a una sonda (v1). */
	int				 ocioso_bumps;
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
static void tr_sync(jit_gen * g, jit_traduccion * t, DWORD pc_k);
static void tr_sync_talon(jit_gen * g, jit_traduccion * t, DWORD pc_k);
static unsigned long long jit_bytes_sync;

/* El volcado de la sincronizacion, emitido UNA vez por bloque y llamado desde
   cada talon. DCEMU_JIT_SYNC_EN_CADA_TALON=1 lo vuelve a poner entero en cada
   uno --la forma anterior byte por byte-- y es el A/B del escalon. */
static int jit_sync_stub = 1;

/* El pliegue de los accesos con direccion constante (los literales de
   PC-relativo). DCEMU_JIT_SIN_DIR_CONSTANTE=1 lo apaga y reproduce la emision
   anterior byte por byte: es el A/B del escalon. */
static int jit_dir_constante = 1;

/*
	Emite la sincronizacion pendiente, si la hay. Va **inmediatamente antes de
	toda llamada a C**, que es el unico sitio desde donde una instruccion emitida
	puede faltar: los ayudantes de memoria expanden el macro de mem.h entero --
	traduccion, watchpoints, UBC, camino lento y el error de direccion-- y
	cualquiera de esos puede salir por longjmp.

	No la consume: un acceso abre dos talones --el fisico y el virtual-- y los
	dos tienen que llevarla. La marca de consumo la levanta el conductor cuando
	la fila termino de emitirse.
*/
static void gen_sync_pendiente(jit_gen * g)
{
	unsigned antes;

	if (!g->sync_activa)
		return;

	antes = jit_x64_largo(&g->e);

	/*
		El volcado es el mismo para todos los accesos del bloque --las ranuras que
		ese bloque mapea y CYC--, asi que se emite UNA vez al final y cada talon
		lo llama. Lo unico propio de la instruccion es el PC.

		Con DCEMU_JIT_SYNC_EN_CADA_TALON=1 vuelve a emitirse entero en cada talon,
		que es la forma anterior byte por byte y el brazo del A/B.
	*/
	if (jit_sync_stub
		&& g->n_sync_llam < (int) (sizeof(g->sync_llam) / sizeof(g->sync_llam[0])))
	{
		jit_x64_mov_mi(&g->e, CTX, O_PC, g->sync_pc);
		g->sync_llam[g->n_sync_llam++] = jit_x64_call(&g->e);
	}
	else
		tr_sync_talon(g, g->sync_t, g->sync_pc);

	jit_bytes_sync += jit_x64_largo(&g->e) - antes;
	g->sync_usada = 1;
}

static void gen_llamar(jit_gen * g, const void * destino, int disp_tabla)
{
	gen_sync_pendiente(g);

	/* Toda salida a C ensucia la vuelta (elision de ociosos). Clase ACCESO:
	   por aqui salen los ayudantes de lectura y escritura, el camino lento
	   de los accesos, PREF y los pares de FMOV. */
	if (g->ocioso_bumps)
		jit_x64_add64_mi(&g->e, CTX, D_OCIOSO_GEN(JIT_IMP_ACCESO), 1);

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
	unsigned char razon[12];	/* por que cayo ahi (la sonda de accesos) */
	int			n_lento;
	x64_parche	lento_fis[2];	/* al ayudante que NO traduce (ya es fisica) */
	unsigned char razon_fis[2];
	int			n_lento_fis;
	int			corto;			/* los saltos al camino lento caben en rel8 */
	x64_reg		fis;			/* que registro lleva la direccion fisica */
	x64_parche	listo;
	x64_parche	listo2;
} jit_acceso;

/*
	La sonda de accesos (DCEMU_JIT_SONDA_ACCESOS=1): cuantos accesos emitidos
	toma cada camino, y **por que guarda** cae al ayudante el que no toma el
	rapido. Es el censo que la fase 6 del plan del estado del arte necesita
	antes de escribirse: separa "el camino rapido cubre casi todo y lo que
	sobra es la zona no plana" de "se cae por la alineacion" o "por la pagina
	con codigo", que piden fastmem, o no lo piden, en distinta medida.

	Va en corrida aparte, como la sonda de cruces: cambia la emision (un
	incremento por guarda fallada) y por eso su tiempo no es una tanda. Los
	saltos pasan a rel32 mientras esta encendida -- los talones de la cuenta
	empujan el destino mas alla del alcance de un rel8, y un desborde
	invalidaria justo los bloques que se quieren censar, sesgando el censo
	hacia los cortos.
*/
#define JIT_RZ_ALINEACION	0
#define JIT_RZ_MODO			1	/* mmu_activa cambio dentro del bloque */
/* Las cinco guardas de la traduccion emitida, separadas: la etiqueta (ASID,
   modo y valida), el permiso, la VPN de la pagina y la generacion de la
   entrada de UTLB de la que salio. Juntas no distinguen "la cache es chica"
   de "el guest la invalida", que piden cosas distintas. */
#define JIT_RZ_TR_PROBAR	2
#define JIT_RZ_TR_ETIQUETA	3
#define JIT_RZ_TR_PERMISO	4
#define JIT_RZ_TR_VPN		5
#define JIT_RZ_TR_GEN		6
#define JIT_RZ_ZONA			7	/* la zona no es memoria plana */
#define JIT_RZ_PAG_CODIGO	8	/* escritura sobre pagina con codigo traducido */
#define JIT_RZ_N			9
/* El UBC de operandos tenia razon propia y ya no: su guarda se pliega en las
   tablas base, asi que un acceso con break armado se cuenta como "zona no
   plana", que es por donde efectivamente sale. */
/* Las dos guardas del atajo de P1/P2 no son una causa nueva: el acceso ya lo
   conto la guarda de cache que lo mando al talon. Cuentan como nada. */
#define JIT_RZ_NADA			JIT_RZ_N

static int					jit_sonda_accesos = 0;

/* El atajo de P1/P2 en la traduccion emitida (ver gen_traducir_mmu).
   DCEMU_JIT_SIN_ATAJO_P1P2=1 lo apaga y reproduce la emision anterior
   byte por byte: es el A/B de la fase y la linea base de antes. */
static int					jit_atajo_p1p2 = 1;

/* La etiqueta de la cache de traducciones, leida de mmu_etiqueta en vez de
   construida en cada acceso (ver gen_traducir_mmu y mmu.h).
   DCEMU_MMU_ETIQUETA_CALCULADA=1 la vuelve a construir y reproduce la emision
   anterior byte por byte: es el brazo del A/B. */
static int					jit_etiqueta_viva = 1;

/* El avance de URC, diferido a una cuenta en vez de un read-modify-write de
   MMUCR por acceso (ver gen_traducir_mmu y mmu.h). DCEMU_MMU_URC_INMEDIATO=1
   vuelve al avance completo y reproduce la emision anterior byte por byte. */
static int					jit_urc_diferido = 1;

/* La entrada de la cache de traducciones mide 32 bytes, asi que el indice se
   corre en vez de multiplicarse y la mascara viene ya negada (ver el struct en
   mmu.h). DCEMU_MMU_EMISION_VIEJA=1 vuelve al imul y al not, dejando como
   unica diferencia contra el arbol anterior la forma de la entrada. */
static int					jit_emision_vieja = 0;

/* El corte del bloque periodico, emitido como UNA comparacion contra el limite
   envenenable de intc.h en vez de dos contra la constante y la bandera.
   DCEMU_JIT_CORTE_VIEJO=1 vuelve a las dos y reproduce la emision anterior
   byte por byte: es el A/B del escalon. */
static int					jit_corte_viejo = 0;

/*
	DIV1 emitida en linea y sin ramas: **medida y APAGADA**, con
	DCEMU_JIT_DIV1_EMITIDA=1 para volver a encenderla.

	El censo la senalaba como el blanco mayor de DCDoom (4,91 % de sus
	instrucciones) y la aritmetica cerraba: cambiar una llamada al manejador --con
	sincronizacion, recarga de CYC y de las cinco ranuras-- por unas cuarenta
	instrucciones rectas. **Perdio su A/B con claridad**: +3,6 % en DOOM, rangos
	disjuntos y 4/4, sobre el canonico reentrenado `E1E565F833F4738B`.

	Y el arena descarta la explicacion facil: 49 474 303 bytes contra 49 239 871,
	o sea +0,48 %. No son los bytes: es que **las ramas del manejador se predicen
	bien** --M es fijo durante una division y el patron de Q lo aprende el
	predictor-- y la version sin ramas las cambia por una cadena de dependencias
	de quince pasos, con dos setcc/movzx y un lee-modifica-escribe de SR al final.
	Es la lección del cuerpo rapido del DSP otra vez, ahora en el SH-4: quitar
	ramas que ya se predicen no compra nada, y alargar la cadena cuesta.

	Queda porque la forma cerrada esta probada (tests/test_arith.c) y porque la
	variante que NO se probo --emitir el switch tal cual, con sus ramas, para
	ahorrar solo la llamada-- tiene aca la mitad del trabajo hecha.
*/
static int					jit_div1_emitida = 0;

/* Las dos guardas por acceso que el censo mostro muertas -- el cambio de modo
   del lado MMU y el break de operando del UBC -- se pliegan en otro lado.
   DCEMU_JIT_GUARDAS_VIEJAS=1 las vuelve a emitir y reproduce la emision
   anterior byte por byte: es el A/B del escalon y la linea base de antes. */
static int					jit_guardas_viejas = 0;

/* La rejilla de 64 bytes consultada en linea antes de desviar una escritura
   sobre una pagina con codigo. DCEMU_JIT_SIN_REJILLA=1 vuelve a desviar por
   pagina --la emision anterior byte por byte-- y es el A/B del escalon. */
static int					jit_rejilla_fina = 1;
static unsigned long long	jit_acc_total = 0;
static unsigned long long	jit_acc_razon[JIT_RZ_N + 1] = { 0 };
static unsigned long long	jit_acc_p1p2 = 0;	/* rescatados por el atajo */
/* Los dos exactos: cuantos accesos llegaron al ayudante. El censo por razon es
   diagnostico y puede contar dos veces uno que el atajo rescato y que despues
   fallo la zona; estos no, y son los que dan el porcentaje. */
static unsigned long long	jit_acc_lento = 0;
static unsigned long long	jit_acc_lento_fis = 0;

static const char * const jit_rz_nombre[JIT_RZ_N] =
{
	"desalineado", "cambio de modo",
	"trad: sondeo apagado", "trad: etiqueta", "trad: permiso",
	"trad: VPN", "trad: generacion", "zona no plana", "pagina con codigo"
};

/* Los tres modos de un acceso. PLANO y MMU son politica, no correccion: el
   ayudante siempre esta detras y decide todo lo que el camino rapido no cubre.
   Con la MMU encendida el camino plano nunca se tomaria, y con ella apagada la
   traduccion emitida seria codigo muerto -- por eso son modos y no uno solo.
   En la fase 1 el modo sale de la clave del bloque; aqui va a mano, que con dos
   bloques escritos a mano es lo mismo. */
#define JIT_ACC_LENTO	0
#define JIT_ACC_PLANO	1
#define JIT_ACC_MMU		2

static void gen_censar(jit_gen * g, const x64_parche * p,
	const unsigned char * razon, int n);

static x64_parche gen_guarda(jit_gen * g, jit_acceso * a, x64_cond cc)
{
	/* a == NULL: la emision compartida (la rutina de traduccion), donde las
	   guardas siempre son largas y aterrizan en la cola de fallo local. */
	return (a != NULL && a->corto)
		? jit_x64_jcc_corto(&g->e, cc) : jit_x64_jcc(&g->e, cc);
}

/* Las dos salidas al camino lento, anotando por que. Con la sonda apagada la
   razon no se usa y la emision es la de siempre, byte por byte. */
static void gen_lento(jit_gen * g, jit_acceso * a, x64_cond cc, int razon)
{
	a->razon[a->n_lento]   = (unsigned char) razon;
	a->lento[a->n_lento++] = gen_guarda(g, a, cc);
}

/* Una guarda de la cache de traducciones: se anota y se resuelve al final,
   junto con las demas. */
static void gen_trad_fallo(jit_gen * g, jit_acceso * a, x64_cond cc, int razon,
	x64_parche * fallo, unsigned char * fallo_rz, int * nf)
{
	(void) a;
	fallo_rz[*nf] = (unsigned char) razon;
	fallo[(*nf)++] = gen_guarda(g, a, cc);
}

/* Y su aterrizaje: un parche ya emitido entra a la lista del camino lento. */
static void gen_lento_parche(jit_acceso * a, x64_parche p, unsigned char razon)
{
	a->razon[a->n_lento]   = razon;
	a->lento[a->n_lento++] = p;
}

static void gen_lento_fis(jit_gen * g, jit_acceso * a, x64_cond cc, int razon)
{
	a->razon_fis[a->n_lento_fis]   = (unsigned char) razon;
	a->lento_fis[a->n_lento_fis++] = gen_guarda(g, a, cc);
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
/*
	El atajo de P1/P2, factorizado porque lo emiten dos sitios y tienen que
	ser el mismo byte a byte: la traduccion en linea (el brazo del A/B) y el
	sitio que llama a la rutina compartida. Devuelve el salto del acierto,
	que el llamador fija donde la fisica ya esta en R11D.
*/
static x64_parche gen_atajo_p1p2(jit_gen * g)
{
	x64_parche traducir, p1p2;

	/* bits 31:30 == 10b es P1/P2 */
	jit_x64_mov_rr(&g->e, X64_RAX, X64_RCX);
	jit_x64_shr_ri(&g->e, X64_RAX, 30);
	jit_x64_cmp_ri(&g->e, X64_RAX, 2);
	traducir = jit_x64_jcc(&g->e, X64_NE);

	jit_x64_mov_rr(&g->e, X64_R11, X64_RCX);

	/*
		El atajo cuenta el intento **y** lo cuenta como «no se traduce»: sin lo
		segundo, un acceso a P1/P2 --que no sondea nada-- entraba al resumen
		como una traduccion SIN acierto, o sea como un fallo de la cache. En
		DCDoom eso son ~534 M de accesos en 20 s emulados contra 81 M de fallos
		de verdad, asi que la tasa de aciertos salia siete veces peor de lo que
		es y «el 84,5 % de los fallos no se traduce» describia el contador, no
		la cache. Ver perf.c, donde el porcentaje se calcula ahora sobre las
		que si se traducen.
	*/
	if (perf_activa)
	{
		jit_x64_add64_mi(&g->e, CTX, D_PERF_TRADUCE, 1);
		jit_x64_add64_mi(&g->e, CTX, D(&perf_mmu_datos_sin_trad), 1);
	}

	if (jit_sonda_accesos)
		jit_x64_add64_mi(&g->e, CTX, D(&jit_acc_p1p2), 1);

	p1p2 = jit_x64_jmp(&g->e);
	jit_x64_fijar(&g->e, traducir);
	return p1p2;
}

static void gen_traducir_mmu(jit_gen * g, jit_acceso * a, unsigned permiso_bit)
{
	x64_parche    sin_urb;
	x64_parche    p1p2;
	x64_parche    fallo[5];
	unsigned char fallo_rz[5];
	int           nf = 0;
	const int     DAT = D_MMU_DATOS;
	const int     compartido = (a == NULL);

	/*
		P1 y P2 **no se traducen nunca**, y por eso no son clientela de la
		cache de traducciones: mmu_traducir() las devuelve tal cual antes de
		mirar la UTLB, asi que su ranura queda sin estrenar para siempre y
		cada acceso volvia a pagar el viaje al ayudante. El censo de accesos
		emitidos lo midio: **el 98 % de los fallos de traduccion de DCDoom y
		el 96,6 % de los de Sega Rally 2 son estas direcciones**, o sea un
		tercio de todos los accesos emitidos de DCDoom.

		**Va adelante de la consulta, y la posicion la decidieron dos tandas.**
		Detras del fallo de cache sale mas barato para el que acierta, y se
		midio: Sega Rally 2 pasaba de +1,1 % (solapado, o sea ruido) a neutro,
		pero DCDoom perdia la mitad de su ganancia -- de −5,9 % a −2,8 %, las
		dos con rangos disjuntos --, porque con un tercio de sus accesos en
		P1/P2 obligarlos a recorrer indice, etiqueta y cuatro comparaciones
		antes del atajo se paga. Se queda la version que gana donde la medicion
		distingue, y se abarata para el que no la necesita (abajo).

		**El modo se resuelve al emitir, no al correr.** SR.MD vive en la clave
		de validez (`jit_validez`, jit.h), asi que un bloque solo se despacha en
		el modo en que se tradujo: en modo usuario no se emite atajo alguno y
		P1/P2 cae al ayudante, que levanta el error de direccion como siempre.
		Eso deja el atajo en cuatro instrucciones en vez de siete.

		Lo que se emite es la misma decision de mmu_traducir(): 0x80000000 <=
		dir < 0xC0000000 -> la fisica ES la virtual. **No avanza URC**: el
		camino en C vuelve antes de tocar la UTLB, y avanzarlo aqui cambiaria
		que entrada reemplaza el proximo LDTLB del guest.
	*/
	p1p2.sitio = NULL;

	if (!compartido && jit_atajo_p1p2 && jit_md_visto)
		p1p2 = gen_atajo_p1p2(g);

	/* if (!mmu_macro_probar) -> mmu_traducir() */
	jit_x64_cmp_mi(&g->e, CTX, D_MACRO_PROBAR, 0);
	if (compartido)
		gen_trad_fallo(g, a, X64_E, JIT_RZ_TR_PROBAR, fallo, fallo_rz, &nf);
	else
		gen_lento(g, a, X64_E, JIT_RZ_TR_PROBAR);

	/*
		r9 = MMU_DATOS_INDICE(dir) * sizeof(mmu_datos_t), en bytes: el indice
		viaja ya multiplicado y la escala del SIB es 1. Es el indice de mmu.h y
		tiene que dar la misma ranura, o la emitida buscaria donde la otra no
		guarda.

		**La entrada mide 32 bytes, asi que esto es un corrimiento** (2026-09-09).
		Media 28 -- no potencia de dos -- y el `imul` de tres ciclos iba
		ADELANTE de la primera carga, o sea en la cabeza de la cadena que
		decide el camino rapido. Ver el comentario del struct en mmu.h: el
		campo que la lleva a 32 no es relleno, es la mascara ya negada.

		DCEMU_MMU_EMISION_VIEJA=1 vuelve a multiplicar (por 32, el tamano de
		hoy) y vuelve a negar la mascara al vuelo: deja como unica diferencia
		contra el arbol anterior la FORMA de la entrada, que es lo que no se
		puede aislar dentro de un binario.
	*/
	jit_x64_mov_rr(&g->e, X64_RAX, X64_RCX);
	jit_x64_shr_ri(&g->e, X64_RAX, 12);
	jit_x64_and_rm(&g->e, X64_RAX, CTX, D_DATOS_MASCARA);
	if (jit_emision_vieja)
		jit_x64_imul_rri(&g->e, X64_RAX, X64_RAX, (int) sizeof(mmu_datos_t));
	else
		jit_x64_shl_ri(&g->e, X64_RAX, MMU_DATOS_DESP);
	jit_x64_mov_rr(&g->e, X64_R9, X64_RAX);

	/*
		edx = la etiqueta vigente.

		**Una carga y no diez instrucciones** (2026-09-08). La etiqueta es
		`ASID_DE(*PTEH) | ((SR_MD == 0) << 8) | MMU_CACHE_VALIDA` y se
		construia aca, en cada acceso: dos cargas DEPENDIENTES --el puntero a
		PTEH y despues PTEH-- mas ocho operaciones, y todo eso alimentando
		justo la comparacion que decide el camino rapido. Ahora la mantiene
		mmu_etiqueta_recalcular() en los tres sitios que pueden moverla (las
		dos entradas de UpdateSR y la escritura del guest a PTEH), y el bloque
		periodico verifica la coherencia una vez por servicio. Ver mmu.h.

		DCEMU_MMU_ETIQUETA_CALCULADA=1 vuelve a construirla aqui y reproduce
		la emision anterior byte por byte: es el brazo del A/B.
	*/
	if (!jit_etiqueta_viva)
	{
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
	}
	else
		jit_x64_mov_rm(&g->e, X64_RDX, CTX, D_ETIQUETA);

	/* etiqueta */
	jit_x64_cmp_rm_idx(&g->e, X64_RDX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, etiqueta));
	gen_trad_fallo(g, a, X64_NE, JIT_RZ_TR_ETIQUETA, fallo, fallo_rz, &nf);

	/* permisos */
	jit_x64_test_mi_idx(&g->e, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, permisos), (int) permiso_bit);
	gen_trad_fallo(g, a, X64_E, JIT_RZ_TR_PERMISO, fallo, fallo_rz, &nf);

	/* (dir & mascara_neg) == vpn. La mascara vive negada en la entrada, asi
	   que el camino rapido no la niega: ver el struct en mmu.h. */
	if (jit_emision_vieja)
	{
		jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
			DAT + (int) offsetof(mmu_datos_t, mascara));
		jit_x64_not_r(&g->e, X64_RAX);
	}
	else
		jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
			DAT + (int) offsetof(mmu_datos_t, mascara_neg));
	jit_x64_and_rr(&g->e, X64_RAX, X64_RCX);
	jit_x64_cmp_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, vpn));
	gen_trad_fallo(g, a, X64_NE, JIT_RZ_TR_VPN, fallo, fallo_rz, &nf);

	/* mmu_utlb_gen[entrada] == gen */
	jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, entrada));
	jit_x64_mov_rm_idx(&g->e, X64_RAX, CTX, X64_RAX, 4, D_UTLB_GEN);
	jit_x64_cmp_rm_idx(&g->e, X64_RAX, CTX, X64_R9, 1,
		DAT + (int) offsetof(mmu_datos_t, gen));
	gen_trad_fallo(g, a, X64_NE, JIT_RZ_TR_GEN, fallo, fallo_rz, &nf);

	/*
		--- MMU_URC_AVANZAR() ---

		**Una suma y no veinte instrucciones** (2026-09-08). El avance era un
		read-modify-write de MMUCR --que vive adentro de regmem, a 16 MB del
		contexto y en una linea de cache que no toca nada mas-- con la carga
		del puntero, la carga dependiente del registro, la extraccion de URC y
		de URB, dos ramas y el almacenamiento, y todo eso en CADA acceso a la
		UTLB. Diferido, es sumarle uno a una cuenta que vive al lado del
		contexto; el valor se materializa en los tres sitios que lo miran.
		Ver mmu.h.

		DCEMU_MMU_URC_INMEDIATO=1 vuelve al avance completo y reproduce la
		emision anterior byte por byte.
	*/
	if (jit_urc_diferido)
		jit_x64_add64_mi(&g->e, CTX, D_URC_PEND, 1);
	else
	{
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
	}

#ifdef DCEMU_SONDA_URC
	/* La sonda de conservacion: el avance emitido se cuenta y deja su
	   virtual, para que el punto de control acote el sitio (ver mmu.h). */
	jit_x64_add64_mi(&g->e, CTX, D(&mmu_sonda_ue), 1);
	jit_x64_mov_mr(&g->e, CTX, D(&mmu_sonda_uv), X64_RCX);
#endif

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

	/* La forma compartida (la rutina): acierto -> EAX=1 con la fisica en
	   R11D; cualquier guarda -> EAX=0, y el SITIO decide el camino lento.
	   ECX (la virtual) se conserva en los dos desenlaces, como en linea. */
	if (compartido)
	{
		int i;

		jit_x64_mov_ri(&g->e, X64_RAX, 1);
		jit_x64_ret(&g->e);

		for (i = 0; i < nf; i++)
			jit_x64_fijar(&g->e, fallo[i]);

		jit_x64_xor_rr(&g->e, X64_RAX, X64_RAX);
		jit_x64_ret(&g->e);
		return;
	}

	/* El talon del fallo: aca llegan las cuatro guardas de la cache. Si la
	   direccion es de P1/P2 y el modo es privilegiado, la fisica es ella
	   misma y el acceso sigue por el camino rapido; si no, al ayudante. */
	/* Las guardas de la cache van derecho al camino lento: el atajo ya
	   decidio antes de consultarla. */
	{
		int i;

		for (i = 0; i < nf; i++)
			gen_lento_parche(a, fallo[i], fallo_rz[i]);
	}

	/* Y aca se juntan los dos: el atajo dejo la fisica en R11D. Con el
	   atajo apagado no hay sitio que fijar y fijar() lo ignora. */
	jit_x64_fijar(&g->e, p1p2);
}

/*
	Las rutinas compartidas de la traduccion (lectura y escritura), emitidas
	UNA vez al frente del arena y llamadas por rel32 desde cada sitio de
	acceso MMU. El censo de bytes (DCEMU_JIT_SONDA_BYTES) midio el porque:
	la traduccion en linea costaba +260/+300 bytes por acceso (un MOV.L a
	320-390 bytes contra los 62 del modo plano) y las plantillas de acceso
	eran ~60 % de los 170-200 MB del arena de los guests MMU -- presion de
	icache pura, el sospechoso del costo por instruccion 2x de SR2. El sitio
	conserva el atajo P1/P2 en linea (el caso dominante de DOOM) y decide su
	propio camino lento con el EAX que la rutina devuelve; ECX (la virtual)
	sobrevive en los dos desenlaces, como en linea. La sonda de accesos
	fuerza la forma en linea, porque sus razones son por sitio. Se emiten al
	frente de tr_traducir -- nunca en medio de un bloque, que comparte el
	arena. DCEMU_JIT_TRAD_EN_LINEA=1 vuelve a la emision en linea entera: el
	brazo del A/B y la linea base anterior.
*/
static const unsigned char *	jit_rut_trad[2] = { NULL, NULL };
static int						jit_trad_en_linea = 0;
static unsigned long long		jit_arena_lleno = 0;	/* propuestas sin arena */

static void jit_rut_trad_emitir(void)
{
	jit_gen g;
	int     k;

	if (jit_rut_trad[0] != NULL || jit_trad_en_linea || jit_sonda_accesos
		|| jit_codigo == NULL)
		return;

	jit_codigo_us = (jit_codigo_us + 15u) & ~15u;

	if (jit_codigo_us + 4096 > jit_codigo_tam)
	{
		jit_trad_en_linea = 1;
		return;
	}

	memset(&g, 0, sizeof(g));
	jit_x64_iniciar(&g.e, jit_codigo + jit_codigo_us,
		jit_codigo_tam - jit_codigo_us);

	for (k = 0; k < 2; k++)
	{
		jit_rut_trad[k] = jit_x64_aqui(&g.e);
		gen_traducir_mmu(&g, NULL, k ? MMU_DATOS_ESCRIBIR : MMU_DATOS_LEER);
	}

	if (g.e.desborde)
	{
		jit_rut_trad[0] = jit_rut_trad[1] = NULL;
		jit_trad_en_linea = 1;
		return;
	}

	jit_codigo_us += jit_x64_largo(&g.e);
	jit_codigo_us  = (jit_codigo_us + 15u) & ~15u;
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
	a->corto   = (modo != JIT_ACC_MMU) && !jit_sonda_accesos;
	a->fis     = (modo == JIT_ACC_MMU) ? X64_R11 : X64_RCX;

	if (jit_sonda_accesos)
		jit_x64_add64_mi(&g->e, CTX, D(&jit_acc_total), 1);

	/* Con la direccion decidida al emitir, la guarda de alineacion se decide
	   tambien: la formula del literal de PC-relativo ya alinea, asi que la
	   prueba no puede fallar y no se emite. Si alguna vez llegara una
	   constante desalineada, se emite igual y el ayudante levanta el error de
	   direccion por el camino de siempre. */
	if (alineacion
		&& !(g->acc_dir_valida && (g->acc_dir & (DWORD) alineacion) == 0))
	{
		jit_x64_test_ri8(&g->e, X64_RCX, alineacion);
		gen_lento(g, a, X64_NE, JIT_RZ_ALINEACION);
	}

	/*
		**La guarda de modo sobrevive solo del lado plano**, y el censo de
		accesos es lo que lo decidio: no se dispara ni una vez en los tres
		guests. La emision ya sabe el modo -- el despacho rechaza un bloque
		cuyo `mmu` no case (jit_despachar) --, asi que lo unico que la guarda
		cubria era que el guest encendiera o apagara la MMU **dentro** del
		bloque que la escribe.

		Del lado MMU eso ya tiene respaldo y no hace falta pagarlo por acceso:
		escribir MMUCR pasa por mmu_mmucr_escrito(), que llama a
		mmu_tlb_invalidar() y **vacia mmu_datos entero**. Con la cache vacia
		ningun acceso emitido acierta, todos caen al ayudante -- que es el
		macro y mira mmu_activa de verdad -- y el unico camino que sigue vivo
		es el atajo de P1/P2, que da la misma fisica con la MMU encendida o
		apagada porque mmu_traducir() devuelve P1/P2 sin tocar la UTLB. URC
		tampoco se mueve: ni el atajo lo avanza ni el macro con la MMU apagada.

		Del lado plano no hay tal respaldo: la tabla de zonas contesta con base
		directa para 0x0C aunque la traduccion se acabe de encender, asi que la
		comparacion se queda. Es una sola, y es el guest sin MMU el que la paga.
	*/
	if (modo != JIT_ACC_MMU || jit_guardas_viejas)
	{
		jit_x64_cmp_mi(&g->e, CTX, D_MMU, 0);
		gen_lento(g, a, (modo == JIT_ACC_MMU) ? X64_E : X64_NE, JIT_RZ_MODO);
	}

	/*
		El break de operando del UBC **ya no se pregunta aqui**: se pliega en
		mem_base_lectura/escritura, igual que el watchpoint y por lo mismo (ver
		mem_directo_recalcular()). La prueba de zona que ya se hace mas abajo
		lo cubre sola, y con el UBC armado el acceso sale por los ayudantes,
		que corren el gancho con la virtual (jit_ubc_fis).

		Es exacto dentro del bloque que arma el break, no solo entre bloques:
		la tabla se lee al correr, asi que el acceso siguiente a la escritura
		de BBRA/BBRB ya la ve en NULL.
	*/
	if (jit_guardas_viejas)
	{
		jit_x64_cmp_mi(&g->e, CTX, D_UBC_OP, 0);
		gen_lento(g, a, X64_NE, JIT_RZ_ZONA);
	}

	if (modo == JIT_ACC_MMU)
	{
		const int ke = (disp_tabla == D_BASE_ESC);

		if (jit_rut_trad[ke] == NULL)
			gen_traducir_mmu(g, a,
				ke ? MMU_DATOS_ESCRIBIR : MMU_DATOS_LEER);
		else
		{
			/* La forma compartida: el atajo P1/P2 en linea (los mismos bytes
			   que en gen_traducir_mmu), la rutina por rel32, y el fallo al
			   camino lento del sitio. JIT_RZ_NADA porque la razon fina vive
			   en la rutina y la sonda de accesos corre en linea. */
			x64_parche p1p2;

			p1p2.sitio = NULL;

			if (jit_atajo_p1p2 && jit_md_visto)
				p1p2 = gen_atajo_p1p2(g);

			if (!jit_x64_call_directo(&g->e, jit_rut_trad[ke]))
				g->e.desborde = 1;

			jit_x64_test_rr(&g->e, X64_RAX, X64_RAX);
			gen_lento(g, a, X64_E, JIT_RZ_NADA);

			jit_x64_fijar(&g->e, p1p2);
		}
	}

	/* rax = la base de la zona; r8 = el desplazamiento dentro de ella.

	   Con la direccion constante y sin MMU, las dos cosas son constantes: el
	   indice de zona se pliega en el desplazamiento de la carga de la tabla y el
	   offset es un inmediato. La prueba de base nula se queda --la tabla cambia
	   cuando se arma un watchpoint o un break de operando-- y es lo unico que
	   sigue mirando el estado. */
	if (g->acc_dir_valida && modo != JIT_ACC_MMU)
	{
		jit_x64_mov64_rm(&g->e, X64_RAX, CTX,
			disp_tabla + (int) ((g->acc_dir >> 24) * 8));
		jit_x64_test64_rr(&g->e, X64_RAX, X64_RAX);
		gen_lento(g, a, X64_E, JIT_RZ_ZONA);
		jit_x64_mov_ri(&g->e, X64_R8, (int) (g->acc_dir & 0x00FFFFFFul));
		return;
	}

	jit_x64_mov_rr(&g->e, X64_RAX, a->fis);
	jit_x64_shr_ri(&g->e, X64_RAX, 24);
	jit_x64_mov64_rm_idx(&g->e, X64_RAX, CTX, X64_RAX, 8, disp_tabla);
	jit_x64_test64_rr(&g->e, X64_RAX, X64_RAX);

	/* Aca la traduccion ya corrio y ya avanzo URC: volver por el ayudante que
	   traduce la avanzaria de nuevo, asi que esta salida usa la fisica. Sin
	   MMU no hay traduccion y da lo mismo, asi que va por el camino comun. */
	if (modo == JIT_ACC_MMU)
		gen_lento_fis(g, a, X64_E, JIT_RZ_ZONA);
	else
		gen_lento(g, a, X64_E, JIT_RZ_ZONA);

	jit_x64_mov_rr(&g->e, X64_R8, a->fis);
	jit_x64_and_ri(&g->e, X64_R8, 0x00FFFFFF);
}

/*
	El aterrizaje de un juego de guardas. Sin sonda es lo que fue siempre: las
	guardas caen todas en el mismo punto. Con la sonda cada una cae en su
	propio talon, que suma uno a su contador y salta al comun -- asi el censo
	dice POR QUE se fue al ayudante, que es la pregunta que la fase 6 hace.
*/
static void gen_censar(jit_gen * g, const x64_parche * p,
	const unsigned char * razon, int n)
{
	x64_parche	al_comun[12];
	int			i;

	if (!jit_sonda_accesos)
	{
		for (i = 0; i < n; i++)
			jit_x64_fijar(&g->e, p[i]);

		return;
	}

	for (i = 0; i < n; i++)
	{
		jit_x64_fijar(&g->e, p[i]);
		jit_x64_add64_mi(&g->e, CTX, D(&jit_acc_razon[razon[i]]), 1);
		al_comun[i] = jit_x64_jmp(&g->e);
	}

	for (i = 0; i < n; i++)
		jit_x64_fijar(&g->e, al_comun[i]);
}

/*
	Cierra el camino rapido y abre los lentos: primero el fisico --la
	traduccion ya corrio, hay que entrar por la zona sin volver a traducir-- y
	despues el virtual, que es el macro entero.
*/
static void gen_rapido_fin(jit_gen * g, jit_acceso * a, int disp_fis,
	const void * ayudante_fis, jit_valor_f val, void * ctx)
{
	a->listo  = jit_x64_jmp(&g->e);
	a->listo2 = a->listo;
	a->listo2.sitio = NULL;

	if (a->n_lento_fis)
	{
		gen_censar(g, a->lento_fis, a->razon_fis, a->n_lento_fis);

		if (jit_sonda_accesos)
			jit_x64_add64_mi(&g->e, CTX, D(&jit_acc_lento_fis), 1);

		/* La virtual, para el gancho del UBC que estos ayudantes corren: la
		   fisica la reemplaza en ECX y el break compara la virtual. Va sobre
		   el camino lento, no sobre el rapido. Con las guardas viejas el
		   ayudante fisico no es alcanzable con un break armado --su guarda
		   desvia antes-- y no se emite, que es lo que hace de la palanca una
		   reproduccion byte por byte. */
		if (!jit_guardas_viejas)
			jit_x64_mov_mr(&g->e, CTX, D(&jit_ubc_virtual), X64_RCX);

		jit_x64_mov_rr(&g->e, X64_RCX, a->fis);

		if (val != NULL)
			val(g, ctx, X64_RDX);

		gen_llamar(g, ayudante_fis, disp_fis);
		a->listo2 = jit_x64_jmp(&g->e);
	}

	gen_censar(g, a->lento, a->razon, a->n_lento);

	if (jit_sonda_accesos)
		jit_x64_add64_mi(&g->e, CTX, D(&jit_acc_lento), 1);
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

		/*
			**La rejilla fina, en linea.** La pagina dice si hay codigo en
			esos 4 KB, no si lo escrito ES codigo, y en Windows CE los datos
			viven en las mismas paginas: 90 616 485 escrituras de 20 segundos
			de DCDoom caen en una pagina con codigo y **ni una sola** toca una
			linea de 64 bytes que lo tenga. Bajar al ayudante por eso son
			noventa millones de llamadas para nada.

			Preguntarle a la rejilla cuesta cuatro instrucciones mas y un
			acceso a 256 KB, pero solo en esas -- el caso comun sale por el
			salto de arriba sin tocarla. Y no hace falta mirar la cola: el
			acceso del guest va alineado (el error de direccion lo filtra
			antes de llegar aqui) y ninguno de 1, 2 o 4 bytes cruza un limite
			de 64.
		*/
		if (jit_rejilla_fina)
		{
			x64_parche limpia = jit_x64_jcc_corto(&g->e, X64_E);

			jit_x64_lea64_idx(&g->e, X64_R9, X64_RAX, X64_R8, 1, 0);
			jit_x64_shift64_ri(&g->e, X64_SHR, X64_R9, 6);
			jit_x64_alu_ri(&g->e, X64_AND, X64_R9, 0x3FFFF);
			jit_x64_cmp8_mi_idx(&g->e, CTX, X64_R9, 1, D_LIN_CODIGO, 0);

			if (a.n_lento_fis)
				gen_lento_fis(g, &a, X64_NE, JIT_RZ_PAG_CODIGO);
			else
				gen_lento(g, &a, X64_NE, JIT_RZ_PAG_CODIGO);

			jit_x64_fijar(&g->e, limpia);
		}
		else if (a.n_lento_fis)
			gen_lento_fis(g, &a, X64_NE, JIT_RZ_PAG_CODIGO);
		else
			gen_lento(g, &a, X64_NE, JIT_RZ_PAG_CODIGO);

		val(g, ctx, X64_RDX);

		if (ancho == 4)
			jit_x64_mov_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, X64_RDX);
		else if (ancho == 2)
			jit_x64_mov16_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, X64_RDX);
		else
			jit_x64_mov8_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, X64_RDX);

		/* La escritura ensucia la vuelta (elision de ociosos): una vuelta con
		   los mismos registros que la anterior pero con una tienda adentro
		   puede estar acumulando en RAM (leer, sumar, guardar, y el registro
		   vuelve a cero), asi que la instantanea de registros no la cubre. Es
		   por fila y no por bloque a proposito: el descubridor no corta tras
		   una rama sin par, y una marca en la cabeza del bloque daria por
		   impura una vuelta por las tiendas de una cola que no corrio. */
		if (g->ocioso_bumps)
			jit_x64_add64_mi(&g->e, CTX, D_OCIOSO_GEN(JIT_IMP_ESCRITURA), 1);

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

	/*
		El buscador: el despacho emitido. El epilogo de los bloques salta aqui
		en vez de a tramp_salir, y si el proximo bloque ya existe y sigue
		valido, se entra **sin salir del marco** -- sin las ocho sacadas, el
		ret, la vuelta de C y las ocho empujadas. Lo que el redespacho por
		ayudante C no podia dar (pagaba una llamada por intento), esto lo da
		en ~10 comparaciones en linea; cualquier cosa rara cae a tramp_salir
		y el despachador C hace lo de siempre, incluida la re-sonda.

		Reproduce EXACTAMENTE las condiciones de jit_despachar entre bloques:
		el corte del bloque periodico primero (la condicion de main_loop), el
		enseñado pendiente de un indirecto (va por C), la sonda 0 del hash
		(colisiones a C), y la validacion entera -- pc, validez, modo MMU,
		clave FPU y el puntero de busqueda. El puntero solo se COMPARA: el
		caso con MMU cae a C, donde MMU_FETCH_PUNTERO hace su resolucion con
		efectos (avance de URC) una sola vez, como hoy. El muestreo de calor
		no se pierde: solo muestrea PCs sin marcar, y un PC con bloque ya
		esta marcado.
	*/
	if (jit_buscador_activo)
	{
		x64_parche salir[12];
		x64_parche fpu_ok;
		int        ns = 0;

		jit_buscador = jit_x64_aqui(&g.e);

		jit_x64_cmp_ri(&g.e, CYC, RELOJ_GRANO);
		salir[ns++] = jit_x64_jcc(&g.e, X64_AE);
		jit_x64_cmp_mi(&g.e, CTX, D_REINTENTO, 0);
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);

		/* Un indirecto fallado deja su sitio anotado para que C lo enseñe.
		   Tragarlo y despachar igual solo POSPONE la enseñanza -- el ~2 % de
		   transiciones que cae a C por el corte la hace a las pocas vueltas
		   -- y deja al buscador sirviendo la salida mas comun del parque: el
		   RTS con varios llamadores, que falla su guarda aprendida. Retirarse
		   aqui era regalar justo la clientela (medido: tanda neutra). */
		jit_x64_mov_mi(&g.e, CTX, D(&jit_ult_sitio), (unsigned) -1);

		/* pc -> sonda 0 del hash -> b */
		jit_x64_mov_rm(&g.e, X64_RCX, CTX, O_PC);
		jit_x64_mov_rr(&g.e, X64_RAX, X64_RCX);
		jit_x64_shr_ri(&g.e, X64_RAX, 1);
		jit_x64_imul_rri(&g.e, X64_RAX, X64_RAX, (int) 2654435761u);
		jit_x64_shr_ri(&g.e, X64_RAX, 32 - JIT_HASH_BITS);
		/* Tabla de ints desde el tope de 65 536: carga de 32 bits, paso 4.
		   El -1 de ranura vacia deja el bit de signo puesto igual. */
		jit_x64_mov_rm_idx(&g.e, X64_RAX, CTX, X64_RAX, 4,
			D(&jit_hash[0]));
		jit_x64_test_rr(&g.e, X64_RAX, X64_RAX);
		salir[ns++] = jit_x64_jcc(&g.e, X64_S);
		jit_x64_imul_rri(&g.e, X64_RAX, X64_RAX, (int) sizeof(jit_bloque));
		jit_x64_lea64_idx(&g.e, X64_R8, CTX, X64_RAX, 1, D(&jit_bloques[0]));

		jit_x64_cmp_rm(&g.e, X64_RCX, X64_R8,
			(int) offsetof(jit_bloque, pc));
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);

		/* validez, modo MMU (mmu_activa + 1 == JIT_ACC_*), clave FPU */
		jit_x64_mov_rm(&g.e, X64_RDX, CTX, D_EPOCA);
		jit_x64_cmp_rm(&g.e, X64_RDX, X64_R8,
			(int) offsetof(jit_bloque, epoca));
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);

		jit_x64_mov_rm(&g.e, X64_RDX, CTX, D_MMU);
		jit_x64_add_ri(&g.e, X64_RDX, 1);
		jit_x64_cmp_rm(&g.e, X64_RDX, X64_R8,
			(int) offsetof(jit_bloque, mmu));
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);

		jit_x64_mov_rm(&g.e, X64_RDX, X64_R8,
			(int) offsetof(jit_bloque, fpu));
		jit_x64_cmp_ri(&g.e, X64_RDX, -1);
		fpu_ok = jit_x64_jcc_corto(&g.e, X64_E);
		jit_x64_cmp_rm(&g.e, X64_RDX, CTX, D(&jit_fpu_visto));
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);
		jit_x64_fijar(&g.e, fpu_ok);

		/* El puntero de busqueda, solo sin MMU (v1): mem_zone[pc>>24] mas el
		   desplazamiento. Con MMU se cae a C, que resuelve con efectos. */
		jit_x64_cmp_mi(&g.e, CTX, D_MMU, 0);
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);
		jit_x64_mov_rr(&g.e, X64_RAX, X64_RCX);
		jit_x64_shr_ri(&g.e, X64_RAX, 24);
		jit_x64_mov64_rm_idx(&g.e, X64_RAX, CTX, X64_RAX, 8,
			D(&mem_zone[0]));
		jit_x64_mov_rr(&g.e, X64_RDX, X64_RCX);
		jit_x64_and_ri(&g.e, X64_RDX, 0x00FFFFFF);
		jit_x64_lea64_idx(&g.e, X64_RAX, X64_RAX, X64_RDX, 1, 0);
		jit_x64_cmp64_rm(&g.e, X64_RAX, X64_R8,
			(int) offsetof(jit_bloque, ptr));
		salir[ns++] = jit_x64_jcc(&g.e, X64_NE);

		/* Los contadores del control de trabajo, como en el despachador. */
		jit_x64_add64_mi(&g.e, CTX, D(&jit_entradas), 1);
		jit_x64_add64_mi(&g.e, X64_R8, (int) offsetof(jit_bloque, veces), 1);

		jit_x64_mov64_rm(&g.e, X64_RAX, X64_R8,
			(int) offsetof(jit_bloque, codigo));
		jit_x64_jmp_r(&g.e, X64_RAX);

		for (i = 0; i < ns; i++)
			jit_x64_fijar(&g.e, salir[i]);

		jit_x64_jmp_a(&g.e, jit_tramp_salir);
	}

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
/*
	La condicion del corte, emitida. Es la de main_loop -- `cycles >= RELOJ_GRANO
	|| intc_sh4_reintentar` -- y se emitia tal cual: dos comparaciones y dos
	saltos, uno de ellos TOMADO en el camino comun para saltear el talon de
	salida que quedaba en medio del codigo caliente.

	Ahora es **una sola comparacion contra un limite envenenable**: quien arma el
	reintento pone el limite en cero (INTC_PEDIR_REINTENTO en intc.h), asi que
	`CYC >= limite` vale exactamente cuando valia la disyuncion. Dos
	instrucciones en vez de cuatro, en casi todas las fronteras del bloque.

	Con DCEMU_JIT_CORTE_VIEJO=1 vuelve la forma de dos comparaciones, byte por
	byte, que es el brazo del A/B.
*/
static void gen_corte_condicion(jit_gen * g, x64_parche * cortar, int * n)
{
	if (jit_corte_viejo)
	{
		jit_x64_cmp_ri(&g->e, CYC, RELOJ_GRANO);
		cortar[(*n)++] = jit_x64_jcc_corto(&g->e, X64_AE);
		jit_x64_cmp_mi(&g->e, CTX, D_REINTENTO, 0);
		return;
	}

	jit_x64_cmp_rm32(&g->e, CYC, CTX, D_LIMITE);
}

static void gen_corte(jit_gen * g, DWORD pc_sig)
{
	x64_parche a_cortar[1], sigue;
	int n = 0;

	gen_corte_condicion(g, a_cortar, &n);
	sigue = jit_x64_jcc_corto(&g->e, jit_corte_viejo ? X64_E : X64_B);

	while (n--)
		jit_x64_fijar(&g->e, a_cortar[n]);

	gen_salir_en(g, pc_sig);
	jit_x64_fijar(&g->e, sigue);
}

/*
	El corte de epoca, tras una escritura en un bloque con MMU.

	Una escritura del guest a PTEH/MMUCR o los arreglos por P4 -- la
	conmutacion de contexto de WinCE -- o sobre una pagina con codigo
	traducido, invalida la pagina vigente de busqueda y mueve la validez
	global. El interprete, en la instruccion siguiente, hace el recorrido de
	busqueda con su avance de URC; un bloque que siga de largo se lo saltea y
	URC queda corrido en uno -- el "acceso-con-avance de mas" que midio la
	conservacion del expediente. Salir aqui, en la frontera de la instruccion,
	pone al despachador a reponer exactamente esa busqueda.

	La comparacion es la de la guarda de cadena: la validez global contra el
	campo del bloque corriente, que toda entrada -- directa o encadenada --
	acaba de igualar. El bloque corriente es jit_bloques[jit_n_bloques]: se
	asigna despues de emitir, pero el indice no se mueve durante la emision (y
	si la emision falla, el codigo se descarta entero).
*/
static void gen_corte_epoca(jit_gen * g, DWORD pc_sig)
{
	x64_parche sigue;

	jit_x64_mov_rm(&g->e, X64_RAX, CTX, D_EPOCA);
	jit_x64_alu_rm(&g->e, X64_CMP, X64_RAX, CTX,
		D(&jit_bloques[jit_n_bloques].epoca));
	sigue = jit_x64_jcc_corto(&g->e, X64_E);
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

	/* Con el buscador emitido, la salida del bloque intenta despachar el
	   siguiente sin salir del marco; sin el (o si fallo su emision), a
	   tramp_salir como siempre. */
	jit_x64_jmp_a(&g->e,
		jit_buscador != NULL ? jit_buscador : jit_tramp_salir);
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

static void gen_salir_enlazable(jit_gen * g, jit_traduccion * t, DWORD pc_sig);
static void gen_salir_dinamico(jit_gen * g, jit_traduccion * t);
static void tr_sync(jit_gen * g, jit_traduccion * t, DWORD pc_k);

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
	/* El PC de cada instruccion. Hoy es pc0 + 2*i; con el superbloque por
	   flujo la traza deja de ser contigua y este arreglo es la verdad. Toda
	   emision que necesite el PC de la instruccion i lee de aca. */
	DWORD					pc[JIT_MAX_INSTR];
	WORD					palabra[JIT_MAX_INSTR];
	const jit_plantilla *	pl[JIT_MAX_INSTR];
	int						n;
	int						modo;			/* JIT_ACC_PLANO o JIT_ACC_MMU */
	int						fpu;			/* modo FPU al traducir, o -1 */

	signed char				slot[16];		/* indice en jit_a[], o -1 */
	unsigned				desp_cuerpo;	/* el largo del prologo emitido */

	/* Para un RTS seguido: el punto de retorno en que la traza continua, o 0.
	   La emision lo convierte en una guarda (PR contra la constante) con
	   salto interno; la salida dinamica de siempre queda para el camino que
	   no coincide. Se limpia por fila al anexar. */
	DWORD					sigue_en[JIT_MAX_INSTR];
	int						fin;			/* JIT_FIN_*: en que termino */
	/* La palabra que corto el descubrimiento (0 si el fin no tiene palabra:
	   tope, ventana, lazo, par). En un corte de ranura es la palabra de la
	   RANURA -- la fila FPU o el acceso sin par que no pudo entrar --, que es
	   la que nombra que plantilla o regla falta. */
	WORD					corte;

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
	/* La fila depende del modo FPU (PR/SZ/Enables): el bloque que la use
	   queda atado al modo vigente al traducir (t->fpu) y no recibe enlaces.
	   Va al final para que las filas viejas la inicialicen a 0 solas. */
	unsigned char	fpu;
	/* La emision cuenta el intento por su cuenta (inc N adentro, ANTES de su
	   corte): el conductor no debe volver a contarlo. Nacio de un bug de
	   contador: el corte interno del envoltorio ligero salia antes del inc N
	   del conductor y perf_instrucciones perdia uno por corte -- 7,18
	   millones en el banco de CT, con la ejecucion intacta (captura y audio
	   identicos): el contador mentia, no el guest. */
	unsigned char	propia;
	/* La fila escribe memoria. En un bloque con MMU, tras cada una va el
	   corte de epoca (gen_corte_epoca): una escritura del guest a PTEH/MMUCR
	   -- o sobre una pagina con codigo traducido -- invalida la pagina de
	   busqueda, y el interprete haria el recorrido con su avance de URC en la
	   busqueda de la instruccion siguiente. Un bloque que siga de largo corre
	   URC en uno: la divergencia de 633 M del expediente de la compuerta. */
	unsigned char	escribe;
	/* El superbloque por flujo: 1 = BRA (destino constante), 2 = BSR (ademas
	   arma el rastreo del punto de retorno), 3 = RTS (continua en el punto de
	   retorno SI el rastreo esta armado; la emision lo verifica con una
	   guarda en caliente, asi que el rastreo puede equivocarse sin romper). */
	unsigned char	sigue;
	/* La fila escribe PR: invalida el rastreo del punto de retorno. 1 = JSR
	   y BSRF, que lo pisan con el suyo (BSR tambien, pero su seguimiento
	   re-arma); 2 = LDS.L @Rm+,PR, el pop del prologo estandar, que REPONE
	   lo apilado si la propia traza lo apilo (el emparejamiento de un nivel;
	   si la pila no era la que parece, la guarda en caliente lo atrapa).
	   LDS Rm,PR no tiene plantilla y corta el bloque: invalida gratis. */
	unsigned char	escribe_pr;
	/* La fila apila PR (STS.L PR,@-Rn): el rastreo recuerda que lo apilado
	   es el punto de retorno vigente, para que el pop lo reponga. */
	unsigned char	apila_pr;
	/* La rama admite el par con ranura de memoria. En JSR, BSR y BSRF la
	   escritura de PR se retrasa hasta que la ranura termina, y el par solo
	   se forma si esa ranura no lee ni escribe PR: ante una falta queda el
	   valor anterior, como tras restaurar la instantanea del interprete; al
	   volver, comprometerlo tarde es indistinguible. Los condicionales
	   (BF/S, BT/S) quedan para cuando el censo los pida. */
	unsigned char	par;
	/* La emision es directa y no toca PC ni llama manejador: admisible en una
	   ranura de retardo aunque sea fila FPU. La prohibicion general existia
	   por el PC += 2 de los manejadores sobre el contexto -- que en una
	   ranura pisa el destino capturado --, y una fila que se emite con puros
	   movs no lo carga. El censo ponderado la pidio: FSTS/FMOV/FLDI en
	   ranura eran el 35 % de las entradas de Crazy Taxi. */
	unsigned char	sin_pc;
	/* Fila TERMINAL: se traduce por manejador con sync completa y el bloque
	   TERMINA en ella, sin enlace de salida -- el despachador re-evalua la
	   clave entera y el PC sale del contexto, porque el manejador es su
	   dueno (TRAPA no deja pc+2). Es lo que permite traducir escritores de
	   SR/FPSCR y LDTLB sin violar la lista blanca de tr_manejador, que es
	   para filas EN MEDIO del bloque. Nunca entra en una ranura ni forma
	   par. */
	unsigned char	terminal;
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
/* Los dos literales de PC-relativo: la direccion sale de la palabra y del PC,
   asi que es constante al traducir. Se anuncia en g->acc_dir para que el
   camino rapido pliegue la zona, el desplazamiento y la alineacion; el VALOR
   se sigue leyendo, que el literal es dato. ECX se carga igual porque el
   camino lento --el ayudante-- espera ahi la virtual. */
static void tr_leer_const(jit_gen * g, jit_traduccion * t, int n, int ancho,
	DWORD dir)
{
	jit_x64_mov_ri(&g->e, X64_RCX, dir);

	g->acc_dir        = dir;
	g->acc_dir_valida = jit_dir_constante;

	tr_leer_a(g, t, n, ancho);

	g->acc_dir_valida = 0;
}

static void pl_movl2(jit_gen * g, jit_traduccion * t, int i)
{
	WORD  w   = t->palabra[i];
	DWORD pc  = t->pc[i];
	DWORD dir = (DWORD) (w & 0xFF) * 4 + (pc & 0xFFFFFFFCul) + 4;

	tr_leer_const(g, t, TN(w), 4, dir);
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
	int j = -1;
	int k;

	/* La pertenencia se decide por el arreglo de PC, no por aritmetica sobre
	   pc0: con la traza por flujo el tramo deja de ser contiguo. Es lineal
	   sobre <= 64 entradas y corre solo al traducir. */
	for (k = 0; k < t->n; k++)
		if (t->pc[k] == dest)
		{
			j = k;
			break;
		}

	if (j >= 0)
	{
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
	DWORD pc = t->pc[i];

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

	gen_corte(g, t->pc[i] + 2);
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

	gen_corte(g, t->pc[i] + 2);
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
	DWORD pc  = t->pc[i];
	DWORD dir = (DWORD) (w & 0xFF) * 2 + pc + 4;

	tr_leer_const(g, t, TN(w), 2, dir);
}

/* --- la frontera FPU (sz0): los FMOV por el puntero de banco vivo -------- */

static void tr_manejador(jit_gen * g, jit_traduccion * t, int i, const void * f);

/* [banco + 4x] <- EAX, con RCX de por medio. Va despues de la lectura, asi
   que RCX (la direccion) ya se consumio. */
static void tr_fr_a(jit_gen * g, int x)
{
	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov_mr(&g->e, X64_RCX, FR_DESP(x), X64_RAX);
}

/* El valor de una escritura cuando sale de FR(x): puntero vivo y carga, sobre
   el mismo registro destino. */
static void tr_valor_fr(jit_gen * g, void * ctx, x64_reg dst)
{
	int x = (int) (size_t) ctx;

	jit_x64_mov64_rm(&g->e, dst, CTX, O_FRB);
	jit_x64_mov_rm(&g->e, dst, dst, FR_DESP(x));
}

static void pl_fmov172(jit_gen * g, jit_traduccion * t, int i)	/* FMOV FRm,FRn */
{
	WORD w = t->palabra[i];

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov_rm(&g->e, X64_RAX, X64_RCX, FR_DESP(TM(w)));
	jit_x64_mov_mr(&g->e, X64_RCX, FR_DESP(TN(w)), X64_RAX);
}

static void pl_fmovs173(jit_gen * g, jit_traduccion * t, int i)	/* FMOV.S @Rm,FRn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	gen_leer32(g, X64_RAX, t->modo);
	tr_fr_a(g, TN(w));
}

static void pl_fmovs174(jit_gen * g, jit_traduccion * t, int i)	/* @(R0,Rm),FRn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TM(w));
	gen_leer32(g, X64_RAX, t->modo);
	tr_fr_a(g, TN(w));
}

/* @Rm+ : el incremento se compromete despues de que la lectura volvio. */
static void pl_fmovs175(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	gen_leer32(g, X64_RAX, t->modo);
	tr_fr_a(g, TN(w));
	tr_alu_ri(g, t, X64_ADD, TM(w), 4);
}

static void pl_fmovs176(jit_gen * g, jit_traduccion * t, int i)	/* FRm,@Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	gen_escribir(g, t->modo, 4, tr_valor_fr, (void *) (size_t) TM(w));
}

/* @-Rn : R(n) se compromete despues de que la escritura volvio. */
static void pl_fmovs177(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RCX, 4);
	gen_escribir(g, t->modo, 4, tr_valor_fr, (void *) (size_t) TM(w));
	tr_alu_ri(g, t, X64_SUB, TN(w), 4);
}

static void pl_fmovs178(jit_gen * g, jit_traduccion * t, int i)	/* FRm,@(R0,Rn) */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TN(w));
	gen_escribir(g, t->modo, 4, tr_valor_fr, (void *) (size_t) TM(w));
}

/*
	La aritmetica de sz0/pr0 va por el manejador real -- escribe Cause/Flag y
	aplica el aplanado DN por operacion, y emitir eso seria una segunda copia
	de las reglas de la FPU --, pero con el **envoltorio ligero**: el manejador
	es puro sobre FR/FPUL/T (sin memoria, sin falta posible con Enables=0
	garantizado por b->fpu) y no toca ningun registro entero, asi que el sync
	completo del conductor sobra. Solo el reloj viaja: se guarda, el manejador
	suma ahi, se recarga. El PC+=2 que el manejador hace sobre el contexto es
	sobre un valor rancio y toda salida lo pisa; en las ranuras seria veneno
	--pisaria el destino capturado de un salto--, y por eso NINGUNA fila FPU
	entra en ranura (el corte del descubrimiento lo garantiza).
*/
static void tr_manejador_fpu(jit_gen * g, jit_traduccion * t, int i,
	const void * f)
{
	/* El intento se cuenta ANTES, como run(): el corte de aqui abajo puede
	   salir del bloque, y una instruccion ejecutada sin contar deja el
	   contador mintiendo. La fila lleva `propia` para que el conductor no
	   vuelva a contar. */
	jit_x64_inc_r(&g->e, N);

	jit_x64_mov_mr(&g->e, CTX, O_CYC, CYC);
	jit_x64_mov_ri(&g->e, X64_RCX, (unsigned) t->palabra[i]);

	if (!jit_x64_call_directo(&g->e, f))
	{
		jit_x64_mov64_ri(&g->e, X64_RAX, (unsigned long long) (size_t) f);
		jit_x64_call_r(&g->e, X64_RAX);
	}

	jit_x64_mov_rm(&g->e, CYC, CTX, O_CYC);

	if (i + 1 < t->n)
		gen_corte(g, t->pc[i] + 2);
}

static void pl_fadd189(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador_fpu(g, t, i, (const void *) fadd189); }
static void pl_fsub198(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador_fpu(g, t, i, (const void *) fsub198); }
static void pl_fmul195(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador_fpu(g, t, i, (const void *) fmul195); }
static void pl_fdiv192(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador_fpu(g, t, i, (const void *) fdiv192); }
static void pl_fcmpeq190(jit_gen * g, jit_traduccion * t, int i){ tr_manejador_fpu(g, t, i, (const void *) fcmpeq190); }
static void pl_fcmpgt191(jit_gen * g, jit_traduccion * t, int i){ tr_manejador_fpu(g, t, i, (const void *) fcmpgt191); }
static void pl_float193(jit_gen * g, jit_traduccion * t, int i) { tr_manejador_fpu(g, t, i, (const void *) float193); }
static void pl_ftrc199(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador_fpu(g, t, i, (const void *) ftrc199); }
static void pl_fsqrt197(jit_gen * g, jit_traduccion * t, int i) { tr_manejador_fpu(g, t, i, (const void *) fsqrt197); }

/* Las seis sin FPSCR se emiten enteras: FNEG y FABS son el bit de signo
   (el manual las define asi, no como aritmetica), y las otras cuatro son
   movimientos. Ninguna suma ciclos: asi vienen sus manejadores. */
static void pl_fneg196(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_alu_mi(&g->e, X64_XOR, X64_RCX, FR_DESP(TN(w)), (int) 0x80000000ul);
}

static void pl_fabs188(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_alu_mi(&g->e, X64_AND, X64_RCX, FR_DESP(TN(w)), 0x7FFFFFFF);
}

static void pl_flds186(jit_gen * g, jit_traduccion * t, int i)	/* FLDS FRm,FPUL */
{
	WORD w = t->palabra[i];

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov_rm(&g->e, X64_RAX, X64_RCX, FR_DESP(TN(w)));
	jit_x64_mov_mr(&g->e, CTX, O_FPUL, X64_RAX);
}

static void pl_fsts187(jit_gen * g, jit_traduccion * t, int i)	/* FSTS FPUL,FRn */
{
	WORD w = t->palabra[i];

	jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_FPUL);
	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov_mr(&g->e, X64_RCX, FR_DESP(TN(w)), X64_RAX);
}

static void pl_fldi0170(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov_mi(&g->e, X64_RCX, FR_DESP(TN(w)), 0);
}

static void pl_fldi1171(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov_mi(&g->e, X64_RCX, FR_DESP(TN(w)), 0x3F800000);
}

/* --- el lote del censo tras los pares (2026-08-08) ----------------------- */

static void pl_movw5(jit_gen * g, jit_traduccion * t, int i)	/* MOV.W Rm,@Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	tr_escribir_de(g, t, TM(w), 2);
}

static void pl_movb19(jit_gen * g, jit_traduccion * t, int i)	/* MOV.B @(d,Rm),R0 */
{
	WORD w = t->palabra[i];
	int  d = (int) (w & 0x0F);

	tr_cargar(g, t, X64_RCX, TM(w));

	if (d)
		jit_x64_add_ri(&g->e, X64_RCX, d);

	tr_leer_a(g, t, 0, 1);
}

static void pl_neg67(jit_gen * g, jit_traduccion * t, int i)	/* NEG Rm,Rn */
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);

	tr_cargar(g, t, X64_RAX, TM(w));
	jit_x64_neg_r(&g->e, X64_RAX);

	if (hn >= 0)
		jit_x64_mov_rr(&g->e, (x64_reg) hn, X64_RAX);
	else
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
}

static void pl_xor83(jit_gen * g, jit_traduccion * t, int i)	/* XOR Rm,Rn */
{
	WORD w = t->palabra[i];

	tr_alu_rr(g, t, X64_XOR, TN(w), TM(w));
}

/* SHAR: como ROTCL, el bit que sale queda en el acarreo y de ahi va a T; el
   sar de x86 preserva el signo igual que el chip. */
static void pl_shar92(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w  = t->palabra[i];
	int  n  = TN(w);
	int  hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_shift_ri(&g->e, X64_SAR, (x64_reg) hn, 1);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_R(n));
		jit_x64_shift_ri(&g->e, X64_SAR, X64_RAX, 1);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}

	gen_poner_t(g, X64_B);
}

static void pl_clrs114(jit_gen * g, jit_traduccion * t, int i)	/* CLRS */
{
	(void) t; (void) i;
	jit_x64_alu_mi(&g->e, X64_AND, CTX, O_SR, (int) 0xFFFFFFFDul);
}

/* --- el lote que cierra el censo (2026-08-09) ---------------------------- */

static void pl_dt(jit_gen * g, jit_traduccion * t, int i)	/* DT Rn */
{
	WORD w = t->palabra[i];

	tr_alu_ri(g, t, X64_SUB, TN(w), 1);
	gen_poner_t(g, X64_E);
}

static void pl_movw14(jit_gen * g, jit_traduccion * t, int i)	/* MOV.W @Rm+,Rn */
{
	tr_leer_mas(g, t, i, 2);
}

/* El mini-lote del censo de SR2 (358 cortes de CLRT; SETT es su espejo). */

static void pl_clrt115(jit_gen * g, jit_traduccion * t, int i)	/* CLRT */
{
	(void) t; (void) i;
	jit_x64_and_mi8(&g->e, CTX, O_SR, 0xFE);
}

static void pl_sett145(jit_gen * g, jit_traduccion * t, int i)	/* SETT */
{
	(void) t; (void) i;
	jit_x64_or_mi8(&g->e, CTX, O_SR, 0x01);
}

/* CMP/STR: T = 1 si algun byte de Rn^Rm es cero. El truco clasico
   (v - 0x01010101) & ~v & 0x80808080 != 0 <=> v tiene un byte cero; es
   exacto en 32 bits, sin falsos positivos. */
static void pl_cmpstr51(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w  = t->palabra[i];
	int  n  = TN(w), m = TM(w);
	int  hm = tr_h(t, m);

	tr_cargar(g, t, X64_RAX, n);

	if (hm >= 0)
		jit_x64_alu_rr(&g->e, X64_XOR, X64_RAX, (x64_reg) hm);
	else
		jit_x64_alu_rm(&g->e, X64_XOR, X64_RAX, CTX, O_R(m));

	jit_x64_mov_rr(&g->e, X64_RDX, X64_RAX);
	jit_x64_alu_ri(&g->e, X64_XOR, X64_RDX, -1);
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RAX, 0x01010101);
	jit_x64_alu_rr(&g->e, X64_AND, X64_RAX, X64_RDX);
	jit_x64_alu_ri(&g->e, X64_AND, X64_RAX, (int) 0x80808080ul);
	gen_poner_t(g, X64_NE);
}

/* --- los pares de sz1: el FMOV de 64 bits ------------------------------- */

#define DR_DESP(x)	((int) offsetof(FPR_BANK, FP.dreg) + 8 * (x))

/* La direccion ya viene en RCX; el segundo argumento del ayudante es el
   desplazamiento del DR dentro del banco. */
static void tr_par(jit_gen * g, const void * f, int disp_tabla, int dr)
{
	jit_x64_mov_ri(&g->e, X64_RDX, (unsigned) DR_DESP(dr));
	gen_llamar(g, f, disp_tabla);
}

static void pl_fmov179(jit_gen * g, jit_traduccion * t, int i)	/* FMOV DRm,DRn */
{
	WORD w = t->palabra[i];
	int  n = (w >> 9) & 7, m = (w >> 5) & 7;

	jit_x64_mov64_rm(&g->e, X64_RCX, CTX, O_FRB);
	jit_x64_mov64_rm(&g->e, X64_RAX, X64_RCX, DR_DESP(m));
	jit_x64_mov64_mr(&g->e, X64_RCX, DR_DESP(n), X64_RAX);
}

static void pl_fmov180(jit_gen * g, jit_traduccion * t, int i)	/* FMOV @Rm,DRn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	tr_par(g, (const void *) jit_leer_par, D_LEER_PAR, (w >> 9) & 7);
}

static void pl_fmov181(jit_gen * g, jit_traduccion * t, int i)	/* @(R0,Rm),DRn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TM(w));
	tr_par(g, (const void *) jit_leer_par, D_LEER_PAR, (w >> 9) & 7);
}

/* @Rm+ : el incremento se compromete despues de que la lectura volvio. */
static void pl_fmov182(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TM(w));
	tr_par(g, (const void *) jit_leer_par, D_LEER_PAR, (w >> 9) & 7);
	tr_alu_ri(g, t, X64_ADD, TM(w), 8);
}

static void pl_fmov183(jit_gen * g, jit_traduccion * t, int i)	/* DRm,@Rn */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	tr_par(g, (const void *) jit_escribir_par, D_ESCR_PAR, (w >> 5) & 7);
}

/* @-Rn : R(n) se compromete despues de que la escritura volvio. */
static void pl_fmov184(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, TN(w));
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RCX, 8);
	tr_par(g, (const void *) jit_escribir_par, D_ESCR_PAR, (w >> 5) & 7);
	tr_alu_ri(g, t, X64_SUB, TN(w), 8);
}

static void pl_fmov185(jit_gen * g, jit_traduccion * t, int i)	/* DRm,@(R0,Rn) */
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TN(w));
	tr_par(g, (const void *) jit_escribir_par, D_ESCR_PAR, (w >> 5) & 7);
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

	gen_corte(g, t->pc[i] + 2);
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

	gen_corte(g, t->pc[i] + 2);
}

static DWORD tr_destino12(const jit_traduccion * t, int i)
{
	WORD  w  = t->palabra[i];
	DWORD pc = t->pc[i];
	int   d  = (int) (w & 0x0FFF);

	if (d & 0x0800)
		d |= ~0x0FFF;

	return (DWORD) (d * 2) + pc + 4;
}

/* BRA / BSR: siempre saltan, con destino constante, asi que el salto puede
   plegarse como arista del bloque. */
static void pl_bra(jit_gen * g, jit_traduccion * t, int i)
{
	/* El par con ranura de memoria: como tr_salto_dinamico_mem pero el
	   destino es constante, asi que no necesita el lugar seguro -- solo la
	   sincronizacion con el PC de la rama y la cuenta de la ranura antes. */
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		const jit_plantilla * r = t->pl[i + 1];

		tr_sync(g, t, t->pc[i]);

		jit_x64_inc_r(&g->e, N);
		gen_volcar_cuenta(g);

		r->emitir(g, t, i + 1);

		if (r->ciclos)
			jit_x64_add_ri(&g->e, CYC, r->ciclos);

		/* Despues de la ranura, como el interprete: la regla de los ciclos
		   evaporados (ver tr_salto_dinamico_mem). */
		jit_x64_add_ri(&g->e, CYC, 2);

		jit_pares_rts++;
		tr_seguir_en(g, t, tr_destino12(t, i));
		return;
	}

	jit_x64_add_ri(&g->e, CYC, 2);
	jit_x64_inc_r(&g->e, N);
	tr_emitir_ranura(g, t, i + 1);
	tr_seguir_en(g, t, tr_destino12(t, i));
}

static void pl_bsr108(jit_gen * g, jit_traduccion * t, int i)
{
	DWORD pc = t->pc[i];

	/* El par de llamada: descubrimiento ya probo que la ranura no observa ni
	   modifica PR, asi que se puede comprometer despues del acceso. */
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		const jit_plantilla * r = t->pl[i + 1];

		tr_sync(g, t, pc);

		jit_x64_inc_r(&g->e, N);
		gen_volcar_cuenta(g);

		r->emitir(g, t, i + 1);

		if (r->ciclos)
			jit_x64_add_ri(&g->e, CYC, r->ciclos);

		jit_x64_mov_mi(&g->e, CTX, O_PR, pc + 4);
		jit_x64_add_ri(&g->e, CYC, 2);

		jit_pares_rts++;
		tr_seguir_en(g, t, tr_destino12(t, i));
		return;
	}

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
	DWORD pc = t->pc[i];

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

/*
	El par con ranura de memoria de un salto dinamico SIN escritura de PR
	(JMP, BRAF, RTS): la sincronizacion va antes de todo y con el PC de la
	rama -- una falta de la ranura deja el contexto en la rama, la semantica
	de la instantanea del interprete --, el destino viaja por el lugar seguro
	del estado (el PC del contexto debe seguir siendo el de la rama hasta
	despues de la ranura), y el intento de la ranura se cuenta ANTES de su
	memoria (la regla de run(): una falta cuenta). Las ramas que escriben PR
	quedan afuera: ver el comentario del campo `par`.
*/
static void tr_salto_dinamico_mem(jit_gen * g, jit_traduccion * t, int i,
	int ciclos, int reg_destino, int relativo)
{
	const jit_plantilla * r = t->pl[i + 1];

	tr_sync(g, t, t->pc[i]);

	if (reg_destino >= 0)
		tr_cargar(g, t, X64_RAX, reg_destino);
	else
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_PR);

	if (relativo)
		jit_x64_alu_ri(&g->e, X64_ADD, X64_RAX, (int) (t->pc[i] + 4));

	jit_x64_mov_mr(&g->e, CTX, D(&jit_estado.destino), X64_RAX);

	jit_x64_inc_r(&g->e, N);
	gen_volcar_cuenta(g);

	r->emitir(g, t, i + 1);

	if (r->ciclos)
		jit_x64_add_ri(&g->e, CYC, r->ciclos);

	/* Los ciclos de la rama van DESPUES de la ranura, como el manejador del
	   interprete. No es cosmetico: la sync ya volco CYC, y una ranura por
	   manejador (NEGC) recarga CYC del contexto -- ciclos sumados al registro
	   antes de ella se evaporan en la recarga. Costo 16 784 instrucciones de
	   divergencia en DOOM con la captura intacta: los cortes corridos mueven
	   la entrega, no la salida. */
	jit_x64_add_ri(&g->e, CYC, ciclos);

	jit_x64_mov_rm(&g->e, X64_RAX, CTX, D(&jit_estado.destino));
	jit_x64_mov_mr(&g->e, CTX, O_PC, X64_RAX);

	jit_pares_rts++;
	gen_salir_dinamico(g, t);
}

/*
	El equivalente para JSR/BSRF. El descubrimiento ya probo que la ranura no
	lee ni escribe PR, por lo que comprometer el retorno despues del acceso
	conserva ambos caminos: una falta deja el PR viejo y el exito deja pc+4.
*/
static void tr_llamada_dinamica_mem(jit_gen * g, jit_traduccion * t, int i,
	int relativo)
{
	const jit_plantilla * r  = t->pl[i + 1];
	DWORD				 pc = t->pc[i];

	tr_sync(g, t, pc);

	tr_cargar(g, t, X64_RAX, TN(t->palabra[i]));

	if (relativo)
		jit_x64_alu_ri(&g->e, X64_ADD, X64_RAX, (int) (pc + 4));

	jit_x64_mov_mr(&g->e, CTX, D(&jit_estado.destino), X64_RAX);

	jit_x64_inc_r(&g->e, N);
	gen_volcar_cuenta(g);

	r->emitir(g, t, i + 1);

	if (r->ciclos)
		jit_x64_add_ri(&g->e, CYC, r->ciclos);

	jit_x64_mov_mi(&g->e, CTX, O_PR, pc + 4);
	jit_x64_add_ri(&g->e, CYC, 3);

	jit_x64_mov_rm(&g->e, X64_RAX, CTX, D(&jit_estado.destino));
	jit_x64_mov_mr(&g->e, CTX, O_PC, X64_RAX);

	jit_pares_rts++;
	gen_salir_dinamico(g, t);
}

static void pl_jmp110(jit_gen * g, jit_traduccion * t, int i)
{
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		tr_salto_dinamico_mem(g, t, i, 3, TN(t->palabra[i]), 0);
		return;
	}

	tr_salto_dinamico(g, t, i, 3, TN(t->palabra[i]), 0);
}

static void pl_jsr111(jit_gen * g, jit_traduccion * t, int i)
{
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		tr_llamada_dinamica_mem(g, t, i, 0);
		return;
	}

	tr_salto_dinamico(g, t, i, 3, TN(t->palabra[i]), 1);
}

static void pl_rts112(jit_gen * g, jit_traduccion * t, int i)
{
	/*
		El RTS seguido: la traza continua en el punto de retorno que el BSR de
		esta misma traza dejo en PR. La semantica no se asume: el PC del
		contexto recibe el PR REAL (antes de la ranura, como todo salto
		dinamico), y una guarda lo compara contra la constante rastreada -- un
		camino interno que se haya salteado la llamada cae en la salida
		dinamica de siempre. El rastreo puede equivocarse; la guarda no.
	*/
	if (t->sigue_en[i] != 0)
	{
		DWORD		ret = t->sigue_en[i];
		x64_parche	fuera;

		jit_x64_add_ri(&g->e, CYC, 3);
		jit_x64_inc_r(&g->e, N);

		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_PR);
		jit_x64_mov_mr(&g->e, CTX, O_PC, X64_RAX);

		tr_emitir_ranura(g, t, i + 1);

		/* La ranura pudo pisar EAX; el PC del contexto es el lugar seguro. */
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_PC);
		jit_x64_cmp_ri32(&g->e, X64_RAX, (int) ret);
		fuera = jit_x64_jcc(&g->e, X64_NE);

		tr_seguir_en(g, t, ret);

		jit_x64_fijar(&g->e, fuera);
		gen_salir_dinamico(g, t);
		return;
	}

	/* El par de retorno (rts; lds.l @r15+,pr, el epilogo estandar): el caso
	   que inauguro el mecanismo, hoy por el ayudante comun. */
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		tr_salto_dinamico_mem(g, t, i, 3, -1, 0);
		return;
	}

	tr_salto_dinamico(g, t, i, 3, -1, 0);
}

/* BRAF/BSRF: como el salto dinamico pero el destino es R(n) + PC + 4, y se
   captura antes de la ranura por el mismo motivo. Los ciclos y el orden --PR
   antes de la ranura-- son los de braf/bsrf109. */
static void tr_salto_relativo(jit_gen * g, jit_traduccion * t, int i,
	int guardar_pr)
{
	DWORD pc = t->pc[i];

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
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		tr_salto_dinamico_mem(g, t, i, 3, TN(t->palabra[i]), 1);
		return;
	}

	tr_salto_relativo(g, t, i, 0);
}

static void pl_bsrf109(jit_gen * g, jit_traduccion * t, int i)
{
	if (i + 1 < t->n && t->pl[i + 1]->accede)
	{
		tr_llamada_dinamica_mem(g, t, i, 1);
		return;
	}

	tr_salto_relativo(g, t, i, 1);
}

/* MOV.W @(R0,Rm),Rn -- el gemelo de 16 bits de movl27/movb25: uno de los dos
   cortadores del lazo de columnas de DOOM (9,4 M de pasadas por segmento). */
static void pl_movw26(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];

	tr_cargar(g, t, X64_RCX, 0);
	tr_ecx_alu(g, t, X64_ADD, TM(w));
	tr_leer_a(g, t, TN(w), 2);
}

/* MOV.W R0,@(disp,Rn) -- el otro cortador del lazo. OJO con los campos: n va
   en los bits 4-7 y el desplazamiento en 0-3, como en el manejador. */
static void pl_movw17(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];
	int  n = (w >> 4) & 0x0F;
	int  d = (int) (w & 0x0F) * 2;

	tr_cargar(g, t, X64_RCX, n);

	if (d)
		jit_x64_add_ri(&g->e, X64_RCX, d);

	tr_escribir_de(g, t, 0, 2);
}

/* LDC Rm,GBR -- la forma de registro (4m1E), 1086 sitios estaticos en SR2.
   GBR no es SR: no gobierna modo ni bancos, es un mov al contexto. */
static void pl_ldc117(jit_gen * g, jit_traduccion * t, int i)
{
	tr_cargar(g, t, X64_RAX, TN(t->palabra[i]));
	jit_x64_mov_mr(&g->e, CTX, O_GBR, X64_RAX);
}

/* MOV.L @(disp,GBR),R0 -- C6xx, lo que quedo arriba del censo de SR2. */
static void pl_movl33(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];
	int  d = (int) (w & 0xFF) * 4;

	jit_x64_mov_rm(&g->e, X64_RCX, CTX, O_GBR);

	if (d)
		jit_x64_add_ri(&g->e, X64_RCX, d);

	tr_leer_a(g, t, 0, 4);
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
	/* No pasa por gen_llamar(), asi que la sincronizacion pendiente se emite
	   aqui. Queda en el mismo sitio que antes --antes del mov de la palabra--
	   asi que la emision de estas filas no cambia un byte. */
	gen_sync_pendiente(g);

	/* Y la llamada al manejador ensucia la vuelta (elision de ociosos). */
	if (g->ocioso_bumps)
		jit_x64_add64_mi(&g->e, CTX, D_OCIOSO_GEN(JIT_IMP_MANEJADOR), 1);

	jit_x64_mov_ri(&g->e, X64_RCX, (unsigned) t->palabra[i]);

	if (!jit_x64_call_directo(&g->e, f))
	{
		jit_x64_mov64_ri(&g->e, X64_RAX, (unsigned long long) (size_t) f);
		jit_x64_call_r(&g->e, X64_RAX);
	}

	jit_x64_mov_rm(&g->e, CYC, CTX, O_CYC);
	tr_prologo(g, t);

	if (i + 1 < t->n)
		gen_corte(g, t->pc[i] + 2);
}

/*
	DIV1 Rm,Rn emitido, y sin ramas.

	**Por que vale la pena.** El censo del contrato (2026-09-02) dio DIV1 en el
	4,91 % de las instrucciones ejecutadas de DCDoom: el guest divide por
	software y cada paso costaba sincronizacion, llamada, recarga de CYC y
	recarga de las cinco ranuras, mas un manejador con dos switch anidados.

	**Por que sin ramas.** Los switch de div1s52() dependen de Q y de M: M es fijo
	durante una division, pero Q alterna con los bits del cociente, o sea que una
	rama ahi falla la prediccion la mitad de las veces. Emitir el switch tal cual
	cambiaria una llamada por veinte ciclos de penalidad.

	**La forma cerrada** (probada contra el manejador real sobre 4096 estados al
	azar mas los bordes, en tests/test_arith.c, div1_la_forma_cerrada_coincide):

	  qs   = bit 31 de Rn ANTES del corrimiento
	  tmp0 = (Rn << 1) | T
	  se RESTA si (Q == M), y se suma si no
	  tmp1 = el prestamo de la resta o el acarreo de la suma
	  Q'   = qs ^ tmp1 ^ M
	  T'   = (Q' == M)

	Los dos trucos que la vuelven recta, en vez de un cmov que este emisor no
	tiene: el operando se niega con mascara --`b' = (b ^ (sel-1)) - (sel-1)`, que
	da b cuando sel vale 1 y -b cuando vale 0-- asi que siempre se SUMA; y el
	acarreo se corrige con `tmp1 = CF ^ !sel`. Eso vale para todo b salvo **b
	cero**, donde acarreo y prestamo dejan de ser complementarios (los dos valen
	0 y la negacion daria 1): de ahi el `and` final contra (b != 0), que es el
	unico caso que la forma cerrada no cubre sola y el que la prueba fuerza a
	mano.

	Las operaciones de 8 bits van todas sobre AL a proposito: setcc y or_mr8
	sobre R8-R15 pedirian REX que el emisor no arma para esa forma.
*/
static void pl_div1s52(jit_gen * g, jit_traduccion * t, int i)
{
	WORD w = t->palabra[i];
	int  n = TN(w), m = TM(w);
	int  hn;

	/* qs = bit 31 de Rn, tomado antes de tocarlo. */
	tr_cargar(g, t, X64_R8, n);
	jit_x64_shift_ri(&g->e, X64_SHR, X64_R8, 31);

	/* tmp0 = (Rn << 1) | T, en RCX. */
	tr_cargar(g, t, X64_RCX, n);
	jit_x64_alu_rr(&g->e, X64_ADD, X64_RCX, X64_RCX);
	jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_SR);
	jit_x64_mov_rr(&g->e, X64_R9, X64_RAX);
	jit_x64_alu_ri(&g->e, X64_AND, X64_R9, 1);
	jit_x64_alu_rr(&g->e, X64_OR, X64_RCX, X64_R9);

	/* sel = Q ^ M (0 = restar) en R9; M en R10. Q es el bit 8 de SR y M el 9. */
	jit_x64_mov_rr(&g->e, X64_R9, X64_RAX);
	jit_x64_shift_ri(&g->e, X64_SHR, X64_R9, 8);
	jit_x64_mov_rr(&g->e, X64_R10, X64_RAX);
	jit_x64_shift_ri(&g->e, X64_SHR, X64_R10, 9);
	jit_x64_alu_rr(&g->e, X64_XOR, X64_R9, X64_R10);
	jit_x64_alu_ri(&g->e, X64_AND, X64_R9, 1);
	jit_x64_alu_ri(&g->e, X64_AND, X64_R10, 1);

	/* b' = sel ? b : -b, por mascara; y el resultado, siempre sumando. */
	tr_cargar(g, t, X64_RDX, m);
	jit_x64_mov_rr(&g->e, X64_RAX, X64_R9);
	jit_x64_alu_ri(&g->e, X64_SUB, X64_RAX, 1);
	jit_x64_mov_rr(&g->e, X64_R11, X64_RDX);
	jit_x64_alu_rr(&g->e, X64_XOR, X64_R11, X64_RAX);
	jit_x64_alu_rr(&g->e, X64_SUB, X64_R11, X64_RAX);
	jit_x64_alu_rr(&g->e, X64_ADD, X64_RCX, X64_R11);

	/* tmp1 = CF ^ !sel, y cero si b era cero. */
	jit_x64_setcc(&g->e, X64_B, X64_RAX);
	jit_x64_movzx_b(&g->e, X64_RAX, X64_RAX);
	jit_x64_mov_rr(&g->e, X64_R11, X64_RAX);
	jit_x64_mov_rr(&g->e, X64_RAX, X64_R9);
	jit_x64_alu_ri(&g->e, X64_XOR, X64_RAX, 1);
	jit_x64_alu_rr(&g->e, X64_XOR, X64_R11, X64_RAX);
	jit_x64_test_rr(&g->e, X64_RDX, X64_RDX);
	jit_x64_setcc(&g->e, X64_NE, X64_RAX);
	jit_x64_movzx_b(&g->e, X64_RAX, X64_RAX);
	jit_x64_alu_rr(&g->e, X64_AND, X64_R11, X64_RAX);

	/* Q' = qs ^ tmp1 ^ M, en R8. */
	jit_x64_alu_rr(&g->e, X64_XOR, X64_R8, X64_R11);
	jit_x64_alu_rr(&g->e, X64_XOR, X64_R8, X64_R10);

	/* T' = !(Q' ^ M), en RAX. */
	jit_x64_mov_rr(&g->e, X64_RAX, X64_R8);
	jit_x64_alu_rr(&g->e, X64_XOR, X64_RAX, X64_R10);
	jit_x64_alu_ri(&g->e, X64_XOR, X64_RAX, 1);

	/* SR por bytes, como gen_poner_t: T es el bit 0 y Q el bit 0 del byte 1.
	   Escribir SR entero pisaria S, el IMASK y los bits altos. */
	jit_x64_and_mi8(&g->e, CTX, O_SR, 0xFE);
	jit_x64_or_mr8(&g->e, CTX, O_SR, X64_RAX);
	jit_x64_mov_rr(&g->e, X64_RAX, X64_R8);
	jit_x64_and_mi8(&g->e, CTX, O_SR + 1, 0xFE);
	jit_x64_or_mr8(&g->e, CTX, O_SR + 1, X64_RAX);

	/* Y recien ahora Rn: todo lo de arriba lo leyo. */
	hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_mov_rr(&g->e, (x64_reg) hn, X64_RCX);
	else
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RCX);
}

/* El brazo del A/B: DIV1 por el manejador real, como antes. La fila cambia de
   ciclos y de `accede` con la palanca, asi que el cambio va en jit_iniciar. */
static void pl_div1s52_c(jit_gen * g, jit_traduccion * t, int i)
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

/* NEGC muta Rn y T con la formula exacta del manejador (dos comparaciones
   encadenadas, no un sbb): por el manejador, como DIV1 y SHAD. No accede a
   memoria ni puede faltar; el accede=1 de su fila es la sincronizacion que
   la llamada necesita, como en todos estos. */
static void pl_negc68(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) negc68);
}

/* ADDC y XTRCT, por el manejador y por el mismo motivo que NEGC: la formula
   exacta de T en uno, y no depender del tamano de operando de los shifts en
   el otro. Ninguno puede faltar; el accede=1 es la sincronizacion. */
static void pl_addc41(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) addc41);
}

static void pl_xtrct38(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) xtrct38);
}

/*
	PREF @Rn: el flush de store queue -- la via por la que Crazy Taxi manda su
	geometria al TA, y su cortador mas pesado (15,8 % de las entradas cortadas
	del censo ponderado). Pasa la lista blanca de tr_manejador porque su unica
	falta posible (mmu_traducir_sq, que no vuelve) va ANTES de toda mutacion:
	addr y src son locales, y memwrite_fisico/TA/PC/ciclos vienen despues. La
	fila lleva escribe=1: el volcado puede caer en RAM (memcpy por SQ), y en
	bloques MMU detras de cada escritura va el corte de epoca.
*/
static void pl_pref142(jit_gen * g, jit_traduccion * t, int i)
{
	tr_manejador(g, t, i, (const void *) pref142);
}

/*
	Las filas TERMINALES del lote B.2b: escritores de SR/FPSCR, LDTLB y TRAPA.
	La emision es tr_manejador tal cual -- su gen_corte ya se saltea en la
	ultima fila, que es la unica posicion en que una terminal puede estar --,
	y el bloque sale por gen_salir_terminal (PC del contexto, sin enlace).
	Tras la llamada tr_manejador recarga los slots del contexto, asi que un
	cambio de banco (LDC SR, TRAPA, FRCHG) llega entero al volcado de salida.
*/
static void pl_ldc116(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) ldc116); }
static void pl_ldtlb136(jit_gen * g, jit_traduccion * t, int i) { tr_manejador(g, t, i, (const void *) ldtlb136); }
static void pl_trapa169(jit_gen * g, jit_traduccion * t, int i) { tr_manejador(g, t, i, (const void *) trapa169); }
static void pl_fschg233(jit_gen * g, jit_traduccion * t, int i) { tr_manejador(g, t, i, (const void *) fschg233); }
static void pl_frchg232(jit_gen * g, jit_traduccion * t, int i) { tr_manejador(g, t, i, (const void *) frchg232); }

/* Y los lectores/escritores de sistema que NO necesitan ser terminales: no
   tocan SR.MD/RB ni FPSCR ni los registros activos (LDC Rm,Rn_BANK escribe
   el banco INACTIVO), asi que entran a la lista blanca comun. */
static void pl_ldc119(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) ldc119); }
static void pl_ldc120(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) ldc120); }
static void pl_ldc123(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) ldc123); }
static void pl_stc152(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) stc152); }
static void pl_stc153(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) stc153); }

/*
	El sub-lote B.2c del censo post-B.2b.

	RTE es la fila terminal perfecta: su manejador es autocontenido -- busca la
	ranura ANTES de escribir SR (la regla del manual), la ejecuta por dentro
	con core.execute y deja PC en SPC --, asi que el mecanismo terminal lo
	traduce sin maquinaria nueva y su contrato de falta es el del interprete:
	la sync previa hace reejecutable al RTE, y una falta en la ranura reejecuta
	el RTE entero, igual que la instantanea. La ranura ejecutada por dentro
	cuenta sus ciclos e instrucciones en el manejador, identico por
	construccion. LDS Rm,FPSCR es el otro escritor de FPSCR con peso (SR2):
	terminal como FSCHG, con fpu=1 por el 0x800 de FD.

	Los tres por manejador comun pasan la lista blanca con las lecturas antes
	de las mutaciones: LDS.L @Rm+,MACH (el 27,9 % de las cortadas de DOOM),
	MOV.W @(d,Rm),R0 y MULS.W.
*/
static void pl_rte143(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) rte143); }
static void pl_lds213(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) lds213); }
static void pl_ldsl133(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador(g, t, i, (const void *) ldsl133); }
static void pl_movw20(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador(g, t, i, (const void *) movw20); }
static void pl_mulsw65(jit_gen * g, jit_traduccion * t, int i)  { tr_manejador(g, t, i, (const void *) mulsw65); }

/* Aca vivio un dia el lote B.3 (LDC.L @Rm+,GBR, 2026-08-21) y se revirtio
   MEDIDO: era la fila mas pesada del censo ponderado (53,3 % de las entradas
   cortadas de DOOM) y la tanda salio neutra con direccion leve en contra --
   los cortes "sin plantilla" ya estaban enlazados, asi que la fila solo
   ahorraba un salto encadenado barato y sumaba una entrada al barrido lineal
   de plantillas. La leccion completa en docs/jit-sota-plan.md: la frontera
   por peso ya no predice tiempo, y la serie B queda cerrada por medicion. */

/* Los movedores de FPUL y PR: emision directa, solo movs (el molde es
   pl_sts164). El registro entero vive en su slot o en el contexto -- por eso
   NO pueden ir por el envoltorio ligero, que no sincroniza los slots. Las
   filas de FPUL llevan fpu=1: son instrucciones FPU (0x800 con FD puesto) y
   la clave del bloque las cubre. Ciclos en la fila, del cuerpo entero de cada
   manejador: lds214 1, sts218 3, lds132 3, sts165 2. */
static void pl_lds214(jit_gen * g, jit_traduccion * t, int i)	/* LDS Rm,FPUL */
{
	tr_cargar(g, t, X64_RAX, TN(t->palabra[i]));
	jit_x64_mov_mr(&g->e, CTX, O_FPUL, X64_RAX);
}

static void pl_sts218(jit_gen * g, jit_traduccion * t, int i)	/* STS FPUL,Rn */
{
	int n  = TN(t->palabra[i]);
	int hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_mov_rm(&g->e, (x64_reg) hn, CTX, O_FPUL);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_FPUL);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

static void pl_lds132(jit_gen * g, jit_traduccion * t, int i)	/* LDS Rm,PR */
{
	tr_cargar(g, t, X64_RAX, TN(t->palabra[i]));
	jit_x64_mov_mr(&g->e, CTX, O_PR, X64_RAX);
}

static void pl_sts165(jit_gen * g, jit_traduccion * t, int i)	/* STS PR,Rn */
{
	int n  = TN(t->palabra[i]);
	int hn = tr_h(t, n);

	if (hn >= 0)
		jit_x64_mov_rm(&g->e, (x64_reg) hn, CTX, O_PR);
	else
	{
		jit_x64_mov_rm(&g->e, X64_RAX, CTX, O_PR);
		jit_x64_mov_mr(&g->e, CTX, O_R(n), X64_RAX);
	}
}

/* La geometria de floatgraph.c por el envoltorio ligero, como FADD: puras
   sobre FR/XF/FPUL, sin registro entero y sin falta con Enables=0. Cada
   manejador suma sus propios ciclos -- FIPR/FTRV/FSRRA 4, FMAC 3 y FSCA
   NINGUNO, leido del cuerpo entero: el envoltorio reproduce lo que el
   interprete haga, sume o no. */
static void pl_ftrv(jit_gen * g, jit_traduccion * t, int i)    { tr_manejador_fpu(g, t, i, (const void *) ftrv); }
static void pl_fipr(jit_gen * g, jit_traduccion * t, int i)    { tr_manejador_fpu(g, t, i, (const void *) fipr); }
static void pl_fmac194(jit_gen * g, jit_traduccion * t, int i) { tr_manejador_fpu(g, t, i, (const void *) fmac194); }
static void pl_fsca(jit_gen * g, jit_traduccion * t, int i)    { tr_manejador_fpu(g, t, i, (const void *) fsca); }
static void pl_fsrra(jit_gen * g, jit_traduccion * t, int i)   { tr_manejador_fpu(g, t, i, (const void *) fsrra); }

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
	DWORD pc  = t->pc[i];
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
	{ NULL, "MOV.L Rm,@Rn",       2, 1, 0, 0, pl_movl6, 0, 0, 1 },
	{ NULL, "MOV.L @(d,Rm),Rn",   1, 1, 0, 0, pl_movl21 },
	{ NULL, "MOV.B @Rm,Rn",       2, 1, 0, 0, pl_movb7 },
	{ NULL, "MOV.B Rm,@Rn",       2, 1, 0, 0, pl_movb4, 0, 0, 1 },
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
	{ NULL, "MOV.L Rm,@(R0,Rn)",  2, 1, 0, 0, pl_movl24, 0, 0, 1 },
	{ NULL, "MOV.B Rm,@(R0,Rn)",  2, 1, 0, 0, pl_movb22, 0, 0, 1 },
	{ NULL, "MOV.B @Rm+,Rn",      1, 1, 0, 0, pl_movb13 },
	{ NULL, "MOV.L @Rm+,Rn",      1, 1, 0, 0, pl_movl15 },
	{ NULL, "MOV.L Rm,@-Rn",      1, 1, 0, 0, pl_movl12, 0, 0, 1 },
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
	{ NULL, "BRA",                2, 0, 1, 1, pl_bra, 0, 0, 0, 1, 0, 0, 1 },
	{ NULL, "BSR",                2, 0, 1, 1, pl_bsr108, 0, 0, 0, 2, 0, 0, 1 },
	{ NULL, "JMP @Rn",            3, 0, 1, 1, pl_jmp110, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "JSR @Rn",            3, 0, 1, 1, pl_jsr111, 0, 0, 0, 0, 1, 0, 1 },
	{ NULL, "RTS",                3, 0, 1, 1, pl_rts112, 0, 0, 0, 3, 0, 0, 1 },
	/* Lo que el censo de Crazy Taxi pidio (2026-08-08): el pushpop de PR corta
	   todo prologo y epilogo de funcion del guest. Ciclos copiados de cada
	   manejador; el 0 de LDS.L @Rm+,PR es del manejador, no un olvido. */
	{ NULL, "STS.L PR,@-Rn",      2, 1, 0, 0, pl_stsl168, 0, 0, 1, 0, 0, 1 },
	{ NULL, "LDS.L @Rm+,PR",      0, 1, 0, 0, pl_ldsl135, 0, 0, 0, 0, 2 },
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
	{ NULL, "STS.L MACL,@-Rn",    3, 1, 0, 0, pl_stsl167, 0, 0, 1 },
	{ NULL, "MOV.L Rm,@(d,Rn)",   1, 1, 0, 0, pl_movl18, 0, 0, 1 },
	{ NULL, "MOV.B R0,@(d,Rn)",   1, 1, 0, 0, pl_movb16, 0, 0, 1 },
	{ NULL, "OR Rm,Rn",           1, 0, 0, 0, pl_or76 },
	{ NULL, "CMP/PZ Rn",          1, 0, 0, 0, pl_cmppz49 },
	{ NULL, "SUB Rm,Rn",          1, 0, 0, 0, pl_sub69 },
	{ NULL, "MOV.W @Rm,Rn",       2, 1, 0, 0, pl_movw8 },
	{ NULL, "MOV.W @(d,PC),Rn",   2, 1, 0, 0, pl_movw1 },
	/* El tercer lote: la division por el manejador real, y los saltos
	   relativos por registro. Las filas de manejador llevan accede=1 (la
	   sincronizacion es el contrato) y ciclos 0 (los suma el manejador). */
	/* Por manejador, que es lo que gano la tanda: la emision sin ramas existe y
	   esta probada, pero perdio por 3,6 % (ver pl_div1s52). Con
	   DCEMU_JIT_DIV1_EMITIDA=1, jit_iniciar cambia la fila a {1, 0, pl_div1s52}. */
	{ NULL, "DIV1 Rm,Rn",         0, 1, 0, 0, pl_div1s52_c },
	{ NULL, "DIV0S Rm,Rn",        0, 1, 0, 0, pl_div0s53 },
	{ NULL, "DIV0U",              0, 1, 0, 0, pl_div0u54 },
	{ NULL, "SHAD Rm,Rn",         0, 1, 0, 0, pl_shad90 },
	{ NULL, "BRAF Rn",            3, 0, 1, 1, pl_braf, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "BSRF Rn",            3, 0, 1, 1, pl_bsrf109, 0, 0, 0, 0, 1, 0, 1 },
	/* El cuarto lote: MAC.L por el manejador reordenado, SHLD, y lo que el
	   censo listo tras el tercero. El 5 de OR #imm es del manejador. */
	{ NULL, "MAC.L @Rm+,@Rn+",    0, 1, 0, 0, pl_macl62 },
	{ NULL, "SHLD Rm,Rn",         0, 1, 0, 0, pl_shld93 },
	{ NULL, "OR #imm,R0",         5, 0, 0, 0, pl_or77 },
	{ NULL, "MOVA @(d,PC),R0",    1, 0, 0, 0, pl_mova34 },
	{ NULL, "MOV.W Rm,@(R0,Rn)",  2, 1, 0, 0, pl_movw23, 0, 0, 1 },
	/* La frontera FPU (sz0): la ultima columna ata el bloque al modo FPU
	   vigente al traducir (b->fpu) y le quita los enlaces. Ciclos de los
	   FMOV copiados de cada manejador LEYENDO EL CUERPO ENTERO
	   (0,2,1,2,1,1,1): fmov172 no suma ninguno, clase mov3 -- y un extractor
	   que busca "cycles +=" por cercania le robo el 2 del manejador
	   siguiente, que costo 633 millones de instrucciones de divergencia. La
	   aritmetica va por el manejador real y lleva 0 aqui. */
	/* sin_pc (campo 8vo tras emitir): emision directa sin PC ni manejador --
	   admisible en ranura de retardo. Solo las filas FPU sin memoria. */
	{ NULL, "FMOV FRm,FRn",        0, 0, 0, 0, pl_fmov172,  1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FMOV.S @Rm,FRn",      2, 1, 0, 0, pl_fmovs173, 1 },
	{ NULL, "FMOV.S @(R0,Rm),FRn", 1, 1, 0, 0, pl_fmovs174, 1 },
	{ NULL, "FMOV.S @Rm+,FRn",     2, 1, 0, 0, pl_fmovs175, 1 },
	{ NULL, "FMOV.S FRm,@Rn",      1, 1, 0, 0, pl_fmovs176, 1, 0, 1 },
	{ NULL, "FMOV.S FRm,@-Rn",     1, 1, 0, 0, pl_fmovs177, 1, 0, 1 },
	{ NULL, "FMOV.S FRm,@(R0,Rn)", 1, 1, 0, 0, pl_fmovs178, 1, 0, 1 },
	{ NULL, "FADD FRm,FRn",        0, 0, 0, 0, pl_fadd189,  1, 1 },
	{ NULL, "FSUB FRm,FRn",        0, 0, 0, 0, pl_fsub198,  1, 1 },
	{ NULL, "FMUL FRm,FRn",        0, 0, 0, 0, pl_fmul195,  1, 1 },
	{ NULL, "FDIV FRm,FRn",        0, 0, 0, 0, pl_fdiv192,  1, 1 },
	{ NULL, "FCMP/EQ FRm,FRn",     0, 0, 0, 0, pl_fcmpeq190, 1, 1 },
	{ NULL, "FCMP/GT FRm,FRn",     0, 0, 0, 0, pl_fcmpgt191, 1, 1 },
	{ NULL, "FLOAT FPUL,FRn",      0, 0, 0, 0, pl_float193, 1, 1 },
	{ NULL, "FTRC FRm,FPUL",       0, 0, 0, 0, pl_ftrc199,  1, 1 },
	{ NULL, "FNEG FRn",            0, 0, 0, 0, pl_fneg196,  1 },
	{ NULL, "FABS FRn",            0, 0, 0, 0, pl_fabs188,  1 },
	{ NULL, "FSQRT FRn",           0, 0, 0, 0, pl_fsqrt197, 1, 1 },
	{ NULL, "FLDS FRm,FPUL",       0, 0, 0, 0, pl_flds186,  1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FSTS FPUL,FRn",       0, 0, 0, 0, pl_fsts187,  1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FLDI0 FRn",           0, 0, 0, 0, pl_fldi0170, 1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FLDI1 FRn",           0, 0, 0, 0, pl_fldi1171, 1, 0, 0, 0, 0, 0, 0, 1 },
	/* Los pares de sz1: un acceso de 8 bytes, como en los manejadores. El 0
	   de FMOV DRm,DRn es del manejador. */
	{ NULL, "FMOV DRm,DRn",        0, 0, 0, 0, pl_fmov179,  1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FMOV @Rm,DRn",        2, 1, 0, 0, pl_fmov180,  1 },
	{ NULL, "FMOV @(R0,Rm),DRn",   2, 1, 0, 0, pl_fmov181,  1 },
	{ NULL, "FMOV @Rm+,DRn",       2, 1, 0, 0, pl_fmov182,  1 },
	{ NULL, "FMOV DRm,@Rn",        1, 1, 0, 0, pl_fmov183,  1, 0, 1 },
	{ NULL, "FMOV DRm,@-Rn",       1, 1, 0, 0, pl_fmov184,  1, 0, 1 },
	{ NULL, "FMOV DRm,@(R0,Rn)",   2, 1, 0, 0, pl_fmov185,  1, 0, 1 },
	/* El lote del censo tras los pares. */
	{ NULL, "MOV.W Rm,@Rn",        2, 1, 0, 0, pl_movw5, 0, 0, 1 },
	{ NULL, "MOV.B @(d,Rm),R0",    1, 1, 0, 0, pl_movb19 },
	{ NULL, "NEG Rm,Rn",           1, 0, 0, 0, pl_neg67 },
	{ NULL, "XOR Rm,Rn",           1, 0, 0, 0, pl_xor83 },
	{ NULL, "SHAR Rn",             1, 0, 0, 0, pl_shar92 },
	{ NULL, "CLRS",                1, 0, 0, 0, pl_clrs114 },
	/* El lote que cierra el censo. */
	{ NULL, "DT Rn",               1, 0, 0, 0, pl_dt },
	{ NULL, "MOV.W @Rm+,Rn",       1, 1, 0, 0, pl_movw14 },
	{ NULL, "CMP/STR Rm,Rn",       1, 0, 0, 0, pl_cmpstr51 },
	/* El mini-lote del censo de SR2. */
	{ NULL, "CLRT",                1, 0, 0, 0, pl_clrt115 },
	{ NULL, "SETT",                1, 0, 0, 0, pl_sett145 },
	/* El mini-lote del censo de la frontera (2026-08-09): los cortadores del
	   lazo de columnas de DOOM (las dos MOV.W), NEGC y LDC Rm,GBR. */
	{ NULL, "MOV.W @(R0,Rm),Rn",   2, 1, 0, 0, pl_movw26 },
	{ NULL, "MOV.W R0,@(d,Rn)",    1, 1, 0, 0, pl_movw17, 0, 0, 1 },
	{ NULL, "NEGC Rm,Rn",          0, 1, 0, 0, pl_negc68 },
	{ NULL, "LDC Rm,GBR",          3, 0, 0, 0, pl_ldc117 },
	{ NULL, "MOV.L @(d,GBR),R0",   2, 1, 0, 0, pl_movl33 },
	/* El lote del censo ponderado (2026-08-18, fase B.2 de jit-sota-plan.md):
	   los cortadores por peso de verdad, que la lista sin ponderar escondia.
	   PREF con escribe=1 (el volcado de SQ puede caer en RAM) y ciclos 0 (el
	   manejador suma el suyo); la geometria FPU por el envoltorio ligero, con
	   los ciclos del manejador (FSCA no suma ninguno); los movedores directos
	   con los ciclos en la fila, leidos del cuerpo entero; LDS Rm,PR con
	   escribe_pr=1 -- invalida el rastreo del punto de retorno, como hacia
	   gratis cuando cortaba. */
	{ NULL, "PREF @Rn",            0, 1, 0, 0, pl_pref142, 0, 0, 1 },
	{ NULL, "FTRV XMTRX,FVn",      0, 0, 0, 0, pl_ftrv,    1, 1 },
	{ NULL, "FIPR FVm,FVn",        0, 0, 0, 0, pl_fipr,    1, 1 },
	{ NULL, "FMAC FR0,FRm,FRn",    0, 0, 0, 0, pl_fmac194, 1, 1 },
	{ NULL, "FSCA FPUL,DRn",       0, 0, 0, 0, pl_fsca,    1, 1 },
	{ NULL, "FSRRA FRn",           0, 0, 0, 0, pl_fsrra,   1, 1 },
	{ NULL, "LDS Rm,FPUL",         1, 0, 0, 0, pl_lds214,  1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "STS FPUL,Rn",         3, 0, 0, 0, pl_sts218,  1, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "LDS Rm,PR",           3, 0, 0, 0, pl_lds132,  0, 0, 0, 0, 1 },
	{ NULL, "STS PR,Rn",           2, 0, 0, 0, pl_sts165 },
	{ NULL, "XTRCT Rm,Rn",         0, 1, 0, 0, pl_xtrct38 },
	{ NULL, "ADDC Rm,Rn",          0, 1, 0, 0, pl_addc41 },
	/* El lote B.2b (2026-08-18). Campos tras `par`: sin_pc, terminal. Las
	   cinco terminales (accede=1 por la sync; el manejador suma sus ciclos y
	   es dueno del PC); los lectores/escritores de sistema que entran a la
	   lista blanca comun; y nada mas -- las marcas sin_pc van sobre las
	   filas FPU directas existentes, arriba. */
	{ NULL, "LDC Rm,SR",           0, 1, 0, 0, pl_ldc116,   0, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "LDTLB",               0, 1, 0, 0, pl_ldtlb136, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "TRAPA #imm",          0, 1, 0, 0, pl_trapa169, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FSCHG",               0, 1, 0, 0, pl_fschg233, 1, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "FRCHG",               0, 1, 0, 0, pl_frchg232, 1, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "LDC Rm,SSR",          0, 1, 0, 0, pl_ldc119 },
	{ NULL, "LDC Rm,SPC",          0, 1, 0, 0, pl_ldc120 },
	{ NULL, "LDC Rm,Rn_BANK",      0, 1, 0, 0, pl_ldc123 },
	{ NULL, "STC SSR,Rn",          0, 1, 0, 0, pl_stc152 },
	{ NULL, "STC SPC,Rn",          0, 1, 0, 0, pl_stc153 },
	/* El sub-lote B.2c (censo post-B.2b): RTE y LDS Rm,FPSCR terminales, y
	   tres por manejador comun. */
	{ NULL, "RTE",                 0, 1, 0, 0, pl_rte143, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "LDS Rm,FPSCR",        0, 1, 0, 0, pl_lds213, 1, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ NULL, "LDS.L @Rm+,MACH",     0, 1, 0, 0, pl_ldsl133 },
	{ NULL, "MOV.W @(d,Rm),R0",    0, 1, 0, 0, pl_movw20 },
	{ NULL, "MULS.W Rm,Rn",        0, 1, 0, 0, pl_mulsw65 },
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
	fmov172, fmovs173, fmovs174, fmovs175, fmovs176, fmovs177, fmovs178,
	fadd189, fsub198, fmul195, fdiv192, fcmpeq190, fcmpgt191, float193,
	ftrc199, fneg196, fabs188, fsqrt197, flds186, fsts187, fldi0170, fldi1171,
	fmov179, fmov180, fmov181, fmov182, fmov183, fmov184, fmov185,
	movw5, movb19, neg67, xor83, shar92, clrs114,
	dt, movw14, cmpstr51,
	clrt115, sett145,
	movw26, movw17, negc68, ldc117,
	movl33,
	pref142, ftrv, fipr, fmac194, fsca, fsrra,
	lds214, sts218, lds132, sts165, xtrct38, addc41,
	ldc116, ldtlb136, trapa169, fschg233, frchg232,
	ldc119, ldc120, ldc123, stc152, stc153,
	rte143, lds213, ldsl133, movw20, mulsw65,
};

/* Cuantas filas de la tabla estan en juego. DCEMU_JIT_PLANTILLAS=N la recorta
   para bisecar: una plantilla infiel se delata en la cuenta de instrucciones,
   pero la cuenta no dice cual, y probar de a una es la forma barata de
   averiguarlo. Por omision, todas. */
static int jit_n_activas = JIT_N_PLANTILLAS;

/*
	Ligar las filas de la tabla a los manejadores reales: la tabla se escribe
	como una lista y el orden de los dos arreglos es lo unico que las une, asi
	que un desajuste seria una plantilla usada para otra instruccion. La
	comprobacion de tamano lo impide.

	Esta aparte de jit_iniciar() porque **el censo del contrato clasifica con la
	tabla y corre con el traductor apagado**, que es su modo natural: apagado,
	todas las instrucciones pasan por el interprete y por lo tanto por el
	gancho. Sin esto la tabla queda en NULL y el censo informa que el 100 % de
	las instrucciones no tiene plantilla, que es lo que paso la primera vez.
*/
static void jit_plantillas_ligar(void)
{
	static int ligadas = 0;
	int i;

	if (ligadas)
		return;

	ligadas = 1;

	for (i = 0; i < JIT_N_PLANTILLAS; i++)
		jit_plantillas[i].f = jit_manejadores[i];
}

static const jit_plantilla * jit_plantilla_de(opcode_f * f)
{
	int i;

	for (i = 0; i < jit_n_activas; i++)
		if (jit_plantillas[i].f == f)
			return &jit_plantillas[i];

	return NULL;
}

/* ------------------------------------------------------------------------ */
/* El censo del contrato: de que clase es cada codificacion                  */
/* ------------------------------------------------------------------------ */

/*
	Como jit_plantilla_de(), pero recorriendo la tabla ENTERA: el censo mide el
	contrato del traductor, no la tabla recortada por DCEMU_JIT_PLANTILLAS, que
	existe para bisecar. Devuelve el indice o -1.
*/
static int jit_pl_indice(opcode_f * f)
{
	int i;

	for (i = 0; i < JIT_N_PLANTILLAS; i++)
		if (jit_plantillas[i].f == f)
			return i;

	return -1;
}

/* La plantilla que le tocaria a una codificacion, por el mismo camino que el
   traductor: el manejador que la oplist le da. Se toma la tabla de PR=0/SZ=0
   para que la clasificacion no dependa del modo FPU vigente al informar. */
static const jit_plantilla * jit_plantilla_de_todas(WORD w)
{
	int i = jit_pl_indice(OP_HANDLER(oplist_pr0_sz0, w));

	return (i < 0) ? NULL : &jit_plantillas[i];
}

static int jit_clase_de(const jit_plantilla * p)
{
	if (p == NULL)			return JIT_CL_SIN;
	if (p->terminal)		return JIT_CL_TERMINAL;
	if (p->rama)			return JIT_CL_RAMA;

	/* `propia` marca a las que cuentan el intento por su cuenta, que son las
	   FPU por envoltorio: las unicas que llaman a C sin ser terminales ni
	   ramas. Van aparte porque su costo es una llamada, no una emision. */
	if (p->propia)			return JIT_CL_FPU;

	/*
		Acceso emitido en linea contra fila traducida llamando al manejador
		real. Las separa **ciclos == 0**, y no por casualidad: tr_manejador
		recarga CYC del contexto despues de la llamada porque el manejador ya
		los sumo, asi que el conductor no debe volver a sumarlos. La distincion
		importa porque el costo es distinto -- una emite el acceso, la otra
		paga una llamada -- y porque solo la primera podria mover su
		sincronizacion al camino lento.

		Una sola fila cae aqui sin ser manejador: LDS.L @Rm+,PR, un acceso en
		linea que no suma ciclos. Su peso se lee aparte en la tabla por
		plantilla, que va al lado justamente para poder corregirlo a mano.
	*/
	if (p->accede)
		return (p->ciclos == 0) ? JIT_CL_MANEJADOR : JIT_CL_ACCESO;

	return JIT_CL_ALU;
}

static unsigned char * jit_clase_tabla = NULL;

const unsigned char * jit_clases(void)
{
	unsigned long w;

	if (jit_clase_tabla != NULL)
		return jit_clase_tabla;

	jit_plantillas_ligar();

	jit_clase_tabla = (unsigned char *) calloc(65536, 1);

	if (jit_clase_tabla == NULL)
		return NULL;

	for (w = 0; w < 65536; w++)
		jit_clase_tabla[w] = (unsigned char)
			jit_clase_de(jit_plantilla_de_todas((WORD) w));

	return jit_clase_tabla;
}

/*
	El informe: la mezcla dinamica por clase y el peso de cada plantilla.

	Se corre con el traductor APAGADO (DCEMU_JIT=0), que es lo que hace pasar
	todas las instrucciones por el gancho del interprete. Las dos formas ejecutan
	lo mismo al digito, asi que la mezcla vale para el traductor aunque la haya
	contado el interprete.
*/
void jit_censo_contrato(const unsigned long long * histo,
	unsigned long long total)
{
	static const char * const nombre[JIT_CL_N] =
	{
		"sin plantilla", "directa (tramo)", "acceso en linea",
		"rama", "FPU por envoltorio", "terminal", "por manejador C"
	};

	unsigned long long por_clase[JIT_CL_N];
	unsigned long long por_pl[JIT_N_PLANTILLAS];
	unsigned long long sin_pl_veces[16];
	unsigned			sin_pl_op[16];
	int				sin_pl_n = 0;
	unsigned long		w;
	int				i, j, c;

	if (histo == NULL || total == 0)
		return;

	jit_plantillas_ligar();

	for (c = 0; c < JIT_CL_N; c++)
		por_clase[c] = 0;

	for (i = 0; i < JIT_N_PLANTILLAS; i++)
		por_pl[i] = 0;

	for (i = 0; i < 16; i++)
	{
		sin_pl_veces[i] = 0;
		sin_pl_op[i]	= 0;
	}

	for (w = 0; w < 65536; w++)
	{
		const jit_plantilla * p;
		int idx;

		if (histo[w] == 0)
			continue;

		idx = jit_pl_indice(OP_HANDLER(oplist_pr0_sz0, (WORD) w));
		p	= (idx < 0) ? NULL : &jit_plantillas[idx];

		por_clase[jit_clase_de(p)] += histo[w];

		if (idx >= 0)
		{
			por_pl[idx] += histo[w];
			continue;
		}

		/* Las que no tienen plantilla se guardan por codificacion: son las que
		   cortan bloques, y el mnemonico dice cual escribir. */
		if (sin_pl_n < 16)
		{
			sin_pl_veces[sin_pl_n] = histo[w];
			sin_pl_op[sin_pl_n]	= (unsigned) w;
			sin_pl_n++;
		}
		else
		{
			int peor = 0;

			for (j = 1; j < 16; j++)
				if (sin_pl_veces[j] < sin_pl_veces[peor])
					peor = j;

			if (histo[w] > sin_pl_veces[peor])
			{
				sin_pl_veces[peor] = histo[w];
				sin_pl_op[peor]	= (unsigned) w;
			}
		}
	}

	fprintf(stderr, "perf: censo del contrato (%llu instrucciones"
		" clasificadas)\n", total);

	for (c = 0; c < JIT_CL_N; c++)
		fprintf(stderr, "perf:   %-20s %14llu  %5.2f %%\n",
			nombre[c], por_clase[c],
			100.0 * (double) por_clase[c] / (double) total);

	fprintf(stderr, "perf:   plantillas mas pesadas (por instrucciones"
		" ejecutadas):\n");

	for (i = 0; i < 24; i++)
	{
		int mejor = -1;

		for (j = 0; j < JIT_N_PLANTILLAS; j++)
			if (por_pl[j] != 0 && (mejor < 0 || por_pl[j] > por_pl[mejor]))
				mejor = j;

		if (mejor < 0)
			break;

		fprintf(stderr, "perf:     %-24s %14llu  %5.2f %%\n",
			jit_plantillas[mejor].nombre, por_pl[mejor],
			100.0 * (double) por_pl[mejor] / (double) total);

		por_pl[mejor] = 0;
	}

	if (sin_pl_n)
	{
		fprintf(stderr, "perf:   sin plantilla, por codificacion"
			" (palabra, veces, %%, mnemonico):\n");

		for (i = 0; i < sin_pl_n; i++)
		{
			int mejor = -1;

			for (j = 0; j < sin_pl_n; j++)
				if (sin_pl_veces[j] != 0
					&& (mejor < 0 || sin_pl_veces[j] > sin_pl_veces[mejor]))
					mejor = j;

			if (mejor < 0)
				break;

			fprintf(stderr, "perf:     %04X  %14llu  %5.2f %%  %s\n",
				sin_pl_op[mejor], sin_pl_veces[mejor],
				100.0 * (double) sin_pl_veces[mejor] / (double) total,
				opcodes_mnemonico((WORD) sin_pl_op[mejor]));

			sin_pl_veces[mejor] = 0;
		}
	}
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

		if (p->emitir == pl_movl33)
		{
			uso[0]++;
			continue;
		}

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

	/*
		La seleccion: los JIT_SLOTS mas usados, como siempre. La colocacion
		depende de los hogares: con ellos, un seleccionado canonico va a SU
		ranura fija y los demas llenan las libres -- mismo conjunto cacheado,
		otra numeracion --, que es lo que hace elidible la interseccion en
		una costura de enlace.
	*/
	{
		int elegido[JIT_SLOTS];
		int n_elegidos = 0;

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

			/* Marca provisoria para que la busqueda no lo repita. */
			t->slot[mejor]        = 0;
			elegido[n_elegidos++] = mejor;
		}

		if (!jit_hogares)
		{
			for (i = 0; i < n_elegidos; i++)
				t->slot[elegido[i]] = (signed char) i;
		}
		else
		{
			unsigned ocupada = 0;

			for (i = 0; i < n_elegidos; i++)
				if (jit_canonico[elegido[i]] >= 0)
				{
					t->slot[elegido[i]] = jit_canonico[elegido[i]];
					ocupada |= 1u << jit_canonico[elegido[i]];
				}

			for (i = 0; i < n_elegidos; i++)
				if (jit_canonico[elegido[i]] < 0)
				{
					for (s = 0; s < JIT_SLOTS; s++)
						if (!(ocupada & (1u << s)))
							break;

					t->slot[elegido[i]] = (signed char) s;
					ocupada |= 1u << s;
				}
		}
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
	La misma sincronizacion, en el talon lento y **sin contar**: el intento ya lo
	conto el conductor con un `inc` delante de la plantilla, que es un byte en el
	camino rapido en vez de los cuarenta del volcado entero.

	El orden importa y es el del interprete: contar ANTES de intentar, para que
	una falta que sale por longjmp deje el intento contado. Lo que el talon hace
	es volcarlo (gen_volcar_cuenta) junto con el resto del estado.
*/
static void tr_sync_talon(jit_gen * g, jit_traduccion * t, DWORD pc_k)
{
	tr_volcar_regs(g, t);
	jit_x64_mov_mi(&g->e, CTX, O_PC, pc_k);
	gen_volcar_cuenta(g);
}

/*
	La salida de un bloque que termina en fila TERMINAL: sin escribir PC -- el
	manejador es su dueno y ya dejo el verdadero en el contexto (TRAPA deja el
	vector, no pc+2) -- y sin enlace: el despachador re-evalua la clave entera,
	que es lo que hace sano traducir escritores de SR/FPSCR y LDTLB. Los slots
	se vuelcan frescos: tr_manejador ya los recargo del contexto despues de la
	llamada, asi que un cambio de banco del manejador llega entero.
*/
static void gen_salir_terminal(jit_gen * g, jit_traduccion * t)
{
	tr_volcar_regs(g, t);
	jit_x64_jmp_a(&g->e, jit_tramp_salir);
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

	/* La misma condicion del corte, en una comparacion: ver gen_corte(). */
	if (jit_corte_viejo)
	{
		jit_x64_cmp_ri(&g->e, CYC, RELOJ_GRANO);
		sin_enlace[0] = jit_x64_jcc(&g->e, X64_AE);
		jit_x64_cmp_mi(&g->e, CTX, D_REINTENTO, 0);
		sin_enlace[1] = jit_x64_jcc(&g->e, X64_NE);
	}
	else
	{
		jit_x64_cmp_rm32(&g->e, CYC, CTX, D_LIMITE);
		sin_enlace[0] = jit_x64_jcc(&g->e, X64_AE);
		sin_enlace[1] = sin_enlace[0];
		sin_enlace[1].sitio = 0;
	}

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

	/* La misma condicion del corte, en una comparacion: ver gen_corte(). */
	if (jit_corte_viejo)
	{
		jit_x64_cmp_ri(&g->e, CYC, RELOJ_GRANO);
		sin_enlace[1] = jit_x64_jcc(&g->e, X64_AE);
		jit_x64_cmp_mi(&g->e, CTX, D_REINTENTO, 0);
		sin_enlace[2] = jit_x64_jcc(&g->e, X64_NE);
	}
	else
	{
		jit_x64_cmp_rm32(&g->e, CYC, CTX, D_LIMITE);
		sin_enlace[1] = jit_x64_jcc(&g->e, X64_AE);
		sin_enlace[2] = sin_enlace[1];
		sin_enlace[2].sitio = 0;
	}

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

/* Nanosegundos gastados traduciendo, sin muestrear: la sonda de tirones
   (perf.h) los mira POR CUADRO, y un muestreo no dice nada de un cuadro
   concreto. */
unsigned long long			jit_ns_traducir = 0;

/* El camino largo de la verificacion por entrada: veces y palabras. */
static unsigned long long	jit_verif_lento = 0;
static unsigned long long	jit_verif_palabras = 0;

/* Para la sonda de tirones (perf.h): cuantas traducciones lleva la corrida.
   Un cuadro que traduce cincuenta bloques de golpe --entrar a una zona
   nueva-- se ve como un tiron, y sin este numero al lado no se distingue de
   uno que subio texturas. */
unsigned long long jit_cuenta_traducidos(void)
{
	return jit_traducidos;
}
static unsigned long long	jit_instr_bloque = 0;
static unsigned long long	jit_fallidos = 0;
static unsigned long long	jit_enlaces_atados = 0;
static unsigned long long	jit_enlaces_dinamicos = 0;

static int tr_es_llamada(const jit_plantilla * p)
{
	return p->emitir == pl_bsr108 || p->emitir == pl_jsr111
		|| p->emitir == pl_bsrf109;
}

/*
	Descubrimiento: camina las palabras desde `pc` resolviendo cada una por
	OP_HANDLER() y parando cuando una no tiene plantilla, cuando se acaba la
	pagina de 1 KB o cuando se llega al tope de instrucciones.
*/
static int tr_descubrir(jit_traduccion * t, DWORD pc)
{
	const WORD *	codigo = (const WORD *) MMU_FETCH_PUNTERO(pc);
	/* La ventana de 1 KB es el contrato de la busqueda BAJO MMU (misma
	   pagina con cualquier tamano); sin MMU no hay busqueda que reproducir
	   -- el mismo criterio que ya usan los enlaces -- y cortar ahi era un
	   limite artificial de largo: primer paso de superbloques. La traza
	   ENTERA vive en la ventana de la entrada, tambien lo seguido por flujo. */
	DWORD			ventana = pc & ~(DWORD) (JIT_LIMITE_PAG - 1);
	int				seguido = 0;
	/* El rastreo del punto de retorno: lo arma un BSR seguido, lo invalida
	   cualquier escritor de PR, y lo consume el RTS que continua. Puede
	   equivocarse sin romper nada -- la emision verifica PR en caliente. */
	DWORD			pr_conocido = 0;
	int				pr_valido   = 0;
	DWORD			pr_apilado  = 0;
	int				pr_apilado_valido = 0;
	int				i;

	t->pc0        = pc;
	t->n          = 0;
	t->n_adelante = 0;
	t->modo       = mmu_activa ? JIT_ACC_MMU : JIT_ACC_PLANO;
	t->fpu        = -1;
	t->fin        = JIT_FIN_TOPE;	/* si nada corta antes, corto el tope */
	t->corte      = 0;

	while (t->n < JIT_MAX_INSTR)
	{
		WORD		instr;
		opcode_f *	f;
		const jit_plantilla * p;

		if (mmu_activa && (pc & ~(DWORD) (JIT_LIMITE_PAG - 1)) != ventana)
		{
			t->fin = JIT_FIN_VENTANA;
			break;
		}

		/* Una traza que ya siguio un flujo puede desembocar en codigo que ya
		   tiene: ahi se corta, y el salto interno o la salida hacia la propia
		   entrada cierran el lazo (la forma del bloque-lazo de siempre). */
		if (seguido)
		{
			int ya = 0;

			for (i = 0; i < t->n; i++)
				if (t->pc[i] == pc)
				{
					ya = 1;
					break;
				}

			if (ya)
			{
				t->fin = JIT_FIN_LAZO;
				break;
			}
		}

		instr = *codigo;
		f     = OP_HANDLER(oplist, instr);
		p     = jit_plantilla_de(f);

		if (p == NULL)
		{
			jit_censar(instr);
			t->fin   = JIT_FIN_PLANTILLA;
			t->corte = instr;
			break;
		}

		/* Una fila FPU ata el bloque al modo vigente. El manejador ya salio
		   de la oplist de ese modo, asi que la palabra y la semantica son
		   coherentes por construccion; jit_fpu_visto es la clave viva.

		   **Bajo la MMU las filas FPU no se traducen** (el bloque corta aqui).
		   No es cautela vaga: la caza de la sesion 2026-08-08 encontro que en
		   los thunks dinamicos de WinCE --dos excepciones encadenadas, refill
		   de ITLB y address error reparado por el kernel-- el orden de los
		   avances de URC entre busqueda y datos difiere legitimamente entre
		   ejecutar por instruccion y por bloque, y URC decide el reemplazo de
		   la TLB, o sea el camino del guest. Un avance corrido costo 633
		   millones de instrucciones de divergencia quince segundos despues.
		   El expediente entero esta en el plan; levantar esto pide resolver
		   esa restriccion, no borrar este if. */
		if (p->fpu)
		{
			/* Con SR.FD puesto, la instruccion alza 0x800 en el despacho,
			   ANTES de tocar nada (run()); un bloque la ejecutaria directo
			   -- traduce la direccion (avance de URC de mas) y escribe el
			   banco viejo --, que es exactamente la divergencia que la caza
			   de la compuerta encontro: el cambio perezoso de contexto FPU
			   de WinCE. FD tambien vive en la clave (bit 3, ver jit.h), asi
			   que el bloque traducido con FD=0 se rechaza al entrar con
			   FD=1; este corte cubre la traduccion misma. */
			if (fpu_deshabilitada)
			{
				jit_censar(instr);
				t->fin   = JIT_FIN_FPU;
				t->corte = instr;
				break;
			}

			/* La compuerta MMU+FPU, levantada por omision desde que el
			   protocolo salio canonico en los seis escenarios (su motivo
			   era el 0x800 de arriba). DCEMU_JIT_SIN_FPU_MMU=1 la cierra:
			   aislamiento, y la linea base anterior byte a byte. */
			if (t->modo == JIT_ACC_MMU && !jit_fpu_mmu)
			{
				jit_censar(instr);
				t->fin   = JIT_FIN_FPU;
				t->corte = instr;
				break;
			}

			t->fpu = (int) jit_fpu_visto;
		}

		/* La fila terminal: se anexa y el bloque TERMINA en ella. Con la
		   palanca apagada corta como si no tuviera plantilla, que es la
		   conducta anterior. Va despues del bloque FPU de arriba para que
		   FSCHG/FRCHG lleguen aca ya atados a la clave y con FD cubierto. */
		if (p->terminal)
		{
			if (!jit_terminales)
			{
				jit_censar(instr);
				t->fin   = JIT_FIN_PLANTILLA;
				t->corte = instr;
				break;
			}

			t->pc[t->n]       = pc;
			t->palabra[t->n]  = instr;
			t->pl[t->n]       = p;
			t->sigue_en[t->n] = 0;
			t->n++;
			t->fin = JIT_FIN_TERMINAL;
			break;
		}

		t->pc[t->n]       = pc;
		t->palabra[t->n]  = instr;
		t->pl[t->n]       = p;
		t->sigue_en[t->n] = 0;
		t->n++;

		if (p->apila_pr)
		{
			pr_apilado        = pr_conocido;
			pr_apilado_valido = pr_valido;
		}

		if (p->escribe_pr)
		{
			/* El pop repone lo que esta misma traza apilo; cualquier otro
			   escritor invalida. Un desbalance real lo atrapa la guarda. */
			if (p->escribe_pr == 2 && pr_apilado_valido)
			{
				pr_conocido       = pr_apilado;
				pr_valido         = 1;
				pr_apilado_valido = 0;
			}
			else
				pr_valido = 0;
		}

		/*
			El par TERMINA la traza. En un retorno o una llamada lo que sigue
			es otra funcion; dejar la cola tras el RTS anexo codigo muerto a
			cada bloque -- SR2 +27 % de arena y dos puntos de tanda perdidos,
			la leccion del tope de 96 otra vez. (El RTS seguido por flujo, que
			exige ranura sin memoria, pasa por su propio camino mas abajo.)
		*/
		if (p->par && jit_par_rts
			&& !(jit_flujo && pr_valido && p->sigue == 3)
			&& t->n < JIT_MAX_INSTR
			&& !(mmu_activa
				&& ((pc + 2) & ~(DWORD) (JIT_LIMITE_PAG - 1)) != ventana))
		{
			WORD					rinstr = codigo[1];
			const jit_plantilla *	rp     =
				jit_plantilla_de(OP_HANDLER(oplist, rinstr));

			if (rp != NULL && rp->accede && !rp->rama && !rp->fpu
				&& !rp->propia && !rp->terminal
				&& (!tr_es_llamada(p)
					|| (jit_par_llamadas
						&& !rp->escribe_pr && !rp->apila_pr)))
			{
				t->pc[t->n]       = pc + 2;
				t->palabra[t->n]  = rinstr;
				t->pl[t->n]       = rp;
				t->sigue_en[t->n] = 0;
				t->n++;
				t->fin = JIT_FIN_PAR;
				break;
			}
		}

		/*
			El superbloque por flujo: BRA, BSR y el RTS con punto de retorno
			conocido no cortan la traza -- la caminata puede SEGUIR. La ranura
			viaja con la rama, o el bloque termina antes de ella (la regla del
			recorte, decidida aqui en linea porque la continuacion depende).
		*/
		if (jit_flujo && p->sigue && (p->sigue != 3 || pr_valido))
		{
			const jit_plantilla *	rp;
			WORD					rinstr;
			DWORD					dest;
			int						en_traza;

			/* La ranura tiene que caber (tope y ventana) y ser admisible:
			   sin acceso a memoria, sin rama, sin FPU -- las reglas de las
			   616 instrucciones y del PC += 2. Si no, antes de la rama. */
			if (t->n >= JIT_MAX_INSTR
				|| (mmu_activa
					&& ((pc + 2) & ~(DWORD) (JIT_LIMITE_PAG - 1)) != ventana))
			{
				t->n--;
				t->fin   = JIT_FIN_RANURA;
				t->corte = instr;
				break;
			}

			rinstr = codigo[1];
			rp     = jit_plantilla_de(OP_HANDLER(oplist, rinstr));

			if (rp == NULL || rp->accede || rp->rama || rp->fpu)
			{
				/* El par sobrevive al flujo: una rama con ranura de memoria
				   no se sigue, pero tampoco se corta -- el par se anexa y
				   la traza termina ahi, como en el camino sin flujo. */
				if (p->par && jit_par_rts && rp != NULL
					&& rp->accede && !rp->rama && !rp->fpu && !rp->propia
					&& !rp->terminal
					&& (!tr_es_llamada(p)
						|| (jit_par_llamadas
							&& !rp->escribe_pr && !rp->apila_pr)))
				{
					t->pc[t->n]       = pc + 2;
					t->palabra[t->n]  = rinstr;
					t->pl[t->n]       = rp;
					t->sigue_en[t->n] = 0;
					t->n++;
					t->fin = JIT_FIN_PAR;
					break;
				}

				t->n--;
				t->fin   = (rp != NULL && rp->accede) ? JIT_FIN_RANURA_MEM
													  : JIT_FIN_RANURA;
				t->corte = rinstr;
				break;
			}

			t->pc[t->n]       = pc + 2;
			t->palabra[t->n]  = rinstr;
			t->pl[t->n]       = rp;
			t->sigue_en[t->n] = 0;
			t->n++;

			dest = (p->sigue == 3) ? pr_conocido
								   : tr_destino12(t, t->n - 2);

			en_traza = 0;
			for (i = 0; i < t->n; i++)
				if (t->pc[i] == dest)
				{
					en_traza = 1;
					break;
				}

			/* Destino ya en la traza: el lazo se cierra por salto interno --
			   y el RTS igual continua, con su guarda, hacia atras. */
			if (en_traza)
			{
				if (p->sigue == 3)
				{
					t->sigue_en[t->n - 2] = dest;
					pr_valido = 0;
					jit_flujo_rts++;
				}

				t->fin = JIT_FIN_LAZO;
				break;
			}

			/* Un BRA con destino alcanzable por la caminata contigua no se
			   sigue: la forma if/else se captura entera asi, y seguir
			   perderia el else que el condicional de arriba necesita. Para
			   BSR no aplica -- su fall-through es el punto de retorno, que
			   la continuacion del RTS repone. */
			if (p->sigue == 1
				&& dest > pc + 2
				&& (dest - (pc + 4)) / 2 < (DWORD) (JIT_MAX_INSTR - t->n))
			{
				pc     += 4;
				codigo += 2;
				continue;
			}

			/* Bajo MMU, fuera de la ventana no se sigue: el salto queda y
			   sale como siempre (enlace, o la salida dinamica del RTS). */
			if (mmu_activa
				&& (dest & ~(DWORD) (JIT_LIMITE_PAG - 1)) != ventana)
			{
				t->fin = JIT_FIN_VENTANA;
				break;
			}

			/* Seguir el flujo. El puntero del destino no tiene efectos: bajo
			   MMU es la misma pagina vigente (la guarda de arriba), y sin
			   MMU no hay busqueda. */
			if (p->sigue == 1)
				jit_flujo_seguidos++;
			else if (p->sigue == 2)
			{
				/* La llamada entra en linea: el punto de retorno queda
				   rastreado para el RTS de la propia traza. */
				pr_conocido = t->pc[t->n - 2] + 4;
				pr_valido   = 1;
				jit_flujo_bsr++;
			}
			else
			{
				t->sigue_en[t->n - 2] = dest;
				pr_valido = 0;
				jit_flujo_rts++;
			}

			seguido = 1;
			pc      = dest;
			codigo  = (const WORD *) MMU_FETCH_PUNTERO(dest);
			continue;
		}

		pc     += 2;
		codigo += 1;
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
	/* Las filas FPU tampoco entran en ranura, aunque no accedan a memoria:
	   sus manejadores hacen PC += 2 sobre el contexto, y en una ranura eso
	   pisaria el destino que el salto capturo en O_PC. */
	/* La excepcion de los pares: su ranura con memoria no corta -- la emision
	   sincroniza con el PC de la rama antes de tocar nada, asi que una falta
	   reejecuta desde ella como en el interprete. En una llamada, ademas, la
	   ranura no puede tocar PR. Las filas `propia` quedan afuera: cuentan su
	   intento por su cuenta y el par lo cuenta antes (regla de run()). */
	for (i = 0; i < t->n; i++)
	{
		const jit_plantilla * r = (i + 1 < t->n) ? t->pl[i + 1] : NULL;

		if (!t->pl[i]->ranura)
			continue;

		/* La fila FPU de emision directa (sin_pc) es admisible en la ranura:
		   su clave ya quedo atada cuando el lazo principal la anexo. Las
		   terminales jamas: son escritores de SR/FPSCR con sync, y en una
		   ranura romperian los bancos bajo los slots. */
		if (r != NULL && !r->rama && !r->terminal
			&& (!r->fpu || (r->sin_pc && jit_ranura_fpu))
			&& (!r->accede
				|| (jit_par_rts && t->pl[i]->par && !r->propia
					&& (!tr_es_llamada(t->pl[i])
						|| (jit_par_llamadas
							&& !r->escribe_pr && !r->apila_pr)))))
			continue;

		t->n   = i;
		t->fin = (r != NULL && r->accede && !r->rama && !r->fpu)
			? JIT_FIN_RANURA_MEM : JIT_FIN_RANURA;
		t->corte = (r != NULL) ? t->palabra[i + 1] : t->palabra[i];
		break;
	}

	return t->n;
}

/*
	Emision. Cada instruccion lleva lo mismo que en los bloques escritos a
	mano: la sincronizacion previa si toca memoria, sus ciclos, su cuenta y su
	corte del bloque periodico en la frontera siguiente.
*/
/*
	El censo de bytes emitidos: de que se compone el bloque, por plantilla y
	por rubro del conductor (prologo+verificacion, sync por acceso, cortes y
	contadores, salidas). Existe porque el reparto por guest dio 254 bytes por
	instruccion en los guests MMU contra 94 en CT, y ese factor 2,7 es el
	sospechoso de icache de los 7,4 ns de SR2 -- pero "donde estan los bytes"
	no se puede corregir sin medirse. Acumula al traducir (frio); imprime con
	DCEMU_JIT_SONDA_BYTES=1.
*/
/* Filas con acceso que no alcanzaron ningun sitio de llamada. Contador de
   control de la sincronizacion en el talon: cero siempre. */
static unsigned long long	jit_sync_sin_consumir = 0;

/* DCEMU_JIT_SYNC_PREVIA=1: la sincronizacion vuelve delante de la plantilla,
   en el camino rapido. Reproduce la emision anterior byte por byte. */
static int				jit_sync_previa = 0;


static unsigned long long	jit_bytes_prologo = 0;
static unsigned long long	jit_bytes_sync;	/* declarada arriba, con gen_sync_pendiente */
static unsigned long long	jit_bytes_resto   = 0;
static unsigned long long	jit_bytes_salidas = 0;
static unsigned long long	jit_bytes_pl[sizeof(jit_plantillas) / sizeof(jit_plantillas[0])];
static unsigned long long	jit_instr_pl[sizeof(jit_plantillas) / sizeof(jit_plantillas[0])];

static int jit_sonda_bytes = 0;

static void tr_emitir_cuerpo(jit_gen * g, jit_traduccion * t)
{
	int i;

	g->ocioso_bumps = (jit_ociosos >= 1 && t->modo == JIT_ACC_PLANO);

	tr_prologo(g, t);

	if (jit_sonda_cruces)
		jit_x64_add64_mi(&g->e, CTX, D(&jit_bloques_corridos), 1);

	t->desp_cuerpo = jit_x64_largo(&g->e);
	jit_bytes_prologo += t->desp_cuerpo;

	for (i = 0; i < t->n; i++)
	{
		const jit_plantilla * p = t->pl[i];
		DWORD pc_i   = t->pc[i];
		DWORD pc_sig = pc_i + 2;
		int   idx    = (int) (p - jit_plantillas);
		unsigned a0  = jit_x64_largo(&g->e), a1, a2;

		t->etiqueta[i] = jit_x64_aqui(&g->e);

		if (p->rama)
		{
			p->emitir(g, t, i);
			jit_bytes_pl[idx] += jit_x64_largo(&g->e) - a0;
			jit_instr_pl[idx]++;
			continue;
		}

		/* La sincronizacion queda ARMADA, no emitida: la pone gen_llamar() o
		   tr_manejador() justo antes de la llamada a C, que es el unico sitio
		   desde donde la fila puede faltar. Ver jit_gen.

		   Con DCEMU_JIT_SYNC_PREVIA=1 vuelve al camino rapido, delante de la
		   plantilla: es la conducta anterior byte por byte y el brazo del A/B. */
		if (jit_sync_previa)
		{
			if (p->accede)
				tr_sync(g, t, pc_i);

			g->sync_activa = 0;
			jit_bytes_sync += jit_x64_largo(&g->e) - a0;
		}
		else
		{
			g->sync_activa = p->accede;
			g->sync_usada  = 0;
			g->sync_t      = t;
			g->sync_pc     = pc_i;

			/* El intento se cuenta ANTES de la plantilla, como en run(): si el
			   acceso falta, el longjmp sale por encima del epilogo y el talon
			   ya volco la cuenta con este intento adentro. Es lo unico del
			   volcado que se queda en el camino rapido, y es un byte. */
			if (p->accede)
				jit_x64_inc_r(&g->e, N);
			}

		a1 = jit_x64_largo(&g->e);

		p->emitir(g, t, i);

		/* Una fila que toca memoria y no llego a ningun sitio de llamada podria
		   faltar sin instantanea: se descarta el bloque y se cuenta, en vez de
		   emitir algo que solo se rompe cuando el guest toque una pagina que no
		   esta. Tiene que quedar en cero en toda corrida. */
		if (g->sync_activa && !g->sync_usada)
		{
			jit_sync_sin_consumir++;
			g->e.desborde = 1;
		}

		g->sync_activa = 0;

		a2 = jit_x64_largo(&g->e);
		jit_bytes_pl[idx] += a2 - a1;
		jit_instr_pl[idx]++;

		if (p->ciclos)
			jit_x64_add_ri(&g->e, CYC, p->ciclos);

		if (!p->accede && !p->propia)
			jit_x64_inc_r(&g->e, N);

		/* Sin ciclos nuevos la condicion del corte no pudo volverse cierta.
		   Y tras la ultima instruccion no hace falta: el bloque termina. */
		if (p->ciclos && i + 1 < t->n && t->pc[i + 1] == pc_sig)
			gen_corte(g, pc_sig);

		/* La traza siguio un flujo: la fila siguiente no es pc_i + 2. Este
		   fall-through -- alcanzable solo si un salto interno entra a la
		   ranura como instruccion comun -- sale del bloque por su PC real en
		   vez de caer en el destino seguido. En el camino real es codigo
		   muerto: el BRA ya salto por su arista. */
		if (i + 1 < t->n && t->pc[i + 1] != pc_sig)
			gen_salir_enlazable(g, t, pc_sig);

		/* Tras la ultima tampoco hace falta este: toda salida del bloque --
		   despachador, cadena o talon -- compara la validez antes de seguir. */
		if (p->escribe && t->modo == JIT_ACC_MMU && i + 1 < t->n
			&& jit_corte_epoca)
			gen_corte_epoca(g, pc_sig);

		jit_bytes_resto += jit_x64_largo(&g->e) - a2;
	}

	jit_bytes_salidas -= jit_x64_largo(&g->e);	/* se completa tras las salidas */

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

	/* El bloque terminal sale por el PC del contexto y sin enlace; cualquier
	   otro, por la salida enlazable de siempre con pc+2 constante. */
	if (t->fin == JIT_FIN_TERMINAL)
		gen_salir_terminal(g, t);
	else
		gen_salir_enlazable(g, t, t->pc[t->n - 1] + 2);
	tr_epilogo(g, t);

	/*
		El talon de sincronizacion del bloque, si alguien lo llamo. Va al final y
		por eso es inalcanzable por caida: el epilogo termina en un salto. Los
		sitios se fijan justo antes de emitirlo, que es cuando `fijar` apunta al
		lugar donde va a empezar.
	*/
	if (g->n_sync_llam)
	{
		for (i = 0; i < g->n_sync_llam; i++)
			jit_x64_fijar(&g->e, g->sync_llam[i]);

		tr_volcar_regs(g, t);
		gen_volcar_cuenta(g);
		jit_x64_ret(&g->e);
	}

	jit_bytes_salidas += jit_x64_largo(&g->e);	/* la otra mitad de la resta */
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
/*
	La costura de un enlace directo (fase de registros persistentes). El que
	salta ya volco TODO su conjunto -- el contexto esta al dia -- y el volcado
	no toca los registros: lo que ambos colocan en hogar canonico ya esta
	donde el sucesor lo espera. Devuelve a donde saltar:

	 - B ⊆ A en canonicas y sin libres: directo al cuerpo, costura vacia.
	 - falta un subconjunto: un talon con solo esas cargas y el salto.
	 - nada en comun, sin hogares, sin lugar o sin cuerpo: NULL, y el parche
	   usa el prologo completo de siempre.

	Un reparcheo (sitio indirecto que aprende otro destino) emite otra
	costura y abandona la anterior: crecimiento acotado por
	JIT_MAX_REPARCHEOS, contado en jit_costuras.
*/
static unsigned char * jit_emitir_costura(const jit_bloque * fuente,
	const jit_bloque * destino)
{
	unsigned		faltan;
	x64_emisor		e;
	unsigned char *	inicio;
	unsigned		usado;
	int				r;

	if (!jit_hogares || fuente == NULL || destino->cuerpo == NULL)
		return NULL;

	faltan = (destino->canonicas & ~fuente->canonicas)
		   | (destino->ranuras & ~destino->canonicas);

	if (faltan == destino->ranuras && faltan != 0)
		return NULL;					/* nada elidible: prologo entero */

	if (faltan == 0)
	{
		jit_costuras_vacias++;
		return (unsigned char *) (size_t) destino->cuerpo;
	}

	/* El talon con cargas parciales PERDIO su A/B (CT +0,7 % consistente,
	   rangos disjuntos): el salto extra y la linea fria de icache cuestan
	   mas que las 4-5 cargas de contexto caliente que eliden. Queda detras
	   de DCEMU_JIT_COSTURAS=2 para remedirlo si el reparto cambia; por
	   omision solo la costura vacia -- B dentro de A, salto directo al
	   cuerpo, cero saltos extra -- que es ganancia pura. */
	if (!jit_costuras_talones)
		return NULL;

	jit_codigo_us = (jit_codigo_us + 15u) & ~15u;

	if (jit_codigo_us >= jit_codigo_tam)
		return NULL;

	jit_x64_iniciar(&e, jit_codigo + jit_codigo_us,
		jit_codigo_tam - jit_codigo_us);
	inicio = jit_x64_aqui(&e);

	for (r = 0; r < 16; r++)
		if (faltan & (1u << r))
			jit_x64_mov_rm(&e, jit_a[destino->mapa[r]], CTX, O_R(r));

	jit_x64_jmp_a(&e, (const unsigned char *) (size_t) destino->cuerpo);

	if (e.desborde || !jit_disp_ok)
		return NULL;

	/* El arena entero es PAGE_EXECUTE_READWRITE desde jit_arena_reservar():
	   no hay proteccion que cambiar. */
	usado = jit_x64_largo(&e);
	jit_codigo_us += usado;
	jit_costuras++;

	return inicio;
}

/* ------------------------------------------------------------------------ */
/* La elision de lazos ociosos                                              */
/* ------------------------------------------------------------------------ */

/*
	El censo del contrato dejo el blanco con nombre y numero: el lazo de
	espera de Crazy Taxi son el 47,22 % de sus instrucciones. Dentro de un
	grano no corre nada externo al guest --ticks, DMA, AICA y lineas de video
	viven en el bloque periodico-- y en modo plano el camino rapido emitido
	solo entra en la RAM del sistema (todo lo demas va por ayudante). Asi que
	si una vuelta de un lazo devuelve el mismo estado de registros que al
	entrar y en el medio no hubo escritura emitida, ni llamada a C, ni
	entrada al despachador, TODAS las vueltas siguientes hasta el corte son
	identicas: se saltean k vueltas sumando k por ciclos y k por
	instrucciones, y la ultima parcial corre de verdad para que el corte
	caiga en la misma instruccion que en el interprete. La grilla del grano
	no se mueve, no se agrega ni se quita ningun servicio, y el total de
	instrucciones queda al digito.

	La sonda vive en un talon por arista de retroceso: un enlace estatico
	plano cuyo destino esta antes que la entrada del bloque que salta (el
	lazo de CT es tres bloques --el par JSR, el RTS+NOP y el cuerpo-- y su
	arista es el BF final del cuerpo hacia la cabeza). El parche del enlace
	apunta el salto al talon; el talon vuelca el contador, llama a la sonda
	con su arista, suma los ciclos elididos y salta al sucesor de siempre. La
	sonda compara el estado con la instantanea que la misma arista tomo la
	vuelta anterior: R0-R15, SR, PR, GBR, MACH y MACL. Lo que no esta en esa
	lista no puede cambiar en una vuelta pura: SSR/SPC/VBR/bancos/FPSCR solo
	se tocan por manejador o fila terminal (bump), y la FPU entera queda
	cubierta porque **un bloque con filas FPU no recibe enlaces**: en una
	vuelta que vuelve a la sonda sin pasar por el despachador todos los
	bloques entraron por enlace, o sea que ninguno tiene filas FPU.

	La retirada es lo que la hace barata: a los JIT_OCIOSO_RETIRADA fallos
	seguidos sin elidir, la arista se reparchea directa al sucesor y la sonda
	deja de correr. Sin ella cada arista de retroceso de un lazo de trabajo
	pagaria una llamada y ~22 comparaciones por vuelta.

	Bajo MMU no entra (v1): URC avanza por vuelta en los aciertos emitidos y
	en los puentes, y el arbol no tiene un contador de avances siempre
	encendido. Los bloques MMU tampoco emiten bumps: una cadena bajo MMU nunca
	llega a una sonda, asi que DCDoom y Sega Rally 2 quedan inertes por
	construccion salvo en su codigo de arranque plano. Con el buscador
	emitido tampoco se instala: ese despacho no pasa por C y no ensucia.
*/
typedef struct
{
	DWORD				regs[16];
	DWORD				sr, pr, gbr, mach, macl;
	DWORD				cyc;
	unsigned long long	instr;
	unsigned long long	gen[JIT_IMP_N];
	int					valido;
	int					fallos;			/* seguidos sin elidir */
	int					retirada;
	unsigned char *		sitio_jmp;		/* el rel32 del enlace, para retirarse */
	unsigned char *		directo;		/* adonde iba el enlace sin sonda */
	DWORD				pc_fuente;
	DWORD				pc_destino;
	unsigned long long	sondas;
	unsigned long long	elisiones;
	unsigned long long	vueltas;
	unsigned long long	instr_elididas;
	unsigned long long	ciclos_elididos;
	/* El censo del fallo: por que esta vuelta no se pudo elidir. Las clases
	   no son excluyentes entre si --una vuelta puede mover dos-- asi que su
	   suma puede pasar el total de fallos, y se dice al imprimirlo. */
	unsigned long long	fallo_clase[JIT_IMP_N];
	unsigned long long	fallo_regs;			/* generacion quieta, registros no */
	unsigned long long	fallo_sin_lugar;	/* punto fijo, pero k == 0 */
} jit_arista;

#define JIT_ARISTAS_N			4096
#define JIT_OCIOSO_RETIRADA		16

static jit_arista			jit_aristas[JIT_ARISTAS_N];
static int					jit_n_aristas = 0;
static unsigned long long	jit_aristas_sin_lugar = 0;
static unsigned long long	jit_ocioso_retiradas = 0;

/*
	El censo de los enlaces que NO reciben sonda, por motivo. Es la mitad del
	censo que la sonda no puede dar: una arista sin sonda no cuenta nada, asi
	que sin esto 'cero elisiones' no distingue 'no habia lazos' de 'los lazos
	estaban del otro lado de una condicion'. Se cuenta al parchear --una vez
	por enlace instalado, no por cruce--; lo que pesa cada motivo en TIEMPO lo
	dice la sonda de cruces (DCEMU_JIT_SONDA_CRUCES).
*/
#define JIT_RECH_DINAMICO	0	/* enlace con comparacion de PC (no estatico) */
#define JIT_RECH_PUENTE		1	/* puente entre paginas */
#define JIT_RECH_ADELANTE	2	/* el destino esta despues: no es retroceso */
#define JIT_RECH_MMU		3	/* bloque MMU (v1 no entra) */
#define JIT_RECH_FPU		4	/* bloque con filas FPU */
#define JIT_RECH_BUSCADOR	5	/* despacho emitido: no pasa por C */
#define JIT_RECH_SIN_LUGAR	6	/* sin arista libre o sin arena */
#define JIT_RECH_N			7

static const char * const jit_rech_nombre[JIT_RECH_N] =
{
	"dinamico", "puente", "adelante", "mmu", "fpu", "buscador", "sin lugar"
};

static unsigned long long	jit_ocioso_rech[JIT_RECH_N];
static unsigned long long	jit_ocioso_enlaces = 0;	/* enlaces parcheados */

/* La retirada: el rel32 del enlace vuelve a apuntar al sucesor directo, y
   el talon queda huerfano en el arena. Lo escribe la propia sonda, desde C
   y en el mismo hilo: el proximo cruce ya no pasa por aqui. */
static void jit_arista_retirar(jit_arista * a)
{
	long long rel = (long long) (a->directo - (a->sitio_jmp + 4));

	if (rel < -2147483647LL || rel > 2147483647LL)
		return;

	a->sitio_jmp[0] = (unsigned char) ((unsigned long long) rel & 0xFF);
	a->sitio_jmp[1] = (unsigned char) (((unsigned long long) rel >> 8) & 0xFF);
	a->sitio_jmp[2] = (unsigned char) (((unsigned long long) rel >> 16) & 0xFF);
	a->sitio_jmp[3] = (unsigned char) (((unsigned long long) rel >> 24) & 0xFF);

	a->retirada = 1;
	jit_ocioso_retiradas++;
}

/*
	La sonda. Llega con el contexto al dia --el enlace ya volco los registros
	y CYC-- y con el contador ya volcado por el talon. Devuelve los ciclos
	elididos, que el talon suma a CYC; las instrucciones se suman aqui.
*/
static unsigned jit_ocioso_sonda(jit_arista * a)
{
	context_t *			c     = &core.context;
	DWORD				cyc   = c->cycles;
	unsigned long long	instr = jit_estado.instr;
	DWORD				k     = 0;
	DWORD				vuelta = 0;

	a->sondas++;

	if (a->valido && memcmp(a->gen, jit_ocioso_gen, sizeof(a->gen)) == 0
		&& memcmp(a->regs, c->registers, sizeof(a->regs)) == 0
		&& a->sr == c->SR_REG.SR_ALL && a->pr == c->PR_REG
		&& a->gbr == c->GBR_REG
		&& a->mach == c->MACH_REG && a->macl == c->MACL_REG)
	{
		DWORD lim = (DWORD) intc_corte_limite;

		vuelta = cyc - a->cyc;

		/*
			Cuantas vueltas enteras caben antes del corte. La vuelta j empieza
			en cyc + (j-1)*vuelta y termina en cyc + j*vuelta, y el corte
			--`CYC >= limite` tras cada instruccion con ciclos-- no salta
			dentro de ella si y solo si su final queda por debajo del limite:
			CYC es monotono en la vuelta. La (k+1)-esima corre de verdad y
			corta donde el interprete cortaria. Con el reintento armado el
			limite es cero y k queda en cero.
		*/
		if (vuelta != 0 && cyc < lim)
			k = (lim - 1 - cyc) / vuelta;

		if (k != 0)
		{
			unsigned long long ni = instr - a->instr;

			jit_estado.instr += k * ni;

			if (perf_activa)
				perf_instrucciones += k * ni;

			a->elisiones++;
			a->vueltas         += k;
			a->instr_elididas  += k * ni;
			a->ciclos_elididos += k * vuelta;
			a->fallos = 0;
		}
		else
		{
			a->fallo_sin_lugar++;
			a->fallos++;
		}

		a->cyc   = cyc + k * vuelta;
		a->instr = jit_estado.instr;
	}
	else
	{
		/*
			El censo del fallo: que movio esta vuelta. Va AQUI --en el camino
			que ya copia la instantanea entera-- y no delante de la prueba,
			porque en el camino que elide no hay nada que clasificar y una
			comparacion de mas por vuelta se paga quince millones de veces por
			minuto en Crazy Taxi. Con la generacion quieta, la unica otra
			causa posible es el archivo de registros: no hace falta volver a
			compararlo para nombrarla.
		*/
		if (a->valido)
		{
			int	j;
			int	sucia = 0;

			for (j = 0; j < JIT_IMP_N; j++)
				if (a->gen[j] != jit_ocioso_gen[j])
				{
					a->fallo_clase[j]++;
					sucia = 1;
				}

			if (!sucia)
				a->fallo_regs++;
		}

		memcpy(a->regs, c->registers, sizeof(a->regs));
		a->sr     = c->SR_REG.SR_ALL;
		a->pr     = c->PR_REG;
		a->gbr    = c->GBR_REG;
		a->mach   = c->MACH_REG;
		a->macl   = c->MACL_REG;
		a->cyc    = cyc;
		a->instr  = instr;
		memcpy(a->gen, jit_ocioso_gen, sizeof(a->gen));
		a->valido = 1;
		a->fallos++;
	}

	if (a->fallos >= JIT_OCIOSO_RETIRADA && !a->retirada)
		jit_arista_retirar(a);

	return k * vuelta;
}

/*
	El talon de la sonda, emitido al parchear un enlace de retroceso plano.
	Se llega por salto y no por call, asi que la pila esta como la dejo el
	trampolin y la llamada a C sale alineada y con su espacio de sombra, igual
	que las de los bloques. Devuelve NULL si la arista no califica o no hay
	sitio, y entonces el enlace se parchea directo como siempre.
*/
static unsigned char * jit_emitir_sonda_ociosa(jit_enlace * e,
	const jit_bloque * fuente, const jit_bloque * destino,
	unsigned char * directo)
{
	x64_emisor		em;
	unsigned char *	inicio;
	jit_arista *	a;

	if (jit_ociosos < 2)
		return NULL;

	/* El censo por motivo, en el orden en que la condicion los descarta. */
	if (jit_buscador != NULL)
	{
		jit_ocioso_rech[JIT_RECH_BUSCADOR]++;
		return NULL;
	}

	if (mmu_activa || fuente->mmu != JIT_ACC_PLANO)
	{
		jit_ocioso_rech[JIT_RECH_MMU]++;
		return NULL;
	}

	if (fuente->fpu >= 0)
	{
		jit_ocioso_rech[JIT_RECH_FPU]++;
		return NULL;
	}

	if (destino->pc > fuente->pc)
	{
		jit_ocioso_rech[JIT_RECH_ADELANTE]++;
		return NULL;
	}

	if (e->sonda != 0)
		a = &jit_aristas[e->sonda - 1];	/* reparcheo: la misma arista */
	else if (jit_n_aristas < JIT_ARISTAS_N)
	{
		a        = &jit_aristas[jit_n_aristas++];
		e->sonda = jit_n_aristas;
	}
	else
	{
		jit_aristas_sin_lugar++;
		jit_ocioso_rech[JIT_RECH_SIN_LUGAR]++;
		return NULL;
	}

	jit_codigo_us = (jit_codigo_us + 15u) & ~15u;

	if (jit_codigo_us >= jit_codigo_tam)
	{
		jit_ocioso_rech[JIT_RECH_SIN_LUGAR]++;
		return NULL;
	}

	jit_x64_iniciar(&em, jit_codigo + jit_codigo_us,
		jit_codigo_tam - jit_codigo_us);
	inicio = jit_x64_aqui(&em);

	/* El contador pendiente, antes de que la sonda lo lea: las mismas tres
	   instrucciones de gen_volcar_cuenta. */
	jit_x64_add64_mr(&em, CTX, D_INSTR, N);

	if (perf_activa)
		jit_x64_add64_mr(&em, CTX, D_PERF, N);

	jit_x64_xor_rr(&em, N, N);

	jit_x64_mov64_ri(&em, X64_RCX, (unsigned long long) (size_t) a);

	if (!jit_x64_call_directo(&em, (const void *) jit_ocioso_sonda))
	{
		jit_x64_mov64_ri(&em, X64_RAX,
			(unsigned long long) (size_t) jit_ocioso_sonda);
		jit_x64_call_r(&em, X64_RAX);
	}

	/* Los ciclos elididos, al registro y al contexto (el enlace ya lo habia
	   volcado con el valor de antes). */
	jit_x64_alu_rr(&em, X64_ADD, CYC, X64_RAX);
	jit_x64_mov_mr(&em, CTX, O_CYC, CYC);
	jit_x64_jmp_a(&em, directo);

	if (em.desborde || !jit_disp_ok)
		return NULL;

	jit_codigo_us += jit_x64_largo(&em);

	/* La arista nace --o renace, en un reparcheo-- sin instantanea; los
	   contadores del resumen sobreviven al reparcheo. */
	a->valido     = 0;
	a->fallos     = 0;
	a->retirada   = 0;
	a->sitio_jmp  = e->sitio_jmp;
	a->directo    = directo;
	a->pc_fuente  = fuente->pc;
	a->pc_destino = destino->pc;

	return inicio;
}

static void jit_parchear_enlace(jit_enlace * e, const jit_bloque * fuente,
	const jit_bloque * destino)
{
	int          disp = D(&destino->epoca);
	DWORD        pc_fuente = fuente->pc;
	unsigned char * salto;
	long long    rel, rel_talon = 0;
	int          puente;

	/* Los bloques FPU no reciben enlaces: su validez depende de PR/SZ/Enables
	   y eso solo lo chequea el despachador. La sonda midio el porque: Crazy
	   Taxi conmuta SZ 13,2 millones de veces por minuto, asi que ni la clave
	   ni una guarda por sitio lo aguantan; entrar por el despachador si. */
	if (destino->fpu >= 0)
		return;

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

	/* El enlace directo intenta coser (los puentes quedan con el prologo
	   entero en esta fase; su talon ya paga la llamada de busqueda). */
	if (!puente)
	{
		unsigned char * costura = jit_emitir_costura(fuente, destino);

		if (costura != NULL)
			salto = costura;
	}

	/* La sonda de la elision de ociosos, delante del sucesor (costura o
	   prologo): solo en aristas de retroceso planas y estaticas. */
	jit_ocioso_enlaces++;

	if (!puente && e->sitio_pc == NULL)
	{
		unsigned char * sonda =
			jit_emitir_sonda_ociosa(e, fuente, destino, salto);

		if (sonda != NULL)
			salto = sonda;
	}
	else if (jit_ociosos >= 2)
		jit_ocioso_rech[puente ? JIT_RECH_PUENTE : JIT_RECH_DINAMICO]++;

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

/* De los nanosegundos de traduccion, los que se van atando enlaces. Ver la
   sonda de tirones en perf.h: sin separarlos, "traducir cuesta 0,65 ms" no
   dice si el costo es emitir o buscar a quien avisarle. */
unsigned long long			jit_ns_enlazar = 0;

/*
	**El indice de enlaces que esperan un PC.**

	La segunda mitad de jit_enlazar() --avisarle al bloque nuevo quien lo
	estaba esperando-- barria TODOS los bloques ya traducidos con un bucle
	interno por sus doce enlaces. Es cuadratico en la cantidad de bloques, y la
	sonda de tirones (perf.h) lo destapo midiendo por cuadro: en Crazy Taxi,
	**16,2 s de una corrida de 120 s emulados se iban traduciendo, y el 97 % de
	eso era este barrido**. Por eso el costo por traduccion sube con la
	corrida: 0,05 ms al principio y 0,65 ms al final, y un cuadro que traduce
	87 bloques tarda 58 ms en vez de 16. Los tirones eran esto.

	El indice invierte la pregunta: en vez de buscar quien esperaba, cada
	enlace estatico se anota bajo el PC que espera cuando su bloque nace, y el
	bloque nuevo mira su propia cuartilla. La lista se **agrega por la cola**,
	no por la cabeza, para que recorrerla de el mismo orden que el barrido
	--bloque ascendente, enlace ascendente--: los parches emiten costuras en el
	arena, asi que otro orden daria otra disposicion y las emisiones dejarian
	de compararse byte a byte.

	Los enlaces no se sacan nunca: un enlace ya parcheado tiene que volver a
	parchearse si mas tarde se traduce otro bloque con el mismo PC, que es lo
	que hacia el barrido. Y no puede desbordar -- hay una ranura por enlace
	posible, JIT_MAX_BLOQUES x JIT_MAX_ENLACES -- porque cada traduccion toma
	un bloque nuevo y el total esta topeado.
*/
#define JIT_PEND_N		65536			/* cuartillas, potencia de dos */
#define JIT_PEND_TOPE	(JIT_MAX_BLOQUES * JIT_MAX_ENLACES)

typedef struct
{
	int	bloque;
	int	enlace;
	int	sig;
} jit_pendiente;

static jit_pendiente *	jit_pend = NULL;
static int				jit_pend_n = 0;
static int				jit_pend_cabeza[JIT_PEND_N];
static int				jit_pend_cola[JIT_PEND_N];
static int				jit_pend_listo = 0;

/* El barrido lineal de antes. DCEMU_JIT_ENLACE_LINEAL=1 lo revive: es el A/B
   del escalon y la reproduccion exacta de la conducta anterior. */
static int				jit_enlace_lineal = 0;

/*
	La verificacion por entrada contra la clave entera, como era antes.
	DCEMU_JIT_VERIF_COMPLETA=1 lo revive -- pero **por el lado de los
	movimientos de epoca, no por el de la comparacion**.

	La primera version puso la palanca dentro de jit_verificar(), que corre una
	vez por entrada al despachador: 1490 millones de veces en los 180 s de
	Crazy Taxi. Ese guest no cambia nada con la palanca --sus contadores de
	verificacion salen identicos en los dos brazos-- y aun asi medía **+1,0 %
	con rangos disjuntos**: la rama era el costo, y estaba midiendo la palanca
	en vez del cambio. Ahora la palanca vive donde la epoca se mueve, que pasa
	miles de veces y no miles de millones, y el camino caliente queda con
	exactamente las mismas dos comparaciones que tenia.
*/
int						jit_verif_completa = 0;

#define JIT_PEND_H(pc)	(((unsigned) (pc) >> 1) & (JIT_PEND_N - 1))

static void jit_pend_agregar(int bloque, int enlace, DWORD pc)
{
	unsigned h;

	if (!jit_pend_listo)
	{
		int j;

		jit_pend = (jit_pendiente *)
			calloc(JIT_PEND_TOPE, sizeof(jit_pendiente));

		if (jit_pend == NULL)
		{
			/* Sin sitio para el indice se vuelve al barrido: lento, pero la
			   conducta es la misma. Callar y perder enlaces seria cambiar la
			   ejecucion sin decirlo. */
			jit_enlace_lineal = 1;
			jit_pend_listo = 1;
			fprintf(stderr, "jit: sin memoria para el indice de enlaces;"
				" se vuelve al barrido lineal\n");
			return;
		}

		for (j = 0; j < JIT_PEND_N; j++)
			jit_pend_cabeza[j] = jit_pend_cola[j] = -1;

		jit_pend_listo = 1;
	}

	if (jit_pend == NULL || jit_pend_n >= JIT_PEND_TOPE)
		return;

	h = JIT_PEND_H(pc);

	jit_pend[jit_pend_n].bloque = bloque;
	jit_pend[jit_pend_n].enlace = enlace;
	jit_pend[jit_pend_n].sig    = -1;

	if (jit_pend_cola[h] < 0)
		jit_pend_cabeza[h] = jit_pend_n;
	else
		jit_pend[jit_pend_cola[h]].sig = jit_pend_n;

	jit_pend_cola[h] = jit_pend_n++;
}

static void jit_enlazar(jit_bloque * nuevo)
{
	unsigned long long t0 = perf_ahora();
	int i, k;
	int nuevo_idx = (int) (nuevo - jit_bloques);

	for (i = 0; i < nuevo->n_enlaces; i++)
	{
		const jit_bloque * d;

		if (nuevo->enlace[i].sitio_pc != NULL)
			continue;			/* dinamico: su destino se aprende corriendo */

		if (!jit_enlace_lineal)
			jit_pend_agregar(nuevo_idx, i, nuevo->enlace[i].pc);

		d = jit_buscar(nuevo->enlace[i].pc);

		if (d != NULL)
			jit_parchear_enlace(&nuevo->enlace[i], nuevo, d);
	}

	if (jit_enlace_lineal)
	{
		for (k = 0; k < jit_n_bloques; k++)
		{
			jit_bloque * b = &jit_bloques[k];

			if (b == nuevo)
				continue;

			for (i = 0; i < b->n_enlaces; i++)
				if (b->enlace[i].sitio_pc == NULL
					&& b->enlace[i].pc == nuevo->pc)
					jit_parchear_enlace(&b->enlace[i], b, nuevo);
		}
	}
	else
	{
		for (k = jit_pend_cabeza[JIT_PEND_H(nuevo->pc)]; k >= 0;
			 k = jit_pend[k].sig)
		{
			jit_bloque * b = &jit_bloques[jit_pend[k].bloque];
			jit_enlace * e = &b->enlace[jit_pend[k].enlace];

			/* La cuartilla junta los PC que colisionan en el hash, asi que la
			   igualdad se comprueba igual: el indice acota la busqueda, no la
			   sustituye. */
			if (b == nuevo || e->pc != nuevo->pc || e->sitio_pc != NULL)
				continue;

			jit_parchear_enlace(e, b, nuevo);
		}
	}

	jit_ns_enlazar += perf_ahora() - t0;
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
		jit_parchear_enlace(e, b, d);

	jit_enlaces_dinamicos++;
}

/*
	La pagina del anfitrion donde viven las palabras de un bloque queda
	vigilada: una escritura ahi mueve la epoca y obliga a verificarlo entero
	otra vez. **Cabeza Y cola**: desde que la ventana de 1 KB es solo bajo MMU
	(superbloques, paso 1), un bloque plano de hasta 128 bytes puede cruzar el
	limite de 4 KB del host, y marcar solo la cabeza dejaba la cola sin
	vigilar -- una escritura del guest ahi no movia la epoca y el codigo viejo
	seguia corriendo, en silencio. Ningun banco lo observa (las tandas salen
	exactas), pero es la clase de agujero del corte de epoca: se cierra por
	construccion, no por suerte.
*/
static void jit_vigilar_tramo(const void * ptr, unsigned bytes)
{
	const unsigned char * p = (const unsigned char *) ptr;
	const unsigned char * fin = p + bytes - 1;

	jit_pag_codigo[JIT_PAG_BIT(p)]   = 1;
	jit_pag_codigo[JIT_PAG_BIT(fin)] = 1;

	/*
		La rejilla de 64 bytes va **entera**, no cabeza y cola: un bloque de
		hasta 96 instrucciones son 192 bytes, o sea cuatro lineas, y dejar las
		del medio sin marcar seria el mismo agujero que la cola de la pagina
		--una escritura ahi no moveria la epoca y el codigo viejo seguiria
		corriendo, en silencio-- pero cuatro veces mas probable.
	*/
	for (; p <= fin; p += 64)
		jit_lin_codigo[JIT_LIN_BIT(p)] = 1;

	jit_lin_codigo[JIT_LIN_BIT(fin)] = 1;
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

	/*
		El tope de bloques era otro desmarcar SILENCIOSO, y peor que el del
		arena: chocado, ninguna traduccion nueva ocurre nunca mas -- las
		retraducciones por remapeo incluidas, o sea el bug del "interpretado
		PARA SIEMPRE" de vuelta por otra puerta. SR2 lo chocaba a mitad de la
		corrida de 60 s por la fuga de inserciones (arreglada arriba) y hoy
		queda en ~29 000; una sesion larga puede chocarlo legitimamente, y eso
		tiene que avisar y contarse, como el tope de pistas del lector.
	*/
	if (jit_n_bloques >= jit_max_bloques)
	{
		if (jit_tope_bloques == 0)
			fprintf(stderr, "jit: tope de %d bloques alcanzado: no se traduce"
				" mas (las propuestas siguientes quedan interpretadas)\n",
				jit_max_bloques);

		jit_tope_bloques++;
		return NULL;
	}

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

			/* El mismo criterio de la ventana que el descubrimiento: bajo
			   MMU manda la pagina; sin MMU, que quede cerca (el tope de
			   instrucciones acota igual). */
			if (dest < t.pc0
				&& (mmu_activa
					? (dest & ~(JIT_LIMITE_PAG - 1))
						== (t.pc0 & ~(JIT_LIMITE_PAG - 1))
					: t.pc0 - dest <= 2u * JIT_MAX_INSTR))
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

	/* Las rutinas compartidas de traduccion, antes del primer bloque: nunca
	   en medio de uno, que comparte el arena. */
	jit_rut_trad_emitir();

	jit_codigo_us = (jit_codigo_us + 15u) & ~15u;

	/* El arena lleno era un desmarcar SILENCIOSO: SR2 chocaba los 192 MB con
	   la emision en linea y dejaba de traducir sin que ningun contador lo
	   dijera -- lo destapo el censo de bytes, no este contador, que existe
	   para la proxima vez. */
	if (jit_codigo_us >= jit_codigo_tam)
	{
		jit_arena_lleno++;
		return NULL;
	}

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
	b->cuerpo     = (void (*)(void)) (jit_codigo + jit_codigo_us
									  + t.desp_cuerpo);
	b->mmu        = t.modo;
	b->fpu        = t.fpu;
	b->fin        = (unsigned char) t.fin;
	b->corte      = t.corte;

	/* La herencia de la retraduccion: si este nacimiento reemplaza a un
	   bloque con lapida, carga con su cuenta (el tope por PC). */
	if (jit_retrad_pc != 0 && t.pc0 == jit_retrad_pc)
	{
		b->retraducido = jit_retrad_n;
		jit_retrad_pc  = 0;
	}

	/* El censo de variantes: cuantas traducciones son la hermana de otro
	   bloque del mismo PC bajo otro modo FPU. Camino de traduccion, frio. */
	if (jit_variantes_fpu && b->fpu >= 0)
	{
		unsigned vh = jit_hash_de(b->pc);
		int vi;

		for (vi = 0; vi < jit_sondeo; vi++)
		{
			int vb = jit_hash[(vh + (unsigned) vi) & (JIT_HASH_N - 1)];

			if (vb < 0)
				break;

			if (jit_bloques[vb].pc == b->pc && &jit_bloques[vb] != b)
			{
				jit_fpu_variantes++;
				break;
			}
		}
	}

	/* El prefijo contiguo lo verifica el puntero de busqueda, como siempre;
	   lo seguido por flujo va como palabras sueltas, que jit_verificar ya
	   compara una a una (el mecanismo de los bloques a mano). */
	{
		int k;

		for (k = 1; k < t.n; k++)
			if (t.pc[k] != t.pc[k - 1] + 2)
				break;

		b->n_palabras = k;

		if (k < t.n)
		{
			memcpy(b->pcs, t.pc, (size_t) t.n * sizeof(DWORD));
			b->extra_dir     = &b->pcs[k];
			b->extra_palabra = &b->copia[k];
			b->n_extra       = t.n - k;
		}
	}

	{
		int r;

		b->ranuras   = 0;
		b->canonicas = 0;

		for (r = 0; r < 16; r++)
		{
			b->mapa[r] = t.slot[r];

			if (t.slot[r] >= 0)
			{
				b->ranuras |= 1u << r;

				if (jit_hogares && jit_canonico[r] == t.slot[r])
					b->canonicas |= 1u << r;
			}
		}
	}

	memcpy(b->copia, t.palabra, (size_t) t.n * sizeof(WORD));
	b->palabras = b->copia;

	memcpy(b->enlace, g.enlace, sizeof(b->enlace));
	b->n_enlaces = g.n_enlaces;

	jit_registrar_marco(b, jit_x64_largo(&g.e));

	jit_codigo_us += jit_x64_largo(&g.e);

	/* La vigilancia, tramo por tramo: cada segmento contiguo de la traza
	   marca sus paginas. El puntero por segmento no tiene efectos (misma
	   pagina vigente bajo MMU; sin MMU no hay busqueda). */
	{
		int seg = 0;
		int k;

		for (k = 1; k <= t.n; k++)
			if (k == t.n || t.pc[k] != t.pc[k - 1] + 2)
			{
				jit_vigilar_tramo(MMU_FETCH_PUNTERO(t.pc[seg]),
					(unsigned) (2 * (k - seg)));
				seg = k;
			}
	}

	b->epoca      = 0;		/* todavia sin verificar */
	/* Y la de escritura tambien tiene que nacer invalida: el contador arranca
	   en 1 y esto en 0, asi que la primera entrada pasa por la comparacion de
	   palabras y deja el bloque con su puntero puesto. Nacer "al dia" saltaria
	   esa primera verificacion, que es la que fija b->ptr. */
	b->epoca_escr = 0;
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

	if (jit_n_bloques >= jit_max_bloques)
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
	b->fpu           = -1;
	b->veces         = 0;

	/* Los bloques a mano tampoco vigilaban sus paginas -- ni las de sus
	   palabras sueltas, que viven en otra parte. El mismo agujero, mas viejo. */
	jit_vigilar_tramo(MMU_FETCH_PUNTERO(pc), (unsigned) (2 * n_palabras));

	{
		int i;

		for (i = 0; i < n_extra; i++)
			jit_vigilar_tramo(MMU_FETCH_PUNTERO(extra_dir[i]), 2);
	}

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
	/*
		**Dos comparaciones, y la segunda es la de escrituras, no la clave.**
		El puntero de busqueda ya identifica el mapeo entero (pagina, ASID y
		modo), asi que lo unico que falta preguntar es si alguien escribio
		sobre las palabras. Con la clave entera aqui, un cambio de modo o de
		mapeo mandaba a comparar palabra por palabra bloques intactos: el
		20-22 % de las entradas, y en SR2 el 96,7 % de ellas acertaba.

		`b->epoca` se repone igual, porque es lo que compara el salto
		encadenado -- ese si se saltea el despachador y necesita la clave.

		**Lo que hace valido saltearse la comparacion de palabras es la ventana
		de 1 KB de tr_descubrir(), y esto la vuelve portante para la CORRECCION
		y no solo para el largo**: bajo MMU la traza entera --lo contiguo y lo
		seguido por flujo, que vive en `extra_dir`-- cae en la ventana de la
		entrada, asi que un solo puntero de busqueda valida el mapeo de todas
		sus palabras. Si alguna vez se relaja esa ventana para que el flujo
		cruce paginas, este camino aceptaria bloques cuya segunda pagina se
		remapeo, y habria que marcar los que cruzan para que bajen al camino
		largo. Sin MMU no hay remapeo y la pregunta no existe.
	*/
	if (codigo == b->ptr && b->epoca_escr == jit_epoca_escr)
	{
		b->epoca = jit_validez;

		return 1;
	}

	/*
		El camino largo, contado: cuantas veces la comparacion de dos no
		alcanza y cuantas PALABRAS cuesta cuando no alcanza. Es lo que
		dimensiona la epoca por pagina antes de escribirla -- DCDoom rechaza
		el 7,8 % de sus entradas, pero "rechazos" no dice cuanto trabajo se
		hizo antes de rechazar, y el resto de las veces el bucle corre entero
		y ACIERTA, que no se contaba en ningun lado.
	*/
	jit_verif_lento++;

	/*
		A mano y no con memcmp: esto corre **una vez por entrada al bloque** --
		434 millones de veces en el banco de DCDoom y 3393 en el de Crazy Taxi --
		y son 16 bytes de media. La llamada al memcmp de la biblioteca, con su
		despacho por tamano, cuesta mas que la comparacion.
	*/
	for (i = 0; i < n; i++)
	{
		jit_verif_palabras++;

		if (codigo[i] != b->palabras[i])
			return 0;
	}

	for (i = 0; i < b->n_extra; i++)
		if (*(const WORD *) MMU_FETCH_PUNTERO(b->extra_dir[i])
			!= b->extra_palabra[i])
			return 0;

	b->epoca      = jit_validez;
	b->epoca_escr = jit_epoca_escr;
	b->ptr        = codigo;

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

			/* El cronometro de la traduccion, para la sonda de tirones
			   (perf.h). No va muestreado: la pregunta es de un cuadro
			   concreto --el que tardo 70 ms-- y un muestreo cada 1021 no
			   dice nada de UN cuadro. Se paga una lectura de reloj por
			   traduccion, que son decenas por segundo, no millones. */
			{
				unsigned long long t0 = perf_ahora();

				b = tr_traducir(pc);
				jit_ns_traducir += perf_ahora() - t0;
			}

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
			jit_rechazos_modo++;
			jit_rechazo_censar(b->pc, 0);
			break;
		}

		/* Y el modo FPU (PR/SZ/algun-Enable) de la traduccion. Los bloques
		   FPU no reciben enlaces, asi que este chequeo los cubre en toda
		   entrada; cada sitio fmov corre siempre bajo el mismo modo --el
		   flip encierra la secuencia-- asi que en regimen siempre pasa. */
		if (b->fpu >= 0 && (unsigned) b->fpu != jit_fpu_visto)
		{
			jit_rechazos++;
			jit_rechazos_fpu++;
			jit_rechazo_censar(b->pc, 1);
			break;
		}

		if (!jit_verificar(b))
		{
			jit_rechazos++;
			jit_rechazos_palabras++;
			jit_rechazo_censar(b->pc, 2);

			/* La memoria ya no es la traducida: lapida y a traducir el
			   contenido vigente por el camino normal del lazo (el continue
			   cae en buscar -> NULL -> tr_traducir). La herencia viaja por
			   jit_retrad_pc porque la traduccion puede no ocurrir en esta
			   visita (corridos > 0) ni empezar en este pc (crecimiento
			   hacia atras); solo la consume el bloque que nazca aqui. */
			if (jit_retraducir && b->retraducido < JIT_MAX_RETRAD)
			{
				jit_retrad_pc = pc;
				jit_retrad_n  = (unsigned char) (b->retraducido + 1);
				b->pc = 1;
				jit_retraducciones++;
				continue;
			}

			jit_retrad_topes++;
			break;
		}

		jit_entradas++;
		b->veces++;
		jit_ult_sitio = -1;
		jit_estado.entrada = (void *) b->codigo;
		/* Entre dos entradas corrio el bloque periodico (o el interprete):
		   la vuelta que cruce una entrada no es pura (elision de ociosos). */
		jit_ocioso_gen[JIT_IMP_DESPACHO]++;
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

/*
	El resumen del traductor. **Lo llama main.c en la secuencia de salida**, no
	atexit(): registrado ahi corria despues de que SDL cerrara la redireccion de
	stderr, asi que la linea no aparecia en ningun archivo salvo con --perf --que
	imprime por otro camino-- y las compuertas, que corren SIN --perf, leian un
	resumen vacio. Un contador de control que solo existe cuando nadie lo mira es
	la misma trampa que el gancho de epoca sin llamador.
*/
void jit_resumen(void)
{
	int i, j;

	if (!jit_entradas && !jit_rechazos)
		return;

	fprintf(stderr, "jit: %llu instrucciones en %llu entradas"
		" (%.1f por entrada), %llu rechazos por verificacion\n",
		jit_estado.instr, jit_entradas,
		jit_entradas ? (double) jit_estado.instr / (double) jit_entradas : 0.0,
		jit_rechazos);

	/* El desglose por causa y los reincidentes: tres causas con correccion
	   distinta viajaban en un solo numero. */
	if (jit_rechazos || jit_fpu_variantes)
	{
		fprintf(stderr, "jit: de esos rechazos: %llu modo MMU, %llu modo FPU,"
			" %llu palabras; %llu variantes FPU traducidas;"
			" %llu retraducciones por palabras, %llu rechazos con el tope\n",
			jit_rechazos_modo, jit_rechazos_fpu, jit_rechazos_palabras,
			jit_fpu_variantes, jit_retraducciones, jit_retrad_topes);

		for (i = 0; i < 8; i++)
		{
			int mayor = -1;
			unsigned long long total_mayor = 0;

			for (j = 0; j < 64; j++)
			{
				jit_rechazo_sitio * s = &jit_rechazo_sitios[j];
				unsigned long long t = s->veces[0] + s->veces[1] + s->veces[2];

				if (s->pc != 0 && t > total_mayor)
				{
					total_mayor = t;
					mayor = j;
				}
			}

			if (mayor < 0 || total_mayor == 0)
				break;

			{
				jit_rechazo_sitio * s = &jit_rechazo_sitios[mayor];

				fprintf(stderr, "jit:   rechazado %8llu veces %08lx"
					" (modo %llu, fpu %llu, palabras %llu)\n",
					total_mayor, (unsigned long) s->pc,
					s->veces[0], s->veces[1], s->veces[2]);
				s->pc = 0;		/* fuera de la proxima vuelta */
			}
		}
	}

	if (jit_bloques_corridos)
		fprintf(stderr, "jit: %llu bloques corridos, %llu cruces de enlace"
			" (%.1f %% de las fronteras)\n",
			jit_bloques_corridos, jit_bloques_corridos - jit_entradas,
			100.0 * (double) (jit_bloques_corridos - jit_entradas)
				  / (double) jit_bloques_corridos);

	/* El censo de bytes emitidos, con DCEMU_JIT_SONDA_BYTES=1. */
	if (jit_sonda_bytes)
	{
		enum { NPL = (int) (sizeof(jit_bytes_pl) / sizeof(jit_bytes_pl[0])) };
		unsigned long long total = jit_bytes_prologo + jit_bytes_sync
			+ jit_bytes_resto + jit_bytes_salidas;
		unsigned long long instrs = 0;
		int k;

		for (k = 0; k < NPL; k++)
		{
			total  += jit_bytes_pl[k];
			instrs += jit_instr_pl[k];
		}

		if (total)
		{
			fprintf(stderr, "jit: bytes emitidos por rubro (total %llu, %.1f"
				" por instruccion emitida):\n"
				"jit:   prologo+verificacion %10llu  %5.1f %%\n"
				"jit:   sync por acceso      %10llu  %5.1f %%\n"
				"jit:   cortes y contadores  %10llu  %5.1f %%\n"
				"jit:   salidas y epilogo    %10llu  %5.1f %%\n",
				total, instrs ? (double) total / (double) instrs : 0.0,
				jit_bytes_prologo, 100.0 * (double) jit_bytes_prologo / (double) total,
				jit_bytes_sync,    100.0 * (double) jit_bytes_sync    / (double) total,
				jit_bytes_resto,   100.0 * (double) jit_bytes_resto   / (double) total,
				jit_bytes_salidas, 100.0 * (double) jit_bytes_salidas / (double) total);

			fprintf(stderr, "jit: por plantilla (bytes, %% del total,"
				" emisiones, bytes por emision):\n");

			for (k = 0; k < 16; k++)
			{
				int mayor = -1;
				unsigned long long m = 0;

				for (i = 0; i < NPL; i++)
					if (jit_bytes_pl[i] > m)
					{
						m = jit_bytes_pl[i];
						mayor = i;
					}

				if (mayor < 0 || m == 0)
					break;

				fprintf(stderr, "jit:   %-14s %10llu  %5.1f %%  %9llu  %6.1f\n",
					jit_plantillas[mayor].nombre, m,
					100.0 * (double) m / (double) total,
					jit_instr_pl[mayor],
					jit_instr_pl[mayor]
						? (double) m / (double) jit_instr_pl[mayor] : 0.0);
				jit_bytes_pl[mayor] = 0;
			}
		}
	}

	/* El censo de uso de ranuras, ponderado por veces: con que registros del
	   guest conviene quedarse si los hogares pasan a ser canonicos. Camina la
	   tabla al salir; cero costo en caliente. */
	if (jit_sonda_cruces && jit_n_bloques > 0)
	{
		unsigned long long	peso[16];
		unsigned long long	total = 0;
		int					r, k;

		for (r = 0; r < 16; r++)
			peso[r] = 0;

		for (k = 0; k < jit_n_bloques; k++)
		{
			total += jit_bloques[k].veces;

			for (r = 0; r < 16; r++)
				if (jit_bloques[k].ranuras & (1u << r))
					peso[r] += jit_bloques[k].veces;
		}

		fprintf(stderr, "jit: presencia de cada registro en las ranuras,"
			" ponderada por veces (%% de %llu):\n", total);

		for (r = 0; r < 16; r++)
			if (peso[r])
				fprintf(stderr, "jit:   r%-2d  %5.1f %%\n", r,
					total ? 100.0 * (double) peso[r] / (double) total : 0.0);
	}

	/* El censo de accesos: cuantos toma el camino rapido emitido y por que
	   guarda se va el resto al ayudante. Es el techo de la fase 6 medido en
	   vez de estimado -- lo que fastmem podria borrar es lo rapido; lo que
	   caeria en falta de pagina es la zona no plana. */
	if (jit_sonda_accesos && jit_acc_total)
	{
		unsigned long long lento = jit_acc_lento + jit_acc_lento_fis;
		int                r;

		fprintf(stderr, "jit: %llu accesos emitidos, %llu por el camino rapido"
			" (%.1f %%), %llu al ayudante; guardas falladas:\n",
			jit_acc_total, jit_acc_total - lento,
			100.0 * (double) (jit_acc_total - lento) / (double) jit_acc_total,
			lento);

		for (r = 0; r < JIT_RZ_N; r++)
			fprintf(stderr, "jit:   %-20s %12llu   %5.2f %%\n",
				jit_rz_nombre[r], jit_acc_razon[r],
				100.0 * (double) jit_acc_razon[r] / (double) jit_acc_total);

		if (jit_acc_p1p2)
			fprintf(stderr, "jit:   de esas, %llu (%.2f %%) las rescato el"
				" atajo de P1/P2\n", jit_acc_p1p2,
				100.0 * (double) jit_acc_p1p2 / (double) jit_acc_total);
	}

	if (!jit_traductor)
		return;

	fprintf(stderr, "jit: %llu costuras (%llu vacias: salto directo al"
		" cuerpo)\n", jit_costuras + jit_costuras_vacias, jit_costuras_vacias);

	if (jit_flujo_seguidos + jit_flujo_bsr + jit_flujo_rts != 0)
	{
		int con_flujo = 0;
		int j;

		for (j = 0; j < jit_n_bloques; j++)
			if (jit_bloques[j].mmu != -1 && jit_bloques[j].n_extra != 0)
				con_flujo++;

		fprintf(stderr, "jit: %llu aristas seguidas al traducir"
			" (%llu BRA, %llu BSR, %llu RTS), %d trazas con flujo\n",
			jit_flujo_seguidos + jit_flujo_bsr + jit_flujo_rts,
			jit_flujo_seguidos, jit_flujo_bsr, jit_flujo_rts, con_flujo);
	}

	if (jit_pares_rts != 0)
		fprintf(stderr, "jit: %llu pares de rama emitidos (rama con ranura"
			" de memoria)\n", jit_pares_rts);

	/* El censo de la frontera: en que termina cada bloque, ponderado por las
	   veces que se corrio. Es lo que separa "hay muchos sitios" de "por ahi
	   pasa la ejecucion": el costo de frontera vive donde pesan las veces. */
	{
		unsigned long long	veces_fin[JIT_FIN_N];
		int					bloques_fin[JIT_FIN_N];
		unsigned long long	total = 0;
		int					j;

		memset(veces_fin, 0, sizeof(veces_fin));
		memset(bloques_fin, 0, sizeof(bloques_fin));

		for (j = 0; j < jit_n_bloques; j++)
			if (jit_bloques[j].mmu != -1)
			{
				veces_fin[jit_bloques[j].fin] += jit_bloques[j].veces;
				bloques_fin[jit_bloques[j].fin]++;
				total += jit_bloques[j].veces;
			}

		if (total != 0)
		{
			fprintf(stderr, "jit: la frontera, por peso (fin del bloque,"
				" %% de las entradas, bloques):\n");

			for (j = 0; j < JIT_FIN_N; j++)
				if (veces_fin[j] != 0)
					fprintf(stderr, "jit:   %-28s %5.1f %%  %6d\n",
						jit_fin_nombre[j],
						100.0 * (double) veces_fin[j] / (double) total,
						bloques_fin[j]);
		}
	}

	fprintf(stderr, "jit: %llu bloques traducidos (%.1f instrucciones cada"
		" uno), %u bytes, %llu emisiones fallidas, %llu sin lugar en la tabla,"
		" %llu sin arena,"
		" %llu enlaces atados (%llu por puente), %llu indirectos aprendidos,"
		" %llu salidas con los enlaces agotados,"
		" %u movimientos de epoca (%u escritura de %llu sobre pagina con"
		" codigo, %u mapeo, %u modo),"
		" %u transiciones de PR/SZ/Enable\n",
		jit_traducidos,
		jit_traducidos ? (double) jit_instr_bloque / (double) jit_traducidos
					   : 0.0,
		jit_codigo_us, jit_fallidos, jit_colisiones, jit_arena_lleno,
		jit_enlaces_atados,
		jit_puentes_atados, jit_enlaces_dinamicos, jit_enlaces_agotados,
		jit_epoca - 1, jit_ep_escritura, jit_ep_pag_vista, jit_ep_mapeo,
		jit_ep_modo, jit_ep_fpu);

	if (jit_tope_bloques)
		fprintf(stderr, "jit: %llu propuestas rechazadas con la tabla de"
			" bloques llena (%d)\n", jit_tope_bloques, jit_max_bloques);

	/* El contador de control de la sincronizacion en el talon. Va sin condicion
	   -- un cero callado no se distingue de una sonda muerta -- y con la forma
	   vigente al lado, porque las dos cosas juntas son lo que dice si el A/B
	   comparo lo que dice comparar. */
	fprintf(stderr, "jit: etiqueta de traduccion %s, %llu incoherencias;"
		" URC %s, %llu pendientes al salir; entrada de %d bytes, indice por %s\n",
		jit_etiqueta_viva ? "viva (una carga)" : "construida en cada acceso",
		mmu_etiqueta_incoherente,
		jit_urc_diferido ? "diferido" : "en cada acceso",
		mmu_urc_pend,
		(int) sizeof(mmu_datos_t),
		jit_emision_vieja ? "imul, mascara negada al vuelo"
						  : "corrimiento, mascara ya negada");

	fprintf(stderr, "jit: sincronizacion %s, %llu filas con acceso sin sitio"
		" de llamada; corte %s, %llu incoherencias del limite; DIV1 %s;"
		" direccion constante %s\n",
		jit_sync_previa ? "delante de la plantilla"
						 : (jit_sync_stub ? "en el talon, por llamada"
										  : "en el talon, entera"),
		jit_sync_sin_consumir,
		jit_corte_viejo ? "en dos comparaciones" : "en una comparacion",
		intc_corte_incoherente,
		jit_div1_emitida ? "emitida sin ramas" : "por manejador",
		jit_dir_constante ? "plegada" : "por tabla");

	/*
		El censo de la segunda vuelta: por que NO se elide. Dos mitades que
		hacen falta juntas -- los enlaces que nunca recibieron sonda (por
		motivo) y, de los que si, que movio la vuelta que no fue pura. Un
		'cero elisiones' sin esto no distingue 'no habia lazos' de 'los lazos
		estaban del otro lado de una condicion' ni de 'el lazo hace trabajo'.
	*/
	if (jit_ociosos >= 2)
	{
		unsigned long long	clase[JIT_IMP_N];
		unsigned long long	regs = 0, sinlugar = 0, rechazos = 0;
		int					k;

		for (k = 0; k < JIT_IMP_N; k++)
			clase[k] = 0;

		for (k = 0; k < jit_n_aristas; k++)
		{
			int m;

			for (m = 0; m < JIT_IMP_N; m++)
				clase[m] += jit_aristas[k].fallo_clase[m];

			regs     += jit_aristas[k].fallo_regs;
			sinlugar += jit_aristas[k].fallo_sin_lugar;
		}

		for (k = 0; k < JIT_RECH_N; k++)
			rechazos += jit_ocioso_rech[k];

		fprintf(stderr, "jit: ociosos, enlaces sin sonda: %llu de %llu"
			" parcheados", rechazos, jit_ocioso_enlaces);

		for (k = 0; k < JIT_RECH_N; k++)
			if (jit_ocioso_rech[k] != 0)
				fprintf(stderr, ", %llu %s", jit_ocioso_rech[k],
					jit_rech_nombre[k]);

		fprintf(stderr, "\n");

		/* Las clases no son excluyentes: una vuelta puede mover dos, asi que
		   la suma puede pasar la cuenta de sondas fallidas. */
		fprintf(stderr, "jit: ociosos, vueltas impuras por clase (no"
			" excluyentes):");

		for (k = 0; k < JIT_IMP_N; k++)
			fprintf(stderr, " %s %llu", jit_imp_nombre[k], clase[k]);

		fprintf(stderr, "; %llu con la generacion quieta y registros"
			" distintos, %llu sin lugar antes del corte\n", regs, sinlugar);
	}

	/* El desglose de los sin-lugar (ver jit_colision_sitios). La linea dice
	   ademas cuantas ranuras de bloque quedaron usadas contra el tope, porque
	   la fuga se cobra ahi. */
	if (jit_colisiones)
	{
		fprintf(stderr, "jit: sin lugar: %llu inserciones sobre %u PCs"
			" distintos; ranuras de bloque %d de %d; los reincidentes:",
			jit_colisiones, jit_colision_pcs, jit_n_bloques, jit_max_bloques);

		for (i = 0; i < 8; i++)
		{
			int mejor = -1;

			for (j = 0; j < 64; j++)
				if (jit_colision_sitios[j].veces != 0
					&& (mejor < 0
						|| jit_colision_sitios[j].veces
							> jit_colision_sitios[mejor].veces))
					mejor = j;

			if (mejor < 0)
				break;

			fprintf(stderr, " %08lx x %llu",
				(unsigned long) jit_colision_sitios[mejor].pc,
				jit_colision_sitios[mejor].veces);

			jit_colision_sitios[mejor].veces = 0;
		}

		fprintf(stderr, "\n");
	}

	/* Donde se fue el tiempo de traducir. **El enlace es cuadratico**: cada
	   bloque nuevo barre TODOS los existentes buscando quien lo esperaba, asi
	   que traducir se encarece a medida que la corrida avanza. La sonda de
	   tirones lo destapo -- los cuadros lentos de Crazy Taxi eran traduccion,
	   y la traduccion era esto. */
	/* La elision de ociosos. La linea de control dice en que posicion corrio
	   la palanca, y 'aristas con sondas en cero' separa 'no habia lazos' de
	   'el talon no corrio' -- la confusion del gancho de epoca sin llamador. */
	{
		unsigned long long sondas = 0, elisiones = 0, vueltas = 0;
		unsigned long long instr_el = 0, ciclos_el = 0, gen_total = 0;
		int k, mostrados;

		for (k = 0; k < JIT_IMP_N; k++)
			gen_total += jit_ocioso_gen[k];

		for (k = 0; k < jit_n_aristas; k++)
		{
			sondas    += jit_aristas[k].sondas;
			elisiones += jit_aristas[k].elisiones;
			vueltas   += jit_aristas[k].vueltas;
			instr_el  += jit_aristas[k].instr_elididas;
			ciclos_el += jit_aristas[k].ciclos_elididos;
		}

		fprintf(stderr, "jit: elision de ociosos %s: generacion %llu, %d aristas"
			" con sonda (%llu sin lugar), %llu sondas, %llu elisiones, %llu"
			" vueltas elididas (%llu instrucciones, %.1f %% del total;"
			" %llu ciclos), %llu retiradas\n",
			jit_ociosos >= 2 ? "entera"
							 : (jit_ociosos == 1 ? "solo bumps" : "apagada"),
			gen_total, jit_n_aristas, jit_aristas_sin_lugar, sondas,
			elisiones, vueltas, instr_el,
			jit_estado.instr
				? 100.0 * (double) instr_el / (double) jit_estado.instr : 0.0,
			ciclos_el, jit_ocioso_retiradas);

		/* Las aristas que mas elidieron, de mayor a menor. */
		for (mostrados = 0; mostrados < 6; mostrados++)
		{
			int mejor = -1;

			for (k = 0; k < jit_n_aristas; k++)
				if (jit_aristas[k].instr_elididas != 0
					&& (mejor < 0 || jit_aristas[k].instr_elididas
									> jit_aristas[mejor].instr_elididas))
					mejor = k;

			if (mejor < 0)
				break;

			fprintf(stderr, "jit:   %08lx -> %08lx: %llu vueltas elididas en"
				" %llu elisiones (%llu instrucciones, %.1f por vuelta),"
				" %llu sondas%s\n",
				(unsigned long) jit_aristas[mejor].pc_fuente,
				(unsigned long) jit_aristas[mejor].pc_destino,
				jit_aristas[mejor].vueltas, jit_aristas[mejor].elisiones,
				jit_aristas[mejor].instr_elididas,
				jit_aristas[mejor].vueltas
					? (double) jit_aristas[mejor].instr_elididas
					  / (double) jit_aristas[mejor].vueltas : 0.0,
				jit_aristas[mejor].sondas,
				jit_aristas[mejor].retirada ? " (retirada)" : "");

			/* Para no repetirla: se anula en una copia local del criterio. */
			jit_aristas[mejor].instr_elididas = 0;
		}
	}

	fprintf(stderr, "jit: verificacion por entrada: %llu veces por el camino"
		" largo (%.1f %% de las entradas), %llu palabras comparadas"
		" (%.1f por vez)\n",
		jit_verif_lento,
		jit_entradas ? 100.0 * (double) jit_verif_lento / (double) jit_entradas
					 : 0.0,
		jit_verif_palabras,
		jit_verif_lento
			? (double) jit_verif_palabras / (double) jit_verif_lento : 0.0);

	fprintf(stderr, "jit: traducir %.0f ms, de los cuales enlazar %.0f ms"
		" (%.0f %%); %.3f ms por traduccion\n",
		(double) jit_ns_traducir / 1e6, (double) jit_ns_enlazar / 1e6,
		jit_ns_traducir
			? 100.0 * (double) jit_ns_enlazar / (double) jit_ns_traducir : 0.0,
		jit_traducidos
			? (double) jit_ns_traducir / 1e6 / (double) jit_traducidos : 0.0);

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

	/*
		El mismo censo PONDERADO POR VECES: la lista de arriba cuenta sitios de
		traduccion y un solo sitio caliente vale millones de entradas. Este
		agrega `veces` de cada bloque sobre su palabra de corte, que es la que
		dice cual plantilla o regla de ranura pagaria de verdad. El acumulador
		`otros` existe para que la tabla llena no trunque en silencio.
	*/
	{
		/* Indexado por la palabra entera: sin tope y sin truncar. Estaticos
		   porque son 832 KB que solo se tocan aca, al salir. */
		static unsigned long long	corte_veces[0x10000];
		static int					corte_bloques[0x10000];
		unsigned long long			total = 0;
		int							k;

		for (j = 0; j < jit_n_bloques; j++)
		{
			const jit_bloque * b = &jit_bloques[j];

			if (b->mmu == -1 || b->corte == 0 || b->veces == 0)
				continue;

			corte_veces[b->corte] += b->veces;
			corte_bloques[b->corte]++;
			total += b->veces;
		}

		if (total != 0)
		{
			fprintf(stderr, "jit: lo que mas corto, ponderado por veces"
				" (palabra, %% de las entradas cortadas, bloques,"
				" mnemonico):\n");

			for (i = 0; i < 16; i++)
			{
				int mejor = -1;

				for (k = 0; k < 0x10000; k++)
					if (corte_veces[k] != 0
						&& (mejor < 0 || corte_veces[k] > corte_veces[mejor]))
						mejor = k;

				if (mejor < 0)
					break;

				fprintf(stderr, "jit:   %04X  %5.1f %%  %6d  %s\n",
					mejor,
					100.0 * (double) corte_veces[mejor] / (double) total,
					corte_bloques[mejor],
					opcodes_mnemonico((WORD) mejor));

				corte_veces[mejor] = 0;
			}
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

	/*
		La adopcion (fase F.2, 2026-08-20): SIN variable corre el traductor,
		que es el binario que se entrega, y DCEMU_JIT=0 es la palanca de
		aislamiento que deja al interprete solo. 1 y 2 conservan su sentido
		de siempre (bloques a mano / traductor), asi que toda receta vieja
		con DCEMU_JIT=2 sigue significando lo mismo -- y todo guion cuyo
		brazo de control BORRABA la variable tiene que poner el 0 explicito,
		que es el cambio que esta regla le cobra a los guiones del arbol.
	*/
	if (v != NULL && atoi(v) == 0)
		return;

	jit_traductor = (v == NULL || atoi(v) >= 2);

	{
		const char * si = getenv("DCEMU_JIT_SIN_INDIRECTOS");

		jit_sin_indirectos = (si != NULL && atoi(si) != 0);
	}

	{
		const char * sp = getenv("DCEMU_JIT_SIN_PUENTES");

		jit_sin_puentes = (sp != NULL && atoi(sp) != 0);
	}

	{
		const char * sv = getenv("DCEMU_JIT_SIN_VARIANTES_FPU");
		const char * sr = getenv("DCEMU_JIT_SIN_RETRADUCIR");
		const char * sb = getenv("DCEMU_JIT_SONDA_BYTES");
		const char * tl = getenv("DCEMU_JIT_TRAD_EN_LINEA");
		const char * sp = getenv("DCEMU_JIT_SYNC_PREVIA");
		const char * cv = getenv("DCEMU_JIT_CORTE_VIEJO");
		const char * sd = getenv("DCEMU_JIT_DIV1_EMITIDA");
		const char * st = getenv("DCEMU_JIT_SYNC_EN_CADA_TALON");
		const char * dc = getenv("DCEMU_JIT_SIN_DIR_CONSTANTE");

		if (st != NULL && atoi(st) != 0)
			jit_sync_stub = 0;

		if (dc != NULL && atoi(dc) != 0)
			jit_dir_constante = 0;

		{
			const char * oc = getenv("DCEMU_JIT_OCIOSOS");

			if (oc != NULL)
			{
				int n = atoi(oc);

				jit_ociosos = (n < 0) ? 0 : (n > 2 ? 2 : n);
			}
		}

		if (sd != NULL && atoi(sd) != 0)
			jit_div1_emitida = 1;

		if (sp != NULL && atoi(sp) != 0)
			jit_sync_previa = 1;

		if (cv != NULL && atoi(cv) != 0)
			jit_corte_viejo = 1;

		if (sv != NULL && atoi(sv) != 0)
			jit_variantes_fpu = 0;
		if (sr != NULL && atoi(sr) != 0)
			jit_retraducir = 0;

		{
			const char * tv = getenv("DCEMU_JIT_TABLA_VIEJA");
			const char * tb = getenv("DCEMU_JIT_BLOQUES");

			if (tv != NULL && atoi(tv) != 0)
			{
				jit_tabla_vieja = 1;
				jit_sondeo      = 8;
			}

			if (tb != NULL)
			{
				int n = atoi(tb);

				if (n >= 1 && n <= JIT_MAX_BLOQUES)
				{
					jit_max_bloques = n;
					fprintf(stderr, "jit: tope de bloques en %d\n", n);
				}
			}
		}
		if (tl != NULL && atoi(tl) != 0)
			jit_trad_en_linea = 1;

		jit_sonda_bytes = (sb != NULL && atoi(sb) != 0);
	}

	{
		const char * n = getenv("DCEMU_JIT_PLANTILLAS");

		if (n != NULL && atoi(n) > 0 && atoi(n) < JIT_N_PLANTILLAS)
			jit_n_activas = atoi(n);
	}

	{
		const char * sf = getenv("DCEMU_JIT_SIN_FPU_MMU");
		const char * ce = getenv("DCEMU_JIT_CORTE_EPOCA");
		const char * ba = getenv("DCEMU_JIT_BUSCADOR");

		if (sf != NULL && atoi(sf) != 0)
			jit_fpu_mmu = 0;

		jit_corte_epoca     = (ce != NULL && atoi(ce) != 0);
		jit_buscador_activo = (ba != NULL && atoi(ba) != 0);

		{
			const char * sc = getenv("DCEMU_JIT_SONDA_CRUCES");
			const char * sh = getenv("DCEMU_JIT_SIN_HOGARES");
			const char * co = getenv("DCEMU_JIT_COSTURAS");
			const char * fl = getenv("DCEMU_JIT_FLUJO");
			const char * pr = getenv("DCEMU_JIT_SIN_PARES");
			const char * pl = getenv("DCEMU_JIT_SIN_PARES_LLAMADA");
			const char * sa = getenv("DCEMU_JIT_SONDA_ACCESOS");
			const char * ap = getenv("DCEMU_JIT_SIN_ATAJO_P1P2");
			const char * gv = getenv("DCEMU_JIT_GUARDAS_VIEJAS");

			if (ap != NULL && atoi(ap) != 0)
				jit_atajo_p1p2 = 0;

			{
				const char * ev = getenv("DCEMU_MMU_ETIQUETA_CALCULADA");
				const char * ui = getenv("DCEMU_MMU_URC_INMEDIATO");
				const char * em = getenv("DCEMU_MMU_EMISION_VIEJA");

				if (ev != NULL && atoi(ev) != 0)
					jit_etiqueta_viva = 0;

				if (ui != NULL && atoi(ui) != 0)
					jit_urc_diferido = 0;

				if (em != NULL && atoi(em) != 0)
					jit_emision_vieja = 1;
			}

			if (gv != NULL && atoi(gv) != 0)
				jit_guardas_viejas = 1;

			{
				const char * rj = getenv("DCEMU_JIT_SIN_REJILLA");
				const char * el = getenv("DCEMU_JIT_ENLACE_LINEAL");

				if (rj != NULL && atoi(rj) != 0)
					jit_rejilla_fina = 0;

				if (el != NULL && atoi(el) != 0)
					jit_enlace_lineal = 1;

				{
					const char * vc = getenv("DCEMU_JIT_VERIF_COMPLETA");

					if (vc != NULL && atoi(vc) != 0)
						jit_verif_completa = 1;
				}
			}

			jit_sonda_cruces  = (sc != NULL && atoi(sc) != 0);
			jit_sonda_accesos = (sa != NULL && atoi(sa) != 0);

			if (sh != NULL && atoi(sh) != 0)
				jit_hogares = 0;

			jit_costuras_talones = (co != NULL && atoi(co) >= 2);

			if (fl != NULL && atoi(fl) != 0)
				jit_flujo = 1;

			if (pr != NULL && atoi(pr) != 0)
				jit_par_rts = 0;

			if (pl != NULL && atoi(pl) != 0)
				jit_par_llamadas = 0;

			{
				const char * st = getenv("DCEMU_JIT_SIN_TERMINALES");
				const char * sr = getenv("DCEMU_JIT_SIN_RANURA_FPU");

				if (st != NULL && atoi(st) != 0)
					jit_terminales = 0;

				if (sr != NULL && atoi(sr) != 0)
					jit_ranura_fpu = 0;
			}
		}
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
	jit_estado.h_leer_par   = (void *) jit_leer_par;
	jit_estado.h_escribir_par = (void *) jit_escribir_par;

	for (i = 0; i < JIT_HASH_N; i++)
		jit_hash[i] = -1;

	jit_plantillas_ligar();

	/* La palanca de DIV1 cambia la fila ENTERA, no solo la emision: por
	   manejador la fila va con ciclos 0 y accede 1 --el manejador suma sus
	   ciclos por dentro y el conductor no debe volver a sumarlos-- y emitida,
	   con ciclos 1 y sin accede. */
	if (jit_div1_emitida)
	{
		int idiv = jit_pl_indice((opcode_f *) div1s52);

		if (idiv >= 0)
		{
			jit_plantillas[idiv].ciclos = 1;
			jit_plantillas[idiv].accede = 0;
			jit_plantillas[idiv].emitir = pl_div1s52;
		}
	}

	/*
		La clasificacion de arriba compara PUNTEROS de manejador, y el plegado
		de funciones identicas del enlazador (/OPT:ICF) puede darle una misma
		direccion a dos manejadores distintos -- el riesgo que la documentacion
		de ICF advierte por su nombre. Paso: lld-link plego nop con NOIMP y
		shal91 con shll94 (MSVC no pliega ninguno), y el traductor paso a
		clasificar palabras que no son instrucciones como NOP -- exacto de
		casualidad, porque solo se pliega codigo identico, pero las trazas
		cambiaron de forma y los conteos dejaron de ser invariantes entre
		compiladores. El enlace clang va con /OPT:NOICF desde entonces; esta
		guarda lo NOMBRA si vuelve, porque el sintoma (conteos distintos con
		captura exacta) no se parece en nada a la causa. 0x0000 no es
		instruccion (NOIMP); 0x4020 es SHAL R0 y 0x4000 SHLL R0, dos funciones
		distintas en el fuente.
	*/
	if (jit_plantilla_de(OP_HANDLER(oplist, 0x0000)) != NULL
		|| OP_HANDLER(oplist, 0x4020) == OP_HANDLER(oplist, 0x4000))
		fprintf(stderr, "jit: el enlazador plego manejadores (ICF): la"
			" clasificacion por puntero esta contaminada\n");

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

}

#endif /* DCEMU_JIT */
