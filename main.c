// #define TTF

#if defined(WIN32) && !defined(__GNUC__)
#pragma comment(lib, "SDL.lib")
#pragma comment(lib, "SDLmain.lib")
#ifdef TTF
#pragma comment(lib, "SDL_ttf.lib")
#else
#pragma comment(lib, "SDL_image.lib")
#endif
#pragma comment(lib, "sdlgfx.lib")
// #pragma comment(lib, "sdlconsole.lib")
#endif

// #include "windows.h"
#include "main.h"
#include "math.h"
#include <SDL/SDL_opengl.h>
// #include <SDL_thread.h>
#ifdef TTF
#include <SDL_ttf.h>
#else
#include "BFont.h"
#endif
#include <time.h>
// #include <unistd.h>
// #include "SDL_gfxPrimitives.h"
// #include "SDL_console.h"
#include "branch.h"
#include "opcodes.h"
#include "mem.h"
#include "intc.h"
#include "debug.h"
#include "graficos.h"
#include "iso.h"
#include "gui.h"
#include "sh4emu.h"

DWORD snd_dbg;			// ...

DWORD G2_FIFO = 0;		// G2 FIFO
DWORD MAPLE_DMAADDR;
DWORD MAPLE_RESET2;
DWORD MAPLE_ENABLE;
DWORD MAPLE_STATE;
DWORD MAPLE_SPEED;
DWORD MAPLE_RESET1;

#define MAX_PARAMS 4

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
unsigned char ltrig = TRIGGER_OFF, rtrig = TRIGGER_OFF;
unsigned char joyx = JOYSTICK_NEUTRAL, joyy = JOYSTICK_NEUTRAL;
SDL_Joystick * js;
bool gui_visible=true;


/* void query_cache(WORD arg)
{
	(opcodes[oplist[arg]].funcion) (arg);
} */

#define TMU_INT

void timer_check();

void timer_check(void)
{
	    if (*TSTR & 4)
	    {
	        if ((long) (*TCNT2) < 0) // underflow
	        {
	                *TCNT2 = *TCOR2;
	                *TCR2 |= TMU_TCR_UNF;
#ifdef TMU_INT
					if ((*TCR2 & TMU_TCR_UNIE))
    	            	intc(EXC_TMU2_TUNI2);
#endif
	   	     }
   		     else
   		     {
					(*TCNT2)--;
			}
	   	 }

	    if (*TSTR & 2)
	    {
	        if ((long) (*TCNT1) < 0) // underflow
	        {
	                *TCNT1 = *TCOR1;
	                *TCR1 |= TMU_TCR_UNF;
#ifdef TMU_INT
				if ((*TCR1 & TMU_TCR_UNIE))
	                intc(EXC_TMU1_TUNI1);
#endif
	        }
	        else
   		     {
					(*TCNT1)--;
			}
	    }

	    if (*TSTR & 1)
	    {
	        if ((long) (*TCNT0) < 0) // underflow
	        {
	                (*TCNT0) = *TCOR0;
	                (*TCR0) |= TMU_TCR_UNF;
#ifdef TMU_INT
 					if ((*TCR0 & TMU_TCR_UNIE))
		                intc(EXC_TMU0_TUNI0);
#endif
	        }
	        else
   		     {
					(*TCNT0)--;
			}
	    }
}


void RedibujarPantalla()
{
	logxmsg(LOG_PVR, "RedibujarPantalla()\n");
	if (DebugVisible)
	{
		DebugUpdate();
		DibujarGL(DebugWindow);
		return;
	}
	if (pvr_framebufferdisplay == true)
	{
		logxmsg(LOG_PVR, "RedibujarPantalla: SDL_GL_SwapBuffers\n");
		DibujarFramebuffer();
		gui_refresh();
		SDL_GL_SwapBuffers();
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	}
}

Uint32 VBlankCallback(Uint32 interval, void * param)
{
	logmsg("VBlankCallback: %d\n", pvr_scanline);
	
	pvr_scanline++;

	if (pvr_scanline == pvr_spg_vblank_int_out)
	{
		logxmsg(LOG_INTC, "VBlankCallback: llamando SCANINT1\n");
		intc_add(ASIC_EVT_PVR_SCANINT1, 0);
	}
	else
	if (pvr_scanline == pvr_spg_vblank_int_in)
	{
		logxmsg(LOG_INTC, "VBlankCallback: llamando SCANINT2\n");
		intc_add(ASIC_EVT_PVR_SCANINT2, 0);
	}

//	pvr_scanline %= 0x1FF;
	pvr_scanline %= pvr_spg_load_vcount;

	return 1;
}

