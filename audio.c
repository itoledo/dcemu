/****************************************************************************

	AUDIO - ver audio.h.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "main.h"
#include "audio.h"
#include "aica.h"
#include "opciones.h"
#include "traza.h"

unsigned long long	audio_cuadros;
unsigned long long	audio_perdidos;
int					audio_pico;

static FILE *	wav;
static unsigned long wav_bytes;
static int		dispositivo_abierto;

/* El dispositivo, como lo entrega SDL3: un flujo atado al dispositivo por
   omision, que pide muestras por la callback de abajo. */
static SDL_AudioStream * flujo;

/*
	Un segundo anillo, para poder escuchar y medir a la vez.

	Con el dispositivo abierto quien vacia el anillo del AICA es la callback de
	SDL, en su propio hilo, y si el volcado a archivo compitiera por las mismas
	muestras cada uno se llevaria la mitad: el .wav saldria entrecortado y el
	altavoz tambien. Asi que la callback copia aqui lo que consumio y el hilo
	principal lo lleva al archivo. Un productor y un consumidor otra vez, y el
	fwrite no sale del hilo del emulador.
*/
#define ECO_CUADROS		16384

static short			eco[ECO_CUADROS * 2];
static volatile unsigned eco_cabeza;
static volatile unsigned eco_cola;

/* ------------------------------------------------------------------------ */
/* El archivo                                                               */
/* ------------------------------------------------------------------------ */

static void escribir_le32(FILE * f, unsigned long v)
{
	fputc((int) (v & 0xFF), f);
	fputc((int) ((v >> 8) & 0xFF), f);
	fputc((int) ((v >> 16) & 0xFF), f);
	fputc((int) ((v >> 24) & 0xFF), f);
}

static void escribir_le16(FILE * f, unsigned v)
{
	fputc((int) (v & 0xFF), f);
	fputc((int) ((v >> 8) & 0xFF), f);
}

/* La cabecera de un WAV PCM de 44100 Hz, 16 bits, estereo. Los dos largos se
   corrigen al cerrar. */
static void wav_cabecera(void)
{
	fwrite("RIFF", 1, 4, wav);
	escribir_le32(wav, 36);					/* se corrige al cerrar */
	fwrite("WAVEfmt ", 1, 8, wav);
	escribir_le32(wav, 16);					/* tamano del bloque fmt */
	escribir_le16(wav, 1);					/* PCM */
	escribir_le16(wav, 2);					/* canales */
	escribir_le32(wav, 44100);
	escribir_le32(wav, 44100 * 4);			/* bytes por segundo */
	escribir_le16(wav, 4);					/* alineacion de bloque */
	escribir_le16(wav, 16);					/* bits por muestra */
	fwrite("data", 1, 4, wav);
	escribir_le32(wav, 0);					/* se corrige al cerrar */
}

/* ------------------------------------------------------------------------ */
/* El dispositivo                                                           */
/* ------------------------------------------------------------------------ */

/*
	La callback de SDL corre en **otro hilo**: no puede tocar nada del
	emulador. Solo vacia el anillo, que para eso es de un productor y un
	consumidor con indices volatiles.

	Si el emulador va mas lento que el tiempo real el anillo se queda corto y
	se completa con silencio, que es lo que hay que hacer: repetir lo anterior
	suena a chasquido.

	SDL3 no entrega un bufer que llenar sino una cantidad: `adicional` son los
	bytes que el dispositivo necesita ahora para no quedarse sin nada, y se le
	entregan con SDL_PutAudioStreamData. Se hace de a lotes de 1024 cuadros,
	que es el tamano de bufer que se pide al abrir, asi el trabajo por llamada
	es el mismo que con SDL 1.2.
*/
static void SDLCALL audio_callback(void * datos, SDL_AudioStream * destino,
	int adicional, int total)
{
	short lote[1024 * 2];

	(void) datos;
	(void) total;

	while (adicional >= 4)
	{
		unsigned cuadros = (unsigned) (adicional / 4);
		unsigned dados;

		if (cuadros > 1024)
			cuadros = 1024;

		dados = aica_salida_leer(lote, cuadros);

		if (dados < cuadros)
			memset(lote + dados * 2, 0, (cuadros - dados) * 4);

		/* Copia para el volcado, si lo hay. Se descarta lo que no entre:
		   perder una parte del .wav es mejor que trabar la callback de
		   audio. */
		if (wav)
		{
			unsigned i;

			for (i = 0; i < dados; i++)
			{
				unsigned proxima = (eco_cabeza + 1) % ECO_CUADROS;

				if (proxima == eco_cola)
					break;

				eco[eco_cabeza * 2]     = lote[i * 2];
				eco[eco_cabeza * 2 + 1] = lote[i * 2 + 1];
				eco_cabeza = proxima;
			}
		}

		SDL_PutAudioStreamData(destino, lote, (int) (cuadros * 4));
		adicional -= (int) (cuadros * 4);
	}
}

