# Plan: arrancar dcemu desde el boot ROM

Estado: **fases 0 a 5 implementadas, hitos B y C alcanzados**. Julio de 2026, sobre `master`. El
inventario de lo que faltaba, y cómo se midió, está en [bios-boot.md](bios-boot.md); este
documento es el orden de trabajo, y al final está lo que quedó andando y lo que no.

Resumen: **la BIOS arranca y arranca el juego**. Llega a su pantalla de fecha y hora,
responde al mando, se entra a Play, File, Music y Settings, y con un `.cdi` montado carga el
`1ST_READ.BIN` del disco y salta a él sin ayuda de nadie. Ver
[Cuarta corrida](#cuarta-corrida-el-hito-c-o-cómo-el-rom-arranca-el-juego), que es lo último;
las secciones anteriores son la historia de cómo se llegó.

## Objetivo

Que `dcemu` parta en `0xA0000000`, corra el boot ROM real y llegue a la pantalla de la
consola. Como efecto secundario, poder arrancar un juego *pasando por la BIOS* en vez de
por los hooks de syscall de `BIOS_HACKS`.

Cuatro hitos observables, en orden:

| hito | qué se ve | fases |
| --- | --- | --- |
| **A** | el boot ROM ya no se cuelga y llega a hablarle a la lectora | 0-1 |
| **B** | con la bandeja vacía, la BIOS llega a su pantalla de "sin disco" | 2-3 |
| **C** | con una ISO montada, lee TOC e IP.BIN y salta al bootstrap | 3 |
| **D** | la animación del logo, con sonido | 5-6 |

El hito C es el que importa: es "la BIOS bootea". El D es otro proyecto.

## Fase 0 — Decisiones de base

- **No romper el camino actual.** `dcemu roto.bin` tiene que seguir funcionando igual. El
  arranque por BIOS es un modo nuevo, no un reemplazo. `BIOS_HACKS` se queda hasta que el
  hito C funcione.
- **El GD-ROM va en archivos propios**, `gdrom.c` / `gdrom.h`, no dentro del `switch` de
  `pvr_write()`. Es la única pieza con estado propio y máquina de estados; mezclarla con
  el resto vuelve a `mem.c` inmanejable. Los sectores los sigue leyendo `iso.c`.
- **Primero respaldo, después comportamiento.** Antes de emular ningún registro nuevo,
  hacer que todo el bloque `0x005Fxxxx` se lea y se escriba contra `control_mem`. Recién
  ahí interceptar los que además disparan trabajo. Así se deja de perder escrituras en
  silencio y el `switch` queda para lo que de verdad hace algo.
- **Todo lo nuevo con pruebas.** `tests/` ya tiene arnés, dobles de memoria y runner. La
  máquina de estados del GD-ROM y el handshake de PDTRA son lógica pura sobre registros:
  se prueban sin abrir una ventana, igual que los opcodes.
- **La traza de memoria pasa a ser una opción permanente.** La instrumentación que se usó
  para medir esto se tiró; hay que rehacerla como `--traza-mem`, porque es la herramienta
  con la que se va a iterar en todas las fases.

## Fase 1 — Desbloqueo

Una tarde. Al final de esta fase el boot ROM deja de colgarse.

### 1.1 Modo de arranque por BIOS

`main.c:857` fija `PC = mem_base + ip_bs1_offset`. Agregar una opción —`dcemu --bios
[imagen]`— que en cambio deje `PC = 0xA0000000` y saltee la carga manual de `ip.bin` y
`1st_read.bin`. La imagen, si se pasa, se monta con `iso_init()` para que la vea el GD-ROM
de la fase 3.

### 1.2 Handshake de PDTRA/PCTRA

En `regmap_read()`, caso especial para `0xFF800030`: devolver los bits de detección de
cable según los 4 bits bajos de PCTRA, y el tipo de cable en el byte alto. El tipo de cable
va como opción (`--cable=vga|rgb|compuesto`), con VGA por omisión.

Ya está verificado que esto desbloquea el halt: con un handshake de prueba, el boot ROM
siguió hasta los registros del GD-ROM.

### 1.3 Zonas no mapeadas

`mem_hash_setup()` deja `mem_zone[i] = NULL` para las zonas libres y
`get_memory_pointer()` no chequea: cualquier acceso ahí es una desreferencia nula. Apuntar
las zonas libres a un bloque de descarte, como hace `tests/memoria_prueba.c`, y dejar que
los handlers de error sigan reportando. Es lo que evita que el emulador se caiga en vez de
avisar.

### 1.4 Sondeo del bus G2

`0xA0600000-0xA06007FF` es el área de dispositivos externos. Devolver un valor fijo que
signifique "no hay nada" en vez de dejar el destino sin tocar. Hoy el boot ROM lee basura
no determinista y el arranque no es reproducible.

### 1.5 Traza de memoria

`--traza-mem` que reporte a stderr, deduplicadas, las direcciones sin emular con el PC que
las pidió, y un anillo con los últimos N PC volcado cuando el PC se queda quieto. Es lo que
convierte "no arranca" en "se traba acá esperando esto".

**Verificación del hito A**: con `--traza-mem`, el PC ya no se queda fijo en el bucle
`SLEEP`/`BRA` de `0x8C000006` y aparecen accesos a `0xA05F70xx`.

## Fase 2 — Estado del sistema

Trabajo chico, todo previo al GD-ROM.

### 2.1 Flash ROM

`cargar_bios()` solo lee `bios.bin`. Agregar la carga de `bios/flash.bin` (128 KB) a un
bloque propio y extender `bios_read()` para cubrir `0x00200000-0x0021FFFF`. La sonda ya vio
al boot ROM leer `0xA021A000`, `0xA021A004` y `0xA021A056-5C`: ahí están región, idioma,
fecha y nombre de la consola.

Si el archivo no está, generar una flash mínima válida en memoria en vez de fallar: que no
tener `flash.bin` no impida arrancar.

### 2.2 Respaldo del bloque de control

Mapear `0x005F0000-0x005FFFFF` sobre `control_mem` para lectura y escritura, y dejar el
`switch` de `pvr_read()`/`pvr_write()` solo para los registros con efecto. Son 89
direcciones que hoy se escriben y se pierden.

### 2.3 Reloj de tiempo real

`0xA0710000` y `0xA0710004` ya están en el `switch` pero solo loguean. Devolver el contador
de segundos desde 1950, partido en dos registros de 16 bits, tomado del reloj del host.

## Fase 3 — GD-ROM

Es el grueso. `gdrom.c` nuevo, enganchado desde `mem.c` para el rango `0x005F7000-0x005F709C`.

### 3.1 Registros

El bloque tiene forma de interfaz ATA. Por lo que se vio en la sonda, el boot ROM lee
`0x005F7018` (estado alternativo) y escribe `0x005F709C` (comando), lo que coincide con el
mapa conocido: datos en `0x7080`, error/features en `0x7084`, razón de interrupción en
`0x7088`, número de sector en `0x708C`, contador de bytes en `0x7090`/`0x7094`, selección de
unidad en `0x7098`, estado/comando en `0x709C`. **Confirmar el mapa completo contra la
documentación antes de escribir código**: media hora de lectura ahorra un día de depuración.

### 3.2 Máquina de estados

Los bits de estado —BSY, DRQ, DRDY, CHECK— y las transiciones al escribir un comando, al
leer o escribir el registro de datos, y al terminar una transferencia. Es lo que el boot
ROM está esperando cuando hoy da vueltas.

Además, el estado de la unidad: bandeja abierta, sin disco, disco leído, listo. El boot ROM
decide entre su menú y el arranque del juego según eso, así que hay que poder simular las
dos situaciones desde la línea de comandos.

### 3.3 Comandos del modo paquete

El comando ATA `PACKET` recibe un paquete de 12 bytes con el comando real. Los que hay que
cubrir para el arranque son los de prueba de unidad, estado, modo, TOC y lectura de
sectores. `hack_gdrom()` en `dcopcodes.c` ya resuelve dos de ellos —lectura de sectores y
TOC— contra `iso.c`; esa lógica se muda a `gdrom.c` y pasa a alimentarse desde el
protocolo en vez de desde la syscall.

Empezar por el mínimo: prueba de unidad, estado y TOC. Con eso alcanza para el hito B.
Lectura de sectores para el hito C.

### 3.4 Interrupciones y DMA

Fin de comando por el ASIC —`intc_add()` ya existe— y la transferencia de sectores, primero
por PIO (leyendo el registro de datos) y después por el DMA del G2 (`0x005F7400`), que es
como lee de verdad. El DMA del SH-4 (`dma_check()` en `main.c`, hoy solo un log) hace falta
para lo mismo.

**Verificación del hito B**: arrancar con `--bios` sin imagen; la BIOS tiene que llegar a
su pantalla de "sin disco" y dejar de pedirle cosas a la lectora.

**Verificación del hito C**: arrancar con `--bios demos/roto/...` (o una ISO de KOS); la
BIOS tiene que leer la TOC, cargar IP.BIN a `0x8C008000` y saltar a `0x8C00B800` sola. A
partir de ahí el camino es el que ya funciona.

## Fase 4 — Retirar los hooks de syscall

Cuando el hito C funcione, `BIOS_HACKS` deja de ser necesario para las imágenes que
arrancan por BIOS. No sacarlo: pasarlo a opción, porque sigue siendo la forma de correr un
`.bin` suelto sin IP.BIN válido.

## Fase 5 — AICA mínimo

Para el hito C probablemente alcance con:

- Mapear los 2 MB de `sound_mem` en `0xA0800000`. Ya está reservado en
  `inicializar_memoria()` y nunca se le asignó zona; el caso puntual `0xA080FFC0` del
  `switch` es un parche que seguramente tapaba justo eso.
- Registros del AICA en `0xA0700000-0xA0702FFF` con respaldo real y los de estado
  devolviendo algo coherente.

El ARM7 y los 64 canales del sintetizador quedan para el hito D. Son un proyecto del tamaño
del núcleo SH-4 y no bloquean el arranque, solo el sonido.

## Fase 6 — La animación del logo

Cuando el arranque llegue hasta ahí, revisar en `graficos.c`: texturas YUV422, volúmenes
modificadores, render a textura y el orden por listas completo. No es un bloqueo, es lo que
se ve después.

## Cómo se prueba

Tres niveles, del más barato al más caro:

1. **Unitario**, en `tests/`: el handshake de PDTRA es una función pura de PCTRA; la
   máquina de estados del GD-ROM es una función pura de (estado, registro escrito). Las dos
   se prueban con el arnés que ya existe, sin SDL. Una suite nueva por pieza, y la de
   cobertura no aplica porque no son opcodes.
2. **Traza**, con `--traza-mem`: después de cada fase, correr el boot ROM y comparar la
   lista de direcciones sin emular contra la de la fase anterior. Tiene que achicarse.
3. **Visual**: los hitos B y C se ven en pantalla. En la práctica hubo que agregar un
   cuarto camino, porque la ventana no se ve: **F5**, que vuelca la RAM de video a un BMP
   sin pasar por OpenGL. Es además el único que sirve para automatizar la verificación.

## El riesgo real

**El boot ROM es más estricto que el hack de syscalls.** Hoy `hack_gdrom()` le da al
programa lo que pide y nadie valida nada. El boot ROM real verifica la información de
licencia de IP.BIN y la región contra la flash, y se niega a arrancar si no le cuadran. Es
perfectamente posible llegar al hito C con la lectora andando y que igual no arranque, no
por un bug del emulador sino porque la imagen no pasaría en una consola real.

Mitigación: probar primero con una imagen que sí arranque en hardware, y tener a mano el
camino viejo (fase 4) para comparar. Si una imagen arranca por `BIOS_HACKS` pero no por
BIOS, lo primero a mirar es la imagen, no el código.

El segundo riesgo, menor: el mapa de registros del GD-ROM y el juego de comandos del modo
paquete están documentados por ingeniería inversa de la época, no por Sega. Conviene
contrastar dos fuentes antes de dar un valor por bueno.

## Estimación

- Fase 1: una tarde. Es el mejor retorno de todo el plan — cuatro cambios chicos y el halt
  desaparece.
- Fase 2: un día.
- Fase 3: la grande. Entre tres días y una semana, según cuánto haya que depurar la máquina
  de estados. En cualquier emulador de DC es del orden de mil líneas.
- Fases 5 y 6: sin estimar, dependen de hasta dónde se quiera llegar con el hito D.

Hasta el hito C, del orden de una semana de trabajo enfocado.

## Lo que quedó

### Archivos nuevos

| archivo | qué es |
| --- | --- |
| `opciones.c/h` | la línea de comandos: `--bios`, `--cable=`, `--bandeja=`, `--traza-mem`, `--sin-hacks-bios` |
| `sistema.c/h` | handshake de PDTRA/PCTRA, flash ROM (carga o síntesis) y RTC |
| `gdrom.c/h` | la lectora: registros ATA, máquina de estados, comandos del modo paquete y DMA del G2 |
| `traza.c/h` | `--traza-mem`: direcciones sin emular deduplicadas y detección de bucles con desensamblado |
| `volcar_framebuffer()` en `graficos.c` | tecla F5: vuelca la RAM de video a `captura.bmp` sin pasar por OpenGL |
| `tests/test_sistema.c`, `tests/test_gdrom.c` | 26 casos nuevos; la suite completa quedó en 413 |

### Fase por fase

- **1.1 modo de arranque**: `--bios [imagen]` deja `PC = 0xA0000000` y saltea la carga
  manual. La imagen, si se pasa, se monta con `iso_init()`; si no se puede montar se
  arranca igual con la bandeja vacía, que es el hito B.
- **1.2 PDTRA/PCTRA**: `sistema_pdtra()` en `regmap_read()`, con `--cable=vga|rgb|compuesto`.
  Desbloquea el halt: el PC ya no se queda en `0x8C000006`.
- **1.3 zonas sin mapear**: apuntan a `descarte_mem`, y el bucle de `mem_hash_setup()`
  ahora cubre también la zona `0xFF` (iba hasta `0xFE`). `mem_read_error()` además
  devuelve ceros en vez de dejar el destino como estaba.
- **1.4 bus G2**: `0x00600000-0x006007FF` contesta un valor fijo.
- **1.5 traza**: `--traza-mem`. Reporta cada dirección sin emular una sola vez con el PC
  que la pidió, y cuando el anillo de 96 PC tiene 64 valores distintos o menos durante
  cuatro millones de instrucciones vuelca el anillo, **desensambla el bucle** y muestra
  los registros. `gdrom.c` además reporta por ahí los comandos y los paquetes.
- **2.1 flash**: se carga `bios/flash.bin` a un bloque propio y `bios_read()` cubre
  `0x00200000-0x0021FFFF`. Sin archivo se sintetiza una flash borrada con las cabeceras
  `KATANA_FLASH____` de las particiones 2, 3 y 4 y el bloque de identificación de la
  partición 0, copiado en estructura del volcado de una consola real. De paso se arregló
  que `bios_read()` leía fuera de `bios_mem` para todo lo que pasara de 2 MB.
- **2.2 bloque de control**: `0x005F0000-0x005FFFFF` se lee y se escribe contra
  `control_mem` en los dos sentidos, y ya no se reporta como error.
- **2.3 RTC**: `0xA0710000` y `0xA0710004` devuelven los segundos desde 1950 tomados del
  reloj del host, con latch para que la pareja de lecturas no se parta.
- **3 GD-ROM**: `gdrom.c`, enganchado desde `mem.c` para `0x005F7000-0x005F70FF` y
  `0x005F7400-0x005F74FF`. Registros ATA, bits BSY/DRQ/DRDY/CHECK, razón de interrupción,
  estado de la unidad y tipo de disco, los comandos `TEST_UNIT`, `REQ_STAT`, `REQ_MODE`,
  `SET_MODE`, `REQ_ERROR`, `GET_TOC`, `REQ_SES`, `CD_OPEN`, `CD_SEEK`, `CD_READ`,
  `GET_SCD` y los dos sin nombre publicado (`0x70` y `0x71`), transferencia por bloques
  DRQ encadenados y por el DMA del G2. La lógica de TOC y de lectura de sectores que
  estaba en `hack_gdrom()` se mudó acá y el hook de syscall la usa desde `gdrom.c`.
  El mapa de registros y los códigos de comando están contrastados contra el controlador
  de GD-ROM del kernel de Linux y contra el núcleo de reicast.
- **3.4 interrupciones**: el fin de comando de la lectora es el bit 0 del registro
  **externo** del ASIC (`SB_ISTEXT`), que `intc.c` no manejaba: se agregó `intc_add_ext()`
  y una segunda cola que `check_ints()` drena contra las máscaras `_B`. El fin de DMA sí
  va por el registro normal. Leer el registro de estado de la lectora baja la
  interrupción, como manda ATA; leer el estado alternativo no.
- **3.4 DMA del SH-4**: `dma_check()` hacía sólo un log y encima estaba comentada dentro
  de `main_loop()`. Ahora transfiere de verdad, respetando el tamaño de unidad y los modos
  de avance de origen y destino, y se llama junto con `timer_check()`. Sólo actúa sobre
  los canales en auto-request: las transferencias que pide un periférico las hace el ASIC.
- **4 hooks de syscall**: pasaron a ser opción (`--hacks-bios` / `--sin-hacks-bios`), con
  `BIOS_HACKS` como interruptor de compilación por encima. Con `--bios` se apagan solos,
  porque escriben código justo donde el boot ROM se instala.
- **5 AICA mínimo**: los 2 MB de `sound_mem` quedaron mapeados en `0xA0800000` y los
  registros en `0xA0700000-0xA0707FFF` tienen respaldo real. Con eso desaparecieron las
  1050 escrituras sin emular que quedaban.

### Una herramienta que hizo falta: el volcado del framebuffer (F5)

La verificación visual que pide este plan no se podía hacer: la ventana sale en negro y
ninguna captura de pantalla la ve. Se bisectó hasta `a04be29`, el commit del port a MSVC
cuya propia documentación dice que el rotozoomer «gira y hace zoom, animada y estable»,
y **ahí también sale en negro**. O sea que no lo rompieron ni estos cambios ni los de
opcodes: es del entorno, no del código.

La tecla **F5** vuelca la RAM de video a `captura.bmp`, sin pasar por OpenGL. Con eso se
resolvió la duda de fondo:

- **`roto.bin` sí dibuja.** El volcado tiene la textura XOR rotada y con zoom, en su
  ventana de 320x240. El intérprete, la FPU y el mapa de memoria están bien; lo que falla
  es la presentación.
- **La BIOS no dibuja.** El framebuffer queda entero en cero, aunque la BIOS sí configura
  el modo de video: el formato pasa de RGB565 (el valor por omisión de `graficos.c`) a
  ARGB0555, así que la escritura a FB_R_CTRL llegó.

Con `--traza-mem`, F5 además vuelca el anillo de PC y desensambla todo lo que haya en él,
no sólo los bucles chicos.

### Qué se verificó

**Hito A: sí.** Con `--traza-mem` el PC ya no se queda en el `SLEEP`/`BRA` de
`0x8C000006` y aparecen accesos al bloque del GD-ROM.

**El protocolo del GD-ROM anda de punta a punta.** Con una imagen montada, el boot ROM
real ejecuta esta secuencia, que es la de una consola:

```
TEST_UNIT
REQ_MODE  desde el byte 18, 8 bytes    -> "Rev 6.43"     (8 de 8 entregados)
TEST_UNIT
0x70 1f   (preparar el disco)
TEST_UNIT
0x71 1f   (autenticación)              -> 1024 de 1024
GET_TOC   sesión 0, 0x198 bytes        -> 408 de 408
```

Sin disco la conversación es más corta y también correcta: `TEST_UNIT` falla con la clave
de sentido «unidad no lista», la BIOS pide el error con `REQ_ERROR` y recibe sus 10 bytes.

**Dónde se detiene.** En los dos casos la BIOS deja de hablarle a la lectora y sigue
corriendo, pero sin avanzar. Con F5 y `--traza-mem` se ve exactamente dónde: en su propio
planificador de tareas. El anillo de PC muestra el conmutador de contexto de
`0x8C001918`-`0x8C00196C` (guarda R8-R14, MACH y PR y cambia de pila), el despacho por
tabla de `0x8C0010F0` (`CMP/HS`, `SHLL2 R7`, `MOV.L @R7,R7`, `JMP @R0`) y, en el fondo,
una espera en `0x8C003716`:

```
8c003716: MOV.L  @(5, R14), R0
8c003718: CMP/EQ #2, R0
8c00371a: BF     8c003752      ; sigue esperando
```

Es decir: la máquina de estados de la BIOS espera que una variable llegue a 2 y nunca
llega. No es un cuelgue del emulador —no hay direcciones sin emular, no hay bucle cerrado,
el planificador sigue rotando tareas—, es que le falta algo que la lectora de verdad hace
y esta no. Averiguar qué es ingeniería inversa de la BIOS y queda fuera de este plan.

Para el caso con disco, además, sigue en pie lo que este mismo documento anticipa en
[El riesgo real](#el-riesgo-real): la lectora se presenta como CD-ROM y la imagen de
prueba no es un GD-ROM con área de alta densidad, así que el boot ROM tendría motivo para
rechazarla igual.

**Direcciones sin emular: cero.** Al terminar la fase 5 no queda ninguna en toda la
corrida del boot ROM. La lista de la sonda original —89 registros del bloque de control
más 1050 escrituras al AICA— quedó vacía.

**Sin regresiones en el camino de siempre.** `dcemu roto.bin` carga igual y corre igual;
se comparó contra una compilación de `39d76c8` en un worktree aparte y la ventana se ve
idéntica.

## Segunda corrida: el vsync de SPG_STATUS

Al volver a probar el arranque después del trabajo de MMU y FPU, el punto de parada ya no
era el planificador de `0x8C003716`: era este bucle, a los 0,474 s de tiempo emulado.

```
8c00cb2e: MOV.L  @R5, R2      ; R5 = 0xA05F810C, SPG_STATUS
8c00cb30: TST    R4, R2       ; R4 = 0x2000
8c00cb32: BT     8c00cb2e     ; repite mientras el bit valga cero
```

`0x005F810C` es **SPG_STATUS**, y el bit 13 es el **vsync**. `pvr_read()` devolvía ahí
`pvr_scanline` y nada más: solo el número de línea, sin ninguno de los bits de sincronía.
El boot ROM esperaba un vsync que no llegaba nunca.

El registro completo es:

| bits | campo |
| --- | --- |
| 9:0 | línea de barrido |
| 10 | número de campo |
| 11 | blanking vertical |
| 12 | hsync |
| 13 | vsync |

Ahora se compone: el vsync se enciende durante las primeras `SPG_WIDTH.vswidth` líneas del
cuadro y el blanking entre `SPG_VBLANK.vbstart` y `vbend`, que envuelve por el final. hsync
y el número de campo quedan en cero — dcemu no lleva posición horizontal ni entrelazado, y
nadie los ha pedido todavía. Si `vswidth` sale cero se fuerza a una línea, porque si no
quien espere el vsync se cuelga igual.

Con eso aparecieron **dos escrituras que faltaban**: `SPG_VBLANK` (`0xA05F80DC`) y
`SPG_WIDTH` (`0xA05F80E0`) solo tenían caso de lectura, así que `pvr_spg_vblank` y
`pvr_spg_width` se quedaban para siempre en el valor por omisión de `reg.c` aunque el guest
los programara. Sus vecinos `SPG_LOAD`, `SPG_CONTROL` y `SPG_HBLANK` sí se recogían.

### Hasta dónde llega ahora

Pasa el vsync, hace un `memset` de unos 630 KB, vuelve a esperar el vsync —y esta vez lo
consigue—, programa una tanda de registros del PVR y se detiene a los **0,546 s** aquí:

```
8c0dbdf4: MOV.L  0x8c0dbe5c, R3   ! 8c22ff94
8c0dbdf6: MOV.L  @R3, R0
8c0dbdf8: CMP/EQ #6, R0
8c0dbdfa: BT     8c0dbe00
8c0dbdfc: BRA    8c0dbdfc         <== se queda aquí a propósito
```

Lee la variable de `0x8C22FF94`, la compara con **6** y, si no coincide, entra en un bucle
infinito deliberado. Vale **5**. Es la misma clase de espera que la de `0x8C003716`
—máquina de estados de la BIOS que no completa un paso—, pero varios pasos más adelante:
antes esperaba un 2 y ni siquiera llegaba a este código.

Con imagen montada llega exactamente al mismo sitio y con los mismos valores.

## El watchpoint, y lo que encontró

Para saber quién escribe `0x8C22FF94` hacía falta un watchpoint de escritura, así que se
implementó: `WATCHPOINT` en `options.h`, con la dirección, el tamaño y un tope de informes.
El gancho está en `memwrite_fisico()` (`mem.h`), que es el único sitio por el que pasan
**todas** las escrituras —las del programa emulado llegan ahí después de traducir, y las
internas del DMA y de los callbacks entran directo—. La implementación vive en `traza.c`,
junto al resto del diagnóstico de arranque. Apagado no cuesta nada; encendido, dos
comparaciones por escritura. Ver `WATCHPOINT` en `options.h`.

Ocho informes en toda la corrida, y el último lo dijo todo:

```
watchpoint: 8c22ff94 = 00000005 (antes 00000000) -- escritura de 4 en 8c22ff94,
            PC 8c0dbc74, PR 8c0dbc6e, 103211502 ciclos
```

Desensamblando ese PC:

```
8c0dbc62: JSR    @R3            ; R3 = 8c0dd46e, lee un registro del PVR
8c0dbc66: MOV.L  R0, @(4,R15)   ; guarda el COREID
8c0dbc6a: JSR    @R3            ; segunda llamada...
8c0dbc6c: MOV    #4, R4         ; ...con el offset 4: la revisión
8c0dbc74: MOV.L  R5, @R2        ; *0x8C22FF94 = 5
8c0dbc7a: CMP/EQ R3, R1         ; ¿COREID == 0x17FD11DB?
8c0dbc7c: BF     8c0dbc86       ;   no -> se queda en 5
8c0dbc82: CMP/EQ #1, R0         ; ¿revisión == 1?
8c0dbc84: BF     8c0dbc8c       ;   no -> sigue
8c0dbc8e: CMP/HS R1, R4         ; ¿revisión >= 17?
8c0dbc90: BF     8c0dbca0       ;   no -> se queda en 5
8c0dbc96: MOV.L  R0, @R2        ; *0x8C22FF94 = 6   <== el 6
```

Es una **comprobación de identidad del chip PVR**. `COREID` (`0x005F8000`) ya devolvía
`0x17FD11DB`, pero **`REVISION` (`0x005F8004`) no tenía caso de lectura**: caía en el
respaldo del bloque de control, que vale cero, y cero no llega a 17. Una consola de serie
responde `0x11` ahí — que es exactamente el umbral que la BIOS exige, y por eso rechaza
también el 1.

Es el mismo tipo de trampa que `SB_G1SYSM`: un registro de identificación que contesta
cualquier cosa y manda al software por el camino equivocado, sin ningún mensaje de error.

### Hasta dónde llega con eso

La variable pasa a 6, el boot ROM sigue de largo y **corre hasta más allá de los 23 s de
tiempo emulado**, contra los 0,546 s de antes. El watchpoint muestra el ciclo completo
repitiéndose —la variable vuelve a 0, luego a 5 y otra vez a 6— o sea que está en un bucle
de nivel superior reintentando algo, alternando entre dos bucles de 26 y 45 instrucciones
alrededor de `0x8C0D9C50`.

**Direcciones sin emular: siguen siendo cero.** Y la pantalla sigue en negro: la traza
reporta que el guest escribió 0 bytes al framebuffer, así que todavía no llega a dibujar.

### Lo que no se alcanzó en esa corrida

- **Hito B: no.** La BIOS no llega a su pantalla de «sin disco». Se detiene esperando que
  una variable de su máquina de estados llegue a un valor que no llega —ver la segunda
  corrida, más arriba—. Lo que sí quedó verificado es que la lectora contesta bien todo lo
  que le preguntan, y que no queda ninguna dirección sin emular.

  *(Resuelto en la [tercera corrida](#tercera-corrida-el-ch2-dma-y-el-mando).)*
- **Hito C: no.** Requiere una imagen que arranque en hardware, que no hay en el
  repositorio, y probablemente además la geometría de GD-ROM.
- **Hito D: no**, y depende del C.
- **La presentación por OpenGL.** La ventana en negro es anterior a este trabajo y no es
  del emulador: el mismo commit que documentaba el rotozoomer andando también sale en
  negro hoy. Vale la pena mirarlo aparte —probablemente sea la cadena
  sdl12-compat → sdl2-compat → SDL3 o el driver—, y mientras tanto F5 alcanza para
  verificar.

  *(Falso: la ventana **sí** se ve. Una captura GDI del área de cliente muestra el
  contenido correcto, y así se verificó toda la tercera corrida. Ver `CLAUDE.md`, sección
  de captura de la ventana, y `docs/demos-kos.md`.)*

## Tercera corrida: el CH2 DMA y el mando

Aquí arranca la BIOS. Tres cosas, y las tres tenían la misma forma: **algo que el guest
pide y que dcemu acepta sin hacer nada y sin decir nada**.

### 1. El CH2 DMA, que es el que la tenía parada

Donde quedó la segunda corrida —dando vueltas alrededor de `0x8C0D9C50`— la BIOS está en
un bucle de espera. Desensamblando la rutina y volcando su tabla:

```
8c0d9cc6: BSR    8c0d9c2a      ; busca en la tabla de descriptores
8c0d9cca: CMP/EQ #1, R0        ; 1 = "no queda ninguno en curso"
8c0d9ccc: BF     8c0d9cc6      ; si queda alguno, vuelve a mirar. Para siempre
```

`0x8C0D9C2A` recorre 2 grupos de 8 descriptores de 32 bytes desde `0x8C204D74` y devuelve 0
en cuanto encuentra uno con los bits 0-1 de su palabra `+0x18` distintos de cero. Un
descriptor tenía `+0x18 = 0x1A`, o sea el estado 2: **transferencia en curso**.

El watchpoint sobre esa palabra dio el código que la pone en 2, y ahí estaba todo:

```
8c0d9a5e: MOV.L R0, @(6, R4)   ; estado = 2, "en curso"
8c0d9a64: MOV.L R3, @R0        ; R0 = 0xFFA00020 -> SAR2 del DMAC
8c0d9aa4: MOV.L R3, @R2        ; R2 = 0xA05F6800 -> SB_C2DSTAT, destino
8c0d9aaa: MOV.L R0, @R3        ; R3 = 0xA05F6804 -> SB_C2DLEN,  0x4800 bytes
8c0d9ab0: MOV.L R1, @R0        ; R0 = 0xA05F6808 -> SB_C2DST = 1, arranca
```

Es el **CH2 DMA**: el canal por el que el guest sube geometría y texturas al TA sin pasar
por las store queues. Lo conduce el Holly, no el DMAC — el SH-4 solo pone el origen en
`SAR2` y arma `CHCR2` en modo de petición externa —, así que `dma_check()` no lo ve: solo
atiende los canales en auto-request, y con razón. Los tres registros no tenían caso en
`pvr_write()`, caían en el respaldo de `control_mem` y se perdían en silencio.

Emularlo son cuarenta líneas (`ch2_dma_ejecutar()` en `mem.c`). El destino manda: si cae en
`0x10000000-0x107FFFFF` cada bloque de 32 bytes va al decodificador del TA —el mismo switch
que usa `pref142()`, extraído a `ta_procesar_bloque()`—, y si no, es una copia. Al terminar
deja `SB_C2DST` en 0, `DMATCR2` en 0, `CHCR2.TE` puesto, y levanta `ASIC_EVT_PVR_DMA`.

**Con eso la BIOS pasa de un bucle infinito a renderizar 51 tiras por cuadro** y muestra su
pantalla de fecha y hora, con el fondo del planeta. De paso arregla
`parallax-serpent_dma`, que estaba en la lista de demos rotas por exactamente esto.

### 2. El descriptor de Maple que valía cero

Con la BIOS en pantalla, el mando no hacía nada: la traza mostraba que el DMA de Maple
arrancaba y no procesaba ni una transferencia.

El descriptor era `td1 = 0x00000000`, `td2 = 0x0C3688E0`, marco `0x00002001`. Y `mem.c`
tenía:

```c
if (td1 == 0) { /* "SB_MDSTAR mal puesto" */ return; }
```

El descriptor tiene la longitud en los bits 0-7, el patrón en 8-15, el puerto en 16-17 y el
fin de lista en el 31. **Un marco de una sola palabra al puerto A que no sea el último de
la lista los deja los cuatro en cero**, así que cero es un descriptor perfectamente válido —
y es justo el primer sondeo del boot ROM, un `Device Request` al puerto A. KOS nunca lo
delata porque siempre marca la última transferencia y sus listas de un elemento dan
`0x80000000`.

Ahora se corta por la dirección de respuesta, que el guest pone siempre en RAM. De paso, el
descriptor de dispositivo que contesta dcemu estaba casi entero en cero: `function_data[0]`
—el campo que dice qué botones y ejes tiene el mando— y los nombres, que van rellenos con
espacios y no terminados en NUL.

### 3. El evento del ASIC que se descartaba

Con el sondeo arreglado la BIOS preguntaba `Device Request` **dos veces en toda la corrida**
y nunca `GetCondition`. Espera el fin del DMA por interrupción, y `check_ints()` terminaba
con:

```c
REMOVE_BIT(intc_queuemask, ASIC_ACK_A);
```

es decir: si en ese instante ninguna de las tres máscaras cubría el evento, se tiraba. En el
chip el bit de `SB_ISTNRM` queda puesto hasta que el guest lo acusa, y si habilita la
máscara después la interrupción llega igual. **Es el mismo error que tenían los
temporizadores** antes de `intc_revisar_sh4()` (ver [clock-plan.md](clock-plan.md)), solo
que del lado del ASIC. El boot ROM habilita la máscara después de arrancar el DMA, así que
perdía el fin de transferencia siempre y no volvía a sondear el bus nunca más.

Ahora el bit se queda pendiente y lo limpia el guest al acusar en `SB_ISTNRM`. El sondeo
pasa de 2 veces por corrida a 77 por segundo de tiempo emulado.

### Hasta dónde llega

Con `--bios` y sin imagen:

1. Pantalla **Set Date/Clock**, con el fondo del planeta y el texto legible.
2. El mando responde: las flechas mueven el cursor y cambian los campos.
3. Confirmando en *Select* se entra al **menú principal** —Play, File, Music, Settings—
   con la fecha del RTC del anfitrión en la barra superior.
4. Los submenús funcionan: *Music* abre el reproductor de CD con el disco animado.

O sea, **hito B alcanzado y algo más**: no es la pantalla de «sin disco», es la interfaz
completa de la consola.

**Hito C: no en esta corrida.** Hacía falta una imagen que arranque en hardware, y seguía en
pie lo de [El riesgo real](#el-riesgo-real) y la geometría de GD-ROM. La imagen de prueba del
repositorio no tiene área de alta densidad: `iso.c` reporta *error al tratar de leer 7
sectores desde sector 45150*.

*(Alcanzado en la [cuarta corrida](#cuarta-corrida-el-hito-c-o-cómo-el-rom-arranca-el-juego),
y por un camino que no era éste: el disco no hay que disfrazarlo de GD-ROM, hay que
presentarlo como el CD que es.)*

### Herramientas que hicieron falta

Todo esto se midió con cuatro opciones nuevas, y ninguna existía al empezar:

| opción | para qué |
| --- | --- |
| `--desensamblar=D:N` | leer el código del boot ROM, que vive en RAM y no está en `bios.bin` en la misma dirección |
| `--volcar=D:N` | leer sus tablas — así se vio que el descriptor estaba en estado 2 |
| `--salir-tras=N` | salir solo a los N segundos de tiempo **emulado**, por el mismo camino que cerrar la ventana, para que el desensamblado y el volcado alcancen a imprimirse. Matar el proceso desde afuera se los lleva por delante |
| `--watchpoint=D[:T]` | era un `#define` y cada pregunta costaba recompilar el emulador entero |

Y dos trazas nuevas bajo `--traza-mem`: el CH2 DMA (origen, destino y tamaño) y el Maple
(selección de disparo, habilitación, y una línea por cada par puerto/comando).

### Lo que quedó afuera a propósito

- **Escritura de la flash.** `bios_read()` cubre la flash, pero escribirla no: el chip
  usa una secuencia de desbloqueo, y tratar esas escrituras como datos corrompería la
  imagen. Mientras tanto los cambios de configuración de la BIOS se pierden, y la traza
  los reporta.
- **La geometría de GD-ROM.** Las imágenes se presentan como CD-ROM de una pista de
  datos, que es lo que `iso.c` sabe leer. Un GD-ROM de verdad tiene dos áreas de densidad
  y el área de alta empieza en el FAD 45150; cambiarlo implica leer `.gdi`/`.cdi` y mover
  la base de FAD, y arrastraría al hook de syscall que sigue usando la misma geometría.
- **La fase 6.** Es revisión de `graficos.c` para la animación del logo, y sólo tiene
  sentido cuando el arranque llegue hasta ahí.

## Cuarta corrida: el hito C, o cómo el ROM arranca el juego

**Aquí arranca el juego.** Con `--bios` y el `.cdi` presentado como lo que es —un CD
selfboot, **sin** `DCEMU_COMO_GD`— el boot ROM 1.01d hace el camino entero: reconoce el
disco, lee el IP.BIN del principio de la pista de datos, el descriptor de volumen y el
directorio raíz, encuentra `1ST_READ.BIN`, carga sus 1.468.208 bytes y salta. El PC queda
dentro del ejecutable, en `0x0C14xxxx`.

Es el mismo sitio al que llega el camino de siempre —sin `--bios`, por los hooks de
syscall—, así que lo que el juego haga desde ahí es otro problema, y es anterior a esto.

### Dónde decide el ROM

Todo esto se desensambló de la RAM con `--traza-desde`, porque el código del ROM no está en
`bios.bin` en la misma dirección.

`0x8C000AE0` es la rutina de arranque de alto nivel:

```
8c000afe: MOV.L @(39, GBR), R0    ; *(0x8C0000E4), el motivo de reinicio
8c000b00: CMP/EQ #1, R0           ; 1 = "vuelvo de menu(1)" -> al menú
...
8c000b3e: MOV.L @(13, GBR), R0    ; *(0x8C00004C), el tipo de disco
8c000b40: CMP/EQ #20, R0          ; 0x20 = CD-ROM/XA -> rama MIL-CD
8c000b42: BF     8c000b6c         ;              si no -> rama GD-ROM
```

La rama MIL-CD (`0x8C000B44`) carga su propio manejador en `0x8CE00000` —un blob que el ROM
reubica ahí— y le pregunta por las sesiones a través de la syscall de GD-ROM (`0x8C0000BC`,
comando 35 = `GETSES`). Si le cuadra, pone `*(u8*)0x8C000024 = 2` y sigue al arranque.

`0x8C0004F4` valida la cabecera del IP.BIN —identificador de hardware, de fabricante, y la
cadena de región contra el índice que trae la flash— y deja el puntero al nombre del
ejecutable en `GBR+0x9C`.

`0x8C000D40` lee el descriptor de volumen, después el directorio raíz, y recorre los
registros ISO9660 comparando cada nombre con ése. Cuando encuentra el archivo calcula su FAD
y su cantidad de sectores y entonces:

```
8c000ddc: CMP/HS R10, R14         ; R14 = FAD del archivo, R10 = 0x6DDD0
8c000dde: BT     8c000dee         ;   por encima -> cargar
8c000de0: MOV.B  @(24, GBR), R0   ; *(0x8C000024)
8c000de4: CMP/EQ #2, R0
8c000de6: BT     8c000dee         ;   == 2 -> cargar
8c000de8: MOV.L  @(8c000e24), R2
8c000dea: BSRF   R2               ; si no, menu(1): al menú, y no vuelve
8c000dec: MOV    #1, R4
```

Esa puerta es la que explica por qué `DCEMU_COMO_GD` no podía funcionar: presentando el
disco como GD-ROM el ROM toma la otra rama, nadie pone el 2, y el `1ST_READ.BIN` de estas
conversiones cae por debajo del umbral. El ROM deja la dirección de carga en `0x8C0000F8`
(`0x8C010000`), el FAD en `0x8C0000F0` y los sectores en `0x8C0000F4`.

### Las cinco de la lectora

Cada una tapaba a la siguiente, y cuatro de las cinco tienen la misma forma que todo lo
demás en este proyecto: **algo que el guest lee y que dcemu contesta sin querer decir nada**.

1. **La respuesta de `REQ_SES` iba corrida un byte.** El segundo byte está reservado y no
   estaba, así que el conteo de sesiones caía en `[1]` y el byte alto del FAD en `[2]`. El
   driver del ROM le devuelve al llamador justamente el byte `[2]`, así que el manejador de
   MIL-CD leía **5** —el byte alto del lead-out— donde esperaba **2 sesiones**, daba el disco
   por no arrancable y se iba al menú. Esto tenía cerrada la rama del CD entera.
2. **`CD_READ` rechazaba todo lo que pasara del FAD 45150 en un disco que no fuera
   GD-ROM.** El *área* de alta densidad no la tiene un CD, pero los *sectores* sí: un CD de
   700 MB llega más allá del FAD 358000, y ahí es donde estas conversiones ponen el
   `1ST_READ.BIN`. El límite ahora es el lead-out, el mismo número que contesta `REQ_SES(0)`.
3. **El DMA del G2 daba el comando por terminado en la primera ráfaga.** De la segunda en
   adelante encontraba «sin datos pendientes» y no movía nada, aunque el guest siguiera
   programando destinos y viendo terminar cada DMA. El bootstrap del IP.BIN trae el
   ejecutable **cifrado**: 45882 ráfagas de 32 bytes a direcciones dispersas, que es como lo
   descifra sobre la marcha. Llegaban los primeros 32 bytes y 1,4 MB de basura.
4. **`SB_GDSTARD` y `SB_GDLEND` (`0x005F74F4`/`0x005F74F8`) no existían.** El driver del ROM
   lee `SB_GDLEND` para saber cuánto de la lectura ya llegó y lo guarda en su bloque de
   comando; lo leía del respaldo del bloque de control, o sea memoria sin inicializar, y se
   la pasaba tal cual al llamador.
5. **El bit `ABRT` del registro ERROR.** `fallar()` escribía sólo la clave de sentido. El
   bootstrap termina la carga sin llevarse el relleno del último sector —lee el tamaño del
   archivo, no los 717 sectores— y cierra con un `NOP` de ATA; el driver lee ERROR, prueba
   `& 4` y, sin ABRT, da el aborto por no ocurrido y deja el comando en «transfiriendo» para
   siempre. Ése era el último eslabón:

   ```
   8c002dc8: CMP/EQ #1, R0        ; ¿CHECK puesto en STATUS?
   8c002dd4: MOV.B  @(R0, R3), R0 ; lee ERROR
   8c002dd8: AND    #4, R0        ; el bit ABRT
   8c002dda: CMP/EQ #4, R0
   8c002ddc: BT     8c002e08      ; abortado -> cerrar el comando
   ```

### Herramientas que hicieron falta esta vez

| opción | para qué |
| --- | --- |
| `--watchpoint-lectura=D[:T]` | quién **mira** una dirección. Una línea por PC distinto. Encontró en un intento al que evalúa el directorio raíz recién entregado |
| `--traza-desde=PC[:N[:K]]` | leer una decisión entera: N instrucciones desde PC, con los registros que cambian. `K` salta llegadas, porque el ROM pasa dos veces por el mismo código |
| `DCEMU_TRAZA_ATA=cmd:N` | lo mismo disparado por la lectora: qué hace el driver del guest con lo que le acaban de contestar |

Y un tope de 4096 informes a las direcciones sin emular: la deduplicación es por dirección,
y un guest que se va por un puntero suelto recorre millones de direcciones **distintas**. Sin
tope el log se fue a los gigabytes y la ventana dejó de responder.

### Qué disco es cada uno, y por qué importa

Lo que la lectora dice del disco decide la rama entera. Tres cosas lo decidían mal:

- **`iso_es_gdrom()` daba GD-ROM por la posición de la pista de datos.** Un `.cdi` describe
  un CD normal; que su pista de datos empiece arriba del LBA 45000 sólo quiere decir que
  delante hay 92 MB de relleno. Ahora sólo es GD-ROM bajo `DCEMU_COMO_GD`, que quedó como
  experimento.
- **Las sesiones se buscaban sólo por «audio y después datos».** Lo que de verdad separa dos
  sesiones es **el hueco**: dentro de una sesión las pistas van pegadas salvo el pregap de
  150 sectores, y cerrar una y abrir otra cuesta unos 11400 —que es justo lo que tienen todas
  las imágenes de prueba—.
- **Y la TOC se partía en dos áreas de densidad en cualquier disco.** Esas áreas sólo existen
  en un GD-ROM; en un CD hay una TOC y punto. Con el corte en el FAD 45150, una imagen que
  deja el relleno en la pista 1 y el juego justo en el 45150 contestaba un área 0 con la
  pista de relleno sola: el juego desaparecía de la TOC.

### La TOC iba al revés en el cable

Lo último que faltaba, y lo que tenía cerrados los selfboot de **datos/datos**.

En el cable cada entrada de la TOC lleva **el byte de control primero** y el FAD detrás en
big-endian, como el resto de las respuestas SPI. `struct TOC` la guarda al revés —control en
los bits 31-28, FAD en los 23-0— porque así la quiere el que la recibe: **el driver de
GD-ROM de la ROM da vuelta cada palabra** antes de entregársela a su llamador, y en ese
formato es en el que KOS la lee (`TOC_CTRL`, `TOC_LBA`). El hook de syscall se salta al
driver, así que ahí la estructura va tal cual; por el cable hay que invertir.

Se midió de la única forma que lo demuestra: dcemu mandaba `41000096` y el guest leía
`96000041`, la palabra dada vuelta byte a byte. Sin invertir, el manejador de MIL-CD —que en
`0x8CE003B6` mira si la primera pista es de datos con un `AND #40`— lo leía del byte
equivocado y rechazaba discos buenos. Da la casualidad de que en un audio/datos el byte
equivocado también daba «no es de datos», y por eso ése era el único formato que arrancaba.

Con esto arrancan los dos formatos, y el ROM encuentra el `1ST_READ.BIN` en todos:

| imagen | pista 1 | pista 2 | formato | 1ST_READ.BIN |
| --- | --- | --- | --- | --- |
| Crazy Taxi (DCRES) | LBA 0, 302, **audio** | LBA 11702, 346490, datos | audio/datos | FAD 357623, 717 sec |
| DCDoom | LBA 0, 302, **audio** | LBA 11702, 18487, datos | audio/datos | FAD 11895, 506 |
| Crazy Taxi (USA) | LBA 0, 33600, datos | LBA 45000, 306552, datos | datos/datos | FAD 350835, 717 |
| Virtua Tennis (USA) | LBA 0, 33600, datos | LBA 45000, 314830, datos | datos/datos | FAD 358810, 1018 |
| Capcom vs. SNK (USA) | LBA 0, 33600, datos | LBA 45000, 314569, datos | datos/datos | FAD 358225, 1174 |

### Lo que queda

- **`Virtua Tenis 2 (USA).cdi` no se parsea.** `cdi.c` no le encuentra pistas y la lectora
  queda sin disco (`unidad=7 formato=0`). Es del lector de DiscJuggler, no del arranque: las
  otras cinco imágenes se parsean bien.
- **mame4all: arreglado el arranque, queda la geometría del framebuffer.** Era al revés de lo
  que decía esta línea antes: el camino del `.bin` suelto **no** desciframos nada, y el
  `1st_read.bin` de `roms/mame4all/` **sí** viene cifrado —descifrarlo da byte a byte el
  `mame4all.bin` que la misma carpeta trae al lado—. Ahora se detecta por el prólogo de
  entrada (`parece_cifrado()`), y con la carpeta empaquetada en un `.iso` mame4all arranca y
  **dibuja su menú**.

  Lo que queda ahí es de vídeo y está medido: el contenido del framebuffer tiene un periodo
  de fila de **640 bytes** —autocorrelación sobre un volcado de `0xA5010000`, 0.783 contra
  0.766 de 1280— mientras `FB_R_SIZE` declara `x_size` 319, o sea 320 palabras de 32 bits =
  1280 bytes. dcemu le cree al registro, lee las filas al doble de largo y el menú sale
  duplicado a lo ancho y aplastado a la mitad de alto. `SCALER_CTL` no lo escribe nunca, así
  que el doblado horizontal por hardware no es la explicación. Sin resolver.
- **El juego arranca pero no dibuja.** Tras el salto el ejecutable corre —el PC recorre
  `0x0C14xxxx`-`0x0C17xxxx`— y termina leyendo por punteros que no apuntan a nada
  (`0x10000011` en adelante, de a 0x2C). Lo mismo pasa por el camino de siempre, que carga
  el `1ST_READ.BIN` a mano y descifra 2084052 bytes sin quejarse, así que no es del arranque
  por BIOS: es el primer problema de emulación *del juego*, y es una investigación nueva.
