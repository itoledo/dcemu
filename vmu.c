/****************************************************************************

	VMU - la Visual Memory del puerto A, ranura 1

	Ver vmu.h. Los formatos vienen del driver de KOS (maple/vmu.c y
	dc/vmufs.h), que es el codigo que va a parsear lo que este archivo
	conteste; el descriptor y la geometria son los de una VMU de serie.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmu.h"

/* Palabra de 32 bits propia para no arrastrar main.h: unsigned int mide 32
   en MSVC y en gcc de Linux por igual. */
typedef unsigned int vmu_u32;
typedef unsigned short vmu_u16;

/* Comandos y respuestas del bus, numeracion de maple.h de KOS. */
#define CMD_DEVINFO		1
#define CMD_GETCOND		9
#define CMD_GETMINFO	10
#define CMD_BREAD		11
#define CMD_BWRITE		12
#define CMD_BSYNC		13
#define CMD_SETCOND		14

#define RESP_DEVINFO	0x05
#define RESP_OK			0x07
#define RESP_DATOS		0x08
#define RESP_FUNC_MALA	0xFE	/* -2 */
#define RESP_CMD_MALO	0xFD	/* -3 */
#define RESP_ARCHIVO	0xFB	/* -5, error de archivo */

#define FUNC_MEMORIA	0x02000000
#define FUNC_LCD		0x04000000
#define FUNC_RELOJ		0x08000000

static unsigned char *	vmu_mem = NULL;
static const char *		vmu_ruta = NULL;
static int				vmu_sucia = 0;

/* ------------------------------------------------------------------------ */
/* Formateo                                                                 */
/* ------------------------------------------------------------------------ */

static void poner_u16(unsigned char * p, vmu_u16 v)
{
	p[0] = (unsigned char) (v & 0xFF);
	p[1] = (unsigned char) (v >> 8);
}

/*
	Una tarjeta recien formateada, como la deja el boot ROM: el bloque raiz
	con sus 16 bytes 0x55, la FAT con los bloques de sistema encadenados y el
	resto libre, y el directorio en cero.

	La fecha del formateo es fija a proposito --1999-09-09, el lanzamiento
	americano-- porque una corrida del reproductor determinista tiene que
	salir byte a byte identica, y la hora del anfitrion es la clase de azar
	que ya partio una vez las corridas en dos lineas de tiempo (SB_SBREV).
*/
static void vmu_formatear(void)
{
	unsigned char *	raiz = vmu_mem + VMU_BLOQUE_RAIZ * VMU_BLOQUE_TAM;
	unsigned char *	fat  = vmu_mem + VMU_BLOQUE_FAT  * VMU_BLOQUE_TAM;
	int				i;

	memset(vmu_mem, 0, VMU_TAM);

	/* Bloque raiz (vmufs.h: vmu_root_t). */
	memset(raiz, 0x55, 16);				/* la marca de "formateada" */
	raiz[0x10] = 0;						/* sin color propio */

	raiz[0x30] = 0x19;					/* BCD: 1999-09-09 09:09:09, jueves */
	raiz[0x31] = 0x99;
	raiz[0x32] = 0x09;
	raiz[0x33] = 0x09;
	raiz[0x34] = 0x09;
	raiz[0x35] = 0x09;
	raiz[0x36] = 0x09;
	raiz[0x37] = 0x03;					/* dia de semana, 0 = lunes */

	poner_u16(raiz + 0x44, VMU_BLOQUES - 1);	/* tamano total, en el campo sin nombre */
	poner_u16(raiz + 0x46, VMU_BLOQUE_FAT);
	poner_u16(raiz + 0x48, 1);					/* la FAT ocupa un bloque */
	poner_u16(raiz + 0x4A, VMU_BLOQUE_DIR);
	poner_u16(raiz + 0x4C, VMU_DIR_BLOQUES);
	poner_u16(raiz + 0x4E, 0);					/* icono */
	poner_u16(raiz + 0x50, VMU_BLOQUES_USUARIO);

	/* FAT: todo libre, y encima los bloques de sistema. El directorio es una
	   cadena que baja del 253 al 241; raiz y FAT terminan en si mismos. */
	for (i = 0; i < VMU_BLOQUES; i++)
		poner_u16(fat + i * 2, VMU_FAT_LIBRE);

	poner_u16(fat + VMU_BLOQUE_RAIZ * 2, VMU_FAT_ULTIMO);
	poner_u16(fat + VMU_BLOQUE_FAT  * 2, VMU_FAT_ULTIMO);

	for (i = VMU_BLOQUE_DIR; i > VMU_BLOQUE_DIR - VMU_DIR_BLOQUES + 1; i--)
		poner_u16(fat + i * 2, (vmu_u16) (i - 1));

	poner_u16(fat + (VMU_BLOQUE_DIR - VMU_DIR_BLOQUES + 1) * 2, VMU_FAT_ULTIMO);
}

