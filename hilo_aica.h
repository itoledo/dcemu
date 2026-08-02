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
	hasta reloj_total, para que tenga con que trabajar. No espera a nadie.

	No lo hace en cada llamada -- el bloque entra cuatro millones de veces por
	segundo y tomar el mutex tantas veces costaria mas de lo que ahorra --, sino
	una de cada HILO_AICA_PUBLICAR. Con 64 el objetivo queda a lo sumo 3200
	ciclos atrasado, que es menos de una muestra: el hilo nunca se queda sin
	trabajo por esto.

	Con el hilo apagado llama a aica_tick() y punto, o sea el camino de siempre.
*/
void hilo_aica_publicar(void);

/*
	El par que envuelve todo acceso del SH-4 al estado del AICA.

	entrar() espera a que el hilo llegue exactamente a reloj_total y **deja el
	mutex tomado**; salir() lo suelta. Entre los dos, el hilo del AICA no puede
	avanzar, asi que el acceso ve un chip detenido en el instante emulado que
	le corresponde.

	Con el hilo apagado los dos no hacen nada.
*/
void hilo_aica_entrar(void);
void hilo_aica_salir(void);

#endif /* _HILO_AICA_H_ */
