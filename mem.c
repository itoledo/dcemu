// #include <windows.h>
#include <time.h>
#include "main.h"
#include "intc.h"
#include "opcodes.h"
#include "graficos.h"

DWORD SQ0[8];
DWORD SQ1[8];

unsigned char * memoria;
unsigned char * video_mem;
unsigned char * regmem;
unsigned char * bios_mem;
unsigned char * ta_mem;
unsigned char * control_mem;
BYTE	stack_mem[256 * 1024];	// la haremos de 256kb...?

char * mem_zone[0x100]; // para las zonas de memoria

typedef void mem_access_read_t (unsigned long direccion, void * p, size_t size);
typedef void mem_access_write_t (unsigned long direccion, void * p, size_t size);

mem_access_read_t video_read;
mem_access_read_t ram_read;
mem_access_read_t regmap_read;
mem_access_read_t mem_read_error;
mem_access_read_t pvr_read;
mem_access_read_t bios_read;
mem_access_read_t ignore_read;

mem_access_write_t video_write;
mem_access_write_t ram_write;
mem_access_write_t regmap_write;
mem_access_write_t mem_write_error;
mem_access_write_t pvr_write;
mem_access_write_t sq_write;
mem_access_write_t ta_write;
mem_access_write_t ignore_write;

mem_access_read_t * mem_hash_read[0x100];
mem_access_write_t * mem_hash_write[0x100];

unsigned char * control_mem;	// empezando en 0x005f0000, 64 kbytes

int inicializar_memoria()
{
	logmsg("creando memoria\n");

	memoria = (unsigned char *) malloc(sizeof(char) * MEM_SIZE); // 16 megabytes

	if (!memoria)
	{
		fprintf(stderr, "No se pudo crear memoria principal.\r\n");
		return 1;
	}

//	orig_mem = memoria;

/*	memoria = memoria - mem_n_base; // lo corremos n_base bytes hacia atrás,
 									// para aprovechar el offset */

	logmsg("creando memoria de video\n");
	
	video_mem = (unsigned char *) malloc(sizeof(unsigned char) * VIDEO_SIZE); // 8 megabytes

	if (!video_mem)
	{
		fprintf(stderr, "No se pudo crear memoria de video.\r\n");
		return 1;
	}

	logmsg("creando memoria de registros\n");

	regmem = (unsigned char *) malloc(sizeof(unsigned char) * 0x00FFFFFF); // 16 megabytes

	if (!regmem)
	{
		fprintf(stderr, "No se pudo crear regmem.\r\n");
		return 1;
	}

	bios_mem = (unsigned char *) malloc(sizeof(unsigned char) * BIOS_SIZE); // 4 MB

	if (!bios_mem)
	{
		fprintf(stderr, "No se pudo crear bios_mem.\r\n");
		return 1;
	}
	
	ta_mem = (unsigned char *) malloc(sizeof(unsigned char) * TA_SIZE);

	if (!ta_mem)
	{
		fprintf(stderr, "No se pudo crear ta_mem.\r\n");
		return 1;
	}
	
	control_mem = (unsigned char *) malloc(sizeof(unsigned char) * CONTROL_SIZE);

	if (!control_mem)
	{
		fprintf(stderr, "No se pudo crear control_mem.\r\n");
		return 1;
	}
	
	return 0;
}

DWORD float_to_dword(float x)
{
	DWORD res;

	memcpy(&res, &x, sizeof(DWORD));

	return res;
}

float dword_to_float(DWORD x)
{
	float res;

	memcpy(&res, &x, sizeof(float));

	return res;
}

void dump_registers()
{
    char buf[4096], buf2[4096];
	int i;
	
    buf2[0] = '\0';

	for (i = 0; i < 24; i++)
	{
		sprintf(buf, "r%d=%lx ", i, registers[i]);
		strcat(buf2, buf);
	}
	
    strcat(buf2, "\r\n");

	for (i = 0; i < 32; i++)
	{
		sprintf(buf, "FR%d=%lx ", i, float_to_dword(float_registers[i]));
		strcat(buf2, buf);
	}
    strcat(buf2, "\r\n");
	sprintf(buf, "T=%d, FPUL=%lx,%f MACL=%lx,%ld MACH=%lx,%ld\r\n", IS_SR_T() ? 1 : 0,
 		(DWORD) FPUL, (float) FPUL,
 		(DWORD) MACL, (signed long) MACL,
		(DWORD) MACH, (signed long) MACH);
	strcat(buf2, buf);
	
	sprintf(buf, "SR=%08lx MD:%d RB:%d\n", SR, IS_SET(SR,SR_MD) ? 1 : 0, IS_SET(SR,SR_RB) ? 1 : 0);
	strcat(buf2, buf);
	
	logmsg(buf2);
}

void check_registers()
{
#ifdef CHECK_VALUE
	int i;
	
	for (i = 0; i < 16; i++)
	{
		if (registers[i] == CHECK_VALUE)
		{
			logmsg("bingo: r[%d]=%x,%d\r\n", i, registers[i], registers[i]);
			dump_registers();
			break;
		}
	}
#endif
}

void regmem_setup(void)
{
	PTEL	= (DWORD *) &regmem[0x000004];	*PTEL = 0;
	CCR		= (DWORD *)	&regmem[0x00001C];	*CCR = 0;
	EXPEVT	= (DWORD *)	&regmem[0x000024];	*EXPEVT = 0;
	INTEVT	= (DWORD *) &regmem[0x000028];	*INTEVT = 0;
	TRA		= (DWORD *) &regmem[0x000020];
	QACR0	= (DWORD *)	&regmem[0x000038];	*QACR0 = 0;
	QACR1   = (DWORD *) &regmem[0x00003C];  *QACR1 = 0;


	PCTRA	= (DWORD *)	&regmem[0x80002C];	*PCTRA = 0;
	PDTRA	= (WORD *)	&regmem[0x800030];	*PDTRA = 0;
	PCTRB	= (DWORD *)	&regmem[0x800040];	*PCTRB = 0;
	PDTRB	= (WORD *)	&regmem[0x800044];	*PDTRB = 0;

	/*** DMA ***/
	SAR0	= (DWORD *)	&regmem[0xA00000];
	DAR0	= (DWORD *)	&regmem[0xA00004];
	DMATCR0	= (DWORD *)	&regmem[0xA00008];
	CHCR0	= (DWORD *)	&regmem[0xA0000C];	*CHCR0 = 0;

	SAR1	= (DWORD *)	&regmem[0xA00010];
	DAR1	= (DWORD *)	&regmem[0xA00014];
	DMATCR1	= (DWORD *)	&regmem[0xA00018];
	CHCR1	= (DWORD *)	&regmem[0xA0001C];		*CHCR1 = 0;

	SAR2	= (DWORD *)	&regmem[0xA00020];
	DAR2	= (DWORD *)	&regmem[0xA00024];
	DMATCR2	= (DWORD *)	&regmem[0xA00028];
	CHCR2	= (DWORD *)	&regmem[0xA0002C];		*CHCR2 = 0;
	
	SAR3	= (DWORD *)	&regmem[0xA00030];
	DAR3	= (DWORD *)	&regmem[0xA00034];
	DMATCR3	= (DWORD *)	&regmem[0xA00038];
	CHCR3	= (DWORD *)	&regmem[0xA0003C];		*CHCR3 = 0;

	DMAOR	= (DWORD *)	&regmem[0xA00040];		*DMAOR = 0;
	/*** FIN DMA ***/
	
	ICR		= (WORD *)	&regmem[0xD00000];		*ICR = 0;
	IPRA	= (WORD *)	&regmem[0xD00004];		*IPRA = 0;
	IPRB	= (WORD *)	&regmem[0xD00008];		*IPRB = 0;
	IPRC	= (WORD *)	&regmem[0xD0000C];		*IPRC = 0;

	/*** TMU Registers ***/
	TOCR	= (BYTE *)	&regmem[0xD80000];		*TOCR = 0;
	TSTR	= (BYTE *)	&regmem[0xD80004];		*TSTR = 0;
	TCOR0	= (DWORD *)	&regmem[0xD80008];		*TCOR0 = 0xFFFFFFFF;
	TCNT0	= (DWORD *)	&regmem[0xD8000C];		*TCNT0 = 0xFFFFFFFF;
	TCR0	= (WORD *)	&regmem[0xD80010];		*TCR0 = 0;
	TCOR1	= (DWORD *)	&regmem[0xD80014];		*TCOR1 = 0xFFFFFFFF;
	TCNT1	= (DWORD *)	&regmem[0xD80018];		*TCNT1 = 0xFFFFFFFF;
	TCR1	= (WORD *)	&regmem[0xD8001C];		*TCR1 = 0;
	TCOR2	= (DWORD *)	&regmem[0xD80020];		*TCOR2 = 0xFFFFFFFF;
	TCNT2	= (DWORD *)	&regmem[0xD80024];		*TCNT2 = 0xFFFFFFFF;
	TCR2	= (WORD *)	&regmem[0xD80028];		*TCR2 = 0;
	TCPR2	= (DWORD *)	&regmem[0xD8002C];
	/*** FIN TMU ***/
	
	SCSMR2	= (WORD *)	&regmem[0xE80000];		*SCSMR2 = 0;
	SCBRR2	= (BYTE *)	&regmem[0xE80004];		*SCBRR2 = 0xFF;
	SCSCR2	= (WORD *)	&regmem[0xE80008];		*SCSCR2 = 0;
	SCFTDR2	= (BYTE *)	&regmem[0xE8000C];		*SCFTDR2 = 0;
	SCFSR2	= (WORD *)	&regmem[0xE80010];		*SCFSR2 = 0x60;
	SCFCR2	= (WORD *)	&regmem[0xE80018];		*SCFCR2 = 0;
	SCSPTR2	= (WORD *)	&regmem[0xE80020];		*SCSPTR2 = 0;
	SCLSR2	= (WORD *)	&regmem[0xE80024];		*SCLSR2 = 0;
}

