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
cmake -S . -B build [-DDCEMU_SH4_JSON=/path/to/SingleStepTests-sh4]
cmake --build build --config Debug --target dcemu_tests dcemu_sh4json
ctest --test-dir build -C Debug --output-on-failure
```

`tests/` holds unit tests for every implemented row of `opcodes[]` (one suite per handler
file, plus one for the dispatch-table expansion), plus suites that are not opcodes:
`sistema` (PDTRA handshake, flash synthesis, RTC), `gdrom` (the drive's state machine,
driven exactly as the boot ROM drives it), `ta` (the TA parameter format — the
classification table and the reassembly of the 64-byte parameters), `mmu`, `wdt`, `tmu`,
`vram` (the two windows of PVR video RAM — the numbering conversion and the 4-byte bank
interleave) and `ubc` (the hardware breakpoint controller, driven with the same register
sequences KOS's driver uses). They link the real handlers and the real
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
normal is exact. Still missing: the I cause on its own, and the qNaN value the chip
generates (`H'7FBFFFFF`, not the host's). **DN and RM are emulated now** — see the
SingleStepTests section below.

**Enable and the three FPU exceptions are wired too** — 0x120 when a cause meets its Enable
bit, 0x800/0x820 when `SR.FD` is set. See "Synchronous exceptions" below for the mechanism
and `demos/fpu-trampa` for the end-to-end test.

A case marked `CASO_XFAIL` documents a known deviation and is expected to fail; if it
starts passing the runner reports `XPASS` and exits non-zero, so the note cannot go stale.

The `cobertura` suite walks `opcodes[]` and fails if any implemented row was never
exercised, so a new instruction gets flagged until it has a test.

End-to-end check after touching the CPU core: `demos/roto/` is a 256-byte rotozoomer that
exercises FSCA, FDIV, FTRC, FLOAT and MUL.L. See its README for how to run it.

### The core against SingleStepTests/sh4

