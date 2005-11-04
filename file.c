#include <stdio.h>
#include "file.h"
#if defined (__GNUC__)
#include "lnxdefs.h"
#endif
#include "main.h"
#include "config.h"
#include <string.h>
#include <unistd.h>
#include "1strdchk.h"
#include "scramble.h"

// offsets for the several IP.BIn fields
int IP_OFFSET [] = {0x00f,0x00f,0x00f,0x007,0x007,0x009,0x005,0x00f,0x00f,0x00f,0x07f};
char ** IP_STRUCT;  // where we will keep the info


// inits the ipstruct

void ipStructInit()
{
IP_STRUCT = (char**) calloc(11,sizeof(char *));
IP_STRUCT[HW_ID] = (char *) malloc(15);
IP_STRUCT[MK_ID] = (char *) malloc(15);
IP_STRUCT[DEV_I] = (char *) malloc(15);
IP_STRUCT[AREA] = (char *) malloc(8);
IP_STRUCT[PHER] = (char *) malloc(7);
IP_STRUCT[PROD_NUM] = (char *) malloc(9);
IP_STRUCT[PROD_VER] = (char *) malloc(5);
IP_STRUCT[REL_DATE] = (char *) malloc(15);
IP_STRUCT[BOOT] = (char *) malloc(15);
IP_STRUCT[CPNY] = (char *) malloc(15);
IP_STRUCT[NAME] = (char *) malloc(127);
}

// loads the ip.bin info
void ip_info_load(unsigned char * p)
{
int i;
//init the ip_bin struct
ipStructInit();
unsigned char * ptr = p;
	for(i =0; i < 11; i++)
	{
		memcpy(IP_STRUCT[i],ptr,IP_OFFSET[i]);
		ptr += IP_OFFSET[i] + 0x1 ;
	};
}

// takes care of loading both the bios and the flash rom

int loadSys()
{
	FILE * fp;
	int idx,i;
	short c;
	unsigned char * target = bios_mem;	
	char * path = config.Bios;
	for(i=0;i < 2;i++)
	{

		fp = fopen(config.Bios, "rb");
	
		if (!fp)
		{
			fprintf(stderr, "No se pudo abrir ficheiro %s !\r\n",path);
			return 1;
		}
	
		idx = 0;

		for (c = fgetc(fp); c != EOF && !feof(fp); c = fgetc(fp))
			target[idx++] = c;

		fclose(fp);

		target = flash_mem;
		path = config.Flash;
		
		fprintf(stdout,"Loaded %x bytes\n",idx);
	}
	return 0;
}

// load the ip.bin
int load_bootstrap( char * fname, void * target)
{
	FILE * fp;
	unsigned char * p = (unsigned char *) target;
	unsigned char c;
	int cnt=0;

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
	
	ip_info_load((unsigned char *) target); // loads the info of the current ip.bin
	
	return 0;
}

// loads 1st_read files.Checks if the binary is scrambled or unscrambled
int load_exec(char * fname, void * target)
{
	FILE * fp;
	char c;
	int cnt = 0;
	int t=0;
	char * p = (char *) target;
	char * file = fname;
	char * temp1 = "dcemu-f1.tmp";
	
	// we are gonna check if the binary is scrambled or unscrambled

	t = IdentifyBin(fname);

	if(t)
	{
		descramble(fname,temp1);
		file = temp1;
	}

	fp = fopen(file, "rb");

	if (!fp)
	{
		fprintf(stderr, "No se pudo abrir %s\r\n", file);
		return -1;
	}

	for (c = fgetc(fp); !feof(fp); c = fgetc(fp))
	{
		*(p++) = c;
		cnt++;
	}

	fclose(fp);
	
	unlink(temp1);// cleaning up
	
	return 0;
}