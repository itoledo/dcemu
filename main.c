// Las bibliotecas las enlaza el sistema de compilacion (CMake / makefiles),
// no #pragma comment(lib, ...).

#include "main.h"
#include "math.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "BFont.h"
#include <time.h>
#include <ctype.h>
// #include <unistd.h>
// #include "SDL_gfxPrimitives.h"
// #include "SDL_console.h"
#include "branch.h"
#include "opcodes.h"
#include "mem.h"
#include "mmu.h"
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
#include "vmu.h"
#include "traza.h"
#include "perf.h"
#ifdef DCEMU_BLOQUES
#include "bloques.h"
#endif
#ifdef DCEMU_FUSION
#include "fusion.h"
#endif
#ifdef DCEMU_JIT
#include "jit.h"
#include "arm7jit.h"
#include "aicadspjit.h"
#endif
#include "hilo_aica.h"
#include "ubc.h"
#include "wdt.h"
#include "tmu.h"
#include "aica.h"
#include "arm7.h"
#include "audio.h"
#include "mando.h"
#include "scramble.h"
#include "SIMDx86/version.h"

DWORD G2_FIFO = 0;		// G2 FIFO
DWORD MAPLE_DMAADDR;
DWORD MAPLE_RESET2;
DWORD MAPLE_ENABLE;
DWORD MAPLE_STATE;
DWORD MAPLE_SPEED;
DWORD MAPLE_RESET1;

#define MAX_PARAMS 4

BFont_Info * font;
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

/* El gamepad del anfitrion, releido una vez por cuadro. */
static struct mando_estado_t mando;

/*
	Lo que ve el Maple: teclado y mando juntos.

	Los botones se combinan con AND porque son activos en bajo -- pulsado en
	cualquiera de los dos es pulsado --, y en los ejes gana el que no este en
	reposo, con prioridad para el mando: si el stick esta movido manda el
	stick, y si no, las teclas.
*/
/*
	Un boton apretado en los sondeos que diga una lista.

	`lista` son numeros de sondeo separados por comas, y cada uno vale por una
	pulsacion de 20 sondeos (~60 sondeos por segundo emulado). Devuelve 1 si el
	sondeo `t` cae dentro de alguna, y traza el primero de cada una.

	La lista, y no uno o dos numeros sueltos, porque entrar a una partida son
	varias confirmaciones seguidas --modo, jugador, cancha-- y despues hay que
	DEJAR de apretar: seguir mandando A durante el juego abre la pausa y se sale
	del partido. Apretar cada 200 sondeos llegaba a la partida y despues la
	abandonaba, que es como se perdieron dos corridas de siete minutos.
*/
static int pulsacion_en(const char * lista, int t, const char * nombre)
{
	const char *	p = lista;
	int				dentro = 0;

	while (*p != '\0')
	{
		int n = atoi(p);

		if (n > 0 && t >= n && t < n + 20)
		{
			if (traza_activa && t == n)
				fprintf(stderr, "traza: pulsando %s en el sondeo %d, "
					"a los %lu ms de tiempo emulado.\n",
					nombre, t, (unsigned long) reloj_ms());

			dentro = 1;
		}

		p = strchr(p, ',');

		if (p == NULL)
			break;

		p++;
	}

	return dentro;
}

