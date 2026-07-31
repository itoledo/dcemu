/****************************************************************************

	GDROM - ver gdrom.h.

	La secuencia completa de un comando del modo paquete, que es lo que el boot
	ROM ejecuta una y otra vez:

	  1. el host deja el byte count en BYCTLLO/HI y las banderas en FEATURES;
	  2. escribe 0xA0 en COMMAND; la lectora levanta DRQ con CoD=1 e IO=0,
	     que quiere decir "mandame el paquete";
	  3. el host escribe los 12 bytes por el registro de datos;
	  4. la lectora ejecuta el comando y, si hay respuesta, levanta DRQ con
	     CoD=0 e IO=1 e interrumpe;
	  5. el host lee bloques de hasta byte count bytes hasta agotar la
	     respuesta; al terminar la lectora baja DRQ, deja CoD=1 e IO=1 y
	     vuelve a interrumpir.

	Si FEATURES tiene el bit 0 la transferencia no va por el registro de datos
	sino por el DMA del G2: la respuesta queda esperando y sale cuando el host
	escribe 1 en SB_GDST.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "gdrom.h"
#include "intc.h"
#include "iso.h"
#include "opciones.h"
#include "traza.h"

struct gdrom_t gdrom;

/* Sentido del ultimo error, para REQ_ERROR. */
static BYTE sentido_clave = GD_SENTIDO_OK;
static BYTE sentido_asc   = 0;
static BYTE sentido_ascq  = 0;

/* ------------------------------------------------------------------------ */
/* Buffer de respuesta                                                      */
/* ------------------------------------------------------------------------ */

static int reservar(int n)
{
	if (n <= gdrom.datos_cap)
		return 1;

	{
		BYTE * nuevo = (BYTE *) realloc(gdrom.datos, (size_t) n);

		if (nuevo == NULL)
		{
			logmsg("gdrom: no se pudo reservar %d bytes\n", n);
			return 0;
		}

		gdrom.datos     = nuevo;
		gdrom.datos_cap = n;
	}

	return 1;
}

/* ------------------------------------------------------------------------ */
/* Estado de la unidad                                                      */
/* ------------------------------------------------------------------------ */

static int hay_disco(void)
{
	return gdrom.unidad != GD_NODISC && gdrom.unidad != GD_OPEN;
}

void gdrom_iniciar(int bandeja)
{
	/* El buffer de respuesta sobrevive al reset: es del emulador, no de la
	   lectora, y volver a reservarlo en cada soft reset lo perderia. */
	BYTE *	buffer = gdrom.datos;
	int		capacidad = gdrom.datos_cap;

	memset(&gdrom, 0, sizeof(gdrom));

	gdrom.datos       = buffer;
	gdrom.datos_cap   = capacidad;
	gdrom.paquete_pos = -1;
	gdrom.estado      = GD_ST_DRDY | GD_ST_DSC;

	switch (bandeja)
	{
		case BANDEJA_ABIERTA:	gdrom.unidad = GD_OPEN;		break;
		case BANDEJA_VACIA:		gdrom.unidad = GD_NODISC;	break;
		case BANDEJA_DISCO:		gdrom.unidad = GD_STANDBY;	break;

		default:
			/* AUTO: hay disco si hay imagen montada. */
			gdrom.unidad = iso_hay_disco() ? GD_STANDBY : GD_NODISC;
			break;
	}

	/*
		Un .iso plano es una sola pista de datos, o sea un CD-ROM. Un .cdi de
		juego trae el area de alta densidad que empieza en el FAD 45150, y ese
		si es un GD-ROM -- decirlo importa, porque el boot ROM decide con esto
		si el disco es arrancable o si le abre el reproductor de CD.

		Y un selfboot -- la conversion a CD con que circulan los juegos: sesion
		de audio mas sesion de datos en modo 2 -- se graba como **CD-ROM XA**,
		que es el unico formato de CD desde el que el boot ROM acepta arrancar
		(el hueco MIL-CD). Reportarlo como CD-ROM plano manda el disco al
		reproductor de musica aunque las sesiones esten bien contestadas.
	*/
	if (!hay_disco())
		gdrom.formato = GD_DISCO_CDDA;
	else
	if (iso_es_gdrom())
		gdrom.formato = GD_DISCO_GDROM;
	else
		gdrom.formato = (iso_num_sesiones() > 1) ? GD_DISCO_CDROM_XA
												 : GD_DISCO_CDROM;

	sentido_clave = GD_SENTIDO_OK;
	sentido_asc   = 0;
	sentido_ascq  = 0;

	logmsg("gdrom: unidad en estado %d, formato %d\n", gdrom.unidad, gdrom.formato);

	if (traza_activa)
		fprintf(stderr, "gdrom: estado_disco: unidad=%d formato=%d "
			"(gdrom=%d sesiones=%d pistas=%d)\n",
			gdrom.unidad, gdrom.formato, iso_es_gdrom(),
			iso_num_sesiones(), iso_num_pistas());
}

/* ------------------------------------------------------------------------ */
/* TOC                                                                      */
/* ------------------------------------------------------------------------ */

