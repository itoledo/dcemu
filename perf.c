/****************************************************************************

	PERF - el desglose de en que se va el tiempo real

	Ver perf.h para el porque. Aqui solo esta el reloj del anfitrion y el
	informe.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>			/* calloc, qsort: solo para la tabla de bloques */

#include "perf.h"
#ifdef DCEMU_BLOQUES
#include "bloques.h"
#endif
#include "tmu.h"			/* reloj_total, reloj_ms() */
#include "arm7.h"			/* los contadores de la memoizacion de barridos */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

int perf_activa = 0;

unsigned long long perf_ns_aica		= 0;
unsigned long long perf_ns_arm		= 0;
unsigned long long perf_ns_escena	= 0;
unsigned long long perf_ns_textura	= 0;
unsigned long long perf_ns_cuadro	= 0;
unsigned long long perf_ns_orden	= 0;
unsigned long long perf_ns_presentar= 0;
unsigned long long perf_ns_servicio	= 0;
unsigned long long perf_ns_ta		= 0;

unsigned long long perf_arm_pasos	= 0;
unsigned long long perf_arm_ocioso	= 0;

unsigned long long perf_ns_captura	= 0;

unsigned long long perf_escenas		= 0;
unsigned long long perf_tiras		= 0;
unsigned long      perf_tiras_max	= 0;
unsigned long      perf_vertices_max= 0;

unsigned long long perf_sync_instantes = 0;
unsigned long long perf_ns_espera	= 0;

unsigned long long perf_mmu_traduce			= 0;
unsigned long long perf_mmu_utlb			= 0;
unsigned long long perf_mmu_utlb_pasos		= 0;
unsigned long long perf_mmu_cache_acierto	= 0;
unsigned long long perf_mmu_datos_acierto	= 0;
unsigned long long perf_mmu_vaciados		= 0;
unsigned long long perf_mmu_datos_choque	= 0;
unsigned long long perf_mmu_datos_capacidad	= 0;
unsigned long long perf_mmu_datos_vacia		= 0;
unsigned long long perf_mmu_datos_sin_trad	= 0;
unsigned long long perf_mmu_fetch_acierto2	= 0;
unsigned long long perf_mmu_fetch_fallo		= 0;
unsigned long long perf_mmu_falta			= 0;
unsigned long long perf_instantaneas		= 0;
unsigned long long perf_instantaneas_usadas	= 0;
unsigned long long perf_instantaneas_elididas = 0;
unsigned long long perf_ns_traducir			= 0;
unsigned long long perf_ns_instantanea		= 0;

/*
	Un acceso al estado del AICA. Solo cuenta si el reloj emulado avanzo desde
	el anterior: si no avanzo, el hilo del AICA ya estaba al dia y no habria
	espera. Ver perf.h.

	reloj_total avanza en el bloque periodico de main_loop(), o sea cada 50
	ciclos, que es exactamente la resolucion con la que el hilo del AICA
	recibiria su objetivo. Asi que la deduplicacion es la correcta, no una
	aproximacion.
*/
void perf_marcar_sync(void)
{
	static unsigned long long ultimo = (unsigned long long) -1;

	if (reloj_total == ultimo)
		return;

	ultimo = reloj_total;
	perf_sync_instantes++;
}

unsigned long long perf_aica_reg_vivo	= 0;
unsigned long long perf_aica_reg_plano	= 0;
unsigned long long perf_aica_reg_escr	= 0;
unsigned long long perf_onda_lect		= 0;
unsigned long long perf_onda_escr		= 0;

/* El censo por paginas de la RAM de onda. Ver perf.h. */
int                perf_sonda_onda		= 0;
unsigned long long perf_onda_pag_lect[PERF_ONDA_PAGS];
unsigned long long perf_onda_pag_escr[PERF_ONDA_PAGS];
unsigned long long perf_onda_arm_reg_lect	= 0;
unsigned long long perf_onda_arm_dato_lect	= 0;

unsigned long      perf_onda_pag_gen[PERF_ONDA_PAGS];
unsigned long      perf_onda_pag_gen_lect[PERF_ONDA_PAGS];
unsigned long long perf_onda_lect_sin_cambio = 0;

void perf_onda_marcar_lectura(unsigned long dir)
{
	unsigned long f = (dir & 0x001FFFFFu) >> PERF_ONDA_PAG_BITS;

	perf_onda_pag_lect[f]++;
	perf_onda_arm_dato_lect++;

	/* La generacion de la pagina no cambio desde la lectura anterior: releerla
	   habria dado lo mismo. Es la tasa de acierto que tendria el contador de
	   generacion, medida antes de escribirlo. */
	if (perf_onda_pag_gen[f] == perf_onda_pag_gen_lect[f])
		perf_onda_lect_sin_cambio++;
	else
		perf_onda_pag_gen_lect[f] = perf_onda_pag_gen[f];
}

