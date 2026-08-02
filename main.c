// #define TTF

// Las bibliotecas las enlaza el sistema de compilacion (CMake / makefiles),
// no #pragma comment(lib, ...).

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
#include "excepciones.h"
#include "intc.h"
#include "debug.h"
#include "graficos.h"
#include "iso.h"
#include "gui.h"
#include "sh4emu.h"
#include "gdrom.h"
#include "opciones.h"
#include "sistema.h"
#include "traza.h"
#include "ubc.h"
#include "wdt.h"
#include "tmu.h"
#include "aica.h"
#include "arm7.h"
#include "audio.h"
#include "mando.h"
#include "scramble.h"
#include "SIMDx86/version.h"

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
/* Estado del **teclado**. El del gamepad va aparte y los dos se combinan en
   entrada_leer(): asi las teclas siguen funcionando con un mando enchufado. */
WORD joystick = 0xFFFF;
unsigned char ltrig = TRIGGER_OFF, rtrig = TRIGGER_OFF;
unsigned char joyx = JOYSTICK_NEUTRAL, joyy = JOYSTICK_NEUTRAL;
SDL_Joystick * js;

/* El gamepad del anfitrion, releido una vez por cuadro. */
static struct mando_estado_t mando;

/*
	Lo que ve el Maple: teclado y mando juntos.

	Los botones se combinan con AND porque son activos en bajo -- pulsado en
	cualquiera de los dos es pulsado --, y en los ejes gana el que no este en
	reposo, con prioridad para el mando: si el stick esta movido manda el
	stick, y si no, las teclas.
*/
void entrada_leer(WORD * botones, BYTE * lt, BYTE * rt, BYTE * jx, BYTE * jy)
{
	*botones = (WORD) (joystick & mando.botones);

	/* DCEMU_PULSAR_START=N: apretar Start durante 20 sondeos a partir del
	   sondeo N (~60 por segundo emulado). Repetible con N2 separado por coma
	   para una segunda pulsacion. Existe porque inyectar teclado desde afuera
	   depende del foco de la ventana y Windows lo niega cuando otra ventana
	   lo retiene: esto navega los menus de un juego de forma determinista,
	   con la misma filosofia que DCEMU_PULSAR_A y --salir-tras. */
	{
		const char * e = getenv("DCEMU_PULSAR_START");

		if (e != NULL)
		{
			static int t = 0;
			int n1 = atoi(e), n2 = 0;
			const char * coma = strchr(e, ',');

			if (coma != NULL)
				n2 = atoi(coma + 1);

			t++;

			if ((t >= n1 && t < n1 + 20) || (n2 > 0 && t >= n2 && t < n2 + 20))
			{
				if (traza_activa && (t == n1 || t == n2))
					fprintf(stderr, "traza: pulsando Start en el sondeo %d, "
						"a los %lu ms de tiempo emulado.\n",
						t, (unsigned long) reloj_ms());

				REMOVE_BIT(*botones, CONT_START);
			}
		}
	}

	/* EXPERIMENTO: pasar el selector de fecha del boot ROM sin nadie delante.
	   Son cinco movimientos a la derecha --mes, dia, ano, hora, minuto-- para
	   llegar a "Select", y ahi el boton A. La secuencia se repite por si la
	   primera vuelta cae antes de que la pantalla este puesta. */
	if (getenv("DCEMU_PULSAR_A"))
	{
		static int t = 0;

		t++;

		if (getenv("DCEMU_SOLO_A"))
		{
			/* Ya en el menu: solo el boton, con el cursor donde este.
			   Con un numero mayor que 1 se pulsa UNA sola vez, en ese
			   sondeo: repetir el boton mientras el ROM ya esta arrancando
			   el juego lo cancela, y entonces no se distingue "aborto" de
			   "lo cancele yo". */
			int una = atoi(getenv("DCEMU_SOLO_A"));

			if (una > 1)
			{
				if (t >= una && t < una + 20)
				{
					if (traza_activa && t == una)
						fprintf(stderr, "traza: pulsando A en el sondeo %d, "
							"a los %lu ms de tiempo emulado.\n",
							t, (unsigned long) reloj_ms());

					REMOVE_BIT(*botones, CONT_A);
				}
			}
			else
			if ((t % 200) < 20)
				REMOVE_BIT(*botones, CONT_A);
		}
		else
		if (t < 900)
		{
			/* La fecha: cinco a la derecha hasta "Select" y ahi el boton. */
			if (t >= 300 && t < 500)
			{
				if (((t - 300) % 40) < 15)
					REMOVE_BIT(*botones, CONT_DPAD_RIGHT);
			}
			else
			if (t >= 500 && t < 560)
			{
				if (((t - 500) % 30) < 15)
					REMOVE_BIT(*botones, CONT_A);
			}
		}
		else
		/* Ya en el menu, con el cursor en "Play": solo el boton. */
		if ((t % 200) < 20)
			REMOVE_BIT(*botones, CONT_A);
	}

	*lt = (mando.ltrig != TRIGGER_OFF) ? mando.ltrig : ltrig;
	*rt = (mando.rtrig != TRIGGER_OFF) ? mando.rtrig : rtrig;

	*jx = (mando.joyx != JOYSTICK_NEUTRAL) ? mando.joyx : joyx;
	*jy = (mando.joyy != JOYSTICK_NEUTRAL) ? mando.joyy : joyy;
}
bool gui_visible=true;


