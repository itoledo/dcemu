/****************************************************************************

	Pruebas de mmu.c -- la ventana de control P4 del SH-4.

	Fase 1 de docs/mmu-plan.md: todavia no hay traduccion, asi que lo que se
	prueba es que los ocho arreglos memoria-mapeados indexen la entrada
	correcta y devuelvan lo que se les escribio.

	Es logica pura sobre arreglos: no hace falta CPU, ni memoria, ni ventana.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "suites.h"

#include "excepciones.h"
#include "mmu.h"

/* ------------------------------------------------------------------------ */
/* Ayudantes                                                                */
/* ------------------------------------------------------------------------ */

#define DIR_ITLB(e)			(0xF2000000ul | ((DWORD)(e) << 8))
#define DIR_ITLB_D1(e)		(0xF3000000ul | ((DWORD)(e) << 8))
#define DIR_ITLB_D2(e)		(0xF3800000ul | ((DWORD)(e) << 8))

#define DIR_UTLB(e)			(0xF6000000ul | ((DWORD)(e) << 8))
#define DIR_UTLB_A(e)		(0xF6000000ul | ((DWORD)(e) << 8) | MMU_BIT_A_TLB)
#define DIR_UTLB_D1(e)		(0xF7000000ul | ((DWORD)(e) << 8))
#define DIR_UTLB_D2(e)		(0xF7800000ul | ((DWORD)(e) << 8))

/* Una entrada de la cache de operandos ocupa 32 bytes, asi que las entradas
   consecutivas estan a 0x20. Es el paso que se ve en la traza de KOS. */
#define DIR_OC(n)			(0xF4000000ul | ((DWORD)(n) << 5))

#define BIT_V				MMU_BIT_V
#define BIT_D				MMU_BIT_D_DIR
#define BIT_SH				MMU_BIT_SH
#define BIT_D_DIR			MMU_BIT_D_DIR
#define BIT_D_DAT			MMU_BIT_D_DAT

static DWORD leer(DWORD direccion)
{
	DWORD valor = 0xDEADBEEF;

	mmu_p4_read(direccion, &valor, sizeof(DWORD));

	return valor;
}

static void escribir(DWORD direccion, DWORD valor)
{
	mmu_p4_write(direccion, &valor, sizeof(DWORD));
}

static void partir(void)
{
	arnes_reset();
	mmu_reset();
}

/* ------------------------------------------------------------ arranque --- */

static void reset_deja_la_tlb_en_blanco(void)
{
	int i;

	partir();

	escribir(DIR_UTLB(7), 0x12345000 | BIT_V);
	escribir(DIR_ITLB(2), 0x00042000 | BIT_V);

	mmu_reset();

	for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
		ESPERAR_U32(mmu_utlb_dir[i], 0);

	for (i = 0; i < MMU_ITLB_ENTRADAS; i++)
		ESPERAR_U32(mmu_itlb_dir[i], 0);
}

/* ---------------------------------------------------------------- ITLB --- */

/* La ITLB tiene 4 entradas y las selecciona con los bits 9-8. */
static void itlb_indexa_con_los_bits_9_8(void)
{
	int e;

	partir();

	for (e = 0; e < MMU_ITLB_ENTRADAS; e++)
		escribir(DIR_ITLB(e), 0xAABB0000 | (DWORD) e);

	for (e = 0; e < MMU_ITLB_ENTRADAS; e++)
	{
		ESPERAR_U32(mmu_itlb_dir[e], 0xAABB0000 | (DWORD) e);
		ESPERAR_U32(leer(DIR_ITLB(e)), 0xAABB0000 | (DWORD) e);
	}
}

/* Los dos arreglos de datos comparten el byte alto 0xF3; los separa el bit 23. */
static void itlb_separa_los_dos_arreglos_de_datos(void)
{
	partir();

	escribir(DIR_ITLB_D1(1), 0x11111111);
	escribir(DIR_ITLB_D2(1), 0x22222222);

	ESPERAR_U32(mmu_itlb_dat1[1], 0x11111111);
	ESPERAR_U32(mmu_itlb_dat2[1], 0x22222222);

	ESPERAR_U32(leer(DIR_ITLB_D1(1)), 0x11111111);
	ESPERAR_U32(leer(DIR_ITLB_D2(1)), 0x22222222);
}

