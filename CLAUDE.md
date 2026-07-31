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
file, plus one for the dispatch-table expansion), plus three suites that are not opcodes:
`sistema` (PDTRA handshake, flash synthesis, RTC), `gdrom` (the drive's state machine,
driven exactly as the boot ROM drives it) and `ta` (the TA parameter format — the
classification table and the reassembly of the 64-byte parameters). They link the real handlers and the real
`opcodes.c`; `tests/memoria_prueba.c` replaces `mem.c` and `tests/dobles.c` replaces the
`graficos.c` / `iso.c` / `intc.c` / `traza.c` symbols the code references, which keeps SDL
and OpenGL out of the link. SDL *headers* are still needed to compile (`opcodes.h` pulls in
`main.h`).

Every row of `opcodes[]` is now implemented — the only one left on `NOIMP` is the
catch-all that covers bit patterns which are not SH-4 instructions. The 16 deviations the
suite originally found have been fixed; `tests/README.md` lists them, plus the three
things that still do not match the manual on purpose (the parts of the FPU that are
neither Cause nor Flag, no cache behind `OCB*`, and two `LDC ...,SGR` rows of doubtful
existence). `LDTLB` and the MMU are no longer on that list — see `docs/mmu-plan.md`.

**FPSCR's Cause and Flag fields are written now** (suite `fpu-excepciones`), which is what
KOS's `basic/fpu/exc` checks. Three rules there are easy to get backwards, and
`docs/sh4-conformidad.md` explains why: `inf/0` is *not* a divide-by-zero, a NaN that
merely passes through raises nothing (invalid is recognised by the result coming out NaN
when no input was), and add/sub can never underflow — a sum that lands below the smallest
normal is exact. Still missing: the I cause on its own, and the DN and RM bits.

**Enable and the three FPU exceptions are wired too** — 0x120 when a cause meets its Enable
bit, 0x800/0x820 when `SR.FD` is set. See "Synchronous exceptions" below for the mechanism
and `demos/fpu-trampa` for the end-to-end test.

A case marked `CASO_XFAIL` documents a known deviation and is expected to fail; if it
starts passing the runner reports `XPASS` and exits non-zero, so the note cannot go stale.

The `cobertura` suite walks `opcodes[]` and fails if any implemented row was never
exercised, so a new instruction gets flagged until it has a test.

End-to-end check after touching the CPU core: `demos/roto/` is a 256-byte rotozoomer that
exercises FSCA, FDIV, FTRC, FLOAT and MUL.L. See its README for how to run it.

**Above the CPU core, the regression baseline is the KOS example tree.** `docs/demos-kos.md`
records the state of all 135 binaries as measured on 2026-07-30 — which ones pass, which fail
and why, and which ones fail because they ask for hardware that is not emulated. It also
documents how the sweep is run and the two ways it produces false negatives. Read it before
concluding that a demo is broken.

## Run

```sh
dcemu.exe [opciones] [1st_read.bin | image.iso | image.cue]   # default argument: 1st_read.bin
```

Needs, relative to the working directory: `bios/bios.bin` (not in the repo), `font.png`
(BFont) and `fixedfont.bmp` (guichan). `bios/flash.bin` is optional — without it a minimal
flash is synthesized. An argument ending in `.bin` loads `ip.bin` plus that binary
directly; anything else is opened as a disc image through libcdio, and `ip.bin` /
`1st_read.bin` are pulled from it (descrambled by `scramble.c`).

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
| `--watchpoint=D[:T]` | informa cada escritura que toque `D` (hex), de `T` bytes, con el PC y el PR |
| `--desensamblar=D:N` | al salir, desensambla `N` instrucciones desde `D`. Repetible |
| `--volcar=D:N` | al salir, vuelca `N` bytes desde `D` en hexadecimal. Repetible |
| `--salir-tras=N` | sale solo a los `N` segundos de tiempo **emulado** |

Los cuatro últimos son de diagnóstico y **todos los números van en hexadecimal**.

**`--traza-mem` is the tool for working on the BIOS boot.** It prints each unemulated
address once with the PC that asked for it, and when the last 96 PCs collapse into 64 or
fewer distinct values over four million instructions it dumps the ring, **disassembles the
loop** and prints the registers. `gdrom.c` also reports ATA commands and SPI packets
through it. See `docs/bios-boot-plan.md`.

Not every loop it reports is a hang — a `memset` over 600 KB and a wait for vsync both trip
the same heuristic. Read the disassembly, not the fact that it fired.

**`--watchpoint=DIR[:TAM]` is the other tool**: a write watchpoint that reports every
write touching a given address, with the PC and PR that did it. The hook is in
`memwrite_fisico()` (`mem.h`) — the one place *every* write goes through, guest and
internal alike — and the implementation is in `traza.c`. It costs a compare against zero
per write when off. It is what answers "who writes this variable", which is the question the
BIOS boot keeps raising; `docs/bios-boot-plan.md` walks through the two cases it solved.

**`--desensamblar` and `--volcar` are how you read the boot ROM.** Its code lives in RAM —
it copies itself there and is *not* at the same address inside `bios.bin` — so the only way
to read a routine or one of its tables is from inside the emulator. Both print at exit,
which is why **`--salir-tras=N` matters**: it leaves through the same path as closing the
window, so `traza_resumen()` runs. Killing the process from outside takes the disassembly
and the dump with it.

**Where the BIOS boot stands: it boots.** With `--bios` it reaches the Set Date/Clock
screen, responds to the pad, and from the main menu you can enter Play, File, Music and
Settings. What was missing was three things, all of the same shape — something the guest
asks for that dcemu accepts without doing anything and without saying anything. See
`docs/bios-boot-plan.md`, "Tercera corrida":

- **the CH2 DMA** (`SB_C2DSTAT`/`SB_C2DLEN`/`SB_C2DST`, `0x005F6800-08`) — see below;
- **a Maple descriptor whose first word is legitimately zero** — see the Maple section;
- **ASIC events being dropped when no mask covered them** — see "Interrupts and timing".

Hito C (booting a game *through* the BIOS) is still unverified: it needs an image that
would boot on hardware, and the GD-ROM geometry.

**`0x005F6800-0x005F6808` is the CH2 DMA, and it is how the guest feeds the TA.** The Holly
drives it, not the DMAC: the SH-4 only puts the source in `SAR2` and arms `CHCR2` for
external request, and writing 1 to `SB_C2DST` is what starts it. So `dma_check()` never sees
it — it only handles auto-request channels, correctly — and the three registers had no case
in `pvr_write()`, fell through to the `control_mem` backing store and vanished. The boot ROM
records the transfer in its own descriptor table with bits 0-1 of `+0x18` set to "in
progress" and waits for the end of DMA to clear them, which never came: that is the loop
around `0x8C0D9C50` it used to sit in forever. `ch2_dma_ejecutar()` in `mem.c` does it, and
the same fix makes `parallax-serpent_dma` work.

