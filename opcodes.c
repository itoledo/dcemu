#include "main.h"
#include "mov.h"
#include "branch.h"
#include "logic.h"
#include "arith.h"
#include "shift.h"
#include "syscontrol.h"
#include "floatsimple.h"
#include "floatcontrol.h"
#include "dcopcodes.h"
#include "floatgraph.h"
#include "opcodes.h"

int oplist[65536];

struct st_cmd opcodes[] =
{
	// Fixed Point Transfer Instructions
	// página 140
	{ 0xE000, 0xF000, "MOV",	OP_T_IMM_RN,			mov0	},			// 1110nnnn iiiiiiii
	{ 0x9000, 0xF000, "MOV.W",	OP_T_AT_DISP_PC_RN,		movw1	},			// 1001nnnn dddddddd
	{ 0xD000, 0xF000, "MOV.L",	OP_T_AT_DISP_PC_RN,		movl2	},		// 1101nnnn dddddddd
	{ 0x6003, 0xF00F, "MOV",	OP_T_RM_RN,				mov3	},			// 0110nnnn mmmm0011
	{ 0x2000, 0xF00F, "MOV.B",	OP_T_RM_AT_RN,			movb4	},		// 0010nnnn mmmm0000
	{ 0x2001, 0xF00F, "MOV.W",	OP_T_RM_AT_RN,			movw5	},		// 0010nnnn mmmm0001
	{ 0x2002, 0xF00F, "MOV.L",	OP_T_RM_AT_RN,			movl6	},		// 0010nnnn mmmm0010
	{ 0x6000, 0xF00F, "MOV.B",	OP_T_AT_RM_RN,			movb7	},		// 0110nnnn mmmm0000
	{ 0x6001, 0xF00F, "MOV.W",	OP_T_AT_RM_RN,			movw8	},		// 0110nnnn mmmm0001
	{ 0x6002, 0xF00F, "MOV.L",	OP_T_AT_RM_RN,			movl9	},		// 0110nnnn mmmm0010
	{ 0x2004, 0xF00F, "MOV.B",	OP_T_RM_AT_MIN_RN,		movb10	}, // 10	// 0010nnnn mmmm0100
	{ 0x2005, 0xF00F, "MOV.W",	OP_T_RM_AT_MIN_RN,		movw11	},		// 0010nnnn mmmm0101
	{ 0x2006, 0xF00F, "MOV.L",	OP_T_RM_AT_MIN_RN,		movl12	},		// 0010nnnn mmmm0110
	{ 0x6004, 0xF00F, "MOV.B",	OP_T_AT_RM_PLUS_RN,		movb13	},		// 0110nnnn mmmm0100
	{ 0x6005, 0xF00F, "MOV.W",	OP_T_AT_RM_PLUS_RN,		movw14	},		// 0110nnnn mmmm0101
	{ 0x6006, 0xF00F, "MOV.L",	OP_T_AT_RM_PLUS_RN,		movl15	}, // 15	// 0110nnnn mmmm0110
	{ 0x8000, 0xFF00, "MOV.B",	OP_T_R0_AT_DISP_RN,		movb16	},
	{ 0x8100, 0xFF00, "MOV.W",	OP_T_R0_AT_DISP_RN,		movw17	},
	{ 0x1000, 0xF000, "MOV.L",	OP_T_RM_AT_DISP_RN,		movl18	},
	{ 0x8400, 0xFF00, "MOV.B",	OP_T_AT_DISP_RM_R0,		movb19	},
	{ 0x8500, 0xFF00, "MOV.W",	OP_T_AT_DISP_RM_R0,		movw20 }, // 20
	{ 0x5000, 0xF000, "MOV.L",	OP_T_AT_DISP_RM_RN,		movl21 },
	{ 0x0004, 0xF00F, "MOV.B",	OP_T_RM_AT_R0_RN,		movb22 },
	{ 0x0005, 0xF00F, "MOV.W",	OP_T_RM_AT_R0_RN,		movw23 }, // 23
	{ 0x0006, 0xF00F, "MOV.L",	OP_T_RM_AT_R0_RN,		movl24 },
	{ 0x000C, 0xF00F, "MOV.B",	OP_T_AT_R0_RM_RN,		movb25 },
	{ 0x000D, 0xF00F, "MOV.W",	OP_T_AT_R0_RM_RN,		movw26 },		// 0000nnnn mmmm1101
	{ 0x000E, 0xF00F, "MOV.L",	OP_T_AT_R0_RM_RN,		movl27 },		// 0000nnnn mmmm1110

