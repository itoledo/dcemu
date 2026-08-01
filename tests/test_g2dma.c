/****************************************************************************

	Pruebas de g2dma.c: los cuatro canales de DMA del bus G2.

	El guion no se invento: es el que usa g2_dma_transfer() de KOS
	(kernel/arch/dreamcast/hardware/g2dma.c) -- enmascarar las dos direcciones
	con 0x1FFFFFE0, redondear el largo a 32 bytes con el bit 31 puesto, elegir
	el disparo y escribir enable y start, en ese orden.

	Lo que estas pruebas cuidan sobre todo es que **SB_ADST se lea 0 despues de
	la transferencia**. Cuando los ocho registros caian en el respaldo del
	bloque de control se leia 1 -- "DMA en curso" -- para siempre, y ahi se
	quedaba el guest.

	Ver docs/aica-plan.md, fase 2.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "g2dma.h"
#include "intc.h"

extern unsigned char * memoria;		/* RAM del sistema, zona 0x0C */
extern unsigned char * g2_mem;		/* la ventana fisica baja, zona 0x00 */

extern int   dobles_int_normal;
extern DWORD dobles_ultima_int_normal;

/* Direcciones de trabajo: RAM del sistema y RAM de sonido, las dos validas
   para el canal 0 segun la seccion 8.4.1.4. */
#define RAIZ		0x0C010000
#define ONDA		0x00810000

#define RAIZ_OFF	(RAIZ & 0x00FFFFFF)
#define ONDA_OFF	(ONDA & 0x00FFFFFF)

#define REG(canal, reg)		(G2DMA_BASE + (canal) * G2DMA_PASO + (reg) * 4)

/* ------------------------------------------------------------------------ */

static void escribir(int canal, int reg, DWORD valor)
{
	g2dma_escribir(REG(canal, reg), &valor, sizeof(valor));
}

static DWORD leer(int canal, int reg)
{
	DWORD v = 0xDEADBEEF;

	g2dma_leer(REG(canal, reg), &v, sizeof(v));

	return v;
}

/* El patron que se transfiere, y donde queda. */
static void preparar(unsigned char * destino, unsigned char * fuente, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		fuente[i] = (unsigned char) (i * 5 + 1);

	memset(destino, 0, n);
}

static void armar(int canal, DWORD g2, DWORD raiz, DWORD largo, DWORD dir)
{
	g2dma_reset();

	escribir(canal, G2DMA_STAG, g2   & 0x1FFFFFE0u);
	escribir(canal, G2DMA_STAR, raiz & 0x1FFFFFE0u);
	escribir(canal, G2DMA_LEN,  largo | 0x80000000u);
	escribir(canal, G2DMA_DIR,  dir);
	escribir(canal, G2DMA_TSEL, 5);			/* lo que KOS pone en el canal del AICA */
}

/* ------------------------------------------------------------------------ */

static void la_transferencia_hacia_el_g2_copia(void)
{
	const size_t n = 256;

	preparar(&g2_mem[ONDA_OFF], &memoria[RAIZ_OFF], n);

	armar(0, ONDA, RAIZ, (DWORD) n, 0);		/* raiz -> G2 */
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_BYTES(&g2_mem[ONDA_OFF], &memoria[RAIZ_OFF], n);
}

static void la_transferencia_desde_el_g2_copia(void)
{
	const size_t n = 256;

	preparar(&memoria[RAIZ_OFF], &g2_mem[ONDA_OFF], n);

	armar(0, ONDA, RAIZ, (DWORD) n, 1);		/* G2 -> raiz */
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_BYTES(&memoria[RAIZ_OFF], &g2_mem[ONDA_OFF], n);
}

static void st_se_lee_cero_al_terminar(void)
{
	/* Este es el caso que dejaba colgado al guest: escribir 1 y volver a leer
	   1 para siempre, porque el registro era puro respaldo. */
	armar(0, ONDA, RAIZ, 64, 0);
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_U32(leer(0, G2DMA_ST), 0);
}

static void el_bit_31_del_largo_apaga_la_habilitacion(void)
{
	/* SB_**LEN bit 31 = 1: "When a transfer ends, the DMA enable register is
	   set to 0". Es lo que KOS pide siempre (RESET_ENABLED). */
	armar(0, ONDA, RAIZ, 64, 0);
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_U32(leer(0, G2DMA_EN), 0);

	/* Y con el bit en 0 la habilitacion sobrevive. */
	g2dma_reset();
	escribir(0, G2DMA_STAG, ONDA);
	escribir(0, G2DMA_STAR, RAIZ);
	escribir(0, G2DMA_LEN,  64);
	escribir(0, G2DMA_EN,   1);
	escribir(0, G2DMA_ST,   1);

	ESPERAR_U32(leer(0, G2DMA_EN), 1);
}

