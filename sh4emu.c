#include "sh4emu.h"
#include <stdio.h>
#include "options.h"
#include "branch.h"
#include "opcodes.h"
#include "excepciones.h"
#include "floatsimple.h"
#include "intc.h"

/*
	El modo de redondeo de la FPU. RM = 01 -- truncar hacia cero -- es el valor
	de reset del SH-4 y el que KOS deja puesto, o sea que **es el modo en que
	corre todo lo que se ejecuta en la consola**; dcemu lo ignoraba y redondeaba
	siempre al mas cercano, que es lo que hace el anfitrion por omision. La
	diferencia es de un ulp por operacion, invisible de a una y acumulable.

	Se cambia el entorno de punto flotante del anfitrion, y solo cuando el guest
	escribe FPSCR: hacerlo por instruccion seria recargar MXCSR en cada
	operacion, que es lo mas caro que puede hacer un interprete.

	Ojo con el alcance: el modo es del proceso, asi que mientras el guest esta
	en RM=01 las cuentas de punto flotante del propio emulador -- las
	coordenadas del TA en graficos.c -- tambien truncan. La diferencia es del
	ultimo bit y nada depende de ella.

	_controlfp esta en MSVC y en MinGW (viene de msvcrt); fuera de Windows va
	<fenv.h>, que es C99 y en Linux sobra.
*/
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <float.h>
#define DC_REDONDEO_CERO()		((void) _controlfp(_RC_CHOP, _MCW_RC))
#define DC_REDONDEO_CERCANO()	((void) _controlfp(_RC_NEAR, _MCW_RC))
#else
#include <fenv.h>
#define DC_REDONDEO_CERO()		((void) fesetround(FE_TOWARDZERO))
#define DC_REDONDEO_CERCANO()	((void) fesetround(FE_TONEAREST))
#endif

/*
	RM tiene dos bits pero el manual solo define 00 (al mas cercano) y 01 (hacia
	cero); 10 y 11 son reservados y se comportan como el mas cercano.

	Lo que se compara es el modo **puesto en el anfitrion**, no el FPSCR
	anterior: quien escriba FPSCR directamente -- reset(), el arnes de las
	pruebas -- no deja los dos de acuerdo, y entonces un valor "igual al de
	antes" dejaria el anfitrion como estaba.
*/
void fpu_aplicar_redondeo(void)
{
	static int puesto = -1;
	int quiero = (FPSCR_RM == 1);

	if (quiero == puesto)
		return;

	puesto = quiero;

	if (quiero)
		DC_REDONDEO_CERO();
	else
		DC_REDONDEO_CERCANO();
}

sh4_cpu core;

/* Registros de la MMU. Solo PTEL estaba enlazado; el resto se agrego en la
   fase 1 de docs/mmu-plan.md. Todavia nadie los obedece: son respaldo, para
   que escribirlos y volver a leerlos de el mismo valor. */
DWORD *		PTEH;		// Page Table Entry High register
DWORD *		PTEL;		// Page Table Entry Low register
DWORD *		TTB;		// Translation Table Base register
DWORD *		TEA;		// TLB Exception Address register
DWORD *		MMUCR;		// MMU Control Register
DWORD *		PTEA;		// Page Table Entry Assistance register

DWORD *		CCR;		// Cache Control Register
DWORD *		INTEVT;		// Interrupt Event Register
DWORD *		EXPEVT;		// Exception Event Register
DWORD *		TRA;		// TRAPA exception register
DWORD *		QACR0;		// Queue Address Control Register 0
DWORD *		QACR1;		// Queue Address Control Register 0

DWORD *		PCTRA;		// Port Control Register A
WORD *		PDTRA;			// Port Data Register A
DWORD *		PCTRB;		// Port Control Register B
WORD *		PDTRB;			// Port data Register B

