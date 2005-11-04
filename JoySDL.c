#include "controller.h"

controller joystick; // we're using the keyboard to emulate the dreamcast's controller
SDL_Joystick * js; // the sdl joystick handler


void setJoyStickStruct()
{
	// if we arrive here a joystick must be connected
	SDL_JoystickEventState(SDL_ENABLE);
	js = SDL_JoystickOpen(0);
		
	//setting up the dev_info structure accordingly
	joystick.dev_info.func = (1 << 24); // controlador
	joystick.dev_info.function_data[0] = 0;
	joystick.dev_info.function_data[1] = 0;
	joystick.dev_info.function_data[2] = 0;
	joystick.dev_info.area_code = 0;
	joystick.dev_info.connector_direction = 0;
	strcpy(joystick.dev_info.product_name, "Controlador DC");
	strcpy(joystick.dev_info.product_license, "SEGA");
	joystick.dev_info.standby_power = 0;
	joystick.dev_info.max_power = 0;

	//setting up the condition structure
	joystick.cond.buttons = 0xFFFF;
	joystick.cond.rtrig = 0;
	joystick.cond.ltrig = 0;
	joystick.cond.joyx = 0;
	joystick.cond.joyy = 0;
	joystick.cond.joy2x = 0;
	joystick.cond.joy2y = 0;
}

int hdl_joy(SDL_Event event)
{

		while (SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_JOYAXISMOTION:
				{
					if ((event.jaxis.value < -3200) || (event.jaxis.value > 3200))
					{
						if (event.jaxis.axis == 0) // izq/der
						{
							if (event.jaxis.value < 0)
							{
								SET_BIT(joystick.cond.buttons, CONT_DPAD_RIGHT);
								REMOVE_BIT(joystick.cond.buttons, CONT_DPAD_LEFT);
							}
							else
							{
								SET_BIT(joystick.cond.buttons, CONT_DPAD_LEFT);
								REMOVE_BIT(joystick.cond.buttons, CONT_DPAD_RIGHT);
							}
						}
						if (event.jaxis.axis == 1) // up/down
						{
							if (event.jaxis.value < 0)
							{
								SET_BIT(joystick.cond.buttons, CONT_DPAD_DOWN);
								REMOVE_BIT(joystick.cond.buttons, CONT_DPAD_UP);
							}
							else
							{
								SET_BIT(joystick.cond.buttons, CONT_DPAD_UP);
								REMOVE_BIT(joystick.cond.buttons, CONT_DPAD_DOWN);
							}
						}
					}
					else
					{
						if (event.jaxis.axis == 0) // izq/der
						{
							SET_BIT(joystick.cond.buttons,CONT_DPAD_LEFT|CONT_DPAD_RIGHT);
						}
						else
						if (event.jaxis.axis == 1) // arr/aba
						{
							SET_BIT(joystick.cond.buttons,CONT_DPAD_UP|CONT_DPAD_DOWN);
						}
					}
				}
				break;
				
				case SDL_JOYBUTTONDOWN:
				{
					logmsg("btdown: %d\r\n", event.jbutton.button);
					switch(event.jbutton.button)
					{
						case 0:	REMOVE_BIT(joystick.cond.buttons, CONT_Y); break;
						case 1: REMOVE_BIT(joystick.cond.buttons, CONT_B); break;
						case 2: REMOVE_BIT(joystick.cond.buttons, CONT_A); break;
						case 3: REMOVE_BIT(joystick.cond.buttons, CONT_X); break;
						case 4: REMOVE_BIT(joystick.cond.buttons, CONT_Y); break;
						case 5: REMOVE_BIT(joystick.cond.buttons, CONT_Z); break;
						case 6: REMOVE_BIT(joystick.cond.buttons, CONT_START); break;
					}
				}
				break;

				case SDL_JOYBUTTONUP:
				{
					logmsg("btup: %d\r\n", event.jbutton.button);
					switch(event.jbutton.button)
					{
						case 0:	SET_BIT(joystick.cond.buttons, CONT_Y); break;
						case 1: SET_BIT(joystick.cond.buttons, CONT_B); break;
						case 2: SET_BIT(joystick.cond.buttons, CONT_A); break;
						case 3: SET_BIT(joystick.cond.buttons, CONT_X); break;
						case 4: SET_BIT(joystick.cond.buttons, CONT_Y); break;
						case 5: SET_BIT(joystick.cond.buttons, CONT_Z); break;
						case 6: SET_BIT(joystick.cond.buttons, CONT_START); break;
					}
				}
				break;

				case SDL_QUIT:
					return -1;
				default:
					gui_event(event);
				break;
		}
	}
	return 0;
}

int controller_joy_init()
{
	setJoyStickStruct();
	joystick.handle=hdl_joy;
	return 0;
}

