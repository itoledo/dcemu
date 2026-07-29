# dcemu

Emulador de Sega Dreamcast escrito en C entre 2004 y 2007: intérprete de la CPU SH-4 y
emulación del chip gráfico PowerVR2 traducida a OpenGL, sobre SDL.

Es un proyecto histórico, sin desarrollo activo desde febrero de 2007. Se publica tal como
quedó, con el historial completo recuperado del repositorio CVS original.

Llegó a correr **Doom**, **MAME** y varios homebrew compilados con
[KallistiOS](https://github.com/KallistiOS/KallistiOS). Nunca logró arrancar un juego
comercial.

## Qué emula

| Subsistema | Estado |
|---|---|
| CPU SH-4, enteros | Completo — despacho por tabla de saltos, delay slots, bancos de registros |
| FPU SH-4 | Instrucciones simples y dobles, más las gráficas: `FSCA`, `FIPR`, `FTRV` |
| Store queues | Sí — es la vía por la que el juego envía geometría al tile accelerator |
| Interrupciones | INTC, eventos ASIC, los tres timers del TMU |
| PVR2 / TA | Listas de polígonos, texturas (incluye *twiddled* y VQ), blending, orden de translúcidos, framebuffer 2D |
| Maple | DMA y estado del control, alimentado desde teclado o joystick |
| GD-ROM | Vía hooks a las syscalls de BIOS; `.iso` e imágenes bin/cue mediante libcdio, con descrambling de `1st_read.bin` |
| SCIF (serial) | Salida redirigida a `logs/serial.txt` |
| DMA del SH-4 | Parcial — los registros existen, las transferencias no están implementadas |
| AICA / sonido | **No emulado** — se reserva la RAM y se acusa recibo del FIFO G2, nada más |
| MMU, VMU, módem/BBA | No emulados |

Incluye un depurador dentro del emulador (F12): desensamblador, vista de registros, volcado
de memoria y ejecución paso a paso.

## Compilación

### Windows, con MSVC (recomendado)

Es la ruta que funciona hoy sin conseguir nada a mano: CMake descarga
[sdl12-compat](https://github.com/libsdl-org/sdl12-compat) y deja las DLL junto al
ejecutable.

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Debug
```

Visual Studio también abre el directorio directamente, sin necesidad de generar una solución
(`dcemu.vcproj` es formato de 2003 y apunta al núcleo viejo `sh4.c`: no sirve).

Este build deja fuera, a propósito, la ventana de log (guichan) y el backend libcdio para
imágenes bin/cue y lectora física; se activan con `-DDCEMU_USE_GUICHAN=ON` y
`-DDCEMU_USE_LIBCDIO=ON`, pero hay que proveer esas dependencias. Las imágenes `.iso` se leen
con el lector propio de `iso9660_min.c`, sin libcdio.

Detalle del port y de lo que falta en [docs/msvc-build-plan.md](docs/msvc-build-plan.md).

### MinGW / Linux (histórico)

```sh
make -f Makefile.linux    # Linux   -> dcemu
make -f Makefile.win      # Windows -> dcemu.exe  (MinGW / Dev-C++)
```

Dependencias: SDL 1.2, OpenGL, [guichan](https://github.com/darkbitsorg/guichan),
libcdio + libiso9660 y [SIMDx86](https://sourceforge.net/projects/simdx86/). Las dos últimas
vienen en el árbol, bajo `include/` y `lib/`.

Advertencia honesta: **son dependencias de 2005 y ninguna se instala hoy sin trabajo.** SDL 1.2
está descontinuado, guichan no tiene releases desde 2010, y las librerías precompiladas de
`lib/` son binarios de MinGW de 32 bits.

Los directorios `obj/` y `logs/` deben existir antes de compilar y ejecutar.

## Pruebas

Hay una batería de pruebas unitarias para los opcodes del SH-4 en [`tests/`](tests/), sobre
la ruta de CMake/MSVC. Enlazan los handlers reales y la tabla real de `opcodes.c`, sin SDL
ni OpenGL.

```sh
cmake --build build --config Debug --target dcemu_tests
ctest --test-dir build -C Debug --output-on-failure
```

Son 387 casos que cubren las 239 filas de la tabla de instrucciones —una suite comprueba
que no quede ninguna sin ejercitar—. Encontraron 16 desviaciones respecto del manual del
SH-4, todas corregidas; la más gruesa: `DIV1` calculaba `T = Q && M` en vez de
`T = (Q == M)`, de modo que **toda división sin signo devolvía cociente 0**. De paso se
implementaron las 29 instrucciones que faltaban (`ADDV`, `SUBV`, `MAC.W`, `SHAL`, las
lógicas `.B` sobre memoria, `CLRMAC`/`CLRS`/`SETS`, varias `LDC`/`STC` y ocho de punto
flotante). El detalle, y lo que sigue sin emularse a propósito, está en
[tests/README.md](tests/README.md).

## Uso

```sh
dcemu [1st_read.bin | imagen.iso | imagen.cue]     # por omisión: 1st_read.bin
```

Requiere en el directorio de trabajo: `bios/bios.bin` (no se distribuye), `font.png` y
`fixedfont.bmp`. Si el argumento termina en `.bin` se carga junto con `ip.bin`; en cualquier
otro caso se abre como imagen de disco y ambos se extraen de ahí.

**Teclas**

| | |
|---|---|
| Flechas | Cruceta |
| `a` `s` `d` `w` | Botones X, A, B, Y |
| `z` | Start |
| `q` `e` | Gatillos izquierdo y derecho |
| `y` `h` `g` `j` | Stick analógico |
| `p` | Pausa |
| F1 | Pantalla completa |
| F2 | Ventana de log |
| F9 / F10 / F11 | Paso a paso / detener / ejecutar |
| F12 | Vista de depuración (arranca oculta: el emulador parte ejecutando) |
| `+` `-` (teclado numérico) | Recorrer el volcado de memoria |

## Estructura

El núcleo está en `sh4emu.c` (contexto de CPU) y `opcodes.c`, que expande una tabla de
instrucciones en cuatro arreglos de 65536 punteros a función, uno por cada combinación de los
bits `PR`/`SZ` del FPSCR: no hay decodificación en tiempo de ejecución. Los handlers se reparten
por categoría entre `mov.c`, `arith.c`, `logic.c`, `shift.c`, `branch.c`, `syscontrol.c`,
`float*.c` y `dcopcodes.c`.

`mem.c` resuelve todos los accesos con dos tablas de 256 entradas indexadas por el byte alto de
la dirección, una de punteros directos y otra de callbacks por región. `graficos.c` implementa
el PVR2 y el tile accelerator.

Hay una descripción detallada de la arquitectura en [CLAUDE.md](CLAUDE.md).

## Ramas

- **`master`** — la línea principal, 2004-04-22 a 2007-02-26.
- **`dcemu-exp`** — rama experimental de noviembre de 2005, abandonada a los 13 días. Contiene
  una refactorización que nunca se integró: archivo de configuración, abstracción de medios
  (`medium.c`, con soporte de lectora física), drivers de entrada separados y un verificador de
  `1st_read.bin`. Está muy atrasada respecto de `master`, pero esos archivos siguen siendo
  material aprovechable.

## Historia

El proyecto vivió en CVS. Este repositorio es una conversión completa: las 886 revisiones
originales se reconstruyeron desde los archivos `,v` y se reagruparon en commits conservando
autor y fecha, así que el historial es el real y no un volcado de la última versión.

Las direcciones de correo de los autores son sintéticas: CVS no las registraba.

## Créditos

Escrito por **itoledo**, con aportes de **necroromancist** y **basoft**.

Componentes de terceros incluidos en el árbol:

- [SIMDx86](https://sourceforge.net/projects/simdx86/) — Patrick Baggett (LGPL)
- [libcdio / libiso9660](https://www.gnu.org/software/libcdio/) — GNU (GPL)
- [stb_image](https://github.com/nothings/stb) — Sean Barrett (dominio público / MIT)
- BFont — renderizador de fuentes de mapa de bits para SDL
- En `dcemu-exp`, `1strdchk.c` es un port del *1st_read.bin File Checker 1.5* de LyingWake

La documentación de la SH-4 de Hitachi y la ingeniería inversa de la comunidad Dreamcast de la
época hicieron posible esto.

## Licencia

Sin licencia definida. El código propio se publica como archivo histórico; los directorios
`include/SIMDx86/`, `include/cdio/` y `lib/` pertenecen a terceros y mantienen sus licencias
originales (LGPL y GPL respectivamente).