/* ---------------------------------------------------------------- UTLB --- */

/* La UTLB tiene 64 entradas y usa los bits 13-8, dos mas que la ITLB. */
static void utlb_indexa_con_los_bits_13_8(void)
{
	int e;

	partir();

	for (e = 0; e < MMU_UTLB_ENTRADAS; e++)
		escribir(DIR_UTLB(e), 0xC0DE0000 | (DWORD) e);

	for (e = 0; e < MMU_UTLB_ENTRADAS; e++)
	{
		ESPERAR_U32(mmu_utlb_dir[e], 0xC0DE0000 | (DWORD) e);
		ESPERAR_U32(leer(DIR_UTLB(e)), 0xC0DE0000 | (DWORD) e);
	}

	/* La entrada 63 es la ultima: el indice no debe desbordar a otra zona. */
	ESPERAR_U32(MMU_UTLB_INDICE(DIR_UTLB(63)), 63);
}

static void utlb_separa_los_dos_arreglos_de_datos(void)
{
	partir();

	escribir(DIR_UTLB_D1(40), 0x33333333);
	escribir(DIR_UTLB_D2(40), 0x44444444);

	ESPERAR_U32(mmu_utlb_dat1[40], 0x33333333);
	ESPERAR_U32(mmu_utlb_dat2[40], 0x44444444);

	ESPERAR_U32(leer(DIR_UTLB_D1(40)), 0x33333333);
	ESPERAR_U32(leer(DIR_UTLB_D2(40)), 0x44444444);
}

/* --------------------------------------------------------- asociativo --- */

/*
	Con el bit A puesto no se indexa: se busca por VPN y ASID. Es como el
	software invalida una pagina puntual sin saber en que entrada quedo.
*/
static void asociativo_toca_la_entrada_que_coincide(void)
{
	partir();

	/* Tres entradas validas, VPN distintos. */
	escribir(DIR_UTLB(5),  0x12345000 | BIT_V | 0x11);
	escribir(DIR_UTLB(20), 0x67890000 | BIT_V | 0x11);
	escribir(DIR_UTLB(50), 0xABCDE000 | BIT_V | 0x11);

	/* Invalidar la del medio por VPN, escribiendo en una direccion cuyos
	   bits 13-8 apuntan a otra entrada cualquiera: no deben usarse. */
	escribir(DIR_UTLB_A(0), 0x67890000 | 0x11);

	ESPERAR_U32(mmu_utlb_dir[5]  & BIT_V, BIT_V);
	ESPERAR_U32(mmu_utlb_dir[20] & BIT_V, 0);
	ESPERAR_U32(mmu_utlb_dir[50] & BIT_V, BIT_V);

	/* El resto de la entrada no se toca: sigue estando el VPN. */
	ESPERAR_U32(mmu_utlb_dir[20] & 0xFFFFFC00, 0x67890000);

	/* Y la entrada 0, que es la que se habria pisado de haberse indexado,
	   sigue en blanco. */
	ESPERAR_U32(mmu_utlb_dir[0], 0);
}

static void asociativo_respeta_el_asid(void)
{
	partir();

	escribir(DIR_UTLB(9), 0x12345000 | BIT_V | 0x22);

	/* Mismo VPN, otro ASID, pagina no compartida: no coincide. */
	escribir(DIR_UTLB_A(0), 0x12345000 | 0x33);
	ESPERAR_U32(mmu_utlb_dir[9] & BIT_V, BIT_V);

	/* Mismo VPN y mismo ASID: ahora si. */
	escribir(DIR_UTLB_A(0), 0x12345000 | 0x22);
	ESPERAR_U32(mmu_utlb_dir[9] & BIT_V, 0);
}

