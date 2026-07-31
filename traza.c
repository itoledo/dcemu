/****************************************************************************

	TRAZA - ver traza.h.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

/* Para desensamblar el bucle donde se trabo y volcar los registros. traza.c
   solo se enlaza en el emulador, nunca en las pruebas. */
#include "main.h"
#include "debug.h"
#include "sh4emu.h"

#include "traza.h"
#include "tmu.h"
#include "opciones.h"
#include "mem.h"

int traza_activa = 0;

/* EXPERIMENTO: cuando es >0 se decrementa por instruccion y al llegar a cero
   vuelca el anillo. Sirve para mirar que hace el guest N instrucciones despues
   de algo -- por ejemplo despues de que la lectora le entregue un archivo. */
long traza_disparo = 0;

/* ------------------------------------------------------------------------ */
/* Caida del emulador                                                       */
/* ------------------------------------------------------------------------ */

/* Ver traza.h. El informe va aca y no en main.c porque lo que hay que rescatar
   -- el anillo, los registros, los rangos pedidos -- vive en este archivo. */

static int caida_en_curso = 0;

static void volcar_registros(void);

static void caida_informe(const char * causa, const char * detalle)
{
	/* Si el propio informe vuelve a caer ya no queda nada que rescatar, y
	   reentrar solo cambiaria una caida muda por un bucle. */
	if (caida_en_curso)
		return;

	caida_en_curso = 1;

	fprintf(stderr, "\n*** dcemu cayo: %s\n", causa);

	if (detalle)
		fprintf(stderr, "    %s\n", detalle);

	fprintf(stderr, "    Cayo el emulador, no el emulado. El PC de mas abajo es\n"
					"    donde iba el guest, que es por donde hay que empezar.\n");
	fflush(stderr);

	/* De lo mas barato a lo mas fragil, vaciando entre medio: si desensamblar
	   memoria rota provoca una segunda caida, lo importante ya salio. */
	volcar_registros();
	fflush(stderr);

	traza_volcar("caida del emulador");
	fflush(stderr);

	traza_rangos();
	fflush(stderr);
}

#ifdef WIN32

static const char * caida_nombre(DWORD codigo)
{
	switch (codigo)
	{
		case EXCEPTION_ACCESS_VIOLATION:		return "acceso invalido a memoria";
		case EXCEPTION_IN_PAGE_ERROR:			return "fallo de pagina";
		case EXCEPTION_DATATYPE_MISALIGNMENT:	return "acceso desalineado";
		case EXCEPTION_ILLEGAL_INSTRUCTION:		return "instruccion ilegal";
		case EXCEPTION_PRIV_INSTRUCTION:		return "instruccion privilegiada";
		case EXCEPTION_STACK_OVERFLOW:			return "desborde de pila";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:		return "division entera por cero";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:		return "division flotante por cero";
		default:								return "excepcion del anfitrion";
	}
}

static LONG WINAPI caida_filtro(EXCEPTION_POINTERS * info)
{
	char	causa[256];
	char	detalle[256];
	DWORD	codigo = info->ExceptionRecord->ExceptionCode;

	sprintf(causa, "%s (codigo %08lx) en %p del anfitrion",
		caida_nombre(codigo),
		(unsigned long) codigo,
		info->ExceptionRecord->ExceptionAddress);

	detalle[0] = '\0';

	/* Para un acceso invalido el primer parametro dice que se intentaba (0
	   leer, 1 escribir, 8 ejecutar) y el segundo sobre que direccion. */
	if ((codigo == EXCEPTION_ACCESS_VIOLATION || codigo == EXCEPTION_IN_PAGE_ERROR)
		&& info->ExceptionRecord->NumberParameters >= 2)
	{
		ULONG_PTR tipo = info->ExceptionRecord->ExceptionInformation[0];

		sprintf(detalle, "intento de %s sobre %p",
			(tipo == 0) ? "lectura" : (tipo == 1) ? "escritura" : "ejecucion",
			(void *) info->ExceptionRecord->ExceptionInformation[1]);
	}

	caida_informe(causa, detalle[0] ? detalle : NULL);

	return EXCEPTION_EXECUTE_HANDLER;
}

void traza_caida_instalar(void)
{
	SetUnhandledExceptionFilter(caida_filtro);
}

#else

#include <signal.h>

