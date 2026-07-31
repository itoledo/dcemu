#ifndef _OPCODES_H_
#define _OPCODES_H_

#include "main.h"

int find_opcode(DWORD mempos);
void initopcodes();

// extern int oplist[65536];
// extern int opcode_primer_restriccion;
#ifdef old_oplist
extern short * oplist;
extern short oplist_pr0_sz0[65536];
extern short oplist_pr0_sz1[65536];
extern short oplist_pr1_sz0[65536];
extern short oplist_pr1_sz1[65536];
#else
extern opcode_f ** oplist;
extern opcode_f * oplist_pr0_sz0[65536];
extern opcode_f * oplist_pr0_sz1[65536];
extern opcode_f * oplist_pr1_sz0[65536];
extern opcode_f * oplist_pr1_sz1[65536];
#endif
extern int idx_NOIMP;

// opcodes con restricci�n
/* extern int idx_FADD;
extern int idx_FSUB;
extern int idx_FMUL;
extern int idx_FDIV;
extern int idx_FCMPEQ;
extern int idx_FCMPGT;
extern int idx_FMOVA0MN;
extern int idx_FMOVMA0N;
extern int idx_FMOVAMN;
extern int idx_FMOVAMPN;
extern int idx_FMOVMAN;
extern int idx_FMOVMAMN;
extern int idx_FMOVMN;
extern int idx_FLOAT;
extern int idx_FTRC;
extern int idx_FNEG;
extern int idx_FABS;
extern int idx_FSQRT; */

#define LSB(arg) (arg & 0xFF)
#define MSB(arg) ((arg >> 8) & 0xFF)
#define BYTE2(arg) ((arg >> 8) & 0x0F)
#define BYTE3(arg) ((arg >> 4) & 0x0F)
#define BYTE4(arg) (arg & 0x0F)
#define BYTES234(arg) (arg & 0x0FFF)
#define BITS57(arg) ((arg >> 4) & 0x07)
#define BITS68(arg) ((arg >> 5) & 0x07)
#define BITS1012(arg) ((arg >> 9) & 0x07)
#define BITS910(arg) ((arg >> 8) & 0x03)
#define BITS1112(arg) ((arg >> 10) & 0x03)


