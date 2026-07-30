/****************************************************************************

	WDT - el temporizador watchdog del SH-4

	Ver wdt.h.

*****************************************************************************/

#include <string.h>

#include "wdt.h"
#include "log.h"

static BYTE  wtcnt;			/* contador de 8 bits, sube */
static BYTE  wtcsr;			/* control y estado */
static DWORD acumulado;		/* ciclos de CPU desde el ultimo incremento */

void wdt_reset(void)
{
	wtcnt     = 0;
	wtcsr     = 0;
	acumulado = 0;
}

BYTE wdt_contador(void)	{ return wtcnt; }
BYTE wdt_control(void)	{ return wtcsr; }

int wdt_es_registro(unsigned long fisica)
{
	return fisica == WDT_WTCNT || fisica == WDT_WTCSR;
}

/* ------------------------------------------------------------------------ */
/* Lectura: siempre de 8 bits                                               */
/* ------------------------------------------------------------------------ */

void wdt_leer(unsigned long fisica, void * p, size_t size)
{
	BYTE valor = (fisica == WDT_WTCNT) ? wtcnt : wtcsr;

	if (!p)
		return;

	/* El manual solo admite lectura de byte. Si piden mas, se completa con
	   ceros en vez de dejar el destino sin tocar: si no, el programa emulado
	   lee lo que hubiera en la variable del llamador. */
	memset(p, 0, size);
	*(BYTE *) p = valor;
}

/* ------------------------------------------------------------------------ */
/* Escritura: 16 bits con byte alto de clave                                */
/* ------------------------------------------------------------------------ */

void wdt_escribir(unsigned long fisica, void * p, size_t size)
{
	WORD  bruto;
	BYTE  clave, valor;

	if (!p || size < sizeof(WORD))
	{
		/* Una escritura de un byte no lleva clave, asi que el hardware la
		   descarta. Registrarlo ayuda: es sintoma de un driver que no sigue
		   el protocolo. */
		logxmsg(LOG_MEM, "wdt: escritura de %d byte(s) en %08lx, sin clave: descartada\n",
			(int) size, fisica);
		return;
	}

	memcpy(&bruto, p, sizeof(WORD));

	clave = (BYTE) (bruto >> 8);
	valor = (BYTE) (bruto & 0xFF);

	if (fisica == WDT_WTCNT)
	{
		if (clave != WDT_CLAVE_WTCNT)
		{
			logxmsg(LOG_MEM, "wdt: WTCNT con clave %02x (esperaba %02x): descartada\n",
				clave, WDT_CLAVE_WTCNT);
			return;
		}

		wtcnt = valor;

		/* Alimentar el contador tambien reinicia la fraccion de ciclo, si no
		   el primer incremento despues de escribirlo llegaria antes de tiempo. */
		acumulado = 0;
	}
	else
	{
		if (clave != WDT_CLAVE_WTCSR)
		{
			logxmsg(LOG_MEM, "wdt: WTCSR con clave %02x (esperaba %02x): descartada\n",
				clave, WDT_CLAVE_WTCSR);
			return;
		}

		/* Al arrancar el contador se empieza a contar desde cero. */
		if (!(wtcsr & WTCSR_TME) && (valor & WTCSR_TME))
			acumulado = 0;

		wtcsr = valor;
	}
}

/* ------------------------------------------------------------------------ */
/* El contador                                                              */
/* ------------------------------------------------------------------------ */

int wdt_tick(DWORD ciclos)
{
	DWORD divisor;
	int   iti = 0;

	if (!(wtcsr & WTCSR_TME))
		return 0;

	divisor = WDT_DIVISOR(wtcsr);

	acumulado += ciclos;

	while (acumulado >= divisor)
	{
		acumulado -= divisor;

		/* El contador es de 8 bits: 0xFF + 1 desborda. */
		if (wtcnt == 0xFF)
		{
			wtcnt = 0;

			if (wtcsr & WTCSR_WTIT)
			{
				/* Modo watchdog: en el chip esto reinicia la maquina. Aca se
				   registra y se sigue -- reiniciar el emulador en silencio
				   seria mucho peor para depurar que un aviso. */
				wtcsr |= WTCSR_WOVF;

				logxmsg(LOG_MEM, "wdt: desborde en modo watchdog (%s), no se reinicia\n",
					(wtcsr & WTCSR_RSTS) ? "reset manual" : "reset de encendido");
			}
			else
			{
				wtcsr |= WTCSR_IOVF;
				iti = 1;
			}
		}
		else
			wtcnt++;
	}

	return iti;
}