/* Una entrada de la TOC: control en los bits 31-28, addr en 27-24 y el FAD en
   los 24 bajos. Las entradas de primera y ultima pista llevan el numero de
   pista en los bits 23-16 en vez del FAD. */
#define TOC_ENTRADA(ctrl, addr, fad) \
	((DWORD) ((((ctrl) & 0xF) << 28) | (((addr) & 0xF) << 24) | ((fad) & 0xFFFFFF)))

#define TOC_PISTA(ctrl, addr, pista) \
	((DWORD) ((((ctrl) & 0xF) << 28) | (((addr) & 0xF) << 24) | (((pista) & 0xFF) << 16)))

#define TOC_CTRL_DATOS		0x4		/* pista de datos, sin pre-enfasis */
#define TOC_ADDR_POSICION	0x1		/* el campo lleva una posicion */

#define TOC_VACIA			0xFFFFFFFF

/*
	El FAD donde empieza el area de alta densidad de un GD-ROM. Es fijo por
	norma, y es lo que separa las dos sesiones del disco.
*/
#define GD_FAD_ALTA_DENSIDAD	45150

/*
	EXPERIMENTO: donde termina el area de alta densidad del GD-ROM original.
	El boot ROM compara la TOC que le contesta la lectora con la copia que el
	IP.BIN lleva en su offset 0x100 (firma "TOC1"), y ahi el lead-out es el del
	disco prensado, no el del CD en que se convirtio.
*/
static int gdrom_leadout_gd(void)
{
	const char * v = getenv("DCEMU_LEADOUT");

	return v ? (int) strtol(v, NULL, 0) : 549300;
}

/*
	La TOC de un area del disco.

	  area 0  densidad simple: las pistas por debajo del FAD 45150
	  area 1  alta densidad: las de ahi en adelante, donde vive el juego

	Antes esto armaba siempre una TOC de una sola pista con todo el disco, que
	es lo que corresponde a un .iso plano. Con un .cdi de juego el boot ROM
	preguntaba por las dos areas, recibia la misma respuesta y concluia que no
	habia juego: "please insert game disc".
*/
void gdrom_construir_toc_area(struct TOC * toc, int area)
{
	int		i, n = iso_num_pistas();
	int		primera = -1, ultima = -1, pistas = 0;
	DWORD	fin = 0;

	for (i = 0; i < 99; i++)
		toc->entry[i] = TOC_VACIA;

	toc->first = TOC_PISTA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, 1);
	toc->last  = TOC_PISTA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, 1);
	toc->dunno = TOC_VACIA;

	for (i = 0; i < n && i < 99; i++)
	{
		int fad = iso_pista_fad(i);
		int alta = (fad >= GD_FAD_ALTA_DENSIDAD);

		if (alta != (area != 0))
			continue;

		if (primera < 0)
			primera = i;

		ultima = i;
		pistas++;

		toc->entry[i] = TOC_ENTRADA(
			iso_pista_es_datos(i) ? TOC_CTRL_DATOS : 0,
			TOC_ADDR_POSICION, (DWORD) fad);

		fin = (DWORD) (fad + iso_pista_sectores(i));
	}

	if (pistas == 0)
		return;

	toc->first = TOC_PISTA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, primera + 1);
	toc->last  = TOC_PISTA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, ultima + 1);

	/* El campo que dcemu llamo "dunno" es la entrada de lead-out: donde
	   termina el area de datos. */
	toc->dunno = TOC_ENTRADA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, fin);

	/*
		EXPERIMENTO: presentando el selfboot como GD-ROM, el area de alta
		densidad tiene que verse como la del disco original, porque el boot ROM
		compara esta TOC con la copia que el IP.BIN lleva en su offset 0x100
		(la firma "TOC1"). En un GD-ROM la pista de datos es la **3** --las 1 y
		2 estan en el area de densidad simple-- y el lead-out queda mucho mas
		lejos que el final del CD.
	*/
	if (area != 0 && iso_gd_presentando() && pistas > 0)
	{
		toc->entry[2] = toc->entry[primera];

		if (primera != 2)
			toc->entry[primera] = TOC_VACIA;

		toc->first = TOC_PISTA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, 3);
		toc->last  = TOC_PISTA(TOC_CTRL_DATOS, TOC_ADDR_POSICION, 3);
		toc->dunno = TOC_ENTRADA(TOC_CTRL_DATOS, TOC_ADDR_POSICION,
			(DWORD) gdrom_leadout_gd());
	}

	if (traza_activa)
	{
		int j;

		fprintf(stderr, "gdrom: TOC area %d: pistas %d..%d, lead-out %lu\n",
			area, primera + 1, ultima + 1, (unsigned long) fin);

		for (j = 0; j < n && j < 99; j++)
			if (toc->entry[j] != TOC_VACIA)
				fprintf(stderr, "gdrom:   pista %d: %08lx (%s, fad %lu)\n",
					j + 1, (unsigned long) toc->entry[j],
					iso_pista_es_datos(j) ? "datos" : "audio",
					(unsigned long) iso_pista_fad(j));
	}
}

