/****************************************************************************

	PERF - el desglose de en que se va el tiempo real

	Ver perf.h para el porque. Aqui solo esta el reloj del anfitrion y el
	informe.

*****************************************************************************/

#include <stdio.h>

#include "perf.h"
#include "tmu.h"			/* reloj_total, reloj_ms() */

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
unsigned long long perf_mmu_fetch_acierto2	= 0;
unsigned long long perf_mmu_fetch_fallo		= 0;
unsigned long long perf_mmu_falta			= 0;
unsigned long long perf_instantaneas		= 0;
unsigned long long perf_instantaneas_usadas	= 0;
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

unsigned long long perf_cuadros			= 0;
unsigned long long perf_instrucciones	= 0;

unsigned long long perf_tex_acierto		= 0;
unsigned long long perf_tex_regenera	= 0;
unsigned long long perf_tex_nueva		= 0;
unsigned long long perf_tex_desalojo	= 0;

unsigned long long perf_tiras_dibujadas	= 0;
unsigned long long perf_tiras_sin_cambio= 0;

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
	if (!perf_activa)
		return;

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

	/*
		La MMU. Solo sale si el guest la encendio alguna vez, porque en todo lo
		demas del arbol estas lineas serian seis ceros. Ver perf.h.
	*/
	if (perf_instantaneas || perf_mmu_traduce)
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
		if (perf_mmu_datos_choque + perf_mmu_datos_capacidad)
			fprintf(stderr, "perf:   ... de los fallos, %.1f %% son la misma"
				" pagina con otra etiqueta (modo o ASID)\n",
				100.0 * (double) perf_mmu_datos_choque
					/ (double) (perf_mmu_datos_choque
								+ perf_mmu_datos_capacidad));

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
}
