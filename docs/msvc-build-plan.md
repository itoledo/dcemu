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
- **La ventana sale en negro.** En julio de 2026, al verificar el arranque por BIOS, se
  descubrió que `roto.bin` ya no se ve, ni en Debug ni en Release. Se bisectó y **este
  mismo commit (`a04be29`) también sale en negro hoy**, así que no lo rompió ningún cambio
  posterior: es del entorno. La emulación está bien — el volcado del framebuffer (tecla
  F5, agregada entonces) muestra el rotozoomer correcto —, lo que falla es la presentación.
  Sospechosos: la cadena sdl12-compat → sdl2-compat → SDL3, o el driver de OpenGL.

## SDL3 (2026-09-05)

El emulador dejó de hablar SDL 1.2 y habla SDL3 (3.4.16) directo. Lo que motivó el cambio: la
cadena sdl12-compat → sdl2-compat → SDL3 eran **tres DLL para llegar a la biblioteca que hacía el
trabajo**, dos capas de traducción de API en el camino de los eventos y del audio, y un paquete
(sdl12-compat) que existe para no portar código, no para seguir desarrollándolo.

Lo que cambió, archivo por archivo — y **nada más**:

| archivo | qué |
| --- | --- |
| `main.h` | ya no incluye SDL (`Uint32` → `uint32_t`); cada usuario incluye `<SDL3/SDL.h>` él mismo. Es lo que deja a `tests/` sin cabeceras de SDL |
| `main.c` | `SDL_Init(SDL_INIT_VIDEO)` devuelve `bool`; eventos `SDL_EVENT_KEY_DOWN/UP/QUIT`, `event.key.key`, `SDLK_A..Z` en mayúscula, y **se descarta `event.key.repeat`** (SDL 1.2 no repetía teclas; con la repetición una `p` sostenida alternaba la pausa sola); F1 por `SDL_SetWindowFullscreen`; `SDL_GetTicks` es de 64 bits. **La redirección de `stdout`/`stderr` a `stdout.txt`/`stderr.txt` junto al exe la hace `salida_redirigir()`**, porque SDL3 no trae SDLmain — todo el banco lee `stderr.txt` ahí. Se fueron el `VBlankCallback` de `SDL_AddTimer`, el camino `#ifdef JOYSTICK` de 2005, `inicializar_fonts()` (fuera por `USE_BIOS_FONT` desde siempre) y las ramas `TTF` |
| `graficos.c` | `SDL_CreateWindow` + `SDL_GL_CreateContext` + `SDL_GL_MakeCurrent` en vez de `SDL_SetVideoMode`; `SDL_GL_SwapWindow`; `SDL_SetWindowTitle`; el tamaño de la ventana se relee una vez por cuadro al presentar (`ventana_ancho/alto`) en vez de `outputscreen->w`. Se fueron `screen` (una superficie que nadie leía) y `draw_backscreen()`. Y **`<SDL3/SDL_opengl_glext.h>` a mano**: el `SDL_opengl.h` de SDL3 se saltea entero, glext incluido, si `<GL/gl.h>` vino antes — y `main.h` lo trae |
| `audio.c` | `SDL_OpenAudioDeviceStream` con callback por cantidad (`adicional` bytes, entregados de a lotes de 1024 cuadros por `SDL_PutAudioStreamData`), `SDL_AUDIO_S16`, `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`. El anillo de eco para el `.wav` es el mismo |
| `hilo.c` | `SDL_Mutex`/`SDL_Condition` y sus funciones nuevas; `SDL_CreateThread` lleva nombre. Ningún otro archivo de hilos cambió — la capa `hilo.h` hizo su trabajo |
| `BFont.c`, `debug.c` | `SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32 / RGB565)`, `SDL_SetSurfaceColorKey`, `SDL_MapSurfaceRGBA`, `SDL_FillSurfaceRect`, `SDL_BYTESPERPIXEL`. Se fueron `BFont_SetFontColor`, `BFont_LoadFontFromSurface` y `BFont_CreateSurface*` (sin llamadores) |
| `gui.cpp`, `gui.h`, `glmoderno.c`, `traza.c`, `arm7.c` | includes y tipos |
| `CMakeLists.txt` | descarga `SDL3-devel-3.4.16-VC.zip` (SHA256 fijado), enlaza `SDL3.lib`, copia una sola DLL. **Sin `SDLmain.lib` ni `/ENTRY:WinMainCRTStartup`**: el `main` es el del CRT; sin consola va `/ENTRY:mainCRTStartup` |

