/****************************************************************************

	MMU - la ventana de control P4 del SH-4

	Fase 1 de docs/mmu-plan.md: los registros de la MMU y las ocho ventanas
	de 0xF0000000 a 0xF7FFFFFF por las que el software llega a los arreglos
	de la cache y de la TLB.

	Todavia NO hay traduccion. MMUCR.AT se registra pero no se obedece, y
	todas las direcciones se siguen resolviendo como hasta ahora. Lo que si
	cambia es que estos accesos dejan de caer en mem_write_error(): un
	binario de KOS barre las 512 entradas del arreglo de direcciones de la
	cache de operandos al arrancar, y hasta ahora cada una de esas
	escrituras se perdia en silencio y ademas inundaba --traza-mem.

	Vive aparte de mem.c a proposito, igual que sistema.c y gdrom.c: mem.c
	no se puede enlazar en las pruebas unitarias porque arrastra SDL y
	OpenGL por los callbacks del PVR.

*****************************************************************************/

#ifndef _MMU_H_
#define _MMU_H_

#include <stddef.h>
#include <setjmp.h>

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. Igual
   que main.h, para que esta cabecera se pueda incluir sola. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* ------------------------------------------------------------------------ */
/* Las ocho ventanas de control, por el byte alto de la direccion            */
/* ------------------------------------------------------------------------ */

#define MMU_P4_IC_DIR		0xF0		/* cache de instrucciones, direcciones */
#define MMU_P4_IC_DAT		0xF1		/* cache de instrucciones, datos */
#define MMU_P4_ITLB_DIR		0xF2		/* ITLB, direcciones */
#define MMU_P4_ITLB_DAT		0xF3		/* ITLB, datos 1 y 2 */
#define MMU_P4_OC_DIR		0xF4		/* cache de operandos, direcciones */
#define MMU_P4_OC_DAT		0xF5		/* cache de operandos, datos */
#define MMU_P4_UTLB_DIR		0xF6		/* UTLB, direcciones */
#define MMU_P4_UTLB_DAT		0xF7		/* UTLB, datos 1 y 2 */

/* Los arreglos de datos vienen en dos mitades; las separa el bit 23 de la
   direccion, no el byte alto. */
#define MMU_BIT_DATOS2		0x00800000

/* Bit asociativo de los arreglos de direcciones. En la TLB es el bit 7; en la
   cache, el bit 3. Escribir con el puesto busca por etiqueta en vez de indexar. */
#define MMU_BIT_A_TLB		0x00000080
#define MMU_BIT_A_CACHE		0x00000008

/* ------------------------------------------------------------------------ */
/* La TLB, todavia como dato crudo                                          */
/* ------------------------------------------------------------------------ */

#define MMU_ITLB_ENTRADAS	4
#define MMU_UTLB_ENTRADAS	64

/* Las entradas se guardan tal como las escribe el software, empaquetadas segun
   el manual. Desempaquetarlas en campos es la fase 2; hacerlo ahora seria
   codigo sin nadie que lo consuma. */
extern DWORD mmu_itlb_dir[MMU_ITLB_ENTRADAS];
extern DWORD mmu_itlb_dat1[MMU_ITLB_ENTRADAS];
extern DWORD mmu_itlb_dat2[MMU_ITLB_ENTRADAS];

extern DWORD mmu_utlb_dir[MMU_UTLB_ENTRADAS];
extern DWORD mmu_utlb_dat1[MMU_UTLB_ENTRADAS];
extern DWORD mmu_utlb_dat2[MMU_UTLB_ENTRADAS];

/* Bits de una entrada, tal como los ve el software por los arreglos
   memoria-mapeados y por PTEL. V y D son un solo bit del chip visible desde
   las dos mitades, y por eso aparecen dos veces con posiciones distintas. */
#define MMU_BIT_V		0x00000100		/* validez: direcciones y datos 1 */
#define MMU_BIT_SH		0x00000002		/* datos 1: pagina compartida */
#define MMU_BIT_D_DIR	0x00000200		/* sucia, arreglo de direcciones */
#define MMU_BIT_D_DAT	0x00000004		/* sucia, arreglo de datos 1 */

/* Indice de entrada dentro de cada arreglo: ITLB bits 9-8 (4 entradas), UTLB
   bits 13-8 (64). Expuestos porque son lo primero que se prueba. */