`tests/singlestep.c` builds a second binary, `dcemu_sh4json`, that runs the real core
against [SingleStepTests/sh4](https://github.com/SingleStepTests/sh4): 233 encodings × 500
cases with **full random initial and final state**. The suites above were written by reading
the manual, so they cover what one remembers to look at; these are the opposite, and they
found eleven more things. **`docs/sh4-conformidad.md`, "La segunda pasada", is the list**;
the headline ones are that `FPSCR.RM` was ignored (RM = 01, truncate, is the reset value and
what KOS leaves, i.e. the mode *everything* runs in), that `FPSCR.DN` was ignored, that FMAC
rounded twice where the manual rounds once, that `SLEEP` advanced PC (so it was a NOP), that
`UpdateSR()`'s sentinel was `0xFFFFFFFF` — exactly what `LDC Rm,SR` with all ones leaves, so
that write silently did nothing — and that FSCA took the whole of FPUL as the angle instead
of its low 16 bits.

The data is 92 MB and not in the repo: `git clone https://github.com/SingleStepTests/sh4.git`,
then `-DDCEMU_SH4_JSON=` at configure time or the env var of the same name at run time.
Without either, the binary exits 77 and CTest marks the test **skipped**. No need to run
their `transcode_json.py` — the runner reads the binary format directly.

**They are not the manual: they came out of Reicast's interpreter.** Where the two disagree
the manual wins, and the 3221 disagreements are classified one by one and counted apart —
neither green nor red — with the manual quote that settles each. The big ones: Reicast does
not write FPSCR's Cause/Flag at all, does not mask FPSCR to `0x003FFFFF`, does not implement
TRAPA, runs RTE's delay slot before writing SR, does not saturate an out-of-range FTRC, and
reads `Rm` after the shift in `DIV1` with `n == m`. Three cases are discarded: the fourth
instruction turns out to be a branch, the generator stops at four fetches and the final state
is mid-instruction.

Floats are compared **bit-exact**, on purpose — that is what exposed the rounding mode, whose
differences were one ulp — with two documented exceptions: any NaN equals any other (the
repository's own `compare_floats()` rule), and FIPR/FTRV/FSRRA/FSCA are compared against an
error bound, the manual's own in 6.6.1 for the first two. `tests/README.md` has the rest.

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
| `--captura-audio=ARCHIVO` | vuelca a un `.wav` lo que el mezclador del AICA produjo. Es **la medida** del sonido, no lo que hizo la tarjeta |
| `--sin-audio` | no abrir la tarjeta de sonido. El AICA se emula igual y `--captura-audio` sigue funcionando |
| `--sin-aica` | no emular el AICA: ni el ARM, ni los canales, ni los temporizadores. Para aislar una regresión |
| `--watchpoint=D[:T]` | informa cada escritura que toque `D` (hex), de `T` bytes, con el PC y el PR |
| `--watchpoint-lectura=D[:T]` | lo mismo para las lecturas: una línea por cada PC distinto que mire `D` |
| `--traza-desde=PC[:N[:K]]` | desensambla las `N` instrucciones que siguen a la llegada a `PC`, saltándose las `K` primeras, con los registros que cambian. Necesita `--traza-mem` |
| `--desensamblar=D:N` | al salir, desensambla `N` instrucciones desde `D`. Repetible |
| `--volcar=D:N` | al salir, vuelca `N` bytes desde `D` en hexadecimal. Repetible |
| `--salir-tras=N` | sale solo a los `N` segundos de tiempo **emulado** |

Los de diagnóstico son los seis últimos y **todos sus números van en hexadecimal** (salvo
los segundos de `--salir-tras`).

**`--traza-mem` is the tool for working on the BIOS boot.** It prints each unemulated
address once with the PC that asked for it, and when the last 96 PCs collapse into 64 or
fewer distinct values over four million instructions it dumps the ring, **disassembles the
loop** and prints the registers. `gdrom.c` also reports ATA commands and SPI packets
through it. See `docs/bios-boot-plan.md`.

Not every loop it reports is a hang — a `memset` over 600 KB and a wait for vsync both trip
the same heuristic. Read the disassembly, not the fact that it fired.

It reports each unemulated address once, but the dedup is by address: a guest that runs off
a stray pointer walks millions of *distinct* ones, so there is a cap of 4096 reports. Without
it the log went to gigabytes and the window stopped responding.

**`--watchpoint=DIR[:TAM]` is the other tool**: a write watchpoint that reports every
write touching a given address, with the PC and PR that did it. The hook is in
`memwrite_fisico()` (`mem.h`) — the one place *every* write goes through, guest and
internal alike — and the implementation is in `traza.c`. It costs a compare against zero
per write when off. It is what answers "who writes this variable", which is the question the
BIOS boot keeps raising; `docs/bios-boot-plan.md` walks through the two cases it solved.

**`--watchpoint-lectura=DIR[:TAM]` is its twin** and answers the other half: who *reads* it.
It hangs off `memread_fisico()`, and reports once per distinct PC — a string compare passes
through the same instruction a hundred times. It is what identifies, in one run, the code
that evaluates something the drive just delivered.

**`--traza-desde=PC[:N[:K]]` is how you read a decision.** It disassembles the N instructions
after the guest reaches PC — skipping the first K arrivals, because the ROM goes through the
same code twice, at power-on and after a reset — printing **only the registers that changed**.
Printing all 16 per instruction is unreadable and does not fit; the changed ones show the data
flow. The ring says where it ended up spinning; this says how it got there, which is what you
need to follow a branch in the boot ROM. `traza_arrancar()` arms the same thing from inside
the emulator, and `gdrom.c` uses it with `DCEMU_TRAZA_ATA=cmd:N` to watch what the guest's
driver does with what the drive just answered.

**`--desensamblar` and `--volcar` are how you read the boot ROM.** Its code lives in RAM —
it copies itself there and is *not* at the same address inside `bios.bin` — so the only way
to read a routine or one of its tables is from inside the emulator. Both print at exit,
which is why **`--salir-tras=N` matters**: it leaves through the same path as closing the
window, so `traza_resumen()` runs. Killing the process from outside takes the disassembly
and the dump with it.

**`MOV.W @(disp,PC)` and `MOV.L @(disp,PC)` do not resolve their literal the same way,
and `disasm()` used to treat both as long.** The word form scales `disp` by 2 and does *not*
align the PC; the long form scales by 4 over `PC & ~3`. Sharing one operand type made the
disassembler name a literal that was not the one being read — off by an arbitrary amount,
and plausible-looking, which is the expensive kind of wrong: it is what made the `.cdi` boot
read as a comparison against `0x1ab0` when the guest was really comparing against `0x3030`.
They are separate operand types now (`OP_T_AT_DISP_PC_RN_W`), so the two rows in `opcodes[]`
cannot drift. `MOVA` still prints the raw `disp` rather than resolving it.

**A crash of the emulator reports the guest's state instead of vanishing**
(`traza_caida_instalar()`, installed first thing in `main()`). A guest that jumps into the
void executes whatever is there and sooner or later takes dcemu down with it; without this
the process simply disappears, SDL takes `stderr` with it, and the one datum that says
where it derailed — the guest PC — is gone. The handler prints the host exception, then the
guest's registers, then the PC ring with its disassembly, then the `--volcar`/`--desensamblar`
ranges: the same thing exiting normally would have printed, in that order so a second fault
while disassembling broken memory still leaves the important part out. It does not try to
continue, is not a host-bug catcher, and costs nothing while nothing crashes. Windows uses
`SetUnhandledExceptionFilter` (SDL does not override it) and everything else `signal()`,
re-raising with the default handler so a core still gets dropped.

**Where the BIOS boot stands: it boots, and the intro animation displays.** With `--bios`
it plays the swirl animation — the embossed spiral on the `0xBFBFBF` background with its
orange trail — and reaches the menu; from there Play, File, Music and Settings all work.
What was missing for the *boot* was three things, all of the same shape — something the
guest asks for that dcemu accepts without doing anything and without saying anything (see
`docs/bios-boot-plan.md`, "Tercera corrida"):

- **the CH2 DMA** (`SB_C2DSTAT`/`SB_C2DLEN`/`SB_C2DST`, `0x005F6800-08`) — see below;
- **a Maple descriptor whose first word is legitimately zero** — see the Maple section;
- **ASIC events being dropped when no mask covered them** — see "Interrupts and timing".

What was missing for the *animation* was two more, both found by the ROM and by no KOS
demo: the PVR **background plane** and the true size of the TA's **Polygon Type 1** header
— see "The background plane" and the `ta.c` note in the graphics pipeline section.

**Hito C is reached: the boot ROM boots a game off a `.cdi` on its own.** With `--bios` and
the image presented as what it is — **a selfboot CD, no `DCEMU_COMO_GD`** — the ROM walks the
whole path: it recognises the disc, reads IP.BIN from the start of the data track, the volume
descriptor and the root directory, finds `1ST_READ.BIN`, loads all 1,468,208 bytes of it and
jumps. The PC lands inside the executable, around `0x0C14xxxx`. That is the same place the
long-standing path reaches (no `--bios`, through the syscall hooks), so what the game does
from there was a separate problem — **and it is solved for Crazy Taxi: hito D is reached**
(2026-08-01). On the hooks path both rips run — loading screen, VMU warning, Start works,
title screen, 3D attract mode. What unblocked it was the disc type answered by
`GDROM_CHECK_DRIVE` (see "BIOS syscall emulation") plus `SYSINFO` function 3; Virtua Tennis
and Capcom vs. SNK still stop earlier, in the SDK's common startup
(`docs/pendientes-plan.md`, vía A).

Two things had to be right before the drive fixes below mattered:

- **The `bios.bin` matters, but not the way it first looked.** The one in the repo is
  `KABUTO Ver.1.004 ... 1998` (string at `bios.bin+0x7cc`), from before MIL-CD; with it the
  ROM reads 17 sectors at FAD 45150 and nothing else. **With 1.01d (1999) the path opens** —
  so "impossible with this ROM" held only for 1.004. Revisions are told apart at a glance:
  1.022, which dropped MIL-CD, has one boot table fewer than the earlier ones. Watch the
  flash: virgin dumps make the ROM ask for the date on every boot, and **patching the region
  code by hand breaks that partition's checksum**, with the same effect. Let the ROM
  configure the date once instead — dcemu saves the flash and `bios/rtc.txt` on exit.
- **Present the disc honestly.** `DCEMU_COMO_GD` announces the data track at FAD 45150 so the
  disc looks like the GD-ROM it was ripped from; `iso_read_sector()` then translates only the
  17 boot-area reads back to the track's real start and leaves the ISO9660's own LBAs alone.
  That got further than nothing while REQ_SES was broken, but it is the **wrong branch**: the
  ROM takes its GD path, and the file it finds sits below a threshold that path refuses, so
  it calls `menu(1)` and returns to the menu. **Left alone, the ROM takes the MIL-CD branch**
  — disc type CD-ROM/XA — which loads its own handler at `0x8CE00000`, enumerates the
  sessions and boots the last one. That is the branch a real console uses for these discs.

**Where the ROM decides, and what it looks at.** All of it disassembled out of RAM with
`--traza-desde`, because the ROM's code is not at the same address inside `bios.bin`:

- `0x8C000AE0` is the top-level boot routine. `*(u32*)0x8C0000E4` is the reboot reason (1 =
  "came back from `menu(1)`", which is what sends it to the menu). `*(u32*)0x8C00004C` is the
  disc type: `0x20` picks the MIL-CD branch at `0x8C000B44`, anything else the GD branch at
  `0x8C000B6C`.
- `0x8C0004F4` validates the IP.BIN header — hardware ID, maker ID, the region string against
  the flash's region index — and leaves the boot filename pointer at `GBR+0x9C`.
  `*(u8*)0x8C000024` is bit 0 of the hex digit at IP.BIN offset `0x3E`.
- `0x8C000D40` reads the volume descriptor, then the root directory, and walks the ISO9660
  records comparing each name against that filename. On a match it computes the FAD and the
  sector count and, **unless `fad >= 0x6DDD0` or `*(u8*)0x8C000024 == 2`, calls `menu(1)`** —
  and `== 2` is exactly what the MIL-CD branch sets. That is the gate the GD presentation
  cannot pass.
- The ROM leaves the load address at `0x8C0000F8` (`0x8C010000`), the FAD at `0x8C0000F0` and
  the sector count at `0x8C0000F4`.

**Five things in the drive were wrong, and each hid the next.** They are worth listing
because four of the five are the same shape as everything else in this project — something
the guest reads that dcemu answers without meaning it:

- **`REQ_SES`'s answer was shifted one byte.** Its second byte is reserved and was missing, so
  the session count landed in `[1]` and the FAD's high byte in `[2]`. The ROM's driver hands
  the caller precisely byte `[2]`, so the MIL-CD handler read **5** — the lead-out's high
  byte — where it wanted **2 sessions**, and gave up. This is what kept the whole CD branch
  shut.
- **`CD_READ` rejected everything above FAD 45150 on a non-GD-ROM.** A CD has no high-density
  *area*, but it does have those *sectors*: a 700 MB CD reaches past FAD 358000, and that is
  where these conversions put `1ST_READ.BIN`. The limit is the lead-out now, the same number
  `REQ_SES(0)` reports.
- **The G2 DMA ended the command on its first burst.** From the second on it found "nothing
  pending" and moved nothing, while the guest kept programming destinations and watching the
  DMA finish. The IP.BIN bootstrap brings the executable in **scrambled**, as 45882 bursts of
  32 bytes to scattered addresses — that is how it descrambles it on the fly — so only the
  first 32 bytes arrived and the other 1.4 MB was garbage.
- **`SB_GDSTARD` and `SB_GDLEND` (`0x005F74F4`/`0x005F74F8`) did not exist.** The ROM's driver
  reads `SB_GDLEND` to know how much of the read has landed and stores it in its command
  block; it was reading the control block's backing store, i.e. uninitialised memory.
- **The `ABRT` bit of the ERROR register.** `fallar()` wrote only the sense key. The bootstrap
  ends the load without taking the last sector's padding — it reads the file's length, not the
  717 sectors — and closes with an ATA `NOP`; the driver reads ERROR, tests `& 4` and, with no
  ABRT, treats the abort as not having happened and leaves the command "transferring" forever.

**Which images boot.** What the drive says about the disc picks the ROM's branch, so `iso.c`
has to get the disc right: a `.cdi` **is a CD** (`iso_es_gdrom()` is only true under
`DCEMU_COMO_GD` now), two sessions are told apart by **the gap** between tracks (inside a
session they are contiguous but for the 150-sector pregap; closing one and opening another
costs about 11400), and the TOC is **not** split into density areas unless the disc really is
a GD-ROM. `iso_init()` lists every track with its LBA, size, mode and file offset — that
listing is the first thing to look at.

Both selfboot layouts work, and the ROM finds `1ST_READ.BIN` on all of them:

| imagen | pista 1 | pista 2 | formato |
| --- | --- | --- | --- |
| Crazy Taxi (DCRES) | LBA 0, 302 sectores, **audio** | LBA 11702, 346490, datos | audio/datos |
| DCDoom | LBA 0, 302, **audio** | LBA 11702, 18487, datos | audio/datos |
| Crazy Taxi (USA) | LBA 0, 33600, datos | LBA 45000, 306552, datos | datos/datos |
| Virtua Tennis (USA) | LBA 0, 33600, datos | LBA 45000, 314830, datos | datos/datos |
| Capcom vs. SNK (USA) | LBA 0, 33600, datos | LBA 45000, 314569, datos | datos/datos |

**The TOC's byte order on the wire is not the struct's.** Each entry goes out with the
**control byte first** and the FAD behind it big-endian, like every other SPI response;
`struct TOC` holds it the other way round — control in bits 31-28, FAD in 23-0 — because that
is what the *receiver* wants: the ROM's GD-ROM driver reverses each word before handing it to
its caller, and that reversed form is the one KOS reads (`TOC_CTRL`, `TOC_LBA`). The syscall
hook skips the driver, so there the struct goes out as-is; `cmd_get_toc()` swaps. Sending it
unswapped makes the guest read the FAD where it expects the control byte, and the ROM's
MIL-CD handler — which checks at `0x8CE003B6` whether the first track is data — rejected
perfectly good discs. That was the whole reason the data/data layout would not boot.

Note dcemu also boots these games **without** `--bios` — it loads `ip.bin` and
`1st_read.bin` from the image directly, and with a `.cdi` that path issues no SPI packet at
all: everything goes through the syscall hooks. Both paths now reach the same place.

`--traza-mem` prints the PC and PR of every SPI packet, what each `REQ_SES` answered, and
the disc format the drive settled on.

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
F9 step, F10 stop, F11 run, F12 debug view, `p` pause, **`f` toggle the FPS counter** (in
the window title, frames presented per real second; on by default — `fps_marcar_cuadro()`
in `graficos.c`), arrows + `a s d w z` = pad, `q`/`e` triggers, `y h g j` analog stick,
keypad `+`/`-` scroll the memory dump. A gamepad works too — see "Input".

**F5 writes `captura.bmp` from video RAM, not from the GL buffer** (`volcar_framebuffer()`
in `graficos.c`), so it shows what the guest drew rather than what GL rasterized. With
`--traza-mem` it also dumps the PC ring and disassembles it.

**F6 writes `captura-gl.bmp` from the GL buffer** (`volcar_gl()`), which is the other half:
3D output never goes through video RAM in dcemu, so for a PVR demo F5 is always black.
`--captura-gl=ARCHIVO` does the same automatically before every swap, so the file holds the
last frame when the emulator exits — pair it with `--salir-tras` and a sweep needs no window
at all. With `DCEMU_CAPTURA_TODAS=1` in the environment every frame goes to its own
numbered file (`f0000-ARCHIVO`, ...) instead of overwriting — it is how you inspect an
animation frame by frame, and what let the boot ROM's intro be diagnosed.

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
distinguishes punch-through on the chip is that it discards on alpha — **and that discard is
implemented now**: strips from list 4 draw with `GL_ALPHA_TEST`/`GL_GEQUAL` against
`PT_ALPHA_REF` (`0x005F811C`, bits 7-0) **with a floor of half an 8-bit step**, which is what
lets a cut-out billboard write Z like the opaque list. Without the test, the alpha-0
background of Crazy Taxi's tree billboards rendered as solid black boxes. The compared alpha
is the modulated one, so a texture with no alpha channel still cuts by the vertex's.

Two mistakes were made tuning this, and both are worth keeping: (1) a **strict** `GL_GREATER`
looks equally plausible and breaks the world — Crazy Taxi's in-game geometry is punch-through
with alpha 1.0, and any session where the game raises `PT_ALPHA_REF` to 255 then discards
*everything* (the whole city dropped to its untextured backing: white streets and buildings).
`GEQUAL` with the epsilon floor passes alpha ≥ ref and still kills exact zero. (2) The grey
pills behind Crazy Taxi's menu items looked like this bug and are **not a bug at all**: real
hardware draws them exactly like that (checked against The King of Grabs' Dreamcast
captures) — measure against a reference before chasing a "wrong" screen. The game leaves
`PT_ALPHA_REF` at 0 on some screens and writes 0x17 on menus, so the register cannot be
assumed constant. `conio-basic` (one punch-through quad per glyph) stays at its reference
2742 pixels.

### Texture formats

`taPolyModifier()` maps the texture control word's `pixelformat` onto a GL format triple;
`get_texture()` untwiddles and uploads. ARGB1555, RGB565, ARGB4444, YUV422 and the two
palette formats are handled, and VQ and stride have their own paths.

**A mipmapped texture stores its levels from 1×1 up, so the big level is NOT at the texture
address** — the level of side 2^n starts at `6 + 2·(4^n − 1)/3` bytes in 16-bpp units (the
table in KOS's `pvrtex`, `MipMapOffset()`), scaled by the format: VQ indices are one byte per
2×2 block (an eighth, counted after the codebook, which stays at the base), 4-bpp palette a
quarter, 8-bpp half. The TCW's mipmap bit used to be parsed and only logged; decoding from
offset 0 reads the small levels as if they were the image, which comes out as structured
block noise. That was Crazy Taxi's cable car, ground and buildings — while the taxi, road
and HUD (no mipmaps) decoded fine, which is what pointed at the bit. dcemu still uploads
only the big level (no GL mipmap chain), so heavy minification shimmers where the chip
would switch levels; no KOS demo sets the bit (measured: the texture-sensitive subset is
byte-identical), a game does.

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

**The cache held 10 entries, lived one scene, and had no bounds check** — `get_texture()` wrote
`cached_textures[cur_tex_count]` unchecked, so the 11th distinct texture of a scene wrote past
both arrays and bound garbage GL ids, which in the compatibility profile silently create new
texture objects that alias each other. No KOS demo exceeds a handful; a game scene uses
hundreds, and the symptom was Crazy Taxi's floor sampling the sky and textures *rotating*
across objects as uploads landed on each other.

**The cache is persistent now: 1024 entries that live across scenes and invalidate by
generation, not by clearing.** `vram.c` keeps a generation counter per 8 KB page of video RAM;
the two write funnels bump them (`vram64_escribir` marks itself, `video_write`'s flat 32-bit
path marks before storing), `pvr_write` keeps a palette generation for `0x005F9000` and
`PAL_RAM_CTRL`, and each cache entry records the generation sum of its footprint (the exact
range `vram64_leer` gathered) plus the palette generation when indexed. A lookup that finds
the key compares generations: unchanged → the GL texture already uploaded serves; changed →
re-decode into the same slot. Lookups go through a hash on the texture address
(`tex_hash[]`) — with 1024 entries always full, the linear scan at ~2600 strips per frame
cost more than the cache saved. Full, it replaces round-robin. The CPU copy is freed right
after `glTexImage2D`: GL owns the pixels. `pvr-fb_tex` (samples its own framebuffer, needs
per-frame invalidation) and `pvr-palette-wormhole` (animates the palette) are the two demos
that prove the invalidation, and both stay byte-identical.

**Mipmapped textures upload the guest's own levels** — they are right there in the gathered
block (smallest first, the pvrtex offset table), they are the artist's (games bake LOD tricks
into them), and decoding them is cheaper than `GL_GENERATE_MIPMAP` regenerating on every
re-upload of a streaming texture: Crazy Taxi's attract, the worst case, times *faster* than
the no-mipmap baseline. Three per-level decoders live in `get_texture()` (VQ via the
codebook, twiddled 16bpp, palette); BUMP and YUV keep `GL_GENERATE_MIPMAP`. **A VQ chain
stops at 2×2** — the 1×1 index shares a byte with it — so `GL_TEXTURE_MAX_LEVEL` clips the
chain; without the clip the texture is incomplete and GL samples it white. The MIN filter
picks mipmap modes in `aplicar_filtros()` when the strip's texture carries the bit.

**Strips with zero vertices are skipped at draw** — a game's shadow headers leave hundreds of
empty end-of-strip records per scene that drew nothing but paid the full GL state churn.

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
handler, restriction}`. **`SR.RB` says which bank *should* be in `registers[0..7]`; `core.context.banco_activo`
records which one *is*.** They are not the same thing, and conflating them cost a game.
There are two entries — `UpdateSR_ya_escrito()`, for the caller that wrote `SR` itself
(exception entry, interrupt entry and `TRAPA`, which set MD/RB/BL by hand), and `UpdateSR()`,
which takes the new value — and the first used to **swap unconditionally**. With
`RB` already 1 — an exception taken from inside another, which is what any handler that lowers
`BL` to allow nesting does — entry set `RB=1` again and swapped anyway: the nested handler saw
bank 0 as its own, and the interrupted code came back from the `RTE` with the other bank's
`R0-R7`. The `RTE` did not undo it, because that path *does* compare and `SSR.RB == SR.RB`.
Both paths now compare against `banco_activo`, which `swap_registers()` is the only thing to
move. The field lives **inside** `core.context` on purpose: the MMU snapshot restores the
register array, so the bank state has to travel with it. `reset()` and `arnes_reset()` set it,
because both assign `SR` directly without going through `UpdateSR()`.

**Those two entries used to be one function told apart by a sentinel, and the sentinel was
`0xFFFFFFFF`** — which is exactly what `LDC Rm,SR` with `Rm` all ones passes. That write was
read as "the caller already wrote SR" and **SR was left untouched**. Splitting them is what
removed the trap; SingleStepTests is what found it.

**Writing SR is not a plain assignment**: `sr_normalizar()` keeps only `0x700083F3` — the mask
the manual puts in `LDC Rm,SR` itself — and clears `RB` when `MD` is 0, because `RB` only
exists in privileged mode ("in user mode, R0-R7 always refer to bank 0"). It lives in
`UpdateSR()` and not at the call sites because it is the rule for *writing SR*, not the rule
for one instruction: `RTE` and `LDC.L @Rm+,SR` were missing it, and `RTE` copies `SSR`, which
`LDC Rm,SSR` can fill with anything.

No KOS demo shows this — KOS takes its interrupts from `RB=0`, so the swap is always a real
change. Virtua Tennis is what exposed it: it lost the index of a callback table across a nested
interrupt, called through a null pointer, landed at address 0 — which is the boot ROM — and ran
through the low system block until it hit the bytes that decode as `TRAPA #23`. Suite
`syscontrol`, case `trapa_desde_el_banco_1_no_vuelve_a_cambiar`.

`initopcodes()` expands it into **four** such tables
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

**`0x005F689C` is `SB_SBREV`, the Holly's System Block revision, and a retail console
answers `0x0B`** (reicast initializes it so). Third of the identification-register family
after `REVISION` and `SB_G1SYSM`, and the worst of the three: with no read case the read
fell through to `control_mem`, which was a malloc never cleared, so it answered recycled
heap garbage that **varied per process instance**. Katana's startup compares it against 8
(Crazy Taxi: PC `0x0C073944`, 118 ms after power-on) to pick its init path, and on the
side that reads < 8 the game's RAM→VRAM texture upload phase never starts — the
intermittent "white world" of `docs/pendientes-plan.md` A.5 was this coin toss at process
start, not a drive race. Every block in `inicializar_memoria()` is calloc now: a register
with no case must answer its reset value, not the heap's history, and zeroed backing is
also what makes two runs of the deterministic reproducer byte-identical (the only line
that differs is the wall-clock one in the exit summary). `DCEMU_TRAZA_EN_MS=N[:M]` in the
environment is the tool that found it — per-millisecond checkpoints of PC and registers
for the first 200 ms, plus the `--traza-desde` instruction trace armed on crossing an
emulated *instant* rather than a PC: what you need when two runs diverge and nothing says
where.

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

**Where the PVR stands: no KOS demo fails on the PVR anymore.** `pvr-fb_tex`, the last one, took
the twin video-RAM windows (see below) *plus* the framebuffer writeback *plus* the depth-sign fix
in `glOrtho` — and that last one also resolved the `pvr_rtt_sized` markers, the residue this
section used to list. Every texture format, every vertex type, sprites, modifier volumes and
render-to-texture are in. The one documented residue left is `tsunami-genmenu`, whose geometry
arrives correctly but lands at y 631..1458 on a 480-line screen and never scrolls in — guest-side,
since dcemu does not touch vertex coordinates.

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

**`ta.c` exists because not every TA parameter is 32 bytes.** Headers carrying *two* face
colors, vertices with floating-point color, all six two-volume textured vertices, both sprite
vertices and the modifier-volume vertex are 64, and they arrive as *two* blocks — one per store
queue. Dispatching each block separately reads the second half as a parameter control word, and
since its first word is usually a float the para-type comes out of the garbage: when that float
is `0.0` the type is **0**, which is end-of-list, so a list closes that the guest never closed.
`ta_clasificar()` is the table — PCW → global parameter type and the vertex type it leaves in
force — and `ta_procesar_bloque()` joins the halves before dispatching. Both `taPolyModifier()`
and the block assembler use that one table, so they cannot drift. `ta.c` is free of SDL and GL
on purpose, like `sistema.c`, so `tests/` links it for real.

**Polygon Type 1 is 32 bytes — its single face color fits in words 4-7.** Only Type 2 (face
*and* offset colors) and Type 4 (one face color per volume) are 64, with the colors in words
8-11 and 12-15. dcemu had Type 1 as 64 with the color read from words 8-11 — the Type 2
layout — and the `ta` suite had the same misreading baked in, so it never objected. No KOS
demo submits a Type 1; the boot ROM does: its swirl trail and logo go out as intensity-mode-1
headers, the assembler glued each header to the first vertex behind it, the face color came
out of that vertex's coordinates (negative alpha: invisible) and every quad lost a vertex.
That is what kept the intro animation's color pass blank.

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

The `pvr_rtt_sized` markers at z=3 and z=4 that this section used to list as "still open" were the
depth sign in `glOrtho` — see "Depth" below. With that fixed all five layers show.

**Do not request `SDL_GL_DEPTH_SIZE`.** Asking for 24 alongside the stencil and `BUFFER_SIZE 32`
makes SDL pick a different pixel format; the context grants 24 bits anyway without asking.

### The two windows of video RAM

**The PVR sees the same 8 MB through two windows that interleave its two 4 MB banks differently.**
The 32-bit area (`0x05000000`/`0xA5000000`) sees them contiguous — the bank is bit 22 — and is
where the framebuffer lives; the 64-bit area (`0x04000000`/`0xA4000000`, plus the TA texture FIFO
at `0x11000000`) alternates them every 4 bytes — the bank is bit 2 — and is where textures are
read. Same byte, two addresses. `vram.c/h` owns the conversion (SDL-free, so `tests/` links it;
suite `vram`), the block stays in 32-bit numbering, and every access through a 64-bit window
converts:

- `video_read`/`video_write` (`mem.c`) dispatch by top byte: zones `0x04`/`0x11` convert, `0x05`/
  `0x13` stay flat. Uploads by store queue or CH2 DMA come through here, so they convert alone.
- `get_texture()` gathers the texture into a contiguous staging buffer with `vram64_leer()` before
  decoding — the TCW's address is 64-bit numbering. One insertion point; every decoder unchanged.
- Render-to-texture **scatters** with `vram64_escribir()`: bit 24 of `FB_W_SOF1` means "write
  through the 64-bit path" and KOS passes the texture address as-is.
- The TA's YUV converter writes its output the same way: `TA_YUV_TEX_BASE` is a texture address.

`pvr-fb_tex` is the demo that forced all of it: it samples its own front buffer as a texture
(texture at `0x0014E900` = FB at `0x004A7480`, exactly ×2 with the bank in bit 2), relying on the
hardware interleave to produce "two correct pixels, two garbage" which it reconstructs with a mask
and two DSTALPHA passes. It needs the windows **and** the framebuffer writeback: dcemu renders 3D
in OpenGL, so the front buffer does not exist in VRAM unless written back. The writeback
(`volcar_escena_a_framebuffer()`) is armed by `get_texture()` the moment a texture's converted
address falls inside the frame the PVR writes or displays, and from then on every scene is read
back with `glReadPixels` and stored through the 32-bit window — before that it costs nothing, so
no other demo pays for it.

**Which window the CH2 DMA writes through is not implied by the address — the guest picks it
with `SB_LMMODE0`/`SB_LMMODE1`.** This is settled by Sega's own *Dreamcast/Dev.Box System
Architecture*, §8.4.1.1: the address in `SB_C2DSTAT` names the **path** (`0x10000000` polygon,
`0x10800000` YUV converter, `0x11000000` direct texture, with `0x12`/`0x13` as their images),
and of that last one it says *"When transferring data to the texture memory via the TA FIFO
buffer and Direct Texture Path, either 64-bit access or 32-bit access can be specified by
setting the SB_LMMODE0 and 1 registers."* `SB_LMMODE0` (`0x005F6884`) governs
`0x11000000-0x11FFFFFF` and `SB_LMMODE1` (`0x005F6888`) its image at `0x13000000`; bit 0 is
**0 = 64-bit (default), 1 = 32-bit**. Both already had backing store in `control_mem` — the
guest's writes were arriving all along, nobody read them.

That one register explains two measurements that could not both be true otherwise. mame4all
dumps whole frames to `0x11000000` — 275 transfers of 614400 bytes in six seconds, without
touching the TA once — and displays them as the framebuffer, which is read in 32-bit
numbering: it sets `SB_LMMODE0` to 1 and needs the write flat, or the frame splits across the
banks and comes out duplicated across the width and squashed to half the height. The boot ROM
uploads its **textures** through the same `0x11000000` — `0x11413000`, `0x1141b000`,
`0x1151b000`, 8 KB to 1 MB each — with `SB_LMMODE0` at its default 0, and needs them
interleaved, because `get_texture()` reads with `vram64_leer()`. Deciding by address instead
cost the boot ROM every glyph in its menu and date panel for a while; see
`docs/pendientes-plan.md`, C.7.

**The store queue is a different path and always interleaves**: `pvr-strided_texture` uploads
its texture through the same window in 32-byte bursts and depends on it — forcing that path
flat takes it to black, 240000 non-black pixels to zero. Note the same doc sentence says
"via the TA FIFO buffer", which store-queue writes to `0x11000000` also go through, so
`SB_LMMODE0` may well govern them too; dcemu hardcodes the interleave there, which is what
`SB_LMMODE0 = 0` — the default, and what KOS leaves — would give anyway. Untested either way.

**`0x06` and `0x07` are image areas of `0x04` and `0x05`**, per table 2-2 of the same document
("the addresses shown in parentheses are an image area"). They were in `mem_zone[]` as aliases
all along but missing from `mem_hash_read`/`mem_hash_write`, so a guest using them hit
`mem_read_error`. Both are bound now, with their P2 forms, and `0x06` is in
`VRAM_VENTANA_64()` because it images the 64-bit window. That table is worth reading as a
checklist: the images of the boot ROM (`0x02`), of the polygon FIFO (`0x12`) and of the YUV
converter (`0x12800000`) are still unbound, and `0x01000000` — which is the **G2 external
area**, i.e. an expansion device a retail console does not have — explains the
`0xA1000400`-`0xA1001800` probes that `Virtua Tenis 2` makes.

`--traza-mem` reports the bytes written to video RAM **per window**, and the first write to each
with the PC that made it — which is what separates "the guest chose this window" from "dcemu
resolved a store queue's target wrongly".

### The background plane

**The chip does not clear the screen to black: it draws a background polygon.**
`ISP_BACKGND_T` (`0x005F808C`) points at it — tag in words over `PARAM_BASE` in bits 23-3,
skip in 26-24 — and the polygon lives in video RAM (through the **32-bit** window, measured
against the boot ROM): 3 header words, then three vertices of 3+skip words whose last word
is the packed color. `color_de_fondo()` reads that color and uses it as the clear color,
which covers the flat case — KOS's `pvr_set_bg_color()` and the boot ROM's `0xBFBFBF`.
The clear happens **at the start of the scene** (`cb_tastart`), because the chip latches its
configuration at STARTRENDER: sampling when presenting the previous scene fell mid-way
through the next frame's register programming.

**The register's value must be validated before it is believed.** dcemu does not write the
TA's output into VRAM, so `TA_ITP_CURRENT` never really advances; KOS computes
`ISP_BACKGND_T` by subtracting against that pointer and on one of the two double-buffer
parities the subtraction goes negative — `0xFF800000`, skip 7, high bits set. On hardware
that frame draws a garbage background hidden behind the scene; here the clear color shows,
so an impossible value keeps the last good color instead. Without that check half the demo
park alternated random background colors, one buffer yes and one no. `libdream` never
programs the plane at all (its demos cover the screen with geometry), which the same
fallback absorbs.

### Depth

**The TA's z is 1/w — larger means nearer — and it is stored through `profundidad_ta()`, which is
monotonic, so the ordering is the chip's.** The PVR's compare modes are written in those terms
(`GREATER` passes what is nearer), and `screeninit()`'s `glOrtho` maps rising eye-z to rising
depth, so the ordering comes out right with no further transformation.

**That mapping requires near/far *inverted* in `glOrtho` — `(RANGO, -RANGO)` — because GL negates
eye z** (`z' = -2z/(far-near)`). With the natural-looking order a larger vertex z came
out with a *smaller* depth value, so `GL_GREATER` kept what is *farther*: a near strip lost against
a far one already drawn. `pvr-fb_tex` measured it — its full-screen mask at z=1 left the cube at
z=4 invisible, 0 non-black pixels in the whole scene — and the `pvr_rtt_sized` markers at z=3/z=4
losing against the interior at z=2 were the same bug. Both `glOrtho` calls (screen and RTT) carry
the inversion.

**Feeding 1/w to `glOrtho` linearly throws away precision exactly where a game lives.** The chip
compares 1/w in floating point, with resolution concentrated near zero; a linear ortho range over
a 24-bit integer depth buffer gives one step per `range/2^24`. Crazy Taxi's raw 1/w spans
**0.01..1000** (now printed at exit by `--traza-mem`), so with the old ±32768 range the step was
0.0039 and the whole distant city (z 0.01..0.1) fit in 23 steps: neighbouring walls landed on the
same depth value and, submitted with `GEQUAL`, whichever drew *later* won — streets vanished
leaving sky, buildings dropped out by camera angle. `profundidad_ta()` stores `log2(1+z)` instead
(range ±32, `PROFUNDIDAD_RANGO`), which spreads the buffer's steps in proportion to the value —
the same pair of walls now sits ~75 steps apart. Monotonic, so every compare mode and everything
above still holds; z = 0 (infinitely far) stays 0, which is also the `glClearDepth`. Sprites and
modifier-volume triangles go through the same function — the volumes are compared against the
scene's depths. The cost: GL interpolates depth linearly in screen space and the map is not
linear, so long-in-depth polygons bow slightly against exact planes; intersection edges can move
a pixel. The fixed 2D quads (`DibujarFramebuffer`, `DibujarGL`) draw with the depth test off, so
their z only matters for clipping — inside ±RANGO.

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

### Fog

**Table fog is emulated; per-vertex fog is not.** TSP bits 23-22 pick the mode (0 table, 1
vertex, 2 none, 3 table 2); the guest writes the density to `FOG_DENSITY` (`0x005F80B8`, a
16-bit float: 1.m7 mantissa in bits 14-8, signed exponent in 7-0), the color to
`FOG_COL_TABLE` (`0x005F80B0`) and the 128-entry curve to `0x005F8200-0x005F83FC`. All of it
used to land in `control_mem` with no reader — the usual hole — which is why `kgl-tunnel`
ended in a black pit instead of fading into its grey fog, hiding the arches and pillars its
walls actually have. The table's index is `density × (1/w)` clamped to `[1, 256)` — exponent
in the slot's high bits, 4-bit mantissa below — and each word carries the far-edge alpha in
the high byte and the near-edge one in the low byte, interpolated by the fraction (KOS fills
it in `pvr_fog_table_exp2()` and friends).

`dibujar_niebla_tira()` evaluates that per **vertex** — from the same `q` the perspective
correction stores — and draws the strip a second time, untextured, blended toward the fog
color. The pass reuses the depth the strip just left: `GL_EQUAL` if it wrote z, the strip's
own compare if it did not (it passes exactly where the original did), and never writes the
buffer. A never-written table is all zeros, so the pass skips itself and costs the other
demos nothing. The chip fogs per pixel; per-vertex differs only inside large triangles.

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

**Within the translucent list the chip sorts by depth per pixel — autosort — and submission
order means nothing.** `compare()` approximates it per strip: nearest z of each strip, far to
near, unless the guest set `ISP_FEED_CFG` bit 0 (pre-sort mode), in which case submission order
is the contract. Measured in Crazy Taxi's menu: the pill (alpha 0.79, z 0.99) enters the list
*before* the flame logo behind it (alpha 0.49, z 0.014), and in submission order the flame
blended on top of the pill — every stacked translucency came out with the layers composed
backwards. Per-strip is an approximation: interpenetrating translucent geometry can still sort
wrong where per-pixel would not.

**A sprite is a complete primitive and never chains** — the strip closes after each one even if
the parameter does not carry the end-of-strip bit. Trusting the bit cost Crazy Taxi's trees:
it submits leaf clusters as sprites without end-of-strip, dcemu chained them into one strip,
and the bridge triangles between one sprite's corners and the next were black rectangles
behind the foliage and giant polygons across the sky (vertices at ±200000 pixels).

**TSP bit 20 is "Use Alpha" and it only forces the *vertex* alpha to 1.0** — texture alpha
stays live and blending stays on. It was wired as the GL blend switch, and that cost the
trees too, the other half: leaves are ARGB1555 VQ textures in the translucent list with
use-alpha off and srcalpha factors — the chip blends them (texture alpha cuts the background),
dcemu drew them opaque and the alpha-0 background came out as black boxes. Blending is now
decided by the list — translucent lists blend with the TSP factors (ONE/ZERO is "no blend" by
itself), opaque and punch-through never blend — and the bit is applied where it belongs, in
the vertex color constructors (`poly_usa_alfa`).

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

**The DMAC's end-of-transfer follows the same pattern**: `dma_canal()` leaves `CHCR.TE` set
(and does not touch `DE`, per the manual), and `intc_revisar_sh4()` derives DMTE0-3 (INTEVT
`0x640`/`0x660`/`0x680`/`0x6A0`, priority in IPRC bits 11-8) from `TE && IE && DMAOR.DME`;
the guest acknowledges by clearing the CHCR, which is what KOS's driver does. Two things
here cost a hang each: the interrupt used to be a log line saying "not implemented", and
**`DMAOR` starts at `0x8201`** (DDT, priority CH2>CH0>CH1>CH3, DME) because that is what
the boot ROM leaves on a retail console — KOS's `dma_init()` only writes DMAOR on NAOMI,
*"these are set by the bios on Dreamcast"*, so with the syscall hooks nobody else set DME
and `basic-dma-speedtest` armed a perfectly valid channel 1 that never ran.

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

**The tail's `desplazamiento` field means two different things.** In 3.5 it is the header's
**size**, counted back from the end of the file; in 2.0 and 3.0 it is the header's absolute
**position**. `cdi_abrir()` used it as a size in all three, which happens to work for 3.5 —
where the two readings coincide — and asks for the whole image as if it were the header on the
other two. `Virtua Tenis 2 (USA).cdi` is a 3.0: 749 MB of `malloc` and a short read, and the
function returned failure **with no message**, so it read as "finds no tracks". The header runs
to the end of the file in all three versions, so the size is now derived from the position and
never from the field, and both exit paths say what happened.

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

Three details of that block cost a boot each, and all three are the sort of thing that reads
as working:

- **`REQ_SES`'s response has a reserved second byte.** `[0]` status, `[1]` zero, `[2]` the
  session count (session 0) or the session's first track, `[3..5]` the FAD. Leaving out `[1]`
  shifts everything and the count is read as the FAD's high byte.
- **A DMA read can take many bursts.** `disparar_dma()` moves `SB_GDLEN` bytes and raises the
  DMA-end event on each, but the *command* only ends when the data runs out. The IP.BIN
  bootstrap pulls a 1.4 MB executable in 32-byte pieces, so ending the command on the first
  burst loses all but 32 bytes — silently, because the guest still sees each DMA finish.
- **The ERROR register is not just the sense key.** Bit 2 is `ABRT`, and that is the bit the
  ROM's driver tests to decide whether a command was aborted. `GD_ERR_*` in `gdrom.h`.

`SB_GDSTARD` and `SB_GDLEND` (`0x005F74F4`/`0x005F74F8`) are the DMA's counters: where it is
and how much of *the command* it has moved. The ROM's driver keeps `SB_GDLEND` in its command
block as "how much has landed", so leaving them to the control block's backing store feeds it
uninitialised memory.

### Sound: the AICA, the ARM7DI and the G2 DMA

**Sound works: four KOS demos play.** `aica.c/h` is the chip's register block, its three timers,
its interrupt controller, its internal DMA and the 64-channel synthesizer; `arm7.c/h` is the
ARM7DI it carries inside; `g2dma.c/h` is the Holly's four G2-DMA channels; `audio.c/h` is the
only piece that touches SDL. The first three are SDL-free on purpose, like `sistema.c` and
`vram.c`, so `tests/` links them for real — suites `aica`, `arm7` and `g2dma`.

The reference is Sega's *Dreamcast/Dev.Box System Architecture*: §4.2.2 and §8.4.5 for the map,
§8.1.1 for the algorithms, §8.4.1.4 for the G2-DMA. `docs/aica-plan.md` is the plan and the
record of what landed. **The KOS ARM firmware
(`kernel/arch/dreamcast/sound/arm/`) is the independent check on the paper**, and in one place
it disambiguates it — see the interrupt level below.

**The ARM runs in all 135 demos, not in the seven sound ones.** `spu_init()` writes
`0xEAFFFFF8` — a branch to itself — at sound RAM address 0 and releases the reset in *every*
program, "so that CD audio works", and the boot ROM drives `ARMRST` three times before reaching
its menu. So the demo sweep is regression for this subsystem, and `--sin-aica` exists to turn
the whole chip off when isolating one.

**The two windows are not the same block with a different prefix.** The SH-4 reaches the AICA
over the G2 at `0x00700000` and the ARM from inside at `0x00800000`, and two registers exist in
only one of them (table 8-25): `ARMRST` (`0x2C00`) is the SH-4's alone — it is the switch that
holds the ARM in reset — and `L`/`M` (`0x2D00`/`0x2D04`), the interrupt number and the
end-of-interrupt, are the ARM's alone. Access size is asymmetric too: the paper restricts the
SH-4 to 4-byte accesses with only the low 16 bits valid, but the ARM writes *bytes* — KOS's
`aica.h` defines `CHNREG8` and uses it for pan (`+0x24`), send level (`+0x25`) and volume
(`+0x29`). So the file is byte-addressable and the restriction applies at the G2 entry.

**Everything derives from `reloj_total`, like the timers.** `gcd(44100, DC_CPU_HZ) = 60`, so it
is exactly **735 samples every 3324992 CPU cycles** — no drift, integers. And the audio block's
22.5792 MHz is exactly `44100 × 512`, so the ARM needs no clock of its own: **512 ARM cycles per
sample**.

**The interrupt level comes from three registers, and KOS pins the reading down.** `SCILV2:SCILV1:SCILV0`
hold one bit each at the source's position. With the values `aica_init()` writes — `0x18`,
`0x50`, `0x08` — source 6 (timer A) comes out **2** and source 3 (MIDI in) comes out **5**,
which are exactly the two numbers `crt0.s` compares against. Any other reading sends the
firmware down the wrong branch of its FIQ. A pending source with no mask **stays pending**, the
same rule the ASIC events needed.

Three things about the ARM7DI cost a test case each, and all three are in the suite:

- **Reading R15 gives PC+8**, and PC+12 when the shift comes from a register or when it is
  stored to memory.
- **`SUBS PC, R14, #4` is not a subtraction**: with `S` set and R15 as destination it restores
  CPSR from SPSR. That is how the firmware's FIQ ends; without it the ARM enters its first
  interrupt and never leaves FIQ mode — losing the main program's R8-R14 on the spot.
- **The bus is 24 bits.** Without clamping the address, KOS's `0xEAFFFFF8` (a branch to PC−24,
  i.e. `0xFFFFFFE8`) lands outside both regions of the map; with it, the core walks zeros —
  which decode as a no-effect `AND` — and wraps, which is the infinite loop KOS's comment
  promises. ARMv3 has no Thumb, no `LDRH`/`STRH`, no `BX` and no coprocessors: those patterns
  take the undefined-instruction vector, which is what the real part does.

