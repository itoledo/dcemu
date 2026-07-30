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

/* De donde se cargo la flash y si hay cambios que devolverle. */
static const char *	flash_ruta = NULL;
static int			flash_sucia = 0;

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

	flash_ruta  = ruta;
	flash_sucia = 0;

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Escritura de la flash                                                    */
/* ------------------------------------------------------------------------ */

/*
	La flash de la Dreamcast es un chip compatible con el juego de comandos de
	AMD/Fujitsu: no se escribe poniendo un byte en una direccion, sino mandando
	una secuencia de desbloqueo a dos direcciones fijas.

	  programar un byte:  AA en 0x5555, 55 en 0x2AAA, A0 en 0x5555, dato
	  borrar un sector:   AA en 0x5555, 55 en 0x2AAA, 80 en 0x5555,
	                      AA en 0x5555, 55 en 0x2AAA, 30 en el sector

	Esto no estaba: las escrituras se reportaban como direcciones sin emular y
	se perdian, asi que **la BIOS pedia la fecha y la hora en cada arranque**.
	Nunca conseguia guardar que ya estaba configurada.

	Programar solo puede bajar bits -- para subirlos hay que borrar el sector
	entero --, y por eso el paso de programacion hace `&=` y no `=`. Es la misma
	regla que ya seguia el hook de syscall de flashrom.
*/

#define FLASH_CMD_1		0x5555
#define FLASH_CMD_2		0x2AAA

/* Los sectores del chip: 8 de 16 KB en los 128 KB. Borrar afecta al sector
   entero, que es lo que hace la BIOS antes de reescribir una particion. */
#define FLASH_SECTOR	0x4000

static enum
{
	FE_LISTA = 0,		/* esperando el AA */
	FE_DESBLOQUEO,		/* llego el AA, se espera el 55 */
	FE_COMANDO,			/* llego el 55, se espera A0 o 80 */
	FE_PROGRAMAR,		/* llego el A0, el proximo byte es el dato */
	FE_BORRAR_1,		/* llego el 80, se espera otro AA */
	FE_BORRAR_2,		/* llego el AA, se espera el 55 */
	FE_BORRAR_3			/* llego el 55, se espera el 30 en el sector */
} flash_estado = FE_LISTA;

void sistema_flash_escribir(DWORD offset, BYTE valor)
{
	if (flash_mem == NULL || offset >= FLASH_SIZE)
		return;

	switch (flash_estado)
	{
		case FE_LISTA:
			if (offset == FLASH_CMD_1 && valor == 0xAA)
				flash_estado = FE_DESBLOQUEO;
			break;

		case FE_DESBLOQUEO:
			flash_estado = (offset == FLASH_CMD_2 && valor == 0x55)
				? FE_COMANDO : FE_LISTA;
			break;

		case FE_COMANDO:
			if (offset != FLASH_CMD_1)
				flash_estado = FE_LISTA;
			else
			if (valor == 0xA0)
				flash_estado = FE_PROGRAMAR;
			else
			if (valor == 0x80)
				flash_estado = FE_BORRAR_1;
			else
				flash_estado = FE_LISTA;
			break;

		case FE_PROGRAMAR:
			/* Programar solo baja bits. */
			flash_mem[offset] &= valor;
			flash_sucia = 1;
			flash_estado = FE_LISTA;
			break;

		case FE_BORRAR_1:
			flash_estado = (offset == FLASH_CMD_1 && valor == 0xAA)
				? FE_BORRAR_2 : FE_LISTA;
			break;

		case FE_BORRAR_2:
			flash_estado = (offset == FLASH_CMD_2 && valor == 0x55)
				? FE_BORRAR_3 : FE_LISTA;
			break;

		case FE_BORRAR_3:
			if (valor == 0x30)
			{
				/* Borrado de sector: todo a 1, que es el estado virgen. */
				DWORD base = offset & ~(DWORD) (FLASH_SECTOR - 1);

				memset(&flash_mem[base], 0xFF, FLASH_SECTOR);
				flash_sucia = 1;
			}
			else
			if (valor == 0x10)
			{
				/* Borrado del chip entero. */
				memset(flash_mem, 0xFF, FLASH_SIZE);
				flash_sucia = 1;
			}

			flash_estado = FE_LISTA;
			break;
	}
}

