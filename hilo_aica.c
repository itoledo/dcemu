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
#include <stdlib.h>			/* getenv: la palanca del giro */
#include <intrin.h>			/* _mm_pause, el giro */

#include "main.h"			/* aica.h usa DWORD; de aqui salen los tipos */
#include "hilo_aica.h"
#include "hilo.h"
#include "aica.h"
#include "tmu.h"			/* reloj_total */
#include "opciones.h"
#include "traza.h"
#include "perf.h"

/*
	Antes se publicaba una de cada 64 entradas, porque publicar era tomar el
	mutex. Ya no: el objetivo es un volatile de un solo escritor (el SH-4) y
	un solo lector (el hilo), el mismo patron que los indices del anillo
	aica_salida[] en aica.h, y publicar es UN almacen. Se publica en CADA
	servicio del bloque periodico a proposito, porque de eso depende la
	exactitud: ver el comentario de hilo_aica_entrar().
*/

/*
	Cuantas muestras avanza el AICA entre dos revisiones del estado compartido.

	Fue 1 por correccion: con 4 el .wav divergia a los 15,6 segundos, y la
	causa era la entrega de la interrupcion del AICA al ASIC -- el unico punto
	del diseno sin determinismo por construccion: el chip levantaba la linea en
	un instante emulado y main_loop() la cobraba en el bloque periodico en que
	se enterara, que dependia del reloj real. **Eso ya no existe** (2026-09-05):
	la linea se anota con su muestra y se entrega con una latencia fija en
	tiempo emulado (aica.h, el registro; hilo_aica_esperar_muestra abajo), y
	con hilos Sega Rally 2 --el guest que la consumia-- sale identico hasta la
	lista de entregas.

	Con eso el paso vuelve a ser una decision de latencia y no de correccion:
	es lo maximo que el SH-4 espera en entrar() cuando pide un alcance. Se
	queda en 1 porque nadie midio otra cosa desde que la entrega es exacta; un
	paso mayor es un experimento pendiente con su A/B, no una regla.
*/
#define HILO_AICA_PASO			1

static hilo *		el_hilo		= NULL;
static hilo_mtx *	mtx			= NULL;
static hilo_cond *	cond		= NULL;

/*
	`objetivo` cruza sin mutex: lo escribe solo el SH-4 (en cada servicio del
	bloque periodico) y lo lee solo el hilo del AICA. En x64 un almacen
	alineado de 64 bits es un solo MOV, y el patron ya esta establecido en el
	arbol con los indices del anillo (aica.h). `alcanzado` si va bajo el
	mutex: lo escribe el hilo y el SH-4 espera sobre el.
*/
static volatile unsigned long long	objetivo	= 0;
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

/*
	Y las esperas de la entrega de la linea (hilo_aica_esperar_muestra): cuantas
	veces el bloque periodico encontro la muestra del horizonte ya terminada,
	cuantas tuvo que esperarla, y cuantas pidio una muestra que el objetivo
	publicado ni siquiera cubre -- que no puede pasar y por eso se cuenta.
*/
static unsigned long long	esperas_linea		= 0;
static unsigned long long	sin_espera_linea	= 0;
static unsigned long long	esperas_imposibles	= 0;
static unsigned long long	esperas_giro		= 0;	/* resueltas girando */

/*
	El giro (DCEMU_HILO_AICA_GIRO=N, en vueltas de _mm_pause): cuantas vueltas
	espera activo cada lado antes de dormirse en la condicion.

	Existe por una medida: con la entrega determinista el hilo principal tiene
	que haber TERMINADO la muestra del horizonte, y en Crazy Taxi llegaba tarde
	al 35 % de los bordes -- no por la holgura (doblarla y cuadruplicarla con
	la demora casi no la movia: 34, 28, 26 %) sino porque el hilo se duerme en
	la condicion tras CADA muestra (44 100 veces por segundo) y el despertar
	cuesta mas que los 9 us reales que dura un intervalo a 2,5x. Un despertar
	es dos cambios de contexto; el giro los evita cuando la proxima muestra
	esta a microsegundos, que es siempre en el guest que corre rapido, y se
	rinde y duerme cuando no. Cero es la conducta anterior.
*/
static int					giro				= -1;

