# Plan: compilar dcemu con MSVC (Visual Studio 2026)

Estado: propuesta, sin ejecutar. Escrito en julio de 2026 a partir de una revisión del
código en `master` (`b92b76b`).

## Contexto

`dcemu.vcproj` es formato 7.10 (Visual Studio 2003) y referencia `sh4.c`, el núcleo viejo que
fue reemplazado por `sh4emu.c` en enero de 2006. Visual Studio no convierte proyectos
anteriores a 2010, así que ese archivo no sirve ni como punto de partida. El build nuevo va con
**CMake**, que VS 2026 abre como carpeta sin necesidad de generar una solución.

La buena noticia: el código propio está limpio. No hay ensamblador inline, ni `__attribute__`,
ni extensiones de GCC, salvo dos `__inline__`. Todo el trabajo pesado está en las cuatro
dependencias externas, que son de 2004-2005 y ninguna compila hoy con MSVC tal cual.

## Fase 0 — Decisiones de base

- **x86 (Win32) primero, no x64.** No hay casts puntero↔`DWORD` (el único está en código
  comentado, `log.c:131`), así que x64 probablemente funcione. Pero el objetivo del primer hito
  es tener un binario comparable contra el de MinGW para detectar regresiones; ampliar a x64
  después.
- C compilado como `/std:c17`; `gui.cpp` como C++17.
- Definir `WIN32` y `_CRT_SECURE_NO_WARNINGS`.
- **No definir `POSX`.** Hoy ambos makefiles lo definen incluso en Windows, y eso hace que
  `main.h:18-25` tome la rama POSIX, que incluye `<unistd.h>`.

## Fase 1 — Dependencias

Aquí está el grueso del trabajo.

### SDL 1.2

