#ifndef _MEM_H_
#define _MEM_H_

#include "options.h"
#include "mmu.h"

#include "traza.h"			/* watchpoint_escritura(), watchpoint_dir */

#define VIDEO_SIZE		(8 * 1024 * 1024)
#define MEM_SIZE		(16 * 1024 * 1024)
#define SOUND_SIZE		(2*1024*1024) // 2 Megabytes
#define BIOS_SIZE		(2*1024*1024) // 2 Megabytes
#define TA_SIZE			(512)
#define CONTROL_SIZE	(0x10000)

extern unsigned char * mem_zone[0x100]; // para las zonas de memoria
#define get_memory_pointer(addr) (&mem_zone[(addr) >> 24][(addr) & 0xFFFFFF])

typedef void mem_access_read_t (unsigned long direccion, void * p, size_t size);
typedef void mem_access_write_t (unsigned long direccion, void * p, size_t size);
extern mem_access_read_t * mem_hash_read[0x100];
extern mem_access_write_t * mem_hash_write[0x100];

/* Contadores de escrituras a RAM de video, solo con --traza-mem. */
extern DWORD traza_video_escrituras;
extern DWORD traza_video_bytes;
extern DWORD traza_video_ventanas;

/* Bytes escritos por cada ventana, indexado por el byte alto de la direccion.
   Saber **cual** ventana lleva el cuadro es lo que separa "el guest escribe
   plano" de "el guest escribe por la de 64 bits y va a los dos bancos". */
extern DWORD traza_video_por_ventana[0x20];
extern long  traza_video_min;
extern long  traza_video_max;

int inicializar_memoria();
void mem_hash_setup(void);
void regmem_setup();
DWORD float_to_dword(float x);
void dump_registers();

/* Acceso interno del emulador. La direccion ya es la definitiva y no pasa por
   la MMU: la usan la carga de archivos, el DMAC, la vista de depuracion, el
   DMA del GD-ROM y los propios handlers de mem.c, que reciben direcciones ya
   despachadas. */
/* El gancho del watchpoint de lectura (--watchpoint-lectura=) va aca por la
   misma razon que el de escritura mas abajo: es el unico sitio por el que pasan
   **todas** las lecturas. Se llama despues de leer, cuando el dato ya esta.

   Apagado, watchpoint_lectura_dir vale cero y esto es una comparacion. */
#define memread_fisico(direccion, target, size)							\
	do																	\
	{																	\
		unsigned long _wl_dir = (direccion);							\
		size_t        _wl_tam = (size);									\
																		\
		(*mem_hash_read[_wl_dir >> 24]) (_wl_dir, (target), _wl_tam);	\
																		\
		if (watchpoint_lectura_dir)										\
			watchpoint_lectura(_wl_dir, _wl_tam);						\
	} while (0)

/* El gancho del watchpoint (--watchpoint=, ver options.h) va aca porque es el
   unico sitio por el que pasan **todas** las escrituras: las del programa
   emulado llegan por memwrite(), que traduce y termina aca, y las internas
   -- DMA del Maple y del GD-ROM, DMAC, callbacks del PVR -- entran directo. Se
   llama despues de escribir para que vea el valor que quedo.

   Sin watchpoint puesto, watchpoint_dir vale cero y esto es una comparacion. */
#define memwrite_fisico(direccion, source, size)						\
	do																	\
	{																	\
		unsigned long _wp_dir = (direccion);							\
		size_t        _wp_tam = (size);									\
																		\
		(*mem_hash_write[_wp_dir >> 24]) (_wp_dir, (source), _wp_tam);	\
																		\
		if (watchpoint_dir)												\
			watchpoint_escritura(_wp_dir, _wp_tam);						\
	} while (0)

/* Acceso del programa emulado: pasa por la MMU. Es el que usan los handlers de
   instrucciones, directamente o a traves de las macros ReadMemory y
   WriteMemory de mas abajo.

   Con mmu_activa en cero -- o sea, en todo lo que corre hoy -- cuesta una
   comparacion contra cero y nada mas. Si la traduccion falla, mmu_traducir()
   no vuelve: salta a main_loop(), que restaura la instantanea de registros y
   entra a la excepcion. Ver docs/mmu-plan.md, fases 3 a 5. */
/* El gancho del UBC (ubc.c): los breakpoints de operando comparan la
   direccion **virtual**, asi que va aca y no en los _fisico -- y de paso los
   accesos internos del emulador quedan fuera solos. Apagado cuesta una
   comparacion contra cero, la misma clase que el watchpoint de arriba. Se
   llama despues del acceso, que es cuando el chip da el break de operando y
   cuando una lectura ya tiene su valor para comparar. */
extern int ubc_operando_activa;
void ubc_operando(unsigned long direccion, const void * valor, size_t tam,
				  int escritura);

#ifdef MEMORY_FUNCTIONS
void memread(unsigned long direccion, void * target, size_t size);
void memwrite(unsigned long direccion, void * source, size_t size);
#else
#define memread(direccion, target, size) \
	do { \
		unsigned long _ubc_d = (direccion); \
		unsigned long _mmu_d = _ubc_d; \
		if (mmu_activa) _mmu_d = mmu_traducir(_mmu_d, MMU_LECTURA); \
		memread_fisico(_mmu_d, (target), (size)); \
		if (ubc_operando_activa) \
			ubc_operando(_ubc_d, (target), (size), 0); \
	} while (0)

#define memwrite(direccion, source, size) \
	do { \
		unsigned long _ubc_d = (direccion); \
		unsigned long _mmu_d = _ubc_d; \
		if (mmu_activa) _mmu_d = mmu_traducir(_mmu_d, MMU_ESCRITURA); \
		memwrite_fisico(_mmu_d, (source), (size)); \
		if (ubc_operando_activa) \
			ubc_operando(_ubc_d, (source), (size), 1); \
	} while (0)
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

/* Donde el boot ROM deja el codigo de maquina de la flash -- cinco digitos y un
   NUL -- antes de entregarle la maquina al juego. Medido contra el ROM real. */
#define REGION_BASE		0x8C000070

/* Y donde deja la direccion en la que cargo el ejecutable. Medido igual. */
#define EJECUTABLE_BASE	0x8C0000F8

#define SYSCALL_SYSINFO		0x8C0000B0
#define SYSCALL_ROMFONT		0x8C0000B4
#define SYSCALL_FLASHROM	0x8C0000B8
#define SYSCALL_GDROM		0x8C0000BC
#define SYSCALL_UNKNOWN		0x8C0000E0

#endif // _MEM_H_