/*
opcode_log_f OP_T_UNK;
opcode_log_f OP_T_IMM_RN;
opcode_log_f OP_T_AT_DISP_PC_RN;
opcode_log_f OP_T_RM_RN;
opcode_log_f OP_T_RM_AT_RN;
opcode_log_f OP_T_AT_RM_RN;
opcode_log_f OP_T_RM_AT_MIN_RN;
opcode_log_f OP_T_AT_RM_PLUS_RN;
opcode_log_f OP_T_R0_AT_DISP_RN;
opcode_log_f OP_T_RM_AT_DISP_RN;
opcode_log_f OP_T_AT_DISP_RM_R0;
opcode_log_f OP_T_AT_DISP_RM_RN;
opcode_log_f OP_T_RM_AT_R0_RN;
opcode_log_f OP_T_AT_R0_RM_RN;
opcode_log_f OP_T_R0_AT_DISP_GBR;
opcode_log_f OP_T_AT_DISP_GBR_R0;
opcode_log_f OP_T_AT_DISP_PC_R0;
opcode_log_f OP_T_RN;
opcode_log_f OP_T_IMM_R0;
opcode_log_f OP_T_NA;
opcode_log_f OP_T_AT_RM_PLUS_AT_RN_PLUS;
opcode_log_f OP_T_IMM_AT_R0_GBR;
opcode_log_f OP_T_AT_RN;
opcode_log_f OP_T_LABEL8;
opcode_log_f OP_T_LABEL12;
opcode_log_f OP_T_RM_SR;
opcode_log_f OP_T_RM_GBR;
opcode_log_f OP_T_RM_VBR;
opcode_log_f OP_T_RM_SSR;
opcode_log_f OP_T_RM_SPC;
opcode_log_f OP_T_RM_SGR;
opcode_log_f OP_T_RM_DBR;
opcode_log_f OP_T_RM_RN_BANK;
opcode_log_f OP_T_AT_RM_PLUS_SR;
opcode_log_f OP_T_AT_RM_PLUS_GBR;
opcode_log_f OP_T_AT_RM_PLUS_VBR;
opcode_log_f OP_T_AT_RM_PLUS_SSR;
opcode_log_f OP_T_AT_RM_PLUS_SPC;
opcode_log_f OP_T_AT_RM_PLUS_SGR;
opcode_log_f OP_T_AT_RM_PLUS_DBR;
opcode_log_f OP_T_AT_RM_PLUS_RN_BANK;
opcode_log_f OP_T_RM_MACH;
opcode_log_f OP_T_RM_MACL;
opcode_log_f OP_T_RM_PR;
opcode_log_f OP_T_AT_RM_PLUS_MACH;
opcode_log_f OP_T_AT_RM_PLUS_MACL;
opcode_log_f OP_T_AT_RM_PLUS_PR;
opcode_log_f OP_T_R0_AT_RN;
opcode_log_f OP_T_SR_RN;
opcode_log_f OP_T_GBR_RN;
opcode_log_f OP_T_SSR_RN;
opcode_log_f OP_T_SPC_RN;
opcode_log_f OP_T_SGR_RN;
opcode_log_f OP_T_DBR_RN;
opcode_log_f OP_T_RM_BANK_RN;
opcode_log_f OP_T_SR_AT_MIN_RN;
opcode_log_f OP_T_GBR_AT_MIN_RN;
opcode_log_f OP_T_VBR_RN;
opcode_log_f OP_T_VBR_AT_MIN_RN;
opcode_log_f OP_T_SSR_AT_MIN_RN;
opcode_log_f OP_T_SPC_AT_MIN_RN;
opcode_log_f OP_T_SGR_AT_MIN_RN;
opcode_log_f OP_T_DBR_AT_MIN_RN;
opcode_log_f OP_T_RM_BANK_AT_MIN_RN;
opcode_log_f OP_T_MACH_RN;
opcode_log_f OP_T_MACL_RN;
opcode_log_f OP_T_PR_RN;
opcode_log_f OP_T_MACH_AT_MIN_RN;
opcode_log_f OP_T_MACL_AT_MIN_RN;
opcode_log_f OP_T_PR_AT_MIN_RN;
opcode_log_f OP_T_IMM;
opcode_log_f OP_T_FRN;
opcode_log_f OP_T_FRM_FRN;
opcode_log_f OP_T_AT_RM_FRN;
opcode_log_f OP_T_AT_R0_RM_FRN;
opcode_log_f OP_T_AT_RM_PLUS_FRN;
opcode_log_f OP_T_FRM_AT_RN;
opcode_log_f OP_T_FRM_AT_MIN_RN;
opcode_log_f OP_T_FRM_AT_R0_RN;
opcode_log_f OP_T_DRM_DRN;
opcode_log_f OP_T_AT_RM_DRN;
opcode_log_f OP_T_AT_R0_RM_DRN;
opcode_log_f OP_T_AT_RM_PLUS_DRN;
opcode_log_f OP_T_DRM_AT_RN;
opcode_log_f OP_T_DRM_AT_MIN_RN;
opcode_log_f OP_T_DRM_AT_R0_RN;
opcode_log_f OP_T_FRM_FPUL;
opcode_log_f OP_T_FPUL_FRN;
opcode_log_f OP_T_FR0_FRM_FRN;
opcode_log_f OP_T_DRN;
opcode_log_f OP_T_DRM_FPUL;
opcode_log_f OP_T_FPUL_DRN;
opcode_log_f OP_T_RM_FPSCR;
opcode_log_f OP_T_RM_FPUL;
opcode_log_f OP_T_AT_RM_PLUS_FPSCR;
opcode_log_f OP_T_AT_RM_PLUS_FPUL;
opcode_log_f OP_T_FPSCR_RN;
opcode_log_f OP_T_FPUL_RN;
opcode_log_f OP_T_FPSCR_AT_MIN_RN;
opcode_log_f OP_T_FPUL_AT_MIN_RN;
opcode_log_f OP_T_DRM_XDN;
opcode_log_f OP_T_XDM_DRN;
opcode_log_f OP_T_XDM_XDN;
opcode_log_f OP_T_AT_RM_XDN;
opcode_log_f OP_T_AT_RM_PLUS_XDN;
opcode_log_f OP_T_AT_R0_RM_XDN;
opcode_log_f OP_T_XDM_AT_RN;
opcode_log_f OP_T_XDM_AT_MIN_RN;
opcode_log_f OP_T_XDM_AT_R0_RN;
opcode_log_f OP_T_FVM_FVN;
opcode_log_f OP_T_XMTRX_FVN;
*/


