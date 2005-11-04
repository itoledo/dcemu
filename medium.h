#ifndef _medium_h_
#define _medium_h_

#if defined(POSX)
#include "lnxdefs.h"
#endif

typedef struct medium medium;

struct medium
{
	int (* init) (char * input); // function to init the given media support
	int (* read)(char * target, int secstart, int secnum); // the function to read sectors from the medium
	int (* getStatus) (DWORD write_adress); // returns the status of the cd drive.
	int (* getSectorMode) (DWORD write_adress);
	int (* getCdInfo) (void); // fills up the toc
	int (* close)	();
};

typedef enum ACESS_MODE ACESS_MODE;

enum ACESS_MODE {ISO,CDROM,BINARY,BINARY_CD}; // BINARY_CD should be used when a binary loaded from the dcemu directory request acess to the cdrom

// we have a reference to the currently in use medium structure

medium * hook;

typedef struct TOC TOC;

struct TOC {
  unsigned int entry[99];
  unsigned int first, last;
  unsigned int dunno;
};

TOC toc;

// inits everything
void medium_init(ACESS_MODE a, char * p);

// closes the given medium and does all of necessary clean ups
void medium_close();

int medium_run();

// some important definitions
#define DRV_BUSY 0 // Drive is busy
#define DRV_PAUSED   1 //  Drive is paused
#define DRV_STANDBY  2 //  Drive is in standby
#define DRV_PLAYING  3  // Drive is playing
#define DRV_SEEK  4  //  Drive is seeking
#define DRV_SCAN  5  // Drive is scanning
#define DRV_OPEN   6  // Drive lid is open
#define DRV_NO_DISC   7  //  Lid is closed, but there is no disc

#endif
