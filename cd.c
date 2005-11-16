#include <stdio.h>
// #include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h> // for unlink

// typedef int ssize_t; // falta en mingw32

#include <cdio/config.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/cd_types.h>

#if defined(POSX)
#include "lnxdefs.h"
#endif
#include "mem.h"
#include "cd.h"
#include "file.h"

CdIo * cdio;
DWORD v_cd=0;
iso9660_stat_t *p_statbuf;

// this files retrieves a file from a cue/bin image should also work with nrg images.Base code taken libcdio examples
int searchCd(CdIo * dev,char * file,char * output)
{
  FILE *p_outfd;
  int i;

  p_statbuf = iso9660_fs_stat (cdio,file);

  if (NULL == p_statbuf) 
    {
      fprintf(stderr, 
	      "Could not get ISO-9660 file information for file %s\n",
	      file);
      cdio_destroy(cdio);
      return 2;
    }

  iso9660_name_translate(file,output);
  
  if (!(p_outfd = fopen (output, "wb")))
    {
      perror ("fopen()");
      cdio_destroy(cdio);
      free(p_statbuf);
      return 3;
    }

  /* Copy the blocks from the ISO-9660 filesystem to the local filesystem. */
  for (i = 0; i < p_statbuf->size; i += ISO_BLOCKSIZE)
    {
      char buf[ISO_BLOCKSIZE];

      memset (buf, 0, ISO_BLOCKSIZE);
      
      if ( 0 != cdio_read_data_sectors (cdio, buf, 
					p_statbuf->lsn + (i / ISO_BLOCKSIZE),
					ISO_BLOCKSIZE, 1) )
      {
	fprintf(stderr, "Error reading ISO 9660 file at lsn %lu\n",
		(long unsigned int) p_statbuf->lsn + (i / ISO_BLOCKSIZE));
		 // cleaning up
 		free(p_statbuf);
 		fclose(p_outfd);
		// an incomplete file is of no use to us so
		unlink(output);
		return -1;
      }
      
      
      fwrite (buf, ISO_BLOCKSIZE, 1, p_outfd);
      
      if (ferror (p_outfd))
	{
		 perror ("fwrite()");
 		// cleaning up
 		free(p_statbuf);
 		fclose(p_outfd);
		// an incomplete file is of no use to us so
		unlink(output);
	  	return 1;
	}
    }
  
  fflush (p_outfd);

  /* Make sure the file size has the exact same byte size. Without the
     truncate below, the file will a multiple of ISO_BLOCKSIZE.
   */
  if (ftruncate (fileno (p_outfd), p_statbuf->size))
    perror ("ftruncate()");

  printf("Extraction of file '%s' from '%s' successful.\n", 
	 output,file);
	
 // cleaning up
 free(p_statbuf);
 fclose(p_outfd);
 return 0;
}


int cd_init(char * input)
{
    cdio_fs_anal_t fs;
    cdio_iso_analysis_t ia;
    char * s;
    lsn_t lsn_ult_sesion;
    int i;
		
	cdio_init();
	
	cdio = cdio_open(input, DRIVER_UNKNOWN);
	
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
		
	fprintf(stderr, "formato filesystem %d: %d %s\n", 1, CDIO_FSTYPE(fs), s);
		
	if (fs & CDIO_FS_ANAL_MULTISESSION)
	{
		fprintf(stderr, "multisesion\n");
	}

	 if(searchCd(cdio,"IP.BIN","strap.temp"))
	{
		fprintf(stderr,"Couldn't find file IP.BIN");
		return -1;
	}
  	// loading the ip.bin	
  	load_bootstrap("strap.temp",get_memory_pointer(mem_base + ip_offset));
  	printf("Boot file %s\n",IP_STRUCT[BOOT]);
  	if(searchCd(cdio,IP_STRUCT[BOOT],"exec.temp"))
	{
		fprintf(stderr,"Couldn't find file %s",IP_STRUCT[BOOT]);
		unlink("strap.bin");
		return -1;
	}
	  //loading the boot file
 	load_exec("exec.temp",get_memory_pointer(mem_base + mem_offset));
 	// all ok cleaning up
 	unlink("strap.temp");
 	unlink("exec.temp");
 return 0;
}

int cd_get_lba()
{
    return cdio_get_track_lba(cdio, 1);
}

int cd_get_mode(DWORD write_address)
{
    int fmt;

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

	v_cd = 8192;
	WriteMemoryL(write_address + 4, &v_cd);
//	v_cd = 2048; // mode 2
	v_cd = 1024 * fmt;
	WriteMemoryL(write_address + 8, &v_cd);
	v_cd = 2048; // sector size in bytes
	WriteMemoryL(write_address + 12, &v_cd);

	return fmt;
}    
    
int cd_read_sector(char * target, int secstart, int secnum)
{
	int ret = 0, i;
	char buf[ISO_BLOCKSIZE];
//	char fname[128];
 
	fprintf(stderr, "leyendo sectores, secstart %d, inicio %d, secnum %d\n", secstart, secstart - 150, secnum);

	for (i = 0; i < secnum; i++)
	{
		ret = cdio_read_data_sectors(cdio, buf, secstart - 150 + i, CDIO_CD_FRAMESIZE, 1);
	
		if (ret != 0)
			fprintf(stderr, "error al tratar de leer sector %d\n", secstart - 150 + i);

		memcpy(target, buf, ISO_BLOCKSIZE);
		target += ISO_BLOCKSIZE;
	}

	return ret;
}


int cd_getStatus(DWORD address)
{
	v_cd = 2; // drive is in standby
	WriteMemoryL(address, &v_cd);
//	v_cd = 0x80; // GD-ROM
	v_cd = 0x10; // CD-ROM
	WriteMemoryL(address + 4, &v_cd);
	return 0;
}

int cd_close()
{

	cdio_destroy(cdio);
	return 0;
}

int dummy_init(char * input)
{
    cdio_fs_anal_t fs;
    cdio_iso_analysis_t ia;
    char * s;
    lsn_t lsn_ult_sesion;
    int i;
		
	cdio_init();

	cdio = cdio_open(input, DRIVER_UNKNOWN);
	
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
		
	fprintf(stderr, "formato filesystem %d: %d %s\n", 1, CDIO_FSTYPE(fs), s);
		
	if (fs & CDIO_FS_ANAL_MULTISESSION)
	{
		fprintf(stderr, "multisesion\n");
	}

  if (load_bootstrap("ip.bin",get_memory_pointer(mem_base + ip_offset)))
	{
		printf("Coudn't load bootstrap");
		return -1;
	}
  if (load_exec("1ST_READ.BIN",get_memory_pointer(mem_base + mem_offset)))
	{
		printf("Coudn't load executable");
		return -1;
	}
 return 0;
}