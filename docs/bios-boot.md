# Qué falta para bootear la BIOS

Estado: **investigación**. Julio de 2026, sobre `master` (`52355a5`), con `bios/bios.bin`
(2 MB) y `bios/flash.bin` del repositorio de trabajo.

Hoy dcemu nunca ejecuta el boot ROM: `main()` carga `ip.bin` y `1st_read.bin` a mano y
arranca en `0x8C00B800`, saltándose el arranque real. Este documento es la lista de lo que
falta para que el emulador parta en el vector de reset (`0xA0000000`) y llegue al menú de
la consola, ordenada por lo que bloquea primero.

## Cómo se midió

No es una lista teórica. Se instrumentó el emulador de forma temporal:

- `PC = 0xA0000000` en vez del bootstrap de IP.BIN.
- `mem_read_error()` y `mem_write_error()` reportan cada dirección distinta que se toca
  sin emulación, con el PC que la pidió.
- Un anillo con los últimos 96 PC, volcado al detectar el bucle de halt.

Con eso se corrió el boot ROM real y se anotó dónde se traba y qué pide. La
instrumentación se revirtió; no quedó nada en el árbol.

**El resultado más importante es que llega bastante lejos.** El boot ROM arranca en
`0xA0000000`, se copia a RAM, corre desde ahí, inicializa el bus, escribe 89 registros
distintos del bloque de control del sistema y recién ahí se traba. El núcleo SH-4 y el
mapa de memoria no son el problema.

Se traba así:

```
8c000000: 0009  NOP
8c000002: 0009  NOP
8c000004: 0009  NOP
8c000006: 001b  SLEEP     <== PC, para siempre
8c000008: affd  BRA  -6
8c00000a: 0009  NOP       (ranura de retardo)
```

Con `SR = 0x700000F1`: MD, RB y BL en 1 e IMASK en 0xF. Es el halt del boot ROM —
duerme con las interrupciones bloqueadas, así que nada lo despierta. Llega ahí desde un
bucle de reintentos que da 10 vueltas y se rinde.

## 0. Un modo de arranque por BIOS

No existe. `main.c:857` fija `PC = mem_base + ip_bs1_offset` y no hay forma de pedir otra
cosa. Antes que nada hace falta una opción (`dcemu --bios`, o detectar que no se pasó
ejecutable) que deje `PC = 0xA0000000` y saltee la carga manual de `ip.bin` y
`1st_read.bin`.

Es media hora de trabajo y sin eso no se puede ni empezar a probar el resto.

## 1. El handshake de PDTRA/PCTRA — detección del cable de video

**Este es el bloqueo actual.** El bucle de reintentos que termina en el halt lee
`0xFF800030` (PDTRA, Port Data Register A) con `R3 = 0xFF80002C` (PCTRA):

```
8c00b92c: e40a  MOV   #10, R4          ; 10 reintentos
8c00b932: 8532  MOV.W @(2,R3), R0      ; lee PDTRA
8c00b936: c903  AND   #3, R0
8c00b938: 3010  CMP/EQ R1, R0
8c00b93a: 8903  BT    +3               ; listo
8c00b93c: 4410  DT    R4
8c00b93e: 8bf8  BF    -8               ; reintenta
8c00b940: ...                          ; se rinde -> halt
```

En la Dreamcast el puerto A del SH-4 está cableado al detector de tipo de cable (VGA, RGB,
compuesto). El boot ROM escribe un patrón en PCTRA y espera que los bits bajos de PDTRA
respondan. `regmap_read()` en `mem.c` devuelve lo que haya en `regmem`, que nunca cambia,
así que la respuesta no llega nunca.

Es el caso especial que todos los emuladores de DC tienen que implementar. Son unas 20
líneas en `regmap_read()`: según los 4 bits bajos de PCTRA, devolver 0 o 3 en los bits
bajos de PDTRA, y el tipo de cable en el byte alto.

Con esto puesto —se probó— el boot ROM **deja de trabarse** y sigue hasta el GD-ROM.

## 2. Flash ROM