**`0x005F8004` is the PVR `REVISION` register and a retail console answers `0x11`.** The
boot ROM checks `COREID` against `0x17FD11DB` *and* demands revision `>= 17` and `!= 1`; it
had no read case, fell through to the control-block backing store, read zero, and parked
itself in a deliberate `BRA` to itself at `0x8C0DBDFC` forever. Same shape as `SB_G1SYSM`.

The window title carries the name of what is running (`titulo_poner()` in `graficos.c`, called
before `screeninit()`), which is what makes a sweep of demos opened one after another readable.

Keys: F1 fullscreen, F2 log window, **F5 dump the framebuffer**, **F6 dump the GL buffer**,
F9 step, F10 stop, F11 run, F12 debug view, `p` pause, arrows + `a s d w z` = pad,
`q`/`e` triggers, `y h g j` analog stick, keypad `+`/`-` scroll the memory dump. A gamepad
works too — see "Input".

**F5 writes `captura.bmp` from video RAM, not from the GL buffer** (`volcar_framebuffer()`
in `graficos.c`), so it shows what the guest drew rather than what GL rasterized. With
`--traza-mem` it also dumps the PC ring and disassembles it.

**F6 writes `captura-gl.bmp` from the GL buffer** (`volcar_gl()`), which is the other half:
3D output never goes through video RAM in dcemu, so for a PVR demo F5 is always black.
`--captura-gl=ARCHIVO` does the same automatically before every swap, so the file holds the
last frame when the emulator exits — pair it with `--salir-tras` and a sweep needs no window
at all.

**It reads the window, which is 800×600, not the emulated 640×480 — and getting that wrong
invalidated a whole sweep.** `screeninit()` stretches the guest's 640×480 over the entire window
with `glOrtho`, so a `glReadPixels(0, 0, 640, 480)` returns the bottom-left rectangle: the top
20% and the right 20% are simply missing. Anything drawing in the top band — the whole `conio`
family, which puts its text there — came out an almost-black BMP and read as "draws nothing".
`conio-basic` went from 8 non-black pixels to 2742 with that one line, and no PVR change at all.
The lesson generalises: when the capture says blank, check the strip counts at exit before
believing it.

**SDL 1.2 redirects `stdout` and `stderr` to files on Windows** — `stdout.txt` and
`stderr.txt` in the working directory. Redirecting the process's output from the shell (or
with PowerShell's `-RedirectStandardError`) captures zero bytes and looks like the emulator
said nothing. `--traza-mem` and the MMU's warnings come out there.

**On screen-grabbing the window — don't, use `--captura-gl`.** Grabbing the client area
works, until it doesn't: in one session `tunnel` went from 3036 distinct colours to 4 with no
change to the emulator, while `--captura-gl` kept reporting 1837. It depends on the host's
compositor and it fails silently, so a whole sweep can come out black and read as a massive
regression. Three more things make a *window* grab come out black, and all have cost time:

- **The demo already exited.** KOS clears the screen on the way out, so anything that ends
  by itself (`video/minifont` sleeps 10 s and returns) is black by the time a 14-second
  harness looks. Grab it earlier, and treat a missing window as "finished", not "failed".
- **Guest time runs about 2.5× fast without `--limitar`**, so a guest-side delay elapses
  sooner in wall-clock than the source suggests: `minifont`'s 10 s `thd_sleep` is over in
  roughly 4 s.

To separate "GL never got the image" from "the grab is black", `DibujarFramebuffer()`
`glReadPixels` four points of the back buffer under `--traza-mem` (every 300th frame) and
prints them next to the bytes it read out of video RAM.

### Sprites and the texture environment

**A sprite is a whole rectangle in one 64-byte parameter** — four corners of which the last is
derived by completing the parallelogram, D = A − B + C — and its colour lives **in the header**
(word 4), not in the vertices. `taSprite()` reuses `taPolyModifier()` because words 1-3 mean the
same thing, then picks up the base and offset colours. `vertice_sprite()` emits A, B, D, C, which
as a triangle strip gives (A,B,D) and (B,D,C), i.e. the rectangle. Watch word 12: there is an
unused word between `Dy` and the UVs, so the three texture words are 13, 14 and 15 — reading them
one early leaves `u` at zero for all four corners and the texture is sampled along a line.

**The texture environment was never emulated**, so everything got GL's default `GL_MODULATE`. The
chip has four modes in TSP bits 7-6: 0 replace, 1 modulate, **2 decal**, 3 modulate-alpha. Decal is
2, not 0 — mixing them up sends a surface whose vertex colour is black through modulate and it comes
out black, which is what kept `pvr-bumpmap` blank.

**Punch-through had `GL_LEQUAL` hardcoded.** The depth buffer is cleared to 0.0 and a scene's z
values land around 0.5, so "less or equal" fails against any untouched pixel — the list could
essentially never draw. It uses the compare mode from its own ISP word, like the opaque list; what
distinguishes punch-through on the chip is that it discards on alpha.

### Texture formats

`taPolyModifier()` maps the texture control word's `pixelformat` onto a GL format triple;
`get_texture()` untwiddles and uploads. ARGB1555, RGB565, ARGB4444, YUV422 and the two
palette formats are handled, and VQ and stride have their own paths.

**BUMP texels are not a colour**: they are two 8-bit angles, elevation S in the high byte and
rotation R in the low one, which the chip combines per pixel with four parameters carried in the
polygon's offset colour — K1, K2, K3 and Q — as `I = K1 + K2·sin(S) + K3·cos(S)·cos(R − Q)`. On the
chip that intensity then *modulates* the textured polygon behind it, which is per-fragment maths
fixed-function GL does not have. `decodificar_bump()` resolves it at upload time and hands GL a
grey. That is exact as long as the parameters come from the header — true for a sprite, where the
offset colour lives there — and what is lost is the combination with the other layer.

**`glTexParameteri` applies to whatever texture is bound, and the filters used to be set
before `glBindTexture`.** So they landed on the *previous* frame's texture and the new one
kept the GL defaults — and the default `GL_TEXTURE_MIN_FILTER` is `GL_NEAREST_MIPMAP_LINEAR`,
which **requires mipmaps**. Without them the texture is incomplete and GL samples it as solid
white. A demo that draws many frames hides this (from the second frame on, the texture is
already bound and does receive the parameters); one that draws a single frame and then waits
for a button came out entirely white. That was `pvr-yuv_converter-*` and `pvr-strided_texture`
— three demos, one line. `aplicar_filtros()` now runs inside `get_texture()`, after both
binds (new texture and cache hit).

**Stride** (bit 25) means the rows in memory are `TEXT_CONTROL & 0x1F` × 32 texels wide
rather than the declared `usize` — it is how a non-power-of-two texture is stored, and the
declared size is rounded up (640×480 is submitted as 1024×512). `get_texture()` copies row by
row; handing GL the raw block skews the image a little further on every row.
`pvr-strided_texture` draws its chessboard with the squares square and aligned, which is
exactly what a wrong stride would ruin.

