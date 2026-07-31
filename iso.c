#include <stdio.h>
// #include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

// typedef int ssize_t; // falta en mingw32

// El backend libcdio (bin/cue y lectora fisica) es opcional: sus .a son de
// MinGW y no sirven con MSVC. Sin USE_LIBCDIO queda solo la ruta .iso, atendida
// por el lector propio de iso9660_min.c.
#ifdef USE_LIBCDIO
#include <cdio/config.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/cd_types.h>
#endif

#include "lnxdefs.h"
#include "iso9660_min.h"
#include "cdi.h"
#include "iso.h"
#include "scramble.h"

// Lo definia <cdio/iso9660.h>.
#ifndef ISO_BLOCKSIZE
#define ISO_BLOCKSIZE MIN_ISO_BLOCKSIZE
#endif

// formatos
enum en_formato { FORMATO_NULL, FORMATO_ISO9660, FORMATO_CDI, FORMATO_CDIO } formato_imagen;
#define ISO_DEFAULT_LBA 150

// variables iso9660
min_iso_t * iso;

/* Con un .cdi el volumen no empieza en el LBA 150 de la convencion de dcemu
   sino donde diga la pista, que en un GD-ROM es el area de alta densidad. */
static unsigned int iso_lba_base = ISO_DEFAULT_LBA;

/* EXPERIMENTO: presentar un selfboot en CD como el GD-ROM del que salio. */
#define GD_FAD_ARRANQUE			45150	/* donde el boot ROM busca el IP.BIN */
#define GD_SECTORES_ARRANQUE	17		/* lo que lee de ahi */

static int iso_gd_presentar = 0;

/* Modo de la pista de datos del .cdi: lo pide iso_get_mode(). */
static int iso_modo_pista = 1;

/* Las pistas del .cdi, para la TOC y para REQ_SES. Un GD-ROM tiene dos
   sesiones -- la de densidad simple y la de alta -- y el boot ROM las pregunta
   antes de decidir que el disco es un juego. */
static struct cdi_t iso_cdi;

#ifdef USE_LIBCDIO
// variables libcdio
CdIo * cdio;
#endif

