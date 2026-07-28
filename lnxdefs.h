#ifndef _def_h_
#define _def_h_

/* En Windows, FLOAT y DOUBLE ya vienen de <windows.h> (minwindef.h y
   wtypesbase.h) como typedefs, y redefinirlos con macros rompe esas cabeceras. */
#ifndef _WIN32
#define FLOAT float
#define DOUBLE double

typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned char BYTE;
typedef unsigned int INT32;
#endif

/* bool: antes era un "typedef short bool" propio. Se unifica en <stdbool.h>
   para no chocar con el bool de C++ (gui.cpp) ni con el de las cabeceras del
   sistema. Ojo: sizeof(bool) pasa de 2 a 1, pero ninguna estructura con
   layout fijo lo usa. */
#ifndef __cplusplus
#include <stdbool.h>
#endif

/* Verificacion de tamano en tiempo de compilacion. MSVC y GCC empaquetan igual
   los bitfields en x86, pero es barato dejarlo comprobado. No se usa
   static_assert de C11 porque Makefile.win apunta a gcc 3.4.2. */
#define DC_ASSERT_SIZE(nombre, tipo, n) \
	typedef char dc_assert_##nombre[(sizeof(tipo) == (n)) ? 1 : -1]

/* MSVC no conoce __inline__ (extension de GCC). */
#ifdef _MSC_VER
#define DC_INLINE __inline
#else
#define DC_INLINE __inline__
#endif

#endif
