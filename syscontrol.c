/****************************************************************************

	SYSCONTROL - System Control Opcodes for SH-4 Processor

*****************************************************************************/
 
#include <stdio.h>
#include "sh4emu.h"

#ifdef _fast_interpreter_
#define sys_n jumptable.jit_array[jumptable.current_pos].cache.n
#define sys_m  jumptable.jit_array[jumptable.current_pos].cache.m
#else
short sys_n=0;
short sys_m =0;
#endif

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
}

OPCODE(sleep116)
{
	// al entrar al sleepmode, se activan las ints
/* 	REMOVE_BIT(SR, SR_MD);
	REMOVE_BIT(SR, SR_BL); */
//	logmsg("sleep116\n");
//	UpdateSR(SR & ~(SR_MD | SR_BL));
	SR_MD = ~SR_MD;
	UpdateSR(SR);

	PC += 2;
}

OPCODE(ldc116) // LDC Rm, SR : Rm -> SR (0100mmmm 00001110)
{
	sys_m = (arg >> 8) & 0x0F;

//	SR = R(sys_m);
	UpdateSR(R(sys_m));

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc116: R%d=%x -> SR=%x\r\n", m, R(sys_m), SR);
#endif
}

OPCODE(ldc118) // LDC Rm, VBR : Rm -> VBR (0100mmmm 00101110)
{
	sys_m = (arg >> 8) & 0x0F;

	VBR = R(sys_m);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc118: r%d=%x -> VBR\r\n", m, R(sys_m));
#endif
}

OPCODE(ldc119) // LDC Rm, SSR : Rm -> SSR (0100mmmm 00111110)
{
	sys_m = (arg >> 8) & 0x0F;

	SSR = R(sys_m);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc119: r%d=%x -> SSR\r\n", m, R(sys_m));
#endif
}

OPCODE(ldc123) // LDC Rm, Rn_BANK (0100mmmm 1nnn1110)
{
	sys_m = (arg >> 8) & 0x0F;
	sys_n = (arg >> 4) & 0x07;
	
	COPY_REG(R_BANK(sys_n), R(sys_m));

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc123: R(%d)=R(%d)_BANK\n", m, n);
#endif
}

OPCODE(ldcl122) // LDC.L @Rm+, SR (0100mmmm 00000111)
{
	sys_m = (arg >> 8) & 0x0F;
	DWORD newSR;

	ReadMemoryL(R(sys_m), &newSR);
	UpdateSR(newSR);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl122: @R%d+=%x, SR=%x\n", m, R(sys_m) - 4, SR);
#endif
}

OPCODE(ldcl123) // LDC.L @Rm+, GBR (0100mmmm 00010111)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &GBR);

	R(sys_m) += 4;

	PC += 2;
}

OPCODE(ldcl124) // LDC.L @Rm+, VBR (0100mmmm 00100111)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &VBR);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl124: @R%d+ (%x) -> VBR = %x\n", m, R(sys_m) - 4, VBR);
#endif
}

OPCODE(ldcl125) // LDC.L @Rm+, SSR (0100mmmm 00110111)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &SSR);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl125: @R%d+=%x, SSR=%x\r\n", m, R(sys_m) - 4, SSR);
#endif
}

OPCODE(ldcl126) // LDC.L @Rm+, SPC (0100mmmm 01000111)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &SPC);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl126: r[%d]=%x, SPC=%x\r\n", m, R(sys_m) - 4, SPC);
#endif
}

OPCODE(ldcl127) // LDC.L @Rm+, SGR (0100mmmm 00110110)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &SGR);

	R(sys_m) += 4;

	PC += 2;
}

OPCODE(ldcl128) // LDC.L @Rm+, DBR (0100mmmm 11110110)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &DBR);

	R(sys_m) += 4;

	PC += 2;
}

