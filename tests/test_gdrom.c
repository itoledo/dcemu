/****************************************************************************

	Pruebas de gdrom.c -- el protocolo de la lectora

	La maquina de estados es una funcion pura de (estado, registro escrito):
	no hace falta ni SDL ni una imagen de disco, los sectores los da el doble
	de iso.c. Lo que se verifica es la secuencia que el boot ROM ejecuta:

	  comando PACKET -> DRQ con CoD=1 -> 12 bytes del paquete -> DRQ con IO=1
	  -> bloques de datos -> fin de comando con CoD=1, IO=1 y sin DRQ.

	Y las dos situaciones que el boot ROM distingue: con disco y sin disco.

*****************************************************************************/

#include <string.h>

#include "main.h"

#include "arnes.h"
#include "suites.h"

#include "gdrom.h"
#include "intc.h"
#include "iso.h"
#include "opciones.h"

/* Direccion base del bloque de registros en P2, que es como lo ve el boot ROM. */
#define REG(off)	(0xA05F7000UL + (off))
#define DMA(off)	(0xA05F7400UL + (off))

/* ------------------------------------------------------------------------ */
/* Ayudantes                                                                */
/* ------------------------------------------------------------------------ */

static DWORD leer(unsigned long direccion, size_t tam)
{
	DWORD valor = 0;

	gdrom_read(direccion, &valor, tam);

	return valor;
}

static void escribir(unsigned long direccion, DWORD valor, size_t tam)
{
	gdrom_write(direccion, &valor, tam);
}

static void preparar(int hay_disco)
{
	arnes_reset();

	dobles_hay_disco = hay_disco;
	opciones.bandeja = BANDEJA_AUTO;

	gdrom_iniciar(opciones.bandeja);
}

/* Manda un comando del modo paquete completo: parametros, PACKET y los 12
   bytes por el registro de datos, de a palabra como hace el host real. */
static void mandar_paquete(const BYTE * paquete, int byte_count, int por_dma)
{
	int i;

	escribir(REG(GDROM_FEATURES), por_dma ? 1 : 0, 1);
	escribir(REG(GDROM_BYCTLLO), byte_count & 0xFF, 1);
	escribir(REG(GDROM_BYCTLHI), (byte_count >> 8) & 0xFF, 1);
	escribir(REG(GDROM_COMMAND), ATA_PACKET, 1);

	for (i = 0; i < GDROM_PAQUETE_TAM / 2; i++)
		escribir(REG(GDROM_DATA),
			(DWORD) (paquete[i * 2] | (paquete[i * 2 + 1] << 8)), 2);
}

/* Vacia la respuesta por PIO. Devuelve cuantos bytes entrego la lectora. */
static int recibir(BYTE * destino, int maximo)
{
	int n = 0;

	while ((leer(REG(GDROM_ALTSTAT), 1) & GD_ST_DRQ) && n + 2 <= maximo)
	{
		WORD w = (WORD) leer(REG(GDROM_DATA), 2);

		destino[n++] = (BYTE) (w & 0xFF);
		destino[n++] = (BYTE) ((w >> 8) & 0xFF);
	}

	return n;
}

static void paquete_simple(BYTE * paquete, BYTE comando)
{
	memset(paquete, 0, GDROM_PAQUETE_TAM);
	paquete[0] = comando;
}

/* ------------------------------------------------------------------------ */
/* Estado inicial                                                           */
/* ------------------------------------------------------------------------ */

static void arranca_lista_con_disco(void)
{
	preparar(1);

	ESPERAR_U32(gdrom.estado & GD_ST_DRDY, GD_ST_DRDY);
	ESPERAR_U32(gdrom.estado & GD_ST_BSY, 0);
	ESPERAR_U32(gdrom.estado & GD_ST_CHECK, 0);
	ESPERAR_U32(gdrom.unidad, GD_STANDBY);

	/* SECTNUM lleva el tipo de disco arriba y el estado de la unidad abajo. */
	ESPERAR_U32(leer(REG(GDROM_SECTNUM), 1), (GD_DISCO_CDROM << 4) | GD_STANDBY);
}

