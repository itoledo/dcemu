#ifndef _BRANCH_H_
#define _BRANCH_H_

OPCODE(bf); 		// BF label (10001011 ssssssss)
OPCODE(bra); 		// BRA label (1010dddd dddddddd)
OPCODE(braf); 		// BRAF label (0000dddd 00100011)
OPCODE(bsr108); 	// BSR label (1011dddd dddddddd)
OPCODE(bsrf109); 	// BSRF Rn (0000nnnn 00000011)
OPCODE(jmp110); 	// JMP @Rn (0100nnnn 00101011)
OPCODE(jsr111); 	// JSR @Rn (0100nnnn 00001011)
OPCODE(rts112); 	// RTS (00000000 00001011)
OPCODE(bfs); 		// 10001111 dddddddd
OPCODE(bt104); 		// 10001001 dddddddd
OPCODE(bts105); 	// 10001101 dddddddd

#endif // _BRANCH_H_