**YUV422** packs two pixels into 32 bits — U, Y0, V, Y1 — sharing chroma. GL has no such
format, so `decodificar_yuv422()` converts to RGB on upload, like the palettes. It is what
the TA's YUV converter produces; see below.

### The TA's YUV converter

A separate TA input at `0x10800000` that takes planar YUV420 or YUV422 and leaves a packed
YUV422 texture in video RAM — how video is uploaded without spending CPU on the conversion.
It is fed 16×16 macroblocks; destination and image size come from `TA_YUV_TEX_BASE`
(`0x005F8148`) and `TA_YUV_TEX_CTRL` (`0x005F814C`), and the chip counts what it converted in
`TA_YUV_TEX_CNT` (`0x005F8150`).

None of it was emulated: zone `0x10` went to `ta_write()` wholesale, which keeps 64 bytes for
the polygon FIFO, so the macroblocks were dropped. `pref142()` now splits the two — the
polygon FIFO is `0x10000000-0x107FFFFF` and the converter `0x10800000` up — and
`pvr_yuv_bloque()` in `graficos.c` does the work.

**The order inside a macroblock is not "all U, all V, all Y"**: it goes in 16×8 halves, each
with its own U, V and Y. YUV420 has one chroma pass for all 16 rows and the macroblock is 384
bytes; YUV422 has one per half and it is 512. Writing `TA_YUV_TEX_BASE` or `TA_YUV_TEX_CTRL`
resets the macroblock count — the chip is starting another image.

**The palette formats do not fit the rest of the pipeline, in three ways** (`decodificar_paleta()`):

- **The palette lives in registers, not in texture memory.** 1024 entries at `0x005F9000`
  with their format in `PAL_RAM_CTRL` (`0x005F8108`). Both already had backing store in
  `control_mem` — the guest's writes were arriving all along, nobody read them.
- **Twiddling runs over pixel indices, not 16-bit words.** The existing loop indexes a
  `Uint16 *`; at 8 bpp that is a byte and at 4 bpp half of one.
- **The bank selector overlaps the scan-order bit.** It is bits 26-21 for 4 bpp and 26-25 for
  8 bpp, on top of what the other formats use as "unused", "stride" and "scan order". So bit
  26 means nothing here and reading it as scan order comes out mirrored: indexed textures are
  always twiddled.

The decoder hands GL plain RGBA8888. The PVR's formats are all ARGB, so R and B swap on the
way — GL_RGBA/GL_UNSIGNED_BYTE wants R in byte 0, which little-endian makes `0xAABBGGRR`.

The texture cache keys on (size, address, bpp, bank): same indices with a different palette is
a different texture, and `pvr-palette-wormhole` animates exactly that.

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

Regions: `0x0C-0x0F/0x8C-0x8F/0xAC-0xAF` system RAM (16 MB, mirrored 4× per window),
`0x04/0x05/0xA4/0xA5` video RAM (8 MB), `0x11/0x13` the TA texture FIFO (also video RAM),
`0x00/0xA0` PVR/system control registers, `0x10` TA polygon FIFO, `0xE0-0xE3` store queues,
`0x1F/0xFF` SH-4 on-chip registers, `0xF0-0xF7` the P4 cache and TLB arrays (`mmu.c`),
`0x70/0x80` BIOS. Unmapped zones default to
`mem_read_error`/`mem_write_error`, and their `mem_zone[]` entry points at a 16 MB discard
block so `get_memory_pointer()` never dereferences NULL.

**The mirrors and the alternate windows are not cosmetic.** Guest code reaches the same
physical memory through several windows and depends on them agreeing:

- KOS's `_start` sizes RAM by writing to `0xACFFFFFF` and `0xADFFFFFF` and checking whether
  they alias. Without zone `0xAD` bound it concluded 32 MB and put its stack at
  `0x8E000000`, so every `push` was discarded and every `pop` returned garbage.
- KOS uploads the AICA firmware to sound RAM through the *physical* window
  (`0x00800000`), not through `0xA0800000`. Zone `0x00` therefore goes to
  `pvr_read`/`pvr_write`, which split by physical address and delegate the boot ROM and
  flash range back to `bios_read()`.
- KOS uploads textures through the TA texture FIFO at `0x11000000` with `sq_fast_cpy()`.

Because `pvr_read`/`pvr_write` label their `switch` cases in P2 form (`0xa0...`), both
switch on `fisica | 0xa0000000` so every window resolves identically.

Zone `0xA0` is a whole 16 MB window that `pvr_read`/`pvr_write` split by physical address
after the big `switch`: `0x005F0000-0x005FFFFF` is backed by `control_mem`,
`0x005F7000-0x005F70FF` and `0x005F7400-0x005F74FF` go to `gdrom.c`, `0x00600000-0x006007FF`
is the G2 external device area (returns a fixed "nothing here"), `0x00700000-0x00707FFF`
are the AICA registers (`aica_mem`), `0x00800000-0x009FFFFF` is the 2 MB of sound RAM, and
`0x00000000-0x001FFFFF` plus `0x00200000-0x0021FFFF` are the boot ROM and the flash, both
served by `bios_read()`.

`regmap_read()` has one special case: `0xFF800030` (PDTRA) goes through
`sistema_pdtra()`, the video cable detector handshake. Without it the boot ROM sleeps
forever.

`pvr_read()` has one too, and for the same kind of reason: **`0x005F74B0` is `SB_G1SYSM`, the
G1 bus system-mode register, not a GD-ROM register**, even though it falls inside the
`0x005F7400-0x005F74FF` block that `mem.c` hands to `gdrom.c` wholesale. It has to be caught
before that dispatch. Its high nibble is the machine type and the low one the region, and a
retail console answers zero in both (`G1_SYSM_RETAIL` in `sistema.h`) — the real region comes
from the flash. With the value the drive used to return, `0x2422211F`, `hardware_sys_mode()`
reported type 1, KOS concluded it was a Set5 devkit, and `spu_init()` cleared **8 MB** of sound
RAM instead of 2 — so 6 MB of store-queue writes landed outside everything mapped.

**`0x005F810C` is `SPG_STATUS`, and it is not just the scanline.** Bits 9:0 are the line,
10 the field number, 11 vertical blanking, 12 hsync and 13 **vsync**. It used to return
`pvr_scanline` and nothing else, so anyone waiting on vsync waited forever — that is where
the real boot ROM sat (`0x8C00CB2E`, `TST #0x2000` on this register). vsync is now on for
the first `SPG_WIDTH.vswidth` lines of the frame and blanking runs from
`SPG_VBLANK.vbstart` to `vbend`, wrapping past the end; hsync and field stay zero, since
dcemu tracks no horizontal position and no interlacing. Finding this also turned up that
**`SPG_VBLANK` and `SPG_WIDTH` had read cases but no write cases**, so `pvr_spg_vblank` and
`pvr_spg_width` kept `reg.c`'s defaults no matter what the guest programmed.