static void arranca_sin_disco(void)
{
	preparar(0);

	ESPERAR_U32(gdrom.unidad, GD_NODISC);
	ESPERAR_U32(leer(REG(GDROM_SECTNUM), 1) & 0x0F, GD_NODISC);
}

static void la_bandeja_manda_sobre_la_imagen(void)
{
	arnes_reset();

	/* Con --bandeja=vacia la lectora dice que no hay disco aunque haya imagen
	   montada: es lo que hace falta para llegar a la pantalla de "sin disco"
	   sin desmontar nada. */
	dobles_hay_disco = 1;
	gdrom_iniciar(BANDEJA_VACIA);
	ESPERAR_U32(gdrom.unidad, GD_NODISC);

	gdrom_iniciar(BANDEJA_ABIERTA);
	ESPERAR_U32(gdrom.unidad, GD_OPEN);

	gdrom_iniciar(BANDEJA_DISCO);
	ESPERAR_U32(gdrom.unidad, GD_STANDBY);
}

/* ------------------------------------------------------------------------ */
/* Fases del protocolo                                                      */
/* ------------------------------------------------------------------------ */

static void packet_pide_el_paquete(void)
{
	preparar(1);

	escribir(REG(GDROM_COMMAND), ATA_PACKET, 1);

	/* DRQ levantado con CoD=1 e IO=0: "mandame los 12 bytes". Esta fase no
	   interrumpe, el host la sondea. */
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, GD_ST_DRQ);
	ESPERAR_U32(leer(REG(GDROM_INTREASON), 1), GD_IR_COD);
	ESPERAR_I32(dobles_int_ext, 0);
}

static void test_unit_termina_sin_datos(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];

	preparar(1);
	paquete_simple(paquete, SPI_TEST_UNIT);

	mandar_paquete(paquete, 0, 0);

	/* Sin datos que entregar: DRQ abajo, CoD e IO arriba, sin error. */
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, 0);
	ESPERAR_U32(gdrom.estado & GD_ST_CHECK, 0);
	ESPERAR_U32(gdrom.estado & GD_ST_DRDY, GD_ST_DRDY);
	ESPERAR_U32(leer(REG(GDROM_INTREASON), 1), GD_IR_COD | GD_IR_IO);
	ESPERAR_I32(dobles_int_ext, 1);
	ESPERAR_U32(dobles_ultima_int_ext, ASIC_EVT_EXT_GDROM);
}

static void test_unit_falla_sin_disco(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];

	preparar(0);
	paquete_simple(paquete, SPI_TEST_UNIT);

	mandar_paquete(paquete, 0, 0);

	ESPERAR_U32(gdrom.estado & GD_ST_CHECK, GD_ST_CHECK);
	ESPERAR_U32(leer(REG(GDROM_ERROR), 1) >> 4, GD_SENTIDO_NO_LISTA);
	ESPERAR_I32(dobles_int_ext, 1);
}

/* Pedir el estado tiene que andar aunque no haya disco: es asi como el boot
   ROM se entera de que no lo hay. */
static void req_stat_anda_sin_disco(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];
	BYTE respuesta[16];
	int  n;

	preparar(0);

	paquete_simple(paquete, SPI_REQ_STAT);
	paquete[4] = 10;

	mandar_paquete(paquete, 10, 0);

	ESPERAR_U32(gdrom.estado & GD_ST_CHECK, 0);
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, GD_ST_DRQ);
	ESPERAR_U32(leer(REG(GDROM_INTREASON), 1), GD_IR_IO);

	n = recibir(respuesta, (int) sizeof(respuesta));

	ESPERAR_I32(n, 10);
	ESPERAR_U32(respuesta[0] & 0x0F, GD_NODISC);

	/* Y al vaciar el ultimo bloque el comando termina. */
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, 0);
	ESPERAR_U32(leer(REG(GDROM_INTREASON), 1), GD_IR_COD | GD_IR_IO);
}

