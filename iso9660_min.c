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
};

static unsigned int leer_le32(const unsigned char * p)
{
	return  (unsigned int) p[0]        |
	       ((unsigned int) p[1] << 8)  |
	       ((unsigned int) p[2] << 16) |
	       ((unsigned int) p[3] << 24);
}

static int posicionar(FILE * fp, unsigned int lba)
{
	/* Una imagen de GD-ROM no llega a 2 GB, pero el cast a 64 bits es gratis. */
#if defined(_MSC_VER)
	return _fseeki64(fp, (__int64) lba * MIN_ISO_BLOCKSIZE, SEEK_SET);
#else
	return fseek(fp, (long) lba * MIN_ISO_BLOCKSIZE, SEEK_SET);
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
	min_iso_t * iso;
	unsigned char pvd[MIN_ISO_BLOCKSIZE];
	const unsigned char * root;

	iso = (min_iso_t *) calloc(1, sizeof(min_iso_t));
	if (iso == NULL)
		return NULL;

	iso->fp = fopen(path, "rb");
	if (iso->fp == NULL)
	{
		fprintf(stderr, "min_iso_open: no se pudo abrir %s\n", path);
		free(iso);
		return NULL;
	}

	if (posicionar(iso->fp, PVD_LBA) != 0 ||
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
	size_t leidos;

	if (iso == NULL || iso->fp == NULL)
		return -1;

	if (posicionar(iso->fp, lba) != 0)
		return -1;

	leidos = fread(buf, 1, (size_t) nblocks * MIN_ISO_BLOCKSIZE, iso->fp);

	return (long) leidos;
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
