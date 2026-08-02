/****************************************************************************

	AICA - ver aica.h.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "main.h"			/* solo por los tipos; no se enlaza nada de SDL */
#include "aica.h"
#include "tmu.h"			/* reloj_total, DC_CPU_HZ */
#include "intc.h"
#include "traza.h"
#include "arm7.h"
#include "opciones.h"
#include "perf.h"

unsigned char		aica_reg[AICA_REG_SIZE];
unsigned long long	aica_muestras;

/*
	La linea del AICA hacia el ASIC (SB_ISTEXT, G2AICINT). El chip solo la sube
	y la baja; **la entrega no se hace aqui**, la cobra quien atienda el ASIC.

	Antes se llamaba a intc_add_ext()/intc_remove_ext() en el acto. Con el AICA
	en su propio hilo (hilo_aica.c) esas llamadas ocurririan fuera del hilo que
	atiende las interrupciones, y tocan tanto intc_queuemask_ext como el
	registro ASIC_ACK_B. Dejar una linea de nivel -- un escritor, un lector, una
	palabra -- es la misma disciplina que ya usa el anillo aica_salida[].

	En el camino de un solo hilo no cambia nada: main_loop() la cobra en el
	mismo bloque periodico, dos lineas despues de aica_tick().
*/
volatile int	aica_linea_asic = 0;

/*
	La relacion exacta entre el reloj de la CPU y el de muestreo. DC_CPU_HZ es
	199499520 y gcd(44100, DC_CPU_HZ) = 60, asi que salen 735 muestras cada
	3324992 ciclos sin resto. Sin deriva y con enteros: la leccion de
	docs/clock-plan.md aplicada al otro reloj.
*/
#define AICA_MUESTRAS_POR_TRAMO		735u
#define AICA_CICLOS_POR_TRAMO		3324992u

/*
	Cuantas muestras se simulan como maximo en una llamada. En marcha normal
	aica_tick() se llama cada 50 ciclos de CPU, o sea cada centesima de
	muestra, y este techo no se toca nunca; existe para que una pausa larga del
	emulador -- un punto de ruptura, una ventana arrastrada -- no se convierta
	en una rafaga de medio millon de instrucciones del ARM.
*/
#define AICA_MUESTRAS_MAX			256u

/* Estado que no vive en el archivo de registros. */
static DWORD				timer_cuenta[3];	/* el contador de 8 bits, vivo */
static unsigned				timer_resto[3];		/* muestras hacia el proximo paso */
static int					int_en_curso;		/* una FIQ tomada y sin M todavia */
static DWORD				int_nivel;			/* lo que contesta L */

/* ------------------------------------------------------------------------ */
/* Acceso al respaldo                                                       */
/* ------------------------------------------------------------------------ */

static DWORD reg16(unsigned long off)
{
	return (DWORD) (aica_reg[off] | (aica_reg[off + 1] << 8));
}

static void poner16(unsigned long off, DWORD v)
{
	aica_reg[off]     = (unsigned char) (v & 0xFF);
	aica_reg[off + 1] = (unsigned char) ((v >> 8) & 0xFF);
}

/* ------------------------------------------------------------------------ */
/* Interrupciones                                                           */
/* ------------------------------------------------------------------------ */

/*
	El nivel de cada fuente sale de tres registros: SCILV2:SCILV1:SCILV0, un
	bit de cada uno en la posicion de la fuente. Las fuentes 8, 9 y 10 usan el
	nivel de la 7, como dice el suplemento de 8.4.5.

	Que la lectura es esta y no otra lo confirma el firmware de KOS: aica_init()
	programa 0x18/0x50/0x08 y el crt0.s compara el numero contra 2 para el
	temporizador. Con esta cuenta la fuente 6 --temporizador A-- da exactamente
	2, y la 3 --entrada MIDI-- da 5, que es el otro numero que el crt0 compara.
*/
static DWORD nivel_de(int fuente)
{
	int b = (fuente > 7) ? 7 : fuente;

	return (((reg16(AICA_SCILV2) >> b) & 1) << 2)
	     | (((reg16(AICA_SCILV1) >> b) & 1) << 1)
	     |  ((reg16(AICA_SCILV0) >> b) & 1);
}

int aica_fiq_pendiente(void)
{
	if (int_en_curso)
		return 0;

	return (reg16(AICA_SCIPD) & reg16(AICA_SCIEB) & AICA_INT_TODAS) != 0;
}

void aica_fiq_tomada(void)
{
	DWORD activas = reg16(AICA_SCIPD) & reg16(AICA_SCIEB) & AICA_INT_TODAS;
	int fuente;

	for (fuente = 0; fuente < 11; fuente++)
		if (activas & (1u << fuente))
			break;

	int_nivel    = (fuente < 11) ? nivel_de(fuente) : 0;
	int_en_curso = 1;
}

/* Deja pendiente una fuente en los dos controladores que la miren. La del
   SH-4 ademas sale por el ASIC: G2AICINT es el bit 1 del registro externo,
   junto al fin de comando de la lectora (ASIC_EVT_SPU_IRQ = 0x0101 en KOS). */
static void pedir_int(DWORD fuente)
{
	poner16(AICA_SCIPD, reg16(AICA_SCIPD) | fuente);
	poner16(AICA_MCIPD, reg16(AICA_MCIPD) | fuente);

	if (reg16(AICA_MCIEB) & fuente)
		aica_linea_asic = 1;
}

/* ------------------------------------------------------------------------ */
/* Temporizadores                                                           */
/* ------------------------------------------------------------------------ */

