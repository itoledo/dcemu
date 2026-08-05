/****************************************************************************

	Pruebas de vmu.c: la Visual Memory de la ranura 1.

	El guion no se invento: es el que usa el driver de KOS
	(kernel/arch/dreamcast/hardware/maple/vmu.c) -- el blkid partido en bytes
	con la fase en el byte 1, la lectura en una fase de 512, la escritura en
	cuatro de 128 mas el BSYNC, y las comparaciones que su codigo hace sobre
	la respuesta (el eco del blkid, el 0x403F7E7E del descriptor).

	Lo que estas pruebas cuidan sobre todo es que **la tarjeta recien
	formateada sea una tarjeta valida**: el bloque raiz con sus 0x55, la FAT
	con la cadena del directorio y GETMINFO diciendo lo mismo que el bloque
	raiz. Un guest puede leer cualquiera de los dos, y si difieren el fallo
	es de los que no avisan.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "vmu.h"

/* Encabezado de un marco: comando, destinatario (la VMU del puerto A es
   0x01), remitente (el anfitrion del puerto A es 0x00) y palabras de datos. */
#define ENCABEZADO(cmd, largo) \
	((unsigned) (cmd) | (0x01u << 8) | (0x00u << 16) | ((unsigned) (largo) << 24))

#define FUNC_MEMORIA	0x02000000u
#define FUNC_LCD		0x04000000u
#define FUNC_RELOJ		0x08000000u

#define BLKID(bloque, fase) \
	((((unsigned) (bloque) & 0xFF) << 24) | (((unsigned) (bloque) >> 8) << 16) \
	| ((unsigned) (fase) << 8))

static unsigned pedido[64];
static unsigned respuesta[160];

static int mandar(int palabras)
{
	memset(respuesta, 0xAA, sizeof(respuesta));

	return vmu_maple(pedido, palabras, respuesta, 160);
}

static unsigned leer_u16(const unsigned char * p)
{
	return (unsigned) p[0] | ((unsigned) p[1] << 8);
}

/* Un bloque completo, como vmu_block_read() de KOS. Devuelve el largo. */
static int leer_bloque(unsigned bloque, unsigned char * destino)
{
	int n;

	pedido[0] = ENCABEZADO(11, 2);
	pedido[1] = FUNC_MEMORIA;
	pedido[2] = BLKID(bloque, 0);

	n = mandar(3);

	if (n == 3 + 128)
		memcpy(destino, &respuesta[3], 512);

	return n;
}

/* Las cuatro fases y el BSYNC, como vmu_block_write_internal() de KOS.
   Devuelve cuantas de las cinco respuestas fueron OK. */
static int escribir_bloque(unsigned bloque, const unsigned char * fuente)
{
	int ok = 0;
	int fase;

	for (fase = 0; fase < 4; fase++)
	{
		pedido[0] = ENCABEZADO(12, 2 + 32);
		pedido[1] = FUNC_MEMORIA;
		pedido[2] = BLKID(bloque, fase);
		memcpy(&pedido[3], fuente + fase * 128, 128);

		if (mandar(3 + 32) == 1 && (respuesta[0] & 0xFF) == 0x07)
			ok++;
	}

	pedido[0] = ENCABEZADO(13, 2);
	pedido[1] = FUNC_MEMORIA;
	pedido[2] = BLKID(bloque, 4);

	if (mandar(3) == 1 && (respuesta[0] & 0xFF) == 0x07)
		ok++;

	return ok;
}

/* ------------------------------------------------------------------------ */

static void tarjeta_nueva(void)
{
	ESPERAR_I32(vmu_iniciar(NULL), 0);
	ESPERAR_I32(vmu_presente(), 1);
}

static void devinfo_de_vmu_oficial(void)
{
	int n;

	tarjeta_nueva();

	pedido[0] = ENCABEZADO(1, 0);
	n = mandar(1);

	/* 28 palabras de descriptor tras el encabezado. */
	ESPERAR_I32(n, 29);
	ESPERAR_U32(respuesta[0] & 0xFF, 0x05);
	ESPERAR_U32((respuesta[0] >> 8) & 0xFF, 0x00);		/* al anfitrion */
	ESPERAR_U32((respuesta[0] >> 16) & 0xFF, 0x01);		/* de la ranura 1 */
	ESPERAR_U32((respuesta[0] >> 24) & 0xFF, 28);

	/* Las tres funciones y la palabra del reloj, que es donde KOS mira si es
	   una VMU oficial (vmu_is_vmu). */
	ESPERAR_U32(respuesta[1], FUNC_MEMORIA | FUNC_LCD | FUNC_RELOJ);
	ESPERAR_U32(respuesta[2], 0x403F7E7E);

	/* El nombre va relleno con espacios, sin NUL, como en el bus. Arranca en
	   el byte 18 del descriptor, que no es borde de palabra. */
	ESPERAR_I32(memcmp((const unsigned char *) respuesta + 4 + 18,
		"Visual Memory ", 14), 0);
}