static void asociativo_ignora_el_asid_si_la_pagina_es_compartida(void)
{
	partir();

	escribir(DIR_UTLB(9), 0x12345000 | BIT_V | 0x22);
	escribir(DIR_UTLB_D1(9), BIT_SH);

	/* Con SH=1 coincide con cualquier ASID. */
	escribir(DIR_UTLB_A(0), 0x12345000 | 0x33);

	ESPERAR_U32(mmu_utlb_dir[9] & BIT_V, 0);
}

static void asociativo_no_mira_las_entradas_invalidas(void)
{
	partir();

	/* Entrada con el VPN buscado pero invalida: no debe considerarse. */
	escribir(DIR_UTLB(3), 0x12345000 | BIT_D);

	escribir(DIR_UTLB_A(0), 0x12345000 | BIT_V);

	/* Si la hubiera tomado, le habria puesto V. */
	ESPERAR_U32(mmu_utlb_dir[3] & BIT_V, 0);
}

/* --------------------------------------------------------------- cache --- */

/*
	dcemu no emula cache. Toda linea se reporta invalida, y las escrituras se
	descartan: como el emulador escribe siempre directo a memoria, nunca hay
	nada sucio que volcar.
*/
static void cache_siempre_reporta_linea_invalida(void)
{
	partir();

	escribir(DIR_OC(0),   0x0C000000 | 1);		/* etiqueta con V=1 */
	escribir(DIR_OC(511), 0x0C001000 | 1);

	ESPERAR_U32(leer(DIR_OC(0)),   0);
	ESPERAR_U32(leer(DIR_OC(511)), 0);

	ESPERAR_U32(leer(0xF0000000ul), 0);			/* instrucciones, direcciones */
	ESPERAR_U32(leer(0xF1000000ul), 0);			/* instrucciones, datos */
	ESPERAR_U32(leer(0xF5000000ul), 0);			/* operandos, datos */
}

/* El barrido que hace KOS al arrancar no debe tocar la TLB. */
static void barrido_de_cache_no_toca_la_tlb(void)
{
	int n;

	partir();

	escribir(DIR_UTLB(11), 0x12345000 | BIT_V);

	for (n = 0; n < 512; n++)
		escribir(DIR_OC(n), 0);

	ESPERAR_U32(mmu_utlb_dir[11], 0x12345000 | BIT_V);
}

/* --------------------------------------------------------------- MMUCR --- */

static void mmucr_ti_invalida_toda_la_tlb(void)
{
	partir();

	escribir(DIR_UTLB(1),    0x11111000 | BIT_V);
	escribir(DIR_UTLB_D1(1), 0x22222000 | BIT_V);
	escribir(DIR_ITLB(2),    0x33333000 | BIT_V);
	escribir(DIR_ITLB_D1(2), 0x44444000 | BIT_V);

	mmu_mmucr_escrito(MMUCR_TI);

	ESPERAR_U32(mmu_utlb_dir[1]  & BIT_V, 0);
	ESPERAR_U32(mmu_utlb_dat1[1] & BIT_V, 0);
	ESPERAR_U32(mmu_itlb_dir[2]  & BIT_V, 0);
	ESPERAR_U32(mmu_itlb_dat1[2] & BIT_V, 0);

	/* Solo baja V; el VPN sigue ahi. */
	ESPERAR_U32(mmu_utlb_dir[1] & 0xFFFFFC00, 0x11111000);
}

static void mmucr_avisa_cuando_encienden_la_traduccion(void)
{
	partir();

	ESPERAR_U32(mmu_mmucr_escrito(0), 0);
	ESPERAR_U32(mmu_mmucr_escrito(MMUCR_SV | MMUCR_SQMD), 0);
	ESPERAR_U32(mmu_mmucr_escrito(MMUCR_AT), 1);
}

