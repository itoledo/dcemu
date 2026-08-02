/****************************************************************************

	Pruebas de opcodes.c -- armado de las tablas de despacho

	El interprete no decodifica en tiempo de ejecucion: initopcodes() expande
	la tabla opcodes[] en cuatro arreglos de 65536 punteros a funcion, uno por
	combinacion de los bits PR y SZ de FPSCR, y main_loop() indexa directo con
	la palabra de instruccion. Todo lo que salga mal en esa expansion se
	convierte en una instruccion que hace otra cosa, en silencio.

	Esta suite verifica la expansion en si:

	  - Ninguna de las 262144 entradas queda en NULL.
	  - Ningun patron de bits queda cubierto por dos filas a la vez en el mismo
		modo. Es lo mismo que revisa checkopcodes(), pero como asercion en vez
		de un log en logs/repetidos.txt.
	  - Cada fila de opcodes[] es alcanzable: ninguna quedo tapada por otra
		posterior.
	  - Los campos n y m no cambian el despacho.
	  - PR y SZ si lo cambian, y UpdateFPSCR() apunta a la tabla que
		corresponde.
	  - Las instrucciones sin implementar caen en NOIMP, y NOIMP avanza PC en
		vez de colgar.

	Cierra con el opcode 0xFFFF, que no existe en el SH-4: es la trampa que
	main() planta en los vectores de syscall de la BIOS para emular el GDROM.

*****************************************************************************/

#include "arnes.h"
#include "dcopcodes.h"
#include "opcodes.h"
#include "suites.h"

/* Mismos valores que en opcodes.c, donde estan definidos localmente. */
#define REQ_PR_0		1
#define REQ_SZ_0		2
#define REQ_PR_0_SZ_1	3
#define REQ_PR_1_SZ_0	4

#define BIT_SZ	0x00100000u
#define BIT_PR	0x00080000u

static oplist_t * tabla(int pr, int sz)
{
	if (pr)
		return sz ? oplist_pr1_sz1 : oplist_pr1_sz0;

	return sz ? oplist_pr0_sz1 : oplist_pr0_sz0;
}

/* A que tablas va una fila segun su restriccion. Espeja el switch de
   initopcodes(). */
static int fila_aplica(long restriccion, int pr, int sz)
{
	switch (restriccion)
	{
		case REQ_PR_0:			return pr == 0;
		case REQ_SZ_0:			return sz == 0;
		case REQ_PR_0_SZ_1:		return pr == 0 && sz == 1;
		case REQ_PR_1_SZ_0:		return pr == 1 && sz == 0;
		default:				return 1;
	}
}

/* --------------------------------------------------- integridad de las tablas */

static void ninguna_entrada_queda_en_null(void)
{
	int pr, sz, huecos = 0;
	long i;

	arnes_reset();

	for (pr = 0; pr < 2; pr++)
		for (sz = 0; sz < 2; sz++)
		{
			oplist_t * t = tabla(pr, sz);

			for (i = 0; i < 65536; i++)
				if (OP_HANDLER(t, i) == NULL)
					huecos++;
		}

	ESPERAR_U32(huecos, 0);
}

static void ningun_patron_lo_reclaman_dos_filas(void)
{
	int pr, sz;
	long i, f;
	int ambiguos = 0;

	arnes_reset();

	for (pr = 0; pr < 2; pr++)
		for (sz = 0; sz < 2; sz++)
			for (i = 0; i < 65536; i++)
			{
				int cuantas = 0;

				for (f = 0; opcodes[f].opdesc; f++)
					if ((i & opcodes[f].mask) == opcodes[f].op &&
						fila_aplica(opcodes[f].restriccion, pr, sz))
						cuantas++;

				if (cuantas > 1)
				{
					if (ambiguos < 5)
						dc_anotar(__FILE__, __LINE__,
								  "el opcode 0x%04X lo reclaman %d filas con PR=%d SZ=%d",
								  (unsigned int) i, cuantas, pr, sz);
					ambiguos++;
				}
			}

	ESPERAR_U32(ambiguos, 0);
}