	{ 0xC000, 0xFF00, "MOV.B",	OP_T_R0_AT_DISP_GBR,	NOIMP	},
	{ 0xC100, 0xFF00, "MOV.W",	OP_T_R0_AT_DISP_GBR,	movw29	},
	{ 0xC200, 0xFF00, "MOV.L",	OP_T_R0_AT_DISP_GBR,	movl30	}, // 30
	{ 0xC400, 0xFF00, "MOV.B",	OP_T_AT_DISP_GBR_R0,	movb31	},
	{ 0xC500, 0xFF00, "MOV.W",	OP_T_AT_DISP_GBR_R0,	NOIMP	},
	{ 0xC600, 0xFF00, "MOV.L",	OP_T_AT_DISP_GBR_R0,	movl33	},
	{ 0xC700, 0xFF00, "MOVA",	OP_T_AT_DISP_PC_R0,		mova34	}, // 34
	{ 0x0029, 0xF0FF, "MOVT",	OP_T_RN,				movt35	},			// 0000nnnn 00101001
	{ 0x6008, 0xF00F, "SWAP.B",	OP_T_RM_RN,				swapb36	},
	{ 0x6009, 0xF00F, "SWAP.W", OP_T_RM_RN,				swapw37 },
	{ 0x200D, 0xF00F, "XTRCT",	OP_T_RM_RN,				xtrct38	},