int sistema_flash_guardar(void)
{
	FILE * fp;

	if (!flash_sucia || flash_mem == NULL || flash_ruta == NULL)
		return 0;

	fp = fopen(flash_ruta, "wb");

	if (fp == NULL)
	{
		fprintf(stderr, "no se pudo guardar la flash en %s\n", flash_ruta);
		return 1;
	}

	fwrite(flash_mem, 1, FLASH_SIZE, fp);
	fclose(fp);

	flash_sucia = 0;

	fprintf(stderr, "flash guardada en %s\n", flash_ruta);

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Reloj de tiempo real                                                     */
/* ------------------------------------------------------------------------ */

static DWORD	rtc_latch = 0;
static int		rtc_latcheado = 0;

/*
	Desfase entre el reloj del anfitrion y el que el guest puso en hora. Sin
	esto las escrituras al RTC se perdian, y la BIOS volvia a pedir la fecha en
	cada arranque: no conseguia dejarla puesta nunca.

	Se guarda al lado de la flash, en el mismo directorio, porque es informacion
	de la misma naturaleza -- estado de la consola, no del emulador.
*/
static long		rtc_desfase = 0;
static int		rtc_desfase_sucio = 0;

DWORD sistema_rtc_desde_hora(time_t t)
{
	if (t < 0)
		t = 0;

	return (DWORD) ((long long) t + RTC_EPOCA_1950 + rtc_desfase);
}

/*
	El guest escribe el reloj en dos mitades de 16 bits, y hasta que no llegan
	las dos no hay una fecha completa. Se junta aca y se convierte en un desfase
	contra el reloj del anfitrion, para que el tiempo siga corriendo en vez de
	quedarse congelado en el instante en que lo pusieron en hora.
*/
static WORD	rtc_escrito_alto = 0;
static int	rtc_tiene_alto = 0;

void sistema_rtc_escribir_alto(WORD valor)
{
	rtc_escrito_alto = valor;
	rtc_tiene_alto = 1;
}

void sistema_rtc_escribir_bajo(WORD valor)
{
	DWORD	pedido;
	DWORD	ahora;

	if (!rtc_tiene_alto)
		return;

	rtc_tiene_alto = 0;

	pedido = ((DWORD) rtc_escrito_alto << 16) | valor;

	/* El desfase se calcula contra el reloj crudo, sin el desfase anterior:
	   lo que el guest pidio es la hora absoluta que quiere ver. */
	ahora = (DWORD) ((long long) time(NULL) + RTC_EPOCA_1950);

	rtc_desfase = (long) ((long long) pedido - (long long) ahora);
	rtc_desfase_sucio = 1;

	fprintf(stderr, "rtc: el guest puso el reloj en %lu (desfase %ld s)\n",
		(unsigned long) pedido, rtc_desfase);
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

/* ------------------------------------------------------------------------ */
/* Persistencia del desfase del reloj                                       */
/* ------------------------------------------------------------------------ */

/*
	El desfase va en un archivo de texto al lado de la flash. Es un solo numero
	y en texto se puede mirar y borrar a mano, que para un ajuste de este
	tamano vale mas que un formato binario.
*/
#define RTC_ARCHIVO		"bios/rtc.txt"

void sistema_rtc_cargar(void)
{
	FILE * fp = fopen(RTC_ARCHIVO, "r");
	long   valor;

	if (fp == NULL)
		return;

	if (fscanf(fp, "%ld", &valor) == 1)
		rtc_desfase = valor;

	fclose(fp);
}

int sistema_rtc_guardar(void)
{
	FILE * fp;

	if (!rtc_desfase_sucio)
		return 0;

	fp = fopen(RTC_ARCHIVO, "w");

	if (fp == NULL)
	{
		fprintf(stderr, "no se pudo guardar el desfase del reloj en %s\n", RTC_ARCHIVO);
		return 1;
	}

	fprintf(fp, "%ld\n", rtc_desfase);
	fclose(fp);

	rtc_desfase_sucio = 0;

	return 0;
}
