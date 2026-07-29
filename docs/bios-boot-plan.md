# Plan: arrancar dcemu desde el boot ROM

Estado: **planificado**. Julio de 2026, sobre `master` (`86d87ad`). El inventario de lo
que falta, y cómo se midió, está en [bios-boot.md](bios-boot.md); este documento es el
orden de trabajo.

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
3. **Visual**: los hitos B y C se ven en pantalla.

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