void gdrom_construir_toc(struct TOC * toc)
{
	/* Sin area: la del juego, que es la que pide el hook de syscall. */
	gdrom_construir_toc_area(toc, iso_es_gdrom() ? 1 : 0);
}

/* ------------------------------------------------------------------------ */
/* Transiciones de la maquina de estados                                    */
/* ------------------------------------------------------------------------ */

static void interrumpir(void)
{
	intc_add_ext(ASIC_EVT_EXT_GDROM);
}

/* Fin de comando sin datos pendientes. */
static void fin_comando(void)
{
	if (traza_activa && gdrom.datos_tam > 0)
		fprintf(stderr, "gdrom: fin de comando, entregados %d de %d bytes\n",
			gdrom.datos_pos, gdrom.datos_tam);

	gdrom.estado      = GD_ST_DRDY | GD_ST_DSC;
	gdrom.razon       = GD_IR_COD | GD_IR_IO;
	gdrom.paquete_pos = -1;
	gdrom.datos_tam   = 0;
	gdrom.datos_pos   = 0;
	gdrom.bloque      = 0;
	gdrom.por_dma     = 0;
	gdrom.esperando_dma = 0;

	interrumpir();
}

static void fallar(BYTE clave, BYTE asc, BYTE ascq)
{
	sentido_clave = clave;
	sentido_asc   = asc;
	sentido_ascq  = ascq;

	gdrom.error       = (BYTE) (clave << 4);
	gdrom.estado      = GD_ST_DRDY | GD_ST_DSC | GD_ST_CHECK;
	gdrom.razon       = GD_IR_COD | GD_IR_IO;
	gdrom.paquete_pos = -1;
	gdrom.datos_tam   = 0;
	gdrom.datos_pos   = 0;
	gdrom.bloque      = 0;
	gdrom.por_dma     = 0;
	gdrom.esperando_dma = 0;

	interrumpir();
}

/* Arranca el bloque DRQ siguiente de una transferencia hacia el host. */
static void siguiente_bloque(void)
{
	int restan = gdrom.datos_tam - gdrom.datos_pos;

	if (restan <= 0)
	{
		fin_comando();
		return;
	}

	/* El limite del host acota el tamano de cada bloque. Un limite de cero no
	   es valido en ATAPI; se toma como "todo de una". */
	gdrom.bloque = (gdrom.limite > 0 && restan > gdrom.limite)
		? gdrom.limite
		: restan;

	/* Y el host lee por esa misma direccion cuanto va en este bloque. */
	gdrom.contador = (WORD) gdrom.bloque;

	gdrom.estado = GD_ST_DRDY | GD_ST_DSC | GD_ST_DRQ;
	gdrom.razon  = GD_IR_IO;					/* datos, lectora -> host */

	interrumpir();
}

/* Deja lista una respuesta y arranca la transferencia. desde/largo son los
   que vienen en el paquete: casi todos los comandos SPI dejan pedir un tramo
   de la respuesta en vez de la respuesta entera. */
static void entregar(const void * origen, int total, int desde, int largo)
{
	if (desde < 0 || desde > total)
		desde = 0;

	if (largo <= 0 || largo > total - desde)
		largo = total - desde;

	if (largo <= 0)
	{
		fin_comando();
		return;
	}

	if (!reservar(largo))
	{
		fallar(GD_SENTIDO_ABORTADO, 0, 0);
		return;
	}

	memcpy(gdrom.datos, (const BYTE *) origen + desde, (size_t) largo);

	gdrom.datos_tam = largo;
	gdrom.datos_pos = 0;
	gdrom.error     = 0;

	if (gdrom.por_dma)
	{
		/* Con DMA no hay bloques DRQ: los datos quedan esperando a que el
		   host dispare SB_GDST. */
		gdrom.esperando_dma = 1;
		gdrom.estado = GD_ST_DRDY | GD_ST_DSC | GD_ST_DRQ;
		gdrom.razon  = GD_IR_IO;
		return;
	}

	siguiente_bloque();
}

/* ------------------------------------------------------------------------ */
/* Respuestas fijas                                                         */
/* ------------------------------------------------------------------------ */

/*
	REQ_MODE: 32 bytes con la configuracion de la lectora. Los primeros diez
	son parametros (velocidad, tiempo de espera, reintentos) y los otros 22 la
	identificacion del firmware: "SE      Rev 6.43990316".
*/
static BYTE modo[32] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0xB4, 0x19, 0x00,
	0x00, 0x08, 0x53, 0x45, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x52, 0x65, 0x76, 0x20, 0x36, 0x2E,
	0x34, 0x33, 0x39, 0x39, 0x30, 0x33, 0x31, 0x36
};

/*
	IDENTIFY DEVICE. El boot ROM no lo usa -- lo usan los sistemas operativos
	que hablan ATAPI generico, como Linux --, asi que alcanza con el nombre del
	fabricante y del modelo en las posiciones donde ATA los pone.
*/
static void identificar(BYTE * dest, int tam)
{
	memset(dest, 0, (size_t) tam);

	if (tam >= 0x50)
	{
		memcpy(&dest[0x00], "SE      ", 8);		/* fabricante */
		memcpy(&dest[0x08], "CD-ROM DRIVE    ", 16);
		memcpy(&dest[0x18], "6.43", 4);
	}
}