#define MMU_ITLB_INDICE(dir)	(((dir) >> 8) & 0x03)
#define MMU_UTLB_INDICE(dir)	(((dir) >> 8) & 0x3F)

/* ------------------------------------------------------------------------ */
/* Acceso                                                                   */
/* ------------------------------------------------------------------------ */

/* Deja la TLB en blanco. La llama regmem_setup(). */
void mmu_reset(void);

/* Atienden las zonas 0xF0 a 0xF7. Misma firma que mem_access_read_t y
   mem_access_write_t, para enchufarlas en mem_hash_read/mem_hash_write. */
void mmu_p4_read(unsigned long direccion, void * p, size_t size);
void mmu_p4_write(unsigned long direccion, void * p, size_t size);

/* Lo llama regmap_write() cuando alguien toca MMUCR: atiende TI y deja
   mmu_activa al dia. Devuelve 1 si AT quedo en 1. */
int mmu_mmucr_escrito(DWORD valor);

/* Bits de MMUCR que hacen falta para eso. */
#define MMUCR_AT			0x00000001		/* traduccion activa */
#define MMUCR_TI			0x00000004		/* invalidar TLB */
#define MMUCR_SV			0x00000100		/* espacio unico */
#define MMUCR_SQMD			0x00000200		/* store queues privilegiadas */
#define MMUCR_URC(v)		(((v) >> 10) & 0x3F)
#define MMUCR_URB(v)		(((v) >> 18) & 0x3F)

/* ------------------------------------------------------------------------ */
/* Traduccion                                                               */
/* ------------------------------------------------------------------------ */

/*
	Copia de MMUCR.AT. Se consulta en cada acceso a memoria, asi que vive
	aparte del registro para no desreferenciar un puntero por acceso. Con la
	MMU apagada -- o sea, en todo lo que corre hoy -- el camino rapido es una
	comparacion contra cero y nada mas.
*/
extern int mmu_activa;

#define MMU_LECTURA		0
#define MMU_ESCRITURA	1

/*
	Traduce una direccion virtual. Si falla NO devuelve: deja la excepcion
	preparada y sale por excepcion_abortar() (ver excepciones.h).

	La direccion que devuelve es fisica *vista por la ventana P2*, o sea
	fisica | 0xA0000000. No es capricho: la tabla de zonas de dcemu mezcla
	bindings fisicos (0x00 BIOS, 0x0C RAM, 0x10 TA) con bindings P2 (0xA0 PVR
	y bloque de control, 0xA4/0xA5 video, 0xAC RAM), y la ventana P2 es la
	unica con cobertura completa: por ejemplo el bloque 0x005Fxxxx solo lo
	atiende pvr_read/pvr_write en la zona 0xA0, no bios_read en la 0x00.
	Como dcemu no emula cache, que la traduccion salga por la ventana sin
	cachear no tiene efecto observable.
*/
DWORD mmu_traducir(DWORD direccion, int escritura);

/*
	El volcado de una store queue (el PREF sobre 0xE0000000-0xE3FFFFFF) con
	MMUCR.AT: traduce por la UTLB la VA completa como escritura -- QACR no
	participa -- y aplica MMUCR.SQMD (fase 6 de docs/mmu-plan.md). Devuelve la
	fisica CRUDA, sin ventana, porque el llamador despacha por zona fisica.
	Si falla no vuelve, como mmu_traducir(). Es lo que SetStoreQueueBase de
	Windows CE espera: el fallo lleva la VPN de la SQ, no la del destino.
*/
DWORD mmu_traducir_sq(DWORD direccion);

/*
	Traduccion para MIRAR: la fisica cruda, o 0 si no se resuelve. No falla,
	no entra a ninguna excepcion y no mueve URC. Es lo que usan los
	diagnosticos que leen memoria del guest desde un punto donde fallar seria
	peor que no ver -- las cadenas de depuracion de Windows CE.
*/
DWORD mmu_traducir_mirar(DWORD direccion);

/* Carga UTLB[URC] desde PTEH/PTEL/PTEA. La llama el opcode LDTLB. */
void mmu_ldtlb(DWORD pteh, DWORD ptel, DWORD ptea, int urc);