void mem_hash_setup(void)
{
	int i, i2;
	
	for (i = 0; i < 0xFF; i++)
	{
		mem_hash_read[i] = mem_read_error;
  		mem_hash_write[i] = mem_write_error;
  		mem_zone[i] = NULL;
	}

	// -------------------------------------------------------------------------------
	// Inicializacion para traz las zonas de la memoria para 
 	// la traduccion de logico a física. 
  	// Utilizado por las funciones que tienen acceso a memoria	
	// -------------------------------------------------------------------------------
	// 00 - 7f = User or Privil. Mode, Cacheable, MMU Available
	// 80 - 9f = Privil. Mode only, Cacheable, No MMU
	// a0 - bf = Privil. Mode only, Non-Cacheable, No MMU
	// c0 - df = Privil. Mode only, Cacheable, MMU Available
	// e0 - ff = Privil. Mode only, Non-Cacheable, No MMU
	// -------------------------------------------------------------------------------
	mem_zone[0x00] = &bios_mem[0];		// BIOS. Also contains system control regs
	// 01 = External Device
	// 02 = ** Unused **				// copy of System control regs, but NOT the BIOS
	// 03 = Copy of 01
	mem_zone[0x04] = &video_mem[0];		// 64-Bit Acc (TA or PVR)
	mem_zone[0x05] = &video_mem[0]; 	// 32-Bit Acc (TA or PVR)
	mem_zone[0x06] = mem_zone[0x04];
	mem_zone[0x07] = mem_zone[0x05];
	// 08 thru 0b = ** Unused **
	mem_zone[0x0c] = &memoria[0];		// SDRAM
	mem_zone[0x0d] = mem_zone[0x0c];
	mem_zone[0x0e] = mem_zone[0x0c];
	mem_zone[0x0f] = mem_zone[0x0d];
	// 10 = TA (write only)
	// 11 = video mem (32/64 bit acc - write only) (Thru TA)
	mem_zone[0x12] = mem_zone[0x10];
	mem_zone[0x13] = mem_zone[0x11];
	// 14 thru 17 = Ext Device
	// 18 thru 1b = ** Unused **
	// 1c thru 1f = SH4 Internal Area
	mem_zone[0x1E] = &stack_mem[0];		// Hmm, this is a cheat!

//	mem_zone[0x80] = &bios_mem[0];		
//	mem_zone[0x8C] = &memoria[0];
//	mem_zone[0xA4] = &video_mem[0];		// 64-Bit Acc
//	mem_zone[0xA5] = &video_mem[0];		// 32-Bit Acc
//	mem_zone[0xAC] = &memoria[0];

	// cree una copia de los bloques que repiten
	// Nota: aqui debemos copiar ciertos bloques usando una repeticion
	// Copy 01 thru 1f -> 21 thru 3f, 41 thru 5f, 61 thru 7f
	for (i = 0x00; i < 0x20; i++)
	{
		for (i2 = 0x20; i2 <= 0xc0; i2 += 0x20)
			mem_zone[i | i2] = mem_zone[i];
	}
 	
 	mem_zone[0x70] = &bios_mem[0];
 	
	// Inicializacion de la tabla de funciones tenía acceso 
 	// a bloques de la memoria	
	mem_hash_read[0x00] = bios_read;
 	mem_hash_read[0x04] = video_read;
	mem_hash_read[0x05] = video_read;
	mem_hash_read[0x0C] = ram_read;
 	mem_hash_read[0x1F] = regmap_read;
 	mem_hash_read[0x70] = bios_read;
 	mem_hash_read[0x7E] = ram_read;
	mem_hash_read[0x80] = bios_read;
	mem_hash_read[0x8C] = ram_read;
 	mem_hash_read[0xA0] = pvr_read;
	mem_hash_read[0xA4] = video_read;
 	mem_hash_read[0xA5] = video_read;
	mem_hash_read[0xAC]	= ram_read;
 	mem_hash_read[0xFF] = regmap_read;

	mem_hash_write[0x04] = video_write;
	mem_hash_write[0x05] = video_write;
 	mem_hash_write[0x0C] = ram_write;
	mem_hash_write[0x10] = ta_write;
 	mem_hash_write[0x1F] = regmap_write;
 	mem_hash_write[0x7E] = ram_write;
 	mem_hash_write[0x8C] = ram_write;
 	mem_hash_write[0xA0] = pvr_write;
 	mem_hash_write[0xA4] = video_write;
	mem_hash_write[0xA5] = video_write;
	mem_hash_write[0xA6] = ignore_write;
 	mem_hash_write[0xAC] = ram_write;
 	mem_hash_write[0xE0] = sq_write;
 	mem_hash_write[0xE1] = sq_write;
 	mem_hash_write[0xE2] = sq_write;
 	mem_hash_write[0xE3] = sq_write;
 	mem_hash_write[0xFF] = regmap_write;
}

void mem_read_error(unsigned long direccion, void * p, size_t size)
{
	logxmsg(LOG_MEM, "mem_read_error: dir %x, tamaño %d\r\n", direccion, size);
//	dump_registers();
}

void mem_write_error(unsigned long direccion, void * p, size_t size)
{
	switch(size)
	{
		case 1:	logxmsg(LOG_MEM, "mem_write_error: dir %x, byte %02x %d '%c'\n", direccion, *(BYTE *)p, *(BYTE *)p, *(BYTE *)p);	break;
		case 2: logxmsg(LOG_MEM, "mem_write_error: dir %x, word %04x %d\n", direccion, *(WORD *) p, *(WORD *)p);					break;
		case 4: logxmsg(LOG_MEM, "mem_write_error: dir %x - %02x, dword %08x %d\n", direccion, (direccion % 0x100) / 4, *(DWORD *)p, *(DWORD *)p);			break;
	}
//	dump_registers();
}

void sq_write(unsigned long direccion, void * p, size_t size)
{
    short pos;

/*    switch(size)
    {
    case 1: logmsg( "sq_write: dir %x, BYTE %x\r\n", direccion, *(BYTE *)p); break;
    case 2: logmsg( "sq_write: dir %x, WORD %x\r\n", direccion, *(WORD *)p); break;
    case 4: logmsg( "sq_write: dir %x, DWORD %x\r\n", direccion, *(DWORD *)p); break;
    } */
	
//	dump_registers();

    pos = (direccion >> 2) & 7; // 3 bits

    if (direccion & 0x20) // 0: SQ0, 1: SQ1
    {
        SQ1[pos] = *(DWORD *) p;
//		logxmsg(LOG_MEM, "sq_write: guardando %x en SQ1[%d]\r\n", *(DWORD *) p, pos);
    }
    else
    {
        SQ0[pos] = *(DWORD *) p;
//		logxmsg(LOG_MEM, "sq_write: guardando %x en SQ0[%d]\r\n", *(DWORD *) p, pos);
    }
}