#define OP_T_UNK				0	// ???
#define OP_T_IMM_RN				1	// #imm, Rn
#define OP_T_AT_DISP_PC_RN		2	// @(disp, PC), Rn -- forma long: disp*4 sobre el PC alineado
#define OP_T_RM_RN				3	// Rm, Rn
#define OP_T_RM_AT_RN			4	// Rm, @Rn
#define OP_T_AT_RM_RN			5	// @Rm, Rn
#define OP_T_RM_AT_MIN_RN		6	// Rm, @-Rn
#define OP_T_AT_RM_PLUS_RN		7	// @Rm+, Rn
#define OP_T_R0_AT_DISP_RN		8	// R0, @(disp, Rn)
#define OP_T_RM_AT_DISP_RN		9	// Rm, @(disp, Rn)
#define OP_T_AT_DISP_RM_R0		10	// @(disp, Rm), R0
#define OP_T_AT_DISP_RM_RN		11	// @(disp, Rm), Rn
#define OP_T_RM_AT_R0_RN		12	// Rm, @(R0, Rn)
#define OP_T_AT_R0_RM_RN		13	// @(R0, Rm), Rn)
#define OP_T_R0_AT_DISP_GBR		14	// R0, @(disp, GBR)
#define OP_T_AT_DISP_GBR_R0		15	// @(disp, GBR), R0
#define OP_T_AT_DISP_PC_R0		16	// @(disp, PC), R0
#define OP_T_RN					17	// Rn
#define OP_T_RM_SGR				18	// Rm, SGR
#define OP_T_IMM_R0				19	// #imm, R0
#define OP_T_NA					20	//
#define OP_T_AT_RM_PLUS_AT_RN_PLUS	21	// @Rm+, @Rn+
#define OP_T_IMM_AT_R0_GBR		22	// #imm, @(R0, GBR)
#define OP_T_AT_RN				23	// @Rn
#define OP_T_LABEL8				24	// label
#define OP_T_LABEL12			25	// label
#define OP_T_RM_SR				26	// Rm, SR
#define OP_T_RM_GBR				27	// Rm, GBR
#define OP_T_RM_VBR				28	// Rm, VBR
#define OP_T_RM_SSR				29	// Rm, SSR
#define OP_T_RM_SPC				30	// Rm, SPC
#define OP_T_RM_DBR				31	// Rm, DBR
#define OP_T_RM_RN_BANK			32	// Rm, Rn_BANK
#define OP_T_AT_RM_PLUS_SR		33	// @Rm+, SR
#define OP_T_AT_RM_PLUS_GBR		34	// @Rm+, GBR
#define OP_T_AT_RM_PLUS_VBR		35	// @Rm+, VBR
#define OP_T_AT_RM_PLUS_SSR		36	// @Rm+, SSR
#define OP_T_AT_RM_PLUS_SPC		37	// @Rm+, SPC
#define OP_T_AT_RM_PLUS_DBR		38	// @Rm+, DBR
#define OP_T_AT_RM_PLUS_RN_BANK	39	// @Rm+, Rn_BANK
#define OP_T_RM_MACH			40	// Rm, MACH
#define OP_T_RM_MACL			41	// Rm, MACL
#define OP_T_RM_PR				42	// Rm, PR
#define OP_T_AT_RM_PLUS_MACH	43	// @Rm+, MACH
#define OP_T_AT_RM_PLUS_MACL	44	// @Rm+, MACL
#define OP_T_AT_RM_PLUS_PR		45	// @Rm+, PR
#define OP_T_R0_AT_RN			46	// R0, @Rn
#define OP_T_SR_RN				47	// SR, Rn
#define OP_T_GBR_RN				48	// GBR, Rn
#define OP_T_SSR_RN				49	// SSR, Rn
#define OP_T_SPC_RN				50	// SPC, Rn
#define OP_T_SGR_RN				51	// SGR, Rn
#define OP_T_DBR_RN				52	// DBR, Rn
#define OP_T_RM_BANK_RN			53	// Rm_BANK, Rn
#define OP_T_SR_AT_MIN_RN		54	// SR, @-Rn
#define OP_T_GBR_AT_MIN_RN		55	// GBR, @-Rn
#define OP_T_VBR_RN				56	// VBR, Rn
#define OP_T_VBR_AT_MIN_RN		57	// VBR, @-Rn
#define OP_T_SSR_AT_MIN_RN		58	// SSR, @-Rn
#define OP_T_SPC_AT_MIN_RN		59	// SPC, @-Rn
#define OP_T_SGR_AT_MIN_RN		60	// SGR, @-Rn
#define OP_T_DBR_AT_MIN_RN		61	// DBR, @-Rn
#define OP_T_RM_BANK_AT_MIN_RN	62	// Rm_BANK, @-Rn
#define OP_T_MACH_RN			63	// MACH, Rn
#define OP_T_MACL_RN			64	// MACL, Rn
#define OP_T_PR_RN				65	// PR, Rn
#define OP_T_MACH_AT_MIN_RN		66	// MACH, @-Rn
#define OP_T_MACL_AT_MIN_RN		67	// MACL, @-Rn
#define OP_T_PR_AT_MIN_RN		68	// PR, @-Rn
#define OP_T_IMM				69	// #imm
#define OP_T_FRN				70	// FRn
#define OP_T_FRM_FRN			71	// FRm, FRn
#define OP_T_AT_RM_FRN			72	// @Rm, FRn
#define OP_T_AT_R0_RM_FRN		73	// @(R0, Rm), FRn
#define OP_T_AT_RM_PLUS_FRN		74	// @Rm+, FRn
#define OP_T_FRM_AT_RN			75	// FRm, @Rn
#define OP_T_FRM_AT_MIN_RN		76	// FRm, @-Rn
#define OP_T_FRM_AT_R0_RN		77	// FRm, @(R0, Rn)
#define OP_T_DRM_DRN			78	// DRm, DRn
#define OP_T_AT_RM_DRN			79	// @Rm, DRn
#define OP_T_AT_R0_RM_DRN		80	// @(R0, Rm), DRn
#define OP_T_AT_RM_PLUS_DRN		81	// @Rm+, DRn
#define OP_T_DRM_AT_RN			82	// DRm, @Rn
#define OP_T_DRM_AT_MIN_RN		83	// DRm, @-Rn
#define OP_T_DRM_AT_R0_RN		84	// DRm, @(R0, Rn)
#define OP_T_FRM_FPUL			85	// FRm, FPUL
#define OP_T_FPUL_FRN			86	// FPUL, FRn
#define OP_T_FR0_FRM_FRN		87	// FR0, FRm, FRn
#define OP_T_DRN				88	// DRn
#define OP_T_DRM_FPUL			89	// DRm, FPUL
#define OP_T_FPUL_DRN			90	// FPUL, DRn
#define OP_T_RM_FPSCR			91	// Rm, FPSCR
#define OP_T_RM_FPUL			92	// Rm, FPUL
#define OP_T_AT_RM_PLUS_FPSCR	93	// @Rm+, FPSCR
#define OP_T_AT_RM_PLUS_FPUL	94	// @Rm+, FPUL
#define OP_T_FPSCR_RN			95	// FPSCR, Rn
#define OP_T_FPUL_RN			96	// FPUL, Rn
#define OP_T_FPSCR_AT_MIN_RN	97	// FPSCR, @-Rn
#define OP_T_FPUL_AT_MIN_RN		98	// FPUL, @-Rn
#define OP_T_DRM_XDN			99	// DRm, XDn
#define OP_T_XDM_DRN			100	// XDm, DRn
#define OP_T_XDM_XDN			101	// XDm, XDn
#define OP_T_AT_RM_XDN			102	// @Rm, XDn
#define OP_T_AT_RM_PLUS_XDN		103	// @Rm+, XDn
#define OP_T_AT_R0_RM_XDN		104 // @(R0, Rm), XDn
#define OP_T_XDM_AT_RN			105	// XDm, @Rn
#define OP_T_XDM_AT_MIN_RN		106	// XDm, @-Rn
#define OP_T_XDM_AT_R0_RN		107	// XDm, @(R0, Rn)
#define OP_T_FVM_FVN			108	// FVm, FVn
#define OP_T_XMTRX_FVN			109	// XMTRX, FVn
#define OP_T_AT_RM_PLUS_SGR		110	// @Rm+, SGR
/* @(disp, PC), Rn en su forma word. No es la misma que OP_T_AT_DISP_PC_RN: el
   desplazamiento va en unidades de 2 y el PC **no** se alinea a 4. Compartir un
   solo tipo hacia que disasm() resolviera todo con la formula del long y
   nombrara un literal que no era, lo que cuesta caro leyendo el boot ROM. */
#define OP_T_AT_DISP_PC_RN_W	111	// @(disp, PC), Rn -- forma word: disp*2 sobre el PC

#endif // _OPCODES_H_