int iso_init(char * sDevice)
{
	if (sDevice == NULL)
	{
		formato_imagen = FORMATO_NULL;
		return 0;
	}

	if (strncmp(&sDevice[strlen(sDevice) - 4], ".iso", 4) == 0) // si termina en .iso
	{
		// usaremos exclusivamente iso9660
		formato_imagen = FORMATO_ISO9660;
		iso_lba_base = ISO_DEFAULT_LBA;

		fprintf(stderr, "iso_init: usando %s como archivo formato iso9660\n", sDevice);

		iso = min_iso_open(sDevice);

		if (iso == NULL)
			return 1;
	}
	else
	if (strncmp(&sDevice[strlen(sDevice) - 4], ".cdi", 4) == 0)
	{
		/* DiscJuggler: el volumen vive dentro de una pista, en cualquier
		   offset y con sectores crudos. cdi.c saca la geometria y el lector de
		   iso9660_min.c hace el resto. */
		struct cdi_t				cdi;
		const struct cdi_pista_t *	pista;
		int							cual;

		if (cdi_abrir(sDevice, &cdi) != 0)
			return 1;

		cual = cdi_pista_de_datos(&cdi);

		if (cual < 0)
		{
			fprintf(stderr, "iso_init: %s no tiene pistas de datos\n", sDevice);
			return 1;
		}

		pista = &cdi.pistas[cual];

		/* EXPERIMENTO: presentar el selfboot como el GD-ROM que era. El area
		   de alta densidad empieza en el LBA 45000, que es donde el boot ROM
		   busca el IP.BIN; el offset dentro del archivo no cambia, asi que
		   min_iso_open_pista() sigue encontrando los mismos bytes. */
		if (getenv("DCEMU_COMO_GD") && pista->lba < 45000)
		{
			iso_gd_presentar = 1;

			fprintf(stderr, "iso_init: presentando el area de arranque en el "
				"FAD %u; la pista de datos sigue en el LBA %u\n",
				GD_FAD_ARRANQUE, pista->lba);
		}

		fprintf(stderr, "iso_init: usando %s, %d pista%s; la de datos empieza en"
			" el LBA %u, %u sectores de %u bytes en modo %u\n",
			sDevice, cdi.n, (cdi.n == 1) ? "" : "s",
			pista->lba, pista->sectores, pista->sector_crudo, pista->modo);

		/* El modo de cada pista es lo que decide cuantas sesiones ve la lectora
		   --audio seguido de datos es un selfboot de dos sesiones-- y con eso
		   el boot ROM elige entre arrancar y abrir el reproductor de musica. */
		for (cual = 0; cual < cdi.n; cual++)
			fprintf(stderr, "iso_init:   pista %d: LBA %u, %u sectores, modo %u"
				" (%s), en el archivo desde %llu\n", cual + 1,
				cdi.pistas[cual].lba, cdi.pistas[cual].sectores,
				cdi.pistas[cual].modo, cdi.pistas[cual].modo ? "datos" : "audio",
				(unsigned long long) cdi.pistas[cual].offset);

		formato_imagen = FORMATO_CDI;
		iso_lba_base = pista->lba;
		iso_modo_pista = (int) pista->modo;
		iso_cdi = cdi;

		iso = min_iso_open_pista(sDevice, pista->lba, pista->offset,
			pista->sector_crudo, pista->desplazamiento);

		if (iso == NULL)
			return 1;
	}
	else
	{
#ifdef USE_LIBCDIO
	    char * s;
//	    lsn_t lsn_ult_sesion;
	    cdio_fs_anal_t fs;
	    cdio_iso_analysis_t ia;

		// trataremos de usar libcdio para parsear la imagen
		formato_imagen = FORMATO_CDIO;

		cdio_init();

		if (sDevice == NULL)
			fprintf(stderr, "cdio_get_default_device: %s\n", sDevice = cdio_get_default_device(NULL));

		cdio = cdio_open(sDevice, DRIVER_UNKNOWN);

		if (!cdio)
		{
			fprintf(stderr, "cdio_open: cdio NULL\n");
			return 1;
		}


		fprintf(stderr,
	 		"primera pista: %d\n"
			"n�mero de pistas: %d\n"
			"formato: %s\n"
			"lsn: %d\n"
			"lba: %d\n",
	    			cdio_get_first_track_num(cdio),
	    			cdio_get_num_tracks(cdio),
	    			track_format2str[cdio_get_track_format(cdio, 1)],
	    			cdio_get_track_lsn(cdio, 1),
	    			cdio_get_track_lba(cdio, 1));

		fs = cdio_guess_cd_type(cdio, 0, 1, &ia);

		switch(CDIO_FSTYPE(fs))
		{
		    case CDIO_FS_ISO_9660:	s = "iso9660";	break;
		    case CDIO_FS_AUDIO:		s = "audio";	break;
		    default:				s = "unknown";	break;
		}

		fprintf(stderr, "formato filesystem %d: %d %s\n", 1, CDIO_FSTYPE(fs), s);

		if (fs & CDIO_FS_ANAL_MULTISESSION)
		{
			fprintf(stderr, "multisesion\n");
		}
#else
		fprintf(stderr, "iso_init: %s no es un .iso y esta compilacion no incluye "
		                "libcdio (bin/cue y lectora fisica).\n", sDevice);
		formato_imagen = FORMATO_NULL;
		return 1;
#endif
	}

	return 0;
}