void dma_check()
{
	if (*DMAOR & DME)
	{
/*		logxmsg(LOG_MEM, "DMA activado\n");
		logxmsg(LOG_MEM, "SAR0: %08x SAR1:%08x SAR2:%08x SAR3:%08x\n",
			*SAR0, *SAR1, *SAR2, *SAR3);
		logxmsg(LOG_MEM, "DAR0: %08x DAR1:%08x DAR2:%08x DAR3:%08x\n",
			*DAR0, *DAR1, *DAR2, *DAR3); */
		if (IS_SET_REG(CHCR0, DE))
		{
			logxmsg(LOG_MEM, "DMA: canal 0 activado, SAR0: %x, DAR0: %x, DMATCR0: %x\r\n", *SAR0, *DAR0, *DMATCR0);
		}
		if (IS_SET_REG(CHCR1, DE))
		{
			logxmsg(LOG_MEM, "DMA: canal 1 activado, SAR1: %x, DAR1: %x, DMATCR1: %x\r\n", *SAR1, *DAR1, *DMATCR1);
		}
		if (IS_SET_REG(CHCR2, DE))
		{
			logxmsg(LOG_MEM, "DMA: canal 2 activado, SAR2: %x, DAR2: %x, DMATCR2: %x\r\n", *SAR2, *DAR2, *DMATCR2);
		}
		if (IS_SET_REG(CHCR3, DE))
		{
			logxmsg(LOG_MEM, "DMA: canal 3 activado, SAR3: %x, DAR3: %x, DMATCR3: %x\r\n", *SAR3, *DAR3, *DMATCR3);
		}
	}
}

void main_loop(void)
{
	SDL_Event event;
	int cnt=0;
//	DWORD valor;
//	int timer_cnt = 0;

	timer_check(); // we need to check the timer on when 0 cycles have been ran

	for (;;)
	{
		for (;;)
		{
			if (DebugMode == DBG_STOP)
				break;
			
#ifdef PRINT_ASM
	disasm(PC, &buf[0]);
	logmsg("TRACE: %s\r\n", buf);
#endif	
//		instr = *(WORD *) str_PC;

			core.execute(*(WORD *) get_memory_pointer(PC));

//			(*PC_func) ();

//			dma_check();

			// de acuerdo a KOS 1.3, el timer recorre (50000000 / 64) ticks/segundo.
			// por lo que en un segundo tenemos 781250 ticks.
			// cada 1 ms -> 781,25 ticks.
			if (core.context.cycles == 50)
			{
				core.context.cycles=0;
				timer_check();
		
			}
			if(intc_queuemask)
				check_ints();

/*			if (PC == BreakPoint)
				DebugMode = DBG_STOP; */
				
			if (DebugMode == DBG_STEP)
			{
				DebugMode = DBG_STOP;
				RedibujarPantalla();
			}
   	
			core.context.cycles++;

//			if (cnt % (500000 / 0x1FF) == 0)
			if (++cnt == 978) // 978)
			{
				pvr_scanline++;
   				cnt = 0;
   				
				if (pvr_scanline == pvr_spg_vblank_int_out)
				{
        			logxmsg(LOG_PVR, "llamando SCANINT1\n");
    				intc_add(ASIC_EVT_PVR_SCANINT1, 0);
				}
				else
				if (pvr_scanline == pvr_spg_vblank_int_in)
				{
        			logxmsg(LOG_PVR, "llamando SCANINT2\n");
    				intc_add(ASIC_EVT_PVR_SCANINT2, 0);
				}

//				if ((++cnt) == 500000)
				if (pvr_scanline >= pvr_spg_load_vcount) // valor máximo que puede tomar
				{
	   				pvr_scanline = 0;
//	   				cnt = 0;
					break; // salimos de este ciclo y vamos al siguiente
				}
			}

		}

		logxmsg(LOG_PVR, "llamando VBLINT\n");
		intc_add(ASIC_EVT_PVR_VBLINT, 0);
//		intc_check(ASIC_EVT_PVR_VBLINT);
		RedibujarPantalla();

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
					
					case SDLK_q: // LEFT
					ltrig = TRIGGER_ON;
					break;

					case SDLK_e: // RIGHT
					rtrig = TRIGGER_ON;
					break;

					case SDLK_y: // joystick up
					joyy = JOYSTICK_UP;
					break;
					
					case SDLK_h: // joystick down
					joyy = JOYSTICK_DOWN;
					break;
					
					case SDLK_g: // joystick left
					joyx = JOYSTICK_LEFT;
					break;
					
					case SDLK_j: // joystick right
					joyx = JOYSTICK_RIGHT;
					break;

					case SDLK_l: // empezar el log en archivo
/*					filelogging++;
					filelogging %= 3; */
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

					case SDLK_p: // pausa
					if (pausa)
						pausa = false;
					else
						pausa = true;
					break;
					
/*					case SDLK_i: // generar int?
					intc(0);
					break; */
						
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

					case SDLK_q: // LEFT
					ltrig = TRIGGER_OFF;
					break;

					case SDLK_e: // RIGHT
					rtrig = TRIGGER_OFF;
					break;
					
					case SDLK_y: // joystick up
					case SDLK_h: // joystick down
					joyy = JOYSTICK_NEUTRAL;
					break;
					
					case SDLK_g: // joystick left
					case SDLK_j: // joystick right
					joyx = JOYSTICK_NEUTRAL;
					break;
		   // toggle fullscreen
                    case SDLK_F1:
					SDL_WM_ToggleFullScreen(outputscreen);
					break;
                    case SDLK_F2:
					if(gui_visible == true) gui_visible=false;
					else gui_visible=true;
					gui_setvisiblelog(gui_visible);
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
				return;

/*				case SDL_USEREVENT:
				RedibujarPantalla();
				break; */

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

				default:
//           			  SDL_EventState(event.type, SDL_IGNORE);
			gui_event(event);
				break;
			}
		}
	}

	logmsg("saliendo de main_loop\n");
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
#ifndef USE_BIOS_FONT
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
					memwrite(cnt++, &nibble, sizeof(BYTE));
					nibble = 0;
				}
			}
		}