/*
	Tres contadores de 8 bits que suben y piden interrupcion al pasar de todo
	unos a todo ceros. TxCTL[2:0] dice cada cuantas muestras suben: 1, 2, 4,
	... 128.

	El temporizador A es el latido del firmware de KOS: su FIQ incrementa el
	reloj de milisegundos que vive en 0x00021000 de la RAM de onda, y todo el
	planificador de flujos cuelga de ahi.
*/
static const DWORD int_del_timer[3] =
{
	AICA_INT_TIMER_A, AICA_INT_TIMER_B, AICA_INT_TIMER_C
};

static const unsigned long off_del_timer[3] =
{
	AICA_TIMER_A, AICA_TIMER_B, AICA_TIMER_C
};

static void timers_avanzar(unsigned muestras)
{
	int t;

	for (t = 0; t < 3; t++)
	{
		DWORD    reg  = reg16(off_del_timer[t]);
		unsigned paso = 1u << ((reg >> 8) & 7);
		unsigned n    = muestras + timer_resto[t];

		timer_resto[t] = n % paso;
		n /= paso;

		while (n--)
		{
			timer_cuenta[t] = (timer_cuenta[t] + 1) & 0xFF;

			if (timer_cuenta[t] == 0)
				pedir_int(int_del_timer[t]);
		}

		/* El contador es visible: se lee por el mismo registro donde se
		   escribio la recarga. */
		poner16(off_del_timer[t], (reg & 0x0700) | timer_cuenta[t]);
	}
}

/* ------------------------------------------------------------------------ */
/* DMA interno del AICA                                                     */
/* ------------------------------------------------------------------------ */

/*
	No es el G2-DMA del Holly: es el del propio chip, y mueve entre la RAM de
	onda y su archivo de registros (8.4.5, DMEA/DRGA/DLG/DGATE/DDIR/DEXE).
	Las tres direcciones son de palabra.

	KOS no lo usa. Se emula igual porque esta documentado, cuesta poco, y un
	registro que se acepta sin hacer nada es la forma exacta de los tres
	agujeros que costaron el arranque por BIOS.
*/
static void dma_interno_ejecutar(void)
{
	DWORD onda  = ((reg16(AICA_DMA_DMEA_ALTO) & 0x7F) << 16)
	            |  (reg16(AICA_DMA_DMEA_BAJO) & 0xFFFC);
	DWORD regs  =   reg16(AICA_DMA_DRGA) & 0x7FFC;
	DWORD largo =   reg16(AICA_DMA_DLG)  & 0x7FFC;
	DWORD gate  =   reg16(AICA_DMA_DRGA) & 0x8000;	/* DGATE */
	DWORD dir   =   reg16(AICA_DMA_DLG)  & 0x8000;	/* DDIR */
	DWORD i;

	for (i = 0; i + 4 <= largo; i += 4)
	{
		if (onda + i + 4 > AICA_ONDA_SIZE || regs + i + 4 > AICA_REG_SIZE)
			break;

		if (dir)
		{
			/* 1: de los registros a la RAM de onda. */
			if (gate)
				memset(&sound_mem[onda + i], 0, 4);
			else
				memcpy(&sound_mem[onda + i], &aica_reg[regs + i], 4);
		}
		else
		{
			if (gate)
				memset(&aica_reg[regs + i], 0, 4);
			else
				memcpy(&aica_reg[regs + i], &sound_mem[onda + i], 4);
		}
	}

	/* "The value goes to 0 at DMA end." */
	poner16(AICA_DMA_DLG, reg16(AICA_DMA_DLG) & ~1u);

	pedir_int(AICA_INT_DMA_FIN);
}

/* ------------------------------------------------------------------------ */
/* El sintetizador: 64 canales                                              */
/* ------------------------------------------------------------------------ */

struct aica_canal	aica_canales[AICA_CANALES];

short				aica_salida[AICA_SALIDA_CUADROS * 2];
volatile unsigned	aica_salida_cabeza;
volatile unsigned	aica_salida_cola;

/* Campos de un canal, leidos del archivo de registros. */
#define CAN(c, off)		reg16((unsigned long) (c) * AICA_CANAL_PASO + (off))

/*
	La ganancia lineal de cada nivel de atenuacion. 0x3FF unidades son 96 dB,
	asi que la tabla es 10^(-96 * i / 0x3FF / 20) en punto fijo de 16 bits.
	Se arma una vez porque son mil entradas y la alternativa es un pow() por
	muestra y por canal.
*/
static int ganancia[AICA_ATT_MAX + 1];
static int tablas_listas;

/*
	Los tiempos de cambio de la envolvente, tabla 8-5 del documento: 64 tasas
	efectivas. La primera columna es el ataque --de -96 dB a 0 dB-- y la segunda
	el resto --de 0 dB a -96 dB--. Se copian tal cual: son medidas del chip y no
	salen de ninguna formula.

	Van en **microsegundos** porque el papel llega a decimas de milisegundo en
	las tasas altas (0,93 / 0,65 / 0,35 ms), y en milisegundos enteros esas
	ocho entradas se redondearian a cero -- o sea a instantaneo, que es otra
	cosa.

	Y hay dos valores que no son tiempos:

	  -1  el infinito del papel: la envolvente **no se mueve**. Es lo que dicen
	      las tasas 0 y 1 de las dos columnas, y confundirlo con "instantaneo"
	      hace que un canal recien disparado se apague en la primera muestra.
	      Es exactamente lo que pasaba: `sound-multi-stream` daba un .wav de
	      ocho segundos con pico 14 sobre 32767.
	   0  instantaneo de verdad, que solo aparece en las tasas 62 y 63 del
	      ataque.

	Una correccion: la entrada 31 de la columna de decaimiento figura como
	"90." en el documento, entre 920 y 690. Es una errata evidente --la
	progresion es geometrica y el termino que corresponde es 790-- y se usa el
	valor corregido.
*/
#define EG_INFINITO		(-1L)