/*** DMA ***/
DWORD	*	SAR0;		// DMA source address register 0
DWORD	*	DAR0;		// DMA destination address register 0
DWORD	*	DMATCR0;	// DMA transfer count register 0
DWORD	*	CHCR0;	// DMA channel control register 0
DWORD	*	SAR1;		// DMA source address register 0
DWORD	*	DAR1;		// DMA destination address register 0
DWORD	*	DMATCR1;	// DMA transfer count register 0
DWORD	*	CHCR1;	// DMA channel control register 0
DWORD	*	SAR2;		// DMA source address register 0
DWORD	*	DAR2;		// DMA destination address register 0
DWORD	*	DMATCR2;	// DMA transfer count register 0
DWORD	*	CHCR2;	// DMA channel control register 0
DWORD	*	SAR3;		// DMA source address register 0
DWORD	*	DAR3;		// DMA destination address register 0
DWORD	*	DMATCR3;	// DMA transfer count register 0
DWORD	*	CHCR3;	// DMA channel control register 0
DWORD	*	DMAOR;	// DMA operation register
/*** FIN DMA ***/

WORD *		ICR;			// Interrupt Control Register
WORD *		IPRA;			// Interrupt Priority Register A
WORD *		IPRB;			// Interrupt Priority Register B
WORD *		IPRC;			// Interrupt Priority Register C

/*** TMU ***/
BYTE    *	TOCR;
BYTE    *	TSTR;
DWORD   *	TCOR0;
DWORD   *	TCNT0;
WORD    *	TCR0;
DWORD   *	TCOR1;
DWORD   *	TCNT1;
WORD    *	TCR1;
DWORD   *	TCOR2;
DWORD   *	TCNT2;
WORD    *	TCR2;
DWORD   *	TCPR2;
/*** ***/

WORD *		SCSMR2;		// Serial Mode Register
BYTE *		SCBRR2;		// Bit Rate Register
WORD *		SCSCR2;		// Serial Control Register
BYTE *		SCFTDR2;	// Transmit FIFO Data Register
WORD *		SCFSR2;		// Serial Status Register
BYTE *		SCFRDR2;	// Receive FIFO Data Register
WORD *		SCFCR2;		// FIFO Control Register
WORD *		SCFDR2;		// FIFO Data Count Register
WORD *		SCSPTR2;		// Serial Port Register
WORD *		SCLSR2;		// Line Status Register



unsigned long delayslot = 0;
unsigned long NEXTPC = 0;


// float register banks
FPR_BANK * BANK0; // 1st register bank
FPR_BANK * BANK1; // 2nd register bank

void reset()
{
 FPSCR = 0x00040001; // Floating Point Status/Control Register: DN=1, RM=01
					// (antes decia 0x0004001, un cero de menos: en vez de DN
					// dejaba prendido un bit de Cause)
 fpu_aplicar_redondeo();	// RM=01: el anfitrion tiene que truncar tambien
 PC =  0xA0000000;   // Program Counter
 MACH = 0; // Multiply-And-Accumulate register High
 MACL = 0; // Multiply-And-Accumulate register Low
 FPUL = 0;
 PR = 0;   // Procedure Register
 SR = 0x700000F0; // Status Register
 /* RB vale 1 en el valor de reset, y registers[0..7] es el banco que SR nombra:
    el banco puesto arranca siendo ese. Sin esto la primera llamada a UpdateSR()
    veria una diferencia que no existe y cambiaria de banco sin motivo. */
 core.context.banco_activo = SR_RB;
 SSR = 0; // Saved Status Register
 SPC = 0; // Saved Program Counter
 GBR = 0; // Global Base Register
 VBR = 0; // Vector Base Register
 SGR = 0; // Saved General Register 15
 DBR = 0; // Debug Base Register
 // clearing up the floating point register banks
 memset(MTRX,0,64);
 memset(XFMTRX,0,64);
 // cleaning up the base doubles
 core.context.cycles = 0;
}

void Close()
{

}

/*
	SR.FD apagada la FPU. Se mantiene aparte de SR para que el despacho no
	tenga que extraer un campo de bits en cada instruccion: lo escribe
	UpdateSR() y vale cero en todo lo que corre hoy.
*/
int fpu_deshabilitada = 0;