/* ------------------------------------------------------------------------ */
/* Carga y persistencia, el mismo dibujo que la flash (sistema.c)           */
/* ------------------------------------------------------------------------ */

int vmu_iniciar(const char * ruta)
{
	FILE *	fp;
	size_t	leidos = 0;

	if (vmu_mem == NULL)
	{
		vmu_mem = (unsigned char *) malloc(VMU_TAM);

		if (vmu_mem == NULL)
		{
			fprintf(stderr, "No se pudo crear la memoria de la VMU.\n");
			return 1;
		}
	}

	vmu_ruta  = ruta;
	vmu_sucia = 0;

	fp = ruta ? fopen(ruta, "rb") : NULL;

	if (fp != NULL)
	{
		leidos = fread(vmu_mem, 1, VMU_TAM, fp);
		fclose(fp);
	}

	if (leidos != VMU_TAM)
	{
		/* Sin archivo, o cortado: tarjeta nueva. Queda sucia para que el
		   archivo aparezca tras la primera corrida. */
		vmu_formatear();
		vmu_sucia = (ruta != NULL);

		if (ruta != NULL)
			fprintf(stderr, "VMU: %s no esta o esta corto;"
				" tarjeta vacia formateada.\n", ruta);
	}

	return 0;
}

void vmu_guardar(void)
{
	FILE * fp;

	if (!vmu_sucia || vmu_mem == NULL || vmu_ruta == NULL)
		return;

	fp = fopen(vmu_ruta, "wb");

	if (fp == NULL)
	{
		fprintf(stderr, "VMU: no se pudo escribir %s.\n", vmu_ruta);
		return;
	}

	fwrite(vmu_mem, 1, VMU_TAM, fp);
	fclose(fp);

	vmu_sucia = 0;
}

int vmu_presente(void)
{
	return vmu_mem != NULL;
}

unsigned char * vmu_imagen(void)
{
	return vmu_mem;
}

/* ------------------------------------------------------------------------ */
/* El protocolo Maple                                                       */
/* ------------------------------------------------------------------------ */

