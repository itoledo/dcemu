#define TTF

#if defined(WIN32) && !defined(__GNUC__)
#pragma comment(lib, "SDL.lib")
#pragma comment(lib, "SDLmain.lib")
#ifdef TTF
#pragma comment(lib, "SDL_ttf.lib")
#else
#pragma comment(lib, "SDL_image.lib")
#endif
#pragma comment(lib, "sdlgfx.lib")
#pragma comment(lib, "sdlconsole.lib")
#endif

// #include "windows.h"
#include "main.h"
#include "math.h"
#include <SDL.h>
#include <SDL_opengl.h>
#ifdef TTF
#include <SDL_ttf.h>
#else
#include "BFont.h"
#endif
#include <time.h>
// #include <unistd.h>
// #include "SDL_gfxPrimitives.h"
#include "SDL_console.h"
#include "branch.h"
#include "opcodes.h"
#include "mem.h"
#include "intc.h"
#include "debug.h"

DWORD snd_dbg;			// ...

DWORD G2_FIFO = 0;		// G2 FIFO
DWORD MAPLE_DMAADDR;
DWORD MAPLE_RESET2;
DWORD MAPLE_ENABLE;
DWORD MAPLE_STATE;
DWORD MAPLE_SPEED;
DWORD MAPLE_RESET1;


unsigned char * memoria;
// unsigned char * orig_mem;
unsigned char * video_mem;
unsigned char * regmem;
unsigned char * bios_mem;
unsigned char * ta_mem;

#define MAX_PARAMS 4

struct opcode_cache_st
{
	opcode_f *	func;
	int			idx;
	DWORD		params[MAX_PARAMS];
};

struct opcode_cache_st opcode_cache[CACHE_SIZE];
#define get_cache_offset(dir) (dir - (mem_base + mem_offset))

// variables globales del programa
long cache_call = 0;
long cache_miss = 0;

#ifdef TTF
TTF_Font * font;
#else
BFont_Info * font;
#endif

#ifdef OPENGL
SDL_Color color_blanco = { 0xff, 0xff, 0xff, 0xff };
SDL_Color color_negro = {0x00, 0x00, 0x00, 0 };
#else
SDL_Color color_blanco = { 0xff, 0xff, 0xff, 0x00 };
SDL_Color color_negro = {0x00, 0x00, 0x00, 0 };
#endif
time_t start_time;
long instrucciones = 0;
bool logging = true;
int filelogging = 0;
bool logmem = false;
bool logvideomem = false;
bool logmemreg = false;
short ultopcnt = 0;
struct opcode_log_str ultop[OPMAXCNT];
char lastop[128];
bool pausa = false;
WORD joystick = 0xFFFF;
SDL_Joystick * js;
BYTE * screenbase;
bool refresh_screen = true;
int screenbits = 16;
int screenwidth = 640;
int screenheight = 480;
int framebuffer_size = 0;

/*
void opcode_log(int pos, WORD arg)
{
	if (pos < 0)
	{
		logmsg("pos < 0");
		fflush(logfp);
		abort();
	}

	ultop[ultopcnt].func = ((pos < 0) ? NULL : opcodes[pos].logfunc);
	ultop[ultopcnt].params.pos = pos;
	ultop[ultopcnt].params.arg = arg;
	ultop[ultopcnt].params.PC = PC;

#ifdef PRINT_ASM

#elif FULL_DEBUG_FROM
	if (PC >= FULL_DEBUG_FROM && PC <= FULL_DEBUG_TO)
#else
	if (filelogging & FILELOG_OPCODES)
#endif
	{
		if (ultop[ultopcnt].func)
			(*ultop[ultopcnt].func) (&ultop[ultopcnt].params);
		logmsg("%s\r\n", lastop);
	}

	ultopcnt++;

	ultopcnt %= OPMAXCNT;
}
*/