/* ------------------------------------------------------------------------ */
/* Busqueda de instrucciones (fase 7)                                       */
/* ------------------------------------------------------------------------ */

/*
	Cache de la pagina de instrucciones vigente. main_loop() y las ranuras de
	retardo buscan cada instruccion por MMU_FETCH_PUNTERO(): con la MMU
	apagada es el get_memory_pointer() de siempre; encendida, el acierto es
	una comparacion de pagina y otra de modo, y el fallo repuebla via
	mmu_fetch_resolver(), que traduce por la UTLB y puede no volver (fallo de
	TLB en la busqueda: EXPEVT 0x040 por el vector 0x400).

	SR.MD participa del acierto porque la proteccion depende de el: cambiar
	de modo no invalida nada, simplemente deja de acertar. Lo que si invalida:
	LDTLB, las escrituras a los arreglos de la UTLB por P4, MMUCR, y PTEH
	porque lleva el ASID (regmap_write). La macro se expande en los
	llamadores porque necesita get_memory_pointer (mem.h) y SR_MD (sh4emu.h),
	que esta cabecera no arrastra a proposito.
*/
extern DWORD           mmu_fetch_vpn;
extern DWORD           mmu_fetch_mascara;
extern DWORD           mmu_fetch_md;
extern unsigned char * mmu_fetch_base;

unsigned char * mmu_fetch_resolver(DWORD pc);

/*
	Las dos invalidaciones, que no son la misma cosa y confundirlas cuesta el
	8 % de un guest con MMU:

	 - mmu_fetch_invalidar() tira **solo la pagina unica** de busqueda, cuya
	   etiqueta no lleva el ASID. Es lo unico que una escritura a PTEH necesita.
	 - mmu_tlb_invalidar() tira ademas las tres cachas etiquetadas por ASID. Es
	   para cuando cambia el contenido de la TLB: LDTLB, los arreglos por P4 y
	   MMUCR.

	El porque completo esta en mmu.c, sobre mmu_tlb_invalidar().
*/
void mmu_fetch_invalidar(void);
void mmu_tlb_invalidar(void);

/* La sonda de techo del recorrido de la UTLB (DCEMU_SONDA_UTLB_CACHE=1); el
   porque esta en mmu.c, junto a la cache. */
void mmu_sondas_iniciar(void);

#define MMU_FETCH_PUNTERO(pc)											\
	(!mmu_activa														\
	 ? (unsigned char *) get_memory_pointer(pc)							\
	 : ((((pc) & ~mmu_fetch_mascara) == mmu_fetch_vpn					\
	     && (DWORD) SR_MD == mmu_fetch_md)								\
	    ? mmu_fetch_base + ((pc) & mmu_fetch_mascara)					\
	    : mmu_fetch_resolver(pc)))

/* ------------------------------------------------------------------------ */
/* El camino rapido de datos, en el macro (fase 3 de rendimiento-plan-2.md) */
/* ------------------------------------------------------------------------ */

/*
	El acierto de la cache de traducciones resueltas, expandido dentro de
	memread()/memwrite() en vez de pagar la entrada a mmu_traducir() en cada
	acceso. Mismo precedente que MMU_FETCH_PUNTERO: el macro vive aca pero se
	expande en llamadores que ya incluyen sh4emu.h (SR_MD, PTEH, MMUCR) y
	perf.h (PERF_CONTAR).

	Lo que compra sobre la llamada: el bit de permiso es una constante en el
	sitio de expansion, y el acierto queda dentro del manejador, sin cruzar la
	frontera de la llamada. Lo que NO cambia, y es lo que lo hace correcto:

	 - **el acierto cuenta como acceso a la UTLB y avanza URC**, igual que en
	   mmu_traducir() -- de URC depende que entrada reemplaza el LDTLB del
	   guest, o sea su camino de ejecucion;
	 - **los contadores de --perf se incrementan igual** que por la llamada,
	   para que la verificacion de trabajo entre corridas no cambie de
	   significado con el interruptor;
	 - el fallo -- y todo lo que no es un acierto limpio: P1/P2/P4, permisos
	   sin validar, generacion vencida -- cae en mmu_traducir(), que decide
	   todo igual que siempre.

	DCEMU_SIN_MMU_MACRO=1 lo apaga en el mismo binario (la sonda del A/B);
	DCEMU_SIN_CACHE_MMU=1 tambien lo apaga, porque sin la cache el sondeo no
	tendria que acertar nunca.
*/
extern int mmu_macro_probar;

