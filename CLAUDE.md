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
file, plus one for the dispatch-table expansion), plus two suites that are not opcodes:
`sistema` (PDTRA handshake, flash synthesis, RTC) and `gdrom` (the drive's state machine,
driven exactly as the boot ROM drives it). They link the real handlers and the real
`opcodes.c`; `tests/memoria_prueba.c` replaces `mem.c` and `tests/dobles.c` replaces the
`graficos.c` / `iso.c` / `intc.c` / `traza.c` symbols the code references, which keeps SDL
and OpenGL out of the link. SDL *headers* are still needed to compile (`opcodes.h` pulls in
`main.h`).

Every row of `opcodes[]` is now implemented — the only one left on `NOIMP` is the
catch-all that covers bit patterns which are not SH-4 instructions. The 16 deviations the
suite originally found have been fixed; `tests/README.md` lists them, plus the three
things that still do not match the manual on purpose (no FPU exception/flag machinery, no
cache behind `OCB*`, and two `LDC ...,SGR` rows of doubtful existence). `LDTLB` and the MMU
are no longer on that list — see `docs/mmu-plan.md`.

A case marked `CASO_XFAIL` documents a known deviation and is expected to fail; if it
starts passing the runner reports `XPASS` and exits non-zero, so the note cannot go stale.

The `cobertura` suite walks `opcodes[]` and fails if any implemented row was never
exercised, so a new instruction gets flagged until it has a test.

End-to-end check after touching the CPU core: `demos/roto/` is a 256-byte rotozoomer that
exercises FSCA, FDIV, FTRC, FLOAT and MUL.L. See its README for how to run it.

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

**`--traza-mem` is the tool for working on the BIOS boot.** It prints each unemulated
address once with the PC that asked for it, and when the last 96 PCs collapse into 64 or
fewer distinct values over four million instructions it dumps the ring, **disassembles the
loop** and prints the registers. `gdrom.c` also reports ATA commands and SPI packets
through it. See `docs/bios-boot-plan.md`.

Keys: F1 fullscreen, F2 log window, **F5 dump the framebuffer**, F9 step, F10 stop,
F11 run, F12 debug view, `p` pause, arrows + `a s d w z` = pad, `q`/`e` triggers,
`y h g j` analog stick, keypad `+`/`-` scroll the memory dump.

**F5 writes `captura.bmp` from video RAM, not from the GL buffer** (`volcar_framebuffer()`
in `graficos.c`), so it shows what the guest drew rather than what GL rasterized. With
`--traza-mem` it also dumps the PC ring and disassembles it.

**On screen-grabbing the window.** The framebuffer path does render — `video/bfont`,
`video/multibuffer` and `video/screenshot` are all legible in a GDI capture of the client
area. Two things make a grab come out black anyway, and both have cost time already:

- **The demo already exited.** KOS clears the screen on the way out, so anything that ends
  by itself (`video/minifont` sleeps 10 s and returns) is black by the time a 14-second
  harness looks. Grab it earlier, and treat a missing window as "finished", not "failed".
- **Guest time runs about 2.5× fast without `--limitar`**, so a guest-side delay elapses
  sooner in wall-clock than the source suggests: `minifont`'s 10 s `thd_sleep` is over in
  roughly 4 s.

To separate "GL never got the image" from "the grab is black", `DibujarFramebuffer()`
`glReadPixels` four points of the back buffer under `--traza-mem` (every 300th frame) and
prints them next to the bytes it read out of video RAM.

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

SH-4 on-chip registers (TMU, DMA, SCIF, INTC, ports) are plain pointers into the `regmem`
block, bound once in `regmem_setup()` (`mem.c`) and declared `extern` in `sh4emu.h`. So
`*TCNT0`, `*DMAOR`, `*IPRA` are both the emulated register and the guest-visible memory.

`pvr_write()` is a large `switch` over individual PVR register addresses using the
`PVR_WRITE_CB_*` macros, which log the access and then invoke a callback in `graficos.c`
(`cb_tastart`, `cb_renderstart`, `cb_param_base`, `cb_fb_r_sof1`, ...). That is how
register writes become rendering work.

### Graphics pipeline

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

Still open: KOS logs `pvr_prim: attempt to submit to unopened list` thousands of times per run in
some demos. Also note `pvr_registered` is `DWORD` in `graficos.c` but `extern int` in `intc.c`.

**`--traza-mem` reports TA activity** for the first three renders: strips, vertices,
end-of-strip count, vertex types, and the min/max of the vertex coordinates. The TA receives
vertices already in screen space, so x must land in `0..width` and y in `0..height`; if not,
the fault is in the vertex layout or the store queues, not in the rasteriser.

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

### Support modules

`opciones.c` parses the command line into the global `opciones`. `sistema.c` holds the
three pieces of system state the boot ROM asks for before the drive: the PDTRA/PCTRA cable
handshake, the flash ROM (loaded from `bios/flash.bin` or synthesized) and the RTC. Both
are free of SDL on purpose, so `tests/` can link them. `traza.c` implements `--traza-mem`.

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
- **`memread`/`memwrite` go through the MMU; `memread_fisico`/`memwrite_fisico` do not.**
  The short names are the guest path on purpose, so instruction handlers translate without
  having to opt in. Emulator-internal accesses that carry already-resolved addresses *and*
  run inside an instruction must use the `_fisico` pair — that means the Maple and GD-ROM
  DMA, the PVR callbacks and the DMAC. Everything else internal uses `0x8C...` (P1), which
  is never translated either way. `mmu.c` owns the TLB and translation (`docs/mmu-plan.md`).
- A failed translation does not return: it `longjmp`s to `main_loop()`, which restores a
  snapshot of `core.context` and enters the exception, so the instruction re-executes. That
  snapshot is only taken when `mmu_activa` — with the MMU off the loop is unchanged.
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
