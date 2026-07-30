// #include <windows.h>
#include <time.h>
#include "main.h"
#include "intc.h"
#include "opcodes.h"
#include "mem.h"
#include "graficos.h"
#include "intc.h"
#include "gui.h"
#include "gdrom.h"
#include "opciones.h"
#include "sistema.h"
#include "traza.h"
#include "mmu.h"
#include "wdt.h"
#include "tmu.h"
#include "syscontrol.h"		/* ta_procesar_bloque(), para el CH2 DMA */


#define DWREF(p) (*(DWORD *)(p))

/* Registros del AICA. No se emula el sintetizador, pero tienen que tener
   respaldo real: el boot ROM escribe y vuelve a leer. Ver la fase 5 del plan. */
#define AICA_REG_BASE	0x00700000
#define AICA_REG_SIZE	0x00008000		// 32 KB: canales, comunes y el bloque 0x4xxx

/* RAM de sonido, 2 MB. Estaba reservada desde siempre y nunca se mapeo. */
#define SOUND_BASE		0x00800000

/* El bloque de control del sistema: 0x005F0000-0x005FFFFF sobre control_mem. */
#define ES_CONTROL(fisica)	(((fisica) & 0x00FF0000) == 0x005F0000)
// extern SDL_mutex * regmap_mutex; // en main.c

DWORD SQ0[8];
DWORD SQ1[8];

unsigned char * memoria;
unsigned char * video_mem;
unsigned char * regmem;
unsigned char * bios_mem;
unsigned char * ta_mem;
unsigned char * control_mem;
unsigned char * sound_mem;
unsigned char * aica_mem;

/* Respaldo de las zonas sin mapear. get_memory_pointer() no chequea nada, asi
   que sin esto cualquier acceso a una zona libre es una desreferencia nula.
   Ver la fase 1.3 del plan. */
unsigned char * descarte_mem;
#define DESCARTE_SIZE	(16 * 1024 * 1024)	// el rango de 24 bits de una zona

BYTE	stack_mem[256 * 1024];	// la haremos de 256kb...?

unsigned char * mem_zone[0x100]; // para las zonas de memoria

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

/* Contadores de escrituras a RAM de video, solo con --traza-mem. Sirven para
   distinguir "el guest no dibuja" de "dcemu lee del sitio equivocado". */
DWORD traza_video_escrituras = 0;
DWORD traza_video_bytes = 0;
DWORD traza_video_ventanas = 0;
long  traza_video_min = 0x7FFFFFFF;
long  traza_video_max = -1;

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

/*	memoria = memoria - mem_n_base; // lo corremos n_base bytes hacia atr�s,
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

	bios_mem = (unsigned char *) malloc(sizeof(unsigned char) * BIOS_SIZE); // 2 MB

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


	sound_mem = (unsigned char *) calloc(1, SOUND_SIZE); // 2 MB

	if (!sound_mem)
	{
		fprintf(stderr, "No se pudo crear sound_mem.\r\n");
		return 1;
	}

	aica_mem = (unsigned char *) calloc(1, AICA_REG_SIZE);

	if (!aica_mem)
	{
		fprintf(stderr, "No se pudo crear aica_mem.\r\n");
		return 1;
	}

	descarte_mem = (unsigned char *) calloc(1, DESCARTE_SIZE);

	if (!descarte_mem)
	{
		fprintf(stderr, "No se pudo crear descarte_mem.\r\n");
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
		sprintf(buf, "r%d=%lx ", i, R(i));
		strcat(buf2, buf);
	}
	
    strcat(buf2, "\r\n");

	for (i = 0; i < 32; i++)
	{
// 		sprintf(buf, "FR%d=%lx ", i, float_to_dword(FR(i)));
// 		strcat(buf2, buf);
	}

    strcat(buf2, "\r\n");
	sprintf(buf, "T=%d, FPUL=%lx,%f MACL=%lx,%ld MACH=%lx,%ld\r\n", IS_SR_T() ? 1 : 0,
 		(DWORD) FPUL, (float) FPUL,
 		(DWORD) MACL, (signed long) MACL,
		(DWORD) MACH, (signed long) MACH);
	strcat(buf2, buf);
	
	sprintf(buf, "SR=%08lx MD:%d RB:%d\n", SR, IS_SH4_REG_SET(SR_MD) ? 1 : 0, IS_SH4_REG_SET(SR_RB) ? 1 : 0);
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
	/* Registros de la MMU (fase 1.1 de docs/mmu-plan.md). Los atiende
	   regmap_read/regmap_write como al resto del bloque en pagina 0xFF; lo
	   unico que hace falta es que existan y arranquen en cero. */
	PTEH	= (DWORD *) &regmem[0x000000];	*PTEH = 0;
	PTEL	= (DWORD *) &regmem[0x000004];	*PTEL = 0;
	TTB		= (DWORD *) &regmem[0x000008];	*TTB = 0;
	TEA		= (DWORD *) &regmem[0x00000C];	*TEA = 0;
	MMUCR	= (DWORD *) &regmem[0x000010];	*MMUCR = 0;
	PTEA	= (DWORD *) &regmem[0x000034];	*PTEA = 0;

	mmu_reset();
	wdt_reset();
	tmu_reset();

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
	
	// Las zonas libres apuntan a un bloque de descarte en vez de quedar en
	// NULL: los handlers de error siguen reportando, pero un acceso perdido
	// ya no tumba el emulador. Ojo con el <=: la zona 0xFF tambien cuenta.
	for (i = 0; i <= 0xFF; i++)
	{
		mem_hash_read[i] = mem_read_error;
  		mem_hash_write[i] = mem_write_error;
  		mem_zone[i] = descarte_mem;
	}

	// -------------------------------------------------------------------------------
	// Inicializacion para traz las zonas de la memoria para 
 	// la traduccion de logico a f�sica. 
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
 	
	// Inicializacion de la tabla de funciones ten�a acceso 
 	// a bloques de la memoria	
	// La ventana fisica de los 16 MB bajos. Iba solo a bios_read, que cubre el
	// boot ROM y la flash pero nada mas: por eso KOS perdia las escrituras a la
	// RAM de sonido (0x00800000), donde carga el firmware ARM de la AICA.
	// pvr_read/pvr_write reparten por direccion fisica y ya delegan el rango de
	// la BIOS en bios_read, asi que cubren las dos cosas.
	mem_hash_read[0x00] = pvr_read;
	mem_hash_write[0x00] = pvr_write;
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
	// 0x11000000-0x11FFFFFF es la FIFO de texturas del TA: lo que se escribe
	// ahi va a la RAM de video con el mismo desplazamiento, que es lo que hace
	// video_write. Sin esto, las texturas que sube KOS por store queue se
	// perdian. 0x13 es su espejo.
	mem_hash_write[0x11] = video_write;
	mem_hash_write[0x13] = video_write;
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

	// Los 16 MB de RAM se repiten cuatro veces en cada ventana de 64 MB, y el
	// software cuenta con eso: el _start de KOS decide si la maquina tiene 16 o
	// 32 MB escribiendo en 0xACFFFFFF y en 0xADFFFFFF y comparando. Sin los
	// espejos, la segunda escritura se pierde, la relectura da 0, KOS cree que
	// hay 32 MB y arma la pila en 0x8E000000 -- una zona que no existe, asi que
	// todo push se descarta y todo pop devuelve basura.
	//
	// mem_zone[] ya tenia los espejos (get_memory_pointer resolvia bien); lo
	// que faltaba era enlazarlos en las tablas de handlers.
	for (i = 1; i < 4; i++)
	{
		mem_hash_read[0x0C + i]  = ram_read;
		mem_hash_read[0x8C + i]  = ram_read;
		mem_hash_read[0xAC + i]  = ram_read;

		mem_hash_write[0x0C + i] = ram_write;
		mem_hash_write[0x8C + i] = ram_write;
		mem_hash_write[0xAC + i] = ram_write;
	}

	// La ventana de control P4: 0xF0-0xF7 son los arreglos de la cache y de la
	// TLB. Hasta ahora caian en mem_read_error/mem_write_error, asi que un
	// binario de KOS inundaba --traza-mem con las 512 escrituras del barrido de
	// la cache de operandos. Ver docs/mmu-plan.md, fases 1.2 y 1.3.
	for (i = MMU_P4_IC_DIR; i <= MMU_P4_UTLB_DAT; i++)
	{
		mem_hash_read[i]  = mmu_p4_read;
		mem_hash_write[i] = mmu_p4_write;
	}
}

