/****************************************************************************

	AICADSPJIT - ver aicadspjit.c.

*****************************************************************************/

#ifndef _AICADSPJIT_H_
#define _AICADSPJIT_H_

/* Instala el emisor del microprograma del DSP (DCEMU_SIN_JIT_DSP=1 lo deja
   sin instalar). Vive en el binario del JIT por lo mismo que jit.c. */
void aicadspjit_iniciar(void);

#endif /* _AICADSPJIT_H_ */