SH-4 on-chip registers (TMU, DMA, SCIF, INTC, ports) are plain pointers into the `regmem`
block, bound once in `regmem_setup()` (`mem.c`) and declared `extern` in `sh4emu.h`. So
`*TCNT0`, `*DMAOR`, `*IPRA` are both the emulated register and the guest-visible memory.

`pvr_write()` is a large `switch` over individual PVR register addresses using the
`PVR_WRITE_CB_*` macros, which log the access and then invoke a callback in `graficos.c`
(`cb_tastart`, `cb_renderstart`, `cb_param_base`, `cb_fb_r_sof1`, ...). That is how
register writes become rendering work.

### Graphics pipeline

**Where the PVR stands (1 August 2026): of the ten KOS demos still failing, exactly one is the
PVR's** — `pvr-fb_tex`, and for the twin video-RAM windows rather than anything about rasterising.
Every texture format, every vertex type, sprites, modifier volumes and render-to-texture are in.
Two residues are documented rather than hidden: the two markers `pvr_rtt_sized` draws at z=3 and
z=4 (see "Render to texture"), and `tsunami-genmenu`, whose geometry arrives correctly but lands at
y 631..1458 on a 480-line screen and never scrolls in — guest-side, since dcemu does not touch
vertex coordinates.

`pref142()` dispatches to the TA decoder only when the resolved store-queue target lands in
the polygon FIFO, tested as `(addr & 0xFF800000) == 0x10000000`. It used to test
`addr & 0x10000000`, a bitwise AND that also matched `0x11xxxxxx` — so every 32-byte block
of texture upload was parsed as a polygon control word.

**Three bugs on this path kept the 3D output blank; all three are fixed, and the shape of
them is worth remembering because none produced an error message.**

- `sq_write()` ignored its `size` argument and stored a single `DWORD`. KOS fills the store
  queues with `fmov.d` (8 bytes), so every store lost its upper half: even dwords good, odd
  dwords stale. In a TA vertex that means x and z garbage (dwords 1 and 3) with y fine
  (dword 2) — measurable as `x 0.0..0.0 ... z inf..inf` in the trace.
- The TA FIFO is a FIFO, but `ta_write()` indexed `ta_mem[direccion - 0x10000000]` while
  `pref142()` read back `ta_mem[addr & 0xFF]`. KOS writes the FIFO at *increasing*
  addresses, so past `0x100` the two diverged and past `TA_SIZE` everything was dropped —
  `pref142()` then re-read one stale record forever, every PCW came out `0xE0000000`,
  end-of-strip never fired and `strip_count` stayed 0, so `glDrawArrays` was never called.
  Both now mask with `& 0x3F`: two 32-byte slots, one per store queue.
- Vertex parameter type 3 (textured, packed color) read its coordinates from
  `ta_address_pointer[6]` — `+0x18`, the base color — instead of `[1]`, overrunning the
  32-byte record by 12 bytes. Type 5 already did it right, which is what gives the game
  away.

`cb_tastart()` also indexed `TriangleStrip[strip_count]` for culling and z-write inside a
loop over `i`, i.e. one past the last valid entry.

Two more of the same family, both fixed: the TSP word's source and destination blend factors
(bits 31-29 and 28-26) were **both** assigned to `pvr_srcblend`, so `pvr_dstblend` was never
written and `glBlendFunc()` got an invalid enum — which GL ignores, leaving whatever blend
was set before. And vertex type 3 took its alpha from bits 23-16, the same field as red,
instead of 31-24.

**Two more that between them lost the whole `conio/*` family**, which draws one textured quad
per character straight through `pvr_prim()`:

- **Global parameters did not survive past the first strip.** On the PVR a polygon header sets
  depth mode, culling, Z write, alpha, both blend factors and everything about the texture,
  and those stay in effect until the *next* header. `taPolyModifier()` wrote them into whatever
  `TriangleStrip[]` entry happened to be open, so a second strip under the same header started
  from zero: `depthmode` came out `0`, which is not a valid enum, so `glDepthFunc()` was ignored
  and the previous strip's value stuck — or `GL_NEVER`, which draws nothing. End-of-strip now
  copies the finished entry into the new one; `index` and `count` refill themselves.
- **Culling ignored the winding.** The PVR field has four values: 0 none, 1 "cull if small" (an
  area threshold, which GL has no equivalent for), 2 cull negative area, 3 cull positive. Any
  non-zero value was treated as `glCullFace(GL_BACK)` with GL's default `GL_CCW`. The TA hands
  over screen-space coordinates with y downwards and `screeninit()`'s `glOrtho()` flips it, so
  the winding GL sees is the opposite of the one the PVR assumes — and every quad conio drew
  was culled. Modes 2 and 3 now set `glFrontFace()` explicitly.

`taListEnd()` raising a list-completion interrupt only `if (pvr_registering != -1)` turned out
**not** to be a problem in practice: `pvr_list_finish()` always submits a blank polygon header
with the right list type before the end-of-list marker, precisely because opening a list and
submitting nothing is a hardware error. The marker itself is 32 zero bytes, so its own list-type
field is useless and `pvr_registering` is the only source. `--traza-mem` now prints the list
state machine (`TA_ALLOC_CTRL`, `TA_LIST_INIT`, each end of list, `STARTRENDER`) and the GL state
each strip goes out with, which is how both bugs above were found.

**`pvr_prim: attempt to submit to unopened list` is not a dcemu bug** — it was listed as one here
for a while. It is guest state end to end: `pvr_list_begin()` sets `pvr_state.list_reg_open`,
`pvr_list_finish()` clears it, and `pvr_prim()` warns when it is `PVR_LIST_NONE`. Nothing the
emulator does can set it. Two measurements settle it: of 31 demos with serial logs only `tunnel`
emits it (278674 times in 8 s), and it never emits the companion `pvr_list_begin: attempt to open
already closed list` — so the guest is submitting with no list open at all. `tunnel` is the KGL
demo that was restored and ported here, and its own source documents the API change that causes it
(current KGL opens the list lazily from the GL state and has no `glKosFinishList`). The suspect
before that was the 64-byte parameter whose second half decoded as an end-of-list — see `ta.c` —
but fixing that changed nothing, which is what prompted actually measuring it.

Note `pvr_registered` is `DWORD` in `graficos.c` but `extern int` in `intc.c`.

**`--traza-mem` reports TA activity** for the first three renders: strips, vertices,
end-of-strip count, vertex types, and the min/max of the vertex coordinates. The TA receives
vertices already in screen space, so x must land in `0..width` and y in `0..height`; if not,
the fault is in the vertex layout or the store queues, not in the rasteriser. At exit it also
prints how many scenes were rendered and the strip count of the last twelve — which is what
separates "the demo stopped submitting" from "the capture is wrong", a distinction that has
already cost a whole sweep.

