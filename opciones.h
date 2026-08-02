/****************************************************************************

	OPCIONES - argumentos de la linea de comandos

	Hasta la fase 1 del arranque por BIOS el unico argumento era el ejecutable
	o la imagen. Ahora hay que poder elegir entre los dos modos de arranque
	(bootstrap de IP.BIN, que es el camino de siempre, y boot ROM real), el
	tipo de cable de video que ve el boot ROM y el estado de la lectora.

	Ver docs/bios-boot-plan.md, fase 1.1.

*****************************************************************************/

#ifndef _OPCIONES_H_
#define _OPCIONES_H_

/* Tipo de cable de video. El valor es el que el boot ROM espera leer en los
   bits 9-8 de PDTRA; no son indices inventados. */
#define CABLE_VGA			0
#define CABLE_RGB			2
#define CABLE_COMPUESTO		3

/* Estado inicial de la lectora. AUTO decide segun haya imagen montada o no. */
#define BANDEJA_AUTO		0
#define BANDEJA_DISCO		1
#define BANDEJA_VACIA		2
#define BANDEJA_ABIERTA		3

/* Cuantos rangos de --desensamblar/--volcar se aceptan. Cuatro alcanzan para
   mirar una rutina, su llamador y las dos tablas que tocan. */
#define RANGOS_MAX			8

struct rango_t
{
	unsigned long	direccion;
	unsigned long	cantidad;		/* instrucciones al desensamblar, bytes al volcar */
};

struct opciones_t
{
	int				arranque_bios;	/* 1: PC = 0xA0000000 en vez del bootstrap */
	int				hacks_bios;		/* 1: parchar los vectores de syscall */
	int				traza_mem;		/* 1: reportar accesos sin emular */

	/* 1: no dejar que el emulador corra mas rapido que una consola. Solo frena,
	   nunca acelera, asi que en las partes donde dcemu ya es mas lento no hace
	   nada. Ver docs/clock-plan.md, fase 4. */
	int				limitar;

	/* Segundos de tiempo *emulado* tras los cuales el emulador sale solo, por
	   el mismo camino que cerrar la ventana. 0: no salir. */
	int				salir_tras;
	/* Si el ejecutable suelto viene cifrado: -1 mirar el prologo, 0 no, 1 si.
	   Solo aplica al argumento .bin; desde una imagen siempre viene cifrado. */
	int				cifrado;

	int				cable;			/* CABLE_* */
	int				bandeja;		/* BANDEJA_* */
	const char *	imagen;			/* .iso/.cue/.bin, o NULL */

	/* Archivo BMP donde volcar lo que GL rasterizo, en cada cuadro. NULL para
	   no hacerlo. Es la verificacion visual que no depende de que la captura de
	   la ventana funcione; ver volcar_gl() en graficos.c. */
	const char *	captura_gl;

	/* El gemelo para el sonido: un .wav con lo que el mezclador del AICA
	   produjo. Va antes que la reproduccion porque es la **medida**: el
	   sonido es lo unico del arbol que no se verifica leyendo un volcado, y
	   "suena bien" no es un dato. Ver audio.h. */
	const char *	captura_audio;

	int				sin_audio;		/* 1: no abrir la tarjeta de sonido */
	int				sin_aica;		/* 1: no emular el AICA en absoluto */

	/* 1: sacar el AICA y el ARM7 a su propio hilo (hilo_aica.c).

	   **Apagado por omision, y no por precaucion sino por medicion**: cuesta
	   entre un 4 y un 5 % de tiempo real en vez de ganarlo. La salida sale bit
	   a bit identica, el protocolo funciona y el SH-4 solo pasa un 0,4 % del
	   tiempo esperando; lo que no cierra es que el mismo trabajo del AICA sale
	   un 46 % mas caro en el segundo hilo y el interprete del primero se
	   frena un 20 %. Ver docs/hilos-plan.md, "Resultado de la fase 1". */
	int				hilos;

	/* Watchpoint de escritura: direccion (0 = apagado) y tamano en bytes.
	   Ver options.h. */
	unsigned long	watchpoint;
	unsigned long	watchpoint_tam;

	/* El gemelo de lectura: quien **mira** esa direccion. */
	unsigned long	watchpoint_lect;
	unsigned long	watchpoint_lect_tam;

	/* Traza de instrucciones: desde donde, cuantas y cuantas llegadas saltar.
	   0 = apagada. */
	unsigned long	traza_desde;
	unsigned long	traza_desde_n;
	unsigned long	traza_desde_salto;

	/* Rangos que se desensamblan y se vuelcan al salir. Son la forma de leer el
	   codigo y las tablas del boot ROM, que viven en RAM y no en el archivo. */
	struct rango_t	desensamblar[RANGOS_MAX];
	int				desensamblar_n;
	struct rango_t	volcar[RANGOS_MAX];
	int				volcar_n;
};

extern struct opciones_t opciones;

/* 0: seguir. 1: error en la linea de comandos. -1: --ayuda, salir sin error. */
int opciones_parsear(int argc, char ** argv);

void opciones_ayuda(const char * programa);

#endif /* _OPCIONES_H_ */