OPCODE(ldcl129) // LDC.L @Rm+, Rn_BANK (0100mmmm 1nnn0111)
{
	sys_m = (arg >> 8) & 0x0F;
	sys_n = (arg >> 4) & 0x07;
	
	ReadMemoryL(R(sys_m),&R_BANK(sys_n));

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl129: @R%d+=%x, R%d_BANK=%x\n", m, R(sys_m) - 4, n, R_BANK(sys_n));
#endif
}

OPCODE(lds130) // LDS Rm, MACH (0100mmmm 00001010)
{
	sys_m = (arg >> 8) & 0x0F;

	MACH = R(sys_m);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc130: r%d=%x -> MACH\r\n", m, R(sys_m));
#endif
}

OPCODE(lds131) // LDS Rm, MACL (0100mmmm 00011010)
{
	sys_m = (arg >> 8) & 0x0F;

	MACL = R(sys_m);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc131: r%d=%x -> MACL\r\n", m, R(sys_m));
#endif
}

OPCODE(lds132) // LDS Rm, PR : Rm -> PR (0100mmmm 00101010)
{
	sys_m = (arg >> 8) & 0x0F;

	PR = R(sys_m);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc132: r%d=%x -> PR\r\n", m, R(sys_m));
#endif
}

OPCODE(ldsl133) // LDS.L @Rm+, MACH (0100mmmm 00000110)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), (DWORD *) &MACH);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc133: r%d=%x -> MACH\r\n", m, R(sys_m));
#endif
}

OPCODE(ldsl134) // LDS.L @Rm+, MACL (0100mmmm 00010110)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), (DWORD *) &MACL);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc134: r%d=%x -> MACL\r\n", m, R(sys_m));
#endif
}

OPCODE(ldsl135) // LDS.L @Rm+, PR : (Rm) -> PR, Rm + 4 -> Rm (0100mmmm 00100110)
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), (DWORD *) &PR);

	R(sys_m) += 4;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc135: R%d=%x -> PR=%x\r\n", m, R(sys_m) - 4, PR);
#endif
}

OPCODE(ocbwb141) // OCBWB @Rn (0000nnnn 10110011)
{
	// FIXME: qué hace este opcode?
	PC += 2;
}

OPCODE(pref142) // PREF @Rn : (Rn) -> operand cache (0000nnnn 10000011)
{
	sys_n = (arg >> 8) & 0x0F;
	DWORD addr, * src;

//    logxmsg(LOG_MEM, "PREF @R%d, valor %x\r\n", n, R(sys_n));

	if (R(sys_n) >= 0xe0000000 && R(sys_n) <= 0xeffffffc)
	{
	    addr = (R(sys_n) & 0x03FFFFC0) | ((( ((R(sys_n) & 0x20) ? *QACR1 : *QACR0)  >> 2) & 0x07) << 26);

//		logmsg("PREF: %lx\r\n", addr);

	    if (R(sys_n) & 0x20)
	    {
	    	src = SQ1;
	    	addr |= 0x20;
		}
		else
			src = SQ0;

//		logxmsg(LOG_MEM, "PREF %x\n", addr);
		memwrite(addr, src, 32); // 32 bytes por cada SQ

		if (addr & 0x10000000) // debieran ser 0x10000000 o 0x10000020
		{
//			logxmsg(LOG_MEM, "PREF %x\n", addr);
			ta_check(addr);
		}
	}

	PC += 2;
}

OPCODE(rte143) // RTE (00000000 00101011)
{
	extern bool inside_int;

//	SR = SSR;
	UpdateSR(SSR);
/*	delayslot = SPC;
	PC_func = PC_f_delayslot; */
	PC += 2;
	core.execute(*(WORD *) get_memory_pointer(PC));
	PC = SPC;
	inside_int = false;

/*	logmsg("rte: SR=%x, SPC=%x\r\n", SR, SPC);
	dump_registers(); */
//	filelogging = 0;
//	dump_llamadas();
}

OPCODE(sett145) // SETT : 1 -> T (00000000 00011000)
{
	SET_T

	PC += 2;
}