static const long eg_us_ataque[64] =
{
	       -1,       -1,  8100000,  6900000,  6000000,  4800000,  4000000,  3400000,
	  3000000,  2400000,  2000000,  1700000,  1500000,  1200000,  1000000,   860000,
	   760000,   600000,   500000,   430000,   380000,   300000,   250000,   220000,
	   190000,   150000,   130000,   110000,    95000,    76000,    63000,    55000,
	    47000,    38000,    31000,    27000,    24000,    19000,    15000,    13000,
	    12000,     9400,     7900,     6800,     6000,     4700,     3800,     3400,
	     3000,     2400,     2000,     1800,     1600,     1300,     1100,      930,
	      850,      650,      530,      440,      400,      350,        0,        0
};

static const long eg_us_decay[64] =
{
	       -1,        -1, 118200000, 101300000,  88600000,  70900000,  59100000,  50700000,
	 44300000,  35500000,  29600000,  25300000,  22200000,  17700000,  14800000,  12700000,
	 11100000,   8900000,   7400000,   6300000,   5500000,   4400000,   3700000,   3200000,
	  2800000,   2200000,   1800000,   1600000,   1400000,   1100000,    920000,    790000,
	   690000,    550000,    460000,    390000,    340000,    270000,    230000,    200000,
	   170000,    140000,    110000,     98000,     85000,     68000,     57000,     49000,
	    43000,     34000,     28000,     25000,     22000,     18000,     14000,     12000,
	    11000,      8500,      7100,      6100,      5400,      4300,      3600,      3100
};

/* Cuanta atenuacion se mueve por muestra, en 16.16. Se precalcula: la
   alternativa es una division en punto flotante por canal y por muestra. */
static long eg_paso_ataque[64];
static long eg_paso_decay[64];

static long paso_desde_us(long us)
{
	if (us == EG_INFINITO)
		return 0;								/* no se mueve */

	if (us <= 0)
		return (long) AICA_ATT_MAX << 16;		/* instantaneo */

	/* 0x3FF unidades en `us` microsegundos, a 44100 muestras por segundo. */
	return (long) ((double) AICA_ATT_MAX * 65536.0 * 1000000.0
	               / ((double) us * 44100.0));
}

static void armar_tablas(void)
{
	int i;

	if (tablas_listas)
		return;

	/* 0x10000 es ganancia 1. Se calcula sin pow(): cada unidad es un factor
	   constante, y multiplicar acumulado da la misma curva. */
	{
		double g = 1.0;
		double paso = 0.9892796;	/* 10^(-96/1023/20) */

		for (i = 0; i <= AICA_ATT_MAX; i++)
		{
			ganancia[i] = (int) (g * 65536.0);
			g *= paso;
		}

		ganancia[AICA_ATT_MAX] = 0;
	}

	for (i = 0; i < 64; i++)
	{
		eg_paso_ataque[i] = paso_desde_us(eg_us_ataque[i]);
		eg_paso_decay[i]  = paso_desde_us(eg_us_decay[i]);
	}

	tablas_listas = 1;
}

/*
	La tasa efectiva del papel: (KRS + OCT) x 2 + FNS[9] + tasa x 2. OCT viene
	en complemento a dos de cuatro bits y KRS = 0xF apaga el escalado.
*/
static int tasa_efectiva(int canal, int tasa)
{
	DWORD r14 = CAN(canal, 0x14);
	DWORD r18 = CAN(canal, 0x18);
	int   krs = (int) ((r14 >> 10) & 0xF);
	int   oct = (int) ((r18 >> 11) & 0xF);
	int   fns9 = (int) ((r18 >> 9) & 1);
	int   e;

	if (oct > 7)
		oct -= 16;

	e = (krs == 0xF) ? tasa * 2 : (krs + oct) * 2 + fns9 + tasa * 2;

	if (e < 0)		e = 0;
	if (e > 63)		e = 63;

	return e;
}

static void eg_avanzar(int canal, struct aica_canal * c)
{
	DWORD r10 = CAN(canal, 0x10);
	DWORD r14 = CAN(canal, 0x14);
	int   ar  = (int) (r10 & 0x1F);
	int   d1r = (int) ((r10 >> 6) & 0x1F);
	int   d2r = (int) ((r10 >> 11) & 0x1F);
	int   rr  = (int) (r14 & 0x1F);
	int   dl  = (int) ((r14 >> 5) & 0x1F);

	switch (c->eg_estado)
	{
	case AICA_EG_ATAQUE:
		c->eg_nivel -= eg_paso_ataque[tasa_efectiva(canal, ar)];

		if (c->eg_nivel <= 0)
		{
			c->eg_nivel  = 0;
			c->eg_estado = AICA_EG_DECAY1;
		}
		break;

	case AICA_EG_DECAY1:
		c->eg_nivel += eg_paso_decay[tasa_efectiva(canal, d1r)];

		/* DL son los cinco bits altos del codigo de la envolvente. */
		if ((c->eg_nivel >> 16) >= (dl << 5))
			c->eg_estado = AICA_EG_DECAY2;
		break;

	case AICA_EG_DECAY2:
		c->eg_nivel += eg_paso_decay[tasa_efectiva(canal, d2r)];
		break;

	default:
		c->eg_nivel += eg_paso_decay[tasa_efectiva(canal, rr)];
		break;
	}

	if (c->eg_nivel > ((long) AICA_ATT_MAX << 16))
		c->eg_nivel = (long) AICA_ATT_MAX << 16;

	/* Llegar al silencio en release apaga el canal de verdad. */
	if (c->eg_estado == AICA_EG_RELEASE
	&&  (c->eg_nivel >> 16) >= AICA_ATT_MAX)
		c->activo = 0;
}

