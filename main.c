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
#include "gui.h"
#include "console.h"
#include "controller.h"
#include "file.h"
#include "medium.h"
#include "config.h"

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
unsigned long instrucciones = 0;
bool logging = true;
int filelogging = 0;
bool logmem = false;
bool logvideomem = false;
bool logmemreg = false;
short ultopcnt = 0;
struct opcode_log_str ultop[OPMAXCNT];
char lastop[128];
bool pausa = false;


void timer_check();

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
//	logmsg("VBlankCallback: %d\n", pvr_scanline);
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

#define TMU_INT

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

void main_loop(void)
{
	SDL_Event event;
	int cnt = 0;
//	WORD instr;
//	DWORD valor;
//	int timer_cnt = 0;

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

//			instr = *(WORD *) str_PC;

			ejecutar_instruccion(*(WORD *) get_memory_pointer(PC));

//			(*PC_func) ();

//			dma_check();

			// de acuerdo a KOS 1.3, el timer recorre (50000000 / 64) ticks/segundo.
			// por lo que en un segundo tenemos 781250 ticks.
			// cada 1 ms -> 781,25 ticks.
			if (instrucciones % 50 == 0)
				timer_check();

			check_ints();

/*			if (PC == BreakPoint)
				DebugMode = DBG_STOP; */
				
			if (DebugMode == DBG_STEP)
			{
				DebugMode = DBG_STOP;
				RedibujarPantalla();
			}
   	
			instrucciones++;

			if (cnt % (500000 / 0x1FF) == 0)
			{
				pvr_scanline++;
				
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
			}

			if ((++cnt) == 500000)
			{
   				cnt = 0;
   				pvr_scanline = 0;
				break; // salimos de este ciclo y vamos al siguiente

			}
		}

		logxmsg(LOG_PVR, "llamando VBLINT\n");
		intc_add(ASIC_EVT_PVR_VBLINT, 0);
//		intc_check(ASIC_EVT_PVR_VBLINT);
		RedibujarPantalla();
		if (control->handle(event))
			{
				fprintf(stderr,"Quitting main loop\n");
				return;
			}
	}

	logmsg("saliendo de main_loop\n");
}

PC_f * PC_func;

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


void closeALL()
{
	fprintf(logfp, "PC:%lx VBR:%lx spd:%ld", PC, VBR, instrucciones/(time(NULL) - start_time));

	SDL_Quit( );

	fclose(logfp);
	fclose(serialfp);
	
	free(memoria);
	free(video_mem);
	free(regmem);

#ifdef TTF
	TTF_CloseFont(font);
#endif
	
	hook->close();
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

	if (dcemu_init(argc,argv))
	{
		return -1;
	}

	inicializar_logs();

    /* initialize SDL */
#ifdef _DEBUG
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE
#else
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER
//	if ( SDL_Init( SDL_INIT_VIDEO
#endif
		) < 0 )
	{
		fprintf( stderr, "Video initialization failed: %s\n",
			SDL_GetError( ) );
		SDL_Quit( );
	}

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

	if (LoadConfig())
	{
		closeALL();
		return -1;
	}

	if(loadSys())
	{
		closeALL();
		return -1;	
	}

//  	PC = mem_base + ip_bs1_offset; // ip_bs1_offset; // + mem_offset;
 	PC = 0x8c010000;
//	PC = 0x00000000;
//	PC = 0x8c0000e0;

//	str_PC = get_memory_pointer(PC);
	
	if (DebugInit())
 	{
		fprintf(stderr, "No se pudo crear pantallas para debug\r\n");
		closeALL();
		return 1;
	}

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

	if(medium_run())
	{
		fprintf(stderr,"Couldn't start emulation");
		closeALL();
		return -1;
	};

	control->init(); // init the controller subsystem


	logmsg("llamando a main_loop\n");

#if defined(DEBUG_MEM_READ) || defined(DEBUG_MEM_WRITE)
	filelogging |= FILELOG_MEMREADS | FILELOG_MEMWRITES;
#endif
	
	main_loop();

//	SDL_RemoveTimer(timer_id);
//	SDL_RemoveTimer(vblank_id);
	
//	timer_running = 0;
//	SDL_WaitThread(timer_thread, NULL);
//	SDL_DestroyMutex(regmap_mutex);
	
	closeALL();

	return 0;
}
