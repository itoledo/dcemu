/****************************************************************************

	HILO_AICA - el AICA y el ARM7 en su propio hilo. Ver hilo_aica.h.

	El protocolo entero esta en este archivo. Son tres campos bajo un mutex y
	una variable de condicion:

	    objetivo   hasta que reloj_total puede avanzar el AICA
	    alcanzado  hasta donde llego de verdad
	    terminar   para salir

	y dos reglas que juntas dan el determinismo:

	  - **objetivo nunca supera a reloj_total y nunca retrocede.** Lo publica el
	    SH-4; el AICA solo lo lee.
	  - **El AICA actualiza alcanzado con el mutex tomado, despues de terminar
	    un paso.** Asi, si el SH-4 tiene el mutex y ve alcanzado >= reloj_total,
	    el otro hilo esta necesariamente detenido: o esperando en la condicion,
	    o bloqueado pidiendo el mutex.

	La segunda merece el razonamiento completo, porque de ella depende que no
	haya carrera y no es evidente. El AICA trabaja **sin** el mutex tomado. Tres
	casos, y en los tres el SH-4 queda a salvo:

	  1. El AICA espera en la condicion. Para volver de hilo_cond_esperar()
	     necesita el mutex, que tiene el SH-4. No avanza.
	  2. El AICA esta trabajando. Entonces cuando empezo valia
	     alcanzado < objetivo, y todavia no actualizo alcanzado. Como objetivo
	     no retrocede y no supera a reloj_total, se cumple
	     alcanzado < objetivo <= reloj_total, o sea que el SH-4 ve
	     alcanzado < reloj_total y espera. Cuando el AICA termina, toma el
	     mutex, actualiza y avisa; el SH-4 despierta ya con el chip al dia y con
	     el mutex en la mano, y el AICA se bloquea en la vuelta siguiente.
	  3. El AICA acaba de terminar y todavia no tomo el mutex. Igual que el 2:
	     alcanzado no esta actualizado, asi que el SH-4 espera.

	El paso del AICA son HILO_AICA_PASO muestras, y elegir ese numero es la
	diferencia entre que la fase sirva y que no: ver el comentario de la
	constante. Medido en docs/hilos-plan.md, "Resultado del paso 0".

*****************************************************************************/

#include <stdio.h>

#include "main.h"			/* aica.h usa DWORD; de aqui salen los tipos */
#include "hilo_aica.h"
#include "hilo.h"
#include "aica.h"
#include "tmu.h"			/* reloj_total */
#include "opciones.h"
#include "traza.h"
#include "perf.h"

/* Una de cada cuantas entradas al bloque periodico publica el objetivo. */
#define HILO_AICA_PUBLICAR		64

/*
	Cuantas muestras avanza el AICA entre dos revisiones del estado compartido.

	**Tiene que ser 1, y no por rendimiento sino por correccion.** Se probo con 4
	para bajar el costo de sincronizar, y el .wav dejo de salir identico: divergia
	a los 15,6 segundos con un corrimiento de dos cuadros. La causa no es la
	mezcla sino la entrega de la interrupcion del AICA al ASIC, que es el unico
	punto del diseno donde el determinismo no esta garantizado por construccion
	(ver aica_linea_asic en aica.h): el chip levanta la linea en un instante
	emulado y main_loop() la cobra en el bloque periodico en que se entere, que
	depende del reloj real. Cuanto mas desacoplados van los dos hilos, mas se
	nota, y con 4 se nota.

	Con 1 el .wav sale bit a bit igual al del camino sin hilos sobre 2 646 565
	muestras, que es la prueba de aceptacion de la fase.

	Es tambien la razon por la que el hilo esta apagado por omision: el costo de
	sincronizar en cada muestra es justamente lo que hace que la fase no gane
	tiempo. Ver docs/hilos-plan.md, "Resultado de la fase 1".
*/
#define HILO_AICA_PASO			1

static hilo *		el_hilo		= NULL;
static hilo_mtx *	mtx			= NULL;
static hilo_cond *	cond		= NULL;

static unsigned long long	objetivo	= 0;
static unsigned long long	alcanzado	= 0;
static int					terminar	= 0;
static int					activo		= 0;

/*
	Cuantos hilos estan esperando en la condicion. Existe para no avisar cuando
	no hay nadie: avisar en cada muestra --44100 veces por segundo emulado--
	cuesta una llamada al sistema que casi siempre no despierta a nadie, y eso
	solo ya era una parte del sobrecosto que hacia perder la primera version.
*/
static int					esperando	= 0;

/* Cuantas veces el SH-4 tuvo que esperar de verdad, y cuantas encontro al
   chip ya al dia. La relacion entre las dos es lo que dice si el hilo sirve;
   sale por --traza-mem al terminar. */
static unsigned long long	esperas		= 0;
static unsigned long long	sin_espera	= 0;

int hilo_aica_activo(void)
{
	return activo;
}

/* ------------------------------------------------------------------------ */
/* El hilo                                                                  */
/* ------------------------------------------------------------------------ */

