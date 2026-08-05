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

/*
	Rellena `dest` con las pistas del .gdi, en el orden del disco, y deja en
	`ruta_datos` la ruta del archivo de la pista de datos de mas arriba -- la que
	hay que abrir para leer el volumen.

	Se reusa `struct cdi_t` a proposito: describe exactamente lo mismo --la tabla
	de pistas del disco-- y es lo que iso.c ya consulta para armar la TOC y las
	sesiones. Un tipo nuevo obligaria a duplicar los seis accesores.

	Devuelve 0 si el .gdi es valido y tiene al menos una pista de datos.
*/
int gdi_abrir(const char * ruta, struct cdi_t * dest,
              char * ruta_datos, size_t ruta_datos_n);

#endif /* _GDI_H_ */