/* ------------------------------------------------------------------------ */
/* Comandos del modo paquete                                                */
/* ------------------------------------------------------------------------ */

static void cmd_req_stat(const BYTE * p)
{
	BYTE stat[10];

	memset(stat, 0, sizeof(stat));

	stat[0] = (BYTE) (gdrom.unidad & 0x0F);
	stat[1] = (BYTE) (gdrom.formato << 4);
	stat[2] = 0x04;						/* control de la pista: datos */
	stat[3] = 0x01;						/* numero de pista */
	stat[4] = 0x01;						/* indice */

	/* Posicion actual del cabezal, en FAD. */
	stat[5] = (BYTE) ((iso_get_lba() >> 16) & 0xFF);
	stat[6] = (BYTE) ((iso_get_lba() >>  8) & 0xFF);
	stat[7] = (BYTE) ( iso_get_lba()        & 0xFF);

	entregar(stat, (int) sizeof(stat), p[2], p[4]);
}

static void cmd_req_error(const BYTE * p)
{
	BYTE sentido[10];

	memset(sentido, 0, sizeof(sentido));

	sentido[0] = 0xF0;					/* codigo de error valido */
	sentido[2] = sentido_clave;
	sentido[8] = sentido_asc;
	sentido[9] = sentido_ascq;

	entregar(sentido, (int) sizeof(sentido), 0, p[4]);
}

static void cmd_req_ses(const BYTE * p)
{
	BYTE	ses[6];
	DWORD	fad;

	memset(ses, 0, sizeof(ses));

	ses[0] = (BYTE) (gdrom.unidad & 0x0F);

	/*
		Cuantas sesiones tiene el disco y donde empieza cada una lo decide
		iso.c: dos en un GD-ROM (densidad simple y alta) y dos en un selfboot
		(audio y datos, el truco MIL-CD de las conversiones a CD). Antes esto
		contestaba "una sesion" para todo lo que no fuera GD-ROM, y el boot
		ROM, viendo un disco de una sesion cuya primera pista es audio,
		concluia CD de musica y abria el reproductor.
	*/
	if (p[2] == 0)
	{
		/* Sesion 0: cuantas sesiones hay y donde termina la ultima. */
		int ultima = iso_num_pistas() - 1;

		ses[1] = (BYTE) iso_num_sesiones();
		fad = (DWORD) (iso_pista_fad(ultima) + iso_pista_sectores(ultima));
	}
	else
	{
		/*
			Una sesion concreta: el numero de su primera pista y su FAD de
			inicio, que es lo que el boot ROM usa para ir a buscarla.

			Pedir una que el disco no tiene es un error, y contestar datos en
			vez de fallar rompe la evaluacion del disco: el boot ROM camina
			las sesiones 1, 2, 3... esperando el CHECK CONDITION que le dice
			donde terminan.
		*/
		if ((int) p[2] > iso_num_sesiones())
		{
			fallar(GD_SENTIDO_ILEGAL, 0x24, 0x00);	/* campo invalido en el CDB */
			return;
		}

		ses[1] = (BYTE) iso_sesion_primera_pista((int) p[2]);
		fad = (DWORD) iso_sesion_fad((int) p[2]);
	}

	ses[2] = (BYTE) ((fad >> 16) & 0xFF);
	ses[3] = (BYTE) ((fad >>  8) & 0xFF);
	ses[4] = (BYTE) ( fad        & 0xFF);

	/* Con esto se ve lo que el boot ROM se lleva de cada sesion, que es con lo
	   que decide si el disco arranca o si abre el reproductor de musica. */
	if (traza_activa)
		fprintf(stderr, "gdrom: REQ_SES(%d) -> estado %d, %s %d, fad %lu\n",
			p[2], ses[0], p[2] ? "primera pista" : "sesiones", ses[1],
			(unsigned long) fad);

	entregar(ses, (int) sizeof(ses), 0, p[4]);
}

static void cmd_get_toc(const BYTE * p)
{
	struct TOC	toc;
	int			largo = (p[3] << 8) | p[4];

	/* El bit 0 del segundo byte elige el area: 0 densidad simple, 1 alta. */
	gdrom_construir_toc_area(&toc, p[1] & 1);

	entregar(&toc, (int) sizeof(toc), 0, largo);
}

