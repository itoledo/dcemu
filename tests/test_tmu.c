/****************************************************************************

	Pruebas de tmu.c -- los tres temporizadores del SH-4.

	Logica pura sobre registros: no hace falta CPU ni ventana. Lo que se
	prueba es la conversion de TPSC a divisor, que el resto de ciclos no se
	pierda entre llamadas, el subdesborde con recarga desde TCOR, y que los
	tres canales sean independientes.

	Ver docs/clock-plan.md, fase 1.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "suites.h"

#include "tmu.h"

/* Punteros por canal, para escribir las pruebas en un bucle. */
static DWORD * tcnt_de(int n)	{ return n == 0 ? TCNT0 : (n == 1 ? TCNT1 : TCNT2); }
static DWORD * tcor_de(int n)	{ return n == 0 ? TCOR0 : (n == 1 ? TCOR1 : TCOR2); }
static WORD  * tcr_de (int n)	{ return n == 0 ? TCR0  : (n == 1 ? TCR1  : TCR2);  }

/* Deja los tres canales parados y en un estado conocido. */
static void partir(void)
{
	int n;

	arnes_reset();
	tmu_reset();

	*TSTR = 0;

	for (n = 0; n < TMU_CANALES; n++)
	{
		*tcnt_de(n) = 0xFFFFFFFF;
		*tcor_de(n) = 0xFFFFFFFF;
		*tcr_de(n)  = 0;
	}

	/* Con TSTR en cero, para que el primer tick vea el arranque. */
	tmu_tick(0);
}

/* Arranca un canal con TPSC, TCOR y UNIE dados. */
static void arrancar(int n, WORD tpsc, DWORD cor, int unie)
{
	*tcor_de(n) = cor;
	*tcnt_de(n) = cor;
	*tcr_de(n)  = (WORD) (tpsc | (unie ? TMU_TCR_UNIE : 0));

	*TSTR |= TMU_TSTR_STR(n);
}

/* ------------------------------------------------------------ el divisor -- */

/*
	El periferico es CPU/4 y TPSC divide otra vez por 4, 16, 64, 256 o 1024,
	asi que desde ciclos de CPU el divisor es 16, 64, 256, 1024 y 4096.

	Esto es lo que estaba mal: no habia divisor, se bajaba una cuenta cada 50
	ciclos. Con el TPSC que usa KOS (Pck/4, o sea 16) eso da 50/16 = 3,125x de
	error, que es exactamente lo que medía basic/watchdog.
*/
static void tpsc_da_el_divisor(void)
{
	arnes_reset();

	ESPERAR_U32(tmu_divisor(0), 16);
	ESPERAR_U32(tmu_divisor(1), 64);
	ESPERAR_U32(tmu_divisor(2), 256);
	ESPERAR_U32(tmu_divisor(3), 1024);
	ESPERAR_U32(tmu_divisor(4), 4096);

	/* Los tres que no existen en la Dreamcast caen al valor por omision. */
	ESPERAR_U32(tmu_divisor(5), 16);
	ESPERAR_U32(tmu_divisor(6), 16);
	ESPERAR_U32(tmu_divisor(7), 16);

	/* Los demas bits de TCR no cambian el divisor. */
	ESPERAR_U32(tmu_divisor(TMU_TCR_UNIE | TMU_TCR_UNF | 2), 256);
}

/* El numero que hace que las dos puntas coincidan. */
static void el_reloj_es_el_que_espera_kos(void)
{
	arnes_reset();

	/* Mismo valor que kernel/arch/dreamcast/kernel/timer.c de KOS. */
	ESPERAR_U32(DC_CPU_HZ, 199499520u);
	ESPERAR_U32(DC_PCK_HZ, 199499520u / 4);

	/* Y con el TPSC por omision de KOS, las cuentas por segundo que espera. */
	ESPERAR_U32(DC_CPU_HZ / tmu_divisor(0), 12468720u);
}

/* --------------------------------------------------------------- contar -- */

static void un_canal_parado_no_cuenta(void)
{
	partir();

	*tcnt_de(0) = 1000;
	/* Sin tocar TSTR: sigue parado. */

	tmu_tick(1000000);

	ESPERAR_U32(*tcnt_de(0), 1000);
}

static void baja_una_cuenta_por_divisor(void)
{
	partir();
	arrancar(0, 0, 1000, 0);		/* TPSC 0 -> divisor 16 */

	tmu_tick(15);
	ESPERAR_U32(*tcnt_de(0), 1000);	/* todavia no llego */

	tmu_tick(1);
	ESPERAR_U32(*tcnt_de(0), 999);	/* 16 ciclos: una cuenta */

	tmu_tick(16 * 100);
	ESPERAR_U32(*tcnt_de(0), 899);
}

