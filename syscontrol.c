/****************************************************************************

	SYSCONTROL - System Control Opcodes for SH-4 Processor

*****************************************************************************/

#include <stdio.h>
#include "sh4.h"

void dump_llamadas()
{
	FILE * fp = fopen("logs/llamadas.txt", "w");
	int i;
	
	for (i = 0; opcodes[i].opdesc; i++)
		if (opcodes[i].llamadas > 0)
		{
			fprintf(fp, "%03d %10d %s\r\n", i, opcodes[i].llamadas, opcodes[i].opdesc);
		}

	fclose(fp);
}

OPCODE(nop) // NOP (00000000 00001001)
{
}

OPCODE(clrt115)	// CLRT (0000000000001000)
{
	REMOVE_BIT(SR, SR_T);
}

OPCODE(sleep116)
{
}

OPCODE(ldc116) // LDC Rm, SR : Rm -> SR (0100mmmm 00001110)
{
	short m = (arg >> 8) & 0x0F;

	SR = R(m);

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc116: R%d=%x -> SR=%x\r\n", m, R(m), SR);
#endif
}

OPCODE(ldc118) // LDC Rm, VBR : Rm -> VBR (0100mmmm 00101110)
{
	short m = (arg >> 8) & 0x0F;

	VBR = R(m);

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc118: r%d=%x -> VBR\r\n", m, R(m));
#endif
}

OPCODE(ldcl122) // LDC.L @Rm+, SR (0100mmmm 00000111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SR);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl122: @R%d+=%x, SR=%x\n", m, R(m) - 4, SR);
#endif
}

OPCODE(ldcl123) // LDC.L @Rm+, GBR (0100mmmm 00010111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &GBR);

	R(m) += 4;
}

OPCODE(ldcl124) // LDC.L @Rm+, VBR (0100mmmm 00100111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &VBR);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl124: @R%d+ (%x) -> VBR = %x\n", m, R(m) - 4, VBR);
#endif
}

OPCODE(ldcl125) // LDC.L @Rm+, SSR (0100mmmm 00110111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SSR);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl125: @R%d+=%x, SSR=%x\r\n", m, R(m) - 4, SSR);
#endif
}

OPCODE(ldcl126) // LDC.L @Rm+, SPC (0100mmmm 01000111)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SPC);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl126: r[%d]=%x, SPC=%x\r\n", m, R(m) - 4, SPC);
#endif
}

OPCODE(ldcl127) // LDC.L @Rm+, SGR (0100mmmm 00110110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &SGR);

	R(m) += 4;
}

OPCODE(ldcl128) // LDC.L @Rm+, DBR (0100mmmm 11110110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), &DBR);

	R(m) += 4;
}

OPCODE(ldcl129) // LDC.L @Rm+, Rn_BANK (0100mmmm 1nnn0111)
{
	short m = (arg >> 8) & 0x0F;
	short n = (arg >> 4) & 0x07;
	
	ReadMemoryL(R(m), &R_BANK(n));

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldcl129: @R%d+=%x, R%d_BANK=%x\n", m, R(m) - 4, n, R_BANK(n));
#endif
}

OPCODE(lds130) // LDS Rm, MACH (0100mmmm 00001010)
{
	short m = (arg >> 8) & 0x0F;

	MACH = R(m);

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc130: r%d=%x -> MACH\r\n", m, R(m));
#endif
}

OPCODE(lds131) // LDS Rm, MACL (0100mmmm 00011010)
{
	short m = (arg >> 8) & 0x0F;

	MACL = R(m);

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc131: r%d=%x -> MACL\r\n", m, R(m));
#endif
}

OPCODE(lds132) // LDS Rm, PR : Rm -> PR (0100mmmm 00101010)
{
	short m = (arg >> 8) & 0x0F;

	PR = R(m);

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc132: r%d=%x -> PR\r\n", m, R(m));
#endif
}

OPCODE(ldsl133) // LDS.L @Rm+, MACH (0100mmmm 00000110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &MACH);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc133: r%d=%x -> MACH\r\n", m, R(m));
#endif
}

OPCODE(ldsl134) // LDS.L @Rm+, MACL (0100mmmm 00010110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &MACL);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc134: r%d=%x -> MACL\r\n", m, R(m));
#endif
}

OPCODE(ldsl135) // LDS.L @Rm+, PR : (Rm) -> PR, Rm + 4 -> Rm (0100mmmm 00100110)
{
	short m = (arg >> 8) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &PR);

	R(m) += 4;

#ifdef DEBUG_SYSCONTROL
	logmsg("ldc135: R%d=%x -> PR=%x\r\n", m, R(m) - 4, PR);
#endif
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

		if (addr & 0x10000000) // debieran ser 0x10000000 o 0x10000020
		{
//			logxmsg(LOG_MEM, "PREF %x\n", addr);
			ta_check(addr);
		}
	}
}

OPCODE(rte143) // RTE (00000000 00101011)
{
	extern bool inside_int;

	SR = SSR;
	delayslot = SPC;
	PC_func = PC_f_delayslot;
	inside_int = false;
	logmsg("rte: SR=%x, SPC=%x\r\n", SR, SPC);
	dump_registers();
//	filelogging = 0;
//	dump_llamadas();
}

OPCODE(sett145) // SETT : 1 -> T (00000000 00011000)
{
	SET_T
}

OPCODE(stc147) // STC SR, Rn : SR -> Rn (0000nnnn 00000010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = SR;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc147: STC SR=%x, R%d=%x\n", SR, n, R(n));
#endif
}

