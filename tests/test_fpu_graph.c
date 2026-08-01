/****************************************************************************

	Pruebas de floatgraph.c -- instrucciones graficas de la FPU

	Son las que el SH-4 agrega para 3D: producto punto de vectores de 4
	(FIPR), multiplicacion de un vector por la matriz XMTRX (FTRV), raiz
	inversa (FSRRA), seno y coseno juntos (FSCA) y los movimientos hacia el
	banco alternativo XD.

	Casos borde que cubre esta suite:

	  - Los registros float se ven de tres formas segun la instruccion:
		sueltos (FR0-FR15), en pares (DR0-DR7) o en vectores de cuatro
		(FV0-FV3). FIPR y FTRV usan la tercera, asi que el numero de vector n
		se refiere a FR[4n] y el resultado va a FR[4n+3].
	  - FTRV con la identidad tiene que dejar el vector igual: es la prueba de
		que la matriz no esta transpuesta.
	  - FSCA reparte seno y coseno en las dos mitades de DRn, y toma el angulo
		como una fraccion de vuelta en 16 bits, no en radianes.
	  - FRCHG intercambia los bancos; FSCHG cambia el tamano de los FMOV.
	  - Los FMOV con XD solo existen con SZ=1.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* Bits de FPSCR como valor. Los accesores de campo (FPSCR_SZ_BIT, FPSCR_FR,
   ...) ya vienen de sh4emu.h; estos son las mascaras. */
#define BIT_SZ	0x00100000u
#define BIT_PR	0x00080000u

static void modo_sz1(void)
{
	UpdateFPSCR((FPSCR & ~BIT_PR) | BIT_SZ);
}

static void poner_vector(int v, float a, float b, float c, float d)
{
	FR(v * 4 + 0) = a;
	FR(v * 4 + 1) = b;
	FR(v * 4 + 2) = c;
	FR(v * 4 + 3) = d;
}

static void identidad_en_xmtrx(void)
{
	int i;

	for (i = 0; i < 16; i++)
		XF(i) = (i % 5) == 0 ? 1.0f : 0.0f;
}

/* ------------------------------------------------------------------ FIPR */

static void fipr_deja_el_producto_punto_en_fr3(void)
{
	arnes_reset();

	poner_vector(0, 1.0f, 2.0f, 3.0f, 4.0f);	/* FV0 */
	poner_vector(1, 5.0f, 6.0f, 7.0f, 8.0f);	/* FV1 */

	ejecutar((WORD) (0xF0ED | (0 << 10) | (1 << 8)));	/* FIPR FV1, FV0 */

	/* 1*5 + 2*6 + 3*7 + 4*8 = 70, y va al ultimo registro de FV0 */
	ESPERAR_F32(FR(3), 70.0f);
	ESPERAR_PC_SIGUIENTE();
}

static void fipr_no_toca_el_vector_fuente(void)
{
	arnes_reset();

	poner_vector(0, 1.0f, 2.0f, 3.0f, 4.0f);
	poner_vector(1, 5.0f, 6.0f, 7.0f, 8.0f);

	ejecutar((WORD) (0xF0ED | (0 << 10) | (1 << 8)));

	ESPERAR_F32(FR(4), 5.0f);
	ESPERAR_F32(FR(7), 8.0f);
}

/* El resultado va a FR[4n+3]. Con n = 1 eso es FR7, no FR4. */
static void fipr_con_vector_destino_distinto_de_cero(void)
{
	arnes_reset();

	poner_vector(0, 1.0f, 2.0f, 3.0f, 4.0f);	/* FV0 */
	poner_vector(1, 5.0f, 6.0f, 7.0f, 8.0f);	/* FV1 */

	ejecutar((WORD) (0xF0ED | (1 << 10) | (0 << 8)));	/* FIPR FV0, FV1 */

	ESPERAR_F32(FR(7), 70.0f);
}

/* ------------------------------------------------------------------ FTRV */

static void ftrv_con_la_identidad_no_cambia_el_vector(void)
{
	arnes_reset();

	identidad_en_xmtrx();
	poner_vector(0, 1.0f, 2.0f, 3.0f, 4.0f);

	ejecutar((WORD) (0xF1FD | (0 << 10)));	/* FTRV XMTRX, FV0 */

	ESPERAR_F32(FR(0), 1.0f);
	ESPERAR_F32(FR(1), 2.0f);
	ESPERAR_F32(FR(2), 3.0f);
	ESPERAR_F32(FR(3), 4.0f);
	ESPERAR_PC_SIGUIENTE();
}