OPCODE(stc147) // STC SR, Rn : SR -> Rn (0000nnnn 00000010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = SR;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc147: STC SR=%x, R%d=%x\n", SR, n, R(sys_n));
#endif
}

OPCODE(stc149) // STC VBR, Rn : VBR -> Rn (0000nnnn 00100010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = VBR;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc149: VBR=%x -> r[%d]=%x\r\n", VBR, n, R(sys_n));
#endif
}

OPCODE(stc152) // STC SSR, Rn : SSR -> Rn (0000nnnn 00110010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = SSR;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc152: SSR=%x -> r[%d]=%x\r\n", SSR, n, R(sys_n));
#endif
}

OPCODE(stc150) // STC GBR, Rn 
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = GBR;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc150: GBR=%x -> r[%d]=%x\r\n", GBR, n, R(sys_n));
#endif
}

OPCODE(stc155) // STC DBR, Rn 
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = DBR;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc155: DBR=%x -> r[%d]=%x\r\n", DBR, n, R(sys_n));
#endif
}

OPCODE(stc153) // STC SPC, Rn 
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = SPC;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc155: DBR=%x -> r[%d]=%x\r\n", DBR, n, R(sys_n));
#endif
}

OPCODE(stcn154) // STC SPC, Rn 
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = SGR;

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc155: DBR=%x -> r[%d]=%x\r\n", DBR, n, R(sys_n));
#endif
}

OPCODE(stc154) // STC Rm_BANK, Rn (0000nnnn 1mmm0010)
{
	sys_n = (arg >> 8) & 0x0F;
	sys_m = (arg >> 4) & 0x07;

	R(sys_n) = R_BANK(sys_m);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl154: STC R%d_BANK=%x, R%d=%x\n", R_BANK(sys_m), R(sys_n));
#endif
}

OPCODE(stcl155) // STC.L SR, @-Rn (0100nnnn 00000011)
{
	sys_n = (arg >> 8) & 0x0F;

    R(sys_n) -= 4;
    
    WriteMemoryL(R(sys_n), &SR);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl155: STC.L SR=%x, @-R%d=%x\n", SR, n, R(sys_n));
#endif
}

OPCODE(stcl156) // STC.L GBR, @-Rn (0100nnnn 00010011)
{
	sys_n = (arg >> 8) & 0x0F;

    R(sys_n) -= 4;
    
    WriteMemoryL(R(sys_n), &GBR);

	PC += 2;
}

OPCODE(stcl157) // STC.L VBR, @-Rn (0100nnnn 00100011)
{
	sys_n = (arg >> 8) & 0x0F;

    R(sys_n) -= 4;
    
    WriteMemoryL(R(sys_n), &VBR);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl157: VBR=%x -> @-R[%d]=%x\r\n", VBR, n, R(sys_n));
#endif
}

OPCODE(stcl158) // STC.L SSR, @-Rn (0100nnnn 00110011)
{
	sys_n = (arg >> 8) & 0x0F;

    R(sys_n) -= 4;
    
    WriteMemoryL(R(sys_n), &SSR);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl158: SSR=%x -> r[%d]\r\n", SSR, n, R(sys_n));
#endif
}

OPCODE(stcl159) // STC.L SPC, @-Rn (0100nnnn 00100011)
{
	sys_n = (arg >> 8) & 0x0F;

    R(sys_n) -= 4;
    
    WriteMemoryL(R(sys_n), &SPC);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl159: SPC=%x -> @-R[%d]=%x\r\n", SPC, n, R(sys_n));
#endif
}

OPCODE(stcl160) // STC.L SGR, @-Rn (0100nnnn 00110010)
{
	sys_n = (arg >> 8) & 0x0F;
	
	R(sys_n) -= 4;
	
	WriteMemoryL(R(sys_n), (DWORD *) &SGR);

	PC += 2;
}

