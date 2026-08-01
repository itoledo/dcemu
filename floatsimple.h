/*
	FPSCR.DN: con el bit puesto, todo desnormalizado -- de entrada o de salida --
	sale aplastado a cero con su signo. Manual del SH-4, 6.2.3. Lo usan los
	handlers de este archivo y los de floatgraph.c y dcopcodes.c.
*/
float  fpu_dn_s(float x);
double fpu_dn_d(double x);

OPCODE(fldi0170); // FLDI0 FRn (1111nnnn 10001101)
OPCODE(fldi1171); // FLDI1 FRn (1111nnnn 10011101)
OPCODE(fmov172); // FMOV FRm, FRn : FRm -> FRn (1111nnnn mmmm1100)
OPCODE(fmovs173); // FMOV.S @Rm, FRn : (Rm) -> FRn (1111nnnn mmmm1000)
OPCODE(fmovs174); // FMOV.S @(R0, Rm), FRn
OPCODE(fmovs175); // FMOV.S @Rm+, FRn (1111nnnn mmmm1001)
OPCODE(fmovs176); // FMOV.S FRm, @Rn : FRm -> (Rn) (1111nnnn mmmm1010)
OPCODE(fmovs177); // FMOV.S FRm, @-Rn (1111nnnn mmmm1011)
OPCODE(fmovs178); // FMOV.S FRm, @(R0, Rn) (1111nnnn mmmm0111)
OPCODE(fmov179); // FMOV DRm, DRn (1111nnn0 mmm01100)
OPCODE(fmov180); //  FMOV @Rm, DRn (1111nnn0 mmmm1000)
OPCODE(fmov181); // FMOV @(R0, Rm), DRn (1111nnn0 mmmm0110)
OPCODE(fmov182); // FMOV @Rm+, DRn (1111nnn0 mmmm1001)
OPCODE(fmov183); // FMOV DRm, @Rn (1111nnnn mmm01010)
OPCODE(fmov184); // FMOV DRm, @-Rn (1111nnnn mmm01011)
OPCODE(fmov185); // FMOV DRm, @(R0, Rn) (1111nnnn mmm00111)
OPCODE(flds186); // FLDS FRm, FPUL (1111mmmm 00011101)
OPCODE(fsts187); // FSTS FPUL, FRn (1111nnnn 00001101)
OPCODE(fcmpeq190); // FCMP/EQ FRm, FRn (1111nnnn mmmm0100)
OPCODE(fcmpgt191); // FCMP/GT FRm, FRn (1111nnnn mmmm0101)
OPCODE(fdiv192); // FDIV FRm, FRn : FRn/FRm -> FRn (1111nnnn mmmm0011)
OPCODE(fmul195); // FMUL FRn * FRm -> FRn (1111nnnn mmmm0010)
OPCODE(fsqrt197); // FSQRT FRn (1111nnnn 01101101)
OPCODE(fadd189); // FADD FRm, FRn : FRn + FRm -> FRn (1111nnnn mmmm0000)
OPCODE(float193); // FLOAT FPUL, FRn : (float) FPUL -> FRn (1111nnnn 00101101)
OPCODE(fmac194); // FMAC FR0, FRm, FRn (1111nnnn mmmm1110)
OPCODE(fneg196); // FNEG FRn (1111nnnn 01001101)
OPCODE(fsub198); // FSUB FRm, FRn (1111nnnn mmmm0001)
OPCODE(ftrc199); // FTRC FRm, FPUL : (long) FRm -> FPUL (1111mmmm00111101)
OPCODE(fabs188); // FABS FRn (1111nnnn 01011101)
OPCODE(fabs200); // FABS DRn (1111nnn0 01011101)
OPCODE(fadd201); // FADD DRm, DRn (1111nnn0 mmm00000)
OPCODE(fcmpeq202); // FCMP/EQ DRm, DRn (1111nnn0 mmm00100)
OPCODE(fcmpgt203); // FCMP/GT DRm, DRn (1111nnn0 mmm00101)
OPCODE(fdiv204); // FDIV DRm, DRn (1111nnn0 mmm00011)
OPCODE(fcnvds205); // FCNVDS DRm, FPUL (1111mmm0 10111101)
OPCODE(fcnvsd206); // FCNVSD FPUL, DRn (1111nnn0 10101101)
OPCODE(float207); // FLOAT FPUL, DRn (1111nnn0 00101101)
OPCODE(fmul208); // FMUL DRm, DRn (1111nnn0 mmm00010)
OPCODE(fneg209); // FNEG DRn (1111nnn0 01001101)
OPCODE(fsqrt210); // FSQRT DRn (1111nnn0 00111101)
OPCODE(fsub211); // FSUB DRm, DRn (1111nnn0 mmm00001)
OPCODE(ftrc212); // FTRC DRm, FPUL (1111mmm0 00011101)

