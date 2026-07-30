/*
	iso9660_min.c -- lector ISO9660 minimo, sin libcdio. Ver iso9660_min.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "iso9660_min.h"

/* Descriptor de volumen primario: LBA 16, tipo 1, identificador "CD001". */
#define PVD_LBA				16
#define PVD_TIPO			1
#define PVD_ROOT_OFFSET		156		/* registro de directorio raiz, 34 bytes */
#define PVD_ESPACIO_OFFSET	80		/* tamano del volumen en sectores, both-endian */

/* Campos de un registro de directorio. */
#define DR_LARGO			0
#define DR_EXT_ATTR			1
#define DR_EXTENT			2		/* LBA, both-endian: los 4 primeros bytes son LE */
#define DR_SIZE				10		/* bytes, both-endian */
#define DR_FLAGS			25
#define DR_ID_LEN			32
#define DR_ID				33

struct min_iso_s
{
	FILE *			fp;
	unsigned int	root_lba;
	unsigned int	root_size;		/* en bytes */
	unsigned int	sectores;		/* tamano del volumen */

	/*
		Geometria de la pista donde vive el volumen. Un .iso es el caso trivial
		-- el sector 0 esta en el byte 0 y los sectores son 2048 bytes limpios
		--, pero dentro de un .cdi el volumen empieza en cualquier offset, los
		sectores son crudos (2336 o 2352, de los que solo 2048 son datos, detras
		de un subheader) y **los LBA que trae el propio ISO9660 son absolutos
		del disco**: en el area de alta densidad de un GD-ROM el directorio raiz
		esta en el 45023, no en el 23.
	*/
	unsigned int	lba_base;		/* el LBA al que corresponde `base` */
	long long		base;			/* su byte en el archivo */
	unsigned int	sector_crudo;	/* 2048, 2336 o 2352 */
	unsigned int	desplazamiento;	/* donde empiezan los 2048 dentro del sector */
};

static unsigned int leer_le32(const unsigned char * p)
{
	return  (unsigned int) p[0]        |
	       ((unsigned int) p[1] << 8)  |
	       ((unsigned int) p[2] << 16) |
	       ((unsigned int) p[3] << 24);
}

/* El byte donde empiezan los datos de usuario del sector `lba` del volumen. */
static long long posicion_de(const min_iso_t * iso, unsigned int lba)
{
	return iso->base + (long long) (lba - iso->lba_base) * iso->sector_crudo
	     + iso->desplazamiento;
}

static int posicionar(min_iso_t * iso, unsigned int lba)
{
	/* Un .cdi de dos capas pasa de 2 GB con facilidad, asi que aca el cast a
	   64 bits ya no es solo higiene. */
#if defined(_MSC_VER)
	return _fseeki64(iso->fp, posicion_de(iso, lba), SEEK_SET);
#else
	return fseeko(iso->fp, (off_t) posicion_de(iso, lba), SEEK_SET);
#endif
}

void min_iso_name_translate(const char * src, char * dst)
{
	int i, largo;

	for (i = 0; src[i] != '\0' && src[i] != ';' && i < 254; i++)
		dst[i] = (char) tolower((unsigned char) src[i]);

	dst[i] = '\0';

	/* ISO9660 deja un punto final en los nombres sin extension. */
	largo = i;
	if (largo > 0 && dst[largo - 1] == '.')
		dst[largo - 1] = '\0';
}

min_iso_t * min_iso_open(const char * path)
{
	return min_iso_open_pista(path, 0, 0, MIN_ISO_BLOCKSIZE, 0);
}

min_iso_t * min_iso_open_pista(const char * path, unsigned int lba_base,
                               long long base, unsigned int sector_crudo,
                               unsigned int desplazamiento)
{
	min_iso_t * iso;
	unsigned char pvd[MIN_ISO_BLOCKSIZE];
	const unsigned char * root;

	iso = (min_iso_t *) calloc(1, sizeof(min_iso_t));
	if (iso == NULL)
		return NULL;

	iso->lba_base       = lba_base;
	iso->base           = base;
	iso->sector_crudo   = sector_crudo;
	iso->desplazamiento = desplazamiento;

	iso->fp = fopen(path, "rb");
	if (iso->fp == NULL)
	{
		fprintf(stderr, "min_iso_open: no se pudo abrir %s\n", path);
		free(iso);
		return NULL;
	}

	if (posicionar(iso, lba_base + PVD_LBA) != 0 ||
	    fread(pvd, 1, MIN_ISO_BLOCKSIZE, iso->fp) != MIN_ISO_BLOCKSIZE)
	{
		fprintf(stderr, "min_iso_open: no se pudo leer el descriptor de volumen\n");
		min_iso_close(iso);
		return NULL;
	}

	if (pvd[0] != PVD_TIPO || memcmp(&pvd[1], "CD001", 5) != 0)
	{
		fprintf(stderr, "min_iso_open: %s no parece una imagen iso9660\n", path);
		min_iso_close(iso);
		return NULL;
	}

	root = &pvd[PVD_ROOT_OFFSET];
	iso->root_lba  = leer_le32(&root[DR_EXTENT]);
	iso->root_size = leer_le32(&root[DR_SIZE]);
	iso->sectores  = leer_le32(&pvd[PVD_ESPACIO_OFFSET]);

	if (iso->root_size == 0)
	{
		fprintf(stderr, "min_iso_open: directorio raiz vacio\n");
		min_iso_close(iso);
		return NULL;
	}

	return iso;
}