static void cmd_cd_read(const BYTE * p)
{
	int fad      = (p[2] << 16) | (p[3] << 8) | p[4];
	int sectores = (p[8] << 16) | (p[9] << 8) | p[10];
	int total;

	if (sectores <= 0)
	{
		fin_comando();
		return;
	}

	total = sectores * 2048;

	if (!reservar(total))
	{
		fallar(GD_SENTIDO_ABORTADO, 0, 0);
		return;
	}

	logmsg("gdrom: CD_READ fad %d, %d sectores\n", fad, sectores);

	/*
		El area de alta densidad solo existe en un GD-ROM: en un CD no hay nada
		por encima del FAD 45150 y la lectora rechaza la peticion. Sin esto,
		iso_read_sector() servia lo que hubiera en esa posicion del archivo
		--que en una imagen grande es un sector real, solo que de otro sitio--
		y el boot ROM se llevaba basura en vez de un error.

		Importa para el arranque: el ROM prueba el area de alta densidad antes
		que nada y usa el fallo para descartarla. Recibiendo datos daba el disco
		por GD-ROM, encontraba que no cuadraban, reintentaba la busqueda entera
		cinco veces y terminaba abriendo el reproductor de CD sin haber mirado
		nunca la pista de datos del CD.
	*/
	if (!iso_es_gdrom() && fad >= GD_FAD_ALTA_DENSIDAD)
	{
		if (traza_activa)
			fprintf(stderr, "gdrom: CD_READ fad %d rechazado: el disco no "
				"tiene area de alta densidad\n", fad);

		/* 0x21: direccion de bloque fuera de rango. */
		fallar(GD_SENTIDO_ILEGAL, 0x21, 0x00);
		return;
	}

	if (iso_read_sector((char *) gdrom.datos, fad, sectores) < 0)
	{
		/* 0x11: error de lectura del medio. */
		fallar(GD_SENTIDO_NO_LISTA, 0x11, 0x00);
		return;
	}

	if (traza_activa)
	{
		int i;

		fprintf(stderr, "gdrom: CD_READ fad %d x%d ->", fad, sectores);

		for (i = 0; i < 16; i++)
			fprintf(stderr, " %02x", gdrom.datos[i]);

		fprintf(stderr, "  |");

		for (i = 0; i < 16; i++)
			fprintf(stderr, "%c", (gdrom.datos[i] >= 32 && gdrom.datos[i] < 127)
				? gdrom.datos[i] : '.');

		fprintf(stderr, "|\n");

		if (getenv("DCEMU_VER_SECTOR"))
		{
			fprintf(stderr, "        ");

			for (i = 0; i < 700 && i < total; i++)
				fprintf(stderr, "%c", (gdrom.datos[i] >= 32 && gdrom.datos[i] < 127)
					? gdrom.datos[i] : '.');

			fprintf(stderr, "\n");
		}
	}

	/* EXPERIMENTO: armar el volcado del anillo N instrucciones despues de
	   entregar cierto sector, para ver que hace el guest con lo que recibio. */
	if (traza_activa && getenv("DCEMU_DISPARO"))
	{
		const char * d = getenv("DCEMU_DISPARO");
		int quefad = atoi(d);
		const char * coma = strchr(d, ',');

		if (fad == quefad && coma)
			traza_disparo = strtol(coma + 1, NULL, 0);
	}

	gdrom.datos_tam = total;
	gdrom.datos_pos = 0;
	gdrom.error     = 0;
	gdrom.unidad    = GD_PAUSE;

	if (gdrom.por_dma)
	{
		gdrom.esperando_dma = 1;
		gdrom.estado = GD_ST_DRDY | GD_ST_DSC | GD_ST_DRQ;
		gdrom.razon  = GD_IR_IO;
		return;
	}

	siguiente_bloque();
}