static void sin_habilitacion_no_arranca(void)
{
	/* "When G2-DMA is disabled through the SB_ADEN register, writing a 1 to
	   this register is not allowed." */
	const size_t n = 64;

	preparar(&g2_mem[ONDA_OFF], &memoria[RAIZ_OFF], n);

	armar(0, ONDA, RAIZ, (DWORD) n, 0);
	escribir(0, G2DMA_ST, 1);				/* sin escribir EN */

	ESPERAR_U32(leer(0, G2DMA_ST), 0);
	ESPERAR_U32(g2_mem[ONDA_OFF], 0);
}

static void el_fin_de_dma_interrumpe_por_el_bit_que_toca(void)
{
	/* ASIC_EVT_G2_DMA0..3 son 0x000F a 0x0012 en asic.h de KOS: registro 0
	   --SB_ISTNRM-- y bits 15 a 18. El del canal 0 es el que espera el
	   semaforo de g2_dma_transfer(); sin el, KOS se queda en sem_wait(). */
	static const DWORD esperado[G2DMA_CANALES] =
	{
		1u << 15, 1u << 16, 1u << 17, 1u << 18
	};
	int canal;

	for (canal = 0; canal < G2DMA_CANALES; canal++)
	{
		/* Los canales 1 a 3 hablan con dispositivos externos, que caen en
		   otro rango; se usa el que cada uno tiene permitido. */
		DWORD g2 = (canal == 0) ? ONDA : 0x01010000;
		int   antes = dobles_int_normal;

		armar(canal, g2, RAIZ, 64, 1);
		escribir(canal, G2DMA_EN, 1);
		escribir(canal, G2DMA_ST, 1);

		ESPERAR_I32(dobles_int_normal, antes + 1);
		ESPERAR_U32(dobles_ultima_int_normal, esperado[canal]);
	}
}

static void un_rango_invalido_no_transfiere(void)
{
	/* La seccion 8.4.1.4 enumera los rangos y dice que un valor fuera de
	   ellos genera una interrupcion de configuracion invalida y **no**
	   arranca la transferencia. Mover a la direccion equivocada seria peor
	   que no mover nada: no dejaria rastro. */
	const size_t n = 64;
	int antes;

	preparar(&g2_mem[ONDA_OFF], &memoria[RAIZ_OFF], n);
	antes = dobles_int_normal;

	/* La RAM de video no es un destino valido del lado del G2. */
	armar(0, 0x05000000, RAIZ, (DWORD) n, 0);
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_U32(leer(0, G2DMA_ST), 0);
	ESPERAR_I32(dobles_int_normal, antes);

	/* Y del lado de la raiz, los registros del AICA tampoco lo son. */
	antes = dobles_int_normal;

	armar(0, ONDA, 0x00700000, (DWORD) n, 0);
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_U32(leer(0, G2DMA_ST), 0);
	ESPERAR_I32(dobles_int_normal, antes);
	ESPERAR_U32(g2_mem[ONDA_OFF], 0);
}

static void los_canales_no_se_pisan(void)
{
	/* Cuatro juegos de ocho registros, con 0x20 bytes de paso. Un error de
	   indice aqui haria que armar un canal moviera otro. */
	int canal, reg;

	g2dma_reset();

	for (canal = 0; canal < G2DMA_CANALES; canal++)
		for (reg = 0; reg < G2DMA_REGS; reg++)
			if (reg != G2DMA_EN && reg != G2DMA_ST && reg != G2DMA_SUSP)
				escribir(canal, reg, 0x1000 * (canal + 1) + reg);

	for (canal = 0; canal < G2DMA_CANALES; canal++)
		for (reg = 0; reg < G2DMA_REGS; reg++)
			if (reg != G2DMA_EN && reg != G2DMA_ST && reg != G2DMA_SUSP)
				ESPERAR_U32(leer(canal, reg),
					(DWORD) (0x1000 * (canal + 1) + reg));
}

static void el_largo_cero_son_32_mb(void)
{
	/* "0x00000000: 32MByte". Con 32 MB ningun rango valido alcanza, asi que
	   la transferencia se rechaza -- que es lo correcto, y sobre todo no es
	   "no hay nada que copiar". */
	int antes = dobles_int_normal;

	armar(0, ONDA, RAIZ, 0, 0);
	escribir(0, G2DMA_EN, 1);
	escribir(0, G2DMA_ST, 1);

	ESPERAR_U32(leer(0, G2DMA_ST), 0);
	ESPERAR_I32(dobles_int_normal, antes);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(la_transferencia_hacia_el_g2_copia),
	CASO(la_transferencia_desde_el_g2_copia),
	CASO(st_se_lee_cero_al_terminar),
	CASO(el_bit_31_del_largo_apaga_la_habilitacion),
	CASO(sin_habilitacion_no_arranca),
	CASO(el_fin_de_dma_interrumpe_por_el_bit_que_toca),
	CASO(un_rango_invalido_no_transfiere),
	CASO(los_canales_no_se_pisan),
	CASO(el_largo_cero_son_32_mb),
};

const dc_suite suite_g2dma = DEFINIR_SUITE("g2dma", casos);
