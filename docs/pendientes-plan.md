# Plan: los pendientes que quedan

Escrito el 31 de julio de 2026, después de la cuarta corrida del arranque por BIOS. Es el
inventario de todo lo que queda abierto en el árbol, ordenado por lo que rinde, con cómo se
prueba cada cosa y qué la puede complicar.

## De dónde sale esta lista

No se inventó nada: cada punto viene anotado en un documento o en el código.

| fuente | qué aporta |
| --- | --- |
| `docs/bios-boot-plan.md`, "Lo que queda" | el juego que arranca y no dibuja, y el `.cdi` que no se parsea |
| `docs/demos-kos.md`, "Lo que falta" | las 7 demos de sonido y las 28 que piden periféricos |
| `docs/sh4-conformidad.md`, "Pendiente" y "Lo que sigue sin cumplir el manual" | RM, DN, la causa I, las dos filas de `SGR`, los ciclos |
| `docs/mmu-plan.md`, "Lo que sigue faltando" | fases 6 y 7, `URC`, alineación |
| `docs/clock-plan.md`, "Lo que sigue faltando" | entrelazado, `TPSC` 110/111, granularidad del límite |
| `docs/msvc-build-plan.md`, "Pendiente" | la ventana en negro, guichan, libcdio, x64 |
| `README.md` | la tabla de subsistemas quedó de antes de casi todo esto |

## El estado del que se parte

| | |
| --- | --- |
| Demos de KOS que funcionan | 100 de 135 |
| Fallan por algo que falta emular | 7, todas del AICA |
| No aplican: piden periféricos | 28 |
| Filas de `opcodes[]` implementadas | 239 de 239, con 516 casos en verde |
| Arranque por boot ROM | llega al menú, arranca el juego del disco y salta |

Ya no falla nada del PVR ni del núcleo SH-4. Lo que queda se reparte en cinco vías que casi
no se tocan entre sí, así que el orden es negociable salvo donde se dice lo contrario.

## Los hitos

