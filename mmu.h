/****************************************************************************

	MMU - la ventana de control P4 del SH-4

	Fase 1 de docs/mmu-plan.md: los registros de la MMU y las ocho ventanas
	de 0xF0000000 a 0xF7FFFFFF por las que el software llega a los arreglos
	de la cache y de la TLB.

	Todavia NO hay traduccion. MMUCR.AT se registra pero no se obedece, y
	todas las direcciones se siguen resolviendo como hasta ahora. Lo que si
	cambia es que estos accesos dejan de caer en mem_write_error(): un
	binario de KOS barre las 512 entradas del arreglo de direcciones de la
	cache de operandos al arrancar, y hasta ahora cada una de esas
	escrituras se perdia en silencio y ademas inundaba --traza-mem.

	Vive aparte de mem.c a proposito, igual que sistema.c y gdrom.c: mem.c
	no se puede enlazar en las pruebas unitarias porque arrastra SDL y
	OpenGL por los callbacks del PVR.

*****************************************************************************/

#ifndef _MMU_H_
#define _MMU_H_

#include <stddef.h>
#include <setjmp.h>

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. Igual
   que main.h, para que esta cabecera se pueda incluir sola. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* ------------------------------------------------------------------------ */
/* Las ocho ventanas de control, por el byte alto de la direccion            */
/* ------------------------------------------------------------------------ */

#define MMU_P4_IC_DIR		0xF0		/* cache de instrucciones, direcciones */
#define MMU_P4_IC_DAT		0xF1		/* cache de instrucciones, datos */
#define MMU_P4_ITLB_DIR		0xF2		/* ITLB, direcciones */
#define MMU_P4_ITLB_DAT		0xF3		/* ITLB, datos 1 y 2 */
#define MMU_P4_OC_DIR		0xF4		/* cache de operandos, direcciones */
#define MMU_P4_OC_DAT		0xF5		/* cache de operandos, datos */
#define MMU_P4_UTLB_DIR		0xF6		/* UTLB, direcciones */
#define MMU_P4_UTLB_DAT		0xF7		/* UTLB, datos 1 y 2 */

/* Los arreglos de datos vienen en dos mitades; las separa el bit 23 de la
   direccion, no el byte alto. */
#define MMU_BIT_DATOS2		0x00800000

/* Bit asociativo de los arreglos de direcciones. En la TLB es el bit 7; en la
   cache, el bit 3. Escribir con el puesto busca por etiqueta en vez de indexar. */
#define MMU_BIT_A_TLB		0x00000080
#define MMU_BIT_A_CACHE		0x00000008

/* ------------------------------------------------------------------------ */
/* La TLB, todavia como dato crudo                                          */
/* ------------------------------------------------------------------------ */

#define MMU_ITLB_ENTRADAS	4
#define MMU_UTLB_ENTRADAS	64

/* Las entradas se guardan tal como las escribe el software, empaquetadas segun
   el manual. Desempaquetarlas en campos es la fase 2; hacerlo ahora seria
   codigo sin nadie que lo consuma. */
extern DWORD mmu_itlb_dir[MMU_ITLB_ENTRADAS];
extern DWORD mmu_itlb_dat1[MMU_ITLB_ENTRADAS];
extern DWORD mmu_itlb_dat2[MMU_ITLB_ENTRADAS];

extern DWORD mmu_utlb_dir[MMU_UTLB_ENTRADAS];
extern DWORD mmu_utlb_dat1[MMU_UTLB_ENTRADAS];
extern DWORD mmu_utlb_dat2[MMU_UTLB_ENTRADAS];

/* Bits de una entrada, tal como los ve el software por los arreglos
   memoria-mapeados y por PTEL. V y D son un solo bit del chip visible desde
   las dos mitades, y por eso aparecen dos veces con posiciones distintas. */
#define MMU_BIT_V		0x00000100		/* validez: direcciones y datos 1 */
#define MMU_BIT_SH		0x00000002		/* datos 1: pagina compartida */
#define MMU_BIT_D_DIR	0x00000200		/* sucia, arreglo de direcciones */
#define MMU_BIT_D_DAT	0x00000004		/* sucia, arreglo de datos 1 */

/* Indice de entrada dentro de cada arreglo: ITLB bits 9-8 (4 entradas), UTLB
   bits 13-8 (64). Expuestos porque son lo primero que se prueba. */
#define MMU_ITLB_INDICE(dir)	(((dir) >> 8) & 0x03)
#define MMU_UTLB_INDICE(dir)	(((dir) >> 8) & 0x3F)

