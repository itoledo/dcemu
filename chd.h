/*
	chd.h -- lector de imagenes CHD (chdman, de MAME).

	Es, con .gdi, uno de los dos formatos en que esta preservada la biblioteca
	de Dreamcast, y el que usan las colecciones actuales: un solo archivo
	comprimido por juego. Los sectores viven en "hunks" comprimidos (LZMA,
	zlib, zstd o FLAC) y los descomprime libchdr (deps/libchdr, BSD/MIT); las
	pistas se describen en entradas de metadatos de texto, una por pista.

	La interpretacion de esos metadatos esta validada contra los .gdi del
	arbol -- el mismo juego en los dos contenedores tiene que dar la misma
	tabla de pistas -- y sigue la de flycast, que es el consumidor probado:

	  - el FAD de cada pista se acumula desde 150, y el campo FRAMES **incluye
	    el relleno** (PAD), que es como la pista 3 de un GD-ROM cae sola en el
	    LBA 45000;
	  - `sectores` es FRAMES menos PAD, que es lo que diria la TOC;
	  - dentro del archivo cada pista empieza en un frame multiplo de 4
	    (CD_TRACK_PADDING de MAME);
	  - cada frame ocupa 2448 bytes (2352 de sector mas 96 de subcanal);
	  - con el tag de metadatos GDROM nuevo ('CHGD') el audio esta guardado
	    con los bytes de cada muestra invertidos, y hay que darlos vuelta.

	El backend se compila solo con DCEMU_USE_CHD (lo define el CMake, que es
	quien trae libchdr); sin el quedan stubs que dicen que esta compilacion no
	incluye .chd, igual que la ruta libcdio.
*/

#ifndef _CHD_DC_H_
#define _CHD_DC_H_

#include "cdi.h"

/*
	Abre la imagen y deja en `dest` las pistas en el orden del disco, en
	`cual` el indice de la pista del volumen y en `es_gd` si la imagen
	describe un GD-ROM (por su tag de metadatos, no por suponerlo).

	La regla de `cual` es la de gdi_abrir(): la primera pista de datos del
	area de alta densidad si la hay, y si no la de datos de mas abajo. En
	`dest` el campo `offset` lleva el **frame** del CHD donde empieza la
	pista, no un byte: nadie fuera de chd.c debe usarlo para leer.

	Devuelve 0 si la imagen es valida y tiene una pista de datos utilizable.
*/
int chd_abrir(const char * ruta, struct cdi_t * dest, int * cual, int * es_gd);

/*
	Lee los 2048 bytes de usuario del sector `lba` (absoluto del disco), de la
	pista de datos que lo contenga -- un GD-ROM reparte sus datos entre varias
	pistas, ver min_iso_agregar_pista() --. Es el lector que se le da a
	min_iso_open_lector(); `ctx` no se usa. Devuelve 1 si leyo, 0 si el sector
	no cae en ninguna pista de datos o la lectura fallo.
*/
int chd_leer_usuario(void * ctx, unsigned int lba, void * buf);

/*
	Lee `n` sectores crudos de audio --2352 bytes cada uno-- desde el FAD
	`fad`, ya con los bytes en el orden del CD, y devuelve cuantos leyo. Es la
	puerta que usa iso_leer_audio(): mismas reglas que alla (0 si el FAD no
	cae en una pista de audio, recortado al final de la pista).
*/
int chd_leer_audio(void * destino, int fad, int n);

void chd_cerrar(void);

#endif /* _CHD_DC_H_ */
