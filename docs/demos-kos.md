# Estado de las demos de KallistiOS

Estado medido el **30 de julio de 2026**, sobre los 135 binarios compilados desde
`kos/examples/dreamcast` más `demos/roto`, con el build de ese día. Este documento es la línea
base de regresión: si un cambio rompe algo, aquí está lo que funcionaba.

| | |
| --- | --- |
| Funcionan | **84** binarios (82 demos: `bfont` y `tunnel` están duplicados) |
| Fallan por algo que falta emular | **23** |
| No aplican: piden periféricos o herramientas del anfitrión | **28** |

## Cómo se mide

Cada demo se corre 11 segundos, se captura el área de cliente de la ventana y se cuentan los
colores distintos muestreando uno de cada seis píxeles; aparte se guarda `logs/serial.txt` y se
extrae la última línea con marca de resultado. Secuencial a propósito: dos instancias se pelean
por `logs/serial.txt` y los resultados salen cruzados.

**Cuatro trampas del método, las cuatro ya costaron tiempo:**

- **La cuenta de colores es un indicio, no un veredicto.** Una demo de consola sale con 4
  colores — ventana negra — y está perfecta: su salida va por el serial. Al revés, una cuenta
  alta dice que algo se dibujó, no que sea lo correcto.
- **Una demo que termina sola deja la ventana negra.** KOS limpia la pantalla al salir, así que
  cualquier cosa que acabe por su cuenta ya está negra cuando la captura llega. `video/minifont`
  duerme 10 segundos y retorna, y el tiempo emulado corre ~2,5× rápido sin `--limitar`, así que
  se termina a los ~4 segundos de reloj real. Funciona; hay que mirarlo antes, o con el volcado
  de F5. Ver `CLAUDE.md`, sección de captura de la ventana.
- **Un `panic` en el serial no siempre es un fallo.** `basic-mmu-nullptr` termina en
  `kernel panic` **porque eso es lo que la demo quiere demostrar**: atrapa el uso de un puntero
  nulo, imprime el diagnóstico y muere. Buscar «panic» y contarlo como rojo la clasificó mal
  durante todo un barrido. Hay que leer la salida completa, no la última línea.
- **SDL 1.2 desvía `stdout` y `stderr` a archivos en Windows.** Van a `stdout.txt` y
  `stderr.txt` en el directorio de trabajo, así que redirigir la salida del proceso desde el
  shell —o con `-RedirectStandardError` de PowerShell— captura cero bytes y parece que el
  emulador no dijo nada. Ahí es donde salen los avisos de `--traza-mem` y el de la MMU.

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

### MMU (0)

Las dos demos de MMU funcionan. Estaban aquí por dos razones distintas y ninguna era la MMU.

**`basic-mmu-pvrmap` caía por un `memwrite` que tenía que ser `memwrite_fisico`.** El volcado
decía `Unhandled exception: PC 8c01ad68, code 2, evt 0060` con `SR 600000f0` —`RB=1`, o sea el
banco de excepción—, y detrás `kernel panic: double fault`. Desensamblando ese PC:

```
8c01ad64 <_maple_dma_start>:
8c01ad64:	mov.l	8c01ad70,r1	! a05f6c18
8c01ad66:	mov	#1,r2
8c01ad68:	mov.l	r2,@r1          <- aquí
```

Es la escritura a `SB_MDST` que arranca el DMA del Maple, y `0xA05F6C18` está en **P2**, que
no se traduce. Lo que fallaba era lo que esa escritura dispara: `pvr_write()` ejecuta el DMA
del Maple ahí mismo, dentro de la instrucción, y una de sus escrituras —`mem.c:1099`, la que
rellena `0xFFFFFFFF` para un puerto sin dispositivo— usaba `memwrite` en vez de
`memwrite_fisico`. Todo el resto del bloque ya usaba la versión física, con el comentario que
explica por qué; esa línea se quedó atrás. Con `AT=1` la dirección del descriptor
(`0x0C04A00C`, física) se traducía, no estaba mapeada, y levantaba un fallo de TLB en
escritura desde dentro del manejador de VBlank —de ahí el `RB=1` y el doble fallo.

Solo se veía con la MMU encendida, que es lo que hacía que solo esta demo lo destapara.
Arreglado, `pvrmap` dibuja su patrón completo a través de la traducción y llega al bucle que
espera START.

