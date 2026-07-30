/****************************************************************************

	OPCIONES - argumentos de la linea de comandos

	Hasta la fase 1 del arranque por BIOS el unico argumento era el ejecutable
	o la imagen. Ahora hay que poder elegir entre los dos modos de arranque
	(bootstrap de IP.BIN, que es el camino de siempre, y boot ROM real), el
	tipo de cable de video que ve el boot ROM y el estado de la lectora.

	Ver docs/bios-boot-plan.md, fase 1.1.

*****************************************************************************/

#ifndef _OPCIONES_H_
#define _OPCIONES_H_

/* Tipo de cable de video. El valor es el que el boot ROM espera leer en los
   bits 9-8 de PDTRA; no son indices inventados. */
#define CABLE_VGA			0
#define CABLE_RGB			2
#define CABLE_COMPUESTO		3

/* Estado inicial de la lectora. AUTO decide segun haya imagen montada o no. */
#define BANDEJA_AUTO		0
#define BANDEJA_DISCO		1
#define BANDEJA_VACIA		2
#define BANDEJA_ABIERTA		3

struct opciones_t
{
	int				arranque_bios;	/* 1: PC = 0xA0000000 en vez del bootstrap */
	int				hacks_bios;		/* 1: parchar los vectores de syscall */
	int				traza_mem;		/* 1: reportar accesos sin emular */

	/* 1: no dejar que el emulador corra mas rapido que una consola. Solo frena,
	   nunca acelera, asi que en las partes donde dcemu ya es mas lento no hace
	   nada. Ver docs/clock-plan.md, fase 4. */
	int				limitar;
	int				cable;			/* CABLE_* */
	int				bandeja;		/* BANDEJA_* */
	const char *	imagen;			/* .iso/.cue/.bin, o NULL */
};

extern struct opciones_t opciones;

/* 0: seguir. 1: error en la linea de comandos. -1: --ayuda, salir sin error. */
int opciones_parsear(int argc, char ** argv);

void opciones_ayuda(const char * programa);

#endif /* _OPCIONES_H_ */