void audio_iniciar(void)
{
	if (opciones.captura_audio)
	{
		wav = fopen(opciones.captura_audio, "wb");

		if (!wav)
			fprintf(stderr, "audio: no se pudo crear %s\n",
				opciones.captura_audio);
		else
		{
			wav_cabecera();
			wav_bytes = 0;
		}
	}

	if (opciones.sin_audio)
		return;

	{
		SDL_AudioSpec pedido;

		memset(&pedido, 0, sizeof(pedido));

		pedido.freq     = 44100;
		pedido.format   = SDL_AUDIO_S16;
		pedido.channels = 2;

		if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
		{
			fprintf(stderr, "audio: no hay subsistema de audio (%s). "
				"El emulador sigue, mudo.\n", SDL_GetError());
			return;
		}

		/* El tamano del bufer del dispositivo: SDL 1.2 lo llevaba en el spec
		   (samples = 1024) y SDL3 lo toma de esta pista al abrir. */
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");

		flujo = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
			&pedido, audio_callback, NULL);

		if (flujo == NULL)
		{
			fprintf(stderr, "audio: no se pudo abrir el dispositivo (%s). "
				"El emulador sigue, mudo.\n", SDL_GetError());
			return;
		}

		dispositivo_abierto = 1;
		SDL_ResumeAudioStreamDevice(flujo);	/* se abre en pausa */

		if (traza_activa)
		{
			SDL_AudioSpec dado;
			int cuadros = 0;

			memset(&dado, 0, sizeof(dado));
			SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(flujo), &dado,
				&cuadros);

			fprintf(stderr, "traza: audio: dispositivo abierto a %d Hz, "
				"%d canales, buffer de %d cuadros\n",
				dado.freq, dado.channels, cuadros);
		}
	}
}

/* Saca del anillo de eco lo que la callback ya reprodujo. */
static unsigned eco_leer(short * destino, unsigned cuadros)
{
	unsigned cabeza = eco_cabeza;
	unsigned cola   = eco_cola;
	unsigned hay = (cabeza >= cola) ? (cabeza - cola)
	                                : (ECO_CUADROS - cola + cabeza);
	unsigned n = (cuadros < hay) ? cuadros : hay;
	unsigned i;

	for (i = 0; i < n; i++)
	{
		destino[i * 2]     = eco[cola * 2];
		destino[i * 2 + 1] = eco[cola * 2 + 1];
		cola = (cola + 1) % ECO_CUADROS;
	}

	eco_cola = cola;

	return n;
}

/* ------------------------------------------------------------------------ */

void audio_volcar(void)
{
	short buffer[1024 * 2];
	unsigned n;

	if (!wav && dispositivo_abierto)
		return;						/* la callback ya vacia el anillo */

	/* De donde salen las muestras depende de quien las este consumiendo: con
	   el dispositivo abierto, del eco que deja la callback; sin el, del anillo
	   del AICA directamente. Asi el .wav sale igual en los dos casos. */
	while ((n = dispositivo_abierto ? eco_leer(buffer, 1024)
	                                : aica_salida_leer(buffer, 1024)) > 0)
	{
		unsigned i;

		for (i = 0; i < n * 2; i++)
		{
			int v = buffer[i];

			if (v < 0)
				v = -v;

			if (v > audio_pico)
				audio_pico = v;
		}

		audio_cuadros += n;

		if (wav)
		{
			fwrite(buffer, 4, n, wav);
			wav_bytes += n * 4;
		}

		if (n < 1024)
			break;
	}
}

void audio_terminar(void)
{
	if (dispositivo_abierto)
	{
		/* Destruir el flujo cierra tambien el dispositivo que
		   SDL_OpenAudioDeviceStream abrio para el. */
		SDL_PauseAudioStreamDevice(flujo);
		SDL_DestroyAudioStream(flujo);
		flujo = NULL;
		dispositivo_abierto = 0;
	}

	if (!wav)
		return;

	/* Los dos largos que quedaron en cero al abrir. */
	fseek(wav, 4, SEEK_SET);
	escribir_le32(wav, 36 + wav_bytes);
	fseek(wav, 40, SEEK_SET);
	escribir_le32(wav, wav_bytes);

	fclose(wav);
	wav = NULL;

	fprintf(stderr, "audio: %s, %lu cuadros (%.2f s), pico %d de 32767\n",
		opciones.captura_audio,
		(unsigned long) (wav_bytes / 4),
		(double) (wav_bytes / 4) / 44100.0,
		audio_pico);
}