void query_cache(WORD arg) // función interna
{
	int i=oplist[arg];
	if (i ==-1) 
	{
		return;
	}
	if (opcodes[i].restriccion)
	{
		// no importa i ya que hay que buscar el opcode igual, de acuerdo a las restricciones
		for (i = 0; opcodes[i].opdesc; i++)
		{
			if ((arg & opcodes[i].mask) == opcodes[i].op)
			{
				if (opcodes[i].restriccion)
				{
					bool cont = false;
					switch(opcodes[i].restriccion)
					{
					case REQ_PR_0:
						if (IS_SET(FPSCR, FPSCR_PR))
							cont = true;
						break;
	
					case REQ_SZ_0:
						if (IS_SET(FPSCR, FPSCR_SZ))
							cont = true;
						break;
	
					case REQ_PR_0_SZ_1:
						if (IS_SET(FPSCR, FPSCR_PR) || !IS_SET(FPSCR, FPSCR_SZ))
							cont = true;
						break;
	
					case REQ_PR_1_SZ_0:
						if (!IS_SET(FPSCR, FPSCR_PR) || IS_SET(FPSCR, FPSCR_SZ))
							cont = true;
						break;
					}
					if (cont)
						continue;
				}
				((opcodes[i].funcion) (( arg )));
				break;
			}
		}
	}
	else
		((opcodes[i].funcion) (( arg )));
}

static SDL_Rect upper_screen;

void RedibujarPantalla()
{
	char buf[512], buf2[128];
	SDL_Rect rc;
	int i;

/*	if (pause == false)
	{
		sprintf(buf, "PC: %08x t:%d spd:%d", //, %08x,%08x,%08x %s",
			PC, 
			dif, (dif > 0) ? instrucciones/dif : 0);
		letras = TTF_RenderText_Solid(font, buf, color_blanco);
		SDL_FillRect(screen, &upper_screen, 0xFF000000);
		SDL_BlitSurface(letras, NULL, screen, NULL);
	}
	else */
	if (pausa == true)
	{
		time_t dif = time(NULL) - start_time;
//		int cnt = OPMAXCNT, idx = ultopcnt - 1;
		SDL_Surface * letras;

		rc.x = 0;
//		rc.h = 20 * 11;
		rc.h = 20 * 1;
		rc.w = screenwidth;
		rc.y = 0;

		SDL_FillRect(backscreen, &rc, 0x00000000);

		sprintf(buf, "PC: %08lx t:%d spd:%d SR:%08x", //, %08x,%08x,%08x %s",
			PC, 
			dif, (dif > 0) ? instrucciones/dif : 0, SR);
#ifdef TTF
		letras = TTF_RenderText_Solid(font, buf, color_blanco);
		logmsg("%s\r\n", buf);
		if (letras)
			SDL_BlitSurface(letras, NULL, backscreen, NULL);
#else
		BFont_PutStringFont(backscreen, font, 0, 0, buf);
#endif

/*		if (idx == -1)
		  idx = OPMAXCNT - 1;
		while(cnt > 0)
		{
			if (ultop[idx].func)
		    	(*ultop[idx].func) (&ultop[idx].params);
			logmsg("%s\r\n", lastop);
#ifdef TTF
			letras = TTF_RenderText_Solid(font, lastop, color_blanco);
#else
			letras = BFont_CreateSurfaceFont(font, lastop);
#endif
			if (letras)
			{
				rc.h = letras->h;
				rc.w = letras->w;
				rc.y = (cnt+1)* 18;
				SDL_BlitSurface(letras, NULL, backscreen, &rc);
			}
			idx--;
			cnt--;
			if (idx == -1)
				idx = OPMAXCNT - 1;
		}
		buf[0] = '\0';
		for (i=0; i < 16; i++)
		{
		  sprintf(buf2, "%x,", registers[i]);
		  strcat(buf, buf2);
        }
#ifdef TTF
		letras = TTF_RenderText_Solid(font, buf, color_blanco);
#else
		letras = BFont_CreateSurfaceFont(font, buf);
#endif
		logmsg("%s\r\n", buf);
		if (letras)
		{
			rc.h = letras->h;
			rc.w = letras->w;
			rc.y = 18;
			SDL_BlitSurface(letras, NULL, backscreen, &rc);
		} */
		refresh_screen = true;
	}

	if ((refresh_screen) || (DebugVisible))
	{
#ifndef OPENGL
		SDL_BlitSurface(backscreen, NULL, screen, NULL);
		DebugUpdate();
		SDL_Flip(screen);
#else
//		SDL_BlitSurface(backscreen, NULL, glscreen, NULL);
//		glRasterPos2f(0, 0);
//		SDL_LockSurface(glscreen);
//		glDrawPixels(screenwidth, screenheight, GL_RGB, GL_BYTE, glscreen->pixels);
//		SDL_UnlockSurface(glscreen);
//		SDL_GL_SwapBuffers();
		SDL_BlitSurface(backscreen, NULL, screen, NULL);
		DebugUpdate();
		SDL_Flip(screen);
		glRasterPos2f(0, 0);
		glDrawPixels(screenwidth, screenheight, GL_RGBA, GL_UNSIGNED_BYTE, screen->pixels);
		SDL_GL_SwapBuffers();
#endif

		refresh_screen = false;
	}
}