void perf_onda_marcar_escritura(unsigned long dir, unsigned long n)
{
	unsigned long a = dir & 0x001FFFFFu;
	unsigned long f = a >> PERF_ONDA_PAG_BITS;
	unsigned long u = (a + (n ? n - 1 : 0)) >> PERF_ONDA_PAG_BITS;

	/* Un memcpy del DMA cruza paginas: se marcan todas las que toca. Si no, un
	   bloque grande contaria solo por donde empieza y el mapa diria que hay
	   paginas limpias que no lo estan -- que es exactamente el error que
	   volveria segura una elision que no lo es. */
	if (u >= PERF_ONDA_PAGS)
		u = PERF_ONDA_PAGS - 1;

	while (f <= u)
	{
		perf_onda_pag_escr[f]++;
		perf_onda_pag_gen[f]++;
		f++;
	}
}

unsigned long long perf_cuadros			= 0;
unsigned long long perf_instrucciones	= 0;
unsigned long long perf_inline_si		= 0;
unsigned long long perf_inline_no		= 0;

unsigned long long perf_tex_acierto		= 0;
unsigned long long perf_tex_regenera	= 0;
unsigned long long perf_tex_nueva		= 0;
unsigned long long perf_tex_desalojo	= 0;

unsigned long long perf_tiras_dibujadas	= 0;
unsigned long long perf_tiras_sin_cambio= 0;

/* ------------------------------------------------------------------------ */
/* La forma de ejecucion del guest. Ver perf.h por que estas cifras y no otras. */

int                perf_forma			= 0;
unsigned long long perf_bloques			= 0;
unsigned long long perf_bloques_instr	= 0;
unsigned long      perf_bloques_max		= 0;
unsigned long long perf_bloques_perdidos= 0;

/* Longitudes: 1, 2, 3, 4, 5-8, 9-16, 17-32, 33 o mas. */
#define PERF_BLOQ_CUBOS		8
static unsigned long long bloq_histo[PERF_BLOQ_CUBOS] = { 0 };

/*
	Los bloques distintos, en una tabla de direccion abierta con sondeo lineal
	y **tope de sondeos**: perder un bloque es una imprecision del instrumento
	--que se informa-- y no vale la pena pagar un recorrido largo por el, porque
	esto corre una vez por corrida secuencial del guest.

	Se reserva al primer uso y solo con --perf: son 6 MB que un binario sin
	medir no toca.
*/
#define PERF_BLOQ_BITS		18
#define PERF_BLOQ_TAM		(1UL << PERF_BLOQ_BITS)
#define PERF_BLOQ_MASC		(PERF_BLOQ_TAM - 1)
#define PERF_BLOQ_SONDEOS	8

struct bloque_entrada
{
	unsigned long		pc;
	unsigned long long	veces;
	unsigned long long	instr;
};

static struct bloque_entrada * bloq_tabla  = NULL;
static unsigned long           bloq_usados = 0;
static int                     bloq_sin_sitio = 0;

/* Knuth multiplicativo sobre el PC en unidades de instruccion: los bloques
   estan alineados a 2 y usar el bit 0 desperdiciaria la mitad de la tabla. */
static unsigned long bloq_hash(unsigned long pc)
{
	unsigned int h = (unsigned int) (pc >> 1) * 2654435761u;

	return (unsigned long) (h >> (32 - PERF_BLOQ_BITS));
}

static void bloque_registrar(unsigned long pc, unsigned long largo)
{
	unsigned long k, i;

	if (bloq_tabla == NULL)
	{
		if (bloq_sin_sitio)
		{
			perf_bloques_perdidos++;
			return;
		}

		bloq_tabla = (struct bloque_entrada *)
			calloc(PERF_BLOQ_TAM, sizeof(struct bloque_entrada));

		if (bloq_tabla == NULL)
		{
			bloq_sin_sitio = 1;
			perf_bloques_perdidos++;
			return;
		}
	}

	k = bloq_hash(pc);

	for (i = 0; i < PERF_BLOQ_SONDEOS; i++)
	{
		struct bloque_entrada * e = &bloq_tabla[(k + i) & PERF_BLOQ_MASC];

		if (e->veces == 0)
		{
			e->pc = pc;
			bloq_usados++;
		}
		else if (e->pc != pc)
			continue;

		e->veces++;
		e->instr += largo;
		return;
	}

	perf_bloques_perdidos++;
}

static void bloque_cerrar(unsigned long pc, unsigned long largo)
{
	unsigned long cubo;

	perf_bloques++;
	perf_bloques_instr += largo;

	if (largo > perf_bloques_max)
		perf_bloques_max = largo;

	     if (largo <=  4)	cubo = largo - 1;
	else if (largo <=  8)	cubo = 4;
	else if (largo <= 16)	cubo = 5;
	else if (largo <= 32)	cubo = 6;
	else					cubo = 7;

	bloq_histo[cubo]++;

	bloque_registrar(pc, largo);
}