//		logmsg("Finalizamos la letra %c.\r\n", *ptr);
		ptr++;
	}
#endif
}

void exitproc(void)
{
	logmsg("Exited with PC = %08x", PC);
}

int main(int argc, char *argv[])
{
//	long idx, cnt = 0;
 	long i, tam; // , j;
//	short c;
	WORD wvalor;
	DWORD dwvalor;
//	SDL_TimerID vblank_id;
//	SDL_Thread * timer_thread;

	//FILE * fp; 

	inicializar_logs();

    /* initialize SDL */
#ifdef _DEBUG
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE
#else
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER
//	if ( SDL_Init( SDL_INIT_VIDEO
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

//	screen = SDL_SetVideoMode(320, 240, 16, SDL_DOUBLEBUF);

	if (glinit() != 0)
		return 1;
	
	screeninit();

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

	// determinemos qué vamos a cargar
	char * ejecutable = argv[1] ? argv[1] : "1st_read.bin";
	
	fprintf(stderr, "usando %s como parametro.\n", ejecutable);

	if (strncmp(&ejecutable[strlen(ejecutable) - 4], ".bin", 4) == 0)
	{
		if (iso_init(NULL))
		{
	 		fprintf(stderr, "No se pudo inicializar ISO.\n");
			return 1;
		}

		// a cargar ip.bin
		logmsg("cargando ip.bin\n");
	
	//	if (cargar_archivo("ip.bin", &memoria[mem_n_base + ip_offset]) < 0)
		if (cargar_archivo("ip.bin", get_memory_pointer(mem_base + ip_offset)) < 0)
		{
			fprintf(stderr, "No se pudo abrir ip.bin.\n");
			return 1;
		}
	
		// a cargar 1st_read.bin
		logmsg("cargando %s\n", ejecutable);
	
	//	if ((tam = cargar_archivo("1st_read.bin", &memoria[mem_n_base + mem_offset])) < 0)
		if ((tam = cargar_archivo(ejecutable, get_memory_pointer(mem_base + mem_offset))) < 0)
		{
			fprintf(stderr, "No se pudo abrir %s.\n", ejecutable);
			return 1;
		}
	}
	else
	{
		// leamos la ISO
		if (iso_init(ejecutable))
		{
			fprintf(stderr, "No se pudo cargar ISO.\n");
			return 1;
		}
		
		if (cargar_archivo_iso("ip.bin", false, get_memory_pointer(mem_base + ip_offset)) <= 0)
		{
			fprintf(stderr, "No se pudo abrir ip.bin. Cargando desde el bootstrap.\n");

			if (cargar_ip_bin(get_memory_pointer(mem_base + ip_offset)) <= 0)
			{
				fprintf(stderr, "No se pudo cargar ip.bin desde bootstrap.\n");
				return 1;
			}
		}

		// busquemos el ejecutable
		if ((tam = cargar_archivo_iso("1st_read.bin", true, get_memory_pointer(mem_base + mem_offset))) <= 0)
		{
			fprintf(stderr, "No se pudo abrir %s.\n", ejecutable);
			return 1;
		}
		
		fprintf(stderr, "leidos %d bytes\n", tam);
	}

	// we start the cpu
	// allocating the current cpu
	initCpuSubSystem();
#ifdef _fast_interpreter_
	init_cpu_jumptable();
#endif
//  	PC = mem_base + ip_bs1_offset; // ip_bs1_offset; // + mem_offset;
// 	PC = 0x8c010000;
//	PC = 0x00000000;
//	PC = 0x8c0000e0;
    PC = 0x8C008300;

//	str_PC = get_memory_pointer(PC);
	
	if (DebugInit())
 	{
		fprintf(stderr, "No se pudo crear pantallas para debug\r\n");
		return 1;
	}

//	DebugShow();
//	ConsolePrintf(0, "%s", "test");
	
//	regmap_mutex = SDL_CreateMutex();
	
//	timer_thread = SDL_CreateThread(timer_check, NULL);

/*	vblank_id = SDL_AddTimer(10, VBlankCallback, NULL);

	if (vblank_id == NULL)
	{
		fprintf(stderr, "No se pudo crear timer: %s\r\n", SDL_GetError());
		return 1;
	} */

	if ( SDL_MUSTLOCK(screen) )
	{
		fprintf(logfp, "Es necesario SDL_Lock\r\n");
	}

	R(15) = mem_base + mem_offset + 1024*1024*15 - 4;

#ifndef USE_BIOS_FONT
	logmsg("inicializando fonts\n");
	inicializar_fonts();
#endif	

#ifdef BIOS_HACKS
	// HACK!
 	dwvalor = HACK_BASE + HACK_ROMFONT;	memwrite(SYSCALL_ROMFONT, &dwvalor, sizeof(DWORD));
	dwvalor = HACK_BASE + HACK_GDROM;	memwrite(SYSCALL_GDROM, &dwvalor, sizeof(DWORD));
 	dwvalor = HACK_BASE + HACK_SYSINFO; memwrite(SYSCALL_SYSINFO, &dwvalor, sizeof(DWORD));
 	dwvalor = HACK_BASE + HACK_FLASHROM; memwrite(SYSCALL_FLASHROM, &dwvalor, sizeof(DWORD));
 	dwvalor = HACK_BASE + HACK_UNKNOWN; memwrite(SYSCALL_UNKNOWN, &dwvalor, sizeof(DWORD));

//	vamos a hacer esto:
//		HACK_BASE       :	RTS
//		HACK_BASE + 2	:	MOV.L (disp*4 + PC & 0xFFFFFFFC + 4), R0
// 		HACK_BASE + 4	:	<POSICION DEL FONT EN LA MEMORIA>
 
 	wvalor = 0x000B;		memwrite(HACK_BASE + HACK_ROMFONT + 0, &wvalor, 2);
 	wvalor = 0xD000;		memwrite(HACK_BASE + HACK_ROMFONT + 2, &wvalor, 2);
#ifdef USE_BIOS_FONT 	
 	dwvalor = 0x00100020;	memwrite(HACK_BASE + HACK_ROMFONT + 4, &dwvalor, 4);
#else 	
 	dwvalor = FONT_BASE;	memwrite(HACK_BASE + HACK_ROMFONT + 4, &dwvalor, 4);
#endif 	

	wvalor = 0x000B; /* RTS */			memwrite(HACK_BASE + HACK_GDROM, &wvalor, 2);
	wvalor = 0xFFFF; /* BIOS_HACK*/		memwrite(HACK_BASE + HACK_GDROM + 2, &wvalor, 2);

	wvalor = 0x000B;					memwrite(HACK_BASE + HACK_SYSINFO, &wvalor, 2);
	wvalor = 0x0009;					memwrite(HACK_BASE + HACK_SYSINFO + 2, &wvalor, 2);

	wvalor = 0x000B;					memwrite(HACK_BASE + HACK_FLASHROM, &wvalor, 2);
	wvalor = 0x0009;					memwrite(HACK_BASE + HACK_FLASHROM + 2, &wvalor, 2);

	wvalor = 0x000B;					memwrite(HACK_BASE + HACK_UNKNOWN, &wvalor, 2);
	wvalor = 0x0009;					memwrite(HACK_BASE + HACK_UNKNOWN + 2, &wvalor, 2);
#endif // BIOS_HACKS

	start_time = time(NULL);

//	PC_func = PC_f_normal;
	
	/* registers[1] = 1;
	R(0) = 0;
	dump_registers();
	negc68(0x0110);
	dump_registers();
	negc68(0x0000);
	dump_registers(); */

	logmsg("llamando a main_loop\n");

	atexit(exitproc);

#if defined(DEBUG_MEM_READ) || defined(DEBUG_MEM_WRITE)
	filelogging |= FILELOG_MEMREADS | FILELOG_MEMWRITES;
#endif

	#ifdef X86_OPT
	printf("Build: '%s'\n", SIMDx86_GetBuildString());
	#endif

	main_loop();

//	SDL_RemoveTimer(timer_id);
//	SDL_RemoveTimer(vblank_id);
	
//	timer_running = 0;
//	SDL_WaitThread(timer_thread, NULL);
//	SDL_DestroyMutex(regmap_mutex);

// 	fprintf(logfp, "PC:%lx VBR:%lx spd:%ld", PC, VBR, instrucciones/(time(NULL) - start_time));

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
