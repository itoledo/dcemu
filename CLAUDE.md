# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## What this is

`dcemu` is a Sega Dreamcast emulator written in C (one C++ translation unit) targeting
SDL 1.2 + OpenGL. It emulates the SH-4 CPU as a threaded interpreter and the PowerVR2
(PVR/TA) graphics chip by translating tile-accelerator display lists into OpenGL calls.

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
classification table and the reassembly of the 64-byte parameters), `mmu`, `wdt`, `tmu`,
`vram` (the two windows of PVR video RAM), `ubc` (the hardware breakpoint controller,
driven with the same register sequences KOS's driver uses), and `aica`, `arm7` and `g2dma`.

They link the real handlers and the real `opcodes.c`; `tests/memoria_prueba.c` replaces
`mem.c` and `tests/dobles.c` replaces the `graficos.c` / `iso.c` / `intc.c` / `traza.c`
symbols the code references, which keeps SDL and OpenGL out of the link. SDL *headers* are
still needed to compile (`opcodes.h` pulls in `main.h`).

**Several files are SDL-free on purpose so the suites can link them for real**: `sistema.c`,
`vram.c`, `ta.c`, `aica.c`, `arm7.c`, `g2dma.c`. Keep them that way.

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

**A KOS demo sweep cannot catch every regression, and this matters when judging a change.**
Several paths are exercised only by commercial games: mipmapped textures, the TSP repeat
modes, blend codes 2 and 3, the Offset Color, the texture environment's alpha rules, the
render-latch registers, the nested-bank swap. For each of those the whole ten-demo control
set stays byte-identical while a game changes. When a change touches one of them, the four
games are the test.

## Run

```sh
dcemu.exe [opciones] [1st_read.bin | image.iso | image.cue]   # default argument: 1st_read.bin
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
| `--traza-mem` | reporta a stderr las direcciones sin emular y dónde se traba el PC, con el tiempo emulado, y al salir la relación con el tiempo real |
| `--limitar` | no dejar que la emulación corra más rápido que una consola. Solo frena |
| `--hacks-bios` / `--sin-hacks-bios` | fuerza o desactiva los hooks de syscall |
| `--captura-gl=ARCHIVO` | vuelca a un BMP lo que OpenGL rasterizó, en cada cuadro |
| `--captura-audio=ARCHIVO` | vuelca a un `.wav` lo que el mezclador del AICA produjo. Es **la medida** del sonido, no lo que hizo la tarjeta |
| `--sin-audio` | no abrir la tarjeta de sonido. El AICA se emula igual y `--captura-audio` sigue funcionando |
| `--sin-aica` | no emular el AICA: ni el ARM, ni los canales, ni los temporizadores. Para aislar una regresión |
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
| `DCEMU_CAPTURA_TODAS=N` | guarda un cuadro de cada N a un archivo numerado propio |
| `DCEMU_TRAZA_EXC=1\|2\|3` | histograma de excepciones / censo de sitios de syscall / flujo completo |
| `DCEMU_TRAZA_SYSCALL=dest[:pr[:N[:K]]]` | traza de instrucciones en la K-ésima aparición de ese syscall (hex:hex:dec:dec) |
| `DCEMU_TRAZA_DEPURACION=1` | imprime lo que el guest manda a su salida de depuración (CE) |
| `DCEMU_TRAZA_EN_MS=N[:M]` | puntos de control por milisegundo de PC y registros |
| `DCEMU_TRAZA_ESCENA=N[:M]` / `=+K[:M]` | vuelca una escena entera tira por tira, por número o por peso |
| `DCEMU_TRAZA_ATA=cmd:N` | traza lo que hace el driver con lo que la lectora contestó |
| `DCEMU_WATCHPOINT_MAX=N` | sube el tope de informes del watchpoint (200 por omisión) |
| `DCEMU_COMO_GD=1` | presenta el disco como el GD-ROM del que se ripeó (rama equivocada, ver notas) |
| `DCEMU_PERFIL_ARM=1` | histogramas del ARM7 por dirección y por fila de despacho |
| `DCEMU_SIN_DIBUJO=1` / `DCEMU_SIN_VOLUMEN=1` / `DCEMU_SIN_FILTRO_MIP=1` | aíslan una etapa del render para medirla |
| `DCEMU_SIN_CACHE_MMU=1` | apaga las tres cachés de traducción de la MMU. **Valen 1,8× en DCDoom**; es el interruptor del A/B y para aislar una regresión |
| `DCEMU_MMU_DATOS=N` | entradas de la caché de traducciones resueltas (4096 por omisión, tope 8192). Para barrer el tamaño sin recompilar |
| `DCEMU_SONDA_SETJMP_POR_INSTRUCCION=1` | vuelve a armar el salto de excepción una vez por instrucción, como era antes (13,5 % más lento) |
| `DCEMU_SONDA_SIN_BANCOS_FPU=1` | la instantánea de excepciones no copia los bancos de coma flotante |
| `DCEMU_SONDA_SIN_INSTANTANEA=1` | la instantánea no copia nada. **Rompe el guest a propósito**: sirve para saber que el mecanismo es portante, no para cronometrar |
| `DCEMU_LTCG` | construcción con LTCG (encendida por omisión) |

Todas viven en el binario normal a propósito: comparar dos compilaciones mete el layout como
variable, y este árbol ya perdió una sesión por eso. Se leen una vez al arrancar, nunca en el
camino caliente. Ver `docs/rendimiento-plan.md`, fase 6.

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
- **The GL buffer is the window, 800×600, not the emulated 640×480.** A `glReadPixels(0, 0,
  640, 480)` returns the bottom-left rectangle and silently drops the top and right 20%.
  Anything drawing in the top band (all of `conio`) reads as "draws nothing".
- **When a capture says blank, check the strip counts at exit before believing it.**
  `--traza-mem` prints how many scenes rendered and the strip count of the last twelve — that
  is what separates "the demo stopped submitting" from "the capture is wrong".
- **A silent `.wav` is a black BMP.** Measure `--captura-audio` the same way: non-zero
  samples, distinct values, RMS and peak.
- **`stdout.txt` and `stderr.txt` land next to the executable**, i.e. `build/Release/`, not in
  the working directory — SDL 1.2 builds the path from `GetModuleFileName`. Redirecting the
  process's output from the shell captures zero bytes. Two instances truncate each other's.
- **`--salir-tras=N` matters**: `--desensamblar`, `--volcar` and the `.wav` all close through
  `traza_resumen()`. Killing the process from outside loses them.
- **Guest time runs ~2.5× fast without `--limitar`**, so a guest-side delay elapses sooner in
  wall-clock than the source suggests.
- **Before any A/B: kill stray `dcemu.exe` processes and `git reset --hard`.** An orphaned
  process eating a core, and `git checkout -- <file>` restoring from the *index*, between them
  cost three wrong numbers in the timing work.
- **`--captura-gl` eats 40% of the real time**, so a performance comparison made with it on
  measures the BMP dump. Discard the first run after a `--clean-first`, and alternate binaries
  within one batch rather than trusting one run of each.
- **Discard the first run of any freshly linked binary, not just after a `--clean-first`.** An
  incremental rebuild is enough: Crazy Taxi measured 133 643 ms on the first run of a new
  `dcemu.exe` and 116 805 on the same code minutes later, with the instruction count identical
  to within 100 in 22 billion. That is a 13% error, larger than most things worth measuring.
  A table whose rows come from different batches cannot be read at all.
- **Demos that place geometry with `rand()`** (the modifier-volume ones) differ run to run. A
  two-colour BMP proves nothing; run them a few times.
- **XInput is read globally, without window focus.** If anyone touches a gamepad during a
  measurement, those presses enter the run.
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
  history.** Every block in `inicializar_memoria()` is calloc for this reason. An
  identification register answered casually has hung the guest three times (`REVISION`,
  `SB_G1SYSM`, `SB_SBREV`). `--traza-mem` reports each such read once — check it.
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
`0x04/0x05/0xA4/0xA5` video RAM (8 MB) with `0x06/0x07` as their image areas, `0x11/0x13` the
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
- **`glClear` of the depth buffer is masked by `glDepthMask`.**
- **Mipmapped textures store their levels from 1×1 up**, so the big level is not at the
  texture address.
- **`glTexParameteri` applies to whatever texture is bound** — set the filters after
  `glBindTexture`, inside `get_texture()`.
- **The two windows of video RAM interleave the banks differently.** `vram.c/h` owns the
  conversion; the block stays in 32-bit numbering and every 64-bit-window access converts.
  Which window the CH2 DMA uses comes from `SB_LMMODE0`/`SB_LMMODE1`, not from the address.

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

**Sound works: four KOS demos play.** `aica.c/h` is the chip's register block, its three
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

**What is not emulated**: CDDA, the audio DSP, the LFO, the FEG filter and the sample-interval
interrupt. The ARM7 is the biggest cost after the SH-4 interpreter, 14-15% of a run.

→ `docs/notas-aica.md` and `docs/arm7-plan.md`.

### GD-ROM and disc images

`gdrom.c/h` is the drive as the hardware sees it: the ATA register block, the status machine
(BSY/DRQ/DRDY/CHECK, interrupt reason, drive state and disc type) and the SPI packet commands
the boot ROM uses. Sectors come from `iso.c`. `mem.c` routes the two address ranges to it;
nothing else touches it except `dcopcodes.c`, which reuses `gdrom_construir_toc()` so both
paths report the same disc. Data goes out either as chained DRQ blocks or through the G2 DMA
(`SB_GDSTAR`/`SB_GDST`), depending on bit 0 of FEATURES at the `PACKET` command.

`iso.c` picks a backend by extension: `.iso` is a flat ISO9660 read by `iso9660_min.c`, `.cdi`
(DiscJuggler) goes through `cdi.c`, anything else needs `USE_LIBCDIO`, which this build does
not have. `iso_init()` lists every track with its LBA, size, mode and file offset — that
listing is the first thing to look at.

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

All four commercial images that parse now run, on both paths. `docs/notas-gdrom.md` has the
layout table, the `.cdi` format, the five drive bugs and the damaged Virtua Tennis rip.

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
the command list at `SB_MDSTAR` and answers each transfer in place. Only port A has a device —
a standard HKT-7700 controller — and the other ports get `0xFFFFFFFF`. Two commands are
implemented: `Device Request` (1) and `GetCondition` (9). **The hardware trigger exists too**:
with `SB_MDTSEL=1` and `SB_MDEN=1`, `maple_vblank()` synthesizes the `SB_MDST` write at each
vblank, which is how Windows CE polls the pad.

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
  first thing in `main()`).

## Where the deep notes live

| archivo | qué cubre |
| --- | --- |
| `docs/notas-herramientas.md` | `--traza-mem`, watchpoints, `--traza-desde`, capturas, las variables `DCEMU_TRAZA_*`, y las lecciones de medición completas |
| `docs/notas-graficos.md` | pipeline PVR/TA, sprites, entorno de textura, formatos, YUV, paletas, caché, RTT, ventanas de VRAM, plano de fondo, profundidad, niebla, volúmenes modificadores |
| `docs/notas-cpu-mem.md` | núcleo SH-4 y bancos, mapa de memoria, registros de identificación, excepciones síncronas, UBC |
| `docs/notas-tiempo.md` | interrupciones ASIC por nivel, `intc_sh4_reintentar`, relojes, TMU/WDT, DMAC |
| `docs/notas-aica.md` | AICA, ARM7DI, G2-DMA, envolventes, KYONB, perfil del ARM |
| `docs/notas-gdrom.md` | lectora, `.cdi`, TOC, qué imágenes arrancan, el rip dañado de Virtua Tennis |
| `docs/notas-arranque.md` | boot ROM, dónde decide, hooks de syscall, bloque de región/SYSID, Windows CE, Maple, flash y RTC |

Los `*-plan.md` son bitácoras de trabajo, no referencia: `bios-boot-plan.md`,
`pendientes-plan.md` (los apartados A.x que citan las notas), `mmu-plan.md`, `aica-plan.md`,
`arm7-plan.md`, `clock-plan.md`, `rendimiento-plan.md`, `hilos-plan.md`, `interprete-plan.md`,
`msvc-build-plan.md`. `demos-kos.md` es el estado de las 135 demos y `sh4-conformidad.md` la
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
