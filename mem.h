#ifndef _MEM_H_
#define _MEM_H_

#include "options.h"

#define VIDEO_SIZE		(8 * 1024 * 1024)
#define MEM_SIZE		(16 * 1024 * 1024)
#define BIOS_SIZE		(4*1024*1024) // 2 Megabytes
#define TA_SIZE			(512)
#define CONTROL_SIZE	(0x10000)

extern char * mem_zone[0x100]; // para las zonas de memoria
#define get_memory_pointer(addr) (&mem_zone[(addr) >> 24][(addr) & 0xFFFFFF])

typedef void mem_access_read_t (unsigned long direccion, void * p, size_t size);
typedef void mem_access_write_t (unsigned long direccion, void * p, size_t size);
extern mem_access_read_t * mem_hash_read[0x100];
extern mem_access_write_t * mem_hash_write[0x100];

int inicializar_memoria();
void mem_hash_setup(void);
void regmem_setup();
DWORD float_to_dword(float x);
void dump_registers();

#ifdef MEMORY_FUNCTIONS
void memread(unsigned long direccion, void * target, size_t size);
void memwrite(unsigned long direccion, void * source, size_t size);
#else
#define memread(direccion, target, size)    (*mem_hash_read[direccion >> 24]) (direccion, target, size)
#define memwrite(direccion, source, size)   (*mem_hash_write[direccion >> 24]) (direccion, source, size)
#endif

#ifdef MEMORY_MACROS
#define ReadMemoryF(a,b) memread(a, b, sizeof(DWORD))
#define ReadMemoryB(a,b) memread(a, b, sizeof(BYTE))
#define ReadMemoryW(a,b) memread(a, b, sizeof(WORD))
#define ReadMemoryL(a,b) memread(a, b, sizeof(DWORD))
#define WriteMemoryF(a,b) memwrite(a, b, sizeof(DWORD))
#define WriteMemoryB(a,b) memwrite(a, b, sizeof(BYTE))
#define WriteMemoryW(a,b) memwrite(a, b, sizeof(WORD))
#define WriteMemoryL(a,b) memwrite(a, b, sizeof(DWORD))
#else
extern void ReadMemoryF(unsigned long direccion, float * valor);
extern void ReadMemoryB(unsigned long direccion, unsigned char * valor);
extern void ReadMemoryL(unsigned long direccion, DWORD * valor);
extern void ReadMemoryW(unsigned long direccion, WORD * valor);
extern void WriteMemoryW(unsigned long direccion, WORD * valor);
extern void WriteMemoryL(unsigned long direccion, DWORD * valor);
extern void WriteMemoryB(unsigned long direccion, BYTE * valor);
extern void WriteMemoryF(unsigned long direccion, float * valor);
#endif // MEMORY_MACROS

#endif // _MEM_H_
