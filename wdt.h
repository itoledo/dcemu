/****************************************************************************

	WDT - el temporizador watchdog del SH-4

	Dos registros y un contador de 8 bits que sube. Sirve para dos cosas
	segun el bit WT/IT de WTCSR:

	  - modo watchdog: si el programa no "alimenta" el contador antes de que
	    desborde, la maquina se reinicia;
	  - modo temporizador de intervalo: el desborde levanta la interrupcion
	    ITI (EXC_WDT_ITI, 0x0560) y el contador vuelve a empezar.

	KOS lo usa por dc/wdt.h y trae un ejemplo que lo valida
	(examples/dreamcast/basic/watchdog). Ese ejemplo avisa en su propio
	comentario que ningun emulador de Dreamcast se molestaba en emularlo.

	El reloj es el de la CPU dividido por CKS: 32 << CKS, o sea de 32 a 4096.
	A 200 MHz eso da un desborde de las 256 cuentas cada 41 us con el divisor
	mas chico y cada 5.25 ms con el mas grande, que es exactamente lo que
	documenta dc/wdt.h.

	OJO CON EL RITMO. main_loop() le pasa 50 "ciclos" cada vez que llama a
	timer_check(), pero esos no son ciclos de 200 MHz: core.context.cycles lo
	incrementan los handlers por instruccion. Y timer_check() decrementa los
	TCNT de a uno por llamada, ignorando el prescaler que el programa dejo en
	TCR. O sea que dcemu no tiene una base de tiempo unica, y este contador es
	una tercera tasa que no esta calibrada contra las otras dos.

	Se nota en examples/dreamcast/basic/watchdog: el ISR de KOS no lee el
	contador, asume 41 us por interrupcion y las cuenta, asi que compara el
	ritmo del WDT contra su reloj de milisegundos, que sale del TMU. Hoy pide
	20 avisos en 10 segundos y los recibe en 3. Ajustar el divisor de aca
	para que cuadre seria encajar un numero magico contra un reloj ya torcido:
	lo que hace falta es una base de tiempo comun para TMU y WDT.

	Vive aparte de mem.c a proposito, igual que sistema.c, gdrom.c y mmu.c:
	es logica pura sobre registros y se prueba sin abrir una ventana.

*****************************************************************************/

#ifndef _WDT_H_
#define _WDT_H_

#include <stddef.h>

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* ------------------------------------------------------------------------ */
/* Registros                                                                */
/* ------------------------------------------------------------------------ */

/* Direcciones reales y su offset dentro de regmem (los 24 bits bajos). */
#define WDT_WTCNT		0x00C00008		/* contador */
#define WDT_WTCSR		0x00C0000C		/* control y estado */

/* Escribirlos exige un byte alto de clave, y son escrituras de 16 bits. Leer
   es de 8 bits. Sin la clave correcta el hardware ignora la escritura. */
#define WDT_CLAVE_WTCNT	0x5A
#define WDT_CLAVE_WTCSR	0xA5

/* Bits de WTCSR. */
#define WTCSR_TME		0x80			/* contador andando */
#define WTCSR_WTIT		0x40			/* 1 = watchdog, 0 = intervalo */
#define WTCSR_RSTS		0x20			/* tipo de reset en modo watchdog */
#define WTCSR_WOVF		0x10			/* desbordo en modo watchdog */
#define WTCSR_IOVF		0x08			/* desbordo en modo intervalo */
#define WTCSR_CKS		0x07			/* divisor: 32 << CKS */

/* ------------------------------------------------------------------------ */
/* Acceso                                                                   */
/* ------------------------------------------------------------------------ */

/* Deja el WDT parado y en cero. La llama regmem_setup(). */
void wdt_reset(void);

/* 1 si la direccion fisica es uno de los dos registros. */
int wdt_es_registro(unsigned long fisica);

void wdt_leer(unsigned long fisica, void * p, size_t size);
void wdt_escribir(unsigned long fisica, void * p, size_t size);

/*
	Avanza el contador segun los ciclos de CPU transcurridos. Devuelve 1 si el
	desborde tiene que levantar la ITI, o sea solo en modo intervalo: en modo
	watchdog el desborde reinicia la maquina de verdad, y eso aca se registra
	pero no se hace.

	La llama main_loop() con el mismo ritmo que timer_check().
*/
int wdt_tick(DWORD ciclos);

/* Estado, expuesto para las pruebas. */
BYTE wdt_contador(void);
BYTE wdt_control(void);

/* Cuantos ciclos de CPU lleva un incremento del contador, segun CKS. */
#define WDT_DIVISOR(wtcsr)	(32u << ((wtcsr) & WTCSR_CKS))

#endif /* _WDT_H_ */
