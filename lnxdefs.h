#ifndef _def_h_
#define _def_h_

#include <stddef.h>		/* offsetof, para los DC_ASSERT de disposicion */

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

/* Igual que DC_ASSERT_SIZE pero para cualquier condicion constante. Sirve para
   fijar desplazamientos dentro de una estructura, no solo su tamano. */
#define DC_ASSERT(nombre, cond) \
	typedef char dc_assert_##nombre[(cond) ? 1 : -1]

/* MSVC no conoce __inline__ (extension de GCC). */
#ifdef _MSC_VER
#define DC_INLINE __inline
#else
#define DC_INLINE __inline__
#endif

/* Alineacion extendida, en posicion de prefijo -- la unica que aceptan los dos
   compiladores. Sirve tanto sobre un tipo (`union DC_ALINEADO(64) FPR_BANK`)
   como sobre una variable (`DC_ALINEADO(64) sh4_cpu core`), y cual de las dos
   conviene no es indiferente:

   - **sobre el tipo** la heredan todas las declaraciones y no se puede olvidar
     ninguna, pero **redondea sizeof hacia arriba**: una estructura de 176 bytes
     alineada a 64 pasa a medir 192. Si algo la copia seguido, esos 16 bytes se
     pagan en cada copia;
   - **sobre la variable** el objeto queda igual de bien colocado y sizeof no
     cambia, a costa de tener que ponerlo en cada declaracion -- incluida la
     `extern`, que es donde la ven los sitios de uso.

   Y en los dos casos: malloc() no respeta la alineacion extendida, asi que un
   tipo asi alineado tiene que ser estatico o venir de _aligned_malloc().

   DCEMU_SIN_ALINEAR la apaga entera. Es el A/B de la alineacion, y tiene que
   ser de compilacion --no una variable de entorno como el resto de las sondas--
   porque lo que se mide **es** la disposicion de los datos: no hay forma de
   tener las dos en el mismo binario.

   Para correr el A/B, poner `#define DCEMU_SIN_ALINEAR 1` aca arriba y
   reconstruir. Que quede claro por que asi y no por la linea de comandos:
   `cmake -S . -B build -DCMAKE_C_FLAGS=/DDCEMU_SIN_ALINEAR` configura sin
   quejarse y **el define no llega al compilador** -- el binario sale byte a
   byte identico al de la otra rama. Se descubrio comparando los hashes, que es
   lo unico que prueba que un A/B compara dos cosas distintas. */
#if defined(DCEMU_SIN_ALINEAR)
#define DC_ALINEADO(n)
#elif defined(_MSC_VER)
#define DC_ALINEADO(n) __declspec(align(n))
#else
#define DC_ALINEADO(n) __attribute__((aligned(n)))
#endif

#endif