/* ------------------------------------------------------------------------ */

/*
	ADPCM de Yamaha, seccion 8.1.1.2 del documento, tal cual:

	  X(n)   = (1 - 2*L4) * (L3 + L2/2 + L1/4 + 1/8) * D(n) + X(n-1)
	  D(n+1) = f(L3,L2,L1) * D(n)

	Los ocho factores de la tabla 8-4 son exactos en 256avos --230, 230, 230,
	230, 307, 409, 512 y 614-- asi que todo se hace con enteros y dos corridas
	dan el mismo resultado bit a bit. D arranca en 127, no baja de 127 y no
	pasa de 24576.
*/
static const int adpcm_factor[8] =
{
	230, 230, 230, 230, 307, 409, 512, 614
};

#define ADPCM_PASO_MIN		127
#define ADPCM_PASO_MAX		24576

static void adpcm_decodificar(struct aica_canal * c, int nibble)
{
	int codigo = nibble & 7;
	int signo  = nibble & 8;
	int delta  = (c->adpcm_paso * ((codigo << 1) | 1)) >> 3;

	c->adpcm_valor += signo ? -delta : delta;

	if (c->adpcm_valor >  32767)	c->adpcm_valor =  32767;
	if (c->adpcm_valor < -32768)	c->adpcm_valor = -32768;

	c->adpcm_paso = (c->adpcm_paso * adpcm_factor[codigo]) >> 8;

	if (c->adpcm_paso < ADPCM_PASO_MIN)		c->adpcm_paso = ADPCM_PASO_MIN;
	if (c->adpcm_paso > ADPCM_PASO_MAX)		c->adpcm_paso = ADPCM_PASO_MAX;
}

/* La muestra numero `n` contada desde SA, con signo, en 16 bits. */
static int leer_muestra(int canal, struct aica_canal * c, DWORD sa, DWORD n)
{
	switch (c->formato)
	{
	case AICA_PCM8:
		return (int) (signed char) sound_mem[(sa + n) & (AICA_ONDA_SIZE - 1)]
		       * 256;

	case AICA_ADPCM:
	case AICA_ADPCM_LARGO:
		{
			/*
				El ADPCM es de estado: para dar la muestra n hay que haber
				decodificado todas las anteriores. Solo se avanza hacia
				adelante; un salto hacia atras --el bucle-- lo resuelve
				canal_bucle() restaurando el estado guardado en LSA.
			*/
			while (c->adpcm_pos <= n)
			{
				DWORD b = (sa + (c->adpcm_pos >> 1)) & (AICA_ONDA_SIZE - 1);
				int   v = sound_mem[b];

				adpcm_decodificar(c, (c->adpcm_pos & 1) ? (v >> 4) : (v & 0xF));
				c->adpcm_pos++;

				/* El estado justo al entrar al bucle se guarda para poder
				   volver a el sin redecodificar desde SA. */
				{
					DWORD lsa = CAN(canal, 0x08);

					if (c->adpcm_pos == lsa)
					{
						c->adpcm_valor_lsa = c->adpcm_valor;
						c->adpcm_paso_lsa  = c->adpcm_paso;
					}
				}
			}

			return c->adpcm_valor;
		}

	default:
		{
			DWORD b = (sa + n * 2) & (AICA_ONDA_SIZE - 1);

			return (int) (short) (sound_mem[b] | (sound_mem[b + 1] << 8));
		}
	}
}

/* Cuantos canales llegaron a sonar alguna vez, para el resumen de la traza. */
unsigned long aica_key_on;

/* Arranca un canal: KEY ON. */
static void canal_encender(int canal)
{
	struct aica_canal * c = &aica_canales[canal];
	DWORD r0 = CAN(canal, 0x00);

	aica_key_on++;

	if (traza_activa)
	{
		static const char * nombre[4] =
			{ "PCM16", "PCM8", "ADPCM", "ADPCM largo" };
		DWORD r18 = CAN(canal, 0x18);
		int   oct = (int) ((r18 >> 11) & 0xF);

		if (oct > 7)
			oct -= 16;

		fprintf(stderr, "traza: AICA key on canal %2d: %-11s SA %06lx "
			"LSA %04lx LEA %04lx OCT %+d FNS %03lx TL %02lx DISDL %lx "
			"DIPAN %02lx%s\n",
			canal, nombre[(r0 >> 7) & 3],
			(unsigned long) (((r0 & 0x7F) << 16) | CAN(canal, 0x04)),
			(unsigned long) CAN(canal, 0x08),
			(unsigned long) CAN(canal, 0x0C),
			oct, (unsigned long) (r18 & 0x3FF),
			(unsigned long) ((CAN(canal, 0x28) >> 8) & 0xFF),
			(unsigned long) ((CAN(canal, 0x24) >> 8) & 0xF),
			(unsigned long) (CAN(canal, 0x24) & 0x1F),
			(r0 & 0x200) ? " bucle" : "");
	}

	c->activo    = 1;
	c->formato   = (int) ((r0 >> 7) & 3);
	c->pos       = 0;
	c->frac      = 0;
	c->dio_la_vuelta = 0;

	c->adpcm_valor = 0;
	c->adpcm_paso  = ADPCM_PASO_MIN;
	c->adpcm_pos   = 0;
	c->adpcm_valor_lsa = 0;
	c->adpcm_paso_lsa  = ADPCM_PASO_MIN;

	c->eg_estado = AICA_EG_ATAQUE;
	c->eg_nivel  = (long) AICA_ATT_MAX << 16;
	c->ultima    = 0;
}

/* KEY OFF: no corta, pasa a release. */
static void canal_apagar(int canal)
{
	struct aica_canal * c = &aica_canales[canal];

	if (c->activo && c->eg_estado != AICA_EG_RELEASE)
		c->eg_estado = AICA_EG_RELEASE;
}

