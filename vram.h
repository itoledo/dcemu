/****************************************************************************

	VRAM - las dos ventanas de la RAM de video del PVR

	La RAM de video son dos bancos de 4 MB, y el chip la muestra entera por dos
	ventanas que los entrelazan distinto. La de 32 bits (0x05000000/0xA5000000)
	los ve contiguos: el banco es el bit 22. La de 64 bits (0x04000000/
	0xA4000000, y la FIFO de texturas en 0x11000000) los alterna cada 4 bytes:
	el banco es el bit 2. El framebuffer se escribe por la de 32 y las texturas
	se leen por la de 64, asi que la misma celda tiene dos direcciones -- y un
	guest que cruza de una numeracion a la otra (pvr-fb_tex muestrea su propio
	front buffer como textura) depende de que no coincidan.

	dcemu guarda el bloque en la numeracion de 32 bits, que es la del
	framebuffer: asi DibujarFramebuffer() y los volcados siguen leyendo plano.
	Todo acceso por una ventana de 64 bits pasa por aqui.

	Sin SDL ni OpenGL a proposito, igual que ta.c y sistema.c: las pruebas lo
	enlazan de verdad.

*****************************************************************************/

#ifndef _VRAM_H_
#define _VRAM_H_

#include <stddef.h>

#include "lnxdefs.h"

/*
	La conversion entre numeraciones, sobre offsets dentro de los 8 MB.

	Con Y = offset dentro del banco y b = banco:

	  32 bits:  b en el bit 22, Y contiguo.
	  64 bits:  b en el bit 2, Y repartido: bits 21-2 de Y en 22-3, bits 1-0
	            tal cual.

	Verificable contra KOS: pvr_get_front_buffer() convierte multiplicando por
	dos (pvr_misc.c), que es este mismo mapa con el banco cayendo en el bit 23
	que la palabra de control de textura no guarda -- por eso fb_tex compensa
	el banco con medio texel de offset de U.
*/
DWORD vram_32_a_64(DWORD a32);
DWORD vram_64_a_32(DWORD a64);

/*
	Acceso en bloque por la ventana de 64 bits: junta (leer) o reparte
	(escribir) los tramos de 4 bytes que la ventana alterna entre los bancos.
	`a64` es un offset en la numeracion de 64 bits; el resultado queda contiguo
	en `p`, que es como lo esperan los decodificadores de textura.
*/
void vram64_leer(DWORD a64, void * p, size_t n);
void vram64_escribir(DWORD a64, const void * p, size_t n);

/*
	Generaciones por pagina de 8 KB, para invalidar la cache de texturas sin
	adivinar: cada escritura a la RAM de video marca sus paginas, y una textura
	guarda la suma de generaciones de su huella al decodificarse -- si al
	volver a pedirla la suma cambio, alguien escribio adentro y hay que
	decodificar de nuevo. Los dos embudos de escritura son video_write()
	(camino de 32 bits) y vram64_escribir(), que marca sola.

	`vram_gen_rango64` toma la huella como la ve vram64_leer(): un rango de la
	numeracion de 64 bits toca los dos bancos, contiguo en cada uno, y el
	colchon de un grupo por punta solo puede costar una invalidacion de mas.
*/
void  vram_marcar(DWORD a32, size_t n);
DWORD vram_gen_rango(DWORD a32, size_t n);
DWORD vram_gen_rango64(DWORD a64, size_t n);

#endif /* _VRAM_H_ */