Uint32 TimerCallback(Uint32 interval, void * param)
{
	SDL_Event e;
	
	e.type = SDL_USEREVENT;
	e.user.code = 1;
/*	e.user.data1 = NULL;
	e.user.data2 = NULL; */

	SDL_PushEvent(&e);

	return 250; /* 1000 */
}

void dma_check()
{
	if (*DMAOR & DME)
	{
/*		fprintf(fp, "DMA activado\r\n");
		fprintf(fp, "SAR0: %08x SAR1:%08x SAR2:%08x SAR3:%08x\r\n",
			*SAR0, *SAR1, *SAR2, *SAR3);
		fprintf(fp, "DAR0: %08x DAR1:%08x DAR2:%08x DAR3:%08x\r\n",
			*DAR0, *DAR1, *DAR2, *DAR3); */
		if (IS_SET_REG(CHCR0, DE))
		{
//			logmsg("DMA: canal 0 activado, SAR0: %x, DAR0: %x, DMATCR0: %x\r\n", *SAR0, *DAR0, *DMATCR0);
		}
		if (IS_SET_REG(CHCR1, DE))
		{
//			logmsg("DMA: canal 1 activado, SAR1: %x, DAR1: %x, DMATCR1: %x\r\n", *SAR1, *DAR1, *DMATCR1);
		}
		if (IS_SET_REG(CHCR2, DE))
		{
//			logmsg("DMA: canal 2 activado, SAR2: %x, DAR2: %x, DMATCR2: %x\r\n", *SAR2, *DAR2, *DMATCR2);
		}
		if (IS_SET_REG(CHCR3, DE))
		{
//			logmsg("DMA: canal 3 activado, SAR3: %x, DAR3: %x, DMATCR3: %x\r\n", *SAR3, *DAR3, *DMATCR3);
		}
	}
}

void timer_check()
{
    if (*TSTR & 8)
    {
//        fprintf(fp, "TCNT2 operando\r\n");
        if ((long) (*TCNT2) < 0) // underflow
        {
//                fprintf(fp, "TCNT2 underflow\r\n");
                *TCNT2 = *TCOR2;
                *TCR2 |= UNF;
        }
        else
                (*TCNT2)--;
    }
    if (*TSTR & 2)
    {
        if ((long) (*TCNT1) < 0) // underflow
        {
//                fprintf(fp, "TCNT1 underflow\r\n");
                *TCNT1 = *TCOR1;
                *TCR1 |= UNF;
        }
        else
                (*TCNT1)--;
    }
    if ((*TSTR) & 1)
    {
//        fprintf(fp, "TCNT0 operando\r\n");
        if ((long) (*TCNT0) < 0) // underflow
        {
//                logmsg("TCNT0 underflow\r\n");
                (*TCNT0) = *TCOR0;
                (*TCR0) |= UNF;
        }
        else
		{
            *TCNT0 = *TCNT0 - 1;
//            fprintf(fp, "Restando 1 a TCNT0: %d\r\n", *TCNT0);
		}
    }
}

