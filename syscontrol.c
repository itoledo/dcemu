/****************************************************************************

	SYSCONTROL - System Control Opcodes for SH-4 Processor

*****************************************************************************/
 
#include <stdio.h>
#include "sh4emu.h"
#include "excepciones.h"
#include "graficos.h"
#include "ta.h"			/* ta_procesar_bloque(): la FIFO de poligonos */

void dump_llamadas()
{
	FILE * fp = fopen("logs/llamadas.txt", "w");
	int i;
	
	for (i = 0; opcodes[i].opdesc; i++)
		if (opcodes[i].llamadas > 0)
		{
			fprintf(fp, "%03d %10u %s\r\n", i, opcodes[i].llamadas, opcodes[i].opdesc);
		}

	fclose(fp);
}

OPCODE(nop) // NOP (00000000 00001001)
{
	PC += 2;
}

OPCODE(clrt115)	// CLRT (0000000000001000)
{
	REMOVE_SH4_BIT(SR_T);

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(clrmac113) // CLRMAC (00000000 00101000)
{
	MACH = 0;
	MACL = 0;

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(clrs114) // CLRS (00000000 01001000)
{
	REMOVE_SH4_BIT(SR_S);

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(sets144) // SETS (00000000 01011000)
{
	SET_SH4_BIT(SR_S);

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(ldtlb136) // LDTLB (00000000 00111000)
{
	/* Carga la entrada de la UTLB que apunta MMUCR.URC desde PTEH, PTEL y
	   PTEA. No toca URC: el contador lo mueve la busqueda, no la carga. */
	mmu_ldtlb(*PTEH, *PTEL, *PTEA, MMUCR_URC(*MMUCR));

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(sleep116)
{
	// al entrar al sleepmode, se activan las ints
/* 	REMOVE_BIT(SR, SR_MD);
	REMOVE_BIT(SR, SR_BL); */
//	logmsg("sleep116\n");
//	UpdateSR(SR & ~(SR_MD | SR_BL));
// 	SR_MD = ~SR_MD;
//  	UpdateSR(SR);

// 	SH4_SLEEPING = true;

	PC += 2;

	core.context.cycles += 4;
}

OPCODE(ldc116) // LDC Rm, SR : Rm -> SR (0100mmmm 00001110)
{
	short m = (arg >> 8) & 0x0F;

//	SR = R(m);
	UpdateSR(R(m) & 0x700083f3);

	PC += 2;

	core.context.cycles += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc116: R%d=%x -> SR=%x\r\n", m, R(m), SR);
#endif
}

OPCODE(ldc117) // LDC Rm, GBR : Rm -> GBR (0100mmmm 00011110)
{
	short m = (arg >> 8) & 0x0F;

	GBR = R(m);

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(ldc120) // LDC Rm, SPC : Rm -> SPC (0100mmmm 01001110)
{
	short m = (arg >> 8) & 0x0F;

	SPC = R(m);

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(ldc121) // LDC Rm, SGR : Rm -> SGR (0100mmmm 00111010)
{
	short m = (arg >> 8) & 0x0F;

	SGR = R(m);

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(ldc118) // LDC Rm, VBR : Rm -> VBR (0100mmmm 00101110)
{
	short m = (arg >> 8) & 0x0F;

	VBR = R(m);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc118: r%d=%x -> VBR\r\n", m, R(m));
#endif
}

OPCODE(ldc119) // LDC Rm, SSR : Rm -> SSR (0100mmmm 00111110)
{
	short m = (arg >> 8) & 0x0F;

	SSR = R(m);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc119: r%d=%x -> SSR\r\n", m, R(m));
#endif
}

OPCODE(ldc123) // LDC Rm, Rn_BANK (0100mmmm 1nnn1110)
{
	short m = (arg >> 8) & 0x0F;
	short n = (arg >> 4) & 0x07;
	
	COPY_REG(R_BANK(n), R(m));

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc123: R(%d)=R(%d)_BANK\n", m, n);
#endif
}

OPCODE(ldcl122) // LDC.L @Rm+, SR (0100mmmm 00000111)
{
	short m = (arg >> 8) & 0x0F;
	DWORD newSR;

	ReadMemoryL(R(m), &newSR);
	UpdateSR(newSR);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl122: @R%d+=%x, SR=%x\n", m, R(m) - 4, SR);
#endif
}

OPCODE(ldcl123) // LDC.L @Rm+, GBR (0100mmmm 00010111)
{
 	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &GBR);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(ldcl124) // LDC.L @Rm+, VBR (0100mmmm 00100111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &VBR);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl124: @R%d+ (%x) -> VBR = %x\n", m, R(m) - 4, VBR);
#endif
}

OPCODE(ldcl125) // LDC.L @Rm+, SSR (0100mmmm 00110111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SSR);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl125: @R%d+=%x, SSR=%x\r\n", m, R(m) - 4, SSR);
#endif
}

OPCODE(ldcl126) // LDC.L @Rm+, SPC (0100mmmm 01000111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SPC);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl126: r[%d]=%x, SPC=%x\r\n", m, R(m) - 4, SPC);
#endif
}

OPCODE(ldcl127) // LDC.L @Rm+, SGR (0100mmmm 00110110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SGR);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(ldcl128) // LDC.L @Rm+, DBR (0100mmmm 11110110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &DBR);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;
}

OPCODE(ldcl129) // LDC.L @Rm+, Rn_BANK (0100mmmm 1nnn0111)
{
	short m = (arg >> 8) & 0x0F;
	short n = (arg >> 4) & 0x07;
	
	ReadMemoryL(R(m),&R_BANK(n));

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl129: @R%d+=%x, R%d_BANK=%x\n", m, R(m) - 4, n, R_BANK(n));
#endif
}

OPCODE(lds130) // LDS Rm, MACH (0100mmmm 00001010)
{
	short m = (arg >> 8) & 0x0F;

	MACH = R(m);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc130: r%d=%x -> MACH\r\n", m, R(m));
#endif
}

OPCODE(lds131) // LDS Rm, MACL (0100mmmm 00011010)
{
	short m = (arg >> 8) & 0x0F;

	MACL = R(m);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc131: r%d=%x -> MACL\r\n", m, R(m));
#endif
}

OPCODE(lds132) // LDS Rm, PR : Rm -> PR (0100mmmm 00101010)
{
	short m = (arg >> 8) & 0x0F;

	PR = R(m);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc132: r%d=%x -> PR\r\n", m, R(m));
#endif
}

OPCODE(ldsl133) // LDS.L @Rm+, MACH (0100mmmm 00000110)
{
 	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &MACH);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc133: r%d=%x -> MACH\r\n", m, R(m));
#endif
}

OPCODE(ldsl134) // LDS.L @Rm+, MACL (0100mmmm 00010110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &MACL);

	R(m) += 4;

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc134: r%d=%x -> MACL\r\n", m, R(m));
#endif
}

OPCODE(ldsl135) // LDS.L @Rm+, PR : (Rm) -> PR, Rm + 4 -> Rm (0100mmmm 00100110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &PR);

	R(m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc135: R%d=%x -> PR=%x\r\n", m, R(m) - 4, PR);
#endif
}

/* Las tres operaciones de bloque de cache no tienen efecto visible sin cache
   emulada: dcemu escribe siempre directo a memoria, asi que no hay nada que
   invalidar ni que volcar. Se dejan como instrucciones validas que solo
   avanzan PC. */
OPCODE(ocbwb141) // OCBWB @Rn (0000nnnn 10110011)
{
	PC += 2;

	core.context.cycles += 1;
}

OPCODE(ocbp140) // OCBP @Rn (0000nnnn 10100011)
{
	PC += 2;

	core.context.cycles += 1;
}

OPCODE(pref142) // PREF @Rn : (Rn) -> operand cache (0000nnnn 10000011)
{
	short n = (arg >> 8) & 0x0F;
	DWORD addr, * src;

//    logxmsg(LOG_MEM, "PREF @R%d, valor %x\r\n", n, R(n));

	if (R(n) >= 0xe0000000 && R(n) <= 0xeffffffc)
	{
	    addr = (R(n) & 0x03FFFFC0) | ((( ((R(n) & 0x20) ? *QACR1 : *QACR0)  >> 2) & 0x07) << 26);

//		logmsg("PREF: %lx\r\n", addr);

	    if (R(n) & 0x20)
	    {
	    	src = SQ1;
	    	addr |= 0x20;
		}
		else
			src = SQ0;

//		logxmsg(LOG_MEM, "PREF %x\n", addr);
		memwrite(addr, src, 32); // 32 bytes por cada SQ

		/* Solo la FIFO de poligonos, 0x10000000-0x107FFFFF. El chequeo de antes
		   era "addr & 0x10000000", un AND de bits que tambien daba verdadero
		   para 0x11xxxxxx -- la FIFO de texturas del TA, por donde KOS sube las
		   texturas con sq_fast_cpy(). Cada bloque de 32 bytes de textura se
		   interpretaba como una palabra de control de poligono. */
		/* 0x10800000 es la entrada del convertidor YUV del TA, no la FIFO de
		   poligonos: mismo cuarto de la zona 0x10, otro destino. */
		if ((addr & 0xFF800000) == 0x10800000)
		{
			pvr_yuv_bloque(src);
		}
		else
		if ((addr & 0xFF800000) == 0x10000000)
		{
//			logxmsg(LOG_MEM, "PREF %x\n", addr);
			// Misma mascara que ta_write(): dos ranuras de 32 bytes, una por
			// store queue. Antes era & 0xFF, que no coincidia con el indice de
			// ta_write() en cuanto el guest pasaba de 0x100.
			//
			// El decodificador vive en ta.c: el CH2 DMA entrega bloques a esta
			// misma FIFO y tiene que interpretarlos igual. Es el que junta los
			// parametros de 64 bytes, que llegan en dos bloques -- uno por
			// store queue.
			ta_procesar_bloque(&ta_mem[addr & 0x3F]); // 0x00 o 0x20
// 			ta_check(addr);
		}
	}

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(rte143) // RTE (00000000 00101011)
{
	extern bool inside_int;

//	SR = SSR;
	UpdateSR(SSR);
/*	delayslot = SPC;
	PC_func = PC_f_delayslot; */
	PC += 2;

	/* RTE tambien tiene ranura de retardo. Igual que en branch.c: la bandera
	   solo la mira run(), para 0x800 contra 0x820. */
	en_ranura_retardo = 1;
	core.execute(*(WORD *) get_memory_pointer(PC));
	en_ranura_retardo = 0;

	PC = SPC;
	inside_int = false;

	core.context.cycles += 5;

/*	logmsg("rte: SR=%x, SPC=%x\r\n", SR, SPC);
	dump_registers(); */
//	filelogging = 0;
//	dump_llamadas();
}

OPCODE(sett145) // SETT : 1 -> T (00000000 00011000)
{
	SET_T

	PC += 2;

	core.context.cycles += 1;
}

OPCODE(stc147) // STC SR, Rn : SR -> Rn (0000nnnn 00000010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = SR;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc147: STC SR=%x, R%d=%x\n", SR, n, R(n));
#endif
}

OPCODE(stc149) // STC VBR, Rn : VBR -> Rn (0000nnnn 00100010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = VBR;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc149: VBR=%x -> r[%d]=%x\r\n", VBR, n, R(n));
#endif
}

OPCODE(stc152) // STC SSR, Rn : SSR -> Rn (0000nnnn 00110010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = SSR;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc152: SSR=%x -> r[%d]=%x\r\n", SSR, n, R(n));
#endif
}

