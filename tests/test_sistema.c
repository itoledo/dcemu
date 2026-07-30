/****************************************************************************

	Pruebas de sistema.c -- las piezas que el boot ROM consulta antes de la
	lectora.

	Las tres son logica pura sobre registros, asi que se prueban sin abrir una
	ventana ni montar una imagen:

	  - el handshake de PDTRA/PCTRA, que es lo que hoy decide si el boot ROM
	    sigue o se duerme para siempre;
	  - la sintesis de una flash minima, para cuando bios/flash.bin no esta;
	  - la conversion de la hora del host a los segundos desde 1950 que cuenta
	    el reloj de la consola.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "suites.h"

#include "opciones.h"
#include "sistema.h"

/* ------------------------------------------------- detector de cable ---- */

/*
	La secuencia que espera el boot ROM: con los cuatro bits bajos de PCTRA en
	8 u 11 los bits bajos de PDTRA valen 3, con cualquier otro valor 0, y hay
	dos excepciones que dependen de lo ultimo que se escribio en PDTRA.
*/
static void pdtra_responde_al_patron(void)
{
	arnes_reset();

	ESPERAR_U32(sistema_pdtra(0x08, 0, CABLE_VGA) & 0x0F, 3);
	ESPERAR_U32(sistema_pdtra(0x0B, 0, CABLE_VGA) & 0x0F, 3);

	ESPERAR_U32(sistema_pdtra(0x00, 0, CABLE_VGA) & 0x0F, 0);
	ESPERAR_U32(sistema_pdtra(0x03, 0, CABLE_VGA) & 0x0F, 0);
	ESPERAR_U32(sistema_pdtra(0x0F, 0, CABLE_VGA) & 0x0F, 0);
}

static void pdtra_excepciones(void)
{
	arnes_reset();

	/* 11 con 2 escrito en PDTRA baja los bits, al reves que el caso general. */
	ESPERAR_U32(sistema_pdtra(0x0B, 2, CABLE_VGA) & 0x0F, 0);

	/* 12 con 2 escrito los sube, aunque 12 por si solo daria 0. */
	ESPERAR_U32(sistema_pdtra(0x0C, 2, CABLE_VGA) & 0x0F, 3);
	ESPERAR_U32(sistema_pdtra(0x0C, 0, CABLE_VGA) & 0x0F, 0);
}

static void pdtra_lleva_el_cable_en_el_byte_alto(void)
{
	arnes_reset();

	ESPERAR_U32(sistema_pdtra(0x08, 0, CABLE_VGA),       0x0003);
	ESPERAR_U32(sistema_pdtra(0x08, 0, CABLE_RGB),       0x0203);
	ESPERAR_U32(sistema_pdtra(0x08, 0, CABLE_COMPUESTO), 0x0303);

	/* Sin patron valido igual hay que informar el cable. */
	ESPERAR_U32(sistema_pdtra(0x00, 0, CABLE_COMPUESTO), 0x0300);
}

/*
	Lo que importa de verdad: el bucle del boot ROM lee PDTRA, se queda con los
	dos bits bajos y compara contra un valor esperado. Si la respuesta fuera
	siempre la misma no habria forma de que la comparacion se cumpla nunca, que
	es exactamente lo que pasaba antes.
*/
static void pdtra_no_es_constante(void)
{
	arnes_reset();

	ESPERAR((sistema_pdtra(0x08, 0, CABLE_VGA) & 3) !=
	        (sistema_pdtra(0x00, 0, CABLE_VGA) & 3));
}

/* --------------------------------------------------------- flash ROM ---- */

static void flash_sintetica_tiene_las_particiones(void)
{
	static unsigned char flash[FLASH_SIZE];

	arnes_reset();

	sistema_flash_sintetizar(flash, 0);

	/* Las tres particiones con cabecera magica, con su numero. */
	ESPERAR_BYTES(&flash[FLASH_PART4_OFF], "KATANA_FLASH____", 16);
	ESPERAR_U32(flash[FLASH_PART4_OFF + 16], 4);

	ESPERAR_BYTES(&flash[FLASH_PART3_OFF], "KATANA_FLASH____", 16);
	ESPERAR_U32(flash[FLASH_PART3_OFF + 16], 3);

	ESPERAR_BYTES(&flash[FLASH_PART2_OFF], "KATANA_FLASH____", 16);
	ESPERAR_U32(flash[FLASH_PART2_OFF + 16], 2);

	/* La reservada viene borrada en las consolas reales. */
	ESPERAR_U32(flash[FLASH_PART1_OFF], 0xFF);
}

/*
	La particion 0 es la que el boot ROM lee primero: 0x1A000 y 0x1A004 son los
	primeros ocho bytes del bloque de identificacion, y 0x1A056-0x1A05C caen en
	el identificador binario de la consola.
*/
static void flash_sintetica_tiene_identificacion(void)
{
	static unsigned char flash[FLASH_SIZE];
	int i;

	arnes_reset();

	sistema_flash_sintetizar(flash, 0);

	ESPERAR_BYTES(&flash[FLASH_PART0_OFF + 0x05], "Dreamcast", 9);

	/* La fecha de fabricacion: epoca de Unix en UTC. */
	ESPERAR_BYTES(&flash[FLASH_PART0_OFF + 0x2C], "197001010000", 12);

	/* Todo el bloque ASCII tiene que ser imprimible: si quedara un 0xFF en
	   medio el parser del boot ROM leeria basura. */
	for (i = 0; i < 0x55; i++)
		ESPERAR(flash[FLASH_PART0_OFF + i] >= 0x20 &&
		        flash[FLASH_PART0_OFF + i] < 0x7F);

	/* Y el identificador binario no puede quedar borrado. */
	ESPERAR(flash[FLASH_PART0_OFF + 0x55] != 0xFF);
}

/* --------------------------------------------------------------- RTC ---- */

static void rtc_cuenta_desde_1950(void)
{
	arnes_reset();

	/* Entre el 1 de enero de 1950 y el de 1970 hay 7305 dias: 20 anios de 365
	   mas los cinco bisiestos. */
	ESPERAR_U32(sistema_rtc_desde_hora(0), 631152000u);
	ESPERAR_U32(sistema_rtc_desde_hora(1), 631152001u);
	ESPERAR_U32(631152000u / 86400u, 7305u);
}

static void rtc_parte_el_contador_en_dos_registros(void)
{
	DWORD alto, bajo, entero;

	arnes_reset();

	/* Leer el alto releva el reloj y lo deja latcheado; el bajo devuelve la
	   otra mitad de ese mismo valor, no de una lectura nueva. */
	alto = sistema_rtc_alto();
	bajo = sistema_rtc_bajo();

	entero = (alto << 16) | bajo;

	ESPERAR(entero >= 631152000u);		/* posterior a 1970 */
	ESPERAR_U32(alto, entero >> 16);
	ESPERAR_U32(bajo, entero & 0xFFFF);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(pdtra_responde_al_patron),
	CASO(pdtra_excepciones),
	CASO(pdtra_lleva_el_cable_en_el_byte_alto),
	CASO(pdtra_no_es_constante),
	CASO(flash_sintetica_tiene_las_particiones),
	CASO(flash_sintetica_tiene_identificacion),
	CASO(rtc_cuenta_desde_1950),
	CASO(rtc_parte_el_contador_en_dos_registros),
};

const dc_suite suite_sistema = DEFINIR_SUITE("sistema", casos);