void main_loop(void)
{
	WORD instr;
	SDL_Event event;
	DWORD valor;
	
	for (;;)
	{
		for (;;)
		{
			if (DebugMode == DBG_STOP)
				break;
				
			instr = *(WORD *) str_PC;

/*			if (PC == HACK_BASE + HACK_ROMFONT)
			{
				logmsg("HACK_ROMFONT\r\n");
				R(0) = mem_base + 1024*1024*5;
				rts112(instr);
			}
			else  */
			if (PC == HACK_BASE + HACK_GDROM)
			{
				logmsg("HACK_GDROM: r6=%x, r7=%x\r\n", R(6), R(7));
				if (R(6) == 0)
				{
					switch(R(7))
					{
						case 0: // GDROM_SEND_COMMAND
						logmsg("GDROM_SEND_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
						switch(R(4))
						{
							case 16: // read sector, R(5) es el lugar donde se guardan los datos
							{
								int secstart, secnum, idx = 0;
								BYTE c;
								DWORD targetaddr;
								FILE * fp;
								char * targetmem;
								
								memread(R(5), &secstart, sizeof(int));
								memread(R(5) + 4, &secnum, sizeof(int));
								memread(R(5) + 8, &targetaddr, sizeof(DWORD));
								
								secstart -= 11700;
			
								if (secstart != 16)
								{
									logmsg("restando 150 a secstart(%d), final %d\n", secstart, secstart - 150);
									secstart -= 150;
								}

								logmsg("read sector: start=%x, num=%x, addr=%x\n", secstart, secnum, targetaddr);
								fp = fopen("dc.iso", "rb");
								if (!fp)
								{
									logmsg("dc.iso no encontrado");
									break;
								}
								logmsg("seeking a %d, %x\n", secstart * 2048, secstart * 2048);
								fseek(fp,secstart*2048, 0);
								targetmem = malloc(sizeof(char) * 2048);
								while (idx < 2048 * secnum)
								{
									targetmem[idx++] = (c = fgetc(fp));
								}
								fclose(fp);
								memwrite(targetaddr, &targetmem[0], 2048 * secnum);
							}
							break;

							case 19: // read toc, R(5) es el lugar donde se guardan los datos
							{
								int session;
								DWORD targetaddr;
								struct TOC toc;
        
					        	// hay que leer el # de sesión, es el 1er numero que apunta esa dir
					        	memread(R(5), &session, sizeof(int));
					        	logmsg("read toc: sesión n° %d\n", session);
					        	
					        	// a llenar un TOC "mula"
					        	toc.entry[0] = 0x40002DB4; // CTRL = 4, LBA = 11700
					        	toc.first = 0x00010000;
					        	toc.last  = 0x00010000;
					        	toc.dunno = 0;
					        	memread(R(5) + 4, &targetaddr, sizeof(DWORD));
					        	logmsg("escribiendo TOC a %x\n", targetaddr);
					        	memwrite(targetaddr, &toc, sizeof(struct TOC));
	        				}
	        				break;
     					}
						R(0) = 0x6969;
						break;
					
					    case 1: // GDROM_CHECK_COMMAND
					    logmsg("GDROM_CHECK_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
					    R(0) = 2;
					    break;
      
    	                case 2: // GDROM_MAIN_LOOP
    	                logmsg("GDROM_MAIN_LOOP\r\n");
    	                break;

       					case 3: // GDROM_INIT
       					logmsg("GDROM_INIT\r\n");
						break;
						
						case 4: // GDROM_CHECK_DRIVE
						logmsg("GDROM_CHECK_DRIVE\r\n");
						valor = 7; // lid closed, no disc
						WriteMemoryL(R(4), &valor);
						valor = 0x80; // GD-ROM
						WriteMemoryL(R(4) + 4, &valor);
						R(0) = 0;
						break;

						case 10: // GDROM_SECTOR_MODE
						logmsg("GDROM_SECTOR_MODE\r\n");
						ReadMemoryL(R(4), &valor);
						logmsg("valor: %x\r\n", valor);
						if (valor == 0)
						{
							valor = 8192;
							WriteMemoryL(R(4) + 4, &valor);
							valor = 2048; // mode 2
							WriteMemoryL(R(4) + 8, &valor);
							valor = 2048; // sector size in bytes
							WriteMemoryL(R(4) + 12, &valor);
						}
						R(0) = 0;
						break;
						
						default:
						logmsg("GDROM: error!\n");
						break;
					}
				}
				rts112(instr);
			}
/*			else
			if (PC >= mem_base + mem_offset
			&&  PC <  mem_base + mem_offset + CACHE_SIZE
//			if ((PC & 0xFF000000) == 0x8C000000
			&&  opcode_cache[PC - (mem_base + mem_offset)].func
  			&&  opcode_cache[PC - (mem_base + mem_offset)].func != query_cache)
			{
#ifdef LOGGING
				if (logging)
					opcode_log(opcode_cache[PC - (mem_base + mem_offset)].idx, instr);
#endif
				(opcode_cache[PC - (mem_base + mem_offset)].func) (instr);
#if defined(EXTRA_REG_DEBUG) || defined(FULL_DEBUG_FROM)
#ifdef FULL_DEBUG_FROM
			if (PC >= FULL_DEBUG_FROM && PC <= FULL_DEBUG_TO)
#endif
                dump_registers();
#endif
#ifdef CHECK_VALUE
				check_registers();
#endif
			if (filelogging & FILELOG_REGISTERS)
				dump_registers();
			if (filelogging & FILELOG_CALLS)
				opcodes[opcode_cache[PC - (mem_base + mem_offset)].idx].llamadas++;
			} */
			else
				query_cache (instr);
//			cache_call++;

			dma_check();
			timer_check();

			(*PC_func) ();

			instrucciones++;

			check_ints();

			if (DebugMode == DBG_STEP)
			{
				DebugMode = DBG_STOP;
				refresh_screen = true;
				RedibujarPantalla();
			}
   	
			if (instrucciones % 16000 == 0) // cada 10 opcodes revisamos los eventos
				break; // salimos de este ciclo y vamos al siguiente
		}

		while (SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_KEYDOWN:
				{
					logmsg("keydown\r\n");
					switch(event.key.keysym.sym)
					{
					case SDLK_LEFT:
//						logging = true;
					REMOVE_BIT(joystick, CONT_DPAD_LEFT);
					break;
	
					case SDLK_RIGHT:
					REMOVE_BIT(joystick, CONT_DPAD_RIGHT);
/*					    G2_FIFO = 0x20;
						logging = false; */
					break;
	
					case SDLK_UP:
/*						if (pause)
							pause = false;
						else
							pause = true; */
					REMOVE_BIT(joystick, CONT_DPAD_UP);
					break;
	
					case SDLK_DOWN:
					REMOVE_BIT(joystick, CONT_DPAD_DOWN);
					break;
					
					case SDLK_a: // BOTON X
					REMOVE_BIT(joystick, CONT_X);
					break;
					
					case SDLK_s: // BOTON A
					REMOVE_BIT(joystick, CONT_A);
					break;
					
					case SDLK_d: // BOTON B
					REMOVE_BIT(joystick, CONT_B);
					break;
					
					case SDLK_w: // BOTON W
					REMOVE_BIT(joystick, CONT_Y);
					break;

					case SDLK_z: // START
					REMOVE_BIT(joystick, CONT_START);
					break;
					
					case SDLK_l: // empezar el log en archivo
					filelogging++;
					filelogging %= 3;
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

					case SDLK_p: // pausa
					if (pausa)
						pausa = false;
					else
						pausa = true;
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
					SET_BIT(joystick, CONT_DPAD_LEFT);
					break;
	
					case SDLK_RIGHT:
					SET_BIT(joystick, CONT_DPAD_RIGHT);
					break;
	
					case SDLK_UP:
					SET_BIT(joystick, CONT_DPAD_UP);
					break;
	
					case SDLK_DOWN:
					SET_BIT(joystick, CONT_DPAD_DOWN);
					break;
					
					case SDLK_a: // BOTON X
					SET_BIT(joystick, CONT_X);
					break;
					
					case SDLK_s: // BOTON A
					SET_BIT(joystick, CONT_A);
					break;
					
					case SDLK_d: // BOTON B
					SET_BIT(joystick, CONT_B);
					break;
					
					case SDLK_w: // BOTON W
					SET_BIT(joystick, CONT_Y);
					break;

					case SDLK_z: // START
					SET_BIT(joystick, CONT_START);
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
					break;

					case SDLK_KP_PLUS:
					MemDebug += 0x140;
					refresh_screen = true;
					RedibujarPantalla();
					break;
					
					case SDLK_KP_MINUS:
					MemDebug -= 0x140;
					refresh_screen = true;
					RedibujarPantalla();
					break;
					
					default:
					break;
					}

				}
				break;

				case SDL_QUIT:
				return;

				case SDL_USEREVENT:
				RedibujarPantalla();
				break;

#ifdef JOYSTICK
				case SDL_JOYAXISMOTION:
				{
					if ((event.jaxis.value < -3200) || (event.jaxis.value > 3200))
					{
						if (event.jaxis.axis == 0) // izq/der
						{
							if (event.jaxis.value < 0)
							{
								SET_BIT(joystick, CONT_DPAD_RIGHT);
								REMOVE_BIT(joystick, CONT_DPAD_LEFT);
							}
							else
							{
								SET_BIT(joystick, CONT_DPAD_LEFT);
								REMOVE_BIT(joystick, CONT_DPAD_RIGHT);
							}
						}
						if (event.jaxis.axis == 1) // up/down
						{
							if (event.jaxis.value < 0)
							{
								SET_BIT(joystick, CONT_DPAD_DOWN);
								REMOVE_BIT(joystick, CONT_DPAD_UP);
							}
							else
							{
								SET_BIT(joystick, CONT_DPAD_UP);
								REMOVE_BIT(joystick, CONT_DPAD_DOWN);
							}
						}
					}
					else
					{
						if (event.jaxis.axis == 0) // izq/der
						{
							SET_BIT(joystick,CONT_DPAD_LEFT|CONT_DPAD_RIGHT);
						}
						else
						if (event.jaxis.axis == 1) // arr/aba
						{
							SET_BIT(joystick,CONT_DPAD_UP|CONT_DPAD_DOWN);
						}
					}
				}
				break;
				
				case SDL_JOYBUTTONDOWN:
				{
					logmsg("btdown: %d\r\n", event.jbutton.button);
					switch(event.jbutton.button)
					{
						case 0:	REMOVE_BIT(joystick, CONT_Y); break;
						case 1: REMOVE_BIT(joystick, CONT_B); break;
						case 2: REMOVE_BIT(joystick, CONT_A); break;
						case 3: REMOVE_BIT(joystick, CONT_X); break;
						case 4: REMOVE_BIT(joystick, CONT_Y); break;
						case 5: REMOVE_BIT(joystick, CONT_Z); break;
						case 6: REMOVE_BIT(joystick, CONT_START); break;
					}
				}
				break;

				case SDL_JOYBUTTONUP:
				{
					logmsg("btup: %d\r\n", event.jbutton.button);
					switch(event.jbutton.button)
					{
						case 0:	SET_BIT(joystick, CONT_Y); break;
						case 1: SET_BIT(joystick, CONT_B); break;
						case 2: SET_BIT(joystick, CONT_A); break;
						case 3: SET_BIT(joystick, CONT_X); break;
						case 4: SET_BIT(joystick, CONT_Y); break;
						case 5: SET_BIT(joystick, CONT_Z); break;
						case 6: SET_BIT(joystick, CONT_START); break;
					}
				}
				break;
#endif // JOYSTICK
			}
		}
	}
}

PC_f * PC_func;

int cargar_bios()
{
	FILE * fp;
	int idx;
	short c;

	// a cargar ip.bin
	fp = fopen("bios/bios.bin", "rb");

	if (!fp)
	{
		fprintf(stderr, "No se pudo abrir BIOS!\r\n");
		return 1;
	}
	
	idx = 0;

	for (c = fgetc(fp); c != EOF && !feof(fp); c = fgetc(fp))
		bios_mem[idx++] = c;

	fclose(fp);

	fprintf(stderr, "Cargados %x bytes de BIOS.\r\n", idx);
	return 0;
}

void inicializar_fonts()
{
	// 288 narrow (12 x 24, 36 bytes / char)
	// letra H
	char * letras = "_!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	char * ptr = letras;
	char buf[2];
	SDL_Surface * lbuf;
	BYTE * p, nibble;
	int x, y, bits;
	int cnt;
	
	buf[1] = '\0';
//	cnt = mem_n_base + 1024*1024*5; // 5 megas hacia adelante
	cnt = FONT_BASE;

	while (*ptr)
	{
		buf[0] = *ptr;
#ifdef TTF
		lbuf = TTF_RenderText_Solid(font, buf, color_blanco);
#else
		lbuf = BFont_CreateSurfaceFont(font, buf);
#endif

		p = lbuf->pixels;
		bits = 0;

		for (y = 0; y < 24; y++)
		{
			for (x = 0; x < 12; x++)
			{
				// hay que formar el nibble
				if (x < lbuf->w && y < lbuf->h)
				{
					p = (BYTE *) lbuf->pixels + y * lbuf->pitch + x * lbuf->format->BytesPerPixel;
					if (*p)
					{
						nibble |= (1 << (7 - (bits % 8)));
					}
				}
				bits++;
				if (bits % 8 == 0)
				{
//					memoria[cnt++] = nibble;
					memwrite(cnt, &nibble, sizeof(BYTE));
					nibble = 0;
				}
			}
		}

//		fprintf(fp, "Finalizamos la letra %c.\r\n", *ptr);
		ptr++;
	}
}

int cargar_archivo( char * fname, void * target)
{
	FILE * fp;
	char c;
	int cnt = 0;
	char * p = (char *) target;

	// a cargar ip.bin
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
	
	return cnt;
}

int main(int argc, char *argv[])
{
//	long idx, cnt = 0;
 	long i, j, tam;
//	short c;
	WORD wvalor;
	DWORD dwvalor;

	SDL_TimerID timer_id;

	unsigned char * moffset;
	FILE * fp; 

	inicializar_logs();

    /* initialize SDL */
#ifdef _DEBUG
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE
#else
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER
#endif
#ifdef JOYSTICK
		| SDL_INIT_JOYSTICK
#endif // JOYSTICK
		) < 0 )
	{
		fprintf( stderr, "Video initialization failed: %s\n",
			SDL_GetError( ) );
		SDL_Quit( );
	}