	// Arithmetic Operator Instructions
	{ 0x300C, 0xF00F, "ADD",	OP_T_RM_RN,				add39 }, // ADD Rm, Rn
	{ 0x7000, 0xF000, "ADD",	OP_T_IMM_RN,			add40 }, // ADD #imm, Rn
	{ 0x300E, 0xF00F, "ADDC",	OP_T_RM_RN,				addc41	},
	{ 0x300F, 0xF00F, "ADDV",	OP_T_RM_RN,				NOIMP	},
	{ 0x8800, 0xFF00, "CMP/EQ", OP_T_IMM_R0,			cmpeq43 },
	{ 0x3000, 0xF00F, "CMP/EQ", OP_T_RM_RN,				cmpeq44 },
	{ 0x3002, 0xF00F, "CMP/HS", OP_T_RM_RN,				cmphs45 },
	{ 0x3003, 0xF00F, "CMP/GE", OP_T_RM_RN,				cmpge46 },
	{ 0x3006, 0xF00F, "CMP/HI", OP_T_RM_RN,				cmphi47 },
	{ 0x3007, 0xF00F, "CMP/GT", OP_T_RM_RN,				cmpgt48 },
	{ 0x4011, 0xF0FF, "CMP/PZ", OP_T_RN,				cmppz49 },
	{ 0x4015, 0xF0FF, "CMP/PL", OP_T_RN,				cmppl50 },
	{ 0x200C, 0xF00F, "CMP/STR",OP_T_RM_RN,				cmpstr51 },
	{ 0x3004, 0xF00F, "DIV1",	OP_T_RM_RN,				div1s52	},
	{ 0x2007, 0xF00F, "DIV0S",	OP_T_RM_RN,				div0s53	},
	{ 0x0019, 0xFFFF, "DIV0U",	OP_T_NA,				div0u54	},
	{ 0x300D, 0xF00F, "DMULS.L",OP_T_RM_RN,				dmulsl55 },
	{ 0x3005, 0xF00F, "DMULU.L",OP_T_RM_RN,				dmulul56 },
	{ 0x4010, 0xF0FF, "DT",		OP_T_RN,				dt }, // 57
	{ 0x600E, 0xF00F, "EXTS.B",	OP_T_RM_RN,				extsb58 },
	// página 143
	{ 0x600F, 0xF00F, "EXTS.W",	OP_T_RM_RN,				extsw59 },
	{ 0x600C, 0xF00F, "EXTU.B", OP_T_RM_RN,				extub60 },
	{ 0x600D, 0xF00F, "EXTU.W", OP_T_RM_RN,				extuw61 },
	{ 0x000F, 0xF00F, "MAC.L",	OP_T_AT_RM_PLUS_AT_RN_PLUS,	macl62	},
	{ 0x400F, 0xF00F, "MAC.W",	OP_T_AT_RM_PLUS_AT_RN_PLUS,	NOIMP	},
	{ 0x0007, 0xF00F, "MUL.L",	OP_T_RM_RN,				mull },
	{ 0x200F, 0xF00F, "MULS.W",	OP_T_RM_RN,				mulsw65		},
	{ 0x200E, 0xF00F, "MULU.W",	OP_T_RM_RN,				muluw66	},
	{ 0x600B, 0xF00F, "NEG",	OP_T_RM_RN,				neg67 },
	{ 0x600A, 0xF00F, "NEGC",	OP_T_RM_RN,				negc68 },
	{ 0x3008, 0xF00F, "SUB",	OP_T_RM_RN,				sub69 },
	{ 0x300A, 0xF00F, "SUBC",	OP_T_RM_RN,				subc70 },
	{ 0x300B, 0xF00F, "SUBV",	OP_T_RM_RN,				NOIMP	},
	// Logic Operation Instructions
	// página 144
	{ 0x2009, 0xF00F, "AND",	OP_T_RM_RN,				and72 }, // 72
	{ 0xC900, 0xFF00, "AND",	OP_T_IMM_R0,			and73 },
	{ 0xCD00, 0xFF00, "AND.B",	OP_T_IMM_AT_R0_GBR,		NOIMP	},
	{ 0x6007, 0xF00F, "NOT",	OP_T_RM_RN,				not75 },
	{ 0x200B, 0xF00F, "OR",		OP_T_RM_RN,				or76 },
	{ 0xCB00, 0xFF00, "OR",		OP_T_IMM_R0,			or77 },
	{ 0xCF00, 0xFF00, "OR.B",	OP_T_IMM_AT_R0_GBR,		NOIMP	},
	{ 0x401B, 0xF0FF, "TAS.B",	OP_T_AT_RN,				tasb79 },
	{ 0x2008, 0xF00F, "TST",	OP_T_RM_RN,				tst80 },
	{ 0xC800, 0xFF00, "TST",	OP_T_IMM_R0,			tst81 },
	{ 0xCC00, 0xFF00, "TST.B",	OP_T_IMM_AT_R0_GBR,		NOIMP	},
	{ 0x200A, 0xF00F, "XOR",	OP_T_RM_RN,				xor83 },
	{ 0xCA00, 0xFF00, "XOR",	OP_T_IMM_R0,			xor84 },
	{ 0xCE00, 0xFF00, "XOR.B",	OP_T_IMM_AT_R0_GBR,		NOIMP	},
	// Shift Instructions
	// página 145
	{ 0x4004, 0xF0FF, "ROTL",	OP_T_RN,				rotl86 },
	{ 0x4005, 0xF0FF, "ROTR",	OP_T_RN,				rotr87 },
	{ 0x4024, 0xF0FF, "ROTCL",	OP_T_RN,				rotcl88	},
	{ 0x4025, 0xF0FF, "ROTCR",	OP_T_RN,				rotcr89	},
	{ 0x400C, 0xF00F, "SHAD",	OP_T_RM_RN,				shad90 }, // 90	0100nnnn mmmm1100
	{ 0x4020, 0xF0FF, "SHAL",	OP_T_RN,				NOIMP	},
	{ 0x4021, 0xF0FF, "SHAR",	OP_T_RN,				shar92 },
	{ 0x400D, 0xF00F, "SHLD",	OP_T_RM_RN,				shld93 },
	{ 0x4000, 0xF0FF, "SHLL",	OP_T_RN,				shll94 },
	{ 0x4001, 0xF0FF, "SHLR",	OP_T_RN,				shlr95 }, // 95
	{ 0x4008, 0xF0FF, "SHLL2",	OP_T_RN,				shll2 }, // 96
	{ 0x4009, 0xF0FF, "SHLR2",	OP_T_RN,				shlr2 }, // 97
	{ 0x4018, 0xF0FF, "SHLL8",	OP_T_RN,				shll8 },
	{ 0x4019, 0xF0FF, "SHLR8",	OP_T_RN,				shlr8 },
	{ 0x4028, 0xF0FF, "SHLL16",	OP_T_RN,				shll16 }, // 100
	{ 0x4029, 0xF0FF, "SHLR16",	OP_T_RN,				shlr16}, // 101
	// Branch Instructions
	// página 146
	{ 0x8B00, 0xFF00, "BF",		OP_T_LABEL8,			bf },
	{ 0x8F00, 0xFF00, "BF/S",	OP_T_LABEL8,			bfs },
	{ 0x8900, 0xFF00, "BT",		OP_T_LABEL8,			bt104 },
	{ 0x8D00, 0xFF00, "BT/S",	OP_T_LABEL8,			bts105 },
	{ 0xA000, 0xF000, "BRA",	OP_T_LABEL12,			bra },
	{ 0x0023, 0xF0FF, "BRAF",	OP_T_RN,				braf },
	{ 0xB000, 0xF000, "BSR",	OP_T_LABEL12,			bsr108 },
	{ 0x0003, 0xF0FF, "BSRF",	OP_T_RN,				bsrf109 },
	{ 0x402B, 0xF0FF, "JMP",	OP_T_AT_RN,				jmp110 }, // 110
	{ 0x400B, 0xF0FF, "JSR",	OP_T_AT_RN,				jsr111 },
	{ 0x000B, 0xFFFF, "RTS",	OP_T_NA,				rts112 },
	// System Control Instructions
	// página 147
	{ 0x0028, 0xFFFF, "CLRMAC",	OP_T_NA,				NOIMP	},
	{ 0x0048, 0xFFFF, "CLRS",	OP_T_NA,				NOIMP	},
	{ 0x0008, 0xFFFF, "CLRT",	OP_T_NA,				clrt115	},
	{ 0x400E, 0xF0FF, "LDC",	OP_T_RM_SR,				ldc116 },
	{ 0x401E, 0xF0FF, "LDC",	OP_T_RM_GBR,			NOIMP	},
	{ 0x402E, 0xF0FF, "LDC",	OP_T_RM_VBR,			ldc118 },
	{ 0x403E, 0xF0FF, "LDC",	OP_T_RM_SSR,			ldc119	},
	{ 0x404E, 0xF0FF, "LDC",	OP_T_RM_SPC,			NOIMP	}, // 120
	{ 0x403A, 0xF0FF, "LDC",    OP_T_RM_SGR,            NOIMP   }, // INSERTADA
	{ 0x40FA, 0xF0FF, "LDC",	OP_T_RM_DBR,			ldc122	},
	{ 0x408E, 0xF08F, "LDC",	OP_T_RM_RN_BANK,		ldc123	}, // REVISAR
	{ 0x4007, 0xF0FF, "LDC.L",	OP_T_AT_RM_PLUS_SR,		ldcl122	},
	{ 0x4017, 0xF0FF, "LDC.L",	OP_T_AT_RM_PLUS_GBR,	ldcl123	},
	{ 0x4027, 0xF0FF, "LDC.L",	OP_T_AT_RM_PLUS_VBR,	ldcl124	},
	{ 0x4037, 0xF0FF, "LDC.L",	OP_T_AT_RM_PLUS_SSR,	ldcl125	},
	{ 0x4047, 0xF0FF, "LDC.L",	OP_T_AT_RM_PLUS_SPC,	ldcl126	},
	{ 0x4036, 0xF0FF, "LDC.L",  OP_T_AT_RM_PLUS_SGR,    ldcl127 }, // INSERTADA
	{ 0x40F6, 0xF0FF, "LDC.L",	OP_T_AT_RM_PLUS_DBR,	ldcl128	}, // CORREGIDA
	{ 0x4087, 0xF08F, "LDC.L",	OP_T_AT_RM_PLUS_RN_BANK,ldcl129	}, // REVISAR
	{ 0x400A, 0xF0FF, "LDS",	OP_T_RM_MACH,			lds130	}, // 130
	{ 0x401A, 0xF0FF, "LDS",	OP_T_RM_MACL,			lds131	},
	{ 0x402A, 0xF0FF, "LDS",	OP_T_RM_PR,				lds132 },				// 0100mmmm 00101010
	{ 0x4006, 0xF0FF, "LDS.L",	OP_T_AT_RM_PLUS_MACH,	ldsl133	},
	{ 0x4016, 0xF0FF, "LDS.L",	OP_T_AT_RM_PLUS_MACL,	ldsl134	},
	{ 0x4026, 0xF0FF, "LDS.L",	OP_T_AT_RM_PLUS_PR,		ldsl135 },			// 0100mmmm 00100110
	{ 0x0038, 0xFFFF, "LDTLB",	OP_T_NA,				NOIMP	},
	{ 0x00C3, 0xF0FF, "MOVCA.L",OP_T_R0_AT_RN,			NOIMP	},
	{ 0x0009, 0xFFFF, "NOP",	OP_T_NA,				nop },
	{ 0x0093, 0xF0FF, "OCBI",	OP_T_AT_RN,				nop	},
	{ 0x00A3, 0xF0FF, "OCBP",	OP_T_AT_RN,				NOIMP	},// 140
	{ 0x00B3, 0xF0FF, "OCBWB",	OP_T_AT_RN,				ocbwb141},
	{ 0x0083, 0xF0FF, "PREF",	OP_T_AT_RN,				pref142 },
	{ 0x002B, 0xFFFF, "RTE",	OP_T_NA,				rte143	},
	// página 148
	{ 0x0058, 0xFFFF, "SETS",	OP_T_NA,				NOIMP	},
	{ 0x0018, 0xFFFF, "SETT",	OP_T_NA,				sett145 },	// 00000000 00011000
	{ 0x001B, 0xFFFF, "SLEEP",	OP_T_NA,				sleep116	},
	{ 0x0002, 0xF0FF, "STC",	OP_T_SR_RN,				stc147 },	
	{ 0x0012, 0xF0FF, "STC",	OP_T_GBR_RN,			stc150	},
	{ 0x0022, 0xF0FF, "STC",	OP_T_VBR_RN,			stc149 },
	{ 0x0032, 0xF0FF, "STC",	OP_T_SSR_RN,			stc152	}, // 150
	{ 0x0042, 0xF0FF, "STC",	OP_T_SPC_RN,			NOIMP	},
	{ 0x003A, 0xF0FF, "STC",	OP_T_SGR_RN,			NOIMP	},
	{ 0x00FA, 0xF0FF, "STC",	OP_T_DBR_RN,			stc155	},
	{ 0x0082, 0xF08F, "STC",	OP_T_RM_BANK_RN,		stc154	}, // REVISAR
	{ 0x4003, 0xF0FF, "STC.L",	OP_T_SR_AT_MIN_RN,		stcl155	},
	{ 0x4013, 0xF0FF, "STC.L",	OP_T_GBR_AT_MIN_RN,		stcl156	},
	{ 0x4023, 0xF0FF, "STC.L",	OP_T_VBR_AT_MIN_RN,		stcl157	},
	{ 0x4033, 0xF0FF, "STC.L",	OP_T_SSR_AT_MIN_RN,		stcl158	},
	{ 0x4043, 0xF0FF, "STC.L",	OP_T_SPC_AT_MIN_RN,		stcl159	},
	{ 0x4032, 0xF0FF, "STC.L",  OP_T_SGR_AT_MIN_RN,     stcl160 }, // CORREGIDA
	{ 0x40F2, 0xF0FF, "STC.L",	OP_T_DBR_AT_MIN_RN,		stcl161	},
	{ 0x4083, 0xF08F, "STC.L",	OP_T_RM_BANK_AT_MIN_RN,	stcl162	}, // REVISAR
	{ 0x000A, 0xF0FF, "STS",	OP_T_MACH_RN,			sts163 },
	{ 0x001A, 0xF0FF, "STS",	OP_T_MACL_RN,			sts164 }, // 164
	{ 0x002A, 0xF0FF, "STS",	OP_T_PR_RN,				sts165 }, // 165
	{ 0x4002, 0xF0FF, "STS.L",	OP_T_MACH_AT_MIN_RN,	stsl166	},
	{ 0x4012, 0xF0FF, "STS.L",	OP_T_MACL_AT_MIN_RN,	stsl167	},
	{ 0x4022, 0xF0FF, "STS.L",	OP_T_PR_AT_MIN_RN,		stsl168 },
	{ 0xC300, 0xFF00, "TRAPA",	OP_T_IMM,				trapa169},
	// Floating-Point Single-Precision Instructions
	{ 0xF08D, 0xF0FF, "FLDI0",	OP_T_FRN,				fldi0170,	REQ_PR_0	}, // 170
	{ 0xF09D, 0xF0FF, "FLDI1",	OP_T_FRN,				fldi1171,	REQ_PR_0	},
	{ 0xF00C, 0xF00F, "FMOV",	OP_T_FRM_FRN,			fmov172,	REQ_SZ_0	},
	{ 0xF008, 0xF00F, "FMOV.S", OP_T_AT_RM_FRN,			fmovs173,	REQ_SZ_0	}, // 173
	{ 0xF006, 0xF00F, "FMOV.S",	OP_T_AT_R0_RM_FRN,		fmovs174,	REQ_SZ_0	},
	{ 0xF009, 0xF00F, "FMOV.S",	OP_T_AT_RM_PLUS_FRN,	fmovs175,	REQ_SZ_0	},
	{ 0xF00A, 0xF00F, "FMOV.S", OP_T_FRM_AT_RN,			fmovs176,	REQ_SZ_0	}, // 176	// 1111nnnn mmmm1010
	{ 0xF00B, 0xF00F, "FMOV.S",	OP_T_FRM_AT_MIN_RN,		fmovs177,	REQ_SZ_0	},
	{ 0xF007, 0xF00F, "FMOV.S",	OP_T_FRM_AT_R0_RN,		fmovs178,	REQ_SZ_0	},
	{ 0xF00C, 0xF11F, "FMOV",	OP_T_DRM_DRN,			fmov179,	REQ_PR_0_SZ_1	},
	{ 0xF008, 0xF10F, "FMOV",	OP_T_AT_RM_DRN,			fmov180,	REQ_PR_0_SZ_1	},
	{ 0xF006, 0xF10F, "FMOV",	OP_T_AT_R0_RM_DRN,		NOIMP,		REQ_PR_0_SZ_1	},
	{ 0xF009, 0xF10F, "FMOV",	OP_T_AT_RM_PLUS_DRN,	fmov182,	REQ_PR_0_SZ_1	},
	{ 0xF00A, 0xF01F, "FMOV",	OP_T_DRM_AT_RN,			NOIMP,		REQ_PR_0_SZ_1	},
	{ 0xF00B, 0xF01F, "FMOV",	OP_T_DRM_AT_MIN_RN,		fmov184,	REQ_PR_0_SZ_1	}, // 184
	{ 0xF007, 0xF01F, "FMOV",	OP_T_DRM_AT_R0_RN,		fmov185,	REQ_PR_0_SZ_1	},
	{ 0xF01D, 0xF0FF, "FLDS",	OP_T_FRM_FPUL,			flds186						},
	{ 0xF00D, 0xF0FF, "FSTS",	OP_T_FPUL_FRN,			fsts187						}, // 187
	{ 0xF05D, 0xF0FF, "FABS",	OP_T_FRN,				NOIMP,		REQ_PR_0		},
	{ 0xF000, 0xF00F, "FADD",	OP_T_FRM_FRN,			fadd189,	REQ_PR_0		}, // 189
	{ 0xF004, 0xF00F, "FCMP/EQ",OP_T_FRM_FRN,			fcmpeq190,	REQ_PR_0		}, // 190
	{ 0xF005, 0xF00F, "FCMP/GT",OP_T_FRM_FRN,			fcmpgt191,	REQ_PR_0		},
	{ 0xF003, 0xF00F, "FDIV",	OP_T_FRM_FRN,			fdiv192,	REQ_PR_0		}, // 192
	{ 0xF02D, 0xF0FF, "FLOAT",	OP_T_FPUL_FRN,			float193,	REQ_PR_0		}, // 193
	{ 0xF00E, 0xF00F, "FMAC",	OP_T_FR0_FRM_FRN,		fmac194,	REQ_PR_0		},
	{ 0xF002, 0xF00F, "FMUL",	OP_T_FRM_FRN,			fmul195,	REQ_PR_0		}, // 195
	{ 0xF04D, 0xF0FF, "FNEG",	OP_T_FRN,				fneg196,	REQ_PR_0		},
	{ 0xF06D, 0xF0FF, "FSQRT",	OP_T_FRN,				fsqrt197,	REQ_PR_0		},
	{ 0xF001, 0xF00F, "FSUB",	OP_T_FRM_FRN,			fsub198,	REQ_PR_0		},
	{ 0xF03D, 0xF0FF, "FTRC",	OP_T_FRM_FPUL,			ftrc199,	REQ_PR_0		}, // 199
	{ 0xF05D, 0xF1FF, "FABS",	OP_T_DRN,				NOIMP,		REQ_PR_1_SZ_0	}, // 200
	{ 0xF000, 0xF11F, "FADD",	OP_T_DRM_DRN,			fadd201,	REQ_PR_1_SZ_0	},
	{ 0xF004, 0xF11F, "FCMP/EQ",OP_T_DRM_DRN,			NOIMP,		REQ_PR_1_SZ_0	},
	{ 0xF005, 0xF11F, "FCMP/GT",OP_T_DRM_DRN,			fcmpgt203,	REQ_PR_1_SZ_0	},
	{ 0xF003, 0xF11F, "FDIV",	OP_T_DRM_DRN,			fdiv204,	REQ_PR_1_SZ_0	},
	{ 0xF0BD, 0xF1FF, "FCNVDS",	OP_T_DRM_FPUL,			NOIMP,		REQ_PR_1_SZ_0	},
	{ 0xF0AD, 0xF1FF, "FCNVSD",	OP_T_FPUL_DRN,			NOIMP,		REQ_PR_1_SZ_0	},
	{ 0xF02D, 0xF1FF, "FLOAT",	OP_T_FPUL_DRN,			float207,	REQ_PR_1_SZ_0	},
	{ 0xF002, 0xF11F, "FMUL",	OP_T_DRM_DRN,			NOIMP,		REQ_PR_1_SZ_0	},
	{ 0xF04D, 0xF1FF, "FNEG",	OP_T_DRN,				NOIMP,		REQ_PR_1_SZ_0	},
	{ 0xF06D, 0xF1FF, "FSQRT",	OP_T_DRN,				fsqrt210,	REQ_PR_1_SZ_0	}, // 210
	{ 0xF001, 0xF11F, "FSUB",	OP_T_DRM_DRN,			NOIMP,		REQ_PR_1_SZ_0	},
	{ 0xF03D, 0xF1FF, "FTRC",	OP_T_DRM_FPUL,			ftrc212,	REQ_PR_1_SZ_0	},
	// Floating-Point Control Instructions
	{ 0x406A, 0xF0FF, "LDS",	OP_T_RM_FPSCR,			lds213		}, // 213
	{ 0x405A, 0xF0FF, "LDS",	OP_T_RM_FPUL,			lds214		}, // 214
	{ 0x4066, 0xF0FF, "LDS.L",	OP_T_AT_RM_PLUS_FPSCR,	ldsl215		}, // 215
	{ 0x4056, 0xF0FF, "LDS.L",	OP_T_AT_RM_PLUS_FPUL,	ldsl216		},
	{ 0x006A, 0xF0FF, "STS",	OP_T_FPSCR_RN,			sts217		},
	{ 0x005A, 0xF0FF, "STS",	OP_T_FPUL_RN,			sts218		},
	{ 0x4062, 0xF0FF, "STS.L",	OP_T_FPSCR_AT_MIN_RN,	stsl219		},
	{ 0x4052, 0xF0FF, "STS.L",	OP_T_FPUL_AT_MIN_RN,	stsl220		}, // 220
	// Floating-Point Graphics Acceleration Instructions
	{ 0xF10C, 0xF11F, "FMOV",	OP_T_DRM_XDN,			fmov221,	REQ_PR_0_SZ_1	},
	{ 0xF01C, 0xF11F, "FMOV",	OP_T_XDM_DRN,			fmov222,	REQ_PR_0_SZ_1	},
	{ 0xF11C, 0xF11F, "FMOV",	OP_T_XDM_XDN,			fmov223,	REQ_PR_0_SZ_1	},
	{ 0xF108, 0xF10F, "FMOV",	OP_T_AT_RM_XDN,			fmov224,	REQ_PR_0_SZ_1	}, // 224
	{ 0xF109, 0xF10F, "FMOV",	OP_T_AT_RM_PLUS_XDN,	fmov225,	REQ_PR_0_SZ_1	},
	{ 0xF106, 0xF10F, "FMOV",	OP_T_AT_R0_RM_XDN,		fmov226,	REQ_PR_0_SZ_1	},
	{ 0xF01A, 0xF01F, "FMOV",	OP_T_XDM_AT_RN,			fmov227,	REQ_PR_0_SZ_1	},
	{ 0xF01B, 0xF01F, "FMOV",	OP_T_XDM_AT_MIN_RN,		fmov228,	REQ_PR_0_SZ_1	},
	{ 0xF017, 0xF01F, "FMOV",	OP_T_XDM_AT_R0_RN,		fmov229,	REQ_PR_0_SZ_1	},
	{ 0xF0ED, 0xF0FF, "FIPR",	OP_T_FVM_FVN,			fipr,		REQ_PR_0		},
	{ 0xF1FD, 0xF3FF, "FTRV",	OP_T_XMTRX_FVN,			ftrv,		REQ_PR_0		},
	{ 0xF07D, 0xF0FF, "FSRRA",  OP_T_FRN,               fsrra,		REQ_PR_0		},
	{ 0xF0FD, 0xF1FF, "FSCA",	OP_T_FPUL_DRN,			fsca,		REQ_PR_0		},
	{ 0xFBFD, 0xFFFF, "FRCHG",	OP_T_NA,				frchg232,	REQ_PR_0		},
	{ 0xF3FD, 0xFFFF, "FSCHG",	OP_T_NA,				fschg233,	REQ_PR_0		},
	{ 0x0000, 0xFFFF, "NOIMP",  OP_T_NA,				NOIMP						},