/*
	KYONEX: "Writing 1 to this register executes KEY_ON, OFF for all slots."
	O sea que el disparo es global y lo que decide canal por canal es KYONB.
	Escribirlo en un solo canal arranca todos los que tengan KYONB puesto, que
	es como el firmware de KOS sincroniza varias voces (aica_sync_play).
*/
static void aplicar_key_on(void)
{
	int i;

	for (i = 0; i < AICA_CANALES; i++)
	{
		int quiere = (CAN(i, 0x00) & 0x4000) != 0;	/* KYONB */

		if (quiere && !aica_canales[i].activo)
			canal_encender(i);
		else
		if (!quiere)
			canal_apagar(i);
	}
}

/*
	Un paso de un canal: avanza la fase, resuelve el bucle y devuelve la
	muestra ya escalada por su envolvente, su TL y su nivel de envio directo.
	El paneo lo aplica el mezclador.
*/
static int canal_muestrear(int canal, int * izq, int * der)
{
	struct aica_canal * c = &aica_canales[canal];
	DWORD r0, sa, lsa, lea;
	DWORD inc;
	int   oct, fns;
	int   muestra;
	long  att;
	DWORD r24, r28;
	int   disdl, dipan;
	int   g;

	*izq = *der = 0;

	if (!c->activo)
		return 0;

	/*
		La envolvente avanza **antes** de que la muestra se use, no despues.
		Puesta al reves, la primera muestra de cada canal sale con la
		atenuacion del reposo -- o sea muda -- y eso se nota: con un ataque
		instantaneo, que es lo que pide el firmware de KOS, el chip ya esta a
		volumen pleno en la primera muestra.
	*/
	eg_avanzar(canal, c);

	if (!c->activo)
		return 0;

	r0  = CAN(canal, 0x00);
	sa  = ((r0 & 0x7F) << 16) | CAN(canal, 0x04);
	lsa = CAN(canal, 0x08);
	lea = CAN(canal, 0x0C);

	/* SSCTL = 1 pide ruido en vez de memoria. */
	if (r0 & 0x400)
		muestra = ((int) (aica_muestras * 1103515245u + 12345u) >> 16) & 0xFFFF;
	else
		muestra = leer_muestra(canal, c, sa, c->pos);

	c->ultima = muestra;

	/* Envolvente, TL y nivel de envio, todo en unidades de atenuacion. */
	r24   = CAN(canal, 0x24);
	r28   = CAN(canal, 0x28);
	disdl = (int) ((r24 >> 8) & 0xF);
	dipan = (int) (r24 & 0x1F);

	att = (c->eg_nivel >> 16) + (long) ((r28 >> 8) & 0xFF) * 4;

	/* DISDL: 0 es silencio, 0xF es 0 dB, 3 dB --32 unidades-- por escalon. */
	if (disdl == 0)
		att = AICA_ATT_MAX;
	else
		att += (15 - disdl) * 32;

	if (att > AICA_ATT_MAX)		att = AICA_ATT_MAX;
	if (att < 0)				att = 0;

	g = ganancia[att];

	/* DIPAN: el bit 4 dice que lado se atenua y los otros cuatro cuanto. */
	{
		int lado = dipan & 0x10;
		int n    = dipan & 0x0F;
		long extra = (n == 0xF) ? AICA_ATT_MAX : (long) n * 32;
		long a2 = att + extra;

		if (a2 > AICA_ATT_MAX)	a2 = AICA_ATT_MAX;

		if (n == 0)
		{
			*izq = (muestra * g) >> 16;
			*der = *izq;
		}
		else
		if (lado)
		{
			*izq = (muestra * g) >> 16;
			*der = (muestra * ganancia[a2]) >> 16;
		}
		else
		{
			*izq = (muestra * ganancia[a2]) >> 16;
			*der = (muestra * g) >> 16;
		}
	}

	/* Y ahora la fase. inc = 2^OCT * (1 + FNS/1024), con 14 bits de
	   fraccion; OCT viene en complemento a dos. */
	{
		DWORD r18 = CAN(canal, 0x18);

		oct = (int) ((r18 >> 11) & 0xF);
		fns = (int) (r18 & 0x3FF);

		if (oct > 7)
			oct -= 16;

		inc = (DWORD) ((1024 + fns) << 4);

		if (oct > 0)
			inc <<= oct;
		else
		if (oct < 0)
			inc >>= -oct;
	}

	c->frac += inc;
	c->pos  += c->frac >> 14;
	c->frac &= 0x3FFF;

	/* Bucle: LPCTL dice si al llegar a LEA se vuelve a LSA o se termina. */
	if (c->pos > lea)
	{
		if (r0 & 0x200)					/* LPCTL */
		{
			c->pos = lsa + (c->pos - lea - 1);
			c->dio_la_vuelta = 1;

			/* El ADPCM vuelve al estado que tenia al cruzar LSA; sin eso el
			   bucle suena a ruido creciente, porque el decodificador es de
			   estado y no se puede rebobinar. */
			if (c->formato == AICA_ADPCM || c->formato == AICA_ADPCM_LARGO)
			{
				c->adpcm_valor = c->adpcm_valor_lsa;
				c->adpcm_paso  = c->adpcm_paso_lsa;
				c->adpcm_pos   = lsa;
			}
		}
		else
		{
			/* "once LEA is reached, processing ends" -- pero la muestra de LEA
			   ya se leyo arriba y es valida: el canal termina **despues** de
			   entregarla, no en vez de entregarla. */
			c->activo = 0;
		}
	}

	return 1;
}