void entrada_leer(WORD * botones, BYTE * lt, BYTE * rt, BYTE * jx, BYTE * jy)
{
	*botones = (WORD) (joystick & mando.botones);

	/* DCEMU_PULSAR_START=N[,N2,...]: apretar Start durante 20 sondeos a partir
	   de cada sondeo de la lista. Existe porque inyectar teclado desde afuera
	   depende del foco de la ventana y Windows lo niega cuando otra ventana
	   lo retiene: esto navega los menus de un juego de forma determinista,
	   con la misma filosofia que DCEMU_PULSAR_A y --salir-tras. */
	{
		const char * e = getenv("DCEMU_PULSAR_START");

		if (e != NULL)
		{
			static int t = 0;

			t++;

			if (pulsacion_en(e, t, "Start"))
				REMOVE_BIT(*botones, CONT_START);
		}
	}

	/* DCEMU_MANTENER_DERECHA=desde[:hasta] (y _ARRIBA, _ABAJO, _IZQUIERDA):
	   esa cruceta SOSTENIDA entre esos dos sondeos (decimal, ~60 por segundo
	   emulado; sin hasta, para siempre). Es la otra mitad de PULSAR_START:
	   aquello pulsa, esto mantiene. Existe para medir la auto-repeticion de un
	   menu sin nadie delante -- una direccion mantenida N segundos exactos,
	   identica en la corrida base y en la corregida, que es lo que un pulgar
	   no da. Se pueden combinar: la diagonal es lo que separo la guerra de
	   contadores de Virtua Tennis de un defecto de dcemu. */
	{
		static const struct {
			const char *	variable;
			WORD			bit;
			const char *	nombre;
		} mantenidas[4] = {
			{ "DCEMU_MANTENER_DERECHA",   CONT_DPAD_RIGHT, "derecha"   },
			{ "DCEMU_MANTENER_IZQUIERDA", CONT_DPAD_LEFT,  "izquierda" },
			{ "DCEMU_MANTENER_ARRIBA",    CONT_DPAD_UP,    "arriba"    },
			{ "DCEMU_MANTENER_ABAJO",     CONT_DPAD_DOWN,  "abajo"     },
		};
		static int t = 0;
		int i;

		t++;

		for (i = 0; i < 4; i++)
		{
			const char * e = getenv(mantenidas[i].variable);
			int desde, hasta;
			const char * dp;

			if (e == NULL)
				continue;

			desde = atoi(e);
			dp    = strchr(e, ':');
			hasta = dp ? atoi(dp + 1) : 0;

			if (t >= desde && (hasta == 0 || t < hasta))
			{
				if (traza_activa && t == desde)
					fprintf(stderr, "traza: manteniendo %s desde el sondeo"
						" %d, a los %lu ms de tiempo emulado.\n",
						mantenidas[i].nombre, t, (unsigned long) reloj_ms());

				REMOVE_BIT(*botones, mantenidas[i].bit);
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
			   Con una lista de sondeos (N[,N2,...], cualquiera mayor que 1)
			   se pulsa una vez en cada uno: repetir el boton mientras el ROM
			   ya esta arrancando el juego lo cancela, y entonces no se
			   distingue "aborto" de "lo cancele yo". El 1 solo, en cambio,
			   pulsa cada 200 sondeos para siempre. */
			const char * lista = getenv("DCEMU_SOLO_A");

			if (atoi(lista) > 1)
			{
				if (pulsacion_en(lista, t, "A"))
					REMOVE_BIT(*botones, CONT_A);
			}
			else
			if ((t % 200) < 20)
				REMOVE_BIT(*botones, CONT_A);
		}
		else
		if (t >= 300 && t < 4000)
		{
			/* La fecha: cinco a la derecha hasta "Select" y ahi el boton.

			   El ciclo **se repite** cada 600 sondeos, que es lo que el
			   comentario de arriba siempre dijo y el codigo no hacia: la
			   secuencia corria una sola vez entre los sondeos 300 y 560, o sea
			   entre los 5 y los 9 segundos emulados, y el panel de la fecha
			   aparece bastante despues de que el ROM termina su animacion. Con
			   una sola pasada las cinco flechas se gastan contra una pantalla
			   que todavia no existe, el ROM se queda pidiendo la fecha para
			   siempre y **ningun disco arranca** -- dos juegos distintos dan
			   escenas identicas, que es como se encontro. */
			int f = (t - 300) % 600;

			if (f < 200)
			{
				if ((f % 40) < 15)
					REMOVE_BIT(*botones, CONT_DPAD_RIGHT);
			}
			else
			if (f < 260)
			{
				if (((f - 200) % 30) < 15)
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

	/*
		DCEMU_GRABAR_MANDO=archivo / DCEMU_MANDO=archivo: la grabadora y el
		replay de la entrada, al nivel de lo que ve el Maple.

		La grabadora escribe una linea por CAMBIO de estado, con el numero de
		sondeo (el mismo reloj de PULSAR_START: ~60 por segundo emulado):

		    v1
		    <sondeo> <botones hex> <lt> <rt> <jx> <jy>

		y el replay la reproduce: desde ese sondeo rige ese estado, hasta el de
		la linea siguiente. Va AL FINAL de todas las mezclas a proposito, por
		las dos puntas: lo que se graba es exactamente lo que el guest vio
		--pulsaciones de PULSAR_START/SOLO_A incluidas, asi que una receta
		vieja se puede grabar una vez y reemitir identica--, y en replay el
		estado grabado REEMPLAZA todo, teclado, XInput y variables: el jitter
		analogico de un mando enchufado ya movio corridas (ver la disciplina de
		medicion) y colarse aqui arruinaria el determinismo que es todo el
		punto.

		El sondeo cuenta en tiempo emulado, o sea que grabar jugando con
		--limitar y reproducir a toda velocidad da la misma secuencia. La VMU
		sigue la regla de siempre: grabar y reproducir tienen que arrancar de
		la misma imagen de tarjeta.
	*/
	{
		/*
			Un paso puede fijarse por numero de sondeo (la forma original) o
			por MILISEGUNDO emulado, con el prefijo 't': "t45500 fffb 0 ...".
			El sondeo cuenta llamadas a entrada_leer, y su relacion con el
			tiempo NO es fija: cada recorrido del Maple es un sondeo, y el
			trafico de la VMU lo acelera -- una receta a ciegas escrita en
			sondeos se desincroniza con cualquier cambio de temporizacion del
			emulador. El milisegundo emulado es estable ante eso, que es lo
			que una receta de menus necesita.
		*/
		typedef struct {
			int		sondeo;		/* o ms emulado, si es_ms */
			WORD	botones;
			BYTE	lt, rt, jx, jy;
			BYTE	es_ms;
		} entrada_paso_t;

		static int				modo = -2;		/* -2 sin leer, 0 nada, 1 graba, 2 replay */
		static FILE *			grabar_fp = NULL;
		static WORD				ult_botones;
		static BYTE				ult_lt, ult_rt, ult_jx, ult_jy;
		static entrada_paso_t *	pasos = NULL;
		static int				pasos_n = 0, paso_actual = 0;
		static int				sondeo = 0;

		sondeo++;

		if (modo == -2)
		{
			const char *	r = getenv("DCEMU_MANDO");
			const char *	g = getenv("DCEMU_GRABAR_MANDO");

			modo = 0;

			if (r != NULL)
			{
				FILE *	fp = fopen(r, "r");
				char	linea[80];

				if (fp != NULL && fgets(linea, sizeof(linea), fp) != NULL
					&& strncmp(linea, "v1", 2) == 0)
				{
					int		cap = 0;
					int		s;
					unsigned b, plt, prt, pjx, pjy;
					int es_ms;
					int c;

					for (;;)
					{
						/* Prefijo opcional 't': el paso va en ms emulados. */
						do
							c = fgetc(fp);
						while (c == ' ' || c == '\n' || c == '\r');

						es_ms = (c == 't');

						if (!es_ms && c != EOF)
							ungetc(c, fp);

						if (fscanf(fp, "%d %x %u %u %u %u",
							&s, &b, &plt, &prt, &pjx, &pjy) != 6)
							break;

						if (pasos_n == cap)
						{
							cap = cap ? cap * 2 : 256;
							pasos = (entrada_paso_t *) realloc(pasos,
								(size_t) cap * sizeof(entrada_paso_t));
						}

						pasos[pasos_n].sondeo  = s;
						pasos[pasos_n].botones = (WORD) b;
						pasos[pasos_n].lt = (BYTE) plt;
						pasos[pasos_n].rt = (BYTE) prt;
						pasos[pasos_n].jx = (BYTE) pjx;
						pasos[pasos_n].jy = (BYTE) pjy;
						pasos[pasos_n].es_ms = (BYTE) es_ms;
						pasos_n++;
					}

					modo = 2;
					fprintf(stderr, "mando: replay de %s, %d pasos\n",
						r, pasos_n);
				}
				else
					fprintf(stderr, "mando: no pude leer %s; replay apagado\n",
						r);

				if (fp != NULL)
					fclose(fp);
			}
			else
			if (g != NULL)
			{
				grabar_fp = fopen(g, "w");

				if (grabar_fp != NULL)
				{
					fprintf(grabar_fp, "v1\n");
					modo = 1;
					fprintf(stderr, "mando: grabando la entrada en %s\n", g);
				}
			}
		}

		if (modo == 2)
		{
			unsigned long long ms = reloj_ms();

			#define PASO_VENCIDO(p) ((p).es_ms \
				? (unsigned long long) (p).sondeo <= ms \
				: (p).sondeo <= sondeo)

			while (paso_actual + 1 < pasos_n
				&& PASO_VENCIDO(pasos[paso_actual + 1]))
				paso_actual++;

			if (pasos_n > 0 && PASO_VENCIDO(pasos[0]))
			{
				*botones = pasos[paso_actual].botones;
				*lt = pasos[paso_actual].lt;
				*rt = pasos[paso_actual].rt;
				*jx = pasos[paso_actual].jx;
				*jy = pasos[paso_actual].jy;
			}
			else
			{
				/* Antes del primer paso grabado: todo suelto y en reposo. */
				*botones = 0xFFFF;
				*lt = *rt = 0;
				*jx = *jy = JOYSTICK_NEUTRAL;
			}
		}
		else
		if (modo == 1
			&& (sondeo == 1 || *botones != ult_botones || *lt != ult_lt
				|| *rt != ult_rt || *jx != ult_jx || *jy != ult_jy))
		{
			/* fflush por linea: la sesion se cierra con la ventana y un
			   buffer sin vaciar perderia la cola de la grabacion. */
			fprintf(grabar_fp, "%d %04x %u %u %u %u\n", sondeo,
				(unsigned) *botones, (unsigned) *lt, (unsigned) *rt,
				(unsigned) *jx, (unsigned) *jy);
			fflush(grabar_fp);

			ult_botones = *botones;
			ult_lt = *lt;
			ult_rt = *rt;
			ult_jx = *jx;
			ult_jy = *jy;
		}
	}
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
		gl_presentar();
		fps_marcar_cuadro();
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	}
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
	INTC_PEDIR_REINTENTO();

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

/* ------------------------------------------------------------------------ */
/* El reloj por eventos (fase 5 de docs/estado-del-arte-plan.md; tmu.h)     */
/* ------------------------------------------------------------------------ */

/* Hasta donde estan al dia los dos temporizadores. */
static unsigned long long marca_ticks = 0;

/*
	Los pone al dia hasta la ultima frontera consumida, en un solo tramo: la
	aritmetica de resto de tmu_tick()/wdt_tick() da lo mismo en un tramo que
	en muchos, y el instante de cada subdesborde lo garantiza el vencimiento,
	no esta llamada. La usa el bloque periodico, y regmap_read() cuando el
	guest sondea TCNT o WTCNT -- que tiene que ver el valor de la frontera,
	como siempre.
*/
void reloj_sincronizar_ticks(void)
{
	if (reloj_total == marca_ticks)
		return;

	timer_check((DWORD) (reloj_total - marca_ticks));
	wdt_tick((DWORD) (reloj_total - marca_ticks));
	marca_ticks = reloj_total;
}

/* 1 si el DMAC propio tiene un canal con trabajo posible: con eso el bloque
   no se saltea, porque dma_check() avanza por sondeo. Los registros se
   escriben por regmap_write(), que invalida. */
static int dma_auto_activo(void)
{
	if (!(*DMAOR & DME))
		return 0;

	return ((*CHCR0 & DE) && !(*CHCR0 & CHCR_TE) && CHCR_RS(*CHCR0) == CHCR_RS_AUTO)
	    || ((*CHCR1 & DE) && !(*CHCR1 & CHCR_TE) && CHCR_RS(*CHCR1) == CHCR_RS_AUTO)
	    || ((*CHCR2 & DE) && !(*CHCR2 & CHCR_TE) && CHCR_RS(*CHCR2) == CHCR_RS_AUTO)
	    || ((*CHCR3 & DE) && !(*CHCR3 & CHCR_TE) && CHCR_RS(*CHCR3) == CHCR_RS_AUTO);
}

/*
	El proximo vencimiento, absoluto sobre reloj_total. Conservador por
	construccion: quedarse corto solo hace correr el bloque de mas -- que es
	exactamente lo de hoy --; pasarse seria una entrega tardia, y por eso
	cada insumo es la aritmetica exacta de su subsistema. La linea de barrido
	esta siempre (<= ~6400 ciclos), asi que nunca es infinito y el delta de
	los temporizadores cabe en DWORD.
*/
static unsigned long long reloj_calcular(void)
{
	static int eventos = -2;			/* -2: sin leer el entorno */
	unsigned long long v, t;

	if (eventos == -2)
	{
		const char * e = getenv("DCEMU_SIN_RELOJ_EVENTOS");

		/* Con el hilo del AICA no se saltea nada: ese camino publica trabajo
		   en cada frontera y no tiene vencimiento que calcular. */
		eventos = !(e != NULL && atoi(e) != 0) && !opciones.hilos;
	}

	if (!eventos || dma_auto_activo())
		return 0;

	v = marca_linea + pvr_ciclos_linea;			/* la linea de barrido */

	/* La inversa del AICA son dos divisiones de 64 bits y su argumento solo
	   cambia cuando una muestra se produce: memoizada, el servicio tipico
	   --el de la linea de barrido-- no las paga. Puro valor calculado, asi
	   que no puede mover nada. */
	{
		static unsigned long long m_memo = ~0ull, t_memo;
		unsigned long long m = aica_muestras_hechas() + 1;

		if (m != m_memo)
		{
			m_memo = m;
			t_memo = aica_reloj_de_muestra(m);
		}

		t = t_memo;
	}

	if (t < v)
		v = t;

	t = tmu_proximo();
	if (t != ~0ull && reloj_total + t < v)
		v = reloj_total + t;

	t = wdt_proximo();
	if (t != ~0ull && reloj_total + t < v)
		v = reloj_total + t;

	t = intc_proximo_vence();
	if (t < v)
		v = t;

	return v;
}

/* Tiempo real (SDL_GetTicks) al entrar a main_loop, para --limitar. */
static unsigned long real_inicio = 0;

/*
	La vuelta de una falta, compartida por las dos formas de armar el salto.

	Restaurar deja PC en la instruccion que fallo -- o en el salto, si la falta
	fue en su ranura de retardo, porque el longjmp desenrolla los dos niveles.
*/
static void falta_reponer(void)
{
	/* El longjmp salteo el resto del bloque en reproduccion. Ver bloques.h. */
#ifdef DCEMU_BLOQUES
	bloques_cortar();
#endif

	excepcion_salto_armado = 0;
	en_ranura_retardo = 0;

	/* El longjmp se salteo el bajado del cable trampa de la elision; el
	   reporte, si correspondia, ya salio en excepcion_abortar(). */
	excepcion_exenta_en_curso = 0;

	/*
		Un error de direccion del camino rapido llega SIN instantanea: no se
		tomo ninguna, y restaurar la ultima que hubo seria reponer el estado de
		otra instruccion. Se entra con el estado que haya -- ver
		excepcion_direccion() en excepciones.h.
	*/
	if (excepcion_sin_instantanea)
		excepcion_sin_instantanea = 0;
	else
	{
		excepcion_instantanea_restaurar();

		/* Lo poco que no se restaura: la excepcion de operacion de FPU deja
		   Cause escrito, y Flag no. */
		excepcion_reponer();
	}

	excepcion_entrar(excepcion_codigo, excepcion_vector);
}


#ifdef DCEMU_INLINE
/*
	Los diez manejadores mas frecuentes, **en linea dentro del bucle**.

	Es lo unico del cuerpo de main_loop() que quedaba sin medir. El perfil de
	Release del 2026-08-04 pone a `main_loop` en el 38,7 % de las muestras de
	Crazy Taxi con ningun manejador por encima del 2 %, y de las piezas que hay
	ahi adentro casi todas ya dieron cero: las cuatro banderas por instruccion
	(fase 2.4), la busqueda de la palabra y la tabla de 65536 punteros (el cache
	de bloques). Lo que el cache de bloques **no** quito fue la llamada indirecta
	en si -- seguia haciendo una por instruccion --, y esto la quita para las
	codificaciones que cubre.

	Sirve para dos cosas a la vez, y por eso se escribio asi y no como sonda: si
	rinde, es la optimizacion; y rinda o no, la fraccion cubierta
	(`perf_inline_si` contra `perf_inline_no`) permite extrapolar cuanto vale la
	llamada para **todas** las instrucciones, que es el numero que decide si un
	recompilador dinamico tiene de donde sacar su ganancia.

	Los cuerpos son **copias literales** de los manejadores, con sus rarezas:
	`mov3` (MOV Rm,Rn) **no suma ciclos** y aca tampoco, porque si no el guest
	diverge. Ninguna de estas diez filas de `opcodes[]` lleva restriccion de
	PR/SZ, asi que resuelven al mismo manejador en las cuatro tablas y meterlas
	en linea es seguro sin mirar FPSCR.

	Devuelve 1 si la atendio; 0 manda a la tabla de siempre.

	**Va forzada en linea, no sugerida.** El `__inline` de DC_INLINE es una
	pista y MSVC la ignora en una funcion de este tamano: la primera version
	quedo como llamada de verdad y la corrida salio **20 % mas lenta** que el
	camino normal -- para las codificaciones que cubre reemplazaba una llamada
	por otra, y para el resto agregaba una encima de la que ya habia. Sin forzar
	el inline esto no mide lo que dice medir.
*/
#if defined(_MSC_VER)
#define DESPACHO_EN_LINEA	__forceinline
#elif defined(__GNUC__)
#define DESPACHO_EN_LINEA	__attribute__((always_inline)) __inline__
#else
#define DESPACHO_EN_LINEA	DC_INLINE
#endif

static DESPACHO_EN_LINEA int despacho_inline(WORD arg)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	switch (arg >> 12)
	{
	case 0x1:					/* movl18: MOV.L Rm,@(disp,Rn) */
		{
			DWORD m_disp = arg & 0x0F;
			DWORD valor  = R(m);

			m_disp <<= 2;
			m_disp += R(n);

			WriteMemoryL(m_disp, &valor);
			PC += 2;
			core.context.cycles += 1;
		}
		return 1;

	case 0x2:
		if ((arg & 0x0F) == 0x2)			/* movl6: MOV.L Rm,@Rn */
		{
			WriteMemoryL(R(n), (DWORD *) &R(m));
			PC += 2;
			core.context.cycles += 2;
			return 1;
		}
		if ((arg & 0x0F) == 0x8)			/* tst80: TST Rm,Rn */
		{
			if (R(n) & R(m))
				UNSET_T
			else
				SET_T
			PC += 2;
			core.context.cycles += 1;
			return 1;
		}
		return 0;

	case 0x3:
		if ((arg & 0x0F) == 0xC)			/* add39: ADD Rm,Rn */
		{
			R(n) += R(m);
			PC += 2;
			core.context.cycles += 1;
			return 1;
		}
		if ((arg & 0x0F) == 0x0)			/* cmpeq44: CMP/EQ Rm,Rn */
		{
			if ((signed) R(m) == (signed) R(n))
				SET_T
			else
				UNSET_T
			PC += 2;
			core.context.cycles += 1;
			return 1;
		}
		return 0;

	case 0x5:					/* movl21: MOV.L @(disp,Rm),Rn */
		{
			DWORD m_disp = (arg & 0x0F);

			ReadMemoryL(R(m) + m_disp * 4, &R(n));
			PC += 2;
			core.context.cycles += 1;
		}
		return 1;

	case 0x6:
		if ((arg & 0x0F) == 0x3)			/* mov3: MOV Rm,Rn */
		{
			R(n) = R(m);
			PC += 2;
			/* Sin ciclos: el manejador tampoco los suma. Ver arriba. */
			return 1;
		}
		if ((arg & 0x0F) == 0x2)			/* movl9: MOV.L @Rm,Rn */
		{
			ReadMemoryL(R(m), (DWORD *) &R(n));
			PC += 2;
			core.context.cycles += 2;
			return 1;
		}
		return 0;

	case 0x7:					/* add40: ADD #imm,Rn */
		{
			signed long s = SignExtend8(arg & 0xFF);

			R(n) += s;
			PC += 2;
			core.context.cycles += 1;
		}
		return 1;

	case 0xE:					/* mov0: MOV #imm,Rn */
		{
			DWORD m_disp = SignExtend8(arg & 0xFF);

			R(n) = m_disp;
			PC += 2;
			core.context.cycles += 1;
		}
		return 1;
	}

	return 0;
}
#endif /* DCEMU_INLINE */

void main_loop(void)
{
	SDL_Event event;
//	int cnt=0;
//	DWORD valor;
//	int timer_cnt = 0;

	// Referencia de tiempo real para --limitar. Se toma aca y no al arrancar el
	// programa para no contar la carga de la BIOS y de la imagen.
	real_inicio = (unsigned long) SDL_GetTicks();

	timer_check(0); // arranca sin ciclos transcurridos: solo fija el TSTR previo

	/* Desde aca el jmp_buf del setjmp de abajo esta (o va a estar) vigente, y
	   un error de direccion puede desenrollar por el. Fuera de este bucle --
	   los arneses de prueba -- la comprobacion de alineacion es inerte. */
	excepcion_salto_valido = 1;

	for (;;)
	{
		/*
			El salto de vuelta de una falta, armado UNA vez y no por
			instruccion.

			Estaba adentro del bucle, en el `else if (setjmp(...) == 0)` que
			envolvia a cada instruccion con la MMU encendida. Eso es un setjmp
			por instruccion emulada -- con Windows CE, 5100 millones en 35
			segundos emulados -- y en MSVC no es barato: guarda el contexto de
			registros y el marco de SEH.

			Aca se ejecuta una vez por vuelta del bucle exterior, que gira solo
			cuando el emulador se detiene. Un longjmp desde excepcion_abortar()
			aterriza aqui, repone la falta y vuelve a entrar al bucle interior;
			la instruccion que fallo se reejecuta, que es justo lo que pide el
			manual.

			Lo unico que hay que cuidar es que ninguna variable local de
			main_loop() sobreviva al salto con valor util: `event` es la unica
			que hay y SDL_PollEvent la vuelve a llenar antes de mirarla.
		*/
		if (setjmp(excepcion_salto) != 0)
			falta_reponer();

#ifdef DCEMU_BLOQUES
		/*
			El cursor del bloque en reproduccion, **local y no global**.

			Un global tiene que recargarse alrededor de cada llamada a manejador
			--el compilador no puede probar que el manejador no lo toca--; una
			local cuya direccion nunca se toma se queda en un registro salvado.
			Medido: con los tres en globales la sonda perdia 11,4 %.

			Va **despues** del setjmp a proposito: el comentario de arriba avisa
			que ninguna local de main_loop() puede sobrevivir al salto, y un
			cursor que sobreviviera apuntaria a media entrada de otro bloque.
			Declarada aca, el longjmp la repone.

			Lo unico que queda global es `bloques_esperado`, porque hay que poder
			envenenarlo desde adentro de un manejador --UpdateFPSCR() repuntando
			la tabla de despacho-- y desde el bloque periodico. Con el envenenado,
			la comprobacion de arriba anula el cursor sola.
		*/
		const struct bloque_e * cur = NULL, * cur_fin = NULL;
#endif

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
			{
#ifdef DCEMU_BLOQUES
				/* Entro a la excepcion: el PC ya es el del manejador. El cursor
				   lo anula la comprobacion de arriba, porque el PC dejo de ser
				   el esperado; aca solo hay que soltar la grabacion. */
				bloques_cortar();
#endif
				continue;
			}

#ifdef DCEMU_BLOQUES
			if (!excepcion_vigilar && bloques_sonda)
			{
				/*
					La sonda de bloques predecodificados (DCEMU_SONDA_BLOQUES=1).
					Ver bloques.h: reemplaza la busqueda de la palabra y la de la
					tabla de 65536 punteros por un recorrido secuencial de lo que
					ya se ejecuto una vez.

					**El selector lo pagan las dos ramas**, asi que el A/B entre
					la sonda encendida y apagada mide el despacho y nada mas. Lo
					unico que subestima es el beneficio absoluto: una version de
					verdad no llevaria este `if`.
				*/
				/*
					**La verificacion va al principio de la vuelta, no despues
					del manejador.** El manejador no es el unico que mueve el
					PC: el bloque periodico de mas abajo entrega interrupciones,
					y `intc_revisar_sh4()` entra a la excepcion cambiando el PC
					**despues** de que el despacho termino. Comprobando aca se
					cubre cualquier desvio venga de donde venga, incluida la
					invalidacion por cambio de tabla de despacho, que envenena
					`bloques_esperado` desde adentro de un manejador.

					Comprobar despues del manejador costo una corrida entera:
					el cursor seguia vivo tras una interrupcion y reproducia la
					instruccion siguiente del bloque en el PC del manejador.
				*/
				if (PC != bloques_esperado)
				{
					if (bloques_grabando)
						bloques_cerrar();

					cur = NULL;
				}

				/* La consulta va **solo al empezar un bloque**: mientras se
				   graba no hay nada que buscar. */
				if (cur == NULL && !bloques_grabando)
					cur = bloques_buscar(PC, (const void *) oplist, &cur_fin);

				bloques_esperado = PC + 2;

				if (cur != NULL)
				{
					PERF_CONTAR(perf_instrucciones);
					cur->f(cur->instr);

					if (++cur == cur_fin)
						cur = NULL;
				}
				else
				{
					/* Grabar: el camino de siempre, anotando antes de ejecutar
					   porque el manejador mueve el PC. */
					WORD		instr = *(WORD *) MMU_FETCH_PUNTERO(PC);
					opcode_f *	f     = OP_HANDLER(oplist, instr);

					bloques_anotar(PC, (const void *) oplist, instr, f);

					PERF_CONTAR(perf_instrucciones);
					f(instr);
				}

				PERF_BLOQUE(PC);
			}
#endif
			if (!excepcion_vigilar)
			{
				// Camino rapido: sin MMU, sin SR.FD y sin bits de Enable en
				// FPSCR no hay falta que abortar, asi que no se saca
				// instantanea ni se arma el salto. Es todo lo que corre hoy.
				//
				// Y se despacha **derecho a la tabla**, sin pasar por
				// core.execute. Eran dos llamadas indirectas por instruccion:
				// core.execute es un puntero a funcion dentro de una estructura
				// que llama a run(), que hace oplist[arg](arg). run() sigue
				// existiendo y sigue siendo el camino de las ranuras de retardo
				// -- branch.c y rte143() lo necesitan, porque su prueba de
				// SR.FD es lo que distingue 0x800 de 0x820 --, pero aqui no
				// hace falta: !excepcion_vigilar implica fpu_deshabilitada en
				// cero, que es la unica razon por la que run() mira algo.
				// Ver docs/rendimiento-plan.md, fase 2.1.
#ifdef DCEMU_FUSION
				/* El prototipo de bloques fusionados (fase 4 de
				   rendimiento-plan-2.md). Solo con el emulador en marcha
				   plena: la traza y el UBC ven instruccion por instruccion,
				   asi que el lazo fusionado no corre cuando estan puestos.
				   Si corre, PC y ciclos quedan avanzados y se cae derecho al
				   bloque periodico, igual que tras un despacho normal. */
				if (fusion_activa && PC == FUSION_CT_ENTRADA
					&& DebugMode == DBG_RUN && !traza_activa && !ubc_activa
					&& fusion_lazo_ct())
					;
				else
#endif
#ifdef DCEMU_JIT
				/* El JIT (fase 0 de docs/recompilador-plan.md). Mismas
				   condiciones que la sonda y por el mismo motivo: la traza y
				   el UBC ven instruccion por instruccion, asi que el codigo
				   emitido no corre cuando estan puestos. El primer filtro es
				   el mapa de bits; la busqueda real solo se paga con el bit
				   puesto. */
				if (jit_activo && JIT_MARCADO(PC)
					&& DebugMode == DBG_RUN && !traza_activa && !ubc_activa
					&& jit_despachar(PC))
					;
				else
#endif
				{
					/* Por MMU_FETCH_PUNTERO y no por get_memory_pointer, aunque
					   aqui la MMU este siempre apagada: !excepcion_vigilar
					   implica !mmu_activa, y con la MMU apagada la macro **es**
					   get_memory_pointer, asi que no se paga nada. Lo que se
					   gana es que el invariante deje de ser implicito: si
					   alguien cambia lo que mira excepcion_vigilar, este camino
					   no se saltea la traduccion en silencio. */
					WORD instr = *(WORD *) MMU_FETCH_PUNTERO(PC);

					PERF_CONTAR(perf_instrucciones);

					/* El censo del contrato (perf.h): la clase de la fila y el
					   largo de las corridas de filas directas. Va junto al
					   contador de instrucciones y por lo mismo -- el camino con
					   MMU y las ranuras cuentan adentro de run(). */
					PERF_OPCODE(PC, instr);
#ifdef DCEMU_INLINE
					if (despacho_inline(instr))
						PERF_CONTAR(perf_inline_si);
					else
					{
						PERF_CONTAR(perf_inline_no);
						OP_DESPACHAR(instr);
					}
#else
					OP_DESPACHAR(instr);
#endif

					/* La forma de ejecucion: si el PC quedo donde seguia, la
					   corrida sigue; si no, se cierra. Ver perf.h. */
					PERF_BLOQUE(PC);
				}
			}
			else if (excepcion_sonda_setjmp_instr)
			{
				// La forma vieja, conservada para poder medir la nueva contra
				// ella en el mismo binario: un setjmp por instruccion.
				if (setjmp(excepcion_salto) == 0)
				{
					excepcion_instantanea_tomar();

					excepcion_salto_armado = 1;
					core.execute(*(WORD *) MMU_FETCH_PUNTERO(PC));
					excepcion_salto_armado = 0;

					PERF_BLOQUE(PC);
				}
				else
					falta_reponer();
			}
			else
			{
				// Las excepciones generales del SH-4 son reejecutables: el
				// manejador arregla lo que haga falta, hace RTE y la
				// instruccion se repite entera. Pero los handlers de dcemu
				// mutan registros alrededor del acceso (MOV.L @Rn+,
				// MOV.L Rm,@-Rn, FMUL sobre su propio destino), asi que hay
				// que poder deshacerlo. Ver docs/mmu-plan.md, fase 5.
				//
				// **El setjmp no esta aca**: vive arriba del bucle exterior y
				// se arma una vez. Un longjmp aterriza alli, repone y vuelve a
				// entrar. Lo que hay que dejar por instruccion es la
				// instantanea --que depende de la instruccion-- y la bandera
				// de armado, que dice si una falta debe saltar o solo
				// registrarse. Ver docs/rendimiento-plan.md, fase 6.3.
				//
				// La instantanea se toma **despues** de buscar la instruccion,
				// para poder saltearla en las codificaciones que opcodes.c
				// audito como incapaces de abortar (la elision, fase 1 de
				// docs/rendimiento-plan-2.md). Eso obliga a declarar antes que
				// no hay instantanea vigente: la busqueda puede faltar, y si
				// faltara con la instantanea de la instruccion ANTERIOR
				// todavia marcada valida, la reposicion la restauraria --
				// desharia una instruccion que si se ejecuto entera. El
				// contenido de la copia no cambia por el orden: la busqueda no
				// toca core.context (URC vive en regmem).
				excepcion_instantanea_invalidar();

				excepcion_salto_armado = 1;

				{
					WORD instr = *(WORD *) MMU_FETCH_PUNTERO(PC);

#ifdef DCEMU_FUSION
					/* El bloque fusionado con MMU (fase 4 de
					   rendimiento-plan-2.md). Corre con el salto armado y la
					   instantanea invalidada: un acceso que falte sale por
					   longjmp con el contexto ya en el estado pre-instruccion
					   (fusion.c). La primera palabra se compara aca; el resto
					   lo verifica el bloque. */
					if (fusion_activa && PC == FUSION_CE_ENTRADA
						&& instr == 0x6173
						&& DebugMode == DBG_RUN && !traza_activa && !ubc_activa
						&& fusion_bloque_ce())
						;
					else
#endif
#ifdef DCEMU_JIT
					/* El JIT con MMU: corre con el salto armado y la
					   instantanea invalidada, asi que un acceso que falte sale
					   por longjmp con el contexto ya en el estado
					   pre-instruccion (jit.c). */
					if (jit_activo && JIT_MARCADO(PC)
						&& DebugMode == DBG_RUN && !traza_activa && !ubc_activa
						&& jit_despachar(PC))
						;
					else
#endif
					if (excepcion_elision && excepcion_instr_exenta[instr])
					{
						/*
							Manejador auditado en opcodes.c: no puede abortar,
							asi que no hay nada que deshacer y la copia entera
							sobra. El intento anterior de esta elision
							clasificaba por tipo de operando y rompio a DCDoom
							en silencio; de ahi las dos redes: el cable trampa
							(excepcion_abortar reporta a los gritos si algo
							aborta igual) y DCEMU_SONDA_ELISION_VERIFICAR=1,
							que toma la instantanea de todos modos y solo
							contrasta la clasificacion.
						*/
						PERF_CONTAR(perf_instantaneas_elididas);

						if (excepcion_sonda_elision_verificar)
							excepcion_instantanea_tomar();

						excepcion_exenta_en_curso = 1;
						core.execute(instr);
						excepcion_exenta_en_curso = 0;
					}
					else
					{
						excepcion_instantanea_tomar();
						core.execute(instr);
					}
				}

				excepcion_salto_armado = 0;

				/* Este es el camino del unico guest con MMU del arbol, asi que
				   sin esto la forma de ejecucion de DCDoom saldria en cero. */
				PERF_BLOQUE(PC);
			}

	//			(*PC_func) ();

			// Cada cuantos ciclos se atiende a los perifericos. El numero y su
			// derivacion viven en tmu.h, junto al reloj: era 50 y con eso este
			// bloque se llevaba un 21 % del tiempo real preguntando si habia
			// algo que hacer.
			//
			// (El comentario que estaba aqui hablaba de los ticks por segundo
			// del timer de KOS, que no es lo que decide este numero.)
			//
			// **La segunda condicion adelanta el bloque cuando una interrupcion
			// se vuelve entregable**, y no espera al compas: lo que le falta a
			// una peticion es que la fuente pida y que SR lo permita, y las dos
			// cosas pasan en instantes precisos --el temporizador que desborda,
			// el DMAC que termina, el guest que escribe SR--. Esos sitios ponen
			// intc_sh4_reintentar; ver intc.c.
			//
			// Va **pegada al `if` que ya se evaluaba**, no en una rama propia, y
			// esa diferencia es todo el costo: con su propio `if` Crazy Taxi
			// pagaba 8,5 % (62 007 ms contra 57 152); aqui no paga nada medible
			// (56 790). Adelantar el bloque entero por una peticion no molesta:
			// pasa unas mil veces por segundo emulado y sus consumidores llevan
			// su propio resto.
			if (core.context.cycles >= RELOJ_GRANO || intc_sh4_reintentar)
			{
				// Consumir el acumulado entero y pasarlo al contador monotono.
				// Los consumidores periodicos comparan contra su propia marca:
				// ya no hay acumuladores que sumen y resten cantidades
				// distintas. Ver docs/clock-plan.md, fase 2.
				DWORD ciclos = core.context.cycles;

				core.context.cycles -= ciclos;
				reloj_total += ciclos;

#ifdef DCEMU_JIT
				/* De donde salen los candidatos del traductor. Aqui y no en el
				   bucle de instrucciones: este bloque corre cada RELOJ_GRANO
				   ciclos --unas 130 instrucciones-- asi que muestrear no
				   cuesta nada en el camino caliente. Ver jit.h. Queda en la
				   frontera barata a proposito: asi el descubrimiento del
				   traductor no depende del reloj por eventos. */
				jit_muestrear(PC);
#endif

				/* El punto de control por ms que NO apaga el JIT (DCEMU_CP_MS).
				   Arranca en -2 ("sin leer"), asi que la primera pasada entra,
				   lee el entorno y lo deja en -1 si esta apagado: costo cero
				   en regimen. Tambien en la frontera barata: su cadencia no
				   puede depender del interruptor. */
				if (traza_cp_tope != -1)
					traza_cp_periodico();

				/* La telemetria del auto de SR2 (DCEMU_SONDA_SR2), misma
				   regla: -1 apagada y el llamador ni siquiera llama. */
				if (traza_sr2_activa != -1)
					traza_sr2_periodico();

				/*
					El reloj por eventos (tmu.h): si ningun vencimiento llego y
					nadie invalido, la frontera termina aca. La grilla y la
					contabilidad quedaron identicas -- lo unico que se ahorra
					es el servicio, que hoy descubre mil veces de cada mil que
					no hay nada que hacer. Con DCEMU_SIN_RELOJ_EVENTOS=1 el
					vencimiento vive en 0 y esto es siempre verdadero.
				*/
				if (reloj_total >= reloj_vencimiento || intc_sh4_reintentar)
				{
				/* Si algo se postea durante este mismo servicio (SCANINT, la
				   linea del AICA), su reloj_tocar() tiene que sobrevivir al
				   recalculo del final: se muestrea aca y alla se compara. */
				unsigned reloj_toques_entrada = reloj_toques;

				PERF_MARCA_MUESTRA(t_serv, n_serv);

				/* El censo de la causa: cuantos servicios corren por un
				   vencimiento real y cuantos solo porque el reintento de
				   entrega quedo armado (UpdateSR lo arma, y WinCE escribe SR
				   cientos de miles de veces por segundo). Aqui vivio unas
				   horas el SERVICIO PARTIDO (2026-08-25): el camino de
				   solo-reintento salteaba ticks/AICA/DMA -- exacto por las
				   premisas del reloj por eventos -- y la tanda salio NEUTRA
				   en los tres guests aun con DOOM corriendo 703 000 de esos
				   servicios por segundo (86,8 %; SR2 60,6 %). La leccion es
				   la de B.3 y el cuerpo rapido del DSP: lo salteado eran
				   cargas y comparaciones predecibles e independientes, que el
				   desorden del procesador ya ejecutaba en la sombra del
				   trabajo vecino. El censo queda porque nombra la forma del
				   bloque; el mecanismo esta en el plan por si el reparto
				   cambia. */
				if (perf_activa)
				{
					if (reloj_total >= reloj_vencimiento)
						perf_serv_vencido++;
					else
					{
						perf_serv_reintento++;

						/* El desglose que decide si el rearme condicional
						   tiene techo: un reintento CON alguien pidiendo es
						   pendiente enmascarado (inevitable); SIN nadie, el
						   armado fue por una escritura de SR con cero
						   pendientes y era ahorrable. */
						if (intc_alguien_pide())
							perf_serv_reintento_pide++;

						/* Y la forma conservadora (banderas crudas), que es
						   la unica que el rearme condicional exacto puede
						   usar: si ESTA fraccion es alta, no hay techo. */
						if (intc_alguien_pide_conservador())
							perf_serv_reintento_cons++;
					}
				}

				/* La coherencia entre la bandera y el limite del corte emitido:
				   un sitio que armara una sin la otra dejaria al traductor sin
				   cortar donde el interprete corta, y eso no se ve hasta mil
				   millones de instrucciones despues. Aqui cuesta una comparacion
				   por servicio y se informa al salir. Ver intc.h. */
				if ((intc_corte_limite == 0) != (intc_sh4_reintentar != 0))
					intc_corte_incoherente++;

				INTC_LIMPIAR_REINTENTO();

				// Los dos temporizadores reciben la cantidad de ciclos y llevan
				// su propio resto, cada uno con su divisor. Ninguno entrega su
				// interrupcion: solo dejan su bandera puesta. La cantidad es lo
				// acumulado desde el ultimo servicio: con el interruptor
				// apagado es el mismo delta de siempre.
				reloj_sincronizar_ticks();

				// El AICA no recibe ciclos: compara contra su propia marca de
				// reloj_total, porque su reloj es otro -- 44100 Hz de muestreo
				// y 22,5792 MHz de bloque de audio. Ver aica.h.
				//
				// Con el hilo del AICA esto solo adelanta su objetivo (un
				// almacen volatile, sin mutex) y no espera a nadie; sin el, es
				// el aica_tick() de siempre. Publicar aqui, en CADA servicio,
				// es lo que hace exacto al hilo: el calendario de
				// publicaciones es el calendario de mezclas del camino de un
				// hilo. Ver hilo_aica.h y docs/hilos-plan.md, fase 1.
				hilo_aica_publicar();

				// Y la linea del AICA hacia el ASIC. El chip solo la sube y la
				// baja (aica_linea_asic); la entrega va aqui, que es el hilo
				// donde vive el controlador de interrupciones. Sin hilos esto
				// corre dos lineas despues del tick y no cambia nada.
				//
				// Se entrega **una vez por peticion**, no por flanco del
				// nivel: intc_add_ext() deduplica sola contra la cola, asi que
				// el codigo original la llamaba cada vez que el chip pedia y
				// eso volvia a encolar el evento despues de que el guest lo
				// consumiera. Por flanco se perdian todas las peticiones menos
				// la primera. Ver aica.h.
				{
					static unsigned vistas_sub = 0, vistas_baj = 0;
					static int sonda_linea = -1;

					if (sonda_linea < 0)
						sonda_linea = getenv("DCEMU_SONDA_LINEA_AICA") != NULL;

					if (aica_demora_linea <= 0)
					{
						// La conducta anterior, textual: los contadores sin
						// instante. Bajo --hilos esto NO es determinista (el
						// contador se hace visible cuando el hilo llega en
						// tiempo real); sin hilos es la linea base de siempre.
						while (vistas_sub != aica_asic_subidas)
						{
							vistas_sub++;

							/* SONDA temporal: el instante de entrega de la linea. */
							if (sonda_linea && vistas_sub <= 400)
								fprintf(stderr, "sonda linea: subida %u en reloj=%llu\n",
									vistas_sub, (unsigned long long) reloj_total);

							intc_add_ext(ASIC_EVT_EXT_AICA);
						}

						while (vistas_baj != aica_asic_bajadas)
						{
							vistas_baj++;
							intc_remove_ext(ASIC_EVT_EXT_AICA);
						}
					}
					else
					{
						// La entrega determinista (2026-09-05): se aplican, en
						// orden causal, los cambios sellados hasta la muestra
						// `m - demora`. Esa muestra el hilo la termino hace rato
						// casi siempre; si no, se lo espera (acotado: a lo sumo
						// lo que tarda en cerrar las `demora` muestras que le
						// faltan, y con el objetivo ya publicado dos lineas mas
						// arriba). Sin hilos la espera no hace nada porque el
						// tick recien corrio hasta aqui mismo.
						//
						// El ORDEN de las tres lecturas es portante: primero la
						// senal de terminacion, despues la cabeza del registro
						// (dentro de sacar), despues la entrada. Al reves, una
						// cabeza vieja con una senal fresca difiere un cambio
						// de la muestra `k` al grano siguiente. Ver aica.h.
						unsigned long long m = aica_muestras_al_reloj();

						if (m >= (unsigned long long) aica_demora_linea)
						{
							unsigned long long k = m - (unsigned long long) aica_demora_linea;
							unsigned long long sello;
							int nivel;

							if (aica_muestras_listas < k)
								hilo_aica_esperar_muestra(k);

							while (aica_linea_log_sacar(k, &nivel, &sello))
							{
								if (nivel)
								{
									vistas_sub++;

									if (sonda_linea && vistas_sub <= 400)
										fprintf(stderr, "sonda linea: subida %u"
											" muestra=%llu en reloj=%llu\n",
											vistas_sub, sello,
											(unsigned long long) reloj_total);

									intc_add_ext(ASIC_EVT_EXT_AICA);
								}
								else
								{
									vistas_baj++;
									intc_remove_ext(ASIC_EVT_EXT_AICA);
								}
							}
						}
					}
				}

				// Y aca la que corresponda, si SR lo permite. Si no se puede, la
				// bandera sigue puesta y se reintenta en la vuelta siguiente,
				// que es lo que hace el chip.
				intc_revisar_sh4();

				// El ASIC va en este compas y le alcanza: lo que DCDoom
				// necesitaba pronto era la entrega al SH-4, no esta. Con la
				// peticion por nivel la compuerta es verdadera mientras haya un
				// bit sin acusar, o sea casi siempre, y por instruccion costaba
				// 9 veces mas en Crazy Taxi.
				if (intc_asic_pendiente())
					check_ints();

				dma_check();

				PERF_SUMAR_MUESTRA(t_serv, perf_ns_servicio);

				/*
					**La linea de barrido se evalua aqui adentro, no por
					instruccion.**

					La condicion es `reloj_total - marca_linea >= pvr_ciclos_linea`
					y `reloj_total` **solo avanza dos lineas mas arriba**, dentro de
					este mismo bloque: es el unico sitio del arbol que lo mueve
					(CLAUDE.md: "reloj_total only ever rises"). Evaluarla por
					instruccion eran tres cargas de globales y una comparacion para
					descubrir, mil veces de cada mil, que el reloj no se habia
					movido.

					Lo unico que puede volverla cierta sin que `reloj_total` avance
					es que el guest escriba SPG_LOAD o SPG_CONTROL y cambie
					`pvr_ciclos_linea`. En ese caso la linea sale hasta RELOJ_GRANO
					ciclos mas tarde, que es la misma granularidad con la que ya
					corren el TMU, el WDT y el AICA.

					Sale del perfil de Release del 2026-08-04: `main_loop` es el
					38,7 % de las muestras en Crazy Taxi y ningun manejador pasa del
					2 %, asi que lo que quede en el cuerpo del bucle vale mirarlo
					una por una.
				*/
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
		}

	//				if ((++cnt) == 500000)
				if (pvr_scanline >= pvr_spg_load_vcount) // valor m�ximo que puede tomar
				{
	   				pvr_scanline = 0;

					/* Con el entrelazado puesto (SPG_CONTROL bit 4) cada vuelta
					   del contador es un campo, y el numero de campo alterna;
					   en progresivo queda en 0. Lo consume SPG_STATUS. */
					pvr_campo = (pvr_spg_control & 0x10) ? !pvr_campo : 0;

	//	   				cnt = 0;
					break; // salimos de este ciclo y vamos al siguiente
				}

				/* El proximo vencimiento, con todo ya al dia -- la linea
				   recien avanzada incluida. El break del fin de cuadro se lo
				   saltea a proposito: la frontera siguiente corre el bloque
				   completo y lo recalcula, que una vez por cuadro es gratis.
				   Y si alguien invalido durante el servicio, el 0 que dejo
				   manda: recalcular aca postergaria esa entrega hasta el
				   proximo vencimiento -- se vio como el vblank tarde. */
				if (reloj_toques == reloj_toques_entrada)
					reloj_vencimiento = reloj_calcular();
				} /* fin del servicio del reloj por eventos */
			}

		}

		logxmsg(LOG_PVR, "llamando VBLINT\n");
		intc_add(ASIC_EVT_PVR_VBLINT, 0);

		// El disparo por hardware del Maple va con el vblank, como en el chip.
		maple_vblank();

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
			excepcion_salto_valido = 0;		/* el jmp_buf deja de estar vigente */
			return;
		}

		// La relacion entre tiempo emulado y real, **por segundo**. El resumen
		// del final da el promedio, y el promedio esconde justo lo que importa
		// para el mando: un menu liviano puede correr al doble mientras una
		// escena en 3D arrastra la media hacia 1. Y la auto-repeticion de un
		// juego cuenta cuadros, asi que corre tan rapido como corra el emulador.
		if (traza_activa)
		{
			static unsigned long long marca_emulado = 0;
			static unsigned long      marca_real    = 0;
			static unsigned long      marca_cuadros = 0;

			unsigned long long emulado = reloj_ms();
			unsigned long      real    = (unsigned long) SDL_GetTicks() - real_inicio;

			marca_cuadros++;

			if (emulado - marca_emulado >= 1000)
			{
				unsigned long de = (unsigned long) (emulado - marca_emulado);
				unsigned long dr = real - marca_real;

				fprintf(stderr, "traza: ritmo: %lu ms emulados en %lu reales"
					" (%.2fx), %lu cuadros\n",
					de, dr, dr ? (double) de / (double) dr : 0.0,
					marca_cuadros);

				/* Y el estado de los tres TMU al lado: un juego que mide el
				   retardo de repeticion de su menu con un temporizador repite
				   al ritmo que ese temporizador le marque, y eso no se ve en
				   el censo de interrupciones si el guest lo sondea en vez de
				   pedir la interrupcion. */
				fprintf(stderr, "traza: TMU TSTR=%02x"
					" ch0 TCR=%04x TCNT=%08lx TCOR=%08lx"
					" ch1 TCR=%04x TCNT=%08lx TCOR=%08lx"
					" ch2 TCR=%04x TCNT=%08lx TCOR=%08lx\n",
					(unsigned) *TSTR,
					(unsigned) *TCR0, (unsigned long) *TCNT0,
					(unsigned long) *TCOR0,
					(unsigned) *TCR1, (unsigned long) *TCNT1,
					(unsigned long) *TCOR1,
					(unsigned) *TCR2, (unsigned long) *TCNT2,
					(unsigned long) *TCOR2);

				marca_emulado = emulado;
				marca_real    = real;
				marca_cuadros = 0;
			}
		}

		// Fin de frame: el unico sitio donde tiene sentido frenar. Con --limitar,
		// si el tiempo emulado se adelanto al real se duerme la diferencia. Solo
		// frena: donde dcemu ya es mas lento que una consola no hace nada. Ver
		// docs/clock-plan.md, fase 4.
		// Lo que el cuadro pasa esperando a proposito -- el freno de --limitar
		// y el swap -- se mide aparte y se descuenta: la sonda de tirones
		// pregunta por el TRABAJO del emulador, y sumarle una espera
		// deliberada la haria acusar al limitador de los tirones que busca.
		{
			unsigned long long ns_espera = 0;
			unsigned long long t0;

			if (opciones.limitar)
			{
				unsigned long long emulado = reloj_ms();
				unsigned long      real    = (unsigned long) SDL_GetTicks() - real_inicio;

				if (emulado > real)
				{
					unsigned long sobra = (unsigned long) (emulado - real);

					// Techo por si la cuenta se desmadra: mejor ir rapido que
					// congelar el emulador esperando.
					if (sobra > 100)
						sobra = 100;

					t0 = perf_ahora();
					SDL_Delay(sobra);
					ns_espera += perf_ahora() - t0;
				}
			}
//		intc_check(ASIC_EVT_PVR_VBLINT);
			t0 = perf_ahora();
			RedibujarPantalla();
			ns_espera += perf_ahora() - t0;

#ifdef DCEMU_JIT
			perf_cuadro(ns_espera, jit_cuenta_traducidos(), jit_epoca,
				jit_ns_traducir);
#else
			perf_cuadro(ns_espera, 0, 0, 0);
#endif
		}

		// El sonido producido durante el cuadro, al .wav si hay volcado. La
		// reproduccion no pasa por aqui: de eso se encarga la callback de SDL,
		// en su propio hilo. Ver audio.h.
		audio_volcar();

		while (SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_EVENT_KEY_DOWN:
				{
					/* SDL 1.2 no repetia teclas; SDL3 si, y una repeticion
					   alternaria sola la pausa o el contador de FPS. */
					if (event.key.repeat)
						break;

					logmsg("keydown\r\n");
					switch(event.key.key)
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
					
					case SDLK_A: // BOTON X
					REMOVE_BIT(joystick, CONT_X);
					break;
					
					case SDLK_S: // BOTON A
					REMOVE_BIT(joystick, CONT_A);
					break;
					
					case SDLK_D: // BOTON B
					REMOVE_BIT(joystick, CONT_B);
					break;
					
					case SDLK_W: // BOTON W
					REMOVE_BIT(joystick, CONT_Y);
					break;

					case SDLK_Z: // START
					REMOVE_BIT(joystick, CONT_START);
					break;
					
					case SDLK_Q: // LEFT
					ltrig = TRIGGER_ON;
					break;

					case SDLK_E: // RIGHT
					rtrig = TRIGGER_ON;
					break;

					case SDLK_Y: // joystick up
					joyy = JOYSTICK_UP;
					break;
					
					case SDLK_H: // joystick down
					joyy = JOYSTICK_DOWN;
					break;
					
					case SDLK_G: // joystick left
					joyx = JOYSTICK_LEFT;
					break;
					
					case SDLK_J: // joystick right
					joyx = JOYSTICK_RIGHT;
					break;

					case SDLK_L: // empezar el log en archivo
/*					filelogging++;
					filelogging %= 3; */
					gui_setvisiblelog(!gui_isvisiblelog());
					break;
					
					case SDLK_M: // logmem
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

					case SDLK_V: // logmem
					if (logvideomem)
						logvideomem = false;
					else
						logvideomem = true;
					break;

					case SDLK_R: // logmem
					if (logmemreg)
						logmemreg = false;
					else
						logmemreg = true;
					break;

					case SDLK_P: // pausa
					if (pausa)
						pausa = false;
					else
						pausa = true;
					break;

					case SDLK_F: // contador de FPS en el titulo
					fps_visible = !fps_visible;
					break;
					
/*					case SDLK_I: // generar int?
					intc(0);
					break; */
						
					default:
					break;
					}
				}
				break;
				
				case SDL_EVENT_KEY_UP:
				{
					logmsg("keyup\r\n");
					switch(event.key.key)
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
					
					case SDLK_A: // BOTON X
					SET_BIT(joystick, CONT_X);
					break;
					
					case SDLK_S: // BOTON A
					SET_BIT(joystick, CONT_A);
					break;
					
					case SDLK_D: // BOTON B
					SET_BIT(joystick, CONT_B);
					break;
					
					case SDLK_W: // BOTON W
					SET_BIT(joystick, CONT_Y);
					break;

					case SDLK_Z: // START
					SET_BIT(joystick, CONT_START);
					break;

					case SDLK_Q: // LEFT
					ltrig = TRIGGER_OFF;
					break;

					case SDLK_E: // RIGHT
					rtrig = TRIGGER_OFF;
					break;
					
					case SDLK_Y: // joystick up
					case SDLK_H: // joystick down
					joyy = JOYSTICK_NEUTRAL;
					break;
					
					case SDLK_G: // joystick left
					case SDLK_J: // joystick right
					joyx = JOYSTICK_NEUTRAL;
					break;
		   // toggle fullscreen
                    case SDLK_F1:
					SDL_SetWindowFullscreen(ventana,
						!(SDL_GetWindowFlags(ventana) & SDL_WINDOW_FULLSCREEN));
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

				case SDL_EVENT_QUIT:
				excepcion_salto_valido = 0;	/* el jmp_buf deja de estar vigente */
				return;

/*				case SDL_USEREVENT:
				RedibujarPantalla();
				break; */


				default:
//           			  SDL_EventState(event.type, SDL_IGNORE);
			gui_event(&event);
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

void exitproc(void)
{
	logmsg("Exited with PC = %08x", PC);
}

/*
	stdout.txt y stderr.txt al lado del ejecutable.

	Con SDL 1.2 lo hacia SDLmain.lib por su cuenta, y todo el banco de guiones
	del arbol lee `stderr.txt` en el directorio del binario: el resumen `jit:`,
	los contadores de control, las sondas. SDL3 no trae SDL_main ni redirige
	nada, asi que se hace aqui, antes de que nadie escriba. Igual que entonces:
	se trunca al abrir (dos instancias se pisan) y stderr queda sin bufer, para
	que un informe de caida llegue entero. DCEMU_SIN_REDIRECCION=1 lo deja en
	la consola, para usar el emulador a mano.
*/
static void salida_redirigir(void)
{
#ifdef _WIN32
	char ruta[MAX_PATH + 16];
	DWORD n;
	char * corte;
	const char * v = getenv("DCEMU_SIN_REDIRECCION");

	if (v != NULL && atoi(v) != 0)
		return;

	n = GetModuleFileNameA(NULL, ruta, MAX_PATH);

	if (n == 0 || n >= MAX_PATH)
		return;

	corte = strrchr(ruta, '\\');

	if (corte == NULL)
		return;

	strcpy(corte + 1, "stdout.txt");

	if (freopen(ruta, "w", stdout) != NULL)
		setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

	strcpy(corte + 1, "stderr.txt");

	if (freopen(ruta, "w", stderr) != NULL)
		setvbuf(stderr, NULL, _IONBF, 0);
#endif
}

/*
	Que Windows no estrangule al emulador cuando su ventana queda tapada.

	Medido el 2026-09-05 (Windows 11 26200, Ryzen AI 9 HX 370, clang canonico):
	Crazy Taxi 120 s corre en 39,7 s con la ventana libre y en 66,6 s con otra
	ventana encima --68 % mas lento, la ejecucion identica al digito--, salvo
	que el proceso este reproduciendo audio (40,3 s tapada con la tarjeta
	abierta). Minimizada no lo dispara. Es la politica de energia de Windows 11
	para los procesos que considera de segundo plano --los manda a los nucleos
	eficientes a baja frecuencia (EcoQoS) y les engrosa el reloj--, y
	reproducir audio exime. Un banco desprendido cae en ese regimen sin que
	nadie lo vea --aqui sin entrada de teclado ni raton desde la noche
	anterior: lo que tapa la ventana puede ser la pantalla apagada o el
	bloqueo--, y --sin-audio era el regimen que lo destapaba: 76-132 s en las
	tandas de la tarde con los dos brazos moviendose juntos, 66 s con la
	ventana libre. Ver docs/notas-herramientas.md.

	SetProcessInformation(ProcessPowerThrottling) con el bit en la mascara de
	control y en cero en la de estado es "nunca": ni la velocidad de ejecucion
	ni la resolucion del reloj (de la que depende el SDL_Delay de --limitar).
	DCEMU_ESTRANGULAR=1 deja al proceso como Windows lo quiera: el A/B.
*/
static void proceso_sin_estrangular(void)
{
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
	PROCESS_POWER_THROTTLING_STATE estado;
	const char * v = getenv("DCEMU_ESTRANGULAR");

	if (v != NULL && atoi(v) != 0)
		return;

	memset(&estado, 0, sizeof(estado));
	estado.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	estado.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED
	                   | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	estado.StateMask   = 0;

	if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
	                          &estado, sizeof(estado)))
		fprintf(stderr, "arranque: estrangulamiento del proceso apagado "
			"(velocidad y resolucion del reloj)\n");
	else
		fprintf(stderr, "arranque: no se pudo apagar el estrangulamiento del "
			"proceso (error %lu)\n", (unsigned long) GetLastError());
#endif
}