/*
	Se llama despues de cada despacho del bucle exterior, con el PC resultante.

	No hace falta el PC de entrada: el de salida de la vuelta anterior **es** la
	direccion de la instruccion que se acaba de ejecutar, asi que `esperado`
	--que se dejo puesto entonces-- ya vale esa direccion mas dos. Si el PC de
	ahora coincide, la instruccion fue secuencial y la corrida sigue; si no,
	hubo salto y la corrida se cierra.

	La primera vuelta solo siembra el estado: se pierde una instruccion de
	22 mil millones.
*/
void perf_bloque_paso(unsigned long pc)
{
	static unsigned long esperado  = 0;
	static unsigned long inicio    = 0;
	static unsigned long largo     = 0;
	static int           arrancado = 0;

	if (!arrancado)
	{
		arrancado = 1;
		inicio    = pc;
		esperado  = pc + 2;
		return;
	}

	largo++;

	if (pc != esperado)
	{
		bloque_cerrar(inicio, largo);
		inicio = pc;
		largo  = 0;
	}

	esperado = pc + 2;
}

/*
	Ordena por **instrucciones ejecutadas**, no por veces.

	Pesar por veces contesta otra pregunta y contesta mal la de aqui: un lazo de
	espera de una sola instruccion --un `bra` a si mismo, un SLEEP-- se ejecuta
	millones de veces y no es donde se va el tiempo. Con el peso por veces Crazy
	Taxi informaba que **4 bloques cubren el 50 %**, que es una cifra sobre el
	sondeo y no sobre el trabajo.

	Lo que decide el tamano de un cache de codigo es cuantos bloques hay que
	tener traducidos para cubrir el 90 % de las instrucciones que se ejecutan.
	Es el mismo error de denominador que costo tres hipotesis en la fase 6 --ver
	el comentario de `datos_acierto` en perf.h--, asi que va dicho aqui tambien.
*/
static int bloque_cmp(const void * a, const void * b)
{
	const struct bloque_entrada * x = (const struct bloque_entrada *) a;
	const struct bloque_entrada * y = (const struct bloque_entrada *) b;

	if (x->instr > y->instr)	return -1;
	if (x->instr < y->instr)	return  1;
	return 0;
}

static unsigned long long arranque = 0;

/*
	El reloj. QueryPerformanceCounter en Windows y CLOCK_MONOTONIC fuera; los
	dos son monotonos y de resolucion muy por debajo del microsegundo, que es
	lo que hace falta para medir una mezcla de muestra.

	La frecuencia de QPC se pregunta una sola vez: es fija desde el arranque
	del sistema y preguntarla en cada llamada seria la mitad del costo.
*/
unsigned long long perf_ahora(void)
{
#ifdef _WIN32
	static LARGE_INTEGER frec = { 0 };
	LARGE_INTEGER ahora;

	if (frec.QuadPart == 0)
		QueryPerformanceFrequency(&frec);

	QueryPerformanceCounter(&ahora);

	/* Se separa en segundos y resto para no desbordar al multiplicar por mil
	   millones: el contador crudo ya es grande. */
	return (unsigned long long) (ahora.QuadPart / frec.QuadPart) * 1000000000ULL
	     + (unsigned long long) (ahora.QuadPart % frec.QuadPart) * 1000000000ULL
	       / (unsigned long long) frec.QuadPart;
#else
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	return (unsigned long long) t.tv_sec * 1000000000ULL
	     + (unsigned long long) t.tv_nsec;
#endif
}

void perf_inicio(void)
{
	const char * v;

	/* Antes de la salida temprana: se lee una vez al arrancar, como el resto de
	   las sondas del arbol, y nunca en el camino caliente. Ver perf.h por que
	   no cuelga de --perf. */
	v = getenv("DCEMU_FORMA");
	perf_forma = (v != NULL && atoi(v) != 0);

	/* Tambien fuera de --perf y por el mismo motivo: es una sonda, no una
	   medicion de tiempo. Contesta si el sondeo del ARM se puede saltear. */
	v = getenv("DCEMU_SONDA_ONDA");
	perf_sonda_onda = (v != NULL && atoi(v) != 0);

	if (!perf_activa)
		return;

	if (perf_forma)
		fprintf(stderr, "perf: midiendo la forma de ejecucion\n");

	if (perf_sonda_onda)
		fprintf(stderr, "perf: censo por paginas de la RAM de onda\n");

	arranque = perf_ahora();
}

static void linea(const char * que, unsigned long long ns, unsigned long long total)
{
	fprintf(stderr, "perf:   %-22s %8.0f ms  %5.1f %%\n",
		que, (double) ns / 1e6,
		total ? 100.0 * (double) ns / (double) total : 0.0);
}

