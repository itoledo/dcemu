/****************************************************************************

	G2DMA - los cuatro canales de DMA del bus G2

	El bus G2 es por donde cuelgan el AICA y el modem. Su bloque de control en
	el Holly tiene cuatro motores de DMA identicos --solo cambia que los
	dispara-- y el canal 0 es el del AICA: es el camino por el que el guest
	sube muestras a la RAM de sonido sin gastar CPU.

	  ch0 AICA, ch1 externo 1, ch2 externo 2, ch3 herramientas de desarrollo

	Referencia: "Dreamcast/Dev.Box System Architecture" (Sega, 99/09/03),
	seccion 8.4.1.4, y la lista de interrupciones de 8.5.2.

	Los ocho registros de cada canal caian en el respaldo del bloque de control
	(ES_CONTROL en mem.c), asi que el guest escribia 1 en SB_ADST, lo volvia a
	leer, le contestaba 1 --"DMA en curso"-- y se quedaba ahi para siempre. Es
	la misma forma que tenian el CH2 DMA y SB_G1SYSM: algo que el guest pide,
	que dcemu acepta sin hacer nada y sin decir nada.

	Ver docs/aica-plan.md, fase 2.

*****************************************************************************/

#ifndef _G2DMA_H_
#define _G2DMA_H_

/* El bloque de los cuatro canales. El resto del area de control del G2
   --0x005F7880 en adelante: SB_G2ID, SB_G2DSTO, SB_G2TRTO, SB_G2APRO-- se
   queda en el respaldo del bloque de control, que es lo que el guest quiere
   de ellos. */
#define G2DMA_BASE			0x005F7800
#define G2DMA_FIN			0x005F787F
#define G2DMA_CANALES		4
#define G2DMA_PASO			0x20		/* bytes entre un canal y el siguiente */

#define G2DMA_ES_REGISTRO(fisica) \
	((fisica) >= G2DMA_BASE && (fisica) <= G2DMA_FIN)

/* Desplazamientos dentro de un canal, en palabras. Los nombres son los del
   canal 0 (AICA); los otros tres tienen los mismos con otro prefijo. */
#define G2DMA_STAG		0	/* 0x00  direccion de partida del lado del G2 */
#define G2DMA_STAR		1	/* 0x04  ... del lado de la memoria de sistema/texturas */
#define G2DMA_LEN		2	/* 0x08  largo en bytes, multiplo de 32; bit 31: apagar EN al terminar */
#define G2DMA_DIR		3	/* 0x0C  0: raiz -> G2, 1: G2 -> raiz */
#define G2DMA_TSEL		4	/* 0x10  que la dispara */
#define G2DMA_EN		5	/* 0x14  habilitacion */
#define G2DMA_ST		6	/* 0x18  arranque, y estado al leerlo */
#define G2DMA_SUSP		7	/* 0x1C  suspension */
#define G2DMA_REGS		8

/* Estado visible para las pruebas. Es el archivo de registros tal cual: el
   indice mayor es el canal y el menor uno de los G2DMA_* de arriba. */
extern unsigned long g2dma_reg[G2DMA_CANALES][G2DMA_REGS];

void g2dma_leer(unsigned long direccion, void * p, size_t size);
void g2dma_escribir(unsigned long direccion, void * p, size_t size);
void g2dma_reset(void);

#endif /* _G2DMA_H_ */