/*
	Una muestra estereo: los 64 canales sumados y pasados por el volumen
	maestro. MONO (bit 15 de 0x2800) desactiva el paneo, y el papel advierte
	que entonces hay que bajar MVOL porque el volumen se duplica.
*/
static void mezclar_una_muestra(void)
{
	long izq = 0, der = 0;
	int  i;
	DWORD r2800 = reg16(AICA_MVOL);
	int   mvol  = (int) (r2800 & 0xF);
	unsigned proxima;

	for (i = 0; i < AICA_CANALES; i++)
	{
		int l, r;

		if (canal_muestrear(i, &l, &r))
		{
			izq += l;
			der += r;
		}
	}

	if (r2800 & 0x8000)						/* MONO */
	{
		long m = (izq + der) / 2;

		izq = der = m;
	}

	/* MVOL: 0 es silencio, 0xF es 0 dB, 3 dB por escalon. */
	if (mvol == 0)
		izq = der = 0;
	else
	if (mvol != 0xF)
	{
		int g = ganancia[(15 - mvol) * 32];

		izq = (izq * g) >> 16;
		der = (der * g) >> 16;
	}

	if (izq >  32767)	izq =  32767;
	if (izq < -32768)	izq = -32768;
	if (der >  32767)	der =  32767;
	if (der < -32768)	der = -32768;

	/* Al anillo. Si el consumidor no vacia se descarta lo nuevo: perder audio
	   es mejor que pisar lo que el otro hilo esta leyendo. */
	proxima = (aica_salida_cabeza + 1) % AICA_SALIDA_CUADROS;

	if (proxima == aica_salida_cola)
		return;

	aica_salida[aica_salida_cabeza * 2]     = (short) izq;
	aica_salida[aica_salida_cabeza * 2 + 1] = (short) der;
	aica_salida_cabeza = proxima;
}

unsigned aica_salida_disponible(void)
{
	unsigned cabeza = aica_salida_cabeza;
	unsigned cola   = aica_salida_cola;

	return (cabeza >= cola) ? (cabeza - cola)
	                        : (AICA_SALIDA_CUADROS - cola + cabeza);
}

unsigned aica_salida_leer(short * destino, unsigned cuadros)
{
	unsigned hay = aica_salida_disponible();
	unsigned n, i;
	unsigned cola = aica_salida_cola;

	n = (cuadros < hay) ? cuadros : hay;

	for (i = 0; i < n; i++)
	{
		destino[i * 2]     = aica_salida[cola * 2];
		destino[i * 2 + 1] = aica_salida[cola * 2 + 1];
		cola = (cola + 1) % AICA_SALIDA_CUADROS;
	}

	aica_salida_cola = cola;

	return n;
}

/* ------------------------------------------------------------------------ */
/* Escritura de un registro                                                 */
/* ------------------------------------------------------------------------ */

/*
	El unico sitio donde una escritura hace algo mas que quedar guardada. Lo
	comparten las dos ventanas; `del_arm` distingue los registros que el papel
	reserva a uno de los dos procesadores.
*/
static void escribir_registro(unsigned long off, DWORD valor, int del_arm)
{
	switch (off)
	{
	case AICA_SCIPD:
	case AICA_MCIPD:
		/* Solo el bit 5 se escribe, y solo con 1: es la interrupcion que un
		   procesador le manda al otro. "If a 0 is written, it is invalid." */
		if (valor & AICA_INT_CPU)
		{
			poner16(off, reg16(off) | AICA_INT_CPU);

			if (off == AICA_MCIPD && (reg16(AICA_MCIEB) & AICA_INT_CPU))
				aica_linea_asic = 1;
		}
		return;

	case AICA_SCIRE:
		poner16(AICA_SCIPD, reg16(AICA_SCIPD) & ~valor);
		return;

	case AICA_MCIRE:
		poner16(AICA_MCIPD, reg16(AICA_MCIPD) & ~valor);

		/* Reconocer del todo baja la linea hacia el SH-4. */
		if (!(reg16(AICA_MCIPD) & reg16(AICA_MCIEB) & AICA_INT_TODAS))
			aica_linea_asic = 0;
		return;

	case AICA_ARMRST:
		/* "This register can only be controlled by the system (SH4)." */
		if (del_arm)
			return;

		if (traza_activa && ((reg16(off) ^ valor) & 1))
			fprintf(stderr, "traza: AICA: el ARM %s (ARMRST=%lu) a los %llu ms\n",
				(valor & 1) ? "entra en reset" : "arranca",
				(unsigned long) (valor & 1), reloj_ms());

		/* Soltar el reset arranca el nucleo desde el vector 0, que es lo que
		   hace spu_enable() de KOS despues de subir el firmware. Reiniciar
		   aqui y no en el arranque del emulador es lo que permite que el
		   guest cargue otro programa y lo vuelva a lanzar. */
		if ((reg16(off) & 1) && !(valor & 1))
			arm7_reset();

		poner16(off, valor & 1);
		return;

	case AICA_INT_M:
		/* Fin de proceso de la interrupcion. Solo el ARM. */
		if (!del_arm)
			return;

		if (valor & 1)
			int_en_curso = 0;
		return;

	case AICA_INT_L:
		return;						/* solo lectura */

	case AICA_TIMER_A:
	case AICA_TIMER_B:
	case AICA_TIMER_C:
		{
			int t = (off == AICA_TIMER_A) ? 0 : (off == AICA_TIMER_B) ? 1 : 2;

			timer_cuenta[t] = valor & 0xFF;
			timer_resto[t]  = 0;
			poner16(off, valor & 0x07FF);
		}
		return;

	case AICA_DMA_DLG:
		poner16(off, valor);

		/* DEXE: escribir 1 arranca; escribir 0 no hace nada. */
		if (valor & 1)
			dma_interno_ejecutar();
		return;

	default:
		poner16(off, valor);

		/* El registro 0 de un canal: KYONEX dispara el KEY ON/OFF de **todos**
		   los canales a la vez, no solo del suyo. */
		if (off < AICA_CANALES * AICA_CANAL_PASO
		&&  (off % AICA_CANAL_PASO) == 0
		&&  (valor & 0x8000))
			aplicar_key_on();
		return;
	}
}