/*
	El descriptor de una Visual Memory de serie, con los mismos valores que
	reporta el hardware: las tres funciones (almacenamiento, LCD y reloj) y
	sus palabras de datos en orden del bit mas alto al mas bajo -- el reloj
	primero, que es donde KOS busca el 0x403F7E7E para distinguir una VMU
	oficial de una tarjeta de terceros. Los nombres van rellenos con espacios
	hasta el largo del campo, como en el bus, no terminados en NUL.
*/
static int devinfo(unsigned char * d)
{
	static const vmu_u32 cabeza[4] =
	{
		FUNC_MEMORIA | FUNC_LCD | FUNC_RELOJ,
		0x403F7E7E,			/* reloj: botones y demas */
		0x00100500,			/* LCD: 48x32, un plano */
		0x00410F00			/* almacenamiento: 512 por bloque, 4 fases */
	};
	static const char nombre[]  = "Visual Memory";
	static const char licencia[] =
		"Produced By or Under License From SEGA ENTERPRISES,LTD.";

	memset(d, 0, 112);
	memcpy(d, cabeza, sizeof(cabeza));

	d[16] = 0xFF;			/* area: todas */
	d[17] = 0;				/* direccion del conector */

	memset(d + 18, ' ', 30);
	memcpy(d + 18, nombre, sizeof(nombre) - 1);

	memset(d + 48, ' ', 60);
	memcpy(d + 48, licencia, sizeof(licencia) - 1);

	d[108] = 0x7C;			/* consumo en espera: 12,4 mA */
	d[109] = 0x00;
	d[110] = 0x82;			/* consumo maximo: 13 mA */
	d[111] = 0x00;

	return 112 / 4;
}

/*
	La geometria (GETMINFO): los mismos numeros que el bloque raiz, en el
	orden de la respuesta de una VMU real. Un guest puede leer cualquiera de
	los dos y tienen que decir lo mismo.
*/
static int meminfo(unsigned char * d)
{
	poner_u16(d +  0, VMU_BLOQUES - 1);			/* ultimo bloque */
	poner_u16(d +  2, 0);						/* particion */
	poner_u16(d +  4, VMU_BLOQUE_RAIZ);
	poner_u16(d +  6, VMU_BLOQUE_FAT);
	poner_u16(d +  8, 1);						/* bloques de FAT */
	poner_u16(d + 10, VMU_BLOQUE_DIR);
	poner_u16(d + 12, VMU_DIR_BLOQUES);
	d[14] = 0;									/* icono del volumen */
	d[15] = 0;
	poner_u16(d + 16, VMU_BLOQUES_USUARIO);		/* area de guardado */
	poner_u16(d + 18, 31);						/* bloques de esa area */
	poner_u16(d + 20, 0);
	poner_u16(d + 22, 0);

	return 24 / 4;
}

/* El blkid: bloque partido en bytes, fase y particion. */
static unsigned bloque_de(vmu_u32 blkid)
{
	return ((blkid >> 24) & 0xFF) | (((blkid >> 16) & 0xFF) << 8);
}

static unsigned fase_de(vmu_u32 blkid)
{
	return (blkid >> 8) & 0xFF;
}