#ifdef JOYSTICK
	if (SDL_NumJoysticks() < 1)
	{
		fprintf(stderr, "No se encontraron joysticks.\r\n");
		SDL_Quit();
	}

	SDL_JoystickEventState(SDL_ENABLE);
	js = SDL_JoystickOpen(0);
#endif

	joystick = 0xFFFF;

	upper_screen.x = upper_screen.y = 0;
	upper_screen.w = 640;
	upper_screen.h = 22;

//	screen = SDL_SetVideoMode(320, 240, 16, SDL_DOUBLEBUF);

	screeninit(640, 480, 16);

//	SDL_SetAlpha(screen, SDL_RLEACCEL, 128);

#ifdef TTF
	TTF_Init();
    
	font = TTF_OpenFont("font.ttf", 16);
#else
	font = BFont_LoadFont("font.png");
//	font = BFont_SetFontColor(font, 0xff, 0xff, 0xff);
#endif

	if (!font)
	{
		fprintf(stderr, "No se pudo cargar font.");
		return 1;
	}

	if (inicializar_memoria())
		return 1;

	// a configurar las tablas de memoria, etc.
	mem_hash_setup();
	regmem_setup();
	initopcodes();
///*	
	logmsg("cargando bios (bios.bin)\n");
    if (cargar_bios())
		return 1; 
