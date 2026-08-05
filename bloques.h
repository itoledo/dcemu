/****************************************************************************

	BLOQUES - el cache de bloques predecodificados

	Es el paso 1.3 del plan de rendimiento: **medir cuanto vale de verdad el
	despacho**. El bucle de main_loop() paga por cada instruccion emulada la
	busqueda de la palabra --dos cargas dependientes por get_memory_pointer()--
	y la busqueda del manejador en una tabla de 65536 punteros, o sea 512 KB.
	Un bloque grabado los reemplaza por un recorrido secuencial de un arreglo
	chico.

	La forma del guest dice que la amortizacion alcanza: **6,45 instrucciones
	por bloque en Crazy Taxi y 12,25 en DCDoom**, con menos de 1300 bloques
	cubriendo el 99 % de las instrucciones (docs/interprete-plan.md). Lo que
	falta saber es sobre que se amortiza, y para eso esta esto.

	## Se graba ejecutando, no decodificando hacia adelante

	Decodificar hacia adelante exigiria saber que instrucciones terminan un
	bloque, o sea **clasificar `opcodes[]`**. Este arbol ya intento clasificar
	esa tabla una vez --por "puede abortar despues de mutar", fase 6.3-- y el
	resultado fue que **DCDoom se corrompio en silencio con las 21 suites, los
	113 191 casos de SingleStepTests y las dos demos de MMU en verde**. No se
	vuelve a ese pozo.

	Aca no hay clasificacion de ninguna clase: el bloque se graba **mientras se
	ejecuta**, anotando (manejador, palabra) de cada instruccion, y se cierra
	cuando el PC deja de ser el siguiente. Lo que termina un bloque lo decide el
	propio guest al ejecutarlo, no una tabla que alguien mantiene a mano.

	## Y se verifica en cada reproduccion

	Reproducir a ciegas seria incorrecto: un `BT` grabado sin saltar salta la
	proxima vez. Por eso el bucle compara el PC contra el esperado **despues de
	cada manejador** y abandona el bloque en cuanto difieren. Esa comparacion no
	es un costo agregado: es la misma que decide si el cursor avanza.

	## Lo que si hay que romper a mano

	`UpdateFPSCR()` **repunta `oplist` entero** cuando cambian PR o SZ
	(sh4emu.c): la misma palabra pasa a resolver a otro manejador. Un bloque
	grabado bajo una combinacion y reproducido bajo otra ejecutaria el manejador
	equivocado, en silencio y solo en codigo de coma flotante.

	La salida no cuesta nada porque usa una variable que el bucle **ya lee**:
	`bloques_esperado`. El bucle la escribe antes de llamar al manejador y la
	compara despues; quien invalide el bloque le pone un PC imposible y la
	comparacion falla sola. Un global no es mas caro que el local que tenia:
	cualquiera de los dos vive en la pila mientras corre el manejador.

	## Lo que NO resuelve, y por que igual sirve para medir

	**Codigo automodificable.** Si el guest escribe encima de una instruccion ya
	grabada, el bloque queda viejo. Resolverlo exige un gancho en el macro de
	memwrite, que es el camino mas caliente del arbol.

	Para medir no hace falta: la sonda es valida si el guest ejecuto **lo
	mismo**, y eso se verifica con las cifras que este arbol ya usa para eso
	--instrucciones, cuadros, escenas, tiras y el SHA-256 de la captura--. Si
	divergen, el numero no sirve y ademas se aprendio que el guest se
	automodifica. Es el mismo criterio con el que se descarto
	DCEMU_SONDA_SIN_INSTANTANEA en la fase 6.

*****************************************************************************/

#ifndef _BLOQUES_H_
#define _BLOQUES_H_

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. Igual
   que mmu.h y sistema.h, para que esta cabecera se pueda incluir sola. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"
#include "log.h"			/* opcode_f */

/* Una instruccion grabada: a que manejador fue y con que palabra. */
struct bloque_e
{
	opcode_f *	f;
	WORD		instr;
};

/* DCEMU_SONDA_BLOQUES=1. Se lee una vez al arrancar, como el resto. */
extern int bloques_sonda;

/*
	El PC que el bucle espera despues del manejador en curso. Vive aca y no en
	el bucle para que UpdateFPSCR() --y cualquier otro que invalide-- pueda
	romper el bloque poniendole un valor imposible. Ver arriba.
*/
extern DWORD bloques_esperado;

#define BLOQUES_PC_IMPOSIBLE	0xFFFFFFFFu

/* Rompe el bloque en curso. Lo llama UpdateFPSCR() cuando cambia PR o SZ. */
#define BLOQUES_ROMPER()		(bloques_esperado = BLOQUES_PC_IMPOSIBLE)

/*
	El cursor de reproduccion.

	Es global y no una local de main_loop() por dos razones, y las dos son de
	correccion:

	 - **El longjmp.** El comentario del setjmp en main_loop() ya avisa que
	   ninguna local de esa funcion puede sobrevivir al salto con valor util.
	   Un cursor que sobreviviera apuntaria a media entrada de un bloque que ya
	   no corresponde al PC.
	 - **`excepcion_vigilar` puede cambiar a mitad de todo.** El guest enciende
	   la MMU, o pone SR.FD, y el camino de bloques deja de correr; cuando
	   vuelve, el cursor viejo reproduciria desde donde quedo. Por eso
	   excepcion_actualizar_vigilancia() corta.
*/
extern const struct bloque_e * bloques_cursor;
extern const struct bloque_e * bloques_cursor_fin;

/* Abandona la reproduccion y la grabacion. Barato: dos escrituras. */
#define BLOQUES_CORTAR()												\
	do { bloques_cursor = NULL; bloques_grabando = 0; } while (0)

extern int bloques_grabando;

void bloques_iniciar(void);

/*
	Busca el bloque que empieza en `pc` bajo la tabla de despacho `variante`, y
	deja el final en `fin`. NULL si no esta grabado.

	**La variante es parte de la etiqueta y no un detalle.** `initopcodes()`
	expande `opcodes[]` en cuatro tablas, una por combinacion de PR y SZ de
	FPSCR, y `UpdateFPSCR()` repunta `oplist` entre ellas: la misma palabra
	resuelve a otro manejador. Un bloque grabado bajo una combinacion y
	reproducido bajo otra ejecutaria el manejador equivocado, solo en codigo de
	coma flotante y sin decir nada. Se etiqueta con el puntero a la tabla, que
	es exacto y no hay que derivarlo de nada.
*/
const struct bloque_e * bloques_buscar(DWORD pc, const void * variante,
	const struct bloque_e ** fin);

/*
	Anota una instruccion que esta por ejecutarse, y arranca la grabacion si no
	habia ninguna. Se llama **antes** del manejador, porque el manejador mueve
	el PC.
*/
void bloques_anotar(DWORD pc, const void * variante, WORD instr, opcode_f * f);

/* Cierra el bloque en curso y lo guarda. */
void bloques_cerrar(void);

/* Descarta la grabacion a medias. Se llama al salir del camino rapido. */
void bloques_cancelar(void);

void bloques_resumen(void);

#endif /* _BLOQUES_H_ */
