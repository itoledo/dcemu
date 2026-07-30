/*
	cdi.h -- lector del header de una imagen DiscJuggler (.cdi).

	Es el formato en que circulan casi todas las imagenes de Dreamcast, y no lo
	lee ni el lector propio de iso9660_min.c -- que espera un volumen plano de
	sectores de 2048 -- ni libcdio, que no lo soporta.

	Un .cdi guarda los datos de las pistas al principio, uno detras de otro y
	con los sectores **crudos** (2336 o 2352 bytes, no 2048), y la descripcion
	de las sesiones y las pistas en un header **al final** del archivo. Los
	ultimos 8 bytes dicen la version y donde empieza ese header.

	Aca solo se parsea lo justo para montar la imagen: donde empieza cada pista
	de datos en el archivo, que LBA le corresponde y como son sus sectores. El
	sistema de archivos lo lee iso9660_min.c por encima.
*/

#ifndef _CDI_H_
#define _CDI_H_

#define CDI_PISTAS_MAX	32

struct cdi_pista_t
{
	unsigned int	lba;			/* LBA del primer sector de datos */
	unsigned int	sectores;		/* cuantos, sin contar el pregap */
	unsigned int	modo;			/* 0 audio, 1 modo 1, 2 modo 2 */
	unsigned int	sector_crudo;	/* 2048, 2336 o 2352 */
	unsigned int	desplazamiento;	/* donde empiezan los 2048 de usuario */
	long long		offset;			/* byte del primer sector de datos */
};

struct cdi_t
{
	struct cdi_pista_t	pistas[CDI_PISTAS_MAX];
	int					n;
};

/*
	Parsea el header. Devuelve 0 si la imagen es un .cdi valido con al menos una
	pista de datos, y deja en `dest` las pistas en el orden del disco.
*/
int cdi_abrir(const char * ruta, struct cdi_t * dest);

/*
	La pista de datos con el LBA mas alto, que en un GD-ROM es el area de alta
	densidad: la que trae IP.BIN y el juego. -1 si no hay ninguna.
*/
int cdi_pista_de_datos(const struct cdi_t * cdi);

#endif /* _CDI_H_ */
