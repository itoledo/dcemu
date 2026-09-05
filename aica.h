/****************************************************************************

	AICA - el chip de sonido, del lado de los registros

	El AICA es un Yamaha con 64 canales de PCM/ADPCM, un DSP de 128 pasos, un
	ARM7DI propio, un RTC y una interfaz MIDI, todo contra 2 MB de SDRAM de
	onda. Este archivo es el bloque de registros y lo que cuelga de el que no
	es ni el sintetizador ni el ARM: los temporizadores, el controlador de
	interrupciones, el DMA interno y el reset del ARM.

	Referencia: "Dreamcast/Dev.Box System Architecture" (Sega, 99/09/03),
	seccion 8.4.5 para el mapa y 8.1.1 para los algoritmos. Ver
	docs/aica-plan.md.

	**Las dos ventanas no son la misma con otro prefijo.** El SH-4 llega por el
	G2 y el ARM desde adentro, y hay registros que existen en una sola de las
	dos (tabla 4-8 y tabla 8-25):

	  area              desde el G2 (SH-4)        desde el ARM
	  datos de canal    0x00700000-0x007027FF     0x00800000-0x008027FF
	  datos comunes     0x00702800-0x00702FFF     0x00802800-0x00802FFF
	  datos del DSP     0x00703000-0x00707FFF     0x00803000-0x00807FFF
	  RAM de onda       0x00800000-0x009FFFFF     0x00000000-0x001FFFFF

	  ARMRST  (0x2C00)  solo desde el G2   "can only be controlled by the SH4"
	  L, M    (0x2D00)  solo desde el ARM  "can only be controlled by the ARM"

	**El tamano de acceso tampoco es simetrico.** Del lado del SH-4 el papel
	dice "register accesses by the SH4 are 4-byte accesses only, and only the
	lower 16 bits are valid"; el ARM en cambio escribe bytes -- aica.h de KOS
	define CHNREG8 y lo usa para el paneo, el nivel de envio y el volumen. Por
	eso el archivo es direccionable por byte y la restriccion se aplica en la
	entrada del G2, no en el respaldo.

*****************************************************************************/

#ifndef _AICA_H_
#define _AICA_H_

/* El bloque de registros: 32 KB. El mapa util llega hasta 0x45C7 (el ultimo
   dato del DSP) mas 0x2C00 y 0x2D04 sueltos. */
#define AICA_REG_BASE		0x00700000		/* visto desde el G2 */
#define AICA_REG_ARM_BASE	0x00800000		/* visto desde el ARM */
#define AICA_REG_SIZE		0x00008000

#define AICA_ES_REGISTRO(fisica) \
	((fisica) >= AICA_REG_BASE && (fisica) < AICA_REG_BASE + AICA_REG_SIZE)

/* La RAM de onda. 2 MB en una consola de serie; el papel documenta hasta 8 en
   las de desarrollo (MEM8MB), que dcemu no ofrece. */
#define AICA_ONDA_BASE		0x00800000		/* visto desde el G2 */
#define AICA_ONDA_SIZE		0x00200000

/* Canales: 64 ranuras de 0x80 bytes desde el offset 0. */
#define AICA_CANALES		64
#define AICA_CANAL_PASO		0x80