The guest submits geometry through the SH-4 store queues, not through a normal write:
`pref142()` in `syscontrol.c` flushes SQ0/SQ1, and when the target lands in the TA FIFO
(`0x10000000`) it hands the 32-byte block to `ta_procesar_bloque()` in `ta.c`, which dispatches
on the para-type to `taListEnd()`, `doUserClip()`, `objectListSet()`, `taPolyModifier()`,
`taSprite()` or `taVertexHandler()` in `graficos.c`. The CH2 DMA (`mem.c`) feeds the same
function.

**`ta.c` exists because not every TA parameter is 32 bytes.** Headers carrying a face color,
vertices with floating-point color, all six two-volume textured vertices, both sprite vertices
and the modifier-volume vertex are 64, and they arrive as *two* blocks — one per store queue.
Dispatching each block separately reads the second half as a parameter control word, and since
its first word is usually a float the para-type comes out of the garbage: when that float is
`0.0` the type is **0**, which is end-of-list, so a list closes that the guest never closed.
`ta_clasificar()` is the table — PCW → global parameter type and the vertex type it leaves in
force — and `ta_procesar_bloque()` joins the halves before dispatching. Both `taPolyModifier()`
and the block assembler use that one table, so they cannot drift. `ta.c` is free of SDL and GL
on purpose, like `sistema.c`, so `tests/` links it for real.

**`taVertexHandler()` implemented four of the fifteen vertex types** — 0, 1, 3 and 5. The other
eleven fell through to a `logxmsg` and were dropped: the vertex never entered `VertexBuffer`, the
strip closed with `count` 0, and nothing drew. That is what left the modifier-volume demos black —
`pvr-modifier_volume` lost 41 of its 42 vertices, all type 9. `--traza-mem` names the guilty type
directly: `[0]=1 [9]=41` with every strip at `n=0`. The three axes are how the color arrives
(packed / floating / intensity), whether the UVs are 32 or 16 bits, and whether there are two
parameter sets. Intensity types multiply the header's **face color**, which survives past its own
header on purpose: intensity mode 2 reuses the one left by the last polygon in mode 1.

Two-volume vertices carry everything twice — set 0 outside the modifier volume, set 1 inside —
and `struct vertex` holds both. Types with a single set get set 1 filled as a copy, so the second
pass can draw any strip without asking what type it was.

### Render to texture

`cb_tastart()` splits in three: `render_a_textura()` decides where the scene goes, `dibujar_escena()`
draws the strips and `terminar_escena()` presents. **The marker is bit 24 of `FB_W_SOF1`**
(`0x005F8060`) — KOS writes `address | BIT(24)`. Size comes from the clip registers (`PCLIP_X`/`Y`,
`0x005F8068`/`0x6C`, maximum in bits 31-16), row pitch from `FB_W_LINESTRIDE` (`0x005F804C`, in
units of 8 bytes) and the pixel format from `FB_W_CTRL` (`0x005F8048`).

dcemu sends 3D to OpenGL, so the scene never passes through video RAM: it has to be drawn and read
back with `glReadPixels`. Two things are easy to get wrong — the guest submits vertices **in the
target's coordinates** (0..128 by 0..64 for `pvr_rtt_sized`), so the viewport and `glOrtho` must be
the texture's and not the screen's; and `glReadPixels` returns bottom-up while the texture is stored
top-down.

**`pvr-texture_render` was passing by accident**: it uses `pvr_scene_begin_rtt` too, and dcemu was
ignoring the destination and sending the scene meant for the texture straight to the screen. Its
colour count dropped from 47539 to 2048 when this landed — which is the round trip through an
RGB565 texture, i.e. the *right* number.

Still open: `pvr_rtt_sized` draws two small markers inside the texture, at z=3 and z=4, that do not
appear, while the background (z=0), the inner rect (z=2) and the borders (z=5) do — one level of
overdraw works, two does not. It is the depth test (`GL_ALWAYS` makes them show) but **not
precision**: the context grants 24 bits (`--traza-mem` prints what SDL actually gave) and shrinking
the RTT pass's `glOrtho` range to ±64, which spreads those z values over a quarter of the buffer,
changes nothing. The strips themselves are provably fine — `--traza-mem` prints all four corners,
and the marker's quad is non-degenerate, correctly wound and inside the texture.

**Do not request `SDL_GL_DEPTH_SIZE`.** Asking for 24 alongside the stencil and `BUFFER_SIZE 32`
makes SDL pick a different pixel format; the context grants 24 bits anyway without asking.

**Video RAM has two windows and dcemu models only one.** The PVR sees the same 8 MB through a
32-bit area (`0xA5000000`), where the framebuffer is written, and a 64-bit area (`0xA4000000`),
where it reads textures — the two interleave the two banks differently, so the same byte has two
different addresses. dcemu points both zones flat at one block, which is fine as long as nothing
crosses between them. `pvr-fb_tex` does exactly that: it samples the front buffer as a texture, and
the numbers do not line up — its texture sits at `0x0014E900` while `FB_W_SOF1` reads `0x004A7480`.
So that demo does not need "write the framebuffer to VRAM"; it needs the twin windows, which is a
subsystem touching every texture and framebuffer path. The writeback was written and then removed
once measurement showed it could never land where the texture looks; what stayed is
`volcar_a_memoria()`, the reusable half, which render-to-texture uses.

### Depth

**The TA's z is 1/w — larger means nearer — and it is stored as-is.** The PVR's compare modes are
written in those terms (`GREATER` passes what is nearer), and `screeninit()`'s `glOrtho` maps rising
eye-z to rising depth, so the ordering comes out right with no transformation.

It used to store the **reciprocal**, which inverts it. Two layers still survive that — the top one
wins anyway because the bottom never wrote — but three do not, and the result is that only the first
is visible. `pvr_rtt_sized` draws a background, an interior, two markers and borders on five z
levels and showed the background alone. `libdream-ta` went from 43333 colours to 65227 on the same
change, and `parallax-serpent_dma`'s spheres started occluding each other properly.

**z = 0 is legal and means infinitely far**, which the reciprocal turned into infinity — GL then
clips the vertex outright. `pvr_rtt_sized` submits its background rect with exactly z = 0.

**`glClear` of the depth buffer is masked by `glDepthMask`.** A strip with "Z Write Disable" set —
which translucent geometry always has — leaves the mask at `GL_FALSE`, and then the clear does
nothing and the next scene starts with the previous one's depths. `limpiar_pantalla()` and the RTT
pass both set the mask before clearing.

### Modifier volumes

The PVR decides per pixel whether it is inside the volume and picks between the polygon's two
parameter sets. In fixed-function GL that is the **stencil buffer**, which has to be asked for
(`SDL_GL_STENCIL_SIZE`) — the context comes with none by default.