void perf_resumen(void)
{
	unsigned long long real, emulado;
	unsigned long long aica_total;

	if (!perf_activa || arranque == 0)
		return;

	real    = perf_ahora() - arranque;
	emulado = reloj_ms();

	fprintf(stderr, "\nperf: %.0f ms reales, %llu ms emulados (%.2fx)\n",
		(double) real / 1e6, emulado,
		real ? (double) emulado * 1e6 / (double) real : 0.0);

	if (perf_cuadros)
		fprintf(stderr, "perf: %llu cuadros presentados (%.1f por segundo real)\n",
			perf_cuadros, (double) perf_cuadros * 1e9 / (double) real);

	/*
		La unidad con la que se compara una optimizacion del despacho contra
		otra. Los ciclos emulados no sirven para eso -- ver perf.h --, y los
		MIPS solos tampoco dicen nada sin el ritmo al que la consola los pedia.
		El SH-4 de la Dreamcast retira del orden de una instruccion por ciclo a
		200 MHz, asi que 200 MIPS es aproximadamente el tiempo real.
	*/
	if (perf_instrucciones)
		fprintf(stderr, "perf: %llu instrucciones, %.1f ns cada una"
			" (%.1f MIPS, %.2f por ciclo emulado)\n",
			perf_instrucciones,
			(double) real / (double) perf_instrucciones,
			(double) perf_instrucciones * 1e3 / (double) real,
			reloj_total ? (double) perf_instrucciones / (double) reloj_total : 0.0);

	/* Solo con -DDCEMU_INLINE. La fraccion cubierta es lo que permite
	   extrapolar el costo de la llamada indirecta a todas las instrucciones. */
	if (perf_inline_si + perf_inline_no)
		fprintf(stderr, "perf: despacho en linea: %llu de %llu instrucciones"
			" (%.1f %%)\n",
			perf_inline_si, perf_inline_si + perf_inline_no,
			100.0 * (double) perf_inline_si
				/ (double) (perf_inline_si + perf_inline_no));

	fprintf(stderr, "perf: reparto del tiempo real\n");

	aica_total = perf_ns_aica + perf_ns_arm;

	linea("AICA (mezcla)",		perf_ns_aica,		real);
	linea("AICA (ARM7)",		perf_ns_arm,		real);
	linea("  AICA total",		aica_total,			real);
	linea("cuadro (cb_tastart)",perf_ns_cuadro,		real);
	linea("  de eso ordenar",	perf_ns_orden,		real);
	linea("  de eso escena",	perf_ns_escena,		real);
	linea("    de eso texturas",perf_ns_textura,	real);
	linea("  de eso presentar",	perf_ns_presentar,	real);
	linea("bloque periodico",	perf_ns_servicio,	real);
	linea("TA (store queue)",	perf_ns_ta,			real);

	if (perf_ns_captura)
		linea("--captura-gl",	perf_ns_captura,	real);

	if (perf_ns_espera)
		linea("esperando al AICA",	perf_ns_espera,	real);

	/* Lo que queda es el interprete del SH-4 y el andamiaje de main_loop().
	   El bloque periodico y el AICA estan anidados, asi que no se restan dos
	   veces: perf_ns_servicio ya incluye a aica_total. Del lado grafico el que
	   se resta es perf_ns_cuadro, que contiene a escena, presentar y captura --
	   y ademas la ordenacion, que antes no la contaba nadie. */
	{
		unsigned long long medido = perf_ns_servicio + perf_ns_cuadro
		                          + perf_ns_ta;

		linea("resto (interprete)",
			real > medido ? real - medido : 0, real);
	}

	/* La cache de texturas. Ver perf.h: las tres cifras piden acciones
	   opuestas, asi que van juntas o no sirven. */
	{
		unsigned long long tex = perf_tex_acierto + perf_tex_regenera
		                       + perf_tex_nueva;

		if (tex)
			fprintf(stderr, "perf: texturas: %llu pedidas, %llu aciertos"
				" (%.1f %%), %llu regeneradas, %llu nuevas, %llu desalojos\n",
				tex, perf_tex_acierto,
				100.0 * (double) perf_tex_acierto / (double) tex,
				perf_tex_regenera, perf_tex_nueva, perf_tex_desalojo);

		if (tex && perf_escenas)
			fprintf(stderr, "perf:   por escena: %.0f pedidas, %.0f subidas\n",
				(double) tex / (double) perf_escenas,
				(double) (perf_tex_regenera + perf_tex_nueva)
				/ (double) perf_escenas);
	}

	if (perf_tiras_dibujadas)
		fprintf(stderr, "perf: %llu llamadas de dibujo, %llu sin cambio de estado"
			" (%.1f %%, o sea %.2f tiras por lote si se agruparan)\n",
			perf_tiras_dibujadas, perf_tiras_sin_cambio,
			100.0 * (double) perf_tiras_sin_cambio / (double) perf_tiras_dibujadas,
			perf_tiras_dibujadas > perf_tiras_sin_cambio
				? (double) perf_tiras_dibujadas
				  / (double) (perf_tiras_dibujadas - perf_tiras_sin_cambio)
				: 0.0);

	if (perf_escenas)
		fprintf(stderr, "perf: %llu escenas, %.0f tiras por escena en promedio,"
			" %lu la mayor\n",
			perf_escenas, (double) perf_tiras / (double) perf_escenas,
			perf_tiras_max);

	if (perf_vertices_max)
		fprintf(stderr, "perf: pico de vertices pedidos por una escena: %lu\n",
			perf_vertices_max);

	if (perf_arm_pasos)
		fprintf(stderr, "perf: ARM7: %llu pasos, %llu ociosos (%.1f %%)\n",
			perf_arm_pasos, perf_arm_ocioso,
			100.0 * (double) perf_arm_ocioso / (double) perf_arm_pasos);

#ifdef DCEMU_BLOQUES
	bloques_resumen();
#endif

	/*
		La forma de ejecucion. Es lo que decide si un cache de bloques o un
		recompilador pueden pagar, y las dos mitades --amortizacion y
		reincidencia-- van juntas: una corrida larga que nunca se repite no
		sirve, y una corrida de una instruccion repetida mil veces tampoco.
	*/
	if (perf_bloques)
	{
		fprintf(stderr, "perf: forma de ejecucion\n");

		fprintf(stderr, "perf:   corridas             %12llu,"
			" %.2f despachos cada una (la mayor %lu)\n",
			perf_bloques,
			(double) perf_bloques_instr / (double) perf_bloques,
			perf_bloques_max);

		/* Con las ranuras de retardo, que se ejecutan anidadas y no dan vuelta
		   del bucle pero si formarian parte del bloque traducido. */
		if (perf_instrucciones)
			fprintf(stderr, "perf:   ... con las ranuras  %15.2f"
				" instrucciones por bloque\n",
				(double) perf_instrucciones / (double) perf_bloques);

		fprintf(stderr, "perf:   longitudes  1:%.1f%%  2:%.1f%%  3:%.1f%%"
			"  4:%.1f%%  5-8:%.1f%%  9-16:%.1f%%  17-32:%.1f%%  33+:%.1f%%\n",
			100.0 * (double) bloq_histo[0] / (double) perf_bloques,
			100.0 * (double) bloq_histo[1] / (double) perf_bloques,
			100.0 * (double) bloq_histo[2] / (double) perf_bloques,
			100.0 * (double) bloq_histo[3] / (double) perf_bloques,
			100.0 * (double) bloq_histo[4] / (double) perf_bloques,
			100.0 * (double) bloq_histo[5] / (double) perf_bloques,
			100.0 * (double) bloq_histo[6] / (double) perf_bloques,
			100.0 * (double) bloq_histo[7] / (double) perf_bloques);

		if (bloq_tabla != NULL)
		{
			unsigned long long acum = 0;
			unsigned long i, n50 = 0, n90 = 0, n99 = 0;

			/* Los vacios tienen instr 0 y quedan al final solos. */
			qsort(bloq_tabla, PERF_BLOQ_TAM, sizeof(struct bloque_entrada),
				bloque_cmp);

			for (i = 0; i < bloq_usados; i++)
			{
				acum += bloq_tabla[i].instr;

				if (!n50 && acum * 2   >= perf_bloques_instr)		n50 = i + 1;
				if (!n90 && acum * 10  >= perf_bloques_instr * 9)	n90 = i + 1;
				if (!n99 && acum * 100 >= perf_bloques_instr * 99)	n99 = i + 1;
			}

			fprintf(stderr, "perf:   bloques distintos    %12lu,"
				" %.1f ejecuciones cada uno\n",
				bloq_usados,
				bloq_usados ? (double) perf_bloques / (double) bloq_usados : 0.0);

			/* El tamano que tendria que tener el cache de codigo. Pesado por
			   **instrucciones**, no por veces: ver bloque_cmp(). */
			fprintf(stderr, "perf:   cubren el 50/90/99 %% de las instrucciones:"
				" %lu / %lu / %lu bloques\n", n50, n90, n99);

			/* Los bloques con nombre, que es lo que el prototipo de la fase 4
			   de rendimiento-plan-2.md necesita: PC de entrada, largo medio y
			   peso. La tabla ya quedo ordenada por instrucciones. */
			for (i = 0; i < 16 && i < bloq_usados; i++)
				fprintf(stderr, "perf:   bloque %2lu: PC %08lx, largo medio"
					" %5.1f, %12llu veces, %5.2f %% de las instrucciones\n",
					i + 1, bloq_tabla[i].pc,
					(double) bloq_tabla[i].instr / (double) bloq_tabla[i].veces,
					bloq_tabla[i].veces,
					100.0 * (double) bloq_tabla[i].instr
						  / (double) perf_bloques_instr);
		}

		/* Un bloque perdido no falsea las corridas --esas se cuentan aparte--,
		   solo subestima los distintos. Se informa para poder descartarlo. */
		if (perf_bloques_perdidos)
			fprintf(stderr, "perf:   ... %llu corridas no entraron en la tabla"
				" (%.3f %%)\n",
				perf_bloques_perdidos,
				100.0 * (double) perf_bloques_perdidos / (double) perf_bloques);
	}

	/*
		La MMU. Solo sale si el guest la encendio alguna vez, porque en todo lo
		demas del arbol estas lineas serian seis ceros. Ver perf.h.
	*/
	if (perf_instantaneas || perf_instantaneas_elididas || perf_mmu_traduce)
	{
		fprintf(stderr, "perf: MMU\n");

		fprintf(stderr, "perf:   instantaneas         %12llu"
			" (%.2f por instruccion)\n",
			perf_instantaneas,
			perf_instrucciones
				? (double) perf_instantaneas / (double) perf_instrucciones : 0.0);

		/* La razon de trabajo util a trabajo tirado: la instantanea existe
		   para poder deshacer, y esto dice cuantas veces hubo que deshacer. */
		fprintf(stderr, "perf:   ... restauradas      %12llu"
			" (1 de cada %.0f)\n",
			perf_instantaneas_usadas,
			perf_instantaneas_usadas
				? (double) perf_instantaneas / (double) perf_instantaneas_usadas
				: 0.0);

		/* La elision (fase 1 de rendimiento-plan-2.md): instrucciones cuyo
		   manejador auditado no puede abortar, asi que la copia se salteo. */
		fprintf(stderr, "perf:   ... elididas         %12llu"
			" (%.2f por instruccion)\n",
			perf_instantaneas_elididas,
			perf_instrucciones
				? (double) perf_instantaneas_elididas
				  / (double) perf_instrucciones : 0.0);

		/* Aciertos = instrucciones - fallos: el acierto no se cuenta porque
		   vive en el camino de cada instruccion. Ver mmu_fetch_resolver(). */
		fprintf(stderr, "perf:   busqueda: fallos     %12llu (%.3f %% de las"
			" instrucciones), %.1f %% los atiende la de 64\n",
			perf_mmu_fetch_fallo,
			perf_instrucciones
				? 100.0 * (double) perf_mmu_fetch_fallo
				  / (double) perf_instrucciones : 0.0,
			perf_mmu_fetch_fallo
				? 100.0 * (double) perf_mmu_fetch_acierto2
				  / (double) perf_mmu_fetch_fallo : 0.0);

		fprintf(stderr, "perf:   datos: traducciones  %12llu"
			" (%.2f por instruccion), %.1f %% ya resueltas, %llu faltas\n",
			perf_mmu_traduce,
			perf_instrucciones
				? (double) perf_mmu_traduce / (double) perf_instrucciones : 0.0,
			perf_mmu_traduce
				? 100.0 * (double) perf_mmu_datos_acierto
				  / (double) perf_mmu_traduce : 0.0,
			perf_mmu_falta);

		/* Las dos piezas del sobrecosto, cronometradas por muestreo. Contra
		   los ~5,5 ns por instruccion de un guest sin MMU, esto dice cuanto
		   del resto queda por atacar y en cual de las dos. */
		fprintf(stderr, "perf:   instantanea          %10.0f ms  %5.1f %%"
			"   traducir %.0f ms  %.1f %%\n",
			(double) perf_ns_instantanea / 1e6,
			100.0 * (double) perf_ns_instantanea / (double) real,
			(double) perf_ns_traducir / 1e6,
			100.0 * (double) perf_ns_traducir / (double) real);

		/* Cada vaciado tira las tres cachas enteras. Si son frecuentes, los
		   fallos no son de tamano sino obligatorios y agrandar no sirve. */
		fprintf(stderr, "perf:   vaciados completos   %12llu"
			" (1 cada %.0f traducciones)\n",
			perf_mmu_vaciados,
			perf_mmu_vaciados
				? (double) perf_mmu_traduce / (double) perf_mmu_vaciados : 0.0);

		/* De que tipo son los fallos de esa cache: la respuesta es
		   asociatividad o tamano, y son cosas distintas. Ver mmu.c. */
		if (perf_mmu_datos_choque + perf_mmu_datos_capacidad
			+ perf_mmu_datos_vacia)
		{
			unsigned long long f = perf_mmu_datos_choque
				+ perf_mmu_datos_capacidad + perf_mmu_datos_vacia;

			fprintf(stderr, "perf:   ... de los fallos, %.1f %% misma pagina"
				" con otra etiqueta (modo o ASID), %.1f %% otra pagina,"
				" %.1f %% ranura sin estrenar\n",
				100.0 * (double) perf_mmu_datos_choque    / (double) f,
				100.0 * (double) perf_mmu_datos_capacidad / (double) f,
				100.0 * (double) perf_mmu_datos_vacia     / (double) f);

			fprintf(stderr, "perf:   ... y %.1f %% de los fallos son"
				" direcciones que NO se traducen (P1/P2/P4)\n",
				100.0 * (double) perf_mmu_datos_sin_trad / (double) f);
		}

		/*
			La cache de traduccion y lo que queda del recorrido detras de
			ella. El recorrido medio cuenta el acierto como 1, asi que sube
			apenas la cache empieza a fallar: es la cifra que avisa si el
			tamano se quedo chico para otro guest.

			Las busquedas son las de los TRES caminos --datos, instrucciones y
			store queues--, que comparten la cache; por eso son mas que las
			traducciones de datos de la linea de arriba.
		*/
		if (perf_mmu_utlb)
			fprintf(stderr, "perf:   busquedas de entrada %12llu,"
				" %.2f %% aciertan la cache, recorrido medio %.1f de 64\n",
				perf_mmu_utlb,
				100.0 * (double) perf_mmu_cache_acierto
					/ (double) perf_mmu_utlb,
				(double) perf_mmu_utlb_pasos / (double) perf_mmu_utlb);
	}

	/*
		El techo de la fase 1 de docs/hilos-plan.md. No es el porcentaje de
		AICA a secas: sacar un trabajo a otro hilo solo devuelve tiempo si el
		hilo que queda tiene con que llenarlo, y ademas hay que descontar lo
		que se pierda sincronizando. Es una cota superior.
	*/
	if (real)
		fprintf(stderr, "perf: techo de sacar el AICA a otro hilo: %.2fx"
			" (de %.2fx a %.2fx)\n",
			1.0 / (1.0 - (double) aica_total / (double) real),
			(double) emulado * 1e6 / (double) real,
			(double) emulado * 1e6 / (double) (real - aica_total));

	/*
		Y el otro numero del paso 0: con que frecuencia el SH-4 toca el estado
		del AICA. Cada uno de estos seria un alcance forzado, o sea una espera
		del hilo principal.
	*/
	{
		unsigned long long sync = perf_aica_reg_vivo + perf_aica_reg_escr
		                        + perf_onda_lect + perf_onda_escr;
		double seg = (double) real / 1e9;

		fprintf(stderr, "perf: accesos del SH-4 al AICA\n");
		fprintf(stderr, "perf:   registro vivo        %10llu  (%.0f/s)\n",
			perf_aica_reg_vivo,  seg ? perf_aica_reg_vivo / seg : 0.0);
		fprintf(stderr, "perf:   registro plano       %10llu  (%.0f/s)\n",
			perf_aica_reg_plano, seg ? perf_aica_reg_plano / seg : 0.0);
		fprintf(stderr, "perf:   escritura de registro%10llu  (%.0f/s)\n",
			perf_aica_reg_escr,  seg ? perf_aica_reg_escr / seg : 0.0);
		fprintf(stderr, "perf:   RAM de onda leida    %10llu  (%.0f/s)\n",
			perf_onda_lect,      seg ? perf_onda_lect / seg : 0.0);
		fprintf(stderr, "perf:   RAM de onda escrita  %10llu  (%.0f/s)\n",
			perf_onda_escr,      seg ? perf_onda_escr / seg : 0.0);
		fprintf(stderr, "perf:   ---- accesos en total  %10llu\n", sync);

		/*
			Y lo que de verdad costaria: instantes emulados distintos. Un
			acceso mas dentro del mismo instante no espera a nadie.
		*/
		fprintf(stderr, "perf:   ---- alcances forzados %10llu"
			" (%.2f por acceso, %.3f por muestra de audio)\n",
			perf_sync_instantes,
			sync ? (double) perf_sync_instantes / (double) sync : 0.0,
			emulado ? (double) perf_sync_instantes
			          / ((double) emulado * 44.1) : 0.0);
	}

	/*
		La memoizacion de los barridos del ARM7. Lo que hay que mirar es
		"instrucciones repuestas": son pasos del ARM que **no se ejecutaron**, y
		el ARM cuesta 13-15 % de la corrida.

		"Sucios" son los barridos que estaban grabados y no se pudieron reponer
		porque alguien escribio una pagina que el barrido lee: es la tasa a la
		que la invalidacion por pagina esta trabajando. Si fuera casi igual a los
		aciertos, el mecanismo estaria al borde de no servir.
	*/
	if (arm7_memo_grabados || arm7_memo_aciertos)
	{
		unsigned long long pasos = perf_arm_pasos + arm7_memo_pasos;

		fprintf(stderr, "perf: ARM7, barridos memoizados\n");
		fprintf(stderr, "perf:   grabados %llu, abortados %llu\n",
			arm7_memo_grabados, arm7_memo_abortados);
		fprintf(stderr, "perf:   repuestos %llu, sucios %llu\n",
			arm7_memo_aciertos, arm7_memo_sucios);
		fprintf(stderr, "perf:   instrucciones repuestas %llu de %llu (%.1f %%)\n",
			arm7_memo_pasos, pasos,
			pasos ? 100.0 * (double) arm7_memo_pasos / (double) pasos : 0.0);

		if (arm7_memo_aciertos)
			fprintf(stderr, "perf:   %.1f instrucciones por reposicion\n",
				(double) arm7_memo_pasos / (double) arm7_memo_aciertos);

		{
			int i;

			for (i = 0; i < ARM7_MEMO_MOTIVOS; i++)
				if (arm7_memo_motivo[i])
					fprintf(stderr, "perf:     abortado por %-20s %12llu\n",
						arm7_memo_motivo_nombre[i], arm7_memo_motivo[i]);
		}
	}

	perf_onda_censo();
}

