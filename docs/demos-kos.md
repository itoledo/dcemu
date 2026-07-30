# Estado de las demos de KallistiOS

Estado medido el **30 de julio de 2026**, sobre los 135 binarios compilados desde
`kos/examples/dreamcast` más `demos/roto`, con el build de ese día. Este documento es la línea
base de regresión: si un cambio rompe algo, aquí está lo que funcionaba.

| | |
| --- | --- |
| Funcionan | **81** binarios (79 demos: `bfont` y `tunnel` están duplicados) |
| Fallan por algo que falta emular | **26** |
| No aplican: piden periféricos o herramientas del anfitrión | **28** |

## Cómo se mide

Cada demo se corre 11 segundos, se captura el área de cliente de la ventana y se cuentan los
colores distintos muestreando uno de cada seis píxeles; aparte se guarda `logs/serial.txt` y se
extrae la última línea con marca de resultado. Secuencial a propósito: dos instancias se pelean
por `logs/serial.txt` y los resultados salen cruzados.

**Dos trampas del método, las dos ya costaron tiempo:**

- **La cuenta de colores es un indicio, no un veredicto.** Una demo de consola sale con 4
  colores — ventana negra — y está perfecta: su salida va por el serial. Al revés, una cuenta
  alta dice que algo se dibujó, no que sea lo correcto.
- **Una demo que termina sola deja la ventana negra.** KOS limpia la pantalla al salir, así que
  cualquier cosa que acabe por su cuenta ya está negra cuando la captura llega. `video/minifont`
  duerme 10 segundos y retorna, y el tiempo emulado corre ~2,5× rápido sin `--limitar`, así que
  se termina a los ~4 segundos de reloj real. Funciona; hay que mirarlo antes, o con el volcado
  de F5. Ver `CLAUDE.md`, sección de captura de la ventana.

Por eso el inventario de abajo separa **verificado a ojo** de **dibuja**: en el segundo grupo la
captura tiene contenido real pero no se revisó imagen por imagen.

## Lo que falta

### Formatos de textura del PVR (7)

`taPolyModifier()` en `graficos.c` despacha el `pixelformat` de la palabra de control de textura,
y para estos cuatro casos llama a la macro `CTT()`, que deja formato, componentes y empaquetado
sin definir. Hay que decodificarlos a algo que GL entienda, igual que se hace con ARGB1555,
RGB565 y ARGB4444.

| demo | qué falta |
| --- | --- |
| `pvr-palette-4bpp`, `pvr-palette-8bpp`, `pvr-palette-wormhole` | paleta de 4 y 8 bpp, más los registros de paleta. Salen en blanco: la geometría se dibuja, la textura no se decodifica |
| `pvr-yuv_converter-YUV420`, `pvr-yuv_converter-YUV422` | YUV422, y el convertidor YUV del TA |
| `pvr-bumpmap` | formato BUMP |
| `pvr-strided_texture` | el stride de textura solo se registra en el log, no se aplica |

### Otras rutas del PVR (6)

| demo | qué falta |
| --- | --- |
| `pvr-modifier_volume`, `pvr-modifier_volume_tex`, `pvr-cheap_shadow` | volúmenes modificadores. `taPolyModifier()` reconoce el tipo de lista pero no hay recorte por volumen |
| `pvr-fb_tex`, `pvr-pvr_rtt_sized` | render a textura (`pvr_scene_begin_rtt`) |
| `pvr-pvrline` | primitivas de línea |
| `parallax-serpent_dma` | negro después de reservar los stacks. Probablemente el DMA del PVR hacia el TA (`pvr_dma_load_ta`), que es por donde esta demo entrega la geometría |

`pvr-modifier_volume_zclip` sí dibuja, pero eso no dice que los volúmenes funcionen: lo que se ve
puede ser la geometría sin recortar.

### Sonido y AICA (7)

Ninguna demo de sonido llega a sonar. El camino de subida del firmware sí está —
`libdream-spu` reporta `Load OK, starting ARM`, o sea que los 2 MB de RAM de sonido y la ventana
física se comportan— y lo que falta es el chip: los 64 canales, sus registros y la cola de
comandos que KOS usa para hablar con el ARM.

`sound-multi-stream` es el que da el diagnóstico más claro: `Assertion g2_read_32_raw(qa +
offsetof(aica_queue_t, valid)) failed at snd_iface.c:84`. KOS escribe una cola en la RAM de
sonido y espera que el ARM la marque como consumida.

Los otros seis: `sound-sfx`, `sound-sfxbuf`, `sound-hello-adx`, `sound-hello-opus`,
`sound-cdda-basic_cdda`, `libdream-spu`.

Nota: `sound-ghettoplay-vorbis`, `sound-hello-mp3` y `sound-hello-ogg` sí dibujan su interfaz y
llegan a crear el hilo del servidor de sonido, así que están en la lista de los que funcionan
—en lo visual— aunque tampoco suenen.

### MMU (2)

