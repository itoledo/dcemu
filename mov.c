//  mov.cpp
#include "main.h"

OPCODE(mov0) // MOV #imm,Rn: imm -> sign extension-> Rn (1110nnnn iiiiiiii)
{
	short n = (arg >> 8) & 0x0F;
	DWORD imm = SignExtend8(arg & 0xFF);

	R(n) = imm;

	PC += 2;

#ifdef ASM_DEBUG
	logmsg("mov0: imm=%x,%d, r[%d]=%x,%d\r\n", imm, imm, n, R(n), R(n));
#endif
}

OPCODE(movw1) // MOV.W @(disp,PC),Rn: (disp*2 + PC + 4) -> sign extension -> Rn (1001nnnn dddddddd)
{
	short n = (arg >> 8) & 0x0F;
	BYTE disp = (arg & 0x00FF);
//	long source = disp*2 + PC + 4 - (mem_base);
	DWORD source = disp*2 + PC + 4;
	WORD w;

//	R(n) = SignExtend16(*(WORD *) &memoria[source]);

	ReadMemoryW(source, &w);
	R(n) = SignExtend16(w);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw1: source=%x, r[%d]=%x,%d\r\n", source, n, R(n), R(n));
#endif
}

OPCODE(movl2) // MOV.L @(disp, PC), Rn: (disp*4 + PC & H'FFFFFFFC + 4) -> Rn
{
	short n = (arg >> 8) & 0x0F;
	WORD disp = (arg & 0xFF);
	DWORD source = disp*4 + (PC & 0xFFFFFFFC) + 4;

	ReadMemoryL(source, (DWORD *) &R(n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl2: source=%x, r[%d]=%x,%d\r\n", source, n, R(n), R(n));
#endif
}

OPCODE(mov3) // MOV Rm,Rn: Rm -> Rn (0110nnnn mmmm0011)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) = R(m);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("mov3: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb4) // MOV.B Rm, @Rn: Rm -> (Rn) (0010nnnn mmmm0000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE valor = (BYTE) (R(m) & 0xFF);

	WriteMemoryB(R(n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb4: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movw5) // MOV.W Rm, @Rn: Rm -> (Rn) (0010nnnn mmmm0001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	WORD valor = (WORD) (R(m) & 0xFFFF);

	WriteMemoryW(R(n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw5: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movl6) // MOV.L Rm, @Rn: Rm -> (Rn) (0010nnnnmmmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

/*	WriteMemoryW(R(n), (WORD) (R(m) & 0xFFFF));
	WriteMemoryW(R(n) + 2, (WORD) ((R(m) >> 16) & 0xFFFF)); */
	WriteMemoryL(R(n), (DWORD *) &R(m));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl6: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb7) // MOV.B @Rm, Rn: (Rm) -> sign extension -> Rn (0110nnnnmmmm0000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE c;

	ReadMemoryB(R(m), &c);

	R(n) = SignExtend8(c);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb7: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movw8) // MOV.W @Rm, Rn: (Rm) -> sign extension -> Rn (0110nnnnmmmm0001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	WORD c;

	ReadMemoryW(R(m), &c);

	R(n) = SignExtend16(c);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw8: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movl9) // MOV.L @Rm,Rn: (Rm) -> Rn (0110nnnnmmmm0010)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &R(n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl9: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb10) // MOV.B Rm, @-Rn  (0010nnnn mmmm0100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE valor = (BYTE) (R(m) & 0xFF);

	R(n)--;
	WriteMemoryB(R(n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb10: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movw11) // MOV.W Rm, @-Rn (0010nnnn mmmm0101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	WORD valor = (WORD) (R(m) & 0xFFFF);

	R(n) -= 2;
	WriteMemoryW(R(n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw11: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movl12) // MOV.L Rm, @-Rn : Rn - 4 -> Rn, Rm -> (Rn) (0010nnnn mmmm0110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) -= 4;
	WriteMemoryL(R(n), (DWORD *) &R(m));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl12: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb13) // MOV.B @Rm+,Rn: (Rm) -> Rn, Rm + 1 -> Rm (0110nnnnmmmm0100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE b;

	ReadMemoryB(R(m), (BYTE *) &b);

	R(n) = SignExtend8(b);

	if (n != m)
		R(m) += 1;

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb13: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movw14) // MOV.W @Rm+,Rn: (Rm) -> sign ext -> Rn, Rm + 2 -> Rm (0110nnnnmmmm0101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	WORD w;

	ReadMemoryW(R(m), &w);

	R(n) = SignExtend16(w);

	if (n != m)
		R(m) += 2;

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw14: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movl15) // MOV.L @Rm+, Rn: (Rm) -> Rn, Rm + 4 -> Rm (0110nnnnmmmm0110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	ReadMemoryL(R(m), (DWORD *) &R(n));

	if (n != m)
		R(m) += 4;

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl15: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb16) // MOV.B R0, @(disp, Rn) (10000000 nnnniiii)
{
	short n = (arg >> 4) & 0x0F;
	BYTE i = arg & 0x0F;
	BYTE valor = (BYTE) (R(0) & 0xFF);

	WriteMemoryB(R(n) + i, &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb16: r[%d]=%x,%d r[%d]=%x,%d\r\n", 0, R(0), R(0), n, R(n), R(n));
#endif
}

OPCODE(movw17) // MOV.W R0, @(disp, Rn) (10000001 nnnniiii)
{
	short n = (arg >> 4) & 0x0F;
	BYTE i = arg & 0x0F;
	WORD valor = (WORD) (R(0) & 0xFFFF);

	WriteMemoryW(R(n) + i*2, &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw17: r[%d]=%x,%d r[%d]=%x,%d\r\n", 0, R(0), R(0), n, R(n), R(n));
#endif
}

OPCODE(movl18) // MOV.L Rm, @(disp, Rn) : Rm -> (disp x 4 + Rn) (0001nnnn mmmmdddd)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	DWORD disp = arg & 0x0F;
	DWORD valor = R(m);
	disp <<= 2;
	disp += R(n);
	
	WriteMemoryL(disp, &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl18: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb19) // MOV.B @(disp, Rm), R0 (10000100 mmmmiiii)
{
	short m = (arg >> 4) & 0x0F;
	BYTE i = arg & 0x0F;
	BYTE b;
	
	memread(R(m) + i, &b, sizeof(BYTE));
	
	R(0) = SignExtend8(b);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb19: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), 0, R(0), R(0));
#endif
}