/* Desplazamientos dentro del bloque comun. Son los del mapa, sin inventar. */
#define AICA_MVOL			0x2800	/* MONO, MEM8MB, DAC18B, VER, MVOL */
#define AICA_RBP			0x2804	/* buffer circular del DSP: RBL, RBP */
#define AICA_MIDI_IN		0x2808	/* MIBUF y las cinco banderas del FIFO */
#define AICA_MIDI_OUT		0x280C	/* AFSEL, MSLC, MOBUF */
#define AICA_MONITOR_EG		0x2810	/* LP, SGC, EG del canal elegido por MSLC */
#define AICA_MONITOR_CA		0x2814	/* CA: la muestra por la que va */
#define AICA_DMA_DMEA_ALTO	0x2880	/* DMEA[22:16], TSCD, MRWINH */
#define AICA_DMA_DMEA_BAJO	0x2884	/* DMEA[15:2] */
#define AICA_DMA_DRGA		0x2888	/* DGATE, DRGA[14:2] */
#define AICA_DMA_DLG		0x288C	/* DDIR, DLG[14:2], DEXE */
#define AICA_TIMER_A		0x2890	/* TACTL, TIMA */
#define AICA_TIMER_B		0x2894
#define AICA_TIMER_C		0x2898
#define AICA_SCIEB			0x289C	/* interrupciones al ARM: habilitacion */
#define AICA_SCIPD			0x28A0	/*                       pendientes */
#define AICA_SCIRE			0x28A4	/*                       reconocimiento */
#define AICA_SCILV0			0x28A8	/*                       nivel, bit 0 */
#define AICA_SCILV1			0x28AC	/*                       nivel, bit 1 */
#define AICA_SCILV2			0x28B0	/*                       nivel, bit 2 */
#define AICA_MCIEB			0x28B4	/* interrupciones al SH-4: habilitacion */
#define AICA_MCIPD			0x28B8	/*                        pendientes */
#define AICA_MCIRE			0x28BC	/*                        reconocimiento */
#define AICA_ARMRST			0x2C00	/* solo desde el G2 */
#define AICA_INT_L			0x2D00	/* solo desde el ARM: que interrupcion entro */
#define AICA_INT_M			0x2D04	/* solo desde el ARM: fin de proceso */

/* Los once bits de SCIPD/SCIEB/SCIRE y de MCIPD/MCIEB/MCIRE. */
#define AICA_INT_EXTERNA	(1u << 0)	/* patilla INTN */
#define AICA_INT_MIDI_IN	(1u << 3)
#define AICA_INT_DMA_FIN	(1u << 4)
#define AICA_INT_CPU		(1u << 5)	/* la escribe el otro procesador */
#define AICA_INT_TIMER_A	(1u << 6)
#define AICA_INT_TIMER_B	(1u << 7)
#define AICA_INT_TIMER_C	(1u << 8)
#define AICA_INT_MIDI_OUT	(1u << 9)
#define AICA_INT_MUESTRA	(1u << 10)
#define AICA_INT_TODAS		0x7FFu

/*
	La version del chip, en VER[3:0] de 0x2800. Es un valor que hay que
	contestar y que ninguna medida de este arbol fija: queda anotado como tal,
	porque ya paso dos veces --REVISION del PVR y SB_G1SYSM-- que un registro
	de identificacion contestado a la ligera dejara al guest colgado lejos de
	aqui.
*/
#define AICA_VERSION		1

/* ------------------------------------------------------------------------ */

/* El archivo de registros, direccionable por byte. Lo miran las pruebas. */
extern unsigned char aica_reg[AICA_REG_SIZE];

/* La RAM de onda: 2 MB. La define mem.c en el emulador (sound_mem) y la
   comparten el ARM, el sintetizador y el DMA. */
extern unsigned char * sound_mem;

/*
	Generaciones por pagina de la RAM de onda.

	Es lo que hace posible saltear los barridos de sondeo del ARM7 (ver
	arm7_memo_* en arm7.c y docs/arm7-plan.md): si nadie escribio una pagina
	desde que el ARM la leyo, releerla da lo mismo por construccion.

	**Por pagina y no global, y eso esta medido**: el ARM escribe su propio
	borrador tanto como lo lee --en DCDoom una pagina tiene 33 909 390 lecturas
	y 33 909 391 escrituras, un lee-modifica-escribe sobre la misma posicion--
	asi que un contador unico para los 2 MB estaria sucio siempre y no serviria
	de nada. Las tablas que el sondeo recorre, en cambio, se leen entre 488 y
	59 055 veces por escritura.

	Los tres que escriben RAM de onda tienen que llamar a onda_marcar_escritura():
	el propio ARM (arm7_escribir), el SH-4 y el DMA del G2 --los dos por mem.c--
	y el DMA interno del AICA. Olvidarse de uno no rompe nada visible: deja una
	pagina sucia haciendose pasar por limpia, y el ARM se saltea un barrido que
	debia rehacer. Es la clase de error que este arbol llama "algo que no hace
	nada y no avisa", asi que la baranda es el .wav de --captura-audio.
*/
#define ONDA_PAG_BITS	10							/* paginas de 1 KB */
#define ONDA_PAG		(1u << ONDA_PAG_BITS)
#define ONDA_PAGS		(AICA_ONDA_SIZE >> ONDA_PAG_BITS)

