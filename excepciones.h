/****************************************************************************

	EXCEPCIONES - la entrada a una excepcion sincrona del SH-4

	Dos piezas que empezaron dentro de la MMU y ahora comparten la MMU y la
	FPU:

	  - `excepcion_entrar()`, la secuencia de excepcion del procesador: guarda
		SSR/SPC/SGR, deja EXPEVT y salta a VBR + vector. `intc()` es el caso
		particular de las interrupciones y la usa igual.

	  - El **aborto de instruccion**. Las excepciones generales del SH-4 son de
		reejecucion: el manejador arregla lo que haya que arreglar, hace RTE y
		la instruccion se repite entera. Pero los handlers de dcemu mutan
		registros alrededor del acceso (`MOV.L @Rn+`, `FMUL` sobre su propio
		destino), asi que hay que poder deshacerlo. `main_loop()` saca una
		instantanea del contexto antes de cada instruccion y arma el salto;
		quien detecta la falta llama a `excepcion_abortar()`, que no devuelve.

	Desenrollar con longjmp resuelve gratis el caso de la ranura de retardo:
	los saltos de branch.c ejecutan la ranura con un `core.execute()` anidado,
	asi que el salto sale de los dos niveles y la instantanea restaurada deja
	PC en la instruccion de salto, que es justo lo que SPC tiene que valer.

	Ver docs/mmu-plan.md (fase 5) y docs/sh4-conformidad.md.

*****************************************************************************/

#ifndef _EXCEPCIONES_H_
#define _EXCEPCIONES_H_

#include <setjmp.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* ------------------------------------------------------------------------ */
/* Codigos de EXPEVT y vectores                                             */
/* ------------------------------------------------------------------------ */

/* Las tres de la FPU. KOS las llama EXC_FPU, EXC_GENERAL_FPU y EXC_SLOT_FPU,
   y las tres son de reejecucion. */
#define EXC_FPU_OPERACION	0x120	/* Cause coincidio con su bit de Enable */
#define EXC_FPU_GENERAL		0x800	/* instruccion de FPU con SR.FD puesto */
#define EXC_FPU_RANURA		0x820	/* idem, en la ranura de retardo */

/* El user break del UBC. Antes y despues de ejecutar comparten el codigo:
   KOS instala el mismo manejador para EXC_USER_BREAK_PRE y _POST. */
#define EXC_UBC_BREAK		0x1E0

/* Todas las excepciones generales entran por el mismo vector. El fallo de TLB
   tiene el suyo, que es lo que lo hace barato en hardware real. */
#define EXC_VEC_GENERAL		0x100

/* ------------------------------------------------------------------------ */
/* La secuencia de excepcion                                                */
/* ------------------------------------------------------------------------ */

void excepcion_entrar(DWORD codigo, DWORD vector);

/* ------------------------------------------------------------------------ */
/* El aborto de instruccion                                                 */
/* ------------------------------------------------------------------------ */

extern jmp_buf	excepcion_salto;
extern int		excepcion_salto_armado;

/* Datos de la falta pendiente, para que main_loop() arme la excepcion despues
   de restaurar la instantanea. */
extern DWORD	excepcion_codigo;
extern DWORD	excepcion_vector;

/*
	Deja la excepcion preparada y sale por el salto. **No devuelve**, salvo que
	no haya salto armado -- que es el caso de un acceso disparado por el propio
	emulador y no por una instruccion, donde no hay nada que reejecutar.
*/
void excepcion_abortar(DWORD codigo, DWORD vector);

/*
	1 cuando main_loop() tiene que sacar instantanea y armar el salto: o la MMU
	traduce, o la FPU esta deshabilitada, o hay algun bit de Enable en FPSCR.

	Es la generalizacion de la vieja prueba contra `mmu_activa`. En todo lo que
	corre hoy vale cero y el camino rapido cuesta exactamente lo de antes: una
	comparacion contra cero. Lo recalculan mmu.c al tocar MMUCR.AT y sh4emu.c
	en UpdateSR() y UpdateFPSCR().
*/
extern int excepcion_vigilar;

void excepcion_actualizar_vigilancia(void);

/*
	La instantanea que hace reejecutable a una instruccion. main_loop() la toma
	antes de ejecutar y la restaura al volver de un aborto.

	No alcanza con copiar core.context: los dos bancos de registros de punto
	flotante viven fuera y el contexto solo guarda punteros a ellos. Por eso
	estan aca y no en main.c, donde antes se hacia con un memcpy suelto.
*/
void excepcion_instantanea_tomar(void);
void excepcion_instantanea_restaurar(void);

/*
	Lo poco que tiene que sobrevivir a la instantanea. La excepcion de operacion
	de FPU deja el campo Cause de FPSCR con la causa que disparo -- y el campo
	Flag sin tocar, que el manual es explicito -- y eso se escribe **despues**
	de restaurar el contexto, no antes. Lo llama main_loop().
*/
void excepcion_reponer(void);

/* La causa que main_loop() tiene que reponer en FPSCR. La escribe floatsimple.c
   antes de abortar. */
extern DWORD excepcion_fpu_cause;

/* ------------------------------------------------------------------------ */
/* Ranura de retardo                                                        */
/* ------------------------------------------------------------------------ */

/*
	1 mientras corre la instruccion de una ranura de retardo. Solo sirve para
	distinguir la excepcion de FPU deshabilitada general (0x800) de la de ranura
	(0x820): el resto del emulador no la mira. La suben y bajan los saltos de
	branch.c con EJECUTAR_RANURA(), y main_loop() la limpia al volver de un
	aborto, porque el longjmp se salta el bajado.
*/
extern int en_ranura_retardo;

#endif /* _EXCEPCIONES_H_ */
