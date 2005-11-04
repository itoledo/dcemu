#ifndef _MEM_H_
#define _MEM_H_

#include "options.h"

#define VIDEO_SIZE		(8 * 1024 * 1024)
#define MEM_SIZE		(16 * 1024 * 1024)
#define SOUND_SIZE		(2*1024*1024) // 2 Megabytes
#define BIOS_SIZE		(2*1024*1024) // 2 Megabytes
#define FLASH_SIZE		(256*1024) // 256 kbytes
#define TA_SIZE			(512)
#define CONTROL_SIZE	(0x10000)

extern unsigned char * mem_zone[0x100]; // para las zonas de memoria
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
#define memread(direccion, target, size)    (*mem_hash_read[(direccion) >> 24]) ((direccion), (target), (size))
#define memwrite(direccion, source, size)   (*mem_hash_write[(direccion) >> 24]) ((direccion), (source), (size))
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


// constantes
#define mem_base		0x8C000000
#define mem_n_base		0x0C000000
#define mem_offset		0x00010000
#define ip_offset		0x00008000
#define ip_bs1_offset	0x0000B800
#define ip_bs2_offset	0x0000E000
#define mem_ram_base	0x0C000000
#define mem_n_ram_base	0x0C000000
#define mem_ram_size	0x01000000
#define CACHE_SIZE		(4096 * 1024) // 1024 kb

#define video_base		0xa5000000
#define n_video_base	0x05000000

#define HACK_BASE		0x8C000100
#define HACK_ROMFONT	0x000
#define HACK_GDROM		0x100
#define HACK_SYSINFO	0x200
#define HACK_FLASHROM	0x300
#define HACK_UNKNOWN	0x400

#define FONT_BASE		(mem_base + 1024*1024*5)
#define FONT_N_BASE		(mem_n_base + 1024*1024*5)

#define SYSCALL_SYSINFO		0x8C0000B0
#define SYSCALL_ROMFONT		0x8C0000B4
#define SYSCALL_FLASHROM	0x8C0000B8
#define SYSCALL_GDROM		0x8C0000BC
#define SYSCALL_UNKNOWN		0x8C0000E0

#endif // _MEM_H_