Las cinco fases del plan (`docs/mmu-plan.md`) están implementadas y las 24 pruebas unitarias
pasan, pero las dos demos que ejercitan la MMU de verdad todavía caen:

| demo | síntoma |
| --- | --- |
| `basic-mmu-nullptr` | `kernel panic: unhandled MMU exception`. KOS imprime `PTEH = 0, PTEL = 1b4, TTB = 0, TEA = 0, MMUCR = 00f40201` y decide que no la puede atender, o sea que el código de excepción o los registros no son los que espera |
| `basic-mmu-pvrmap` | `kernel panic: double fault`. Llega a listar los hilos, así que arranca bien y se cae al mapear la RAM del PVR |

### SH-4, resto (4)

| demo | qué falta |
| --- | --- |
| `basic-breaking` | `Breakpoint Test: FAILURE`. Necesita el UBC, el controlador de breakpoints por hardware |
| `basic-fpu-exc` | `TEST FAILED!`. Excepciones de FPU. Es una de las tres desviaciones que `tests/README.md` documenta a propósito |
| `basic-dma-speedtest` | el serial se corta después del escaneo del maple: se traba antes de medir |
| — | `tunnel` y otras siguen logueando `pvr_prim: attempt to submit to unopened list` en cantidad, aunque dibujen bien. Ver `CLAUDE.md` |

## Lo que no aplica (28)

Fallan porque piden algo que dcemu no emula, así que fallar es el comportamiento correcto y no
hay nada que arreglar salvo que se decida emular el periférico.

| qué piden | demos |
| --- | --- |
| Red (BBA o LAN adapter) | `network-basic`, `network-dns-client`, `network-httpd`, `network-isp-settings`, `network-lftpd`, `network-ntp`, `network-ping`, `network-ping6`, `network-speedtest`, `network-udpecho6` |
| Modem | `modem-basic`, `modem-ppp` |
| Tarjeta SD o IDE | `filesystem-browse`, `filesystem-sd-ext2fs`, `filesystem-sd-speedtest`, `g1ata-atatest` |
| Imagen de disco montada | `libdream-cdfs` |
| Dreameye | `dreameye-basic`, `dreameye-sd` |
| Teclado o ratón | `keyboard-keytest`, `keyboard-keyrawtest`, `libdream-mouse` |
| Pistola | `lightgun-basic` |
| VMU | `libdream-vmu`, `vmu-vmu_lcd` |
| LCD | `libdream-lcd` |
| `dc-tool` en el anfitrión | `profiling-gcov` (necesita `-c .`), `basic-gdb_breaking` (espera un GDB conectado) |

## Lo que funciona

### Consola, con veredicto explícito en el serial (31)

`hello`, `basic-asserthnd`, `basic-exec`, `basic-memtest32`, `basic-posix_resource`,
`basic-stackprotector`, `basic-stacktrace`, `basic-watchdog`, las diez de
`basic-threading-*` (`atomics`, `barrier`, `compiler_tls`, `general`, `once`,
`recursive_lock`, `reentrant_mutex`, `rwsem`, `spinlock_test`, `tls`), `cpp-concurrency`,
`cpp-filesystem`, `cpp-modplug_test`, `cpp-out_of_memory`, `dev-devroot`, `dev-random`,
`filesystem-pty`, `library`, `objc-runtime`, `micropython`, `maple`, `conio-conio_dbgio`,
`profiling-gprof`.

### Gráficos verificados a ojo (17 binarios, 15 demos)

`video-bfont` = `bfont`, `video-minifont`, `video-multibuffer`, `video-screenshot`,
`video-palmenu`, `conio-basic`, `conio-kosh`, `conio-wump`, `conio-adventure`,
`kgl-tunnel` = `tunnel`, `libdream-ta`, `parallax-bubbles`, `png`, `pvr-texture_render`,
`tsunami-font`.

`video-palmenu` es correcto tal como sale: sin consola europea ni cable distinto de VGA lo que
corresponde es el patrón XOR y esperar Start. Su `flashrom_get_region: unknown code '00111'`
viene del contenido de `bios/flash.bin`, no de un fallo del emulador.

### Dibujan; la captura tiene contenido pero no se revisó una por una (33)

`2ndmix`, `cdrom-stream`, `cpp-clock`, `cpp-dcplib`, `filesystem-sd-mke2fs`,
`libdream-320x240`, `libdream-640x480`, `libdream-keyboard`, `libdream-rgb888`, `lua-basic`,
`mie-basic`, `parallax-delay_cube`, `parallax-font`, `parallax-raster_melt`,
`parallax-rotocube`, `parallax-sinus`, `plasma`, `pthread-general`,
`pvr-modifier_volume_zclip`, `pvr-plasma`, `pvr-pvrmark`, `pvr-pvrmark_strips`,
`pvr-pvrmark_strips_direct`, `roto`, `rumble`, `sound-ghettoplay-vorbis`, `sound-hello-mp3`,
`sound-hello-ogg`, `tsunami-banner`, `tsunami-genmenu`, `vmu-vmu_beep`, `vmu-vmu_game`,
`vmu-vmu_pkg`.
