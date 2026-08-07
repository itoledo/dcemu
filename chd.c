/*
	chd.c -- lector de imagenes CHD. Ver chd.h para el formato y las reglas.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chd.h"
#include "gdi.h"		/* GDI_LBA_ALTA_DENSIDAD: la regla del volumen es la suya */

#ifdef DCEMU_USE_CHD

#include <libchdr/chd.h>

/* Reglas de MAME (cdrom.h): cada frame ocupa 2448 bytes y dentro del archivo
   las pistas empiezan en frames multiplos de 4. */
#define CHD_FRAME_BYTES		2448
#define CHD_PISTA_RELLENO	4

/* Entre las dos sesiones de un CD multisesion van el lead-out, el lead-in y
   el pregap (1:30:00 + 1:00:00 + 0:02:00 = 11400 frames). chdman guarda las
   pistas pegadas, asi que la ultima hay que anunciarla donde una grabadora la
   habria puesto, o los LBA absolutos de su ISO9660 no calzan. Es la regla de
   flycast; ninguna de las imagenes a mano la ejercita (todas son GD-ROM). */
#define CHD_HUECO_SESION	11400

struct chd_pista_t
{
	unsigned int	lba;			/* absoluto del disco */
	unsigned int	sectores;		/* sin el relleno: lo que diria la TOC */
	unsigned int	modo;			/* 0 audio, 1 modo 1, 2 modo 2 */
	unsigned int	sector_crudo;	/* bytes de sector dentro del frame */
	unsigned int	desplazamiento;	/* donde empiezan los 2048 de usuario */
	unsigned int	frame;			/* frame del CHD donde empieza */
};

static chd_file *			chd_archivo = NULL;
static struct chd_pista_t	chd_pistas[CDI_PISTAS_MAX];
static int					chd_n_pistas = 0;
static int					chd_audio_invertido = 0;

/* El hunk descomprimido mas reciente. Una pista se recorre de corrido, asi
   que casi toda lectura cae en el hunk que ya esta armado. */
static unsigned char *		chd_hunk = NULL;
static unsigned int			chd_hunk_bytes = 0;
static unsigned int			chd_frames_por_hunk = 0;
static unsigned int			chd_hunk_cargado = 0xFFFFFFFF;

static int					chd_aviso_fuera = 0;

/* El frame `frame` dentro del hunk cacheado, o NULL si no se pudo leer. */
static const unsigned char * chd_frame_ptr(unsigned int frame)
{
	unsigned int hunk = frame / chd_frames_por_hunk;

	if (hunk != chd_hunk_cargado)
	{
		if (chd_read(chd_archivo, hunk, chd_hunk) != CHDERR_NONE)
			return NULL;

		chd_hunk_cargado = hunk;
	}

	return chd_hunk + (size_t) (frame % chd_frames_por_hunk) * CHD_FRAME_BYTES;
}

/* La pista que contiene ese LBA, del modo pedido (0 audio, 1 datos), o -1. */
static int chd_pista_de(unsigned int lba, int datos)
{
	int i;

	for (i = 0; i < chd_n_pistas; i++)
		if ((chd_pistas[i].modo != 0) == datos
		 && lba >= chd_pistas[i].lba
		 && lba <  chd_pistas[i].lba + chd_pistas[i].sectores)
			return i;

	return -1;
}