/*
	Un registro es "vivo" si su valor depende del reloj del AICA. Solo esos
	obligarian a sincronizar en el diseno de docs/hilos-plan.md; el resto sale
	de respaldo plano y se puede contestar sin esperar a nadie. La distincion
	es la mitad de la medida del paso 0.
*/
static int registro_vivo(unsigned long off)
{
	switch (off)
	{
	case AICA_MONITOR_EG:		/* ademas limpia dio_la_vuelta al leerlo */
	case AICA_MONITOR_CA:
	case AICA_TIMER_A:
	case AICA_TIMER_B:
	case AICA_TIMER_C:
	case AICA_SCIPD:
	case AICA_MCIPD:
		return 1;
	}

	return 0;
}

static DWORD leer_registro(unsigned long off, int del_arm)
{
	if (!del_arm && perf_activa)
	{
		if (registro_vivo(off))
		{
			perf_aica_reg_vivo++;
			perf_marcar_sync();
		}
		else
			perf_aica_reg_plano++;
	}

	switch (off)
	{
	/*
		Los dos registros de monitoreo. MSLC elige el canal y detras se leen
		su estado de envolvente, su nivel, si dio la vuelta el bucle y por que
		muestra va. aica_get_pos() del firmware de KOS lee el segundo, y el
		planificador de flujos decide con eso cuando rellenar el buffer: sin
		esto la reproduccion se traba o se pisa.
	*/
	case AICA_MONITOR_EG:
		{
			int canal = (int) ((reg16(AICA_MIDI_OUT) >> 8) & 0x3F);
			struct aica_canal * c = &aica_canales[canal];
			DWORD v = (DWORD) ((c->eg_estado & 3) << 13)
			        | (DWORD) ((c->eg_nivel >> 16) & 0x1FFF);

			if (c->dio_la_vuelta)
			{
				v |= 0x8000;
				c->dio_la_vuelta = 0;		/* leerlo lo limpia */
			}

			return v;
		}

	case AICA_MONITOR_CA:
		{
			int canal = (int) ((reg16(AICA_MIDI_OUT) >> 8) & 0x3F);

			return aica_canales[canal].pos & 0xFFFF;
		}

	case AICA_MVOL:
		/* VER[3:0] es de solo lectura y va encima de lo que el guest dejo. */
		return (reg16(off) & ~0x0F00u) | ((AICA_VERSION & 0xF) << 8);

	case AICA_ARMRST:
		return del_arm ? 0 : reg16(off);

	case AICA_INT_L:
		return del_arm ? int_nivel : 0;

	case AICA_INT_M:
		return 0;

	default:
		return reg16(off);
	}
}

/* ------------------------------------------------------------------------ */
/* Las dos ventanas                                                         */
/* ------------------------------------------------------------------------ */

/*
	Desde el SH-4. "Register accesses by the SH4 are 4-byte accesses only, and
	only the lower 16 bits are valid": se atiende lo que pidan, pero el dato
	util son 16 bits y la direccion se alinea, que es lo que el chip hace.
*/
void aica_leer(unsigned long direccion, void * p, size_t size)
{
	unsigned long off = (direccion & 0x00FFFFFF) - AICA_REG_BASE;
	DWORD dw = leer_registro(off & ~3u, 0);

	/* Un acceso de menos de cuatro bytes esta fuera de lo que el papel
	   permite al SH-4, pero si llega hay que contestar el byte que pidio y no
	   el bajo de la palabra. */
	if (size < 4)
		dw >>= (off & 3) * 8;

	memcpy(p, &dw, size > sizeof(dw) ? sizeof(dw) : size);
}

void aica_escribir(unsigned long direccion, void * p, size_t size)
{
	unsigned long off = (direccion & 0x00FFFFFF) - AICA_REG_BASE;
	DWORD dw = 0;

	memcpy(&dw, p, size > sizeof(dw) ? sizeof(dw) : size);

	if (size < 4)
	{
		int desp = (int) ((off & 3) * 8);

		if (desp >= 16)					/* la mitad alta no existe */
			return;

		dw = (reg16(off & ~3u) & ~((0xFFFFFFFFu >> (32 - size * 8)) << desp))
		   | ((dw & (0xFFFFFFFFu >> (32 - size * 8))) << desp);
	}

	escribir_registro(off & ~3u, dw & 0xFFFF, 0);
}

/*
	Desde el ARM. Aqui si hay accesos de byte: aica.c del firmware de KOS
	escribe el paneo en +0x24, el nivel de envio en +0x25 y el volumen en
	+0x29, uno a uno.
*/
DWORD aica_arm_leer(unsigned long offset, int tam)
{
	unsigned long off = offset & (AICA_REG_SIZE - 1);
	DWORD palabra = leer_registro(off & ~3u, 1);

	switch (tam)
	{
	case 1:		return (palabra >> ((off & 3) * 8)) & 0xFF;
	case 2:		return (off & 2) ? (palabra >> 16) : (palabra & 0xFFFF);
	default:	return palabra;
	}
}