extern unsigned long onda_gen[ONDA_PAGS];

void onda_marcar_escritura_larga(unsigned long dir, unsigned long n);

/*
	Casi todas las escrituras son de 1, 2 o 4 bytes y caen enteras dentro de una
	pagina: eso es un incremento y nada mas. Solo los bloques del DMA cruzan, y
	esos van a la funcion.

	La distincion importa porque esto corre en **cada escritura del ARM** --
	decenas de millones por corrida -- y una llamada por escritura se paga
	tambien cuando la memoizacion esta apagada, o sea que ni siquiera aparece en
	su A/B.
*/
#define onda_marcar_escritura(dir, n)									\
	do {																\
		unsigned long onda_a_ = (unsigned long) (dir)					\
		                        & (AICA_ONDA_SIZE - 1);					\
																		\
		if (((onda_a_ & (ONDA_PAG - 1)) + (unsigned long) (n))			\
		     <= ONDA_PAG)												\
			onda_gen[onda_a_ >> ONDA_PAG_BITS]++;						\
		else															\
			onda_marcar_escritura_larga(onda_a_, (unsigned long) (n));	\
	} while (0)

void aica_reset(void);

/* Accesos desde el SH-4, por el bus G2. La direccion es la fisica completa. */
void aica_leer(unsigned long direccion, void * p, size_t size);
void aica_escribir(unsigned long direccion, void * p, size_t size);

/* Accesos desde el ARM. El offset ya viene relativo al bloque (0..0x7FFF) y
   el tamano es 1, 2 o 4. */
DWORD aica_arm_leer(unsigned long offset, int tam);
void  aica_arm_escribir(unsigned long offset, int tam, DWORD valor);

/*
	Avanza el chip hasta donde llegue reloj_total. No recibe ciclos ni lleva
	acumulador: compara contra su propia marca del contador monotono, que es lo
	que docs/clock-plan.md dejo establecido.

	La cuenta es exacta: 735 muestras cada 3324992 ciclos de CPU, porque
	gcd(44100, DC_CPU_HZ) = 60.
*/
void aica_tick(void);

/*
	Lo mismo pero contra un reloj cualquiera y con un tope de muestras por
	vuelta. Es lo que usa el hilo del AICA (hilo_aica.c), que avanza hasta el
	objetivo que le fijaron -- nunca adelante de reloj_total -- y de a una
	muestra, para que el SH-4 nunca espere mas que eso cuando pide un alcance.

	No descarta nada: avanza la marca solo por lo que hizo. Devuelve el ciclo
	hasta el que efectivamente llego.
*/
unsigned long long aica_tick_hasta(unsigned long long reloj, unsigned tope);

/* En que ciclo cae la muestra numero m, y cuantas lleva hechas el chip. */
unsigned long long aica_reloj_de_muestra(unsigned long long m);
unsigned long long aica_muestras_hechas(void);

/*
	La linea del AICA hacia el ASIC (G2AICINT, bit 1 de SB_ISTEXT). El chip la
	sube y la baja; **la entrega la hace quien atienda el ASIC**, no aqui, para
	que el hilo del AICA no toque ni intc_queuemask_ext ni ASIC_ACK_B. Un
	escritor y un lector, como el anillo aica_salida[].
*/
extern volatile int aica_linea_asic;

/*
	Y cuantas veces la subio y la bajo. Son las que decide la entrega, no el
	nivel, y la diferencia costo una regresion: `intc_add_ext()` **deduplica
	sola** contra la cola de eventos, asi que el codigo original la llamaba en
	cada peticion y eso volvia a encolar el evento cada vez que el guest lo
	consumia. Entregar por flanco del nivel pierde todas las peticiones
	posteriores a la primera mientras la linea sigue alta -- y con eso KOS se
	quedaba esperando en un bucle de tres instrucciones a los 26 ms de arrancar,
	en cualquier demo.

	Contadores monotonos: un escritor cada uno, un lector, y el lector lleva su
	propia marca. Misma disciplina que el anillo aica_salida[].
*/
extern volatile unsigned aica_asic_subidas;
extern volatile unsigned aica_asic_bajadas;