/*
	Modo 1 o modo 2 de una pista de datos con sectores de 2352, leido del
	propio sector como hace gdi_modo_de_pista(): el byte 15, detras de los
	doce de sincronismo y los tres de direccion. El tipo de los metadatos
	deberia decir lo mismo; si difieren manda el sector, que es el que el
	lector va a entregar.
*/
static unsigned int chd_modo_de_pista(const struct chd_pista_t * p,
                                      unsigned int modo_meta)
{
	static const unsigned char sync[12] =
		{ 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	const unsigned char * s = chd_frame_ptr(p->frame);

	if (s == NULL || memcmp(s, sync, sizeof(sync)) != 0
	 || (s[15] != 1 && s[15] != 2))
		return modo_meta;

	if (s[15] != modo_meta)
		fprintf(stderr, "chd_abrir: los metadatos dicen modo %u y el sector "
			"del LBA %u dice modo %u; manda el sector\n",
			modo_meta, p->lba, s[15]);

	return s[15];
}

int chd_abrir(const char * ruta, struct cdi_t * dest, int * cual, int * es_gd)
{
	const chd_header *	cab;
	chd_error			err;
	unsigned int		fad_total = 150;	/* FAD acumulado, arranca en el pregap */
	unsigned int		frame = 0;			/* frame acumulado dentro del CHD */
	int					i, mejor = -1, respaldo = -1;

	chd_cerrar();
	memset(dest, 0, sizeof(*dest));

	err = chd_open(ruta, CHD_OPEN_READ, NULL, &chd_archivo);

	if (err != CHDERR_NONE)
	{
		fprintf(stderr, "chd_abrir: no se pudo abrir %s: %s\n",
			ruta, chd_error_string(err));
		chd_archivo = NULL;
		return 1;
	}

	cab = chd_get_header(chd_archivo);

	if (cab->hunkbytes == 0 || cab->hunkbytes % CHD_FRAME_BYTES != 0)
	{
		fprintf(stderr, "chd_abrir: %s no es un CHD de CD: sus hunks miden %u "
			"bytes, que no es multiplo de %u\n",
			ruta, cab->hunkbytes, CHD_FRAME_BYTES);
		chd_cerrar();
		return 1;
	}

	chd_hunk_bytes      = cab->hunkbytes;
	chd_frames_por_hunk = cab->hunkbytes / CHD_FRAME_BYTES;
	chd_hunk            = (unsigned char *) malloc(chd_hunk_bytes);
	chd_hunk_cargado    = 0xFFFFFFFF;

	if (chd_hunk == NULL)
	{
		chd_cerrar();
		return 1;
	}

	/* Los CHD v4 y anteriores solo podian ser GD-ROM; los MIL-CD llegaron con
	   la v5. Los tags de metadatos lo confirman pista por pista. */
	*es_gd = cab->version < 5;
	chd_audio_invertido = 0;

	for (chd_n_pistas = 0; chd_n_pistas < CDI_PISTAS_MAX; chd_n_pistas++)
	{
		struct chd_pista_t *	p = &chd_pistas[chd_n_pistas];
		char					meta[512];
		char					tipo[32], subtipo[32], pgtipo[32], pgsub[32];
		uint32_t				meta_n = 0;
		int						num = -1, frames = 0, relleno = 0;
		int						pregap = 0, postgap = 0;

		if (chd_get_metadata(chd_archivo, CDROM_TRACK_METADATA2_TAG,
				(uint32_t) chd_n_pistas, meta, sizeof(meta), &meta_n,
				NULL, NULL) == CHDERR_NONE)
		{
			sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &num, tipo, subtipo,
				&frames, &pregap, pgtipo, pgsub, &postgap);
		}
		else if (chd_get_metadata(chd_archivo, CDROM_TRACK_METADATA_TAG,
				(uint32_t) chd_n_pistas, meta, sizeof(meta), &meta_n,
				NULL, NULL) == CHDERR_NONE)
		{
			sscanf(meta, CDROM_TRACK_METADATA_FORMAT, &num, tipo, subtipo,
				&frames);
		}
		else if (chd_get_metadata(chd_archivo, GDROM_OLD_METADATA_TAG,
				(uint32_t) chd_n_pistas, meta, sizeof(meta), &meta_n,
				NULL, NULL) == CHDERR_NONE)
		{
			/* El tag viejo ('CHGT') salio de un chdman parcheado que guardaba
			   el audio sin invertir. */
			sscanf(meta, GDROM_TRACK_METADATA_FORMAT, &num, tipo, subtipo,
				&frames, &relleno, &pregap, pgtipo, pgsub, &postgap);
			*es_gd = 1;
		}
		else if (chd_get_metadata(chd_archivo, GDROM_TRACK_METADATA_TAG,
				(uint32_t) chd_n_pistas, meta, sizeof(meta), &meta_n,
				NULL, NULL) == CHDERR_NONE)
		{
			sscanf(meta, GDROM_TRACK_METADATA_FORMAT, &num, tipo, subtipo,
				&frames, &relleno, &pregap, pgtipo, pgsub, &postgap);
			*es_gd = 1;
			chd_audio_invertido = 1;
		}
		else
			break;

		if (num != chd_n_pistas + 1)
		{
			fprintf(stderr, "chd_abrir: la entrada %d de metadatos dice pista "
				"%d\n", chd_n_pistas, num);
			chd_cerrar();
			return 1;
		}

		/* Un pregap distinto de 0 diria que delante de la pista hay frames
		   que no estan en el archivo, y ninguna imagen que circule lo trae:
		   antes de inventar la resta conviene ver una. */
		if (strcmp(subtipo, "NONE") != 0 || pregap != 0 || postgap != 0
		 || frames <= 0 || relleno < 0 || relleno > frames)
		{
			fprintf(stderr, "chd_abrir: pista %d con %s que este lector no "
				"contempla: %s\n", num,
				(pregap || postgap) ? "pregap/postgap" : "metadatos", meta);
			chd_cerrar();
			return 1;
		}

		if (strcmp(tipo, "AUDIO") == 0)
		{
			p->modo = 0;  p->sector_crudo = 2352;  p->desplazamiento = 0;
		}
		else if (strcmp(tipo, "MODE1") == 0 || strcmp(tipo, "MODE1/2048") == 0)
		{
			p->modo = 1;  p->sector_crudo = 2048;  p->desplazamiento = 0;
		}
		else if (strcmp(tipo, "MODE1_RAW") == 0 || strcmp(tipo, "MODE1/2352") == 0)
		{
			p->modo = 1;  p->sector_crudo = 2352;  p->desplazamiento = 16;
		}
		else if (strcmp(tipo, "MODE2_FORM1") == 0)
		{
			p->modo = 2;  p->sector_crudo = 2048;  p->desplazamiento = 0;
		}
		else if (strcmp(tipo, "MODE2") == 0 || strcmp(tipo, "MODE2_FORM_MIX") == 0
		      || strcmp(tipo, "MODE2/2336") == 0)
		{
			p->modo = 2;  p->sector_crudo = 2336;  p->desplazamiento = 8;
		}
		else if (strcmp(tipo, "MODE2_RAW") == 0 || strcmp(tipo, "MODE2/2352") == 0)
		{
			p->modo = 2;  p->sector_crudo = 2352;  p->desplazamiento = 24;
		}
		else
		{
			fprintf(stderr, "chd_abrir: pista %d de tipo %s, que este lector "
				"no contempla\n", num, tipo);
			chd_cerrar();
			return 1;
		}

		p->lba      = fad_total - 150;
		p->sectores = (unsigned int) (frames - relleno);
		p->frame    = frame;

		fad_total += (unsigned int) frames;
		frame     += (unsigned int) ((frames + CHD_PISTA_RELLENO - 1)
		           / CHD_PISTA_RELLENO) * CHD_PISTA_RELLENO;
	}

	if (chd_n_pistas == 0)
	{
		fprintf(stderr, "chd_abrir: %s no trae metadatos de pistas de CD\n",
			ruta);
		chd_cerrar();
		return 1;
	}

	/* En un CD multisesion chdman guardo las pistas pegadas: la ultima se
	   anuncia (y se lee) detras del hueco entre sesiones. En un GD-ROM no hay
	   nada que mover: el relleno ya dejo cada pista en su LBA. */
	if (!*es_gd && chd_n_pistas > 1)
	{
		chd_pistas[chd_n_pistas - 1].lba += CHD_HUECO_SESION;

		fprintf(stderr, "chd_abrir: CD multisesion: la pista %d se anuncia "
			"tras el hueco entre sesiones, en el LBA %u\n",
			chd_n_pistas, chd_pistas[chd_n_pistas - 1].lba);
	}

	/* La pista del volumen, con la regla de gdi_abrir(): la primera de datos
	   del area de alta densidad, y si no hay, la de datos de mas abajo. */
	for (i = 0; i < chd_n_pistas; i++)
	{
		if (chd_pistas[i].modo == 0)
			continue;

		if (chd_pistas[i].lba >= GDI_LBA_ALTA_DENSIDAD
		 && (mejor < 0 || chd_pistas[i].lba < chd_pistas[mejor].lba))
			mejor = i;

		if (respaldo < 0 || chd_pistas[i].lba < chd_pistas[respaldo].lba)
			respaldo = i;
	}

	if (mejor < 0)
		mejor = respaldo;

	if (mejor < 0)
	{
		fprintf(stderr, "chd_abrir: %s no tiene pistas de datos\n", ruta);
		chd_cerrar();
		return 1;
	}

	/* El modo de verdad de cada pista de datos crudos, leido del sector como
	   hace el .gdi. Decide donde empiezan los 2048 de usuario. */
	for (i = 0; i < chd_n_pistas; i++)
		if (chd_pistas[i].modo != 0 && chd_pistas[i].sector_crudo == 2352)
		{
			chd_pistas[i].modo = chd_modo_de_pista(&chd_pistas[i],
				chd_pistas[i].modo);
			chd_pistas[i].desplazamiento = (chd_pistas[i].modo == 2) ? 24 : 16;
		}

	for (i = 0; i < chd_n_pistas; i++)
	{
		dest->pistas[i].lba            = chd_pistas[i].lba;
		dest->pistas[i].sectores       = chd_pistas[i].sectores;
		dest->pistas[i].modo           = chd_pistas[i].modo;
		dest->pistas[i].sector_crudo   = chd_pistas[i].sector_crudo;
		dest->pistas[i].desplazamiento = chd_pistas[i].desplazamiento;
		dest->pistas[i].offset         = (long long) chd_pistas[i].frame;
	}

	dest->n = chd_n_pistas;
	*cual   = mejor;

	return 0;
}