static void ftrv_escala_por_la_diagonal(void)
{
	arnes_reset();

	identidad_en_xmtrx();
	XF(0)  = 2.0f;
	XF(5)  = 3.0f;
	XF(10) = 4.0f;
	XF(15) = 5.0f;

	poner_vector(0, 1.0f, 1.0f, 1.0f, 1.0f);

	ejecutar((WORD) (0xF1FD | (0 << 10)));

	ESPERAR_F32(FR(0), 2.0f);
	ESPERAR_F32(FR(1), 3.0f);
	ESPERAR_F32(FR(2), 4.0f);
	ESPERAR_F32(FR(3), 5.0f);
}

/* La traslacion vive en la ultima columna: m[12..15] multiplican la cuarta
   componente del vector. Si la matriz estuviera transpuesta, este caso daria
   otra cosa. */
static void ftrv_aplica_la_traslacion(void)
{
	arnes_reset();

	identidad_en_xmtrx();
	XF(12) = 10.0f;
	XF(13) = 20.0f;
	XF(14) = 30.0f;

	poner_vector(0, 1.0f, 2.0f, 3.0f, 1.0f);

	ejecutar((WORD) (0xF1FD | (0 << 10)));

	ESPERAR_F32(FR(0), 11.0f);
	ESPERAR_F32(FR(1), 22.0f);
	ESPERAR_F32(FR(2), 33.0f);
	ESPERAR_F32(FR(3), 1.0f);
}

static void ftrv_sobre_el_vector_1(void)
{
	arnes_reset();

	identidad_en_xmtrx();
	XF(0) = 2.0f;

	poner_vector(1, 1.0f, 2.0f, 3.0f, 4.0f);

	ejecutar((WORD) (0xF1FD | (1 << 10)));	/* FTRV XMTRX, FV1 */

	ESPERAR_F32(FR(4), 2.0f);
	ESPERAR_F32(FR(5), 2.0f);
	ESPERAR_F32(FR(6), 3.0f);
	ESPERAR_F32(FR(7), 4.0f);
}

/* ----------------------------------------------------------------- FSRRA */

static void fsrra_da_la_raiz_inversa(void)
{
	arnes_reset();

	FR(3) = 4.0f;
	ejecutar(instr_n(0xF07D, 3));		/* FSRRA FR3 */

	ESPERAR_F32_APROX(FR(3), 0.5f, 1e-6f);
	ESPERAR_PC_SIGUIENTE();
}

static void fsrra_de_uno(void)
{
	arnes_reset();

	FR(3) = 1.0f;
	ejecutar(instr_n(0xF07D, 3));

	ESPERAR_F32_APROX(FR(3), 1.0f, 1e-6f);
}

/* ------------------------------------------------------------------ FSCA */

static void fsca_en_cero(void)
{
	arnes_reset();

	FPUL = 0;
	ejecutar((WORD) (0xF0FD | (0 << 9)));	/* FSCA FPUL, DR0 */

	ESPERAR_F32_APROX(FR(0), 0.0f, 1e-6f);	/* seno */
	ESPERAR_F32_APROX(FR(1), 1.0f, 1e-6f);	/* coseno */
	ESPERAR_PC_SIGUIENTE();
}

/* El angulo es una fraccion de vuelta en 16 bits: 0x4000 es un cuarto de
   vuelta, no 16384 radianes. */
static void fsca_en_un_cuarto_de_vuelta(void)
{
	arnes_reset();

	FPUL = 0x4000;
	ejecutar((WORD) (0xF0FD | (0 << 9)));

	ESPERAR_F32_APROX(FR(0), 1.0f, 1e-5f);
	ESPERAR_F32_APROX(FR(1), 0.0f, 1e-5f);
}

static void fsca_en_media_vuelta(void)
{
	arnes_reset();

	FPUL = 0x8000;
	ejecutar((WORD) (0xF0FD | (1 << 9)));	/* FSCA FPUL, DR1 */

	ESPERAR_F32_APROX(FR(2), 0.0f, 1e-5f);
	ESPERAR_F32_APROX(FR(3), -1.0f, 1e-5f);
}

/* -------------------------------------------------------- FRCHG / FSCHG */

/* El angulo son los **16 bits bajos** de FPUL y nada mas: la parte alta no
   entra. Tomando FPUL entero -- y ademas sin signo, que es DWORD -- el angulo
   salia de miles de millones de radianes y las 500 pruebas de
   1111nnn011111101 de SingleStepTests fallaban todas. */
static void fsca_solo_mira_los_16_bits_bajos(void)
{
	arnes_reset();

	FPUL = 0xABCD4000;					/* un cuarto de vuelta, con basura arriba */
	ejecutar((WORD) (0xF0FD | (0 << 9)));

	ESPERAR_F32_APROX(FR(0), 1.0f, 1e-5f);
	ESPERAR_F32_APROX(FR(1), 0.0f, 1e-5f);
}

