/****************************************************************************

	HILO_AICA - el AICA y el ARM7 en su propio hilo

	Ver docs/hilos-plan.md, fase 1. Este archivo es el unico que sabe que hay
	hilos de por medio: aica.c y arm7.c siguen sin incluir hilo.h y sin saber
	nada, que es lo que deja a tests/ enlazarlos sin SDL.

	**La salida no cambia.** El hilo del AICA nunca va adelante del SH-4, y
	cualquier acceso del SH-4 a su estado lo obliga a ponerse al dia hasta ese
	reloj_total exacto antes de que el acceso ocurra. Visto desde el chip, la
	secuencia de eventos y sus instantes en tiempo emulado son **identicos** a
	los del emulador de un hilo -- no parecidos: los mismos. Lo unico que cambia
	es en que momento de tiempo real se hace el trabajo.

	La prueba de eso es que el .wav de --captura-audio salga bit a bit igual que
	el de la rama sin hilos.

*****************************************************************************/

#ifndef _HILO_AICA_H_
#define _HILO_AICA_H_

#include <stddef.h>

/*
	Arranca el hilo. Con --sin-hilos, con --sin-aica o si no se pudo crear, deja
	todo como estaba y el AICA sigue avanzando desde main_loop(): el resto del
	emulador no distingue los dos casos.
*/
void hilo_aica_iniciar(void);
void hilo_aica_terminar(void);

/* 1 si el hilo esta corriendo. */
int  hilo_aica_activo(void);

/*
	Desde el bloque periodico de main_loop(): adelanta el objetivo del hilo
	hasta reloj_total, para que tenga con que trabajar. No espera a nadie y no
	toma el mutex (el objetivo es un volatile de un escritor y un lector, como
	los indices del anillo): se publica en CADA servicio, porque el calendario
	de publicaciones ES el calendario de mezclas del emulador de un hilo -- de
	el depende la exactitud del par de abajo.

	Con el hilo apagado llama a aica_tick() y punto, o sea el camino de siempre.
*/
void hilo_aica_publicar(void);

/*
	El par que envuelve todo acceso del SH-4 al estado del lado del audio
	(registros del AICA, RAM de onda, y los comandos del CDDA en cdda.c).

	entrar() espera a que el hilo llegue al ultimo objetivo PUBLICADO -- no a
	reloj_total: las muestras pendientes a este instante se mezclan despues del
	acceso, con el estado nuevo, que es exactamente lo que hace el emulador de
	un hilo -- y **deja el mutex tomado**; salir() lo suelta. Entre los dos, el
	hilo del AICA no puede avanzar, asi que el acceso ve el chip detenido en el
	mismo punto en que lo veria el camino de un hilo.

	Con el hilo apagado los dos no hacen nada.
*/
void hilo_aica_entrar(void);
void hilo_aica_salir(void);

/*
	Desde el bloque periodico, para la entrega de la linea del AICA (aica.h,
	el registro con sello): espera a que el hilo haya terminado la muestra
	`muestra` -- no a que alcance el objetivo entero, que seria lockstep. Con
	el hilo apagado no hace nada. Nunca dentro de entrar()/salir().
*/
void hilo_aica_esperar_muestra(unsigned long long muestra);

#endif /* _HILO_AICA_H_ */