static void toda_fila_es_alcanzable(void)
{
	int pr, sz;
	long f;
	int tapadas = 0;

	arnes_reset();

	for (f = 0; opcodes[f].opdesc; f++)
		for (pr = 0; pr < 2; pr++)
			for (sz = 0; sz < 2; sz++)
			{
				if (!fila_aplica(opcodes[f].restriccion, pr, sz))
					continue;

				if (OP_HANDLER(tabla(pr, sz), opcodes[f].op) != opcodes[f].funcion)
				{
					if (tapadas < 5)
						dc_anotar(__FILE__, __LINE__,
								  "la fila %ld (%s, 0x%04X) no quedo en la tabla PR=%d SZ=%d",
								  f, opcodes[f].opdesc,
								  (unsigned int) opcodes[f].op, pr, sz);
					tapadas++;
				}
			}

	ESPERAR_U32(tapadas, 0);
}

/* ------------------------------------------------------ campos de operando */

static void los_campos_n_y_m_no_cambian_el_handler(void)
{
	static const WORD bases[] =
	{
		0x300C,		/* ADD Rm, Rn */
		0x2009,		/* AND Rm, Rn */
		0x6003,		/* MOV Rm, Rn */
		0x2002,		/* MOV.L Rm, @Rn */
		0x000F,		/* MAC.L @Rm+, @Rn+ */
	};
	int b, n, m;
	int distintos = 0;

	arnes_reset();

	for (b = 0; b < (int) (sizeof(bases) / sizeof(bases[0])); b++)
	{
		opcode_f * esperado = OP_HANDLER(oplist_pr0_sz0, bases[b]);

		for (n = 0; n < 16; n++)
			for (m = 0; m < 16; m++)
				if (OP_HANDLER(oplist_pr0_sz0, instr_nm(bases[b], n, m)) != esperado)
					distintos++;
	}

	ESPERAR_U32(distintos, 0);
}

static void el_inmediato_no_cambia_el_handler(void)
{
	int i;
	int distintos = 0;
	opcode_f * esperado = OP_HANDLER(oplist_pr0_sz0, 0xE000);	/* MOV #imm, Rn */

	arnes_reset();

	for (i = 0; i < 256; i++)
		if (OP_HANDLER(oplist_pr0_sz0, instr_ni(0xE000, 5, i)) != esperado)
			distintos++;

	ESPERAR_U32(distintos, 0);
}

/* ------------------------------------------------------------- PR y SZ */

static void pr_y_sz_cambian_el_significado_de_un_patron(void)
{
	arnes_reset();

	/* 0xF00C es FMOV FRm,FRn con SZ=0 y FMOV DRm,DRn con SZ=1. */
	ESPERAR(OP_HANDLER(oplist_pr0_sz0, 0xF00C) != OP_HANDLER(oplist_pr0_sz1, 0xF00C));

	/* 0xF000 es FADD simple con PR=0 y FADD doble con PR=1. */
	ESPERAR(OP_HANDLER(oplist_pr0_sz0, 0xF000) != OP_HANDLER(oplist_pr1_sz0, 0xF000));

	/* Los enteros no dependen del modo. */
	ESPERAR(OP_HANDLER(oplist_pr0_sz0, 0x300C) == OP_HANDLER(oplist_pr1_sz1, 0x300C));
	ESPERAR(OP_HANDLER(oplist_pr0_sz0, 0x300C) == OP_HANDLER(oplist_pr0_sz1, 0x300C));
}

static void updatefpscr_apunta_a_la_tabla_correcta(void)
{
	arnes_reset();

	UpdateFPSCR(0);
	ESPERAR(oplist == oplist_pr0_sz0);

	UpdateFPSCR(BIT_SZ);
	ESPERAR(oplist == oplist_pr0_sz1);

	UpdateFPSCR(BIT_PR);
	ESPERAR(oplist == oplist_pr1_sz0);

	UpdateFPSCR(BIT_PR | BIT_SZ);
	ESPERAR(oplist == oplist_pr1_sz1);
}

/* Las instrucciones de doble precision no existen con PR=0: el mismo patron
   tiene que resolver a la version simple. */
static void las_instrucciones_dobles_solo_existen_con_pr1(void)
{
	arnes_reset();

	/* FSQRT DR0 (0xF06D) con PR=1 no es el mismo handler que FSQRT FR0. */
	ESPERAR(OP_HANDLER(oplist_pr1_sz0, 0xF06D) != OP_HANDLER(oplist_pr0_sz0, 0xF06D));

	/* FTRC DRm,FPUL con PR=1 tampoco. */
	ESPERAR(OP_HANDLER(oplist_pr1_sz0, 0xF03D) != OP_HANDLER(oplist_pr0_sz0, 0xF03D));
}

/* ------------------------------------------------------------------ NOIMP */

/* NOIMP es el relleno de la tabla: cubre los patrones de bits que no
   corresponden a ninguna instruccion del SH-4. Que ninguna fila con nombre
   apunte ahi es la forma de decir "estan todas implementadas". */