static void req_mode_devuelve_el_firmware(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];
	BYTE respuesta[40];
	int  n;

	preparar(1);

	paquete_simple(paquete, SPI_REQ_MODE);
	paquete[2] = 0;
	paquete[4] = 32;

	mandar_paquete(paquete, 32, 0);

	n = recibir(respuesta, (int) sizeof(respuesta));

	ESPERAR_I32(n, 32);
	ESPERAR_BYTES(&respuesta[10], "SE      Rev 6.43990316", 22);
}

/* El paquete deja pedir un tramo de la respuesta, no solo la respuesta entera. */
static void req_mode_respeta_el_tramo(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];
	BYTE respuesta[40];
	int  n;

	preparar(1);

	paquete_simple(paquete, SPI_REQ_MODE);
	paquete[2] = 18;					/* desde la version del firmware */
	paquete[4] = 8;

	mandar_paquete(paquete, 8, 0);

	n = recibir(respuesta, (int) sizeof(respuesta));

	ESPERAR_I32(n, 8);
	ESPERAR_BYTES(respuesta, "Rev 6.43", 8);
}

/* ------------------------------------------------------------------------ */
/* TOC                                                                      */
/* ------------------------------------------------------------------------ */

static void toc_describe_una_pista_de_datos(void)
{
	struct TOC toc;
	int i;

	arnes_reset();
	dobles_hay_disco = 1;

	gdrom_construir_toc(&toc);

	/* Pista 1: control 4 (datos), addr 1 (posicion) y el FAD de la imagen. */
	ESPERAR_U32(toc.entry[0] >> 28, 4);
	ESPERAR_U32((toc.entry[0] >> 24) & 0xF, 1);
	ESPERAR_U32(toc.entry[0] & 0xFFFFFF, (DWORD) iso_get_lba());

	/* Primera y ultima pista: el numero va en los bits 23-16. */
	ESPERAR_U32((toc.first >> 16) & 0xFF, 1);
	ESPERAR_U32((toc.last  >> 16) & 0xFF, 1);

	/* El lead-out va despues de la ultima pista. */
	ESPERAR((toc.dunno & 0xFFFFFF) > (toc.entry[0] & 0xFFFFFF));

	/* Las 98 pistas que no existen tienen que quedar marcadas como vacias, no
	   con lo que hubiera en la pila. */
	for (i = 1; i < 99; i++)
		ESPERAR_U32(toc.entry[i], 0xFFFFFFFF);
}

static void get_toc_entrega_408_bytes(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];
	BYTE respuesta[512];
	int  n;

	preparar(1);

	paquete_simple(paquete, SPI_GET_TOC);
	paquete[1] = 0;
	paquete[3] = (BYTE) (sizeof(struct TOC) >> 8);
	paquete[4] = (BYTE) (sizeof(struct TOC) & 0xFF);

	/* Byte count chico a proposito: la TOC no entra en un solo bloque DRQ y
	   la lectora tiene que encadenarlos. */
	mandar_paquete(paquete, 64, 0);

	n = recibir(respuesta, (int) sizeof(respuesta));

	ESPERAR_I32(n, (int) sizeof(struct TOC));
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, 0);

	/* Un bloque por cada 64 bytes, mas el fin de comando, y cada uno
	   interrumpe. 408 = 6 bloques de 64 y uno de 24. */
	ESPERAR_I32(dobles_int_ext, 8);
}

/* ------------------------------------------------------------------------ */
/* Lectura de sectores                                                      */
/* ------------------------------------------------------------------------ */