void mem_read_error(unsigned long direccion, void * p, size_t size)
{
	// Devolver ceros en vez de dejar el destino como estaba: si no, el
	// programa emulado lee lo que hubiera en la variable del llamador y la
	// corrida no es reproducible.
	if (p)
		memset(p, 0, size);

	traza_acceso(TRAZA_LECTURA, direccion, size, PC);

	logxmsg(LOG_MEM, "mem_read_error: dir %x, tama�o %d\r\n", direccion, size);
//	dump_registers();
}

void mem_write_error(unsigned long direccion, void * p, size_t size)
{
	traza_acceso(TRAZA_ESCRITURA, direccion, size, PC);

	switch(size)
	{
		case 1:	logxmsg(LOG_MEM, "mem_write_error: dir %x, byte %02x %d '%c'\n", direccion, *(BYTE *)p, *(BYTE *)p, *(BYTE *)p);	break;
		case 2: logxmsg(LOG_MEM, "mem_write_error: dir %x, word %04x %d\n", direccion, *(WORD *) p, *(WORD *)p);					break;
		case 4: logxmsg(LOG_MEM, "mem_write_error: dir %x - %02x, dword %08x %d\n", direccion, (direccion % 0x100) / 4, *(DWORD *)p, *(DWORD *)p);			break;
		default: logxmsg(LOG_MEM, "mem_write_error: size %d\n", size);	break;
	}
//	dump_registers();
}