static void ninguna_instruccion_queda_sin_implementar(void)
{
	long f;
	int faltantes = 0;

	arnes_reset();

	for (f = 0; opcodes[f].opdesc; f++)
	{
		if (opcodes[f].funcion != NOIMP)
			continue;

		/* La fila comodin es la unica que corresponde que apunte a NOIMP. */
		if (opcodes[f].op == 0x0000 && opcodes[f].mask == 0xFFFF)
			continue;

		dc_anotar(__FILE__, __LINE__, "%s (0x%04X, fila %ld) sigue sin implementar",
				  opcodes[f].opdesc, (unsigned int) opcodes[f].op, f);
		faltantes++;
	}

	ESPERAR_U32(faltantes, 0);
}

/* Patrones que no son instrucciones del SH-4 y por eso no tienen fila propia:
   tienen que resolver al relleno. */
static void los_encodings_invalidos_caen_en_noimp(void)
{
	static const WORD invalidos[] =
	{
		0x0000,		/* 0000nnnn 00000000 no existe */
		0x0001,
		0x0010,
		0x0011,
		0x00E0,
		0x4014,		/* 0100nnnn 00010100: el hueco entre STC.L GBR y CMP/PL */
	};
	int i;

	arnes_reset();

	for (i = 0; i < (int) (sizeof(invalidos) / sizeof(invalidos[0])); i++)
		if (OP_HANDLER(oplist_pr0_sz0, invalidos[i]) != NOIMP)
			dc_anotar(__FILE__, __LINE__, "0x%04X deberia caer en NOIMP",
					  (unsigned int) invalidos[i]);
}

static void noimp_avanza_pc_sin_colgar(void)
{
	arnes_reset();

	ejecutar(0x0001);					/* no es una instruccion valida */

	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------- opcode 0xFFFF (BIOS) */

/* 0xFFFF no es una instruccion del SH-4. main() escribe esa palabra en los
   vectores de syscall de la BIOS y el emulador la usa para atender el GDROM
   desde el host. El stub del GDROM lleva el ilegal en el offset 0 -- sin RTS:
   hack_gdrom() fija el PC del retorno el mismo, de ordinario PR (asi el
   MAINLOOP puede desviar el retorno al callback PIO del guest; ver main.c).
   Aqui se ejecuta con una peticion de lectura de sectores; iso_read_sector()
   lo atiende el doble de tests/dobles.c, que devuelve un patron
   reproducible. */
static void el_hack_de_la_bios_lee_sectores(void)
{
	DWORD destino = PRUEBA_DATOS + 0x1000;
	DWORD parametros = PRUEBA_DATOS;
	int i;
	int distintos = 0;

	arnes_reset();

	escribir_l(parametros + 0, 5);			/* primer sector */
	escribir_l(parametros + 4, 1);			/* cantidad */
	escribir_l(parametros + 8, destino);	/* donde dejarlos */

	R(4) = 16;								/* GDROM: leer sectores */
	R(5) = parametros;
	R(6) = 0;
	R(7) = 0;								/* GDROM_SEND_COMMAND */

	PR = 0x8C123456;						/* el llamador del syscall */
	PC = HACK_BASE + HACK_GDROM;
	ejecutar(0xFFFF);

	for (i = 0; i < 2048; i++)
		if (leer_b(destino + i) != (BYTE) ((5 * 7 + i) & 0xFF))
			distintos++;

	ESPERAR_U32(distintos, 0);
	ESPERAR_U32(R(0), 0x6969);
	ESPERAR_U32(PC, 0x8C123456);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(ninguna_entrada_queda_en_null),
	CASO(ningun_patron_lo_reclaman_dos_filas),
	CASO(toda_fila_es_alcanzable),
	CASO(los_campos_n_y_m_no_cambian_el_handler),
	CASO(el_inmediato_no_cambia_el_handler),
	CASO(pr_y_sz_cambian_el_significado_de_un_patron),
	CASO(updatefpscr_apunta_a_la_tabla_correcta),
	CASO(las_instrucciones_dobles_solo_existen_con_pr1),
	CASO(ninguna_instruccion_queda_sin_implementar),
	CASO(los_encodings_invalidos_caen_en_noimp),
	CASO(noimp_avanza_pc_sin_colgar),
	CASO(el_hack_de_la_bios_lee_sectores),
};

const dc_suite suite_decodificacion = DEFINIR_SUITE("decodificacion", casos);