Después del cable, el boot ROM lee `0xA021A000`, `0xA021A004`, `0xA021A056`, `0xA021A058`
y `0xA021A05C`: es la flash de 128 KB en `0x00200000-0x0021FFFF`, donde vive la
configuración del sistema (región, idioma, fecha, nombre de la consola).

`bios/flash.bin` está en el repositorio pero **nadie lo carga**. `cargar_bios()` solo lee
`bios.bin`, y `bios_read()` cubre `0xA0000000-0xA01FFFFF`. Falta reservar el bloque,
cargarlo y extender el rango.

Trabajo chico. Sin esto el boot ROM ve basura donde espera su configuración y no se puede
predecir qué hace.

## 3. Respaldo real para el bloque de control del sistema

La sonda registró **89 direcciones distintas** escritas sin ningún efecto, casi todas en
`0xA05F6800-0xA05F7CFF`: control de DMA, bus G2, canales de sonido y GD-ROM.

`pvr_write()` es un `switch` sobre direcciones concretas; lo que no está en la lista cae en
`mem_write_error()` y se pierde. Escribir y volver a leer un registro devuelve otra cosa.

La solución barata es mapear todo el rango `0x005F0000-0x005FFFFF` sobre `control_mem`
—que ya está reservado y ya se usa en el `default` de `pvr_read()`— y dejar el `switch`
solo para los registros que además disparan trabajo. Trabajo chico, y quita 89 fuentes de
comportamiento indefinido de una vez.

## 4. La interfaz de registros del GD-ROM

**Es el trabajo grueso.** Con el cable resuelto, el boot ROM llega a `0xA05F7018` y
`0xA05F709C` desde `0x8C001FCA` y `0x8C001EE8`: son los registros ATA de la lectora.

dcemu no emula nada de esto. Lo que tiene es `hack_gdrom()` en `dcopcodes.c`, que
intercepta la *syscall* de la BIOS y lee sectores desde la imagen con `iso.c`. Eso funciona
para un binario que ya está corriendo, pero el boot ROM no usa syscalls: habla con el
hardware.

Hay que implementar:

- El bloque de registros ATA en `0xA05F7000-0xA05F709C`: status, error, comando, contador
  de bytes, selector de unidad, y el registro de datos.
- La máquina de estados del protocolo: `SET FEATURES`, `IDENTIFY`, y sobre todo el modo
  paquete (SPI) con los comandos que usa el boot ROM: `TEST_UNIT`, `REQ_MODE`, `REQ_STAT`,
  `GET_TOC`, `CD_READ`.
- Los estados de "bandeja abierta / sin disco / disco listo", porque el boot ROM decide
  entre el menú y el arranque del juego según eso.
- Las interrupciones de fin de comando por el ASIC, que ya existe (`intc_add`).

Sin esto el boot ROM se queda dando vueltas esperando a la lectora —que es exactamente
donde termina hoy la corrida instrumentada, ciclando entre `0x8C0010FE`, `0x8C00B6xx` y
`0x8C0031xx`.

Es la pieza más grande de la lista. Como referencia, en cualquier emulador de DC es del
orden de mil líneas.

## 5. AICA: RAM de sonido, registros y el ARM7

Tres cosas distintas, en orden de dificultad:

- **La RAM de sonido no está mapeada.** `sound_mem` se reserva en `inicializar_memoria()`
  (`mem.c:114`) y no se le asigna ninguna zona: `0xA0800000` cae en `pvr_read`/`pvr_write`,
  que solo tienen el caso puntual `0xA080FFC0` (`snd_dbg`, un hack). Mapear los 2 MB es
  trivial y probablemente sea lo que ese hack estaba tapando.
- **Los registros del AICA** en `0xA0700000-0xA0702FFF`. La sonda ya vio una lectura a
  `0xA0702C00`.
- **El ARM7 del AICA.** El boot ROM sube un programa a la RAM de sonido y espera que
  responda. Para el sonido del logo hace falta emular el ARM7 entero, más los 64 canales
  del sintetizador. Es un proyecto del tamaño del núcleo SH-4.

Para *bootear* quizá alcance con lo primero y con que los registros de estado respondan
algo coherente. Para que el arranque suene, no.

## 6. Reloj de tiempo real