int chd_leer_usuario(void * ctx, unsigned int lba, void * buf)
{
	const struct chd_pista_t *	p;
	const unsigned char *		s;
	int							i = chd_pista_de(lba, 1);

	(void) ctx;

	if (i < 0)
	{
		if (!chd_aviso_fuera)
		{
			chd_aviso_fuera = 1;
			fprintf(stderr, "chd: el sector %u no cae en ninguna pista de "
				"datos de la imagen\n", lba);
		}

		return 0;
	}

	p = &chd_pistas[i];
	s = chd_frame_ptr(p->frame + (lba - p->lba));

	if (s == NULL)
		return 0;

	memcpy(buf, s + p->desplazamiento, 2048);

	return 1;
}

int chd_leer_audio(void * destino, int fad, int n)
{
	unsigned char *				d = (unsigned char *) destino;
	const struct chd_pista_t *	p;
	unsigned int				lba;
	int							i, cabe, leidos;

	if (fad < 150 || n <= 0)
		return 0;

	lba = (unsigned int) fad - 150;
	i   = chd_pista_de(lba, 0);

	/* Mismas reglas que iso_leer_audio(): un FAD fuera de pista o en una de
	   datos entrega 0 y cdda.c pone silencio. */
	if (i < 0)
		return 0;

	p    = &chd_pistas[i];
	cabe = (int) (p->lba + p->sectores - lba);

	if (n > cabe)
		n = cabe;

	for (leidos = 0; leidos < n; leidos++, d += 2352)
	{
		const unsigned char * s = chd_frame_ptr(p->frame + (lba - p->lba)
		                                        + (unsigned int) leidos);

		if (s == NULL)
			break;

		memcpy(d, s, 2352);

		/* chdman guarda las muestras con los bytes invertidos (el orden del
		   Red Book); las del mezclador son little-endian. */
		if (chd_audio_invertido)
		{
			int b;

			for (b = 0; b < 2352; b += 2)
			{
				unsigned char t = d[b];

				d[b]     = d[b + 1];
				d[b + 1] = t;
			}
		}
	}

	return leidos;
}

void chd_cerrar(void)
{
	if (chd_archivo != NULL)
	{
		chd_close(chd_archivo);
		chd_archivo = NULL;
	}

	free(chd_hunk);
	chd_hunk        = NULL;
	chd_n_pistas    = 0;
	chd_aviso_fuera = 0;
	chd_hunk_cargado = 0xFFFFFFFF;
}

#else /* !DCEMU_USE_CHD */

int chd_abrir(const char * ruta, struct cdi_t * dest, int * cual, int * es_gd)
{
	(void) dest; (void) cual; (void) es_gd;

	fprintf(stderr, "chd_abrir: %s es un .chd y esta compilacion no incluye "
		"libchdr (DCEMU_USE_CHD).\n", ruta);

	return 1;
}

int chd_leer_usuario(void * ctx, unsigned int lba, void * buf)
{
	(void) ctx; (void) lba; (void) buf;
	return 0;
}

int chd_leer_audio(void * destino, int fad, int n)
{
	(void) destino; (void) fad; (void) n;
	return 0;
}

void chd_cerrar(void)
{
}

#endif /* DCEMU_USE_CHD */
