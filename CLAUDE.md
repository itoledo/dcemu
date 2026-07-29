# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`dcemu` is a Sega Dreamcast emulator written in C (one C++ translation unit) targeting
SDL 1.2 + OpenGL. It emulates the SH-4 CPU as a threaded interpreter and the PowerVR2
(PVR/TA) graphics chip by translating tile-accelerator display lists into OpenGL calls.

The codebase is from 2004-2007 and was developed in CVS by `itoledo`, `necroromancist`
and `basoft`. Identifiers, comments and log strings are mostly in Spanish (`memoria`,
`graficos`, `pantalla`, `cargar_archivo`, `Dibujar*`) — match that when editing existing
code, using neutral Spanish (no voseo).

## Build

There is no linter and no CI. There is a unit-test suite for the SH-4 opcode handlers
(`tests/`, MSVC/CMake only) — see "Tests" below. Everything above the CPU core is still
verified by building and running the emulator against a demo/ISO.

```sh
make -f Makefile.win      # Windows, MinGW/Dev-C++ gcc  -> dcemu.exe
make -f Makefile.linux    # Linux                       -> dcemu
make -f Makefile.win clean
```

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
cmake -S . -B build
cmake --build build --config Debug --target dcemu_tests
ctest --test-dir build -C Debug --output-on-failure
```

`tests/` holds unit tests for every implemented row of `opcodes[]` (one suite per handler
file, plus one for the dispatch-table expansion). They link the real handlers and the real
`opcodes.c`; `tests/memoria_prueba.c` replaces `mem.c` and `tests/dobles.c` replaces the
`graficos.c` / `iso.c` / `intc.c` symbols the handlers reference, which keeps SDL and
OpenGL out of the link. SDL *headers* are still needed to compile (`opcodes.h` pulls in
`main.h`).

Every row of `opcodes[]` is now implemented — the only one left on `NOIMP` is the
catch-all that covers bit patterns which are not SH-4 instructions. The 16 deviations the
suite originally found have been fixed; `tests/README.md` lists them, plus the three
things that still do not match the manual on purpose (no FPU exception/flag machinery, no
MMU or cache behind `LDTLB`/`OCB*`, and two `LDC ...,SGR` rows of doubtful existence).

A case marked `CASO_XFAIL` documents a known deviation and is expected to fail; if it
starts passing the runner reports `XPASS` and exits non-zero, so the note cannot go stale.

The `cobertura` suite walks `opcodes[]` and fails if any implemented row was never
exercised, so a new instruction gets flagged until it has a test.

End-to-end check after touching the CPU core: `demos/roto/` is a 256-byte rotozoomer that
exercises FSCA, FDIV, FTRC, FLOAT and MUL.L. See its README for how to run it.

## Run

```sh
dcemu.exe [1st_read.bin | image.iso | image.cue]   # default argument: 1st_read.bin
```

Needs, relative to the working directory: `bios/bios.bin` (not in the repo), `font.png`
(BFont) and `fixedfont.bmp` (guichan). An argument ending in `.bin` loads `ip.bin` plus
that binary directly; anything else is opened as a disc image through libcdio, and
`ip.bin` / `1st_read.bin` are pulled from it (descrambled by `scramble.c`).

Keys: F1 fullscreen, F2 log window, F9 step, F10 stop, F11 run, F12 debug view,
`p` pause, arrows + `a s d w z` = pad, `q`/`e` triggers, `y h g j` analog stick,
keypad `+`/`-` scroll the memory dump.

## Architecture

### CPU core

`sh4emu.c/h` defines a single global `sh4_cpu core`. Everything reaches CPU state through
macros rather than the struct: `PC`, `R(n)`, `SR_T`, `VBR`, `FR(x)`, `DR(x)`, `FPSCR`.
`initCpuSubSystem()` wires `core.execute` and the float register banks.

Dispatch is a **fully expanded jump table**: `main_loop()` calls
`core.execute(*(WORD *) get_memory_pointer(PC))`, which is `oplist[opcode](opcode)` — a
65536-entry array of function pointers indexed by the raw instruction word. There is no
decode step at runtime.

`opcodes.c` holds the master table `opcodes[]` of `{op, mask, mnemonic, operand-type,
handler, restriction}`. `initopcodes()` expands it into **four** such tables
(`oplist_pr0_sz0`, `oplist_pr0_sz1`, `oplist_pr1_sz0`, `oplist_pr1_sz1`), one per
combination of the FPSCR `PR`/`SZ` bits, so double-precision and float-pair variants of
the same encoding resolve without a per-instruction check. `UpdateFPSCR()` in `sh4emu.c`
repoints `oplist` when those bits change and swaps the float banks when `FR` changes;
`UpdateSR()` swaps the banked general registers when `RB` changes.

**Adding an instruction = one row in `opcodes[]` + a handler function.** Nothing else
changes. Rows must not overlap: `initopcodes()` logs colliding encodings to
`logs/repetidos.txt`. Unimplemented encodings point at `NOIMP` (`dcopcodes.c`).

Handlers live in files by category — `mov.c`, `arith.c`, `logic.c`, `shift.c`, `branch.c`,
`syscontrol.c`, `floatsimple.c`, `floatcontrol.c`, `floatgraph.c` (FIPR/FTRV/matrix ops),
and `dcopcodes.c` for Dreamcast-specific behaviour. Each handler advances `PC` itself
(`PC += 2`), including the delay-slot logic in `branch.c`.

### Memory

`mem.c` drives everything off **the top byte of the address** through two parallel
256-entry tables:

- `mem_zone[0x100]` — raw base pointers; `get_memory_pointer(addr)` gives direct access
  (used for instruction fetch and fast paths).
- `mem_hash_read[0x100]` / `mem_hash_write[0x100]` — per-region handler functions;
  `memread()` / `memwrite()` are macros that call straight through them.

Regions: `0x0C/0x8C/0xAC` system RAM (16 MB), `0x04/0x05/0xA4/0xA5` video RAM (8 MB),
`0xA0` PVR/system control registers, `0x10` TA FIFO, `0xE0-0xE3` store queues,
`0x1F/0xFF` SH-4 on-chip registers, `0x00/0x70/0x80` BIOS. Unmapped zones default to
`mem_read_error`/`mem_write_error`.

SH-4 on-chip registers (TMU, DMA, SCIF, INTC, ports) are plain pointers into the `regmem`
block, bound once in `regmem_setup()` (`mem.c`) and declared `extern` in `sh4emu.h`. So
`*TCNT0`, `*DMAOR`, `*IPRA` are both the emulated register and the guest-visible memory.

`pvr_write()` is a large `switch` over individual PVR register addresses using the
`PVR_WRITE_CB_*` macros, which log the access and then invoke a callback in `graficos.c`
(`cb_tastart`, `cb_renderstart`, `cb_param_base`, `cb_fb_r_sof1`, ...). That is how
register writes become rendering work.

### Graphics pipeline

The guest submits geometry through the SH-4 store queues, not through a normal write:
`pref142()` in `syscontrol.c` flushes SQ0/SQ1, and when the target lands in the TA FIFO
(`0x10000000`) it decodes the parameter control word and dispatches on the para-type to
`taListEnd()`, `doUserClip()`, `objectListSet()`, `taPolyModifier()` or
`taVertexHandler()` in `graficos.c`.

Those build up `VertexBuffer[]` and `TriangleStrip[]` (declared — and *defined* — in
`render.h`, which only `graficos.c` may include for that reason). A write to `TA_LIST_INIT`
triggers `cb_tastart()`, which sorts the strips so translucent geometry draws last, sets
per-strip GL state, uploads/binds textures via `get_texture()` (handles twiddled and VQ
formats), and issues `glDrawArrays(GL_TRIANGLE_STRIP, ...)`.

`glops.c/h` is an **older, now-bypassed** path: a recorded display list of `GLOP_*`
commands replayed by `glop_process()`. `graficos.c` has its `#include "glops.h"` commented
out and calls OpenGL directly; `glops.c` is still compiled and linked. Do not assume
changes there affect rendering.

