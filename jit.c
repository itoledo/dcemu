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

/* El emisor produce x86-64 y el contexto se direcciona por desplazamiento, asi
   que estas dos suposiciones son parte del contrato y no del ambiente. */
typedef char jit_assert_64[(sizeof(void *) == 8) ? 1 : -1];
typedef char jit_assert_ul[(sizeof(unsigned long) == 4) ? 1 : -1];

int jit_activo = 0;
unsigned char jit_mapa[8192];

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
	void *					h_escribir8;
	/* PTEH y MMUCR viven adentro de regmem, que es un calloc de 16 MB: en
	   Windows una reserva de ese tamano no sale del monton chico y puede caer
	   a terabytes de la imagen, con lo cual no hay desplazamiento de 32 bits
	   desde el contexto que los alcance. Se guardan los punteros aca -- una
	   carga mas por acceso, sobre esta misma linea de cache -- en vez de
	   hornear la direccion. Lo descubrio el emisor negandose a emitir el
	   bloque, que es exactamente para lo que existe esa comprobacion. */
	void *					p_pteh;
	void *					p_mmucr;
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

void jit_escribir8(DWORD dir, DWORD valor)
{
	BYTE b = (BYTE) (valor & 0xFF);

	WriteMemoryB(dir, &b);
}

/* ------------------------------------------------------------------------ */
/* El arena                                                                 */
/* ------------------------------------------------------------------------ */

/* Fase 0: dos bloques. El arena de 16 MB del plan llega con el traductor. */
#define JIT_ARENA_TAM		(256u * 1024u)
#define JIT_MAX_BLOQUES		8

static unsigned char *	jit_arena     = NULL;
static unsigned char *	jit_codigo    = NULL;	/* donde empieza el codigo */
static unsigned			jit_codigo_us = 0;		/* cuanto se lleva emitido */
static unsigned			jit_codigo_tam = 0;

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
} jit_bloque;

static jit_bloque	jit_bloques[JIT_MAX_BLOQUES];
static int			jit_n_bloques = 0;

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
#define D_ESCR8		D(&jit_estado.h_escribir8)
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

static void gen_prologo(jit_gen * g)
{
	int i;

	for (i = 0; i < 8; i++)
	{
		jit_x64_push(&g->e, jit_empujados[i]);
		g->marco.tras_push[i] = (unsigned char) jit_x64_largo(&g->e);
	}

	jit_x64_sub64_ri(&g->e, X64_RSP, JIT_MARCO_RSP);
	g->marco.tras_sub = (unsigned char) jit_x64_largo(&g->e);
	g->marco.tam      = g->marco.tras_sub;

	/* Fuera del prologo que describe el desenrollado: no toca la pila. */
	jit_x64_mov64_ri(&g->e, CTX, (unsigned long long) (size_t) &core.context);
	jit_x64_mov_rm(&g->e, CYC, CTX, O_CYC);
	jit_x64_xor_rr(&g->e, N, N);

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
typedef struct
{
	x64_parche	lento[12];
	int			n_lento;
	int			corto;		/* los saltos al camino lento caben en rel8 */
	x64_reg		fis;		/* que registro lleva la direccion fisica */
	x64_parche	listo;
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
	a->n_lento = 0;
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
	a->lento[a->n_lento++] = gen_guarda(g, a, X64_E);

	jit_x64_mov_rr(&g->e, X64_R8, a->fis);
	jit_x64_and_ri(&g->e, X64_R8, 0x00FFFFFF);
}

/* Cierra el camino rapido y abre el lento. */
static void gen_rapido_fin(jit_gen * g, jit_acceso * a)
{
	int i;

	a->listo = jit_x64_jmp(&g->e);

	for (i = 0; i < a->n_lento; i++)
		jit_x64_fijar(&g->e, a->lento[i]);
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
		gen_rapido_fin(g, &a);
	}

	gen_llamar(g, (const void *) jit_leer32, D_LEER32);

	if (modo != JIT_ACC_LENTO)
		jit_x64_fijar(&g->e, a.listo);

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
		gen_rapido_fin(g, &a);
	}

	gen_llamar(g, (const void *) jit_leer8s, D_LEER8S);

	if (modo != JIT_ACC_LENTO)
		jit_x64_fijar(&g->e, a.listo);

	if (dst != X64_RAX)
		jit_x64_mov_rr(&g->e, dst, X64_RAX);
}

/* El valor viene en `valor`, que tiene que ser no volatil: en el camino lento
   se copia a EDX recien antes de la llamada. */