int main(int argc, char *argv[])
{
//	long idx, cnt = 0;
 	long tam; // , i, j;

	/* Donde termino la carga del ejecutable y cuanto midio, para el registro de
	   transferencias que verifican los titulos de Windows CE. Se calculan al
	   cargar y se aplican despues de gdrom_iniciar(), que borra el estado de la
	   lectora -- ponerlos antes es escribirlos para nadie. */
	DWORD boot_log_fin = 0, boot_log_largo = 0;

	/* Si el IP.BIN marca el titulo como Windows CE (bit 0 del campo de
	   perifericos, offset 0x3E) y si su cabecera resulto usable. Ver donde se
	   carga el ejecutable. */
	int ip_ce_bit = 0, es_ce = 0, ce_log_puesto = 0;
//	short c;
	WORD wvalor;
	DWORD dwvalor;
//	SDL_TimerID vblank_id;
//	SDL_Thread * timer_thread;

	//FILE * fp;

	salida_redirigir();
	proceso_sin_estrangular();

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

	/* SDL3: solo video (los eventos vienen con el). El temporizador ya no es
	   un subsistema, y el audio lo abre audio_iniciar() cuando hace falta. */
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "Video initialization failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

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

	font = BFont_LoadFont("font.png");

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

#ifdef DCEMU_JIT
	// El traductor de bloques del ARM7 a x64 (DCEMU_SIN_JIT_ARM=1 lo deja
	// sin instalar). Vive en el binario del JIT por lo mismo que jit.c.
	arm7jit_iniciar();

	// El microprograma del DSP emitido (DCEMU_SIN_JIT_DSP=1 lo deja sin
	// instalar): el tercer emisor, por la misma puerta.
	aicadspjit_iniciar();
#endif

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

	// La Visual Memory de la ranura 1: los guardados de corridas anteriores,
	// o una tarjeta vacia formateada si el archivo no existe.
	if (!opciones.sin_vmu && vmu_iniciar(opciones.vmu_archivo))
		return 1;

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
		/* --disco= monta una imagen en la lectora aunque el arranque venga
		   del .bin suelto: sin eso la bandeja queda vacia y una demo que use
		   el disco (las pistas de audio, el sistema de archivos) no tiene
		   nada que leer. */
		if (iso_init((char *) opciones.disco))
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

		/*
			DCEMU_SONDA_SIN_CE=1: apaga el bit "usa Windows CE" del IP.BIN.

			**Es una sonda de diagnostico, no un arreglo**, y aisla una frontera
			concreta. El campo de perifericos del IP.BIN son siete digitos hex en
			el offset 0x38, y el bit 0 --el de menor peso del ultimo digito, en
			0x3E-- dice que el titulo es de Windows CE. El bootstrap de Sega que
			corre desde el propio IP.BIN lo mira y, si esta puesto, verifica una
			tabla de bloques transferidos en 0x8CE01010; si no cuadra llama al
			syscall 0x8C0000E0, que es el reinicio.

			Con eso se explica por que DCDoom arranca por .cdi y no por .gdi: el
			rip en .cdi lleva el bit **apagado** --la conversion a selfboot lo
			limpio-- y el .gdi, que es el original, lo lleva puesto. O sea que
			este arbol nunca ejecuto el arranque de CE de verdad: la imagen que
			funciona lo esquiva.

			La sonda existe para contestar si esa verificacion es lo unico que
			falta o si detras hay mas. Ver docs/notas-arranque.md.
		*/
		{
			const char * v = getenv("DCEMU_SONDA_SIN_CE");

			if (v != NULL && atoi(v) != 0)
			{
				unsigned char *	ip = get_memory_pointer(mem_base + ip_offset);
				unsigned char	d  = ip[0x3E];

				/* Digito hexadecimal a valor, y de vuelta sin el bit 0. */
				int n = (d >= 'A') ? (d - 'A' + 10)
				      : (d >= 'a') ? (d - 'a' + 10)
				      : (d >= '0' && d <= '9') ? (d - '0') : -1;

				if (n >= 0)
				{
					n &= ~1;
					ip[0x3E] = (unsigned char) ((n < 10) ? ('0' + n)
					                                     : ('A' + n - 10));

					fprintf(stderr, "sonda: bit de Windows CE del IP.BIN "
						"apagado (%c -> %c)\n", d, ip[0x3E]);
				}
			}
		}

		/*
			Y el bit, leido **despues** de la sonda para que la sonda sirva de
			algo: es lo que decide como se carga el ejecutable. Ver mas abajo.
		*/
		{
			const unsigned char *	ip = get_memory_pointer(mem_base + ip_offset);
			unsigned char			d  = ip[0x3E];

			int n = (d >= 'A') ? (d - 'A' + 10)
			      : (d >= 'a') ? (d - 'a' + 10)
			      : (d >= '0' && d <= '9') ? (d - '0') : 0;

			ip_ce_bit = n & 1;
		}

		/*
			Busquemos el ejecutable. El nombre no es siempre 1ST_READ.BIN: lo
			declara el IP.BIN en su cabecera (offset 0x60, 16 bytes rellenos
			con espacios), y es el mismo campo que usa el boot ROM -- deja el
			puntero en GBR+0x9C. Un juego de Windows CE arranca 0WINCEOS.BIN,
			y con el nombre cableado DCDoom no cargaba por este camino.
		*/
		{
			const unsigned char *	ip = get_memory_pointer(mem_base + ip_offset);
			char					nombre_boot[17];
			int						i;

			memcpy(nombre_boot, &ip[0x60], 16);
			nombre_boot[16] = '\0';

			/* A minusculas y sin el relleno, que es lo que compara
			   min_iso_stat_root() tras min_iso_name_translate(). */
			for (i = 0; nombre_boot[i] != '\0' && nombre_boot[i] != ' '; i++)
				nombre_boot[i] = (char) tolower((unsigned char) nombre_boot[i]);
			nombre_boot[i] = '\0';

			if (nombre_boot[0] == '\0')
				strcpy(nombre_boot, "1st_read.bin");

			/*
				**Un titulo de Windows CE no trae un 1ST_READ.BIN cifrado: trae
				una imagen con un sector de cabecera delante, sin cifrar.**

				El campo de perifericos del IP.BIN lo dice en su bit 0 (offset
				0x3E), y eso es justamente para lo que sirve. El formato salio de
				comparar el archivo en el disco con lo que el boot ROM de verdad
				deja en RAM:

					sector 0, en el offset 0x10:
						+0x00  cuantas transferencias
						+0x04  destino, en fisica
						+0x08  tamano de sector
						+0x0C  largo
					sector 1 en adelante: la imagen, tal cual

				Esos 24 bytes son **byte a byte** los que el ROM escribe en
				BOOT_LOG_BASE, o sea que el cargador los copia de aqui; y el
				bootstrap del IP.BIN despues los verifica contra SB_GDSTARD. Con
				lo que el circulo cierra: la cabecera describe la transferencia,
				el cargador la hace y la anota, y el bootstrap la comprueba.

				Descifrar esto lo destroza -- el bootstrap terminaba saltando a un
				epilogo de funcion y volviendo enseguida -- y cargarlo crudo pero
				sin quitar la cabecera lo deja un sector corrido.
			*/
			es_ce = (ip_ce_bit != 0);

			/* Cifrado: lo decide el formato de la imagen (ver iso.h), y una
			   imagen de Windows CE nunca lo esta -- lo suyo es la cabecera. */
			tam = cargar_archivo_iso(nombre_boot,
				!es_ce && iso_ejecutable_cifrado(),
				get_memory_pointer(mem_base + mem_offset));

			if (tam > 0 && es_ce)
			{
				unsigned char *	p = get_memory_pointer(mem_base + mem_offset);
				DWORD			cab[6];

				memcpy(cab, p + 0x10, sizeof(cab));

				/* Se cree la cabecera solo si describe algo coherente: una
				   transferencia, dentro de lo que se leyo y a la RAM. Si no,
				   se deja como estaba y que se vea en la traza. */
				if (cab[0] == 1 && cab[3] > 0
				 && (long) (cab[3] + 0x800) <= tam
				 && (cab[1] & 0x1FFFFFFF) == ((mem_base + mem_offset) & 0x1FFFFFFF))
				{
					fprintf(stderr, "arranque: imagen de Windows CE, %lu bytes a "
						"%08lx (cabecera de un sector)\n",
						(unsigned long) cab[3], (unsigned long) cab[1]);

					/* La carga util empieza en el segundo sector. */
					memmove(p, p + 0x800, cab[3]);
					tam = (long) cab[3];

					/*
						Y el registro de transferencias sale de la cabecera. Se
						arma campo por campo y no copiando los 24 bytes: los
						cuatro primeros son de la cabecera, pero los dos ultimos
						--la direccion de la entrada y su cero-- los pone el
						cargador, y en el archivo esos bytes son otra cosa.
					*/
					{
						DWORD tabla[6];

						tabla[0] = cab[0];		/* cuantas */
						tabla[1] = cab[1];		/* destino */
						tabla[2] = cab[2];		/* tamano de sector */
						tabla[3] = cab[3];		/* entrada[0]: largo */
						tabla[4] = cab[1];		/*             direccion */
						tabla[5] = 0;

						memwrite(BOOT_LOG_BASE, tabla, sizeof(tabla));
					}

					boot_log_fin   = cab[1] + cab[3];
					boot_log_largo = cab[3];
					ce_log_puesto  = 1;
				}
				else
					fprintf(stderr, "arranque: el titulo dice Windows CE pero su "
						"cabecera no describe una transferencia usable "
						"(%08lx %08lx %08lx)\n",
						(unsigned long) cab[0], (unsigned long) cab[1],
						(unsigned long) cab[3]);
			}

			if (tam <= 0)
			{
				fprintf(stderr, "No se pudo abrir %s.\n", nombre_boot);
				return 1;
			}
		}
		
		fprintf(stderr, "leidos %ld bytes\n", tam);

		/*
			El registro de transferencias del boot ROM, que el camino de hooks no
			creaba porque carga el ejecutable leyendo el archivo y no por DMA.

			El bootstrap del IP.BIN --el codigo de Sega que corre desde
			0x8C008000, igual en todos los juegos-- lo lleva cableado en
			0x8CE01010 y lo verifica cuando el titulo esta marcado como Windows
			CE: toma la ultima entrada, suma direccion + largo y lo compara
			contra SB_GDSTARD. Si no cuadra llama al syscall de reinicio, y por
			eso DCDoom por .gdi se rendia al arrancar.

			El formato salio de mirar lo que deja el ROM de verdad con --bios:

				+0x00  cuantas entradas
				+0x04  destino, en fisica
				+0x08  0x800, el tamano de sector
				+0x0C  entrada[0]: largo
				+0x10               direccion, en fisica
				+0x14               0
				+0x18  entrada[1]...

			Se anota **lo que de verdad se transfirio**, que es lo que hace que
			la comprobacion pase por construccion y no por casualidad: la tabla
			es un registro, no un valor magico que haya que adivinar.
		*/
		/* Salvo que ya lo haya puesto la cabecera de un titulo de Windows CE,
		   que es la fuente de verdad cuando existe. */
		if (!ce_log_puesto)
		{
			DWORD	fisica = (mem_base + mem_offset) & 0x1FFFFFFF;
			DWORD	tabla[6];

			tabla[0] = 1;						/* una transferencia */
			tabla[1] = fisica;
			tabla[2] = 0x800;
			tabla[3] = (DWORD) tam;				/* largo */
			tabla[4] = fisica;					/* direccion */
			tabla[5] = 0;

			memwrite(BOOT_LOG_BASE, tabla, sizeof(tabla));

			/* Los contadores de la DMA se ponen **despues de gdrom_iniciar()**,
			   que los borra. Aqui solo se anota que hay que ponerlos. */
			boot_log_fin   = fisica + (DWORD) tam;
			boot_log_largo = (DWORD) tam;
		}
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

	/* Y recien ahora los contadores de la DMA con los que quedo la carga del
	   ejecutable: gdrom_iniciar() acaba de dejar la lectora en cero. Ver
	   BOOT_LOG_BASE. */
	if (boot_log_largo)
		gdrom_dma_contadores(boot_log_fin, boot_log_largo);

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

	R(15) = mem_base + mem_offset + 1024*1024*15 - 4;

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

	// El stub del GD-ROM no es RTS + ranura como los demas: el opcode ilegal
	// va primero y hack_gdrom() fija el PC del retorno el mismo (PC = PR de
	// ordinario). Es lo que permite que el MAINLOOP (r7=2) "llame" al callback
	// PIO del guest como lo hace el gdGdcExecServer real: PC = callback con
	// R4 = su argumento y PR intacto, y el RTS del callback vuelve solo al
	// llamador del MAINLOOP. Con el RTS del stub eso era imposible: rts112
	// fija su destino antes de ejecutar la ranura.
	wvalor = 0xFFFF; /* BIOS_HACK*/		memwrite(HACK_BASE + HACK_GDROM, &wvalor, 2);
	wvalor = 0x0009; /* NOP */			memwrite(HACK_BASE + HACK_GDROM + 2, &wvalor, 2);

	// La entrada fija del GD-ROM y el word de 8C0000C0 que el ROM real deja
	// apuntandola. Windows CE la llama por la direccion, no por el vector;
	// ver SYSCALL_GDROM_FIJO en mem.h.
	dwvalor = HACK_GDROM_FIJO;			memwrite(SYSCALL_GDROM_FIJO, &dwvalor, sizeof(DWORD));
	wvalor = 0xFFFF; /* BIOS_HACK */	memwrite(HACK_GDROM_FIJO, &wvalor, 2);
	wvalor = 0x0009; /* NOP */			memwrite(HACK_GDROM_FIJO + 2, &wvalor, 2);

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

	perf_inicio();
	arm7_perfil_inicio();
	excepcion_sondas_iniciar();
#ifdef DCEMU_BLOQUES
	bloques_iniciar();
#endif
#ifdef DCEMU_FUSION
	fusion_iniciar();
#endif
#ifdef DCEMU_JIT
	jit_iniciar();
#endif
	mmu_sondas_iniciar();

	/* El AICA y el ARM7 a su propio hilo. Va justo antes del bucle: hasta aqui
	   el arranque escribe RAM de onda y registros del AICA sin competencia. */
	hilo_aica_iniciar();

	main_loop();

	/* Y se para antes de guardar nada: el hilo toca RAM de onda. */
	hilo_aica_terminar();

	/* Lo que el guest escribio en la flash -- la fecha, el idioma, los ajustes
	   de juegos -- vuelve al archivo. Sin esto la BIOS pide la hora en cada
	   arranque, porque nunca consigue guardar que ya esta configurada. */
	sistema_flash_guardar();
	sistema_rtc_guardar();
	vmu_guardar();		/* lo que el juego salvo en la tarjeta */
	mando_terminar();

	traza_resumen();
	perf_cuadros_resumen();		/* la distribucion de tiempos de cuadro */
	perf_resumen();
	arm7_perfil_resumen();
#ifdef DCEMU_JIT
	jit_resumen();		/* aca y no por atexit: ver jit.c */
	arm7jit_resumen();
#endif

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

	SDL_Quit( );

	return 0;
}
