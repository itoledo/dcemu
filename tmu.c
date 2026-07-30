/****************************************************************************

	TMU - los tres temporizadores del SH-4

	Ver tmu.h y docs/clock-plan.md, fase 1.

*****************************************************************************/

#include "tmu.h"
#include "sh4emu.h"
#include "log.h"

/* Ciclos de CPU desde el arranque. Lo incrementa main_loop(). */
unsigned long long reloj_total = 0;

/* Resto de ciclos que todavia no alcanzo para una cuenta, por canal. */
static DWORD resto[TMU_CANALES];

/* Ultimo TSTR visto, para detectar el arranque de un canal. */
static BYTE tstr_previo;

void tmu_reset(void)
{
	int n;

	for (n = 0; n < TMU_CANALES; n++)
		resto[n] = 0;

	tstr_previo = 0;
}

DWORD tmu_resto(int canal)
{
	if (canal < 0 || canal >= TMU_CANALES)
		return 0;

	return resto[canal];
}

DWORD tmu_divisor(WORD tcr)
{
	WORD tpsc = tcr & TMU_TCR_TPSC;

	switch (tpsc)
	{
		case 0:		return 16;		/* Pck/4    */
		case 1:		return 64;		/* Pck/16   */
		case 2:		return 256;		/* Pck/64   */
		case 3:		return 1024;	/* Pck/256  */
		case 4:		return 4096;	/* Pck/1024 */

		default:
		/* 101 reservado, 110 reloj del RTC, 111 reloj externo. La Dreamcast no
		   cablea ninguno de los dos ultimos. */
		logxmsg(LOG_INTC, "tmu: TPSC %d sin emular, se usa Pck/4\n", tpsc);
		return 16;
	}
}

unsigned long long reloj_us(void)
{
	/* Multiplicar primero y dividir despues: con /199 el error seria del 0,25%.
	   A 1e6 ciclos por microsegundo esto aguanta unas 25 horas emuladas antes
	   de desbordar los 64 bits, de sobra. */
	return (reloj_total * 1000000ull) / DC_CPU_HZ;
}

unsigned long long reloj_ms(void)
{
	return (reloj_total * 1000ull) / DC_CPU_HZ;
}

DWORD reloj_ciclos_por_linea(DWORD vcount, DWORD spg_control)
{
	/* Campos por segundo x100, para no meter coma flotante en el bucle. */
	DWORD campos_x100;

	/* Sin vcount programado se asume el modo VGA por omision de reg.c. */
	if (vcount == 0)
		vcount = 0x20C;

	if (spg_control & SPG_CTRL_PAL)
		campos_x100 = 5000;					/* 50 Hz    */
	else if (spg_control & SPG_CTRL_NTSC)
		campos_x100 = 5994;					/* 59,94 Hz */
	else
		campos_x100 = 6000;					/* VGA, 60 Hz */

	return (DWORD) ((DC_CPU_HZ * 100ull) / ((unsigned long long) vcount * campos_x100));
}

int tmu_tick(DWORD ciclos)
{
	/* Los registros son punteros sueltos; juntarlos aca deja el bucle legible
	   y evita repetir el cuerpo tres veces, que es como estaba antes. */
	DWORD * tcnt[TMU_CANALES] = { TCNT0, TCNT1, TCNT2 };
	DWORD * tcor[TMU_CANALES] = { TCOR0, TCOR1, TCOR2 };
	WORD  * tcr [TMU_CANALES] = { TCR0,  TCR1,  TCR2  };

	BYTE arrancados = *TSTR;
	BYTE nuevos     = (BYTE) (arrancados & ~tstr_previo);
	int  pendientes = 0;
	int  n;

	tstr_previo = arrancados;

	for (n = 0; n < TMU_CANALES; n++)
	{
		DWORD divisor;

		/* Arrancar un canal empieza a contar desde cero. */
		if (nuevos & TMU_TSTR_STR(n))
			resto[n] = 0;

		if (!(arrancados & TMU_TSTR_STR(n)))
			continue;

		divisor = tmu_divisor(*tcr[n]);

		resto[n] += ciclos;

		while (resto[n] >= divisor)
		{
			resto[n] -= divisor;

			if (*tcnt[n] == 0)
			{
				/* Subdesborde: recarga de TCOR y marca UNF. El periodo son
				   TCOR+1 cuentas, como en el chip. */
				*tcnt[n] = *tcor[n];
				*tcr[n] |= TMU_TCR_UNF;

				if (*tcr[n] & TMU_TCR_UNIE)
					pendientes |= (1 << n);
			}
			else
				(*tcnt[n])--;
		}
	}

	return pendientes;
}