static void caida_senal(int sig)
{
	const char * causa;

	switch (sig)
	{
		case SIGSEGV:	causa = "acceso invalido a memoria (SIGSEGV)";		break;
		case SIGBUS:	causa = "acceso desalineado o invalido (SIGBUS)";	break;
		case SIGILL:	causa = "instruccion ilegal (SIGILL)";				break;
		case SIGFPE:	causa = "excepcion aritmetica (SIGFPE)";			break;
		default:		causa = "senal fatal";								break;
	}

	caida_informe(causa, NULL);

	/* Devolver la senal a su manejador por omision y relanzarla: asi el
	   sistema deja el core si esta configurado para dejarlo, y el codigo de
	   salida sigue diciendo que el proceso murio por una senal. */
	signal(sig, SIG_DFL);
	raise(sig);
}

void traza_caida_instalar(void)
{
	signal(SIGSEGV, caida_senal);
	signal(SIGBUS,  caida_senal);
	signal(SIGILL,  caida_senal);
	signal(SIGFPE,  caida_senal);
}

#endif

/* ------------------------------------------------------------------------ */
/* Direcciones sin emular                                                   */
/* ------------------------------------------------------------------------ */

/* Tabla abierta, potencia de dos. Si se llena se deja de deduplicar y se
   reporta todo, que es ruidoso pero no pierde informacion. */
#define TABLA_TAM	2048

struct entrada_t
{
	unsigned long	direccion;
	int				tipo;
	int				usada;
};

static struct entrada_t	tabla[TABLA_TAM];
static int				tabla_usadas = 0;

static int ya_vista(unsigned long direccion, int tipo)
{
	unsigned int	h = (unsigned int) ((direccion * 2654435761u) >> 11) & (TABLA_TAM - 1);
	int				i;

	for (i = 0; i < TABLA_TAM; i++)
	{
		struct entrada_t * e = &tabla[(h + i) & (TABLA_TAM - 1)];

		if (!e->usada)
		{
			if (tabla_usadas >= TABLA_TAM - 1)
				return 0;	/* tabla llena: no se anota, se reporta siempre */

			e->direccion	= direccion;
			e->tipo			= tipo;
			e->usada		= 1;
			tabla_usadas++;

			return 0;
		}

		if (e->direccion == direccion && e->tipo == tipo)
			return 1;
	}

	return 0;
}

void traza_acceso(int tipo, unsigned long direccion, size_t tam, DWORD pc)
{
	if (!traza_activa)
		return;

	if (ya_vista(direccion, tipo))
		return;

	fprintf(stderr, "traza: %s sin emular en %08lx (%d bytes) desde PC %08lx\n",
		(tipo == TRAZA_ESCRITURA) ? "escritura" : "lectura",
		direccion, (int) tam, (unsigned long) pc);
}

/* ------------------------------------------------------------------------ */
/* Rangos a pedido (--desensamblar, --volcar)                               */
/* ------------------------------------------------------------------------ */

/*
	El codigo del boot ROM que interesa vive en RAM -- se copia ahi al arrancar
	y no esta en bios.bin en la misma direccion --, asi que la unica forma de
	leerlo es desde adentro del emulador. Estas dos salidas son lo que convierte
	"se traba en 0x8C0D9C2A" en "esta esperando esto".

	Van fuera de traza_activa a proposito: pedir un desensamblado no obliga a
	arrastrar todo el ruido de --traza-mem.
*/
void traza_rangos(void)
{
	int i;

	for (i = 0; i < opciones.desensamblar_n; i++)
	{
		DWORD			dir = (DWORD) opciones.desensamblar[i].direccion;
		unsigned long	n   = opciones.desensamblar[i].cantidad;
		unsigned long	j;

		fprintf(stderr, "traza: desensamblado de %08lx, %lu instrucciones:\n",
			(unsigned long) dir, n);

		for (j = 0; j < n; j++)
		{
			char buffer[256];

			buffer[0] = '\0';
			disasm(dir, buffer);
			fprintf(stderr, "  %08lx: %s\n", (unsigned long) dir, buffer);

			dir += 2;
		}
	}

	for (i = 0; i < opciones.volcar_n; i++)
	{
		DWORD			dir = (DWORD) opciones.volcar[i].direccion;
		unsigned long	n   = opciones.volcar[i].cantidad;
		unsigned long	j;

		fprintf(stderr, "traza: volcado de %08lx, %lu bytes:\n",
			(unsigned long) dir, n);

		for (j = 0; j < n; j += 16)
		{
			unsigned long	k;
			unsigned long	fin = (n - j < 16) ? (n - j) : 16;

			fprintf(stderr, "  %08lx:", (unsigned long) (dir + j));

			for (k = 0; k < fin; k++)
			{
				BYTE b = 0;

				memread_fisico(dir + j + k, &b, 1);
				fprintf(stderr, " %02x", (unsigned) b);
			}

			fprintf(stderr, "\n");
		}
	}
}