//*/

	// a cargar ip.bin
	logmsg("cargando ip.bin\n");

//	if (cargar_archivo("ip.bin", &memoria[mem_n_base + ip_offset]) < 0)
	if (cargar_archivo("ip.bin", get_memory_pointer(mem_base + ip_offset)) < 0)
	{
		fprintf(stderr, "No se pudo abrir ip.bin");
		return 1;
	}

	logmsg("vaciando cache\n");

	for (i = 0; i < CACHE_SIZE; i++)
	{
		opcode_cache[i].func = query_cache;
		opcode_cache[i].idx = -1;
	}

	// a cargar 1st_read.bin
	logmsg("cargando 1st_read.bin\n");

//	if ((tam = cargar_archivo("1st_read.bin", &memoria[mem_n_base + mem_offset])) < 0)
	if ((tam = cargar_archivo("1st_read.bin", get_memory_pointer(mem_base + mem_offset))) < 0)
	{
		fprintf(stderr, "No se pudo abrir 1st_read.bin");
		return 1;
	}

	PC = mem_base + ip_bs1_offset; // ip_bs1_offset; // + mem_offset;
//	PC = 0x8c008300;
//	PC = 0;
	
	if (DebugInit(screen))
 	{
		fprintf(stderr, "No se pudo crear pantallas para debug\r\n");
		return 1;
	}

