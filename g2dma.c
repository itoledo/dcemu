/****************************************************************************

	G2DMA - ver g2dma.h.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "main.h"			/* solo por los tipos; no se enlaza nada de SDL */
#include "g2dma.h"
#include "mem.h"
#include "intc.h"
#include "traza.h"

unsigned long g2dma_reg[G2DMA_CANALES][G2DMA_REGS];

/*
	El fin de transferencia de cada canal, en SB_ISTNRM. Son cuatro bits
	seguidos desde el 15; asic.h de KOS los llama ASIC_EVT_G2_DMA0..3 y les da
	0x000F a 0x0012, o sea registro 0 y bits 15 a 18. El del canal 0 es ademas
	ASIC_EVT_SPU_DMA, que es el mismo evento visto desde el otro lado.
*/
#define G2DMA_EVENTO(canal)		(1u << (15 + (canal)))

/* Bit 31 de SB_**LEN: al terminar, apagar SB_**EN. */
#define G2DMA_LEN_FIN			0x80000000u

/*
	Los rangos que el documento enumera para cada lado, seccion 8.4.1.4. Un
	valor fuera de estos no arranca la transferencia: genera una interrupcion
	de "invalid setting". Se validan de verdad y no por prolijidad -- una DMA
	que copia a una direccion equivocada es justo la clase de error que este
	arbol tarda dias en encontrar, porque no deja rastro.

	La proteccion adicional de SB_G2APRO (0x005F78BC) no se mira: KOS la
	programa para desactivarla y nada mas la usa.
*/
static int rango_g2_valido(int canal, unsigned long a, unsigned long largo)
{
	unsigned long fin = a + largo;

	if (fin < a)					/* desbordo */
		return 0;

	if (canal == 0)
	{
		/* Registros del AICA, RAM de onda, y la imagen del AICA. */
		if (a >= 0x00700000 && fin <= 0x00708000)	return 1;
		if (a >= 0x00800000 && fin <= 0x00A00000)	return 1;
		if (a >= 0x02700000 && fin <= 0x03000000)	return 1;
		return 0;
	}

	/* Los tres canales de dispositivos externos. */
	if (a >= 0x01000000 && fin <= 0x02000000)	return 1;
	if (a >= 0x03000000 && fin <= 0x04000000)	return 1;
	if (a >= 0x14000000 && fin <= 0x18000000)	return 1;

	return 0;
}

static int rango_raiz_valido(unsigned long a, unsigned long largo)
{
	unsigned long fin = a + largo;

	if (fin < a)
		return 0;

	if (a >= 0x0C000000 && fin <= 0x0D000000)	return 1;	/* memoria de sistema */
	if (a >= 0x04000000 && fin <= 0x05000000)	return 1;	/* texturas, ventana de 64 bits */
	if (a >= 0x05000000 && fin <= 0x05800000)	return 1;	/* texturas, ventana de 32 bits */

	return 0;
}

