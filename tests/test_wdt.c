/****************************************************************************

	Pruebas de wdt.c -- el temporizador watchdog del SH-4.

	Logica pura sobre dos registros y un contador: no hace falta CPU, memoria
	ni ventana. Lo que se prueba es el protocolo de acceso (la clave del byte
	alto), el ritmo del contador segun CKS, y las dos formas del desborde.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "suites.h"

#include "wdt.h"

/* Escribe un registro con su clave, como lo hace el driver de KOS. */
static void escribir(unsigned long reg, BYTE valor)
{
	BYTE clave = (reg == WDT_WTCNT) ? WDT_CLAVE_WTCNT : WDT_CLAVE_WTCSR;
	WORD palabra = (WORD) ((clave << 8) | valor);

	wdt_escribir(reg, &palabra, sizeof(WORD));
}

static BYTE leer(unsigned long reg)
{
	BYTE valor = 0xAA;

	wdt_leer(reg, &valor, sizeof(BYTE));

	return valor;
}

/* Arranca el contador en modo intervalo con el divisor mas chico (32). */
static void arrancar_intervalo(BYTE cuenta_inicial)
{
	wdt_reset();
	escribir(WDT_WTCNT, cuenta_inicial);
	escribir(WDT_WTCSR, WTCSR_TME);		/* WT/IT = 0, CKS = 0 -> divisor 32 */
}

/* -------------------------------------------------------------- arranque -- */

static void reset_deja_todo_parado(void)
{
	arnes_reset();
	wdt_reset();

	ESPERAR_U32(wdt_contador(), 0);
	ESPERAR_U32(wdt_control(), 0);

	/* Parado, no cuenta ni con un millon de ciclos. */
	ESPERAR_U32(wdt_tick(1000000), 0);
	ESPERAR_U32(wdt_contador(), 0);
}

static void reconoce_sus_dos_registros(void)
{
	arnes_reset();

	ESPERAR_U32(wdt_es_registro(WDT_WTCNT), 1);
	ESPERAR_U32(wdt_es_registro(WDT_WTCSR), 1);

	ESPERAR_U32(wdt_es_registro(0x00C00000), 0);	/* FRQCR */
	ESPERAR_U32(wdt_es_registro(0x00C00004), 0);	/* STBCR */
	ESPERAR_U32(wdt_es_registro(0x00C00010), 0);	/* STBCR2 */
}

/* ----------------------------------------------------------------- clave -- */

/*
	Escribir estos dos registros exige un byte alto de clave: 0x5A para WTCNT y
	0xA5 para WTCSR. Sin la clave el hardware descarta la escritura, y eso hay
	que respetarlo: si no, un driver con un bug parece funcionar en el emulador.
*/
static void sin_la_clave_correcta_no_escribe(void)
{
	arnes_reset();
	wdt_reset();

	/* Clave equivocada en WTCNT. */
	WORD mala = (0x12 << 8) | 0x77;
	wdt_escribir(WDT_WTCNT, &mala, sizeof(WORD));
	ESPERAR_U32(wdt_contador(), 0);

	/* La clave de WTCSR no sirve para WTCNT. */
	mala = (WDT_CLAVE_WTCSR << 8) | 0x77;
	wdt_escribir(WDT_WTCNT, &mala, sizeof(WORD));
	ESPERAR_U32(wdt_contador(), 0);

	/* Con la propia, si. */
	escribir(WDT_WTCNT, 0x77);
	ESPERAR_U32(wdt_contador(), 0x77);
}

static void una_escritura_de_un_byte_se_descarta(void)
{
	BYTE b = 0x55;

	arnes_reset();
	wdt_reset();

	/* Un byte no puede llevar clave, asi que el hardware la ignora. */
	wdt_escribir(WDT_WTCNT, &b, sizeof(BYTE));
	ESPERAR_U32(wdt_contador(), 0);
}

static void se_lee_de_a_un_byte(void)
{
	arnes_reset();
	wdt_reset();

	escribir(WDT_WTCNT, 0x42);
	escribir(WDT_WTCSR, WTCSR_TME | WTCSR_WTIT);

	ESPERAR_U32(leer(WDT_WTCNT), 0x42);
	ESPERAR_U32(leer(WDT_WTCSR), WTCSR_TME | WTCSR_WTIT);
}

/* -------------------------------------------------------------- el ritmo -- */

/* Con CKS = 0 el divisor es 32: una cuenta cada 32 ciclos de CPU. */
static void cuenta_una_vez_por_divisor(void)
{
	arrancar_intervalo(0);

	ESPERAR_U32(wdt_tick(31), 0);
	ESPERAR_U32(wdt_contador(), 0);		/* todavia no llego */

	ESPERAR_U32(wdt_tick(1), 0);
	ESPERAR_U32(wdt_contador(), 1);		/* 32 ciclos: una cuenta */

	ESPERAR_U32(wdt_tick(32 * 10), 0);
	ESPERAR_U32(wdt_contador(), 11);
}

