/****************************************************************************

	Pruebas de vram.c: las dos ventanas de la RAM de video del PVR.

	El mapa esta descrito en vram.h. El caso de referencia sale de medir
	pvr-fb_tex: su framebuffer esta en 0x004A7480 por la ventana de 32 bits y
	KOS le calcula 0x0094E900 de textura multiplicando por dos -- ese producto
	es este mapa, con el banco cayendo en el bit que la palabra de control no
	guarda y que la demo compensa con medio texel de U.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "vram.h"

extern unsigned char * video_mem;

#define VRAM_TAM	(8 * 1024 * 1024)
#define BANCO		0x00400000

/* ------------------------------------------------------------------------ */

static void conversion_ida_y_vuelta(void)
{
	static const DWORD muestras[] =
	{
		0x000000, 0x000001, 0x000002, 0x000003, 0x000004, 0x000007,
		0x000008, 0x0000FF, 0x001000, 0x3FFFFC, 0x3FFFFF,
		0x400000, 0x400004, 0x4A7480, 0x7FFFFF,
	};
	size_t i;

	for (i = 0; i < sizeof(muestras) / sizeof(muestras[0]); i++)
	{
		DWORD a32 = muestras[i];

		ESPERAR_U32(vram_64_a_32(vram_32_a_64(a32)), a32);
	}

	/* Y al reves, sobre toda la forma del offset de 64. */
	for (i = 0; i < sizeof(muestras) / sizeof(muestras[0]); i++)
	{
		DWORD a64 = muestras[i];

		ESPERAR_U32(vram_32_a_64(vram_64_a_32(a64)), a64);
	}
}

static void conversion_es_biyectiva_por_tramos(void)
{
	/* Dos direcciones de 32 distintas nunca caen en la misma de 64. Se
	   recorre un tramo denso alrededor del borde de banco. */
	static unsigned char visto[0x200];
	DWORD a32;

	memset(visto, 0, sizeof(visto));

	for (a32 = 0x3FFFC0; a32 < 0x3FFFC0 + 0x40; a32++)
	{
		DWORD a64 = vram_32_a_64(a32);

		ESPERAR_U32(visto[a64 & 0x1FF], 0);
		visto[a64 & 0x1FF] = 1;
	}

	for (a32 = 0x400000; a32 < 0x400040; a32++)
	{
		DWORD a64 = vram_32_a_64(a32);

		ESPERAR_U32(visto[a64 & 0x1FF], 0);
		visto[a64 & 0x1FF] = 1;
	}
}

static void el_caso_medido_de_fb_tex(void)
{
	/* 0x4A7480 esta en el banco 1, offset 0xA7480. En la ventana de 64 eso es
	   el offset duplicado con el banco en el bit 2. */
	ESPERAR_U32(vram_32_a_64(0x004A7480), 0x0014E904);
	ESPERAR_U32(vram_64_a_32(0x0014E904), 0x004A7480);

	/* La misma celda sin el bit de banco es la del banco 0. */
	ESPERAR_U32(vram_64_a_32(0x0014E900), 0x000A7480);

	/* El banco alterna cada 4 bytes en la ventana de 64... */
	ESPERAR_U32(vram_64_a_32(0x00000000), 0x00000000);
	ESPERAR_U32(vram_64_a_32(0x00000004), 0x00400000);
	ESPERAR_U32(vram_64_a_32(0x00000008), 0x00000004);
	ESPERAR_U32(vram_64_a_32(0x0000000C), 0x00400004);

	/* ...y los bytes dentro del tramo de 4 quedan en orden. */
	ESPERAR_U32(vram_64_a_32(0x00000001), 0x00000001);
	ESPERAR_U32(vram_64_a_32(0x00000005), 0x00400001);
}

static void escribir_por_64_y_leer_por_32(void)
{
	/* Un patron contiguo por la ventana de 64 debe quedar repartido: 4 bytes
	   al banco 0, 4 al banco 1, 4 al 0... Es el "dos pixeles buenos, dos de
	   basura" que describe pvr-fb_tex, visto desde el otro lado. */
	static const unsigned char patron[16] =
	{
		0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
		0x30, 0x31, 0x32, 0x33, 0x40, 0x41, 0x42, 0x43,
	};

	memset(video_mem, 0, 64);
	memset(video_mem + BANCO, 0, 64);

	vram64_escribir(0x000000, patron, sizeof(patron));

	ESPERAR_BYTES(&video_mem[0],         &patron[0],  4);	/* banco 0 */
	ESPERAR_BYTES(&video_mem[BANCO],     &patron[4],  4);	/* banco 1 */
	ESPERAR_BYTES(&video_mem[4],         &patron[8],  4);	/* banco 0 */
	ESPERAR_BYTES(&video_mem[BANCO + 4], &patron[12], 4);	/* banco 1 */
}

static void leer_por_64_junta_los_bancos(void)
{
	unsigned char leido[16];

	memset(video_mem, 0, 64);
	memset(video_mem + BANCO, 0, 64);

	memcpy(&video_mem[0],         "AAAA", 4);
	memcpy(&video_mem[BANCO],     "BBBB", 4);
	memcpy(&video_mem[4],         "CCCC", 4);
	memcpy(&video_mem[BANCO + 4], "DDDD", 4);

	vram64_leer(0x000000, leido, sizeof(leido));

	ESPERAR_BYTES(leido, "AAAABBBBCCCCDDDD", 16);
}

static void acceso_desalineado(void)
{
	/* Cabeza y cola impares: el reparto respeta los tramos de 4. */
	static const unsigned char patron[9] =
		{ 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8 };
	unsigned char leido[9];

	memset(video_mem, 0, 64);
	memset(video_mem + BANCO, 0, 64);

	/* a64 = 3: un byte del banco 0, cuatro del 1, cuatro del 0. */
	vram64_escribir(3, patron, sizeof(patron));

	ESPERAR_U32(video_mem[3],         0xD0);
	ESPERAR_BYTES(&video_mem[BANCO],  &patron[1], 4);
	ESPERAR_BYTES(&video_mem[4],      &patron[5], 4);

	memset(leido, 0, sizeof(leido));
	vram64_leer(3, leido, sizeof(leido));

	ESPERAR_BYTES(leido, patron, sizeof(leido));
}

static void gather_scatter_identidad(void)
{
	/* Escribir por 64 y leer por 64 devuelve lo mismo, byte a byte, tambien
	   cruzando el limite de banco y con largos que no son multiplo de 8. */
	unsigned char patron[41];
	unsigned char leido[41];
	int i;

	for (i = 0; i < (int) sizeof(patron); i++)
		patron[i] = (unsigned char) (i * 7 + 3);

	vram64_escribir(0x1FFFF5, patron, sizeof(patron));
	memset(leido, 0, sizeof(leido));
	vram64_leer(0x1FFFF5, leido, sizeof(leido));

	ESPERAR_BYTES(leido, patron, sizeof(leido));
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(conversion_ida_y_vuelta),
	CASO(conversion_es_biyectiva_por_tramos),
	CASO(el_caso_medido_de_fb_tex),
	CASO(escribir_por_64_y_leer_por_32),
	CASO(leer_por_64_junta_los_bancos),
	CASO(acceso_desalineado),
	CASO(gather_scatter_identidad),
};

const dc_suite suite_vram = DEFINIR_SUITE("vram", casos);