/* El resto no se pierde entre llamadas, con cuentas que no son multiplos. */
static void no_pierde_el_resto(void)
{
	int i;

	partir();
	arrancar(0, 0, 10000, 0);

	/* Cien llamadas de 7 ciclos son 700, o sea 43 cuentas y sobran 12. */
	for (i = 0; i < 100; i++)
		tmu_tick(7);

	ESPERAR_U32(*tcnt_de(0), 10000 - 43);
	ESPERAR_U32(tmu_resto(0), 700 - 43 * 16);
}

/* Arrancar un canal pone su resto en cero. */
static void arrancar_pone_el_resto_en_cero(void)
{
	partir();
	arrancar(0, 0, 1000, 0);

	tmu_tick(15);					/* deja 15 de resto */
	ESPERAR_U32(tmu_resto(0), 15);

	/* Pararlo y volverlo a arrancar. */
	*TSTR &= ~TMU_TSTR_STR(0);
	tmu_tick(0);
	*TSTR |= TMU_TSTR_STR(0);
	tmu_tick(0);

	ESPERAR_U32(tmu_resto(0), 0);

	/* Y el ciclo suelto de antes no cuenta. */
	tmu_tick(1);
	ESPERAR_U32(*tcnt_de(0), 1000);
}

/* ---------------------------------------------------------- subdesborde -- */

/*
	Al llegar a cero y bajar una vez mas, recarga de TCOR y marca UNF. El
	periodo son TCOR+1 cuentas.
*/
static void subdesborde_recarga_de_tcor(void)
{
	partir();
	arrancar(0, 0, 3, 0);			/* TCOR = 3 -> periodo de 4 cuentas */

	tmu_tick(16 * 3);
	ESPERAR_U32(*tcnt_de(0), 0);
	ESPERAR_U32(*tcr_de(0) & TMU_TCR_UNF, 0);	/* todavia no */

	tmu_tick(16);
	ESPERAR_U32(*tcnt_de(0), 3);				/* recargado */
	ESPERAR_U32(*tcr_de(0) & TMU_TCR_UNF, TMU_TCR_UNF);
}

/* La interrupcion solo se pide si UNIE esta puesto. */
static void la_interrupcion_depende_de_unie(void)
{
	partir();
	arrancar(0, 0, 0, 0);			/* TCOR = 0, sin UNIE */

	ESPERAR_U32(tmu_tick(16), 0);				/* subdesborda, no pide */
	ESPERAR_U32(*tcr_de(0) & TMU_TCR_UNF, TMU_TCR_UNF);

	partir();
	arrancar(1, 0, 0, 1);			/* canal 1, con UNIE */

	ESPERAR_U32(tmu_tick(16), 1 << 1);			/* pide, y en su bit */
}

/* Varios subdesbordes en una sola llamada se colapsan en un pedido. */
static void varias_vueltas_en_una_llamada(void)
{
	partir();
	arrancar(2, 0, 0, 1);			/* TCOR = 0: una vuelta por cuenta */

	ESPERAR_U32(tmu_tick(16 * 5), 1 << 2);
	ESPERAR_U32(*tcnt_de(2), 0);
}

/* -------------------------------------------------------------- canales -- */

/* Los tres son independientes: distinto divisor, distinto TCOR, a la vez. */
static void los_tres_canales_son_independientes(void)
{
	partir();

	arrancar(0, 0, 100, 0);			/* divisor 16   */
	arrancar(1, 1, 100, 0);			/* divisor 64   */
	arrancar(2, 2, 100, 0);			/* divisor 256  */

	tmu_tick(256);

	ESPERAR_U32(*tcnt_de(0), 100 - 16);
	ESPERAR_U32(*tcnt_de(1), 100 - 4);
	ESPERAR_U32(*tcnt_de(2), 100 - 1);
}

/* Parar uno no afecta a los otros. */
static void parar_uno_no_toca_a_los_otros(void)
{
	partir();

	arrancar(0, 0, 100, 0);
	arrancar(1, 0, 100, 0);

	tmu_tick(16 * 10);
	ESPERAR_U32(*tcnt_de(0), 90);
	ESPERAR_U32(*tcnt_de(1), 90);

	*TSTR &= ~TMU_TSTR_STR(0);

	tmu_tick(16 * 10);
	ESPERAR_U32(*tcnt_de(0), 90);	/* parado */
	ESPERAR_U32(*tcnt_de(1), 80);	/* sigue */
}

