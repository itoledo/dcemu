#include <stdio.h>
// #include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

typedef int ssize_t; // falta en mingw32

#include <cdio/config.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/cd_types.h>

#include "iso.h"

iso9660_t * iso;
CdIo * cdio;
char * sDevice;

int iso_init()
{
    cdio_fs_anal_t fs;
    cdio_iso_analysis_t ia;
    char * s;
    
/*	iso9660_stat_t * statbuf;

	iso = iso9660_open("dc.iso");
	
	if (iso == NULL)
		return 1; */
		
	cdio_init();
	
	fprintf(stderr, "cdio_get_default_device: %s\n", sDevice = cdio_get_default_device(NULL));

	cdio = cdio_open(sDevice, DRIVER_UNKNOWN);
	
	if (!cdio)
	{
		fprintf(stderr, "cdio_open: cdio NULL\n");
		return 1;
	}		

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

	fprintf(stderr, "formato filesystem: %d %s\n", CDIO_FSTYPE(fs), s);

	// test
/*	FILE * fp;
	char * target;
	target = (char *) malloc(sizeof(char) * 2048 * 2049);
	iso_read_sector(target, 27 + 150, 2049);
	fp = fopen("dump.raw", "wb");
	fwrite(target, sizeof(char), 2048*2049, fp);
	fclose(fp); */

	return 0;
}

int iso_get_lba()
{
    return cdio_get_track_lba(cdio, 1);
}

int iso_get_mode()
{
    int fmt;

    switch(cdio_get_track_format(cdio, 1))
    {
        case TRACK_FORMAT_AUDIO:	fmt = -1;	break;
        case TRACK_FORMAT_CDI:		fmt = -1;	break;
        case TRACK_FORMAT_XA:		fmt = 2;	break;
        case TRACK_FORMAT_DATA:		fmt = 1;	break;
        case TRACK_FORMAT_PSX:		fmt = -1;	break;
        default:					fmt = -1;	break;
	}	
	
	if (fmt == -1)
	{
	    fprintf(stderr, "error en formato de pista 1: %s\n", track_format2str[cdio_get_track_format(cdio, 1)]);
	    fmt = 1; // dejémoslo en modo1
	}
	
	return fmt;
}    
    
int iso_read_sector(char * target, int secstart, int secnum)
{
	int ret = 0, i;
	char buf[ISO_BLOCKSIZE];
 
	fprintf(stderr, "leyendo sectores, inicio %d, secnum %d\n", secstart - 150, secnum);

	for (i = 0; i < secnum; i++)
	{
		ret = cdio_read_mode1_sector(cdio, buf, secstart - 150 + i, false);
	
		if (ret != 0)
			fprintf(stderr, "error al tratar de leer sector %d\n", secstart - 150 + i);

		memcpy(target, buf, ISO_BLOCKSIZE);
		target += ISO_BLOCKSIZE;
	}

	return ret;
}