static void leer_giro(void)
{
	if (giro < 0)
	{
		const char * v = getenv("DCEMU_HILO_AICA_GIRO");

		/* 2000: medido en Crazy Taxi a 60 s, demora 1 -- 0 vueltas: 33 % de
		   los bordes con espera y 18 677 ms; 500: 0,10 % y 17 244; 2000:
		   0,01 % y 16 928; 8000: 0,00 % y 16 815 (sin hilos 18 711). El
		   escalon de 2000 a 8000 ya no paga lo que cuesta girar. */
		giro = (v != NULL) ? atoi(v) : 2000;

		if (giro < 0)
			giro = 0;
	}
}

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

	leer_giro();

	for (;;)
	{
		unsigned long long obj, llegue;

		/* El giro, antes del mutex: `alcanzado` lo escribe solo este hilo y
		   `objetivo` es el volatile de un escritor, asi que se miran sin el.
		   `terminar` se lee sucio y se vuelve a mirar bajo el mutex. */
		if (giro > 0)
		{
			int i;

			for (i = 0; i < giro && !terminar && alcanzado >= objetivo; i++)
				_mm_pause();
		}

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
	   diferencia importa aunque sea inaudible.

	   entrar() espera al objetivo PUBLICADO, asi que primero se publica el
	   reloj final: es el unico sitio donde el alcance debe llegar hasta
	   reloj_total mismo. */
	hilo_aica_publicar();
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
	{
		fprintf(stderr, "traza: hilo del AICA: %llu alcances con espera,"
			" %llu sin ella (%.1f %% sin esperar).\n",
			(unsigned long long) esperas, (unsigned long long) sin_espera,
			(esperas + sin_espera)
				? 100.0 * (double) sin_espera / (double) (esperas + sin_espera)
				: 0.0);

	}

	/* La linea: cuantas veces el horizonte ya estaba listo y cuantas no. Va
	   SIN condicion, como los contadores de control del traductor: las
	   imposibles tienen que ser cero y un cero callado no se distingue de una
	   sonda muerta. */
	/* El denominador honesto son los bordes de muestra, no las llamadas: el
	   bloque periodico solo llama cuando la senal de terminacion ya venia
	   atrasada, asi que "esperas sobre llamadas" sale casi siempre ~100 %. */
	{
		unsigned long long muestras = aica_muestras_hechas();

		fprintf(stderr, "hilo del AICA, linea: %llu esperas sobre %llu"
			" muestras (%.2f %% de los bordes), %llu resueltas girando"
			" (giro %d), %llu llegaron tarde sin esperar, %llu imposibles\n",
			esperas_linea, muestras,
			muestras ? 100.0 * (double) esperas_linea / (double) muestras : 0.0,
			esperas_giro, giro, sin_espera_linea, esperas_imposibles);
	}

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
	if (!activo)
	{
		/* El camino de siempre. */
		aica_tick();
		return;
	}

	/* Monotono por construccion: reloj_total solo sube. Un almacen volatile,
	   sin mutex: ver la declaracion de `objetivo`. */
	objetivo = reloj_total;

	/*
		El aviso solo hace falta si el hilo duerme, y `esperando` se mira sin
		el mutex a proposito: si la lectura sucia pierde la carrera con el
		hilo que se esta por dormir, el proximo servicio --microsegundos de
		reloj real despues-- lo despierta. Es latencia de una publicacion,
		nunca un aviso perdido para siempre.
	*/
	if (esperando)
	{
		hilo_mtx_tomar(mtx);

		if (esperando)
			hilo_cond_avisar_a_todos(cond);

		hilo_mtx_soltar(mtx);
	}
}

