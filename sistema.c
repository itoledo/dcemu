/****************************************************************************

	SISTEMA - ver sistema.h.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sistema.h"

/* ------------------------------------------------------------------------ */
/* Detector de cable de video                                               */
/* ------------------------------------------------------------------------ */

/*
	El boot ROM configura los cuatro bits bajos de PCTRA como salida o entrada
	y despues lee PDTRA esperando un patron concreto; da diez vueltas y si no
	lo consigue se duerme con las interrupciones bloqueadas, que es el halt en
	0x8C000006 que hoy termina el arranque.

	La secuencia que espera esta documentada por ingenieria inversa y es la
	misma en todos los emuladores de Dreamcast: con PCTRA en 8 u 11 los bits
	bajos de PDTRA valen 3, salvo la combinacion 11/2 que da 0, y con PCTRA en
	12 y PDTRA en 2 vuelven a valer 3. Los bits 9-8 llevan el tipo de cable.
*/
WORD sistema_pdtra(DWORD pctra, WORD pdtra, int cable)
{
	DWORD	control = pctra & 0x0F;
	DWORD	datos   = (DWORD) (pdtra & 0x0F);
	WORD	bajos;

	if (control == 0x08 || control == 0x0B)
		bajos = 3;
	else
		bajos = 0;

	if (control == 0x0B && datos == 2)
		bajos = 0;
	else
	if (control == 0x0C && datos == 2)
		bajos = 3;

	return (WORD) (bajos | ((cable & 0x03) << 8));
}

/* ------------------------------------------------------------------------ */
/* Flash ROM                                                                */
/* ------------------------------------------------------------------------ */

unsigned char * flash_mem = NULL;

#define FLASH_MAGIA			"KATANA_FLASH____"
#define FLASH_MAGIA_LARGO	16

/* Cabecera de particion: la magia, el numero de particion y la version.
   El resto de los 64 bytes queda borrado (0xFF), igual que en una consola. */
static void escribir_cabecera(unsigned char * dest, unsigned int offset, int particion)
{
	memcpy(&dest[offset], FLASH_MAGIA, FLASH_MAGIA_LARGO);
	dest[offset + 16] = (unsigned char) particion;
	dest[offset + 17] = 0;					/* version */
}

/*
	La particion 0 no lleva magia: empieza directo con el bloque de
	identificacion de la maquina, 85 bytes ASCII y despues 11 bytes binarios
	con el identificador unico de la consola.

	De esos 85 bytes solo estan documentados el codigo de maquina, el nombre y
	la fecha de fabricacion; el resto son digitos cuyo significado no consta en
	ninguna fuente. Se rellenan con '0', que es lo que tienen en las consolas
	donde ese campo no se uso: el objetivo es que el parser del boot ROM
	encuentre la estructura, no reproducir una consola concreta.
*/
static void escribir_identificacion(unsigned char * dest, time_t fabricacion)
{
	unsigned char *	id = &dest[FLASH_PART0_OFF];
	struct tm *		t;
	char			fecha[16];

	memset(id, '0', 0x55);
	memset(&id[0x05], ' ', 32);				/* nombre de la maquina */

	memcpy(&id[0x00], "00000", 5);			/* codigo de maquina */
	memcpy(&id[0x05], "Dreamcast", 9);

	t = gmtime(&fabricacion);

	if (t != NULL)
	{
		sprintf(fecha, "%04d%02d%02d%02d%02d",
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
		memcpy(&id[0x2C], fecha, 12);
	}

	/* Identificador de la consola. En una real es unico; aqui es fijo, asi la
	   corrida es reproducible. */
	{
		static const unsigned char ident[11] =
			{ 0xD3, 0xD0, 0x2F, 0x80, 0x04, 0x17, 0xC3, 0x24, 0xAD, 0xFF, 0xFE };

		memcpy(&id[0x55], ident, sizeof(ident));
	}
}

void sistema_flash_sintetizar(unsigned char * dest, time_t fabricacion)
{
	/* Flash borrada: todo a 0xFF. Las particiones de bloques quedan vacias,
	   que es una consola recien salida de fabrica y sin configurar. */
	memset(dest, 0xFF, FLASH_SIZE);

	escribir_cabecera(dest, FLASH_PART4_OFF, 4);
	escribir_cabecera(dest, FLASH_PART3_OFF, 3);
	escribir_cabecera(dest, FLASH_PART2_OFF, 2);
	/* La particion 1 esta reservada y en las consolas reales viene sin
	   cabecera, entera a 0xFF. Se deja igual. */

	escribir_identificacion(dest, fabricacion);
}

int sistema_flash_iniciar(const char * ruta)
{
	FILE *	fp;
	size_t	leidos;

	if (flash_mem == NULL)
	{
		flash_mem = (unsigned char *) malloc(FLASH_SIZE);

		if (flash_mem == NULL)
		{
			fprintf(stderr, "No se pudo crear flash_mem.\n");
			return 1;
		}
	}

	fp = ruta ? fopen(ruta, "rb") : NULL;

	if (fp == NULL)
	{
		fprintf(stderr, "No se pudo abrir %s: sintetizando una flash minima.\n",
			ruta ? ruta : "(sin ruta)");
		sistema_flash_sintetizar(flash_mem, time(NULL));
		return 0;
	}

	memset(flash_mem, 0xFF, FLASH_SIZE);
	leidos = fread(flash_mem, 1, FLASH_SIZE, fp);
	fclose(fp);

	fprintf(stderr, "Cargados %lx bytes de flash.\n", (unsigned long) leidos);

	if (leidos == 0)
	{
		fprintf(stderr, "%s esta vacio: sintetizando una flash minima.\n", ruta);
		sistema_flash_sintetizar(flash_mem, time(NULL));
	}

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Reloj de tiempo real                                                     */
/* ------------------------------------------------------------------------ */

static DWORD	rtc_latch = 0;
static int		rtc_latcheado = 0;

DWORD sistema_rtc_desde_hora(time_t t)
{
	if (t < 0)
		t = 0;

	return (DWORD) t + RTC_EPOCA_1950;
}

WORD sistema_rtc_alto(void)
{
	rtc_latch = sistema_rtc_desde_hora(time(NULL));
	rtc_latcheado = 1;

	return (WORD) (rtc_latch >> 16);
}

WORD sistema_rtc_bajo(void)
{
	/* El uso normal es leer el alto y despues el bajo. Si alguien lee solo el
	   bajo hay que relevar el reloj igual. */
	if (!rtc_latcheado)
		rtc_latch = sistema_rtc_desde_hora(time(NULL));

	rtc_latcheado = 0;

	return (WORD) (rtc_latch & 0xFFFF);
}
