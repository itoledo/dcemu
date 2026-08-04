/****************************************************************************

	MMU - la ventana de control P4 del SH-4

	Ver mmu.h y docs/mmu-plan.md, fase 1.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "excepciones.h"
#include "mmu.h"
#include "log.h"
#include "sh4emu.h"
#include "mem.h"		/* get_memory_pointer, para el cache de pagina del fetch */
#include "perf.h"		/* el desglose de la MMU: ver perf.h */

int mmu_activa = 0;

DWORD	mmu_exc_direccion;

DWORD mmu_itlb_dir[MMU_ITLB_ENTRADAS];
DWORD mmu_itlb_dat1[MMU_ITLB_ENTRADAS];
DWORD mmu_itlb_dat2[MMU_ITLB_ENTRADAS];

DWORD mmu_utlb_dir[MMU_UTLB_ENTRADAS];
DWORD mmu_utlb_dat1[MMU_UTLB_ENTRADAS];
DWORD mmu_utlb_dat2[MMU_UTLB_ENTRADAS];

/*
	La generacion de cada entrada de la UTLB.

	Existe porque LDTLB **reemplaza una entrada de 64** y sin embargo obligaba a
	vaciar las tres cachas de traduccion enteras. Medido sobre DCDoom: 1 091 201
	vaciados completos, uno cada 1850 traducciones --el manejador de recarga de
	Windows CE hace LDTLB en cada falta--, y con eso la cache de traducciones
	resueltas se quedaba en el 64 % de aciertos por mas que se la agrandara ocho
	veces. Los fallos no eran de tamano: eran obligatorios.

	Con esto, LDTLB solo incrementa la generacion de la entrada que toca y todo
	lo derivado de las otras 63 sigue valiendo. Una entrada guardada se valida
	comparando la generacion, que es una carga sobre un arreglo de 256 bytes
	siempre caliente.
*/
static DWORD mmu_utlb_gen[MMU_UTLB_ENTRADAS];

/* Nombres cortos para uso interno; el formato lo define mmu.h. */
#define BIT_V			MMU_BIT_V

/* Campos de una entrada de direcciones que hacen falta para la busqueda
   asociativa. */
#define VPN_DE(e)		((e) & 0xFFFFFC00)
#define ASID_DE(e)		((e) & 0x000000FF)
#define BIT_SH			MMU_BIT_SH
#define BIT_D_DIR		MMU_BIT_D_DIR
#define BIT_D_DAT		MMU_BIT_D_DAT

void mmu_reset(void)
{
	mmu_activa = 0;
	excepcion_salto_armado = 0;
	excepcion_actualizar_vigilancia();
	mmu_tlb_invalidar();

	memset(mmu_itlb_dir,  0, sizeof(mmu_itlb_dir));
	memset(mmu_itlb_dat1, 0, sizeof(mmu_itlb_dat1));
	memset(mmu_itlb_dat2, 0, sizeof(mmu_itlb_dat2));

	memset(mmu_utlb_dir,  0, sizeof(mmu_utlb_dir));
	memset(mmu_utlb_dat1, 0, sizeof(mmu_utlb_dat1));
	memset(mmu_utlb_dat2, 0, sizeof(mmu_utlb_dat2));
}

/* ------------------------------------------------------------------------ */
/* Entrada y salida de un valor de 32 bits                                  */
/* ------------------------------------------------------------------------ */

/*
	El manual solo admite acceso de palabra larga a estas ventanas. En vez de
	rechazar los demas tamanos -- que dejaria el destino sin tocar y haria la
	corrida irreproducible, el mismo problema que arreglaba mem_read_error() --
	se copian los bytes bajos que entren.
*/
static void devolver(DWORD valor, void * p, size_t size)
{
	if (!p)
		return;

	if (size >= sizeof(DWORD))
	{
		memcpy(p, &valor, sizeof(DWORD));

		if (size > sizeof(DWORD))
			memset((unsigned char *) p + sizeof(DWORD), 0, size - sizeof(DWORD));
	}
	else
		memcpy(p, &valor, size);
}

static DWORD recoger(const void * p, size_t size)
{
	DWORD valor = 0;

	if (!p)
		return 0;

	memcpy(&valor, p, (size > sizeof(DWORD)) ? sizeof(DWORD) : size);

	return valor;
}

/* ------------------------------------------------------------------------ */
/* Busqueda asociativa en la UTLB                                           */
/* ------------------------------------------------------------------------ */

/*
	Escribir en el arreglo de direcciones con el bit A puesto no indexa: compara
	el VPN (y el ASID, salvo en paginas compartidas) contra todas las entradas y
	actualiza la que coincide. Es como el software invalida una pagina puntual.

	Se implementa ahora, aunque nadie traduzca todavia, porque la alternativa
	seria indexar con los bits 13-8 -- o sea, pisar una entrada al azar que no
	es la que pidieron. Un respaldo plano que miente no sirve ni para probar.
*/
static void utlb_escritura_asociativa(DWORD valor)
{
	int i;
	int encontrada = 0;

	for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
	{
		if (!(mmu_utlb_dir[i] & BIT_V))
			continue;

		if (VPN_DE(mmu_utlb_dir[i]) != VPN_DE(valor))
			continue;

		/* Una pagina compartida coincide con cualquier ASID. */
		if (!(mmu_utlb_dat1[i] & BIT_SH) && ASID_DE(mmu_utlb_dir[i]) != ASID_DE(valor))
			continue;

		/* Solo se actualizan V y D; el resto de la entrada queda como estaba. */
		mmu_utlb_dir[i] = (mmu_utlb_dir[i] & ~(BIT_V | 0x00000200))
						| (valor & (BIT_V | 0x00000200));

		encontrada = 1;
	}

	/* Sin coincidencia no pasa nada. En el chip de verdad dos coincidencias son
	   causa de reset; aca se registran y se sigue, como dice el plan. */
	if (!encontrada)
		logxmsg(LOG_MEM, "mmu: escritura asociativa en UTLB sin coincidencia, VPN %08x\n", VPN_DE(valor));
}

