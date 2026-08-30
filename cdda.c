/****************************************************************************

	CDDA - el audio de CD de la lectora. Ver cdda.h.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>			/* getenv(), para el interruptor de aislamiento */
#include <string.h>

#include "lnxdefs.h"
#include "cdda.h"
#include "iso.h"
#include "traza.h"
#include "hilo_aica.h"

/*
	El CDDA es estado del lado del AICA: cdda_muestra() corre una vez por
	muestra dentro de mezclar_una_muestra(), o sea EN EL HILO DEL AICA cuando
	--hilos esta puesto. Todo lo demas de este archivo lo llama el SH-4 (los
	paquetes SPI de gdrom.c y los hooks de syscall de dcopcodes.c), asi que
	cada puerta de ese lado fuerza primero el alcance -- la misma regla que
	los registros del AICA en mem.c: el comando cae con el chip detenido en el
	reloj_total exacto, y la salida no se mueve ni una muestra. Sin el gancho,
	un PLAY aplicado con el mezclador atrasado corria la musica una muestra:
	el .wav de Sega Rally 2 salia 4 bytes corrido desde los 32 s.

	Con el hilo apagado entrar()/salir() no hacen nada, y tests/dobles.c les
	da cuerpos vacios: cdda.c sigue sin saber que existe SDL.
*/

/*
	DCEMU_SIN_CDDA=1: la lectora acepta los comandos de audio y contesta como
	siempre --sonando, en tal FAD-- pero no entrega una sola muestra.

	Apagar el mecanismo entero no serviria para lo que sirve un interruptor de
	estos: un juego que sondea el estado de su musica cambiaria de camino, y lo
	que se estaria comparando serian dos ejecuciones distintas. Lo que se calla
	es la salida y nada mas.

	Se lee una vez, al arrancar la primera reproduccion, y no en el camino
	caliente. Ver CLAUDE.md, "Measurement discipline".
*/
static int mudo = -1;

/* Un sector de audio son 2352 bytes: 588 cuadros estereo de 16 bits. No lleva
   encabezado ni correccion de errores -- todo el sector son muestras. */
#define CDDA_BYTES_SECTOR	2352
#define CDDA_CUADROS_SECTOR	588

/*
	Cuantos sectores se leen de una vez. A 75 sectores por segundo, ocho son
	107 ms de audio y una lectura del archivo cada 107 ms; de a uno serian 75
	por segundo, que tampoco es nada, pero esto sale gratis y deja el camino
	caliente --una muestra por vuelta-- sin tocar el disco casi nunca.
*/
#define CDDA_SECTORES_BUF	8

static BYTE	buf[CDDA_SECTORES_BUF * CDDA_BYTES_SECTOR];
static int	buf_fad     = 0;	/* FAD del primer sector del buffer */
static int	buf_sectores = 0;	/* cuantos valen */
static int	cursor      = 0;	/* cuadro dentro del buffer */

static int	estado      = CDDA_EST_SIN_INFO;
static int	fad_ini     = 0;
static int	fad_fin     = 0;
static int	fad_lectura = 0;	/* el proximo sector a traer */
static int	repeticiones = 0;

void cdda_reiniciar(void)
{
	hilo_aica_entrar();

	estado       = CDDA_EST_SIN_INFO;
	fad_ini      = 0;
	fad_fin      = 0;
	fad_lectura  = 0;
	repeticiones = 0;
	buf_fad      = 0;
	buf_sectores = 0;
	cursor       = 0;

	hilo_aica_salir();
}

/* El FAD del cuadro que se esta entregando. Con el buffer vacio es el proximo
   que se va a traer, que es lo que hay que contestar despues de un SEEK.
   El cuerpo es interno para que cdda_pista() no tome el gancho dos veces:
   hilo_aica_entrar() no es reentrante. */
static int fad_crudo(void)
{
	if (buf_sectores == 0)
		return fad_lectura;

	return buf_fad + cursor / CDDA_CUADROS_SECTOR;
}

int cdda_fad(void)
{
	int fad;

	hilo_aica_entrar();
	fad = fad_crudo();
	hilo_aica_salir();

	return fad;
}

int cdda_estado(void)
{
	int e;

	hilo_aica_entrar();
	e = estado;
	hilo_aica_salir();

	return e;
}

int cdda_pista(void)
{
	int fad;
	int i;
	int pista = 1;

	hilo_aica_entrar();
	fad = fad_crudo();

	for (i = 0; i < iso_num_pistas(); i++)
	{
		int desde = iso_pista_fad(i);

		if (fad >= desde && fad < desde + iso_pista_sectores(i))
		{
			pista = i + 1;
			break;
		}
	}

	hilo_aica_salir();

	return pista;
}