**Two envelope bugs produced silence and no error message, and only the measurement found them:**

- **Rate 0 in table 8-5 means ∞ — the envelope does not move — not "instantaneous".** Reading it
  the other way switched a channel off on the first sample of the decay. The symptom was an
  eight-second `.wav` with **peak 14 out of 32767**: the audible equivalent of a black BMP. With
  the table right the same file peaks at 16533.
- **The envelope advances before the sample is used, not after.** The other way round, every
  channel's first sample comes out at rest attenuation, i.e. muted.

The ADPCM follows §8.1.1.2 literally and is done in integers — the eight factors of table 8-4
are exact in 256ths — so it is **deterministic**: two runs give a bit-identical `.wav`. Note the
paper has a typo: entry 31 of the decay column reads `90.` between `920.` and `690.`; the
progression is geometric and the right term is 790.

**`--captura-audio=ARCHIVO.wav` is the twin of `--captura-gl`, and it goes in before the mixer,
not after.** It dumps what the *mixer produced*, not what the sound card did with it — the same
lesson that moved graphics off window grabs. Measure it the way BMPs are measured: non-zero
samples, distinct values, RMS and peak. A silent `.wav` is a black BMP. It closes through
`traza_resumen()`, so `--salir-tras` matters as much as it does for the disassembly. `--sin-audio`
skips the sound card and keeps the dump; listening and measuring coexist, because with the
device open the dump comes from a second ring the callback fills.

