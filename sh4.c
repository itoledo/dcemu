// sh4.c

#include <stdio.h>
#include "main.h" // para definición de BIOS_SIZE
#include "sh4.h"
#include "options.h"
#include "branch.h"

extern FILE * logfp, * serialfp, * memfp;
extern unsigned char * memoria;

// unsigned char *str_PC;

DWORD registers[24];
float float_registers[32];
unsigned long SR = 0x700000F0; // Status Register
unsigned long SSR = 0; // Saved Status Register
unsigned long SPC = 0; // Saved Program Counter
unsigned long GBR = 0; // Global Base Register
unsigned long VBR = 0; // Vector Base Register
unsigned long SGR = 0; // Saved General Register 15
unsigned long DBR = 0; // Debug Base Register

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

unsigned long MACH = 0; // Multiply-And-Accumulate register High
unsigned long MACL = 0; // Multiply-And-Accumulate register Low
DWORD FPUL = 0;
unsigned long PR = 0;   // Procedure Register
unsigned long PC = 0xA0000000;   // Program Counter
unsigned long FPSCR = 0x0004001; // Floating Point Status/Control Register
unsigned long delayslot = 0;
unsigned long NEXTPC = 0;

#ifndef MACRO_REPLACEMENTS
void ejecutar_instruccion(DWORD instr)
{
//	DWORD valor;

/*
			if (PC == HACK_BASE + HACK_ROMFONT)
			{
				logmsg("HACK_ROMFONT\r\n");
				R(0) = mem_base + 1024*1024*5;
				rts112(instr);
			}
			else
*/
/*			if (PC == HACK_BASE + HACK_SYSINFO
			||  PC == HACK_BASE + HACK_FLASHROM)
			{
				logmsg("HACK: PC=%x, retornando 0\n", PC);
				R(0) = 0;
				rts112(instr);
			}
			else */
//			if (PC == HACK_BASE + HACK_GDROM)
//			{
//				logmsg("HACK_GDROM: r6=%x, r7=%x\r\n", R(6), R(7));
//				if (R(6) == 0)
//				{
//					switch(R(7))
//					{
//						case 0: // GDROM_SEND_COMMAND
//						logmsg("GDROM_SEND_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
//						switch(R(4))
//						{
//							case 16: // read sector, R(5) es el lugar donde se guardan los datos
//							{
//								int secstart, secnum, idx = 0;
//								BYTE c;
//								DWORD targetaddr;
////								FILE * fp;
//								char * targetmem;
//
//								memread(R(5), &secstart, sizeof(int));
//								memread(R(5) + 4, &secnum, sizeof(int));
//								memread(R(5) + 8, &targetaddr, sizeof(DWORD));
//
////								secstart -= 11700;
//
////								if (secstart != 16)
///*								if (secstart != 11700 - 16)
//								{
//									logmsg("restando 150 a secstart(%d), final %d\n", secstart, secstart - 150);
//									secstart -= 150;
//								} */
//
//								logmsg("read sector: start=%x, num=%x, addr=%x\n", secstart, secnum, targetaddr);
///*								fp = fopen("dc.iso", "rb");
//								if (!fp)
//								{
//									logmsg("dc.iso no encontrado");
//									break;
//								}
//								logmsg("seeking a %d, %x\n", secstart * 2048, secstart * 2048);
//								fseek(fp,secstart*2048, 0); */
//								targetmem = malloc(sizeof(char) * 2048 * secnum);
///*								while (idx < 2048 * secnum)
//								{
//									targetmem[idx++] = (c = fgetc(fp));
//								}
//								fclose(fp); */
//								iso_read_sector(&targetmem[0], secstart, secnum);
//								memwrite(targetaddr, &targetmem[0], 2048 * secnum);
//								free(targetmem);
//							}
//							break;
//
//							case 19: // read toc, R(5) es el lugar donde se guardan los datos
//							{
//								int session;
//								DWORD targetaddr;
//								struct TOC toc;
//
//					        	// hay que leer el # de sesión, es el 1er numero que apunta esa dir
//					        	memread(R(5), &session, sizeof(int));
//					        	logmsg("read toc: sesión n° %d\n", session);
//
//					        	// a llenar un TOC "mula"
////					        	toc.entry[0] = 0x40002DB4; // CTRL = 4, LBA = 11700
////					        	toc.entry[0] = 0x40000000; // CTRL = 4, LBA = 0
//								toc.entry[0] = 0x40000000 | iso_get_lba();
//					        	toc.first = 0x00010000;
//					        	toc.last  = 0x00010000;
//					        	toc.dunno = 0;
//					        	memread(R(5) + 4, &targetaddr, sizeof(DWORD));
//					        	logmsg("escribiendo TOC a %x\n", targetaddr);
//					        	memwrite(targetaddr, &toc, sizeof(struct TOC));
//	        				}
//	        				break;
//     					}
//						R(0) = 0x6969;
//						break;
//
//					    case 1: // GDROM_CHECK_COMMAND
//					    logmsg("GDROM_CHECK_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
//					    R(0) = 2;
//					    break;
//
//    	                case 2: // GDROM_MAIN_LOOP
//    	                logmsg("GDROM_MAIN_LOOP\r\n");
//    	                break;
//
//       					case 3: // GDROM_INIT
//       					logmsg("GDROM_INIT\r\n");
//						break;
//
//						case 4: // GDROM_CHECK_DRIVE
//						logmsg("GDROM_CHECK_DRIVE\r\n");
////						valor = 7; // lid closed, no disc
//						valor = 2; // drive is in standby
//						WriteMemoryL(R(4), &valor);
////						valor = 0x80; // GD-ROM
//						valor = 0x10; // CD-ROM
//						WriteMemoryL(R(4) + 4, &valor);
//						R(0) = 0;
//						break;
//
//						case 10: // GDROM_SECTOR_MODE
//						logmsg("GDROM_SECTOR_MODE\r\n");
//						ReadMemoryL(R(4), &valor);
//						logmsg("valor: %x\r\n", valor);
//						if (valor == 0)
//						{
//							valor = 8192;
//							WriteMemoryL(R(4) + 4, &valor);
////							valor = 2048; // mode 2
//							valor = 1024 * iso_get_mode();
//							WriteMemoryL(R(4) + 8, &valor);
//							valor = 2048; // sector size in bytes
//							WriteMemoryL(R(4) + 12, &valor);
//						}
//						else
//							logmsg("GDROM_SECTOR_MODE: valor != 0 no implementado\n");
//						R(0) = 0;
//						break;
//
//						default:
//						logmsg("GDROM: error!\n");
//						break;
//					}
//				}
//				rts112(instr);
//			}
/*			else
			if (PC >= mem_base + mem_offset
			&&  PC <  mem_base + mem_offset + CACHE_SIZE
//			if ((PC & 0xFF000000) == 0x8C000000
			&&  opcode_cache[PC - (mem_base + mem_offset)].func
  			&&  opcode_cache[PC - (mem_base + mem_offset)].func != query_cache)
			{
#ifdef LOGGING
				if (logging)
					opcode_log(opcode_cache[PC - (mem_base + mem_offset)].idx, instr);
#endif
				(opcode_cache[PC - (mem_base + mem_offset)].func) (instr);
#if defined(EXTRA_REG_DEBUG) || defined(FULL_DEBUG_FROM)
#ifdef FULL_DEBUG_FROM
			if (PC >= FULL_DEBUG_FROM && PC <= FULL_DEBUG_TO)
#endif
                dump_registers();
#endif
#ifdef CHECK_VALUE
				check_registers();
#endif
			if (filelogging & FILELOG_REGISTERS)
				dump_registers();
			if (filelogging & FILELOG_CALLS)
				opcodes[opcode_cache[PC - (mem_base + mem_offset)].idx].llamadas++;
			} */
/*			else
			{ */
				query_cache (instr);
//			cache_call++;
//			}
}
#endif

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