/*
	La regla de exactitud, y por que se espera al OBJETIVO PUBLICADO y no a
	reloj_total: en el emulador de un hilo, un cambio de estado del lado del
	audio (una escritura de registro, un PLAY del CDDA, una rafaga a la RAM de
	onda) se aplica en su reloj_total exacto, y las muestras que a ese instante
	seguian pendientes --porque el bloque periodico corre entre bloques del
	traductor, no entre instrucciones-- se mezclan DESPUES, ya con el estado
	nuevo. El conjunto de muestras mezcladas antes del cambio es entonces
	"todas las de borde <= el ultimo servicio", y como publicar ocurre en cada
	servicio, ese conjunto es exactamente { muestras <= objetivo }.

	Esperar aqui hasta reloj_total mezclaria de mas: las pendientes caerian
	ANTES del cambio, con el estado viejo, y el .wav se corria --medido: el
	PLAY del CDDA de Sega Rally 2 con dos muestras pendientes salia 8 bytes
	corrido--. Esperar al objetivo publicado reproduce el calendario del
	emulador de un hilo al byte.

	Mientras el SH-4 espera aqui no hay quien publique (el que publica es el
	mismo hilo que espera), asi que el objetivo esta congelado y la espera
	termina.
*/
void hilo_aica_entrar(void)
{
	unsigned long long obj;

	if (!activo)
		return;

	hilo_mtx_tomar(mtx);

	obj = objetivo;

	if (alcanzado >= obj)
	{
		sin_espera++;
		return;					/* con el mutex tomado, a proposito */
	}

	esperas++;

	hilo_cond_avisar_a_todos(cond);

	{
		PERF_MARCA(t_esp);

		while (alcanzado < obj)
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

/*
	La espera de la entrega de la linea (2026-09-05), y en que se diferencia
	de entrar(): espera a que el hilo haya TERMINADO la muestra `muestra`
	(aica_muestras_listas), no a que alcance el objetivo publicado entero.
	Esperar el objetivo entero seria el lockstep que el plan viejo daba por
	inevitable; el horizonte de la entrega va `demora` muestras por detras y
	el hilo casi siempre ya lo paso.

	Se espera sobre aica_muestras_listas y no sobre `alcanzado`: el objetivo
	puede caer entre dos bordes de muestra, con lo que `alcanzado == objetivo`
	no dice cuantas muestras estan cerradas.

	Avisa a la condicion antes de dormirse, como entrar(): publicar() lee
	`esperando` sucio y confia en que la publicacion siguiente despierte al
	hilo -- pero mientras el SH-4 duerme aqui nadie publica, asi que si el hilo
	perdio esa carrera y se durmio, sin este aviso los dos dormirian para
	siempre.

	Termina: la muestra pedida es <= muestras(objetivo) (publicar() corrio dos
	lineas antes en el mismo servicio), asi que el hilo la puede producir sin
	una publicacion nueva, y avisa tras cada muestra mientras haya alguien
	esperando. Si aun asi la muestra pasa el objetivo -- que no puede ocurrir
	-- se cuenta y no se espera.

	Solo desde el bloque periodico; nunca dentro de entrar()/salir() ni desde
	el hilo del AICA.
*/
void hilo_aica_esperar_muestra(unsigned long long muestra)
{
	if (!activo)
		return;

	/* El giro de este lado: la muestra que falta suele estar a microsegundos
	   de cerrarse, y dormir aqui es un par de cambios de contexto en el
	   camino critico. Se gira sobre la senal de terminacion, sin mutex. */
	leer_giro();

	if (giro > 0)
	{
		int i;

		for (i = 0; i < giro && aica_muestras_listas < muestra; i++)
			_mm_pause();

		if (aica_muestras_listas >= muestra)
		{
			esperas_giro++;
			return;
		}
	}

	hilo_mtx_tomar(mtx);

	if (aica_muestras_listas >= muestra)
	{
		sin_espera_linea++;
		hilo_mtx_soltar(mtx);
		return;
	}

	if (aica_muestras_de_reloj(objetivo) < muestra)
	{
		esperas_imposibles++;
		hilo_mtx_soltar(mtx);
		return;
	}

	esperas_linea++;

	hilo_cond_avisar_a_todos(cond);

	{
		PERF_MARCA(t_esp);

		while (aica_muestras_listas < muestra)
		{
			esperando++;
			hilo_cond_esperar(cond, mtx);
			esperando--;
		}

		PERF_SUMAR(t_esp, perf_ns_espera_linea);
	}

	hilo_mtx_soltar(mtx);
}

void hilo_aica_salir(void)
{
	if (!activo)
		return;

	hilo_mtx_soltar(mtx);
}