**`0x005F7800-0x005F787F` are the four G2-DMA channels, and they used to vanish into
`control_mem`** — so the guest wrote 1 to `SB_ADST`, read it back, got 1 ("DMA in progress") and
stayed there. Same shape as the CH2 DMA and `SB_G1SYSM`. `spu_dma_transfer()` is what
`snd_stream.c` uses to refill the stream buffer, so that one alone blocked the streaming demos.
End of transfer is bits 15-18 of `SB_ISTNRM`, one per channel; the AICA's own interrupt
(`G2AICINT`) is bit 1 of `SB_ISTEXT`, next to the drive's end-of-command.

**The boot ROM's own firmware plays, and it is the best check there is on the pitch.** With
`--bios` the chime comes out at 6.46 s: 70 key-ons across 48 channels, all PCM16 of the *same*
sample (`SA 01852a`, looping `LSA 0001`..`LEA 00ab`) at ten different pitches. Those ten land on
table 8-7 of the paper to within one LSB — C2, F♯3, G3, B3, D4, E4, F4, G♯4, A♯4, D♯5. If the
phase increment were wrong they would not sit on the tempered scale; they would drift further
out the further from the base note, and they don't. No KOS demo gives that check, because none
of them plays a chord. Eight samples out of 1.4 million touch the rail, so the mixer is not
saturating either.

