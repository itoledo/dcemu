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

/*
	El cuadro grafico entero: cb_tastart(), que es la frontera de cuadro y por
	donde pasa TODO lo que dcemu hace por escena.

	Existe porque el reparto tenia un agujero y la conclusion que salia de el
	era falsa. perf_ns_escena mide solo el bucle de tiras; **antes** de llamarlo,
	cb_tastart() recorre todos los vertices de la escena para sacar la
	profundidad de cada tira y despues ordena TriangleStrip[] con qsort -- 144
	bytes por elemento, del orden de mil elementos, diez mil veces --, y eso caia
	entero en "resto (interprete)". Un desglose que llama "interprete" a la
	ordenacion de la geometria no puede contestar si el camino grafico pesa.

	Anida a escena, presentar y captura, asi que el resto se calcula contra este
	y no contra la suma de aquellos.
*/
extern unsigned long long perf_ns_cuadro;
extern unsigned long long perf_ns_orden;		/* profundidad por tira + qsort */
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
extern unsigned long long perf_tex_desalojo;

/*
	Llamadas de dibujo. Con el perfil poniendo el 34,5 % del tiempo real adentro
	del driver y solo el 2,9 % en codigo nuestro, lo que importa no es cuanto
	tarda dibujar sino cuantas veces se entra al driver. Hoy es una por tira;
	ver dibujar_tira() en graficos.c por que no se agrupan.
*/
extern unsigned long long perf_tiras_dibujadas;	/* una por glDrawArrays de tira */

/*
	Cuantas de esas tiras podrian haberse sumado al lote de la anterior, o sea
	las que salieron de tira_estado() **sin emitir una sola llamada de estado**.

	Es la medida directa del techo de glMultiDrawArrays, y existe porque el
	intento anterior se midio al reves: se implemento primero y despues se
	descubrio que daba 1,0 tiras por llamada. Esta cifra lo contesta antes, y
	con la escena real: si queda cerca de cero, agrupar no tiene nada que
	ganar por mas que se ordene por material.
*/
extern unsigned long long perf_tiras_sin_cambio;

/*
	La MMU, que es el bloque grande sin desglosar del unico guest que la
	enciende.

	El reparto de --perf le llama "resto (interprete)" al 93,6 % de DCDoom, y
	adentro de ese numero hay tres cosas que piden acciones distintas y que
	nadie separo todavia:

	 - la instantanea por instruccion (context_t mas los dos bancos de coma
	   flotante) que arma excepcion_instantanea_tomar();
	 - la traduccion de la busqueda de instruccion, que tiene cache de una
	   entrada y por lo tanto deberia ser casi gratis;
	 - **la traduccion de cada acceso a datos, que no tiene cache ninguna**:
	   recorre las 64 entradas de la UTLB desempaquetando mascaras al vuelo y
	   encima hace un read-modify-write de MMUCR por el avance de URC.

	`utlb_pasos` es la cifra que decidio la tercera: dividida por las busquedas
	da la profundidad media del recorrido, que antes de la cache de traduccion
	era de 34,5 entradas sobre 64. `cache_acierto` es la tasa de aciertos de esa
	cache, y `utlb_pasos` mide ahora lo que queda: un acierto cuenta como 1.

	**Cuidado con leer `datos_acierto` como una tasa de aciertos.** El
	denominador son TODAS las traducciones, y de esas una parte grande --el 35 %
	en DCDoom-- son direcciones de P1 o P2, que no pasan por la TLB y salen por
	un `return` temprano sin mirar ninguna cache. Contra las que si necesitan
	traducir, la cache sirve el 99 %: la cifra honesta es `utlb` --las busquedas
	que llegan al segundo nivel-- contra `traduce` menos las de P1/P2.

	`falta` cuenta las traducciones que terminaron en excepcion, o sea las
	unicas veces que la instantanea sirvio para algo. Contra `instantanea`
	--que se toma en TODAS-- da la razon de trabajo util a trabajo tirado.
*/
extern unsigned long long perf_mmu_traduce;			/* mmu_traducir(), datos */
extern unsigned long long perf_mmu_utlb;			/* ... de esas, las que buscan entrada */
extern unsigned long long perf_mmu_utlb_pasos;		/* entradas recorridas en total */
extern unsigned long long perf_mmu_cache_acierto;	/* aciertos de la cache de traduccion */
extern unsigned long long perf_mmu_datos_acierto;
extern unsigned long long perf_mmu_vaciados;		/* mmu_tlb_invalidar(): el vaciado entero */
extern unsigned long long perf_mmu_datos_choque;	/* fallo con la MISMA pagina: modo o ASID */
extern unsigned long long perf_mmu_datos_capacidad;	/* fallo con otra pagina: capacidad */
extern unsigned long long perf_mmu_fetch_fallo;		/* el acierto no se cuenta: ver mmu.c */
extern unsigned long long perf_mmu_fetch_acierto2;	/* ... de esos, los que atiende la de 64 */
extern unsigned long long perf_mmu_falta;			/* traducciones que abortaron */
extern unsigned long long perf_instantaneas;		/* instantaneas tomadas */
extern unsigned long long perf_instantaneas_usadas;	/* ... y restauradas */

/*
	Y cuanto cuestan las dos piezas que quedan del sobrecosto de un guest con
	MMU. **No creerles: el instrumento no llega a esta escala.**

	Se cronometran por muestreo, como el bloque periodico, pero aquello dura
	microsegundos y esto nanosegundos: el par de QueryPerformanceCounter cuesta
	bastante mas que lo que envuelve, asi que cada muestra mide sobre todo el
	reloj y multiplicarla por PERF_MUESTREO lo agranda. Medido sobre DCDoom, la
	instantanea informa **235,9 % del tiempo real**, que es la manera en que un
	instrumento avisa que no sirve.

	Se dejan porque la cifra absurda es mas util que ninguna: dice que a esta
	escala hay que medir implementando el cambio y comparando dos binarios en la
	misma tanda, no cronometrando adentro.
*/
extern unsigned long long perf_ns_traducir;		/* mmu_traducir() entera */
extern unsigned long long perf_ns_instantanea;	/* excepcion_instantanea_tomar() */

void perf_inicio(void);
void perf_resumen(void);

#define PERF_MARCA(v)		unsigned long long v = perf_activa ? perf_ahora() : 0
#define PERF_SUMAR(v, ac)	do { if (perf_activa) (ac) += perf_ahora() - (v); } while (0)
#define PERF_CONTAR(c)		do { if (perf_activa) (c)++; } while (0)
#define PERF_SUMAR_A(c, n)	do { if (perf_activa) (c) += (n); } while (0)

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