/* Y con el bit 31 puesto, que es lo que hacia que el (float) sin signo diera
   un angulo enorme en vez de medio giro. */
static void fsca_con_el_bit_alto_puesto(void)
{
	arnes_reset();

	FPUL = 0x80008000;					/* media vuelta */
	ejecutar((WORD) (0xF0FD | (0 << 9)));

	ESPERAR_F32_APROX(FR(0), 0.0f, 1e-5f);
	ESPERAR_F32_APROX(FR(1), -1.0f, 1e-5f);
}

/* FIPR acumula los cuatro productos en doble y redondea una sola vez (manual,
   6.4). Con acumulador de simple precision, dos productos que desbordan a
   infinito con signos opuestos dan NaN; el chip entrega un infinito. */
static void fipr_no_desborda_en_los_productos_parciales(void)
{
	arnes_reset();

	poner_vector(0, 1e20f, 1e20f, 0.0f, 0.0f);
	poner_vector(1, 1e20f, -1e20f, 0.0f, 1.0f);

	ejecutar((WORD) (0xF0ED | (1 << 10) | (0 << 8)));	/* FIPR FV0, FV1 */

	/* 1e40 - 1e40 = 0 en doble; en simple los dos productos son +-inf. */
	ESPERAR_F32(FR(7), 0.0f);
}

static void frchg_intercambia_los_bancos(void)
{
	arnes_reset();

	FR(0) = 1.5f;
	XF(0) = 2.5f;

	ejecutar(0xFBFD);					/* FRCHG */

	ESPERAR_U32(FPSCR_FR, 1);
	ESPERAR_F32(FR(0), 2.5f);
	ESPERAR_F32(XF(0), 1.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void frchg_dos_veces_vuelve_al_principio(void)
{
	arnes_reset();

	FR(0) = 1.5f;
	XF(0) = 2.5f;

	ejecutar(0xFBFD);
	ejecutar(0xFBFD);

	ESPERAR_U32(FPSCR_FR, 0);
	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(XF(0), 2.5f);
}

static void fschg_cambia_el_tamano_de_transferencia(void)
{
	arnes_reset();

	ESPERAR_U32(FPSCR_SZ_BIT, 0);

	ejecutar(0xF3FD);					/* FSCHG */

	ESPERAR_U32(FPSCR_SZ_BIT, 1);
	ESPERAR_PC_SIGUIENTE();

	ejecutar(0xF3FD);

	ESPERAR_U32(FPSCR_SZ_BIT, 0);
}

/* Despues de FSCHG el mismo patron de bits mueve 8 bytes en vez de 4. */
static void fschg_cambia_lo_que_hace_fmov(void)
{
	arnes_reset();

	FR(4) = 1.5f;
	FR(5) = 2.5f;

	/* Con SZ=0, 0xF04C es FMOV FR4, FR0: mueve un solo registro. */
	ejecutar((WORD) (0xF00C | (0 << 8) | (4 << 4)));

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 0.0f);

	FR(0) = 0.0f;
	ejecutar(0xF3FD);					/* FSCHG */

	/* Con SZ=1, el mismo patron es FMOV DR2, DR0: mueve el par. */
	ejecutar((WORD) (0xF00C | (0 << 8) | (4 << 4)));

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 2.5f);
}

/* --------------------------------------------------------- FMOV con XD */

static void fmov_dr_a_xd(void)
{
	arnes_reset();
	modo_sz1();

	FR(0) = 1.5f;						/* DR0 */
	FR(1) = 2.5f;

	ejecutar((WORD) (0xF10C | (0 << 9) | (0 << 5)));	/* FMOV DR0, XD0 */

	ESPERAR_F32(XF(0), 1.5f);
	ESPERAR_F32(XF(1), 2.5f);
	ESPERAR_PC_SIGUIENTE();
}

static void fmov_xd_a_dr(void)
{
	arnes_reset();
	modo_sz1();

	XF(2) = 1.5f;						/* XD1 */
	XF(3) = 2.5f;

	ejecutar((WORD) (0xF01C | (0 << 9) | (1 << 5)));	/* FMOV XD1, DR0 */

	ESPERAR_F32(FR(0), 1.5f);
	ESPERAR_F32(FR(1), 2.5f);
}

static void fmov_xd_a_xd(void)
{
	arnes_reset();
	modo_sz1();

	XF(2) = 1.5f;
	XF(3) = 2.5f;

	ejecutar((WORD) (0xF11C | (0 << 9) | (1 << 5)));	/* FMOV XD1, XD0 */

	ESPERAR_F32(XF(0), 1.5f);
	ESPERAR_F32(XF(1), 2.5f);
}

