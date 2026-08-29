# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## What this is

`dcemu` is a Sega Dreamcast emulator written in C (one C++ translation unit) targeting
SDL 1.2 + OpenGL. It emulates the SH-4 CPU with a dynamic recompiler to x64 (on by
default since the 2026-08-20 adoption — `DCEMU_JIT=0` is the isolation lever that leaves
the threaded interpreter alone) and the PowerVR2 (PVR/TA) graphics chip by translating
tile-accelerator display lists into OpenGL calls.

The codebase is from 2004-2007 and was developed in CVS by `itoledo`, `necroromancist`
and `basoft`. Identifiers, comments and log strings are mostly in Spanish (`memoria`,
`graficos`, `pantalla`, `cargar_archivo`, `Dibujar*`) — match that when editing existing
code, using neutral Spanish (no voseo).

**Where the detail lives.** This file is the map and the rules. The archaeology — what each
bug looked like, how it was measured, why the wrong reading was plausible — lives in
`docs/notas-*.md`, one per subsystem. See "Where the deep notes live" at the end. Read the
relevant `notas` file before changing a subsystem; the failure modes there are not
recoverable from the code.

## Build

There is no linter and no CI. There is a unit-test suite for the SH-4 opcode handlers
(`tests/`, MSVC/CMake only) — see "Tests". Everything above the CPU core is verified by
building and running the emulator against a demo/ISO.

```sh
make -f Makefile.win      # Windows, MinGW/Dev-C++ gcc  -> dcemu.exe
make -f Makefile.linux    # Linux                       -> dcemu
make -f Makefile.win clean
```

**On this machine the emulator is built with CMake/MSVC, not with the makefiles** — there
is no `make` or Dev-C++ gcc installed, and the CMakeLists that `tests/` brought also builds
the `dcemu` target:

```sh
cmake --build build --config Release --target dcemu    # -> build/Release/dcemu.exe
```

Run it from the repo root (`bios/`, `font.png` and `roms/` resolve against the working
directory). Touching only `.c` files needs no clean; MSBuild tracks headers.

**Profile-guided optimization is wired up and it is not optional for measurement.** In this
tree the binary layout moves a benchmark as much as an optimization does: adding code to
`mmu.c` that an MMU-less guest never executes cost Crazy Taxi 8%, twice. PGO pins it — and it
is worth **+15% on Crazy Taxi and +9% on DCDoom** on top of that.

```sh
cmake -S . -B build -DDCEMU_PGO=GEN && cmake --build build --config Release --target dcemu
herramientas\pgo.ps1                 # trains on the fixed bench, merges the .pgd
cmake -S . -B build -DDCEMU_PGO=USE && cmake --build build --config Release --target dcemu
```

Two things the training gets wrong if left alone, both measured: the instrumented binary
**does not start without `pgort140.dll`** (it fails with `0xC000007B` and a shell that ignores
the exit code sees three happy runs and zero profile data — `pgo.ps1` copies the DLL and
verifies each run left its `.pgc`), and **an unweighted profile reproduces the very trade-off
PGO is there to remove**: 90 emulated seconds of each Katana guest are ~22 billion
instructions against DCDoom's 3.1, so the MMU guest lands at 12% of the profile and *loses*
6.3%. DCDoom merges with weight 7 (`pgomgr /merge:N`). The `.pgd` lives in `build-pgo/`,
outside the build tree, so it survives a clean.

**The JIT binary (`-DDCEMU_JIT=ON`, `build-jit/`) carries its own profile**,
`build-pgo/dcemu-jit.pgd` — it is a different program, and merging its runs into the tree's
`.pgd` would silently degrade the default binary. `herramientas\pgo.ps1 -Jit` trains it:
the same bench **plus Sega Rally 2** (60 s, no keys — `-Jit` only, so the default binary's
bank is untouched), each guest running **twice, once per form** (interpreter and translator,
equal weight), because that binary exists for the A/B between the forms and training only
one would leave the other laid out by the linker — the exact thing PGO is there to remove.
The script verifies each translator run left the `jit:` summary on stderr; without that
check, a JIT that fails to engage trains the interpreter twice and nobody notices.

**Desde la fase F.2 (2026-08-20) el binario que se entrega es el de clang con el traductor
encendido por omisión** — `build-clang\dcemu.exe`, con `DCEMU_JIT=ON` compilado adentro (ahora
la omisión del CMakeLists) y sin variable de ambiente el guest corre bajo el recompilador;
`DCEMU_JIT=0` es la palanca de aislamiento del intérprete, y `1`/`2` conservan su sentido
(bloques a mano / traductor), así que toda receta vieja con `DCEMU_JIT=2` significa lo mismo.
**Todo guion cuyo brazo de control borraba la variable pasó a poner el `0` explícito** — la
trampa que la adopción cobra. La F.1 que la avala: 131/131 demos con piso de ruido 0 y señal 0,
y 47/48 juegos exactos (el restante es MvC2, el expediente entendido al ciclo). **El «colgado» de
Mortal Kombat Gold resultó ser el tope de 32 pistas del lector de imágenes** (2026-08-24): su CHD
trae 53 y la pista final de datos —donde vive su 1ST_READ.BIN— se caía EN SILENCIO; el guest
arrancaba sobre ceros y la tormenta de TRAPA parecía incompatibilidad. `CDI_PISTAS_MAX` pasó a 99
(el tope del formato), con dos guardas nuevas que nombran la falla: llegar al tope avisa, y una
lectura fallida del binario de arranque avisa. MKG corre y pelea (int≡jit al punto, 40 000).
El binario MSVC (`build-jit/`) sigue como control del A/B entre cadenas.

**El binario clang existe y ganó su tanda (fase G, `build-clang/`)**: clang-cl 22.1.8
(`E:\llvm\22.1.8`, entorno vía `herramientas\llvm-entorno.ps1`) + lld-link + ThinLTO sobre el
mismo fuente, con `-ffp-contract=off` (el `/fp:precise` de clang sí contrae a FMA y la
conformidad se compara al bit — 113 191 ok / 0 fallan también bajo clang), `-fno-strict-aliasing`
(lo que los makefiles siempre pasaron) y **`/OPT:NOICF` obligatorio** — ver el invariante del
plegado ICF más abajo. Se configura con
`cmake -S . -B build-clang -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link -DDCEMU_JIT=ON`
y su PGO va por el esquema LLVM con perfil propio (`build-pgo/dcemu-jit-clang.profdata`;
`herramientas\pgo.ps1 -Clang`, mismo banco y mismos pesos). Medido 2026-08-20 con la compuerta
entera verde antes de cronometrar (capturas y 120 000 puntos de control idénticos en los tres
brazos por guest, `.wav` de CT byte a byte entre compiladores, trío CHD + THPS1 int≡jit): **DOOM
−2,7 %, CT −4,3 %, SR2 −2,3 %, tres rangos disjuntos** contra el MSVC reentrenado del mismo
fuente (clang `6D0D8B6DDE5E72A8`, MSVC `C1F4DC470406BC5C`). La serie de absolutos se corta ahí;
la adopción queda para la fase F. Detalle en `docs/jit-sota-plan.md`, fase G.

La misma rama trae `-DDCEMU_SAN=ASAN|UBSAN` (builds `build-asan/`/`build-ubsan/`, con
`DCEMU_LTCG=OFF`; UBSan además con `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` porque su
runtime es de CRT estático): la red de corrección para `jit.c` y el arena, nunca cronómetro.
El banco entero salió **limpio bajo los dos** el 2026-08-20, con la sonda verificada (los
símbolos del sanitizer están en el PDB — el silencio es limpieza, no una sonda muerta).

`obj/` and `logs/` must exist before building/running (both are kept in the repo via a
dummy `remove.txt`). `inicializar_logs()` aborts startup if it cannot create `logs/*.txt`.

Only `Makefile.linux` has header dependencies per object; `Makefile.win` does not, so
**after touching a header, `clean` first on Windows** or objects will go stale.

Dependencies: SDL 1.2, SDL_image, OpenGL/GLU, guichan (`guichan`, `guichan_sdl`,
`guichan_opengl`), libcdio + libiso9660, SIMDx86. The last three ship in-tree under
`include/` and `lib/{win32,linux}/`.

`Makefile.win` hardcodes `C:/Dev-Cpp` paths and `-march=athlon-xp -m3dnow`; both makefiles
compile with `-DPOSX -DX86_OPT -fno-strict-aliasing -O3`. `POSX` is defined on Windows too
— it does not mean "POSIX only". `X86_OPT` pulls in SIMDx86.

`dcemu.dev` (Dev-C++) and `dcemu.sln`/`dcemu.vcproj` (Visual Studio) exist but are stale
relative to the makefiles; the makefiles are the source of truth for the object list.

## Tests

```sh
cmake -S . -B build [-DDCEMU_SH4_JSON=/path/to/SingleStepTests-sh4]
cmake --build build --config Debug --target dcemu_tests dcemu_sh4json
ctest --test-dir build -C Debug --output-on-failure
```