void traza_resumen(void)
{
	traza_rangos();

	if (!traza_activa)
		return;

	fprintf(stderr, "traza: %d direcciones distintas sin emular.\n", tabla_usadas);

	/* Cuanta geometria trajeron las ultimas escenas. Lo de arriba reporta las
	   tres primeras, que es lo que sirve para arrancar; esto contesta la otra
	   pregunta -- si la ventana quedo con contenido o si el guest dejo de
	   mandar --, que es la que separa un parpadeo de una demo terminada. */
	traza_ta_resumen();

	/* Cuanto mas lento que una consola corrio el emulador. Antes no habia con
	   que compararlo. Ver docs/clock-plan.md, fase 4. */
	{
		unsigned long long emulado = reloj_ms();
		unsigned long real = SDL_GetTicks();

		if (emulado > 0 && real > 0)
			fprintf(stderr, "traza: %llu ms emulados en %lu ms reales (%.2fx)\n",
				emulado, real, (double) emulado / (double) real);
	}

	traza_volcar("al salir");
}

/* ------------------------------------------------------------------------ */
/* Anillo de PC                                                             */
/* ------------------------------------------------------------------------ */

#define ANILLO_TAM		96

/* Cada cuantas instrucciones se revisa si el PC sigue avanzando. Tiene que ser
   bastante mas que el anillo para no confundir un bucle normal con un halt. */
#define REVISION		4000000

/* Si en toda una revision el anillo tiene menos PC distintos que esto, el
   emulador esta dando vueltas en un bucle de espera. Se cuenta por PC
   distintos y no por rango de direcciones porque un bucle con una llamada
   adentro salta lejos y aun asi es un bucle. */
#define BUCLE_MAX		64

static DWORD	anillo[ANILLO_TAM];
static int		anillo_pos = 0;
static int		anillo_lleno = 0;

static unsigned long	pasos = 0;
static DWORD			atasco_reportado = 0xFFFFFFFF;

/* Los PC distintos del anillo, en orden de aparicion. Devuelve cuantos hay. */
static int pc_distintos(DWORD * dest, int max)
{
	int n = anillo_lleno ? ANILLO_TAM : anillo_pos;
	int desde = anillo_lleno ? anillo_pos : 0;
	int i, j, cantidad = 0;

	for (i = 0; i < n; i++)
	{
		DWORD pc = anillo[(desde + i) % ANILLO_TAM];

		for (j = 0; j < cantidad; j++)
			if (dest[j] == pc)
				break;

		if (j == cantidad)
		{
			if (cantidad >= max)
				return max + 1;		/* mas de los que caben: no es un bucle */

			dest[cantidad++] = pc;
		}
	}

	return cantidad;
}

static void volcar_registros(void)
{
	int i;

	fprintf(stderr, "traza: SR=%08lx PR=%08lx\n",
		(unsigned long) SR, (unsigned long) PR);

	for (i = 0; i < 16; i++)
	{
		fprintf(stderr, " r%-2d=%08lx", i, (unsigned long) R(i));

		if ((i % 4) == 3)
			fprintf(stderr, "\n");
	}
}

void traza_volcar(const char * motivo)
{
	/* El volcado a pedido desensambla todo lo que haya en el anillo, no solo
	   los bucles chicos: un bucle de espera con llamadas adentro pasa de
	   BUCLE_MAX y es justo el que hay que poder mirar. */
	DWORD	distintos[ANILLO_TAM];
	int		i, n, desde, cantidad;

	if (!traza_activa)
		return;

	n = anillo_lleno ? ANILLO_TAM : anillo_pos;

	if (n == 0)
		return;

	/* Con el tiempo emulado al lado, dos corridas se pueden comparar: "se trabo
	   a los 3,4 s" dice mucho mas que "a los cuatro millones de instrucciones".
	   Ver docs/clock-plan.md, fase 4. */
	fprintf(stderr, "traza: %s, a los %llu.%03llu s de tiempo emulado. Ultimos %d PC:\n",
		motivo,
		(unsigned long long) (reloj_ms() / 1000),
		(unsigned long long) (reloj_ms() % 1000),
		n);

	desde = anillo_lleno ? anillo_pos : 0;

	for (i = 0; i < n; i++)
	{
		fprintf(stderr, " %08lx", (unsigned long) anillo[(desde + i) % ANILLO_TAM]);

		if ((i % 8) == 7)
			fprintf(stderr, "\n");
	}

	if ((n % 8) != 0)
		fprintf(stderr, "\n");

	/* Desensamblarlos es la diferencia entre "se traba aca" y "se traba
	   esperando esto". */
	cantidad = pc_distintos(distintos, ANILLO_TAM);

	{
		char buffer[256];

		fprintf(stderr, "traza: %d instrucciones distintas:\n", cantidad);

		for (i = 0; i < cantidad; i++)
		{
			buffer[0] = '\0';
			disasm(distintos[i], buffer);
			fprintf(stderr, "  %08lx: %s\n", (unsigned long) distintos[i], buffer);
		}
	}

	volcar_registros();
}