static void ejecutar_paquete(void)
{
	const BYTE * p = gdrom.paquete;

	logmsg("gdrom: paquete %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);

	/* Con --traza-mem los paquetes salen por stderr: sin esto no hay forma de
	   saber si el boot ROM llego a hablarle a la lectora. El PC y el PR dicen
	   desde donde se mando, que es la punta del hilo para desensamblar al que
	   toma las decisiones -- asi se investigo la evaluacion de discos. */
	if (traza_activa)
		fprintf(stderr, "gdrom: paquete %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  (PC=%08lx PR=%08lx)\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11],
			(unsigned long) PC, (unsigned long) PR);

	/* Casi todo necesita disco. Los que no: pedir estado, pedir el error,
	   pedir el modo y abrir la bandeja. */
	if (!hay_disco())
	{
		switch (p[0])
		{
			case SPI_REQ_STAT:
			case SPI_REQ_ERROR:
			case SPI_REQ_MODE:
			case SPI_SET_MODE:
			case SPI_CD_OPEN:
				break;

			default:
				/* 0x3A: medio ausente. */
				fallar(GD_SENTIDO_NO_LISTA, 0x3A, 0x00);
				return;
		}
	}

	switch (p[0])
	{
		case SPI_TEST_UNIT:
			fin_comando();
			break;

		case SPI_REQ_STAT:
			cmd_req_stat(p);
			break;

		case SPI_REQ_MODE:
			entregar(modo, (int) sizeof(modo), p[2], p[4]);
			break;

		case SPI_SET_MODE:
			/* El host quiere escribir parametros. dcemu no tiene nada que
			   configurar, pero hay que aceptar el comando o el boot ROM
			   reintenta. */
			fin_comando();
			break;

		case SPI_REQ_ERROR:
			cmd_req_error(p);
			break;

		case SPI_GET_TOC:
			cmd_get_toc(p);
			break;

		case SPI_REQ_SES:
			cmd_req_ses(p);
			break;

		case SPI_CD_OPEN:
			gdrom.unidad  = GD_OPEN;
			gdrom.formato = GD_DISCO_CDDA;
			fin_comando();
			break;

		case SPI_CD_SEEK:
			gdrom.unidad = GD_PAUSE;
			fin_comando();
			break;

		case SPI_CD_READ:
		case SPI_CD_READ2:
			cmd_cd_read(p);
			break;

		case SPI_GET_SCD:
		{
			/* Subcodigo: el boot ROM lo pide para saber si hay audio sonando.
			   Se devuelve un bloque en cero, que significa "nada". */
			BYTE	scd[100];
			int		largo = (p[3] << 8) | p[4];

			memset(scd, 0, sizeof(scd));
			scd[1] = (BYTE) (gdrom.unidad & 0x0F);

			entregar(scd, (int) sizeof(scd), 0, largo);
		}
		break;

		case 0x70:
			/* Comando sin nombre publicado: es el que prepara el disco antes
			   de poder leerlo (el controlador de Linux lo llama
			   "prepare disk"). Deja la unidad lista. */
			gdrom.unidad = GD_PAUSE;
			fin_comando();
			break;

		case 0x71:
			/* Tampoco tiene nombre publicado: forma parte de la autenticacion
			   del disco y su respuesta es propia del firmware de la lectora.
			   El paquete no trae largo, asi que el que manda es el byte count
			   que dejo el host. Se contesta con ceros. */
		{
			int largo = gdrom.limite > 0 ? gdrom.limite : 32;

			if (getenv("DCEMU_71_FALLA") && !iso_es_gdrom())
			{
				fallar(GD_SENTIDO_ILEGAL, 0x20, 0x00);
				break;
			}

			if (!reservar(largo))
			{
				fallar(GD_SENTIDO_ABORTADO, 0, 0);
				break;
			}

			memset(gdrom.datos, 0, (size_t) largo);

			gdrom.datos_tam = largo;
			gdrom.datos_pos = 0;
			gdrom.error     = 0;

			if (gdrom.por_dma)
			{
				gdrom.esperando_dma = 1;
				gdrom.estado = GD_ST_DRDY | GD_ST_DSC | GD_ST_DRQ;
				gdrom.razon  = GD_IR_IO;
			}
			else
				siguiente_bloque();
		}
		break;

		default:
			logmsg("gdrom: comando SPI no implementado: %02x\n", p[0]);
			/* 0x20: codigo de operacion invalido. */
			fallar(GD_SENTIDO_ILEGAL, 0x20, 0x00);
			break;
	}
}

/* ------------------------------------------------------------------------ */
/* Comandos ATA                                                             */
/* ------------------------------------------------------------------------ */

static void ejecutar_ata(BYTE comando)
{
	if (traza_activa)
		fprintf(stderr, "gdrom: comando ATA %02x\n", comando);

	switch (comando)
	{
		case ATA_PACKET:
			/* Ahora vienen los 12 bytes del paquete. */
			gdrom.paquete_pos = 0;
			gdrom.por_dma     = gdrom.features & 0x01;
			gdrom.estado      = GD_ST_DRDY | GD_ST_DSC | GD_ST_DRQ;
			gdrom.razon       = GD_IR_COD;		/* comando, host -> lectora */
			memset(gdrom.paquete, 0, sizeof(gdrom.paquete));
			/* Esta fase no interrumpe: el host sondea DRQ. */
			break;

		case ATA_SOFT_RESET:
			logmsg("gdrom: soft reset\n");
			gdrom_iniciar(opciones.bandeja);
			break;

		case ATA_EXEC_DIAG:
			gdrom.error = 0x01;					/* diagnostico correcto */
			fin_comando();
			break;

		case ATA_IDENTIFY:
		{
			BYTE ident[0x50];

			identificar(ident, (int) sizeof(ident));
			gdrom.por_dma = 0;
			entregar(ident, (int) sizeof(ident), 0, (int) sizeof(ident));
		}
		break;

		case ATA_SET_FEATURES:
			fin_comando();
			break;

		case ATA_NOP:
			fallar(GD_SENTIDO_ABORTADO, 0, 0);
			break;

		default:
			logmsg("gdrom: comando ATA no implementado: %02x\n", comando);
			fallar(GD_SENTIDO_ABORTADO, 0x20, 0x00);
			break;
	}
}

/* ------------------------------------------------------------------------ */
/* Registro de datos                                                        */
/* ------------------------------------------------------------------------ */

static DWORD leer_datos(size_t size)
{
	DWORD	valor = 0;
	size_t	i;

	for (i = 0; i < size; i++)
	{
		BYTE b = 0;

		if (gdrom.bloque > 0 && gdrom.datos_pos < gdrom.datos_tam)
		{
			b = gdrom.datos[gdrom.datos_pos++];
			gdrom.bloque--;
		}

		valor |= ((DWORD) b) << (8 * i);
	}

	if (gdrom.bloque == 0 && (gdrom.estado & GD_ST_DRQ))
	{
		/* Se acabo el bloque: o viene otro, o se acabo el comando. */
		siguiente_bloque();
	}

	return valor;
}

static void escribir_datos(DWORD valor, size_t size)
{
	size_t i;

	if (gdrom.paquete_pos < 0)
	{
		/* Datos hacia la lectora fuera de un paquete: SET_MODE y poco mas.
		   No hay nada que guardar. */
		return;
	}

	for (i = 0; i < size && gdrom.paquete_pos < GDROM_PAQUETE_TAM; i++)
		gdrom.paquete[gdrom.paquete_pos++] = (BYTE) ((valor >> (8 * i)) & 0xFF);

	if (gdrom.paquete_pos >= GDROM_PAQUETE_TAM)
	{
		gdrom.paquete_pos = -1;
		gdrom.estado      = GD_ST_DRDY | GD_ST_DSC | GD_ST_BSY;
		ejecutar_paquete();
	}
}

/* ------------------------------------------------------------------------ */
/* DMA del G2                                                               */
/* ------------------------------------------------------------------------ */

static void disparar_dma(void)
{
	/* SB_GDSTAR lleva una direccion fisica alineada a 32 bytes. Se usa tal
	   cual: las zonas de P0 estan mapeadas, tanto la RAM del sistema (0x0C)
	   como la de video (0x04/0x05), que tambien es destino valido. */
	DWORD	destino = gdrom.dma_star & 0x1FFFFFE0;
	int		largo;

	if (!gdrom.esperando_dma)
	{
		logmsg("gdrom: SB_GDST sin datos pendientes\n");
		gdrom.dma_st = 0;
		return;
	}

	largo = gdrom.datos_tam - gdrom.datos_pos;

	if (gdrom.dma_len > 0 && (DWORD) largo > gdrom.dma_len)
		largo = (int) gdrom.dma_len;

	logmsg("gdrom: DMA de %d bytes a %08x\n", largo, destino);

	if (largo > 0)
	{
		/* SB_GDSTAR es una direccion fisica que programa el guest, y esto se
		   dispara desde una escritura a registro, o sea dentro de una
		   instruccion: no debe pasar por la MMU. */
		memwrite_fisico(destino, &gdrom.datos[gdrom.datos_pos], (size_t) largo);
		gdrom.datos_pos += largo;
	}

	gdrom.dma_st = 0;
	gdrom.esperando_dma = 0;

	intc_add(ASIC_EVT_GDROM_DMA, 0);

	fin_comando();
}

/* ------------------------------------------------------------------------ */
/* Acceso a los registros                                                   */
/* ------------------------------------------------------------------------ */

/* Nombre de cada registro, para la traza. */
static const char * nombre_registro(unsigned long fisica)
{
	if (fisica >= GDROM_DMA_BASE)
	{
		switch (fisica - GDROM_DMA_BASE)
		{
			case GDROM_DMA_STAR:	return "SB_GDSTAR";
			case GDROM_DMA_LEN:		return "SB_GDLEN";
			case GDROM_DMA_DIR:		return "SB_GDDIR";
			case GDROM_DMA_EN:		return "SB_GDEN";
			case GDROM_DMA_ST:		return "SB_GDST";
			default:				return "dma?";
		}
	}

	switch (fisica - GDROM_BASE)
	{
		case GDROM_ALTSTAT:		return "ALTSTAT/DEVCTRL";
		case GDROM_DATA:		return "DATA";
		case GDROM_ERROR:		return "ERROR/FEATURES";
		case GDROM_INTREASON:	return "INTREASON/SECTCNT";
		case GDROM_SECTNUM:		return "SECTNUM";
		case GDROM_BYCTLLO:		return "BYCTLLO";
		case GDROM_BYCTLHI:		return "BYCTLHI";
		case GDROM_DRVSEL:		return "DRVSEL";
		case GDROM_STATUS:		return "STATUS/COMMAND";
		default:				return "?";
	}
}

/* Los registros que se sondean en bucle no se trazan: llenarian la salida. */
static int vale_la_pena_trazar(unsigned long fisica, int escritura)
{
	if (fisica < GDROM_DMA_BASE)
	{
		unsigned long off = fisica - GDROM_BASE;

		if (off == GDROM_DATA)
			return 0;

		if (!escritura && (off == GDROM_ALTSTAT || off == GDROM_STATUS))
			return 0;
	}

	return 1;
}

static DWORD registro_leer(unsigned long fisica, size_t size)
{
	if (fisica >= GDROM_DMA_BASE)
	{
		switch (fisica - GDROM_DMA_BASE)
		{
			case GDROM_DMA_STAR:	return gdrom.dma_star;
			case GDROM_DMA_LEN:		return gdrom.dma_len;
			case GDROM_DMA_DIR:		return gdrom.dma_dir;
			case GDROM_DMA_EN:		return gdrom.dma_en;
			case GDROM_DMA_ST:		return gdrom.dma_st;

			default:
				/* El resto del bloque son los registros de temporizacion del
				   bus G1 y la proteccion de direcciones del DMA. No hacen
				   nada aca, pero el boot ROM los escribe y los vuelve a leer,
				   asi que van contra control_mem como el resto del bloque de
				   control del sistema. */
				logxmsg(LOG_MEM, "gdrom: lectura de registro DMA %08lx\n", fisica);
				return *(DWORD *) &control_mem[fisica & 0xFFFF];
		}
	}

	switch (fisica - GDROM_BASE)
	{
		case GDROM_DATA:
			return leer_datos(size);

		case GDROM_ALTSTAT:
			/* Igual que STATUS pero sin acusar la interrupcion. */
			return gdrom.estado;

		case GDROM_STATUS:
			/* Leer el estado baja la linea de interrupcion de la lectora. */
			intc_remove_ext(ASIC_EVT_EXT_GDROM);
			return gdrom.estado;

		case GDROM_ERROR:
			return gdrom.error;

		case GDROM_INTREASON:
			return gdrom.razon;

		case GDROM_SECTNUM:
			return (DWORD) ((gdrom.formato << 4) | (gdrom.unidad & 0x0F));

		case GDROM_BYCTLLO:
			return gdrom.contador & 0xFF;

		case GDROM_BYCTLHI:
			return (gdrom.contador >> 8) & 0xFF;

		case GDROM_DRVSEL:
			return gdrom.drvsel;

		default:
			logxmsg(LOG_MEM, "gdrom: lectura de registro %08lx\n", fisica);
			return *(DWORD *) &control_mem[fisica & 0xFFFF];
	}
}

static void registro_escribir(unsigned long fisica, DWORD valor, size_t size)
{
	if (fisica >= GDROM_DMA_BASE)
	{
		switch (fisica - GDROM_DMA_BASE)
		{
			case GDROM_DMA_STAR:	gdrom.dma_star = valor;	break;
			case GDROM_DMA_LEN:		gdrom.dma_len  = valor;	break;
			case GDROM_DMA_DIR:		gdrom.dma_dir  = valor;	break;
			case GDROM_DMA_EN:		gdrom.dma_en   = valor;	break;

			case GDROM_DMA_ST:
				gdrom.dma_st = valor & 1;

				if (!(valor & 1))
					break;

				if (gdrom.dma_en)
					disparar_dma();
				else
				{
					/* El disparo sin SB_GDEN no hace nada en el hardware, y
					   deja al host esperando: conviene verlo. */
					logmsg("gdrom: SB_GDST con SB_GDEN apagado\n");

					if (traza_activa)
						fprintf(stderr, "gdrom: SB_GDST con SB_GDEN apagado\n");
				}
				break;

			default:
				logxmsg(LOG_MEM, "gdrom: escritura de registro DMA %08lx = %08x\n",
					fisica, valor);
				*(DWORD *) &control_mem[fisica & 0xFFFF] = valor;
				break;
		}

		return;
	}

	switch (fisica - GDROM_BASE)
	{
		case GDROM_DATA:
			escribir_datos(valor, size);
			break;

		case GDROM_COMMAND:
			ejecutar_ata((BYTE) valor);
			break;

		case GDROM_FEATURES:
			gdrom.features = (BYTE) valor;
			break;

		case GDROM_SECTCNT:
			/* En modo paquete el contador de sectores lleva el modo de DMA;
			   no cambia nada del protocolo. */
			break;

		case GDROM_BYCTLLO:
			gdrom.limite   = (WORD) ((gdrom.limite & 0xFF00) | (valor & 0xFF));
			gdrom.contador = gdrom.limite;
			break;

		case GDROM_BYCTLHI:
			gdrom.limite   = (WORD) ((gdrom.limite & 0x00FF) | ((valor & 0xFF) << 8));
			gdrom.contador = gdrom.limite;
			break;

		case GDROM_DRVSEL:
			gdrom.drvsel = (BYTE) valor;
			break;

		case GDROM_DEVCTRL:
			gdrom.devctrl = (BYTE) valor;

			/* Bit 2: reset por software. */
			if (valor & 0x04)
				gdrom_iniciar(opciones.bandeja);
			break;

		default:
			logxmsg(LOG_MEM, "gdrom: escritura de registro %08lx = %08x\n", fisica, valor);
			*(DWORD *) &control_mem[fisica & 0xFFFF] = valor;
			break;
	}
}

void gdrom_read(unsigned long direccion, void * p, size_t size)
{
	unsigned long	fisica = direccion & 0x00FFFFFF;
	DWORD			valor = registro_leer(fisica, size);

	if (traza_activa && vale_la_pena_trazar(fisica, 0))
		fprintf(stderr, "gdrom: lee %-18s = %08x\n", nombre_registro(fisica), valor);

	switch (size)
	{
		case 1:		*(BYTE *)  p = (BYTE)  valor;	break;
		case 2:		*(WORD *)  p = (WORD)  valor;	break;
		case 4:		*(DWORD *) p = (DWORD) valor;	break;
		default:	memset(p, 0, size);				break;
	}
}

void gdrom_write(unsigned long direccion, void * p, size_t size)
{
	unsigned long	fisica = direccion & 0x00FFFFFF;
	DWORD			valor;

	switch (size)
	{
		case 1:		valor = *(BYTE *)  p;	break;
		case 2:		valor = *(WORD *)  p;	break;
		case 4:		valor = *(DWORD *) p;	break;
		default:	return;
	}

	if (traza_activa && vale_la_pena_trazar(fisica, 1))
		fprintf(stderr, "gdrom: graba %-18s = %08x\n", nombre_registro(fisica), valor);

	registro_escribir(fisica, valor, size);
}