Usar [sdl12-compat](https://github.com/libsdl-org/sdl12-compat): implementa la API 1.2 sobre
SDL2, se mantiene activo y compila con CMake + MSVC sin parches. SDL 1.2.15 original ya no
compila limpio.

### SDL_image

Se usa en un solo lugar: `IMG_Load` en `BFont.c:113`, para cargar `font.png`. No vale la pena
arrastrar SDL_image 1.2. Dos salidas baratas:

- `stb_image.h`, que es un único header, o
- convertir `font.png` a BMP y usar `SDL_LoadBMP`.

### guichan

Sin releases desde 2010; es la dependencia más molesta. Solo alimenta la ventana de log de
`gui.cpp`. Plan: ponerlo detrás de `#define USE_GUICHAN` y **compilar sin GUI en el primer
hito**, con stubs vacíos para `gui_init`, `gui_event`, `gui_refresh`, `gui_addlog`,
`gui_addlogchar`, `gui_setvisiblelog` y `gui_isvisiblelog`. Reponerlo después, o cambiarlo por
Dear ImGui.

### libcdio + libiso9660

Autotools, muy penoso con MSVC. Además los `.a` de `lib/win32/` son binarios de MinGW,
incompatibles con el linker de MSVC.

La superficie real en `iso.c` es de unas quince funciones, y la ruta `.iso` usa solo seis:
`iso9660_open`, `iso9660_ifs_readdir`, `iso9660_iso_seek_read`, `iso9660_name_translate`,
`_CDIO_LIST_FOREACH` y `_cdio_list_free`.

Plan: partir `iso.c` en dos backends y escribir uno propio de ISO9660 (unas 300 líneas: leer el
PVD en LBA 16, recorrer el directorio raíz, leer sectores de 2048 bytes). Cubre `.iso`, que es
lo que usan los homebrew de KOS. El backend libcdio —imágenes bin/cue y lectora física— queda
para la fase 5.

### SIMDx86

Los `.a` son de MinGW y sus headers usan `asm("emms")`. La superficie usada es mínima:

| Símbolo | Uso |
|---|---|
| `SIMDx86_sqrtf` | `floatsimple.c:471` |
| `SIMDx86_sqrt` | `floatsimple.c:642` |
| `SIMDx86_rsqrtf` | `floatgraph.c:217` |
| `SIMDx86Vector_Dot4` | `floatgraph.c:163` |
| `SIMDx86Matrix_Vector4Multiply` | `floatgraph.c:203` |
| `SIMDx86_GetBuildString` | `main.c:963` |

Un `simdx86_stub.c` de unas 40 líneas en C plano los reemplaza.

### OpenGL

Solo hace falta `opengl32.lib`. `glu32` y `winmm` están en los makefiles pero **no se usan**:
la única llamada a GLU está comentada (`graficos.c:1158`) y no hay ninguna a winmm. Sacarlas
del link.

## Fase 2 — Portabilidad del código propio

Esto es corto.

| Archivo | Problema | Arreglo |
|---|---|---|
| `log.c:1` | `#include <unistd.h>` incondicional | guardar con `#ifndef _MSC_VER` |
| `log.c:59-66` | `unlink()` | `remove()`, que es ISO C |
| `floatsimple.c:12,22` | `__inline__` | macro `DC_INLINE` (`__inline` en MSVC) |
| `lnxdefs.h:14` | `typedef short bool` choca con el `<stdbool.h>` que arrastra `cdio/types.h` | unificar en `<stdbool.h>` |
| `include/SIMDx86/align.h:9` | `__attribute__((aligned(16)))` | dejarlo vacío en MSVC |
| `main.c:3-13` | `#pragma comment(lib, ...)` apunta a `SDL_ttf` y `sdlgfx`, que no se usan | borrar; el link lo maneja CMake |
| `BFont.c:21-23` | workaround `_vsnprintf` "para MS Visual C++" | con C17 `vsnprintf` es estándar |

Dos detalles:

- Al unificar `bool` en `<stdbool.h>`, `sizeof(bool)` pasa de 2 a 1. Revisar que no aparezca
  dentro de structs con layout fijo.
- `ALIGNED` se usa en posición *trailing* (`} SIMDx86Vector ALIGNED;`) y `__declspec(align)` de
  MSVC va **antes** del tipo, así que no hay equivalente directo. Como de todos modos se
  reemplaza el SIMD por C plano, dejarlo vacío no cuesta nada.

Además: `/wd4133` para el paso de `float[4]` donde `SIMDx86Vector_Dot4` espera un
`SIMDx86Vector*` (`floatgraph.c:163`), y fijar el layout de bitfields con
`static_assert(sizeof(...) == 4)` sobre `SR_BIT_t`, `FPSCR_REG_BITS_t` y los tres structs de
`render.h`. MSVC y GCC coinciden en x86, pero es barato dejarlo verificado en compilación.

## Fase 3 — CMakeLists

- Lista de fuentes: la de `Makefile.linux`, que es la única al día (usa `sh4emu.c`, no
  `sh4.c`) — 23 archivos `.c` más `gui.cpp`.
- `enable_language(RC)` para `dcemu_private.rc`.
- Crear `logs/` en el directorio de salida: `inicializar_logs()` aborta el arranque si no puede
  abrir sus archivos.
- Copiar `font.png` y `fixedfont.bmp` junto al ejecutable.

## Fase 4 — Primer hito

Correr un homebrew de KOS como `.bin` suelto: sin ISO y sin GUI. Eso ya ejercita CPU, memoria y
PVR completos. Después habilitar el backend `.iso`.

## Fase 5 — Reponer lo desactivado

GUI (guichan o Dear ImGui) y backend libcdio para bin/cue y lectora física.

## Fase 6 — Opcional: x64

## El riesgo real

MSVC no tiene equivalente a `-fno-strict-aliasing`, que ambos makefiles pasan. En la práctica
MSVC no hace análisis de aliasing agresivo, así que el type-punning de `mem.c`
(`*(DWORD *) &video_mem[addr]`) y de `floatsimple.c` (`extract_double` / `put_double`, que dan
vuelta las mitades de un `double`) debería sobrevivir.

Es el único punto donde una diferencia entre compiladores se manifiesta como comportamiento
silencioso y no como error de compilación. Por eso conviene compilar primero en `/Od` y recién
optimizar cuando el binario corra igual que el de MinGW.

## Estimación

Fase 1 entre uno y dos días, siendo el lector ISO9660 propio la parte más larga. Fases 2 y 3,
unas horas.
