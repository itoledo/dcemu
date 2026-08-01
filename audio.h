/****************************************************************************

	AUDIO - la salida del AICA: la tarjeta de sonido y el volcado a .wav

	Es la unica parte del sonido que toca SDL. aica.c produce cuadros estereo a
	44100 Hz en un anillo y no sabe quien los consume; aqui se los lleva la
	callback de audio de SDL, el volcado a archivo, o los dos.

	**El volcado existe antes que la reproduccion, y a proposito.** El sonido
	es lo unico del arbol que no se puede verificar leyendo un volcado de
	memoria, y "suena bien" no es una medida. `--captura-audio=ARCHIVO.wav`
	es el gemelo de `--captura-gl`: guarda lo que **el mezclador produjo**, no
	lo que el anfitrion hizo con ello. Sobre ese archivo se cuentan muestras no
	nulas, valores distintos, RMS y pico, igual que se cuentan los colores de
	un BMP.

	La leccion viene de `--captura-gl`: capturar la salida del sistema
	anfitrion depende de su mezclador y falla en silencio, exactamente como
	capturar la ventana.

	Ver docs/aica-plan.md, "Como se prueba".

*****************************************************************************/

#ifndef _AUDIO_H_
#define _AUDIO_H_

/*
	Abre el dispositivo de audio y, si opciones.captura_audio no es NULL, el
	archivo. Ninguna de las dos cosas es fatal: si no hay tarjeta el emulador
	sigue igual, solo que mudo.
*/
void audio_iniciar(void);

/*
	Vacia el anillo hacia el archivo. Se llama una vez por cuadro desde
	main_loop(); la reproduccion no pasa por aqui -- de eso se encarga la
	callback de SDL, en su propio hilo.

	Sin dispositivo de audio abierto, esta funcion es la unica que consume el
	anillo, y por eso una corrida con --captura-audio y sin tarjeta produce el
	mismo archivo que una con las dos.
*/
void audio_volcar(void);

/* Cierra el .wav dejando su cabecera con los largos definitivos. Lo llama
   traza_resumen(), o sea el mismo camino que cerrar la ventana. */
void audio_terminar(void);

/* Lo que la traza reporta al salir: cuantos cuadros se produjeron, cuantos se
   perdieron por anillo lleno y cual fue el pico. */
extern unsigned long long	audio_cuadros;
extern unsigned long long	audio_perdidos;
extern int					audio_pico;

#endif /* _AUDIO_H_ */