static int cuerpo(void * dato)
{
	(void) dato;

	for (;;)
	{
		unsigned long long obj, llegue;

		hilo_mtx_tomar(mtx);

		while (!terminar && alcanzado >= objetivo)
		{
			esperando++;
			hilo_cond_esperar(cond, mtx);
			esperando--;
		}

		if (terminar)
		{
			hilo_mtx_soltar(mtx);
			return 0;
		}

		obj = objetivo;

		hilo_mtx_soltar(mtx);

		/* Sin el mutex: todo lo que se toca aqui es estado del AICA, y quien
		   lo quiera mirar desde el SH-4 pasa por hilo_aica_entrar(), que
		   espera a que este al dia. Ver el razonamiento de arriba. */
		llegue = aica_tick_hasta(obj, HILO_AICA_PASO);

		hilo_mtx_tomar(mtx);

		/* Nunca retroceder: aica_tick_hasta() devuelve el reloj del ultimo
		   borde de muestra, que puede quedar por detras del objetivo cuando
		   este cae en medio de una. Si no queda nada por hacer, el AICA esta
		   al dia con el objetivo aunque no haya producido una muestra nueva. */
		if (llegue >= obj)
			alcanzado = obj;
		else
		if (llegue > alcanzado)
			alcanzado = llegue;

		/* Solo si hay alguien esperando. Ver `esperando` arriba. */
		if (esperando)
			hilo_cond_avisar_a_todos(cond);

		hilo_mtx_soltar(mtx);
	}
}

/* ------------------------------------------------------------------------ */
/* Arranque y parada                                                        */
/* ------------------------------------------------------------------------ */

void hilo_aica_iniciar(void)
{
	if (!opciones.hilos || opciones.sin_aica)
		return;

	mtx  = hilo_mtx_crear();
	cond = hilo_cond_crear();

	if (mtx == NULL || cond == NULL)
	{
		fprintf(stderr, "hilo_aica: no se pudo crear el mutex o la condicion;"
			" el AICA sigue en el hilo principal.\n");
		hilo_mtx_destruir(mtx);
		hilo_cond_destruir(cond);
		mtx = NULL;
		cond = NULL;
		return;
	}

	objetivo  = reloj_total;
	alcanzado = reloj_total;
	terminar  = 0;

	el_hilo = hilo_crear(cuerpo, NULL, "aica");

	if (el_hilo == NULL)
	{
		fprintf(stderr, "hilo_aica: no se pudo crear el hilo;"
			" el AICA sigue en el hilo principal.\n");
		hilo_mtx_destruir(mtx);
		hilo_cond_destruir(cond);
		mtx = NULL;
		cond = NULL;
		return;
	}

	activo = 1;

	if (traza_activa)
		fprintf(stderr, "traza: el AICA y el ARM7 corren en su propio hilo.\n");
}

void hilo_aica_terminar(void)
{
	if (!activo)
		return;

	/* Antes de parar, que llegue al dia. Sin esto el hilo se detiene donde
	   este y la ultima muestra no se produce: el .wav salia cuatro bytes --un
	   cuadro estereo-- mas corto que el del camino sin hilos, con el resto
	   identico. La prueba de aceptacion es que sean iguales, asi que la
	   diferencia importa aunque sea inaudible. */
	hilo_aica_entrar();
	hilo_aica_salir();

	hilo_mtx_tomar(mtx);
	terminar = 1;
	hilo_cond_avisar_a_todos(cond);
	hilo_mtx_soltar(mtx);

	hilo_esperar(el_hilo);

	activo  = 0;
	el_hilo = NULL;

	if (traza_activa)
		fprintf(stderr, "traza: hilo del AICA: %llu alcances con espera,"
			" %llu sin ella (%.1f %% sin esperar).\n",
			(unsigned long long) esperas, (unsigned long long) sin_espera,
			(esperas + sin_espera)
				? 100.0 * (double) sin_espera / (double) (esperas + sin_espera)
				: 0.0);

	hilo_mtx_destruir(mtx);
	hilo_cond_destruir(cond);
	mtx  = NULL;
	cond = NULL;
}

/* ------------------------------------------------------------------------ */
/* Los dos puntos de contacto con el SH-4                                   */
/* ------------------------------------------------------------------------ */

void hilo_aica_publicar(void)
{
	static unsigned n = 0;

	if (!activo)
	{
		/* El camino de siempre. */
		aica_tick();
		return;
	}

	if ((++n & (HILO_AICA_PUBLICAR - 1)) != 0)
		return;

	hilo_mtx_tomar(mtx);

	/* Monotono por construccion: reloj_total solo sube. */
	objetivo = reloj_total;

	if (esperando)
		hilo_cond_avisar_a_todos(cond);

	hilo_mtx_soltar(mtx);
}

void hilo_aica_entrar(void)
{
	if (!activo)
		return;

	hilo_mtx_tomar(mtx);

	objetivo = reloj_total;

	if (alcanzado >= reloj_total)
	{
		sin_espera++;
		return;					/* con el mutex tomado, a proposito */
	}

	esperas++;

	hilo_cond_avisar_a_todos(cond);

	{
		PERF_MARCA(t_esp);

		while (alcanzado < reloj_total)
		{
			esperando++;
			hilo_cond_esperar(cond, mtx);
			esperando--;
		}

		PERF_SUMAR(t_esp, perf_ns_espera);
	}

	/* Y se vuelve con el mutex tomado: el acceso pasa ahora, con el otro hilo
	   detenido. */
}

void hilo_aica_salir(void)
{
	if (!activo)
		return;

	hilo_mtx_soltar(mtx);
}