**Le falta el texto «Press START!», y eso no es de dcemu.** `bfont` pide la dirección de la
fuente al syscall del boot ROM, que responde `0x00100020` —P0—. `pvrmap` mapea las páginas
virtuales de 0 a 8 MB sobre la RAM del PVR, y `0x00100020` cae justo dentro: la lectura de la
fuente se traduce y lee la RAM de video en vez de la ROM. Es la demo pisándose su propia
fuente. Que la causa es esa está comprobado: reportando `0xA0100020` el texto sale. No se
cambió, porque **P0 es lo que devuelve el hardware** — `bios/bios.bin` trae dos veces la
constante `0x00100020` y ninguna `0xA0100020`, así que en una consola real pasaría lo mismo.

**`basic-mmu-nullptr` estaba aquí por un falso positivo.** Su `kernel panic: unhandled MMU
exception` **es el final previsto de la demo**, no un fallo. `catchnull` devuelve `NULL`
incondicionalmente, así que KOS imprime `cannot map virtual address 00000000` y entra en
pánico a propósito; el fuente lo dice (`/* We shouldn't get here... */`) y GCC hasta emitió
un `abort()` justo detrás de la escritura, porque desreferenciar `NULL` es comportamiento
indefinido. Lo que la demo pide, dcemu lo hace bien:

- la escritura a `NULL` con `AT=1` falla en la UTLB y levanta el código 0x060 por el vector
  0x400, que KOS clasifica como `dtlb_miss_write` —es una escritura, correcto—;
- `SPC` apunta a **la instrucción que falló**, `8c01013a`, que es exactamente el
  `mov.w r1,@r1` del desensamblado, no la siguiente;
- `TEA` y `PTEH.VPN` quedan en 0, que es la dirección que falló, con el `ASID` conservado;
- el callback del usuario recibe la página y el PC correctos y los imprime.

Lo que `nullptr` **no** prueba es el otro medio camino —mapear y reejecutar—, porque su
callback nunca devuelve una página. Eso lo cubre `demos/mmu-mapeo`, escrito para esto:
mapea una página, escribe por la virtual, y comprueba por la física (P1, sin traducir) que
la escritura llegó. Reporta `TEST SUCCEEDED!`. Con eso, **las cinco fases del plan
(`docs/mmu-plan.md`) están verificadas sobre un programa real**, no solo por las 24 pruebas
unitarias.

### SH-4, resto (3)

| demo | qué falta |
| --- | --- |
| `basic-breaking` | `Breakpoint Test: FAILURE`. Necesita el UBC, el controlador de breakpoints por hardware |
| `basic-dma-speedtest` | el serial se corta después del escaneo del maple: se traba antes de medir |
| — | `tunnel` y otras siguen logueando `pvr_prim: attempt to submit to unopened list` en cantidad, aunque dibujen bien. Ver `CLAUDE.md` |

`basic-fpu-exc` estaba en esta lista con `TEST FAILED!` y ya no: solo pedía los campos
Cause y Flag de FPSCR, que ahora se escriben. Está en la lista de consola con veredicto.
Ver `docs/sh4-conformidad.md`.

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

### Consola, con veredicto explícito en el serial (33)

`basic-mmu-nullptr` está en esta lista aunque su veredicto sea un `kernel panic`: es el que
la demo busca. Ver la sección de MMU.

`hello`, `basic-asserthnd`, `basic-exec`, `basic-fpu-exc`, `basic-memtest32`,
`basic-mmu-nullptr`, `basic-posix_resource`, `basic-stackprotector`, `basic-stacktrace`,
`basic-watchdog`, las diez de
`basic-threading-*` (`atomics`, `barrier`, `compiler_tls`, `general`, `once`,
`recursive_lock`, `reentrant_mutex`, `rwsem`, `spinlock_test`, `tls`), `cpp-concurrency`,
`cpp-filesystem`, `cpp-modplug_test`, `cpp-out_of_memory`, `dev-devroot`, `dev-random`,
`filesystem-pty`, `library`, `objc-runtime`, `micropython`, `maple`, `conio-conio_dbgio`,
`profiling-gprof`.

### Gráficos verificados a ojo (18 binarios, 16 demos)

`video-bfont` = `bfont`, `video-minifont`, `video-multibuffer`, `video-screenshot`,
`video-palmenu`, `conio-basic`, `conio-kosh`, `conio-wump`, `conio-adventure`,
`kgl-tunnel` = `tunnel`, `libdream-ta`, `parallax-bubbles`, `png`, `pvr-texture_render`,
`tsunami-font`, `basic-mmu-pvrmap`.

`basic-mmu-pvrmap` dibuja su patrón de círculos concéntricos —el `(x*x + y*y) & 0xff` de la
demo— escribiéndolo entero a través de la traducción de la MMU, y espera Start. Le falta el
texto, por la razón que explica la sección de MMU: no es del emulador.

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