int iso_get_lba()
{
	switch(formato_imagen)
	{
		/* En FAD, que es lo que lleva la TOC: el LBA donde empieza la pista mas
		   los 150 del pregap. Para un .iso plano el volumen esta en el LBA 0 y
		   esto son los 150 de siempre. */
		case FORMATO_ISO9660:	return ISO_DEFAULT_LBA;
		case FORMATO_CDI:		return (int) (iso_lba_base + ISO_DEFAULT_LBA);
#ifdef USE_LIBCDIO
		case FORMATO_CDIO:		return cdio_get_track_lba(cdio, 1);
#endif
		case FORMATO_NULL:		return 0;
		default:				return 0;
	}
}

int iso_hay_disco()
{
	return formato_imagen != FORMATO_NULL;
}

/*
	Un GD-ROM se distingue de un CD-ROM en que sus datos estan en un area de
	alta densidad que empieza en el FAD 45150. Una imagen .iso plana nunca lo
	es; un .cdi lo es si su pista de datos empieza ahi, que es lo normal en las
	imagenes de juegos.

	Importa porque el boot ROM decide con esto si el disco es arrancable o si
	le abre el reproductor de CD.
*/
int iso_gd_presentando(void)
{
	return iso_gd_presentar;
}

/*
	Si la lectora tiene que decir "GD-ROM" o "CD-ROM".

	Un `.cdi` **es un CD**: DiscJuggler describe un disco de 12 cm normal, y una
	consola de verdad lo ve como tal por mas que la pista de datos empiece
	arriba del LBA 45000 -- eso solo quiere decir que delante hay una pista de
	audio de 92 MB, que es como se convirtio el GD-ROM original y cabe de sobra
	en un CD de 80 minutos.

	Decir GD-ROM manda al boot ROM a su rama de GD-ROM, y ahi hay una puerta que
	estas imagenes no pasan: para cargar el ejecutable pide que su FAD sea mayor
	que 0x6DDD0, que en un GD-ROM prensado se cumple --el mastering deja el
	1ST_READ.BIN al final, en el borde exterior-- y en una conversion a CD, que
	es mas chica, no. Presentado como el CD que es, el ROM toma la rama de
	MIL-CD y arranca. Ver docs/bios-boot-plan.md, cuarta corrida.

	Queda DCEMU_COMO_GD para el experimento contrario, que fue como se descubrio
	que la rama de GD-ROM no era el camino.
*/
int iso_es_gdrom()
{
	return formato_imagen == FORMATO_CDI && iso_gd_presentar;
}

/* ------------------------------------------------------------------------ */
/* Pistas, para la TOC y las sesiones                                       */
/* ------------------------------------------------------------------------ */

int iso_num_pistas(void)
{
	return (formato_imagen == FORMATO_CDI) ? iso_cdi.n : 1;
}

/* Todo lo que sale de aca va en FAD, que es lo que lleva la TOC. */
int iso_pista_fad(int i)
{
	if (formato_imagen != FORMATO_CDI)
		return ISO_DEFAULT_LBA;

	if (i < 0 || i >= iso_cdi.n)
		return 0;

	/* Presentando el selfboot como GD-ROM, la pista de datos se anuncia donde
	   el boot ROM la espera. Es solo lo que se dice en la TOC y en las
	   sesiones: leer sigue yendo al sitio de verdad. */
	if (iso_gd_presentar && iso_pista_es_datos(i))
		return GD_FAD_ARRANQUE;

	return (int) (iso_cdi.pistas[i].lba + ISO_DEFAULT_LBA);
}

int iso_pista_sectores(int i)
{
	if (formato_imagen != FORMATO_CDI)
		return iso_num_sectores();

	if (i < 0 || i >= iso_cdi.n)
		return 0;

	return (int) iso_cdi.pistas[i].sectores;
}

int iso_pista_es_datos(int i)
{
	if (formato_imagen != FORMATO_CDI)
		return 1;

	if (i < 0 || i >= iso_cdi.n)
		return 0;

	return iso_cdi.pistas[i].modo != 0;
}