/* void query_cache(WORD arg)
{
	(opcodes[oplist[arg]].funcion) (arg);
} */

#define TMU_INT

void timer_check(DWORD ciclos);

/*
	El cuerpo vive en tmu.c: es logica pura sobre registros y asi se puede
	probar sin SDL.

	Lo que habia antes bajaba cada TCNT de a uno por llamada e ignoraba el
	campo TPSC de TCR, o sea que el ritmo era una constante. Ver
	docs/clock-plan.md, fase 1.

	La entrega de las interrupciones NO se hace aca: tmu_tick() deja UNF puesto
	en el TCR del canal y intc_revisar_sh4() la entrega cuando SR lo permite.
	Hacerlo en el momento del subdesborde perdia el evento si BL estaba puesto.
*/
void timer_check(DWORD ciclos)
{
	tmu_tick(ciclos);
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
		capturar_gl_framebuffer();
		SDL_GL_SwapBuffers();
		fps_marcar_cuadro();
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

/*
	DMA del SH-4. Hasta ahora dma_check() solo escribia al log y ni siquiera se
	llamaba: las transferencias no se hacian. Ver el punto 8 de docs/bios-boot.md.

	Campos de CHCR que importan aca:

	  bit    0  DE   canal activado
	  bit    1  TE   transferencia terminada
	  bit    2  IE   interrumpir al terminar
	  bits 6-4  TS   tamano de cada unidad
	  bits 11-8 RS   quien pide la transferencia
	  bits 13-12 SM  como avanza la direccion de origen
	  bits 15-14 DM  como avanza la de destino
*/
/* CHCR_TE y CHCR_IE viven en sh4emu.h: los comparte intc_revisar_sh4(). */
#define CHCR_TS(chcr)	(((chcr) >> 4) & 0x7)
#define CHCR_RS(chcr)	(((chcr) >> 8) & 0xF)
#define CHCR_SM(chcr)	(((chcr) >> 12) & 0x3)
#define CHCR_DM(chcr)	(((chcr) >> 14) & 0x3)

#define CHCR_RS_AUTO	0x4		// auto-request: la transferencia sale sola

static int dma_unidad(DWORD chcr)
{
	switch (CHCR_TS(chcr))
	{
		case 0:		return 8;	// quadword
		case 1:		return 1;
		case 2:		return 2;
		case 3:		return 4;
		case 4:		return 32;	// bloque de cache
		default:	return 4;
	}
}

// 01 incrementa, 10 decrementa, el resto deja la direccion fija.
static long dma_paso(DWORD modo, int unidad)
{
	switch (modo)
	{
		case 1:		return  unidad;
		case 2:		return -unidad;
		default:	return 0;
	}
}

static void dma_canal(int n, DWORD * sar, DWORD * dar, DWORD * dmatcr, DWORD * chcr)
{
	int		unidad;
	long	paso_o, paso_d;
	DWORD	origen, destino, cuenta;
	BYTE	buf[32];

	if (!(*chcr & DE) || (*chcr & CHCR_TE))
		return;

	if (CHCR_RS(*chcr) != CHCR_RS_AUTO)
	{
		// La pide un periferico. En la Dreamcast esas transferencias las hace
		// el ASIC (PVR, GD-ROM, Maple, G2), no el DMAC del SH-4.
		logxmsg(LOG_MEM, "DMA: canal %d con RS=%x, no es auto-request\n", n, CHCR_RS(*chcr));
		return;
	}

	unidad  = dma_unidad(*chcr);
	paso_o  = dma_paso(CHCR_SM(*chcr), unidad);
	paso_d  = dma_paso(CHCR_DM(*chcr), unidad);
	origen  = *sar;
	destino = *dar;
	cuenta  = *dmatcr ? *dmatcr : 0x1000000;	// 0 significa 16M unidades

	logxmsg(LOG_MEM, "DMA: canal %d, %x -> %x, %d x %d bytes\n",
		n, origen, destino, cuenta, unidad);

	while (cuenta--)
	{
		// El DMAC trabaja con direcciones fisicas: SAR y DAR los programa el
		// guest ya resueltos. Ademas esto corre entre instrucciones, donde no
		// hay salto armado al que abortar.
		memread_fisico(origen, buf, unidad);
		memwrite_fisico(destino, buf, unidad);

		origen  += paso_o;
		destino += paso_d;
	}

	*sar    = origen;
	*dar    = destino;
	*dmatcr = 0;

	/* El fin de transferencia deja TE puesto y no toca DE: asi lo describe el
	   manual, y TE en 1 ya impide reejecutar el canal. Con IE, la peticion de
	   DMTE la deriva intc_revisar_sh4() de estas mismas banderas -- aca no se
	   entrega nada, igual que el TMU y el WDT. */
	*chcr  |= CHCR_TE;

	if (traza_activa)
		fprintf(stderr, "traza: DMAC canal %d: transferencia hecha, "
			"CHCR=%08lx (IE=%d)\n", n, (unsigned long) *chcr,
			(*chcr & CHCR_IE) ? 1 : 0);
}

void dma_check()
{
	if (!(*DMAOR & DME))
		return;

	dma_canal(0, SAR0, DAR0, DMATCR0, CHCR0);
	dma_canal(1, SAR1, DAR1, DMATCR1, CHCR1);
	dma_canal(2, SAR2, DAR2, DMATCR2, CHCR2);
	dma_canal(3, SAR3, DAR3, DMATCR3, CHCR3);
}

/* La instantanea para reejecutar una instruccion que aborta vive en
   excepciones.c: no alcanza con core.context, porque los dos bancos de
   registros de punto flotante estan fuera de el. */

/* Marca del contador monotono para el barrido de pantalla. Fuera del contexto:
   restaurar la instantanea de la MMU no debe hacer retroceder el reloj. */
static unsigned long long marca_linea = 0;

/* Tiempo real (SDL_GetTicks) al entrar a main_loop, para --limitar. */
static unsigned long real_inicio = 0;

void main_loop(void)
{
	SDL_Event event;
//	int cnt=0;
//	DWORD valor;
//	int timer_cnt = 0;

	// Referencia de tiempo real para --limitar. Se toma aca y no al arrancar el
	// programa para no contar la carga de la BIOS y de la imagen.
	real_inicio = SDL_GetTicks();

	timer_check(0); // arranca sin ciclos transcurridos: solo fija el TSTR previo

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

			if (traza_activa)
				traza_paso(PC);

			/* La frontera del UBC: entrega el break pendiente de la
			   instruccion (o el operando) anterior, o evalua un break de
			   instruccion sobre este PC. Si entro a la excepcion, PC ya es el
			   manejador y no hay nada que ejecutar en esta vuelta. */
			if (ubc_activa && ubc_revisar_instruccion())
				continue;

			if (!excepcion_vigilar)
			{
				// Camino rapido: sin MMU, sin SR.FD y sin bits de Enable en
				// FPSCR no hay falta que abortar, asi que no se saca
				// instantanea ni se arma el salto. Es todo lo que corre hoy y
				// tiene que costar exactamente lo de antes.
				core.execute(*(WORD *) get_memory_pointer(PC));
			}
			else if (setjmp(excepcion_salto) == 0)
			{
				// Las excepciones generales del SH-4 son reejecutables: el
				// manejador arregla lo que haga falta, hace RTE y la
				// instruccion se repite entera. Pero los handlers de dcemu
				// mutan registros alrededor del acceso (MOV.L @Rn+,
				// MOV.L Rm,@-Rn, FMUL sobre su propio destino), asi que hay
				// que poder deshacerlo. Ver docs/mmu-plan.md, fase 5.
				excepcion_instantanea_tomar();

				excepcion_salto_armado = 1;
				core.execute(*(WORD *) get_memory_pointer(PC));
				excepcion_salto_armado = 0;
			}
			else
			{
				// Volvimos de una falta. Restaurar deja PC en la instruccion
				// que fallo -- o en el salto, si la falta fue en su ranura de
				// retardo, porque el longjmp desenrolla los dos niveles.
				excepcion_salto_armado = 0;
				en_ranura_retardo = 0;
				excepcion_instantanea_restaurar();

				// Lo poco que no se restaura: la excepcion de operacion de FPU
				// deja Cause escrito, y Flag no.
				excepcion_reponer();

				excepcion_entrar(excepcion_codigo, excepcion_vector);
			}

//			(*PC_func) ();

			// de acuerdo a KOS 1.3, el timer recorre (50000000 / 64) ticks/segundo.
			// por lo que en un segundo tenemos 781250 ticks.
			// cada 1 ms -> 781,25 ticks.
			if (core.context.cycles >= 50)
			{
				// Consumir el acumulado entero y pasarlo al contador monotono.
				// Los consumidores periodicos comparan contra su propia marca:
				// ya no hay acumuladores que sumen y resten cantidades
				// distintas. Ver docs/clock-plan.md, fase 2.
				DWORD ciclos = core.context.cycles;

				core.context.cycles -= ciclos;
				reloj_total += ciclos;

				// Los dos temporizadores reciben la cantidad de ciclos y llevan
				// su propio resto, cada uno con su divisor. Ninguno entrega su
				// interrupcion: solo dejan su bandera puesta.
				timer_check(ciclos);
				wdt_tick(ciclos);

				// El AICA no recibe ciclos: compara contra su propia marca de
				// reloj_total, porque su reloj es otro -- 44100 Hz de muestreo
				// y 22,5792 MHz de bloque de audio. Ver aica.h.
				aica_tick();

				// Y aca se entrega la que corresponda, si SR lo permite. Si no
				// se puede, la bandera sigue puesta y se reintenta en la vuelta
				// siguiente, que es lo que hace el chip.
				intc_revisar_sh4();

				// El ASIC va en el mismo compas. Con la peticion por nivel la
				// compuerta es verdadera mientras haya un bit sin acusar, o
				// sea casi siempre: por instruccion costaba 9 veces mas en
				// Crazy Taxi. Cada 50 ciclos la latencia queda a la par de la
				// de los perifericos del SH-4, y el tic de las demoras pasa a
				// valer lo que dice (50 ciclos por vuelta).
				if (intc_asic_pendiente())
					check_ints();

				dma_check();
			}

/*			if (PC == BreakPoint)
				DebugMode = DBG_STOP; */
				
			if (DebugMode == DBG_STEP)
			{
				DebugMode = DBG_STOP;
				RedibujarPantalla();
			}
   	
// 			core.context.cycles++;

//			if (cnt % (500000 / 0x1FF) == 0)
			// Antes era 978 fijo, una constante empirica que hacia los frames
			// 6,5 veces mas rapidos. Ahora sale de SPG_LOAD y SPG_CONTROL:
			// DC_CPU_HZ / (lineas * campos por segundo). Ver
			// docs/clock-plan.md, fase 3.
			//
			// Y la cuenta va contra una marca del contador monotono, no contra
			// un acumulador que se suma y se resta (fase 2).
			if (reloj_total - marca_linea >= pvr_ciclos_linea)
			{
				pvr_scanline++;

				marca_linea += pvr_ciclos_linea;

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
				if (pvr_scanline >= pvr_spg_load_vcount) // valor m�ximo que puede tomar
				{
	   				pvr_scanline = 0;
//	   				cnt = 0;
					break; // salimos de este ciclo y vamos al siguiente
				}
			}

		}

		logxmsg(LOG_PVR, "llamando VBLINT\n");
		intc_add(ASIC_EVT_PVR_VBLINT, 0);

		// XInput no manda eventos: hay que preguntarle. Una vez por cuadro es
		// mas seguido de lo que el guest sondea el Maple, asi que alcanza.
		mando_leer(&mando);

		// Salida automatica por tiempo emulado. Va aca, en el fin de frame, para
		// que salga por el mismo camino que SDL_QUIT y traza_resumen() alcance a
		// correr: matar el proceso desde afuera se lleva por delante el
		// desensamblado y el volcado, que es justo lo que se fue a buscar.
		//
		// Es tiempo emulado y no real a proposito: asi dos corridas se detienen
		// en el mismo punto del arranque aunque la maquina este mas cargada.
		if (opciones.salir_tras > 0 &&
			reloj_ms() >= (unsigned long long) opciones.salir_tras * 1000)
		{
			fprintf(stderr, "salida automatica a los %d s de tiempo emulado.\n",
				opciones.salir_tras);
			return;
		}

		// Fin de frame: el unico sitio donde tiene sentido frenar. Con --limitar,
		// si el tiempo emulado se adelanto al real se duerme la diferencia. Solo
		// frena: donde dcemu ya es mas lento que una consola no hace nada. Ver
		// docs/clock-plan.md, fase 4.
		if (opciones.limitar)
		{
			unsigned long long emulado = reloj_ms();
			unsigned long      real    = SDL_GetTicks() - real_inicio;

			if (emulado > real)
			{
				unsigned long sobra = (unsigned long) (emulado - real);

				// Techo por si la cuenta se desmadra: mejor ir rapido que
				// congelar el emulador esperando.
				if (sobra > 100)
					sobra = 100;

				SDL_Delay(sobra);
			}
		}
//		intc_check(ASIC_EVT_PVR_VBLINT);
		RedibujarPantalla();

		// El sonido producido durante el cuadro, al .wav si hay volcado. La
		// reproduccion no pasa por aqui: de eso se encarga la callback de SDL,
		// en su propio hilo. Ver audio.h.
		audio_volcar();

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

					case SDLK_f: // contador de FPS en el titulo
					fps_visible = !fps_visible;
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

                    case SDLK_F5:
					volcar_framebuffer("captura.bmp");
					// Con la traza puesta, F5 sirve ademas para preguntar
					// "donde esta el PC ahora mismo".
					traza_volcar("a pedido (F5)");
					traza_rangos();
					break;

					// F6: lo que GL rasterizo. F5 vuelca la RAM de video, que
					// en una demo 3D esta vacia -- el render no pasa por ahi.
                    case SDLK_F6:
					volcar_gl("captura-gl.bmp");
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

	for (c = fgetc(fp); c != EOF && !feof(fp) && idx < BIOS_SIZE; c = fgetc(fp))
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
		nibble = 0;	// si no, el primer byte de la primera letra sale con basura

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
 	long tam; // , i, j;
//	short c;
	WORD wvalor;
	DWORD dwvalor;
//	SDL_TimerID vblank_id;
//	SDL_Thread * timer_thread;

	//FILE * fp;

	/* Antes que nada: si algo tumba al emulador, que al menos diga por donde
	   iba el guest en vez de desaparecer en silencio. Ver traza.h. */
	traza_caida_instalar();

	{
		int r = opciones_parsear(argc, argv);

		if (r)
			return (r < 0) ? 0 : 1;	// -1 es --ayuda, que no es un error
	}

	traza_activa = opciones.traza_mem;

	watchpoint_dir = opciones.watchpoint;
	watchpoint_tam = (size_t) opciones.watchpoint_tam;

	watchpoint_lectura_dir = opciones.watchpoint_lect;
	watchpoint_lectura_tam = (size_t) opciones.watchpoint_lect_tam;

	traza_desde_pc    = opciones.traza_desde;
	traza_desde_n     = (long) opciones.traza_desde_n;
	traza_desde_salto = (long) opciones.traza_desde_salto;

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

	/*
		El gamepad va por XInput y no por SDL: el camino de SDL de arriba es de
		2005, esta detras de un #ifdef que nadie define, y mapea los botones por
		indice, que con un mando moderno no significa nada. Ver mando.c.
	*/
	mando_reposo(&mando);

	if (mando_iniciar())
	{
		struct mando_estado_t prueba;

		fprintf(stderr, "mando: %s\n", mando_leer(&prueba)
			? "gamepad conectado" : "sin gamepad conectado, solo teclado");
	}

//	screen = SDL_SetVideoMode(320, 240, 16, SDL_DOUBLEBUF);

	/* Antes de screeninit(), que es quien pone el caption. Con --bios lo que se
	   ejecuta es el boot ROM y la imagen es solo lo que ve la lectora. */
	titulo_poner(opciones.imagen ? opciones.imagen : "1st_read.bin");

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

	// La tabla de despacho del ARM del AICA, expandida igual que la del SH-4.
	arm7_init();
	arm7_reset();

	// La salida de sonido: la tarjeta y/o el .wav de --captura-audio.
	audio_iniciar();
///*	
	logmsg("cargando bios (bios.bin)\n");
    if (cargar_bios())
		return 1;
//*/

	// La flash de 128 KB: region, idioma, fecha y nombre de la consola. Si el
	// archivo no esta se sintetiza una minima en vez de fallar.
	logmsg("cargando flash (flash.bin)\n");
	if (sistema_flash_iniciar("bios/flash.bin"))
		return 1;

	// Y la hora que el guest haya puesto en corridas anteriores.
	sistema_rtc_cargar();

	// Arranque por el boot ROM: no se carga nada a mano, la imagen (si la hay)
	// se monta para que la vea la lectora. Ver la fase 1.1 del plan.
	if (opciones.arranque_bios)
	{
		if (opciones.imagen != NULL)
		{
			fprintf(stderr, "montando %s en la lectora.\n", opciones.imagen);

			if (iso_init((char *) opciones.imagen))
			{
				// Sin imagen valida se sigue igual: con la bandeja vacia la
				// BIOS tiene que llegar a su pantalla de "sin disco".
				fprintf(stderr, "No se pudo montar la imagen; se arranca sin disco.\n");
				iso_init(NULL);
			}
		}
		else
		{
			fprintf(stderr, "arranque por BIOS sin imagen: bandeja vacia.\n");
			iso_init(NULL);
		}
	}
	else
	{

	// determinemos qu� vamos a cargar
	char * ejecutable = opciones.imagen ? (char *) opciones.imagen : "1st_read.bin";

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

		/*
			El 1ST_READ.BIN de un disco va **cifrado** -- el cifrado lo pone el
			mastering -- y el mismo binario suelto en una carpeta normalmente
			no. Las dos cosas se pasan aca como un .bin y no hay nada en el
			nombre que las distinga, asi que se mira el prologo de entrada; ver
			parece_cifrado() en scramble.c. --cifrado / --sin-cifrado lo fuerzan.

			Y se dice lo que se decidio: adivinar en silencio es lo que hace que
			un arranque roto parezca un bug del emulador. La carpeta de mame4all
			--ip.bin y un 1st_read.bin cifrado, sin imagen-- salia ejecutando
			ceros en 0x00006b03 porque el ejecutable llegaba revuelto.
		*/
		{
			unsigned char * bin = get_memory_pointer(mem_base + mem_offset);
			int				cifrado = (opciones.cifrado >= 0)
				? opciones.cifrado
				: parece_cifrado(bin, (unsigned long) tam);

			if (cifrado)
			{
				fprintf(stderr, "%s viene cifrado: descifrando %ld bytes.\n",
					ejecutable, tam);

				descramble_memoria(bin, (unsigned long) tam);
			}
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
		
		fprintf(stderr, "leidos %ld bytes\n", tam);
	}

	/*
		El boot ROM deja el codigo de maquina de la flash en REGION_BASE antes
		de entregarle la consola al juego, y hay juegos que lo miran. Sin
		--bios nadie lo escribia y ahi quedaba lo que hubiera.

		Crazy Taxi es el caso que lo destapo: compara ese word contra 0x3030
		--los dos primeros digitos-- y, si coincide, da la maquina por conocida;
		si no, se va a preguntarle a un dispositivo del bus G1 externo que en
		una consola de serie no existe, y se queda esperandolo para siempre.
	*/
	memcpy(get_memory_pointer(REGION_BASE), &flash_mem[FLASH_PART0_OFF], 5);
	((unsigned char *) get_memory_pointer(REGION_BASE))[5] = '\0';

	/*
		Al lado va el identificador binario de la maquina: los 8 bytes que en la
		flash siguen al bloque ASCII de la particion 0. Es lo que en KOS se
		conoce como el "system ID" de la consola.

		Medido, no deducido: se arranco con --bios y el 1.01d y se volco
		0x8C000000-0x8C0000FF con la BIOS ya en el menu. Los 8 bytes de
		SYSID_BASE salen identicos a flash+0x56, y el codigo de region de
		REGION_BASE identico a flash+0x00, o sea que el ROM copia los dos del
		mismo sitio. Por eso este va derivado de la flash y no como constante:
		sigue a la flash que se este usando, igual que el de al lado.

		Lo que ese mismo volcado dejo medido y **no** se reproduce, por no
		saber que significa:

		  0x8C000060  0x00C0C0C0, estable entre corridas, no sale de la flash
		  0x8C000064  cambia de una corrida a otra: un contador o un reloj
		  0x8C000078  8 bytes copiados del ultimo registro de 16 de la
		              particion 2 de la flash (los ajustes del sistema). Se
		              sabe de donde salen; falta el formato de esa particion.

		Ver docs/pendientes-plan.md, C.3.
	*/
	memcpy(get_memory_pointer(SYSID_BASE), &flash_mem[FLASH_PART0_OFF + 0x56], 8);

	/*
		Y en EJECUTABLE_BASE deja la direccion donde cargo el ejecutable. El
		bootstrap del IP.BIN la lee de ahi en vez de llevarla como constante, y
		sin ella se lleva un cero y salta a la nada: el guest terminaba
		ejecutando la zona de vectores --que aqui son ceros-- y de ahi seguia de
		dos en dos hasta tumbar al emulador. Es el mismo agujero que
		REGION_BASE, en el bloque de al lado.

		El resto de ese bloque (0x8C0000E4 a 0x8C0000F4) tambien lleva campos
		que el ROM rellena y aqui siguen en cero. Ponerlos hace que el juego
		llegue algo mas lejos, pero no se sabe que son: quedan fuera hasta
		saberlo. 0x8C0000E0 no se toca -- ahi va un vector de syscall que los
		hooks ya instalaron.
	*/
	{
		DWORD ejecutable = mem_base + mem_offset;

		memcpy(get_memory_pointer(EJECUTABLE_BASE), &ejecutable, sizeof(DWORD));
	}

	} // fin del camino sin --bios

	// La lectora ya puede saber si hay disco.
	gdrom_iniciar(opciones.bandeja);

	// we start the cpu
	// allocating the current cpu
	initCpuSubSystem();

	// Con --bios se parte en el vector de reset y el boot ROM hace todo el
	// trabajo; si no, en el bootstrap de IP.BIN, que es el camino de siempre.
	PC = opciones.arranque_bios ? 0xA0000000 : (mem_base + ip_bs1_offset);
 //	PC = 0x8c010000;
//	PC = 0x8c000000;
//	PC = 0x00000000;
// 	PC = 0x8c0000e0;
//	PC = 0x8C008300;

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
	// Los hooks de syscall: siguen siendo la forma de correr un .bin suelto
	// sin IP.BIN valido, pero ahora son opcionales. Con --bios estorban, asi
	// que opciones_parsear() los apaga solo. Ver la fase 4 del plan.
	if (opciones.hacks_bios)
	{
	// HACK!
 	dwvalor = HACK_BASE + HACK_ROMFONT;	memwrite(SYSCALL_ROMFONT, &dwvalor, sizeof(DWORD));
	dwvalor = HACK_BASE + HACK_GDROM;	memwrite(SYSCALL_GDROM, &dwvalor, sizeof(DWORD));
 	dwvalor = HACK_BASE + HACK_SYSINFO; memwrite(SYSCALL_SYSINFO, &dwvalor, sizeof(DWORD));
 	dwvalor = HACK_BASE + HACK_FLASHROM; memwrite(SYSCALL_FLASHROM, &dwvalor, sizeof(DWORD));
 	dwvalor = HACK_BASE + HACK_UNKNOWN; memwrite(SYSCALL_UNKNOWN, &dwvalor, sizeof(DWORD));

	// Igual que el del GD-ROM y el de la flash: RTS y el opcode ilegal en la
	// ranura de retardo, que dcopcodes.c despacha a hack_romfont(). Antes era
	// RTS + MOV.L @(0,PC),R0 con la direccion de la fuente como literal en
	// HACK_BASE + 4, o sea que las tres funciones del syscall respondian lo
	// mismo. Ver hack_romfont() por que eso colgaba a lock_bfont() de KOS.
	wvalor = 0x000B; /* RTS */			memwrite(HACK_BASE + HACK_ROMFONT + 0, &wvalor, 2);
	wvalor = 0xFFFF; /* BIOS_HACK */	memwrite(HACK_BASE + HACK_ROMFONT + 2, &wvalor, 2);

	wvalor = 0x000B; /* RTS */			memwrite(HACK_BASE + HACK_GDROM, &wvalor, 2);
	wvalor = 0xFFFF; /* BIOS_HACK*/		memwrite(HACK_BASE + HACK_GDROM + 2, &wvalor, 2);

	// SYSINFO y UNKNOWN siguen sin hacer nada, pero ahora lo dicen: mismo par
	// RTS + opcode ilegal que los otros tres, despachado a hack_mudo(). Eran
	// RTS + NOP, o sea que volvian en silencio y un juego que dependiera de
	// ellos se colgaba sin dejar rastro de por que.
	wvalor = 0x000B; /* RTS */			memwrite(HACK_BASE + HACK_SYSINFO, &wvalor, 2);
	wvalor = 0xFFFF; /* BIOS_HACK */	memwrite(HACK_BASE + HACK_SYSINFO + 2, &wvalor, 2);

	// Igual que el del GD-ROM: RTS y el opcode ilegal en la ranura de retardo,
	// que dcopcodes.c despacha a hack_flashrom(). Antes era RTS + NOP, o sea
	// que el syscall volvia sin hacer nada y flashrom_get_region() de KOS
	// reportaba "can't find partition 0".
	wvalor = 0x000B; /* RTS */			memwrite(HACK_BASE + HACK_FLASHROM, &wvalor, 2);
	wvalor = 0xFFFF; /* BIOS_HACK */	memwrite(HACK_BASE + HACK_FLASHROM + 2, &wvalor, 2);

	wvalor = 0x000B; /* RTS */			memwrite(HACK_BASE + HACK_UNKNOWN, &wvalor, 2);
	wvalor = 0xFFFF; /* BIOS_HACK */	memwrite(HACK_BASE + HACK_UNKNOWN + 2, &wvalor, 2);
	}
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

	/* Lo que el guest escribio en la flash -- la fecha, el idioma, los ajustes
	   de juegos -- vuelve al archivo. Sin esto la BIOS pide la hora en cada
	   arranque, porque nunca consigue guardar que ya esta configurada. */
	sistema_flash_guardar();
	sistema_rtc_guardar();
	mando_terminar();

	traza_resumen();

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
