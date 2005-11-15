#include <stdio.h>
// #include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

// typedef int ssize_t; // falta en mingw32

#include <cdio/config.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/cd_types.h>

#include "iso.h"
#include "scramble.h"

// formatos
enum en_formato { FORMATO_NULL, FORMATO_ISO9660, FORMATO_CDIO } formato_imagen;
#define ISO_DEFAULT_LBA 150

// variables iso9660
iso9660_t * iso;

// variables libcdio
CdIo * cdio;

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
		
		fprintf(stderr, "iso_init: usando %s como archivo formato iso9660\n", sDevice);

//		iso9660_stat_t * statbuf;

		iso = iso9660_open(sDevice);
	
		if (iso == NULL)
			return 1;
	}
	else
	{
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

	/*	cdio_get_drive_cap(cdio, &readcap, &writecap, &misccap);
		
		if (misccap & CDIO_DRIVE_CAP_MISC_MULTI_SESSION)
		{
			fprintf(stderr, "multi sesión\n");
		} */

		fprintf(stderr,
	 		"primera pista: %d\n"
			"número de pistas: %d\n"
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

/*		if (cdio_get_last_session(cdio, &lsn_ult_sesion))
		{
			fprintf(stderr, "cdio_get_last_session: error\n");
		}
		else
		{
			fprintf(stderr, "lsn ultima sesion: %d\n", lsn_ult_sesion);
		} */
	}

	return 0;
}

int iso_get_lba()
{
	switch(formato_imagen)
	{
		case FORMATO_ISO9660:	return ISO_DEFAULT_LBA;
		case FORMATO_CDIO:		return cdio_get_track_lba(cdio, 1);
		case FORMATO_NULL:		return 0;
	}
	
	return 0;
}

int iso_get_mode()
{
    int fmt;

	if (formato_imagen == FORMATO_ISO9660 || formato_imagen == FORMATO_NULL)
		return 1; // TRACK_FORMAT_DATA

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
	    fmt = 1; // dejémoslo en modo1
	}
	
	fprintf(stderr, "cdio_get_track_format: %d\n", fmt);

	return fmt;
}    
    
int iso_read_sector(char * target, int secstart, int secnum)
{
	int ret = 0, i;
	char buf[ISO_BLOCKSIZE];
//	char fname[128];
 
	fprintf(stderr, "leyendo sectores, secstart %d, secnum %d\n", secstart, secnum);
	
	switch(formato_imagen)
	{
		case FORMATO_CDIO:
		{
			for (i = 0; i < secnum; i++)
			{
		//		ret = cdio_read_mode1_sector(cdio, buf, secstart - 150 + i, false);
		//		ret = cdio_read_mode1_sector(cdio, buf, secstart + i, false);
				ret = cdio_read_data_sectors(cdio, buf, secstart - 150 + i, CDIO_CD_FRAMESIZE, 1);
			
				if (ret != 0)
					fprintf(stderr, "error al tratar de leer sector %d\n", secstart - 150 + i);
		
				memcpy(target, buf, ISO_BLOCKSIZE);
				target += ISO_BLOCKSIZE;
				
		/*		sprintf(fname, "sector%d.bin", secstart + i);
				FILE * fp = fopen(fname, "wb");
				if (fp)
				{
					fwrite(buf, sizeof(char), ISO_BLOCKSIZE, fp);
					fclose(fp);
				} */
			}
		}
		break;
		
		case FORMATO_ISO9660:
		{
			ret = iso9660_iso_seek_read(iso, target, secstart - ISO_DEFAULT_LBA, secnum);
				
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
	CdioList_t * list;
	CdioListNode_t *entnode;
	lsn_t lsn = 0;
	uint32_t size = 0;
	uint32_t secsize = 0;

	if (formato_imagen == FORMATO_NULL)
	{
		fprintf(stderr, "tratando de leer archivo %s desde iso NULL.\n", fname);
		return -1;
	}

	switch(formato_imagen)
	{
		case FORMATO_CDIO: list = iso9660_fs_readdir(cdio, "/", false); break;
		case FORMATO_ISO9660: list = iso9660_ifs_readdir(iso, "/"); break;
	}
	
	if (list == NULL)
	{
		fprintf(stderr, "No se pudo abrir directorio.\n");
		return -1;
	}
	
	if (mempos == NULL)
	{
		fprintf(stderr, "mempos == NULL?\n");
		return -1;
	}
	
	_CDIO_LIST_FOREACH(entnode, list)
	{
      char filename[4096];
      iso9660_stat_t *p_statbuf = (iso9660_stat_t *) _cdio_list_node_data (entnode);
      iso9660_name_translate(p_statbuf->filename, filename);
      
      // acá tenemos el nombre del archivo, ahora deberemos leerlo.
      if (!strcmp(filename, fname))
      {
			lsn = p_statbuf->lsn;
			secsize = p_statbuf->secsize;
			size = p_statbuf->size;
	  }

//      free(p_statbuf);
	}
	
	_cdio_list_free(list, true);
	
	// necesitamos el lsn y el size
	if (size > 0)
	{
		fprintf(stderr, "leyendo %s desde lsn %d, %d sectores, %d bytes por sector.\n", fname, lsn, secsize, ISO_BLOCKSIZE);

		switch(formato_imagen)
		{
			case FORMATO_CDIO: if (cdio_read_data_sectors(cdio, mempos, lsn, ISO_BLOCKSIZE, secsize) == DRIVER_OP_SUCCESS) fprintf(stderr, "archivo leido exitosamente.\n"); break;
			case FORMATO_ISO9660: if (iso9660_iso_seek_read(iso, mempos, lsn, secsize) > 0) fprintf(stderr, "archivo leido exitosamente.\n"); break;
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
		case FORMATO_CDIO:
		{
			if (cdio_read_data_sectors(cdio, mempos, 0, ISO_BLOCKSIZE, 16) == DRIVER_OP_SUCCESS)
				return 1;
		}
		break;
		
		case FORMATO_ISO9660:
		{
			if (iso9660_iso_seek_read(iso, mempos, 0, 16) > 0)
				return 1;
		}
		break;
		
		case FORMATO_NULL:
			return 0;
	}
	
	return 0;
}