/*
	El caso de KOS: timer_prime(TMU2, 1, 1) programa TCOR para que subdesborde
	una vez por segundo con Pck/4. Aca se comprueba que la cuenta cierra.
*/
static void el_segundo_de_kos(void)
{
	DWORD cuentas_por_segundo = DC_CPU_HZ / tmu_divisor(0);

	partir();

	/* KOS pone TCNT y TCOR en Pck/4 = 12468720 para un subdesborde por segundo. */
	arrancar(2, 0, cuentas_por_segundo, 1);

	/* Un segundo entero de ciclos deja el contador justo en cero, sin
	   subdesbordar todavia: son cuentas_por_segundo cuentas de 16 ciclos, o
	   sea DC_CPU_HZ exacto. */
	ESPERAR_U32(tmu_tick(DC_CPU_HZ), 0);
	ESPERAR_U32(*tcnt_de(2), 0);

	/* Y con una cuenta mas, subdesborda y recarga. El periodo son TCOR+1
	   cuentas, igual que en el chip: uno de mas en doce millones. */
	ESPERAR_U32(tmu_tick(16), 1 << 2);
	ESPERAR_U32(*tcnt_de(2), cuentas_por_segundo);
}

/* ----------------------------------------------- contador monotono ------- */

/*
	La conversion de ciclos a tiempo. Es la que permite que --traza-mem diga
	"se trabo a los 3,4 s" y que se pueda medir cuanto mas lento que una consola
	corre el emulador.
*/
static void el_reloj_convierte_ciclos_a_tiempo(void)
{
	unsigned long long guardado = reloj_total;

	arnes_reset();

	/* Un segundo de ciclos son mil milisegundos y un millon de microsegundos. */
	reloj_total = DC_CPU_HZ;
	ESPERAR_U32((DWORD) reloj_ms(), 1000);
	ESPERAR_U32((DWORD) reloj_us(), 1000000);

	/* Medio segundo. */
	reloj_total = DC_CPU_HZ / 2;
	ESPERAR_U32((DWORD) reloj_ms(), 500);

	/* Y una cantidad chica no se redondea a cero: 199 ciclos son ~1 us. */
	reloj_total = 199;
	ESPERAR_U32((DWORD) reloj_us(), 0);
	reloj_total = 200;
	ESPERAR_U32((DWORD) reloj_us(), 1);

	reloj_total = guardado;
}

/* ------------------------------------------------------- barrido -------- */

/*
	El ritmo del barrido sale de SPG_LOAD y SPG_CONTROL, no de una constante.
	main_loop() usaba 978 ciclos por linea, unas 6,5 veces mas rapido de lo que
	corresponde.
*/
static void ciclos_por_linea_segun_la_norma(void)
{
	arnes_reset();

	/* VGA por omision de reg.c: 0x20C = 524 lineas a 60 Hz. */
	ESPERAR_U32(reloj_ciclos_por_linea(0x20C, 0x100), 6345);

	/* NTSC: 59,94 Hz, un pelo mas lento. */
	ESPERAR_U32(reloj_ciclos_por_linea(0x20C, SPG_CTRL_NTSC), 6351);

	/* PAL: 50 Hz y 625 lineas. */
	ESPERAR_U32(reloj_ciclos_por_linea(624, SPG_CTRL_PAL), 6394);

	/* PAL manda sobre NTSC si vinieran los dos. */
	ESPERAR_U32(reloj_ciclos_por_linea(624, SPG_CTRL_PAL | SPG_CTRL_NTSC),
				reloj_ciclos_por_linea(624, SPG_CTRL_PAL));
}

/* La cuenta cierra: lineas x ciclos por linea x campos ~= un segundo de CPU. */
static void un_campo_dura_lo_que_debe(void)
{
	DWORD porLinea = reloj_ciclos_por_linea(0x20C, 0x100);
	DWORD porCampo = porLinea * 0x20C;

	arnes_reset();

	/* Sesenta campos tienen que dar un segundo de ciclos, con el error de
	   redondeo de la division entera. */
	ESPERAR(porCampo * 60 <= DC_CPU_HZ);
	ESPERAR(porCampo * 60 > DC_CPU_HZ - (0x20C * 60));
}

/* Sin vcount programado se cae al modo VGA en vez de dividir por cero. */
static void sin_vcount_no_divide_por_cero(void)
{
	arnes_reset();

	ESPERAR_U32(reloj_ciclos_por_linea(0, 0x100),
				reloj_ciclos_por_linea(0x20C, 0x100));
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(tpsc_da_el_divisor),
	CASO(el_reloj_es_el_que_espera_kos),
	CASO(un_canal_parado_no_cuenta),
	CASO(baja_una_cuenta_por_divisor),
	CASO(no_pierde_el_resto),
	CASO(arrancar_pone_el_resto_en_cero),
	CASO(subdesborde_recarga_de_tcor),
	CASO(la_interrupcion_depende_de_unie),
	CASO(varias_vueltas_en_una_llamada),
	CASO(los_tres_canales_son_independientes),
	CASO(parar_uno_no_toca_a_los_otros),
	CASO(el_segundo_de_kos),
	CASO(el_reloj_convierte_ciclos_a_tiempo),
	CASO(ciclos_por_linea_segun_la_norma),
	CASO(un_campo_dura_lo_que_debe),
	CASO(sin_vcount_no_divide_por_cero),
};

const dc_suite suite_tmu = DEFINIR_SUITE("tmu", casos);