void swap_floatregisters(void)
{
    float tempr[16];

	memcpy(&tempr[0], &float_registers[16], sizeof(float)*16);
	memcpy(&float_registers[16], &float_registers[0], sizeof(float)*16);
	memcpy(&float_registers[0], &tempr[0], sizeof(float)*16);
}

void swap_registers(void)
{
	DWORD tempr[8];
	
	memcpy(&tempr[0], &registers[16], sizeof(DWORD)*8);
	memcpy(&registers[16], &registers[0], sizeof(DWORD)*8);
	memcpy(&registers[0], &tempr[0], sizeof(DWORD)*8);
}

void UpdateFPSCR(DWORD newFPSCR)
{
	if (FPSCR & FPSCR_FR)
	{
 		if (newFPSCR & FPSCR_FR)
 			;
		else
			swap_floatregisters();
		FPSCR = newFPSCR;
		return;
	}			

	if (newFPSCR & FPSCR_FR)
		swap_floatregisters();
	FPSCR = newFPSCR;
}

void UpdateSR(DWORD newSR)
{
	// veamos cómo estamos ahora
//	if (IS_SET(SR, SR_MD) && IS_SET(SR, SR_RB))
	if ((SR & (SR_MD | SR_RB)) == (SR_MD | SR_RB))
	{
//		if (IS_SET(newSR, SR_MD) && IS_SET(newSR, SR_RB))
		if ((newSR & (SR_MD | SR_RB)) == (SR_MD | SR_RB))
			;
		else
			swap_registers();
		SR = newSR;
		return;
	}
	
//	if (IS_SET(newSR, SR_MD) && IS_SET(newSR, SR_RB))
	if ((newSR & (SR_MD | SR_RB)) == (SR_MD | SR_RB))
		swap_registers();

	SR = newSR;	
}