/* La etiqueta de las caches de traduccion. Compartida con mmu.c. */
#define ASID_DE(e)			((e) & 0x000000FF)
#define MMU_CACHE_VALIDA	0x00010000ul	/* bit fuera del ASID y del modo */
/* La forma "vale en ambos modos" de la etiqueta de mmu_cache (sin el bit de
   modo): la lleva un llenado cuyo ASID caso por IGUALDAD con PTEH (o una
   pagina compartida) -- ahi el recorrido encuentra la misma entrada en los
   dos modos, asi que el modo en la etiqueta solo tiraba aciertos: el censo
   dio 98,3 % (DOOM) / 91,4 % (SR2) de los fallos por etiqueta como solo-modo
   con el mismo ASID. Un llenado que caso via espacio unico (sv y ASID ajeno)
   conserva el modo, que es el caso por el que el bit existe. */
#define MMU_CACHE_AMBOS		0x00020000ul

/*
	La etiqueta vigente de mmu_datos, calculada UNA VEZ y no en cada acceso.

	`ASID_DE(*PTEH) | ((SR_MD == 0) << 8) | MMU_CACHE_VALIDA` es funcion de dos
	cosas que casi nunca cambian --el ASID que el guest deja en PTEH y el modo
	privilegiado-- y se construia en cada acceso: en el codigo emitido son diez
	instrucciones con **dos cargas dependientes** (el puntero a PTEH y despues
	PTEH) alimentando justo la comparacion que decide el camino rapido. Sega
	Rally 2 hace 3400 millones de accesos emitidos en 60 s emulados.

	Se mueve en los tres unicos sitios que pueden moverla, como el limite del
	corte con la bandera de reintento (intc.h): las dos entradas de UpdateSR()
	--todo cambio de SR.MD pasa por ahi, incluida la entrada a una excepcion,
	que pone MD a mano y avisa-- y la escritura del guest a PTEH. El bloque
	periodico verifica la coherencia una vez por servicio y
	`mmu_etiqueta_incoherente` sale en el resumen: un sitio nuevo que cambiara
	el ASID o el modo sin avisar dejaria al emulador traduciendo con la
	etiqueta de antes, que es una divergencia silenciosa y tardia.
*/
extern DWORD				mmu_etiqueta;
extern unsigned long long	mmu_etiqueta_incoherente;

void mmu_etiqueta_recalcular(void);

/* La forma canonica, en un solo sitio: la usan quien la recalcula y quien
   verifica la coherencia, asi que no pueden discrepar. */
#define MMU_ETIQUETA_DE(pteh, md)										\
	(ASID_DE(pteh) | ((DWORD) ((md) == 0) << 8) | MMU_CACHE_VALIDA)

#define MMU_DATOS_N			8192			/* tope; el efectivo lo da la mascara */
#define MMU_DATOS_LEER		1u
#define MMU_DATOS_ESCRIBIR	2u

typedef struct
{
	DWORD	vpn;			/* direccion & ~mascara */
	DWORD	mascara;
	DWORD	mascara_neg;	/* ~mascara, ya negada: ver el comentario de abajo */
	DWORD	etiqueta;		/* ASID | usuario << 8 | VALIDA */
	DWORD	base;			/* la fisica de la pagina, ya compuesta */
	DWORD	permisos;		/* que tipos de acceso ya pasaron todas las pruebas */
	int		entrada;		/* de que entrada de la UTLB salio */
	DWORD	gen;			/* y con que generacion */
} mmu_datos_t;