Two mechanisms, and the header says which: the PCW's **Volume** bit means the vertex carries two
parameter sets and set 1 applies inside; the **Shadow** bit means cheap shadow — one set whose
intensity is scaled by `FPU_SHAD_SCALE` (`0x005F8074`, factor in bits 7-0, enable in bit 8).
`cheap_shadow` asks for 0.5 and the blue inside comes out `0x7F`, which is how you know it works.

**Each affected strip is drawn twice with the stencil test inverted** — outside with set 0, inside
with set 1 — rather than drawing set 0 whole and overlaying set 1. That way every pixel is written
once, which is what matters with alpha blending: overlaying would blend twice.

Two stencil bits, not one: the volume in list 1 affects the opaque list and the one in list 3 the
translucent list, independently (`PLANTILLA_OPACO` / `PLANTILLA_TRANS`).

**It is a union of triangles, not a real volume.** The chip resolves closed 3D volumes by counting
front and back faces; `marcar_volumenes()` just turns each triangle's bit on (instruction 2, "close
excluding", turns it off). That covers what the KOS demos do — a flat screen-space square — and a
shadow projected onto a plane, which is the ordinary use; a closed convex volume seen from inside
would come out wrong.

**When measuring those demos, note they place their geometry with `rand()`**, so the volume
overlaps a polygon in one run and not the next. A two-colour BMP proves nothing; run them a few
times.

Those build up `VertexBuffer[]` and `TriangleStrip[]` (declared — and *defined* — in
`render.h`, which only `graficos.c` may include for that reason). A write to `TA_LIST_INIT`
triggers `cb_tastart()`, which sorts the strips so translucent geometry draws last, sets
per-strip GL state, uploads/binds textures via `get_texture()` (handles twiddled and VQ
formats), and issues `glDrawArrays(GL_TRIANGLE_STRIP, ...)`.

**That sort has to be stable and was not.** `qsort` is not, so two strips of the same list type
came out in an order that depends on the algorithm's internal state — different from one frame to
the next even when the scene was identical. Opaque geometry does not care, the depth test decides;
translucent geometry draws with Z write off and the order *is* the result, so the same frame came
out different every time and the screen flickered. `compare()` now breaks the tie with `index`,
which only grows within a frame, so ties resolve in submission order — which is also what the chip
does inside a list.

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

**A queued ASIC event that no mask covers stays queued.** `check_ints()` used to end with
`REMOVE_BIT(intc_queuemask, ASIC_ACK_A)` — if none of the three masks covered the event at
that instant, it was thrown away. On the chip the `SB_ISTNRM` bit stays set until the guest
acknowledges it by writing the register, and if the guest enables the mask afterwards the
interrupt still arrives. Same mistake the timers had before `intc_revisar_sh4()` (see
`docs/clock-plan.md`), on the ASIC side, and what it loses leaves no trace. The boot ROM is
what exposed it: it enables the Maple DMA, starts it, and waits for the end by interrupt —
enabling the mask *after* queueing it — so it lost that event every time and never polled
the bus again. Only the guest's acknowledge clears a pending event now, in the `SB_ISTNRM`
case of `pvr_write()`.

`wdt.c/h` is the SH-4 watchdog timer: two key-protected registers and an 8-bit up-counter
that either resets the machine (watchdog mode) or raises `EXC_WDT_ITI` (interval mode).
`tmu.c/h` is the three TMU channels. Both take a cycle count from `main_loop()` and keep
their own remainder, each dividing by its own prescaler.

**Everything periodic derives from one constant and one counter.** `DC_CPU_HZ = 199499520` in
`tmu.h` — the same number KOS uses in `kernel/arch/dreamcast/kernel/timer.c`, so both ends
agree by construction. `reloj_total` accumulates CPU cycles and only ever rises; periodic
consumers keep their own mark and compare against it rather than accumulating and subtracting.
It lives outside `core.context` deliberately: the MMU re-execution snapshot restores the whole
context, and the clock must not rewind. `reloj_us()` / `reloj_ms()` convert. The peripheral clock is `CPU/4`, and a TMU channel's `TPSC` divides that again,
so from CPU cycles the divider is 16, 64, 256, 1024 or 4096. `docs/clock-plan.md` has the
history: `timer_check()` used to decrement each `TCNT` once per call and ignore `TPSC`
entirely, which made KOS's millisecond clock run 3.125× slow — `50/16` exactly.

**Peripherals never deliver their own interrupt.** `tmu_tick()` and `wdt_tick()` only set
`TCR.UNF` / `WTCSR.IOVF`; `intc_revisar_sh4()` derives the request from those flags and
delivers it when `SR` allows. That distinction matters: the old code called `intc()` at the
moment of underflow, and `intc()` returns false when `SR.BL` is set or `IMASK` is too high —
so the event was **silently dropped**. On real hardware the request stays asserted until the
guest clears the flag. Dropping them made KOS lose time (`basic/watchdog` measured 8 s where
it asked for 10) and starved the thread scheduler badly enough to hang
`basic/threading/atomics`. Both pass now.

The scanline rate comes from the same constant: `reloj_ciclos_por_linea()` in `tmu.c` computes
`DC_CPU_HZ / (lines × fields per second)` from `SPG_LOAD.vcount` and `SPG_CONTROL` — 6345
cycles for VGA, 6351 for NTSC, 6394 for PAL. `main_loop()` compares against `pvr_ciclos_linea`
(`reg.c`), which `mem.c` recomputes when the guest writes either register. It used to be a
hardcoded 978, so frames came **6.5× too fast** — which meant `cb_tastart()` fired before the
guest had finished submitting a scene, rendering partial frames. That was the cause of the
corrupt band along the bottom of the `tunnel` demo.

The ASIC has **two** status registers. `intc_add()` covers the normal one (`SB_ISTNRM`,
`ASIC_ACK_A`); the GD-ROM's end-of-command arrives on the external one (`SB_ISTEXT`,
`ASIC_ACK_B`), which is what `intc_add_ext()` / `intc_queuemask_ext` are for — `check_ints()`
drains that queue against the `_B` masks. `intc_remove_ext()` exists because reading the
drive's status register deasserts its interrupt line, as ATA requires.

`dma_check()` in `main.c` runs the SH-4 DMAC for real (unit size, source/destination
address modes), but only on channels in auto-request mode: peripheral-requested transfers
on the Dreamcast are done by the ASIC, not the DMAC.

### Disc images

`iso.c` picks a backend by extension. `.iso` is a flat ISO9660 read by
`iso9660_min.c`; `.cdi` (DiscJuggler) goes through `cdi.c`; anything else needs
`USE_LIBCDIO`, which this build does not have.

**A `.cdi` is how virtually every Dreamcast image circulates**, and neither the flat reader nor
libcdio reads one. It stores the track data at the front — one track after another, with
**raw** sectors of 2336 or 2352 bytes rather than 2048 — and the session/track table in a
header at the *end* of the file; the last 8 bytes give the version and where that header
starts.