void sq_write(unsigned long direccion, void * p, size_t size)
{
    // El bit 5 elige la cola; los cinco bits bajos son el desplazamiento
    // dentro de los 32 bytes.
    DWORD *       sq  = (direccion & 0x20) ? SQ1 : SQ0;
    unsigned long off = direccion & 0x1F;

    // Antes esto hacia "SQ[pos] = *(DWORD *) p" e ignoraba size, o sea que
    // copiaba 4 bytes y nada mas. KOS llena la store queue con fmov.d, de 8
    // bytes, asi que cada escritura perdia su mitad alta: quedaban buenos los
    // dwords pares y basura los impares. En un vertice del TA eso daba x y z
    // corruptos (dwords 1 y 3) con y bueno (dword 2).
    if (off + size > sizeof(DWORD) * 8)
        size = sizeof(DWORD) * 8 - off;

    memcpy((unsigned char *) sq + off, p, size);
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
	
//	memcpy(p, &video_mem[addr], size);
    switch(size)
    {
        case 1:		*(BYTE *)p = *(BYTE *) &video_mem[addr];		break;
        case 2:		*(WORD *)p = *(WORD *) &video_mem[addr];		break;
        case 4:		*(DWORD *)p = *(DWORD *) &video_mem[addr];	break;
		default:	memcpy(p, &video_mem[addr], size);   		break;
    }

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
	unsigned long fisica = direccion & 0x00FFFFFF;

	/* SB_G1SYSM no es de la lectora aunque caiga en su rango. Va antes que
	   GDROM_ES_REGISTRO() justamente por eso. Ver sistema.h. */
	if (fisica == G1_SYSM)
	{
		dw = G1_SYSM_RETAIL;
		memcpy(p, &dw, size > sizeof(dw) ? sizeof(dw) : size);
		return;
	}

	if (GDROM_ES_REGISTRO(fisica))
	{
		gdrom_read(direccion, p, size);
		return;
	}

	/* Las etiquetas de abajo estan escritas en forma P2 (0xa0...), pero esta
	   funcion atiende tambien la ventana fisica (zona 0x00), por donde KOS
	   llega a la RAM de sonido y a la AICA. Normalizar aca hace que las dos
	   ventanas se comporten igual en vez de que una vea los registros vivos y
	   la otra solo el respaldo. */
	switch(fisica | 0xa0000000)
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

		/*** CH2 DMA. SB_C2DST siempre se lee 0: la transferencia se hace
		     entera dentro de la escritura que la arranca. ***/
		case 0xa05f6800: // SB_C2DSTAT
		{
			memcpy(p, &SB_C2DSTAT, size);
		}
		break;

		case 0xa05f6804: // SB_C2DLEN
		{
			memcpy(p, &SB_C2DLEN, size);
		}
		break;

		case 0xa05f6808: // SB_C2DST
		{
			memcpy(p, &SB_C2DST, size);
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
			logmsg( "pvr_read: MAPLE_STATE: %x\r\n", MAPLE_STATE);
			if (MAPLE_STATE & 0x1)
			{
				logmsg( "MAPLE_STATE:Desactivando DMA\r\n");
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

		case 0xa05f8004: // REVISION [CORETYPE], version del nucleo PVR2
		{
			/*
				0x11 en una consola de serie, y el boot ROM lo verifica: en
				0x8C0DBC7A compara COREID contra 0x17FD11DB y despues exige que
				esta revision sea distinta de 1 y **mayor o igual que 17**. Si
				no, deja su variable de estado en 5, nunca la sube a 6 y termina
				colgado a proposito en el BRA de 0x8C0DBDFC.

				Sin este caso la lectura caia en el respaldo del bloque de
				control, que vale cero, y cero no llega a 17. Es el mismo tipo
				de trampa que SB_G1SYSM: un registro de identificacion que
				contesta cualquier cosa y manda al software por el camino
				equivocado. Ver docs/bios-boot-plan.md.
			*/
			dw = 0x00000011;
			memcpy(p, &dw, size);
			logxmsg(LOG_PVR, "pvr_read: REVISION\n");
		}
		break;

		case 0xa05f8000 + 0x11 * 4:		// FB_R_CTRL
		{
			memcpy(p, &pvr_fb_r_ctrl, size);
			logxmsg(LOG_PVR, "pvr_read: FB_R_CTRL\r\n");
		}
		break;

		case 0xa05f80cc: // SPG_VBLANK_INT
		{
			memcpy(p, &pvr_spg_vblank_int, size);
			logxmsg(LOG_PVR, "pvr_read: SCANINTPOS\n");
		}
		break;
		
		case 0xa05f80d0: // SPG_CONTROL
		{
//			dw = 0x100; // VGA
			memcpy(p, &pvr_spg_control, sizeof(DWORD));
			logxmsg(LOG_PVR, "pvr_read: SPG_CONTROL\n");
		}
		break;
		
		case 0xa05f80d4: // SPG_HBLANK
		{
			memcpy(p, &pvr_spg_hblank, sizeof(DWORD));
			logxmsg(LOG_PVR, "pvr_read: SPG_HBLANK\n");
		}
		break;

		case 0xa05f80d8: // SPG_LOAD
		{
			memcpy(p, &pvr_spg_load, sizeof(DWORD));
			logxmsg(LOG_PVR, "pvr_read: SPG_LOAD\n");
		}
		break;
		
		case 0xa05f80dc: // SPG_VBLANK
		{
			memcpy(p, &pvr_spg_vblank, sizeof(DWORD));
			logxmsg(LOG_PVR, "pvr_read: SPG_VBLANK\n");
		}
		break;
		
		case 0xa05f80e0: // SPG_WIDTH 
		{
			memcpy(p, &pvr_spg_width, sizeof(DWORD));
			logxmsg(LOG_PVR, "pvr_read: SPG_WIDTH\n");
		}
		break;
		
		case 0xa05f80e8: // BITMAPTYPE2
		{
			dw = 0x00160000;
			memcpy(p, &dw, size);
			logxmsg(LOG_PVR, "pvr_read: BITMAPTYPE2\r\n");
		}
		break;

		case 0xa05f8050:
		{
			dw = 0x00100203;
			memcpy(p, &dw, size);
		}
		break;

		case 0xa05f810c: // SPG_STATUS
		{
			/*
				No es solo el numero de linea. Los bits altos llevan la
				sincronia, y el boot ROM real se queda esperando el vsync antes
				de seguir: el bucle de 0x8C00CB2E lee este registro y repite
				mientras el bit 13 valga cero.

				  9:0  linea de barrido
				  10   numero de campo
				  11   blanking vertical
				  12   hsync
				  13   vsync

				El vsync dura SPG_WIDTH.vswidth lineas al principio del cuadro,
				y el blanking va de SPG_VBLANK.vbstart hasta vbend, que envuelve
				por el final. hsync y el numero de campo quedan en cero: dcemu
				no lleva posicion horizontal ni entrelazado.
			*/
			DWORD linea   = (DWORD) pvr_scanline;
			DWORD vswidth = (pvr_spg_width >> 8) & 0x0F;
			DWORD vbstart = pvr_spg_vblank & 0x3FF;
			DWORD vbend   = (pvr_spg_vblank >> 16) & 0x3FF;

			dw = linea & 0x3FF;

			/* Sin vswidth programado no habria vsync nunca, y quien lo espera
			   se cuelga. Una linea es el minimo que tiene sentido. */
			if (vswidth == 0)
				vswidth = 1;

			if (linea < vswidth)
				dw |= 0x2000;			/* vsync */

			if (linea >= vbstart || linea < vbend)
				dw |= 0x0800;			/* blanking vertical */

			memcpy(p, &dw, size);
		}
		break;

		case 0xa05f8114:
		{
			dw = 0;
			memcpy(p, &dw, size);
			logxmsg(LOG_PVR, "pvr_read: FB_C_SOF: framebuffer current read address\n");
		}
		break;

		case 0xa05f8128:	// TA_ISP_BASE, PVR_TA_VERTBUF_START
		{
      		memcpy(p, &pvr_ta_isp_base, size);
		}
		break;

		case 0xa05f8138:	// TA_ITP_CURRENT, PVR_TA_VERTBUF_POS
		{
      		memcpy(p, &pvr_ta_itp_current, size);
		}
		break;

		case 0xa05f8144:
		{
			dw = 0;
			memcpy(p, &dw, size);
			logxmsg(LOG_PVR, "pvr_read: ta_init\r\n");
		}
		break;

		// El RTC cuenta segundos desde 1950 partidos en dos registros de 16
		// bits. Se toma del reloj del host.
		case 0xa0710000:
		{
			dw = sistema_rtc_alto();
			memcpy(p, &dw, size);
		}
		break;

		case 0xa0710004:
		{
			dw = sistema_rtc_bajo();
			memcpy(p, &dw, size);
		}
		break;

		default:
		{
			// Bloque de control del sistema. Antes se copiaba desde
			// control_mem y ademas se reportaba el acceso como error; ahora
			// es respaldo de verdad y solo el switch de arriba dispara
			// trabajo. Ver la fase 2.2 del plan.
			if (ES_CONTROL(fisica))
			{
				memcpy(p, &control_mem[fisica & 0xFFFF], size);
				logxmsg(LOG_MEM, "leyendo desde control_mem: DW %x\n", *(DWORD *) p);
				break;
			}

			// RAM de sonido: 2 MB en 0x00800000. Estaba reservada y sin mapear.
			if (fisica >= SOUND_BASE && fisica + size <= SOUND_BASE + SOUND_SIZE)
			{
				memcpy(p, &sound_mem[fisica - SOUND_BASE], size);
				break;
			}

			// Registros del AICA: respaldo real, sin sintetizador detras.
			if (fisica >= AICA_REG_BASE && fisica + size <= AICA_REG_BASE + AICA_REG_SIZE)
			{
				memcpy(p, &aica_mem[fisica - AICA_REG_BASE], size);
				break;
			}

			// Bus G2, area de dispositivos externos (modem, BBA). No se emula
			// ninguno, pero hay que contestar algo fijo.
			if (fisica >= G2_EXT_BASE && fisica <= G2_EXT_FIN)
			{
				dw = G2_EXT_VACIO;
				memcpy(p, &dw, size);
				break;
			}

			// Boot ROM y flash.
			if (fisica < BIOS_SIZE
			||  (fisica >= FLASH_BASE && fisica < FLASH_BASE + FLASH_SIZE))
			{
				bios_read(direccion, p, size);
				break;
			}

			mem_read_error(direccion, p, size);
		}
		break;
	}
}

void bios_read(unsigned long direccion, void * p, size_t size)
{
	// El area de ROM son 4 MB de espacio de direcciones: los 2 primeros MB son
	// el boot ROM y en 0x200000 estan los 128 KB de flash con la configuracion
	// del sistema (region, idioma, fecha, nombre de la consola).
	unsigned long fisica = direccion & 0x3FFFFF;

	if (ES_CONTROL(direccion))
	{
		memcpy(p, &control_mem[direccion & 0xFFFF], size);
		logxmsg(LOG_MEM, "leyendo control_mem en bios_read, dir %x\n", direccion);
		return;
	}

	if (fisica >= FLASH_BASE && fisica + size <= FLASH_BASE + FLASH_SIZE)
	{
		memcpy(p, &flash_mem[fisica - FLASH_BASE], size);
		return;
	}

	if (fisica + size <= BIOS_SIZE)
	{
		memcpy(p, &bios_mem[fisica], size);
		return;
	}

	// Entre el final de la flash y los 4 MB no hay nada. Antes se leia fuera
	// de bios_mem, que solo tiene 2 MB reservados.
	mem_read_error(direccion, p, size);
}

/* void bios_write(unsigned long direccion, void * p, size_t size)
{
	memcpy(&bios_mem[direccion % 0x20000000], p, size);
} */

void ta_write(unsigned long direccion, void * p, size_t size)
{
	/* La FIFO del TA no es memoria direccionable: la direccion dentro de la
	   ventana no importa, solo cual de las dos store queues llego. KOS escribe
	   con direcciones crecientes (sq_cpy incrementa el destino), asi que usar
	   el desplazamiento crudo se pasaba de TA_SIZE y descartaba todo despues de
	   512 bytes -- y peor, dejaba de coincidir con el indice que usa pref142()
	   para releer el registro, que entonces veia siempre el mismo dato viejo.

	   Los dos usan ahora & 0x3F: dos ranuras de 32 bytes, una por store queue. */
	memcpy(&ta_mem[direccion & 0x3F], p, size);
}

/*
	CH2 DMA -- el canal por el que el guest sube geometria y texturas al TA sin
	pasar por las store queues. Lo conduce el Holly, no el DMAC: el SH-4 solo
	pone el origen en SAR2 y arma CHCR2 en modo de peticion externa, y el
	arranque real es escribir 1 en SB_C2DST. Por eso dma_check() no lo ve --
	solo atiende los canales en auto-request-- y por eso el canal se quedaba
	colgado sin que nada lo reportara.

	Es lo que detenia el arranque por BIOS: el boot ROM anota la transferencia
	en su tabla de descriptores con los bits 0-1 en "en curso" y espera a que
	los baje el fin de DMA, que no llegaba nunca. Ver docs/bios-boot-plan.md.

	La transferencia se hace entera y de una vez, como el resto de los DMA de
	dcemu (Maple, GD-ROM). El destino manda:

	  - 0x10000000-0x107FFFFF es la FIFO de poligonos: cada bloque de 32 bytes
	    va al decodificador del TA, igual que si lo hubiera vaciado una store
	    queue;
	  - cualquier otro destino (FIFO de texturas en 0x11xxxxxx, RAM de video)
	    es una copia y ya.
*/
static void ch2_dma_ejecutar(void)
{
	DWORD	origen  = *SAR2;
	DWORD	destino = SB_C2DSTAT;
	DWORD	largo   = SB_C2DLEN;
	DWORD	i;

	if (traza_activa)
		fprintf(stderr, "traza: CH2 DMA %08lx -> %08lx, %lu bytes\n",
			(unsigned long) origen, (unsigned long) destino,
			(unsigned long) largo);

	if ((destino & 0xFF800000) == 0x10000000)
	{
		for (i = 0; i + 32 <= largo; i += 32)
		{
			BYTE bloque[32];

			memread_fisico(origen + i, bloque, 32);
			ta_procesar_bloque(bloque);
		}
	}
	else
	{
		for (i = 0; i + 4 <= largo; i += 4)
		{
			DWORD palabra;

			memread_fisico(origen + i, &palabra, 4);
			memwrite_fisico(destino + i, &palabra, 4);
		}
	}

	/* Como la deja el hardware al terminar: el canal libre, el largo consumido
	   y el origen avanzado. El DMAC tambien marca su fin -- el guest puede
	   estar mirando CHCR2.TE en vez de la interrupcion. */
	*SAR2    = origen + largo;
	*DMATCR2 = 0;
	*CHCR2  |= 0x2;			/* TE: transfer end */

	SB_C2DLEN = 0;
	SB_C2DST  = 0;

	intc_add(ASIC_EVT_PVR_DMA, 0);
}

void pvr_write(unsigned long direccion, void * p, size_t size)
{
	DWORD dw;
	bool last;
	unsigned long fisica = direccion & 0x00FFFFFF;

	if (GDROM_ES_REGISTRO(fisica))
	{
		gdrom_write(direccion, p, size);
		return;
	}

	// Respaldo del bloque de control: todo 0x005F0000-0x005FFFFF se escribe de
	// verdad, y el switch de abajo queda para los registros que ademas
	// disparan trabajo. Ver la fase 2.2 del plan.
	if (ES_CONTROL(fisica))
	{
		memcpy(&control_mem[fisica & 0xFFFF], p, size);
	}

	/* Igual que en pvr_read: las etiquetas son P2 y la ventana fisica tiene que
	   resolver a lo mismo. */
	switch(fisica | 0xa0000000)
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

		/*** CH2 DMA: la subida al TA. Ver ch2_dma_ejecutar(). ***/
		case 0xa05f6800: // SB_C2DSTAT
		{
			memcpy(&SB_C2DSTAT, p, size);
		}
		break;

		case 0xa05f6804: // SB_C2DLEN
		{
			memcpy(&SB_C2DLEN, p, size);
		}
		break;

		case 0xa05f6808: // SB_C2DST
		{
			memcpy(&dw, p, size);

			/* Escribir 0 con una transferencia en curso la aborta; aca nunca
			   hay una en curso, porque se hacen enteras. */
			if (dw & 1)
				ch2_dma_ejecutar();
			else
				SB_C2DST = 0;
		}
		break;

		case 0xa05f6900: // SB_ISTNRM, acuse de los eventos normales
		{
			memcpy(&dw, p, size);
			REMOVE_BIT(ASIC_ACK_A, dw);

			/* El acuse del guest es lo unico que baja un evento pendiente.
			   check_ints() ya no los tira por su cuenta cuando la mascara no
			   los deja pasar; ver el comentario en intc.c. */
			REMOVE_BIT(intc_queuemask, dw);

			logxmsg(LOG_INTC, "pvr_write: ASIC_ACK_A: quitando bits %x, quedamos en %x\r\n", dw, ASIC_ACK_A);
		}
		break;

		case 0xa05f6904: // Pending Interrupts 2
		{
			memcpy(&dw, p, size);
			REMOVE_BIT(ASIC_ACK_B, dw);
			logxmsg(LOG_INTC, "pvr_write: ASIC_ACK_B: quitando bits %x, quedamos en %x\r\n", dw, ASIC_ACK_B);
		}
		break;

		case 0xa05f6908: // Pending Interrupts 3
		{
			memcpy(&dw, p, size);
			REMOVE_BIT(ASIC_ACK_C, dw);
			logxmsg(LOG_INTC, "pvr_write: ASIC_ACK_C: quitando bits %x, quedamos en %x\r\n", dw, ASIC_ACK_C);
		}
		break;

		case 0xa05f6910: // Enabled Interrupts IRQD_A
		{
			memcpy(&ASIC_IRQD_A, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQD_A: %x\r\n", ASIC_IRQD_A);
		}
		break;

		case 0xa05f6914: // Enabled Interrupts IRQD_B
		{
			memcpy(&ASIC_IRQD_B, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQD_B: %x\r\n", ASIC_IRQD_B);
		}
		break;

		case 0xa05f6918: // Enabled Interrupts IRQD_C
		{
			memcpy(&ASIC_IRQD_C, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQD_C: %x\r\n", ASIC_IRQD_C);
		}
		break;

		case 0xa05f6920: // Enabled Interrupts IRQB_A
		{
			memcpy(&ASIC_IRQB_A, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQB_A: %x\r\n", ASIC_IRQB_A);
		}
		break;

		case 0xa05f6924: // Enabled Interrupts IRQB_B
		{
			memcpy(&ASIC_IRQB_B, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQB_B: %x\r\n", ASIC_IRQB_B);
		}
		break;

		case 0xa05f6928: // Enabled Interrupts IRQB_C
		{
			memcpy(&ASIC_IRQB_C, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQB_C: %x\r\n", ASIC_IRQB_C);
		}
		break;

		case 0xa05f6930: // Enabled Interrupts IRQ9_A
		{
			memcpy(&ASIC_IRQ9_A, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQ9_A: %x\r\n", ASIC_IRQ9_A);
		}
		break;

		case 0xa05f6934: // Enabled Interrupts IRQ9_B
		{
			memcpy(&ASIC_IRQ9_B, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQ9_B: %x\r\n", ASIC_IRQ9_B);
		}
		break;

		case 0xa05f6938: // Enabled Interrupts IRQ9_C
		{
			memcpy(&ASIC_IRQ9_C, p, size);
			logxmsg(LOG_INTC, "pvr_write: ASIC_IRQ9_C: %x\r\n", ASIC_IRQ9_C);
		}
		break;

		case 0xa05f6940: // SB_PDTNRM	PVR-DMA trigger select from normal interrupt
		{
			memcpy(&SB_PDTNRM, p, size);
			logxmsg(LOG_INTC, "SB_PDTNRM	PVR-DMA trigger select from normal interrupt: %x\n", SB_PDTNRM);
		}
		break;

		case 0xa05f6944: // SB_PDTEXT	PVR-DMA trigger select from external interrupt
		{
			memcpy(&SB_PDTEXT, p, size);
			logxmsg(LOG_INTC, "SB_PDTEXT	PVR-DMA trigger select from external interrupt: %x\n", SB_PDTEXT);
		}
		break;

		/*** MAPLE ***/
		case 0xa05f6c04: // MAPLE_DMAADDR
		{
			memcpy(&MAPLE_DMAADDR, p, size);
//			logmsg( "pvr_write: MAPLE_DMAADDR: %x\r\n", (MAPLE_DMAADDR & 0x1FFFFFE0));
		}
		break;
		
		case 0xa05f6c10: // SB_MDTSEL, seleccion de disparo
		{
			memcpy(&MAPLE_RESET2, p, size);
//			logmsg( "pvr_write: MAPLE_RESET2: %x\r\n", MAPLE_RESET2);

			if (traza_activa)
				fprintf(stderr, "traza: maple SB_MDTSEL=%08lx (%s)\n",
					(unsigned long) MAPLE_RESET2,
					(MAPLE_RESET2 & 1) ? "disparo por hardware" : "por software");
		}
		break;

		case 0xa05f6c14: // SB_MDEN, habilitacion del DMA
		{
			memcpy(&MAPLE_ENABLE, p, size);
//			logmsg( "pvr_write: MAPLE_ENABLE: %x\r\n", MAPLE_ENABLE);

			if (traza_activa)
				fprintf(stderr, "traza: maple SB_MDEN=%08lx\n",
					(unsigned long) MAPLE_ENABLE);
		}
		break;

		case 0xa05f6c18: // SB_MDST, arranque del DMA
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
					/* El DMA del Maple lo hace el ASIC con direcciones fisicas
					   que el guest dejo en la lista de comandos, y esto se
					   dispara desde una escritura a registro, o sea dentro de
					   una instruccion: nada de esto pasa por la MMU. */
					memread_fisico(curaddr,  &td1, sizeof(DWORD));
					memread_fisico(curaddr + 4, &td2, sizeof(DWORD));
					/*
						Cortar por la direccion de respuesta y no por el
						descriptor.

						El descriptor tiene la longitud en los bits 0-7, el
						patron en 8-15, el puerto en 16-17 y el fin de lista en
						el 31; un marco de una sola palabra al puerto A que no
						sea el ultimo de la lista los deja los cuatro en cero,
						o sea que **cero es un descriptor perfectamente valido**.
						El chequeo era "td1 == 0 -> tabla mal puesta", y por eso
						el boot ROM no encontraba nunca el mando: su primer
						sondeo es exactamente eso, un Device Request al puerto A.
						KOS nunca lo delata porque siempre marca la ultima
						transferencia y sus listas de un elemento dan
						0x80000000.

						Lo que si distingue una tabla sin inicializar es la
						direccion de respuesta: el guest la pone siempre en RAM
						del sistema. Sin esta guarda, una tabla en cero da mil
						vueltas escribiendo "no hay nada" en direcciones
						arbitrarias.
					*/
					if ((td2 & 0x1F000000) != 0x0C000000)
					{
						logmsg( "pvr_write: MAPLE_DMAADDR: dir %x erronea\r\n", MAPLE_DMAADDR);

						if (traza_activa)
							fprintf(stderr, "traza: maple respuesta a %08lx, que no es"
								" RAM: se corta la lista\n", (unsigned long) td2);

						return;
					}
//					logmsg( "le�dos td1:%x, td2:%x\r\n", td1, td2);
					last = (td1 & 0x80000000) ? true : false;
//					logmsg( "tama�o del paquete: %x\r\n", (tam = (td1 & 0xFF) + 1));
					tam = (td1 & 0xFF) + 1;
					// ahora tenemos el paquetito! a tratar de leerlo.

					if (((td1 >> 16) & 0x3) != 0) // si no es el puerto 1
					{
						// grabemos 0xFFFFFFFF
						logmsg( "Grabando 0xFFFFFFFF en %x, tam=%d, td=%x\r\n", td2, tam, td1);
						dw = 0xFFFFFFFF;
						memwrite_fisico(td2, &dw, sizeof(DWORD));
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
							memread_fisico(curaddr + 8 + j*4, &paquete[j], sizeof(DWORD));
						}
						// ya tenemos el paquete en memoria!
//						logmsg( "comando: %x\r\n", (paquete[0]) & 0xFF);
//						logmsg( "recaddr: %x\r\n", (recadr = (paquete[0] >> 8) & 0xFF));
//						logmsg( "sender : %x\r\n", (sendadr = (paquete[0] >> 16) & 0xFF));
//						logmsg( "adit.w.: %x\r\n", (paquete[0] >> 24) & 0xFF);
						recadr = (paquete[0] >> 8) & 0xFF;
						sendadr = (paquete[0] >> 16) & 0xFF;

						logmsg("recadr:%x, sendadr:%x\r\n", recadr, sendadr);

						/* Quien pregunta que, y a quien: una vez por cada par
						   (puerto, comando). Sin deduplicar son ~77 lineas por
						   segundo de tiempo emulado en cuanto el guest empieza
						   a leer el mando. */
						if (traza_activa)
						{
							static unsigned char vistos[4][32];
							unsigned puerto = (td1 >> 16) & 0x3;
							unsigned cmd    = paquete[0] & 0xFF;

							if (!(vistos[puerto][cmd >> 3] & (1 << (cmd & 7))))
							{
								vistos[puerto][cmd >> 3] |= (unsigned char) (1 << (cmd & 7));

								fprintf(stderr, "traza: maple puerto %u comando %02x"
									" para %02x de %02x, %d palabras\n",
									puerto, cmd, recadr, sendadr, tam);
							}
						}

						if (recadr != 0x20)
						{
							logmsg( "Grabando 0xFFFFFFFF en %x, tam=%d, td=%x\r\n", td2, tam, td1);
							dw = 0xFFFFFFFF;
							memwrite_fisico(td2, &dw, sizeof(DWORD));
						}
						else
						if ((paquete[0] & 0xFF) == 1) // Request Device Info
						{
							/*
								El descriptor de un mando HKT-7700 de serie.

								Estaba casi entero en cero, y eso no es
								inofensivo: function_data[0] es el que dice
								**que botones y ejes tiene** el mando, y el boot
								ROM lo mira. Los nombres van rellenos con
								espacios hasta el largo del campo, como en el
								bus -- no terminados en NUL --, asi que se
								arranca de un memset y se copia sin el cero.
							*/
							memset(&devinfo, 0, sizeof(devinfo));

							devinfo.func = (1 << 24); // controlador
							devinfo.function_data[0] = 0xFE060F00;
							devinfo.area_code = 0xFF;
							devinfo.connector_direction = 0;

							memset(devinfo.product_name, ' ', sizeof(devinfo.product_name));
							memcpy(devinfo.product_name, "Dreamcast Controller",
								strlen("Dreamcast Controller"));

							memset(devinfo.product_license, ' ', sizeof(devinfo.product_license));
							memcpy(devinfo.product_license,
								"Produced By or Under License From SEGA ENTERPRISES,LTD.",
								strlen("Produced By or Under License From SEGA ENTERPRISES,LTD."));

							devinfo.standby_power = 0x01AE;
							devinfo.max_power = 0x01F4;
							// a hacer el paquete de respuesta
							paquete[0] = 0x05 | // device info (response)
								((sendadr << 8) & 0xFF00) |
								((((recadr == 0x20) ? 0x20 : 0) << 16) & 0xFF0000) |
								(((sizeof(maple_devinfo_t)/4) << 24) & 0xFF000000);
//							logmsg( "Escribiendo en %x: %x\r\n", td2, paquete[0]);
							memwrite_fisico(td2, &paquete[0], sizeof(DWORD));
//							logmsg( "Escribiendo devinfo en %x\r\n", td2 + 4);
							memwrite_fisico(td2 + 4, &devinfo, sizeof(maple_devinfo_t));
						}
						else
						if ((paquete[0] & 0xFF) == 9) // MAPLE_COMMAND_GETCOND
						{
							// recibe FUNC
							cont_cond_t ct;
							
							ct.buttons = joystick; // CONT_START

							/* Solo cuando cambia: si no, son ~77 lineas por
							   segundo de tiempo emulado. */
							if (traza_activa)
							{
								static WORD ultimo = 0xFFFF;

								if (joystick != ultimo)
								{
									fprintf(stderr, "traza: maple botones %04x\n",
										(unsigned) joystick);
									ultimo = joystick;
								}
							}

							ct.rtrig = rtrig;
							ct.ltrig = ltrig;
							ct.joyx = joyx;
							ct.joyy = joyy;
							ct.joy2x = 128;
							ct.joy2y = 128;

							paquete[0] = 0x08 | // data transfer (response)
								((sendadr << 8) & 0xFF00) |
								((((recadr == 0x20) ? 0x20 : 1) << 16) & 0xFF0000) |
								(((sizeof(cont_cond_t)/4 + 1) << 24) & 0xFF000000);
								
//							logmsg( "Escribiendo en %x: %x\r\n", td2, paquete[0]);
							memwrite_fisico(td2, &paquete[0], sizeof(DWORD));
							dw = (1 << 24); // MAPLE_FUNC_CONTROLLER
							memwrite_fisico(td2 + 4, &dw, sizeof(DWORD));
//							logmsg( "Escribiendo cont_cond en %x\r\n", td2 + 8);
							memwrite_fisico(td2 + 8, &ct, sizeof(cont_cond_t));
						}
						else
						{
							logmsg("comando MAPLE no implementado: %x\r\n", paquete[0]);

							if (traza_activa)
								fprintf(stderr, "traza: maple comando %02lx sin emular\n",
									(unsigned long) (paquete[0] & 0xFF));
						}
						intc_add(ASIC_EVT_MAPLE_DMA, 0);
						free(paquete);
					}
					else
					{
						logmsg("MAPLE: tama�o = %d\r\n", tam);
	 				}
//					curaddr += ((td1 & 0xFF) + 1);
					curaddr += ((td1 & 0xFF) + 1) * 4 + 8; // para que se salte el descriptor + el paquete
					i++;
				}

				/* La transferencia se hizo entera aca mismo, asi que el canal
				   ya esta libre. Antes el bit se bajaba en la *lectura* del
				   registro, que hacia que la primera consulta viera un DMA en
				   curso que no existia. */
				REMOVE_BIT(MAPLE_STATE, 0x1);

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

		// INICIO REGISTROS PVR

/*		case 0xa05f8012: // RENDERFORMAT, "alpha config"
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

			logxmsg(LOG_PVR, "0xa05f8008: RENDERFORMAT=%x\n", *(DWORD *)p);
			if (dw & 0x1000)
				logxmsg(LOG_PVR, "dither enable\n");
			idx = dw & 0x07;
			logxmsg(LOG_PVR, "formato: %s\n", s_RENDERFORMAT[idx]);
			SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, m_RENDERFORMAT[0][idx]);
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, m_RENDERFORMAT[1][idx]);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, m_RENDERFORMAT[2][idx]);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, m_RENDERFORMAT[3][idx]);
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, m_RENDERFORMAT[4][idx]);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		}
		break; */

#define PVR_WRITE_1(dir, texto, arg)		{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg);} break; }
#define PVR_WRITE_CB_1(dir, callback, texto, arg)		{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg);} callback(direccion, p, size); break; }
#define PVR_WRITE_VAR_1(dir, texto, arg, var)	{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg); var = arg;} break; }
#define PVR_WRITE_2(dir, texto, arg1, arg2)	{ case (dir): { logxmsg(LOG_PVR, texto "\n", arg1, arg2);} break; }

		PVR_WRITE_1(0xa05f8000 + 0x00 * 4, "COREID [ID] (PVR2 Core ID), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x01 * 4, "CORETYPE [REV] (PVR2 Core version), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x02 * 4, "COREDISABLE [CORERESET] (PVR2) (Enable/disable the submodules of the PVR2 Core), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x03 * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x04 * 4, "Unknown, grabando %x", *(DWORD *) p);
//		PVR_WRITE_CB_1(0xa05f8000 + 0x05 * 4, cb_renderstart, "RENDERSTART (3D) (Start render strobe), grabando %x", *(DWORD *) p);
		PVR_WRITE_CB_1(0xa05f8000 + 0x05 * 4, cb_renderstart, "RENDERSTART (3D) (Start render strobe), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x06 * 4, "[TESTSELECT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x07 * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_CB_1(0xa05f8000 + 0x08 * 4, cb_param_base, "PRIMALLOCBASE [PARAM_BASE] (3D) (Primitive allocation base), grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x09 * 4, "[SPANSORTCFG], grabando %x", *(DWORD *) p);
 		PVR_WRITE_1(0xa05f8000 + 0x0a * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_CB_1(0xa05f8000 + 0x0b * 4, cb_region_base, "TILEARRAY (Tile Array base address) [REGION_BASE], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0c * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0d * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0e * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x0f * 4, "Unknown, grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x10 * 4, "BORDERCOLOR (2D) (Border colour), grabando %x", *(DWORD *) p);

		case 0xa05f8000 + 0x11 * 4:		// BITMAPTYPE (bitmap display settings)
		{
		    bool reinit = false;
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
				if (screenbits != 16 || screenformat != FRAMEBUFFER_ARGB0555)
					reinit = true;
				screenbits = 16;
				screenformat = FRAMEBUFFER_ARGB0555;
				break;
				
				case 0x01:
				logxmsg(LOG_PVR, "RGB565\r\n");
				if (screenbits != 16 || screenformat != FRAMEBUFFER_RGB565)
					reinit = true;
				screenbits = 16;
				screenformat = FRAMEBUFFER_RGB565;
				break;
				
				case 0x02:
				logxmsg(LOG_PVR, "RGB888\r\n");
				if (screenbits != 24 || screenformat != FRAMEBUFFER_RGB888)
					reinit = true;
				screenbits = 24;
				screenformat = FRAMEBUFFER_RGB888;
				break;
				
				case 0x03:
				logxmsg(LOG_PVR, "ARGB0888\r\n");
				if (screenbits != 32 || screenformat != FRAMEBUFFER_ARGB0888)
					reinit = true;
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
				if (reinit)
					screeninit();
			}
			else
				pvr_framebufferdisplay = false;
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
				logxmsg(LOG_PVR, "modo inv�lido. valores por defecto.\n");
				screenheight = 480;
				screenwidth = 640;
				screenbits = 16;
			}
			screeninit();
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
		PVR_WRITE_CB_1(0xa05f8000 + 0x23 * 4, cb_isp_backgnd_t, "BGPLANE [ISP_BACKGND_T] (3D) (Background plane location), grabando %x", *(DWORD *) p);

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
//		PVR_WRITE_1(0xa05f8000 + 0x32 * 4, "[SPG_HBLANK_INT], grabando %x", *(DWORD *) p);
//		PVR_WRITE_1(0xa05f8000 + 0x33 * 4, "[SPG_VBLANK_INT], grabando %x", *(DWORD *) p);
//		PVR_WRITE_1(0xa05f8000 + 0x34 * 4, "[SPG_CONTROL], grabando %x", *(DWORD *) p);
//		PVR_WRITE_1(0xa05f8000 + 0x35 * 4, "[SPG_HBLANK], grabando %x", *(DWORD *) p);

		case (0xa05f8000 + 0x32 * 4): // 0xa05f80c8, SPG_HBLANK_INT
		{
		    logxmsg(LOG_PVR, "SPG_HBLANK_INT: hblank_in_interrupt = %x\n", (DWREF(p) >> 16) & 0x3FF);
		    logxmsg(LOG_PVR, "SPG_HBLANK_INT: hblank_int_mode = %d\n", (DWREF(p) >> 12) & 0x3);
		    logxmsg(LOG_PVR, "SPG_HBLANK_INT: line_comp_val = %x\n", DWREF(p) & 0x3FF);
		}
  		break;
        
		case (0xa05f8000 + 0x33 * 4): // 0xa05f80cc, SPG_VBLANK_INT
		{
      		pvr_spg_vblank_int_out = (DWREF(p) >> 16) & 0x3FF;
      		pvr_spg_vblank_int_in  = DWREF(p) & 0x3FF;
		    logxmsg(LOG_PVR, "SPG_VBLANK_INT: vblank out interrupt line number = %x\n", pvr_spg_vblank_int_out);
		    logxmsg(LOG_PVR, "SPG_VBLANK_INT: vblank in interrupt line number = %x\n", pvr_spg_vblank_int_in);
		}
  		break;
        
		case (0xa05f8000 + 0x34 * 4): // 0xa05f80d0, SPG_CONTROL
		{
		    logxmsg(LOG_PVR, "SPG_CONTROL: csync_on_h = %d\n", (DWREF(p) >> 9) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: sync_direction = %d\n", (DWREF(p) >> 8) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: PAL = %d\n", (DWREF(p) >> 7) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: NTSC = %d\n", (DWREF(p) >> 6) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: force_field2 = %d\n", (DWREF(p) >> 5) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: interlace = %d\n", (DWREF(p) >> 4) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: spg_lock = %d\n", (DWREF(p) >> 3) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: mcsync_pol = %d\n", (DWREF(p) >> 2) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: mvsync_pol = %d\n", (DWREF(p) >> 1) & 1);
		    logxmsg(LOG_PVR, "SPG_CONTROL: mhsync_pol = %d\n", (DWREF(p) >> 0) & 1);
		    pvr_spg_control = DWREF(p);

			// La norma decide los campos por segundo, y con eso el ritmo del
			// barrido. Ver docs/clock-plan.md.
			pvr_ciclos_linea = reloj_ciclos_por_linea(pvr_spg_load_vcount, pvr_spg_control);
			logxmsg(LOG_PVR, "SPG_CONTROL: %d ciclos por linea\n", pvr_ciclos_linea);
		}
		break;

		case 0xa05f8000 + 0x35 * 4: // 0xa05f80d4, SPG_HBLANK
		{
		    logxmsg(LOG_PVR, "SPG_HBLANK: hbend = %x, hbstart = %x\n",
		    	(DWREF(p) >> 16) & 0x3ff,
		    	DWREF(p) & 0x3ff);
		    pvr_spg_hblank = DWREF(p);
		}
		break;

		case 0xa05f8000 + 0x36 * 4: // 0xa05f80d8, SPG_LOAD
		{
			dw = *(DWORD *) p;
			logxmsg(LOG_PVR, "SPG_LOAD: vcount = %x, hcount = %x\n",
   				(dw >> 16) & 0x3ff,
   				dw & 0x3ff);
			pvr_spg_load = dw;
			pvr_spg_load_vcount = (dw >> 16) & 0x3ff;

			// El ritmo del barrido depende de esto. Ver docs/clock-plan.md.
			pvr_ciclos_linea = reloj_ciclos_por_linea(pvr_spg_load_vcount, pvr_spg_control);
			logxmsg(LOG_PVR, "SPG_LOAD: %d ciclos por linea\n", pvr_ciclos_linea);
		}
		break;

		// Estos dos solo se leian: el valor por omision de reg.c se quedaba
		// aunque el guest los programara. SPG_STATUS los necesita para saber
		// cuando esta en vsync y cuando en blanking.
		case 0xa05f8000 + 0x37 * 4: // 0xa05f80dc, SPG_VBLANK
		{
			pvr_spg_vblank = DWREF(p);
			logxmsg(LOG_PVR, "SPG_VBLANK: vbstart = %x, vbend = %x\n",
				pvr_spg_vblank & 0x3ff,
				(pvr_spg_vblank >> 16) & 0x3ff);
		}
		break;

		case 0xa05f8000 + 0x38 * 4: // 0xa05f80e0, SPG_WIDTH
		{
			pvr_spg_width = DWREF(p);
			logxmsg(LOG_PVR, "SPG_WIDTH: hswidth = %x, vswidth = %x\n",
				pvr_spg_width & 0x7f,
				(pvr_spg_width >> 8) & 0x0f);
		}
		break;

		case 0xa05f80e4: // TEXT_CONTROL: ancho de las texturas rectangulares
		{
			/* Los cinco bits bajos son el ancho en unidades de 32 texels. El
			   log leia dw *antes* de cargarlo del puntero, asi que reportaba el
			   valor de la escritura anterior. El respaldo del bloque de control
			   ya lo guarda; get_texture() lo lee de ahi. */
			dw = *(DWORD *) p;

			logxmsg(LOG_PVR, "pvr_write: TEXT_CONTROL: stride %d texels\n",
				(int) ((dw & 0x1F) * 32));
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
		PVR_WRITE_CB_1(0xa05f8000 + 0x51 * 4, cb_tastart, "TASTART [TA_LIST_INIT] (TA) (Start vertex enqueueing strobe), grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x52 * 4, "[TA_YUV_TEX_BASE], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x53 * 4, "[TA_YUV_TEX_CTRL], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x54 * 4, "[TA_YUV_TEX_CNT], grabando %x", *(DWORD *) p);

		PVR_WRITE_1(0xa05f8000 + 0x58 * 4, "[TA_LIST_CONT], grabando %x", *(DWORD *) p);
		PVR_WRITE_1(0xa05f8000 + 0x59 * 4, "PPALLOCEND [TA_NEXT_OPB_INIT] (TA) (PP-block allocation area end), grabando %x", *(DWORD *) p);

		// fin PVR

		case 0xa0710000: // Dreamcast RTC, reg 1
		case 0xa0710004:
		{
			// Poner el reloj en hora no se emula: el RTC sigue al del host.
			logmsg("pvr_write: RTC\r\n");
		}
		break;

		default:
		{
			// El bloque de control ya quedo escrito arriba.
			if (ES_CONTROL(fisica))
				break;

			// RAM de sonido. Antes solo se levantaba el bit del FIFO del G2 y
			// los datos se perdian; el caso puntual 0xa080ffc0 de mas arriba
			// era un parche que tapaba justo esto.
			if (fisica >= SOUND_BASE && fisica + size <= SOUND_BASE + SOUND_SIZE)
			{
				memcpy(&sound_mem[fisica - SOUND_BASE], p, size);
				SET_BIT(G2_FIFO, AICA_FIFO);
				break;
			}

			if (fisica >= AICA_REG_BASE && fisica + size <= AICA_REG_BASE + AICA_REG_SIZE)
			{
				memcpy(&aica_mem[fisica - AICA_REG_BASE], p, size);
				logxmsg(LOG_MEM, "pvr_write: registro del AICA (%x)\r\n", direccion);
				break;
			}

			if (fisica >= G2_EXT_BASE && fisica <= G2_EXT_FIN)
			{
				// No hay dispositivo: la escritura se descarta en silencio,
				// pero sin reportarla como error.
				break;
			}

			mem_write_error(direccion, p, size);
		}
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

	// Contar las escrituras a RAM de video por ventana. Si el guest cree que
	// dibuja y esto no sube, el problema esta en el camino de escritura y no en
	// el de lectura.
	if (traza_activa)
	{
		traza_video_escrituras++;
		traza_video_bytes += (DWORD) size;
		traza_video_ventanas |= 1u << ((direccion >> 24) & 0x0F);

		if (addr < traza_video_min)	traza_video_min = addr;
		if (addr > traza_video_max)	traza_video_max = addr;
	}

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

//	if (addr < VIDEO_SIZE)
	switch(size)
	{
     	case 1:		*(BYTE *) &video_mem[addr] = *(BYTE *) p;	break;
     	case 2:		*(WORD *) &video_mem[addr] = *(WORD *) p;	break;
     	case 4:		*(DWORD *) &video_mem[addr] = *(DWORD *) p;	break;
     	default:	memcpy(&video_mem[addr], p, size);			break;
	}

//	memcpy(&video_mem[addr], p, size);
	
/*	if (addr < framebuffer_size)
	{
		SDL_LockSurface(backscreen);
		memcpy(backscreen->pixels + addr, p, size);
		SDL_UnlockSurface(backscreen); */
/*	} */
}

void regmap_read(unsigned long direccion, void * p, size_t size)
{
	// PDTRA (0xFF800030): el puerto A del SH-4 esta cableado al detector de
	// tipo de cable de video. El boot ROM escribe un patron en PCTRA y espera
	// que los bits bajos de PDTRA respondan; si no lo consigue en diez vueltas
	// se duerme para siempre. Ver la fase 1.2 del plan.
	if ((direccion & 0x00FFFFFF) == 0x800030)
	{
		DWORD valor = sistema_pdtra(*PCTRA, *PDTRA, opciones.cable);

		memcpy(p, &valor, (size > sizeof(DWORD)) ? sizeof(DWORD) : size);
		return;
	}

	// El WDT lleva su estado en wdt.c, no en regmem: escribirlo exige un byte
	// alto de clave que no debe quedar en el respaldo.
	if (wdt_es_registro(direccion & 0x00FFFFFF))
	{
		wdt_leer(direccion & 0x00FFFFFF, p, size);
		return;
	}

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
	// Antes del respaldo: el WDT valida una clave y guarda su estado aparte.
	if (wdt_es_registro(direccion & 0x00FFFFFF))
	{
		wdt_escribir(direccion & 0x00FFFFFF, p, size);
		return;
	}

//	SDL_mutexP(regmap_mutex);
	memcpy(&regmem[direccion & 0x00FFFFFF], p, size);
//	SDL_mutexV(regmap_mutex);
	
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
		case 0x000010: // MMUCR, MMU Control Register
		{
			mmu_mmucr_escrito(*MMUCR);

			// TI es de un solo disparo: invalida y no queda puesto.
			*MMUCR &= ~MMUCR_TI;
		}
		break;

		case 0xD80004: // TSTR, Timer Start Register
		{
			logxmsg(LOG_INTC, "TSTR: %02x\n", *(BYTE *)TSTR);
		}
		break;
		
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
			logmsg( "SCBRR2: %x\n", *SCBRR2);
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
			// fprintf(serialfp, "%c", *SCFTDR2);
			fputc(*SCFTDR2, serialfp);
			gui_addlogchar(*SCFTDR2);
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

#ifdef DEBUG_MEM_REGISTERS
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
#endif
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
    switch(size)
    {
        case 1:    *(BYTE *) p = *(BYTE *) get_memory_pointer(direccion);   break;
        case 2:    *(WORD *) p = *(WORD *) get_memory_pointer(direccion);   break;
        case 4:    *(DWORD *) p = *(DWORD *) get_memory_pointer(direccion); break;
        default:   memcpy(p, get_memory_pointer(direccion), size);          break;
    }
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
//	memcpy(get_memory_pointer(direccion), p, size);
    switch(size)
    {
        case 1:    *(BYTE *) get_memory_pointer(direccion) = *(BYTE *)p;        break;
        case 2:    *(WORD *) get_memory_pointer(direccion) = *(WORD *)p;        break;
        case 4:    *(DWORD *) get_memory_pointer(direccion) = *(DWORD *)p;        break;
        default:   memcpy(get_memory_pointer(direccion), p, size);              break;
	}
}

#ifdef MEMORY_FUNCTIONS
void memread(unsigned long direccion, void * target, size_t size)
{
#ifdef DEBUG_MEM_HASH
	if (mem_hash_read[direccion >> 24] == NULL) 
	{
		logxmsg(LOG_MEM, "memread: mem_hash_read is NULL for bank %02x\r\n", (direccion >> 24));
		return;
	}
#endif

	(*mem_hash_read[direccion >> 24]) (direccion, target, size);

#ifdef DEBUG_MEM_READ
	if (filelogging & FILELOG_MEMREADS)
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
#ifdef DEBUG_MEM_HASH
	if (mem_hash_write[direccion >> 24] == NULL) 
	{
		logxmsg(LOG_MEM, "memwrite: mem_hash_write is NULL for bank %02x\r\n", (direccion >> 24));
		return;
	}
#endif

	(*mem_hash_write[direccion >> 24]) (direccion, source, size);
#ifdef DEBUG_MEM_WRITE
	if (filelogging & FILELOG_MEMWRITES)
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
#endif // MEMORY_FUNCTIONS

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
	logmsg( "Le�do: %f\r\n", *valor);
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
	logmsg( "Le�do: %x\r\n", *valor);
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
	logmsg( "Le�do: %x\r\n", *valor);
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
	logmsg( "Le�do: %x\r\n", *valor);
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