/*
	La entrada mide 32 bytes y eso NO es relleno: el campo que la lleva de 28 a
	32 es la mascara ya negada, que el camino rapido negaba en cada acceso.
	Tres cosas salen de la misma linea, y por eso van juntas:

	  - **el indice deja de multiplicar**. 28 no es potencia de dos, asi que el
	    codigo emitido llevaba un `imul` de tres ciclos ADELANTE de la primera
	    carga, o sea en la cabeza de la cadena; con 32 es un `shl 5`.
	  - **la entrada deja de cruzar lineas de cache**. Con 28 bytes, 7 de cada
	    16 entradas quedan a caballo de dos lineas de 64, y el camino rapido lee
	    la entrada COMPLETA (etiqueta, permisos, mascara, vpn, entrada, gen y
	    base). Con 32 entran exactamente dos por linea y ninguna se parte.
	  - **el `not` se va del camino rapido**: la comparacion de vpn queda
	    `(dir & mascara_neg) == vpn` en vez de negar la mascara recien cargada.

	Las dos mascaras se escriben juntas en el unico sitio que llena la entrada;
	la comparacion de reuso mira `mascara`, asi que la negada la sigue de balde.
*/
DC_ASSERT_SIZE(mmu_datos, mmu_datos_t, 32);

/* El corrimiento que reemplaza a la multiplicacion en el codigo emitido. Los
   dos asertos van juntos a proposito: un cambio de tamano de la entrada tiene
   que romper la compilacion, no emitir un indice que apunta a otra ranura --
   que seria exacto en el interprete y silenciosamente distinto en el emitido. */
#define MMU_DATOS_DESP		5
DC_ASSERT(mmu_datos_desp, (1 << MMU_DATOS_DESP) == sizeof(mmu_datos_t));

extern mmu_datos_t	mmu_datos[MMU_DATOS_N];
extern DWORD		mmu_datos_mascara;

/*
	El indice de la cache de traducciones resueltas: la pagina, truncada.

	**Se probo mezclar los bits altos** ((dir >> 12) ^ (dir >> 25)), con la
	hipotesis de que la de kernel 0x8001_0000 y la de usuario 0x0001_0000 se
	pisaban por caer en la misma ranura. **Medido: neutro** -- el censo de
	accesos emitidos dio 64,1 % de camino rapido antes y despues --, porque los
	fallos no encontraban la ranura ocupada sino **sin estrenar**: eran P1/P2,
	que esta funcion devuelve sin traducir y por eso nunca se guardan aqui. La
	misma medicion descarto la capacidad (de 64 a 8192 entradas la tasa no se
	mueve). Ver docs/estado-del-arte-plan.md, "La fase 6, reescrita por su
	propio censo".
*/
#define MMU_DATOS_INDICE(dir)	(((dir) >> 12) & mmu_datos_mascara)

extern DWORD		mmu_utlb_gen[MMU_UTLB_ENTRADAS];

/* Un acceso a la UTLB avanza URC -- acierto de cache incluido. Un solo cuerpo
   para el macro y para mmu.c (urc_avanzar). */
/*
	La sonda de conservacion de avances (DCEMU_SONDA_URC, el expediente de la
	compuerta): uc cuenta TODO avance del lado C -- este macro es el unico
	cuerpo --, ue el avance emitido en linea del traductor, uv la virtual del
	ultimo emitido. Los tres se definen siempre en mmu.c (traza.c los imprime
	sin condicionales); solo el conteo se compila bajo la opcion, porque este
	macro es el camino mas caliente del arbol.
*/
extern unsigned long long	mmu_sonda_uc;
extern unsigned long long	mmu_sonda_ue;
extern DWORD				mmu_sonda_uv;

#ifdef DCEMU_SONDA_URC
#define MMU_SONDA_UC()	do { mmu_sonda_uc++; } while (0)
#else
#define MMU_SONDA_UC()	do { } while (0)
#endif

/*
	URC se avanza DIFERIDO: el acceso solo suma uno a una cuenta y el valor se
	materializa donde alguien lo mira (2026-09-08).

	El avance es un read-modify-write de MMUCR en cada acceso a la UTLB, y
	MMUCR vive adentro de `regmem` --un bloque de 16 MB-- a una linea de cache
	que no toca nada mas: en el codigo emitido eran **veinte instrucciones**
	con una carga del puntero, una carga dependiente del registro, la extraccion
	de URC y de URB, dos ramas y un almacenamiento. Diferido es `add [pend], 1`.

	**Nadie puede ver la diferencia**, y esa es la premisa que lo hace exacto:
	URC solo se observa en tres sitios y los tres materializan primero --el
	LDTLB del guest (syscontrol.c), una lectura del guest a MMUCR
	(`regmap_read`) y las trazas--, y una escritura del guest a MMUCR
	**descarta** lo pendiente, porque el valor que el guest escribe es el que
	queda (aplicar y despues pisar es pisar). Ver mmu_urc_al_dia().

	DCEMU_MMU_URC_INMEDIATO=1 vuelve al avance en cada acceso y reproduce la
	emision anterior byte por byte.
*/
extern unsigned long long	mmu_urc_pend;