**The track walk is validated rather than trusted.** The step between sessions varies across
versions and there is no unambiguous way to follow it, so `cdi.c` scans for the per-track
filename and checks each candidate: a real track has `total == length + pregap`, and mode and
sector size in range. A position that is not a track fails that on its own. `cdi_abrir()`
then checks that the tracks together occupy exactly up to where the header begins — if they
don't, something was misread and it says so.

**Two coordinate systems meet here and it is easy to mix them up:**

- **FAD = LBA + 150.** The drive's `CD_READ` speaks FAD; `min_iso_*` speaks LBA.
- **Inside a GD-ROM's high-density area, the ISO9660's own LBAs are absolute disc
  addresses** — the root directory is at 45023, not at 23 — so `min_iso_open_pista()` takes
  the track's base LBA and works in that numbering. For a flat `.iso` the base is 0 and
  everything reduces to what it was.

`iso_es_gdrom()` is true when the data track starts at or past LBA 45000. The drive reports
`GD_DISCO_GDROM` instead of `GD_DISCO_CDROM` for those, and `gdrom_construir_toc_area()`
builds a **separate TOC per density area** — single density below FAD 45150, high density
above. Before that the drive answered the same one-track TOC to both, and the boot ROM
concluded there was no game: *"please insert game disc"*.

### GD-ROM

`gdrom.c/h` is the drive as the hardware sees it: the ATA register block, the status
machine (BSY/DRQ/DRDY/CHECK, interrupt reason, drive state and disc type) and the SPI
packet commands the boot ROM uses. Sectors still come from `iso.c`. `mem.c` routes the two
address ranges to it; nothing else touches it except `dcopcodes.c`, which reuses
`gdrom_construir_toc()` for the syscall hook so both paths report the same disc.

The register map and command codes are checked against two independent sources — the Linux
kernel's GD-ROM driver and reicast's core. Data goes out either as chained DRQ blocks
through the data register or through the G2 DMA (`SB_GDSTAR`/`SB_GDST`), depending on bit 0
of FEATURES at the time of the `PACKET` command.

### Input

`mando.c` reads a host gamepad through **XInput**, loaded at runtime with `LoadLibrary` so
the build gains no dependency and the emulator still starts where the DLL is absent. It maps
almost 1:1 onto a Dreamcast pad — d-pad, four face buttons, Start, and a left stick, plus two
triggers that are **analogue 0-255 on both consoles**, so those pass through untouched. LB/RB
and the right stick have no counterpart and go unused.

`main.c` polls it once per frame and `entrada_leer()` merges it with the keyboard: buttons
with AND (they are active-low, so pressed on either is pressed), axes by whichever is not at
rest, gamepad first. So the keys keep working with a pad plugged in.

The `SDL_JOY*` handling in `main.c` is the 2005 path, behind an `#ifdef JOYSTICK` nobody
defines. It maps buttons by index, which means nothing on a modern pad. XInput is the live
one.

### Maple

The controller bus lives inside `pvr_write()`, in the `SB_MDST` case (`0x005F6C18`): writing
1 walks the command list at `SB_MDSTAR` and answers each transfer in place. Only port A has
a device — a standard HKT-7700 controller — and the other ports get `0xFFFFFFFF`, which is
"nothing here". Two commands are implemented: `Device Request` (1) and `GetCondition` (9).

**The first word of a transfer descriptor is legitimately zero.** It holds the length in
bits 0-7, the pattern in 8-15, the port in 16-17 and end-of-list in bit 31, so a one-word
frame to port A that is not the last of the list leaves all four at zero. The code used to
read that as "`SB_MDSTAR` is wrong" and bail out — and that is *exactly* the boot ROM's
first probe, a `Device Request` to port A, so the BIOS never found the controller. KOS never
shows it because it always marks the last transfer and its single-element lists come out
`0x80000000`. The bail-out now tests the response address, which the guest always puts in
system RAM.

The device descriptor was also nearly all zeros. `function_data[0]` is the field that says
**which buttons and axes the controller has**, and the name fields are space-padded to their
full width on the bus, not NUL-terminated.

`--traza-mem` reports the trigger select, the enable, and one line per (port, command) pair.

### BIOS syscall emulation

With `BIOS_HACKS` enabled in `options.h` **and** `opciones.hacks_bios` set at runtime,
`main()` patches the syscall vector slots (`0x8C0000B0`-`0x8C0000E0`) to point at stub code
it writes at `HACK_BASE`. Three of those stubs are an illegal opcode in the delay slot of an
`RTS`, which maps to `BIOS_HACK` in `dcopcodes.c`:

- `hack_gdrom()` services `GDROM_SEND_COMMAND` (sector reads, TOC) directly from the mounted
  image via `iso.c`.