OPCODE(stc150) // STC GBR, Rn 
{ 	short n = (arg >> 8) & 0x0F;

	R(n) = GBR;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc150: GBR=%x -> r[%d]=%x\r\n", GBR, n, R(n));
#endif
}

OPCODE(stc155) // STC DBR, Rn 
{
	short n = (arg >> 8) & 0x0F;

	R(n) = DBR;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc155: DBR=%x -> r[%d]=%x\r\n", DBR, n, R(n));
#endif
}

OPCODE(stc153) // STC SPC, Rn : SPC -> Rn (0000nnnn 01000010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = SPC;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc153: SPC=%x -> r[%d]=%x\r\n", SPC, n, R(n));
#endif
}

OPCODE(stcn154) // STC SGR, Rn : SGR -> Rn (0000nnnn 00111010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = SGR;

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcn154: SGR=%x -> r[%d]=%x\r\n", SGR, n, R(n));
#endif
}

OPCODE(stc154) // STC Rm_BANK, Rn (0000nnnn 1mmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x07;

	R(n) = R_BANK(m);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl154: STC R%d_BANK=%x, R%d=%x\n", R_BANK(m), R(n));
#endif
}

OPCODE(stcl155) // STC.L SR, @-Rn (0100nnnn 00000011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &SR);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl155: STC.L SR=%x, @-R%d=%x\n", SR, n, R(n));
#endif
}