/*
	Sesiones. El .cdi no guarda a que sesion pertenece cada pista, pero la
	frontera se puede inferir, y lo que la delata es **el hueco**: dentro de una
	sesion las pistas van pegadas, salvo el pregap de 150 sectores, mientras que
	cerrar una sesion y abrir otra cuesta el lead-out y el lead-in, unos 11400.
	Una pista que no empieza donde termino la anterior arranca sesion nueva.

	Antes esto solo miraba el otro indicio, "audio y despues datos", que es como
	se ve la conversion clasica --una pista de audio corta y el juego detras--.
	Pero hay selfboots de **datos y datos**: Virtua Tennis pone 33600 sectores de
	relleno en la pista 1 y el juego en el LBA 45000, y Capcom vs SNK 217425 y el
	juego en el 228825. Los dos salian como "una sesion", la lectora los daba por
	CD-ROM plano y el boot ROM se iba por su rama de GD-ROM, que no los arranca.

	El boot ROM decide con esto: pide las sesiones y arranca la ultima. Todo lo
	demas -- un .iso plano, un CD con una sola pista -- es una sola sesion.
*/

/* Dentro de una sesion las pistas se tocan. Este es el margen que se tolera
   antes de llamarlo sesion nueva: el pregap normal es de 150 sectores. */
#define ISO_HUECO_SESION	300

/* La primera pista de la segunda sesion, o -1 si el disco tiene una sola. */
static int iso_pista_segunda_sesion(void)
{
	int i, audio_visto = 0;

	if (formato_imagen != FORMATO_CDI)
		return -1;

	for (i = 0; i < iso_cdi.n; i++)
	{
		if (iso_es_gdrom() && iso_pista_fad(i) >= 45150)
			return i;

		if (i > 0 && iso_pista_fad(i) >
			iso_pista_fad(i - 1) + iso_pista_sectores(i - 1) + ISO_HUECO_SESION)
			return i;

		if (!iso_pista_es_datos(i))
			audio_visto = 1;
		else
		if (audio_visto)
			return i;
	}

	return -1;
}

int iso_num_sesiones(void)
{
	return (iso_pista_segunda_sesion() >= 0) ? 2 : 1;
}

/* Donde empieza la sesion `n` (1 o 2), en FAD. */
int iso_sesion_fad(int n)
{
	int segunda = iso_pista_segunda_sesion();

	if (n >= 2 && segunda >= 0)
		return iso_pista_fad(segunda);

	return iso_pista_fad(0);
}

/* El numero (desde 1) de la primera pista de la sesion `n`. */
int iso_sesion_primera_pista(int n)
{
	int segunda = iso_pista_segunda_sesion();

	if (n >= 2 && segunda >= 0)
		return segunda + 1;

	return 1;
}

int iso_num_sectores()
{
	switch(formato_imagen)
	{
		case FORMATO_ISO9660:
		case FORMATO_CDI:		return (int) min_iso_sectores(iso);
#ifdef USE_LIBCDIO
		/* cdio_get_track_lsn de la pista de lead-out (0xAA) da donde termina
		   el area de datos. */
		case FORMATO_CDIO:		return (int) cdio_get_track_lsn(cdio, CDIO_CDROM_LEADOUT_TRACK);
#endif
		case FORMATO_NULL:		return 0;
		default:				return 0;
	}
}

int iso_get_mode()
{
#ifdef USE_LIBCDIO
    int fmt;
#endif

	if (formato_imagen == FORMATO_ISO9660 || formato_imagen == FORMATO_NULL)
		return 1; // TRACK_FORMAT_DATA

	/* El .cdi trae el modo real de la pista; casi siempre 2 (XA). */
	if (formato_imagen == FORMATO_CDI)
		return (int) iso_modo_pista;

#ifdef USE_LIBCDIO
    switch(cdio_get_track_format(cdio, 1))
    {
        case TRACK_FORMAT_AUDIO:	fmt = -1;	break;
        case TRACK_FORMAT_CDI:		fmt = -1;	break;
        case TRACK_FORMAT_XA:		fmt = 2;	break; // era 2
        case TRACK_FORMAT_DATA:		fmt = 1;	break;
        case TRACK_FORMAT_PSX:		fmt = -1;	break;
        default:					fmt = -1;	break;
	}

	if (fmt == -1)
	{
	    fprintf(stderr, "error en formato de pista 1: %s\n", track_format2str[cdio_get_track_format(cdio, 1)]);
	    fmt = 1; // dej�moslo en modo1
	}

	fprintf(stderr, "cdio_get_track_format: %d\n", fmt);

	return fmt;
#else
	return 1;
#endif
}

