/****************************************************************************

	DEBUG - Interactive Debugger Window

*****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include "main.h"
#include "debug.h"
#include <SDL/SDL_opengl.h>
#include "graficos.h"
#include "BFont.h"
#include <time.h>
#include "main.h"

int DebugVisible = 1;
int DebugMode = DBG_STOP;
DWORD MemDebug = 0x8c000000;

SDL_Surface *DebugWindow;
// SDL_Surface *DebugDest;

BFont_Info *DebugFont;

int DebugInit(void)
{
//	DebugDest = Screen;

//	DebugFont = BFont_LoadFont("ConsoleFont.bmp");
	DebugFont = BFont_LoadFont("font.bmp");
	
//	BFont_SetFontColor(DebugFont, 0xff, 0xff, 0xff);
	
	if (DebugWindow == NULL) 
//		DebugWindow = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 16, 0, 0, 0, 0);
		DebugWindow = SDL_CreateRGBSurface(SDL_SWSURFACE, 1024, 512, 16, 0, 0, 0, 0);

	if (DebugWindow == NULL)
 		return 1;

//   	SDL_SetAlpha(DebugWindow, SDL_SRCALPHA | SDL_RLEACCEL, 0x80);    //60

	return 0;
}

void DebugShow(void)
{
	DebugVisible = 1;
}

void DebugHide(void)
{
	DebugVisible = 0;
}

void DebugUpdate(void)
{
	char buf[100];
	char buf2[100];
	char buf3[20];
	SDL_Rect rect;
	int x, y;
	WORD opcode;
	BYTE b;
	Uint32 color_black = SDL_MapRGBA(DebugWindow->format, 0x0, 0x0, 0x0, 0x0);
	Uint32 color_white = SDL_MapRGBA(DebugWindow->format, 0xff, 0xff, 0xff, 0x0);
	extern int filelogging;

	if (DebugVisible) 
 	{
#if defined(DEBUG_MEM_READ) || defined(DEBUG_MEM_WRITE)
		filelogging &= ~(FILELOG_MEMREADS | FILELOG_MEMWRITES);
#endif

   		SDL_FillRect(DebugWindow, NULL, color_black);
   		
   		// Populate Debug Window
   		rect.x = 3; rect.y = 3;
     	rect.h = 3; rect.w = 630;
  		SDL_FillRect(DebugWindow, &rect, color_white);
	
     	rect.h = 470; rect.w = 3;
  		SDL_FillRect(DebugWindow, &rect, color_white);
	
   		rect.x = 633;
  		SDL_FillRect(DebugWindow, &rect, color_white);
	
	    rect.x = 230; rect.h = 275;
  		SDL_FillRect(DebugWindow, &rect, color_white);

  		rect.w = 630; rect.h = 3;
    	rect.x = 3; rect.y = 275;
  		SDL_FillRect(DebugWindow, &rect, color_white);
         
        rect.y = 473; rect.w += 3;
  		SDL_FillRect(DebugWindow, &rect, color_white);
        
        rect.y = 450; 
  		SDL_FillRect(DebugWindow, &rect, color_white);
   		BFont_PutStringFont(DebugWindow, DebugFont, 10, 457, "F9 = Single Step   /"
        	"   F10 = Stop Execution   /   F11 = Run   /   F12 = Toggle Debug Screen   /   KP+ KP- = Page through Memory");

		// Disassembly Listing
   		BFont_PutStringFont(DebugWindow, DebugFont, 10, 10, "----- DISASSEMBLY -----");
		for (y = 0; y < 20; y++)
		{
			memread(PC + (y*2), &opcode, 2);
			disasm(PC + (y*2), &buf2[0]);
			sprintf(buf, "H'%08x: %04x  ", (unsigned int) PC + (y * 2), opcode);
			strcat(buf, buf2);
   			BFont_PutStringFont(DebugWindow, DebugFont, 10, 30 + (y * 12), buf);
   		}

   		// Memory Dump
   		BFont_PutStringFont(DebugWindow, DebugFont, 240, 10, "----- MEMORY DUMP -----");
		for (y = 0; y < 20; y++)
		{
			sprintf(buf, "H'%08x: ", (unsigned int) MemDebug + (y * 16));
			strcpy(buf3, "");
			for (x=0; x<16; x++)
			{
				memread(MemDebug + (y * 16) + x, &b, 1);
				sprintf(buf2, "%02x ", b);
				strcat(buf, buf2);
				if ((b >= 0x20) && (b <= 0x7f))
					sprintf(buf2, "%c", b);
				else
					strcpy(buf2, ".");
				strcat(buf3, buf2);
			}
			strcat(buf, buf3);
   			BFont_PutStringFont(DebugWindow, DebugFont, 240, 30 + (y * 12), buf);
   		}
     	
        // Registers	
   		BFont_PutStringFont(DebugWindow, DebugFont, 10, 280, "----- REGISTERS DUMP -----");
   		for (y = 0; y < 16; y++)
   		{
        	sprintf(buf, "R%-2d : %08x", y, (unsigned int) R(y));
        	BFont_PutStringFont(DebugWindow, DebugFont, 10 + ((y / 4) * 75), 300 + ((y % 4) * 12), buf);
    	}

    	sprintf(buf, "SR  : %08x", (unsigned int) SR);
    	BFont_PutStringFont(DebugWindow, DebugFont, 315, 300, buf);
    	sprintf(buf, "SSR : %08x", (unsigned int) SSR);
        BFont_PutStringFont(DebugWindow, DebugFont, 315, 312, buf);
    	sprintf(buf, "SPC : %08x", (unsigned int) SPC);
        BFont_PutStringFont(DebugWindow, DebugFont, 315, 324, buf);
    	sprintf(buf, "GBR : %08x", (unsigned int) GBR);
        BFont_PutStringFont(DebugWindow, DebugFont, 315, 336, buf);

    	sprintf(buf, "MACH: %08x", (unsigned int) MACH);
    	BFont_PutStringFont(DebugWindow, DebugFont, 395, 300, buf);
    	sprintf(buf, "MACL: %08x", (unsigned int) MACL);
        BFont_PutStringFont(DebugWindow, DebugFont, 395, 312, buf);
    	sprintf(buf, "PR  : %08x", (unsigned int) PR);
        BFont_PutStringFont(DebugWindow, DebugFont, 395, 324, buf);
    	sprintf(buf, "PC  : %08x", (unsigned int) PC);
        BFont_PutStringFont(DebugWindow, DebugFont, 395, 336, buf);

    	sprintf(buf, "VBR : %08x", (unsigned int) VBR);
    	BFont_PutStringFont(DebugWindow, DebugFont, 475, 300, buf);
    	sprintf(buf, "SGR : %08x", (unsigned int) SGR);
        BFont_PutStringFont(DebugWindow, DebugFont, 475, 312, buf);
    	sprintf(buf, "DBR : %08x", (unsigned int) DBR);
        BFont_PutStringFont(DebugWindow, DebugFont, 475, 324, buf);

    	sprintf(buf, "FPSCR: %08x", (unsigned int) FPSCR);
        BFont_PutStringFont(DebugWindow, DebugFont, 555, 300, buf);
    	sprintf(buf, "FPUL : %08x", (unsigned int) FPUL);
        BFont_PutStringFont(DebugWindow, DebugFont, 555, 312, buf);

    	sprintf(buf, "Status Register (SR) :-");
        BFont_PutStringFont(DebugWindow, DebugFont, 510, 350, buf);
    	sprintf(buf, "T: %1x", ((SR & 0x01)));
        BFont_PutStringFont(DebugWindow, DebugFont, 510, 362, buf);
    	sprintf(buf, "S: %1x", ((SR >> 1) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 540, 362, buf);
    	sprintf(buf, "Q: %1x", ((SR >> 8) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 570, 362, buf);
    	sprintf(buf, "M: %1x", ((SR >> 9) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 600, 362, buf);
    	sprintf(buf, "FD: %1x", ((SR >> 15) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 510, 374, buf);
    	sprintf(buf, "BL: %1x", ((SR >> 28) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 540, 374, buf);
    	sprintf(buf, "RB: %1x", ((SR >> 29) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 570, 374, buf);
    	sprintf(buf, "MD: %1x", ((SR >> 30) & 0x01));
        BFont_PutStringFont(DebugWindow, DebugFont, 600, 374, buf);
    	sprintf(buf, "IMASK: %1x%1x%1x%1x (%x)", 
     		((SR >> 7) & 0x01), ((SR >> 6) & 0x01), ((SR >> 5) & 0x01),
       		((SR >> 4) & 0x01), ((SR >> 4) & 0x0f));
        BFont_PutStringFont(DebugWindow, DebugFont, 510, 386, buf);
        
   		for (y = 0; y < 16; y++)
   		{
        	sprintf(buf, "FR%-2d : %+10.8e", y, FR(y));
        	BFont_PutStringFont(DebugWindow, DebugFont, 10 + ((y / 4) * 125), 350 + ((y % 4) * 12), buf);
    	}
    	
//		SDL_BlitSurface(DebugWindow, NULL, DebugDest, NULL);
#if defined(DEBUG_MEM_READ) || defined(DEBUG_MEM_WRITE)
		filelogging |= (FILELOG_MEMREADS | FILELOG_MEMWRITES);
#endif
	}
}

void disasm(DWORD address, char *buffer)
{
	WORD opcode;
	WORD op;

	memread(address, &opcode, 2);

	op = find_opcode(address);

	if (op != 0xffff) {
		switch(opcodes[op].params) 
		{
			LOG_OPCODE2(OP_T_AT_DISP_PC_RN, "@(%lx), R%d", LSB(opcode)*4 + (address & 0xFFFFFFFC) + 4, BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RN, "@R%d", BYTE2(opcode))
			LOG_OPCODE2(OP_T_IMM_RN, "#%x, R%d", LSB(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_RN, "R%d, R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_AT_RN, "R%d, @R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_RM_RN, "@R%d, R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_AT_MIN_RN, "R%d, @-R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_RM_PLUS_RN, "@R%d+, R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_R0_AT_DISP_RN, "R0, @(%x, R%d)", BYTE4(opcode), BYTE3(opcode))
			LOG_OPCODE3(OP_T_RM_AT_DISP_RN, "R%d, @(%x, R%d)", BYTE3(opcode), BYTE4(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_DISP_RM_R0, "@(%x, R%d), R0", BYTE4(opcode), BYTE3(opcode))
			LOG_OPCODE3(OP_T_AT_DISP_RM_RN, "@(%x, R%d), R%d", BYTE4(opcode), BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_AT_R0_RN, "R%d, @(R0, R%d)", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_R0_RM_RN, "@(R0, R%d), R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE1(OP_T_R0_AT_DISP_GBR, "R0, @(%x, GBR)", LSB(opcode))
			LOG_OPCODE1(OP_T_AT_DISP_GBR_R0, "@(%x, GBR), R0", LSB(opcode))
			LOG_OPCODE1(OP_T_AT_DISP_PC_R0, "@(%x, PC), R0", LSB(opcode))
			LOG_OPCODE1(OP_T_RN, "R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_IMM_R0, "#%x, R0", LSB(opcode))
			LOG_OPCODE0(OP_T_NA)
			LOG_OPCODE2(OP_T_AT_RM_PLUS_AT_RN_PLUS, "@R%d+, @R%d+", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE1(OP_T_IMM_AT_R0_GBR, "#%x, @(R0, GBR)", LSB(opcode))
			LOG_OPCODE1(OP_T_LABEL8, "%lx", SignExtend8(LSB(opcode))*2 + (address) + 4)
			LOG_OPCODE1(OP_T_LABEL12, "%lx", SignExtend12(BYTES234(opcode))*2 + (address) + 4)
			LOG_OPCODE1(OP_T_RM_SR, "R%d, SR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_GBR, "R%d, GBR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_VBR, "R%d, VBR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_SSR, "R%d, SSR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_SPC, "R%d, SPC", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_DBR, "R%d, DBR", BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_RN_BANK, "R%d, R%d_BANK", BYTE2(opcode), BITS57(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_SR, "@R%d+, SR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_GBR, "@R%d+, GBR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_VBR, "@R%d+, VBR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_SSR, "@R%d+, SSR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_SPC, "@R%d+, SPC", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_DBR, "@R%d+, DBR", BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_RM_PLUS_RN_BANK, "@R%d+, R%d_BANK", BYTE2(opcode), BITS57(opcode))
			LOG_OPCODE1(OP_T_RM_MACH, "R%d, MACH", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_MACL, "R%d, MACH", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_PR, "R%d, PR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_MACH, "@R%d+, MACH", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_MACL, "@R%d+, MACH", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_PR, "@R%d+, PR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_R0_AT_RN, "R0, @R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SR_RN, "SR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_GBR_RN, "GBR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_VBR_RN, "VBR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SSR_RN, "SSR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SPC_RN, "SPC, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SGR_RN, "SGR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_DBR_RN, "DBR, R%d", BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_BANK_RN, "R%d_BANK, R%d", BITS57(opcode), BYTE2(opcode))
			LOG_OPCODE1(OP_T_SR_AT_MIN_RN, "SR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_GBR_AT_MIN_RN, "GBR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_VBR_AT_MIN_RN, "VBR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SSR_AT_MIN_RN, "SSR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SPC_AT_MIN_RN, "SPC, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_SGR_AT_MIN_RN, "SGR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_DBR_AT_MIN_RN, "DBR, @-R%d", BYTE2(opcode))
			LOG_OPCODE2(OP_T_RM_BANK_AT_MIN_RN, "R%d_BANK, @-R%d", BITS57(opcode), BYTE2(opcode))
			LOG_OPCODE1(OP_T_MACH_RN, "MACH, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_MACL_RN, "MACL, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_PR_RN, "PR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_MACH_AT_MIN_RN, "MACH, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_MACL_AT_MIN_RN, "MACL, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_PR_AT_MIN_RN, "PR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_IMM, "#%x", LSB(opcode))
			LOG_OPCODE1(OP_T_FRN, "FR%d", BYTE2(opcode))
			LOG_OPCODE2(OP_T_FRM_FRN, "FR%d, FR%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_RM_FRN, "@R%d, FR%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_R0_RM_FRN, "@(R0, R%d), FR%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_AT_RM_PLUS_FRN, "@R%d+, FR%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_FRM_AT_RN, "FR%d, @R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_FRM_AT_MIN_RN, "FR%d, @-R%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_FRM_AT_R0_RN, "FR%d, @(R0, R%d)", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_DRM_DRN, "DR%d, DR%d", BITS68(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_AT_RM_DRN, "@R%d, DR%d", BYTE3(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_AT_R0_RM_DRN, "@(R0, R%d), DR%d", BYTE3(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_AT_RM_PLUS_DRN, "@R%d+, DR%d", BYTE3(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_DRM_AT_RN, "DR%d, @R%d", BITS68(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_DRM_AT_MIN_RN, "DR%d, @-R%d", BITS68(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_DRM_AT_R0_RN, "DR%d, @(R0, R%d)", BITS68(opcode), BYTE2(opcode))
			LOG_OPCODE1(OP_T_FRM_FPUL, "FR%d, FPUL", BYTE2(opcode))
			LOG_OPCODE1(OP_T_FPUL_FRN, "FPUL, FR%d", BYTE2(opcode))
			LOG_OPCODE2(OP_T_FR0_FRM_FRN, "FR0, FR%d, FR%d", BYTE3(opcode), BYTE2(opcode))
			LOG_OPCODE1(OP_T_DRN, "DR%d", BITS1012(opcode))
			LOG_OPCODE1(OP_T_DRM_FPUL, "DR%d, FPUL", BITS1012(opcode))
			LOG_OPCODE1(OP_T_FPUL_DRN, "FPUL, DR%d", BITS1012(opcode))
			LOG_OPCODE1(OP_T_RM_FPSCR, "R%d, FPSCR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_FPUL, "R%d, FPUL", BYTE2(opcode))
			LOG_OPCODE1(OP_T_RM_SGR, "R%d, SGR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_FPSCR, "@R%d+, FPSCR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_FPUL, "@R%d+, FPUL", BYTE2(opcode))
			LOG_OPCODE1(OP_T_AT_RM_PLUS_SGR, "@R%d+, SGR", BYTE2(opcode))
			LOG_OPCODE1(OP_T_FPSCR_RN, "FPSCR, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_FPUL_RN, "FPUL, R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_FPSCR_AT_MIN_RN, "FPSCR, @-R%d", BYTE2(opcode))
			LOG_OPCODE1(OP_T_FPUL_AT_MIN_RN, "FPUL, @-R%d", BYTE2(opcode))
			LOG_OPCODE2(OP_T_DRM_XDN, "DR%d, XD%d", BITS68(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_XDM_DRN, "XD%d, DR%d", BITS68(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_XDM_XDN, "XD%d, XD%d", BITS68(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_AT_RM_XDN, "@R%d, XD%d", BYTE3(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_AT_RM_PLUS_XDN, "@R%d+, XD%d", BYTE3(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_AT_R0_RM_XDN, "@(R0, R%d), XD%d", BYTE3(opcode), BITS1012(opcode))
			LOG_OPCODE2(OP_T_XDM_AT_RN, "XD%d, @R%d", BITS68(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_XDM_AT_MIN_RN, "XD%d, @-R%d", BITS68(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_XDM_AT_R0_RN, "XD%d, @(R0, R%d)", BITS68(opcode), BYTE2(opcode))
			LOG_OPCODE2(OP_T_FVM_FVN, "FV%d, FV%d", BITS910(opcode), BITS1112(opcode))
			LOG_OPCODE1(OP_T_XMTRX_FVN, "XMTRX, FV%d", BITS1112(opcode))
			default:
				sprintf(buffer, "%-9.7s", opcodes[op].opdesc);
		} 
	} else
		sprintf(buffer, "???");
}

void DrawDebugInlineInfo(SDL_Surface * surface)
{
extern time_t start_time;
extern unsigned long instrucciones;

	char buf[512]; //, buf2[128];
	SDL_Rect rc;
	time_t dif = time(NULL) - start_time;

	rc.x = 0;
	rc.h = 12;
	rc.w = 320;
	rc.y = 0;

	SDL_FillRect(surface, &rc, 0x00000000);

	sprintf(buf, "PC: %08lx t:%d spd:%d SR:%08x", //, %08x,%08x,%08x %s",
			PC, 
			dif, (dif > 0) ? instrucciones/dif : 0, SR);
	BFont_PutStringFont(surface, DebugFont, 0, 0, buf);
}