static void mmucr_descompone_urc_y_urb(void)
{
	partir();

	/* URC en los bits 15-10, URB en los 23-18. */
	ESPERAR_U32(MMUCR_URC(0x0000FC00ul), 0x3F);
	ESPERAR_U32(MMUCR_URB(0x00FC0000ul), 0x3F);

	ESPERAR_U32(MMUCR_URC(0x00002400ul), 9);
	ESPERAR_U32(MMUCR_URB(0x00240000ul), 9);
}

/* --------------------------------------------------------------- LDTLB --- */

/* Arma una entrada valida en la UTLB por la via de LDTLB. */
static void cargar(int urc, DWORD vpn, DWORD asid, DWORD ppn, DWORD bits)
{
	*PTEH  = (vpn & 0xFFFFFC00) | (asid & 0xFF);
	*PTEL  = (ppn & 0x1FFFFC00) | BIT_V | bits;
	*PTEA  = 0;
	*MMUCR = (*MMUCR & ~0x0000FC00ul) | ((DWORD) urc << 10);

	mmu_ldtlb(*PTEH, *PTEL, *PTEA, urc);
}

/* PR=11 (todos, lectura/escritura), D=1, pagina de 4 KB. */
#define PAG4K_RW	(0x00000010ul | (3ul << 5) | BIT_D_DAT)

static void ldtlb_carga_la_entrada_que_apunta_urc(void)
{
	partir();

	cargar(17, 0x12345000, 0x2A, 0x0C001000, PAG4K_RW);

	/* Direcciones: VPN y ASID de PTEH, mas V y D que vienen de PTEL. */
	ESPERAR_U32(mmu_utlb_dir[17] & 0xFFFFFC00, 0x12345000);
	ESPERAR_U32(mmu_utlb_dir[17] & 0xFF, 0x2A);
	ESPERAR_U32(mmu_utlb_dir[17] & BIT_V, BIT_V);
	ESPERAR_U32(mmu_utlb_dir[17] & BIT_D_DIR, BIT_D_DIR);

	/* Datos 1: PTEL tal cual, sin los bits reservados. */
	ESPERAR_U32(mmu_utlb_dat1[17] & 0x1FFFFC00, 0x0C001000);

	/* Y no toco ninguna otra entrada. */
	ESPERAR_U32(mmu_utlb_dir[16], 0);
	ESPERAR_U32(mmu_utlb_dir[18], 0);
}

/* V y D son un solo bit del chip, visible desde los dos arreglos. */
static void v_y_d_se_ven_desde_los_dos_arreglos(void)
{
	partir();

	escribir(DIR_UTLB(3), 0x40000000 | BIT_V | BIT_D_DIR);
	ESPERAR_U32(mmu_utlb_dat1[3] & BIT_V, BIT_V);
	ESPERAR_U32(mmu_utlb_dat1[3] & BIT_D_DAT, BIT_D_DAT);

	escribir(DIR_UTLB_D1(3), 0);
	ESPERAR_U32(mmu_utlb_dir[3] & BIT_V, 0);
	ESPERAR_U32(mmu_utlb_dir[3] & BIT_D_DIR, 0);
}

/* ---------------------------------------------------------- traduccion --- */

/*
	Las regiones sin traducir salen tal cual. Convertirlas a fisica romperia
	el despacho de dcemu, donde mem_hash[0x80] y mem_hash[0xA0] no son lo
	mismo aunque la direccion fisica si lo sea.
*/
static void p1_y_p2_no_pasan_por_la_tlb(void)
{
	partir();
	mmu_activa = 1;

	ESPERAR_U32(mmu_traducir(0x8C001000, MMU_LECTURA),   0x8C001000);
	ESPERAR_U32(mmu_traducir(0xAC001000, MMU_ESCRITURA), 0xAC001000);
	ESPERAR_U32(mmu_traducir(0xA05F8128, MMU_LECTURA),   0xA05F8128);

	mmu_activa = 0;
}