static void gen_escribir8(jit_gen * g, x64_reg valor, int modo)
{
	jit_acceso a;

	if (modo != JIT_ACC_LENTO)
	{
		gen_rapido_inicio(g, &a, D_BASE_ESC, 0, modo);
		jit_x64_mov8_mr_idx(&g->e, X64_RAX, X64_R8, 1, 0, valor);
		gen_rapido_fin(g, &a);
	}

	jit_x64_mov_rr(&g->e, X64_RDX, valor);
	gen_llamar(g, (const void *) jit_escribir8, D_ESCR8);

	if (modo != JIT_ACC_LENTO)
		jit_x64_fijar(&g->e, a.listo);
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
	gen_volcar_cuenta(g);

	jit_x64_add64_ri(&g->e, X64_RSP, JIT_MARCO_RSP);

	for (i = 7; i >= 0; i--)
		jit_x64_pop(&g->e, jit_empujados[i]);

	jit_x64_ret(&g->e);
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
	gen_escribir8(g, A(2), JIT_ACC_MMU);
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

/* ------------------------------------------------------------------------ */
/* Emision, registro y despacho                                             */
/* ------------------------------------------------------------------------ */

static jit_marco	jit_marco_comun;
static int			jit_marco_visto = 0;

static void jit_marcar(DWORD pc)
{
	unsigned i = ((pc >> 1) & 0xFFFFu);

	jit_mapa[i >> 3] |= (unsigned char) (1u << (i & 7u));
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

	/* Los dos bloques tienen que compartir el prologo, porque comparten la
	   informacion de desenrollado. */
	if (!jit_marco_visto)
	{
		jit_marco_comun = g.marco;
		jit_marco_visto = 1;
	}
	else if (memcmp(&jit_marco_comun, &g.marco, sizeof(jit_marco)) != 0)
		return 0;

	b = &jit_bloques[jit_n_bloques++];

	b->pc            = pc;
	b->codigo        = (void (*)(void)) (jit_codigo + jit_codigo_us);
	b->palabras      = palabras;
	b->n_palabras    = n_palabras;
	b->extra_dir     = extra_dir;
	b->extra_palabra = extra_palabra;
	b->n_extra       = n_extra;

#ifdef _WIN32
	{
		RUNTIME_FUNCTION * rf = &jit_tabla_rt[jit_n_bloques - 1];

		rf->BeginAddress      = (DWORD) (jit_codigo + jit_codigo_us - jit_arena);
		rf->EndAddress        = (DWORD) (jit_codigo + jit_codigo_us
									+ jit_x64_largo(&g.e) - jit_arena);
		rf->UnwindInfoAddress = (DWORD) (jit_unwind - jit_arena);
	}
#endif

	jit_codigo_us += jit_x64_largo(&g.e);

	jit_marcar(pc);

	return 1;
}

/* Las palabras del bloque, contra la copia que la traduccion guardo, por el
   puntero de pagina que la busqueda de main_loop() ya resolvio. Codigo
   automodificado, otro proceso en la misma VA, otra imagen: la comparacion
   falla, el bloque no corre y el guest sigue interpretado. */
static int jit_verificar(const jit_bloque * b)
{
	const WORD * codigo = (const WORD *) MMU_FETCH_PUNTERO(b->pc);
	int i;

	if (memcmp(codigo, b->palabras, (size_t) b->n_palabras * sizeof(WORD)) != 0)
		return 0;

	for (i = 0; i < b->n_extra; i++)
		if (*(const WORD *) MMU_FETCH_PUNTERO(b->extra_dir[i])
			!= b->extra_palabra[i])
			return 0;

	return 1;
}

int jit_despachar(DWORD pc)
{
	int i;

	for (i = 0; i < jit_n_bloques; i++)
	{
		jit_bloque * b = &jit_bloques[i];

		if (b->pc != pc)
			continue;

		if (!jit_verificar(b))
		{
			jit_rechazos++;
			return 0;
		}

		jit_entradas++;
		b->codigo();

		return 1;
	}

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Arranque                                                                 */
/* ------------------------------------------------------------------------ */

static void jit_resumen(void)
{
	if (jit_entradas || jit_rechazos)
		fprintf(stderr, "jit: %llu instrucciones en %llu entradas"
			" (%.1f por entrada), %llu rechazos por verificacion\n",
			jit_estado.instr, jit_entradas,
			jit_entradas ? (double) jit_estado.instr / (double) jit_entradas
						 : 0.0,
			jit_rechazos);
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

	if (v == NULL || atoi(v) == 0)
		return;

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
	jit_estado.h_escribir8  = (void *) jit_escribir8;
	jit_estado.p_pteh       = (void *) PTEH;
	jit_estado.p_mmucr      = (void *) MMUCR;

	if (!jit_emitir(JIT_CT_ENTRADA, gen_bloque_ct,
			jit_ct_palabras, 20,
			jit_ct_extra_dir, jit_ct_extra_palabra, 2)
	 || !jit_emitir(JIT_CE_ENTRADA, gen_bloque_ce,
			jit_ce_palabras, 17, NULL, NULL, 0))
	{
		fprintf(stderr, "jit: el emisor se quejo; sigue el interprete\n");
		jit_n_bloques = 0;
		memset(jit_mapa, 0, sizeof(jit_mapa));
		return;
	}

#ifdef _WIN32
	jit_unwind_armar(jit_unwind, &jit_marco_comun, jit_empujados, 8,
		JIT_MARCO_RSP);

	/*
		Sin esto el primer longjmp desde adentro de un bloque se lleva el
		proceso, y eso es el camino normal del bloque con MMU. Si falla, el
		JIT no arranca: correr sin la tabla seria correr sabiendo que la
		primera falta rompe.
	*/
	if (!RtlAddFunctionTable(jit_tabla_rt, (DWORD) jit_n_bloques,
			(DWORD64) (size_t) jit_arena))
	{
		fprintf(stderr, "jit: RtlAddFunctionTable fallo; sigue el"
			" interprete\n");
		jit_n_bloques = 0;
		memset(jit_mapa, 0, sizeof(jit_mapa));
		return;
	}
#endif

	jit_activo = 1;

	fprintf(stderr, "jit: %d bloques emitidos, %u bytes (DCEMU_JIT=1)\n",
		jit_n_bloques, jit_codigo_us);

	v = getenv("DCEMU_JIT_VOLCADO");

	if (v != NULL && *v != '\0')
		jit_volcar(v);

	atexit(jit_resumen);
}

#endif /* DCEMU_JIT */
