/****************************************************************************

	EXCEPCIONES - la entrada a una excepcion sincrona del SH-4

	Ver excepciones.h.

*****************************************************************************/

#include <setjmp.h>
#include <string.h>

#include "excepciones.h"
#include "log.h"
#include "mmu.h"
#include "sh4emu.h"
#include "traza.h"

jmp_buf	excepcion_salto;
int		excepcion_salto_armado = 0;

DWORD	excepcion_codigo;
DWORD	excepcion_vector;
DWORD	excepcion_fpu_cause;

int		excepcion_vigilar = 0;
int		en_ranura_retardo = 0;

/*
	intc() hace lo mismo para el camino de interrupcion, con INTEVT y el vector
	0x600; esto es para todo lo demas.

	Ojo: a diferencia de intc(), no mira SR.BL. Las excepciones de reejecucion
	con BL puesto son causa de reset en el chip, y aca se registra y se entra
	igual, que es mas util para depurar que reiniciar en silencio.
*/
void excepcion_entrar(DWORD codigo, DWORD vector)
{
	SSR = SR;
	SPC = PC;
	SGR = R(15);

	*EXPEVT = codigo;

	SET_SH4_BIT(SR_BL);
	SET_SH4_BIT(SR_MD);
	SET_SH4_BIT(SR_RB);
	UpdateSR(SH4_SYSTEM_REGISTER_INTC_REWRITTEN);

	PC = VBR + vector;

	logxmsg(LOG_INTC, "excepcion: EXPEVT %03x, SPC %08x, salta a %08x\n",
		codigo, SPC, PC);

	/*
		Una excepcion general que el guest no esperaba termina casi siempre en
		un BRA a si mismo, y desde ahi no se puede saber cual fue: el manejador
		ya piso los registros. Con --traza-mem se reporta cada codigo la primera
		vez, que es lo que convierte "se colgo en 0xAC00E0B2" en "instruccion
		ilegal en tal PC".
	*/
	if (traza_activa)
	{
		static DWORD vistos[16];
		static int   n = 0;
		int          i;

		for (i = 0; i < n; i++)
			if (vistos[i] == codigo)
				return;

		if (n < 16)
			vistos[n++] = codigo;

		fprintf(stderr, "traza: excepcion EXPEVT %03lx desde PC %08lx,"
			" salta a %08lx\n",
			(unsigned long) codigo, (unsigned long) SPC, (unsigned long) PC);
	}
}

void excepcion_abortar(DWORD codigo, DWORD vector)
{
	excepcion_codigo = codigo;
	excepcion_vector = vector;

	if (excepcion_salto_armado)
	{
		excepcion_salto_armado = 0;
		longjmp(excepcion_salto, 1);
	}

	logxmsg(LOG_MEM, "excepcion: %03x fuera de una instruccion\n", codigo);
}

void excepcion_actualizar_vigilancia(void)
{
	/* SR.FD apaga la FPU entera; cualquier bit de Enable arma la trampa de
	   operacion de FPU. Ninguno de los dos esta puesto en lo que corre hoy.

	   fpu_deshabilitada se deriva aca y en ningun otro lado: es una copia de
	   SR.FD para que el despacho no extraiga un campo de bits por instruccion,
	   y si se pudiera escribir por separado las dos se irian de sincronia. */
	fpu_deshabilitada = SR_FD;

	excepcion_vigilar = mmu_activa
					 || fpu_deshabilitada
					 || (FPSCR & FPU_ENABLE_TODAS) != 0;
}

/*
	La instantanea del estado que una instruccion puede llegar a mutar antes de
	fallar.

	No alcanza con copiar core.context: los dos bancos de registros de punto
	flotante viven fuera, y el contexto solo guarda los punteros. Sin esto,
	FMOV.S @Rm+,FRn que falla por MMU dejaria FRn escrito, y sobre todo la
	trampa de FPU no podria cumplir lo que pide el manual -- que el registro
	destino no se actualice.
*/
static context_t	instantanea_contexto;
static FPR_BANK		instantanea_fr;
static FPR_BANK		instantanea_xf;

void excepcion_instantanea_tomar(void)
{
	memcpy(&instantanea_contexto, &core.context, sizeof(context_t));
	memcpy(&instantanea_fr, core.context.FR_BANK, sizeof(FPR_BANK));
	memcpy(&instantanea_xf, core.context.XF_BANK, sizeof(FPR_BANK));
}

void excepcion_instantanea_restaurar(void)
{
	/* Los punteros de banco vuelven con el contexto, asi que hay que escribir
	   el contenido despues de restaurarlo: si FRCHG los intercambio, los
	   bloques tienen que volver a su banco original, no al que quedo. */
	memcpy(&core.context, &instantanea_contexto, sizeof(context_t));
	memcpy(core.context.FR_BANK, &instantanea_fr, sizeof(FPR_BANK));
	memcpy(core.context.XF_BANK, &instantanea_xf, sizeof(FPR_BANK));
}

void excepcion_reponer(void)
{
	if (excepcion_codigo != EXC_FPU_OPERACION)
		return;

	/* Cause si, Flag no: el manual dice que en una excepcion de FPU el campo
	   Flag no se actualiza. La instantanea ya devolvio los dos a como estaban
	   antes de la instruccion, asi que basta con escribir Cause encima. */
	FPSCR = (FPSCR & ~FPU_CAUSA_TODAS) | excepcion_fpu_cause;
}
