OPCODE(fmov221); // FMOV DRm, XDn			(1111nnn1 mmm01100)
OPCODE(fmov222); // FMOV XDm, DRn			(1111nnn0 mmm11100)
OPCODE(fmov223); // FMOV XDm, XDn			(1111nnn1 mmm11100)
OPCODE(fmov224); // FMOV @Rm, XDn			(1111nnn1 mmmm1000)
OPCODE(fmov225); // FMOV @Rm+, XDn			(1111nnn1 mmmm1001)
OPCODE(fmov226); // FMOV @(R0, Rm), XDn		(1111nnn1 mmmm0110)
OPCODE(fmov227); // FMOV XDm, @Rn			(1111nnnn mmm11010)
OPCODE(fmov228); // FMOV XDm, @-Rn			(1111nnnn mmm11011)
OPCODE(fmov229); // FMOV XDm, @(R0, Rn)		(1111nnnn mmm10111)
OPCODE(ftrv); // FTRV XMTRX, FVn (1111nnn 011111101)
OPCODE(fsrra); // FSRRA FRn (1111nnnn 01111101)
OPCODE(frchg232);
OPCODE(fschg233);