/* Un acierto sale por la ventana P2, que es la unica con cobertura completa. */
static void acierto_devuelve_la_fisica_en_p2(void)
{
	partir();
	mmu_activa = 1;

	cargar(0, 0x12345000, 0, 0x0C001000, PAG4K_RW);

	ESPERAR_U32(mmu_traducir(0x12345000, MMU_LECTURA),  0xAC001000);
	ESPERAR_U32(mmu_traducir(0x12345ABC, MMU_LECTURA),  0xAC001ABC);
	ESPERAR_U32(mmu_traducir(0x12345FFF, MMU_ESCRITURA), 0xAC001FFF);

	mmu_activa = 0;
}

/* Los cuatro tamanos de pagina: cambia cuanto se compara y cuanto se copia. */
static void los_cuatro_tamanos_de_pagina(void)
{
	partir();
	mmu_activa = 1;

	/* 1 KB: SZ=00 */
	cargar(0, 0x00010000, 0, 0x0C000000, (0ul << 5) | (3ul << 5) | BIT_D_DAT);
	ESPERAR_U32(mmu_traducir(0x000103FF, MMU_LECTURA), 0xAC0003FF);

	/* 64 KB: SZ=10 -> bit 7 */
	mmu_reset();
	mmu_activa = 1;
	cargar(0, 0x00010000, 0, 0x0C000000, 0x80ul | (3ul << 5) | BIT_D_DAT);
	ESPERAR_U32(mmu_traducir(0x0001F000, MMU_LECTURA), 0xAC00F000);

	/* 1 MB: SZ=11 -> bits 7 y 4 */
	mmu_reset();
	mmu_activa = 1;
	cargar(0, 0x00100000, 0, 0x0C000000, 0x90ul | (3ul << 5) | BIT_D_DAT);
	ESPERAR_U32(mmu_traducir(0x001ABCDE, MMU_LECTURA), 0xAC0ABCDE);

	mmu_activa = 0;
}

static void el_asid_tiene_que_coincidir(void)
{
	partir();
	mmu_activa = 1;

	cargar(0, 0x12345000, 0x11, 0x0C001000, PAG4K_RW);

	/* Con el ASID actual distinto del de la entrada, no acierta. */
	*PTEH = (*PTEH & 0xFFFFFC00) | 0x99;
	mmu_traducir(0x12345000, MMU_LECTURA);
	ESPERAR_U32(excepcion_codigo, MMU_EXC_FALLO_R);

	/* Con el mismo, si. */
	*PTEH = (*PTEH & 0xFFFFFC00) | 0x11;
	ESPERAR_U32(mmu_traducir(0x12345000, MMU_LECTURA), 0xAC001000);

	mmu_activa = 0;
}

/* --------------------------------------------------------- excepciones --- */

/*
	Sin salto armado, fallar() vuelve en vez de saltar, asi que se puede mirar
	el fallo que dejo preparado. Con salto armado eso lo hace main_loop().
*/
static void fallo_de_tlb_deja_codigo_vector_tea_y_pteh(void)
{
	partir();
	mmu_activa = 1;

	*PTEH = 0x00000055;		/* ASID que tiene que sobrevivir */

	mmu_traducir(0x87654321 & 0x7FFFFFFF, MMU_LECTURA);

	ESPERAR_U32(excepcion_codigo, MMU_EXC_FALLO_R);
	ESPERAR_U32(excepcion_vector, MMU_VEC_FALLO);
	ESPERAR_U32(*TEA, 0x07654321);
	ESPERAR_U32(*PTEH & 0xFFFFFC00, 0x07654000);
	ESPERAR_U32(*PTEH & 0xFF, 0x55);

	mmu_traducir(0x07654321, MMU_ESCRITURA);
	ESPERAR_U32(excepcion_codigo, MMU_EXC_FALLO_W);
	ESPERAR_U32(excepcion_vector, MMU_VEC_FALLO);

	mmu_activa = 0;
}

