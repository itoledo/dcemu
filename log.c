#ifndef _MSC_VER
#include <unistd.h>
#endif
#include "main.h"
#include "log.h"
#include "opcodes.h"
#include "options.h"
#include "gui.h"

FILE * logfp, * serialfp, * memfp, * pvrfp, * intcfp, * glopfp;

void logmsg (char * fmt, ...)
{
#ifdef LOGGING
	char buf [4096];

	va_list args;
	va_start (args, fmt);
	vsprintf (buf, fmt, args);
	va_end (args);
 
	fprintf(logfp, "%8lx: %s", PC, buf);
#ifdef LOG_FFLUSH
	fflush(logfp);
#endif
#endif
}

void logxmsg (int tipo, char * fmt, ...)
{
#ifdef LOGGING
	char buf [4096];
	FILE * fp;

	va_list args;
	va_start (args, fmt);
	vsprintf (buf, fmt, args);
	va_end (args);
 
	switch(tipo)
	{
		case LOG_MEM:	fp = memfp;	break;
		case LOG_PVR:	fp = pvrfp; break;
		case LOG_INTC:	fp = intcfp;	break;
		case LOG_GLOP:	fp = glopfp;	break;
		default:		fp = logfp; break;
	}
  
	fprintf(fp, "%8lx: %s", PC, buf);
#ifdef LOG_FFLUSH
	fflush(fp);
#endif
#endif
}

int inicializar_logs()
{
/*	FILE * fp;
	int i, j; */

    remove("logs/error.txt");
    remove("logs/disasm.txt");
    remove("logs/memoria.txt");
    remove("logs/serial.txt");
    remove("logs/pvr.txt");
    remove("logs/intc.txt");
    remove("logs/repetidos.txt");
    remove("logs/glop.txt");
	
	logfp = fopen("logs/disasm.txt", "w");

	if (!logfp)
	{
		fprintf(stderr, "No se pudo abrir disasm.txt\r\n");
		return 1;
	}

	memfp = fopen("logs/memoria.txt", "w");

	if (!memfp)
	{
		fprintf(stderr, "No se pudo abrir memoria.txt\r\n");
		return 1;
	}

	serialfp = fopen("logs/serial.txt", "w");

	if (!serialfp)
	{
		fprintf(stderr, "No se pudo abrir serial.txt");
		return 1;
	}

	pvrfp = fopen("logs/pvr.txt", "w");

	if (!pvrfp)
	{
		fprintf(stderr, "No se pudo abrir pvr.txt");
		return 1;
	}

	intcfp = fopen("logs/intc.txt", "w");

	if (!intcfp)
	{
		fprintf(stderr, "No se pudo abrir intc.txt");
		return 1;
	}

	glopfp = fopen("logs/glop.txt", "w");

	if (!glopfp)
	{
		fprintf(stderr, "No se pudo abrir glop.txt");
		return 1;
	}

/*    fp = fopen("logs/listado.txt", "w");

	for (i = 0; i < tam + 1; i += 2)
	{
		j = find_opcode(mem_base + mem_offset + i); // partimos desde 0x8c010000

		if (j != -1)
        {
			ultop[0].func = opcodes[j].logfunc;
        	ultop[0].params.pos = j;
        	memcpy(&ultop[0].params.arg, &memoria[mem_n_base + mem_offset + i], sizeof(WORD));
        	ultop[0].params.PC = mem_base + mem_offset + i;

    		(*ultop[0].func) (&(ultop[0].params));

    		fprintf(fp, "%04x %s\r\n", (DWORD) ultop[0].params.arg, lastop);
		}
		else
			fprintf(fp, "%04x ???\r\n", *(WORD *) &memoria[mem_n_base + mem_offset + i]);
	}

	fclose(fp); */
	
	return 0;
}