Lo que no cambió y se verificó: el contexto GL (`traza: contexto GL: profundidad 24 bits,
plantilla 8, alfa 8`, y la línea nueva `formato real R8 G8 B8 … muestras 0; AMD Radeon(TM) 880M
Graphics, 4.6.0 Compatibility Profile Context`, idénticas en los dos binarios), la regla de no
pedir `SDL_GL_DEPTH_SIZE`, la captura de `pvr-texture_render` byte a byte contra el binario
anterior, `ctest` 22/22 sin cabeceras de SDL, y la compuerta de juegos (`linea-gate.ps1`: captura,
`.wav`, puntos de control y lista de entregas contra `build-ref`, con y sin `--hilos`). El binario
nuevo importa **sólo `SDL3.dll`**.

**Lo que sí cambió y no debía: la referencia «ventana» era un FBO.** La primera compuerta dio
DCDoom y Sega Rally 2 idénticos y **Crazy Taxi con la captura distinta** — `.wav`, 40 000 puntos
de control, lista de entregas y total de instrucciones al dígito, y 3714 píxeles de 480 000 a
±1 LSB repartidos por toda la imagen (más un bloque con deltas grandes en el borde derecho),
determinista (dos corridas de cada binario, idénticas entre sí). El diagnóstico, por eliminación:
el árbol anterior compilado hoy reproduce `build-ref` byte a byte (no es el árbol); el contexto GL
es el mismo hasta el `GL_RENDERER` (no es la GPU ni el formato de píxel); `glDisable(GL_DITHER)` no
mueve un píxel (no es el tramado); y **con `--render=fbo` los dos binarios son byte a byte
iguales** — o sea que la diferencia está en rasterizar en el búfer de la ventana. Y ahí cae la
ficha: **sdl12-compat dibujaba en un FBO propio de 800×600 y lo escalaba a la ventana** (su
"OpenGL scaling", encendido por omisión), así que la referencia de siempre era un FBO y SDL3
directo fue el primero en dibujar de verdad en la ventana, donde el driver de AMD redondea
distinto. El arreglo es reproducirlo a propósito: `graficos.c` rasteriza `--render=ventana` en un
FBO del tamaño de la ventana (`destino_ventana`) y lo copia 1:1 al presentar; con eso los tres
guests vuelven byte a byte a `build-ref`, y la captura queda fuera del alcance de la ventana (DWM,
oclusión, propiedad de píxeles). `DCEMU_VENTANA_DIRECTA=1` dibuja en la ventana a secas — el A/B,
y lo que queda sin FBO. La lección es la de siempre en este árbol: una capa de compatibilidad
también hace cosas que nadie le pidió, y la línea base las lleva adentro.

Una trampa del instrumento que costó una vuelta: `cmake --build ... | Select-String | Select-Object
-First 80` **mata a ninja** cuando junta 80 líneas — `Select-Object -First` corta el pipeline
aguas arriba — y `$LASTEXITCODE` queda en cero. El binario viejo seguía ahí, importando `SDL.dll`,
y el "rc=0" era de otro comando. La compilación se manda a un archivo y se grepea después.

Los makefiles (`Makefile.win`, `Makefile.linux`) siguen nombrando SDL 1.2 y no se portaron: ya no
compilaban en estas máquinas (no hay gcc), y portarlos es `sdl3-config --cflags --libs` — el código
no tiene nada de SDL 1.2. La ventana en negro de `roto.bin` de arriba queda como estaba: no se
volvió a mirar en esta pasada.

**La adopción se midió antes de escribirse (2026-09-05), con la cadena entera desprendida del
árbol de procesos.** Tres compuertas y una tanda, todas sobre el binario final — `BD5AE036…` sin
perfil para las compuertas, `35BF46FA…` reentrenado con `pgo.ps1 -Clang` para la tanda — contra
el árbol anterior (`1dc1938`) compilado en un worktree con la misma cadena y su propio perfil
(`A4FEE3C3…`, verificado con `llvm-profdata show`: 777,6 mil millones de cuentas en los dos
perfiles, 1304 funciones contra 1297 — las siete que se fueron con SDL 1.2):

- **Compuerta de juegos** (`linea-gate.ps1`, cinco brazos): d0 IGUAL a `build-ref` en Sega
  Rally 2, DCDoom y Crazy Taxi — captura, `.wav`, puntos de control y lista de entregas —;
  d1-con ≡ d1-sin y d1-conB ≡ d1-con en los tres; y el control d0-con contra d0 de SR2 sigue
  DISTINTO, que es lo que prueba que la compuerta ve. Contadores de control del hilo en cero.
- **Parque KOS** (`barrido.ps1`, 151 demos, RTC clavado, VMU fresca, un barrido por binario):
  151 con captura en los dos, **0 capturas distintas, 0 códigos de salida distintos, 0
  veredictos serial distintos**.