OPCODE(stcl161) // STC.L DBR, @-Rn (0100nnnn 11110010)
{
	sys_n = (arg >> 8) & 0x0F;
	
	R(sys_n) -= 4;
	
	WriteMemoryL(R(sys_n), (DWORD *) &DBR);

	PC += 2;
}

OPCODE(stcl162) // STC.L Rm_BANK, @-Rn (0100nnnn 1mmm0011)
{
	sys_n = (arg >> 8) & 0x0F;
	sys_m = (arg >> 4) & 0x07;
	
	R(sys_n) -= 4;
	
	WriteMemoryL(R(sys_n), (DWORD *) &R_BANK(sys_m));
	
	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc.l R%d_BANK=%x, @-R%d=%x\n", m, R_BANK(sys_m), n, R(sys_n));
#endif
}

OPCODE(sts163) // STS MACH, Rn : MACH -> Rn (0000nnnn 00001010)
{
	sys_n = (arg >> 8) & 0x0F;

//	R(sys_n) = MACH;
	COPY_REG(R(sys_n), MACH);

	PC += 2;

#ifdef DEBUG_SYSCONTROL_STS
	logmsg("sts163: reg[%d]=%d, MACH=%x,%d\r\n", n, R(sys_n), MACH, MACH);
#endif
}

OPCODE(sts164) // STS MACL, Rn : MACL -> Rn (0000nnnn 00011010)
{
	sys_n = (arg >> 8) & 0x0F;

//	R(sys_n) = MACL;
	COPY_REG(R(sys_n), MACL);

	PC += 2;

#ifdef DEBUG_SYSCONTROL_STS
	logmsg("sts164: reg[%d]=%d, MACL=%x,%d\r\n", n, R(sys_n), MACL, MACL);
#endif
}

OPCODE(sts165) // STS PR, Rn : PR -> Rn (0000nnnn 00101010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) = PR;

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "sts165: n:%d, reg[n]=%d, PR=%d\r\n", n, R(sys_n), PR);
#endif
}

OPCODE(stsl166) // STS.L MACH, @-Rn (0100nnnn 00000010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) -= 4;

	WriteMemoryL(R(sys_n), &MACH);

	PC += 2;

#ifdef ASM_DEBUG
	logmsg("stsl167: n:%d, reg[n]=%d, MACH=%d\r\n", n, R(sys_n), MACH);
#endif
}

OPCODE(stsl167) // STS.L MACL, @-Rn (0100nnnn 00010010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) -= 4;

	WriteMemoryL(R(sys_n), &MACL);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stsl167: n:%d, reg[n]=%d, MACL=%d\r\n", n, R(sys_n), MACL);
#endif
}

OPCODE(stsl168) // STS.L PR, @-Rn : Rn - 4 -> Rn, PR -> (Rn) (0100nnnn 00100010)
{
	sys_n = (arg >> 8) & 0x0F;

	R(sys_n) -= 4;

	WriteMemoryL(R(sys_n), &PR);

	PC += 2;

#ifdef DEBUG_SYSCONTROL
	logmsg("stsl168: PR=%x -> @-R%d=%x\r\n", PR, n, R(sys_n));
#endif
}

OPCODE(trapa169) // TRAPA #imm (11000011 iiiiiiii)
{
	WORD imm = arg & 0xFF;

	SPC = PC + 2;
	SSR = SR;
	*TRA = (imm << 2);
	*EXPEVT = 0x160;

	SET_SH4_BIT(SR_MD);
	SET_SH4_BIT(SR_RB);
	SET_SH4_BIT(SR_BL);
	UpdateSR(SH4_SYSTEM_REGISTER_INTC_REWRITTEN);

	
/*	NEXTPC = VBR + 0x0100;
	PC_func = PC_f_nextpc; */

	PC = VBR + 0x0100;

//	logmsg("imm: %x, NEXTPC: %x\r\n", imm, NEXTPC);
}

OPCODE(ldc122) // LDC Rm, DBR
{
	sys_m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(sys_m), &DBR);

	PC += 2;
}