/*
	Y el registro de esos cambios CON SU MUESTRA (2026-09-05), que es lo que
	deja entregar la linea en un instante que sea funcion del tiempo emulado y
	no del reloj real -- el unico punto del hilo del AICA sin determinismo por
	construccion. Ver el comentario largo en aica.c.

	El contrato de orden, sin atomicos: el productor escribe la entrada, luego
	la cabeza, y al terminar la muestra `aica_muestras_listas`; el consumidor
	lee `aica_muestras_listas`, luego la cabeza, luego la entrada. Todo
	volatile para que el compilador no reordene; x86 no reordena almacenes
	entre si ni cargas entre si.

	`aica_demora_linea` es DCEMU_AICA_DEMORA_LINEA en muestras: main_loop()
	aplica en cada servicio los cambios sellados hasta
	muestras_hasta(reloj_total) - demora. Con 0 no se anota nada y se usan los
	contadores de arriba, que es la conducta anterior byte a byte.
*/
#define AICA_LINEA_LOG				4096u

extern int							aica_demora_linea;
extern volatile unsigned long long	aica_muestras_listas;
extern unsigned long long			aica_linea_log_anotadas;
extern unsigned long long			aica_linea_log_perdidas;

/* La cuenta de muestras de un reloj cualquiera, y la de reloj_total memoizada
   por borde (solo desde el hilo principal). */
unsigned long long aica_muestras_de_reloj(unsigned long long reloj);
unsigned long long aica_muestras_al_reloj(void);

/* El primer ciclo en que la cuenta de muestras llega a m (el techo; el piso es
   aica_reloj_de_muestra). Es el borde en que la muestra m pasa a estar
   debida: el hilo del AICA duerme hasta que el objetivo lo cruce. */
unsigned long long aica_primer_reloj_de_muestra(unsigned long long m);

/* Saca el siguiente cambio sellado en `hasta` o antes; 0 si no hay. */
int aica_linea_log_sacar(unsigned long long hasta, int * nivel,
	unsigned long long * muestra);
unsigned aica_linea_log_pendientes(void);
void aica_linea_resumen(void);

/* 1 mientras el ARM esta en reset (ARMRST bit 0). */
int aica_arm_en_reset(void);

/* La linea de FIQ hacia el ARM: 1 si hay una interrupcion habilitada pendiente
   y ninguna en curso. aica_int_tomada() la marca en curso y deja su nivel en
   L; el ARM la libera escribiendo M. */
int  aica_fiq_pendiente(void);
void aica_fiq_tomada(void);

/* Cuantas muestras lleva producidas el chip. Solo para las pruebas y la traza. */
extern unsigned long long aica_muestras;

/* ------------------------------------------------------------------------ */
/* El sintetizador                                                          */
/* ------------------------------------------------------------------------ */

/* Formatos de PCMS[1:0]. */
#define AICA_PCM16			0
#define AICA_PCM8			1
#define AICA_ADPCM			2
#define AICA_ADPCM_LARGO	3

/* Estados de la envolvente, en el orden que reporta SGC. */
#define AICA_EG_ATAQUE		0
#define AICA_EG_DECAY1		1
#define AICA_EG_DECAY2		2
#define AICA_EG_RELEASE		3

/*
	La atenuacion se lleva en las unidades del propio chip: 0x3FF son los 96 dB
	que separan el maximo del silencio, o sea unos 0,09375 dB por unidad. Con
	eso las tres tablas de volumen del papel salen enteras -- 3 dB son 32
	unidades y el peso del bit 0 de TL, 0,4 dB, son algo mas de 4.
*/
#define AICA_ATT_MAX		0x3FF

struct aica_canal
{
	int			activo;			/* sonando o en release */
	int			formato;
	DWORD		pos;			/* muestra en curso, contada desde SA */
	DWORD		frac;			/* fase fraccionaria, 14 bits */
	int			dio_la_vuelta;	/* LP del monitor; lo limpia leerlo */

