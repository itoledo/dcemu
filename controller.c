#include <stdio.h>
#include "controller.h"
#include "KbSdl.h"
#include "JoySDL.h"


/* choose the controller we want use from now on */
int setController(CONTROL_ACESS_MODE a)
{
 switch(a)
 {
 	case KEYBOARD:
		control = &keyboard;
		control->init = controller_key_init;
		puts("Keyboard choosed");
	break;
	case JOYSTICK:	
		if ((SDL_Init(SDL_INIT_JOYSTICK) > 0) && SDL_NumJoysticks() > 1)
		{
			control = &joystick;
			control->init = controller_joy_init;
		}
		else 
		{
			fprintf(stderr,"No joystick detected");
			control = &keyboard;
			control->init = controller_key_init;
		}
	break;
	default:
		puts("BUGGGGGGGG");
	break;
 }
 return 0;

}