int iso_read_sector(char * target, int secstart, int secnum)
{
	int ret = 0;
#ifdef USE_LIBCDIO
	int i;
	char buf[ISO_BLOCKSIZE];
#endif
//	char fname[128];

	fprintf(stderr, "leyendo sectores, secstart %d, secnum %d\n", secstart, secnum);

	switch(formato_imagen)
	{
#ifdef USE_LIBCDIO
		case FORMATO_CDIO:
		{
			for (i = 0; i < secnum; i++)
			{
				ret = cdio_read_data_sectors(cdio, buf, secstart - 150 + i, CDIO_CD_FRAMESIZE, 1);

				if (ret != 0)
					fprintf(stderr, "error al tratar de leer sector %d\n", secstart - 150 + i);

				memcpy(target, buf, ISO_BLOCKSIZE);
				target += ISO_BLOCKSIZE;
			}
		}
		break;
#endif

		case FORMATO_ISO9660:
		case FORMATO_CDI:
		{
			/*
				secstart viene en FAD, que es el LBA mas 150: es lo que pide el
				comando CD_READ de la lectora, y en un GD-ROM el area de alta
				densidad empieza en el FAD 45150. min_iso_* habla en LBA y ya
				sabe donde empieza el volumen -- 0 en un .iso plano, 45000 en la
				pista de datos de un .cdi --, asi que la conversion es la misma
				para los dos.
			*/
			/* EXPERIMENTO: presentando el disco como GD-ROM, el boot ROM pide
			   el area de arranque en la numeracion nueva --45150 en adelante--
			   pero el ISO9660 del disco lleva sus LBA en la vieja, asi que las
			   direcciones que salen de el (el directorio raiz, los archivos)
			   llegan sin desplazar. Se reconocen porque caen por debajo del
			   area de alta densidad. */
			if (iso_gd_presentar
				&& secstart >= GD_FAD_ARRANQUE
				&& secstart <  GD_FAD_ARRANQUE + GD_SECTORES_ARRANQUE)
				secstart += (int) iso_lba_base - (GD_FAD_ARRANQUE - ISO_DEFAULT_LBA);

			ret = (int) min_iso_seek_read(iso, target,
				(unsigned int) (secstart - ISO_DEFAULT_LBA), secnum);

			if (ret <= 0)
				fprintf(stderr, "error al tratar de leer %d sectores desde sector %d\n", secnum, secstart);
		}
		break;

		case FORMATO_NULL:
		{
			memset(target, 0, secnum * ISO_BLOCKSIZE);
			ret = 1;
		}
		break;

		default:
		break;
	}

	return ret;
}

int cargar_archivo( char * fname, void * target)
{
	FILE * fp;
	char c;
	int cnt = 0;
	char * p = (char *) target;

	// a cargar ip.bin
	fp = fopen(fname, "rb");

	if (!fp)
	{
		fprintf(stderr, "No se pudo abrir %s\r\n", fname);
		return -1;
	}

	for (c = fgetc(fp); !feof(fp); c = fgetc(fp))
	{
		*(p++) = c;
		cnt++;
	}

	fclose(fp);

	return cnt;
}