`DibujarFramebuffer()` handles the 2D case, uploading PVR video RAM as a texture on a
screen-sized quad (`FRAMEBUFFER_*` formats).

### Interrupts and timing

`main_loop()` in `main.c` is the whole scheduler: execute one instruction, and every 50
accumulated cycles call `timer_check()` (which decrements TMU `TCNT0/1/2` and raises
`EXC_TMU*_TUNI*` on underflow). Every 978 cycles is one scanline; scanline compares raise
`SCANINT1`/`SCANINT2`, end of frame raises `ASIC_EVT_PVR_VBLINT`, redraws and pumps SDL
events.

`intc.c` queues ASIC events with `intc_add()` into `intc_queuemask`; `check_ints()` drains
them and `intc()` performs the actual SH-4 exception entry (SSR/SPC save, VBR jump).

### BIOS syscall emulation

With `BIOS_HACKS` enabled in `options.h`, `main()` patches the syscall vector slots
(`0x8C0000B0`-`0x8C0000E0`) to point at stub code it writes at `HACK_BASE`. The GDROM stub
is an illegal opcode that maps to `BIOS_HACK` in `dcopcodes.c`, which calls `hack_gdrom()`
to service `GDROM_SEND_COMMAND` (sector reads, TOC) directly from the mounted image via
`iso.c`. Font lookups are redirected to a font rasterized into guest RAM at `FONT_BASE`
by `inicializar_fonts()`.