/*
	Que instrucciones cuentan como "de FPU" para SR.FD. Son todas las de punto
	flotante mas las cuatro transferencias de FPUL y FPSCR, que el manual del
	SH-4 lista junto con ellas porque tocan registros de la FPU aunque el
	opcode no empiece con 1111.
*/
int es_instruccion_fpu(WORD instr)
{
	if ((instr & 0xF000) == 0xF000)
		return 1;

	switch (instr & 0xF0FF)
	{
		case 0x405A:	/* LDS   Rm, FPUL      */
		case 0x406A:	/* LDS   Rm, FPSCR     */
		case 0x4056:	/* LDS.L @Rm+, FPUL    */
		case 0x4066:	/* LDS.L @Rm+, FPSCR   */
		case 0x005A:	/* STS   FPUL, Rn      */
		case 0x006A:	/* STS   FPSCR, Rn     */
		case 0x4052:	/* STS.L FPUL, @-Rn    */
		case 0x4062:	/* STS.L FPSCR, @-Rn   */
			return 1;
	}

	return 0;
}

void run(WORD arg)
{
	/* El chequeo va aca y no en main_loop() porque asi cubre tambien las
	   ranuras de retardo, que branch.c ejecuta con un core.execute() anidado.
	   La distincion entre 0x800 y 0x820 es justamente esa. */
	if (fpu_deshabilitada && es_instruccion_fpu(arg))
		excepcion_abortar(en_ranura_retardo ? EXC_FPU_RANURA : EXC_FPU_GENERAL,
						  EXC_VEC_GENERAL);

#ifdef old_oplist
	(opcodes[oplist[arg]].funcion) (arg);
#else
	oplist[arg]  (arg);
#endif
}

#ifdef _fast_interpreter_
void runCache (WORD arg)
{
#ifdef old_oplist
	(opcodes[oplist[arg]].funcion) (arg);
#else
	oplist[arg]  (arg);
#endif
}
#endif

void initCpuSubSystem()
{
  BANK0  =(FPR_BANK *) malloc (sizeof(FPR_BANK));
  BANK1  =(FPR_BANK *) malloc (sizeof(FPR_BANK));
  // setting up the default pointers
 core.context.FR_BANK = BANK0;
 core.context.XF_BANK = BANK1;
 // setting up the function pointers
 core.resetCpu = &reset;
 core.closeCpu = &Close;
 core.execute = &run;
 #ifdef _fast_interpreter_
//  core.executeBlock = &runCache;
 #endif
 core.resetCpu();
}

void UpdateFPSCR(DWORD newFPSCR)
{
	// aqu� hacemos el swap de los registros float
	// y revisamos si hay cambio en FPSCR_PR o FPSCR_SZ

/*	if ((FPSCR ^ newFPSCR) & (FPSCR_PR | FPSCR_SZ)) // alg�n cambio?
	{ */
		// determinemos qu� cambio se hizo
		// nos fijamos en newFPSCR
	int f;
	f = FPSCR_FR;
	FPSCR = newFPSCR;

	fpu_aplicar_redondeo();

	if(FPSCR_PR_BIT)
		{
			if(FPSCR_SZ_BIT)
				oplist = oplist_pr1_sz1;
			else oplist = oplist_pr1_sz0;
		}
	else
		{
			if(!FPSCR_SZ_BIT)
				oplist = oplist_pr0_sz0;
			else
				oplist = oplist_pr0_sz1;
		}
	if(f != FPSCR_FR)
		SWITCH_FLOAT_REG_BANKS();

	// Cualquier bit de Enable arma la trampa de operacion de FPU, y para
	// abortar la instruccion hace falta la instantanea de main_loop().
	excepcion_actualizar_vigilancia();
}
/*
	if (FPSCR & FPSCR_FR)
	{
 		if (newFPSCR & FPSCR_FR)
 			;
		else
			SWITCH_FLOAT_REG_BANKS();;
		FPSCR = newFPSCR;
		return;
	}			

	if (newFPSCR & FPSCR_FR)
		SWITCH_FLOAT_REG_BANKS();
	FPSCR = newFPSCR;
*/