- `hack_romfont()` services the ROM font syscall. **This one takes its function number in
  `R1`, not `R7`** (see KOS's `syscall_font.s`): 0 returns the font address, 1 takes the
  mutex, 2 releases it. The lock must answer **0** to mean granted.
- `hack_flashrom()` services the flash ROM syscall — info, read, write, delete. The
  convention is `syscall(r4, r5, r6, func)`, so the function number arrives in `R7` and the
  result goes back in `R0`. Write only clears bits (`&=`), because that is what flash can do
  without an erase and the guest's settings blocks rely on it. Without this, KOS's
  `flashrom_get_region()` reported `can't find partition 0` — it asks the BIOS for the
  partition offsets rather than parsing the flash.

The remaining stubs (`SYSINFO`, `UNKNOWN`) are still `RTS` + `NOP`: they return without doing
anything.

Note that `flashrom_get_region()` only recognizes three exact strings — `00000` (Japan),
`00110` (US) and `00211` (Europe). A flash dump holding any other code, such as the `00111`
in `bios/flash.bin` here, makes KOS log `unknown code`. That is KOS being strict, not a
dcemu bug.

With `USE_BIOS_FONT` defined — it is — the font address the stub reports is `0x00100020`,
the real font inside `bios/bios.bin`, and `inicializar_fonts()` compiles out entirely. The
alternative path rasterizes a font into guest RAM at `FONT_BASE` instead. KOS indexes the
font as `direccion + (ch - 32) * 36` for ASCII 33-126 (`bfont_find_char`), which the real
ROM data matches exactly.

**The romfont stub used to be `RTS` + `MOV.L @(0,PC),R0` with the address as a literal, so
it answered the address to all three functions.** That returned a non-zero value for the
lock, and `lock_bfont()` polls `thd_poll(bfont_lock, ...)` until the syscall answers 0 — so
it spun forever. `bfont_draw_ex()` takes that lock before translating the character, which
means *every* `bfont_draw_*` call hung there. The symptom was a demo that painted its
background and then froze with no text and no error: `video/bfont`, `video/multibuffer` and
`video/screenshot` all looked like a broken framebuffer. `video/minifont` was unaffected
because `minifont_draw_str` uses a font built into KOS and never calls the syscall — that
asymmetry is what identifies this bug.

`--bios` turns the hooks off: the stubs are written at `0x8C000100`-`0x8C000500`, which is
exactly where the boot ROM installs itself.

### Synchronous exceptions

`excepciones.c/h` owns the general-exception path, shared by the MMU and the FPU. Two
pieces: `excepcion_entrar()` (save SSR/SPC/SGR, set EXPEVT, jump to `VBR + vector` — it
moved here from `intc.c`, since it is the processor's sequence and not the interrupt
controller's), and the **instruction abort**.

SH-4 general exceptions are re-execution type, but dcemu's handlers mutate registers around
the access (`MOV.L @Rn+`, `FMUL` onto its own destination), so `main_loop()` snapshots state
before each instruction and arms a `setjmp`; whoever detects the fault calls
`excepcion_abortar()`, which does not return. The `longjmp` unwinds both levels of a delay
slot for free, so SPC lands on the branch — which is what the manual wants.

- **`excepcion_vigilar`** decides whether the loop snapshots at all. It generalises the old
  test against `mmu_activa`: 1 if the MMU translates, `SR.FD` is set, or any FPSCR Enable
  bit is on. Zero in everything that runs today, so the fast path costs what it did.
- **The snapshot must include the float banks.** `core.context` holds only *pointers* to
  them, so a plain `memcpy` of the context restores nothing of FR/XF — the MMU path had that
  bug since phase 5 of `docs/mmu-plan.md` and nobody noticed. `excepcion_instantanea_tomar()`
  and `..._restaurar()` copy both banks too.
- **0x800/0x820 are checked in `run()`**, not in `main_loop()`, so delay slots are covered —
  `branch.c` and `rte143()` run those through a nested `core.execute()`, and that is exactly
  what distinguishes the two codes. `en_ranura_retardo` is raised by `EJECUTAR_RANURA()`;
  `main_loop()` clears it on abort because the `longjmp` skips the lowering.
- **`fpu_deshabilitada` is derived only in `excepcion_actualizar_vigilancia()`.** It is a
  copy of `SR.FD` so the dispatcher need not extract a bitfield per instruction; writing it
  anywhere else lets the two drift (`arnes_reset()` assigns `SR = 0` directly).

Testing it needs the whole loop, not just a handler call: `ejecutar_vigilado()` in the test
harness replays snapshot → `setjmp` → restore → `excepcion_entrar()`. End to end,
`demos/fpu-trampa` installs a KOS handler for `EXC_FPU` and `demos/mmu-mapeo` maps a page
and checks the write landed at the physical address. **`SR.FD` cannot be tested from KOS at
all** — its exception entry starts with `sts.l fpscr,@-r0`, an FPU instruction, so with FD
set it would fault forever. True on real hardware too; KOS never uses FD.

**Both MMU demos pass**, and neither was failing because of the MMU. `basic/mmu/nullptr`'s
`kernel panic` is the demo's intended ending (`catchnull` returns `NULL` on purpose) —
reading only the last serial line misclassified it for a whole sweep. `basic/mmu/pvrmap`
died on one `memwrite` that should have been `memwrite_fisico`; see below.

### Support modules

`opciones.c` parses the command line into the global `opciones`. `sistema.c` holds the
three pieces of system state the boot ROM asks for before the drive: the PDTRA/PCTRA cable
handshake, the flash ROM (loaded from `bios/flash.bin` or synthesized) and the RTC. Both
are free of SDL on purpose, so `tests/` can link them. `traza.c` implements `--traza-mem`.

**The flash and the RTC are writable and they persist**, and they have to be: without that
the BIOS asks for the date and time on *every* boot, because it never manages to record that
it is already set.

- **The flash is not memory, it is a chip with a command set** (AMD/Fujitsu compatible).
  Writing a byte means `AA` to `0x5555`, `55` to `0x2AAA`, `A0` to `0x5555`, then the data;
  erasing a sector is a six-write sequence ending in `30`. Programming can only clear bits —
  hence `&=`, the same rule the flashrom syscall hook already followed. `sistema_flash_guardar()`
  writes `bios/flash.bin` back at exit, and only if something changed.
- **The RTC write is stored as an offset against the host clock**, not as a frozen timestamp,
  so time keeps running. It lands in `bios/rtc.txt` — one number, in text, so it can be read
  and deleted by hand.

`gui.cpp` is the only C++ file — a guichan overlay log window, exported to C via
`extern "C"` in `gui.h`. `debug.c` is the in-emulator disassembler and register/memory view
(F12), driven by `DebugMode` (`DBG_STOP`/`DBG_RUN`/`DBG_STEP`). `BFont.c` is a bitmap font
renderer. `log.c` writes `logs/{disasm,memoria,serial,pvr,intc,glop}.txt`.

## Conventions and gotchas

- **`options.h` is the feature switchboard.** Nearly all debug output and several
  behaviours are compile-time `#define` toggles there. Check it before adding a `printf`.
  The write watchpoint used to live there too; it is `--watchpoint=` now, because every
  question cost a full rebuild. Only its report cap stayed behind (`WATCHPOINT_MAX`).
- **Logging is compiled out by default.** `logmsg()` / `logxmsg()` expand to nothing
  unless `LOGGING` is defined in `options.h`; `LOG_FFLUSH` makes it survive a crash.
  Runtime toggles also exist (`filelogging`, keys `l`/`m`/`v`/`r`).
- **`memread`/`memwrite` go through the MMU; `memread_fisico`/`memwrite_fisico` do not.**
  The short names are the guest path on purpose, so instruction handlers translate without
  having to opt in. Emulator-internal accesses that carry already-resolved addresses *and*
  run inside an instruction must use the `_fisico` pair — that means the Maple and GD-ROM
  DMA, the PVR callbacks and the DMAC. Everything else internal uses `0x8C...` (P1), which
  is never translated either way. `mmu.c` owns the TLB and translation (`docs/mmu-plan.md`).
  **One `memwrite` in the Maple DMA was missed** (`mem.c`, the `0xFFFFFFFF` fill for a port
  with no device) and it cost `basic/mmu/pvrmap` a double fault. These only misbehave with
  the MMU on, so nothing else in the tree shows them — grep before assuming they are all
  converted.
- **The ROM font lives at `0x00100020`, a P0 address, so the MMU translates it.** That is
  what the real boot ROM answers — `bios.bin` holds that constant twice and never
  `0xA0100020` — so it is correct, but a guest that maps the low pages shadows its own font.
  `basic/mmu/pvrmap` does exactly that and loses its text; on hardware it would too.
- A failed translation does not return: it `longjmp`s to `main_loop()`, which restores the
  snapshot and enters the exception, so the instruction re-executes. The snapshot is only
  taken when `excepcion_vigilar` — see "Synchronous exceptions" above.
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