void video_read(unsigned long direccion, void * p, size_t size)
{
//	long addr = direccion & 0x0FFFFFFF;
	long addr = direccion & 0x00FFFFFF;

//	memcpy(p, &video_mem[direccion - video_base], size);
#ifdef DEBUG_MEM_VIDEO
//	if (logvideomem)
		logxmsg(LOG_MEM, "video_read: dir %x\r\n", direccion);
#endif
	
	memcpy(p, &video_mem[addr], size);

/*	if (addr >= 0x04000000 && addr <= 0x07FFFFFF)
	{
		memcpy(p, &video_mem[addr - 0x04000000], size);
	} */

/*    if ((direccion - video_base) >= (screen->w * screen->h * (screen->format->BitsPerPixel / 8))
    {
    	memcpy(p, &video_mem[direccion - video_base], 
	else
	if ((direccion - video_base) < 0)
    {
//        logmsg("dibujando fuera de la pantalla: dir %x\r\n", direccion);
    }
    else
		ReadPixelN(direccion - video_base, p, size); */
}

void pvr_read(unsigned long direccion, void * p, size_t size)
{
	DWORD dw;
	switch(direccion)
	{
	case 0xa080ffc0: // snd_dbg
		{
			memcpy(p, &snd_dbg, size);
			snd_dbg &= 3; // hack
			snd_dbg ^= 3; // hack
		}
		break;
	
	case 0xa05f688c:
		{
			memcpy(p, &G2_FIFO, size);
			REMOVE_BIT(G2_FIFO, AICA_FIFO);
		}
		break;

		case 0xa05f6900: // Pending Interrupts 1
		{
			memcpy(p, &ASIC_ACK_A, size);
		}
		break;

		case 0xa05f6904: // Pending Interrupts 2
		{
			memcpy(p, &ASIC_ACK_B, size);
		}
		break;

		case 0xa05f6908: // Pending Interrupts 3
		{
			memcpy(p, &ASIC_ACK_C, size);
		}
		break;

		case 0xa05f6910: // Enabled Interrupts IRQD_A
		{
			memcpy(p, &ASIC_IRQD_A, size);
		}
		break;

		case 0xa05f6914: // Enabled Interrupts IRQD_B
		{
			memcpy(p, &ASIC_IRQD_B, size);
		}
		break;

		case 0xa05f6918: // Enabled Interrupts IRQD_C
		{
			memcpy(p, &ASIC_IRQD_C, size);
		}
		break;

		case 0xa05f6920: // Enabled Interrupts IRQB_A
		{
			memcpy(p, &ASIC_IRQB_A, size);
		}
		break;

		case 0xa05f6924: // Enabled Interrupts IRQB_B
		{
			memcpy(p, &ASIC_IRQB_B, size);
		}
		break;

		case 0xa05f6928: // Enabled Interrupts IRQB_C
		{
			memcpy(p, &ASIC_IRQB_C, size);
		}
		break;

		case 0xa05f6930: // Enabled Interrupts IRQ9_A
		{
			memcpy(p, &ASIC_IRQ9_A, size);
		}
		break;

		case 0xa05f6934: // Enabled Interrupts IRQ9_B
		{
			memcpy(p, &ASIC_IRQ9_B, size);
		}
		break;

		case 0xa05f6938: // Enabled Interrupts IRQ9_C
		{
			memcpy(p, &ASIC_IRQ9_C, size);
		}
		break;

		case 0xa05f6c18: // MAPLE State
		{
			memcpy(p, &MAPLE_STATE, size);
//			logmsg( "pvr_read: MAPLE_STATE: %x\r\n", MAPLE_STATE);
			if (MAPLE_STATE & 0x1)
			{
//				logmsg( "MAPLE_STATE:Desactivando DMA\r\n");
				REMOVE_BIT(MAPLE_STATE, 0x1);
			}
		}
		break;

		case 0xa05f8000: // COREID
		{
			dw = 0x17fd11db; // fijo para todas las DC
			memcpy(p, &dw, size);
			logmsg("pvr_read: COREID\r\n");
		}
		break;

		case 0xa05f8000 + 0x11 * 4:		// FB_R_CTRL
		{
			memcpy(p, &pvr_fb_r_ctrl, size);
			logxmsg(LOG_PVR, "pvr_read: FB_R_CTRL\r\n");
		}
		break;

		case 0xa05f80cc: // SCANINTPOS
		{
			memcpy(p, &PVR_SCANINTPOS, size);
			logmsg("pvr_read: SCANINTPOS\n");
		}
		break;

		case 0xa05f80e8: // BITMAPTYPE2
		{
			dw = 0x00160000;
			memcpy(p, &dw, size);
			logmsg("pvr_read: BITMAPTYPE2\r\n");
		}
		break;

		case 0xa05f8050:
		{
			dw = 0x00100203;
			memcpy(p, &dw, size);
		}
		break;

		case 0xa05f810c:
		{
			dw = (instrucciones / 10) % 0x1FF;
			memcpy(p, &dw, size);
		}
		break;

		case 0xa05f8114:
		{
			logxmsg(LOG_PVR, "pvr_read: FB_C_SOF: framebuffer current read address\n");
		}
		break;

		case 0xa05f8144:
		{
			logxmsg(LOG_PVR, "pvr_read: ta_init\r\n");
		}
		break;
	
		case 0xa0710000: // Dreamcast RTC, reg 1
		case 0xa0710004:
		{
			logmsg("pvr_read: RTC\r\n");
		}
		break;

		default:
		if (direccion >= 0xA0000000 && direccion <= 0xA01FFFFF) // Boot ROM & Flash ROM
		{
			bios_read(direccion, p, size);
		}
		else
		{
			if ((direccion & 0x00FF0000) == 0x005F0000)
			{
				memcpy(p, &control_mem[direccion & 0xFFFF], size);
				logxmsg(LOG_MEM, "leyendo desde control_mem: DW %x\n", *(DWORD *) p);
			}
			mem_read_error(direccion, p, size);
		}
		break;
	}
}

void bios_read(unsigned long direccion, void * p, size_t size)
{
//	memcpy(p, &bios_mem[direccion & 0x3FFFFF], size);
	if ((direccion & 0x00FF0000) == 0x005F0000)
	{
		memcpy(p, &control_mem[direccion & 0xFFFF], size);
		logxmsg(LOG_MEM, "leyendo control_mem en bios_read, dir %x\n", direccion);
	}
	else
		memcpy(p, &bios_mem[direccion & 0x3FFFFF], size);
}

/* void bios_write(unsigned long direccion, void * p, size_t size)
{
	memcpy(&bios_mem[direccion % 0x20000000], p, size);
} */

void ta_write(unsigned long direccion, void * p, size_t size)
{
	if (direccion >= 0x10000000 + TA_SIZE)
	{
		logxmsg(LOG_PVR, "TA: ERROR! direccion %x\n", direccion);
	}
	else
		memcpy(&ta_mem[direccion - 0x10000000], p, size);
}