static void cd_read_entrega_los_sectores(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];
	BYTE respuesta[2048 * 2];
	int  n, i;
	int  fad = 150;

	preparar(1);

	memset(paquete, 0, sizeof(paquete));
	paquete[0]  = SPI_CD_READ;
	paquete[1]  = 0x20;
	paquete[2]  = (BYTE) ((fad >> 16) & 0xFF);
	paquete[3]  = (BYTE) ((fad >> 8) & 0xFF);
	paquete[4]  = (BYTE) (fad & 0xFF);
	paquete[10] = 2;					/* dos sectores */

	mandar_paquete(paquete, 2048, 0);

	n = recibir(respuesta, (int) sizeof(respuesta));

	ESPERAR_I32(n, 2048 * 2);

	/* El doble de iso.c llena el byte i del sector s con (s * 7 + i) & 0xFF:
	   asi se ve que llegaron los sectores pedidos y no otros. */
	for (i = 0; i < 8; i++)
	{
		ESPERAR_U32(respuesta[i], (BYTE) ((fad * 7 + i) & 0xFF));
		ESPERAR_U32(respuesta[2048 + i], (BYTE) (((fad + 1) * 7 + i) & 0xFF));
	}

	ESPERAR_U32(gdrom.unidad, GD_PAUSE);
}

static void cd_read_falla_sin_disco(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];

	preparar(0);

	memset(paquete, 0, sizeof(paquete));
	paquete[0]  = SPI_CD_READ;
	paquete[10] = 1;

	mandar_paquete(paquete, 2048, 0);

	ESPERAR_U32(gdrom.estado & GD_ST_CHECK, GD_ST_CHECK);
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, 0);
	ESPERAR_U32(leer(REG(GDROM_ERROR), 1) >> 4, GD_SENTIDO_NO_LISTA);
}

/* ------------------------------------------------------------------------ */
/* DMA del G2                                                               */
/* ------------------------------------------------------------------------ */

static void cd_read_por_dma_deja_los_datos_en_ram(void)
{
	BYTE	paquete[GDROM_PAQUETE_TAM];
	DWORD	destino = PRUEBA_DATOS;
	int		fad = 150;
	int		i;

	preparar(1);

	memset(paquete, 0, sizeof(paquete));
	paquete[0]  = SPI_CD_READ;
	paquete[1]  = 0x20;
	paquete[2]  = (BYTE) ((fad >> 16) & 0xFF);
	paquete[3]  = (BYTE) ((fad >> 8) & 0xFF);
	paquete[4]  = (BYTE) (fad & 0xFF);
	paquete[10] = 1;

	escribir(DMA(GDROM_DMA_STAR), destino & 0x1FFFFFE0, 4);
	escribir(DMA(GDROM_DMA_LEN), 2048, 4);
	escribir(DMA(GDROM_DMA_DIR), 0, 4);
	escribir(DMA(GDROM_DMA_EN), 1, 4);

	mandar_paquete(paquete, 2048, 1);

	/* Con DMA los datos quedan esperando: nada se movio todavia. */
	ESPERAR_I32(gdrom.esperando_dma, 1);
	ESPERAR_U32(leer_l(destino), 0);

	escribir(DMA(GDROM_DMA_ST), 1, 4);

	/* Ahora si: el sector esta en RAM, SB_GDST volvio a cero y aviso por las
	   dos vias, el fin de DMA y el fin de comando. */
	ESPERAR_U32(leer(DMA(GDROM_DMA_ST), 4), 0);
	ESPERAR_I32(gdrom.esperando_dma, 0);
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, 0);

	for (i = 0; i < 8; i++)
		ESPERAR_U32(leer_b(destino + i), (BYTE) ((fad * 7 + i) & 0xFF));

	ESPERAR_U32(dobles_ultima_int_normal, ASIC_EVT_GDROM_DMA);
	ESPERAR_U32(dobles_ultima_int_ext, ASIC_EVT_EXT_GDROM);
}

/* ------------------------------------------------------------------------ */
/* Registros sueltos                                                        */
/* ------------------------------------------------------------------------ */