	/* Decodificador de ADPCM: es de estado, asi que hay que llevarlo. */
	int			adpcm_valor;
	int			adpcm_paso;
	DWORD		adpcm_pos;		/* hasta que muestra esta decodificado */
	int			adpcm_valor_lsa;
	int			adpcm_paso_lsa;

	/* Envolvente de amplitud. El nivel es atenuacion en 16.16. */
	int			eg_estado;
	long		eg_nivel;

	/*
		Filtro FEG: el paso bajo por canal (seccion 8.1.1.7). El nivel es el
		valor de 13 bits del papel en 16.16, con su propia envolvente de cuatro
		estados -- comparte los codigos AICA_EG_* pero no el estado del AEG,
		porque las dos caminan a tasas distintas. prev1/prev2/err son el estado
		del IIR de dos polos; err realimenta el error de truncado de cada
		muestra, que es lo que hace exacta la aritmetica entera.
	*/
	int			feg_activo;
	int			feg_estado;
	long		feg_nivel;
	long		feg_prev1;
	long		feg_prev2;
	long		feg_err;

	/*
		LFO (seccion 8.1.1.5): un contador de fase de 8 bits que avanza cada
		`aica_lfo_recarga[LFOF]` muestras y da la vuelta. De el salen las
		cuatro formas de onda; ALFOS lo mezcla a la atenuacion y PLFOS al
		incremento de fase. Corre libre entre key-on: LFORE es el que lo
		reinicia, no el disparo.
	*/
	int			lfo_estado;
	long		lfo_cuenta;

	int			ultima;			/* la ultima muestra decodificada, con signo */
};

extern struct aica_canal aica_canales[AICA_CANALES];

/*
	Muestras entre dos pasos del contador del LFO, por valor de LFOF. La
	formula viene de la ingenieria inversa (Highly Theoretical) y reproduce
	exactamente la tabla de frecuencias del papel: 1020 en LFOF 0 son
	256 x 1020 muestras por vuelta = 0,169 Hz (el papel dice 0,17), y 1 en
	LFOF 0x1F son 172,3 Hz. Visible para que las pruebas la comparen con la
	tabla.
*/
extern long aica_lfo_recarga[32];

/*
	La salida: un anillo de cuadros estereo de 16 bits que llena el emulador y
	vacia quien reproduzca o vuelque. Es de un productor y un consumidor, y el
	consumidor puede ser el hilo de audio de SDL -- de ahi que los indices sean
	volatiles y que aqui no haya nada de SDL.
*/
#define AICA_SALIDA_CUADROS		8192

extern volatile unsigned aica_salida_cabeza;
extern volatile unsigned aica_salida_cola;
extern short             aica_salida[AICA_SALIDA_CUADROS * 2];

/*
	Cuadros que el anillo tiro porque el consumidor no llegaba a vaciarlo, y
	cuantas veces se lleno.

	**Existe porque descartar en silencio es la forma de falla de este arbol.**
	Sin --limitar el tiempo del guest corre mas rapido que el real, y el AICA
	produce muestras a ese ritmo: a 1,15x son 50 700 por segundo real contra las
	44 100 que consume la tarjeta, o sea que uno de cada ocho cuadros se pierde
	y el sonido se entrecorta. Es correcto tirarlos --pisar lo que el otro hilo
	esta leyendo seria peor-- pero no decirlo convierte "el emulador va rapido"
	en "el sonido de dcemu esta mal", que son dos problemas distintos.

	traza_resumen() los informa al salir, con --traza-mem.
*/
/* El censo del LFO (no emulado): key-on que pidieron modulacion de tono o
   de amplitud. Decide si implementarlo, como decidio con el DSP. */
extern unsigned long aica_censo_plfo;
extern unsigned long aica_censo_alfo;
extern unsigned long aica_censo_feg;
extern unsigned long aica_censo_feg_vals[4];
extern unsigned long aica_key_on;

extern unsigned long long aica_salida_perdidas;
extern unsigned long      aica_salida_llenadas;

/* Saca hasta `cuadros` cuadros al buffer dado. Devuelve cuantos entrego. */
unsigned aica_salida_leer(short * destino, unsigned cuadros);

/* Cuantos cuadros hay esperando. */
unsigned aica_salida_disponible(void);

#endif /* _AICA_H_ */