OPCODE(stcl156) // STC.L GBR, @-Rn (0100nnnn 00010011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &GBR);

	PC += 2;

	core.context.cycles += 2;
}

OPCODE(stcl157) // STC.L VBR, @-Rn (0100nnnn 00100011)
{
  	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &VBR);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl157: VBR=%x -> @-R[%d]=%x\r\n", VBR, n, R(n));
#endif
}

OPCODE(stcl158) // STC.L SSR, @-Rn (0100nnnn 00110011)
{
  	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &SSR);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl158: SSR=%x -> r[%d]\r\n", SSR, n, R(n));
#endif
}

OPCODE(stcl159) // STC.L SPC, @-Rn (0100nnnn 00100011)
{
  	short  n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &SPC);

	PC += 2;
	
	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl159: SPC=%x -> @-R[%d]=%x\r\n", SPC, n, R(n));
#endif
}

OPCODE(stcl160) // STC.L SGR, @-Rn (0100nnnn 00110010)
{
 	short n = (arg >> 8) & 0x0F;
	
	R(n) -= 4;
	
	WriteMemoryL(R(n), (DWORD *) &SGR);

	PC += 2;

	core.context.cycles += 2;
}

OPCODE(stcl161) // STC.L DBR, @-Rn (0100nnnn 11110010)
{
	short n = (arg >> 8) & 0x0F;
	
	R(n) -= 4;
	
	WriteMemoryL(R(n), (DWORD *) &DBR);

	PC += 2;

	core.context.cycles += 2;
}