int cargar_archivo_iso(char * fname, bool scrambled, unsigned char * mempos)
{
	unsigned int lsn = 0;
	unsigned int size = 0;
	unsigned int secsize = 0;

	if (formato_imagen == FORMATO_NULL)
	{
		fprintf(stderr, "tratando de leer archivo %s desde iso NULL.\n", fname);
		return -1;
	}

	if (mempos == NULL)
	{
		fprintf(stderr, "mempos == NULL?\n");
		return -1;
	}

	switch(formato_imagen)
	{
#ifdef USE_LIBCDIO
		case FORMATO_CDIO:
		{
			CdioList_t * list = iso9660_fs_readdir(cdio, "/", false);
			CdioListNode_t * entnode;

			if (list == NULL)
			{
				fprintf(stderr, "No se pudo abrir directorio.\n");
				return -1;
			}

			_CDIO_LIST_FOREACH(entnode, list)
			{
			  char filename[4096];
			  iso9660_stat_t *p_statbuf = (iso9660_stat_t *) _cdio_list_node_data (entnode);
			  iso9660_name_translate(p_statbuf->filename, filename);

			  // ac� tenemos el nombre del archivo, ahora deberemos leerlo.
			  if (!strcmp(filename, fname))
			  {
					lsn = p_statbuf->lsn;
					secsize = p_statbuf->secsize;
					size = p_statbuf->size;
			  }
			}

			_cdio_list_free(list, true);
		}
		break;
#endif

		case FORMATO_ISO9660:
		case FORMATO_CDI:
		{
			if (!min_iso_stat_root(iso, fname, &lsn, &size, &secsize))
			{
				fprintf(stderr, "no se encontro %s en el directorio raiz.\n", fname);
				size = 0;
			}
		}
		break;

		default:
		break;
	}

	// necesitamos el lsn y el size
	if (size > 0)
	{
		fprintf(stderr, "leyendo %s desde lsn %d, %d sectores, %d bytes por sector.\n", fname, lsn, secsize, ISO_BLOCKSIZE);

		switch(formato_imagen)
		{
#ifdef USE_LIBCDIO
			case FORMATO_CDIO: if (cdio_read_data_sectors(cdio, mempos, lsn, ISO_BLOCKSIZE, secsize) == DRIVER_OP_SUCCESS) fprintf(stderr, "archivo leido exitosamente.\n"); break;
#endif
			/* FORMATO_CDI no estaba en esta lista y si en la del stat de mas
			   arriba: el archivo se encontraba, se reportaba su tamano... y no
			   se leia ni un byte. Lo que se ejecutaba era la RAM sin
			   inicializar, y de ahi que un .cdi terminara siempre en el menu
			   de la BIOS. */
			case FORMATO_ISO9660:
			case FORMATO_CDI:
				if (min_iso_seek_read(iso, mempos, lsn, secsize) > 0)
					fprintf(stderr, "archivo leido exitosamente.\n");
				break;

			default: break;
		}

		if (scrambled)
		{
			FILE * fp = fopen("scrambled.bin", "wb");
			if (!fp)
			{
				fprintf(stderr, "no se pudo guardar scrambled.bin\n");
				return -1;
			}
			fwrite(mempos, sizeof(char), size, fp);
			fclose(fp);

			fprintf(stderr, "haciendo descrambling\n");
			descramble("scrambled.bin", "descrambled.bin");

			return cargar_archivo("descrambled.bin", mempos);
		}
	}

	return size;
}

int cargar_ip_bin(unsigned char * mempos)
{
	switch(formato_imagen)
	{
#ifdef USE_LIBCDIO
		case FORMATO_CDIO:
		{
			if (cdio_read_data_sectors(cdio, mempos, 0, ISO_BLOCKSIZE, 16) == DRIVER_OP_SUCCESS)
				return 1;
		}
		break;
#endif

		case FORMATO_ISO9660:
		case FORMATO_CDI:
		{
			/* IP.BIN no es un archivo del sistema de archivos: son los 16
			   primeros sectores de la pista. */
			if (min_iso_seek_read(iso, mempos, min_iso_lba_base(iso), 16) > 0)
				return 1;
		}
		break;

		case FORMATO_NULL:
			return 0;

		default:
			break;
	}

	return 0;
}