	{ 0, 0, NULL, 0, (void *) NULL }
};

int find_opcode(DWORD mempos)
{
	int i, ret = -1;
	WORD target;
	bool cont;
	
	ReadMemoryW(mempos, &target);

	for (i = 0; opcodes[i].opdesc; i++)
	{
		cont = false;
		if ((target & opcodes[i].mask) == opcodes[i].op)
		{
			if (opcodes[i].restriccion)
			{
				switch(opcodes[i].restriccion)
				{
					case REQ_PR_0:
						if (IS_SET(FPSCR, FPSCR_PR))
							cont = true;
						break;
	
					case REQ_SZ_0:
						if (IS_SET(FPSCR, FPSCR_SZ))
							cont = true;
						break;
	
					case REQ_PR_0_SZ_1:
						if (IS_SET(FPSCR, FPSCR_PR) || !IS_SET(FPSCR, FPSCR_SZ))
							cont = true;
						break;
	
					case REQ_PR_1_SZ_0:
						if (!IS_SET(FPSCR, FPSCR_PR) || IS_SET(FPSCR, FPSCR_SZ))
							cont = true;
						break;
				}
				if (cont)
					continue;

            	if (ret == -1)
            		ret = i;
           		else
           		{
                	FILE * f = fopen("logs/error.txt", "a");
                	fprintf(f, "opcode %x marca 2 o mas resultados: %d %s, %d %s\r\n",
                		target, ret, opcodes[ret].opdesc, i, opcodes[i].opdesc);
               		fclose(f);
           		}
				//break;
 		    } 
 		    else
 		    {
            	if (ret == -1)
            		ret = i;
           		else
           		{
                	FILE * f = fopen("logs/error.txt", "a");
                	fprintf(f, "opcode %x marca 2 o mas resultados: %d %s, %d %s\r\n",
                		target, ret, opcodes[ret].opdesc, i, opcodes[i].opdesc);
               		fclose(f);
           		}
 		    }
		}
	}

	return ret;
}

void initopcodes()
{
	int i;int i2;//int fb;
	int noimp;

	for (i = 0; opcodes[i].opdesc; i++)
		if (opcodes[i].op == 0 && opcodes[i].mask == 0xFFFF) // NOIMP
			break;

	if (!opcodes[i].opdesc)
	{
		fprintf(stderr, "opcode NOIMP no encontrado!\n");
		abort();
	}

	noimp = i;

	for (i2 = 0; i2<65536; i2++)
	{
		oplist[i2]=noimp;
	}

	for (i = 0; opcodes[i].opdesc; i++)
	{
		for (i2 = 0; i2<65536; i2++)
		{
			if ((i2 & opcodes[i].mask) == opcodes[i].op)
			{
				oplist[i2]=i;
			}
		}
	}
}