**What is not emulated**: CDDA, the audio DSP, the LFO, the FEG filter (the paper says how to
leave it pass-through: `Q = 4`, `FLV = 0x1FF8`, and KOS's firmware simply turns it off) and the
sample-interval interrupt. `docs/aica-plan.md`, "Lo que sigue faltando", has the detail —
including that on the KOS path **CDDA arrives as a syscall, not as an SPI packet**: command 20
of the GD-ROM vector, which `hack_gdrom()` in `dcopcodes.c` would have to answer.

Two values are answered without a measurement behind them, and they are flagged as such because
an identification register answered casually has already hung the guest twice (`REVISION` and
`SB_G1SYSM`): `VER[3:0]` of `0x2800`, given 1, and `MEM8MB`, accepted and ignored.

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
  image via `iso.c`. **`GDROM_CHECK_DRIVE` answers disc type GD-ROM (`0x80`) whenever a disc
  is in**, not what the image is: this path replaces console, BIOS and drive to run the
  mounted game, and a commercial game's original disc is a GD-ROM. Katana's `gdFsInit()`
  compares that word against a literal `0x80` and anything else makes it return −5 and retry
  the whole drive init forever — Crazy Taxi sat at `LOADING (31K)` in an infinite
  INIT/SEND(CMD_INIT)/MAINLOOP/CHECK/CHECK_DRIVE syscall loop, on *both* rips (GD layout and
  MIL-CD layout: no selfboot ships that check patched). Fixing it is what took the game from
  its loading screen to the title and the 3D attract mode — hito D. The `--bios` path does
  not come through here and keeps seeing the CD the image really is, which its MIL-CD branch
  needs.
- `hack_romfont()` services the ROM font syscall. **This one takes its function number in
  `R1`, not `R7`** (see KOS's `syscall_font.s`): 0 returns the font address, 1 takes the
  mutex, 2 releases it. The lock must answer **0** to mean granted.
- `hack_flashrom()` services the flash ROM syscall — info, read, write, delete. The
  convention is `syscall(r4, r5, r6, func)`, so the function number arrives in `R7` and the
  result goes back in `R0`. Write only clears bits (`&=`), because that is what flash can do
  without an erase and the guest's settings blocks rely on it. Without this, KOS's
  `flashrom_get_region()` reported `can't find partition 0` — it asks the BIOS for the
  partition offsets rather than parsing the flash.

`SYSINFO` is answered now (`hack_sysinfo()`), with the numbering confirmed against KOS's
`kernel/arch/dreamcast/hardware/syscalls.c`: function 0 is INIT (on the ROM it copies the
console ID from flash to `0x8C000068`, which `main()` already leaves done — see `SYSID_BASE`),
2 is ICON (still unimplemented, still reported), and **3 is ID, which returns in `R0` a
pointer to the 8-byte ID, not the ID itself** — KOS dereferences it. Answering 0 there was
not neutral: Crazy Taxi follows the pointer and copies its "ID" through it, writing around
address `0x10`. The unnamed vector at `0x8C0000E0` (`UNKNOWN`) still does nothing, but it
**says so**: `hack_mudo()` reports the name, the four arguments, the PC and the PR through
`--traza-mem`, and leaves `R0` at 0 rather than at whatever was there — a garbage pointer is
worse than a null one, because the guest follows it. These used to be `RTS` + `NOP`, i.e. the
exact shape of every other hole in this tree: something the guest asks for, answered without
meaning it, leaving no trace.

**The syscall stubs are not the only thing the boot ROM leaves behind: it also writes the
flash's machine code — five digits and a NUL — at `0x8C000070` (`REGION_BASE`) before handing
the console to the game.** Without `--bios` nobody wrote it and whatever was there stayed.
Games do look at it: Crazy Taxi compares that *word* against `0x3030` — the first two digits —
and, if it matches, treats the machine as known; if it does not, it goes and asks a device on
the external G1 bus at `0x03010000` that a retail console does not have, and waits on its
bit 7 forever. That single uninitialised word was the whole difference between the game
hanging in an idle loop and reaching its `LOADING CRAZY TAXI` screen. It is copied from
`flash_mem[FLASH_PART0_OFF]`, so it follows whatever flash is in use rather than being a
constant.

**The rest of that block is the ROM's copy of the flash, not constants**, which is what makes
it derivable instead of magic. Measured by booting `--bios` with the 1.01d and dumping
`0x8C000000-0x8C0000FF` at the menu (two runs, identical but for one byte):
`0x8C000068` holds the console's 8-byte binary ID, which is flash partition 0 at `+0x56`, and
`0x8C000078` holds 8 bytes of system settings taken from the **last** 16-byte record of flash
partition 2. The first is reproduced (`SYSID_BASE`, next to `REGION_BASE`, from the same
flash); the second is not, because that partition's record format is not worked out. The two
words at `0x8C000060`/`0x64` are `0x00C0C0C0` and something that changes between runs, and
neither comes from the flash — unexplained, so left alone rather than hardcoded.

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

### UBC (hardware breakpoints)

`ubc.c/h` is the SH-4's user break controller, driven the way KOS's driver drives it
(`kernel/arch/dreamcast/hardware/ubc.c`): two channels with address masks and optional
ASID, channel B optionally comparing the transferred data (`BDRB`/`BDMRB` under
`BRCR.DBEB`), and `BRCR.SEQ` chaining them — A's match only arms B. `CMFA`/`CMFB` are set
on match and **only the guest clears them**; its handler reads them to know which channel
fired. The exception is EXPEVT `0x1E0` through the general vector; instruction breaks
honour `PCBA`/`PCBB` (before/after execution), operand breaks are always "after": they go
pending and deliver at the next instruction boundary, so SPC lands on the following
instruction — KOS prints `PC - 2` for exactly that reason. With `SR.BL` set the delivery
waits without being lost.

Instruction breaks are evaluated at `main_loop()`'s boundary (one flag test when no
channel is armed); operand breaks hook the guest-path `memread`/`memwrite` macros in
`mem.h`, which compare the **virtual** address — internal accesses go through `*_fisico`
and stay out by themselves. A break on a delay-slot instruction is not detected (the slot
runs inside the branch's nested `core.execute()`); the manual restricts that case anyway.

**`basic-breaking` passes four of its five groups; the fifth fails in the binary, not the
UBC.** GCC 15.2 at -O2 deletes the `test_function("Sega", "Sony")` call in
`break_on_sequence` — a pure static function whose result is discarded (the first test
survives because it assigns to a `volatile`) — so sequence condition A is unreachable, on
real hardware too. Rebuilt with a one-line `volatile`, the demo prints
`***** Breakpoint Test: SUCCESS *****` end to end.

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
