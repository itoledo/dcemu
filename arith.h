#ifndef _ARITH_H_
#define _ARITH_H_

OPCODE(add39); 		// ADD Rm, Rn : Rn + Rm -> Rn (0011nnnn mmmm1100)
OPCODE(add40); 		// ADD #imm, Rn (0111nnnn ssssssss)
OPCODE(addc41); 	// ADDC Rm, Rn (0011nnnn mmmm1110)
OPCODE(dt); 		// DT Rn (0100nnnn 00010000)
OPCODE(cmpeq43); 	// CMP/EQ #imm, R0 (10001000 iiiiiiii)
OPCODE(cmpeq44); 	// CMP/EQ Rm, Rn (0011nnnn mmmm0000)
OPCODE(cmphs45); 	// CMP/HS Rm, Rn (0011nnnn mmmm0010)
OPCODE(cmpge46); 	// CMP/GE Rm, Rn (0011nnnn mmmm0011)
OPCODE(cmphi47); 	// CMP/HI Rm, Rn (0011nnnn mmmm0110)
OPCODE(cmpgt48); 	// CMP/GT Rm, Rn
OPCODE(cmppz49); 	// CMP/PZ Rn (0100nnnn 00010001)
OPCODE(cmppl50); 	// CMP/PL Rn (0100nnnn 00010101)
OPCODE(cmpstr51); 	// CMP/STR Rm, Rn (0010nnnn mmmm1100)
OPCODE(div1s52); 	// DIV1 Rm, Rn (0011nnnn mmmm0100)
OPCODE(div0s53); 	// DIV0S Rm, Rn (0010nnnn mmmm0111)
OPCODE(div0u54); 	// DIV0U (00000000 00011001)
OPCODE(dmulsl55); 	// DMULS.L Rm, Rn (0011nnnn mmmm1101)
OPCODE(dmulul56); 	// DMULU.L Rm, Rn (0011nnnn mmmm0101)
OPCODE(extsb58); 	// EXTS.B Rm, Rn (0110nnnn mmmm1110)
OPCODE(extsw59); 	// EXTS.W Rm, Rn (0110nnnn mmmm1111)
OPCODE(extub60); 	// EXTU.B Rm, Rn
OPCODE(extuw61); 	// EXTU.W Rm, Rn (0110nnnn mmmm1101)
OPCODE(macl62);		// MAC.L @Rm+, @Rn+	(0000nnnn mmmm1111)
OPCODE(mulsw65); 	// MULS.W Rm, Rn (0010nnnn mmmm1111)
OPCODE(muluw66); 	// MULU.W Rm, Rn (0010nnnn mmmm1110)
OPCODE(neg67); 		// NEG Rm, Rn : 0 - Rm -> Rn (0110nnnn mmmm1011)
OPCODE(negc68); 	// NEGC Rm, Rn (0110nnnn mmmm1010)
OPCODE(sub69); 		// SUB Rm, Rn : Rn - Rm -> Rn (0011nnnn mmmm1000)
OPCODE(subc70); 	// SUBC Rm, Rn : Rn - Rm - T -> Rn (0011nnnn mmmm1010)
OPCODE(mull); 		// Rn x Rm -> MACL (0000nnnn mmmm0111)

#endif // _ARITH_H_