/*
	La transferencia. Se hace entera y de una vez, como el resto de los DMA de
	dcemu (Maple, CH2, GD-ROM).

	Las dos direcciones vienen ya resueltas, asi que van por el par _fisico:
	memread/memwrite pasarian por la MMU y traducirian una direccion que no es
	virtual. La regla esta en CLAUDE.md y ya costo un fallo doble en el Maple.
*/
static void g2dma_ejecutar(int canal)
{
	unsigned long * r = g2dma_reg[canal];
	unsigned long g2    = r[G2DMA_STAG] & 0x1FFFFFE0u;
	unsigned long raiz  = r[G2DMA_STAR] & 0x1FFFFFE0u;
	unsigned long largo = r[G2DMA_LEN]  & 0x01FFFFE0u;
	unsigned long i;
	unsigned long origen, destino;

	/* "0x00000000: 32MByte" -- el cero no es "nada que hacer". */
	if (largo == 0)
		largo = 0x02000000;

	if (!rango_g2_valido(canal, g2, largo) || !rango_raiz_valido(raiz, largo))
	{
		if (traza_activa)
			fprintf(stderr, "traza: G2-DMA ch%d con rango invalido: "
				"G2 %08lx, raiz %08lx, %lu bytes. No se hace.\n",
				canal, g2, raiz, largo);

		r[G2DMA_ST] = 0;
		return;
	}

	/* SB_**DIR: 0 = del bus raiz al dispositivo del G2, 1 = al reves. */
	if (r[G2DMA_DIR] & 1)
	{
		origen  = g2;
		destino = raiz;
	}
	else
	{
		origen  = raiz;
		destino = g2;
	}

	if (traza_activa)
		fprintf(stderr, "traza: G2-DMA ch%d %08lx -> %08lx, %lu bytes\n",
			canal, origen, destino, largo);

	for (i = 0; i + 4 <= largo; i += 4)
	{
		DWORD palabra;

		memread_fisico(origen + i, &palabra, 4);
		memwrite_fisico(destino + i, &palabra, 4);
	}

	/* Como lo deja el hardware. SB_**ST vuelve a 0 --"DMA not in progress"-- y,
	   si el bit 31 del largo estaba puesto, la habilitacion tambien. */
	r[G2DMA_ST] = 0;

	if (r[G2DMA_LEN] & G2DMA_LEN_FIN)
		r[G2DMA_EN] = 0;

	/* Bit 4 de SB_**SUSP: "DMA transfer has ended". */
	r[G2DMA_SUSP] |= 0x10;

	intc_add(G2DMA_EVENTO(canal), 0);
}

void g2dma_leer(unsigned long direccion, void * p, size_t size)
{
	unsigned long fisica = direccion & 0x00FFFFFF;
	int canal = (int) ((fisica - G2DMA_BASE) / G2DMA_PASO);
	int reg   = (int) (((fisica - G2DMA_BASE) % G2DMA_PASO) / 4);
	DWORD dw  = (DWORD) g2dma_reg[canal][reg];

	/* "Access to the G2 bus control registers must be made as 4-byte long word
	   access." Se contesta igual lo que se pida, pero nunca mas de una palabra. */
	memcpy(p, &dw, size > sizeof(dw) ? sizeof(dw) : size);
}

void g2dma_escribir(unsigned long direccion, void * p, size_t size)
{
	unsigned long fisica = direccion & 0x00FFFFFF;
	int canal = (int) ((fisica - G2DMA_BASE) / G2DMA_PASO);
	int reg   = (int) (((fisica - G2DMA_BASE) % G2DMA_PASO) / 4);
	DWORD dw  = 0;

	memcpy(&dw, p, size > sizeof(dw) ? sizeof(dw) : size);

	switch (reg)
	{
	case G2DMA_EN:
		/* "G2-DMA is forcibly terminated by writing a 0 to this register while
		   G2-DMA is in progress." */
		g2dma_reg[canal][G2DMA_EN] = dw & 1;

		if (!(dw & 1))
			g2dma_reg[canal][G2DMA_ST] = 0;
		break;

	case G2DMA_ST:
		/* Escribir 0 esta prohibido; escribir 1 arranca, y solo si el canal
		   esta habilitado. La transferencia entera ocurre aca dentro, asi que
		   una lectura posterior siempre ve 0.

		   Que dispara la transferencia lo dice SB_**TSEL, pero KOS arranca por
		   software en los cuatro casos: para el canal del AICA escribe 5
		   --suspension habilitada mas control por patilla externa-- y para los
		   otros 4, y en los dos escribe despues este registro. */
		if ((dw & 1) && (g2dma_reg[canal][G2DMA_EN] & 1))
		{
			g2dma_reg[canal][G2DMA_ST] = 1;
			g2dma_ejecutar(canal);
		}
		break;

	case G2DMA_SUSP:
		/* De los seis bits, el guest solo escribe el 0 (pedido de suspension);
		   los otros dos son de solo lectura. Como la transferencia no se
		   fracciona, no hay nada que suspender: se guarda el pedido y ya. */
		g2dma_reg[canal][G2DMA_SUSP] =
			(g2dma_reg[canal][G2DMA_SUSP] & ~1u) | (dw & 1);
		break;

	default:
		g2dma_reg[canal][reg] = dw;
		break;
	}
}

void g2dma_reset(void)
{
	memset(g2dma_reg, 0, sizeof(g2dma_reg));
}
