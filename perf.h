/****************************************************************************

	PERF - el desglose de en que se va el tiempo real

	Existe para contestar la pregunta del paso 0 de docs/hilos-plan.md: cuanto
	vale de verdad sacar el AICA y el ARM7 a otro hilo, y con que frecuencia el
	SH-4 tocaria su estado -- que es lo que en ese diseno obliga a sincronizar y
	por lo tanto el techo del paralelismo.

	**Se mide adentro de una sola corrida, no comparando dos.** dcemu nunca fija
	el intervalo de intercambio, asi que el vsync lo decide el driver: comparar
	una corrida normal contra una con --sin-aica queda a merced de un tope de 60
	que puede tapar justo la diferencia que se busca. Un desglose interno es
	inmune a eso.

	Apagado cuesta una comparacion contra cero por punto de medida, la misma
	clase que el watchpoint. Los relojes se toman donde el llamado es caro y
	poco frecuente -- la mezcla de una muestra ocurre 44100 veces por segundo,
	no cuatro millones -- para que el instrumento no sea el que se mide.

	Sin SDL a proposito, como aica.c y vram.c, para que tests/ lo enlace.

*****************************************************************************/

#ifndef _PERF_H_
#define _PERF_H_

/* --perf. Apagado en cero. */
extern int perf_activa;

/* Reloj monotono del anfitrion, en nanosegundos. */
unsigned long long perf_ahora(void);

/* Acumuladores de tiempo real, en nanosegundos. */
extern unsigned long long perf_ns_aica;			/* mezclar_una_muestra() */
extern unsigned long long perf_ns_arm;			/* arm7_ejecutar() */
extern unsigned long long perf_ns_escena;		/* dibujar_escena() entera */
extern unsigned long long perf_ns_textura;		/* get_texture(), subconjunto de la anterior */
extern unsigned long long perf_ns_presentar;	/* el intercambio de buffers */
extern unsigned long long perf_ns_servicio;		/* el bloque periodico de main_loop() */
extern unsigned long long perf_ns_ta;			/* ta_procesar_bloque() por store queue */

/*
	El ARM7: cuantos pasos dio y cuantos de ellos fueron un salto a si mismo.
	Si la segunda cifra domina, el ARM esta esperando y lo que hay que hacer no
	es moverlo de hilo sino no ejecutarlo -- que es mucho mas barato.
*/
extern unsigned long long perf_arm_pasos;
extern unsigned long long perf_arm_ocioso;

/* El glReadPixels de --captura-gl, para poder descontarlo. */
extern unsigned long long perf_ns_captura;

/*
	Cuanta geometria trajo cada escena. Es el dato que dice si lo que se esta
	midiendo es una pantalla de menu o el juego: CLAUDE.md reporta ~2600 tiras
	por escena en el atractivo de Crazy Taxi, y un menu trae dos ordenes de
	magnitud menos. Sin esto, un desglose donde el PVR no pesa no se puede
	interpretar.
*/
extern unsigned long long perf_escenas;
extern unsigned long long perf_tiras;
extern unsigned long      perf_tiras_max;

/*
	El pico de vertices que **pidio** una escena, contando los que no cupieron.
	Es lo que dice de que tamano deberian ser VertexBuffer[] y TriangleStrip[]:
	65000 y 10000 son numeros de dcemu, no del PVR, y hasta ahora nadie habia
	medido cuanto pide una escena real.
*/
extern unsigned long      perf_vertices_max;

/*
	Los alcances de verdad, que no son los accesos.

	En el diseno de la fase 1 de docs/hilos-plan.md el hilo principal espera al
	del AICA solo si este quedo atras, y su objetivo se fija con reloj_total. O
	sea que varios accesos dentro del **mismo instante emulado** -- una rafaga
	de DMA, un memcpy a la RAM de onda -- cuestan una sola espera: a partir de
	la segunda, el AICA ya esta al dia.

	Contar accesos, entonces, sobreestima. Lo que decide si la fase sirve es
	cuantos instantes distintos hay, y esa es la unica cifra honesta.
*/
extern unsigned long long perf_sync_instantes;

/* Tiempo real que el SH-4 paso bloqueado esperando al hilo del AICA. Es el
   precio de la fase 1 y hay que poder verlo al lado de su beneficio. */
extern unsigned long long perf_ns_espera;

void perf_marcar_sync(void);

#define PERF_SYNC()		do { if (perf_activa) perf_marcar_sync(); } while (0)