/* ------------------------------------------------------------------------ */
/* Acceso                                                                   */
/* ------------------------------------------------------------------------ */

/* Deja la TLB en blanco. La llama regmem_setup(). */
void mmu_reset(void);

/* Atienden las zonas 0xF0 a 0xF7. Misma firma que mem_access_read_t y
   mem_access_write_t, para enchufarlas en mem_hash_read/mem_hash_write. */
void mmu_p4_read(unsigned long direccion, void * p, size_t size);
void mmu_p4_write(unsigned long direccion, void * p, size_t size);

/* Lo llama regmap_write() cuando alguien toca MMUCR: atiende TI y deja
   mmu_activa al dia. Devuelve 1 si AT quedo en 1. */
int mmu_mmucr_escrito(DWORD valor);

/* Bits de MMUCR que hacen falta para eso. */
#define MMUCR_AT			0x00000001		/* traduccion activa */
#define MMUCR_TI			0x00000004		/* invalidar TLB */
#define MMUCR_SV			0x00000100		/* espacio unico */
#define MMUCR_SQMD			0x00000200		/* store queues privilegiadas */
#define MMUCR_URC(v)		(((v) >> 10) & 0x3F)
#define MMUCR_URB(v)		(((v) >> 18) & 0x3F)

/* ------------------------------------------------------------------------ */
/* Traduccion                                                               */
/* ------------------------------------------------------------------------ */

/*
	Copia de MMUCR.AT. Se consulta en cada acceso a memoria, asi que vive
	aparte del registro para no desreferenciar un puntero por acceso. Con la
	MMU apagada -- o sea, en todo lo que corre hoy -- el camino rapido es una
	comparacion contra cero y nada mas.
*/
extern int mmu_activa;

#define MMU_LECTURA		0
#define MMU_ESCRITURA	1

/*
	Traduce una direccion virtual. Si falla NO devuelve: deja la excepcion
	preparada y sale por excepcion_abortar() (ver excepciones.h).

	La direccion que devuelve es fisica *vista por la ventana P2*, o sea
	fisica | 0xA0000000. No es capricho: la tabla de zonas de dcemu mezcla
	bindings fisicos (0x00 BIOS, 0x0C RAM, 0x10 TA) con bindings P2 (0xA0 PVR
	y bloque de control, 0xA4/0xA5 video, 0xAC RAM), y la ventana P2 es la
	unica con cobertura completa: por ejemplo el bloque 0x005Fxxxx solo lo
	atiende pvr_read/pvr_write en la zona 0xA0, no bios_read en la 0x00.
	Como dcemu no emula cache, que la traduccion salga por la ventana sin
	cachear no tiene efecto observable.
*/
DWORD mmu_traducir(DWORD direccion, int escritura);

/* Carga UTLB[URC] desde PTEH/PTEL/PTEA. La llama el opcode LDTLB. */
void mmu_ldtlb(DWORD pteh, DWORD ptel, DWORD ptea, int urc);

/* ------------------------------------------------------------------------ */
/* Excepciones                                                              */
/* ------------------------------------------------------------------------ */

/* Codigos de EXPEVT. */
#define MMU_EXC_FALLO_R		0x040		/* fallo de TLB en lectura */
#define MMU_EXC_FALLO_W		0x060		/* fallo de TLB en escritura */
#define MMU_EXC_PRIMERA_W	0x080		/* primera escritura a la pagina */
#define MMU_EXC_PROT_R		0x0A0		/* violacion de proteccion, lectura */
#define MMU_EXC_PROT_W		0x0C0		/* violacion de proteccion, escritura */
#define MMU_EXC_DIR_R		0x0E0		/* error de direccion, lectura */
#define MMU_EXC_DIR_W		0x100		/* error de direccion, escritura */

/* Desplazamientos de vector. El fallo de TLB tiene el suyo, que es lo que lo
   hace barato en hardware real y lo que espera el manejador de KOS. */
#define MMU_VEC_FALLO		0x400
#define MMU_VEC_GENERAL		0x100

/*
	El salto de aborto vive en excepciones.h: lo comparten la MMU y la FPU.
	mmu_traducir() sale por excepcion_abortar() ante un fallo, y los datos de
	la falta pendiente quedan en excepcion_codigo / excepcion_vector.
*/
extern DWORD	mmu_exc_direccion;

#endif /* _MMU_H_ */