static void bloque_raiz_formateado(void)
{
	unsigned char bloque[512];
	int i, unos = 0;

	tarjeta_nueva();

	ESPERAR_I32(leer_bloque(255, bloque), 131);

	for (i = 0; i < 16; i++)
		if (bloque[i] == 0x55)
			unos++;

	ESPERAR_I32(unos, 16);
	ESPERAR_U32(leer_u16(bloque + 0x46), 254);	/* FAT */
	ESPERAR_U32(leer_u16(bloque + 0x48), 1);
	ESPERAR_U32(leer_u16(bloque + 0x4A), 253);	/* directorio */
	ESPERAR_U32(leer_u16(bloque + 0x4C), 13);
	ESPERAR_U32(leer_u16(bloque + 0x50), 200);	/* bloques de usuario */
}

static void fat_recien_formateada(void)
{
	unsigned char fat[512];
	int i, libres = 0;

	tarjeta_nueva();

	ESPERAR_I32(leer_bloque(254, fat), 131);

	/* Raiz y FAT terminan en si mismos; el directorio baja encadenado del
	   253 al 241. */
	ESPERAR_U32(leer_u16(fat + 255 * 2), 0xFFFA);
	ESPERAR_U32(leer_u16(fat + 254 * 2), 0xFFFA);

	for (i = 253; i > 241; i--)
		ESPERAR_U32(leer_u16(fat + i * 2), (unsigned) i - 1);

	ESPERAR_U32(leer_u16(fat + 241 * 2), 0xFFFA);

	for (i = 0; i <= 240; i++)
		if (leer_u16(fat + i * 2) == 0xFFFC)
			libres++;

	ESPERAR_I32(libres, 241);
}

static void getminfo_dice_lo_mismo_que_el_bloque_raiz(void)
{
	unsigned char raiz[512];
	unsigned char mi[24];
	int n;

	tarjeta_nueva();

	pedido[0] = ENCABEZADO(10, 1);
	pedido[1] = FUNC_MEMORIA;
	n = mandar(2);

	ESPERAR_I32(n, 8);							/* func + 24 bytes */
	ESPERAR_U32(respuesta[0] & 0xFF, 0x08);
	ESPERAR_U32(respuesta[1], FUNC_MEMORIA);

	/* A un buffer propio: leer_bloque() reusa `respuesta`. */
	memcpy(mi, &respuesta[2], 24);

	ESPERAR_I32(leer_bloque(255, raiz), 131);

	ESPERAR_U32(leer_u16(mi + 0), 255);					/* ultimo bloque */
	ESPERAR_U32(leer_u16(mi + 6),  leer_u16(raiz + 0x46));
	ESPERAR_U32(leer_u16(mi + 8),  leer_u16(raiz + 0x48));
	ESPERAR_U32(leer_u16(mi + 10), leer_u16(raiz + 0x4A));
	ESPERAR_U32(leer_u16(mi + 12), leer_u16(raiz + 0x4C));
	ESPERAR_U32(leer_u16(mi + 16), leer_u16(raiz + 0x50));
}

static void escribir_y_releer_un_bloque(void)
{
	unsigned char patron[512];
	unsigned char vuelta[512];
	int i;

	tarjeta_nueva();

	for (i = 0; i < 512; i++)
		patron[i] = (unsigned char) (i * 7 + 3);

	ESPERAR_I32(escribir_bloque(17, patron), 5);
	ESPERAR_I32(leer_bloque(17, vuelta), 131);
	ESPERAR_BYTES(vuelta, patron, 512);

	/* El eco del blkid: el driver lo compara contra el que pidio. */
	ESPERAR_U32(respuesta[2], BLKID(17, 0));
}

