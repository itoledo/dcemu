/****************************************************************************

	ARM7JIT - ver arm7jit.c. Compila solo con DCEMU_JIT: emite x86-64 y
	reserva su arena con VirtualAlloc, asi que vive donde vive jit.c.

*****************************************************************************/

#ifndef _ARM7JIT_H_
#define _ARM7JIT_H_

/* Reserva el arena e instala el emisor sobre los bloques de arm7.c.
   DCEMU_SIN_JIT_ARM=1 lo deja sin instalar, que es el A/B. Idempotente:
   la suite lo llama para instalar y desinstala con
   arm7_blq_instalar_emisor(NULL). */
void arm7jit_iniciar(void);

/* Cuantos bloques se emitieron y cuantos se declinaron. Va junto a los
   resumenes del ARM. */
void arm7jit_resumen(void);

#endif /* _ARM7JIT_H_ */