static void fmov_carga_xd_desde_memoria(void)
{
	arnes_reset();
	modo_sz1();

	escribir_f(PRUEBA_DATOS + 0, 1.5f);
	escribir_f(PRUEBA_DATOS + 4, 2.5f);
	R(2) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF108 | (0 << 9) | (2 << 4)));	/* FMOV @R2, XD0 */

	ESPERAR_F32(XF(0), 1.5f);
	ESPERAR_F32(XF(1), 2.5f);
}

static void fmov_carga_xd_con_postincremento(void)
{
	arnes_reset();
	modo_sz1();

	escribir_f(PRUEBA_DATOS + 0, 1.5f);
	escribir_f(PRUEBA_DATOS + 4, 2.5f);
	R(2) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF109 | (0 << 9) | (2 << 4)));	/* FMOV @R2+, XD0 */

	ESPERAR_F32(XF(0), 1.5f);
	ESPERAR_F32(XF(1), 2.5f);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 8);
}

static void fmov_carga_xd_indexada(void)
{
	arnes_reset();
	modo_sz1();

	escribir_f(PRUEBA_DATOS + 0x10, 1.5f);
	escribir_f(PRUEBA_DATOS + 0x14, 2.5f);
	R(0) = 0x10;
	R(1) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF106 | (0 << 9) | (1 << 4)));	/* FMOV @(R0, R1), XD0 */

	ESPERAR_F32(XF(0), 1.5f);
	ESPERAR_F32(XF(1), 2.5f);
}

static void fmov_guarda_xd_en_memoria(void)
{
	arnes_reset();
	modo_sz1();

	XF(0) = 1.5f;
	XF(1) = 2.5f;
	R(1) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF01A | (1 << 8) | (0 << 5)));	/* FMOV XD0, @R1 */

	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0), 1.5f);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 4), 2.5f);
}

static void fmov_guarda_xd_con_predecremento(void)
{
	arnes_reset();
	modo_sz1();

	XF(0) = 1.5f;
	XF(1) = 2.5f;
	R(1) = PRUEBA_DATOS + 8;

	ejecutar((WORD) (0xF01B | (1 << 8) | (0 << 5)));	/* FMOV XD0, @-R1 */

	ESPERAR_U32(R(1), PRUEBA_DATOS);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0), 1.5f);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 4), 2.5f);
}

static void fmov_guarda_xd_indexado(void)
{
	arnes_reset();
	modo_sz1();

	XF(0) = 1.5f;
	XF(1) = 2.5f;
	R(0) = 0x10;
	R(1) = PRUEBA_DATOS;

	ejecutar((WORD) (0xF017 | (1 << 8) | (0 << 5)));	/* FMOV XD0, @(R0, R1) */

	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0x10), 1.5f);
	ESPERAR_F32(leer_f(PRUEBA_DATOS + 0x14), 2.5f);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(fipr_deja_el_producto_punto_en_fr3),
	CASO(fipr_no_toca_el_vector_fuente),
	CASO(fipr_con_vector_destino_distinto_de_cero),
	CASO(ftrv_con_la_identidad_no_cambia_el_vector),
	CASO(ftrv_escala_por_la_diagonal),
	CASO(ftrv_aplica_la_traslacion),
	CASO(ftrv_sobre_el_vector_1),
	CASO(fsrra_da_la_raiz_inversa),
	CASO(fsrra_de_uno),
	CASO(fsca_en_cero),
	CASO(fsca_en_un_cuarto_de_vuelta),
	CASO(fsca_en_media_vuelta),
	CASO(fsca_solo_mira_los_16_bits_bajos),
	CASO(fsca_con_el_bit_alto_puesto),
	CASO(fipr_no_desborda_en_los_productos_parciales),
	CASO(frchg_intercambia_los_bancos),
	CASO(frchg_dos_veces_vuelve_al_principio),
	CASO(fschg_cambia_el_tamano_de_transferencia),
	CASO(fschg_cambia_lo_que_hace_fmov),
	CASO(fmov_dr_a_xd),
	CASO(fmov_xd_a_dr),
	CASO(fmov_xd_a_xd),
	CASO(fmov_carga_xd_desde_memoria),
	CASO(fmov_carga_xd_con_postincremento),
	CASO(fmov_carga_xd_indexada),
	CASO(fmov_guarda_xd_en_memoria),
	CASO(fmov_guarda_xd_con_predecremento),
	CASO(fmov_guarda_xd_indexado),
};

const dc_suite suite_fpu_graph = DEFINIR_SUITE("fpu-graph", casos);