//	DebugShow();
//	ConsolePrintf(0, "%s", "test");
	
	logmsg("añadiendo timer\n");

	timer_id = SDL_AddTimer(20, TimerCallback, NULL);

	if (timer_id == NULL)
	{
		fprintf(stderr, "No se pudo crear timer: %s\r\n", SDL_GetError());
		return 1;
	}

	if ( SDL_MUSTLOCK(screen) )
	{
		fprintf(logfp, "Es necesario SDL_Lock\r\n");
	}

	moffset = &memoria[mem_n_base + mem_offset]; // ip_bs1_offset]; // &memoria[mem_offset];
//	str_PC = &memoria[mem_n_base + ip_bs1_offset];
//	str_PC = &memoria[0xc008300];
//	str_PC = &bios_mem[0];
	str_PC = get_memory_pointer(mem_base + ip_bs1_offset);

	R(15) = mem_base + mem_offset + 1024*1024*15 - 4;

	logmsg("inicializando fonts\n");

	inicializar_fonts();

	// HACK!
//	*(DWORD *) &memoria[SYSCALL_ROMFONT - mem_base] = SYSCALL_ROMFONT;
//	*(DWORD *) &memoria[SYSCALL_ROMFONT - 0x80000000] = SYSCALL_ROMFONT;

