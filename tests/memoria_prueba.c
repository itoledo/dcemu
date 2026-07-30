/****************************************************************************

	MEMORIA_PRUEBA - reemplazo de mem.c para las pruebas unitarias

	mem.c no se puede enlazar en el binario de pruebas: pvr_write() llama a los
	callbacks de graficos.c, que arrastra SDL y OpenGL. Aqui se reproduce solo
	lo que los handlers de opcodes necesitan, con la misma estructura de mem.c:
	las dos tablas de 256 entradas indexadas por el byte alto de la direccion.

	Diferencias deliberadas con mem.c, documentadas porque cambian lo que las
	pruebas pueden observar:

	  - Todas las zonas apuntan a un bloque "basura" de 16 MB, incluidas las no
		mapeadas. En mem.c quedan en NULL y get_memory_pointer() sobre ellas
		revienta. Aqui una prueba que se equivoque de direccion escribe en la
		basura en vez de tumbar el runner, y el handler de la zona igual
		reporta el error.
	  - No hay registros del PVR ni del SH-4 salvo QACR0/QACR1 (PREF) y
		TRA/EXPEVT (TRAPA), que son los unicos que tocan los opcodes.
	  - Sin logging: los logxmsg() de mem.c son no-ops con LOGGING apagado.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "mem.h"
#include "sh4emu.h"

#include "arnes.h"

#define BLOQUE			(16 * 1024 * 1024)	/* cubre el rango de 24 bits de get_memory_pointer */
#define REGMEM_PRUEBA	0x1000

/* Definidos en mem.c en el emulador; aqui los aporta este modulo. */
unsigned char * memoria;
unsigned char * video_mem;
unsigned char * regmem;
unsigned char * ta_mem;

/* Respaldo de los registros del TMU. En el emulador viven en regmem, en
   0xD800xx; aqui van en su propio bloque para no reservar 14 MB de mas. */
static unsigned char tmu_regs[0x40];

/* Respaldo del bloque de control del sistema. gdrom.c lo usa para los
   registros del bus G1 que no emula pero que igual tienen que poder leerse
   despues de escribirse. */
unsigned char * control_mem;

/* Respaldo de las zonas sin mapear: solo existe para que get_memory_pointer()
   nunca devuelva un puntero a NULL + offset. */
static unsigned char * basura;

unsigned char * mem_zone[0x100];
mem_access_read_t * mem_hash_read[0x100];
mem_access_write_t * mem_hash_write[0x100];

DWORD SQ0[8];
DWORD SQ1[8];

/* Cuenta los accesos a zonas sin mapear. Una prueba puede mirarlo para
   confirmar que un opcode no salio de la zona que le corresponde. */
int prueba_accesos_invalidos;

/* ------------------------------------------------------------------------ */
/* Handlers, calcados de mem.c                                              */
/* ------------------------------------------------------------------------ */

static void ram_read(unsigned long direccion, void * p, size_t size)
{
	switch (size)
	{
		case 1:	*(BYTE *)  p = *(BYTE *)  get_memory_pointer(direccion);	break;
		case 2:	*(WORD *)  p = *(WORD *)  get_memory_pointer(direccion);	break;
		case 4:	*(DWORD *) p = *(DWORD *) get_memory_pointer(direccion);	break;
		default: memcpy(p, get_memory_pointer(direccion), size);			break;
	}
}

static void ram_write(unsigned long direccion, void * p, size_t size)
{
	switch (size)
	{
		case 1:	*(BYTE *)  get_memory_pointer(direccion) = *(BYTE *)  p;	break;
		case 2:	*(WORD *)  get_memory_pointer(direccion) = *(WORD *)  p;	break;
		case 4:	*(DWORD *) get_memory_pointer(direccion) = *(DWORD *) p;	break;
		default: memcpy(get_memory_pointer(direccion), p, size);				break;
	}
}

static void error_read(unsigned long direccion, void * p, size_t size)
{
	(void) direccion; (void) size;

	prueba_accesos_invalidos++;

	/* mem.c deja el destino intacto; se replica para no inventar datos. */
	(void) p;
}

static void error_write(unsigned long direccion, void * p, size_t size)
{
	(void) direccion; (void) p; (void) size;

	prueba_accesos_invalidos++;
}

/* La store queue: el byte 5 de la direccion elige SQ0 o SQ1, los bits 2-4
   eligen el long dentro de la cola. Igual que sq_write() en mem.c. */
static void sq_write(unsigned long direccion, void * p, size_t size)
{
	short pos = (short) ((direccion >> 2) & 7);

	(void) size;

	if (direccion & 0x20)
		SQ1[pos] = *(DWORD *) p;
	else
		SQ0[pos] = *(DWORD *) p;
}

static void ta_write(unsigned long direccion, void * p, size_t size)
{
	if (direccion >= 0x10000000 + TA_SIZE)
		prueba_accesos_invalidos++;
	else
		memcpy(&ta_mem[direccion - 0x10000000], p, size);
}