/*
	Trae el proximo pedazo, envolviendo por el final del rango si toca repetir.
	Deja `buf_sectores` en 0 y el estado en TERMINADO cuando ya no queda nada.
*/
static void rellenar(void)
{
	int quedan, cuantos, leidos;

	if (estado != CDDA_EST_SONANDO)
		return;

	if (fad_lectura > fad_fin)
	{
		/* Se acabo el rango. 15 es "para siempre" y no se descuenta; 0 es que
		   ya se toco la ultima vuelta. */
		if (repeticiones == CDDA_REPETIR_SIEMPRE)
		{
			fad_lectura = fad_ini;
		}
		else
		if (repeticiones > 0)
		{
			repeticiones--;
			fad_lectura = fad_ini;
		}
		else
		{
			estado       = CDDA_EST_TERMINADO;
			buf_sectores = 0;
			cursor       = 0;

			if (traza_activa)
				fprintf(stderr, "traza: CD-DA terminado en el FAD %d\n", fad_fin);

			return;
		}
	}

	quedan  = fad_fin - fad_lectura + 1;
	cuantos = (quedan < CDDA_SECTORES_BUF) ? quedan : CDDA_SECTORES_BUF;

	leidos = iso_leer_audio(buf, fad_lectura, cuantos);

	/*
		Un sector que no se puede leer --se pidio fuera del disco, o el rango
		entra en una pista de datos-- **no detiene la reproduccion**: se entrega
		silencio y la cabeza sigue avanzando, que es lo que deja al juego
		terminar su pista y seguir. Detenerse aqui seria inventar un error que
		la lectora no reporto.
	*/
	if (leidos <= 0)
	{
		memset(buf, 0, (size_t) cuantos * CDDA_BYTES_SECTOR);
		leidos = cuantos;
	}
	else
	if (leidos < cuantos)
		memset(&buf[(size_t) leidos * CDDA_BYTES_SECTOR], 0,
			(size_t) (cuantos - leidos) * CDDA_BYTES_SECTOR);

	buf_fad      = fad_lectura;
	buf_sectores = cuantos;
	cursor       = 0;
	fad_lectura += cuantos;
}

int cdda_muestra(int * izq, int * der)
{
	const BYTE * p;

	if (estado != CDDA_EST_SONANDO || mudo == 1)
		return 0;

	if (cursor >= buf_sectores * CDDA_CUADROS_SECTOR)
	{
		rellenar();

		if (estado != CDDA_EST_SONANDO || buf_sectores == 0)
			return 0;
	}

	/* Little endian explicito: es el orden del CD, no el del anfitrion. */
	p = &buf[(size_t) cursor * 4];

	*izq = (short) (p[0] | (p[1] << 8));
	*der = (short) (p[2] | (p[3] << 8));

	cursor++;

	return 1;
}

/* ------------------------------------------------------------------------ */
/* Los comandos                                                             */
/* ------------------------------------------------------------------------ */

static void arrancar(int desde, int hasta, int veces, const char * como)
{
	if (mudo < 0)
	{
		mudo = (getenv("DCEMU_SIN_CDDA") != NULL);

		if (mudo && traza_activa)
			fprintf(stderr, "traza: DCEMU_SIN_CDDA: la lectora contesta el "
				"audio pero no entrega muestras\n");
	}

	if (hasta < desde)
		hasta = desde;

	if (veces > CDDA_REPETIR_SIEMPRE)
		veces = CDDA_REPETIR_SIEMPRE;

	fad_ini      = desde;
	fad_fin      = hasta;
	fad_lectura  = desde;
	repeticiones = veces;
	buf_sectores = 0;
	cursor       = 0;
	estado       = CDDA_EST_SONANDO;

	if (traza_activa)
		fprintf(stderr, "traza: CD-DA %s, FAD %d a %d, %d repeticion%s\n",
			como, desde, hasta, veces, (veces == 1) ? "" : "es");
}

void cdda_reproducir_sectores(int desde, int hasta, int veces)
{
	hilo_aica_entrar();
	arrancar(desde, hasta, veces, "por sectores");
	hilo_aica_salir();
}

/*
	Por numero de pista. La tabla de pistas de iso.c ya habla en FAD, asi que
	esto es solo buscar los extremos; una pista fuera de rango se recorta contra
	el disco en vez de rechazarse, que es lo que hace la lectora.
*/
void cdda_reproducir_pistas(int pista_ini, int pista_fin, int veces)
{
	int n = iso_num_pistas();
	int a, b, desde, hasta;

	if (n <= 0)
		return;

	a = pista_ini - 1;
	b = pista_fin - 1;

	if (a < 0)		a = 0;
	if (a >= n)		a = n - 1;
	if (b < a)		b = a;
	if (b >= n)		b = n - 1;

	desde = iso_pista_fad(a);
	hasta = iso_pista_fad(b) + iso_pista_sectores(b) - 1;

	hilo_aica_entrar();
	arrancar(desde, hasta, veces, "por pistas");
	hilo_aica_salir();
}

void cdda_pausar(void)
{
	hilo_aica_entrar();

	if (estado == CDDA_EST_SONANDO)
		estado = CDDA_EST_PAUSADO;

	hilo_aica_salir();
}

void cdda_seguir(void)
{
	hilo_aica_entrar();

	if (estado == CDDA_EST_PAUSADO)
		estado = CDDA_EST_SONANDO;

	hilo_aica_salir();
}

void cdda_parar(void)
{
	hilo_aica_entrar();

	estado       = CDDA_EST_SIN_INFO;
	buf_sectores = 0;
	cursor       = 0;

	hilo_aica_salir();
}

/*
	SEEK deja la cabeza donde se le dijo y **pausada**: el disco sigue girando y
	el juego tiene que soltar la pausa para que suene. Es lo que dice el
	protocolo y lo que espera quien encadena un SEEK con un RELEASE.
*/
void cdda_buscar(int fad)
{
	hilo_aica_entrar();

	fad_lectura  = fad;
	fad_ini      = fad;
	buf_sectores = 0;
	cursor       = 0;

	if (fad_fin < fad)
		fad_fin = fad;

	estado = CDDA_EST_PAUSADO;

	hilo_aica_salir();
}
