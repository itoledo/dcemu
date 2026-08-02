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
unsigned long long perf_ns_presentar= 0;
unsigned long long perf_ns_servicio	= 0;
unsigned long long perf_ns_ta		= 0;

unsigned long long perf_arm_pasos	= 0;
unsigned long long perf_arm_ocioso	= 0;

unsigned long long perf_ns_captura	= 0;

unsigned long long perf_escenas		= 0;
unsigned long long perf_tiras		= 0;
unsigned long      perf_tiras_max	= 0;

unsigned long long perf_sync_instantes = 0;
unsigned long long perf_ns_espera	= 0;

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

	fprintf(stderr, "perf: reparto del tiempo real\n");

	aica_total = perf_ns_aica + perf_ns_arm;

	linea("AICA (mezcla)",		perf_ns_aica,		real);
	linea("AICA (ARM7)",		perf_ns_arm,		real);
	linea("  AICA total",		aica_total,			real);
	linea("dibujar_escena()",	perf_ns_escena,		real);
	linea("  de eso texturas",	perf_ns_textura,	real);
	linea("presentar",			perf_ns_presentar,	real);
	linea("bloque periodico",	perf_ns_servicio,	real);
	linea("TA (store queue)",	perf_ns_ta,			real);

	if (perf_ns_captura)
		linea("--captura-gl",	perf_ns_captura,	real);

	if (perf_ns_espera)
		linea("esperando al AICA",	perf_ns_espera,	real);

	/* Lo que queda es el interprete del SH-4 y el andamiaje de main_loop().
	   El bloque periodico y el AICA estan anidados, asi que no se restan dos
	   veces: perf_ns_servicio ya incluye a aica_total. */
	{
		unsigned long long medido = perf_ns_servicio + perf_ns_escena
		                          + perf_ns_presentar + perf_ns_ta
		                          + perf_ns_captura;

		linea("resto (interprete)",
			real > medido ? real - medido : 0, real);
	}

	if (perf_escenas)
		fprintf(stderr, "perf: %llu escenas, %.0f tiras por escena en promedio,"
			" %lu la mayor\n",
			perf_escenas, (double) perf_tiras / (double) perf_escenas,
			perf_tiras_max);

	if (perf_arm_pasos)
		fprintf(stderr, "perf: ARM7: %llu pasos, %llu ociosos (%.1f %%)\n",
			perf_arm_pasos, perf_arm_ocioso,
			100.0 * (double) perf_arm_ocioso / (double) perf_arm_pasos);

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
