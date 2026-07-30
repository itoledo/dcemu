# Plan: arrancar dcemu desde el boot ROM

Estado: **fases 0 a 5 implementadas**. Julio de 2026, sobre `master` (`39d76c8`). El
inventario de lo que faltaba, y cómo se midió, está en [bios-boot.md](bios-boot.md); este
documento es el orden de trabajo, y al final está lo que quedó andando y lo que no.

Resumen: el emulador ya no se cuelga, corre el boot ROM real y le habla al GD-ROM hasta
leer la TOC. El hito A está verificado. El B no se alcanza: la BIOS termina la conversación
con la lectora y se queda dando vueltas en su propio planificador de tareas sin dibujar
nada. Ver [Lo que quedó](#lo-que-quedó).

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

### Lo que no se alcanzó

- **Hito B: no.** La BIOS no llega a su pantalla de «sin disco». Se detiene esperando que
  una variable de su máquina de estados llegue a un valor que no llega —ver la segunda
  corrida, más arriba—. Lo que sí quedó verificado es que la lectora contesta bien todo lo
  que le preguntan, y que no queda ninguna dirección sin emular.
- **Hito C: no.** Requiere una imagen que arranque en hardware, que no hay en el
  repositorio, y probablemente además la geometría de GD-ROM.
- **Hito D: no**, y depende del C.
- **La presentación por OpenGL.** La ventana en negro es anterior a este trabajo y no es
  del emulador: el mismo commit que documentaba el rotozoomer andando también sale en
  negro hoy. Vale la pena mirarlo aparte —probablemente sea la cadena
  sdl12-compat → sdl2-compat → SDL3 o el driver—, y mientras tanto F5 alcanza para
  verificar.

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
