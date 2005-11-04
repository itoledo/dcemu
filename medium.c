#include <stdio.h>
#include "medium.h"
#include "iso.h"
#include "cd.h"
#include "file.h"
#include "mem.h"

int bin_init(char * input)
{
  if (load_bootstrap("ip.bin",get_memory_pointer(mem_base + ip_offset)))
	{
		printf("Coudn't load bootstrap");
		return -1;
	}
  if (load_exec(input,get_memory_pointer(mem_base + mem_offset)))
	{
		printf("Coudn't load executable");
		return -1;
	}
 return 0;
}

medium CD_STRUCT =
{
 cd_init,
 cd_read_sector,
 cd_getStatus,
 cd_get_mode,
 cd_get_lba,
 cd_close
};


medium ISO_STRUCT =
{
 iso_init,
 iso_read_sector,
 iso_get_status,
 iso_getSectorMode,
 iso_get_info,
 cd_close
};

medium CD_BIN_STRUCT =
{
 dummy_init,
 cd_read_sector,
 cd_getStatus,
 cd_get_mode,
 cd_get_lba,
 cd_close
};

medium DEMO_STRUCT =
{
 bin_init,
 NULL,
 iso_get_status, // just adding some necessary fields
 iso_getSectorMode, 
 NULL,
 NULL
};

char * path;


void medium_init(ACESS_MODE a,char * p)
{
 switch(a)
 {
	case ISO:
		hook = &ISO_STRUCT;
	break;
	case CDROM:
		hook = &CD_STRUCT;
	break;
	case BINARY: 
		hook = &DEMO_STRUCT; // demos will only use the ramdisk
	break;
	case BINARY_CD:
		hook = &CD_BIN_STRUCT;
	break;
 }
	path = p;
}

int medium_run()
{
	return hook->init(path);
}

void medium_close()
{
	if(hook->read != NULL)
		hook->close();
	hook = NULL;
}