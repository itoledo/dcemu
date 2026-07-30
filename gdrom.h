/****************************************************************************

	GDROM - la lectora, vista como la ve el hardware

	El boot ROM no usa las syscalls de la BIOS: le habla directo a la interfaz
	ATA de la lectora, en el bloque de control del sistema. Aca esta esa
	interfaz -- registros, maquina de estados del protocolo y los comandos del
	modo paquete (SPI) que hacen falta para arrancar.

	Los sectores los sigue leyendo iso.c; este modulo solo pone el protocolo.

	El mapa de registros y los codigos de comando estan contrastados contra dos
	fuentes independientes: el controlador de GD-ROM del kernel de Linux
	(drivers/cdrom/gdrom.c) y el nucleo de reicast (core/hw/gdrom/gdromv3.h).

	Ver docs/bios-boot-plan.md, fase 3.

*****************************************************************************/

#ifndef _GDROM_H_
#define _GDROM_H_

#include <stddef.h>

/* WORD/DWORD/BYTE vienen de <windows.h> en Windows y de lnxdefs.h fuera. Igual
   que main.h, para que esta cabecera se pueda incluir sola. */
#ifdef WIN32
#include <windows.h>
#endif

#include "lnxdefs.h"

/* ------------------------------------------------------------------------ */
/* Mapa de registros                                                        */
/* ------------------------------------------------------------------------ */

/* Direcciones fisicas, sin el prefijo de area: mem.c enmascara antes. */
#define GDROM_BASE			0x005F7000
#define GDROM_FIN			0x005F70FF
#define GDROM_DMA_BASE		0x005F7400
#define GDROM_DMA_FIN		0x005F74FF

#define GDROM_ES_REGISTRO(fisica) \
	(((fisica) >= GDROM_BASE     && (fisica) <= GDROM_FIN) || \
	 ((fisica) >= GDROM_DMA_BASE && (fisica) <= GDROM_DMA_FIN))

/* Offsets dentro del bloque ATA. Cada registro ocupa una direccion propia
   alineada a 4, no son puertos consecutivos. */
#define GDROM_ALTSTAT		0x18	/* lectura:   estado, sin acusar la int */
#define GDROM_DEVCTRL		0x18	/* escritura: control del dispositivo */
#define GDROM_DATA			0x80
#define GDROM_ERROR			0x84	/* lectura */
#define GDROM_FEATURES		0x84	/* escritura */
#define GDROM_INTREASON		0x88	/* lectura */
#define GDROM_SECTCNT		0x88	/* escritura */
#define GDROM_SECTNUM		0x8C
#define GDROM_BYCTLLO		0x90
#define GDROM_BYCTLHI		0x94
#define GDROM_DRVSEL		0x98
#define GDROM_STATUS		0x9C	/* lectura */
#define GDROM_COMMAND		0x9C	/* escritura */

/* Bloque de DMA del G2 para la lectora. */
#define GDROM_DMA_STAR		0x04	/* SB_GDSTAR, direccion de destino */
#define GDROM_DMA_LEN		0x08	/* SB_GDLEN */
#define GDROM_DMA_DIR		0x0C	/* SB_GDDIR, 0: lectora -> RAM */
#define GDROM_DMA_EN		0x14	/* SB_GDEN */
#define GDROM_DMA_ST		0x18	/* SB_GDST, disparo y estado */

/* ------------------------------------------------------------------------ */
/* Bits del registro de estado (ATA)                                        */
/* ------------------------------------------------------------------------ */

#define GD_ST_CHECK			0x01	/* hay un error; mirar el registro ERROR */
#define GD_ST_CORR			0x04
#define GD_ST_DRQ			0x08	/* hay datos que transferir */
#define GD_ST_DSC			0x10
#define GD_ST_DF			0x20
#define GD_ST_DRDY			0x40	/* acepta comandos */
#define GD_ST_BSY			0x80

/* Razon de interrupcion. */
#define GD_IR_COD			0x01	/* 1: comando/estado, 0: datos */
#define GD_IR_IO			0x02	/* 1: lectora -> host */

/* Estado de la unidad: nibble bajo de SECTNUM. */
#define GD_BUSY				0x00
#define GD_PAUSE			0x01
#define GD_STANDBY			0x02
#define GD_PLAY				0x03
#define GD_SEEK				0x04
#define GD_SCAN				0x05
#define GD_OPEN				0x06	/* bandeja abierta */
#define GD_NODISC			0x07
#define GD_RETRY			0x08
#define GD_ERROR			0x09