static void altstat_no_acusa_la_interrupcion(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];

	preparar(1);
	paquete_simple(paquete, SPI_TEST_UNIT);

	mandar_paquete(paquete, 0, 0);

	ESPERAR_U32(intc_queuemask_ext & ASIC_EVT_EXT_GDROM, ASIC_EVT_EXT_GDROM);

	/* El estado alternativo deja la interrupcion en pie... */
	leer(REG(GDROM_ALTSTAT), 1);
	ESPERAR_U32(intc_queuemask_ext & ASIC_EVT_EXT_GDROM, ASIC_EVT_EXT_GDROM);

	/* ...y el registro de estado la baja, como manda ATA. */
	leer(REG(GDROM_STATUS), 1);
	ESPERAR_U32(intc_queuemask_ext & ASIC_EVT_EXT_GDROM, 0);
}

static void comando_desconocido_aborta(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];

	preparar(1);
	paquete_simple(paquete, 0x5A);		/* no existe */

	mandar_paquete(paquete, 0, 0);

	ESPERAR_U32(gdrom.estado & GD_ST_CHECK, GD_ST_CHECK);
	ESPERAR_U32(leer(REG(GDROM_ERROR), 1) >> 4, GD_SENTIDO_ILEGAL);
}

static void req_error_cuenta_la_ultima_falla(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];
	BYTE respuesta[16];
	int  n;

	preparar(0);

	/* Primero una falla... */
	paquete_simple(paquete, SPI_TEST_UNIT);
	mandar_paquete(paquete, 0, 0);

	/* ...y despues se pregunta por ella. */
	paquete_simple(paquete, SPI_REQ_ERROR);
	paquete[4] = 10;
	mandar_paquete(paquete, 10, 0);

	n = recibir(respuesta, (int) sizeof(respuesta));

	ESPERAR_I32(n, 10);
	ESPERAR_U32(respuesta[0], 0xF0);
	ESPERAR_U32(respuesta[2], GD_SENTIDO_NO_LISTA);
	ESPERAR_U32(respuesta[8], 0x3A);	/* medio ausente */
}

static void soft_reset_deja_la_lectora_como_al_principio(void)
{
	BYTE paquete[GDROM_PAQUETE_TAM];

	preparar(1);

	/* Se deja la lectora a media transferencia... */
	paquete_simple(paquete, SPI_REQ_MODE);
	paquete[4] = 32;
	mandar_paquete(paquete, 32, 0);
	ESPERAR_U32(gdrom.estado & GD_ST_DRQ, GD_ST_DRQ);

	/* ...y el reset la limpia. */
	escribir(REG(GDROM_COMMAND), ATA_SOFT_RESET, 1);

	ESPERAR_U32(gdrom.estado, GD_ST_DRDY | GD_ST_DSC);
	ESPERAR_I32(gdrom.paquete_pos, -1);
	ESPERAR_I32(gdrom.datos_tam, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(arranca_lista_con_disco),
	CASO(arranca_sin_disco),
	CASO(la_bandeja_manda_sobre_la_imagen),
	CASO(packet_pide_el_paquete),
	CASO(test_unit_termina_sin_datos),
	CASO(test_unit_falla_sin_disco),
	CASO(req_stat_anda_sin_disco),
	CASO(req_mode_devuelve_el_firmware),
	CASO(req_mode_respeta_el_tramo),
	CASO(toc_describe_una_pista_de_datos),
	CASO(get_toc_entrega_408_bytes),
	CASO(cd_read_entrega_los_sectores),
	CASO(cd_read_falla_sin_disco),
	CASO(cd_read_por_dma_deja_los_datos_en_ram),
	CASO(altstat_no_acusa_la_interrupcion),
	CASO(comando_desconocido_aborta),
	CASO(req_error_cuenta_la_ultima_falla),
	CASO(soft_reset_deja_la_lectora_como_al_principio),
};

const dc_suite suite_gdrom = DEFINIR_SUITE("gdrom", casos);
