#ifndef _DEBUG_H_
#define _DEBUG_H_

#include "SDL.h"
#include "sh4.h"
#include "opcodes.h"
#include "mem.h"

#define	DBG_STOP	0
#define	DBG_RUN		1
#define	DBG_STEP	2
#define DBG_ANIMATE	3

#define LOG_OPCODE0(param)								{ case param: sprintf(buffer, "%-9.7s ", opcodes[op].opdesc); break; }
#define LOG_OPCODE1(param, string, arg1)				{ case param: sprintf(buffer, "%-9.7s " string, opcodes[op].opdesc, arg1); break; }
#define LOG_OPCODE2(param, string, arg1, arg2)			{ case param: sprintf(buffer, "%-9.7s " string, opcodes[op].opdesc, arg1, arg2); break; }
#define LOG_OPCODE3(param, string, arg1, arg2, arg3)	{ case param: sprintf(buffer, "%-9.7s " string, opcodes[op].opdesc, arg1, arg2, arg3); break; }

extern int DebugVisible;
extern int DebugMode;
extern DWORD MemDebug;
extern DWORD BreakPoint;

void DebugUpdate(void);
int DebugInit(SDL_Surface *screenptr);
void DebugShow(void);
void DebugHide(void);
void DebugPrintf (int which, char * fmt, ...);
void disasm(DWORD address, char *buffer);

#endif // _DEBUG_H_
