/****************************************************************************

	TMU - los tres temporizadores del SH-4

	Tres cuentas descendentes de 32 bits. Cada una baja a un ritmo que sale
	del reloj periferico dividido por el campo TPSC de su TCR; al subdesbordar
	recarga de TCOR, marca UNF y, si UNIE esta puesto, pide su interrupcion.

	Ver docs/clock-plan.md, fase 1. Lo que habia antes bajaba cada TCNT de a
	uno por llamada e ignoraba TPSC, o sea que el ritmo era una constante y no
	dependia de lo que programaba el guest. KOS pide Pck/4 y esperaba 12,47
	millones de cuentas por segundo; recibia 3,99 millones. Esos 3,125x son
	exactamente el desfase que reportaba examples/dreamcast/basic/watchdog.

	Vive aparte de main.c a proposito, igual que sistema.c, gdrom.c, mmu.c y
	wdt.c: es logica pura sobre registros y se prueba sin abrir una ventana.

*****************************************************************************/

#ifndef _TMU_H_
#define _TMU_H_

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

#define TMU_CANALES		3

/* ------------------------------------------------------------------------ */
/* El reloj                                                                 */
/* ------------------------------------------------------------------------ */

/*
	La unica constante de conversion del emulador. Es el mismo numero que usa
	KOS en kernel/arch/dreamcast/kernel/timer.c, asi que las dos puntas
	coinciden por construccion y no por casualidad.
*/
#define DC_CPU_HZ		199499520u
#define DC_PCK_HZ		(DC_CPU_HZ / 4)		/* el periferico es CPU/4 */

/* ------------------------------------------------------------------------ */
/* El contador monotono                                                     */
/* ------------------------------------------------------------------------ */

/*
	Ciclos de CPU desde el arranque. Solo sube, y a proposito vive fuera de
	core.context: la instantanea que saca main_loop() para reejecutar una
	instruccion que fallo por MMU restaura el contexto entero, y el reloj no
	tiene que retroceder.

	Cada consumidor periodico guarda su propia marca y compara contra esta, en
	vez de acumular y restar. Eso es lo que elimina los restos mal llevados: el
	acumulador de lineas sumaba todo lo acumulado pero restaba solo una parte,
	asi que el sobrante se contaba dos veces. Ver docs/clock-plan.md, fase 2.
*/
extern unsigned long long reloj_total;

/* Microsegundos de tiempo emulado. */
unsigned long long reloj_us(void);

/* Milisegundos de tiempo emulado. */
unsigned long long reloj_ms(void);

/* ------------------------------------------------------------------------ */
/* Bits de los registros                                                    */
/* ------------------------------------------------------------------------ */

/* TCR: selector de reloj. TMU_TCR_UNIE y TMU_TCR_UNF estan en sh4emu.h. */
#define TMU_TCR_TPSC	0x0007

/* TSTR: un bit por canal. */
#define TMU_TSTR_STR(n)	(1 << (n))

/* ------------------------------------------------------------------------ */
/* Acceso                                                                   */
/* ------------------------------------------------------------------------ */

/* Deja los tres canales sin resto pendiente. La llama regmem_setup(). */
void tmu_reset(void);

/*
	Cuantos ciclos de CPU lleva una cuenta del canal, segun TPSC.

	El periferico ya es CPU/4 y TPSC divide otra vez por 4, 16, 64, 256 o
	1024, asi que desde ciclos de CPU el divisor es 16 << (2*TPSC): 16, 64,
	256, 1024, 4096.

	TPSC 101 esta reservado y 110/111 son el reloj del RTC y uno externo, que
	la Dreamcast no cablea. Los tres caen al valor por omision.
*/
DWORD tmu_divisor(WORD tcr);

/*
	Avanza los tres canales con los ciclos de CPU transcurridos. Devuelve una
	mascara con los canales que subdesbordaron y tienen UNIE puesto, o sea los
	que piden interrupcion: bit 0 para TMU0, bit 1 para TMU1, bit 2 para TMU2.

	Solo cuentan los canales arrancados en TSTR. Arrancar uno le pone el resto
	en cero, para que la primera cuenta no llegue antes de tiempo.
*/
int tmu_tick(DWORD ciclos);

/* Resto pendiente de un canal, expuesto para las pruebas. */
DWORD tmu_resto(int canal);

/* ------------------------------------------------------------------------ */
/* El barrido de pantalla                                                   */
/* ------------------------------------------------------------------------ */

/* Bits de SPG_CONTROL que deciden la norma de video. */
#define SPG_CTRL_INTERLACE	(1 << 4)
#define SPG_CTRL_NTSC		(1 << 6)
#define SPG_CTRL_PAL		(1 << 7)

/*
	Cuantos ciclos de CPU dura una linea de barrido, dados el total de lineas
	por campo (SPG_LOAD.vcount) y SPG_CONTROL.

	main_loop() usaba 978, una constante empirica de 2004 que hacia los frames
	6,5 veces mas rapidos de lo que corresponde. La cuenta es
	DC_CPU_HZ / (lineas * campos por segundo), y los campos por segundo salen de
	la norma: 50 en PAL, 59,94 en NTSC y 60 en VGA.

	Ver docs/clock-plan.md, fase 3.
*/
DWORD reloj_ciclos_por_linea(DWORD vcount, DWORD spg_control);

#endif /* _TMU_H_ */