int vmu_maple(const void * paquete, int tam_palabras,
			  void * respuesta, int resp_max_palabras)
{
	const unsigned char *	pq = (const unsigned char *) paquete;
	unsigned char *			rp = (unsigned char *) respuesta;

	vmu_u32		encabezado, func = 0, blkid = 0;
	unsigned	cmd, destino, remite;
	unsigned	codigo = RESP_CMD_MALO;
	int			datos = 0;			/* palabras de respuesta tras el encabezado */

	if (vmu_mem == NULL || tam_palabras < 1 || resp_max_palabras < 1)
		return 0;

	memcpy(&encabezado, pq, 4);

	cmd     = encabezado & 0xFF;
	destino = (encabezado >> 8)  & 0xFF;	/* nosotros */
	remite  = (encabezado >> 16) & 0xFF;	/* el anfitrion */

	if (tam_palabras >= 2)
		memcpy(&func, pq + 4, 4);

	if (tam_palabras >= 3)
		memcpy(&blkid, pq + 8, 4);

	switch (cmd)
	{
		case CMD_DEVINFO:
		codigo = RESP_DEVINFO;
		datos  = devinfo(rp + 4);
		break;

		case CMD_GETCOND:
		/* Los botones de la VMU viven en la funcion reloj. 1 es suelto. */
		if (func == FUNC_RELOJ)
		{
			vmu_u32 cond = 0x000000FF;

			codigo = RESP_DATOS;
			memcpy(rp + 4, &func, 4);
			memcpy(rp + 8, &cond, 4);
			datos = 2;
		}
		else
			codigo = RESP_FUNC_MALA;
		break;

		case CMD_GETMINFO:
		if (func == FUNC_MEMORIA)
		{
			codigo = RESP_DATOS;
			memcpy(rp + 4, &func, 4);
			datos = 1 + meminfo(rp + 8);
		}
		else
			codigo = RESP_FUNC_MALA;
		break;

		case CMD_BREAD:
		if (func == FUNC_MEMORIA)
		{
			unsigned bloque = bloque_de(blkid);

			if (bloque >= VMU_BLOQUES || fase_de(blkid) != 0)
				codigo = RESP_ARCHIVO;
			else
			{
				/* El bloque entero en una fase, con el blkid de vuelta:
				   el driver lo compara contra el que pidio. */
				codigo = RESP_DATOS;
				memcpy(rp + 4, &func, 4);
				memcpy(rp + 8, &blkid, 4);
				memcpy(rp + 12, vmu_mem + bloque * VMU_BLOQUE_TAM,
					VMU_BLOQUE_TAM);
				datos = 2 + VMU_BLOQUE_TAM / 4;
			}
		}
		else
		if (func == FUNC_RELOJ)
		{
			/* La fecha, fija: ver vmu_formatear(). Anio de 16 bits y BCD no:
			   este va en binario (vmu_datetime_t de KOS). */
			static const unsigned char fecha[8] =
				{ 0xCF, 0x07, 9, 9, 9, 9, 9, 3 };

			codigo = RESP_DATOS;
			memcpy(rp + 4, &func, 4);
			memcpy(rp + 8, fecha, 8);
			datos = 3;
		}
		else
			codigo = RESP_FUNC_MALA;
		break;

		case CMD_BWRITE:
		if (func == FUNC_MEMORIA)
		{
			unsigned bloque = bloque_de(blkid);
			unsigned fase   = fase_de(blkid);

			/* Un cuarto de bloque por fase: asi escribe la flash de verdad,
			   y asi manda los datos el driver. */
			if (bloque >= VMU_BLOQUES || fase >= 4
			||  tam_palabras < 3 + (VMU_BLOQUE_TAM / 4) / 4)
				codigo = RESP_ARCHIVO;
			else
			{
				memcpy(vmu_mem + bloque * VMU_BLOQUE_TAM
						+ fase * (VMU_BLOQUE_TAM / 4),
					pq + 12, VMU_BLOQUE_TAM / 4);

				vmu_sucia = 1;
				codigo = RESP_OK;
			}
		}
		else
		if (func == FUNC_LCD || func == FUNC_RELOJ)
		{
			/* El dibujo del LCD y la puesta en hora se aceptan y se
			   descartan: no hay pantallita y la fecha es fija. Contestar OK
			   es lo que evita que el driver reintente cuatro veces. */
			codigo = RESP_OK;
		}
		else
			codigo = RESP_FUNC_MALA;
		break;

		case CMD_BSYNC:
		/* El "gracias Nagra" de KOS: cierra la escritura del bloque. Es el
		   momento natural de persistir. */
		vmu_guardar();
		codigo = RESP_OK;
		break;

		case CMD_SETCOND:
		/* El zumbador. Se acepta y no suena. */
		codigo = RESP_OK;
		break;

		default:
		codigo = RESP_CMD_MALO;
		break;
	}

	if (1 + datos > resp_max_palabras)
		return 0;

	/* El encabezado de vuelta: destinatario y remitente intercambiados. El
	   remitente somos nosotros con la direccion por la que nos llamaron --
	   0x01, puerto A ranura 1 -- sin bitmap: eso es cosa del dispositivo
	   principal. */
	encabezado = codigo | (remite << 8) | (destino << 16)
			   | ((vmu_u32) datos << 24);
	memcpy(rp, &encabezado, 4);

	return 1 + datos;
}