/* El resto de ciclos no se pierde entre llamadas. */
static void no_pierde_la_fraccion_de_ciclo(void)
{
	int i;

	arrancar_intervalo(0);

	/* Diez llamadas de 16 ciclos son 160, o sea cinco cuentas. */
	for (i = 0; i < 10; i++)
		wdt_tick(16);

	ESPERAR_U32(wdt_contador(), 5);
}

/* CKS elige el divisor: 32 << CKS, de 32 a 4096. */
static void cks_elige_el_divisor(void)
{
	int cks;

	for (cks = 0; cks < 8; cks++)
	{
		DWORD divisor = 32u << cks;

		wdt_reset();
		escribir(WDT_WTCNT, 0);
		escribir(WDT_WTCSR, (BYTE) (WTCSR_TME | cks));

		ESPERAR_U32(WDT_DIVISOR(wdt_control()), divisor);

		wdt_tick(divisor - 1);
		ESPERAR_U32(wdt_contador(), 0);

		wdt_tick(1);
		ESPERAR_U32(wdt_contador(), 1);
	}
}

/* Alimentar el contador reinicia tambien la fraccion pendiente. */
static void alimentarlo_reinicia_la_fraccion(void)
{
	arrancar_intervalo(0);

	wdt_tick(31);						/* 31 de los 32 que hacen una cuenta */
	escribir(WDT_WTCNT, 0x80);			/* lo alimenta */

	ESPERAR_U32(wdt_tick(1), 0);
	ESPERAR_U32(wdt_contador(), 0x80);	/* no incrementa por el ciclo suelto */

	wdt_tick(31);
	ESPERAR_U32(wdt_contador(), 0x81);
}

/* ------------------------------------------------------------- desbordes -- */

/*
	En modo intervalo el desborde levanta la ITI, marca IOVF y el contador
	vuelve a empezar de cero.
*/
static void desborde_en_modo_intervalo_pide_iti(void)
{
	arrancar_intervalo(0xFF);

	/* Una cuenta mas y desborda. */
	ESPERAR_U32(wdt_tick(32), 1);
	ESPERAR_U32(wdt_contador(), 0);
	ESPERAR_U32(wdt_control() & WTCSR_IOVF, WTCSR_IOVF);
	ESPERAR_U32(wdt_control() & WTCSR_WOVF, 0);
}

/* En modo watchdog marca WOVF y NO pide interrupcion: en el chip reinicia. */
static void desborde_en_modo_watchdog_no_pide_iti(void)
{
	wdt_reset();
	escribir(WDT_WTCNT, 0xFF);
	escribir(WDT_WTCSR, WTCSR_TME | WTCSR_WTIT);

	ESPERAR_U32(wdt_tick(32), 0);
	ESPERAR_U32(wdt_contador(), 0);
	ESPERAR_U32(wdt_control() & WTCSR_WOVF, WTCSR_WOVF);
	ESPERAR_U32(wdt_control() & WTCSR_IOVF, 0);
}

/*
	El caso del ejemplo basic/watchdog: en modo watchdog con el divisor mas
	grande, el programa alimenta el contador en un bucle y nunca debe desbordar.
	Lo que ese ejemplo comprobaba -- y fallaba -- es que el contador suba.
*/
static void el_caso_del_ejemplo_de_kos(void)
{
	int i;
	BYTE maximo = 0;

	wdt_reset();
	escribir(WDT_WTCNT, 0);
	escribir(WDT_WTCSR, (BYTE) (WTCSR_TME | WTCSR_WTIT | 7));	/* divisor 4096 */

	/* Cuatro mil vueltas alimentandolo, mirando el maximo que alcanza. En el
	   hardware el tiempo pasa dentro del cuerpo del bucle; aca los ciclos los
	   damos nosotros, asi que hay que avanzarlos antes de mirar el contador. */
	for (i = 0; i < 4000; i++)
	{
		BYTE ahora;

		wdt_tick(4096 * 3);			/* tres cuentas entre alimentaciones */

		ahora = wdt_contador();

		if (ahora > maximo)
			maximo = ahora;

		escribir(WDT_WTCNT, 0);		/* lo alimenta */
	}

	/* Tres cuentas por vuelta, y se alimenta cada vez: nunca pasa de 3. */
	ESPERAR_U32(maximo, 3);

	/* El contador tiene que haber subido: es exactamente lo que el ejemplo
	   reportaba como "The WDT counter never even incremented!". */
	ESPERAR(maximo > 0);

	/* Y no debe haber desbordado nunca. */
	ESPERAR_U32(wdt_control() & WTCSR_WOVF, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(reset_deja_todo_parado),
	CASO(reconoce_sus_dos_registros),
	CASO(sin_la_clave_correcta_no_escribe),
	CASO(una_escritura_de_un_byte_se_descarta),
	CASO(se_lee_de_a_un_byte),
	CASO(cuenta_una_vez_por_divisor),
	CASO(no_pierde_la_fraccion_de_ciclo),
	CASO(cks_elige_el_divisor),
	CASO(alimentarlo_reinicia_la_fraccion),
	CASO(desborde_en_modo_intervalo_pide_iti),
	CASO(desborde_en_modo_watchdog_no_pide_iti),
	CASO(el_caso_del_ejemplo_de_kos),
};

const dc_suite suite_wdt = DEFINIR_SUITE("wdt", casos);
