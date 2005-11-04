#include "main.h"
#include "sh4.h"
#include "opcodes.h"
#include <math.h>
#include "medium.h"

#define PI 3.1415926535

OPCODE(fsca) // FSCA FPUL, DRn
{
//	long n = (arg >> 8) & 0x0F;
	long n = (arg >> 9) & 0x07;
	float x = (float) (2 * PI * (float) FPUL / 65536.0);
//	float x = (float) (PI * FPUL / 65536.0);

	FR(n*2) = sin(x);
	FR(n*2+1) = cos(x);
/*	float_R(n) = sin(FPUL);
	float_registers[n+1] = cos(FPUL); */

/*	float_R(n) = 0;
	float_registers[n+1] = 0; */

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "fsca: n:%d, FPUL=%f, fl_reg[n]=%f, fl_reg[n+1]=%f\r\n",
		n, (float) FPUL, FR(n), float_registers[n+1]);
#endif
}

OPCODE(NOIMP)
{
	logmsg("opcode no implementado: %s (%d)\r\n", opcodes[find_opcode(PC)].opdesc, find_opcode(PC));
	PC += 2;
}

void hack_gdrom()
{
	DWORD valor;

	logmsg("HACK_GDROM: r6=%x, r7=%x\r\n", R(6), R(7));
	if (R(6) == 0)
	{
		switch(R(7))
		{
			case 0: // GDROM_SEND_COMMAND
			logmsg("GDROM_SEND_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
			switch(R(4))
			{
				case 16: // read sector, R(5) es el lugar donde se guardan los datos
				{
					int secstart, secnum;
					DWORD targetaddr;
					char * targetmem;

					memread(R(5), &secstart, sizeof(int));
					memread(R(5) + 4, &secnum, sizeof(int));
					memread(R(5) + 8, &targetaddr, sizeof(DWORD));

					logmsg("read sector: start=%x, num=%x, addr=%x\n", secstart, secnum, targetaddr);
					targetmem = malloc(sizeof(char) * 2048 * secnum);
					hook->read(&targetmem[0], secstart, secnum);
					memwrite(targetaddr, &targetmem[0], 2048 * secnum);
					free(targetmem);
				}
				break;

				case 19: // read toc, R(5) es el lugar donde se guardan los datos
				{
					int session;
					DWORD targetaddr;
					struct TOC toc;

		        	// hay que leer el # de sesión, es el 1er numero que apunta esa dir
		        	memread(R(5), &session, sizeof(int));
		        	logmsg("read toc: sesión n° %d\n", session);

		        	// a llenar un TOC "mula"
//					        	toc.entry[0] = 0x40002DB4; // CTRL = 4, LBA = 11700
//					        	toc.entry[0] = 0x40000000; // CTRL = 4, LBA = 0
					toc.entry[0] = 0x40000000 | hook->getCdInfo();
		        	toc.first = 0x00010000;
		        	toc.last  = 0x00010000;
		        	toc.dunno = 0;
		        	memread(R(5) + 4, &targetaddr, sizeof(DWORD));
		        	logmsg("escribiendo TOC a %x\n", targetaddr);
		        	memwrite(targetaddr, &toc, sizeof(struct TOC));
   				}
   				break;
			}
			R(0) = 0x6969;
			break;

		    case 1: // GDROM_CHECK_COMMAND
		    logmsg("GDROM_CHECK_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
		    R(0) = 2;
		    break;

		    case 2: // GDROM_MAIN_LOOP
		    logmsg("GDROM_MAIN_LOOP\r\n");
		    break;

			case 3: // GDROM_INIT
			logmsg("GDROM_INIT\r\n");
			break;

			case 4: // GDROM_CHECK_DRIVE
			logmsg("GDROM_CHECK_DRIVE\r\n");
			hook->getStatus(R(4));
			R(0) = 0;
			break;

			case 10: // GDROM_SECTOR_MODE
			logmsg("GDROM_SECTOR_MODE\r\n");
			ReadMemoryL(R(4), &valor);
			logmsg("valor: %x\r\n", valor);
			if (valor == 0)
			{
				hook->getSectorMode(R(4));
			}
			else
				logmsg("GDROM_SECTOR_MODE: valor != 0 no implementado\n");
			R(0) = 0;
			break;

			default:
			logmsg("GDROM: error!\n");
			break;
		}
	}
}

OPCODE(BIOS_HACK)
{
    logmsg("bios_hack\n");

    switch(PC)
    {
        case HACK_BASE + HACK_GDROM + 2: hack_gdrom(); break;
        default:	logmsg("bios_hack: error\n"); break;
    }
    
    PC += 2;
}

