#ifndef _SHIFT_H_
#define _SHIFT_H_

OPCODE(rotl86);		// ROTL Rn : T <- Rn <- MSB (0100nnnn 00000100)
OPCODE(rotr87);		// ROTR Rn (0100nnnn 00000101)
OPCODE(rotcl88);	// ROTCL Rn (0100nnnn 00100100)
OPCODE(rotcr89);	// ROTCR Rn (0100nnnn 00100101)
OPCODE(shad90);		// SHAD Rm, Rn (0100nnnn mmmm1100)
OPCODE(shar92);		// SHAR Rn : MSB -> Rn -> T (0100nnnn 00100001)
OPCODE(shld93);		// SHLD Rm, Rn (0100nnnn mmmm1101)
OPCODE(shll94);		// SHLL Rn : T <- Rn <- 0 (0100nnnn 00000000)
OPCODE(shlr95);		// SHLR Rn : 0 -> Rn -> T (0100nnnn 00000001)
OPCODE(shll2);		// SHLL2 Rn: Rn << 2 -> Rn (0100nnnn 00001000)
OPCODE(shlr2);		// SHLR2 Rn: Rn >> 2 -> Rn (0100nnnn 00001001)
OPCODE(shll8);		// SHLL8 Rn (0100nnnn 00011000)
OPCODE(shlr8);		// SHLR8 Rn (0100nnnn 00011001)
OPCODE(shll16);		// SHLL16 Rn (0100nnnn 00101000)
OPCODE(shlr16);		// SHLR16 Rn (0100nnnn 00101001)

#endif // _SHIFT_H_