void min_iso_close(min_iso_t * iso)
{
	if (iso == NULL)
		return;

	if (iso->fp != NULL)
		fclose(iso->fp);

	free(iso);
}

long min_iso_seek_read(min_iso_t * iso, void * buf, unsigned int lba, unsigned int nblocks)
{
	unsigned char *	p = (unsigned char *) buf;
	long			total = 0;
	unsigned int	i;

	if (iso == NULL || iso->fp == NULL)
		return -1;

	/* Con sectores de 2048 limpios se pueden pedir todos de una; con sectores
	   crudos hay que ir uno a uno, porque entre unos datos y los siguientes
	   quedan el subheader y el ECC. */
	if (iso->sector_crudo == MIN_ISO_BLOCKSIZE && iso->desplazamiento == 0)
	{
		if (posicionar(iso, lba) != 0)
			return -1;

		return (long) fread(buf, 1, (size_t) nblocks * MIN_ISO_BLOCKSIZE, iso->fp);
	}

	for (i = 0; i < nblocks; i++)
	{
		size_t leidos;

		if (posicionar(iso, lba + i) != 0)
			break;

		leidos = fread(p, 1, MIN_ISO_BLOCKSIZE, iso->fp);
		total += (long) leidos;
		p += MIN_ISO_BLOCKSIZE;

		if (leidos != MIN_ISO_BLOCKSIZE)
			break;
	}

	return total;
}

unsigned int min_iso_sectores(min_iso_t * iso)
{
	return (iso == NULL) ? 0 : iso->sectores;
}

unsigned int min_iso_lba_base(min_iso_t * iso)
{
	return (iso == NULL) ? 0 : iso->lba_base;
}

int min_iso_stat_root(min_iso_t * iso, const char * nombre,
                      unsigned int * lsn, unsigned int * size, unsigned int * secsize)
{
	unsigned char * dir;
	unsigned int    sectores, offset;
	int             encontrado = 0;

	if (iso == NULL)
		return 0;

	sectores = (iso->root_size + MIN_ISO_BLOCKSIZE - 1) / MIN_ISO_BLOCKSIZE;

	dir = (unsigned char *) malloc((size_t) sectores * MIN_ISO_BLOCKSIZE);
	if (dir == NULL)
		return 0;

	if (min_iso_seek_read(iso, dir, iso->root_lba, sectores) <= 0)
	{
		free(dir);
		return 0;
	}

	offset = 0;
	while (offset < iso->root_size && !encontrado)
	{
		const unsigned char * rec = &dir[offset];
		unsigned int largo = rec[DR_LARGO];
		unsigned int id_len;
		char id[256], traducido[256];

		/* Un largo 0 significa que el resto del sector es relleno: hay que
		   saltar al sector siguiente. */
		if (largo == 0)
		{
			offset = (offset / MIN_ISO_BLOCKSIZE + 1) * MIN_ISO_BLOCKSIZE;
			continue;
		}

		if (offset + largo > sectores * MIN_ISO_BLOCKSIZE)
			break;

		id_len = rec[DR_ID_LEN];

		if (id_len > 0 && id_len < sizeof(id) && DR_ID + id_len <= largo)
		{
			memcpy(id, &rec[DR_ID], id_len);
			id[id_len] = '\0';

			min_iso_name_translate(id, traducido);

			if (strcmp(traducido, nombre) == 0)
			{
				*lsn     = leer_le32(&rec[DR_EXTENT]);
				*size    = leer_le32(&rec[DR_SIZE]);
				*secsize = (*size + MIN_ISO_BLOCKSIZE - 1) / MIN_ISO_BLOCKSIZE;
				encontrado = 1;
			}
		}

		offset += largo;
	}

	free(dir);

	return encontrado;
}
