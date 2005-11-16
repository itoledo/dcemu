// main.h
#ifndef _MAIN_H_
#define _MAIN_H_

#ifdef WIN32
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
#endif

#if defined(POSX) 
#include <GL/gl.h>								// Header File For The OpenGL32 Library
#include <GL/glu.h>								// Header File For The GLu32 Library
#include "lnxdefs.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#endif

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

// extern unsigned char * str_PC;

// graficos.cpp
// extern void PutPixel(Uint32 pos, Uint32 pixel);
// extern void PutPixelW(Uint32 pos, WORD pixel);
// extern void PutPixelL(Uint32 pos, DWORD pixel);
extern void PutPixelN(Uint32 pos, void * data, size_t size);
extern void ReadPixelN(Uint32 pos, void * data, size_t size);
extern bool logmem;
extern bool logmemreg;
extern bool logvideomem;

// constantes
#define mem_base		0x8C000000
#define mem_n_base		0x0C000000
#define mem_offset		0x00010000
#define ip_offset		0x00008000
#define ip_bs1_offset	0x0000B800
#define ip_bs2_offset	0x0000E000
#define mem_ram_base	0x0C000000
#define mem_n_ram_base	0x0C000000
#define mem_ram_size	0x01000000
#define CACHE_SIZE		(4096 * 1024) // 1024 kb

#define video_base		0xa5000000
#define n_video_base	0x05000000

#define HACK_BASE		0x8C000100
#define HACK_ROMFONT	0x000
#define HACK_GDROM		0x100
#define HACK_SYSINFO	0x200
#define HACK_FLASHROM	0x300
#define HACK_UNKNOWN	0x400

#define FONT_BASE		(mem_base + 1024*1024*5)
#define FONT_N_BASE		(mem_n_base + 1024*1024*5)

#define SYSCALL_SYSINFO		0x8C0000B0
#define SYSCALL_ROMFONT		0x8C0000B4
#define SYSCALL_FLASHROM	0x8C0000B8
#define SYSCALL_GDROM		0x8C0000BC
#define SYSCALL_UNKNOWN		0x8C0000E0

extern unsigned char * memoria;
// extern unsigned char * orig_mem;
extern unsigned char * video_mem;
extern unsigned char * regmem;
extern unsigned char * bios_mem;
extern unsigned char * ta_mem;
extern unsigned char * control_mem;	// empezando en 0x005f0000
extern FILE * logfp, * serialfp, * memfp;
extern bool pausa;
extern unsigned long instrucciones;

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

// joystick
extern WORD joystick;
extern unsigned char ltrig, rtrig;
extern unsigned char joyx, joyy;

#define TRIGGER_ON		0xFF
#define TRIGGER_OFF		0x00

#define JOYSTICK_UP		0x00
#define JOYSTICK_DOWN	0xFF
#define JOYSTICK_LEFT	0x00
#define JOYSTICK_RIGHT	0xFF
#define JOYSTICK_NEUTRAL	128

#define CONT_C  		(1<<0)
#define CONT_B  		(1<<1)
#define CONT_A  		(1<<2)
#define CONT_START		(1<<3)
#define CONT_DPAD_UP	(1<<4)
#define CONT_DPAD_DOWN  (1<<5)
#define CONT_DPAD_LEFT  (1<<6)
#define CONT_DPAD_RIGHT (1<<7)
#define CONT_Z  		   (1<<8)
#define CONT_Y  		   (1<<9)
#define CONT_X  		   (1<<10)
#define CONT_D  		   (1<<11)
#define CONT_DPAD2_UP		   (1<<12)
#define CONT_DPAD2_DOWN 	   (1<<13)
#define CONT_DPAD2_LEFT 	   (1<<14)
#define CONT_DPAD2_RIGHT	   (1<<15)

typedef struct {
	DWORD		func;
	DWORD		function_data[3];
	BYTE		area_code;
	BYTE		connector_direction;
	char		product_name[30];
	char		product_license[60];
	WORD		standby_power;
	WORD		max_power;
} maple_devinfo_t;

/* controller condition structure */
typedef struct {
	WORD buttons;			/* buttons bitfield */
	BYTE rtrig;			/* right trigger */
	BYTE ltrig;			/* left trigger */
	BYTE joyx;			/* joystick X */
	BYTE joyy;			/* joystick Y */
	BYTE joy2x;			/* second joystick X */
	BYTE joy2y;			/* second joystick Y */
} cont_cond_t;

struct TOC {
  unsigned int entry[99];
  unsigned int first, last;
  unsigned int dunno;
};

#endif // _MAIN_H
