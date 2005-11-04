#include "controller.h"

// we're using the keyboard to emulate the dreamcast's controller
controller keyboard;

int hdl_key(SDL_Event event)
{
	while (SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_KEYDOWN:
				{
					switch(event.key.keysym.sym)
					{
					case SDLK_LEFT:
					REMOVE_BIT(keyboard.cond.buttons, CONT_DPAD_LEFT);
					break;
	
					case SDLK_RIGHT:
					REMOVE_BIT(keyboard.cond.buttons, CONT_DPAD_RIGHT);
					break;
	
					case SDLK_UP:
					REMOVE_BIT(keyboard.cond.buttons, CONT_DPAD_UP);
					break;
	
					case SDLK_DOWN:
					REMOVE_BIT(keyboard.cond.buttons, CONT_DPAD_DOWN);
					break;
					
					case SDLK_a: // BOTON X
					REMOVE_BIT(keyboard.cond.buttons, CONT_X);
					break;
					
					case SDLK_s: // BOTON A
					REMOVE_BIT(keyboard.cond.buttons, CONT_A);
					break;
					
					case SDLK_d: // BOTON B
					REMOVE_BIT(keyboard.cond.buttons, CONT_B);
					break;
					
					case SDLK_w: // BOTON W
					REMOVE_BIT(keyboard.cond.buttons, CONT_Y);
					break;

					case SDLK_z: // START
					REMOVE_BIT(keyboard.cond.buttons, CONT_START);
					break;
					
					case SDLK_q: // LEFT
					keyboard.cond.ltrig = TRIGGER_ON;
					break;

					case SDLK_e: // RIGHT
					keyboard.cond.rtrig = TRIGGER_ON;
					break;

					case SDLK_y: // joystick up
					keyboard.cond.joyy = JOYSTICK_UP;
					break;
					
					case SDLK_h: // joystick down
					keyboard.cond.joyy = JOYSTICK_DOWN;
					break;
					
					case SDLK_g: // joystick left
					keyboard.cond.joyx = JOYSTICK_LEFT;
					break;
					
					case SDLK_j: // joystick right
					keyboard.cond.joyx = JOYSTICK_RIGHT;
					break;

					case SDLK_l:
					gui_setvisiblelog(!gui_isvisiblelog());
					break;
					
					case SDLK_m: // logmem
					if ((filelogging & (FILELOG_MEMREADS | FILELOG_MEMWRITES)) == 0)
					{
						logmsg("activando filelog memoria\n");
						SET_BIT(filelogging, FILELOG_MEMREADS | FILELOG_MEMWRITES);
					}
					else
					{
						REMOVE_BIT(filelogging, FILELOG_MEMREADS | FILELOG_MEMWRITES);
						logmsg("desactivando filelog memoria\n");
					}
					break;

					case SDLK_v: // logmem
					if (logvideomem)
						logvideomem = false;
					else
						logvideomem = true;
					break;

					case SDLK_r: // logmem
					if (logmemreg)
						logmemreg = false;
					else
						logmemreg = true;
					break;
					
					case SDLK_i: // generar int?
					intc(0);
					break;
						
					default:
					break;
					}
				}
				break;
				
				case SDL_KEYUP:
				{
					logmsg("keyup\r\n");
					switch(event.key.keysym.sym)
					{
					case SDLK_LEFT:
					SET_BIT(keyboard.cond.buttons, CONT_DPAD_LEFT);
					break;
	
					case SDLK_RIGHT:
					SET_BIT(keyboard.cond.buttons, CONT_DPAD_RIGHT);
					break;
	
					case SDLK_UP:
					SET_BIT(keyboard.cond.buttons, CONT_DPAD_UP);
					break;
	
					case SDLK_DOWN:
					SET_BIT(keyboard.cond.buttons, CONT_DPAD_DOWN);
					break;
					
					case SDLK_a: // BOTON X
					SET_BIT(keyboard.cond.buttons, CONT_X);
					break;
					
					case SDLK_s: // BOTON A
					SET_BIT(keyboard.cond.buttons, CONT_A);
					break;
					
					case SDLK_d: // BOTON B
					SET_BIT(keyboard.cond.buttons, CONT_B);
					break;
					
					case SDLK_w: // BOTON W
					SET_BIT(keyboard.cond.buttons, CONT_Y);
					break;

					case SDLK_z: // START
					SET_BIT(keyboard.cond.buttons, CONT_START);
					break;

					case SDLK_q: // LEFT
					keyboard.cond.ltrig = TRIGGER_OFF;
					break;

					case SDLK_e: // RIGHT
					keyboard.cond.rtrig = TRIGGER_OFF;
					break;
					
					case SDLK_y: // joystick up
					case SDLK_h: // joystick down
					keyboard.cond.joyy = JOYSTICK_NEUTRAL;
					break;
					
					case SDLK_g: // joystick left
					case SDLK_j: // joystick right
					keyboard.cond.joyx = JOYSTICK_NEUTRAL;
					break;

                    case SDLK_F9:
					DebugMode = DBG_STEP;
					break;

                    case SDLK_F10:
					DebugMode = DBG_STOP;
					break;

                    case SDLK_F11:
					DebugMode = DBG_RUN;
					break;

                    case SDLK_F12:
					DebugVisible = 1 - (DebugVisible);
					glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
					break;

					case SDLK_KP_PLUS:
					MemDebug += 0x140;
					RedibujarPantalla();
					break;
					
					case SDLK_KP_MINUS:
					MemDebug -= 0x140;
					RedibujarPantalla();
					break;
					
					default:
					break;
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

int setUpKeyboardStruct()
{
	//setting up the dev_info structure accordingly
	keyboard.dev_info.func = (1 << 24); // controlador
	keyboard.dev_info.function_data[0] = 0;
	keyboard.dev_info.function_data[1] = 0;
	keyboard.dev_info.function_data[2] = 0;
	keyboard.dev_info.area_code = 0;
	keyboard.dev_info.connector_direction = 0;
	strcpy(keyboard.dev_info.product_name, "Controlador DC");
	strcpy(keyboard.dev_info.product_license, "SEGA");
	keyboard.dev_info.standby_power = 0;
	keyboard.dev_info.max_power = 0;

	//setting up the condition structure
	keyboard.cond.buttons = 0xFFFF;
	keyboard.cond.rtrig = 0;
	keyboard.cond.ltrig = 0;
	keyboard.cond.joyx = 0;
	keyboard.cond.joyy = 0;
	keyboard.cond.joy2x = 128;
	keyboard.cond.joy2y = 128;

	keyboard.handle=hdl_key;

	return 0;
}

int controller_key_init()
{
	setUpKeyboardStruct();
	return 0;
}