OPCODE(movw20) // MOV.W @(disp, Rm), R0 : (disp x 2 + Rm) -> sign ext -> R0 (10000101 mmmmdddd)
{
	short m = (arg >> 4) & 0x0F;
	BYTE d = arg & 0x0F;
	WORD w;

	ReadMemoryW(d * 2 + R(m), &w);

	R(0) = SignExtend16(w);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw20: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), 0, R(0), R(0));
#endif
}

OPCODE(movl21) // MOV.L @(disp, Rm), Rn : (disp x 4 + Rm) -> Rn (0101nnnn mmmmdddd)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE disp = (arg & 0x0F);

	ReadMemoryL(R(m) + disp * 4, (DWORD *) &R(n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl21: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb22) // MOV.B Rm, @(R0, Rn) : Rm -> (R0 + Rn) (0000nnnnmmmm0100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE valor = (BYTE) (R(m) & 0xFF);

	WriteMemoryB(R(0) + R(n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb22: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movw23) // MOV.W Rm, @(R0, Rn) : Rm -> (R0 + Rn) (0000nnnnmmmm0101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	WORD valor = (WORD) (R(m) & 0xFFFF);

	WriteMemoryW(R(0) + R(n), &valor);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw23: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movl24) // MOV.L Rm, @(R0, Rn) : Rm -> (R0 + Rn) (0000nnnnmmmm0110)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	WriteMemoryL(R(0) + R(n), (DWORD *) &R(m));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl24: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb25) // MOV.B @(R0, Rm), Rn : (R0 + Rm) -> sign ext -> Rn (0000nnnn mmmm1100)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	BYTE b;

	ReadMemoryB(R(0) + R(m), &b);

	R(n) = SignExtend8(b);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movb25: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movw26) // MOV.W @(R0, Rm), Rn : (R0 + Rm) -> sign ext -> Rn (0000nnnn mmmm1101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
	WORD w;

	ReadMemoryW(R(0) + R(m), &w);

	R(n) = SignExtend16(w);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movw26: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movl27) // MOV.L @(R0, Rm), Rn : (R0 + Rm) -> Rn (0000nnnn mmmm1101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	ReadMemoryL(R(0) + R(m), &R(n));

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("movl27: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(movb31) // MOV.B @(disp, GBR), R0 (11000100 iiiiiiii)
{
	BYTE disp = arg & 0xFF;
	DWORD addr = GBR + disp;
	BYTE t;
	
	memread(addr, &t, sizeof(BYTE));
 	R(0) = SignExtend8(t);	

	PC += 2;
}

OPCODE(movl33) // MOV.L @(disp, GBR), R0 (11000110 iiiiiiii)
{
	BYTE disp = arg & 0xFF;
	DWORD addr = GBR + disp * 4;
	
	memread(addr, &R(0), sizeof(DWORD));

	PC += 2;
}

OPCODE(mova34) // MOVA @(disp, PC), R0 : disp x 4 + PC & 0xFFFFFFFC + 4 -> R0 (11000111 dddddddd)
{
	DWORD disp = arg & 0x00FF;

	disp *= 4;
	disp += ((PC + 4) & 0xFFFFFFFC);

	R(0) = disp;

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
	short n = (arg >> 8) & 0x0F;

	if (IS_SR_T())
		R(n) = 1;
	else
		R(n) = 0;

	PC += 2;
}

OPCODE(swapb36) // SWAP.B Rm, Rn (0110nnnn mmmm1000)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

//	R(n) = ((R(m) >> 16) & 0xFFFF) | ((R(m) << 16) & 0xFFFF0000);
	R(n) = (R(m) & 0xFFFF0000) | ((R(m) >> 8) & 0x000000FF) | ((R(m) << 8) & 0x0000FF00);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("swapb36: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(swapw37) // SWAP.W Rm, Rn (0110nnnn mmmm1001)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;

	R(n) = ((R(m) >> 16) & 0xFFFF) | ((R(m) << 16) & 0xFFFF0000);

	PC += 2;

#ifdef DEBUG_MOV
	logmsg("swapw37: r[%d]=%x,%d r[%d]=%x,%d\r\n", m, R(m), R(m), n, R(n), R(n));
#endif
}

OPCODE(xtrct38) // XTRCT Rm, Rn (0010nnnn mmmm1101)
{
	short n = (arg >> 8) & 0x0F;
	short m = (arg >> 4) & 0x0F;
    DWORD x;

#ifdef DEBUG_MOV
    logmsg("xtrct: antes: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        m, R(m), R(m),
        n, R(n), R(n));
#endif

    x = ((R(n) >> 16) & 0xFFFF) | ((R(m) << 16) & 0xFFFF0000);

    R(n) = x;    

	PC += 2;

#ifdef DEBUG_MOV
    logmsg("xtrct: despues: r[%d]=%x,%d r[%d]=%x,%d\r\n",
        m, R(m), R(m),
        n, R(n), R(n));
#endif
}

OPCODE(movw29) // MOV.W R0, @(disp, GBR)
{
	BYTE disp = (arg & 0x00FF);
	DWORD source = (disp << 1) + GBR;
	WORD valor = (WORD) (R(0) & 0xFFFF);
	
	WriteMemoryW(source, &valor);

	PC += 2;
}

OPCODE(movl30) // MOV.L R0, @(disp, GBR)
{
	BYTE disp = (arg & 0x00FF);
	DWORD source = (disp << 2) + GBR;
	
	WriteMemoryL(source, &R(0));

	PC += 2;
}