- **Tanda de dos binarios** (`herramientas/binarios-ab.ps1`, escrita para esto: imprime el hash
  de los dos y falla si coinciden, calentamiento por binario descartado, orden rotado dentro del
  par, RTC clavado, `--sin-vmu`, audio abierto, cuatro rondas):

| guest | SDL3 (ms) | SDL 1.2 por compat (ms) | media | pares | rangos |
| --- | --- | --- | --- | --- | --- |
| DCDoom, 35 s | 23 218–23 535 | 23 546–23 711 | −0,9 % | 4/4 | disjuntos |
| Sega Rally 2, 60 s | 46 820–46 948 | 47 608–47 995 | −1,9 % | 4/4 | disjuntos |
| Crazy Taxi, 180 s | 66 690–66 967 | 72 053–72 925 | −7,8 % | 4/4 | disjuntos |

Los totales de instrucciones y de entradas del traductor salen idénticos al dígito entre los dos
binarios en las 24 corridas (DOOM 5 284 887 508, SR2 8 463 488 574, CT 21 955 648 151): el guest
hizo lo mismo y la diferencia es entera del anfitrión. **Lo que la tanda no dice es de qué parte
del anfitrión.** El binario nuevo cambia a la vez el camino del audio (un flujo de SDL3 en vez de
tres capas de traducción por callback, cada una con su hilo) y el del cuadro (el FBO propio
copiado 1:1 en vez del de sdl12-compat escalado), y el guest que más gana es el que más presenta
— CT ~163 cuadros por segundo real contra ~91 de DOOM y ~77 de SR2 —, que es un indicio y no una
atribución. También conviene decir que el absoluto del binario viejo (72,0–72,9 s) cae en la
banda de «portátil en uso» de esta máquina (73,9–75,3 s en la tanda contaminada de la mañana) y
no en la de reposo (66,2–67,1 s el 2026-09-03), con los dos brazos alternados en el mismo lote:
lo que se compara es el par, y el absoluto sólo dice que la cadena de compatibilidad rinde peor
en la misma máquina que el binario nuevo. La tanda que separa audio de cuadro es la misma con
`--sin-audio` en los dos brazos; la de dos rondas que corrió dentro de la cadena salió ilegible
(una corrida del binario nuevo a 91 735 ms contra 75 908 de su repetición, 21 % dentro de un
brazo) y se repitió a cuatro rondas — el resultado va al final de esta sección.

Y una trampa del instrumento, pagada con cinco horas y media de máquina parada: la cadena esperaba
un marcador FIN que nunca llegó. El pwsh que envolvía el barrido del binario viejo cayó en el
`Stop-Process` por línea de comando de la mañana (el patrón casaba con su propia línea), el
barrido terminó igual — 151 con captura a las 12:22 — y el marcador no se escribió. Regla: el
marcador lo escribe el guion que hace el trabajo, no un envoltorio, y quien espera mira también el
resultado (`resumen.csv` y la línea «N con captura»), no sólo el marcador.

**La atribución, hasta donde la tanda de `--sin-audio` la deja llegar.** A cuatro rondas, Crazy
Taxi 180 s sin abrir la tarjeta en los dos brazos: **SDL3 111 864–113 262 ms contra 120 622–131 861
de la cadena de compatibilidad, −9,2 %, rangos disjuntos, 4/4**, totales al dígito. La brecha no se
esfuma al quitar el audio, así que **la ganancia no vive en el camino del audio**: queda en el del
cuadro y los eventos — el `SDL_GL_SwapBuffers` que en sdl12-compat era su propio FBO escalado más
la bomba de eventos atravesando dos capas —, que es lo que predecía el indicio de la cadencia de
presentación. Lo que esa tanda **no** puede dar es un número: su nivel absoluto está un 48 % por
encima de su propia repetición de una hora antes (111,9 s contra 75,9 del mismo binario, misma
receta, con la máquina en corriente, plan equilibrado y 4 % de carga al terminar), que es la
señal con la que este árbol declara que una tanda corrió con la máquina en otro estado; los dos
brazos se movieron juntos y el par se lee, la magnitud no. Ese régimen resultó inestable por una
razón que se encontró la misma noche: **Windows 11 estrangula al proceso cuya ventana está tapada
y no reproduce audio**, y `--sin-audio` era el brazo que lo destapaba — `DCEMU_ESTRANGULAR` en
CLAUDE.md y la lección en `docs/notas-herramientas.md`; el proceso ya se exime solo.
Marcas sobre el binario adoptado (`35BF46FA…`), con audio: CT 2,70×, DCDoom 1,50×, SR2 1,28×.
