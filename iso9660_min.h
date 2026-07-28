/*
	iso9660_min.h -- lector ISO9660 minimo, sin libcdio.

	libcdio usa autotools y los .a de lib/win32 son binarios de MinGW, que el
	linker de MSVC no lee. De libiso9660 dcemu solo necesita abrir un .iso, listar
	el directorio raiz y leer sectores de 2048 bytes, que es lo que hay aqui.

	El backend libcdio (imagenes bin/cue y lectora fisica) sigue disponible
	compilando con USE_LIBCDIO.
*/

#ifndef _ISO9660_MIN_H_
#define _ISO9660_MIN_H_

#define MIN_ISO_BLOCKSIZE 2048

typedef struct min_iso_s min_iso_t;

/* Abre la imagen y valida el descriptor de volumen primario. NULL si falla. */
min_iso_t * min_iso_open(const char * path);

void min_iso_close(min_iso_t * iso);

/* Lee nblocks sectores desde lba. Devuelve los bytes leidos, o <= 0 si falla. */
long min_iso_seek_read(min_iso_t * iso, void * buf, unsigned int lba, unsigned int nblocks);

/* Busca un archivo en el directorio raiz, comparando con el nombre ya
   traducido (minusculas, sin el ";version"). Devuelve 1 si lo encuentra.
   secsize queda en sectores, size en bytes. */
int min_iso_stat_root(min_iso_t * iso, const char * nombre,
                      unsigned int * lsn, unsigned int * size, unsigned int * secsize);

/* Traduccion de nombre al estilo de iso9660_name_translate: pasa a minusculas,
   elimina el ";version" y el punto final. dst debe tener al menos 256 bytes. */
void min_iso_name_translate(const char * src, char * dst);

#endif // _ISO9660_MIN_H_
