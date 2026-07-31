/****************************************************************************

	TA - formato de los parametros del tile accelerator

	El TA recibe la geometria en bloques de 32 bytes, pero no todos los
	parametros miden 32. Miden 64 los encabezados con color de cara, los
	vertices con color en punto flotante, todos los de dos volumenes, los
	sprites y los vertices de volumen modificador, y llegan partidos en dos
	bloques.

	Quien los entrega -- las store queues (pref142) o el CH2 DMA -- no sabe
	nada de eso: manda 32 bytes y avisa. Si cada bloque se despacha por
	separado, la segunda mitad se lee como si fuera una palabra de control, y
	como su primera palabra suele ser un float el tipo que sale es basura.
	Cuando ese float vale 0.0 el tipo sale 0, que es **fin de lista**: se
	cierra una lista que el guest no cerro.

	Por eso hay que saber cuanto mide cada parametro antes de despacharlo, y
	eso sale de la palabra de control mas el tipo de vertice que dejo vigente
	el ultimo encabezado. Esa es toda la tabla que hay aca.

	Sin SDL ni OpenGL a proposito, igual que sistema.c: las pruebas lo enlazan
	de verdad.

*****************************************************************************/

#ifndef _TA_H_
#define _TA_H_

#include "lnxdefs.h"

/*
	Los tipos de parametro global. Los cinco primeros son los del manual --
	"Polygon Type 0" a "Polygon Type 4" --; los dos ultimos no son poligonos y
	se numeran detras para que quepan en la misma variable.
*/
#define TA_GLOBAL_POLY0		0	/* 32 bytes */
#define TA_GLOBAL_POLY1		1	/* 32: color de cara en las palabras 4-7 */
#define TA_GLOBAL_POLY2		2	/* 64: color de cara y de offset */
#define TA_GLOBAL_POLY3		3	/* 32: dos volumenes, sin color de cara */
#define TA_GLOBAL_POLY4		4	/* 64: dos volumenes, dos colores de cara */
#define TA_GLOBAL_VOLUMEN	5	/* 32: encabezado de volumen modificador */
#define TA_GLOBAL_SPRITE	6	/* 32 */

/*
	Los tipos de vertice. 0 a 14 son los del manual; 15 y 16 son los dos de
	sprite y 17 el de volumen modificador, que el manual no numera junto a los
	demas pero aca conviene que compartan el espacio.
*/
#define TA_VERTICE_SPRITE0	15	/* sprite sin textura, 64 bytes */
#define TA_VERTICE_SPRITE1	16	/* sprite con textura, 64 */
#define TA_VERTICE_VOLUMEN	17	/* volumen modificador, 64: un triangulo */

#define TA_TIPOS			18	/* cuantos hay, para dimensionar arreglos */

/*
	Que trae una palabra de control de encabezado: el tipo de parametro global
	que es ese encabezado y el tipo de vertice que deja vigente detras.

	Es una funcion pura de la palabra de control. La usan dos: este modulo,
	para saber cuantos bytes esperar, y graficos.c, para saber que campos leer.
*/
void ta_clasificar(DWORD pcw, int * global, int * vertice);

/* Cuanto mide un parametro de cada clase: 32 o 64 bytes. */
int ta_tam_global(int global);
int ta_tam_vertice(int vertice);

/*
	Un bloque de 32 bytes entrando a la FIFO de poligonos. Junta los que son
	mitad de un parametro de 64 y despacha a graficos.c cuando el parametro
	esta completo.
*/
void ta_procesar_bloque(void * bloque);

/*
	Olvida el parametro a medio armar. Va al empezar una escena (TA_LIST_INIT):
	si el guest abandono uno por la mitad, la mitad que quedo no debe pegarse
	con el primer bloque de la escena siguiente.
*/
void ta_reiniciar(void);

/* El parametro completo, para que graficos.c lo lea. Apunta al buffer de
   armado cuando el parametro medía 64 bytes, y al bloque original cuando 32. */
extern DWORD * ta_address_pointer;

#endif /* _TA_H_ */