/* ------------------------------------------------------------------------ */
/* Lectura                                                                  */
/* ------------------------------------------------------------------------ */

void mmu_p4_read(unsigned long direccion, void * p, size_t size)
{
	switch ((direccion >> 24) & 0xFF)
	{
		/* Arreglos de la cache. dcemu no emula cache, asi que toda linea esta
		   invalida: etiqueta 0, U=0, V=0. Es la respuesta honesta, y ademas es
		   la que evita que el software crea que hay algo sucio que volcar. */
		case MMU_P4_IC_DIR:
		case MMU_P4_IC_DAT:
		case MMU_P4_OC_DIR:
		case MMU_P4_OC_DAT:
		devolver(0, p, size);
		break;

		case MMU_P4_ITLB_DIR:
		devolver(mmu_itlb_dir[MMU_ITLB_INDICE(direccion)], p, size);
		break;

		case MMU_P4_ITLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			devolver(mmu_itlb_dat2[MMU_ITLB_INDICE(direccion)], p, size);
		else
			devolver(mmu_itlb_dat1[MMU_ITLB_INDICE(direccion)], p, size);
		break;

		case MMU_P4_UTLB_DIR:
		devolver(mmu_utlb_dir[MMU_UTLB_INDICE(direccion)], p, size);
		break;

		case MMU_P4_UTLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			devolver(mmu_utlb_dat2[MMU_UTLB_INDICE(direccion)], p, size);
		else
			devolver(mmu_utlb_dat1[MMU_UTLB_INDICE(direccion)], p, size);
		break;

		default:
		devolver(0, p, size);
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Escritura                                                                */
/* ------------------------------------------------------------------------ */

void mmu_p4_write(unsigned long direccion, void * p, size_t size)
{
	DWORD valor = recoger(p, size);

	switch ((direccion >> 24) & 0xFF)
	{
		/*
			Arreglos de la cache: se acepta y se descarta.

			Con el bit A puesto esto es una purga asociativa, que en el chip
			vuelca la linea a memoria si estaba sucia. dcemu escribe siempre
			directo a memoria, asi que nunca hay nada sucio y no volcar es
			correcto, no una simplificacion.

			Esta rama es la que saca de --traza-mem las 512 escrituras por
			corrida que hace KOS al barrer la cache de operandos.
		*/
		case MMU_P4_IC_DIR:
		case MMU_P4_IC_DAT:
		case MMU_P4_OC_DIR:
		case MMU_P4_OC_DAT:
		break;

		case MMU_P4_ITLB_DIR:
		mmu_itlb_dir[MMU_ITLB_INDICE(direccion)] = valor;
		break;

		case MMU_P4_ITLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			mmu_itlb_dat2[MMU_ITLB_INDICE(direccion)] = valor;
		else
			mmu_itlb_dat1[MMU_ITLB_INDICE(direccion)] = valor;
		break;

		case MMU_P4_UTLB_DIR:
		mmu_tlb_invalidar();

		if (direccion & MMU_BIT_A_TLB)
			utlb_escritura_asociativa(valor);
		else
		{
			int e = MMU_UTLB_INDICE(direccion);

			mmu_utlb_dir[e] = valor;

			/* V y D son un solo bit del chip, visible desde los dos arreglos:
			   en el de direcciones estan en 8 y 9, en el de datos 1 en 8 y 2.
			   Escribir uno tiene que verse en el otro. */
			mmu_utlb_dat1[e] = (mmu_utlb_dat1[e] & ~(BIT_V | BIT_D_DAT))
							 | (valor & BIT_V)
							 | ((valor & BIT_D_DIR) >> 7);
		}
		break;

		case MMU_P4_UTLB_DAT:
		mmu_tlb_invalidar();

		if (direccion & MMU_BIT_DATOS2)
			mmu_utlb_dat2[MMU_UTLB_INDICE(direccion)] = valor;
		else
		{
			int e = MMU_UTLB_INDICE(direccion);

			mmu_utlb_dat1[e] = valor;

			mmu_utlb_dir[e] = (mmu_utlb_dir[e] & ~(BIT_V | BIT_D_DIR))
							| (valor & BIT_V)
							| ((valor & BIT_D_DAT) << 7);
		}
		break;

		default:
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* LDTLB                                                                    */
/* ------------------------------------------------------------------------ */

void mmu_ldtlb(DWORD pteh, DWORD ptel, DWORD ptea, int urc)
{
	int e = urc & (MMU_UTLB_ENTRADAS - 1);

	/*
		**Solo la entrada que se reemplaza.** LDTLB toca una de 64, asi que
		vaciar las tres cachas enteras --que es lo que se hacia-- tiraba todo lo
		derivado de las otras 63 por nada. Con el manejador de recarga de Windows
		CE eso ocurria 1 091 201 veces en 35 segundos emulados, una cada 1850
		traducciones, y era la razon de que la cache de traducciones resueltas se
		quedara en el 64 % de aciertos por mas que se la agrandara ocho veces.

		La pagina unica de busqueda si se tira: no guarda de que entrada salio.
	*/
	mmu_utlb_gen[e]++;
	mmu_fetch_invalidar();

	/* PTEH: VPN en 31-10, ASID en 7-0. Los bits 9-8 son reservados. */
	mmu_utlb_dir[e] = (pteh & 0xFFFFFC00) | (pteh & 0x000000FF)
					| (ptel & BIT_V)					/* V viene de PTEL */
					| ((ptel & BIT_D_DAT) << 7);		/* D tambien, bit 2 -> 9 */

	/* PTEL tiene exactamente el formato del arreglo de datos 1: PPN en 28-10,
	   V, SZ1, PR, SZ0, C, D, SH y WT en los bits bajos. */
	mmu_utlb_dat1[e] = ptel & 0x1FFFFDFF;

	/* PTEA: TC en el bit 3, SA en 2-0. */
	mmu_utlb_dat2[e] = ptea & 0x0000000F;
}

/* ------------------------------------------------------------------------ */
/* Traduccion                                                               */
/* ------------------------------------------------------------------------ */

/*
	SZ1 esta en el bit 7 y SZ0 en el 4. Juntos dan el tamano de pagina, y de
	ahi sale la mascara del desplazamiento dentro de la pagina.
*/
static DWORD mascara_de_pagina(DWORD dat1)
{
	switch ((((dat1) >> 6) & 2) | (((dat1) >> 4) & 1))
	{
		case 0:		return 0x000003FFul;	/*  1 KB */
		case 1:		return 0x00000FFFul;	/*  4 KB */
		case 2:		return 0x0000FFFFul;	/* 64 KB */
		default:	return 0x000FFFFFul;	/*  1 MB */
	}
}

/*
	Deja la excepcion preparada y sale por el salto. No devuelve, salvo que no
	haya salto armado -- que es el caso de un acceso disparado por el propio
	emulador y no por una instruccion, donde no hay nada que reejecutar.
*/
static DWORD fallar(DWORD codigo, DWORD vector, DWORD direccion)
{
	mmu_exc_direccion = direccion;

	/* TEA lleva la direccion que fallo y PTEH.VPN su VPN; el ASID no se toca. */
	*TEA  = direccion;
	*PTEH = (direccion & 0xFFFFFC00) | (*PTEH & 0x000000FF);

	/* No devuelve si hay salto armado. Si no lo hay -- un acceso del propio
	   emulador, no de una instruccion -- deja la excepcion anotada y sigue. */
	excepcion_abortar(codigo, vector);

	return direccion;
}

/*
	Busca la entrada de la UTLB que cubre una direccion: V puesto, VPN igual
	bajo la mascara de la pagina, y el ASID de PTEH salvo pagina compartida o
	modo espacio unico privilegiado. Deja la mascara en *mascara_out y
	devuelve el indice, o -1 sin coincidencia.

	Es el unico recorrido de la UTLB: lo comparten los datos y la busqueda de
	instrucciones, para que la regla de coincidencia no pueda divergir entre
	las dos caras.

	Recorrido lineal de las 64 entradas, desempaquetando al vuelo. Delante
	tiene la cache de traduccion (utlb_buscar), que es la que evita llegar
	aca: medido, este recorrido promediaba 34,5 entradas de 64 y se llevaba
	el 42 % del tiempo real de DCDoom.
*/
/*
	URC avanza con cada acceso a la UTLB -- acierte o falle -- y con URB > 0
	vuelve a cero al alcanzarlo; es el contador con el que LDTLB elige su
	entrada. El manejador de recarga de Windows CE no escribe URC jamas: cuenta
	con este avance. Sin el, cada LDTLB cae en la misma entrada, la recarga de
	la pagina de codigo desaloja a la de datos y la instruccion que falla por
	las dos queda en ping-pong infinito.

	Esta afuera del recorrido porque **un acierto de la cache tambien es un
	acceso a la UTLB** y tiene que avanzarlo igual. Si no lo hiciera, la cache
	cambiaria que entrada elige el LDTLB del guest, o sea su camino de
	ejecucion: dejaria de ser una optimizacion para pasar a ser otro emulador.
*/
static void urc_avanzar(void)
{
	DWORD urc = (MMUCR_URC(*MMUCR) + 1) & 0x3F;

	if (MMUCR_URB(*MMUCR) && urc == MMUCR_URB(*MMUCR))
		urc = 0;

	*MMUCR = (*MMUCR & ~0x0000FC00ul) | (urc << 10);
}

static int utlb_encontrar_ex(DWORD direccion, int usuario, int sv,
							 DWORD * mascara_out, int avanzar_urc)
{
	int i;

	if (avanzar_urc)
		urc_avanzar();

	for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
	{
		DWORD dir = mmu_utlb_dir[i];
		DWORD mascara;

		if (!(dir & BIT_V))
			continue;

		mascara = mascara_de_pagina(mmu_utlb_dat1[i]);

		if ((dir & ~mascara) != (direccion & ~mascara))
			continue;

		/* El ASID se ignora en paginas compartidas, y tambien en modo espacio
		   unico (MMUCR.SV) cuando corremos en modo privilegiado. */
		if (!(mmu_utlb_dat1[i] & BIT_SH) && !(sv && !usuario)
			&& ASID_DE(dir) != ASID_DE(*PTEH))
			continue;

		*mascara_out = mascara;

		/* Cuanto se recorrio: se suma una vez al salir y no una por vuelta,
		   para que el instrumento no sea lo que se mide. Ver perf.h. */
		PERF_SUMAR_A(perf_mmu_utlb_pasos, (unsigned long long) i + 1);
		return i;
	}

	PERF_SUMAR_A(perf_mmu_utlb_pasos, (unsigned long long) MMU_UTLB_ENTRADAS);
	return -1;
}

/* El recorrido de siempre: avanza URC, como el chip en cada acceso a la UTLB. */
static int utlb_encontrar(DWORD direccion, int usuario, int sv, DWORD * mascara_out)
{
	return utlb_encontrar_ex(direccion, usuario, sv, mascara_out, 1);
}

/* ------------------------------------------------------------------------ */
/* La cache de traduccion                                                    */
/* ------------------------------------------------------------------------ */

/*
	Delante del recorrido de la UTLB, y es lo que hace que un guest con MMU no
	cueste cuatro veces por instruccion.

	El problema que resuelve, medido sobre DCDoom antes de existir: 1313
	millones de recorridos con **34,5 entradas de 64 de promedio**, o sea unas
	45 mil millones de vueltas de ese bucle en 35 segundos emulados -- 8,3 por
	instruccion emulada --, cada una desempaquetando la mascara de pagina de su
	entrada. El camino de busqueda de instrucciones ya tenia su cache de una
	pagina y fallaba el 1,73 %; el de datos no tenia ninguna.

	**Que guarda: el indice de la entrada, no la traduccion.** Es la decision de
	diseno que la hace segura. Devuelve exactamente lo que habria devuelto el
	recorrido, y la proteccion, el bit D y la primera escritura se siguen
	evaluando sobre la entrada real de la UTLB en cada uno de los tres
	llamadores. Una cache que guardara la direccion fisica tendria que replicar
	esas reglas, y replicarlas es como se rompen.

	**La etiqueta.** Pagina (con su propia mascara), ASID y modo, empacados de
	modo que el acierto sea una comparacion de palabra y otra de pagina:

	 - el ASID va porque una entrada no compartida solo casa con el suyo;
	 - el modo (usuario contra privilegiado) va porque de el depende que el ASID
	   se ignore, via la regla de espacio unico;
	 - **MMUCR.SV no va**, y esa es la unica cosa de aca que no se ve en el
	   codigo: escribir MMUCR invalida la cache entera, asi que un cambio de SV
	   no puede sobrevivir a una entrada guardada. Si alguien quita esa
	   invalidacion, esto miente en silencio.

	**La invalidacion cuelga de mmu_fetch_invalidar()** a proposito: los cuatro
	sitios que mutan la TLB -- LDTLB, las escrituras a los arreglos por P4,
	MMUCR y PTEH por el ASID -- ya la llamaban para el cache de busqueda, asi
	que la correccion de esta cache es la misma que la de aquella y no un
	argumento nuevo que haya que verificar aparte.

	**Las paginas de 1 KB comparten ranura de a cuatro**, porque el indice usa
	los bits de 4 KB. Es conflicto, no error: la etiqueta lo atrapa y el caso
	degrada a un recorrido. Se indexa por 4 KB y no por 1 KB porque una pagina
	de 4 KB ocuparia entonces cuatro ranuras con la misma respuesta.
*/
#define MMU_CACHE_N			256					/* medido: ver docs/rendimiento-plan.md, 6.1 */
#define MMU_CACHE_VALIDA	0x00010000ul		/* bit fuera de ASID y del modo */

typedef struct
{
	DWORD	vpn;			/* direccion & ~mascara */
	DWORD	mascara;
	DWORD	etiqueta;		/* ASID | usuario << 8 | VALIDA; 0 = vacia */
	int		entrada;		/* indice en la UTLB */
	DWORD	gen;			/* generacion de esa entrada al guardarla */
} mmu_cache_t;


static mmu_cache_t	mmu_cache[MMU_CACHE_N];

/*
	El segundo nivel: la traduccion de datos ya resuelta. Ver mmu_traducir().

	El permiso va en un campo de bits y **no en la etiqueta**. La primera
	version lo metia en la etiqueta y acertaba solo el 57 %: leer y escribir la
	misma pagina daban dos etiquetas distintas que caian en la misma ranura y se
	expulsaban a cada acceso -- justo el patron de un blit, que es lo que hace
	DOOM --. Con el permiso aparte, una pagina ocupa una ranura y cada tipo de
	acceso se valida por separado la primera vez.
*/
#define MMU_DATOS_N			8192		/* el tope; el efectivo lo fija la mascara */
#define MMU_DATOS_LEER		1u
#define MMU_DATOS_ESCRIBIR	2u

typedef struct
{
	DWORD	vpn;			/* direccion & ~mascara */
	DWORD	mascara;
	DWORD	etiqueta;		/* ASID | usuario << 8 | VALIDA */
	DWORD	base;			/* la fisica de la pagina, ya compuesta */
	DWORD	permisos;		/* que tipos de acceso ya pasaron todas las pruebas */
	int		entrada;		/* de que entrada de la UTLB salio */
	DWORD	gen;			/* y con que generacion */
} mmu_datos_t;

static mmu_datos_t	mmu_datos[MMU_DATOS_N];

/* Cuantas entradas se usan de verdad. Variable y no constante para poder barrer
   el tamano dentro de un mismo binario: comparar dos compilaciones mete el
   layout como variable. DCEMU_MMU_DATOS=N (potencia de dos). */
static DWORD		mmu_datos_mascara = MMU_DATOS_N - 1;

/*
	Y la de busqueda de instrucciones, detras de la pagina unica que ya habia.

	La pagina unica de MMU_FETCH_PUNTERO() acierta el 98,3 % -- pero el 1,7 %
	restante son 94 millones de resoluciones completas en 35 segundos emulados,
	porque Windows CE salta entre el kernel, el proceso y sus DLL sin parar y
	cada cruce desaloja la unica entrada que hay. Esto las atiende con una
	comparacion en vez de rehacer la traduccion.
*/
#define MMU_FETCH_N			64

typedef struct
{
	DWORD			vpn;
	DWORD			mascara;
	DWORD			etiqueta;	/* ASID | md << 8 | VALIDA */
	unsigned char *	base;
	int				entrada;	/* de que entrada de la UTLB salio */
	DWORD			gen;
} mmu_fetch_t;

static mmu_fetch_t	mmu_fetch_cache[MMU_FETCH_N];

/* El interruptor para aislar una regresion y para el A/B, en la misma familia
   que DCEMU_SIN_DIBUJO y DCEMU_SIN_AICA. Se lee una vez al arrancar. */
static int mmu_cache_apagada = 0;

/*
	Vaciar las tres cachas. Es lo que hay que hacer cuando **cambia el contenido
	de la TLB**: LDTLB, las escrituras a los arreglos por P4, y MMUCR.

	Lo que NO lo pide es una escritura a PTEH, y esa distincion vale el 8 % de
	DCDoom. PTEH lleva el ASID, y el manejador de recarga de Windows CE lo
	escribe en **cada** falta -- millones de veces --; con la invalidacion
	unica, cada una vaciaba mil entradas recien pobladas y la cache de datos se
	quedaba en el 62 % de aciertos. Pero el ASID esta en la etiqueta de las tres
	cachas, asi que una entrada de otro ASID simplemente no casa, y vuelve a
	casar sola cuando el guest vuelve a ese ASID. Si en el medio alguien
	reemplazo esa entrada de la UTLB, eso paso por LDTLB, que si invalida.

	Ver mmu_fetch_invalidar(), que es la mitad barata y la unica que PTEH
	necesita.
*/
void mmu_tlb_invalidar(void)
{
	PERF_CONTAR(perf_mmu_vaciados);

	memset(mmu_cache, 0, sizeof(mmu_cache));
	memset(mmu_datos, 0, sizeof(mmu_datos));
	memset(mmu_fetch_cache, 0, sizeof(mmu_fetch_cache));

	mmu_fetch_invalidar();
}

void mmu_sondas_iniciar(void)
{
	const char * v = getenv("DCEMU_SIN_CACHE_MMU");

	mmu_cache_apagada = (v != NULL && atoi(v) != 0);

	v = getenv("DCEMU_MMU_DATOS");

	if (v != NULL)
	{
		int n = atoi(v);

		/* A la potencia de dos que no pase del tope. */
		if (n > MMU_DATOS_N)
			n = MMU_DATOS_N;

		if (n >= 1)
		{
			int m = 1;

			while ((m << 1) <= n)
				m <<= 1;

			mmu_datos_mascara = (DWORD) (m - 1);
			fprintf(stderr, "mmu: cache de traducciones resueltas,"
				" %d entradas\n", m);
		}
	}

	mmu_tlb_invalidar();

	if (mmu_cache_apagada)
		fprintf(stderr, "mmu: cache de traduccion APAGADA"
			" (DCEMU_SIN_CACHE_MMU)\n");
}

/*
	La busqueda que usan los tres caminos que traducen: datos (mmu_traducir),
	instrucciones (traducir_busqueda) y las store queues (mmu_traducir_sq).

	Comparten cache porque comparten respuesta: el indice que devuelve el
	recorrido depende de (direccion, usuario, sv, ASID) y de nada mas -- no de
	quien pregunta ni de si es lectura o escritura --. Que la busqueda de
	instrucciones entre por aca es lo que le da cache a sus 94 millones de
	fallos de pagina, que antes recorrian enteros.
*/
static int utlb_buscar(DWORD direccion, int usuario, int sv, DWORD * mascara_out)
{
	DWORD			etiqueta;
	mmu_cache_t *	e;
	int				i;

	/* Las busquedas de los tres caminos, que es contra lo que se mide la tasa
	   de aciertos y el recorrido medio. */
	PERF_CONTAR(perf_mmu_utlb);

	if (mmu_cache_apagada)
		return utlb_encontrar(direccion, usuario, sv, mascara_out);

	etiqueta = ASID_DE(*PTEH) | ((DWORD) usuario << 8) | MMU_CACHE_VALIDA;
	e        = &mmu_cache[(direccion >> 12) & (MMU_CACHE_N - 1)];

	if (e->etiqueta == etiqueta && (direccion & ~e->mascara) == e->vpn
		&& mmu_utlb_gen[e->entrada] == e->gen)
	{
		/* Un acierto es un acceso a la UTLB tambien: ver urc_avanzar(). */
		urc_avanzar();

		PERF_CONTAR(perf_mmu_cache_acierto);
		PERF_SUMAR_A(perf_mmu_utlb_pasos, 1);

		*mascara_out = e->mascara;
		return e->entrada;
	}

	i = utlb_encontrar(direccion, usuario, sv, mascara_out);

	/* Una falta no se guarda: la entrada no existe todavia y el manejador del
	   guest esta por cargarla, lo que invalida esto de todos modos. */
	if (i >= 0)
	{
		e->vpn      = direccion & ~*mascara_out;
		e->mascara  = *mascara_out;
		e->etiqueta = etiqueta;
		e->entrada  = i;
		e->gen      = mmu_utlb_gen[i];
	}

	return i;
}

/*
	El segundo nivel: la traduccion ya resuelta.

	utlb_buscar() evita el recorrido, pero mmu_traducir() sigue pagando por cada
	acceso las comprobaciones de rango de P1/P2/P4, las de proteccion, la de
	primera escritura y la composicion de la fisica. Con 2018 millones de
	accesos en 35 segundos emulados eso es lo que queda.

	mmu_datos[] guarda el resultado final para (pagina, ASID, modo, lectura o
	escritura). **El tipo de acceso entra en la etiqueta**, y esa es la razon por
	la que saltearse las comprobaciones es correcto: una entrada solo se guarda
	despues de que ESE tipo de acceso paso todas -- proteccion y bit D
	incluidos -- sobre esa misma pagina. Una lectura permitida no autoriza una
	escritura, porque son etiquetas distintas.

	El bit D es el caso que hay que mirar dos veces: escribir en una pagina
	limpia levanta "primera escritura", el manejador del guest marca la pagina y
	reejecuta. Marcarla pasa por LDTLB o por los arreglos de la UTLB, y los dos
	invalidan; asi que no puede quedar guardada una escritura autorizada contra
	una pagina que todavia estaba limpia.

	P1, P2 y P4 no se guardan: no se traducen y ya son baratas.
*/
DWORD mmu_traducir(DWORD direccion, int escritura)
{
	int usuario = (SR_MD == 0);
	int sv;
	DWORD mascara, d1, fisica, etiqueta;
	mmu_datos_t * dc;
	int i, pr;

	PERF_CONTAR(perf_mmu_traduce);

	if (!mmu_cache_apagada)
	{
		DWORD permiso = escritura ? MMU_DATOS_ESCRIBIR : MMU_DATOS_LEER;

		etiqueta = ASID_DE(*PTEH) | ((DWORD) usuario << 8) | MMU_CACHE_VALIDA;
		dc       = &mmu_datos[(direccion >> 12) & mmu_datos_mascara];

		if (dc->etiqueta == etiqueta && (dc->permisos & permiso)
			&& (direccion & ~dc->mascara) == dc->vpn
			&& mmu_utlb_gen[dc->entrada] == dc->gen)
		{
			/* Un acierto sigue siendo un acceso a la UTLB. Ver urc_avanzar(). */
			urc_avanzar();

			PERF_CONTAR(perf_mmu_datos_acierto);
				return dc->base | (direccion & dc->mascara);
		}

		/*
			De que tipo es el fallo. Si la ranura tenia la MISMA pagina y fallo
			igual, lo que se estan pisando son dos etiquetas de la misma pagina
			--modo o ASID-- y la salida es asociatividad; si tenia otra pagina,
			es capacidad y la salida es tamano. Sin esta distincion las dos
			hipotesis se parecen y ya se gasto una medicion en la equivocada.
		*/
		if (perf_activa && dc->etiqueta != 0)
		{
			if ((direccion & ~dc->mascara) == dc->vpn)
				perf_mmu_datos_choque++;
			else
				perf_mmu_datos_capacidad++;
		}
	}
	else
	{
		etiqueta = 0;
		dc       = NULL;
	}

	sv = (*MMUCR & MMUCR_SV) != 0;

	/*
		P1 y P2 no pasan por la TLB nunca, y se devuelven SIN tocar. Convertirlas
		a fisica romperia el despacho: mem_hash_read[0x80] es bios_read mientras
		que mem_hash_read[0xA0] es pvr_read, o sea que la misma direccion fisica
		se atiende distinto segun la ventana. Dejarlas como estan mantiene el
		comportamiento actual exacto.
	*/
	if (direccion >= 0x80000000ul && direccion < 0xC0000000ul)
	{
		if (usuario)
			return fallar(escritura ? MMU_EXC_DIR_W : MMU_EXC_DIR_R,
						  MMU_VEC_GENERAL, direccion);

		return direccion;
	}

	/* P4: registros de control y store queues. Tampoco se traduce. Que las
	   store queues respeten MMUCR.SQMD es la fase 6. */
	if (direccion >= 0xE0000000ul)
	{
		if (usuario && direccion >= 0xE4000000ul)
			return fallar(escritura ? MMU_EXC_DIR_W : MMU_EXC_DIR_R,
						  MMU_VEC_GENERAL, direccion);

		return direccion;
	}

	/* P3 es solo privilegiada. P0/U0 la ve cualquiera. */
	if (direccion >= 0xC0000000ul && usuario)
		return fallar(escritura ? MMU_EXC_DIR_W : MMU_EXC_DIR_R,
					  MMU_VEC_GENERAL, direccion);

	i = utlb_buscar(direccion, usuario, sv, &mascara);

	if (i < 0)
	{
		PERF_CONTAR(perf_mmu_falta);
		return fallar(escritura ? MMU_EXC_FALLO_W : MMU_EXC_FALLO_R,
					  MMU_VEC_FALLO, direccion);
	}

	d1 = mmu_utlb_dat1[i];

	/* PR: 00 privilegiado solo lectura, 01 privilegiado lectura/escritura,
	   10 todos solo lectura, 11 todos lectura/escritura. */
	pr = (d1 >> 5) & 3;

	if (usuario && pr < 2)
		return fallar(escritura ? MMU_EXC_PROT_W : MMU_EXC_PROT_R,
					  MMU_VEC_GENERAL, direccion);

	if (escritura && !(pr & 1))
		return fallar(MMU_EXC_PROT_W, MMU_VEC_GENERAL, direccion);

	/* Escribir en una pagina limpia no es violacion de proteccion sino
	   "primera escritura": el manejador marca la pagina y reejecuta. */
	if (escritura && !(mmu_utlb_dir[i] & BIT_D_DIR))
		return fallar(MMU_EXC_PRIMERA_W, MMU_VEC_GENERAL, direccion);

	/* PPN en los bits 28-10 del arreglo de datos 1. */
	fisica = ((d1 & 0x1FFFFC00ul) & ~mascara) | (direccion & mascara);

	/* Recien aca se guarda: paso proteccion, bit D y todo lo demas para ESTE
	   tipo de acceso. Antes de este punto, cada `fallar` salio por longjmp.

	   Si la ranura ya tenia esta misma pagina, se suma el permiso al que
	   hubiera; si tenia otra, se reemplaza y el permiso arranca solo con el
	   que se acaba de validar. */
	if (dc != NULL)
	{
		DWORD vpn = direccion & ~mascara;

		if (dc->etiqueta != etiqueta || dc->vpn != vpn
			|| dc->mascara != mascara || dc->entrada != i
			|| dc->gen != mmu_utlb_gen[i])
		{
			dc->vpn      = vpn;
			dc->mascara  = mascara;
			dc->etiqueta = etiqueta;
			dc->base     = (((d1 & 0x1FFFFC00ul) & ~mascara) | 0xA0000000ul);
			dc->permisos = 0;
			dc->entrada  = i;
			dc->gen      = mmu_utlb_gen[i];
		}

		dc->permisos |= escritura ? MMU_DATOS_ESCRIBIR : MMU_DATOS_LEER;
	}

	return fisica | 0xA0000000ul;
}

/*
	El volcado de una store queue con la MMU activa -- la fase 6 de
	docs/mmu-plan.md. Con MMUCR.AT puesto, el PREF sobre 0xE0000000-0xE3FFFFFF
	traduce por la UTLB la VA COMPLETA de la SQ y QACR no participa (manual del
	SH-4, 4.6: el destino sale de la entrada de la TLB); con SQMD en 1 el
	acceso de usuario es error de direccion. Es exactamente lo que configura
	SetStoreQueueBase de Windows CE: mapea la VA de la SQ en sus tablas, y su
	manejador de recarga atiende las VA altas por una rama propia, asi que el
	fallo tiene que llevar ESA VPN. Enmascarar antes de traducir -- la formula
	de QACR, que es la de MMU apagada -- le presentaba a CE un fallo por una
	VPN de la ranura 1 que sus tablas no mapean, el recorrido por software
	confirmaba PTE 0, y el blit de video de ddhal.dll moria por violacion de
	acceso c0000005 en el PREF: DCDoom entero se iba por ese error.

	Devuelve la fisica cruda, sin ventana: el llamador despacha por zona
	fisica (VRAM, FIFO del TA, RAM), igual que con la formula de QACR. Si
	falla no vuelve, como mmu_traducir().

	Lo que queda de la fase 6: SQMD tambien proteje las ESCRITURAS al buffer
	de la SQ (la MOV a 0xE3xxxxxx), que entran por sq_write() sin pasar por
	aca. CE corre con SQMD en 0, asi que nada de lo que corre lo distingue.
*/
DWORD mmu_traducir_sq(DWORD direccion)
{
	int usuario = (SR_MD == 0);
	int sv      = (*MMUCR & MMUCR_SV) != 0;
	DWORD mascara, d1;
	int i, pr;

	if (usuario && (*MMUCR & MMUCR_SQMD))
		return fallar(MMU_EXC_DIR_W, MMU_VEC_GENERAL, direccion);

	i = utlb_buscar(direccion, usuario, sv, &mascara);

	if (i < 0)
		return fallar(MMU_EXC_FALLO_W, MMU_VEC_FALLO, direccion);

	d1 = mmu_utlb_dat1[i];
	pr = (d1 >> 5) & 3;

	if ((usuario && pr < 2) || !(pr & 1))
		return fallar(MMU_EXC_PROT_W, MMU_VEC_GENERAL, direccion);

	if (!(mmu_utlb_dir[i] & BIT_D_DIR))
		return fallar(MMU_EXC_PRIMERA_W, MMU_VEC_GENERAL, direccion);

	return ((d1 & 0x1FFFFC00ul) & ~mascara) | (direccion & mascara);
}

/*
	Traduccion para MIRAR, no para ejecutar: devuelve la fisica cruda o 0 si no
	se puede resolver, y **no falla, no entra a ninguna excepcion y no toca
	nada del guest** -- ni siquiera URC, que el recorrido normal avanza.

	Es para los diagnosticos que quieren leer memoria del guest en un punto
	donde fallar seria peor que no ver: el volcado de las cadenas de
	depuracion de Windows CE se hace desde dentro de excepcion_entrar(), y
	ahi un longjmp a medias dejaria al emulador en un estado imposible.
*/
DWORD mmu_traducir_mirar(DWORD direccion)
{
	int usuario = (SR_MD == 0);
	int sv      = (*MMUCR & MMUCR_SV) != 0;
	DWORD mascara;
	int i;

	if (!mmu_activa)
		return direccion;

	/* P1/P2/P4 no pasan por la TLB. */
	if (direccion >= 0x80000000ul)
		return direccion;

	i = utlb_encontrar_ex(direccion, usuario, sv, &mascara, 0);

	if (i < 0)
		return 0;

	return ((mmu_utlb_dat1[i] & 0x1FFFFC00ul) & ~mascara) | (direccion & mascara);
}

/* ------------------------------------------------------------------------ */
/* Busqueda de instrucciones (fase 7)                                       */
/* ------------------------------------------------------------------------ */

DWORD           mmu_fetch_vpn     = 0xFFFFFFFFul;
DWORD           mmu_fetch_mascara = 0;
DWORD           mmu_fetch_md      = 0xFFFFFFFFul;
unsigned char * mmu_fetch_base    = NULL;

void mmu_fetch_invalidar(void)
{
	/* vpn imposible: ningun PC es 0xFFFFFFFF (seria impar). */
	mmu_fetch_vpn     = 0xFFFFFFFFul;
	mmu_fetch_mascara = 0;
	mmu_fetch_md      = 0xFFFFFFFFul;
	mmu_fetch_base    = NULL;

	/* Solo la pagina unica, que es la unica de las cuatro cachas cuya etiqueta
	   NO lleva el ASID: la compara MMU_FETCH_PUNTERO en el camino de cada
	   instruccion y meterselo costaria una carga mas ahi. Por eso es esta --y
	   solo esta-- la que una escritura a PTEH tiene que tirar. El resto se
	   vacia con mmu_tlb_invalidar(), que es para cuando la TLB cambia. */
}

/*
	Traduce la direccion de una instruccion. Mismos codigos que una lectura de
	datos -- fallo 0x040 por el vector 0x400, proteccion 0x0A0, error de
	direccion 0x0E0 --, sin las caras de escritura. La ITLB real es un cache
	de la UTLB que el chip rellena solo en el fallo, asi que aqui se busca
	directo en la UTLB; lo unico que se pierde es una escritura directa al
	arreglo de la ITLB por P4, que ningun sistema usa para mapear codigo.

	P1 y P2 salen sin traducir, como en los datos, con la zona entera de
	16 MB como "pagina": mem_zone[] resuelve por el byte alto, asi que el
	puntero base vale para toda la zona. Ejecutar desde P4 o desde las store
	queues es error de direccion.
*/
static DWORD traducir_busqueda(DWORD pc, DWORD * mascara_out, int * indice_out)
{
	int usuario = (SR_MD == 0);
	int sv      = (*MMUCR & MMUCR_SV) != 0;
	DWORD d1;
	int i, pr;

	/* -1 dice "no salio de la UTLB". La generacion de esa ranura no se puede
	   comprobar, asi que P1/P2 no se guardan en la cache de busqueda. */
	*indice_out = -1;

	if (pc >= 0x80000000ul && pc < 0xC0000000ul)
	{
		if (usuario)
			return fallar(MMU_EXC_DIR_R, MMU_VEC_GENERAL, pc);

		*mascara_out = 0x00FFFFFFul;
		return pc;
	}

	if (pc >= 0xE0000000ul)
		return fallar(MMU_EXC_DIR_R, MMU_VEC_GENERAL, pc);

	if (pc >= 0xC0000000ul && usuario)
		return fallar(MMU_EXC_DIR_R, MMU_VEC_GENERAL, pc);

	i = utlb_buscar(pc, usuario, sv, mascara_out);

	if (i < 0)
		return fallar(MMU_EXC_FALLO_R, MMU_VEC_FALLO, pc);

	d1 = mmu_utlb_dat1[i];
	pr = (d1 >> 5) & 3;

	if (usuario && pr < 2)
		return fallar(MMU_EXC_PROT_R, MMU_VEC_GENERAL, pc);

	*indice_out = i;

	return (((d1 & 0x1FFFFC00ul) & ~*mascara_out) | (pc & *mascara_out))
		 | 0xA0000000ul;
}

/*
	El camino lento del fetch: traduce, repuebla el cache de pagina y devuelve
	el puntero host. Si la traduccion falla no vuelve -- fallar() sale por
	excepcion_abortar() y main_loop() entra a la excepcion --, y por eso el
	cache se escribe recien con la traduccion resuelta.
*/
unsigned char * mmu_fetch_resolver(DWORD pc)
{
	DWORD			mascara = 0;
	DWORD			fisica;
	DWORD			etiqueta;
	int				indice = -1;
	mmu_fetch_t *	fc;

	/* Solo los fallos de la pagina unica: el acierto vive dentro de
	   MMU_FETCH_PUNTERO, o sea en el camino de CADA instruccion, y contarlo
	   ahi seria medir el instrumento. Los aciertos son las instrucciones
	   menos esto. */
	PERF_CONTAR(perf_mmu_fetch_fallo);

	/* El ASID va en la etiqueta --a diferencia de la pagina unica-- y eso es
	   lo que permite que una escritura a PTEH no tenga que tirar esta cache.
	   Ver mmu_tlb_invalidar(). */
	etiqueta = ASID_DE(*PTEH) | ((DWORD) SR_MD << 8) | MMU_CACHE_VALIDA;
	fc       = &mmu_fetch_cache[(pc >> 12) & (MMU_FETCH_N - 1)];

	/*
		El segundo nivel. Repuebla la pagina unica y contesta sin volver a
		traducir: el 1,7 % que falla la pagina unica son 94 millones de
		resoluciones, y casi todas vuelven a una pagina que ya se resolvio
		--el ida y vuelta entre el kernel de CE y el proceso--.
	*/
	if (!mmu_cache_apagada
		&& fc->etiqueta == etiqueta && (pc & ~fc->mascara) == fc->vpn
		&& mmu_utlb_gen[fc->entrada] == fc->gen)
	{
		PERF_CONTAR(perf_mmu_fetch_acierto2);

		mmu_fetch_vpn     = fc->vpn;
		mmu_fetch_mascara = fc->mascara;
		mmu_fetch_md      = (DWORD) SR_MD;
		mmu_fetch_base    = fc->base;

		return fc->base + (pc & fc->mascara);
	}

	fisica = traducir_busqueda(pc, &mascara, &indice);

	mmu_fetch_vpn     = pc & ~mascara;
	mmu_fetch_mascara = mascara;
	mmu_fetch_md      = (DWORD) SR_MD;
	mmu_fetch_base    = (unsigned char *) get_memory_pointer(fisica & ~mascara);

	/* Solo lo que salio de la UTLB: sin entrada no hay generacion contra la
	   cual validar despues. P1 y P2 vuelven a resolverse, que es barato. */
	if (!mmu_cache_apagada && indice >= 0)
	{
		fc->vpn      = mmu_fetch_vpn;
		fc->mascara  = mascara;
		fc->etiqueta = etiqueta;
		fc->base     = mmu_fetch_base;
		fc->entrada  = indice;
		fc->gen      = mmu_utlb_gen[indice];
	}

	return mmu_fetch_base + (pc & mascara);
}

/* ------------------------------------------------------------------------ */
/* MMUCR                                                                    */
/* ------------------------------------------------------------------------ */

int mmu_mmucr_escrito(DWORD valor)
{
	static int ya_aviso = 0;
	int i;

	mmu_tlb_invalidar();

	/* TI invalida la TLB entera: baja el bit V de todas las entradas. Es un
	   bit de un solo disparo, no queda puesto. */
	if (valor & MMUCR_TI)
	{
		for (i = 0; i < MMU_ITLB_ENTRADAS; i++)
		{
			mmu_itlb_dir[i]  &= ~BIT_V;
			mmu_itlb_dat1[i] &= ~BIT_V;
		}

		for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
		{
			mmu_utlb_dir[i]  &= ~BIT_V;
			mmu_utlb_dat1[i] &= ~BIT_V;
		}

		logxmsg(LOG_MEM, "mmu: TI, TLB invalidada\n");
	}

	mmu_activa = (valor & MMUCR_AT) != 0;
	excepcion_actualizar_vigilancia();

	if (!mmu_activa)
		return 0;

	/*
		Sale por stderr y no por logxmsg porque tiene que verse sin LOGGING.
		La traduccion ya funciona para los accesos a datos, pero quedan dos
		agujeros conocidos y conviene que quien depure los tenga presentes.
	*/
	if (!ya_aviso)
	{
		fprintf(stderr,
			"mmu: MMUCR.AT=1 -- traduccion encendida, datos e instrucciones.\n"
			"     Sin traducir todavia: las store queues (fase 6). Ver\n"
			"     docs/mmu-plan.md.\n");

		ya_aviso = 1;
	}

	return 1;
}