void aica_arm_escribir(unsigned long offset, int tam, DWORD valor)
{
	unsigned long off = offset & (AICA_REG_SIZE - 1);
	unsigned long base = off & ~3u;
	DWORD palabra = reg16(base);

	switch (tam)
	{
	case 1:
		{
			int desp = (int) ((off & 3) * 8);

			if (desp >= 16)			/* la mitad alta no existe */
				return;

			palabra = (palabra & ~(0xFFu << desp))
			        | ((valor & 0xFF) << desp);
		}
		break;

	case 2:
		if (off & 2)
			return;
		palabra = valor & 0xFFFF;
		break;

	default:
		palabra = valor & 0xFFFF;
		break;
	}

	escribir_registro(base, palabra, 1);
}

/* ------------------------------------------------------------------------ */
/* El paso del tiempo                                                       */
/* ------------------------------------------------------------------------ */

/*
	Cuantas muestras van desde el encendido hasta donde este reloj_total. Se
	divide primero y se multiplica despues sobre el resto: multiplicar directo
	desbordaria un entero de 64 bits, aunque recien a los cuatro anos de tiempo
	emulado.
*/
static unsigned long long muestras_hasta(unsigned long long reloj)
{
	return reloj / AICA_CICLOS_POR_TRAMO * AICA_MUESTRAS_POR_TRAMO
	     + (reloj % AICA_CICLOS_POR_TRAMO) * AICA_MUESTRAS_POR_TRAMO
	       / AICA_CICLOS_POR_TRAMO;
}

static unsigned long long muestras_hasta_ahora(void)
{
	return muestras_hasta(reloj_total);
}

/* La inversa: en que ciclo cae la muestra numero m. La necesita el hilo del
   AICA para decir hasta donde llego cuando avanza de a una. */
unsigned long long aica_reloj_de_muestra(unsigned long long m)
{
	return m / AICA_MUESTRAS_POR_TRAMO * AICA_CICLOS_POR_TRAMO
	     + (m % AICA_MUESTRAS_POR_TRAMO) * AICA_CICLOS_POR_TRAMO
	       / AICA_MUESTRAS_POR_TRAMO;
}

unsigned long long aica_muestras_hechas(void)
{
	return aica_muestras;
}

/*
	El cuerpo, contra un reloj cualquiera y con un tope de muestras por vuelta.

	El reloj es parametro porque el hilo del AICA no avanza hasta reloj_total
	sino hasta el objetivo que le fijaron, que nunca va adelante de reloj_total.
	Y el tope es parametro porque ese hilo avanza de a una muestra: asi el SH-4
	nunca espera mas que una muestra cuando pide un alcance.

	A diferencia de aica_tick(), **no descarta nada**: avanza la marca solo por
	lo que efectivamente hizo. Descartar es lo correcto cuando el emulador
	estuvo detenido, no cuando alguien pide de a poco.
*/
unsigned long long aica_tick_hasta(unsigned long long reloj, unsigned tope)
{
	unsigned long long objetivo = muestras_hasta(reloj);
	unsigned long long faltan;

	if (opciones.sin_aica)
	{
		aica_muestras = objetivo;
		return reloj;
	}

	if (objetivo <= aica_muestras)
		return reloj;

	faltan = objetivo - aica_muestras;

	if (faltan > tope)
		faltan = tope;

	aica_muestras += faltan;

	timers_avanzar((unsigned) faltan);

	{
		unsigned long long i;
		PERF_MARCA(t0);

		for (i = 0; i < faltan; i++)
			mezclar_una_muestra();

		PERF_SUMAR(t0, perf_ns_aica);
	}

	/*
		Y el ARM. Su reloj no necesita cuenta aparte: 22579200 / 44100 = 512
		exacto, asi que son 512 ciclos por muestra y punto.
	*/
	{
		PERF_MARCA(t1);

		arm7_ejecutar((long) faltan * 512);

		PERF_SUMAR(t1, perf_ns_arm);
	}

	return aica_reloj_de_muestra(aica_muestras);
}

/*
	El camino de siempre, de un solo hilo: avanzar hasta reloj_total.

	Conserva la semantica que tenia: si quedo muy atras --el emulador estuvo
	detenido en el depurador-- se descarta lo que no se va a simular en vez de
	acumular deuda, porque si no el chip se queda atras para siempre. Descartar
	primero y despues avanzar es exactamente lo que hacia poner la marca en el
	objetivo antes de acotar `faltan`.
*/
void aica_tick(void)
{
	if (!opciones.sin_aica)
	{
		unsigned long long objetivo = muestras_hasta_ahora();

		if (objetivo > aica_muestras
		 && objetivo - aica_muestras > AICA_MUESTRAS_MAX)
			aica_muestras = objetivo - AICA_MUESTRAS_MAX;
	}

	aica_tick_hasta(reloj_total, AICA_MUESTRAS_MAX);
}

int aica_arm_en_reset(void)
{
	return (reg16(AICA_ARMRST) & 1) != 0;
}

void aica_reset(void)
{
	armar_tablas();

	memset(aica_reg, 0, sizeof(aica_reg));
	memset(timer_cuenta, 0, sizeof(timer_cuenta));
	memset(timer_resto, 0, sizeof(timer_resto));
	memset(aica_canales, 0, sizeof(aica_canales));

	aica_salida_cabeza = 0;
	aica_salida_cola   = 0;

	int_en_curso  = 0;
	int_nivel     = 0;

	/* La marca arranca donde este el reloj, no en cero: un reset en caliente
	   con reloj_total ya avanzado produciria si no una rafaga de todas las
	   muestras transcurridas desde el encendido. */
	aica_muestras = muestras_hasta_ahora();

	/* El ARM arranca detenido: KOS lo suelta escribiendo 0 en ARMRST, y hasta
	   entonces la RAM de onda es suya para cargarle el firmware. */
	poner16(AICA_ARMRST, 1);

	/* Los dos FIFO de MIDI arrancan vacios (MIEMP y MOEMP). */
	poner16(AICA_MIDI_IN, 0x0900);
}
