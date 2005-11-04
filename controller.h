// defines an interface for the controllers

#ifndef _controller_
#define _controller_

#if defined (__GNUC__)
#include "lnxdefs.h"
#endif
#include <SDL.h>
#include "main.h"
#include "debug.h"
#include "gui.h"
#include "intc.h"

typedef enum CONTROL_ACESS_MODE CONTROL_ACESS_MODE;

// the different controller types
enum CONTROL_ACESS_MODE{KEYBOARD,JOYSTICK};

// Maple Device info
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

// Basically what a controller means for dcemu
typedef struct
{
	int (* init)(void);
	maple_devinfo_t dev_info;
	cont_cond_t cond;
	int (* handle)(SDL_Event event);
}controller;

// the connected controller
controller *  control;// the connected input handler

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

// start the controller subsystem
int setController(CONTROL_ACESS_MODE a);

#endif