void pvr_write(unsigned long direccion, void * p, size_t size)
{
	DWORD dw;
	bool last;

	if ((direccion & 0x00FF0000) == 0x005F0000)
	{
		memcpy(&control_mem[direccion & 0xFFFF], p, size);
	}

	switch(direccion)
	{
	case 0xa080ffc0: // snd_dbg
		{
			memcpy(&snd_dbg, p, size);
		}
		break;
	
	case 0xa05f688c: // G2 FIFO
		{
			memcpy(&G2_FIFO, p, size);
		}
		break;

		case 0xa05f6900: // Pending Interrupts 1
		{
			memcpy(&dw, p, size);
			REMOVE_BIT(ASIC_ACK_A, dw);
			logmsg("pvr_write: ASIC_ACK_A: %x\r\n", ASIC_ACK_A);
		}
		break;

		case 0xa05f6904: // Pending Interrupts 2
		{
			memcpy(&dw, p, size);
			REMOVE_BIT(ASIC_ACK_B, dw);
			logmsg("pvr_write: ACK_ACK_B: %x\r\n", ASIC_ACK_B);
		}
		break;

		case 0xa05f6908: // Pending Interrupts 3
		{
			memcpy(&dw, p, size);
			REMOVE_BIT(ASIC_ACK_C, dw);
			logmsg("pvr_write: ASIC_ACK_C: %x\r\n", ASIC_ACK_C);
		}
		break;

		case 0xa05f6910: // Enabled Interrupts IRQD_A
		{
			memcpy(&ASIC_IRQD_A, p, size);
			logmsg("pvr_write: ASIC_IRQD_A: %x\r\n", ASIC_IRQD_A);
		}
		break;

		case 0xa05f6914: // Enabled Interrupts IRQD_B
		{
			memcpy(&ASIC_IRQD_B, p, size);
			logmsg("pvr_write: ASIC_IRQD_B: %x\r\n", ASIC_IRQD_B);
		}
		break;

		case 0xa05f6918: // Enabled Interrupts IRQD_C
		{
			memcpy(&ASIC_IRQD_C, p, size);
			logmsg("pvr_write: ASIC_IRQD_C: %x\r\n", ASIC_IRQD_C);
		}
		break;

		case 0xa05f6920: // Enabled Interrupts IRQB_A
		{
			memcpy(&ASIC_IRQB_A, p, size);
			logmsg("pvr_write: ASIC_IRQB_A: %x\r\n", ASIC_IRQB_A);
		}
		break;

		case 0xa05f6924: // Enabled Interrupts IRQB_B
		{
			memcpy(&ASIC_IRQB_B, p, size);
			logmsg("pvr_write: ASIC_IRQB_B: %x\r\n", ASIC_IRQB_B);
		}
		break;

		case 0xa05f6928: // Enabled Interrupts IRQB_C
		{
			memcpy(&ASIC_IRQB_C, p, size);
			logmsg("pvr_write: ASIC_IRQB_C: %x\r\n", ASIC_IRQB_C);
		}
		break;

		case 0xa05f6930: // Enabled Interrupts IRQ9_A
		{
			memcpy(&ASIC_IRQ9_A, p, size);
			logmsg("pvr_write: ASIC_IRQ9_A: %x\r\n", ASIC_IRQ9_A);
		}
		break;

		case 0xa05f6934: // Enabled Interrupts IRQ9_B
		{
			memcpy(&ASIC_IRQ9_B, p, size);
			logmsg("pvr_write: ASIC_IRQ9_B: %x\r\n", ASIC_IRQ9_B);
		}
		break;

		case 0xa05f6938: // Enabled Interrupts IRQ9_C
		{
			memcpy(&ASIC_IRQ9_C, p, size);
			logmsg("pvr_write: ASIC_IRQ9_C: %x\r\n", ASIC_IRQ9_C);
		}
		break;

		/*** MAPLE ***/
		case 0xa05f6c04: // MAPLE_DMAADDR
		{
			memcpy(&MAPLE_DMAADDR, p, size);
//			logmsg( "pvr_write: MAPLE_DMAADDR: %x\r\n", (MAPLE_DMAADDR & 0x1FFFFFE0));
		}
		break;
		
		case 0xa05f6c10: // MAPLE_RESET2
		{
			memcpy(&MAPLE_RESET2, p, size);
//			logmsg( "pvr_write: MAPLE_RESET2: %x\r\n", MAPLE_RESET2);
		}
		break;

		case 0xa05f6c14: // MAPLE_ENABLE
		{
			memcpy(&MAPLE_ENABLE, p, size);
//			logmsg( "pvr_write: MAPLE_ENABLE: %x\r\n", MAPLE_ENABLE);
		}
		break;

		case 0xa05f6c18: // MAPLE_STATE
		{
			logmsg("MAPLE_STATE\r\n");
			memcpy(&MAPLE_STATE, p, size);
			if (MAPLE_STATE & 0x1)
			{
				DWORD td1 = 0, td2, curaddr;
				int i = 0, j, tam;

//				logmsg( "pvr_write: MAPLE_STATE enabled\r\n");

				// llenemos con algunos datos!
				// primero tenemos que leer MAPLE_DMAADDR...
				last = false;
				curaddr = MAPLE_DMAADDR;
				while (last == false && i < 1000)
				{
					memread(curaddr,  &td1, sizeof(DWORD));
					memread(curaddr + 4, &td2, sizeof(DWORD));
					if (td1 == 0)
					{
						logmsg( "pvr_write: MAPLE_DMAADDR: dir %x erronea\r\n", MAPLE_DMAADDR);
						return;
					}
//					logmsg( "leídos td1:%x, td2:%x\r\n", td1, td2);
					last = (td1 & 0x80000000) ? true : false;
//					logmsg( "tamaño del paquete: %x\r\n", (tam = (td1 & 0xFF) + 1));
					tam = (td1 & 0xFF) + 1;
					// ahora tenemos el paquetito! a tratar de leerlo.

					if (((td1 >> 16) & 0x3) != 0) // si no es el puerto 1
					{
						// grabemos 0xFFFFFFFF
						logmsg( "Grabando 0xFFFFFFFF en %x, tam=%d, td=%x\r\n", td2, tam, td1);
						dw = 0xFFFFFFFF;
						memwrite(td2, &dw, sizeof(DWORD));
					}
					else
					if (tam > 0)
					{
						DWORD * paquete;
						maple_devinfo_t devinfo;
						int recadr, sendadr;
						
						paquete = (DWORD *) malloc(sizeof(DWORD) * tam);
						for (j = 0; j < tam; j++)
						{
//							logmsg( "Leyendo paquete en %x\r\n", curaddr + 8 + j*4);
							memread(curaddr + 8 + j*4, &paquete[j], sizeof(DWORD));
						}
						// ya tenemos el paquete en memoria!
//						logmsg( "comando: %x\r\n", (paquete[0]) & 0xFF);
//						logmsg( "recaddr: %x\r\n", (recadr = (paquete[0] >> 8) & 0xFF));
//						logmsg( "sender : %x\r\n", (sendadr = (paquete[0] >> 16) & 0xFF));
//						logmsg( "adit.w.: %x\r\n", (paquete[0] >> 24) & 0xFF);
						recadr = (paquete[0] >> 8) & 0xFF;
						sendadr = (paquete[0] >> 16) & 0xFF;
						
						logmsg("recadr:%x, sendadr:%x\r\n", recadr, sendadr);

						if (recadr != 0x20)
						{
							logmsg( "Grabando 0xFFFFFFFF en %x, tam=%d, td=%x\r\n", td2, tam, td1);
							dw = 0xFFFFFFFF;
							memwrite(td2, &dw, sizeof(DWORD));
						}
						else
						if ((paquete[0] & 0xFF) == 1) // Request Device Info
						{
							// tenemos que guardar el deviceinfo!
							devinfo.func = (1 << 24); // controlador
							devinfo.function_data[0] = 0;
							devinfo.function_data[1] = 0;
							devinfo.function_data[2] = 0;
							devinfo.area_code = 0;
							devinfo.connector_direction = 0;
							strcpy(devinfo.product_name, "Controlador DC");
							strcpy(devinfo.product_license, "SEGA");
							devinfo.standby_power = 0;
							devinfo.max_power = 0;
							// a hacer el paquete de respuesta
							paquete[0] = 0x05 | // device info (response)
								((sendadr << 8) & 0xFF00) |
								((((recadr == 0x20) ? 0x20 : 0) << 16) & 0xFF0000) |
								(((sizeof(maple_devinfo_t)/4) << 24) & 0xFF000000);
//							logmsg( "Escribiendo en %x: %x\r\n", td2, paquete[0]);
							memwrite(td2, &paquete[0], sizeof(DWORD));
//							logmsg( "Escribiendo devinfo en %x\r\n", td2 + 4);
							memwrite(td2 + 4, &devinfo, sizeof(maple_devinfo_t));
						}
						else
						if ((paquete[0] & 0xFF) == 9) // MAPLE_COMMAND_GETCOND
						{
							// recibe FUNC
							cont_cond_t ct;
							
							ct.buttons = joystick; // CONT_START
							ct.rtrig = 0;
							ct.ltrig = 0;
							ct.joyx = 0;
							ct.joyy = 0;
							ct.joy2x = 0;
							ct.joy2y = 0;

							paquete[0] = 0x08 | // data transfer (response)
								((sendadr << 8) & 0xFF00) |
								((((recadr == 0x20) ? 0x20 : 1) << 16) & 0xFF0000) |
								(((sizeof(cont_cond_t)/4 + 1) << 24) & 0xFF000000);
								
//							logmsg( "Escribiendo en %x: %x\r\n", td2, paquete[0]);
							memwrite(td2, &paquete[0], sizeof(DWORD));
							dw = (1 << 24); // MAPLE_FUNC_CONTROLLER
							memwrite(td2 + 4, &dw, sizeof(DWORD));
//							logmsg( "Escribiendo cont_cond en %x\r\n", td2 + 8);
							memwrite(td2 + 8, &ct, sizeof(cont_cond_t));
						}
						else
						{
							logmsg("comando MAPLE no implementado: %x\r\n", paquete[0]);
						}

						free(paquete);
					}
					else
					{
						logmsg("MAPLE: tamaño = %d\r\n", tam);
	 				}
//					curaddr += ((td1 & 0xFF) + 1);
					curaddr += ((td1 & 0xFF) + 1) * 4 + 8; // para que se salte el descriptor + el paquete
					i++;
				}
				maple_dma = true;
			}
			else
			{
				logmsg( "pvr_write: MAPLE_STATE disabled\r\n");
			}
		}
		break;

		case 0xa05f6c80: // MAPLE_SPEED
		{
			memcpy(&MAPLE_SPEED, p, size);
//			logmsg( "pvr_write: MAPLE_SPEED: %x\r\n", MAPLE_SPEED);
		}
		break;

		case 0xa05f6c8c: // MAPLE_RESET1
		{
			memcpy(&MAPLE_RESET1, p, size);
//			logmsg( "pvr_write: MAPLE_RESET1: %x\r\n", MAPLE_RESET1);
		}
		break;
		/*** FIN MAPLE ***/

		case 0xa05f8012: // RENDERFORMAT, "alpha config"
		{
			DWORD dw = *(DWORD *) p;
			char * s_RENDERFORMAT[] = {
   				"0RGB1555",
       			"RGB565",
          		"ARGB4444",
            	"ARGB1555",
             	"RGB888",
              	"0RGB8888",
               	"ARGB8888",
                "ARGB4444" };
			// A, R, G, B, bpp
			int m_RENDERFORMAT[][5] = {
   				{ 0, 5, 5, 5, 16 },
       			{ 0, 5, 6, 5, 16 },
       			{ 4, 4, 4, 4, 16 },
       			{ 1, 5, 5, 5, 16 },
       			{ 0, 8, 8, 8, 16 },
       			{ 0, 8, 8, 8, 32 },
       			{ 8, 8, 8, 8, 32 },
       			{ 4, 4, 4, 4, 16 } };
   			int idx;

			logmsg("0xa05f8008: RENDERFORMAT=%x\n", *(DWORD *)p);
			if (dw & 0x1000)
				logmsg("dither enable\n");
			idx = dw & 0x07;
			logmsg("formato: %s\n", s_RENDERFORMAT[idx]);
			SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, m_RENDERFORMAT[0][idx]);
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, m_RENDERFORMAT[1][idx]);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, m_RENDERFORMAT[2][idx]);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, m_RENDERFORMAT[3][idx]);
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, m_RENDERFORMAT[4][idx]);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		}
		break;

