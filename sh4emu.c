#include "sh4emu.h"
#include <stdio.h>
#include "options.h"
#include "branch.h"
#include "opcodes.h"
#include "floatsimple.h"
#include "intc.h"


sh4_cpu core;

DWORD *		PTEL;		// Page Table Entry Low register
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
 FPSCR = 0x0004001; // Floating Point Status/Control Register
 PC =  0xA0000000;   // Program Counter
 MACH = 0; // Multiply-And-Accumulate register High
 MACL = 0; // Multiply-And-Accumulate register Low
 FPUL = 0;
 PR = 0;   // Procedure Register
 SR = 0x700000F0; // Status Register
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

void close()
{

}

void run (WORD instr)
{
(opcodes[oplist[instr]].funcion) (instr);
}

#ifdef _fast_interpreter_
void runBlock (void)
{
	execute_loop();
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
 core.closeCpu = &close;
 core.execute = &run;
 #ifdef _fast_interpreter_
 core.runBlock = &runBlock;
 #endif
 core.resetCpu();
}

void UpdateFPSCR(DWORD newFPSCR)
{
	// aquí hacemos el swap de los registros float
	// y revisamos si hay cambio en FPSCR_PR o FPSCR_SZ

/*	if ((FPSCR ^ newFPSCR) & (FPSCR_PR | FPSCR_SZ)) // algún cambio?
	{ */
		// determinemos qué cambio se hizo
		// nos fijamos en newFPSCR
	int f;
	f = FPSCR_FR;
	FPSCR = newFPSCR;
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



void swap_registers(void)
{
	DWORD tempr[8];
	
	memcpy(&tempr[0], &core.context.registers[16], sizeof(DWORD)*8);
	memcpy(&core.context.registers[16], &core.context.registers[0], sizeof(DWORD)*8);
	memcpy(&core.context.registers[0], &tempr[0], sizeof(DWORD)*8);
}

void UpdateSR(DWORD new)
{
	int md,rb;
	// veamos cómo estamos ahora
//	if (IS_SET(SR, SR_MD) && IS_SET(SR, SR_RB))
	if (new == SH4_SYSTEM_REGISTER_INTC_REWRITTEN)
		swap_registers();
	else
	{
	md = SR_MD;
	rb = SR_RB;
	SR = new;
	if ((SR_MD != md) | (SR_RB != rb))
		{
		swap_registers();
		}
	}
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
	if (offset < BIOS_SIZE) // está dentro del área de BIOS?
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

	if (offset < BIOS_SIZE) // está dentro del área de BIOS?
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