/* Tipo de disco: nibble alto de SECTNUM. */
#define GD_DISCO_CDDA		0x0
#define GD_DISCO_CDROM		0x1
#define GD_DISCO_CDROM_XA	0x2
#define GD_DISCO_CDI		0x4
#define GD_DISCO_GDROM		0x8

/* ------------------------------------------------------------------------ */
/* Comandos                                                                 */
/* ------------------------------------------------------------------------ */

#define ATA_NOP				0x00
#define ATA_SOFT_RESET		0x08
#define ATA_EXEC_DIAG		0x90
#define ATA_PACKET			0xA0
#define ATA_IDENTIFY		0xA1
#define ATA_SET_FEATURES	0xEF

#define SPI_TEST_UNIT		0x00
#define SPI_REQ_STAT		0x10
#define SPI_REQ_MODE		0x11
#define SPI_SET_MODE		0x12
#define SPI_REQ_ERROR		0x13
#define SPI_GET_TOC			0x14
#define SPI_REQ_SES			0x15
#define SPI_CD_OPEN			0x16
#define SPI_CD_PLAY			0x20
#define SPI_CD_SEEK			0x21
#define SPI_CD_SCAN			0x22
#define SPI_CD_READ			0x30
#define SPI_CD_READ2		0x31
#define SPI_GET_SCD			0x40

/* Claves de sentido (nibble alto del registro ERROR). */
#define GD_SENTIDO_OK		0x0
#define GD_SENTIDO_NO_LISTA	0x2		/* NOT READY: sin disco o bandeja abierta */
#define GD_SENTIDO_ILEGAL	0x5		/* ILLEGAL REQUEST */
#define GD_SENTIDO_ATENCION	0x6		/* UNIT ATTENTION: cambio de medio */
#define GD_SENTIDO_ABORTADO	0xB		/* ABORTED COMMAND */

/* ------------------------------------------------------------------------ */
/* Estado                                                                   */
/* ------------------------------------------------------------------------ */

#define GDROM_PAQUETE_TAM	12

struct gdrom_t
{
	BYTE	estado;			/* registro de estado ATA */
	BYTE	error;
	BYTE	razon;			/* CoD / IO */
	BYTE	unidad;			/* GD_BUSY .. GD_ERROR */
	BYTE	formato;		/* GD_DISCO_* */
	BYTE	features;
	BYTE	drvsel;
	BYTE	devctrl;

	/* El byte count tiene dos papeles: el host escribe cuanto esta dispuesto a
	   recibir de una vez, y la lectora contesta cuanto va a entregar en el
	   bloque en curso. Se leen por la misma direccion, asi que hacen falta los
	   dos valores. */
	WORD	limite;			/* lo que escribio el host */
	WORD	contador;		/* lo que lee el host */

	BYTE	paquete[GDROM_PAQUETE_TAM];
	int		paquete_pos;	/* bytes recibidos; -1 si no espera paquete */

	BYTE *	datos;			/* respuesta pendiente de entregar */
	int		datos_cap;
	int		datos_tam;
	int		datos_pos;
	int		bloque;			/* bytes que quedan del bloque DRQ en curso */

	int		por_dma;		/* la transferencia en curso va por DMA */
	int		esperando_dma;	/* hay datos listos esperando SB_GDST */

	DWORD	dma_star;
	DWORD	dma_len;
	DWORD	dma_dir;
	DWORD	dma_en;
	DWORD	dma_st;
};

extern struct gdrom_t gdrom;

/* ------------------------------------------------------------------------ */
/* Interfaz                                                                 */
/* ------------------------------------------------------------------------ */

/* bandeja es una constante BANDEJA_* de opciones.h. */
void gdrom_iniciar(int bandeja);

void gdrom_read(unsigned long direccion, void * p, size_t size);
void gdrom_write(unsigned long direccion, void * p, size_t size);

/* La TOC del disco montado, compartida con el hook de syscall de la BIOS.
   struct TOC vive en main.h. */
struct TOC;
void gdrom_construir_toc(struct TOC * toc);

#endif /* _GDROM_H_ */