### Support modules

`gui.cpp` is the only C++ file — a guichan overlay log window, exported to C via
`extern "C"` in `gui.h`. `debug.c` is the in-emulator disassembler and register/memory view
(F12), driven by `DebugMode` (`DBG_STOP`/`DBG_RUN`/`DBG_STEP`). `BFont.c` is a bitmap font
renderer. `log.c` writes `logs/{disasm,memoria,serial,pvr,intc,glop}.txt`.

## Conventions and gotchas

- **`options.h` is the feature switchboard.** Nearly all debug output and several
  behaviours are compile-time `#define` toggles there. Check it before adding a `printf`.
- **Logging is compiled out by default.** `logmsg()` / `logxmsg()` expand to nothing
  unless `LOGGING` is defined in `options.h`; `LOG_FFLUSH` makes it survive a crash.
  Runtime toggles also exist (`filelogging`, keys `l`/`m`/`v`/`r`).
- Instruction handlers own `PC`. Forgetting `PC += 2` hangs the emulator silently.
- `decode.h` and `dcemu_private.h` are not referenced by any source file.
- The old `PC_func` / `str_PC` indirection in `sh4emu.c` is dead code behind
  `PC_FUNCTIONS`; the live path uses `get_memory_pointer(PC)`.

## Repository history

This repo was converted from its original CVS/RCS archive to git. All 886 revisions
(2004-04-22 → 2007-02-26) were imported: `master` carries the trunk, `dcemu-exp` is the
2005 experimental branch. The `dcemu-rewrite` CVS tag exists in the archive but never
received a commit.

The untouched `,v` archives are kept in `_cvsroot/` and excluded from git. Files that CVS
had deleted (`cd.c`, `config.c`, `controller.c`, `fast_interpreter.c`, `sh4.c`,
`JoySDL.c`, `KbSdl.c`, ...) are deletions in the history, not files on disk — recover one
with `git log --diff-filter=D --name-only` then `git show <commit>^:<path>`.

Author e-mail addresses were not recorded by CVS and are synthesized.
