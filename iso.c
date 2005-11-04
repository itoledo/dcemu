#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h> // for unlink

#include <cdio/config.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/cd_types.h>

#if defined(POSX)
#include "lnxdefs.h"
#endif
#include "mem.h"
#include "iso.h"
#include "file.h"

/* The code in this file oads an iso file with Mode 1 sectors.Basically a non bootable iso */


iso9660_t * p_iso;
iso9660_stat_t *p_statbuf;
DWORD valor=0;

int searchIso(char * file,iso9660_t * p_iso,char * output)
{
  FILE *p_outfd;
  int i;
  
  p_statbuf = iso9660_ifs_stat_translate (p_iso, file);

  if (NULL == p_statbuf) 
    {
      fprintf(stderr, 
	      "Could not get ISO-9660 file information for file %s\n",
	      file);
      iso9660_close(p_iso);
      return 2;
    }

  if (!(p_outfd = fopen (output, "wb")))
    {
      perror ("fopen()");
      free(p_statbuf);
      iso9660_close(p_iso);
      return 3;
    }

  /* Copy the blocks from the ISO-9660 filesystem to the local filesystem. */
  for (i = 0; i < p_statbuf->size; i += ISO_BLOCKSIZE)
    {
      char buf[ISO_BLOCKSIZE];

      memset (buf, 0, ISO_BLOCKSIZE);
      
      if ( ISO_BLOCKSIZE != iso9660_iso_seek_read (p_iso, buf, p_statbuf->lsn 
						   + (i / ISO_BLOCKSIZE),
						   1) )
      {
	fprintf(stderr, "Error reading ISO 9660 file at lsn %lu\n",
		(long unsigned int) p_statbuf->lsn + (i / ISO_BLOCKSIZE));
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
		return -1;
	}
    }
  
  fflush (p_outfd);

  /* Make sure the file size has the exact same byte size. Without the
     truncate below, the file will a multiple of ISO_BLOCKSIZE.
   */
  if (ftruncate (fileno (p_outfd), p_statbuf->size))
    perror ("ftruncate()");

 	
 // cleaning up
 free(p_statbuf);
 fclose(p_outfd);

 return 0;
}


/* Opens the iso reads the ip.bin and the boot file and loads them :) */
int iso_init(char * iso)
{
  int i;
  iso9660_t *p_iso;
  p_iso = iso9660_open (iso);
  if (NULL == p_iso) {
    fprintf(stderr, "Sorry, couldn't open ISO 9660 image %s\n", iso);
    return 1;
  }
  if(searchIso("IP.BIN",p_iso,"strap.temp"))
	{
		fprintf(stderr,"Couldn't find file IP.BIN");
		return -1;
	}
  // loading the ip.bin	
  load_bootstrap("strap.temp",get_memory_pointer(mem_base + ip_offset));
  printf("Boot file %s\n",IP_STRUCT[BOOT]);
  if(searchIso(IP_STRUCT[BOOT],p_iso,"boot.temp"))
	{
		fprintf(stderr,"Couldn't find file %s",IP_STRUCT[BOOT]);
		unlink("ip.bin");
		return -1;
	}
  //loading the boot file
 load_exec("boot.temp",get_memory_pointer(mem_base + mem_offset));
 // all ok cleaning up
 unlink("strap.temp");
 unlink("boot.temp");
 return 0;
}

/*  Should fill up the toc */
int iso_get_info()
{
// no idea what should go here 
 return 0;
}

/* cleaning up */
int closeIso()
{
  if(!iso9660_close(p_iso))
	{
		fprintf(stderr,"Could not close iso.Probably still in use\n");
		return 1;
	}
 free(p_statbuf);
 return 0;
}

/* setup the sector info */
int iso_getSectorMode(DWORD address)
{
	valor = 8192;
	WriteMemoryL(address + 4, &valor);
//	valor = 2048; // mode 2
	valor = 1024; // mode 1 iso
	WriteMemoryL(address + 8, &valor);
	valor = 2048; // sector size in bytes
	WriteMemoryL(address + 12, &valor);

	return 1; // assuming mode 1 isos
}    

/* setup  the status */    
int iso_get_status(DWORD address)
{	
	valor = 2; // drive is in standby
	WriteMemoryL(address, &valor);
//	valor = 0x80; // GD-ROM
	valor = 0x10; // CD-ROM
	WriteMemoryL(address + 4, &valor);
}


/* reading the sector stuff */
int iso_read_sector(char * target, int secstart, int secnum)
{
 int i;
fprintf(stderr, "leyendo sectores, inicio %d, secnum %d\n", secstart - 150, secnum);
 for (i = 0; i < secnum; i += ISO_BLOCKSIZE)
    {
      char buf[ISO_BLOCKSIZE];

      memset (buf, 0, ISO_BLOCKSIZE);
      
      if ( ISO_BLOCKSIZE != iso9660_iso_seek_read (p_iso, buf, secstart - 150 + i ,1) )
      {
		fprintf(stderr, "error al tratar de leer sector %d\n", secstart - 150 + i);
      }
     
	memcpy(target, buf, ISO_BLOCKSIZE);
	target += ISO_BLOCKSIZE;
}
}