OPCODE(stcl162) // STC.L Rm_BANK, @-Rn (0100nnnn 1mmm0011)
{

	short n = (arg >> 8) & 0x0F;
 	short m = (arg >> 4) & 0x07;
	
	R(n) -= 4;
	
	WriteMemoryL(R(n), (DWORD *) &R_BANK(m));
	
	PC += 2;
	
	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc.l R%d_BANK=%x, @-R%d=%x\n", m, R_BANK(m), n, R(n));
#endif
}

OPCODE(sts163) // STS MACH, Rn : MACH -> Rn (0000nnnn 00001010)
{
 	short n = (arg >> 8) & 0x0F;

//	R(n) = MACH;
	COPY_REG(R(n), MACH);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL_STS
	logmsg("sts163: reg[%d]=%d, MACH=%x,%d\r\n", n, R(n), MACH, MACH);
#endif
}

OPCODE(sts164) // STS MACL, Rn : MACL -> Rn (0000nnnn 00011010)
{
 	short n = (arg >> 8) & 0x0F;

//	R(n) = MACL;
	COPY_REG(R(n), MACL);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL_STS
	logmsg("sts164: reg[%d]=%d, MACL=%x,%d\r\n", n, R(n), MACL, MACL);
#endif
}

OPCODE(sts165) // STS PR, Rn : PR -> Rn (0000nnnn 00101010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = PR;

	PC += 2;

	core.context.cycles += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "sts165: n:%d, reg[n]=%d, PR=%d\r\n", n, R(n), PR);
#endif
}

OPCODE(stsl166) // STS.L MACH, @-Rn (0100nnnn 00000010)
{
 	short n = (arg >> 8) & 0x0F;

	R(n) -= 4;

	WriteMemoryL(R(n), &MACH);

	PC += 2;

	core.context.cycles += 3;

#ifdef ASM_DEBUG
	logmsg("stsl167: n:%d, reg[n]=%d, MACH=%d\r\n", n, R(n), MACH);
#endif
}

OPCODE(stsl167) // STS.L MACL, @-Rn (0100nnnn 00010010)
{
 	short n = (arg >> 8) & 0x0F;

	R(n) -= 4;

	WriteMemoryL(R(n), &MACL);

	PC += 2;

	core.context.cycles += 3;

#ifdef DEBUG_SYSCONTROL
	logmsg("stsl167: n:%d, reg[n]=%d, MACL=%d\r\n", n, R(n), MACL);
#endif
}

OPCODE(stsl168) // STS.L PR, @-Rn : Rn - 4 -> Rn, PR -> (Rn) (0100nnnn 00100010)
{
 	short n = (arg >> 8) & 0x0F;

	R(n) -= 4;

	WriteMemoryL(R(n), &PR);

	PC += 2;

	core.context.cycles += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stsl168: PR=%x -> @-R%d=%x\r\n", PR, n, R(n));
#endif
}

OPCODE(trapa169) // TRAPA #imm (11000011 iiiiiiii)
{
	WORD imm = arg & 0xFF;

	SPC = PC + 2;
	SSR = SR;
	SGR = R(15);	// la secuencia de excepcion del SH-4 guarda el stack pointer
	*TRA = (imm << 2);
	*EXPEVT = 0x160;

	SET_SH4_BIT(SR_MD);
	SET_SH4_BIT(SR_RB);
	SET_SH4_BIT(SR_BL);
	UpdateSR(SH4_SYSTEM_REGISTER_INTC_REWRITTEN);

	
/*	NEXTPC = VBR + 0x0100;
	PC_func = PC_f_nextpc; */

	PC = VBR + 0x0100;

	core.context.cycles += 7;

//	logmsg("imm: %x, NEXTPC: %x\r\n", imm, NEXTPC);
}

OPCODE(ldc122) // LDC Rm, DBR : Rm -> DBR (0100mmmm 11111010)
{
 	short m = (arg >> 8) & 0x0F;

	/* Copia el registro. La variante que lee de memoria es LDC.L @Rm+, DBR
	   (ldcl128), que es otra instruccion. */
	DBR = R(m);

	PC += 2;

	core.context.cycles += 3;
}