#define PVR_WRITE_1(dir, texto, arg)		{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg);} break; }
#define PVR_WRITE_CB_1(dir, callback, texto, arg)		{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg);} callback(direccion, p, size); break; }
#define PVR_WRITE_2(dir, texto, arg1, arg2)	{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg1, arg2);} break; }

		PVR_WRITE_1(0xa05f8000 + 0x00 * 4, "COREID [ID] (PVR2 Core ID), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x01 * 4, "CORETYPE [REV] (PVR2 Core version), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x02 * 4, "COREDISABLE [CORERESET] (PVR2) (Enable/disable the submodules of the PVR2 Core), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x03 * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x04 * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_CB_1(0xa05f8000 + 0x05 * 4, cb_renderstart, "RENDERSTART (3D) (Start render strobe), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x06 * 4, "[TESTSELECT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x07 * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x08 * 4, "PRIMALLOCBASE [PARAMBASE] (3D) (Primitive allocation base), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x09 * 4, "[SPANSORTCFG], grabando %x", *(DWORD *) p);
 		PVR_WRITE_1(0xa05f8000 + 0x0a * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0b * 4, "TILEARRAY (Tile Array base address), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0c * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0d * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0e * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0f * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x10 * 4, "BORDERCOLOR (2D) (Border colour), grabando %x", *(DWORD *) p);

		case 0xa05f8000 + 0x11 * 4:		// BITMAPTYPE (bitmap display settings)
		{
			logxmsg(LOG_PVR, "pvr_write: BITMAPTYPE [FB_R_CTRL] (bitmap display settings), grabando %x\n",
				*(DWORD *)p);
			dw = *(DWORD *) p;
			pvr_fb_r_ctrl = dw;
			logxmsg(LOG_PVR, "pvr_write: displaymode: %x\r\n", dw);
			if (dw & 0x02)
				logxmsg(LOG_PVR, "line doubling enable\r\n");
			switch((dw >> 2) & 0x3)
			{
				case 0x00:
				logxmsg(LOG_PVR, "ARGB0555\r\n");
				screenbits = 16;
				screenformat = FRAMEBUFFER_ARGB0555;
				break;
				
				case 0x01:
				logxmsg(LOG_PVR, "RGB565\r\n");
				screenbits = 16;
				screenformat = FRAMEBUFFER_RGB565;
				break;
				
				case 0x02:
				logxmsg(LOG_PVR, "RGB888\r\n");
				screenbits = 24;
				screenformat = FRAMEBUFFER_RGB888;
				break;
				
				case 0x03:
				logxmsg(LOG_PVR, "ARGB0888\r\n");
				screenbits = 32;
				screenformat = FRAMEBUFFER_ARGB0888;
				break;
			}
			if (dw & 0x00800000)
   				logxmsg(LOG_PVR, "pixel clock double enable\r\n");
			logxmsg(LOG_PVR, "screenbits: %d\r\n", screenbits);
			if (dw & 0x01)
			{
				logxmsg(LOG_PVR, "bitmap display enable\r\n");
				pvr_framebufferdisplay = true;
				screeninit();
			}
			else
				pvr_framebufferdisplay = false;
			refresh_screen = true;
		}
		break;

		PVR_WRITE_CB_1(0xa05f8000 + 0x12 * 4, cb_fb_w_ctrl, "RENDERFORMAT [FB_W_CTRL] (3D) (Render output format): %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x13 * 4, "RENDERPITCH [FB_W_LINESTRIDE] (3D) (Render target pitch)", *(DWORD *) p);
		PVR_WRITE_CB_1(0xa05f8000 + 0x14 * 4, cb_fb_r_sof1, "FRAMEBUF [FB_R_SOF1] (2D) (Framebuffer address): %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x15 * 4, "FRAMEBUF [FB_R_SOF2] (2D) (Framebuffer address, short field): %x", *(DWORD *) p);
		PVR_WRITE_2(0xa05f8000 + 0x16 * 4, "Unknown, grabando %x, M=%x", *(DWORD *) p, 0x00000000);

		case 0xa05f8000 + 0x17 * 4:		// DIWSIZE (2D) (Display window size)
		{
			long modulo;

			logxmsg(LOG_PVR, "pvr_write: DIWSIZE [FB_R_SIZE] (2D) (Display window size), grabando %x\n",
				*(DWORD *)p);

			dw = *(DWORD *) p;
			logxmsg(LOG_PVR, "pvr_write: displaysize: %x\r\n", dw);
				
			screenwidth = (dw & 0x3FF) + 1;
			logxmsg(LOG_PVR, "screenwidth (en unidades de 32 bits): %d\r\n", screenwidth);

			screenheight = ((dw >> 10) & 0x3FF) + 1;
			logxmsg(LOG_PVR, "screenheight: %d\r\n", screenheight);

			modulo = (dw >> 20) & 0x3FF;
			logxmsg(LOG_PVR, "modulo: %d\r\n", modulo);

			if (screenheight == 0)
			{
				logxmsg(LOG_PVR, "modo inválido. valores por defecto.\n");
				screenheight = 480;
				screenwidth = 640;
				screenbits = 16;
			}
			screeninit();
			refresh_screen = true;
		}
		break;

		PVR_WRITE_1(0xa05f8000 + 0x18 * 4, "RENDERBASE [FB_W_SOF1](3D) (Render target base address), grabando %x", *(DWORD *) p);
		PVR_WRITE_2(0xa05f8000 + 0x19 * 4, "[FB_W_SOF2], grabando %x, M=%x", *(DWORD *) p, 0x00600a00);
		PVR_WRITE_1(0xa05f8000 + 0x1a * 4, "RENDERWINDOWX [FB_X_CLIP] (3D) (Render output window X-start and X-stop), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x1b * 4, "RENDERWINDOWY [FB_Y_CLIP] (3D) (Render output window Y-start and Y-stop), grabando %x", *(DWORD *) p);
		PVR_WRITE_2(0xa05f8000 + 0x1c * 4, "Unknown, grabando %x, M=%x", *(DWORD *) p, 0x00000000);
		PVR_WRITE_1(0xa05f8000 + 0x1d * 4, "CHEAPSHADOWS [FPU_SHAD_SCALE] (3D) (Cheap shadow enable + strength), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x1e * 4, "CULLINGVALUE [FPU_CULL_VAL] (3D) (Minimum allowed polygon area), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x1f * 4, "[FPU_PARAM_CFG] (3D) (Something to do with rendering), grabando %x", *(DWORD *) p);
		PVR_WRITE_2(0xa05f8000 + 0x20 * 4, "[HALF_OFFSET], grabando %x, M=%x", *(DWORD *) p, 0x00000007);
		PVR_WRITE_2(0xa05f8000 + 0x21 * 4, "[FPU_PERP_VAL] (3D) (Something to do with rendering), grabando %x, M=%x", *(DWORD *)p, 0);
		PVR_WRITE_2(0xa05f8000 + 0x22 * 4, "[ISP_BACKGND_D], grabando %x, M=%x", *(DWORD *)p, 0x38d1b710);
		PVR_WRITE_1(0xa05f8000 + 0x23 * 4, "BGPLANE [ISP_BACKGND_T] (3D) (Background plane location), grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x26 * 4, "[ISP_FEED_CFG], grabando %x", *(DWORD *) p);

		case 0xa05f8000 + 0x24 * 4:		// Unknown
		case 0xa05f8000 + 0x25 * 4:
		case 0xa05f8000 + 0x27 * 4:
		{
			logxmsg(LOG_PVR, "pvr_write: Unknown, grabando %x\n",
				*(DWORD *)p);
		}
		break;

		case 0xa05f8000 + 0x28 * 4:		// UNNAMED
		{
			logxmsg(LOG_PVR, "pvr_write: [SDRAM_REFRESH], grabando %x, M=%x\n",
				*(DWORD *)p, 0x00000020);
		}
		break;

		case 0xa05f8000 + 0x29 * 4:		// Unknown
		{
			logxmsg(LOG_PVR, "pvr_write: [SDRAM_ARB_CFG], grabando %x, M=%x\n",
				*(DWORD *)p, 0x0000001f);
		}
		break;

		case 0xa05f8000 + 0x2a * 4:		// UNNAMED (PVR) (Graphics memory control)
		{
			logxmsg(LOG_PVR, "pvr_write: SDRAM_CFG (PVR) (Graphics memory control), grabando %x, M=%x\n",
				*(DWORD *)p, 0x15d1c951);
		}
		break;

		case 0xa05f8000 + 0x2b * 4:		// Unknown
		{
			logxmsg(LOG_PVR, "pvr_write: Unknown, grabando %x, M=%x\n",
				*(DWORD *)p, 0x0);
		}
		break;

		case 0xa05f8000 + 0x2c * 4:		// FOGTABLECOLOR (3D) (Fogging colour when using table fog)
		{
			logxmsg(LOG_PVR, "pvr_write: FOGTABLECOLOR [FOG_COL_RAM] (3D) (Fogging colour when using table fog), grabando %x\n",
				*(DWORD *)p);
		}
		break;

		PVR_WRITE_1(0xa05f8000 + 0x2d * 4, "[FOG_COL_VERT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x2e * 4, "[FOG_DENSITY], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x2f * 4, "[FOG_CLAMP_MAX], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x30 * 4, "[FOG_CLAMP_MIN], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x31 * 4, "[SPG_TRIGGER_POS], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x32 * 4, "[SPG_HBLANK_INT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x33 * 4, "[SPG_VBLANK_INT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x34 * 4, "[SPG_CONTROL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x35 * 4, "[SPG_HBLANK], grabando %x", *(DWORD *) p);

		case 0xa05f80d8: 				// FRAMETOTAL (2D) (Total number of frame cycles)
		{
			dw = *(DWORD *) p;
			logxmsg(LOG_PVR, "pvr_write: [SPG_VBLANK]\r\n");
			logxmsg(LOG_PVR, "pvr_write: número de scanlines por frame: %d\r\n", MSB(dw));
			logxmsg(LOG_PVR, "pvr_write: número de clocks por scanline: %d\r\n", LSB(dw));
		}
		break;

		case 0xa05f80e4: // TEXTURESTRIDE (3D) (Width of rectangular texture)
		{
			dw = *(DWORD *) p;
			long valor = dw & 0x1F; // 5 bits
			logxmsg(LOG_PVR, "pvr_write: TEXTURESTRIDE [SPG_WIDTH] : %02x %d\n", valor, valor);
		}
		break;

		PVR_WRITE_1(0xa05f8000 + 0x3a * 4, "[TEXT_CONTROL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x3b * 4, "[VO_CONTROL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x3c * 4, "[VO_STARTX], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x3d * 4, "[VO_STARTY], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x3e * 4, "[SCALER_CTL], grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x42 * 4, "[PAL_RAM_CTRL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x43 * 4, "[SPG_STATUS], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x44 * 4, "[FB_BURSTCTRL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x45 * 4, "[FB_C_SOF], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x46 * 4, "[Y_COEFF], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x47 * 4, "[PT_ALPHA_REF], grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x49 * 4, "PPMATRIXBASE [TA_OL_BASE] (TA) (Root PP-block matrices base address), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x4a * 4, "PRIMALLOCSTART [TA_ISP_BASE] (TA) (Primitive allocation area start), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x4b * 4, "PPALLOCSTART [TA_OL_LIMIT] (TA) (PP-block allocation area start), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x4c * 4, "PRIMALLOCEND [TA_ISP_LIMIT] (TA) (Primitive allocation area end), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x4d * 4, "PPALLOCPOS [TA_NEXT_OPB] (TA) (Current PP-block allocation position), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x4e * 4, "PRIMALLOCPOS [TA_ITP_CURRENT] (TA) (Current primitive allocation position), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x4f * 4, "TILEARRAYSIZE [TA_GLOB_TILE_CLIP] (TA) (Tile Array dimensions), grabando %x", *(DWORD *) p);
		PVR_WRITE_CB_1(0xa05f8000 + 0x50 * 4, cb_ppblocksize, "PPBLOCKSIZE [TA_ALLOC_CTRL] (TA) (PP-block sizes), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x51 * 4, "TASTART [TA_LIST_INIT] (TA) (Start vertex enqueueing strobe), grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x52 * 4, "[TA_YUV_TEX_BASE], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x53 * 4, "[TA_YUV_TEX_CTRL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x54 * 4, "[TA_YUV_TEX_CNT], grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x58 * 4, "[TA_LIST_CONT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x59 * 4, "PPALLOCEND [TA_NEXT_OPB_INIT] (TA) (PP-block allocation area end), grabando %x", *(DWORD *) p);

		// fin PVR

		case 0xa0710000: // Dreamcast RTC, reg 1
		case 0xa0710004:
		{
			logmsg("pvr_write: RTC\r\n");
		}
		break;

		default:
		if ((direccion & 0xfff00000) == 0xa0700000)
		{
			logmsg( "pvr_write: SPU Registers (%x)\r\n", direccion);
		}
		else
		if ((direccion & 0xfff00000) == 0xa0800000
		||  (direccion & 0xfff00000) == 0xa0900000)
		{
//			logmsg( "pvr_write: sound ram (%x)\r\n", direccion);
			SET_BIT(G2_FIFO, AICA_FIFO);
		}
		else
			mem_write_error(direccion, p, size);
		break;
	}
}

void video_write(unsigned long direccion, void * p, size_t size)
{ 
//	memcpy(&video_mem[direccion - video_base], p, size);
//	PutPixelN(direccion - video_base, p, size);
//	Uint8 * target = (Uint8 *) screen->pixels + (direccion - video_base);
//	long addr = direccion & 0x0FFFFFFF;
	long addr = direccion & 0x00FFFFFF;

#ifdef DEBUG_MEM_VIDEO
//	if (logvideomem)
	switch(size)
	{
		case 1:	logxmsg(LOG_MEM, "video_write: dir %x, BYTE %x\n", direccion, *(BYTE *) p);	break;
		case 2: logxmsg(LOG_MEM, "video_write: dir %x, WORD %x\n", direccion, *(WORD *) p);	break;
		case 4: logxmsg(LOG_MEM, "video_write: dir %x, DWORD %x\n", direccion, *(DWORD *) p);	break;
	}
#endif

/*    if ((direccion - video_base) < (backscreen->w * backscreen->h * (backscreen->format->BitsPerPixel / 8))
    &&  (direccion - video_base) > 0)
    {
    	SDL_LockSurface(screen);
    	memcpy(screenbase + direccion, p, size);
    	SDL_UnlockSurface(screen);
	} */

/*	if (addr >= 0x04000000 && addr <= 0x07FFFFFF)
	{
		memcpy(p, &video_mem[addr - 0x04000000], size);
	} */

/*	switch(size)
	{
		case 1:		logxmsg(LOG_PVR, "%x=%x\n", addr, *(BYTE *) p);	break;
		case 2:		logxmsg(LOG_PVR, "%x=%x\n", addr, *(WORD *) p); break;
		case 4:		logxmsg(LOG_PVR, "%x=%x\n", addr, *(DWORD *) p); break;
	} */

	if (addr < VIDEO_SIZE)
		memcpy(&video_mem[addr], p, size);
	
/*	if (addr < framebuffer_size)
	{
		SDL_LockSurface(backscreen);
		memcpy(backscreen->pixels + addr, p, size);
		SDL_UnlockSurface(backscreen); */
		refresh_screen = true;
/*	} */
}

void regmap_read(unsigned long direccion, void * p, size_t size)
{
	memcpy(p, &regmem[direccion & 0x00FFFFFF], size);

/*    if ((direccion & 0x00FF0000) == 0xD80000)
    {
        switch(size)
        {
        case 1:
        logmsg("time registers byte read: 0x%x = %x\r\n", direccion, *(BYTE *) p);
        break;
        
        case 2:
        logmsg("time registers word read: 0x%x = %x\r\n", direccion, *(WORD *) p);
        break;

        case 4:
        logmsg("time registers dword read: 0x%x = %x\r\n", direccion, *(DWORD *) p);
        break;
        
        default:
        logmsg("time registers read: ???");
        break;
        }
    } */

#ifdef DEBUG_MEM_REGISTERS
	if (logmemreg)
	switch(size)
	{
		case 1:
			logmsg( "regmap_read: dir %8x, byte %x\r\n", direccion, *(BYTE *) p);
			break;

		case 2:
			logmsg( "regmap_read: dir %8x, word %x\r\n", direccion, *(WORD *) p);
			break;

		case 4:
			logmsg( "regmap_read: dir %8x, dword %x\r\n", direccion, *(DWORD *) p);
			break;
	}
#endif
}

void regmap_write(unsigned long direccion, void * p, size_t size)
{
	memcpy(&regmem[direccion & 0x00FFFFFF], p, size);
	
/*    if ((direccion & 0x00FF0000) == 0xD80000)
    {
        switch(size)
        {
        case 1:
        logmsg("time registers byte write: 0x%x = %x\r\n", direccion, *(BYTE *) p);
        break;
        
        case 2:
        logmsg("time registers word write: 0x%x = %x\r\n", direccion, *(WORD *) p);
        break;

        case 4:
        logmsg("time registers dword write: 0x%x = %x\r\n", direccion, *(DWORD *) p);
        break;
        
        default:
        logmsg("time registers write: ???");
        break;
        }
    } */

	switch(direccion & 0x00FFFFFF)
	{
		case 0xe80000: // SCSMR2, Serial Mode Register
		{
			CHECK_BIT(SCSMR2, CHR);
			CHECK_BIT(SCSMR2, PE);
			CHECK_BIT(SCSMR2, PM);
			CHECK_BIT(SCSMR2, STOP);
			CHECK_BIT(SCSMR2, CKS1);
   			CHECK_BIT(SCSMR2, CKS0);
		}
		break;
		
		case 0xe80004: // SCBRR2, Bit Rate Register
		{
			logmsg( "SCBRR2: %x\r\n", *SCBRR2);
		}
		break;

		case 0xe80008:	// SCSCR2, Serial Control Register
		{
			CHECK_BIT(SCSCR2, TIE);
			CHECK_BIT(SCSCR2, RIE);
			CHECK_BIT(SCSCR2, TE);
			CHECK_BIT(SCSCR2, RE);
			CHECK_BIT(SCSCR2, REIE);
			CHECK_BIT(SCSCR2, CKE1);
		}
		break;
		
		case 0xe8000c:  // SCFTDR2, Transmit FIFO Data Register
  		{
  			logmsg( "SCFTDR2: %x, '%c'\r\n", *SCFTDR2, *SCFTDR2);
			fprintf(serialfp, "%c", *SCFTDR2);
			fflush(serialfp);
    	}
     	break;

		case 0xe80010:  // SCFSR2, Serial Status Register
		{
/*			CHECK_BIT(SCFSR2, PER3);
			CHECK_BIT(SCFSR2, PER2);
			CHECK_BIT(SCFSR2, PER1);
			CHECK_BIT(SCFSR2, PER0);
			CHECK_BIT(SCFSR2, FER3);
			CHECK_BIT(SCFSR2, FER2);
			CHECK_BIT(SCFSR2, FER1);
			CHECK_BIT(SCFSR2, FER0);
			CHECK_BIT(SCFSR2, ER);
			CHECK_BIT(SCFSR2, TEND);
			CHECK_BIT(SCFSR2, TDFE);
			CHECK_BIT(SCFSR2, BRK);
			CHECK_BIT(SCFSR2, FER);
			CHECK_BIT(SCFSR2, PER);
			CHECK_BIT(SCFSR2, RDF);
			CHECK_BIT(SCFSR2, DR); */

			REG_SET_BIT(SCFSR2, SCFSR2_TDFE);
			REG_SET_BIT(SCFSR2, SCFSR2_TEND);
		}
		break;
	}

// #ifdef DEBUG_MEM_REGISTERS
//	if (logmemreg)
	switch(size)
	{
		case 1:
			logxmsg(LOG_MEM, "regmap_write: dir %8x, byte %x\r\n", direccion, *(BYTE *) p);
			break;

		case 2:
			logxmsg(LOG_MEM, "regmap_write: dir %8x, word %x\r\n", direccion, *(WORD *) p);
			break;

		case 4:
			logxmsg(LOG_MEM, "regmap_write: dir %8x, dword %x\r\n", direccion, *(DWORD *) p);
			break;
	}
// #endif
}

void ram_read(unsigned long direccion, void * p, size_t size)
{
//logmsg( "ram_read: dir %8x, size %x\r\n", direccion, (int) size);
//memcpy(p, &memoria[(direccion & 0x1FFFFFFF)], size);
//	memcpy(p, &orig_mem[(direccion & 0xFFFFFF)], size);
#ifdef DEBUG_MEMORY_POINTER
	if (get_memory_pointer(direccion) == NULL)
	{	
		logxmsg(LOG_MEM, "ram_read: direccion %x invalida\n", direccion);
		return;
	}
#endif
	memcpy(p, get_memory_pointer(direccion), size);
}

void ignore_read(unsigned long direccion, void * p, size_t size)
{
}

void ignore_write(unsigned long direccion, void * p, size_t size)
{
}

void ram_write(unsigned long direccion, void * p, size_t size)
{
//logmsg( "ram_write: dir %8x, size %x\r\n", direccion, (int) size);
//memcpy(&memoria[(direccion % 0x20000000)], p, size);
//memcpy(&orig_mem[(direccion & 0xFFFFFF)], p, size);
#ifdef DEBUG_MEMORY_POINTER
	if (get_memory_pointer(direccion) == NULL)
	{	
		logxmsg(LOG_MEM, "ram_write: direccion %x invalida\n", direccion);
		return;
	}
#endif
	memcpy(get_memory_pointer(direccion), p, size);
}

void memread(unsigned long direccion, void * target, size_t size)
{
	if (mem_hash_read[direccion >> 24] == NULL) 
	{
		logxmsg(LOG_MEM, "memread: mem_hash_read is NULL for bank %02x\r\n", (direccion >> 24));
		return;
	}

	(*mem_hash_read[direccion >> 24]) (direccion, target, size);
#ifdef DEBUG_MEM_READ
//	if (filelogging & FILELOG_MEMREADS)
	switch(size)
	{
		case 1:
			logxmsg(LOG_MEM, "memread: dir %8x, byte %x\r\n", direccion, *(BYTE *) target);
			break;

		case 2:
			logxmsg(LOG_MEM, "memread: dir %8x, word %x\r\n", direccion, *(WORD *) target);
			break;

		case 4:
			logxmsg(LOG_MEM, "memread: dir %8x, dword %x\r\n", direccion, *(DWORD *) target);
			break;
	}
#endif
}

void memwrite(unsigned long direccion, void * source, size_t size)
{
	if (mem_hash_write[direccion >> 24] == NULL) 
	{
		logxmsg(LOG_MEM, "memwrite: mem_hash_write is NULL for bank %02x\r\n", (direccion >> 24));
		return;
	}
	
	(*mem_hash_write[direccion >> 24]) (direccion, source, size);
#ifdef DEBUG_MEM_WRITE
//	if (filelogging & FILELOG_MEMWRITES)
	switch(size)
	{
		case 1:
			logxmsg(LOG_MEM, "memwrite: dir %8x, byte %x\r\n", direccion, *(BYTE *) source);
			break;

		case 2:
			logxmsg(LOG_MEM, "memwrite: dir %8x, word %x\r\n", direccion, *(WORD *) source);
			break;

		case 4:
			logxmsg(LOG_MEM, "memwrite: dir %8x, dword %x\r\n", direccion, *(DWORD *) source);
			break;
	}
#endif
}

#ifndef MEMORY_MACROS
void ReadMemoryF(unsigned long direccion, float * valor)
{
/*	unsigned long realdir = direccion % 0x20000000;

	memid(direccion);

#ifdef DEBUG_MEM
	logmsg( "Leyendo float en dir %x, real %x\r\n", direccion, realdir);
#endif

	if (realdir >= n_video_base && realdir <= n_video_base + video_size)
	{
		*valor = flunpack(&video_mem[realdir - n_video_base]);
	}
	else
	if (realdir >= mem_n_base && realdir <= mem_n_base + mem_size)
	{
		*valor = flunpack(&memoria[realdir - mem_n_base]);
	}
	else
	{
		logmsg( "ReadMemoryF: direccion %x invalida", direccion);
		*valor = 0;
	}

#ifdef DEBUG_MEM
	logmsg( "Leído: %f\r\n", *valor);
#endif */
	memread(direccion, valor, sizeof(DWORD));
}

void ReadMemoryB(unsigned long direccion, unsigned char * valor)
{
/*	unsigned long realdir = direccion % 0x20000000;

	BYTE * addr = (BYTE *) memid(direccion);

#ifdef DEBUG_MEM
	logmsg( "Leyendo byte en dir %x, real %x\r\n", direccion, realdir);
#endif

	if (addr == NULL)
	{
		logmsg( "ReadMemoryB: direccion %x invalida\r\n", direccion);
		*valor = 0;
	}
	else
		*valor = *addr;

#ifdef DEBUG_MEM
	logmsg( "Leído: %x\r\n", *valor);
#endif */
	memread(direccion, valor, sizeof(BYTE));
}

void ReadMemoryL(unsigned long direccion, DWORD * valor)
{
/*	unsigned long realdir = direccion % 0x20000000;

	DWORD * addr = (DWORD *) memid(direccion);

#ifdef DEBUG_MEM_READ
	logmsg( "Leyendo dword en dir %x, real %x\r\n", direccion, realdir);
#endif

	if (addr == NULL)
	{
		logmsg( "ReadMemoryL: direccion %x invalida\r\n", direccion);
		*valor = 0;
	}
	else
		*valor = *addr;

#ifdef DEBUG_MEM_READ
	logmsg( "Leído: %x\r\n", *valor);
#endif
*/
	memread(direccion, valor, sizeof(DWORD));
}

void ReadMemoryW(unsigned long direccion, WORD * valor)
{
/*	unsigned long realdir = direccion % 0x20000000;

	WORD * addr = (WORD *) memid(direccion);

#ifdef DEBUG_MEM_READ
	logmsg( "Leyendo word en dir %x, real %x\r\n", direccion, realdir);
#endif

	if (addr)
        *valor = *addr;
	else
	{
		logmsg( "ReadMemoryW: direccion %x invalida\r\n", direccion);
		*valor = 0;
	}

#ifdef DEBUG_MEM_READ
	logmsg( "Leído: %x\r\n", *valor);
#endif
*/
	memread(direccion, valor, sizeof(WORD));
}

void WriteMemoryW(unsigned long direccion, WORD * valor)
{
/*	WORD * addr = (WORD *) memid(direccion);

	unsigned long realdir = direccion % 0x20000000;

#ifdef DEBUG_MEM_WRITE
	logmsg( "WORD %04x -> %08x\r\n", valor, direccion);
#endif

	if (addr == NULL)
	{
		logmsg( "WriteMemoryW: direccion %x invalida, valor %x\r\n", direccion, valor);
	}
	else
	{
		*addr = valor;

		if (realdir >= n_video_base && realdir <= n_video_base + video_size)
		{
//			*(WORD *) &video_mem[realdir - n_video_base] = valor;
			PutPixelW(realdir - n_video_base, valor);
		}
	} */
	memwrite(direccion, valor, sizeof(WORD));
}

void WriteMemoryL(unsigned long direccion, DWORD * valor)
{
/*	DWORD * addr = (DWORD *) memid(direccion);

	unsigned long realdir = direccion % 0x20000000;

#ifdef DEBUG_MEM_WRITE
	logmsg( "DWORD %08x -> %08x\r\n", valor, direccion);
#endif

	if (addr == NULL)
	{
		logmsg( "WriteMemoryL: direccion %x invalida, valor %x\r\n", direccion, valor);
	}
	else
	{
		*addr = valor;
		if (realdir >= n_video_base && realdir <= n_video_base + video_size)
		{
			PutPixelL(realdir - n_video_base, (DWORD) valor);
		}
	} */
	memwrite(direccion, valor, sizeof(DWORD));
}

void WriteMemoryB(unsigned long direccion, BYTE * valor)
{
/*	BYTE * addr = (BYTE *) memid(direccion);

	unsigned long realdir = direccion % 0x20000000;

#ifdef DEBUG_MEM_WRITE
	logmsg( "BYTE %x -> %08x\r\n", valor, direccion);
#endif

	if (addr == NULL)
	{
		logmsg( "WriteMemoryB: direccion %x invalida, valor %x\r\n", direccion, valor);
	}
	else
	{
		if (realdir >= n_video_base && realdir <= n_video_base + video_size)
		{
			PutPixel(realdir - n_video_base, valor);
		}
		*addr = valor;
	} */
	memwrite(direccion, valor, sizeof(BYTE));
}

void WriteMemoryF(unsigned long direccion, float * valor)
{
/*	memid(direccion);

	direccion = direccion % 0x20000000;

#ifdef DEBUG_MEM_WRITE
	logmsg( "FLOAT %f -> %08x\r\n", valor, direccion);
#endif

	if (direccion >= n_video_base && direccion <= n_video_base + video_size)
	{
		* (float *) &video_mem[direccion - n_video_base] = valor;

		PutPixel(direccion - n_video_base, valor);
	}
	else
	if (direccion >= mem_n_base && direccion <= mem_n_base + mem_size)
	{
		* (float *) &memoria[direccion - mem_n_base] = valor;
	}
	else
	{
		logmsg( "WriteMemoryF: direccion %x invalida, valor %x\r\n", direccion, valor);
	} */
	memwrite(direccion, valor, sizeof(DWORD));
}
#endif

