#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>

#include <stdarg.h>
#include "lnxdefs.h"

#define LOG_MEM			1
#define LOG_PVR			2
#define LOG_INTC		3
#define LOG_GLOP		4

extern FILE * logfp, * serialfp, * memfp, * pvrfp, * intcfp, * glopfp;

struct opcode_log_params
{
    int		pos;
    WORD	arg;
    DWORD	PC;
};

typedef void opcode_log_f(struct opcode_log_params * params);
typedef struct opcode_log_params opcode_log_params;

struct opcode_log_str
{
    opcode_log_f * func;
    opcode_log_params params;
};

// typedef __fastcall void opcode_f (WORD);
typedef void opcode_f (WORD);

extern char lastop[128];

#define OPMAXCNT 10
extern struct opcode_log_str ultop[OPMAXCNT];
extern short ultopcnt;

struct st_cmd
{
	const DWORD		op;
	const DWORD		mask;
	const char *	opdesc;
	const int		params;
//	opcode_log_f *	logfunc;
	opcode_f *		funcion;
// 	const  int			fd;
	const long		restriccion;
	unsigned long	llamadas;
};

extern struct st_cmd opcodes[];

#define LOG(str) { fprintf(logfp, "PC: %8x " #str " %8x\r\n", PC, str); }

int inicializar_logs();
void logmsg (char * fmt, ...);
void logxmsg (int tipo, char * fmt, ...);

#endif // _LOG_H_
