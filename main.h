// main.h
#ifndef _MAIN_H_
#define _MAIN_H_

#if defined(WIN32)
#include <windows.h>
#else
#include "lnxdefs.h"
#endif
#include <stdio.h>
#include <SDL/SDL.h>

// OpenGL
#if defined(WIN32)
#include <gl\gl.h>								// Header File For The OpenGL32 Library
#include <gl\glu.h>								// Header File For The GLu32 Library
// #include <gl\glaux.h>								// Header File For The GLaux Library
#else
#include <GL/gl.h>								// Header File For The OpenGL32 Library
#include <GL/glu.h>								// Header File For The GLu32 Library
#include "lnxdefs.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#endif

#include "controller.h"
#include "medium.h"

#include "options.h"
#include "reg.h"
#include "log.h"
#include "sh4.h"

extern int filelogging;
#define FILELOG_OPCODES		1
#define FILELOG_REGISTERS	2
#define FILELOG_CALLS		4
#define FILELOG_MEMREADS	8
#define FILELOG_MEMWRITES	16

#define CHECK_BIT(reg,bit) { if (*reg & bit) logmsg(#reg ": " #bit " activado\r\n"); else logmsg(#reg ": " #bit " desactivado\r\n"); }

// extern BYTE * str_PC;

// graficos.cpp
// extern void PutPixel(Uint32 pos, Uint32 pixel);
// extern void PutPixelW(Uint32 pos, WORD pixel);
// extern void PutPixelL(Uint32 pos, DWORD pixel);
extern void PutPixelN(Uint32 pos, void * data, size_t size);
extern void ReadPixelN(Uint32 pos, void * data, size_t size);
extern bool logmem;
extern bool logmemreg;
extern bool logvideomem;

extern BYTE * memoria;
extern BYTE * video_mem;
extern BYTE * regmem;
extern BYTE * bios_mem;
extern BYTE * flash_mem;
extern BYTE * ta_mem;
extern BYTE * control_mem;	// empezando en 0x005f0000
extern FILE * logfp, * serialfp, * memfp;
extern bool pausa;
extern DWORD instrucciones;

extern DWORD G2_FIFO;		// G2 FIFO
#define AICA_FIFO		(0x01)
#define EXTERNAL_FIFO	(0x10)

extern DWORD MAPLE_DMAADDR;		// 0xa05f6c04
extern DWORD MAPLE_RESET2;		// 0xa05f6c10
extern DWORD MAPLE_ENABLE;		// 0xa05f6c14
extern DWORD MAPLE_STATE;		// 0xa05f6c18
extern DWORD MAPLE_SPEED;		// 0xa05f6c80
extern DWORD MAPLE_RESET1;		// 0xa05f6c8c

extern DWORD snd_dbg;			// ...

#endif // _MAIN_H