/*
	Los accesos del SH-4 al estado del AICA. En el diseno de la fase 1 de
	docs/hilos-plan.md cada uno de estos obliga al hilo del AICA a ponerse al
	dia, asi que su frecuencia es exactamente lo que decide si la fase sirve.

	"Vivo" es un registro cuyo valor depende del reloj del AICA -- las dos
	ventanas de monitoreo, los contadores de los temporizadores, las banderas
	de interrupcion --; "plano" es respaldo que se puede leer sin sincronizar
	nada. La distincion importa: solo los vivos son un costo inevitable.
*/
extern unsigned long long perf_aica_reg_vivo;
extern unsigned long long perf_aica_reg_plano;
extern unsigned long long perf_aica_reg_escr;
extern unsigned long long perf_onda_lect;
extern unsigned long long perf_onda_escr;

/* Cuadros presentados, para poder dar fps sin depender del titulo de ventana. */
extern unsigned long long perf_cuadros;

/*
	Instrucciones despachadas. No es lo mismo que ciclos emulados y esa es
	justamente la razon de existir: el CPI del SH-4 va de 1 a 5 segun la
	instruccion, asi que "ciclos por segundo real" no compara dos versiones del
	despacho -- una mezcla distinta de instrucciones mueve la cifra sin que el
	interprete haya cambiado. **Nanosegundos por instruccion** si compara.

	Se cuenta en los dos lugares que despachan: el camino rapido de main_loop()
	y run(), que es por donde pasan las ranuras de retardo (branch.c y rte143()
	las ejecutan con un core.execute() anidado) y el camino con excepciones. Una
	ranura es una instruccion tan real como cualquier otra y en codigo del SH-4
	son del orden del 10 %: dejarlas fuera sesga la cifra hacia abajo.
*/
extern unsigned long long perf_instrucciones;

/*
	La cache de texturas, que es el bloque grande que queda sin explicar:
	get_texture() se lleva el 32 % del tiempo real en un binario optimizado, con
	1024 entradas persistentes que deberian estar evitando las resubidas.

	Hay tres explicaciones posibles y **piden acciones opuestas**, asi que sin
	medir no se puede elegir:

	 - acierta y el costo es el bind y el cambio de estado por tira -> hay que
	   reducir tiras, no texturas;
	 - falla porque 1024 entradas no alcanzan para una escena de este juego ->
	   hay que agrandarla, y `desalojo` lo dice;
	 - falla porque la generacion se invalida de mas -- el marcado por pagina de
	   8 KB de vram.c tira una textura entera cuando el guest escribe cerca --
	   y eso lo dice `regenera` con `desalojo` en cero.
*/
extern unsigned long long perf_tex_acierto;		/* clave y generacion al dia */
extern unsigned long long perf_tex_regenera;	/* la clave estaba, cambio la generacion */
extern unsigned long long perf_tex_nueva;		/* clave que no estaba */
extern unsigned long long perf_tex_desalojo;	/* la rueda piso una entrada viva */

void perf_inicio(void);
void perf_resumen(void);

#define PERF_MARCA(v)		unsigned long long v = perf_activa ? perf_ahora() : 0
#define PERF_SUMAR(v, ac)	do { if (perf_activa) (ac) += perf_ahora() - (v); } while (0)
#define PERF_CONTAR(c)		do { if (perf_activa) (c)++; } while (0)

/*
	La variante para lo que se llama millones de veces por segundo.

	El bloque periodico de main_loop() entra unas 160 millones de veces en una
	corrida de 40 segundos emulados: dos QueryPerformanceCounter por entrada son
	unos 6 segundos reales, o sea que el instrumento pasaria a ser una quinta
	parte de lo que dice medir. Se cronometra una entrada de cada PERF_MUESTREO
	y se multiplica.

	El sesgo del muestreo es despreciable con 160 mil muestras; el de medirlo
	todo, no.
*/
/* Primo a proposito. Con 1024 y un publicador que actuaba una de cada 64
   entradas, **toda** entrada muestreada caia sobre una que publicaba: el
   bloque periodico llego a informar 281 % del tiempo real. Un periodo que no
   comparta factores con nada del bucle no se puede sincronizar con el. */
#define PERF_MUESTREO		1021

#define PERF_MARCA_MUESTRA(v, n)										\
	static unsigned long n = 0;											\
	int v##_va = perf_activa && ((++n % PERF_MUESTREO) == 0);			\
	unsigned long long v = v##_va ? perf_ahora() : 0

#define PERF_SUMAR_MUESTRA(v, ac)										\
	do {																\
		if (v##_va)														\
			(ac) += (perf_ahora() - (v)) * PERF_MUESTREO;				\
	} while (0)

#endif /* _PERF_H_ */