Siguiendo la letra donde quedó `bios-boot-plan.md` (el hito C es "el boot ROM arranca el
juego del disco"):

| hito | qué se ve | vía |
| --- | --- | --- |
| **D** | un juego comercial dibuja su primer cuadro | A |
| **E** | una demo de KOS suena | B |
| **F** | las 135 demos revisadas una por una, sin deuda de verificación | C |

El hito D es el que importa: es lo único que separa a dcemu de "corre homebrew" de "corre un
juego", y el README todavía dice que nunca lo logró.

---

## Vía A — El juego arranca y no dibuja (hito D)

Lo que está anotado: tras el salto el ejecutable corre —el PC recorre `0x0C14xxxx`-`0x0C17xxxx`—
y termina leyendo por punteros que no apuntan a nada, `0x10000011` en adelante y de a `0x2C`.
Pasa igual por el camino de los hooks de syscall, así que no es del arranque por BIOS.

### A.0 — Volver a medir dónde se para cada imagen

**Primero esto, porque la nota es anterior a media docena de arreglos.** Entre que se escribió
y hoy entraron el signo de la profundidad, las dos ventanas de RAM de vídeo, el plano de fondo,
el POLY1, el DMTE del DMAC, el UBC y la palabra de región en `0x8C000070`. La nota puede estar
describiendo un estado que ya no existe.

Producto: una tabla de cinco filas —las cuatro `.cdi` de `roms/` que se parsean, más mame4all—
con dónde se para cada una hoy, por los dos caminos (`--bios` y hooks). Se arma con
`--salir-tras`, `--captura-gl` y el resumen del anillo de PC al salir, que es lo que ya
imprime `traza_resumen()`.

Corridas cortas y acotadas: `--salir-tras=8` y a mirar el volcado, no dejar el emulador
corriendo.

### A.-1 — Lo que se midió el 31 de julio, y cambia la hipótesis

Salió de mirar la franja de ruido que se ve arriba de la pantalla en Virtua Tennis y en
Virtua Tenis 2. **Esa franja no es el juego dibujando mal: es el juego no dibujando.**

Virtua Tennis (USA), ocho segundos, camino de siempre:

```
traza: 0 escenas rendidas; tiras de las ultimas 0:
traza: bytes a RAM de video por ventana: 04=16384 05=8388608
```

Cero escenas por el TA en ocho segundos. Los 8388608 bytes por la ventana de 32 bits son los
**8 MB de la RAM de vídeo escritos una vez**, o sea un borrado; lo que se ve en la franja es
lo que quedó al principio del framebuffer, presentado por el camino 2D. Nueve filas de 600 en
la ventana, ruido vertical, y el resto negro.

Y el juego se para muy temprano: el primer bucle detectado es **a los 0,032 s de tiempo
emulado**, y hay seis antes del segundo medio. El último deja al guest aquí:

```
8c1fc888: MOV  #fe, R3      ; R3 = 0xFFFFFE00
8c1fc88c: STC  VBR, R14
8c1fc88e: MOV.L @(8c1fc960), R4
8c1fc890: MOV.L @R4, R4     ; R4 = 0x160
...       ADD R3,R2 / SHAR x3 / ADD R2,R14 / MOV.L @(R0,R14),R14
```

Con `SR=600000f0` —`RB=1`, banco de excepción, todas las interrupciones enmascaradas—. Es el
despachador de excepciones **del juego**: toma el código de evento, le resta 0x200 y divide
por 8 para indexar su tabla. Con **0x160** el índice sale negativo (−20) y lee por debajo de
la tabla.

**0x160 es `TRAPA`**, y el juego lo ejecutó de verdad: es el único sitio del árbol que deja ese
valor en `EXPEVT` fuera de `excepciones.c` (`syscontrol.c:876`), y en toda la corrida no se
entregó **ninguna** excepción general —`traza: excepcion EXPEVT ...` no aparece ni una vez—.
La tabla del juego empieza en 0x200, que es donde empiezan los códigos de **interrupción**:
está preparado para interrupciones y le llegó una trampa.

### A.-1b — Seguida hasta el fondo: era el banco de registros — **resuelto**

Se le agregó a `--traza-mem` un reporte de cada `TRAPA` que ejecuta el guest, con el anillo de
PC en la primera. Con eso la cadena salió entera, y de abajo hacia arriba es:

1. `TRAPA #23` desde `0x8C00006C`, que **no es código**: es el bloque bajo del sistema. Los
   bytes que el ROM deja ahí —parte del identificador de la consola, ver C.3— decodifican como
   `TRAPA #0x17`. El guest está ejecutando datos.
2. Llegó ahí corriendo desde `0x8C000018`, adonde saltó el **boot ROM**: el anillo muestra al
   guest ejecutando desde la dirección `0x00000000`, que es la ROM, leyendo `EXPEVT` y
   despachando.
3. Y llegó a la dirección 0 por un `JSR @R1` con **R1 en cero**, en `0x8C1F6F16`.
4. Ese `JSR` está en un despachador de callbacks: una tabla en `0x8C470A20` de registros de 8
   bytes `{función, argumento}`, indexada por el argumento. La tabla **sí se llena** —un
   watchpoint la ve recibir tres punteros distintos—, así que el problema era el índice.
5. El índice sale mal **a través de una interrupción**: el `RTE` devuelve al guest a la mitad
   del despachador, en `0x8C1F6F0A`, y de ahí el cálculo da una entrada en `0xA4D51ED0` —la
   ventana de la RAM de vídeo—, donde leer da cero.

Y el motivo: **`UpdateSR()` intercambiaba el banco de `R0-R7` aunque `RB` ya valiera 1.** El
despachador corre con `SR=600000f0`, o sea ya en el banco 1; la interrupción anidada
—el manejador baja `BL` a propósito— volvía a poner `RB=1` e intercambiaba igual, así que el
código interrumpido volvía con los `R0-R7` del otro banco. `R2`, `R3` y `R4` son banqueados.

Arreglado con `core.context.banco_activo`, que registra el banco **puesto** frente al que `SR`
pide. Detalle completo en [sh4-conformidad.md](sh4-conformidad.md). Ninguna demo de KOS lo veía
porque KOS toma sus interrupciones desde `RB=0`; la suite tenía el caso de ida y no el de
vuelta, y ahora tiene los dos.

**Con eso el `TRAPA` desaparece**, pero el juego sigue en 0 escenas: hay más detrás. Las nuevas
direcciones sin emular que aparecen al pasar ese punto (`0x902940E4`, `0x020A8618` y siguientes
de a 4, desde `0x8C1FA438`/`43C`) son el próximo hilo, y tienen la misma pinta: un recorrido de
tabla con la base mal.

De paso, dos filas del **desensamblador** mentían: `LDS Rm,MACL` y `LDS.L @Rm+,MACL` se
imprimían como `MACH`. Los manejadores estaban bien —son filas distintas en `opcodes[]`—, pero
el desensamblado de un manejador de excepciones mostraba dos `MACH` seguidos y eso hizo perder
un rato sospechando de la tabla. Es la misma clase de error que el de `MOV.W @(disp,PC)` que ya
está anotada en `CLAUDE.md`: plausible y falso.

Lo que sigue abierto de A.2: la hipótesis del registro que contesta cero baja a segundo lugar
para Virtua Tennis, pero no está descartada para los demás.

Un dato más, del mismo día. Crazy Taxi (USA) por el camino de siempre, seis segundos,
`--traza-mem`:

```
traza: lectura sin emular en 2d2d2d0a (1 bytes) desde PC 0c148966
traza: 1 direcciones distintas sin emular.
```

`0x2d2d2d0a` son los bytes `0a 2d 2d 2d`, o sea `"\n---"`: **el puntero es texto leído como
puntero**. Y el PC cae en el mismo `0x0C14xxxx` que anota `bios-boot-plan.md`. O sea que el
juego está recorriendo como tabla de punteros algo que en realidad es una cadena — el síntoma
de que una tabla que esperaba llena no lo está, o de que un puntero base salió mal mucho antes.
Es una sola dirección sin emular en toda la corrida, así que no es que falte un registro.

### A.1 — De dónde sale el puntero

`0x10000011` cae dentro de la FIFO de polígonos del TA (`0x10000000`-`0x107FFFFF`), que en el
chip es de **solo escritura**: nadie lee una tabla de registros de `0x2C` bytes ahí. O sea que
el puntero está mal formado, no mal usado, y la pregunta es en qué instrucción se forma.

Herramientas, todas ya escritas:

- `--watchpoint=DIR` sobre la palabra donde el juego guarda ese puntero: dice quién la
  escribió, con el PC y el PR.
- `--watchpoint-lectura=DIR` sobre lo que esa rutina leyó antes: una línea por PC distinto.
- `--traza-desde=PC:N:K` sobre el PC donde se forma, para leer la decisión con los registros
  que cambian.
- El manejador de caídas, que ya vuelca el estado del guest en vez de desaparecer.

### A.2 — La hipótesis por descartar primero

Es la forma que tuvo **todo** lo demás en este proyecto: algo que el guest pide, que dcemu
contesta sin querer decir nada, y que nadie reporta. Los candidatos concretos, en orden de
sospecha:

- un registro del bloque de control sin caso en `pvr_read()` que cae al respaldo y devuelve
  cero —fue el `SB_G1SYSM`, fue el `REVISION` del PVR, fue el `SB_GDLEND`—;
- un campo del bloque `0x8C000060`-`0x8C00007F` que el boot ROM real rellena y dcemu no (ver
  C.3): la palabra de región ya costó un cuelgue entero de Crazy Taxi;
- una syscall que devuelve sin hacer nada (`SYSINFO`, `UNKNOWN`; ver C.4);
- un dispositivo del bus G1/G2 que contesta "nada aquí" donde el juego espera datos.

Los tres primeros son baratos de descartar y dos de ellos ya están en la vía C, que por eso
conviene hacer antes.

**Cómo se prueba**: que el juego dibuje algo por el TA, medido con `--captura-gl` y con el
recuento de tiras que `--traza-mem` imprime al salir. Cero tiras dice "el guest no mandó", no
"el PVR falló"; esa distinción ya está resuelta y no hay que volver a pagarla.

**El riesgo real**: que no sea una cosa sino diez, y que cada una tape a la siguiente —que es
exactamente lo que pasó con las cinco de la lectora—. Por eso el producto de A.0 es una tabla
de cinco imágenes y no una: si cuatro se paran en el mismo sitio, es una causa común y vale la
pena; si cada una se para en un sitio distinto, es el juego y hay que elegir el más simple.

---

## Vía B — El AICA (hito E)

Siete demos fallan y ninguna suena. El camino de subida del firmware funciona —`libdream-spu`
reporta `Load OK, starting ARM`, o sea que los 2 MB de RAM de sonido y la ventana física se
comportan—; lo que falta es el chip.

### B.0 — La decisión de base: LLE o HLE

**Es la primera decisión y condiciona todo lo demás.**

- **HLE**: reconocer el protocolo de la cola de comandos que usa KOS (`aica_queue_t` de
  `snd_iface.c`) y sintetizar el sonido en el anfitrión. Arregla las siete demos con mucho
  menos trabajo. **No sirve para un juego**: cada juego sube su propio firmware ARM y habla su
  propio protocolo, que no es el de KOS.
- **LLE**: emular el ARM7DI y los 64 canales. Sirve para todo. El núcleo ARM es chico —ARMv3,
  sin Thumb, sin MMU, sin coprocesadores— y este árbol ya tiene el patrón para escribirlo: una
  tabla de despacho y un archivo por categoría, como `opcodes.c`.

La recomendación es **LLE**, por dos razones: el hito D es un juego y el HLE no lo alcanza, y
un ARM7DI sin Thumb es menos trabajo que el SH-4 que ya está escrito.

### B.1 — El ARM7DI

Núcleo nuevo, aislado, con su propia suite en `tests/` como todo lo demás. Corre a 22,58 MHz
sobre los mismos `reloj_total` que ya lleva `tmu.c`, para no inventar una segunda base de
tiempo (esa lección está en `docs/clock-plan.md`). Se verifica solo: si el ARM arranca y
ejecuta el firmware de KOS, la cola pasa a `valid` y la aserción de `sound-multi-stream`
—`snd_iface.c:84`— deja de saltar. Ese es el primer veredicto medible.

### B.2 — Los canales y la mezcla

64 canales con formato (PCM8, PCM16, ADPCM de Yamaha), envolvente, LFO, paneo y volumen,
mezclados a una salida de SDL. Se puede escalonar: PCM16 primero, que es lo que usan casi
todas las demos, y ADPCM después.

### B.3 — CDDA

`sound-cdda-basic_cdda` pide reproducir una pista de audio del disco. La lectora ya sabe qué
pistas son de audio —`iso_init()` las lista con su modo— así que es el comando de la lectora
más la ruta a la salida de SDL, no otro chip.

**El riesgo real**: el sonido es lo único del árbol que no se puede verificar leyendo un
volcado. Hace falta un método —volcar el búfer de mezcla a un `.wav` y compararlo, igual que
`--captura-gl` volcó lo que rasterizó OpenGL— **antes** de escribir la mezcla, o se va a
depurar de oído.

---

## Vía C — Los residuos acotados (hito F)

Cada uno es de una sesión o menos, y dos de ellos alimentan la vía A.

### C.1 — `Virtua Tenis 2 (USA).cdi` no se parsea — **resuelto**

Era **el mismo campo significando dos cosas distintas**. Los últimos 8 bytes del archivo
llevan la versión y un `desplazamiento`, y ese desplazamiento es el **tamaño** del header en
la 3.5 —se cuenta desde el final— pero la **posición** del header en la 2.0 y la 3.0.
`cdi_abrir()` lo usaba como tamaño en los tres casos.

Las cinco imágenes que funcionaban son 3.5, donde las dos lecturas coinciden. Ésta es una 3.0
(y de 2001, no de 2018 como las otras), así que se pedían los **749 MB de la imagen entera**
como si fueran el header: el `malloc` fallaba o la lectura se quedaba corta, y `cdi_abrir()`
devolvía 1 **sin decir nada** — de ahí que pareciera "no encuentra pistas".

El header llega hasta el final del archivo en las tres versiones, así que el tamaño se deriva
ahora de la posición y no del campo. Con eso la imagen monta:

```
iso_init: usando Virtua Tenis 2 (USA).cdi, 2 pistas; la de datos empieza en el LBA 45000,
          286564 sectores de 2336 bytes en modo 2
iso_init:   pista 1: LBA 0, 33600 sectores, modo 0 (audio)
iso_init:   pista 2: LBA 45000, 286564 sectores, modo 2 (datos)
```

O sea un selfboot audio/datos, el mismo formato que Crazy Taxi (DCRES) y DCDoom. Van además
un tope de cordura para el tamaño del header y **el mensaje que faltaba**: los dos caminos de
salida que no lo tenían eran justo por donde se iba esta imagen.

Con eso el juego carga y corre, y se para en otro sitio que los demás — lo cual es
precisamente el insumo de A.0. Ocho segundos por el camino de siempre:

```
lectura sin emular en a1000400 (4 bytes) desde PC 8c28cd86   (y 0800, 1400, 1800)
lectura sin emular en 185d0b24 (4 bytes) desde PC 8c2952da
escritura sin emular en 00000000 (4 bytes) desde PC 8c29a444
```

Las cuatro de `0xA10004xx`-`0xA10018xx` son un dispositivo del bus G2, sondeado desde un mismo
PC: es la cuarta sospecha de A.2, y aquí sale sola. Sin veredicto por el serial.

### C.2 — El README quedó de antes de todo esto — **resuelto**

Decía que la MMU no está emulada, que las transferencias del DMA no están implementadas, que
el GD-ROM va solo por hooks de syscall y que **nunca logró arrancar un juego comercial**. Las
cuatro eran falsas. Actualizados la tabla de subsistemas, la cuenta de pruebas (543, no 387),
el uso —`.cdi`, `--bios`, `--ayuda`— y las teclas que faltaban (F5, F6, `f`, el gamepad).

### C.3 — El bloque `0x8C000060`-`0x8C00007F` — **medido; dos tercios reproducidos**

Se volcó del ROM real: `--bios` con el 1.01d, un `.cdi` de juego montado y
`--volcar=8c000000:100`, con la BIOS ya en el menú. Dos corridas, idénticas salvo un byte.

Lo que hay ahí **no son constantes: es la copia que el ROM hace de la flash**, que es lo que
convierte el punto de "copiar números medidos" en "derivarlos del mismo sitio que el ROM":

| dirección | qué | de dónde |
| --- | --- | --- |
| `0x8C000060` | `0x00C0C0C0`, estable | no sale de la flash. Sin explicar |
| `0x8C000064` | cambia entre corridas | un contador o un reloj. Sin explicar |
| `0x8C000068`, 8 B | identificador binario de la consola | flash partición 0, `+0x56` |
| `0x8C000070`, 6 B | código de región y NUL | flash partición 0, `+0x00` — ya estaba |
| `0x8C000078`, 8 B | ajustes del sistema | flash partición 2, último registro de 16 |

**Implementado el de `0x8C000068`**, que sale de la flash igual que el de al lado y queda
verificado byte a byte contra el volcado del ROM. Los otros dos quedan afuera a propósito: el
de `0x78` se sabe de dónde sale pero no el formato de esa partición —registros de 16 bytes
que se agregan al final, gana el último—, y los de `0x60`/`0x64` no se sabe qué son. Copiar
números medidos sin saber qué significan es justo lo que este árbol no hace.

Lo que sí queda cerrado es que **ya no es un agujero desconocido**: está medido y anotado.

### C.4 — Los stubs `SYSINFO` y `UNKNOWN` — **resuelto**

Eran `RTS` + `NOP`: volvían sin hacer nada y **sin decirlo**, que es la forma exacta que tuvo
cada uno de los agujeros de este árbol. Ahora llevan el mismo par `RTS` + opcode ilegal que
los otros tres stubs, despachado a `hack_mudo()`, que reporta por `--traza-mem` el nombre, los
cuatro argumentos, el PC y el PR. Siguen sin hacer nada: lo que cambia es que se ven.

`R0` queda en 0 en vez de en lo que hubiera, porque un puntero de vuelta con basura es peor
que uno nulo — el guest lo sigue.

Queda una pista para más adelante: `SYSINFO` lleva el número de función en R7 y una de sus
funciones devuelve el identificador de 8 bytes de la consola, que es justo el que C.3 acaba de
dejar en `0x8C000068`. Falta confirmar la numeración contra `syscall_sysinfo` de KOS antes de
contestarla.

### C.5 — La deuda de verificación de las demos

33 binarios están en "dibujan; la captura tiene contenido pero no se revisó una por una". No
es un fallo, es que nadie las miró. Una pasada con `--captura-gl` y el ojo cierra el hito F y
puede destapar cosas —así salieron el filtro de textura y el recorte del volcado—.

### C.6 — Residuos ya diagnosticados que se dejan como están

Van listados para que nadie los vuelva a investigar:

- `tsunami-genmenu`: la geometría llega bien y aterriza en y 631..1458 sobre una pantalla de
  480. Es del guest; dcemu no toca las coordenadas.
- `tunnel` y su `attempt to submit to unopened list`: estado del guest de punta a punta,
  medido y zanjado.
- `basic-breaking`, quinto grupo: GCC 15.2 a `-O2` elimina la llamada; falla también en
  hardware real.
- El `// FIXME` de `arith.c:801` (NEGC) y el bloque comentado de arriba: la fila está
  implementada y con prueba. Es un comentario viejo, se borra y ya.

---

## Vía D — Conformidad: SH-4, MMU y reloj

Nada de esto rompe ninguna demo hoy. Se ataca por lo barato y por lo que puede estar
falseando un resultado sin avisar.

### D.1 — Los bits RM y DN de FPSCR (barato, y puede importar)

`docs/sh4-conformidad.md` los deja afuera junto con la causa I, pero **no cuestan lo mismo**.
La causa I obliga a leer MXCSR después de cada instrucción emulada, que es lo más caro
posible en un intérprete; RM y DN se configuran **una vez, cuando el guest escribe FPSCR**, no
por instrucción:

- **RM**: modo de redondeo. Solo tiene dos valores en el SH-4, 00 al más cercano y 01 truncar.
  Se aplica con `fesetround()` desde `UpdateFPSCR()`, que ya existe y ya se llama en el sitio
  exacto. Importa porque **el valor de reset es 01 y el código del boot ROM corre con el
  redondeo equivocado** desde siempre.
- **DN**: vaciar desnormalizados a cero. Es FTZ/DAZ de MXCSR, misma vía y mismo sitio.

Se verifica con casos nuevos en la suite `fpu-excepciones`, que ya existe.

### D.2 — Las dos filas de `SGR`

`LDC Rm,SGR` (0x403A) y `LDC.L @Rm+,SGR` (0x4036) vienen marcadas `// INSERTADA` por los
autores originales y el resumen del manual lista SGR solo para `STC`. Es una consulta al
manual y, si no existen, dos filas menos y un `NOIMP` más. Lo más barato de toda la lista.

### D.3 — Errores de dirección por acceso desalineado

No se levantan. dcemu tampoco los levantaba antes, así que nada depende de ello, pero es un
fallo que un juego real sí produce y que hoy pasa inadvertido.

### D.4 — MMU, fases 6 y 7

- **Fase 6**: las store queues resuelven por `QACR0`/`QACR1` y no respetan `MMUCR.SQMD`.
  Acotado.
- **Fase 7**: la búsqueda de instrucción sigue siendo `get_memory_pointer(PC)` sin traducir y
  sin ITLB; los arreglos de la ITLB se leen y escriben y nadie los consulta. **Es la única
  cosa de esta vía que toca el camino caliente**, así que va con medición antes y después.
- `MMUCR.URC` no se incrementa, así que no hay reemplazo por LRU.
- La traducción recorre las 64 entradas desempaquetando al vuelo. Está bien por ahora.

### D.5 — Reloj

- El entrelazado (`SPG_CTRL_INTERLACE`) se ignora: se cuenta por campo. Un programa que
  distinga campo par de impar no lo vería.
- `TPSC` 110 y 111 caen al valor por omisión con aviso. La Dreamcast no los cablea.
- El límite duerme por cuadro, con granularidad de ~16 ms.
- Los ciclos por instrucción son aproximados y las pruebas no los verifican.

---

## Vía E — El entorno

### E.1 — Verificar si la ventana sigue en negro

`docs/msvc-build-plan.md` anota que `roto.bin` no se ve ni en Debug ni en Release, que se
bisectó y que el commit del port **también** sale en negro hoy: es del entorno, no de un
cambio. Los sospechosos son la cadena sdl12-compat → sdl2-compat → SDL3 y el driver de
OpenGL.

**Pero esa nota es anterior a todo julio**, y desde entonces la BIOS muestra su animación en
la ventana y se revisaron demos a ojo. O sea que o se arregló solo, o afecta a un caso y no a
todos. **Verificar antes de investigar**: correr `roto.bin` y mirar.

### E.2 — Fases 5 y 6 del port a MSVC

Reponer la ventana de log (guichan) y el backend libcdio, que hoy quedan fuera a propósito, y
la variante x64. Ninguna bloquea nada.

---

## Orden recomendado

1. ~~**Vía C.1, C.2, C.3, C.4**~~ — **hecho el 31 de julio de 2026.** Cerraron los dos
   candidatos de A.2 (ninguno era la causa: los stubs mudos no llegan a llamarse en Crazy
   Taxi), arreglaron una imagen y dejaron de mentir en la portada. De paso salió la lectura
   de `0x2d2d2d0a` que está anotada en la vía A.
2. **Vía A.0** — media sesión. La tabla de dónde se para cada imagen hoy. Sin esto se puede
   estar persiguiendo un fantasma de hace una semana.
3. **Vía A.1 y A.2** — el hito D. Es donde está el valor.
4. **Vía D.1 y D.2** — una sesión, en cualquier hueco. RM puede estar falseando aritmética del
   boot ROM desde siempre y cuesta una llamada en `UpdateFPSCR()`.
5. **Vía B** — el hito E. Es la más grande de todas y empieza por una decisión, no por código.
6. **Vía C.5, D.3-D.5, E** — cuando no haya nada mejor.

La vía B se puede adelantar entera si el sonido pesa más que el vídeo; no depende de nada de
lo anterior. Lo que **no** conviene es empezarla sin haber resuelto B.0.

## Cómo se prueba, en general

Lo de siempre en este árbol, y por escrito porque cada punto ya costó tiempo una vez:

- Los 135 binarios de KOS son la línea base de regresión. `docs/demos-kos.md` dice qué
  funciona hoy; un cambio que rompa algo de ahí se nota.
- Se mide con `--captura-gl`, **no** capturando la ventana: capturar la ventana depende del
  compositor del anfitrión y falla en silencio.
- La cuenta de colores es un indicio, no un veredicto. El veredicto sale del serial cuando lo
  hay, y de mirar la imagen cuando no.
- El recuento de tiras al salir separa "el guest no mandó" de "el PVR no dibujó".
- Corridas cortas y acotadas, con `--salir-tras`: una corrida larga traba la ventana.
- `stdout` y `stderr` van a `stdout.txt` y `stderr.txt` en Windows, no al shell.

## El riesgo real

- **Que la vía A no sea una cosa sola.** Es lo más probable y es lo que pasó con la lectora
  (cinco fallos, cada uno tapando al siguiente). Se mitiga con A.0: cinco imágenes, no una.
- **Que la vía B se coma el proyecto.** Un ARM7DI más 64 canales más ADPCM es comparable a lo
  que ya hay escrito del PVR. Si se entra, se entra por fases con veredicto en cada una, y la
  primera es "la aserción de `snd_iface.c:84` deja de saltar".
- **Que la vía D rompa lo que funciona.** RM cambia el resultado de *toda* aritmética de punto
  flotante del guest. Es el mismo riesgo que tuvo `DIV1` en su momento —para mejor, pero
  cambia— así que se verifica contra el barrido entero, no contra la demo que lo motivó.

## Estimación

Muy gruesa, en sesiones de trabajo:

| vía | sesiones |
| --- | --- |
| C.1-C.4 (residuos que alimentan A) | 1 |
| A.0 (la tabla) | 0,5 |
| A.1-A.2 (el hito D) | 2 a 8, sin piso claro |
| D.1-D.2 (RM, DN, SGR) | 1 |
| B (el AICA completo) | 8 a 15 |
| C.5 (revisar las 33) | 1 |
| D.3-D.5, E | 3 |

El rango de A es honesto: puede ser un registro sin caso —como fueron el `REVISION` del PVR y
el `SB_G1SYSM`, media hora cada uno una vez encontrados— o puede ser una función del juego que
depende de algo que no está. Lo que acota el rango es A.0.
