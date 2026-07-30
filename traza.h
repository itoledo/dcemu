/****************************************************************************

	TRAZA - instrumentacion del mapa de memoria (--traza-mem)

	Es la herramienta con la que se itera el arranque por BIOS: convierte "no
	arranca" en "se traba aca esperando esto". Dos cosas:

	  - cada direccion sin emular que se toca, una sola vez, con el PC que la
	    pidio;
	  - un anillo con los ultimos PC, volcado cuando el PC deja de avanzar.

	Sale por stderr y no depende de LOGGING: es diagnostico de arranque, no
	traza de ejecucion. Con --traza-mem apagado todo esto cuesta una
	comparacion por instruccion.

	Ver docs/bios-boot-plan.md, fase 1.5.

*****************************************************************************/

#ifndef _TRAZA_H_
#define _TRAZA_H_

#include <stddef.h>

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. Igual
   que main.h, para que esta cabecera se pueda incluir sola. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"
#include "options.h"		/* WATCHPOINT y su configuracion */

extern int traza_activa;

#define TRAZA_LECTURA	0
#define TRAZA_ESCRITURA	1

/* Anota un acceso a una direccion sin emular. Si esa direccion ya se reporto
   con ese mismo tipo de acceso no vuelve a imprimir nada. */
void traza_acceso(int tipo, unsigned long direccion, size_t tam, DWORD pc);

/* Un PC al anillo. Lo llama main_loop() despues de cada instruccion. */
void traza_paso(DWORD pc);

/* Vuelca el anillo a stderr. */
void traza_volcar(const char * motivo);

/* Resumen final: cuantas direcciones distintas quedaron sin emular. */
void traza_resumen(void);

/*
	Watchpoint de escritura, detras de WATCHPOINT en options.h. Lo llama el
	gancho de memwrite_fisico() en mem.h **despues** de escribir, asi que ve el
	valor que quedo; el anterior lo recuerda de la vez pasada.

	Va aca y no en mem.c porque es diagnostico de arranque, igual que el resto
	de este archivo, y porque asi puede volcar el anillo de PC.
*/
#ifdef WATCHPOINT
void watchpoint_escritura(unsigned long direccion, size_t tam);
#endif

#endif /* _TRAZA_H_ */