static void cada_fase_escribe_su_cuarto(void)
{
	unsigned char patron[512];
	unsigned char vuelta[512];
	int i;

	tarjeta_nueva();

	for (i = 0; i < 512; i++)
		patron[i] = (unsigned char) (255 - (i & 0xFF));

	/* Solo las fases 1 y 3: los otros dos cuartos tienen que quedar como
	   estaban (cero, tarjeta recien formateada). */
	for (i = 1; i < 4; i += 2)
	{
		pedido[0] = ENCABEZADO(12, 2 + 32);
		pedido[1] = FUNC_MEMORIA;
		pedido[2] = BLKID(30, i);
		memcpy(&pedido[3], patron + i * 128, 128);
		ESPERAR_U32(mandar(3 + 32), 1);
	}

	ESPERAR_I32(leer_bloque(30, vuelta), 131);

	for (i = 0; i < 128; i++)
	{
		ESPERAR_I32(vuelta[i], 0);
		ESPERAR_I32(vuelta[256 + i], 0);
	}

	ESPERAR_I32(memcmp(vuelta + 128, patron + 128, 128), 0);
	ESPERAR_I32(memcmp(vuelta + 384, patron + 384, 128), 0);
}

static void bloque_fuera_de_rango(void)
{
	tarjeta_nueva();

	pedido[0] = ENCABEZADO(11, 2);
	pedido[1] = FUNC_MEMORIA;
	pedido[2] = BLKID(256, 0);

	ESPERAR_I32(mandar(3), 1);
	ESPERAR_U32(respuesta[0] & 0xFF, 0xFB);		/* error de archivo */
}

static void funcion_desconocida_y_comando_desconocido(void)
{
	tarjeta_nueva();

	/* GETMINFO de una funcion que la tarjeta no tiene. */
	pedido[0] = ENCABEZADO(10, 1);
	pedido[1] = 0x01000000;						/* mando */
	ESPERAR_I32(mandar(2), 1);
	ESPERAR_U32(respuesta[0] & 0xFF, 0xFE);

	/* Un comando que no existe. */
	pedido[0] = ENCABEZADO(0x22, 0);
	ESPERAR_I32(mandar(1), 1);
	ESPERAR_U32(respuesta[0] & 0xFF, 0xFD);
}

static void lcd_y_zumbador_contestan_ok(void)
{
	tarjeta_nueva();

	/* El dibujo del LCD (vmu_draw_lcd de KOS): 48 palabras de mapa de bits.
	   Se descarta, pero un error haria reintentar al driver. */
	pedido[0] = ENCABEZADO(12, 2 + 48);
	pedido[1] = FUNC_LCD;
	pedido[2] = 0;
	memset(&pedido[3], 0xFF, 48 * 4);
	ESPERAR_I32(mandar(3 + 48), 1);
	ESPERAR_U32(respuesta[0] & 0xFF, 0x07);

	/* El zumbador (vmu_beep_raw): SETCOND sobre el reloj. */
	pedido[0] = ENCABEZADO(14, 2);
	pedido[1] = FUNC_RELOJ;
	pedido[2] = 0;
	ESPERAR_I32(mandar(3), 1);
	ESPERAR_U32(respuesta[0] & 0xFF, 0x07);
}

static void botones_sueltos(void)
{
	tarjeta_nueva();

	/* vmu_poll() de KOS: GETCOND sobre el reloj, y 1 es suelto. */
	pedido[0] = ENCABEZADO(9, 1);
	pedido[1] = FUNC_RELOJ;

	ESPERAR_I32(mandar(2), 3);
	ESPERAR_U32(respuesta[0] & 0xFF, 0x08);
	ESPERAR_U32(respuesta[1], FUNC_RELOJ);
	ESPERAR_U32(respuesta[2] & 0xFF, 0xFF);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(devinfo_de_vmu_oficial),
	CASO(bloque_raiz_formateado),
	CASO(fat_recien_formateada),
	CASO(getminfo_dice_lo_mismo_que_el_bloque_raiz),
	CASO(escribir_y_releer_un_bloque),
	CASO(cada_fase_escribe_su_cuarto),
	CASO(bloque_fuera_de_rango),
	CASO(funcion_desconocida_y_comando_desconocido),
	CASO(lcd_y_zumbador_contestan_ok),
	CASO(botones_sueltos),
};

const dc_suite suite_vmu = DEFINIR_SUITE("vmu", casos);
