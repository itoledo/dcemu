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

/* EXPERIMENTO: ver traza.c. */
extern long traza_disparo;

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

/* Los rangos de --desensamblar y --volcar. Los llama traza_resumen() al salir
   y F5 a pedido; no dependen de --traza-mem. */
void traza_rangos(void);

/*
	Informar la caida del emulador en vez de desaparecer sin decir nada.

	Un guest que salta al vacio ejecuta lo que haya y tarde o temprano tumba al
	emulador. Sin esto la caida es muda: el proceso se va, SDL se lleva stderr
	con el y no queda ni el PC del emulado, que es el unico dato que dice donde
	se descarrilo. Con esto sale ese PC, los registros, el anillo y los rangos
	de --volcar/--desensamblar, o sea lo mismo que se habria impreso saliendo
	por la puerta normal.

	No es un cazador de errores del anfitrion ni intenta seguir: informa y
	termina. No depende de --traza-mem y no cuesta nada mientras nada caiga.

	Se instala una sola vez, al principio de main().
*/
void traza_caida_instalar(void);

/*
	Watchpoint de escritura, configurado con --watchpoint=DIR[:TAM]. Lo llama el
	gancho de memwrite_fisico() en mem.h **despues** de escribir, asi que ve el
	valor que quedo; el anterior lo recuerda de la vez pasada.

	Va aca y no en mem.c porque es diagnostico de arranque, igual que el resto
	de este archivo, y porque asi puede volcar el anillo de PC.

	watchpoint_dir en cero significa apagado, y es lo que consulta el gancho
	antes de llamar: sin watchpoint la instrumentacion es una comparacion.
*/
extern unsigned long	watchpoint_dir;
extern size_t			watchpoint_tam;

void watchpoint_escritura(unsigned long direccion, size_t tam);

/*
	El de lectura, con --watchpoint-lectura=DIR[:TAM]. Cuelga de
	memread_fisico(), y contesta "quien mira esto" -- la pregunta que aparece
	cuando el guest recibe algo de la lectora y no se sabe quien lo evalua.

	Informa una vez por PC distinto, no una vez por lectura: comparar una cadena
	pasa cien veces por la misma instruccion.
*/
extern unsigned long	watchpoint_lectura_dir;
extern size_t			watchpoint_lectura_tam;

void watchpoint_lectura(unsigned long direccion, size_t tam);

/*
	--traza-desde=PC:N: las N instrucciones que siguen a la primera llegada a
	PC, desensambladas y con los registros que cambiaron. Lo consulta
	traza_paso(), que ya se llama por instruccion con --traza-mem.
*/
extern unsigned long	traza_desde_pc;
extern long				traza_desde_n;
extern long				traza_desde_salto;

/* Lo mismo pero disparado por el emulador, no por un PC: para preguntas cuyo
   disparador es un suceso del hardware. No hace nada si ya hay una traza. */
void traza_arrancar(long n);

#endif /* _TRAZA_H_ */
