/* Ver vram.h para el mapa de las dos ventanas. */

#include <string.h>

#include "main.h"			/* solo por los tipos; no se enlaza nada de SDL */
#include "vram.h"

/* El bloque de 8 MB, en la numeracion de 32 bits. Lo define mem.c en el
   emulador y memoria_prueba.c en las pruebas. */
extern unsigned char * video_mem;

#define VRAM_MASCARA	0x007FFFFF
#define VRAM_BANCO		0x00400000

DWORD vram_32_a_64(DWORD a32)
{
	DWORD y = a32 & (VRAM_BANCO - 1);

	return ((y & ~3u) << 1) | (((a32 >> 22) & 1) << 2) | (y & 3);
}

DWORD vram_64_a_32(DWORD a64)
{
	a64 &= VRAM_MASCARA;

	return (((a64 >> 2) & 1) << 22) | (((a64 >> 3) << 2) | (a64 & 3));
}

void vram64_leer(DWORD a64, void * p, size_t n)
{
	unsigned char *	d = (unsigned char *) p;

	/* Cabeza: hasta el proximo multiplo de 8, tramo a tramo. */
	while (n && (a64 & 7))
	{
		size_t tramo = 4 - (a64 & 3);

		if (tramo > n)
			tramo = n;

		memcpy(d, &video_mem[vram_64_a_32(a64)], tramo);

		d += tramo;
		a64 += (DWORD) tramo;
		n -= tramo;
	}

	/* Cuerpo: cada 8 bytes de la ventana son el mismo offset en ambos bancos,
	   4 del banco 0 y 4 del banco 1. */
	{
		DWORD y = ((a64 & VRAM_MASCARA) >> 3) * 4;

		while (n >= 8)
		{
			memcpy(d,     &video_mem[y],              4);
			memcpy(d + 4, &video_mem[VRAM_BANCO + y], 4);

			d += 8;
			a64 += 8;
			y += 4;
			n -= 8;
		}
	}

	/* Cola. */
	while (n)
	{
		size_t tramo = 4 - (a64 & 3);

		if (tramo > n)
			tramo = n;

		memcpy(d, &video_mem[vram_64_a_32(a64)], tramo);

		d += tramo;
		a64 += (DWORD) tramo;
		n -= tramo;
	}
}

void vram64_escribir(DWORD a64, const void * p, size_t n)
{
	const unsigned char *	s = (const unsigned char *) p;

	while (n && (a64 & 7))
	{
		size_t tramo = 4 - (a64 & 3);

		if (tramo > n)
			tramo = n;

		memcpy(&video_mem[vram_64_a_32(a64)], s, tramo);

		s += tramo;
		a64 += (DWORD) tramo;
		n -= tramo;
	}

	{
		DWORD y = ((a64 & VRAM_MASCARA) >> 3) * 4;

		while (n >= 8)
		{
			memcpy(&video_mem[y],              s,     4);
			memcpy(&video_mem[VRAM_BANCO + y], s + 4, 4);

			s += 8;
			a64 += 8;
			y += 4;
			n -= 8;
		}
	}

	while (n)
	{
		size_t tramo = 4 - (a64 & 3);

		if (tramo > n)
			tramo = n;

		memcpy(&video_mem[vram_64_a_32(a64)], s, tramo);

		s += tramo;
		a64 += (DWORD) tramo;
		n -= tramo;
	}
}
