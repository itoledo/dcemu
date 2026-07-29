/****************************************************************************

	ARNES - implementacion. Ver arnes.h.

*****************************************************************************/

#include <string.h>

#include "main.h"
#include "opcodes.h"
#include "sh4emu.h"

#include "arnes.h"
#include "dctest.h"

/* Los bancos de registros float los reserva initCpuSubSystem() en sh4emu.c. */
extern FPR_BANK * BANK0;
extern FPR_BANK * BANK1;

void memoria_prueba_iniciar(void);

/* Valor de FPSCR al reset segun el manual del SH-4: DN=1 (desnormalizados a
   cero), RM=01 (truncar hacia cero), PR=0, SZ=0, FR=0.

   Ojo: reset() en sh4emu.c pone 0x0004001, que es 0x4001 -- le falta un cero y
   en vez de DN prende un bit de Cause. Ver tests/README.md. */
#define FPSCR_PRUEBA	0x00040001u

void arnes_iniciar(void)
{
	memoria_prueba_iniciar();

	initopcodes();			/* arma las cuatro tablas de 65536 punteros */
	initCpuSubSystem();		/* reserva los bancos float y engancha core.execute */
}

void arnes_reset(void)
{
	int i;

	/* Registros generales, incluidos los 8 bancarios (16..23). */
	for (i = 0; i < 24; i++)
		R(i) = 0;

	PC		= PRUEBA_PC;
	PR		= 0;
	MACH	= 0;
	MACL	= 0;
	GBR		= 0;
	VBR		= 0;
	SSR		= 0;
	SPC		= 0;
	SGR		= 0;
	DBR		= 0;
	FPUL	= 0;

	/* SR en cero: T, S, Q, M, MD, RB, BL y la mascara de interrupciones
	   apagados. Es un punto de partida mas comodo que el 0x700000F0 del reset
	   real, y ningun handler depende de los bits reservados. */
	SR = 0;

	core.context.cycles = 0;
	core.context.cycles_v_int = 0;
	core.context.cycles_v_int_total = 0;

	delayslot = 0;
	NEXTPC = 0;

	/* Bancos float en un estado conocido y sin intercambiar. */
	core.context.FR_BANK = BANK0;
	core.context.XF_BANK = BANK1;
	memset(BANK0, 0, sizeof(FPR_BANK));
	memset(BANK1, 0, sizeof(FPR_BANK));

	FPSCR = 0;
	UpdateFPSCR(FPSCR_PRUEBA);

	/* Memoria: solo la ventana que usan las pruebas. Limpiar los 16 MB en
	   cada caso costaria mas que correr la suite entera. */
	memset(get_memory_pointer(PRUEBA_VENTANA_INICIO), 0,
		   PRUEBA_VENTANA_FIN - PRUEBA_VENTANA_INICIO);

	arnes_registros_en_cero();
	dobles_reset();

	dc_empezar_caso();
}

void poner_instr(DWORD dir, WORD instr)
{
	*(WORD *) get_memory_pointer(dir) = instr;
}

/* Handlers que llego a ejecutar la corrida, para la suite de cobertura. Se
   acumulan durante toda la sesion: arnes_reset() no los borra. */
#define MAX_HANDLERS_VISTOS	512

static opcode_f * vistos[MAX_HANDLERS_VISTOS];
static int cantidad_vistos;

static void anotar_handler(opcode_f * funcion)
{
	int i;

	for (i = 0; i < cantidad_vistos; i++)
		if (vistos[i] == funcion)
			return;

	if (cantidad_vistos < MAX_HANDLERS_VISTOS)
		vistos[cantidad_vistos++] = funcion;
}

int arnes_handler_ejecutado(opcode_f * funcion)
{
	int i;

	for (i = 0; i < cantidad_vistos; i++)
		if (vistos[i] == funcion)
			return 1;

	return 0;
}

void ejecutar(WORD instr)
{
	poner_instr(PC, instr);

	anotar_handler(oplist[instr]);

	/* Igual que main_loop(): el despacho lee la palabra desde memoria. */
	core.execute(*(WORD *) get_memory_pointer(PC));
}

/* ------------------------------------------------------------------------ */
/* Acceso directo a la memoria de prueba                                    */
/* ------------------------------------------------------------------------ */

void escribir_b(DWORD dir, BYTE valor)	{ *(BYTE *)  get_memory_pointer(dir) = valor; }
void escribir_w(DWORD dir, WORD valor)	{ *(WORD *)  get_memory_pointer(dir) = valor; }
void escribir_l(DWORD dir, DWORD valor)	{ *(DWORD *) get_memory_pointer(dir) = valor; }

void escribir_f(DWORD dir, float valor)
{
	memcpy(get_memory_pointer(dir), &valor, sizeof(float));
}

BYTE  leer_b(DWORD dir)	{ return *(BYTE *)  get_memory_pointer(dir); }
WORD  leer_w(DWORD dir)	{ return *(WORD *)  get_memory_pointer(dir); }
DWORD leer_l(DWORD dir)	{ return *(DWORD *) get_memory_pointer(dir); }

float leer_f(DWORD dir)
{
	float valor;

	memcpy(&valor, get_memory_pointer(dir), sizeof(float));

	return valor;
}

/* ------------------------------------------------------------------------ */
/* Constructores de instrucciones                                           */
/* ------------------------------------------------------------------------ */

WORD instr_nm(WORD base, int n, int m)
{
	return (WORD) (base | ((n & 0x0F) << 8) | ((m & 0x0F) << 4));
}

WORD instr_n(WORD base, int n)
{
	return (WORD) (base | ((n & 0x0F) << 8));
}

WORD instr_ni(WORD base, int n, int imm)
{
	return (WORD) (base | ((n & 0x0F) << 8) | (imm & 0xFF));
}

WORD instr_i(WORD base, int imm)
{
	return (WORD) (base | (imm & 0xFF));
}

WORD instr_nd(WORD base, int n, int d)
{
	return (WORD) (base | ((n & 0x0F) << 4) | (d & 0x0F));
}

WORD instr_nmd(WORD base, int n, int m, int d)
{
	return (WORD) (base | ((n & 0x0F) << 8) | ((m & 0x0F) << 4) | (d & 0x0F));
}