//	*(DWORD *) &memoria[SYSCALL_ROMFONT - 0x80000000]	= HACK_BASE + HACK_ROMFONT;
 	dwvalor = HACK_BASE + HACK_ROMFONT;	memwrite(SYSCALL_ROMFONT, &dwvalor, sizeof(DWORD));

 	// vamos a hacer esto:
/*		HACK_BASE		:	MOV.L (disp*4 + PC & 0xFFFFFFFC + 4), R0
 		HACK_BASE + 2	:	RTS
 		HACK_BASE + 4	:	<POSICION DEL FONT EN LA MEMORIA> */
 
 	wvalor = 0xD000;		memwrite(HACK_BASE + HACK_ROMFONT + 0, &wvalor, 2);
 	wvalor = 0x000B;		memwrite(HACK_BASE + HACK_ROMFONT + 2, &wvalor, 2);
 	dwvalor = FONT_BASE;	memwrite(HACK_BASE + HACK_ROMFONT + 4, &dwvalor, 4);

/*	*(DWORD *) &memoria[SYSCALL_GDROM	- 0x80000000]	= HACK_BASE + HACK_GDROM;
	*(WORD *) &memoria[HACK_BASE + HACK_GDROM - 0x80000000] = 0x0009; // NOP */
	dwvalor = HACK_BASE + HACK_GDROM;	memwrite(SYSCALL_GDROM, &dwvalor, sizeof(DWORD));
	wvalor = 0x0009;					memwrite(HACK_BASE + HACK_GDROM, &wvalor, 2);

	start_time = time(NULL);

	PC_func = PC_f_normal;
	
	/* registers[1] = 1;
	R(0) = 0;
	dump_registers();
	negc68(0x0110);
	dump_registers();
	negc68(0x0000);
	dump_registers(); */

	logmsg("llamando a main_loop\n");

	main_loop();

	SDL_RemoveTimer(timer_id);

	fprintf(logfp, "PC:%lx VBR:%lx\r\n", PC, VBR);

	fclose(logfp);
	fclose(serialfp);
	
	free(memoria);
	free(video_mem);
	free(regmem);

#ifdef TTF
	TTF_CloseFont(font);
#endif

	SDL_Quit( );

	return 0;
}