/* ------------------------------------------------------------------------ */

static unsigned char * reservar(size_t n, const char * nombre)
{
	unsigned char * p = (unsigned char *) calloc(1, n);

	if (!p)
	{
		fprintf(stderr, "memoria_prueba: no se pudo reservar %s\n", nombre);
		exit(2);
	}

	return p;
}

void memoria_prueba_iniciar(void)
{
	int i;

	memoria		= reservar(BLOQUE, "RAM");
	video_mem	= reservar(BLOQUE, "video");
	basura		= reservar(BLOQUE, "basura");
	regmem		= reservar(REGMEM_PRUEBA, "registros");
	ta_mem		= reservar(TA_SIZE, "TA");
	control_mem	= reservar(CONTROL_SIZE, "control");

	for (i = 0; i < 0x100; i++)
	{
		mem_zone[i]			= basura;
		mem_hash_read[i]	= error_read;
		mem_hash_write[i]	= error_write;
	}

	/* RAM del sistema: P0/P1/P2 apuntan al mismo bloque, como en el SH-4. */
	mem_zone[0x0C] = memoria;
	mem_zone[0x8C] = memoria;
	mem_zone[0xAC] = memoria;

	mem_hash_read[0x0C]  = ram_read;
	mem_hash_read[0x8C]  = ram_read;
	mem_hash_read[0xAC]  = ram_read;
	mem_hash_write[0x0C] = ram_write;
	mem_hash_write[0x8C] = ram_write;
	mem_hash_write[0xAC] = ram_write;

	/* RAM de video. mem.c la atiende con video_read/video_write, que hacen lo
	   mismo que ram_read/ram_write mas la notificacion al framebuffer; para
	   los opcodes la diferencia no se nota. */
	mem_zone[0x04] = video_mem;
	mem_zone[0x05] = video_mem;
	mem_zone[0xA4] = video_mem;
	mem_zone[0xA5] = video_mem;

	mem_hash_read[0x04]  = ram_read;
	mem_hash_read[0x05]  = ram_read;
	mem_hash_read[0xA4]  = ram_read;
	mem_hash_read[0xA5]  = ram_read;
	mem_hash_write[0x04] = ram_write;
	mem_hash_write[0x05] = ram_write;
	mem_hash_write[0xA4] = ram_write;
	mem_hash_write[0xA5] = ram_write;

	/* FIFO del TA y store queues. */
	mem_zone[0x10] = ta_mem;
	mem_hash_write[0x10] = ta_write;

	mem_hash_write[0xE0] = sq_write;
	mem_hash_write[0xE1] = sq_write;
	mem_hash_write[0xE2] = sq_write;
	mem_hash_write[0xE3] = sq_write;

	/* Mismos offsets que regmem_setup() en mem.c. */
	TRA		= (DWORD *) &regmem[0x20];
	EXPEVT	= (DWORD *) &regmem[0x24];
	QACR0	= (DWORD *) &regmem[0x38];
	QACR1	= (DWORD *) &regmem[0x3C];

	/* Los de la MMU: LDTLB lee PTEH/PTEL/PTEA y MMUCR.URC, y un fallo de
	   traduccion deja TEA y PTEH. */
	PTEH	= (DWORD *) &regmem[0x00];
	PTEL	= (DWORD *) &regmem[0x04];
	TTB		= (DWORD *) &regmem[0x08];
	TEA		= (DWORD *) &regmem[0x0C];
	MMUCR	= (DWORD *) &regmem[0x10];
	PTEA	= (DWORD *) &regmem[0x34];

	/* Los del TMU. Van fuera del bloque de 0x1000 que usa el resto, asi que
	   viven en un bloque propio: solo hacen falta como respaldo para tmu.c. */
	TOCR	= (BYTE *)  &tmu_regs[0x00];
	TSTR	= (BYTE *)  &tmu_regs[0x04];
	TCOR0	= (DWORD *) &tmu_regs[0x08];
	TCNT0	= (DWORD *) &tmu_regs[0x0C];
	TCR0	= (WORD *)  &tmu_regs[0x10];
	TCOR1	= (DWORD *) &tmu_regs[0x14];
	TCNT1	= (DWORD *) &tmu_regs[0x18];
	TCR1	= (WORD *)  &tmu_regs[0x1C];
	TCOR2	= (DWORD *) &tmu_regs[0x20];
	TCNT2	= (DWORD *) &tmu_regs[0x24];
	TCR2	= (WORD *)  &tmu_regs[0x28];
}

void arnes_registros_en_cero(void)
{
	memset(regmem, 0, REGMEM_PRUEBA);
	memset(tmu_regs, 0, sizeof(tmu_regs));
	memset(SQ0, 0, sizeof(SQ0));
	memset(SQ1, 0, sizeof(SQ1));
	memset(ta_mem, 0, TA_SIZE);

	prueba_accesos_invalidos = 0;
}
