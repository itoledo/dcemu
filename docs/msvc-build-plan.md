# Plan: compilar dcemu con MSVC (Visual Studio 2026)

Estado: **ejecutado**. Escrito en julio de 2026 a partir de una revisión del código en
`master` (`b92b76b`); las fases 0 a 3 están implementadas y compilan. Ver
[Resultado](#resultado) al final para lo que cambió respecto de lo planificado y lo que
queda pendiente.

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

---

## Resultado

Fases 0 a 4 completas. `dcemu.exe` compila en Win32 con Visual Studio 2026, en Debug y en
Release, sin errores, y corre el rotozoomer de 256 bytes de `demos/roto/` con el mismo aspecto
que en el build de MinGW: la textura XOR gira y hace zoom, animada y estable.

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Debug
```

### Diferencias respecto de lo planificado

**SDL 1.2.** No hizo falta compilar sdl12-compat: publica un paquete de desarrollo para MSVC
(`sdl12-compat-devel-VC.zip`) con cabeceras, `.lib` y DLL. `CMakeLists.txt` lo descarga, con el
SHA256 fijado, y replica las cabeceras bajo un subdirectorio `SDL/` porque el paquete las deja
planas y el código incluye `<SDL/SDL.h>`.

Cuidado con la cadena de DLL: `SDL.dll` es sdl12-compat, que carga `SDL2.dll`, que **no es
SDL2** sino sdl2-compat, que a su vez carga `SDL3.dll`. Si falta cualquiera de las tres el
proceso muere antes del `main` con `STATUS_DLL_INIT_FAILED` (`0xC0000142`), sin mensaje.

**SDL_image.** Reemplazado por `stb_image.h` (`include/stb_image.h`), no por una conversión a
BMP: `font.png` es color type 6 (RGBA), así que se carga con cuatro canales a una superficie de
32 bits, que es exactamente lo que entregaba `IMG_Load`. `debug.c` carga `font.bmp` por la misma
función y stb también lee BMP.

**libcdio.** El lector propio quedó en `iso9660_min.c` + `iso9660_min.h`, unas 200 líneas en vez
de las 300 estimadas. `iso.c` conserva los dos backends; el de libcdio está tras `USE_LIBCDIO`.
El lector se verificó contra una imagen ISO9660 sintética: encuentra `ip.bin` y `1st_read.bin`,
traduce los nombres, devuelve lsn/tamaño correctos y responde bien a un archivo inexistente.

**SIMDx86.** `simdx86_stub.c`, seis símbolos. El `/wd4133` previsto resultó innecesario:
`Vector(x)` es `float[4][4]`, que decae a `float *` y calza con el prototipo real.

**static_assert.** Se usa el idioma C89 (`typedef char x[cond ? 1 : -1]`), no `static_assert` de
C11, porque `Makefile.win` apunta a gcc 3.4.2. La macro es `DC_ASSERT_SIZE` en `lnxdefs.h`.

**Consola.** `DCEMU_CONSOLE=ON` enlaza con `/SUBSYSTEM:CONSOLE /ENTRY:WinMainCRTStartup`, porque
`SDL_main.h` hace `#define main SDL_main` y el punto de entrada real lo aporta `SDLmain.lib`.
Aun así sdl12-compat redirige la salida a `stdout.txt` y `stderr.txt` como el SDL 1.2 original;
para verla en la consola hay que exportar `SDL_STDIO_REDIRECT=0`.

**Arranque.** `debug.c` traía `DebugVisible = 1` y `DebugMode = DBG_STOP`, así que el emulador
abría en la vista de depuración y detenido, esperando F11. Ahora arranca ejecutando y con la
pantalla normal; F12, F10, F11 y F9 siguen funcionando igual. El cambio también afecta a los
builds de los makefiles.

### Problemas que el plan no anticipó

| Archivo | Problema |
|---|---|
| `lnxdefs.h` | `#define FLOAT float` / `DOUBLE double` chocan con los `typedef` de `<windows.h>` (`minwindef.h`, `wtypesbase.h`) y rompen las cabeceras del sistema con C2632. Quedaron bajo `#ifndef _WIN32`. |
| `graficos.h` | Los prototipos `cb_renderstart`, `cb_param_base`, `cb_region_base`, `cb_fb_w_ctrl`, `cb_ppblocksize` y `cb_fb_r_sof1` estaban tras `#if defined(POSX)`, así que en Windows `mem.c` los llamaba sin declarar. Ahora se declaran siempre. |
| `log.h` | `<stdarg.h>` y `lnxdefs.h` también estaban tras `POSX`. |
| `mov.c:671,684` | `MOV.B @(disp,GBR),R0` y `MOV.W @(disp,GBR),R0` pasaban `r` en vez de `&r` a `ReadMemoryB`/`ReadMemoryW`: el valor sin inicializar se usaba como puntero de destino. Bug real y anterior al port, que MSVC delató con C4700 + C4022. |

### Fase 4 — primer hito

Corre `demos/roto/roto.bin` (rotozoomer de 256 bytes, quarn/Outbreak 2002) como `.bin` suelto,
sin ISO y sin GUI. Ejercita el intérprete completo, la FPU (`FSCA`, `FDIV`, `FTRC`, `FLOAT`),
`MUL.L` y el camino de framebuffer 2D del PVR. Instrucciones en
[demos/roto/README.md](../demos/roto/README.md).

El riesgo de aliasing que preocupaba no se materializó: el type-punning de `mem.c` y de
`floatsimple.c` funciona igual en Debug (`/Od`) que en Release (`/O2`).

### Pendiente

- **Fase 5** — reponer la ventana de log y el backend libcdio.
- **Fase 6** — x64.
- Probar algo más grande que un 256b: Doom o MAME, que es lo que llegó a correr en su momento.
