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

/*
	Cada cuantos ciclos de CPU main_loop() atiende a los perifericos.

	**Era 50**, una constante de 2004 sin fundamento -- el comentario que la
	acompanaba habla de otra cosa --, y con ella el bloque entra unos 160
	millones de veces en 40 segundos emulados. Medido con --perf sobre Crazy
	Taxi: un 21 % del tiempo real se iba en descubrir que no habia nada que
	hacer, entre timer_check(), wdt_tick(), intc_revisar_sh4() y dma_check().

	**Subirla no pierde ninguna cuenta.** tmu_tick() y wdt_tick() reciben la
	cantidad de ciclos y llevan su propio resto contra su propio divisor, asi
	que la trayectoria de TCNT y el instante exacto de cada subdesborde son los
	mismos con cualquier grano; el AICA no recibe ciclos, compara contra su
	propia marca de reloj_total. Lo unico que se mueve es **cuando se observa**
	lo que ya paso: una bandera puede quedar hasta RELOJ_GRANO ciclos sin mirar,
	y la interrupcion que deriva de ella entregarse igual de tarde.

	400 ciclos son 2 us, y los tres relojes que el guest puede notar quedan muy
	por encima: la muestra del AICA son 22,7 us, la linea de barrido 31,8 us y
	el latido del planificador de KOS 1 ms. Es ademas donde esta la rodilla:
	baja el costo del bloque a un octavo, y subirlo mas rinde poco porque a esa
	altura ya no pesa.

	Lo que si queda cuantizado a este grano es **el valor de TCNT que ve un
	guest que lo sondea**. La salida definitiva seria calcularlo al leerlo y
	hacer el bloque dirigido por vencimientos en vez de por un grano fijo -- lo
	que ademas ahorraria otro 2 % --, pero no hace falta para el 19 % que hay
	aqui. Ver docs/rendimiento-plan.md, fase 2.5.
*/
#define RELOJ_GRANO		400

/*
	El reloj por eventos (fase 5 de docs/estado-del-arte-plan.md): la frontera
	del bloque periodico sigue siendo cada RELOJ_GRANO ciclos --la grilla no
	cambia, y con ella ninguna entrega--, pero el servicio completo corre solo
	cuando reloj_total alcanzo el vencimiento mas cercano (subdesborde de TMU o
	WDT, muestra del AICA, linea de barrido, demora del INTC, DMA propio) o
	cuando alguien lo invalido. Todo lo que pueda mover un vencimiento tiene
	que llamar a reloj_tocar(): las escrituras a registros on-chip
	(regmap_write), las del PVR/ASIC (pvr_write, que cubre las mascaras SB) y
	los eventos nuevos del INTC. Conservador por construccion: correr el bloque
	de mas es exactamente lo de hoy; correrlo de menos seria una entrega
	tardia.

	DCEMU_SIN_RELOJ_EVENTOS=1 deja el vencimiento en 0 para siempre: el bloque
	completo corre en cada frontera, que es el comportamiento anterior bit a
	bit, y el A/B.

	El contador de toques existe porque el servicio mismo postea eventos --las
	comparaciones de linea postean SCANINT despues de que intc_revisar_sh4() ya
	corrio-- y el recalculo del final pisaria esa invalidacion: la entrega, que
	hoy sale en la frontera siguiente, se iria hasta el proximo vencimiento.
	main_loop() muestrea el contador al entrar al servicio y solo recalcula si
	nadie toco en el medio; si alguien toco, el vencimiento queda en 0 y la
	frontera siguiente corre el bloque completo -- la misma cadencia de hoy.
*/
extern unsigned long long reloj_vencimiento;
extern unsigned reloj_toques;

#define reloj_tocar()	(reloj_vencimiento = 0, reloj_toques++)

/* Ciclos hasta el primer subdesborde del TMU (~0ull si ningun canal corre),
   con el estado como quedo en el ultimo tick: entre medio nadie lo consulta,
   porque las escrituras invalidan. */
unsigned long long tmu_proximo(void);

/* Pone al dia los temporizadores hasta la ultima frontera consumida: lo que
   un guest que sondea TCNT o WTCNT tiene que ver es el valor de esa
   frontera, ni mas fresco ni mas viejo. Vive en main.c, que es quien lleva
   la marca; mem.c lo llama desde la lectura de esos registros. */
void reloj_sincronizar_ticks(void);

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
