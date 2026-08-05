/*
	cdda.h -- el audio de CD de la lectora (CD-DA).

	Una pista de audio de un disco de Dreamcast no pasa por el AICA como pasan
	las voces del juego: **la lectora la decodifica ella y la entrega al chip de
	sonido ya como muestras**, por una entrada aparte, y el AICA la suma a su
	mezcla. El juego no ve las muestras nunca; solo le dice a la lectora "toca
	desde aca hasta aca, N veces" y despues pregunta como va.

	Por eso esto es un modulo de la lectora y no del AICA, y por eso alcanza con
	sumar en `mezclar_una_muestra()`: el formato del CD --44 100 Hz, estereo,
	16 bits con signo, little endian-- es exactamente el de la salida del
	mezclador, asi que hay una muestra de CD por cada muestra del AICA y no hay
	remuestreo que hacer.

	Las dos vias de mando llegan aca:

	  - los comandos del paquete SPI (gdrom.c): CD_PLAY 0x20, CD_SEEK 0x21,
	    CD_SCAN 0x22, y GET_SCD / REQ_STAT para preguntar;
	  - los del driver del boot ROM cuando estan puestos los hooks
	    (dcopcodes.c): 20 PLAY_TRACKS, 21 PLAY_SECTORS, 22 PAUSE, 23 RELEASE,
	    27 SEEK, 33 STOP, 34 GETSCD, 36 REQ_STAT.

	Las dos tienen que contestar lo mismo, que es la regla del arbol para la
	lectora.

	**Lo que no esta**: el nivel de CD-DA es fijo. En el chip la entrada de CD
	pasa por el mezclador del DSP, con sus propios registros de atenuacion, y el
	DSP no se emula; se suma despues de MVOL, que es lo mas parecido a "otra
	entrada del DAC". Tampoco esta el SCAN (avance rapido), que se acepta como
	un SEEK.
*/

#ifndef _CDDA_H_
#define _CDDA_H_

/* Los codigos de estado de audio del subcodigo Q, tal como los define el
   protocolo (y los nombra syscalls.h de KOS). Es lo que contestan GET_SCD y
   REQ_STAT, y lo que mira un juego para saber si su musica sigue sonando. */
#define CDDA_EST_INVALIDO	0x00
#define CDDA_EST_SONANDO	0x11
#define CDDA_EST_PAUSADO	0x12
#define CDDA_EST_TERMINADO	0x13
#define CDDA_EST_ERROR		0x14
#define CDDA_EST_SIN_INFO	0x15

/* Repeticiones: 0 es una sola vez, 15 es para siempre. Es el campo del
   comando, no una convencion de dcemu. */
#define CDDA_REPETIR_SIEMPRE	15

/*
	Arranca la reproduccion. `pista_ini`/`pista_fin` son numeros de pista
	empezando en 1 (comando PLAY_TRACKS); `fad_ini`/`fad_fin` son FAD absolutos
	(PLAY_SECTORS). Los dos terminan en lo mismo: un rango de FAD y un contador
	de vueltas.
*/
void cdda_reproducir_pistas(int pista_ini, int pista_fin, int repeticiones);
void cdda_reproducir_sectores(int fad_ini, int fad_fin, int repeticiones);

void cdda_pausar(void);			/* PAUSE:   se detiene donde va */
void cdda_seguir(void);			/* RELEASE: sigue desde ahi */
void cdda_parar(void);			/* STOP:    se olvida de todo */
void cdda_buscar(int fad);		/* SEEK:    se posiciona y queda pausado */

/* Como quedo la lectora al poner un disco o al reiniciarla. */
void cdda_reiniciar(void);

/* Para contestar GET_SCD y REQ_STAT. `cdda_pista()` devuelve la pista en la
   que esta la cabeza, empezando en 1, y 1 si no hay nada sonando. */
int cdda_estado(void);
int cdda_fad(void);
int cdda_pista(void);

/*
	La proxima muestra estereo, en la escala de 16 bits con signo. Devuelve 0 --
	y no toca izq/der -- si no hay nada que sonar, que es el caso normal.

	La llama el mezclador del AICA una vez por muestra. Adentro solo hay un
	indice y una copia salvo cada 588 muestras, que es cuando hace falta leer el
	sector siguiente de la imagen.
*/
int cdda_muestra(int * izq, int * der);

#endif /* _CDDA_H_ */