void traza_paso(DWORD pc)
{
	if (traza_disparo > 0 && --traza_disparo == 0)
		traza_volcar("disparo");

	DWORD	distintos[BUCLE_MAX];
	int		cantidad;

	anillo[anillo_pos] = pc;
	anillo_pos = (anillo_pos + 1) % ANILLO_TAM;

	if (anillo_pos == 0)
		anillo_lleno = 1;

	if (++pasos < REVISION)
		return;

	pasos = 0;

	if (!anillo_lleno)
		return;

	cantidad = pc_distintos(distintos, BUCLE_MAX);

	if (cantidad > BUCLE_MAX)
	{
		atasco_reportado = 0xFFFFFFFF;
		return;
	}

	/* Solo se reporta cada bucle nuevo. La clave es el PC mas bajo del bucle y
	   no el primero del anillo, que cambia segun donde caiga el corte. */
	{
		DWORD	menor = distintos[0];
		int		i;

		for (i = 1; i < cantidad; i++)
			if (distintos[i] < menor)
				menor = distintos[i];

		if (atasco_reportado != menor)
		{
			char buf[128];

			atasco_reportado = menor;

			sprintf(buf, "el PC lleva %d instrucciones en un bucle de %d",
				REVISION, cantidad);
			traza_volcar(buf);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* Watchpoint de escritura                                                  */
/* ------------------------------------------------------------------------ */

/*
	Lo llama el gancho de memwrite_fisico() en mem.h, despues de escribir.

	El valor anterior no se lee antes de cada escritura: se recuerda el de la
	vez pasada. Asi la funcion no tiene que partirse en dos ni llevar estado a
	traves de la escritura, que ademas puede reentrar -- escribir un registro
	del PVR dispara el DMA del Maple, que vuelve a escribir memoria.
*/

unsigned long	watchpoint_dir = 0;		/* 0: apagado. Lo pone --watchpoint= */
size_t			watchpoint_tam = 4;

static DWORD	wp_anterior = 0;
static int		wp_arrancado = 0;
static int		wp_informes = 0;

void watchpoint_escritura(unsigned long direccion, size_t tam)
{
	DWORD escrita  = (DWORD) direccion & 0x1FFFFFFFu;
	DWORD vigilada = (DWORD) watchpoint_dir & 0x1FFFFFFFu;
	DWORD ahora    = 0;

	/* Solapamiento de rangos: escribir un byte dentro de la palabra vigilada
	   tambien cuenta. */
	if (escrita + tam <= vigilada || escrita >= vigilada + watchpoint_tam)
		return;

	if (wp_informes >= WATCHPOINT_MAX)
		return;

	memread_fisico(watchpoint_dir, &ahora, watchpoint_tam);

	/* La primera vez no hay con que comparar: se toma como valor de partida. */
	if (!wp_arrancado)
	{
		wp_arrancado = 1;
		wp_anterior  = ahora;
	}

#ifdef WATCHPOINT_SOLO_CAMBIOS
	if (ahora == wp_anterior)
		return;
#endif

	fprintf(stderr,
		"watchpoint: %08lx = %08lx (antes %08lx)%s"
		" -- escritura de %u en %08lx, PC %08lx, PR %08lx, %llu ciclos\n",
		(unsigned long) watchpoint_dir,
		(unsigned long) ahora,
		(unsigned long) wp_anterior,
		(ahora == wp_anterior) ? ", sin cambio" : "",
		(unsigned) tam,
		(unsigned long) direccion,
		(unsigned long) PC,
		(unsigned long) PR,
		(unsigned long long) reloj_total);

#ifdef WATCHPOINT_ANILLO
	traza_volcar("watchpoint");
#endif

	wp_anterior = ahora;

	if (++wp_informes >= WATCHPOINT_MAX)
		fprintf(stderr, "watchpoint: %d informes, no se reporta mas\n",
			WATCHPOINT_MAX);
}