/*
	swap_registers() es lo unico que mueve R0-R7 de banco, asi que
	core.context.banco_activo es la verdad sobre el banco **puesto**; SR.RB dice
	cual deberia estar. Las dos cosas se separan, y de ahi salia un error: la
	entrada a una excepcion pone RB=1 sin mirar si ya valia 1. Ver UpdateSR().
*/
void swap_registers(void)
{
	DWORD tempr[8];

	memcpy(&tempr[0], &core.context.registers[16], sizeof(DWORD)*8);
	memcpy(&core.context.registers[16], &core.context.registers[0], sizeof(DWORD)*8);
	memcpy(&core.context.registers[0], &tempr[0], sizeof(DWORD)*8);

	core.context.banco_activo ^= 1;
}

/*
	Lo que queda de un valor cualquiera al escribirlo en SR. Dos reglas, y las
	dos las fijan las pruebas de SingleStepTests/sh4 sobre las tres
	instrucciones que escriben SR desde un dato -- LDC Rm,SR, LDC.L @Rm+,SR y
	RTE -- en sus 1500 casos:

	  - **Solo existen los bits de 0x700083F3.** Es la mascara que el manual del
		SH-4 pone en la propia definicion de LDC Rm,SR; el resto son reservados
		y se leen en cero.
	  - **Con MD en cero, RB tambien queda en cero.** RB solo tiene sentido en
		modo privilegiado: en modo usuario R0-R7 son siempre los del banco 0.
		Escribir SR con MD=0 y RB=1 no deja el bit puesto ni cambia de banco.

	La mascara estaba en LDC Rm,SR y en ningun otro lado, asi que RTE copiaba
	SSR entero -- y SSR se puede escribir con LDC Rm,SSR, que no filtra nada.
*/
DWORD sr_normalizar(DWORD valor)
{
	valor &= 0x700083F3;

	if (!(valor & (1u << 30)))		/* MD */
		valor &= ~(1u << 29);		/* RB */

	return valor;
}

void UpdateSR(DWORD new)
{
	/*
		Dos entradas: o el llamador ya escribio SR el mismo -- la entrada a una
		excepcion, a una interrupcion y TRAPA, que ponen MD/RB/BL a mano y
		avisan con el centinela -- o trae el valor nuevo y lo escribimos aca.

		En los dos casos la decision es la misma y es sobre el banco **puesto**,
		no sobre el que habia en SR: mover R0-R7 solo si el banco que SR pide no
		es el que esta.

		El centinela intercambiaba **siempre**, y ahi estaba el error. Con RB ya
		en 1 -- una excepcion tomada desde dentro de otra, que es lo que hace un
		manejador que baja BL para permitir anidamiento -- la entrada ponia RB=1
		otra vez e intercambiaba igual: el manejador veia el banco 0 como si
		fuera el suyo, y el codigo interrumpido volvia del RTE con R0-R7 del
		otro banco. El RTE no lo deshacia, porque ese camino si compara y
		SSR.RB == SR.RB. Virtua Tennis se caia justo ahi: perdia el indice de
		una tabla de callbacks a traves de una interrupcion anidada, llamaba por
		un puntero nulo y terminaba ejecutando el ROM desde la direccion 0.

		Con MD en cero, RB tambien: lo aplica sr_normalizar(). Antes no se
		miraba, y la nota decia que en el chip con MD=0 se accede siempre al
		banco 0 "aca no, igual que antes"; las pruebas de SingleStepTests lo
		fijaron sobre 1500 casos de las tres instrucciones que escriben SR.

		**El centinela ya no viaja por el mismo parametro.** Valia 0xFFFFFFFF, o
		sea exactamente lo que deja un LDC Rm,SR con Rm en todos unos: esa
		escritura se tomaba por "el llamador ya escribio SR" y **SR quedaba sin
		tocar**. Ahora la entrada del emulador es UpdateSR_ya_escrito().
	*/
	SR = sr_normalizar(new);

	if ((int) SR_RB != core.context.banco_activo)
		swap_registers();

	// SR.FD apaga la FPU entera: si esta puesto, main_loop() tiene que armar
	// el salto para poder abortar la instruccion que la toque. La funcion
	// tambien deriva fpu_deshabilitada de SR.FD.
	excepcion_actualizar_vigilancia();
}

