/*
	gdi.h -- lector del indice de una imagen GD-ROM (.gdi).

	Es, con .chd, uno de los dos formatos en que esta preservada la biblioteca de
	Dreamcast, y el unico de los dos que no necesita una dependencia nueva: un
	.gdi es **texto plano** y las pistas son archivos crudos al lado.

		3
		1 0     4 2048 track01.iso 0
		2 1860  0 2352 track02.raw 0
		3 45000 4 2352 track03.bin 0

	Primera linea, cuantas pistas. Despues una por pista:

		numero  LBA  tipo  tamano_de_sector  archivo  offset

	`tipo` es 4 para datos y 0 para audio -- son los bits de control del subcanal
	Q, no un modo --, y el LBA es el del disco, o sea FAD menos 150.

	La diferencia de fondo con un .cdi es que **cada pista es un archivo aparte**,
	en vez de vivir todas dentro del mismo con un header al final. Para el resto
	del arbol da igual: lo unico que se abre de verdad es la pista de datos de
	alta densidad, y min_iso_open_pista() ya sabe trabajar sobre un archivo con
	sectores crudos.

	Lo que el .gdi **no** dice es si una pista de datos de 2352 bytes es modo 1 o
	modo 2, y de eso depende donde empiezan los 2048 de usuario (16 o 24). Se lee
	del propio sector: byte 15, detras de los 12 de sincronismo y los 3 de
	direccion. Suponerlo desplaza cada lectura y el volumen no se monta.
*/

#ifndef _GDI_H_
#define _GDI_H_

#include "cdi.h"

/* Donde empieza el area de alta densidad de un GD-ROM, en LBA. */
#define GDI_LBA_ALTA_DENSIDAD	45000

/*
	Rellena `dest` con las pistas del .gdi, en el orden del disco, deja en
	`ruta_datos` la ruta del archivo de la pista que trae el volumen y en `cual`
	su indice.

	**Cual es "la pista que trae el volumen" no es obvio y esta medido.** Un
	.cdi de juego tiene una sola pista de datos y por eso cdi_pista_de_datos()
	toma la de LBA mas alto; un GD-ROM prensado puede tener varias. Dave Mirra
	Freestyle BMX tiene catorce pistas: la 3 en el LBA 45000 con el sistema de
	archivos, diez de audio CDDA en medio, y la 14 en el 414528 con mas datos.
	Con la regla del .cdi se elegia la 14 y la imagen no montaba.

	La regla correcta es **la primera pista de datos en el area de alta
	densidad**: ahi es donde el GD-ROM pone su ISO9660, y es lo que el boot ROM
	da por sentado cuando busca el IP.BIN en el FAD 45150.

	Se reusa `struct cdi_t` a proposito: describe exactamente lo mismo --la tabla
	de pistas del disco-- y es lo que iso.c ya consulta para armar la TOC y las
	sesiones. Un tipo nuevo obligaria a duplicar los seis accesores.

	Devuelve 0 si el .gdi es valido y tiene una pista de datos utilizable.
*/
int gdi_abrir(const char * ruta, struct cdi_t * dest,
              char * ruta_datos, size_t ruta_datos_n, int * cual);

/*
	La ruta del archivo de la pista `i` de la ultima imagen abierta, ya resuelta
	contra el directorio del .gdi. La necesita iso.c para registrar en el lector
	**todas** las pistas de datos y no solo la del volumen: en un GD-ROM los
	archivos pueden estar en otra pista que el ISO9660 que los describe.
*/
const char * gdi_ruta_de(int i);

#endif /* _GDI_H_ */