/* Aplica lo pendiente y deja MMUCR con el URC que corresponde. Barata cuando
   no hay nada pendiente, que es el caso de todo guest sin MMU. */
void mmu_urc_al_dia(void);

/* Lo que el guest escribe manda: lo pendiente se descarta. */
void mmu_urc_descartar(void);

/*
	La forma cerrada de N avances, en un solo sitio porque la prueba de
	`tests/test_mmu.c` la compara contra N pasos de a uno.

	Con URB en cero --el caso de todo el parque-- URC es un contador de seis
	bits y N avances son una suma. Con URB puesto, el paso es
	`u = (u+1) & 63; if (u == URB) u = 0`, asi que desde `urc` faltan `k0`
	pasos para llegar a cero (URB - urc si urc < URB, y 64 - urc si no, que es
	cuando se llega a cero por el envolvimiento de seis bits) y despues el
	ciclo tiene periodo URB.
*/
DWORD mmu_urc_tras(DWORD urc, DWORD urb, unsigned long long n);

#define MMU_URC_AVANZAR()												\
	do																	\
	{																	\
		mmu_urc_pend++;													\
		MMU_SONDA_UC();													\
	} while (0)

/* De perf.h, que los llamadores ya incluyen via mem.h. */
#define MMU_TRADUCIR_EN_SITIO(var, permiso_bit, escritura)				\
	do																	\
	{																	\
		mmu_datos_t *	_mm_e;											\
		DWORD			_mm_tag;										\
																		\
		if (!mmu_macro_probar)											\
		{																\
			(var) = mmu_traducir((var), (escritura));					\
			break;														\
		}																\
																		\
		_mm_e   = &mmu_datos[MMU_DATOS_INDICE(var)];					\
		_mm_tag = mmu_etiqueta;											\
																		\
		if (_mm_e->etiqueta == _mm_tag									\
			&& (_mm_e->permisos & (permiso_bit))						\
			&& ((var) & _mm_e->mascara_neg) == _mm_e->vpn				\
			&& mmu_utlb_gen[_mm_e->entrada] == _mm_e->gen)				\
		{																\
			MMU_URC_AVANZAR();											\
			PERF_CONTAR(perf_mmu_traduce);								\
			PERF_CONTAR(perf_mmu_datos_acierto);						\
			(var) = _mm_e->base | ((var) & _mm_e->mascara);				\
		}																\
		else															\
			(var) = mmu_traducir((var), (escritura));					\
	} while (0)

/* ------------------------------------------------------------------------ */
/* Excepciones                                                              */
/* ------------------------------------------------------------------------ */

/* Codigos de EXPEVT. */
#define MMU_EXC_FALLO_R		0x040		/* fallo de TLB en lectura */
#define MMU_EXC_FALLO_W		0x060		/* fallo de TLB en escritura */
#define MMU_EXC_PRIMERA_W	0x080		/* primera escritura a la pagina */
#define MMU_EXC_PROT_R		0x0A0		/* violacion de proteccion, lectura */
#define MMU_EXC_PROT_W		0x0C0		/* violacion de proteccion, escritura */
#define MMU_EXC_DIR_R		0x0E0		/* error de direccion, lectura */
#define MMU_EXC_DIR_W		0x100		/* error de direccion, escritura */

/* Desplazamientos de vector. El fallo de TLB tiene el suyo, que es lo que lo
   hace barato en hardware real y lo que espera el manejador de KOS. */
#define MMU_VEC_FALLO		0x400
#define MMU_VEC_GENERAL		0x100

/*
	El salto de aborto vive en excepciones.h: lo comparten la MMU y la FPU.
	mmu_traducir() sale por excepcion_abortar() ante un fallo, y los datos de
	la falta pendiente quedan en excepcion_codigo / excepcion_vector.
*/
extern DWORD	mmu_exc_direccion;

#endif /* _MMU_H_ */