/* Escribir en una pagina limpia no es violacion de proteccion. */
static void escribir_en_pagina_limpia_es_primera_escritura(void)
{
	partir();
	mmu_activa = 1;

	/* Misma entrada pero con D=0. */
	cargar(0, 0x12345000, 0, 0x0C001000, 0x00000010ul | (3ul << 5));

	/* Leer funciona. */
	ESPERAR_U32(mmu_traducir(0x12345000, MMU_LECTURA), 0xAC001000);

	/* Escribir da 0x080, no 0x0C0, y por el vector general. */
	mmu_traducir(0x12345000, MMU_ESCRITURA);
	ESPERAR_U32(excepcion_codigo, MMU_EXC_PRIMERA_W);
	ESPERAR_U32(excepcion_vector, MMU_VEC_GENERAL);

	mmu_activa = 0;
}

static void pr_decide_quien_lee_y_quien_escribe(void)
{
	partir();
	mmu_activa = 1;

	/* PR=10: todos, solo lectura. D=1 para aislar la proteccion. */
	cargar(0, 0x12345000, 0, 0x0C001000, 0x00000010ul | (2ul << 5) | BIT_D_DAT);

	ESPERAR_U32(mmu_traducir(0x12345000, MMU_LECTURA), 0xAC001000);

	mmu_traducir(0x12345000, MMU_ESCRITURA);
	ESPERAR_U32(excepcion_codigo, MMU_EXC_PROT_W);
	ESPERAR_U32(excepcion_vector, MMU_VEC_GENERAL);

	mmu_activa = 0;
}

static void modo_usuario_no_llega_a_la_pagina_privilegiada(void)
{
	partir();
	mmu_activa = 1;

	/* PR=01: privilegiado, lectura/escritura. */
	cargar(0, 0x12345000, 0, 0x0C001000, 0x00000010ul | (1ul << 5) | BIT_D_DAT);

	/* En privilegiado entra. */
	SR_MD = 1;
	ESPERAR_U32(mmu_traducir(0x12345000, MMU_LECTURA), 0xAC001000);

	/* En usuario, violacion de proteccion. */
	SR_MD = 0;
	mmu_traducir(0x12345000, MMU_LECTURA);
	ESPERAR_U32(excepcion_codigo, MMU_EXC_PROT_R);

	/* Y P1 desde usuario es error de direccion, no de proteccion. */
	mmu_traducir(0x8C001000, MMU_LECTURA);
	ESPERAR_U32(excepcion_codigo, MMU_EXC_DIR_R);
	ESPERAR_U32(excepcion_vector, MMU_VEC_GENERAL);

	SR_MD = 1;
	mmu_activa = 0;
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(reset_deja_la_tlb_en_blanco),
	CASO(itlb_indexa_con_los_bits_9_8),
	CASO(itlb_separa_los_dos_arreglos_de_datos),
	CASO(utlb_indexa_con_los_bits_13_8),
	CASO(utlb_separa_los_dos_arreglos_de_datos),
	CASO(asociativo_toca_la_entrada_que_coincide),
	CASO(asociativo_respeta_el_asid),
	CASO(asociativo_ignora_el_asid_si_la_pagina_es_compartida),
	CASO(asociativo_no_mira_las_entradas_invalidas),
	CASO(cache_siempre_reporta_linea_invalida),
	CASO(barrido_de_cache_no_toca_la_tlb),
	CASO(mmucr_ti_invalida_toda_la_tlb),
	CASO(mmucr_avisa_cuando_encienden_la_traduccion),
	CASO(mmucr_descompone_urc_y_urb),
	CASO(ldtlb_carga_la_entrada_que_apunta_urc),
	CASO(v_y_d_se_ven_desde_los_dos_arreglos),
	CASO(p1_y_p2_no_pasan_por_la_tlb),
	CASO(acierto_devuelve_la_fisica_en_p2),
	CASO(los_cuatro_tamanos_de_pagina),
	CASO(el_asid_tiene_que_coincidir),
	CASO(fallo_de_tlb_deja_codigo_vector_tea_y_pteh),
	CASO(escribir_en_pagina_limpia_es_primera_escritura),
	CASO(pr_decide_quien_lee_y_quien_escribe),
	CASO(modo_usuario_no_llega_a_la_pagina_privilegiada),
};

const dc_suite suite_mmu = DEFINIR_SUITE("mmu", casos);