`0xA0710000` y `0xA0710004` están en el `switch` de `pvr_read()` pero solo loguean: no
devuelven nada. El boot ROM lee la hora para el menú. Trabajo chico: son dos registros de
16 bits con el contador de segundos desde 1950.

## 7. El sondeo del bus G2

La sonda vio una lectura a `0xA0600004`, que es el área de dispositivos externos del bus G2
—ahí van el módem y la BBA—. Hoy `mem_read_error()` no toca el destino, así que el boot
ROM lee lo que hubiera en esa variable: basura no determinista.

Aunque no se emule ningún dispositivo, hay que devolver un valor fijo que signifique "acá
no hay nada". Trabajo mínimo, pero mientras no esté, el arranque no es reproducible.

## 8. DMA del SH-4

`dma_check()` existe en `main.c:180` y solo escribe al log; además está comentada dentro de
`main_loop()`. Las transferencias no se hacen. El boot ROM y la BIOS mueven datos por DMA
—sobre todo hacia el PVR y el AICA—, así que los cuatro canales tienen que funcionar de
verdad, junto con el DMA del bus G2 y el del PVR (`0xA05F6800`, `0xA05F7400`).

## 9. Lo que falta del PVR para la animación de arranque

El logo giratorio de la Dreamcast no es un framebuffer plano: usa el tile accelerator con
texturas, gradientes y transparencias. `graficos.c` ya hace geometría, texturas *twiddled*
y VQ, y ordenamiento de translúcidos, así que la base está. Lo que habría que revisar
cuando el arranque llegue hasta ahí:

- Texturas YUV422, que el logo usa.
- Volúmenes modificadores.
- Render a textura.
- El *punch-through* y el orden por listas completo.

No es un bloqueo: es lo que se ve *después* de resolver los puntos 1 a 5.

## 10. Excepciones del SH-4

`intc()` implementa la entrada a interrupción (guardar SSR/SPC, saltar a VBR). No hay
excepciones generales: error de dirección, instrucción ilegal, ni fallo de TLB. El boot
ROM instala manejadores en `VBR+0x100`, `VBR+0x400` y `VBR+0x600` —se ven los `RTE` en
`0x8C000014`—, así que los espera. Mientras nada los dispare no molesta, pero cualquier
error de alineación en el código emulado hoy pasa desapercibido en vez de llegar al
manejador.

## 11. Robustez del mapa de memoria

Las zonas no mapeadas quedan en `NULL` en `mem_zone[]`, y `get_memory_pointer()` no
chequea: cualquier acceso a una zona sin mapear es una desreferencia de puntero nulo. La
sonda se cayó exactamente así al querer volcar memoria en `0x8BFFFFF8`.

Con el boot ROM tocando direcciones que nadie probó, conviene que las zonas libres apunten
a un bloque de descarte, como hace `tests/memoria_prueba.c`.

## Lo que no hace falta

- **La MMU.** El boot ROM corre en P1/P2, sin traducción. `LDTLB` como no-op alcanza.
- **La caché.** Se escriben CCR y las operaciones de bloque, pero sin efecto observable
  mientras la memoria sea coherente, que es el caso.

## Orden sugerido

| # | qué | tamaño | desbloquea |
| --- | --- | --- | --- |
| 0 | Modo de arranque por BIOS | trivial | poder probar |
| 1 | Handshake PDTRA/PCTRA | chico | **el halt actual** |
| 7 | Sondeo G2 determinista | trivial | reproducibilidad |
| 11 | Zonas no mapeadas a descarte | trivial | no caerse |
| 2 | Cargar y mapear la flash | chico | configuración del sistema |
| 3 | Respaldo del bloque de control | chico | 89 registros fantasma |
| 6 | RTC | chico | hora en el menú |
| 4 | **GD-ROM (registros ATA + SPI)** | **grande** | **llegar al menú** |
| 8 | DMA del SH-4 y del G2 | mediano | transferencias reales |
| 5 | AICA (RAM, registros, ARM7) | grande | sonido del arranque |
| 9 | PVR: YUV, modificadores | mediano | animación del logo |
| 10 | Excepciones generales | mediano | diagnóstico |

Los cuatro primeros son una tarde. El punto 4 es el que decide si esto llega al menú o no.
