//  mov.cpp
#include "main.h"

#ifdef _fast_interpreter_
#define mv_n jumptable.jit_array[jumptable.current_pos].cache.n
#define mv_m  jumptable.jit_array[jumptable.current_pos].cache.m
#define m_disp jumptable.jit_array[jumptable.current_pos].cache.d_disp
#define m_wdisp  jumptable.jit_array[jumptable.current_pos].cache.w_disp
#define m_bdisp jumptable.jit_array[jumptable.current_pos].cache.b_disp
#else
short mv_n=0;
short mv_m=0;
DWORD m_disp=0;
BYTE m_bdisp=0;
WORD m_wdisp=0;
#endif

OPCODE(mov0) // MOV #imm,Rn: imm -> sign extension-> Rn (1110nnnn iiiiiiii)
{
	mv_n = (arg >> 8) & 0x0F;
	m_disp  = SignExtend8(arg & 0xFF);

	R(mv_n) = m_disp;

	PC += 2;

#ifdef ASM_DEBUG
	logmsg("mov0: imm=%x,%d, r[%d]=%x,%d\r\n", imm, imm, n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw1) // MOV.W @(disp,PC),Rn: (disp*2 + PC + 4) -> sign extension -> Rn (1001nnnn dddddddd)
{
	mv_n = (arg >> 8) & 0x0F;
	m_bdisp = (arg & 0x00FF);
//	long source = disp*2 + PC + 4 - (mem_base);
	m_disp = m_bdisp*2 + PC + 4;
	WORD w;

//	R(mv_n) = SignExtend16(*(WORD *) &memoria[source]);

	ReadMemoryW(m_disp, &w);
	R(mv_n) = SignExtend16(w);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw1: source=%x, r[%d]=%x,%d\r\n", source, n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl2) // MOV.L @(disp, PC), Rn: (disp*4 + PC & H'FFFFFFFC + 4) -> Rn
{
	mv_n = (arg >> 8) & 0x0F;
	m_wdisp = (arg & 0xFF);
	m_disp = m_wdisp*4 + (PC & 0xFFFFFFFC) + 4;

	ReadMemoryL(m_disp, (DWORD *) &R(mv_n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl2: source=%x, r[%d]=%x,%d\r\n", source, n, R(mv_n), R(mv_n));
#endif
}

OPCODE(mov3) // MOV Rm,Rn: Rm -> Rn (0110nnnn mmmm0011)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	R(mv_n) = R(mv_m);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("mov3: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb4) // MOV.B Rm, @Rn: Rm -> (Rn) (0010nnnn mmmm0000)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	BYTE valor = (BYTE) (R(mv_m) & 0xFF);

	WriteMemoryB(R(mv_n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb4: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw5) // MOV.W Rm, @Rn: Rm -> (Rn) (0010nnnn mmmm0001)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	WORD valor = (WORD) (R(mv_m) & 0xFFFF);

	WriteMemoryW(R(mv_n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw5: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl6) // MOV.L Rm, @Rn: Rm -> (Rn) (0010nnnnmmmm0010)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

/*	WriteMemoryW(R(mv_n), (WORD) (R(mv_m) & 0xFFFF));
	WriteMemoryW(R(mv_n) + 2, (WORD) ((R(mv_m) >> 16) & 0xFFFF)); */
	WriteMemoryL(R(mv_n), (DWORD *) &R(mv_m));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl6: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb7) // MOV.B @Rm, Rn: (Rm) -> sign extension -> Rn (0110nnnnmmmm0000)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	BYTE c;

	ReadMemoryB(R(mv_m), &c);

	R(mv_n) = SignExtend8(c);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb7: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw8) // MOV.W @Rm, Rn: (Rm) -> sign extension -> Rn (0110nnnnmmmm0001)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	WORD c;

	ReadMemoryW(R(mv_m), &c);

	R(mv_n) = SignExtend16(c);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw8: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl9) // MOV.L @Rm,Rn: (Rm) -> Rn (0110nnnnmmmm0010)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	ReadMemoryL(R(mv_m), (DWORD *) &R(mv_n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl9: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb10) // MOV.B Rm, @-Rn  (0010nnnn mmmm0100)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	BYTE valor = (BYTE) (R(mv_m) & 0xFF);

	R(mv_n)--;
	WriteMemoryB(R(mv_n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb10: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw11) // MOV.W Rm, @-Rn (0010nnnn mmmm0101)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	WORD valor = (WORD) (R(mv_m) & 0xFFFF);

	R(mv_n) -= 2;
	WriteMemoryW(R(mv_n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw11: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl12) // MOV.L Rm, @-Rn : Rn - 4 -> Rn, Rm -> (Rn) (0010nnnn mmmm0110)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	R(mv_n) -= 4;
	WriteMemoryL(R(mv_n), (DWORD *) &R(mv_m));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl12: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb13) // MOV.B @Rm+,Rn: (Rm) -> Rn, Rm + 1 -> Rm (0110nnnnmmmm0100)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	BYTE b;

	ReadMemoryB(R(mv_m), (BYTE *) &b);

	R(mv_n) = SignExtend8(b);

	if (mv_n != mv_m)
		R(mv_m) += 1;

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb13: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw14) // MOV.W @Rm+,Rn: (Rm) -> sign ext -> Rn, Rm + 2 -> Rm (0110nnnnmmmm0101)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	WORD w;

	ReadMemoryW(R(mv_m), &w);

	R(mv_n) = SignExtend16(w);

	if (mv_n != mv_m)
		R(mv_m) += 2;

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw14: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl15) // MOV.L @Rm+, Rn: (Rm) -> Rn, Rm + 4 -> Rm (0110nnnnmmmm0110)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	ReadMemoryL(R(mv_m), (DWORD *) &R(mv_n));

	if (mv_n != mv_m)
		R(mv_m) += 4;

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl15: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb16) // MOV.B R0, @(disp, Rn) (10000000 nnnniiii)
{
	mv_n = (arg >> 4) & 0x0F;
	m_bdisp = arg & 0x0F;
	BYTE valor = (BYTE) (R(0) & 0xFF);

	WriteMemoryB(R(mv_n) + m_bdisp, &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb16: r[%d]=%x,%d r[%d]=%x,%d\r\n", 0, R(0), R(0), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw17) // MOV.W R0, @(disp, Rn) (10000001 nnnniiii)
{
	mv_n = (arg >> 4) & 0x0F;
	m_bdisp  = arg & 0x0F;
	WORD valor = (WORD) (R(0) & 0xFFFF);

	WriteMemoryW(R(mv_n) + m_bdisp*2, &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw17: r[%d]=%x,%d r[%d]=%x,%d\r\n", 0, R(0), R(0), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl18) // MOV.L Rm, @(disp, Rn) : Rm -> (disp x 4 + Rn) (0001nnnn mmmmdddd)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	m_disp = arg & 0x0F;
	DWORD valor = R(mv_m);
	m_disp <<= 2;
	m_disp += R(mv_n);
	
	WriteMemoryL(m_disp, &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl18: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb19) // MOV.B @(disp, Rm), R0 (10000100 mmmmiiii)
{
	mv_m = (arg >> 4) & 0x0F;
	m_bdisp= arg & 0x0F;
	BYTE b;
	
	memread(R(mv_m) + m_bdisp, &b, sizeof(BYTE));
	
	R(0) = SignExtend8(b);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb19: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), 0, R(0), R(0));
#endif
}

OPCODE(movw20) // MOV.W @(disp, Rm), R0 : (disp x 2 + Rm) -> sign ext -> R0 (10000101 mmmmdddd)
{
	mv_m = (arg >> 4) & 0x0F;
	m_bdisp = arg & 0x0F;
	WORD w;

	ReadMemoryW(m_bdisp * 2 + R(mv_m), &w);

	R(0) = SignExtend16(w);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw20: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), 0, R(0), R(0));
#endif
}

OPCODE(movl21) // MOV.L @(disp, Rm), Rn : (disp x 4 + Rm) -> Rn (0101nnnn mmmmdddd)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	m_disp = (arg & 0x0F);

	ReadMemoryL(R(mv_m) + m_disp * 4, &R(mv_n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl21: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb22) // MOV.B Rm, @(R0, Rn) : Rm -> (R0 + Rn) (0000nnnnmmmm0100)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	BYTE valor = (BYTE) (R(mv_m) & 0xFF);

	WriteMemoryB(R(0) + R(mv_n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb22: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw23) // MOV.W Rm, @(R0, Rn) : Rm -> (R0 + Rn) (0000nnnnmmmm0101)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	WORD valor = (WORD) (R(mv_m) & 0xFFFF);

	WriteMemoryW(R(0) + R(mv_n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw23: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl24) // MOV.L Rm, @(R0, Rn) : Rm -> (R0 + Rn) (0000nnnnmmmm0110)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	WriteMemoryL(R(0) + R(mv_n), (DWORD *) &R(mv_m));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl24: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb25) // MOV.B @(R0, Rm), Rn : (R0 + Rm) -> sign ext -> Rn (0000nnnn mmmm1100)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	BYTE b;

	ReadMemoryB(R(0) + R(mv_m), &b);

	R(mv_n) = SignExtend8(b);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb25: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw26) // MOV.W @(R0, Rm), Rn : (R0 + Rm) -> sign ext -> Rn (0000nnnn mmmm1101)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
	WORD w;

	ReadMemoryW(R(0) + R(mv_m), &w);

	R(mv_n) = SignExtend16(w);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw26: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movl27) // MOV.L @(R0, Rm), Rn : (R0 + Rm) -> Rn (0000nnnn mmmm1101)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	ReadMemoryL(R(0) + R(mv_m), &R(mv_n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl27: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movb31) // MOV.B @(disp, GBR), R0 (11000100 iiiiiiii)
{
	m_bdisp = arg & 0xFF;
	DWORD addr = GBR + m_bdisp;
	BYTE t;
	
	memread(addr, &t, sizeof(BYTE));
 	R(0) = SignExtend8(t);	

	PC += 2;
}

OPCODE(movl33) // MOV.L @(disp, GBR), R0 (11000110 iiiiiiii)
{
	m_bdisp = arg & 0xFF;
	DWORD addr = GBR + m_bdisp * 4;
	
	memread(addr, &R(0), sizeof(DWORD));

	PC += 2;
}

OPCODE(mova34) // MOVA @(disp, PC), R0 : disp x 4 + PC & 0xFFFFFFFC + 4 -> R0 (11000111 dddddddd)
{
	m_disp = arg & 0x00FF;

	m_disp *= 4;
	m_disp += ((PC + 4) & 0xFFFFFFFC);

	R(0) =m_disp;

/*
//	disp *= 4;
//	disp += ((PC + 4) & 0xFFFFFFFC);

	R(0) = ((PC & 0xFFFFFFFC) + 4 + (disp << 2));
*/

	PC += 2;

#ifdef DEBUG_MOV_MOVA
	logmsg("mova: r[0]=%x\r\n", disp);
#endif
}

OPCODE(movt35) // MOVT Rn : T -> Rn (0000nnnn 00101001)
{
	mv_n = (arg >> 8) & 0x0F;

	if (IS_SR_T())
		R(mv_n) = 1;
	else
		R(mv_n) = 0;

	PC += 2;
}

OPCODE(swapb36) // SWAP.B Rm, Rn (0110nnnn mmmm1000)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

//	R(mv_n) = ((R(mv_m) >> 16) & 0xFFFF) | ((R(mv_m) << 16) & 0xFFFF0000);
	R(mv_n) = (R(mv_m) & 0xFFFF0000) | ((R(mv_m) >> 8) & 0x000000FF) | ((R(mv_m) << 8) & 0x0000FF00);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("swapb36: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(swapw37) // SWAP.W Rm, Rn (0110nnnn mmmm1001)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;

	R(mv_n) = ((R(mv_m) >> 16) & 0xFFFF) | ((R(mv_m) << 16) & 0xFFFF0000);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("swapw37: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(mv_m), R(mv_m), n, R(mv_n), R(mv_n));
#endif
}

OPCODE(xtrct38) // XTRCT Rm, Rn (0010nnnn mmmm1101)
{
	mv_n = (arg >> 8) & 0x0F;
	mv_m = (arg >> 4) & 0x0F;
    DWORD x;

#ifdef DEBUG_MOV
    logmsg("xtrct: antes: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        m, R(mv_m), R(mv_m),
        n, R(mv_n), R(mv_n));
#endif

    x = ((R(mv_n) >> 16) & 0xFFFF) | ((R(mv_m) << 16) & 0xFFFF0000);

    R(mv_n) = x;    

	PC += 2;

#ifdef DEBUG_MOV
    logmsg("xtrct: despues: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        m, R(mv_m), R(mv_m),
        n, R(mv_n), R(mv_n));
#endif
}

OPCODE(movw29) // MOV.W R0, @(disp, GBR)
{
	m_bdisp = (arg & 0x00FF);
	DWORD source = (m_bdisp << 1) + GBR;
	WORD valor = (WORD) (R(0) & 0xFFFF);
	
	WriteMemoryW(source, &valor);

	PC += 2;
}

OPCODE(movl30) // MOV.L R0, @(disp, GBR)
{
	m_bdisp = (arg & 0x00FF);
	DWORD source = (m_bdisp << 2) + GBR;
	
	WriteMemoryL(source, &R(0));

	PC += 2;
}