OPCODE(stc149) // STC VBR, Rn : VBR -> Rn (0000nnnn 00100010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = VBR;

#ifdef DEBUG_SYSCONTROL
	logmsg("stc149: VBR=%x -> r[%d]=%x\r\n", VBR, n, R(n));
#endif
}

OPCODE(stc154) // STC Rm_BANK, Rn (0000nnnn 1mmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x07;

	R(n) = R_BANK(m);

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl154: STC R%d_BANK=%x, R%d=%x\n", R_BANK(m), R(n));
#endif
}

OPCODE(stcl155) // STC.L SR, @-Rn (0100nnnn 00000011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &SR);

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl155: STC.L SR=%x, @-R%d=%x\n", SR, n, R(n));
#endif
}

OPCODE(stcl156) // STC.L GBR, @-Rn (0100nnnn 00010011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &GBR);
}

OPCODE(stcl157) // STC.L VBR, @-Rn (0100nnnn 00100011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &VBR);

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl157: VBR=%x -> @-R[%d]=%x\r\n", VBR, n, R(n));
#endif
}

OPCODE(stcl158) // STC.L SSR, @-Rn (0100nnnn 00110011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &SSR);

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl158: SSR=%x -> r[%d]\r\n", SSR, n, R(n));
#endif
}

OPCODE(stcl159) // STC.L SPC, @-Rn (0100nnnn 00100011)
{
	short n = (arg >> 8) & 0x0F;

    R(n) -= 4;
    
    WriteMemoryL(R(n), &SPC);

#ifdef DEBUG_SYSCONTROL
	logmsg("stcl159: SPC=%x -> @-R[%d]=%x\r\n", SPC, n, R(n));
#endif
}

OPCODE(stcl160) // STC.L SGR, @-Rn (0100nnnn 00110010)
{
	short n = (arg >> 8) & 0x0F;
	
	R(n) -= 4;
	
	WriteMemoryL(R(n), (DWORD *) &SGR);
}

OPCODE(stcl161) // STC.L DBR, @-Rn (0100nnnn 11110010)
{
	short n = (arg >> 8) & 0x0F;
	
	R(n) -= 4;
	
	WriteMemoryL(R(n), (DWORD *) &DBR);
}

OPCODE(stcl162) // STC.L Rm_BANK, @-Rn (0100nnnn 1mmm0011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x07;
	
	R(n) -= 4;
	
	WriteMemoryL(R(n), (DWORD *) &R_BANK(m));
	
#ifdef DEBUG_SYSCONTROL
	logmsg("stc.l R%d_BANK=%x, @-R%d=%x\n", m, R_BANK(m), n, R(n));
#endif
}

OPCODE(sts163) // STS MACH, Rn : MACH -> Rn (0000nnnn 00001010)
{
	short n = (arg >> 8) & 0x0F;

//	R(n) = MACH;
	COPY_REG(R(n), MACH);

#ifdef DEBUG_SYSCONTROL_STS
	logmsg("sts163: reg[%d]=%d, MACH=%x,%d\r\n", n, R(n), MACH, MACH);
#endif
}

OPCODE(sts164) // STS MACL, Rn : MACL -> Rn (0000nnnn 00011010)
{
	short n = (arg >> 8) & 0x0F;

//	R(n) = MACL;
	COPY_REG(R(n), MACL);

#ifdef DEBUG_SYSCONTROL_STS
	logmsg("sts164: reg[%d]=%d, MACL=%x,%d\r\n", n, R(n), MACL, MACL);
#endif
}

OPCODE(sts165) // STS PR, Rn : PR -> Rn (0000nnnn 00101010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) = PR;

#ifdef ASM_DEBUG
	fprintf(logfp, "sts165: n:%d, reg[n]=%d, PR=%d\r\n", n, R(n), PR);
#endif
}

OPCODE(stsl166) // STS.L MACH, @-Rn (0100nnnn 00000010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) -= 4;

	WriteMemoryL(R(n), &MACH);

#ifdef ASM_DEBUG
	logmsg("stsl167: n:%d, reg[n]=%d, MACH=%d\r\n", n, R(n), MACH);
#endif
}

OPCODE(stsl167) // STS.L MACL, @-Rn (0100nnnn 00010010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) -= 4;

	WriteMemoryL(R(n), &MACL);

#ifdef DEBUG_SYSCONTROL
	logmsg("stsl167: n:%d, reg[n]=%d, MACL=%d\r\n", n, R(n), MACL);
#endif
}

OPCODE(stsl168) // STS.L PR, @-Rn : Rn - 4 -> Rn, PR -> (Rn) (0100nnnn 00100010)
{
	short n = (arg >> 8) & 0x0F;

	R(n) -= 4;

	WriteMemoryL(R(n), &PR);

#ifdef DEBUG_SYSCONTROL
	logmsg("stsl168: PR=%x -> @-R%d=%x\r\n", PR, n, R(n));
#endif
}

OPCODE(trapa169) // TRAPA #imm (11000011 iiiiiiii)
{
	WORD imm = arg & 0xFF;

	SPC = PC + 2;
	SSR = SR;
	*TRA = (imm << 2);
	*EXPEVT = 0x160;
	SET_BIT(SR, SR_MD);
	SET_BIT(SR, SR_RB);
	SET_BIT(SR, SR_BL);
	
	NEXTPC = VBR + 0x0100;
	PC_func = PC_f_nextpc;
	
//	logmsg("imm: %x, NEXTPC: %x\r\n", imm, NEXTPC);
}