/*
	La otra entrada: la entrada a una excepcion, la entrada a una interrupcion y
	TRAPA escriben MD, RB y BL a mano sobre el SR que ya estaba y despues avisan
	por aqui. No hay valor que normalizar -- los tres bits que ponen son
	validos -- y lo unico que falta es acomodar el banco de registros.
*/
void UpdateSR_ya_escrito(void)
{
	if ((int) SR_RB != core.context.banco_activo)
		swap_registers();

	excepcion_actualizar_vigilancia();
}

#ifdef PC_FUNCTIONS
void PC_f_normal(void)
{
	PC += 2;
	str_PC += 2;
}

void PC_f_delayslot2(void)
{
	if (delayslot == 0)
	{
		logmsg("PC_f_delayslot2: saltando a delayslot 0? PC=%x\n", PC);
		dump_registers();
		fclose(logfp);
		abort();
	}

	PC = delayslot;
	PC_func = PC_f_normal;

	/*	if ((PC & 0xFF000000) == 0xAC000000)
	{
		str_PC = &memoria[PC - 0xAC000000];
	}
	else
	if ((PC & 0xE0000000) == 0xA0000000) // P2
	{
//		fprintf(fp, "Saltando a P2\r\n");
		str_PC = &memoria[PC - 0xA0000000 + mem_offset];
	}
	else
	if ((PC & 0xE0000000) == 0x000000000) // P0
	{
//		fprintf(fp, "Saltando a P0\r\n");
		str_PC = &memoria[PC + mem_offset];
	}
	else */

/*	DWORD offset = PC % 0x20000000;
	if (offset < BIOS_SIZE) // est� dentro del �rea de BIOS?
		str_PC = &bios_mem[offset];
	else
		str_PC = &memoria[(PC % 0x20000000)]; // - mem_n_base]; */
//	logmsg("saltando a %02x %06x\n", (PC >> 24) & 0xFF, PC & 0x00FFFFFF);
//	str_PC = &mem_zone[(PC >> 24) & 0xFF][PC & 0x00FFFFFF];
#ifdef DEBUG_MEMORY_POINTER
	if (get_memory_pointer(PC) == NULL)
	{	
		logxmsg(LOG_MEM, "PC_f_delayslot2: direccion %x invalida\n", PC);
		abort();
	}
#endif
	str_PC = get_memory_pointer(PC);
}

void PC_f_delayslot(void)
{
	PC += 2;
	str_PC += 2;
	PC_func = PC_f_delayslot2;
}

void PC_f_nextpc(void)
{
	if (NEXTPC == 0)
	{
		logmsg("PC_f_nextpc: saltando a delayslot 0? PC=%x\n", PC);
		dump_registers();
		fclose(logfp);
		abort();
	}

	PC = NEXTPC;
	PC_func = PC_f_normal;

	/*	if ((PC & 0xFF000000) == 0xAC000000)
	{
		str_PC = &memoria[PC - 0xAC000000];
	}
	else
	if ((PC & 0xE0000000) == 0xA0000000) // P2
	{
//		fprintf(fp, "Saltando a P2\r\n");
		str_PC = &memoria[PC - 0xA0000000 + mem_offset];
	}
	else
	if ((PC & 0xE0000000) == 0x000000000) // P0
	{
//		fprintf(fp, "Saltando a P0\r\n");
		str_PC = &memoria[PC + mem_offset];
	}
	else */

/*	DWORD offset = PC % 0x20000000;

	if (offset < BIOS_SIZE) // est� dentro del �rea de BIOS?
		str_PC = &bios_mem[offset];
	else
		str_PC = &memoria[offset]; // - mem_n_base];
		
	logmsg("offset: %x\n", offset); */

//	logmsg("saltando a %02x %06x\n", (PC >> 24) & 0xFF, PC & 0x00FFFFFF);
//	str_PC = &mem_zone[(PC >> 24) & 0xFF][PC & 0x00FFFFFF];

#ifdef DEBUG_MEMORY_POINTER
	if (get_memory_pointer(PC) == NULL)
	{	
		logxmsg(LOG_MEM, "PC_f_nextpc: direccion %x invalida\n", PC);
		abort();
	}
#endif

	str_PC = get_memory_pointer(PC);
}
#endif

