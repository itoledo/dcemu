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

/*
	Lo mismo, pero para un volumen que no empieza en el byte 0 del archivo ni
	tiene sectores de 2048 limpios: es el caso de una pista dentro de un .cdi.

	  lba_base        el LBA que corresponde a `base`. En un GD-ROM los LBA del
	                  propio ISO9660 son absolutos del disco -- el directorio
	                  raiz esta en el 45023, no en el 23 --, asi que todas las
	                  funciones de aca hablan en esa misma numeracion
	  base            su byte en el archivo
	  sector_crudo    cuanto ocupa un sector en el archivo (2048, 2336 o 2352)
	  desplazamiento  donde empiezan sus 2048 bytes de usuario

	min_iso_open() es esto con (0, 0, 2048, 0).
*/
min_iso_t * min_iso_open_pista(const char * path, unsigned int lba_base,
                               long long base, unsigned int sector_crudo,
                               unsigned int desplazamiento);

/* El LBA del primer sector de la pista, en la numeracion de arriba. */
unsigned int min_iso_lba_base(min_iso_t * iso);

void min_iso_close(min_iso_t * iso);

/* Lee nblocks sectores desde lba. Devuelve los bytes leidos, o <= 0 si falla. */
long min_iso_seek_read(min_iso_t * iso, void * buf, unsigned int lba, unsigned int nblocks);

/* Sectores que ocupa el volumen, segun el descriptor primario. Es lo que hace
   falta para saber donde va el lead-out de la TOC. */
unsigned int min_iso_sectores(min_iso_t * iso);

/* Busca un archivo en el directorio raiz, comparando con el nombre ya
   traducido (minusculas, sin el ";version"). Devuelve 1 si lo encuentra.
   secsize queda en sectores, size en bytes. */
int min_iso_stat_root(min_iso_t * iso, const char * nombre,
                      unsigned int * lsn, unsigned int * size, unsigned int * secsize);

/* Traduccion de nombre al estilo de iso9660_name_translate: pasa a minusculas,
   elimina el ";version" y el punto final. dst debe tener al menos 256 bytes. */
void min_iso_name_translate(const char * src, char * dst);

#endif // _ISO9660_MIN_H_