`tests/` holds unit tests for every implemented row of `opcodes[]` (one suite per handler
file, plus one for the dispatch-table expansion), plus suites that are not opcodes:
`sistema` (PDTRA handshake, flash synthesis, RTC), `gdrom` (the drive's state machine,
driven exactly as the boot ROM drives it), `ta` (the TA parameter format — the
classification table and the reassembly of the 64-byte parameters), `jit_x64` (the x86-64
emitter of the recompiler, compared byte for byte against Intel's encodings — it is not
SH-4 at all, it is the tool the translation is made with), `mmu`, `wdt`, `tmu`,
`vram` (the two windows of PVR video RAM), `ubc` (the hardware breakpoint controller,
driven with the same register sequences KOS's driver uses), `vmu` (the memory card, driven with
the exact frames KOS's `vmu.c` sends), and `aica`, `arm7` and `g2dma`.

They link the real handlers and the real `opcodes.c`; `tests/memoria_prueba.c` replaces
`mem.c` and `tests/dobles.c` replaces the `graficos.c` / `iso.c` / `intc.c` / `traza.c`
symbols the code references, which keeps SDL and OpenGL out of the link. SDL *headers* are
still needed to compile (`opcodes.h` pulls in `main.h`).

**Several files are SDL-free on purpose so the suites can link them for real**: `sistema.c`,
`vram.c`, `ta.c`, `aica.c`, `arm7.c`, `g2dma.c`, `cdda.c`, `vmu.c`, `jit_x64.c`. Keep them
that way. `cdda.c` is
linked because `aica.c` calls it once per sample; `tests/dobles.c` supplies an `iso_leer_audio()`
that reports no audio tracks, so it stays silent and never touches the filesystem.

Every row of `opcodes[]` is implemented — the only one left on `NOIMP` is the catch-all
covering bit patterns that are not SH-4 instructions. `tests/README.md` lists the 16
deviations the suite originally found, plus the three things that do not match the manual on
purpose (the parts of the FPU that are neither Cause nor Flag, no cache behind `OCB*`, and
two `LDC ...,SGR` rows of doubtful existence).

A case marked `CASO_XFAIL` documents a known deviation and is expected to fail; if it starts
passing the runner reports `XPASS` and exits non-zero, so the note cannot go stale. The
`cobertura` suite walks `opcodes[]` and fails if any implemented row was never exercised, so
a new instruction gets flagged until it has a test.

End-to-end check after touching the CPU core: `demos/roto/` is a 256-byte rotozoomer that
exercises FSCA, FDIV, FTRC, FLOAT and MUL.L. See its README.

### The core against SingleStepTests/sh4

`tests/singlestep.c` builds a second binary, `dcemu_sh4json`, that runs the real core
against [SingleStepTests/sh4](https://github.com/SingleStepTests/sh4): 233 encodings × 500
cases with **full random initial and final state**. The suites above were written by reading
the manual, so they cover what one remembers to look at; these are the opposite, and they
found eleven more things. **`docs/sh4-conformidad.md`, "La segunda pasada", is the list.**

The data is 92 MB and not in the repo: `git clone https://github.com/SingleStepTests/sh4.git`,
then `-DDCEMU_SH4_JSON=` at configure time or the env var of the same name at run time.
Without either, the binary exits 77 and CTest marks the test **skipped**. No need to run
their `transcode_json.py` — the runner reads the binary format directly.

**They are not the manual: they came out of Reicast's interpreter.** Where the two disagree
the manual wins, and the 3221 disagreements are classified one by one and counted apart —
neither green nor red — with the manual quote that settles each. Floats are compared
**bit-exact**, on purpose — that is what exposed the rounding mode, whose differences were
one ulp — with two documented exceptions (any NaN equals any other; FIPR/FTRV/FSRRA/FSCA
against an error bound). `tests/README.md` has the rest.

### The regression baseline above the CPU core

**The KOS example tree.** `docs/demos-kos.md` records the state of all 135 binaries as
measured on 2026-07-30 — which ones pass, which fail and why, and which fail because they ask
for hardware that is not emulated. It also documents how the sweep is run and the two ways it
produces false negatives. Read it before concluding that a demo is broken.

**The VMU's presence is part of the baseline configuration, not a detail.** A card on the bus
adds maple traffic to every vblank, which moves the emulated frame boundary, so an animated demo
lands on a different frame at the same `--salir-tras` — all seven PVR control demos changed hash
when the VMU was added, and all seven came back byte-identical under `--sin-vmu`. Two
consequences: a sweep is only comparable against another sweep with the same VMU setting, and
**a run can write the card**, so an A/B has to start from the same image (delete
`bios/vmu-a1.bin`, or point `--vmu=` at a scratch copy). DCDoom's end-to-end reference has one
hash per configuration: `198B396F…` with `--sin-vmu`, `68F7C61A…` with the card. **Both changed on
2026-08-06** when the half-pixel sampling convention was corrected; `DCEMU_SIN_MEDIO_PIXEL=1`
reproduces the previous `36578F59…` byte for byte, which is what proves the switch isolates it.

**A KOS demo sweep cannot catch every regression, and this matters when judging a change.**
Several paths are exercised only by commercial games: mipmapped textures, the TSP repeat
modes, blend codes 2 and 3, the Offset Color, the texture environment's alpha rules, the
render-latch registers, the nested-bank swap. For each of those the whole ten-demo control
set stays byte-identical while a game changes. When a change touches one of them, the four
games are the test.

## Run

```sh
dcemu.exe [opciones] [1st_read.bin | imagen.iso | .cdi | .gdi | .chd]   # default: 1st_read.bin
```

Needs, relative to the working directory: `bios/bios.bin` (not in the repo), `font.png`
(BFont) and `fixedfont.bmp` (guichan). `bios/flash.bin` is optional — without it a minimal
flash is synthesized. An argument ending in `.bin` loads `ip.bin` plus that binary directly;
anything else is opened as a disc image, and `ip.bin` / the boot binary are pulled from it
(descrambled by `scramble.c`). The boot binary's name comes from IP.BIN offset 0x60 — it is
not always `1ST_READ.BIN` (DCDoom's is `0WINCEOS.BIN`).

Options are parsed by `opciones.c` into the global `opciones`:

| opción | qué hace |
| --- | --- |
| `--bios` | arranca en `0xA0000000` y deja trabajar al boot ROM real; el argumento posicional es la imagen que ve la lectora. Apaga los hooks de syscall |
| `--cable=vga\|rgb\|compuesto` | tipo de cable que devuelve el handshake de PDTRA (VGA por omisión) |
| `--bandeja=auto\|disco\|vacia\|abierta` | estado inicial de la lectora; `auto` mira si hay imagen montada |
| `--disco=IMAGEN` | imagen que ve la lectora cuando el argumento posicional es un `.bin` suelto — que sin esto arranca con la bandeja vacía. Es lo que hace comprobable a `sound-cdda-basic_cdda` (y a cualquier demo que use el disco) |
| `--traza-mem` | reporta a stderr las direcciones sin emular y dónde se traba el PC, con el tiempo emulado, y al salir la relación con el tiempo real |
| `--limitar` | no dejar que la emulación corra más rápido que una consola. Solo frena |
| `--hacks-bios` / `--sin-hacks-bios` | fuerza o desactiva los hooks de syscall |
| `--captura-gl=ARCHIVO` | vuelca a un BMP lo que OpenGL rasterizó, en cada cuadro |
| `--captura-audio=ARCHIVO` | vuelca a un `.wav` lo que el mezclador del AICA produjo. Es **la medida** del sonido, no lo que hizo la tarjeta |
| `--sin-audio` | no abrir la tarjeta de sonido. El AICA se emula igual y `--captura-audio` sigue funcionando |
| `--sin-aica` | no emular el AICA: ni el ARM, ni los canales, ni los temporizadores. Para aislar una regresión |
| `--vmu=ARCHIVO` | imagen de la Visual Memory de la ranura 1 (`bios/vmu-a1.bin` por omisión; se crea formateada si no existe) |
| `--sin-vmu` | sin tarjeta en la ranura 1. Es el interruptor de aislamiento, y **el que reproduce la línea base anterior byte a byte** |
| `--render=MODO` | `ventana` (por omisión, y **es la referencia**), `fbo` —rasterizar a la resolución emulada en un destino propio, respetando el aspecto— o `shader`, que además rasteriza con GLSL en vez de función fija |
| `--escala=N` | resolución interna ×N (1 a 8). Implica `--render=fbo`. **Medida: no cuesta nada** — ver abajo |
| `DCEMU_OIT_SOLO_FONDO=1\|2\|3\|4` | sonda de `--render=oit`: 1 emite sólo el fondo, 2 pinta cuántas capas juntó cada píxel, 3 el **alfa** del fondo (que es lo que consume la mezcla por DST_ALPHA y una captura RGB no muestra) y 4 el color del fragmento más cercano sin mezclar. Separan «la lista está vacía» de «la mezcla da negro», que dan el mismo síntoma |
| `DCEMU_VOL_SONDA=1\|2` | sonda de los volúmenes por píxel: 1 pinta la tira de rojo donde la máscara dio dentro y de verde donde dio fuera —lo que el shader **lee**—, 2 lee la máscara de vuelta y cuenta los texeles marcados —lo que la pasada **escribió**—. Hacen falta las dos: dan el mismo síntoma y separan el lado que falla |
| `DCEMU_SIN_VOL_PARIDAD=1` | los volúmenes modificadores vuelven a la **cuenta con signo por sentido de giro** (INCR frontales / DECR traseras, dentro = cuenta ≠ 0), la conducta anterior byte a byte — verificado contra el binario previo en el cuadro 8400 del attract de CT. Encendida por omisión (2026-08-25), la marca es la del chip: **paridad de caras delante de la superficie, por grupo, plegada con OR** (DevBox §3.4.5.1 — el hardware ignora el devanado). Es el arreglo de la **sombra-cortina del taxi de Crazy Taxi al saltar**: sus volúmenes traen devanado mixto (30 de 30 grupos medidos, p. ej. 20 CW/16 CCW en un volumen cerrado) y la cuenta con signo marcaba las paredes. Árbitro de consola real: el storyboard del attract en hardware (`l78Y3gAblwY`, ~91 s) — sombra plana desplazada en el suelo, sin cortina. Donde el devanado es consistente y no hay solape, paridad ≡ cuenta (±1 coinciden módulo 2), que es por qué el parque no se mueve. `herramientas/vol-paridad-gate.ps1` y `vol-paridad-lote.ps1` |
| `DCEMU_SIN_MEDIO_PIXEL=1` | vuelve al punto de muestreo de antes del 2026-08-06: GL en el centro del píxel en vez del entero, que es donde muestrea el chip. **Cambia todas las capturas del árbol**, así que es el interruptor que reproduce cualquier línea base anterior byte a byte |
| `DCEMU_MEDIO_PIXEL_MIL=N` | fija el corrimiento del `glOrtho` en `N` milésimos de píxel de destino en vez del valor del árbol (484). El punto de muestreo queda en `s = 0,5 − N/1000`, y es la sonda que **cerró la pregunta del medio píxel**: `pvr-fb_tex` es un medidor directo de la convención, con una ventana de dos lados —pasa si y sólo si `0 ≤ s < 0,5`, medido en `herramientas/fbtex-ventana.ps1`— y el árbol cae en el medio. El conteo de colores del logo de Crazy Taxi no era un segundo testigo: mide dónde puso el guest su geometría (una grilla en `x,9`), no dónde muestrea dcemu. Ver la regla en «Graphics pipeline» |
| `DCEMU_SIN_CLAMP_BORDE=1` | vuelve a `GL_REPEAT` en las tiras cuyas UV no salen de [0,1], que es la conducta anterior. Encendido por omisión: **es el arreglo de la costura del logo de Crazy Taxi**. Esa pantalla son cuadros de 16×16 pegados borde con borde con UV **exactamente 0..1** y REPEAT — a 1:1 no hay mezcla, pero la ventana estira 640→800 y en el borde de cada cuadro GL envuelve y trae el texel opuesto: una línea cada 20 píxeles. Si el rango de UV cabe en [0,1] la tira **nunca repite**, así que REPEAT y CLAMP_TO_EDGE solo difieren en el filtro del borde. Seguro por construcción, y la medida lo confirma: de los 15 juegos **11 salen byte a byte iguales** (SF3 entre ellos) y de las 139 demos cambian 13 fuera del piso de ruido, todas de textura a pantalla completa, sin un solo cambio de veredicto. En DCDoom y 4x4 EVO lo que cambia son **2796 píxeles exactos** — el perímetro de 800×600 al píxel |
| `DCEMU_MEDIO_TEXEL=1` | enciende el medio texel del lado de la textura, que estuvo por omisión un solo día (2026-08-14/15) y **está apagado**: la premisa era que el chip mapea `u=0` al centro del texel 0 y GL a su borde, y **el propio guest la desmiente** — las UV crudas de Street Fighter III son `0,5/256`, `16,5/256` y `32,5/256`, o sea que el juego ya direcciona centros de texel. Sumarle otro medio deja el muestreo sobre la frontera: su fondo pasa de **0 a 5012 picos de costura por columna** y de 0 a 2633 por fila, de una imagen limpia a una rejilla; ChuChu sube 20 %. Cambia todas las capturas del árbol (37 de las 139 demos) y `pvr-fb_tex` sale idéntico con y sin **a 1:1** y no en el camino de ventana, donde el corrimiento de la geometría es 0,4 px. Queda porque **la costura del logo de Crazy Taxi sigue abierta**, con hipótesis nueva: UV 0..1 con REPEAT, el filtro envuelve en `u=1,0`. Ver la regla en «Graphics pipeline» |
| `--watchpoint=D[:T]` | informa cada escritura que toque `D` (hex), de `T` bytes, con el PC y el PR |
| `--watchpoint-lectura=D[:T]` | lo mismo para las lecturas: una línea por cada PC distinto que mire `D` |
| `--traza-desde=PC[:N[:K]]` | desensambla las `N` instrucciones que siguen a la llegada a `PC`, saltándose las `K` primeras, con los registros que cambian. Necesita `--traza-mem` |
| `--desensamblar=D:N` | al salir, desensambla `N` instrucciones desde `D`. Repetible |
| `--volcar=D:N` | al salir, vuelca `N` bytes desde `D` en hexadecimal. Repetible |
| `--salir-tras=N` | sale solo a los `N` segundos de tiempo **emulado** |

Los de diagnóstico son los seis últimos y **todos sus números van en hexadecimal** (salvo los
segundos de `--salir-tras`).

Environment variables, all decimal (`atoi`) — see `docs/notas-herramientas.md` for each:

| variable | qué hace |
| --- | --- |
| `DCEMU_PULSAR_START=N[,...]` / `DCEMU_SOLO_A=N[,...]` + `DCEMU_PULSAR_A=1` | aprietan el botón durante 20 sondeos desde cada número; 60 sondeos por segundo emulado |
| `DCEMU_SIN_FMT_CLAVE=1` | la clave de la caché de texturas vuelve a ignorar el formato de píxel — el comportamiento anterior byte a byte. Con el formato fuera de la clave, una misma dirección declarada ARGB1555 por una tira y RGB565 por otra comparte entrada y sirve la decodificación del último que la regeneró: el auto «semitransparente»/confeti de los menús de Sega Rally 2 (fotos 1555 del carrusel y página de librea 565 en el mismo slot). El arreglo sigue la regla que ya tenía el bit de mipmap |
| `DCEMU_GRABAR_MANDO=archivo` / `DCEMU_MANDO=archivo` | la grabadora y el replay de la entrada, al nivel de lo que ve el Maple: una línea por cambio de estado con su número de sondeo. La grabación va **después** de todas las mezclas (teclado, XInput y las variables de arriba: lo grabado es lo que el guest vio, así que una receta vieja se graba una vez y se reemite idéntica), y el replay **reemplaza** la entrada real entera — el jitter analógico de un mando enchufado no se cuela. El sondeo es tiempo emulado: se graba jugando con `--limitar` y se reproduce a toda velocidad. La VMU sigue la regla de siempre: grabar y reproducir arrancan de la misma imagen. Validado en lazo cerrado: receta grabada → replay → captura byte a byte idéntica. **Un paso también puede fijarse por milisegundo emulado**, con el prefijo `t` (`t45500 fffb 0 0 128 128`): el sondeo cuenta recorridos del Maple y su relación con el tiempo no es fija —el tráfico de la VMU lo acelera—, así que una receta de menús escrita en sondeos se desincroniza con cualquier cambio de temporización; la forma `t` es estable ante eso |
| `DCEMU_MAPLE_DEMORA_VMU_US=N` | el **tiempo de servicio de la VMU**: un recorrido del Maple que tocó la tarjeta no termina antes de N µs (13 000 por omisión; `0` lo apaga, la conducta anterior). Es el arreglo de la **caja automática de Sega Rally 2 y su rapid-fire de START**: la tarjeta real es un microcontrolador lento, dcemu contestaba en el largo del alambre (0,2-0,9 ms) y a esa velocidad el montaje de MapleDev (WinCE) falla y FILESYS lo reintenta cada ~133 ms para siempre — cada vuelta tumba y rearma DirectInput con 1-2 cuadros de acelerador en cero, y el promedio dejaba la primera marcha a 63 mph, bajo el umbral de 64,5 del cambio. El barrido de la demora (tabla en `mem.c` sobre `MAPLE_VMU_DEMORA_US`): ≤1 ms reintento eterno, 2-10 ms **cuelga el arranque** (el timeout de ~10 ms de WinCE), 12-14 ms sano, ≥16 ms cuelga de nuevo (el fin cae tras el próximo vblank). Verificado: segunda a los 76,5 s y tercera marcha con la omisión y sin variables; pausa firme; compuerta de 5 juegos (DCDoom, VT, CT, CT2, MKG) verde. **Cambia la temporización de todo guest que toque la tarjeta**: el `0` reproduce la línea base anterior |
| `DCEMU_MAPLE_DEMORA_NOMINAL=1` | el fin del DMA Maple vuelve a estimarse en 8 palabras por marco en vez de contar las palabras reales de cada respuesta (un `BREAD` de VMU son 131). El A/B del largo del alambre; ojo: por sí sola la cuenta real salió **peor** para SR2 (los marcos sin VMU terminaban antes, no después) — el arreglo es la demora de servicio de arriba |
| `DCEMU_MAPLE_PUBLICAR_AL_FIN=1` | las respuestas del Maple se escriben a la RAM todas juntas al completarse el DMA, en vez de al armarse cada una (la omisión, que es lo que hace el bus: cada respuesta se DMA al llegar; solo la interrupción marca el fin). Medido neutro en el expediente de SR2 al dígito |
| `DCEMU_SONDA_MAPLE=1\|2` | el censo del DMA Maple desde los 35 s: por DMA, comandos, destinos y palabras nominales contra reales; con `2`, además las palabras del pedido y la cabeza de la respuesta de cada marco hacia la VMU — lo que dejó legible el ciclo eterno del montaje |
| `DCEMU_SONDA_SR2=1` | telemetría del auto de Sega Rally 2 cada 100 ms emulados (velocidad, marcha, rpm, vel/rpm normalizadas, rampa del acelerador), localizando el bloque del auto por su firma (el factor 0,78 y la tabla de la caja). Pasiva, desde el bloque periódico; existe porque las capturas no pueden comparar velocidades terminales entre corridas cuyos menús arrancan la carrera en instantes distintos |
| `DCEMU_WATCHPOINT_TODAS=1` | el watchpoint informa **cada** acceso en vez de uno por PC distinto — para preguntas de cadencia, no de «quién mira» |
| `DCEMU_WATCHPOINT_VIRTUAL=D[:T]` | watchpoint por dirección **virtual** (hex), lecturas y escrituras, con la física resuelta en el informe — el complemento del watchpoint físico cuando WinCE remapea las páginas a mitad de corrida |
| `DCEMU_VOLCAR_RAM=archivo` | los 16 MB de RAM del sistema en crudo, al salir: la búsqueda por diferencia — dos corridas idénticas salvo lo aislado, y las direcciones donde difieren son un puñado |
| `DCEMU_CAPTURA_TODAS=N` | guarda un cuadro de cada N a un archivo numerado propio |
| `DCEMU_TRAZA_EXC=1\|2\|3` | histograma de excepciones / censo de sitios de syscall / flujo completo |
| `DCEMU_TRAZA_SYSCALL=dest[:pr[:N[:K]]]` | traza de instrucciones en la K-ésima aparición de ese syscall (hex:hex:dec:dec) |
| `DCEMU_TRAZA_DEPURACION=1` | imprime lo que el guest manda a su salida de depuración (CE) |
| `DCEMU_TRAZA_EN_MS=N[:M]` | puntos de control por milisegundo de PC y registros. **Enciende la traza, que apaga el JIT**: para una divergencia del traductor usa `DCEMU_CP_MS` |
| `DCEMU_CP_MS=N` | un punto de control por ms emulado (PC, registros, MACL, FR0/FR1) desde el bloque periódico, **sin apagar el JIT**: dos corridas exactas dan puntos idénticos y el primero distinto acota una bifurcación intérprete/traductor a un milisegundo. Costo cero apagada |
| `DCEMU_TRAZA_ESCENA=N[:M]` / `=+K[:M]` | vuelca una escena entera tira por tira, por número o por peso |
| `DCEMU_VOLCAR_TA=archivo:desde:hasta` | los bloques crudos de 32 bytes del embudo del TA mientras la escena vigente cae en `[desde, hasta]` (decimal, la cuenta de `DCEMU_TRAZA_ESCENA`; necesita `--traza-mem`). Es la **verdad de base** de la geometría: separa «el guest no lo mandó» de «dcemu lo perdió en el rearmado» — lo que un volcado de `TriangleStrip[]` no puede contestar. Con él quedó absuelto el TA en el expediente del auto de SR2 (quads de 4 vértices en el crudo = quads en el buffer, sin pérdida) |
| `DCEMU_SIN_TEX=hex` | descarta al dibujar toda tira cuya textura viva en esa dirección. Separa «lo que tapa» de «lo que falta»: si una figura se compone al quitar un atlas, esas tiras la pintaban encima; si queda un agujero, lo de abajo nunca estuvo |
| `DCEMU_TRAZA_ATA=cmd:N` | traza lo que hace el driver con lo que la lectora contestó |
| `DCEMU_TRAZA_TLB=D` | informa (hex, como `DCEMU_TRAZA_SYSCALL`) a qué física traduce cada `LDTLB` la dirección virtual `D`. **Los watchpoints comparan direcciones físicas**: vigilar la virtual tal cual ya produjo una conclusión falsa |
| `DCEMU_WATCHPOINT_MAX=N` | sube el tope de informes del watchpoint (200 por omisión) |
| `DCEMU_COMO_GD=1` | presenta el disco como el GD-ROM del que se ripeó (rama equivocada, ver notas) |
| `DCEMU_PERFIL_ARM=1` | histogramas del ARM7 por dirección y por fila de despacho |
| `DCEMU_SIN_DIBUJO=1` / `DCEMU_SIN_VOLUMEN=1` / `DCEMU_SIN_FILTRO_MIP=1` | aíslan una etapa del render para medirla |
| `DCEMU_SIN_CDDA=1` | la lectora contesta el audio de CD como siempre pero no entrega muestras. Calla la salida, no el mecanismo: apagarlo entero cambiaría el camino del guest que sondea su música |
| `DCEMU_SIN_CACHE_MMU=1` | apaga las tres cachés de traducción de la MMU. **Valen 1,8× en DCDoom**; es el interruptor del A/B y para aislar una regresión |
| `DCEMU_SIN_MMU_MACRO=1` | todo acceso de datos entra por `mmu_traducir()` en vez de sondear la caché dentro del macro de `memread`/`memwrite`. Es el A/B de la fase 3 de `rendimiento-plan-2.md`: **vale 1,6 % en DCDoom**, ≈0 sin MMU |
| `DCEMU_FORMA=1` | forma de ejecución del guest: longitud de los bloques básicos, cuántos distintos y con qué reincidencia. **Sólo existe si se compiló con `-DDCEMU_FORMA=ON`**, porque el gancho cuesta 4,4 % (ver `docs/interprete-plan.md`) |
| `DCEMU_INLINE` (compilación) | despacha en línea los diez manejadores más frecuentes, sin llamada indirecta. **Medido: cuesta 19 %** aunque cubra el 35,3 % de las instrucciones — el bucle caliente engorda más de lo que ahorran las llamadas |
| `DCEMU_SONDA_BLOQUES=1` | caché de bloques predecodificados: saltea la búsqueda de la palabra y la de la tabla de 65536 punteros. **Medido y no sirve** — ruido en juego, −3,3 % en menús—, así que sólo existe con `-DDCEMU_BLOQUES=ON`. Queda para volver a correr el A/B sin rehacer la idea |
| `DCEMU_FUSION=1` | el lazo más caliente de Crazy Taxi corre como C fusionado (registros en locales, sin despacho por instrucción). **Sólo existe con `-DDCEMU_FUSION=ON`**: es la sonda que decidió el recompilador — **17,4 % cubriendo el 46 % del volumen**, ejecución idéntica al dígito. Ver `rendimiento-plan-2.md`, fase 4 |
| `DCEMU_JIT=0\|1\|2` | el recompilador dinámico. **Desde la adopción (F.2, 2026-08-20) la omisión es el traductor**: sin variable corre `2`, y `0` es la palanca de aislamiento que deja al intérprete solo — todo guion cuyo brazo de control borraba la variable pone ahora el `0` explícito. Compilado adentro por omisión (`-DDCEMU_JIT=ON`); el A/B sigue corriendo sobre una sola imagen, con el perfil propio del binario (ver arriba). Con `1` corren los dos bloques emitidos a mano de la fase 0; con `2` (o sin variable), el traductor automático: **144 plantillas elegidas por censo (las últimas 22 por el censo ponderado por veces: PREF, la geometría FPU, los movedores, XTRCT/ADDC, y el lote B.2b — cinco filas TERMINALES que traducen a los escritores de SR, LDTLB, TRAPA y FSCHG/FRCHG terminando el bloque en ellas, más las filas FPU directas admitidas en ranura de retardo), exacto al dígito y con capturas byte a byte en los tres guests del banco y en el trío CHD** — DCDoom (MMU, **56,1 instrucciones por entrada, −41,5 % y 1,37× tiempo real**), Crazy Taxi (**34,4, −27,6 %, 2,21×**) y Sega Rally 2 (MMU+FPU, **35,2, −29,7 % y 1,16×**), tanda 2026-08-19 sobre binario reentrenado `14D6AFDA3C7BF7BE` (`docs/jit-sota-plan.md`, fases B.2/B.2b; palancas `DCEMU_JIT_SIN_TERMINALES` y `DCEMU_JIT_SIN_RANURA_FPU`). Los pares de rama cubren también BSR/JSR/BSRF: PR se compromete después de la ranura solo cuando esta no lo lee ni escribe, así una falta conserva el PR anterior como la instantánea del intérprete. **Los ciclos de la rama van después de la ranura** — sumarlos antes se evapora en la recarga de CYC de una ranura por manejador, ±15-19 k instrucciones de divergencia con la captura intacta. Lo que lo hace sano son la **época** con SR.MD en la clave de validez, el **puente** entre páginas (`DCEMU_JIT_SIN_PUENTES=1` lo aísla), y la **clave FPU con SR.FD como bit 3**. La regla de lectura: **los absolutos se comparan dentro de un binario o no se comparan**. El estado, las reglas y los veredictos completos viven en `docs/recompilador-plan.md` |
| `DCEMU_JIT_PLANTILLAS=N` | recorta la tabla de plantillas del traductor a las primeras `N`. Es la palanca de bisección: una plantilla infiel se delata en la cuenta de instrucciones, pero la cuenta no dice cuál |
| `DCEMU_JIT_VOLCADO=ARCHIVO` | vuelca el código emitido en crudo y lista dónde quedó cada bloque, para desensamblarlo. Es el desarme del riesgo «el emisor mismo» del lado del binario; del lado del código lo es `tests/test_jit_x64.c`. **En esta máquina no hay `objdump`**: el desensamblador es el de LLVM que vive en `E:\llvm\22.1.8` (22.1.8, fuera de `Program Files` y fuera del árbol de VS a propósito — el toolset de Visual Studio ya no lo trae; el entorno se arma punteando `herramientas\llvm-entorno.ps1`, y el porqué está en `docs/jit-sota-plan.md`, fase G paso 0), y como `llvm-objdump` no lee binarios crudos, el crudo se envuelve primero — `llvm-objcopy -I binary -O elf64-x86-64 v.bin v.o` y después `llvm-objdump -D --triple=x86_64 --section=.data v.o`. Ese LLVM **tampoco trae `llvm-mc`**, pero el ensamblador integrado de clang cubre lo mismo: `clang -c a.s -o a.o` sobre un `.s` con `.intel_syntax noprefix` |
| `DCEMU_JIT_SIN_FPU_MMU=1` | cierra la compuerta MMU+FPU (resuelta: era el 0x800 del cambio perezoso de contexto FPU de WinCE, con FD como bit 3 de la clave de modo). Aislamiento, y la línea base anterior |
| `DCEMU_JIT_CORTE_EPOCA=1` | enciende el corte de época tras cada escritura en bloques MMU: protector del agujero teórico del orden de búsqueda, que **ningún banco observa** y cuesta 6-8 % de cobertura. El análisis en el plan |
| `DCEMU_JIT_BUSCADOR=1` | enciende el despacho emitido dentro del arena. **Medido neutro** aun sirviendo la salida dominante — el viaje al despachador C no es el costo, tres mediciones lo dicen — y queda como palanca para rehacer el A/B |
| `DCEMU_JIT_SIN_HOGARES=1` | colocación secuencial vieja de las ranuras y cero costuras de enlace: el aislamiento de la fase de registros persistentes, con emisión bit-idéntica a la anterior |
| `DCEMU_JIT_COSTURAS=2` | reactiva el talón de costura con cargas parciales, que **perdió su A/B** (CT +0,7 %: el salto extra y la línea fría de icache superan a las 4-5 cargas elididas); por omisión solo la costura vacía |
| `DCEMU_JIT_SONDA_CRUCES=1` | un contador emitido en la cabeza de cada bloque: cruces de enlace (corridos − entradas; 36-42 % de las fronteras) y el censo de presencia de registros ponderado por veces, ambos al salir |
| `DCEMU_JIT_FLUJO=1` | revive el superbloque por flujo (seguir BRA/BSR/RTS al descubrir, con rastreo de PR y guarda en caliente). **Medido neutro dos veces**, y la segunda con el enlazado ya arreglado —que era el motivo legítimo para dudar del primer veredicto—: DOOM y CT solapados, **SR2 +0,9 % con rangos disjuntos**. Crazy Taxi ahorra **68 millones de entradas al despachador (−4,6 %)** y el tiempo no se mueve: la tercera medición que dice que el viaje al despachador C no es el costo. Efecto lateral que nadie esperaba: el flujo **acorta** los bloques (22,7→19,8 instrucciones en DOOM), porque seguir una arista desemboca en código que la traza ya tiene y ahí se corta. Exacto al dígito en los tres guests (9000 puntos de `DCEMU_CP_MS` idénticos) |
| `DCEMU_JIT_SIN_PARES=1` / `DCEMU_JIT_SIN_PARES_LLAMADA=1` | apagan todos los pares de rama o solo BSR/JSR/BSRF. Los de llamada solos valen ~1,0/5,5/0,4 % en DOOM/CT/SR2; el par **termina la traza** |
| `DCEMU_JIT_SONDA_ACCESOS=1` | el censo de los accesos emitidos: cuántos toman el camino rápido y **por qué guarda** cae el resto al ayudante (desalineado, cambio de modo, las cinco de la traducción por separado, zona no plana, página con código). Va en corrida aparte, como la sonda de cruces: cambia la emisión. Es la sonda que **reescribió la fase 6** — mostró que la zona no plana era el 2,3 % y la traducción el 33,6 %, y que el 98 % de esos fallos eran P1/P2, que no se traducen. Tres guardas —alineación, modo y UBC— **no se disparan ni una vez** en los tres guests; las dos últimas ya no se emiten y la de alineación no se puede plegar (es el error de dirección, o sea una función) |
| `DCEMU_JIT_GUARDAS_VIEJAS=1` | vuelve a emitir las dos guardas por acceso que el censo mostró muertas y reproduce la emisión anterior byte por byte. Apagadas por omisión: el **UBC de operando** se pliega en las tablas base como el watchpoint (`mem_directo_recalcular()`, y los ayudantes físicos corren el gancho con la virtual), y el **cambio de modo** desaparece del lado MMU, donde escribir MMUCR ya vacía `mmu_datos` entero y manda todo acceso emitido al ayudante — del lado plano se queda, porque ahí la tabla de zonas contesta con base directa aunque la traducción se acabe de encender. **DCDoom −1,6 %**, rangos disjuntos; SR2 y CT dentro de su dispersión. Emisión −1 132 176 bytes (−3,5 %) |
| `DCEMU_SONDA_CUADROS=1` | la sonda de tirones: la **distribución** de tiempos de cuadro (p50/p90/p99/máx, cuántos pasan de 16,7 y de 33,4 ms) y, de los peores, **qué pasó dentro** — bloques traducidos y su tiempo, movimientos de época, texturas, tiras, y **el tiempo emulado que avanzó**, que es el que separa «dcemu se frenó» de «el juego hizo un cuadro largo». Existe porque **un tirón no mueve la media**: 1300 cuadros lentos entre 7200 son medio segundo sobre dos minutos, invisibles en una tanda y lo único que se nota jugando. Es la que encontró el enlazado cuadrático. Dice explícitamente qué no está contando: `tex` y `tiras` piden `--perf`, y un cero ahí se leería como «las texturas no fueron» en vez de «nadie las miraba» |
| `DCEMU_JIT_VERIF_COMPLETA=1` | la verificación por entrada vuelve a comparar la **clave entera**, como antes. La clave lleva época y SR.MD porque es lo que compara el salto encadenado, que se saltea el despachador; la verificación por entrada no se lo saltea — calcula el puntero de búsqueda y lo compara, y ese puntero **ya identifica página, ASID y modo**. Exigirle además la clave mandaba a comparar palabra por palabra bloques intactos: **el 20-22 % de las entradas** de DCDoom y SR2, y en SR2 el **96,7 % de ellas acertaba**. Separado, las verificaciones largas de DCDoom pasan de 9,1 % a 0,9 % y las palabras de 56 M a 428 k: **DOOM −1,6 %, SR2 −3,2 %**, rangos disjuntos; CT no distingue (sus contadores salen idénticos en los dos brazos). La palanca vive en los movimientos de época y **no** en `jit_verificar()`: puesta ahí costaba +1,0 % en CT por la rama, una vez por cada una de sus 1490 millones de entradas |
| `DCEMU_JIT_SIN_VARIANTES_FPU=1` | la búsqueda de bloques vuelve a ser **ciega al modo FPU** (2026-08-23): el modo equivocado era un rechazo del despachador que mandaba el tramo al intérprete. El desglose nuevo del resumen (`de esos rechazos: … modo MMU, … modo FPU, … palabras`, con los PC reincidentes) mostró que **el 70 % de los rechazos de DOOM (5,8 M/35 s) y el 94 % de los de SR2 (3,9 M/60 s) eran modo FPU, y en SR2 dos sitios cargaban 3,7 M** — entradas de bloque visitadas bajo los dos modos PR/SZ. Encendida (la omisión), `jit_buscar()` saltea la entrada de otro modo y sigue el sondeo: el modo equivocado es un **miss que traduce la variante hermana**, y las dos conviven en el hash — los enlaces ya rechazaban bloques FPU, el salto encadenado re-verifica la clave, y el dedup de `tr_traducir` usa el mismo buscar. SR2 queda en **0 rechazos FPU con 2 variantes**; DOOM en 0 con 0 (renace en el modo vigente). Techo medido honesto: los tramos interpretados eran ~1,4 instrucciones — el costo era el viaje al despachador. Tanda (`2ED0992E6125BB2A`, ambiente degradándose): **DOOM −1,1 % con 4/4 y solape** (el estándar débil de la máscara), SR2 −0,6 % 3/4, CT inerte. El residuo nombrado: los 2,5 M de rechazos por **palabras** de DOOM — remapeos de WinCE cuyos bloques corren interpretados para siempre; para su propio censo |
| `DCEMU_JIT_SIN_RETRADUCIR=1` | apaga la **retraducción por fallo de palabras** (2026-08-24): cuando `jit_verificar` encuentra que la memoria ya no es la traducida (los remapeos de WinCE), el bloque viejo recibe una lápida en el pc y el lazo del despachador traduce el contenido vigente como a cualquier miss, con herencia de cuenta y tope de 16 por PC contra el ping-pong de contenidos alternantes — que el censo mostró que **no existe**: DOOM 2 531 592 → **117** rechazos por palabras (117 retraducciones, 0 al tope: cada sitio cambió una vez), SR2 228 293 → 309. Sin ella esos bloques corrían interpretados PARA SIEMPRE. Tanda sobre el canónico reentrenado (`3FB976C6AE4C55CE`): **SR2 −2,3 % con rangos disjuntos y 4/4** — su mayor ganancia desde el atajo P1/P2, y **cruza el tiempo real (~1,04×)** —, DOOM neutro (sus viajes al despachador eran baratos), CT inerte. El efecto que ganó SR2 lo delató la contabilidad: los sitios calientes remapeados (los bimodales del expediente FPU) quedan retraducidos a su forma vigente — sin filas FPU, un solo bloque para ambos modos, entradas más largas (36,0 → 37,2) — en vez de rebotar entre variantes y verificaciones. Los rechazos quedan en ~1 200 por guest (modo MMU del arranque): extintos como categoría |
| `DCEMU_JIT_TRAD_EN_LINEA=1` | vuelve a emitir la **traducción MMU en línea en cada sitio de acceso** (2026-08-25), la conducta anterior. Por omisión el cuerpo de `gen_traducir_mmu` se emite UNA vez como dos **rutinas compartidas** (lectura/escritura) al frente del arena y cada sitio llama por rel32, conservando el atajo P1/P2 en línea y decidiendo su camino lento con el EAX devuelto. La cadena de sondas que lo encontró: reparto (SR2 a 7,4 ns/instr contra 3,6 de CT) → censo de accesos (254 B emitidos/instr contra 94) → censo de bytes por plantilla (`DCEMU_JIT_SONDA_BYTES=1`, permanente): **un `MOV.L @Rm,Rn` emitía 320 bytes contra 62 del modo plano** y los accesos eran ~60 % del arena — presión de icache. Con las rutinas: arena de SR2 **169,6 → 97,4 MB**, y la tanda (`02EE89A0671B8A51`) dio **SR2 −11,1 % y DOOM −7,2 %, ambos con rangos disjuntos y 4/4** — la mayor ganancia desde el índice de enlaces; CT inerte por construcción (plano: cero llamadas). Marcas: SR2 ~1,10×, DOOM ~1,35×. La sonda de accesos fuerza la forma en línea (razones por sitio). De paso quedaron destapados y nombrados dos topes silenciosos: **el arena de 192 MB** (SR2 lo chocaba con la emisión en línea y dejaba de traducir sin contador — ahora `sin arena` en el resumen) y la **tabla de 32 768 bloques** (índice `short`, 13 276 sin lugar fríos con las rutinas — censo pendiente) |
| `DCEMU_JIT_ENLACE_LINEAL=1` | vuelve al barrido lineal de `jit_enlazar()`: cada bloque nuevo recorría **todos** los ya traducidos buscando quién lo esperaba, o sea cuadrático. **Eran los tirones**: en Crazy Taxi, 12,8 s de una corrida de 120 s emulados se iban traduciendo y el 97 % era ese barrido, con el costo por traducción subiendo de 0,05 ms al principio a 0,44 ms al final. Con el índice por PC destino, en tanda reentrenada de tres brazos (`herramientas/enlace-ab.ps1`, binario `5B9CAAE45A977784`): **DOOM −9,3 %, CT −14,5 %, SR2 −16,0 %**, los tres con rangos disjuntos, y los cuadros pasados de 16,7 ms de **14,9 % a 0,65 %** sin ninguno por encima de 33,4. La firma cuadrática está en la propia tabla: el costo por traducción del brazo lineal crece con el banco (0,174 ms a 35 s, 0,365 a 60, 0,490 a 180) mientras el índice se queda en 0,008-0,014. Exacto: mismos bloques, mismos bytes emitidos, mismos enlaces atados, mismos totales al dígito |
| `DCEMU_JIT_SIN_REJILLA=1` | vuelve a desviar al ayudante **toda** escritura sobre una página con código traducido, en vez de preguntarle en línea a una segunda rejilla de 64 bytes. La página dice si hay código en esos 4 KB, no si lo escrito ES código: en Windows CE los datos viven en las mismas páginas, y de las **90 616 485** escrituras de 20 s de DCDoom que caen en una página con código, **ninguna** toca una línea que lo tenga. **DCDoom −1,3 % más** sobre el pliegue de guardas (−2,8 % los dos juntos, tres rangos disjuntos); SR2 y CT dentro de su dispersión. Y lo que lo destapó fue que `JIT_ESCRITURA()` **nunca tuvo un llamador**: la época no se movía por escritura desde que el traductor existe. Ver `docs/recompilador-plan.md` |
| `DCEMU_JIT_SIN_ATAJO_P1P2=1` | apaga el atajo de P1/P2 en la traducción emitida y reproduce la emisión anterior byte por byte. Encendido por omisión: **DOOM −8,1 %, SR2 −2,1 %**, rangos disjuntos, con el camino rápido de 64,1 % a 91,3 % (DOOM) y de 87,9 % a 96,9 % (SR2). Dos cosas lo hacen así y las dos son medidas: va **adelante** de la consulta a la caché (detrás del fallo, DOOM perdía la mitad) y el modo se resuelve **al emitir**, porque SR.MD vive en la clave de validez y un bloque sólo se despacha en el modo en que se tradujo |
| `DCEMU_SONDA_URC` (compilación) | la sonda de conservación de avances de URC (`uc` en el único cuerpo C, `ue` emitido, `uv` la virtual del último), impresa por los puntos de control. Es la que cerró la caza de la compuerta en tres corridas |
| `DCEMU_MMU_DATOS=N` | entradas de la caché de traducciones resueltas (4096 por omisión, tope 8192). Para barrer el tamaño sin recompilar |
| `DCEMU_SONDA_SETJMP_POR_INSTRUCCION=1` | vuelve a armar el salto de excepción una vez por instrucción, como era antes (13,5 % más lento) |
| `DCEMU_SIN_ELISION_INSTANTANEA=1` | vuelve a copiar la instantánea en **todas** las instrucciones, no solo en las que pueden abortar. Es el A/B de la elisión (fase 1 de `rendimiento-plan-2.md`): **vale 2,5 % en DCDoom**, ≈0 sin MMU |
| `DCEMU_SONDA_ELISION_VERIFICAR=1` | toma la instantánea siempre y solo **contrasta** la clasificación de `opcodes.c` contra los abortos reales: cualquier reporte del cable trampa es una fila mal auditada, con la corrección intacta |
| `DCEMU_SONDA_SIN_BANCOS_FPU=1` | la instantánea de excepciones no copia los bancos de coma flotante |
| `DCEMU_SONDA_SIN_INSTANTANEA=1` | la instantánea no copia nada. **Rompe el guest a propósito**: sirve para saber que el mecanismo es portante, no para cronometrar |
| `DCEMU_SIN_MEMO_ARM=1` / `DCEMU_MEMO_ARM=1` | la memoización de barridos de sondeo del ARM7 — **apagada por omisión desde el 2026-08-20 cuando los bloques con cola/encadenado corren**, por veredicto medido: el censo de rechazos mostró que el **44,7 % de los pasos de CT se rechazaba por «grabando»** (el memo graba el barrido de canales que la muestra siguiente invalida — repone solo 7,6 % — y mientras graba los bloques están apagados), y el A/B sobre un solo binario dio **CT −1,7 % con rangos disjuntos** apagándola; SR2 no distingue y DOOM no elide nada. Sola valía +0,5 %: el trade se invirtió cuando los bloques cubrieron los mismos lazos. `DCEMU_MEMO_ARM=1` la fuerza encendida bajo bloques (el brazo de vuelta del A/B); `DCEMU_SIN_MEMO_ARM=1` la apaga también sin bloques. La omisión nueva salió exacta a nivel de salida a través del cambio de estado (bmp, `.wav` y 60 000 puntos idénticos). Ver `docs/arm7-plan.md` y `docs/jit-sota-plan.md`, fase E |
| `DCEMU_SIN_PREDECO_ARM=1` | apaga la predecodificación del ARM7: una entrada por palabra de la RAM de onda con los campos ya extraídos y un manejador por forma, válida mientras la memoria tenga la palabra de la que se decodificó (la regla de `jit_verificar` — aguanta al DMA, al DSP y a la suite, que escriben sin pasar por `arm7_escribir`). Encendida vale **3,3-5,1 % de la corrida** (DOOM −3,6, CT −5,1, SR2 −3,3; rangos disjuntos), la mayor ganancia del ARM7 del árbol; pasos e histograma del ARM idénticos al dígito, capturas canónicas y `.wav` byte a byte. Apagarla apaga también los bloques. Ver `docs/arm7-plan.md`, última sección |
| `DCEMU_SIN_BLOQUES_ARM=1` | apaga los bloques del ARM7: tramos rectos que no tocan PC ni el modo, corridos sobre las entradas predecodificadas con la verificación hecha una vez, un solo chequeo de FIQ y PC avanzando sin preguntar. Cuatro teoremas los hacen exactos (`docs/arm7-plan.md`, «El diseño del escalón 2»): la FIQ solo cambia dentro de un lote si el ARM toca el archivo de registros o CPSR; el bloque entra solo si sus ciclos máximos caben en el saldo; la memoización convive (no se corre bloque grabando, el borde va por el intérprete); el acceso dinámico que cae en el archivo sale por el costado (`arm7_toco_reg`). **61,4 % de los pasos en bloques de 3,3; vale −0,4/−0,6/−1,8 %** (SR2/DOOM/CT, rangos disjuntos) — modesto: su valor grande es que `arm7_blq_correr()` es el punto único donde la emisión x64 reemplaza al lazo en C |
| `DCEMU_SIN_JIT_ARM=1` | no instala el traductor de bloques del ARM7 a x64 (`arm7jit.c`, **solo con `-DDCEMU_JIT=ON`**, como `jit.c` y por lo mismo): plantillas por forma sobre las entradas predecodificadas — ALU con inmediato en sus dos S, desplazamiento inmediato S=0 con los casos de cantidad cero resueltos al emitir, LDR/STR con la rotación desalineada por CL, MRS; el resto llama al manejador de la entrada privada del bloque, idéntico por construcción. Las banderas se arman de las del anfitrión (C invertido en las restas), el PC viaja bakeado (por eso solo entra con `r15 == base`). **Vale −2,4/−2,4/−2,0 % más** (DOOM/CT/SR2, disjuntos) sobre los bloques en C; con los tres escalones las marcas quedaron en **DOOM 1,12×, CT 92,1 s, SR2 0,93×**. Suite propia `tests/test_arm7jit.c` (lazo en C contra emitido, estado y RAM de onda enteros); capturas canónicas y `.wav` de referencia intactos |
| `DCEMU_SIN_RAMA_ARM=1` | apaga la **cola de salto y el encadenado en el lugar** de los bloques del ARM7 (2026-08-20): si lo que cortó el tramo recto es un B/BL entra al bloque como última entrada y corre adentro — ejecuta por `d_salto`, así que el borde de la memoización corre idéntico por construcción —, y después de la cola (salte o caiga) el bloque busca el bloque del destino y **sigue corriéndolo en la misma llamada**, re-verificando presupuesto y palabras por salto (el lazo caliente de CT es un ciclo de DOS bloques: su salida de en medio lo parte). Dos reglas con lección pagada: **los ciclos del cuerpo se comprometen a `arm7.ciclos` antes de la cola** (la reposición de la memoización compara contra ese saldo; sin el compromiso aceptaba reposiciones que el paso a paso rechaza — lo cazó el histograma con perfil, no las capturas, que salieron byte a byte), y la salida lateral manda incluso con las rectas completas. Con el sondeo de abajo: **CT −1,6 % con tres rangos disjuntos escalonados**, DOOM/SR2 neutros; capturas, `.wav` e histogramas idénticos en los tres brazos |
| `DCEMU_SIN_DSP_CORTE=1` | el DSP vuelve a correr los 128 pasos del microprograma en cada muestra (2026-08-21). Encendido por omisión: el lazo corre **hasta el último paso con efecto observable** (twt/iwt/mwt/ewt/frcl/yrl/adrl — y `mrd`, porque `dsp_memval` es un anillo de 4 que cruza muestras), y lo que sigue solo mueve `acc`/`shifted`, que mueren con la muestra. Salió de partir el contador de `--perf` (`de eso canales` / `de eso DSP+EF`): **el mezclador era 11,1 % de CT y el 92 % de eso es el DSP** — los canales son 1,2 % porque hay 2,4 activos de 64. CT programa 78 pasos con corte real en el 85 (el resumen de traza lo imprime): corren 86 de 128. Compuerta verde con **el `.wav` de la reverberación byte a byte** entre brazos + capturas y 110 k puntos; tanda sobre el canónico (`205A022D8BB91680`): **CT −1,7 % con rangos disjuntos**, DOOM/SR2 neutros (no alimentan su DSP — la premisa «no lo programan» resultó falsa el 2026-08-23: cargan el programa por omisión de Katana, 105 pasos corriendo sobre MIXS vacío, ver `DCEMU_SIN_JIT_DSP`). Marca de CT: 2,47× |
| `DCEMU_SIN_MASCARA_CANALES=1` | el mezclador vuelve a llamar a los 64 canales por muestra (2026-08-21). Encendida por omisión: un bit por canal con `activo == 1`, mantenido en los **tres únicos sitios** que escriben `activo` (key-on, silencio de release, fin por LEA) más el reset, recorrido por bit más bajo primero — el orden del lazo de siempre. El censo de `--perf` dio 2,4 activos de 64 en CT: casi todo el 1,2 % de «canales» eran llamadas a canales apagados (quedó en 0,7 %). La suite solo LEE `activo` (diez aserciones que ejercitan la coherencia de la máscara). Compuerta verde entera (`.wav` byte a byte, 110 k puntos); tanda sobre el canónico (`88D6E76EB962464A`): **CT −0,8 % y DOOM −0,6 %, los dos con 4 de 4 rondas a favor y rangos solapados** — dirección clara sin el estándar estricto, como el retorno del ARM7 —; SR2 ilegible (su dispersión de siempre). DOOM y SR2 al dígito en las 16 corridas |
| `DCEMU_SIN_DSP_RAPIDO` | no existe: fue el cuerpo rápido por clase de paso del DSP (63 de 86 pasos «MAC simple»), medido **NEUTRO al milisegundo** en CT y revertido el mismo día — el patrón de ramas por paso es fijo entre muestras y el predictor se aprende la secuencia entera; el costo real es la cadena MAC con sus cargas. El censo quedó en el resumen de traza y la lección en `docs/jit-sota-plan.md` |
| `DCEMU_SIN_JIT_DSP=1` | no instala el **emisor del microprograma del DSP** (`aicadspjit.c`, **solo con `-DDCEMU_JIT=ON`**, el tercer emisor del árbol): el DSP queda en el cuerpo C, que es el A/B. Encendido, el programa se emite a x64 **una vez por reconstrucción** (la regla de `dsp_sucio`) y corre línea recta una vez por muestra: los ~24 campos por paso son inmediatos en vez de cargas — que la lección del cuerpo rápido nombró como el costo real —, ysel/shift/fuente resueltos al emitir, coef/MADRS/RBP horneados, los registros persistentes (acc/shifted/frc/y/dec) en registros del anfitrión, `empacar`/`desempacar` como llamadas y la marca de onda en línea. El estado vive en un bloque (`aicadsp_est`) y `jit_x64` ganó `movsxd`/`imul64` (el producto MAC es de 37 bits). Baranda triple: suite de equivalencia (12 programas de 28 pasos con palabras **crudas al azar**, lazo C contra emitido, estado entero + 2 MB de onda + generaciones de página), compuerta con el `.wav` de la reverberación de CT **byte a byte**, y el resumen que dice «programa emitido»/«cuerpo C» — un A/B con el emisor caído mediría C contra C en silencio. La primera tanda salió **+34 % en CT** y destapó la **tormenta de reconstrucciones**: `aicadsp_tocar()` cubría 0x2800-0x3BFF entero y los acks de timers/INTC del ARM (12 000/s) reconstruían — y reemitían — por toque; el DSP solo relee 0x2804 y 0x3000-0x3BFF, y con la ventana achicada (exacto por idempotencia, sin palanca) fueron **370 850 → 46**, con el brazo C dejando de pagar un reescaneo que arrastraba desde la predecodificación. Tanda sobre el canónico reentrenado (`4E25653D7EE69A7A`): **CT −3,8 % y DOOM −3,5 % con rangos disjuntos y 4/4, SR2 −2,4 % con 4/4 y solape de 101 ms** — los tres ganan porque la premisa «DOOM/SR2 no programan el DSP» era falsa: cargan el programa por omisión de Katana (105 pasos, corte en 111) sobre MIXS vacío, y dcemu masticaba ese silencio a ~1 µs por muestra. La mayor ganancia por mecanismo desde el índice de enlaces |
| `DCEMU_SIN_VERIF_ONDA=1` | vuelve al `memcmp` en cada encadenado de los bloques del ARM7 (2026-08-21): el corredor re-verificaba las palabras del destino en **cada salto** — 117,6 M de memcmp de hasta 52 bytes en 30 s de CT — y el sello por generaciones de página (`onda_gen[]`, el contrato de aica.h del que ya dependía la memoización: todo escritor de onda marca su página) lo baja a **16 045, 99,99 % elidido** — las páginas con código casi no se escriben, así que el sello sobrevive a los lotes: la separación código/datos de la rejilla fina, ahora del lado del ARM. La lección medida en el camino: la v1 por frontera de lote solo elidía 58,9 %, y no eran las escrituras (quitarlas del invalidador no movió el censo) sino **la frontera misma** — 512 ciclos, una por muestra. Límites heredados del contrato, documentados en `arm7.c`: quien escribe `sound_mem` sin marcar (la suite) no invalida — la baranda son las suites y el `.wav`, como con la memo — y el envolvimiento de 2³². Compuerta verde entera (capturas/wav/110 k puntos, histogramas y conteos de bloques al dígito: no elide pasos, solo memcmps) y tanda sobre el canónico clang reentrenado (`F7DBB92730EFF1D8`): **DOOM −1,1 %, CT −2,7 %, SR2 −1,6 %, los tres con rangos disjuntos** — la mayor ganancia del ARM7 desde la predecodificación |
| `DCEMU_SIN_CADENA_ARM=1` | apaga el **encadenado emitido** de los bloques del ARM7 (2026-08-22): la cola B/BL y el salto al sucesor vuelven a resolverse en el lazo C, que era un cruce **cada 3,4 pasos** (108,7 M de encadenados en 30 s de CT) pagando dos llamadas indirectas, presupuesto, búsqueda de ranura y sello de onda por cruce. Encendido, el epílogo del bloque x64 emite la cola entera — los ciclos del cuerpo se comprometen **antes** (la regla de la memoización), condición de las banderas del anfitrión, BL escribe r14, y el B hacia atrás llama a `arm7_memo_borde` con sus tres desenlaces (reposición → salida al C con PC dinámico; grabación armada → contabiliza y sale; normal → r15 constante) — y la costura `aj_cadena` verifica base, longitud, presupuesto con los pasos no comprometidos y el doble sello de onda por página antes de saltar a la **entrada interna** del sucesor (post-prólogo: mismo marco, la pila no crece). `arm7_blq_ult_pasos` es acumulador (`+=`) para que los pasos crucen la cadena, y toda salida al C cae en frontera de instrucción con estado consistente. Compuerta verde entera (`1AAB2099FB59F538`: capturas, 110 k puntos, `.wav` de CT byte a byte); tanda sobre el canónico reentrenado (`03890F37CB1593C9`): **CT −1,8 % con rangos disjuntos y 4/4 pares**, SR2 −0,7 % con 4/4 y solape (dirección clara), DOOM neutro — casi no encadena. Del techo diseñado de 2-4 % se cobró la mitad |
| `DCEMU_SIN_SONDEO_ARM=1` | la lectura del archivo de registros vuelve a **cortar** el bloque del ARM7 (la conducta anterior del teorema 4). Refinamiento medido: dentro de un lote la FIQ pendiente solo cambia por **escrituras** del ARM — `aica_tick()` y el SH-4 corren entre lotes, y `leer_registro()` no toca ni `int_nivel` ni los pendientes —, así que la lectura ya no marca `arm7_toco_reg` y los lazos de sondeo caben enteros en bloques. Vale **−0,6 %** en CT encima de la cola (rango disjunto), neutro en DOOM/SR2 |
| `DCEMU_SIN_FORMAS_ARM=1` | apaga las **formas anchas** del ARM7 (2026-08-20): LDR/STR con desplazamiento por registro, la ALU con desplazamiento por registro (Rs) y MUL/MLA vuelven a `d_generico` — el intérprete anterior entero. Las formas son transcripciones de sus `op_*` (con la asimetría de `op_datos`: rn/rm como PC+12, rs normal) y salieron del **censo de marcas negativas por PC** (`arm7 neg:` bajo perfil, la sonda que se escribió antes que el código): las doce ranuras más golpeadas eran un LDR-R, un MUL, seis `STMFD sp!,{pc}` y tres retornos. Compuerta verde entera con histogramas de ejecución idénticos (binario `14B07E12CA5479D7`); suites 23/23 con tres casos nuevos |
| `DCEMU_SIN_CABE_ARM=1` | las formas anchas se decodifican pero los bloques **no las admiten** — ni a ellas ni al `STM` con PC en la lista (guardar el PC escribe PC+12 y no salta; el rechazo por «PC en la lista» solo es correcto para cargas) — la admisión anterior, y el brazo del medio del A/B. Con la admisión ancha: cobertura 90,4 → **94,0 %** de los pasos, corridas de 18,0 a **23,5**, despachos de bloque −20 %, marca negativa 9,0 → **5,3 %** (el residuo es el retorno `LDM sp!,{pc}`, nombrado como cola generalizada en el plan). Tanda reentrenada (`09ECCD2A14E01F21`): **CT −0,8 % y SR2 −0,6 %, ambos con rangos disjuntos**, DOOM neutro — y el brazo del medio no se separa del viejo: la ganancia es entera de la admisión, no del intérprete |
| `DCEMU_SIN_RETORNO_ARM=1` | apaga la **cola de retorno** de los bloques del ARM7 (2026-08-20): el `LDM` que carga el PC **sin el bit S** entra como terminal (con S escribe CPSR y cambia de modo — ése no encadena), y la terminal puede estar **sola** — las tres ranuras `LDM sp!,{pc}` del censo eran destinos de salto directos, y una cola sola vale por el bloque al que encadena. Tras la cola se chequea `arm7_toco_reg` antes de encadenar (el retorno lee la pila y pudo caer en el archivo). Escalera medida en CT: cobertura 94,0 → **98,0 %**, corridas 23,5 → **59,5**, despachos de bloque 15,0 → **6,2 M**, marca negativa 5,3 → **1,2 %** (el residuo es estructural: seis MSR de CPSR y el camino de la FIQ). Compuerta de cuatro brazos verde con histogramas idénticos (`74362FE86542880B`); tanda en dos intentos (el primero con la máquina en uso, descartado entero): **la pila formas+retorno da CT −1,6 % con rangos disjuntos contra el árbol anterior**, y el retorno solo gana las tres rondas de CT (−324/−437/−1123 ms) con un solape de 9 ms — dirección clara sin el estándar estricto; DOOM neutro, SR2 ilegible |
| `DCEMU_SONDA_ONDA=1` | censo por páginas de 1 KB de la RAM de onda: lecturas de datos del ARM contra escrituras de quien sea. Es lo que contesta si el sondeo del ARM7 se puede saltear — ver `docs/arm7-plan.md` |
| `DCEMU_SIN_RELOJ_EVENTOS=1` | apaga el reloj por eventos: el bloque periódico completo corre en cada frontera de 400 ciclos, **el comportamiento anterior bit a bit** (barrido KOS 139/139 idéntico entre estados). Encendido, el servicio corre solo al llegar el próximo vencimiento (línea, muestra del AICA memoizada, TMU, WDT, demoras del INTC; con DMA auto no se saltea) o cuando alguien invalida con `reloj_tocar()` — la grilla, `reloj_total` y cada entrega no se mueven, por eso es exacto por construcción. Vale **−0,7/−1,0 % en SR2 y −0,5 % en CT, neutro en DOOM** (el guest denso en invalidaciones). Las tres reglas que lo sostienen (invalidar todo lo que mueva una entrega, sincronizar ticks antes de una escritura on-chip, el contador `reloj_toques` que impide que el recálculo pise una invalidación del propio servicio) están en `docs/clock-plan.md`, fase 5 |
| `DCEMU_ARCH` (compilación) | conjunto de instrucciones (`AVX2`, `AVX`, `SSE2`, `OFF`). **Medido: `AVX2` cuesta 2,1 %**, por tamaño del código caliente; viene en `OFF` |
| `DCEMU_SIN_ALINEAR` (compilación) | apaga `DC_ALINEADO`, o sea la alineación a 64 de `core`, de la instantánea y de los bancos de FPU. Es el A/B de la alineación: **vale 2,4 % en Crazy Taxi y 1,9 % en Virtua Tennis, ≈0 en DCDoom**, ver `docs/interprete-plan.md` |
| `DCEMU_LTCG` | construcción con LTCG (encendida por omisión) |

Todas viven en el binario normal a propósito: comparar dos compilaciones mete el layout como
variable, y este árbol ya perdió una sesión por eso. Se leen una vez al arrancar, nunca en el
camino caliente. Ver `docs/rendimiento-plan.md`, fase 6.

**Hay dos excepciones, y cada una lo es por su propio motivo.**

`DCEMU_FORMA` está medida: su gancho es una rama por despacho y cuesta **4,4 %** en Crazy
Taxi (1,50× contra 1,55-1,57×, alternando los dos binarios en una tanda). Se puede compilar
aparte sin romper la regla porque **cuenta bloques del guest en vez de cronometrar al
emulador**: las cuentas salen idénticas en cualquier compilación, y es el tiempo —no el
conteo— lo que la disposición del binario contamina.

`DCEMU_SIN_ALINEAR` no tiene alternativa: **lo que mide es la disposición de los datos**, así
que las dos ramas no pueden convivir en un binario. Su A/B carga con esa contaminación por
construcción, y por eso se corre alternando dentro de una tanda y mirando también la
dispersión, no sólo la media.

Keys: F1 fullscreen, F2 log window, **F5 dump the framebuffer**, **F6 dump the GL buffer**,
F9 step, F10 stop, F11 run, F12 debug view, `p` pause, **`f` toggle the FPS counter**, arrows
+ `a s d w z` = pad, `q`/`e` triggers, `y h g j` analog stick, keypad `+`/`-` scroll the
memory dump. A gamepad works too.

## Measurement discipline

These are the rules that keep costing runs. They are not about the emulator; they are about
how to believe a measurement of it.

- **Capture with `--captura-gl`, never by grabbing the window.** A window grab depends on the
  host compositor and fails silently — `tunnel` once went from 3036 distinct colours to 4
  with no emulator change while `--captura-gl` kept reporting 1837. A whole sweep can come out
  black and read as a massive regression.
- **F5 reads video RAM, F6 and `--captura-gl` read the GL buffer.** 3D never passes through
  video RAM in dcemu, so for a PVR demo F5 is always black. That is not a bug.
- **The GL buffer is the window, 800×600, not the emulated 640×480** — in the default
  `--render=ventana`. A `glReadPixels(0, 0, 640, 480)` returns the bottom-left rectangle and
  silently drops the top and right 20%. Anything drawing in the top band (all of `conio`) reads
  as "draws nothing". **With `--render=fbo` the size is the emulated one times `--escala`**, so
  a capture is not the same image and not even the same dimensions: a sweep is only comparable
  against another sweep with the same render setting.
- **A probe is code, and a wrong probe confirms whatever it was built to test.** The OIT layer
  counter sat behind an `if (solo_fondo != 0)` that had already returned the background, so it
  painted the background and never counted a thing. Read as "the screen scene stacks nothing", it
  sent a whole session down the wrong path and got written into the notes as a fact. With the probe
  fixed, every pixel had two layers. Before trusting a probe on the first question you ask it, make
  it report something you already know the answer to.
- **A metric that moves with the lever is not thereby measuring the lever, and "sharper" is never by
  itself "more correct".** The distinct-colour count moves hard with the half-pixel shift — Crazy Taxi
  −89 % — and it is a real effect, but what it measures is bilinear blending against *the guest's*
  sub-pixel geometry placement, not dcemu's sampling convention. Sharpening it meant cancelling the
  game's own 0.1-texel offset. It read as a witness for a whole session, against a demo built on purpose
  to answer the question. **Prefer the thing whose author was testing the same question you are**, and
  before letting a metric arbitrate, write down what it would read if the answer were the other way.
- **A test's tolerance window is part of what it says.** `pvr-fb_tex` decides the sampling point to
  within half a pixel and is decisive about it; the same arithmetic says it can be off by a whole texel
  on the *texture* side without noticing, which is why it is silent on the half texel. "The demo passes"
  is worth nothing until you know how wrong the emulator would have to be for it to fail.
- **When a capture says blank, check the strip counts at exit before believing it.**
  `--traza-mem` prints how many scenes rendered and the strip count of the last twelve — that
  is what separates "the demo stopped submitting" from "the capture is wrong".
- **A silent `.wav` is a black BMP.** Measure `--captura-audio` the same way: non-zero
  samples, distinct values, RMS and peak. Crazy Taxi's is silent for the whole run **unless the
  bench's key presses are set** — without them the game sits before the title and never makes a
  sound, so the file looks like a valid baseline and guards nothing.
- **`--sin-audio` costs ~50%**: Crazy Taxi runs at 1.14× with it and 1.72× without. It is
  needed to capture the `.wav`, so an A/B run with it measures a regime the bench never sees —
  the ARM7 memoization read as −0.09% (noise) that way and −0.49% (consistent, disjoint ranges)
  without it. Audio capture and the stopwatch cannot share a run, same as `--captura-gl`.
- **`stdout.txt` and `stderr.txt` land next to the executable**, i.e. `build/Release/`, not in
  the working directory — SDL 1.2 builds the path from `GetModuleFileName`. Redirecting the
  process's output from the shell captures zero bytes. Two instances truncate each other's.
- **`--salir-tras=N` matters**: `--desensamblar`, `--volcar` and the `.wav` all close through
  `traza_resumen()`. Killing the process from outside loses them.
- **Guest time runs ~2.5× fast without `--limitar`**, so a guest-side delay elapses sooner in
  wall-clock than the source suggests.
- **Running fast throws sound away, and that is not a sound bug.** The AICA produces samples at
  the pace of *emulated* time and the card consumes 44 100 a second of *real* time; the ring
  discards the excess. Dave Mirra at 1.33× dropped **7.9 s of a 30 s run** (348 690 frames in
  622 bursts); with `--limitar` it runs 0.99× and drops 0.1 s. `traza_resumen()` now prints the
  count whenever anything was lost — **not gated on `--traza-mem`**, because the trace itself
  costs enough to drag the emulator below real time (0.73× against 1.33×) and the symptom
  disappears exactly when the flag that would report it is on. To *listen* to a game, use
  `--limitar`; `--captura-audio` is unaffected either way, since the `.wav` is written from the
  ring by the emulator, not by the card.
- **Before any A/B: kill stray `dcemu.exe` processes and `git reset --hard`.** An orphaned
  process eating a core, and `git checkout -- <file>` restoring from the *index*, between them
  cost three wrong numbers in the timing work.
- **One verification chain per machine.** Two chains running concurrently — an unfinished
  original plus its relaunch — shared `build-jit`, `stderr.txt` (two instances truncate each
  other) and the PGO training, and the resulting tanda reported the previous round's numbers
  **bit for bit**: that impossibility was the tell, since six new templates cannot leave every
  counter unchanged. Check `Get-Process dcemu` before believing a tanda, and never launch a
  second chain while one runs.
- **`--captura-gl` eats 40% of the real time**, so a performance comparison made with it on
  measures the BMP dump. Discard the first run after a `--clean-first`, and alternate binaries
  within one batch rather than trusting one run of each.
- **Discard the first run of any freshly linked binary, not just after a `--clean-first`.** An
  incremental rebuild is enough: Crazy Taxi measured 133 643 ms on the first run of a new
  `dcemu.exe` and 116 805 on the same code minutes later, with the instruction count identical
  to within 100 in 22 billion. That is a 13% error, larger than most things worth measuring.
  A table whose rows come from different batches cannot be read at all.
- **An A/B proves nothing until you have hashed both binaries.** A swap script whose
  `Copy-Item` failed measured the same binary ten times and produced a perfectly plausible
  table — identical work, sane spread, a small difference between "A" and "B". And
  `-DCMAKE_C_FLAGS=/DSOMETHING` configured without complaint and never reached the compiler;
  what gave it away was the two binaries hashing the same. Verify the swap, both in the shell
  and in the build.
- **Alternate the order within the pair too, not just the binaries.** If the first run of each
  pair pays anything for being first, "A always goes first" turns that cost into a difference
  between binaries.
- **Two changes measured through one lever read backwards.** The guard folding and the 64-byte
  grid were shipped together and A/B'd together: DCDoom came out overlapping and Sega Rally 2
  disjoint at −0.8 %. With a third arm isolating each half, DCDoom separates cleanly into
  −1.6 % and −1.3 % — three disjoint ranges, −2.8 % together — and SR2 turns out to be the one
  that cannot tell (63 620–65 317 ms inside one arm; its −0.8 % did not reproduce). A neutral
  combined result can be two effects cancelling, or a real one buried under another guest's
  spread, and nothing in the combined table distinguishes them. One lever per question.
- **The warm-up is per guest, not per batch.** A batch that warms up on DCDoom and then starts
  Crazy Taxi pays the disc image's cold cache on Crazy Taxi's first run: 128 372 ms against
  119-121 k for every other run in that block, which is what left its pair overlapping. Dropping
  that run afterwards would have produced −1.2 %, which is exactly the post-hoc selection the
  rest of this section exists to prevent — so the fix is a warm-up per guest, decided before
  the numbers.
- **A data-layout optimization measured without pinning the layout does not measure what it
  says**, the same way code optimizations did not before PGO. Reordering `context_t` measured
  ≈0 in Release because `core` had no declared alignment and the linker decided the outcome;
  with the alignment the pair is worth 1.9-2.4% on the guests without MMU. Before filing one
  as "no gain", check that something is holding the layout still.
- **Demos that place geometry with `rand()`** (the modifier-volume ones) differ run to run. A
  two-colour BMP proves nothing; run them a few times.
- **A park sweep is unreadable until you have measured its noise floor, and the floor is 40 of 139.**
  Run the same arm twice and compare it against itself *before* comparing it against anything else.
  Measured 2026-08-15: exactly 40 demos differ between two consecutive runs of one binary, and they are
  **the same 40** that differ against a baseline from five days earlier — so the sweep's apparent "40
  regressions" were zero. Three causes, none of them the emulator: most console demos **return to the
  BIOS menu when they finish**, so what the capture holds at `--salir-tras=8` is the boot ROM's screen
  with the host clock in it (which is why a dozen unrelated demos share one hash and all move together
  every day); the threading demos race; the modifier-volume ones use `rand()`. `DCEMU_RTC_FIJO=N` pins
  the first cause but changes every such capture, so it cannot be added to an existing baseline — it has
  to start one. **Medido 2026-08-20: un barrido que arranca fresco con el RTC clavado y la VMU fresca
  por demo (`barrido.ps1 -Vmu`) tiene piso CERO de 131** — a 8 s emulados hasta los hilos y `rand()`
  salen deterministas, así que el piso viejo era entero el reloj y la tarjeta.
  `herramientas/barrido-jit.ps1` es el conductor con esa receta. Ojo: con un `RTC_FIJO` de fecha
  inválida (anterior a 1998) las demos que salen al BIOS caen en la pantalla de poner la hora en vez
  del menú — determinista igual, pero es otra captura.
- **The serial verdict survives what the image cannot.** For the demos that end up on the BIOS menu the
  BMP says nothing at all, while `SUCCEEDED`/`FAIL`/`panic` in `logs/serial.txt` is stable and is the
  real regression signal. `barrido.ps1` saves it per demo; compare those before concluding anything from
  hashes.
- **XInput is read globally, without window focus.** If anyone touches a gamepad during a
  measurement, those presses enter the run. **And an idle pad is enough**: its analog jitter
  alone moved a Crazy Taxi run by ±51 and by ±1 672 instructions with the capture byte-identical
  — bimodal (two discrete totals, not a spread), which is what distinguishes it from a real
  divergence, along with reproducibility on a quiet re-run. DCDoom and Sega Rally 2 do not
  branch on analog values and are the pad-immune arbiters for exactness.
- **The DCEMU_* variables are decimal.** `3e8` reads as 3, and the button lands in the wrong
  frame with no warning.

## Invariants

Rules a wrong edit anywhere in the tree would violate.

- **`memread`/`memwrite` go through the MMU; `memread_fisico`/`memwrite_fisico` do not.** The
  short names are the guest path on purpose, so instruction handlers translate without opting
  in. Emulator-internal accesses that carry already-resolved addresses *and* run inside an
  instruction must use the `_fisico` pair — the Maple and GD-ROM DMA, the PVR callbacks, the
  DMAC. Everything else internal uses `0x8C...` (P1), never translated either way. These only
  misbehave with the MMU on, so nothing else in the tree shows them — grep before assuming
  they are all converted.
- **A translated write of more than one page must be chunked** (`memwrite_paginado()`):
  `memwrite` translates once per call.
- **Every write to guest memory must reach the translator's epoch hook**, and the two places
  that guarantee it are `memwrite_fisico` (all internal and slow-path writes) and the direct
  branch of the `memwrite` macro (which bypasses it). The emitted fast path bypasses both,
  which is exactly why it carries the page-with-code guard. **This invariant was violated for
  the whole life of the recompiler**: `JIT_ESCRITURA()` was defined with no caller, so a guest
  write over translated code never invalidated anything — Sega Rally 2 and Crazy Taxi both do
  it. The tell was in every run's summary (`0 escritura`) and read for months as "no guest
  self-modifies"; what distinguishes that from "nobody is looking" is the control counter next
  to it (`jit_ep_pag_vista`), which is why it now prints unconditionally. Two grids, on
  purpose: pages (L1-resident) decide cheaply whether to ask, 64-byte lines decide for real —
  moving the epoch per page would fire 90 million times per 20 emulated seconds of DCDoom.
- **Instruction handlers own `PC`.** Forgetting `PC += 2` hangs the emulator silently.
- **Adding an instruction = one row in `opcodes[]` + a handler function.** Nothing else
  changes. Rows must not overlap: `initopcodes()` logs colliding encodings to
  `logs/repetidos.txt`. Unimplemented encodings point at `NOIMP` (`dcopcodes.c`).
- **`SR.RB` says which bank *should* be in `registers[0..7]`; `core.context.banco_activo`
  records which one *is*.** Both `UpdateSR()` entries compare against `banco_activo`, never
  against `RB` alone. The field lives inside `core.context` because the MMU snapshot restores
  the register array.
- **Writing SR goes through `sr_normalizar()`** — mask `0x700083F3`, and clear `RB` when `MD`
  is 0. That is the rule for *writing SR*, not for one instruction, so it belongs in
  `UpdateSR()` and not at the call sites.
- **A register read with no case of its own must answer its reset value, not the heap's
  history.** Every block in `inicializar_memoria()` is calloc for this reason. A read-only
  register answered casually has hung the guest four times (`REVISION`, `SB_G1SYSM`,
  `SB_SBREV`, `SB_TFREM` — whose idle value is 8, "TA FIFO empty", so the calloc's 0 reads as
  "full forever"). `--traza-mem` reports each such read once — check it. And with the MMU on,
  first translate the address the guest polls (`DCEMU_TRAZA_TLB`): Sega Rally 2's "own counter
  at 0x00446880" was this register, mapped into user space.
- **`reloj_total` only ever rises and lives outside `core.context`**, because the MMU
  re-execution snapshot restores the context and the clock must not rewind. Periodic consumers
  keep their own mark and compare, rather than accumulating and subtracting.
- **Peripherals never deliver their own interrupt.** `tmu_tick()`/`wdt_tick()` set the flag;
  `intc_revisar_sh4()` derives the request and delivers it when `SR` allows. Calling `intc()`
  at the moment of underflow drops the event silently whenever `SR.BL` is set.
- **The exception snapshot must include the float banks.** `core.context` holds only pointers
  to them, so a plain `memcpy` of the context restores nothing of FR/XF.
- **`options.h` is the feature switchboard.** Nearly all debug output and several behaviours
  are compile-time `#define` toggles there. Check it before adding a `printf`. The write
  watchpoint used to live there too; it is `--watchpoint=` now, because every question cost a
  full rebuild. Only its report cap stayed behind (`WATCHPOINT_MAX`).
- **Logging is compiled out by default.** `logmsg()`/`logxmsg()` expand to nothing unless
  `LOGGING` is defined in `options.h`; `LOG_FFLUSH` makes it survive a crash. Runtime toggles
  exist too (`filelogging`, keys `l`/`m`/`v`/`r`).
- **`tests/dobles.c` replaces `intc.c` in the harness**, so a new global that `intc.c` defines
  has to be defined there too.
- **Los manejadores de `opcodes[]` tienen que conservar direcciones distintas.**
  `jit_plantilla_de()` clasifica cada palabra comparando el puntero de su manejador, y
  `/OPT:ICF` pliega funciones byte-idénticas en una sola dirección — el ICF de lld-link pliega
  `nop` con `NOIMP` y `shal91` con `shll94` (MSVC hoy no pliega ninguno con plantilla), con lo
  que el traductor clasificaba palabras que no son instrucciones como NOP: exacto de casualidad,
  pero las trazas cambian de forma y los conteos dejan de ser invariantes entre compilaciones.
  El enlace clang va con `/OPT:NOICF` y `jit_iniciar()` lleva la guarda que lo nombra si vuelve,
  porque el síntoma — contadores del traductor distintos con captura exacta — no se parece en
  nada a la causa.

**The recurring failure shape in this project**, worth stating because it names most of the
bugs above: *something the guest asks for that dcemu accepts without doing anything and
without saying anything*. A register with backing store in `control_mem` and no reader, a
stub that answers `RTS` + `NOP`, a DMA that reports finishing without moving anything. None
of them produce an error message. When a guest hangs, the question is not "what did dcemu do
wrong" but "what did dcemu answer without meaning it".

## Architecture

### CPU core

`sh4emu.c/h` defines a single global `sh4_cpu core`. Everything reaches CPU state through
macros rather than the struct: `PC`, `R(n)`, `SR_T`, `VBR`, `FR(x)`, `DR(x)`, `FPSCR`.
`initCpuSubSystem()` wires `core.execute` and the float register banks.

Dispatch is a **fully expanded jump table**: `main_loop()` calls
`core.execute(*(WORD *) get_memory_pointer(PC))`, which is `oplist[opcode](opcode)` — a
65536-entry array of function pointers indexed by the raw instruction word. There is no decode
step at runtime.

`opcodes.c` holds the master table `opcodes[]` of `{op, mask, mnemonic, operand-type, handler,
restriction}`. `initopcodes()` expands it into **four** tables (`oplist_pr0_sz0`,
`oplist_pr0_sz1`, `oplist_pr1_sz0`, `oplist_pr1_sz1`), one per combination of the FPSCR
`PR`/`SZ` bits. `UpdateFPSCR()` repoints `oplist` when those bits change and swaps the float
banks when `FR` changes; `UpdateSR()` swaps the banked general registers when `RB` changes.

Handlers live in files by category — `mov.c`, `arith.c`, `logic.c`, `shift.c`, `branch.c`,
`syscontrol.c`, `floatsimple.c`, `floatcontrol.c`, `floatgraph.c` (FIPR/FTRV/matrix ops), and
`dcopcodes.c` for Dreamcast-specific behaviour. Each handler advances `PC` itself, including
the delay-slot logic in `branch.c`.

FPSCR's Cause and Flag fields are written (suite `fpu-excepciones`, which is what KOS's
`basic/fpu/exc` checks), `DN` and `RM` are emulated, and the three FPU exceptions are wired
(0x120 when a cause meets its Enable bit, 0x800/0x820 when `SR.FD` is set).
`docs/sh4-conformidad.md` explains the three rules there that are easy to get backwards.
Still missing: the I cause on its own, and the qNaN value the chip generates (`H'7FBFFFFF`,
not the host's).

→ `docs/notas-cpu-mem.md` for the bank-swap trap, the `0xFFFFFFFF` sentinel and the four
dispatch tables.

### Memory

`mem.c` drives everything off **the top byte of the address** through two parallel 256-entry
tables:

- `mem_zone[0x100]` — raw base pointers; `get_memory_pointer(addr)` gives direct access (used
  for instruction fetch and fast paths).
- `mem_hash_read[0x100]` / `mem_hash_write[0x100]` — per-region handler functions;
  `memread()` / `memwrite()` are macros that call straight through them.

Regions: `0x0C-0x0F/0x8C-0x8F/0xAC-0xAF` system RAM (16 MB, mirrored 4× per window),
`0x04/0x05/0x84/0x85/0xA4/0xA5` video RAM (8 MB) with `0x06/0x07/0x86/0x87/0xA6/0xA7` as their
image areas, `0x11/0x13` the
TA texture FIFO (also video RAM), `0x00/0xA0` PVR/system control registers, `0x10` TA polygon
FIFO (`0x10800000` up is the YUV converter, a different path), `0xE0-0xE3` store queues,
`0x1F/0xFF/0xBF` SH-4 on-chip registers (`0xBF` is area 7 through P2 — Windows CE's HAL starts
the system tick writing TSTR at `0xBFD80004`), `0xF0-0xF7` the P4 cache and TLB arrays
(`mmu.c`), `0x70/0x80` BIOS. Unmapped zones default to `mem_read_error`/`mem_write_error`, and
their `mem_zone[]` entry points at a 16 MB discard block so `get_memory_pointer()` never
dereferences NULL.

**The mirrors and the alternate windows are not cosmetic** — guest code reaches the same
physical memory through several windows and depends on them agreeing. Because
`pvr_read`/`pvr_write` label their `switch` cases in P2 form (`0xa0...`), both switch on
`fisica | 0xa0000000` so every window resolves identically.

**A missing window is silent, and this tree has now paid for it four times**: system RAM
through P1 (`0x8C`), area 7 through P2 (`0xBF`), the video-RAM image areas (`0x06/0x07`,
`0xA6/0xA7`), and video RAM through P1 (`0x84-0x87`) — Sega Rally 2 wrote 16 KB to
`0x85000000` from a single PC and all 4096 writes evaporated. `mem_zone[]` replicates every
window automatically, so `get_memory_pointer()` always resolves; what has to be added by hand
each time is the `mem_hash_read`/`mem_hash_write` entry. When a guest touches memory that
should exist, check the handler table before anything else. And note the failure is *worse*
than an error: `--traza-mem` reports an unmapped access, but a window wired to `ignore_write`
reports nothing at all — `0xA6` was set to `video_write` and then overwritten with
`ignore_write` a few lines later, and the second assignment won for years.

SH-4 on-chip registers (TMU, DMA, SCIF, INTC, ports) are plain pointers into the `regmem`
block, bound once in `regmem_setup()` and declared `extern` in `sh4emu.h`. So `*TCNT0`,
`*DMAOR`, `*IPRA` are both the emulated register and the guest-visible memory.

`pvr_write()` is a large `switch` over individual PVR register addresses using the
`PVR_WRITE_CB_*` macros, which log the access and then invoke a callback in `graficos.c`
(`cb_tastart`, `cb_renderstart`, `cb_param_base`, `cb_fb_r_sof1`, ...). That is how register
writes become rendering work.

Two special cases have to be caught before their block's dispatch: `0xFF800030` (PDTRA, the
video-cable handshake, in `regmap_read()`) and `0x005F74B0` (`SB_G1SYSM`, which falls inside
the range `mem.c` hands to `gdrom.c` wholesale).

→ `docs/notas-cpu-mem.md` for the window layout, the identification-register family and
`SPG_STATUS`.

### Graphics pipeline

**No KOS demo fails on the PVR.** Every texture format, every vertex type, sprites, modifier
volumes, render-to-texture, fog, the background plane and both video-RAM windows are in. The
one documented residue is `tsunami-genmenu`, whose geometry arrives correctly but lands at
y 631..1458 on a 480-line screen — guest-side, since dcemu does not touch vertex coordinates.

The guest submits geometry through the SH-4 store queues: `pref142()` in `syscontrol.c`
flushes SQ0/SQ1, and when the target lands in the polygon FIFO — tested as
`(addr & 0xFF800000) == 0x10000000` — it hands the 32-byte block to `ta_procesar_bloque()` in
`ta.c`, which dispatches on the para-type to `taListEnd()`, `doUserClip()`, `objectListSet()`,
`taPolyModifier()`, `taSprite()` or `taVertexHandler()` in `graficos.c`. The CH2 DMA
(`0x005F6800-08`) and the Sort-DMA (`0x005F6810-20`, what Windows CE's ddraw uses) feed the
same function through `mem.c`.

**`ta.c` exists because not every TA parameter is 32 bytes.** Headers with two face colors,
vertices with floating-point color, the six two-volume textured vertices, both sprite vertices
and the modifier-volume vertex are 64, and arrive as *two* store-queue blocks.
`ta_clasificar()` is the one table — PCW → global parameter type and the vertex type it leaves
in force — used by both `taPolyModifier()` and the block assembler, so they cannot drift.
Polygon Type 1 is 32 bytes; only Types 2 and 4 are 64.

Those build up `VertexBuffer[]` and `TriangleStrip[]` (declared — and *defined* — in
`render.h`, which only `graficos.c` may include for that reason). A write to `TA_LIST_INIT`
triggers `cb_tastart()`, which splits in three: `render_a_textura()` decides where the scene
goes, `dibujar_escena()` sorts the strips and draws them, `terminar_escena()` presents.

Rules of the chip that the code has to respect, each of which was a bug at some point:

- **A polygon header's state stays in effect until the next header** — depth mode, culling, Z
  write, alpha, both blend factors and everything about the texture.
- **The current list is latched by the FIRST global parameter after `TA_LIST_INIT` or after an
  end-of-list**; the list-type field of every later header is ignored (Sega §3.7.4.1).
- **A `TA_LIST_INIT` with nothing registered since the last one does not present.** The
  discriminator is `pvr_listdone`, not `strip_count`.
- **RENDERDONE is raised by the guest's STARTRENDER**, not by `TA_LIST_INIT`.
- **The five render-output registers are latched at `STARTRENDER`** (`regs_render_latchear()`),
  not read when dcemu draws a frame later.
- **A sprite is a complete primitive and never chains**, whatever the end-of-strip bit says.
- **Strips with zero vertices must be skipped at draw** — `glDrawArrays(..., first, 0)` faults
  inside the ICD.
- **The blend factors are two tables, not one**: codes 2 and 3 ("Other Color") mean the
  destination's on the source side and the source's on the destination side.
- **The texture environment's output alpha is a different rule in each of the four modes**
  (DevBox p. 210): `PIXA = TEXA` for 0 and 1, `COLA` for 2, `COLA × TEXA` for 3.
- **TSP bit 20 ("Use Alpha") only forces the vertex alpha to 1.0** — it is not the blend
  switch. Blending is decided by the list.
- **The TA's z is 1/w, larger means nearer**, stored through `profundidad_ta()` as `log2(1+z)`
  — monotonic, so every compare mode holds. `glOrtho` carries near/far inverted because GL
  negates eye z. z = 0 is legal and means infinitely far.
- **`glClear` of the depth buffer is masked by `glDepthMask`.** And by the scissor, which is why the
  user clip is turned off at the end of every scene.
- **The screen width comes from two registers that the guest writes in whatever order it likes.**
  `FB_R_SIZE` gives the width in **32-bit units** and `FB_R_CTRL` the bits per pixel, so the pixel
  width is `units * 4 / bytes-per-pixel`. Two things were wrong and each one alone was enough: the
  formula read `units * (bits == 32 ? 1 : 2)`, which is right for 16 and 32 and **silently wrong for
  24**; and `screeninit()` only re-ran from `FB_R_CTRL` when the write also set "bitmap display
  enable", so a guest that changed the format on its own kept a width computed from the previous
  depth. Quake III is the only one of the fourteen images that asks for a 24-bit framebuffer: its
  screen came out 960 wide instead of 640, so its 640-wide geometry filled the left two thirds of the
  `glOrtho` and **its intro screens were pinned to the left with a black third on the right**. It
  fixes itself on reaching the menu, which is why it looked like a problem with the intros.
- **The user tile clip is implemented** (`doUserClip()` was a stub that only logged). The rectangle
  arrives in 32×32 tiles, inclusive on all four corners, and each header's bits 17-16 say whether it
  applies: 0 no, 2 inside, 3 outside. Mode 2 is `glScissor`, and **the rectangle has to be converted
  to the render target's coordinates** — y flipped and scaled — or it clips the wrong region as soon
  as the target is not the emulated size, which is always except in one case. Mode 3 has no scissor
  that expresses it (GL cannot clip to the complement of a rectangle) and nothing in the park uses
  it: it warns once and draws whole. Dead or Alive 2 is the one that exercises this — 3465 clip
  parameters and 2310 headers in mode 2 per 40-second run — and no other image asks for it at all.
- **The chip samples a pixel at its integer coordinate; OpenGL samples at the centre.** So the same
  geometry interpolates texture coordinates half a pixel apart in the two, and `screeninit()`'s
  `glOrtho` carries the correction (`medio_pixel()`). Three things about it, each one a bug that was
  made and measured: the shift is **half a pixel of the render target expressed in guest units**,
  not half an emulated pixel — with `--render=ventana` the 640 wide screen is stretched over 800, so
  it is 0.4, and a fixed 0.5 puts the first target pixel's centre outside geometry that starts at 0
  and **DCDoom, Virtua Tennis and Dave Mirra lost their left column and top row**. It is a hair less
  than half, because exactly half puts the sample point on the edge of all integer-aligned geometry
  and leaves coverage to the rasterizer's tie-break — which the `glOrtho`'s y flip resolves the wrong
  way, costing a row. And **dcemu's own full-screen quads must take it back out** (`DibujarFramebuffer()`):
  they are a 1:1 copy filtered `GL_LINEAR`, so half a pixel does not shift them, it blends every pixel
  with its neighbour.
- **A strip whose UVs never leave [0,1] is not asking for repetition, so it gets `GL_CLAMP_TO_EDGE`.**
  This is the fix for Crazy Taxi's logo seams, and the mechanism is entirely in what the guest submits:
  that screen is 16×16 quads laid edge to edge (x = 639.9 / 655.9 / 671.9 …), each with a 16×16 texture
  and UV **exactly 0..1**, `tsp=208824c9` — no Clamp, no Flip, so REPEAT, bilinear. At 1:1 that is exact:
  16 texels across 16 pixels, no blending. The window path stretches 640 to 800, each quad becomes 20
  pixels, and at every quad edge **GL wraps and pulls in the opposite texel** — one line every 20 pixels.
  The rule touches no UVs (the previous attempt did, and broke SF3): if the range fits in [0,1] the strip
  never repeats, so REPEAT and CLAMP_TO_EDGE differ *only* in the filter at the very edge. Safe by
  construction — a strip with UV 0..4 keeps REPEAT, an atlas strip with 0.2..0.8 never reaches the edge —
  and the measurement matches that shape: **11 of the 15 games come out byte-identical** (SF3 among
  them), the other four change where they should (DCDoom and 4x4 EVO change **2796 pixels exactly**, the
  perimeter of 800×600), and 13 of 139 demos change with zero verdict changes. `DCEMU_SIN_CLAMP_BORDE=1`.
- **The half-pixel `glOrtho` shift moves texture sampling too, not just coverage — and `pvr-fb_tex`
  measures it directly, with a two-sided window the tree sits inside. Closed.** Write `s` for where
  inside the pixel GL ends up sampling (`s = 0.5` is the pixel centre, i.e. no shift; the tree ships
  `s ≈ 0.016`); `DCEMU_MEDIO_PIXEL_MIL=N` sweeps it, `s = 0.5 − N/1000`.
  **Read fb_tex's source before believing anything about it** (KOS `examples/dreamcast/pvr/fb_tex`, Paul
  Cercueil): it is not a sharpness test and not an odd corner of the strided path, it is a *ruler for the
  sampling point*. The front buffer lives in 32-bit video RAM and textures are read through the 64-bit
  window, which interleaves the banks every 4 bytes — two good pixels then two of garbage. So the demo
  declares the buffer as a 1024×1024 texture with **stride 640** and **`PVR_FILTER_NEAREST`**, and draws
  the screen as two 320-wide halves with `u` from 0 to 640/1024: **two texels per screen pixel**. A mask
  of alternating columns (alpha 1 on the even ones, 0 on the odd) plus two passes with U offsets of 0 and
  −1 texel rebuild the row — pass A wants texel `2p` at even pixel `p`, pass B texel `2p−1` at the odd
  one. So the texture coordinate at pixel `p` is `2(p+s)` and the chosen texel is `floor(2p+2s)`, which
  equals `2p` **if and only if `0 ≤ s < 0.5`**. That is a prediction with two sides, and it holds
  (`herramientas/fbtex-ventana.ps1`, mean difference between the screen's two halves; the failure mode is
  literally two half-width copies):

  | N | 0 | 125 | 250 | 375 | 484 (hoy) | 499 | 500 | 501 | 600 | 750 | 999 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | `s` | 0.500 | 0.375 | 0.250 | 0.125 | 0.016 | 0.001 | 0.000 | −0.001 | −0.100 | −0.250 | −0.499 |
  | halves | **1.92** | 52.75 | 52.75 | 52.75 | 52.75 | 52.70 | 52.70 | 52.70 | **6.37** | **6.37** | **6.37** |

  Half a pixel wide, both edges where the arithmetic puts them, and the shift the tree ships lands in the
  middle of it. **The unshifted GL convention (`s = 0.5`) is outside.** So the shift is right and this
  is settled.
  **The colour count is not a second witness — it never measured the convention.** It counts distinct
  colours, which for 1:1 texel content rises when bilinear blends neighbours; at `s = 0.5` Crazy Taxi's
  logo screen drops from 5721 colours to 612, and across the 15 games at 1:1 CT −89.3 %, CT2 −86.9 %,
  Capcom vs. SNK −83.6 %, ChuChu −11.3 %, Mat Hoffman −10.8 %, Quake III −4.6 %, Dave Mirra −4.4 %, six
  unchanged, and the two magnified screens rise (4x4 EVO +5.3 %, DCDoom +88 %). What that measures is
  **where the guest put its geometry**, not where dcemu samples: the scene dump shows Crazy Taxi's logo
  laid out as 16-pixel tiles on a grid whose origin is at **x.9**, with UV exactly 0..1 on 16×16 textures
  and bilinear filtering (`tsp=208824c9`, filter 2). Sampling at the integer coordinate — the chip — puts
  the sample 0.1 texel into the tile, a 60/40 blend with the neighbour, so the screen is *softened on
  hardware too* and 5721 is the faithful answer. Moving `s` to 0.5 lands it at 0.6, near a texel centre,
  and the image sharpens — by cancelling the guest's own offset, which is not a fix. The lesson is the
  general one: **an oracle that answers "sharper" is not thereby answering "more correct"**, and a metric
  has to be shown to depend on the thing under test before it can arbitrate it.
- **That correction was thought to have a companion on the texture side. It does not — the guest says
  so, and the whole episode is worth keeping.** The idea: `u = 0` names texel 0's **centre** on the chip
  and its **edge** in GL (index `u·W − 0.5`), so the UVs were missing half a texel. It shipped on
  2026-08-14 because it removed the 1-pixel seams across Crazy Taxi's logo and moved DCDoom's whole image
  one pixel (column 0 went from 39 618 of ink against column 1's 78 180, to 78 312 with a smooth
  progression). **It was off again within a day**, because the commercial-game pass found Street Fighter
  III's clean gradient background turning into a grid of seams — measured, `0 → 5012` seam peaks per
  column and `0 → 2633` per row (`herramientas/costuras.ps1`); ChuChu Rocket +20 %. The scene dump names
  the mechanism beyond argument: **SF3's raw UVs are `0.001953 = 0.5/256`, `0.064453 = 16.5/256`,
  `0.126953 = 32.5/256` — the game already addresses texel centres.** If the chip put `u = 0` at texel
  0's centre, asking for `(k+0.5)/W` would land exactly on the boundary between two texels, a 50/50 blend
  on real hardware and the worst possible choice for a UI atlas. So the chip's convention is GL's, and
  adding half a texel is what puts sampling on the boundary. The symptom that had motivated it — Crazy
  Taxi's logo seam — turned out to have the better hypothesis stated at the time, and that one shipped
  instead: UV 0..1 under `GL_REPEAT`, where the filter wraps at the edge. That is an edge addressing-mode
  question, not a question of where `u = 0` lands (see the edge-clamp bullet above), and with it the
  seam is closed. `DCEMU_MEDIO_TEXEL=1` stays as the lever and the previous baseline. Two lessons, both
  paid for: **a fix that explains one symptom is not thereby right** — the seam and the one-pixel
  displacement had one plausible common cause and it was the wrong one; and **the KOS park cannot
  arbitrate a texture-sampling change**, since all 37 demos it moved looked fine and the contradiction
  only appeared in a game. The one piece of evidence that points the other way is weak and is only worth
  knowing so it does not get re-discovered as news: Crazy Taxi's logo grid sits at `x.9`, which under the
  centre convention would sample 0.1 past texel 0's centre (nearly crisp) and under GL's samples 0.1 past
  its edge (a 60/40 blend). Like SF3's, that is the *game author's* belief about the chip, not the chip.
- **`pvr-fb_tex` cannot arbitrate the half texel, and the reason is the same arithmetic that makes it
  arbitrate the half pixel.** Its window is one whole texel wide (two texels per screen pixel), so a
  half-texel offset on the texture side only slides the window by a quarter of a pixel and `s ≈ 0.016`
  stays inside it either way. That is why it comes out **byte-identical with and without the half texel
  at 1:1** (`--render=fbo --escala=1`, hash `99C746FD…` in both arms) — not because the two corrections
  cancel. *At 1:1* is load-bearing: in the default window path the target is 800×600, the geometry shift
  is 0.4 px instead of 0.5, and the demo *does* change, deterministically. The claim was first written
  without the condition and the park sweep is what caught it: the demo turned up among the 37 the
  correction moved, which reads as a contradiction until you notice the sweep runs `--render=ventana`.
  One residue of the demo itself: the copy drops column 0 and row 0, which is its own `-1/1024` U offset
  reading outside the texture at the left edge.
- **Mipmapped textures store their levels from 1×1 up**, so the big level is not at the
  texture address.
- **`glTexParameteri` applies to whatever texture is bound** — set the filters after
  `glBindTexture`, inside `get_texture()`.
- **The two windows of video RAM interleave the banks differently.** `vram.c/h` owns the
  conversion; the block stays in 32-bit numbering and every 64-bit-window access converts.
  Which window the CH2 DMA uses comes from `SB_LMMODE0`/`SB_LMMODE1`, not from the address.
- **The destination address names the path, and the store queue and the CH2 DMA must read it
  the same way**: `0x10000000` polygon FIFO, `0x10800000` YUV converter, `0x11000000` direct
  texture. They are two entrances to one chip and the guest picks whichever suits it. The DMA
  knew only the first and copied the rest as memory — a video went in, the end-of-DMA was
  reported on time and not one macroblock was converted.
- **"End of Transferring YUV" (`SB_ISTNRM` bit 6) is not the end of the CH2 DMA (bit 19).** One
  says the bytes arrived, the other that the texture is written, and a guest may wait on either.
- **A video texture does not declare its own size**: the side is the power of two the chip
  demands and the real width travels in the *stride*. In `get_texture()` one `paso_16` decides
  both how many bytes are gathered from video RAM and how the decoder walks them — the two
  counts that must never disagree, because when they do the one that gathers less wins and the
  one that walks more leaves the buffer.

The texture cache is persistent: 1024 entries across scenes, invalidated by a per-8 KB-page
generation counter in `vram.c` plus a palette generation, looked up through a hash on the
texture address.

`glops.c/h` is an **older, now-bypassed** path: a recorded display list of `GLOP_*` commands
replayed by `glop_process()`. `graficos.c` has its `#include` commented out and calls OpenGL
directly; `glops.c` is still compiled and linked. Do not assume changes there affect
rendering. `DibujarFramebuffer()` handles the 2D case.

**The whole graphics path costs 7.6% of a run**, which is the ceiling for anything left in it;
the 9.5 million draw calls are worth 1.7% of that, which is why a VBO and strip batching were
both discarded by measurement.

#### The off-screen render target (`--render=fbo`)

`glmoderno.c/h` resolves the GL entry points `opengl32.dll` does not export and owns an FBO —
colour texture plus packed depth24/stencil8 — that the scene can be drawn into instead of the
window's back buffer. **The context was already GL 4.6**: SDL 1.2 has no version or profile
attributes, so it cannot ask for a *core* context, and it does not need to — the default context
on Windows and Mesa is compatibility, which reaches 4.6 on any current driver, and the entry
points load through `SDL_GL_GetProcAddress`. Fixed function and modern GL live in the same
context, so the migration is incremental. `offset_iniciar()` was already doing this for
`glSecondaryColorPointer`; this is the same pattern at scale.

What it buys, and what it does not:

- **The rasterization rectangle stops being the window.** Ask anything that reads back what GL
  drew for its size through `render_ancho()`/`render_alto()`, never `outputscreen->w`. This is
  the same trap as before, mirrored: reading 800×600 out of a 640×480 target returns the
  bottom-left rectangle plus garbage.
- **The framebuffer dump stops resampling.** It used to read 800×600 and store 640×480 by
  nearest neighbour; at scale 1 source and destination now match exactly.
- **Internal resolution scaling, and it is free.** Measured: Crazy Taxi 1.28× either way, Sega
  Rally 2 0.51× at ×1, ×2 and ×4 alike. The bottleneck is the SH-4 interpreter, not the GPU, so
  the extra pixels cost nothing — this is the one lever in the graphics path with headroom.
- **It changes nothing about how a pixel is computed**: no shaders, same fixed-function
  pipeline, same draw model. That is why the two paths are comparable at all, and why the
  baseline still holds — `--render=ventana` is the default and stays byte-identical.

#### The programmable path (`--render=shader`)

A vertex/fragment pair that reproduces `GL_COMBINE`, `glAlphaFunc` and `GL_COLOR_SUM`. Written in
**compatibility GLSL 1.20** with the built-ins (`gl_Vertex`, `gl_Color`, `gl_SecondaryColor`,
`gl_MultiTexCoord0`, `gl_ModelViewProjectionMatrix`), which is what lets the existing client
arrays keep feeding it without touching the draw path — generic attributes would mean VBOs and
VAOs. Note `screeninit()` puts the `glOrtho` in the MODELVIEW and leaves PROJECTION identity, so
`gl_ModelViewProjectionMatrix` *is* the ortho.

- **The uniforms hang off the state shadow, not the draw loop.** `gl_textura()`,
  `gl_alpha_test()`, `offset_estado()` and the texture-env `switch` each update their uniform, so
  the shader and fixed function cannot disagree about what state is set. Anything that touches
  `GL_TEXTURE_2D` or `GL_ALPHA_TEST` by hand has to go through those helpers or the uniform goes
  stale.
- **Paths that draw with fixed function on purpose must switch the program off**: the framebuffer
  quads, the debug view, and `marcar_volumenes()` — that last one submits `glBegin/glEnd`
  positions only, so the colour and UVs the shader would see are whatever GL currently holds, and
  a `discard` from a stale uniform would leave the stencil half-marked with nothing to report it.
- **With the program bound, `GL_ALPHA_TEST` must be disabled.** The alpha test is a per-fragment
  operation *after* the shader, so in a compatibility context both rules apply and the stricter
  one wins — which is the approximation. The shader's exact rule would never have taken effect.
- **The eight PVR control demos and all five commercial games come out byte-identical** to
  `--render=fbo`, including `pvr-texture_render`, `pvr-fb_tex`, `pvr-modifier_volume_zclip` and
  Dave Mirra's FMV. The plan expected exact comparison to stop working here; it did not.
- **`--render=oit` orders the translucent list per pixel**, which is what the chip does; today's
  `qsort` orders it per strip and its own comment admits interpenetrating geometry can come out
  wrong. Per-pixel linked lists (SSBO + atomic image), resolved in a full-screen pass that sorts
  each list and applies the TSP's eight blend factors in order. **The TSP blend codes 4-7 name
  their operand absolutely — SRC alpha, DST alpha, on both sides; only 2/3 are "the other one"** —
  the same rule that separates the two `blend_modes` tables. The first resolve passed propio/otro
  symmetrically, so every α<1 strip erased the accumulated background (Sega Rally 2's transmission
  screen, nearly black, exposed it, 2026-08-10). With that fixed, **eleven of the twelve control
  demos are byte-identical to `--render=shader`** and the other two (`2ndmix`, `tsunami-banner`)
  agree to ≤2 LSB — the quantization residue of packing each fragment to 8 bits at stacking time
  while GL blends unquantized and rounds per strip.
- **Modifier volumes are resolved per pixel**, which is what the chip does: the face count goes to an
  image the fragment shader can read, so the polygon picks between its two parameter sets inside the
  shader instead of being drawn twice with the stencil as a gate. **Inside/outside is decided by
  parity per volume model, folded with OR — the chip's rule, blind to winding** (2026-08-25; both
  paths, see `DCEMU_SIN_VOL_PARIDAD` in the table). Parameter set 1 rides in texture
  units 1, 2 and 3. The marking pass still has to run *after* the depth is resolved — that is the
  chip's order, and it is the geometry pass that disappears, not the marking. Three silent traps came
  out of it, all in `docs/notas-graficos.md`: `glFrontFace` has to be pinned even with culling off
  (`gl_FrontFacing` reads it, and `gl_cull()` changes it per strip); a guard that returned on
  `vol_mascara == 0` meant the image the same call was supposed to create never got created; and
  image uniforms did not take through `glProgramUniform*`, which is why they now carry
  `layout(binding = N)` in the source.
- **"Close excluding" is implemented now, and `demos/volumen-excluir/` is the only thing that
  exercises it.** Nothing in the park does: 1.16 M strips of Crazy Taxi in play, the three volume
  demos and the other eight games contain zero instruction-2 triangles, and zero strips selecting the
  TSP secondary accumulation buffer either — which also **removes it as a suspect for the Virtua
  Tennis 2 shadow**. The demo checks itself without a reference image: include and exclude are exact
  complements, and the two captures come out complementary on 307 200 of 307 200 pixels.
- **The TSP secondary accumulation buffer (bits 25/24) is implemented**, as the FBO's second colour
  attachment: the destination is picked with `glDrawBuffer`, and the source by reading that attachment
  as a texture in the fragment shader — the half fixed function cannot do. Both bits apply together or
  neither, because applying only the destination would accumulate a group into a buffer nobody then
  composites, i.e. it would vanish. `demos/acumulador/` is the only content that selects it, and it too
  checks itself: two additive squares over black, summed straight into the primary in one flavour and
  through the secondary in the other, must come out byte-identical — they do, and `--render=fbo`, which
  does not implement the bits, fails it in the predicted way. **With `--render=oit` the bits do not
  apply** (the fragment stacks instead of being redirected) and a strip that asks for them says so once.
- **The stacking pass needs its own program, with `layout(early_fragment_tests)`.** A shader
  containing `discard` forces the depth test *after* it runs, so the OIT epilogue — stack, then
  discard — stacked the fragments depth was about to reject: **translucent geometry hidden behind
  opaque geometry entered the list and the resolve blended it over what was hiding it.** That is
  every scene with walls, not one demo. The qualifier cannot go on the shared program, because with
  the test brought forward the depth is *written* before the shader and a punch-through fragment
  that discards would leave its z behind; so the two programs are the same source differing in that
  one line, kept in step by `glProgramUniform*` (which writes a uniform without binding). Without
  that entry point there is no second program and no ordered transparency — better off than drawing
  what should be hidden.
- **Bump mapping is evaluated per pixel** from the raw angles instead of being baked into the
  texture at upload. Same formula — `pvr-bumpmap` agrees to within 1 level, which is byte rounding
  — but it fixes what baking cannot: K1..K3 and Q come from the *polygon*, while the texture cache
  is keyed by address, so two polygons sharing a bump map with different parameters both got the
  first one's intensity. `gl_bump()` compares the parameters too, not just the on/off.
- **Fog is the one thing that deliberately differs**, and it is the first item of 2.c: the shader
  evaluates it per pixel and *before* the blend, which is what the chip does, instead of a second
  full geometry pass per strip with the alpha interpolated between vertices. `q` rides in
  `gl_TexCoord[0].w` for free. `DCEMU_SIN_NIEBLA=1` isolates it — with fog off the two paths are
  byte-identical again, so nothing else moved. It also removes a wart: the second pass called
  `gl_estado_olvidar()`, so every foggy strip destroyed the next one's state shadow.

Two ways that comparison lied before it told the truth, both worth knowing because they produce
the same symptom — "the shader broke 99.98% of the pixels" with both images perfect, each showing
a different moment:

- **An A/B of render paths runs with `--sin-vmu`.** The first run of a pair writes the card and
  the second starts from a different one, so the guest takes another path.
- **`--captura-gl` overwrites its file every frame, so the file existing does not mean the run
  finished.** Waiting on "the capture is there" compares a half-progressed frame against a
  finished one. Wait for the process, not for the file.

→ `docs/notas-graficos.md` for all of it: the texture formats, the YUV converter, the palette
rules, the cache, RTT, the VRAM windows, the background plane, depth, fog and modifier
volumes.

### Interrupts and timing

`main_loop()` in `main.c` is the whole scheduler: execute one instruction, and every
`RELOJ_GRANO` accumulated cycles (400) run the periodic block — `tmu_tick()`, `wdt_tick()`,
`intc_revisar_sh4()`, `intc_asic_pendiente()`. Scanline compares raise `SCANINT1`/`SCANINT2`,
end of frame raises `ASIC_EVT_PVR_VBLINT`, redraws and pumps SDL events.

`intc.c` queues ASIC events with `intc_add()`; `intc()` performs the SH-4 exception entry.
The ASIC has **two** status registers: the normal one (`SB_ISTNRM`, `ASIC_ACK_A`,
`intc_add()`) and the external one (`SB_ISTEXT`, `ASIC_ACK_B`, `intc_add_ext()`), where the
GD-ROM's end-of-command arrives.

**The ASIC's normal interrupt lines are level-triggered**, derived from `SB_ISTNRM` against its
three masks — delivery consumes nothing, only the guest's acknowledge or masking lowers a
line. **Events with transfer time keep their delay** (`intc_add(evt, cnt)`): the status bit
turns on when the event occurs, not when the guest kicks the operation.

**Everything periodic derives from one constant and one counter**: `DC_CPU_HZ = 199499520` in
`tmu.h`, the same number KOS uses, and `reloj_total`. The peripheral clock is `CPU/4` and a
TMU channel's `TPSC` divides again, so from CPU cycles the divider is 16, 64, 256, 1024 or
4096. `reloj_ciclos_por_linea()` computes the scanline rate from `SPG_LOAD.vcount` and
`SPG_CONTROL` — 6345 cycles for VGA, 6351 NTSC, 6394 PAL.

**Delivery cannot wait for the periodic block.** `intc_sh4_reintentar` is armed by the moments
that can open a window — `tmu.c` on `TCR.UNF`, `wdt.c` on `WTCSR.IOVF`, `dma_canal()` on
`CHCR.TE`, both `UpdateSR()` entries — and the periodic block's own condition tests it, so the
block runs early whenever a request becomes deliverable. It must be tested **inside** that
condition, not in an `if` of its own: with its own branch Crazy Taxi pays 8.5%, folded in it
pays nothing measurable.

`wdt.c/h` is the SH-4 watchdog timer: two key-protected registers and an 8-bit up-counter that
either resets the machine (watchdog mode) or raises `EXC_WDT_ITI` (interval mode). `tmu.c/h` is
the three TMU channels — `TCNT0/1/2`, raising `EXC_TMU*_TUNI*` on underflow. Both take a cycle
count from `main_loop()` and keep their own remainder, each dividing by its own prescaler. `dma_check()` in `main.c` runs the SH-4
DMAC for real, but only on channels in auto-request mode: peripheral-requested transfers on the
Dreamcast are done by the ASIC. `DMAOR` starts at `0x8201` because that is what the boot ROM
leaves on a retail console.

→ `docs/notas-tiempo.md` and `docs/clock-plan.md`.

### Sound: the AICA, the ARM7DI and the G2 DMA

**Sound works: five KOS demos play** (`sound-cdda-basic_cdda` needs a disc via `--disco=`, and
mind that the `.cdi` audio tracks in `roms/` are silent filler — the `.gdi` `track02.raw`
jingles are real; `sound-hello-opus` and `libdream-spu` are broken guest-side as shipped, see
`docs/demos-kos.md`). `aica.c/h` is the chip's register block, its three
timers, its interrupt controller, its internal DMA and the 64-channel synthesizer; `arm7.c/h`
is the ARM7DI it carries inside; `g2dma.c/h` is the Holly's four G2-DMA channels
(`0x005F7800-7F`); `audio.c/h` is the only piece that touches SDL.

The reference is Sega's *Dreamcast/Dev.Box System Architecture*: §4.2.2 and §8.4.5 for the
map, §8.1.1 for the algorithms, §8.4.1.4 for the G2-DMA. `docs/aica-plan.md` is the plan.

**The ARM runs in all 135 demos, not in the seven sound ones** — `spu_init()` releases its
reset in every program — so the demo sweep is regression for this subsystem, and `--sin-aica`
exists to turn the chip off when isolating one.

Rules: the SH-4 reaches the AICA at `0x00700000` and the ARM at `0x00800000`, and two
registers exist in only one window each (`ARMRST` is the SH-4's, `L`/`M` the ARM's); the file
is byte-addressable and the 4-byte restriction applies at the G2 entry. Timing derives from
`reloj_total` — exactly 735 samples every 3324992 CPU cycles, and 512 ARM cycles per sample. A
pending interrupt source with no mask stays pending. The ADPCM is done in integers and is
deterministic.

**CD audio plays** (`cdda.c/h`). It is a piece of the *drive*, not of the AICA: the GD-ROM
decodes an audio track itself and hands the chip finished samples through a separate input.
The guest never sees them — it says "play from here to here, N times" and then asks how it is
going. Both command paths reach `cdda.c` and must answer alike: the SPI packets `CD_PLAY`
(0x20), `CD_SEEK` (0x21) and `CD_SCAN` (0x22) in `gdrom.c`, and the boot ROM driver's 20
PLAY_TRACKS, 21 PLAY_SECTORS, 22 PAUSE, 23 RELEASE, 27 SEEK, 33 STOP in `dcopcodes.c`.

Three things make it small: the CD's format **is** the mixer's output format (44 100 Hz, 16-bit
signed stereo), so it is one CD frame per `mezclar_una_muestra()` and there is no resampling;
`iso_leer_audio()` reads the raw 2352-byte sectors straight from the track, which in a `.gdi` is
its own file; and the sum happens **after MVOL**, because CDDA is not one of the 64 voices — a
game that mutes its effects should keep its music.

**`GET_SCD` and `REQ_STAT` are half of it.** A game follows its own music by polling the head
position and switching tracks when it passes a FAD. Answering "track 1, data, parked at 150"
forever is the tree's usual failure shape: a valid answer that means nothing.

`DCEMU_SIN_CDDA=1` keeps the drive answering "playing, at such a FAD" and delivers no samples.
Silencing the whole mechanism would not isolate anything — a guest that polls its music would
take a different path, and the two runs would no longer be comparable.

**The effects DSP is emulated** (`aicadsp.c/h`): the 128-step microprogram, read straight out of
`aica_reg[]` so DMA uploads and guest readback work by construction, with a zero-cost early-out
when no program is loaded — which is the whole KOS park. **Three of the commercial games program
it for real** (Crazy Taxi 78 steps, Tennis 2K2 and Virtua Tennis 2 110), which is reverb dcemu
used to drop. CDDA now has two paths: through EXTS and the EFSDL levels of slots 16/17 when the
guest programs them (the chip's rule, through MVOL), and the old fixed-level path when it never
does — because with syscall hooks nobody ran the boot ROM's sound init. The `dsp` test suite
hand-assembles microprograms; the audio guardrail is the `.wav`, byte-identical on the KOS demo
with signal and bit-reproducible on Crazy Taxi with the reverb on.

**The FEG filter is emulated** — the per-voice resonant lowpass of §8.1.1.7. The seven-game census
found one client (DOA2); extending it to all fourteen found **seven** — DOA2, Crazy Taxi 2, Sega
Rally 2, ChuChu Rocket, DCDoom (599 of 906 key-ons), Mat Hoffman (90/90) and Quake III (20/20). The
envelope comes from the papers — the DevBox's FEG table is the AEG decay table ×4 entry by entry, so
it is derived, not copied — and the IIR arithmetic from the published reverse engineering (Corlett's
Highly Theoretical, via flycast), since Sega's own docs left the equation in lost figures. Three
deliberate skips in `feg_decidir()`, each documented: LPOFF (undocumented bit 5 of `+0x28`, what KOS
sets, what protects the demo park), all-five-FLV-zero (a register file nobody wrote), and the
pass-through — including Katana's `0x1FF7`, one LSB under the documented `0x1FF8`, so every Katana
game skips the filter its driver parks open. Guardrails: cpp-modplug and Crazy Taxi `.wav`
byte-identical, DOA2 changes by RMS +0.3% and is bit-reproducible across binaries.

**The LFO is emulated too, and how its client appeared is the lesson**: the seven-game census said
"nobody uses it" and that was true of seven, not of fourteen — extending it found ChuChu Rocket
asking for pitch LFO on 15 key-ons, the same trap as the FEG one level up. The phase counter's
reload formula (from Highly Theoretical) reproduces Sega's Hz table exactly, and where flycast and
the paper disagree the paper wins twice: ALFO depth is `>> (7-ALFOS)` in the chip's 0.09375 dB units
(all seven depths land on table 8-9's dB values; flycast is twice as deep), and PLFO modulates the
phase increment **linearly** — which is exactly what produces the table's asymmetric ±cent pairs
(−231/+202, −112/+103, −55/+52), where flycast interpolates in cents and gets symmetric values. The
sample-interval interrupt (INTON, bit 10) is emulated as well, pended only when SCIEB/MCIEB enables
it. `CD_SCAN` speed remains unemulated with a sentinel in the trace. The census counters stay as
per-run usage reporting; the probe has its own test (`el_censo_del_lfo_cuenta`). The ARM7 is the
biggest cost after the SH-4 interpreter — 8.9-18.9% of a run under the JIT even after the
predecode cache (`DCEMU_SIN_PREDECO_ARM` in the table above), which took a quarter to a
third off it; the ARM7→x64 translator is the open second step (`docs/arm7-plan.md`).

→ `docs/notas-aica.md` and `docs/arm7-plan.md`.

### GD-ROM and disc images

`gdrom.c/h` is the drive as the hardware sees it: the ATA register block, the status machine
(BSY/DRQ/DRDY/CHECK, interrupt reason, drive state and disc type) and the SPI packet commands
the boot ROM uses. Sectors come from `iso.c`. `mem.c` routes the two address ranges to it;
nothing else touches it except `dcopcodes.c`, which reuses `gdrom_construir_toc()` so both
paths report the same disc. Data goes out either as chained DRQ blocks or through the G2 DMA
(`SB_GDSTAR`/`SB_GDST`), depending on bit 0 of FEATURES at the `PACKET` command.

`iso.c` picks a backend by extension: `.iso` is a flat ISO9660 read by `iso9660_min.c`, `.cdi`
(DiscJuggler) goes through `cdi.c`, `.gdi` (a text index plus one raw file per track) through
`gdi.c`, `.chd` (chdman, the format of current collections) through `chd.c` over libchdr —
vendored in `deps/libchdr` because its published binaries are MinGW — and anything else needs
`USE_LIBCDIO`, which this build does not have. **`.cdi`, `.gdi` and `.chd` share everything
above the open** — the same track table, TOC and sessions, selected by `ES_MULTIPISTA()`. The
first two differ only in which file holds the data track (`min_iso_open_pista()`); a `.chd` has
no file to seek — sectors live in compressed hunks — so it opens the third way,
`min_iso_open_lector()`, where a callback in `chd.c` serves each 2048-byte sector and owns all
track geometry. The CHD metadata arithmetic (cumulative FADs with PAD inside FRAMES, 4-frame
file padding, `CHGD` audio byteswapped) is validated against the tree's `.gdi`s — same game,
both containers, byte-identical captures — and written up in `docs/notas-gdrom.md`. Whether the
image is a GD-ROM comes from its metadata tag, not the extension: a MIL-CD `.chd` keeps the
scrambled executable and the CD rules. A `.gdi` does not record whether a 2352-byte data track is mode 1 or
mode 2, and that decides where the 2048 user bytes start (16 or 24), so it is read from the
sector's own header rather than assumed. `iso_init()` lists every track with its LBA, size, mode and file offset — that
listing is the first thing to look at.

**Audio tracks are read by a different door.** `iso_leer_audio()` hands out raw 2352-byte
sectors — no volume, no header, no 2048-byte user area — and opens the track's file itself,
because `iso_init()` only registers the data tracks with `min_iso_*`. It is what `cdda.c`
pulls from.

Rules that cost a boot each:

- **FAD = LBA + 150.** The drive's `CD_READ` speaks FAD; `min_iso_*` speaks LBA. Inside a
  GD-ROM's high-density area the ISO9660's own LBAs are absolute disc addresses.
- **The TOC's byte order on the wire is not the struct's** — control byte first, FAD
  big-endian behind it. `cmd_get_toc()` swaps; the syscall hook, which skips the ROM's driver,
  does not.
- **A `.cdi` is a CD.** `iso_es_gdrom()` is only true under `DCEMU_COMO_GD`, and the TOC is not
  split into density areas unless the disc really is a GD-ROM.
- **A DMA read can take many bursts**; the *command* only ends when the data runs out.
- **The ERROR register is not just the sense key** — bit 2 is `ABRT` (`GD_ERR_*` in
  `gdrom.h`).
- `SB_GDSTARD`/`SB_GDLEND` (`0x005F74F4`/`0x005F74F8`) are the DMA's counters and the ROM's
  driver reads them.

Every commercial image in `roms/` runs, on both paths — **fourteen of them as of 2026-08-06**: Crazy
Taxi, Crazy Taxi 2, Virtua Tennis, Virtua Tennis 2, Capcom vs. SNK, Street Fighter III, Sega Rally 2,
Dave Mirra, ChuChu Rocket, DCDoom, and the five whose `.gdi` zips were sitting unextracted — **4X4
EVO, Dead or Alive 2, Mat Hoffman's Pro BMX, Quake III Arena and Tennis 2K2**. Four of those five
reach gameplay on the first try with no changes to the emulator; Quake III boots and renders its
"SELECT DEVICE" screen and does not advance past it under the blind button bench, which is an input
question and not a hang — the exit dump shows the guest executing normally. Between them they report
**one unemulated address each at most**, and the recurring one is the G2 expansion probe at
`0xA1000400`-`0xA1001800`, which is benign.

**Extracting the zips is the cheapest compatibility work in the tree and it was already paid for.**
The plan's Vía 3 called test material the biggest gap against the state of the art, and it is
especially the gap for the graphics work: the KOS park exercises neither mipmaps, nor the TSP repeat
modes, nor blend codes 2 and 3, nor the Offset Color. Five more commercial games is five more places
those paths get walked. `roms/` now holds them extracted — mind the disk, it went from 13.8 GB free
to 8.3.

**The `.chd` backend added three more (2026-08-07)**, from the eight redump-named CHDs in
`E:\Juegos\roms\dreamcast`: **18 Wheeler reaches gameplay** (its first ~45 emulated seconds draw
one strip per scene and capture black — that is its boot sequence waiting for START, not a hang),
**Tony Hawk's Pro Skater 2** shows its intro (and is the one image exercising data spread over
two high-density tracks, the Dave Mirra shape), and **Capcom vs. SNK 2** reaches its memory-card
screen. The other five CHDs are second containers for games already running — which is what made
them the guardrail: Crazy Taxi 2 and Virtua Tennis are **byte-identical** `.chd` against `.gdi`
at 20 emulated seconds.

`docs/notas-gdrom.md` has the layout table, the `.cdi` format, the `.chd` metadata rules, the five
drive bugs and the damaged Virtua Tennis rip — **damaged rip, not damaged region**: the same USA
version off a three-track `.gdi` plays fine, so the rule is "try another rip", not "avoid the USA
release".

### BIOS syscall emulation

With `BIOS_HACKS` in `options.h` **and** `opciones.hacks_bios` at runtime, `main()` patches the
syscall vector slots (`0x8C0000B0`-`0x8C0000E0`) to point at stub code at `HACK_BASE`. Most
stubs are an illegal opcode in the delay slot of an `RTS`, mapping to `BIOS_HACK` in
`dcopcodes.c`. The GD-ROM stub is the exception — its illegal opcode sits at offset 0 with no
RTS, and `hack_gdrom()` sets the return PC itself, because a MAINLOOP with a latched PIO piece
"calls" the guest's PIO callback instead of returning.

The hooks: `hack_gdrom()` (sector reads, TOC, the PIO and DMA stream protocols, one live
request at a time), `hack_romfont()` (**function number in `R1`, not `R7`**; the lock must
answer 0), `hack_flashrom()` (write only clears bits, `&=`), `hack_sysinfo()` (function 3
returns a *pointer* to the 8-byte ID) and `hack_mudo()` for the unnamed vector, which does
nothing but says so. The same GD stub is also installed at `8C0010F0`, the ROM's fixed service
entry, with `8C0000C0` pointing there — Windows CE's maple.dll calls that address as a
build-time constant.

**`main()` also writes what the boot ROM leaves in low RAM**: the flash's five-digit machine
code at `0x8C000070` (`REGION_BASE`) and the console ID at `0x8C000068` (`SYSID_BASE`), both
copied from whatever flash is in use. Games read them — Crazy Taxi hangs forever without the
region word.

`--bios` turns the hooks off: the stubs are written at `0x8C000100`-`0x8C000500`, which is
exactly where the boot ROM installs itself.

→ `docs/notas-arranque.md` for every hook's semantics, the ROM's decision addresses, the
Windows CE work and the Maple bus.

### MMU, synchronous exceptions and UBC

`mmu.c` owns the TLB and translation (`docs/mmu-plan.md`). **Instruction fetch translates too**
(phase 7): `main_loop()`, the delay slots and the RTE fetch through `MMU_FETCH_PUNTERO()`, a
page cache that costs one compare when the MMU is off. The RTE's slot is fetched *before* `SR`
is written — the manual's rule, and with the MMU on a kernel's return-to-user has its slot in a
privileged page. `MMUCR.URC` advances on every UTLB access, which is what lets a software
TLB-miss handler's `LDTLB` pick a fresh entry each time.

**With the MMU on, the SQ flush target comes from the UTLB, not from QACR**
(`mmu_traducir_sq()`, SH-4 manual §4.6). Masking first — the QACR formula, which is the MMU-off
rule — is what killed Windows CE's ddraw.

**Three translation caches sit in front of the 64-entry UTLB scan, and they are worth 1.8× on
the only guest that uses the MMU.** `utlb_buscar()` caches the *entry index* — never the
translation — so protection, the D bit and first-write are still evaluated on the real entry;
`mmu_datos[]` caches a fully resolved translation together with **which access types already
passed every check** (read and write are separate permission bits on one entry, not separate
tags — that mistake cost half the hit rate); and a 64-entry second level backs the single-page
instruction-fetch cache. Two rules hold them together and breaking either is silent:

- **A cache hit is still a UTLB access, so it must advance `MMUCR.URC`.** That counter decides
  which entry the guest's `LDTLB` replaces, i.e. its execution path.
- **`LDTLB` bumps one entry's generation (`mmu_utlb_gen[]`), it does not flush.** It replaces
  1 of 64 entries; flushing everything ran 1 091 201 times in 35 emulated seconds. The full
  flush (`mmu_tlb_invalidar()`) is only for the P4 array writes and MMUCR. `mmu_fetch_invalidar()`
  is the cheap half — only the single fetch page, whose tag has no ASID — and it is all a PTEH
  write needs.

**The exception snapshot's `setjmp` lives above the instruction loop**, not around each
instruction; the longjmp lands there, calls `falta_reponer()` and re-enters. And the two FP
banks enter the snapshot only when `es_instruccion_fpu()` says the instruction can write them —
decided inside `run()`, which is what covers delay slots too.

`excepciones.c/h` owns the general-exception path, shared by the MMU and the FPU:
`excepcion_entrar()` (save SSR/SPC/SGR, set EXPEVT, jump to `VBR + vector`) and the instruction
abort. SH-4 general exceptions are re-execution type, but dcemu's handlers mutate registers
around the access, so `main_loop()` snapshots state before each instruction and arms a
`setjmp`; whoever detects the fault calls `excepcion_abortar()`, which does not return.
`excepcion_vigilar` decides whether the loop snapshots at all — 1 if the MMU translates, `SR.FD`
is set, or any FPSCR Enable bit is on. Zero in everything that runs today.

**Misaligned data accesses raise the address error** (0x0E0 read / 0x100 write, TEA and
PTEH.VPN like a TLB fault), checked at the head of `memread`/`memwrite` before translation.
Three deliberate rules in `excepcion_direccion()`: with the snapshot armed it aborts cleanly;
on the fast path there is no snapshot and the exception enters **with the state as it is**
(real software panics there, it does not retry); and outside `main_loop()` the check is inert
(`excepcion_salto_valido`) — that valve is what keeps the SingleStepTests harness intact, since
its random material is full of misaligned accesses that Reicast, the source of its expected
results, never faults. The *instruction* address error (odd PC) is not raised — the documented
residue.

`ubc.c/h` is the SH-4's user break controller, driven the way KOS's driver drives it: two
channels with address masks and optional ASID, channel B optionally comparing data,
`BRCR.SEQ` chaining them. `CMFA`/`CMFB` are set on match and only the guest clears them. The
exception is EXPEVT `0x1E0`. Instruction breaks are evaluated at `main_loop()`'s boundary;
operand breaks hook the guest-path `memread`/`memwrite` macros and compare the **virtual**
address.

**The ROM font lives at `0x00100020`, a P0 address, so the MMU translates it.** That is what the
real boot ROM answers, so it is correct — but a guest that maps the low pages shadows its own
font, as `basic/mmu/pvrmap` does. On hardware it would too.

→ `docs/notas-cpu-mem.md` and `docs/mmu-plan.md`.

### Input and Maple

`mando.c` reads a host gamepad through **XInput**, loaded at runtime with `LoadLibrary` so the
build gains no dependency. It maps almost 1:1 onto a Dreamcast pad; the two triggers are
analogue 0-255 on both consoles and pass through untouched. `main.c` polls it once per frame
and `entrada_leer()` merges it with the keyboard: buttons with AND (active-low), axes by
whichever is not at rest, gamepad first. The `SDL_JOY*` handling in `main.c` is the 2005 path
behind an `#ifdef JOYSTICK` nobody defines.

The Maple bus lives inside `pvr_write()`, in the `SB_MDST` case (`0x005F6C18`): writing 1 walks
the command list at `SB_MDSTAR` and answers each transfer in place. Port A carries a standard
HKT-7700 controller with a VMU in slot 1 (`vmu.c`, below); the other ports get `0xFFFFFFFF`. The
controller answers `Device Request` (1) and `GetCondition` (9). **The hardware trigger exists
too**: with `SB_MDTSEL=1` and `SB_MDEN=1`, `maple_vblank()` synthesizes the `SB_MDST` write at
each vblank, which is how Windows CE polls the pad.

Two rules of the list that each cost a game:

- **The pattern field (bits 8-10) decides the shape of the instruction, and only START (0)
  carries a receive address and a packet.** Occupy-SDCKB (2), RESET (3), release-SDCKB (4) and
  NOP (7) are the descriptor alone, one word. Reading a receive address they do not have
  desynchronizes the whole walk: the *next* descriptor is read as an address, fails the
  "not RAM" guard and truncates the list. Katana's library puts a NOP in its enumeration list,
  which is why Crazy Taxi 2 never got past the memory-card screen.
- **The source byte of a reply is not just the device address: its low 5 bits are the bitmap of
  connected subdevices** (`0x21` = controller with something in slot 1). It is the *only* way a
  guest discovers the VMU — nobody sends a Device Request to a slot without seeing that bit
  first.

### The VMU (memory card)

`vmu.c/h` is the Visual Memory in slot 1 of port A: 128 KB of flash in 256 blocks of 512 bytes,
with the filesystem the boot ROM and games expect (root block 255, FAT 254, directory 253 down
to 241, 200 user blocks). Implemented commands: `DEVINFO` (1), `GETCOND` (9), `GETMINFO` (10),
`BREAD` (11), `BWRITE` (12), `BSYNC` (13), `SETCOND` (14). The formats come from KOS's driver
(`maple/vmu.c`, `dc/vmufs.h`), which is the code that parses what dcemu answers.

- **A read is one phase of 512 bytes; a write is four phases of 128 plus a `BSYNC`**, because
  that is how the real flash is programmed. The `blkid` word is
  `((block & 0xFF) << 24) | ((block >> 8) << 16) | (phase << 8) | partition`, and `BREAD` must
  echo it — the driver compares.
- **`GETMINFO` and the root block must say the same thing.** A guest reads whichever it prefers;
  if they disagree the failure is silent.
- **The LCD and clock functions are declared and accepted, not emulated.** Answering an error
  makes the driver retry four times.
- **The image persists like the flash** (`bios/vmu-a1.bin`, `--vmu=` to move it, `--sin-vmu` to
  take the card off the bus), written at `BSYNC` and at exit. A missing file formats an empty
  card; the format timestamp is fixed, not the host clock, so a run stays deterministic.

### Support modules

`opciones.c` parses the command line into the global `opciones`. `sistema.c` holds the three
pieces of system state the boot ROM asks for before the drive: the PDTRA/PCTRA cable handshake,
the flash ROM (loaded from `bios/flash.bin` or synthesized) and the RTC. **The flash and the
RTC are writable and persist** — without that the BIOS asks for the date on every boot. The
flash is a chip with a command set, not memory, and programming can only clear bits.

`traza.c` implements `--traza-mem` and the watchpoints. `gui.cpp` is the only C++ file — a
guichan overlay log window, exported to C via `extern "C"` in `gui.h`. `debug.c` is the
in-emulator disassembler and register/memory view (F12), driven by `DebugMode`. `BFont.c` is a
bitmap font renderer, driven by `DebugMode` (`DBG_STOP`/`DBG_RUN`/`DBG_STEP`). `log.c` writes
`logs/{disasm,memoria,serial,pvr,intc,glop}.txt`.

## Conventions and gotchas

- `decode.h` and `dcemu_private.h` are not referenced by any source file.
- The old `PC_func` / `str_PC` indirection in `sh4emu.c` is dead code behind `PC_FUNCTIONS`;
  the live path uses `get_memory_pointer(PC)`.
- `pvr_registered` is `DWORD` in `graficos.c` but `extern int` in `intc.c`.
- `MOV.W @(disp,PC)` and `MOV.L @(disp,PC)` do not resolve their literal the same way, and they
  are separate operand types in `opcodes[]` (`OP_T_AT_DISP_PC_RN_W`) so the two rows cannot
  drift. `MOVA` still prints the raw `disp`.
- `flashrom_get_region()` recognizes only `00000`, `00110` and `00211`; the `00111` in
  `bios/flash.bin` here makes KOS log `unknown code`. That is KOS being strict.
- Do not request `SDL_GL_DEPTH_SIZE` — asking for it alongside the stencil makes SDL pick a
  different pixel format, and the context grants 24 bits anyway.
- A crash reports the guest's state instead of vanishing (`traza_caida_instalar()`, installed
  first thing in `main()`), **and on Windows the host's stack with function, file and line**
  (dbghelp; Release already carries `/Zi`, so the PDB sits next to the binary). Read it before
  reaching for the isolation switches — bisecting with `DCEMU_SIN_*` costs a run per guess and
  says nothing when none of them moves the crash.

## Where the deep notes live

| archivo | qué cubre |
| --- | --- |
| `docs/notas-herramientas.md` | `--traza-mem`, watchpoints, `--traza-desde`, capturas, las variables `DCEMU_TRAZA_*`, y las lecciones de medición completas |
| `docs/notas-graficos.md` | pipeline PVR/TA, sprites, entorno de textura, formatos, YUV, paletas, caché, RTT, ventanas de VRAM, plano de fondo, profundidad, niebla, volúmenes modificadores |
| `docs/notas-cpu-mem.md` | núcleo SH-4 y bancos, mapa de memoria, registros de identificación, excepciones síncronas, UBC |
| `docs/notas-tiempo.md` | interrupciones ASIC por nivel, `intc_sh4_reintentar`, relojes, TMU/WDT, DMAC |
| `docs/notas-aica.md` | AICA, ARM7DI, G2-DMA, envolventes, KYONB, perfil del ARM |
| `docs/notas-gdrom.md` | lectora, `.cdi`, TOC, qué imágenes arrancan, el rip dañado de Virtua Tennis |
| `docs/notas-arranque.md` | boot ROM, dónde decide, hooks de syscall, bloque de región/SYSID, Windows CE, Maple, la VMU, flash y RTC |

Los `*-plan.md` son bitácoras de trabajo, no referencia: `bios-boot-plan.md`,
`pendientes-plan.md` (los apartados A.x que citan las notas), `mmu-plan.md`, `aica-plan.md`,
`arm7-plan.md`, `clock-plan.md`, `rendimiento-plan.md`, `rendimiento-plan-2.md`,
`recompilador-plan.md` (el JIT — la excepción del grupo: quedó reescrito **en forma de
conclusiones** — estado, reglas, veredictos medidos y pendientes — con la bitácora entera
en git), `estado-del-arte-plan.md` (el plan maestro de rendimiento: fases 0-6 hechas;
su segunda vuelta es `jit-sota-plan.md` — la FPU emitida, la elisión de ociosos, los
residuos con techo, el ARM7/mezclador restante, clang/LLVM como palanca de compilación
(nunca como backend del JIT) y la adopción por omisión, con el trío CHD de
`E:\Juegos\roms\dreamcast` como material de exactitud nuevo),
`hilos-plan.md`, `interprete-plan.md`, `msvc-build-plan.md`. `demos-kos.md` es el estado de las 135 demos y `sh4-conformidad.md` la
conformidad del núcleo contra el manual. Los PDF de `docs/` son la documentación de Sega y el
manual del SH-4.

## Repository history

This repo was converted from its original CVS/RCS archive to git. All 886 revisions
(2004-04-22 → 2007-02-26) were imported: `master` carries the trunk, `dcemu-exp` is the 2005
experimental branch. The `dcemu-rewrite` CVS tag exists in the archive but never received a
commit.

The untouched `,v` archives are kept in `_cvsroot/` and excluded from git. Files that CVS had
deleted (`cd.c`, `config.c`, `controller.c`, `fast_interpreter.c`, `sh4.c`, `JoySDL.c`,
`KbSdl.c`, ...) are deletions in the history, not files on disk — recover one with
`git log --diff-filter=D --name-only` then `git show <commit>^:<path>`.

Author e-mail addresses were not recorded by CVS and are synthesized.
