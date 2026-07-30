/****************************************************************************

	SISTEMA - las piezas de estado que el boot ROM consulta antes de la lectora

	Tres cosas sin relacion entre si, salvo que las tres son logica pura sobre
	registros y las tres las pide el boot ROM temprano:

	  - el handshake de PDTRA/PCTRA, que es el detector de tipo de cable de
	    video y es lo que hoy cuelga el arranque (fase 1.2 del plan);
	  - la flash ROM de 128 KB, donde viven region, idioma, fecha y nombre de
	    la consola (fase 2.1);
	  - el reloj de tiempo real (fase 2.3).

	Viven aparte de mem.c a proposito: mem.c no se puede enlazar en las
	pruebas unitarias porque arrastra SDL y OpenGL por los callbacks del PVR,
	y estas tres si se prueban sin abrir una ventana.

*****************************************************************************/

#ifndef _SISTEMA_H_
#define _SISTEMA_H_

#include <time.h>

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. Igual
   que main.h, para que esta cabecera se pueda incluir sola. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* ------------------------------------------------------------------------ */
/* Detector de cable de video (PDTRA / PCTRA)                               */
/* ------------------------------------------------------------------------ */

/* El puerto A del SH-4 esta cableado al detector de cable. El boot ROM
   escribe un patron en PCTRA y espera que los bits bajos de PDTRA respondan;
   el byte alto lleva el tipo de cable. Funcion pura: (PCTRA, PDTRA, cable)
   -> valor que ve el programa al leer PDTRA. */
WORD sistema_pdtra(DWORD pctra, WORD pdtra, int cable);

/* ------------------------------------------------------------------------ */
/* Flash ROM                                                                */
/* ------------------------------------------------------------------------ */

/* Direccion fisica de la flash y su tamano. Cae dentro del area del boot ROM,
   asi que la atiende bios_read(). */
#define FLASH_BASE			0x00200000
#define FLASH_SIZE			0x00020000		/* 128 KB */

/* Las cinco particiones, en el orden fisico del volcado de una consola real.
   La 0 (ajustes de fabrica) es la unica sin cabecera magica: arranca directo
   con el bloque ASCII de identificacion de la maquina. */
#define FLASH_PART4_OFF		0x00000			/* 64 KB, bloques */
#define FLASH_PART3_OFF		0x10000			/* 32 KB, ajustes de juegos */
#define FLASH_PART1_OFF		0x18000			/*  8 KB, reservada */
#define FLASH_PART0_OFF		0x1A000			/*  8 KB, ajustes de fabrica */
#define FLASH_PART2_OFF		0x1C000			/* 16 KB, bloques */

extern unsigned char * flash_mem;

/* Reserva el bloque y lo llena: si el archivo existe lo carga, si no sintetiza
   una flash minima valida. Devuelve 0 si todo bien, 1 si no pudo reservar. */
int sistema_flash_iniciar(const char * ruta);

/* Escribe en dest (FLASH_SIZE bytes) una flash sin configurar pero con la
   estructura que el boot ROM espera encontrar. Expuesta para las pruebas. */
void sistema_flash_sintetizar(unsigned char * dest, time_t fabricacion);

/*
	Una escritura del guest a la flash, con el desplazamiento dentro de los
	128 KB. No es memoria: el chip espera una secuencia de desbloqueo antes de
	dejar programar o borrar, y sin ella la escritura no hace nada. Ver el
	comentario en sistema.c.
*/
void sistema_flash_escribir(DWORD offset, BYTE valor);

/* Devuelve la flash al archivo del que salio, si cambio. 0 si todo bien. */
int sistema_flash_guardar(void);

/* ------------------------------------------------------------------------ */
/* Reloj de tiempo real                                                     */
/* ------------------------------------------------------------------------ */

/* El RTC de la Dreamcast cuenta segundos desde el 1 de enero de 1950. */
#define RTC_EPOCA_1950	631152000U		/* segundos entre 1950 y 1970 */

DWORD sistema_rtc_desde_hora(time_t t);

/*
	Poner el reloj en hora. El guest escribe las dos mitades de 16 bits y hasta
	que no llegan las dos no hay fecha; lo que queda guardado es el **desfase**
	contra el reloj del anfitrion, para que el tiempo siga corriendo.
*/
void sistema_rtc_escribir_alto(WORD valor);
void sistema_rtc_escribir_bajo(WORD valor);

/* El desfase persiste entre corridas, igual que la flash. */
void sistema_rtc_cargar(void);
int  sistema_rtc_guardar(void);

/* Los dos registros de 16 bits. El alto releva el reloj del host y lo deja
   latcheado; el bajo devuelve la mitad baja de ese mismo valor, para que la
   pareja de lecturas no se parta entre dos segundos distintos. */
WORD sistema_rtc_alto(void);
WORD sistema_rtc_bajo(void);

/* ------------------------------------------------------------------------ */
/* Bus G1: modo del sistema                                                 */
/* ------------------------------------------------------------------------ */

/*
	SB_G1SYSM dice de que maquina se trata: el nibble alto es el tipo y el bajo
	la region. Una Dreamcast de venta al publico contesta cero en los dos, y la
	region de verdad se lee de la flash, no de aqui.

	Cae dentro de 0x005F7400-0x005F74FF, que es el bloque de control del bus G1.
	dcemu manda ese rango entero a la lectora, asi que hay que atajarlo antes:
	con el valor que salia de ahi -- 0x2422211F, o sea tipo 1 -- KOS creia estar
	en un kit de desarrollo Set5 y spu_init() limpiaba 8 MB de RAM de sonido en
	vez de 2, con lo que 6 MB de escrituras caian fuera de todo lo mapeado.
*/
#define G1_SYSM			0x005F74B0
#define G1_SYSM_RETAIL	0x00000000		/* tipo 0, region 0 */

/* ------------------------------------------------------------------------ */
/* Bus G2: area de dispositivos externos                                    */
/* ------------------------------------------------------------------------ */

/* 0x00600000-0x006007FF es donde irian el modem y la BBA. No se emula
   ninguno, pero hay que contestar algo fijo: si se deja el destino sin tocar
   el boot ROM lee basura de la pila y el arranque no es reproducible. */
#define G2_EXT_BASE		0x00600000
#define G2_EXT_FIN		0x006007FF
#define G2_EXT_VACIO	0x00000000		/* "aqui no hay nada" */

#endif /* _SISTEMA_H_ */
