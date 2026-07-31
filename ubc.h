/****************************************************************************

	UBC - el controlador de breakpoints por hardware del SH-4

	Dos canales. El A compara direccion (de instruccion o de operando, con
	mascara y ASID opcional); el B ademas puede comparar el dato transferido
	(BDRB/BDMRB, con BRCR.DBEB). BRCR.SEQ los encadena: el match de A no
	genera excepcion, solo deja armado al B. La condicion cumplida deja
	CMFA/CMFB puestos en BRCR y **solo el guest los limpia** -- su manejador
	los lee para saber que canal disparo.

	La excepcion es EXPEVT 0x1E0 por el vector general, antes o despues de
	ejecutar la instruccion segun BRCR.PCBA/PCBB para los breaks de
	instruccion; los de operando son siempre despues. "Despues" significa que
	SPC queda en la instruccion siguiente, y el manejador de KOS imprime
	PC - 2 por eso mismo.

	Como se engancha en dcemu:

	  - main_loop() llama a ubc_revisar_instruccion() en la frontera de cada
	    instruccion cuando ubc_activa != 0: ahi se entregan los breaks
	    pendientes (SPC = la siguiente, que es el PC actual) y se evaluan los
	    de instruccion sobre el PC que viene.
	  - Los accesos de operando entran por el gancho de los macros
	    memread/memwrite de mem.h -- el camino del guest, con la direccion
	    **virtual**, que es la que el UBC compara; los accesos internos van por
	    *_fisico y quedan fuera solos. Apagado cuesta una comparacion, como el
	    watchpoint de --traza-mem.
	  - Los registros viven en regmem (regmap_write avisa al tocarlos), asi
	    que el guest lee CMFA/CMFB donde este modulo los deja.

	Un break de instruccion sobre la ranura de retardo no se detecta: la
	ranura corre dentro del core.execute() del salto y la frontera ve solo al
	salto. El manual restringe ese caso de todos modos.

	Sin SDL a proposito, igual que ta.c y sistema.c: las pruebas lo enlazan de
	verdad (suite `ubc`).

*****************************************************************************/

#ifndef _UBC_H_
#define _UBC_H_

#include <stddef.h>

#include "lnxdefs.h"

/* Los registros, como punteros al respaldo (regmem en el emulador, un bloque
   propio en las pruebas). Los enlaza regmem_setup() / memoria_prueba. */
extern DWORD *	UBC_BARA;	/* 0xFF200000, direccion canal A */
extern BYTE *	UBC_BAMRA;	/* 0xFF200004, mascara canal A */
extern WORD *	UBC_BBRA;	/* 0xFF200008, ciclo de bus canal A */
extern DWORD *	UBC_BARB;	/* 0xFF20000C */
extern BYTE *	UBC_BAMRB;	/* 0xFF200010 */
extern WORD *	UBC_BBRB;	/* 0xFF200014 */
extern DWORD *	UBC_BDRB;	/* 0xFF200018, dato canal B */
extern DWORD *	UBC_BDMRB;	/* 0xFF20001C, mascara del dato */
extern WORD *	UBC_BRCR;	/* 0xFF200020, control */
extern BYTE *	UBC_BASRA;	/* 0xFF000014, ASID canal A (bloque CCN) */
extern BYTE *	UBC_BASRB;	/* 0xFF000018, ASID canal B */

/* Bits de BRCR. */
#define UBC_BRCR_CMFA	(1 << 15)	/* condicion A cumplida (limpia el guest) */
#define UBC_BRCR_CMFB	(1 << 14)	/* condicion B cumplida */
#define UBC_BRCR_PCBA	(1 << 10)	/* canal A: break despues de ejecutar */
#define UBC_BRCR_DBEB	(1 << 7)	/* canal B compara el dato */
#define UBC_BRCR_PCBB	(1 << 6)	/* canal B: break despues de ejecutar */
#define UBC_BRCR_SEQ	(1 << 3)	/* secuencial: A arma a B */

/* 1 con algun canal armado (campo ID de su BBR != 0): main_loop() solo llama
   al UBC cuando vale. El segundo restringe el gancho de mem.h a los canales
   que miran operandos. Los recalcula ubc_registros_escritos(). */
extern int ubc_activa;
extern int ubc_operando_activa;

/* Lo llama regmap_write() cuando el guest toca 0xFF2000xx, y las pruebas a
   mano despues de configurar los registros. */
void ubc_registros_escritos(void);

/*
	La frontera de instruccion: entrega el break pendiente si SR.BL lo
	permite, o evalua los canales de instruccion contra el PC actual.
	Devuelve 1 si entro a la excepcion (main_loop no debe ejecutar la
	instruccion: PC ya es el manejador).
*/
int ubc_revisar_instruccion(void);

/* El gancho de operandos: direccion virtual, el valor transferido, el tamano
   y si fue escritura. Lo llaman los macros de mem.h tras el acceso. */
void ubc_operando(unsigned long direccion, const void * valor, size_t tam,
				  int escritura);

/* Estado interno en cero (pendiente, latch de secuencia). Para el reset del
   arnes; los registros no se tocan. */
void ubc_reiniciar(void);

#endif /* _UBC_H_ */