/*
	El censo por paginas de la RAM de onda. Ver perf.h por que existe.

	Lo que hay que leer de aca es **una sola linea**: cuantas de las lecturas de
	datos del ARM caen en paginas que alguien escribio alguna vez. Si es casi
	cero, el sondeo se puede saltear con un contador de generacion por pagina; si
	es alto, no, y el camino 2 de docs/arm7-plan.md hay que replantearlo.
*/
void perf_onda_censo(void)
{
	unsigned long long lect_limpias = 0, lect_sucias = 0;
	unsigned long pags_lect = 0, pags_escr = 0, pags_ambas = 0;
	int i;

	if (!perf_sonda_onda)
		return;

	for (i = 0; i < PERF_ONDA_PAGS; i++)
	{
		if (perf_onda_pag_lect[i])
		{
			pags_lect++;

			if (perf_onda_pag_escr[i])
				lect_sucias += perf_onda_pag_lect[i];
			else
				lect_limpias += perf_onda_pag_lect[i];
		}

		if (perf_onda_pag_escr[i])
			pags_escr++;

		if (perf_onda_pag_lect[i] && perf_onda_pag_escr[i])
			pags_ambas++;
	}

	fprintf(stderr, "perf: RAM de onda, censo por paginas de %d KB\n",
		1 << (PERF_ONDA_PAG_BITS - 10));
	fprintf(stderr, "perf:   lecturas de datos del ARM %14llu\n",
		perf_onda_arm_dato_lect);
	fprintf(stderr, "perf:   ... en paginas nunca escritas %10llu (%.2f %%)\n",
		lect_limpias, perf_onda_arm_dato_lect
			? 100.0 * (double) lect_limpias / (double) perf_onda_arm_dato_lect
			: 0.0);
	fprintf(stderr, "perf:   ... en paginas escritas      %10llu (%.2f %%)\n",
		lect_sucias, perf_onda_arm_dato_lect
			? 100.0 * (double) lect_sucias / (double) perf_onda_arm_dato_lect
			: 0.0);
	/* **La cifra que decide.** Ver perf.h: la de arriba es la pregunta
	   pesimista, esta es la que el mecanismo haria de verdad. */
	fprintf(stderr, "perf:   ... sin cambio desde la lectura anterior %10llu"
		" (%.2f %%)\n",
		perf_onda_lect_sin_cambio, perf_onda_arm_dato_lect
			? 100.0 * (double) perf_onda_lect_sin_cambio
			  / (double) perf_onda_arm_dato_lect
			: 0.0);
	fprintf(stderr, "perf:   lecturas del ARM a registros %10llu (%.2f %%)\n",
		perf_onda_arm_reg_lect,
		(perf_onda_arm_reg_lect + perf_onda_arm_dato_lect)
			? 100.0 * (double) perf_onda_arm_reg_lect
			  / (double) (perf_onda_arm_reg_lect + perf_onda_arm_dato_lect)
			: 0.0);
	fprintf(stderr, "perf:   paginas: %lu leidas, %lu escritas, %lu ambas\n",
		pags_lect, pags_escr, pags_ambas);

	/* Las diez paginas mas leidas, con lo que se escribio en cada una: es el
	   mapa que dice si los lazos calientes viven en una zona quieta. */
	fprintf(stderr, "perf:   las 10 paginas mas leidas por el ARM\n");

	{
		int n;

		for (n = 0; n < 10; n++)
		{
			int mejor = -1;

			for (i = 0; i < PERF_ONDA_PAGS; i++)
				if (perf_onda_pag_lect[i] != ~0ull
				 && (mejor < 0
				  || perf_onda_pag_lect[i] > perf_onda_pag_lect[mejor]))
					mejor = i;

			if (mejor < 0 || perf_onda_pag_lect[mejor] == 0)
				break;

			fprintf(stderr,
				"perf:     %06X-%06X  %12llu lecturas  %10llu escrituras%s\n",
				(unsigned) mejor << PERF_ONDA_PAG_BITS,
				(unsigned) ((mejor + 1) << PERF_ONDA_PAG_BITS) - 1,
				perf_onda_pag_lect[mejor], perf_onda_pag_escr[mejor],
				perf_onda_pag_escr[mejor] ? "" : "   <- quieta");

			/* Tachada para que la vuelta siguiente no la vuelva a elegir. No se
			   restaura porque el censo corre una sola vez, al salir. */
			perf_onda_pag_lect[mejor] = ~0ull;
		}
	}
}
