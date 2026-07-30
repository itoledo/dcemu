/****************************************************************************

	MANDO - un gamepad del anfitrion como mando de Dreamcast

	En Windows se lee por XInput, que es la API estandar para gamepads desde
	Vista y la que entiende cualquier mando compatible con Xbox -- o sea,
	practicamente todos los que se venden. El calce con el mando de Dreamcast
	es casi exacto: cruceta, cuatro botones frontales, Start y un stick
	analogico, mas dos gatillos que en las dos consolas son **analogicos** de
	0 a 255 y no interruptores.

	Lo que no calza:

	  - el Dreamcast no tiene botones traseros digitales ni segundo stick, asi
	    que LB/RB y el stick derecho no se usan;
	  - tiene C y D, que ningun mando moderno tiene y que casi ningun juego usa.

	La DLL se carga en tiempo de ejecucion con LoadLibrary, no en el enlace: asi
	el emulador arranca igual donde no este, y el build no gana una dependencia.
	Fuera de Windows todo esto se compila a funciones vacias y el teclado sigue
	siendo la unica entrada.

*****************************************************************************/

#ifndef _MANDO_H_
#define _MANDO_H_

#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* Estado del mando en el formato que espera el Maple: los botones activos en
   bajo, como CONT_*, y los ejes de 0 a 255. */
struct mando_estado_t
{
	WORD	botones;	/* bits CONT_*, en 1 los que NO estan pulsados */
	BYTE	ltrig;		/* gatillos, 0 suelto */
	BYTE	rtrig;
	BYTE	joyx;		/* stick, 128 en el centro */
	BYTE	joyy;
	int		conectado;
};

/* Busca la API del anfitrion. Devuelve 1 si la encontro. Llamarla una vez. */
int mando_iniciar(void);

void mando_terminar(void);

/*
	Lee el primer mando conectado. Devuelve 1 si hay uno y `dest` quedo con su
	estado; 0 si no hay ninguno, y entonces `dest` queda en reposo.

	Consulta el estado en el momento -- XInput no manda eventos --, asi que se
	llama una vez por cuadro.
*/
int mando_leer(struct mando_estado_t * dest);

/* Deja el estado en reposo: nada pulsado, ejes centrados. */
void mando_reposo(struct mando_estado_t * dest);

#endif /* _MANDO_H_ */
