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
| `docs/sh4-conformidad.md`, "Pendiente" y "Lo que sigue sin cumplir el manual" | la causa I, el qNaN del chip, las dos filas de `SGR`, los ciclos (RM y DN ya están) |
| `docs/mmu-plan.md`, "Lo que sigue faltando" | fases 6 y 7, `URC`, alineación |
| `docs/clock-plan.md`, "Lo que sigue faltando" | entrelazado, `TPSC` 110/111, granularidad del límite |
| `docs/msvc-build-plan.md`, "Pendiente" | la ventana en negro, guichan, libcdio, x64 |
| `README.md` | la tabla de subsistemas quedó de antes de casi todo esto |

## El estado del que se parte

Actualizado el 1 de agosto de 2026.

| | |
| --- | --- |
| Demos de KOS que funcionan | **104** de 135 (100 en lo visual y cuatro que además suenan) |
| Fallan por algo que falta emular | **3**: CDDA y dos de sonido con causa propia |
| No aplican: piden periféricos | 28 |
| Filas de `opcodes[]` implementadas | 239 de 239, con **615 casos unitarios** en verde |
| El núcleo contra SingleStepTests/sh4 | **116.500 casos, 0 fallos** |
| Arranque por boot ROM | llega al menú, arranca el juego del disco y salta |
| mame4all | arranca desde el `.iso` y dibuja su menú |
| **Crazy Taxi** | **corre**: título y modo attract en 3D, responde al mando (A.3) |

Ya no falla nada del PVR ni del núcleo SH-4. Lo que queda se reparte en cinco vías que casi
no se tocan entre sí, así que el orden es negociable salvo donde se dice lo contrario.

## Los hitos

Siguiendo la letra donde quedó `bios-boot-plan.md` (el hito C es "el boot ROM arranca el
juego del disco"):

| hito | qué se ve | vía | estado |
| --- | --- | --- | --- |
| **D** | un juego comercial dibuja su primer cuadro | A | **alcanzado el 1 de agosto de 2026: Crazy Taxi corre** — ver A.3 |
| **E** | una demo de KOS suena | B | **alcanzado el 1 de agosto de 2026: suenan cuatro** |
| **F** | las 135 demos revisadas una por una, sin deuda de verificación | C | pendiente |

El hito D era el que importaba: es lo único que separaba a dcemu de "corre homebrew" a "corre
un juego". Crazy Taxi (los dos rips) muestra su pantalla de carga, el aviso de VMU, responde
al Start, pasa a su pantalla de título y corre el modo attract en 3D — el motor entero, a 528
tiras por escena. Virtua Tennis y Capcom vs. SNK siguen parados, en el **otro** bloqueo (el
del arranque común del SDK, A.-1b); ese hilo sigue abierto.

---

## Vía A — El juego arranca y no dibuja (hito D)

Lo que está anotado: tras el salto el ejecutable corre —el PC recorre `0x0C14xxxx`-`0x0C17xxxx`—
y termina leyendo por punteros que no apuntan a nada, `0x10000011` en adelante y de a `0x2C`.
Pasa igual por el camino de los hooks de syscall, así que no es del arranque por BIOS.

### A.0 — Dónde se para cada imagen — **medido el 31 de julio de 2026**

Nueve imágenes, ocho segundos cada una, camino de los hooks de syscall, con `--traza-mem` y
`--captura-gl`. Secuencial: dos instancias se pelean por `logs/serial.txt`.

| imagen | pistas / datos | escenas | RAM vídeo (04 / 05) | dirección sin emular | bucles | captura |
| --- | --- | --- | --- | --- | --- | --- |
| Crazy Taxi (USA) | 2, LBA 45000 | **9**, una de 21 tiras | 128 / 8434176 | `2d2d2d0a` ← PC `0c148966` | 108, último a 7,86 s | negra |
| Crazy Taxi DCRES | 2, LBA 11702 | **9**, una de 21 tiras | 128 / 8434320 | `2d2d2d0a` ← PC `0c148966` | 101, último a 7,93 s | negra |
| Capcom vs. SNK (USA) | 2, LBA 45000 | 0 | 16384 / 8388608 | `2d2d2d0a` ← PC `8c1fb446` | 6, último a 0,47 s | franja |
| Capcom vs. SNK DCRES | 2, LBA 228825 | 2, de 0 tiras | 16512 / 8417744 | `2d2d2d0a` ← PC `8c1fb446` | 6, último a 0,47 s | negra |
| Virtua Tennis (USA) | 2, LBA 45000 | 0 | 16384 / 8388608 | `2d2d2d0a` ← PC `8c1dcc66` | 7, último a 0,79 s | franja |
| Virtua Tennis DCRES | 2, LBA 45000 | 0 | 16384 / 8388608 | `2d2d2d0a` ← PC `8c1dcc66` | 7, último a 0,79 s | franja |
| Virtua Tenis 2 (USA) | 2, LBA 45000 | 0 | 16384 / 8388608 | `a1000400`… ← PC `8c28cd86` | 9, último a 0,82 s | franja |
| DCDoom | 2, LBA 11702 | — | — | — | — | **no carga** |
| mame4all (`.iso`) | carpeta empaquetada | 0 (no usa el TA) | 0 / 235315200 | ninguna | 105 | **su propio menú** |

Cuatro cosas salen de esta tabla y ninguna se veía mirando una imagen sola.

> **Un dato más, del 1 de agosto de 2026, ahora que el AICA se emula:** Crazy Taxi (USA) corrido
> catorce segundos **no toca el AICA ni una vez** — cero disparos de canal, cero transferencias
> por el G2-DMA, y el `--traza-mem` del chip completamente vacío. Es una medida barata y vale
> para cualquiera de estas imágenes: un juego que estuviera corriendo de verdad estaría
> disparando canales mucho antes de dibujar. Así que el sonido no era lo que le faltaba, y de
> paso queda otro indicador para saber si un cambio de esta vía movió algo.

**1. El `2d2d2d0a` es del juego, no del rip.** Aparece en seis de las ocho, y el PC que lo lee
es **el mismo para los dos rips del mismo juego** y distinto entre juegos: `0c148966` en las dos
Crazy Taxi, `8c1fb446` en las dos Capcom, `8c1dcc66` en las dos Virtua Tennis. O sea que es
código del juego llegando al mismo sitio, reproducible, y no un accidente de una conversión.
Virtua Tenis 2 no lo tiene: es el único motor distinto.

**2. Hay dos formas de fallar, no una.** Capcom, las dos Virtua Tennis y Tenis 2 se paran
**antes del segundo segundo** y no mandan una sola escena. Crazy Taxi corre los ocho segundos,
manda nueve escenas —una con 21 tiras— y sigue dando vueltas hasta el final. Son problemas
distintos y conviene no tratarlos como uno.

**3. La franja de ruido de arriba no es del juego.** Las capturas de Capcom (USA), Virtua
Tennis (USA) y Virtua Tennis DCRES son **byte a byte idénticas** — mismo md5 —, y tres juegos
distintos no producen los mismos píxeles. Las tres escribieron además exactamente los mismos
16384 bytes por la ventana de 64 bits y 8388608 —8 MB justos— por la de 32. La lectura que
encaja: es el arranque común del SDK de Sega, y la franja son esos 16 KB de la ventana de 64
bits vistos como framebuffer, porque el juego muere antes de programar el vídeo y dcemu
presenta la RAM desde el offset 0. Nueve filas de 600 en la ventana ≈ siete de 480 en el
guest, que es lo que dan 16384 bytes repartidos entre los dos bancos. **No hay que buscar ahí
el dibujo del juego.**

**4. DCDoom no carga por este camino**, y no estaba anotado: `no se encontro 1st_read.bin en el
directorio raiz`. **No es del `.cdi`**: el lector saca sus dos pistas exactamente como las
lista `CLAUDE.md` —LBA 0, 302 sectores de audio y LBA 11702, 18487 de datos— y lee el IP.BIN
sin problema desde el lsn 19136. Lo que falla es el recorrido del ISO9660 del cargador
directo, que es otro código que el del ROM: la tabla de `CLAUDE.md` dice que **el ROM sí**
encuentra su `1ST_READ.BIN`. Así que es anterior a este trabajo y está acotado a un camino.

Nota sobre mame4all: la fila decía "el menú de la BIOS" porque se había corrido el
`1st_read.bin` **suelto**, que no es la configuración que funciona. **Vuelto a medir el 1 de
agosto de 2026 con la carpeta empaquetada en un `.iso`, mame4all arranca y dibuja su propio
menú** —"SELECT YOUR MAME4ALL: AGED / CLASSIC / GOLD" con las tres capturas—, 93650 colores.
No manda una sola escena por el TA: escribe el framebuffer directo por la ventana de 32 bits,
de a 32 bytes desde el PC `8c05364e`, 235 MB en ocho segundos. Coincide con lo que
`bios-boot-plan.md` ya daba por resuelto; lo que faltaba era la medida.

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

### A.0b — Vuelto a medir el 1 de agosto de 2026, y **la tabla de arriba no es reproducible**

Las mismas nueve imágenes después de los once arreglos del núcleo SH-4. Lo estable coincide
con la tabla del 31 de julio, carácter por carácter:

- **Crazy Taxi**, los dos rips: 9 escenas, una de 21 tiras, `2d2d2d0a` ← PC `0c148966`, 108
  bucles, el último a 7,863 s. Idéntico.
- El PC que lee `2d2d2d0a` sigue siendo el mismo por juego y distinto entre juegos:
  `0c148966`, `8c1fb446`, `8c1dcc66`. Virtua Tenis 2 sigue con `a1000400` ← `8c28cd86`.
- Las capturas de Capcom (USA), las dos Virtua Tennis y Tenis 2 vuelven a salir **idénticas
  entre sí** (mismo md5): el arranque común del SDK de Sega, ya explicado abajo.
- DCDoom sigue sin cargar por este camino, con el mismo mensaje.

**Y aparece algo que la tabla no podía ver porque cada imagen se corrió una sola vez: el
resultado cambia entre corridas del mismo binario sobre la misma imagen.** Capcom vs. SNK
(USA), tres corridas seguidas, alterna entre dos estados:

| estado | escenas | direcciones sin emular |
| --- | --- | --- |
| A | 2, de 0 tiras | 1 — solo `2d2d2d0a` |
| B | 0 | 6 — `2d2d2d0a` y **escrituras a `0x00000000`, `4`, `8`, `0xC` y `0x18`** |

**No es de los arreglos del SH-4**: compilado el árbol en `6c672f9` —justo antes de esa
pasada— da la misma alternancia, dos veces A y una vez B. Es anterior y estaba tapada por
haber medido una sola vez cada imagen.

Dos consecuencias:

1. **Las columnas finas de la tabla A.0 no sirven como línea base**. La aparente diferencia
   entre Capcom (USA) y Capcom DCRES —una con 0 escenas y la otra con 2— es ruido, no el rip.
   Lo que sí sirve es lo estable: el PC que lee `2d2d2d0a`, el md5 de la captura y las nueve
   escenas de Crazy Taxi.
2. **El estado B es una escritura por puntero nulo**, y esa es exactamente la forma del error
   que la vía A.-1b ya persiguió una vez: perder un valor y llamar por un puntero que quedó en
   cero. Que aparezca y desaparezca según la corrida dice que depende del tiempo — una
   interrupción que cae en un sitio distinto—, que es la misma familia. Es la mejor pista que
   hay hoy para la vía A, y se reproduce en cinco minutos.

Qué la hace variar está sin identificar. El candidato obvio es el RTC, que devuelve la hora del
anfitrión y persiste en `bios/rtc.txt`, más el momento en que caen las interrupciones respecto
del código del juego.

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

### A.4 — Virtua Tennis, mapeado hasta la cuarta espera (1 de agosto de 2026, tarde)

Con las herramientas nuevas (`DCEMU_TRAZA_ESCENA`, los watchpoints y `--traza-desde`) el
arranque de Virtua Tennis quedó leído capa por capa. Lo firme:

- **La lectura de `0x2d2d2d0a` era un herring rojo**: es el crt0 — un "touch" de un puntero
  sin inicializar cuyo resultado se descarta (Crazy Taxi hace exactamente lo mismo y corre), y
  los ceros byte a byte que siguen son el clear del BSS. Cerrado para todos los juegos.
- **La tabla de `0x8C470A20` es la de handlers de eventos del ASIC** (entradas de 8 bytes
  {función, argumento}, indexadas por número de evento). VT registra las 0-3, la 8 (fin de
  lista modificadora opaca; instancia en `0x8C45F920`) y la 10; **des-registra la 7 a
  propósito** (el "NULL" viene de un `MOV #0,R5` en el delay slot — es el des-registrador).
- **Espera 3**: un bucle con timeout que sondea `[0x8C45F904] != 0`, y ese flag lo pone el
  handler del evento 8 — que dcemu nunca emitía, porque el juego cierra su única lista como
  opaca (evento 7) y dcemu solo emite el evento de la lista que clasificó. Con el experimento
  `DCEMU_FIN_TODAS` (emitir también los eventos de las listas habilitadas que quedaron
  vacías) esa espera se libera y el juego avanza a **2 escenas rendidas** (STARTRENDER
  dispara). **La semántica quedó zanjada contra el documento de Sega** (§3.7.4.1: *"one of
  four types of interrupt signals (corresponding to the polygon type) is output"* — un EOL
  emite solo el evento de la lista en curso), así que `FIN_TODAS` **no** es lo que hace el
  hardware y no se convierte en comportamiento. El PCW del encabezado está verificado crudo
  (`0x808C0002`, traza nueva `encabezado abre lista N`): la lista es opaca de verdad, **con
  el bit de volumen/sombra encendido** — la hipótesis que queda es que los polígonos con ese
  bit alimentan además la maquinaria de la lista modificadora (el juego habilita opmod en
  `TA_ALLOC_CTRL`) y el hardware emite también el 8 al cerrarla. **Implementado como regla
  estrecha** (`lista_con_volumen` en `taPolyModifier()`/`taListEnd()`): al cerrar una lista
  cuyos encabezados trajeron el bit de volumen o sombra, sale también el evento de su lista
  modificadora asociada, si `TA_ALLOC_CTRL` la habilita y no se cerró sola. Con eso Virtua
  Tennis pasa su espera 3 **sin el experimento**: STARTRENDER dispara y quedó en la espera
  4. Los demos de volúmenes, dentro de su varianza de `rand()` (medida en el mismo binario);
  el resto del subconjunto, byte a byte.
- **Espera 4** (donde está hoy con el experimento): el juego marca "pendiente" (bit 0 de
  `[0x8C45F940]`, PC `8c1fa13e`) y sondea a que su ISR lo limpie. El bit se marca ~61M ciclos
  después del único fin de lista, así que no es la carrera del punto siguiente; falta
  identificar qué evento debería limpiarlo.

Y de esa persecución salieron dos mejoras estructurales que ya quedaron:

- **La traza de entregas del ASIC** (`traza: asic evento ... entregado por IRQ9/B/D` o
  `encolado sin entregar`, con las tres máscaras): responde de una vez "¿le llegó la
  interrupción al juego?", la pregunta de siempre.
- **Los eventos del ASIC ocurren con demora** (`intc_add(evt, cnt)` → cnt×50 ciclos; el bit
  de `SB_ISTNRM` y la entrega esperan al vencimiento): en el chip el fin de lista ocurre
  cuando el TA terminó de consumirla, no en el instante del marcador. dcemu lo disparaba
  instantáneo y le ganaba la carrera al juego que arma su espera justo después de mandar la
  lista. **Con la demora, el arranque de VT se volvió determinista** — la alternancia A/B
  de la tabla A.0b, que dependía de dónde caían las interrupciones, desapareció (3 corridas
  idénticas). El barrido corto de demos y Crazy Taxi, sin cambios.

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

### A.3 — Resuelto para Crazy Taxi: el SDK exige un GD-ROM — **hito D alcanzado el 1 de agosto de 2026**

La hipótesis de A.2 era la correcta, en su forma más literal: algo que el guest pide, que
dcemu contesta sin querer decir nada. Solo que no era un registro: era **la respuesta de un
syscall**.

La cadena de medición, que costó tres corridas:

1. Con la pantalla de carga capturada cuadro a cuadro (`DCEMU_CAPTURA_TODAS`), la primera
   escena de Crazy Taxi resultó no ser negra: dibuja **"LOADING CRAZY TAXI (31K)"** y ahí se
   queda. El juego no estaba colgado: estaba *cargando*, clavado en los primeros 31K.
2. El reporte de syscalls de `--traza-mem` mostró el bucle: `GDROM_INIT` (r7=3),
   `SEND_COMMAND` con **CMD_INIT** (24), `MAINLOOP`, `CHECK_COMMAND` → COMPLETED,
   `CHECK_DRIVE` → `{2, 0x20}`, y vuelta a empezar. Cientos de veces: el juego reintentaba la
   inicialización entera de la lectora, para siempre.
3. `--traza-desde=0c15d5f4` —la dirección de retorno del `CHECK_DRIVE`— mostró la decisión:
   el juego carga el literal **0x80**, lo compara contra el word de tipo de disco que contestó
   el syscall (0x20, CD-ROM/XA) y ante la diferencia devuelve **−5** y reintenta. Es
   `gdFsInit()` del SDK de Katana: **exige que el disco sea un GD-ROM**. El chequeo
   antipiratería del SDK, y ninguno de los dos rips lo trae parcheado: el DCRES (layout
   MIL-CD, datos en el LBA 11702) hace exactamente el mismo bucle que el USA (layout GD,
   datos en 45000).

El arreglo, en `hack_gdrom()` caso `CHECK_DRIVE`: **con disco puesto, el camino de los hooks
contesta GD-ROM (0x80)**, no lo que diga la imagen. Ese camino reemplaza consola, BIOS y
lectora para correr el juego montado, y el disco original de un juego comercial es un GD-ROM;
contestar el tipo de la imagen era describir el rip, no el disco que el juego espera. El
camino por `--bios` no pasa por ahí y sigue viendo el CD que la imagen es, que es lo que su
rama de MIL-CD necesita.

Con eso, los dos rips de Crazy Taxi corren: pantalla de carga (el contador pasa de 31K),
aviso de VMU, **responde al Start**, pantalla de título con su "PRESS START BUTTON"
parpadeando, y el modo attract en 3D — el taxi por la ciudad, 343 escenas en 8 segundos, 528
tiras por escena. Verificado con captura cuadro a cuadro y mandando el Start por
`keybd_event` (un toque de `SendKeys` dura menos que un muestreo del mando: hay que
*mantener* la tecla).

De paso salió y se cerró **C.4**: Crazy Taxi también llama `SYSINFO` función 3, seguía el
puntero nulo del stub mudo y escribía su "identificador" alrededor de la dirección 0x10. La
numeración quedó confirmada contra KOS (`hardware/syscalls.c`: 0 INIT, 2 ICON, 3 ID) y la 3
contesta ahora `SYSID_BASE`. Ver C.4.

Lo que queda de la vía A, medido el mismo día con los dos arreglos puestos:

| imagen | estado |
| --- | --- |
| Crazy Taxi (USA y DCRES) | **corre** — título y attract en 3D |
| Virtua Tennis (los dos rips) | igual que antes: 0 escenas. Su bloqueo es el de A.-1b: las lecturas por `0x902940E4`/`0x020A8618` desde `0x8C1FA438` |
| Capcom vs. SNK | igual: se para **antes** de tocar la lectora (un solo INIT), en el arranque común del SDK — mismo hilo que Virtua Tennis |
| mame4all | idéntico, byte a byte: 93650 colores |

La lectura de `0x2d2d2d0a` quedó explicada por el lado que no acusaba: en Crazy Taxi ocurre
**una sola vez** y el juego sigue; era consecuencia del estado a medio cargar, no la causa. En
Virtua Tennis y Capcom sigue siendo el marcador de su bloqueo.

Del primer rato de juego en vivo salieron tres cosas más, dos ya resueltas el mismo día:

- **La profundidad perdía geometría entera** — calles que desaparecían dejando ver el cielo,
  edificios que se caían según el ángulo de cámara. No era el orden: era la **precisión**. El
  1/w crudo de Crazy Taxi va de 0.01 a 1000 (medido; `--traza-mem` ahora lo imprime al salir),
  y el `glOrtho` lineal de ±32768 sobre un buffer de 24 bits da un paso de 0.0039: la ciudad
  lejana entera (z 0.01..0.1) cabía en 23 pasos, dos paredes vecinas caían en el mismo valor y
  con `GEQUAL` ganaba la que se dibujara después. `profundidad_ta()` guarda `log2(1+z)` sobre
  un rango de ±32 — monótona, así que las pruebas del chip no cambian; el mismo par de paredes
  queda ahora a ~75 pasos. Verificado: los cuatro demos críticos de profundidad idénticos en
  colores (fb_tex 93, rtt_sized 5, tunnel 5151, libdream-ta 65480), los de volúmenes dentro de
  su varianza de `rand()`, y la ciudad del attract completa. Ver "Depth" en `CLAUDE.md`.
- El `printf` de depuración `Oops Debug` de `dibujar_escena()` — borrado.
- **Las texturas pixeladas/con ruido eran los mipmaps**, y está resuelto: el bit del TCW se
  parseaba y solo se logueaba, y una textura con mipmaps guarda sus niveles del 1×1 al
  grande — decodificar desde el offset 0 lee los niveles chicos revueltos, que es
  exactamente el ruido en bloques. El nivel grande empieza en `6 + 2·(4^n−1)/3` bytes (la
  tabla de `pvrtex` de KOS), ÷8 para los índices VQ tras el codebook, ÷4 en paleta de 4 bpp
  y ÷2 en 8. El tranvía, el suelo y los edificios salen bien ahora; el taxi y el HUD nunca
  estuvieron mal porque no llevan mipmaps, y esa asimetría fue la pista. Ninguna demo de KOS
  usa el bit (el subconjunto sensible a texturas sale byte a byte idéntico). Ver "Texture
  formats" en `CLAUDE.md`.

Y de jugarlo en vivo con todo lo anterior puesto salieron dos más, resueltos el mismo día:

- **Texturas "rotando" entre el piso y otros objetos** (el piso muestreando el cielo): la
  caché de texturas tenía **10 entradas** y `get_texture()` escribía
  `cached_textures[cur_tex_count]` sin tope — la textura 11 de una escena escribía fuera de
  los dos arreglos y ligaba ids de GL basura, que crean objetos que se pisan entre sí.
  Ninguna demo pasa de un puñado; una escena de Crazy Taxi usa cientos. `MAX_TEXTURE_COUNT`
  ahora es 1024, con tope que pisa el último slot y avisa por `--traza-mem`. Verificado en
  vivo y con el attract; el barrido corto de demos, idéntico.
- **Los árboles opacos, con caja negra**: la lista punch-through dibujaba sin descartar por
  alfa — no había `glAlphaFunc` en todo `graficos.c` y `PT_ALPHA_REF` (`0x005F811C`) solo se
  respaldaba. Las tiras de la lista 4 van ahora con `GL_ALPHA_TEST` contra ese registro, que
  es exactamente lo que distingue al punch-through en el chip. `conio-basic`, que manda un
  quad punch-through por glifo, queda en sus 2742 píxeles de referencia.
- **La comparación del punch-through quedó en `GEQUAL` contra el umbral con un piso de medio
  paso**, y el camino hasta ahí dejó dos lecciones caras. Se probó la desigualdad estricta
  (`GREATER`) por una teoría plausible sobre el menú, y **rompió el mundo**: el juego manda su
  geometría punch-through con α=1.0, y en las sesiones donde sube `PT_ALPHA_REF` a 255 el
  estricto descarta todo — la ciudad entera caía a su geometría de respaldo sin textura,
  blanca. Y las "pastillas grises" del menú que motivaron todo **no son un bug**: la consola
  real las dibuja exactamente así (verificado contra las capturas de Dreamcast de The King of
  Grabs) — hay que medir contra una referencia antes de perseguir una pantalla "mal". El
  registro no es constante: 0 en varias pantallas, 0x17 en los menús. Se midió con la
  herramienta nueva `DCEMU_TRAZA_ESCENA=N[:M]`, que vuelca el estado GL completo (y el
  `PT_ALPHA_REF`) de las tiras de una escena elegida.

Y de la segunda sesión de juego en vivo salieron tres más, los tres del pipeline translúcido,
resueltos el mismo día — el detalle está en `CLAUDE.md` ("Graphics pipeline"):

- **El autosort de la lista translúcida**: el chip la ordena por profundidad por pixel y dcemu
  la dibujaba en orden de envío — en el menú, la llama del logo (lejana) caía encima de las
  pastillas (cercanas) y las ensuciaba. `compare()` ordena ahora por la z de la tira, de lejos
  a cerca, salvo pre-sort (`ISP_FEED_CFG` bit 0). Con esto el menú quedó idéntico a las
  capturas de consola real. La herramienta que lo destrabó: `DCEMU_PULSAR_START=N[,N2]`,
  que aprieta Start por sondeo del mando — la inyección de teclado desde afuera depende del
  foco de la ventana y Windows la niega; esto navega los menús de forma determinista.
- **Los sprites se encadenaban**: un sprite es una primitiva completa y dcemu confiaba en el
  bit de fin de tira, que Crazy Taxi no manda — dos sprites lejanos quedaban en una sola tira
  y los triángulos puente eran polígonos gigantes negros cruzando el cielo (vértices a
  ±200000 px, medidos).
- **El bit 20 del TSP ("Use Alpha") usado como interruptor del blending**: en el chip solo
  fuerza a 1.0 el alfa del vértice — el de la textura sigue vivo y la mezcla también. Las
  hojas de los árboles (ARGB1555 VQ, lista translúcida, use-alpha apagado) salían opacas con
  su fondo alfa-0 como caja negra. La mezcla la decide ahora la lista y los factores; el bit
  se aplica en los constructores de color de vértice. Con esto los árboles y palmeras quedan
  como en el hardware.

De ahí salieron dos rediseños, hechos el mismo día — el detalle en `CLAUDE.md`:

- **La caché de texturas es persistente e invalida por generaciones**: contadores por página
  de 8 KB en `vram.c` que marcan los dos embudos de escritura, más una generación de paleta;
  cada entrada guarda la suma de su huella y se re-decodifica solo cuando alguien escribió
  adentro. Búsqueda por hash de dirección (la lineal sobre 1024 entradas siempre llenas comía
  lo ahorrado — y de paso salió que la restauración de la cuenta usaba `i`, que los bucles
  del decodificador pisan: ese bug hacía "lenta" la primera medición). `fb_tex` y
  `palette-wormhole`, los dos demos que viven de la invalidación, byte a byte idénticos.
  Si el "mundo blanco" intermitente era el batido de subidas contra el driver, esto lo
  elimina de raíz; queda en observación.
- **Las texturas con mipmaps llevan cadena de GL** (`GL_GENERATE_MIPMAP` + filtro MIN de
  mipmaps): corta el alias del piso lejano en ángulos rasantes. Crazy Taxi con todo puesto
  queda a la par de la base en su peor caso (el attract, que rota texturas) y gana en todo
  lo que no rota.

Los dos residuos que quedaban, hechos también:

- **Los niveles de mip se suben del guest** (VQ, 16bpp twiddled y paleta; BUMP/YUV siguen con
  `GL_GENERATE_MIPMAP`): son los del artista y salen más baratos que regenerar — el attract
  de Crazy Taxi pasa de 36.8 s (base sin nada) a 34.9 s con toda la cadena. La cola de VQ
  termina en 2×2 y se recorta con `GL_TEXTURE_MAX_LEVEL`; sin el recorte la textura queda
  incompleta y GL la muestrea blanca.
- **Las tiras con cero vértices se saltan al dibujar**: los encabezados de sombra dejan
  cientos por escena que no pintaban nada y pagaban todo el estado de GL igual.

---

## Vía B — El AICA (hito E)

> **Esta vía tiene su propio plan desarrollado: [aica-plan.md](aica-plan.md)**, escrito contra el
> documento de arquitectura de Sega. Lo que sigue es el resumen del que salió.
>
> **Hecha hasta la fase 4, y el hito E está alcanzado: cuatro demos suenan** (1 de agosto de
> 2026). Están el G2-DMA, el bloque de registros, el ARM7DI y los 64 canales con PCM y ADPCM,
> más `--captura-audio` para medirlo en un archivo. Quedan la fase 5 (CDDA) y la 6 (el DSP);
> ver "Lo que quedó" y "Lo que sigue faltando" en ese plan.

Siete demos fallan y ninguna suena. El camino de subida del firmware funciona —`libdream-spu`
reporta `Load OK, starting ARM`, o sea que los 2 MB de RAM de sonido y la ventana física se
comportan—; lo que falta es el chip.

### B.-1 — El mapa de registros está documentado

La documentación de Sega trae el AICA entero, y eso baja el riesgo de esta vía de "hay que
deducir el chip" a "hay que implementar lo que dice el papel". El documento es
**Dreamcast/Dev.Box System Architecture** (Sega, 99/09/03), y **no va en el repositorio** —son
2,5 MB de binario—; se baja de
<https://segaretro.org/images/7/78/DreamcastDevBoxSystemArchitecture.pdf> y conviene dejarlo
en `docs/`, que ya está ignorado para ese nombre. Lo que aporta:

- **§8.4.5, "AICA Register"**: el mapa completo. 64 slots de canal, cada uno de 0x80 bytes,
  desde `0x00700000` en direcciones del G2 (`0x00800000` vistas desde el ARM). Por canal:
  `SA[22:0]` dirección de inicio, `LSA`/`LEA` el bucle, `LPCTL` su modo, `PCMS` el formato
  —0 PCM de 16 bits, 1 PCM de 8, 2 ADPCM de Yamaha, 3 ADPCM de flujo largo—, la envolvente
  (`AR`, `D1R`, `D2R`, `RR`, `DL`, `KRS`), el tono (`OCT`, `FNS`), el LFO (`LFOF`, `PLFOWS`,
  `PLFOS`, `ALFOWS`, `ALFOS`), nivel y paneo (`DISDL`, `DIPAN`, `TL`), el filtro (`FLV0`-`FLV4`,
  `FAR`, `FD1R`, `FD2R`, `FRR`) y `KYONB`/`KYONEX` para disparar.
- **§8.1.1, "Technical Explanation Concerning Audio"**: control de bucle, ADPCM, AEG, PG, LFO,
  mezclador, FEG y el DSP de audio, uno por uno.
- Un detalle de acceso que hay que respetar desde el principio: *"register accesses by the SH4
  are 4-byte accesses only, and only the lower 16 bits are valid"*.

La ventana que dcemu ya reserva (`0x00700000-0x00707FFF`, `aica_mem`) cubre el bloque.

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

### C.4 — Los stubs `SYSINFO` y `UNKNOWN` — **resuelto, y SYSINFO ahora contesta**

Eran `RTS` + `NOP`: volvían sin hacer nada y **sin decirlo**, que es la forma exacta que tuvo
cada uno de los agujeros de este árbol. Primero se les puso el mismo par `RTS` + opcode ilegal
que los otros tres stubs, despachado a `hack_mudo()`, que reporta por `--traza-mem` el nombre,
los cuatro argumentos, el PC y el PR.

La pista que quedaba anotada se confirmó el 1 de agosto de 2026, contra
`kernel/arch/dreamcast/hardware/syscalls.c` de KOS: las funciones de `SYSINFO` son **0 INIT,
2 ICON y 3 ID**, y la 3 devuelve en R0 **un puntero** al identificador de 8 bytes —
`syscall_sysinfo_id()` lo desreferencia—. En la consola ese puntero es `0x8C000068`, adonde
INIT lo copió de la flash; dcemu deja lo mismo en `SYSID_BASE` desde `main()` (C.3), así que
`hack_sysinfo()` contesta 0 a INIT (no hay nada que hacer) y `SYSID_BASE` a ID.

No era limpieza teórica: **Crazy Taxi llama la función 3**, seguía el puntero nulo del stub
mudo y copiaba su "identificador" escribiendo alrededor de la dirección 0x10 — visible en
`--traza-mem` como ocho escrituras sin emular en `0x10`-`0x17`. Con la respuesta puesta,
desaparecen. ICON (la 2) y el vector sin nombre de `0x8C0000E0` siguen mudos, y se ven.

### C.5 — La deuda de verificación de las demos

33 binarios están en "dibujan; la captura tiene contenido pero no se revisó una por una". No
es un fallo, es que nadie las miró. Una pasada con `--captura-gl` y el ojo cierra el hito F y
puede destapar cosas —así salieron el filtro de textura y el recorte del volcado—.

### C.7 — La BIOS perdió su texto — **resuelto: era `SB_LMMODE0`**

**Lo decide el guest con un registro, y dcemu no lo miraba.** Lo zanja la documentación de
Sega, *Dreamcast/Dev.Box System Architecture* §8.4.1.1: la dirección de `SB_C2DSTAT` nombra el
**camino** —`0x10000000` polígonos, `0x10800000` convertidor YUV, `0x11000000` textura directa,
y `0x12`/`0x13` sus imágenes— y sobre el último dice:

> *"When transferring data to the texture memory via the TA FIFO buffer and Direct Texture
> Path, either 64-bit access or 32-bit access can be specified by setting the SB_LMMODE0 and 1
> registers."*

`SB_LMMODE0` (`0x005F6884`) manda sobre `0x11000000-0x11FFFFFF` y `SB_LMMODE1` (`0x005F6888`)
sobre su imagen en `0x13000000`. Bit 0: **0 = 64 bits (por omisión), 1 = 32 bits**. Los dos ya
tenían respaldo en `control_mem`: las escrituras del guest llegaban desde siempre y nadie las
leía — la misma forma que el `SB_G1SYSM`, el `REVISION` del PVR y el `SB_GDLEND`.

Medido, no deducido: al salir, **mame4all deja `SB_LMMODE0 = 0x00000001`** y **la BIOS lo deja
en `0x00000000`**. Con eso los dos casos que no podían ser ciertos a la vez se explican solos,
y las dos hipótesis que se habían probado antes —la ventana y la lectura del framebuffer— eran
descartes correctos.

Verificado en los dos sentidos: la BIOS vuelve a sus 145 píxeles de glifo y sus 425 de borde,
idéntico a `7e3119b`; mame4all sale **byte a byte idéntico** a su referencia. Y sin tocar la
store queue, que es otro camino: `pvr-strided_texture` sigue en sus 240000 píxeles no negros.

Lo que **no** vuelve es el contraste de antes de `dcc1292`: el texto se lee apagado porque el
fondo ahora es el celeste correcto en vez de negro. Eso es lo esperado, no un residuo.

#### Cómo se llegó, que es la parte reutilizable

Abierto el 31 de julio de 2026, porque se vio en vivo que el menú del boot ROM ya no muestra
sus etiquetas. **Es una regresión real y está acotada a dos commits**, ninguno de esta corrida.

El síntoma no es solo el menú: el panel del selector de fecha sale igual, con sus flechas y
sin una letra. La BIOS no rasteriza texto en ningún lado.

**Bisecado hasta el commit, y es uno solo: `9edc6f5`.** Se arrancó `--bios --salir-tras=25
--captura-gl` en cada revisión y se contaron los colores del recorte (290,258)-(400,296), que
es el interior de la pastilla *Play*. Contar colores y no contraste es lo que hace la medida
fiable, y es lo que corrigió una conclusión equivocada — ver más abajo.

| revisión | relleno | borde | glifo | colores |
| --- | --- | --- | --- | --- |
| `c86bb7b` | (101,49,9) ×3598 | (203,150,111) ×425 | (127,74,34) ×145 | 6 |
| `1175c59` las dos ventanas de VRAM | (101,49,9) ×3598 | ×425 | **×145** | 6 |
| `95d94a8` DMTE del DMAC | (101,49,9) ×3598 | ×425 | **×145** | 6 |
| `d7cd243` UBC | (101,49,9) ×3598 | ×425 | **×145** | 6 |
| `dcc1292` plano de fondo y POLY1 | **(189,152,113)** ×3598 | ×425 | **×145** | 6 |
| `56963d3` | (189,152,113) ×3598 | ×425 | ×145 | 6 |
| `7e3119b` | (189,152,113) ×3598 | ×425 | **×145** | 6 |
| `9edc6f5` el CH2 DMA lineal | (189,152,113) **×4168** | — | **—** | **4** |
| `master` (43c2189) | (189,152,113) ×4168 | — | — | 4 |

El glifo son 145 píxeles y el borde de la pastilla 425, **idénticos en todas las revisiones
hasta `7e3119b` inclusive**. En `9edc6f5` los dos desaparecen a la vez y se funden en el
relleno: 3598 + 425 + 145 = 4168 exacto. O sea que no se perdió "el texto": se perdió **toda
la capa de detalle de la pastilla**, borde incluido, que es la firma de una textura que dejó
de decodificarse.

**`dcc1292` está absuelto, y la primera lectura de esto era un error de método.** Lo único que
cambió ahí es el relleno, de marrón oscuro a pastel, porque ese commit arregló el plano de
fondo: la misma pastilla translúcida sobre un fondo claro en vez de negro. Medir "píxeles
lejos del color dominante" contaba el borde como glifo mientras el fondo era negro y dejaba de
contarlo cuando se aclaró, y de ahí salía un falso 582 → 145. Mismos píxeles, distinta cuenta.

**El mecanismo está probado, no deducido.** `9edc6f5` hace que `ch2_dma_ejecutar()` reescriba
un destino de la ventana de 64 bits a la de 32 (`dst = (dst & 0x007FFFFF) | 0x05000000`), con
lo que la transferencia sale lineal. Anulando **solo** esa reescritura y volviendo a arrancar
la BIOS, el texto vuelve exacto: 145 píxeles de glifo, igual que `7e3119b`. Así que la BIOS
sube esa textura por el CH2 DMA y la necesita entrelazada, porque `get_texture()` la lee con
`vram64_leer()`.

Y ahí está el conflicto, que `CLAUDE.md` ya anticipaba: *"lo único que falta confirmar en
hardware es que el DMA no distinga las dos ventanas; es la única lectura que encaja con las
medidas"*. **Esta es la medida que no encaja**: mame4all necesita lineal y la BIOS entrelazado,
por el mismo camino. Falta el factor que los distingue, y elegir cuál romper no es el trabajo.

**La ventana no es el factor, y está descartado por medición.** Se armó el `.iso` de mame4all
—ver abajo— y se instrumentó `ch2_dma_ejecutar()` para informar el destino real, antes de la
reescritura. Los dos usan **la misma ventana, la `0x11`**:

| | destino | tamaño |
| --- | --- | --- |
| mame4all | `0x11000000` siempre, offset **0** | 614400 bytes = 640×480×2, un cuadro entero, repetido |
| boot ROM | `0x11413000`, `0x1141b000`, `0x1151b000`, `0x1151d000`, `0x11521000`… | 8 KB a 1 MB, variados: son texturas |

Se probó igual la regla "`0x11` lineal, `0x04` entrelazado": mame4all sale **byte a byte
idéntico** a la referencia y el texto de la BIOS **sigue en cero**. Confirma lo anterior desde
el otro lado.

**Y tampoco es de la lectura del framebuffer**, que era la otra hipótesis y la más física —que
mame4all programara un paso o un módulo que reconstruyera el entrelazado al mostrarlo—.
Volcando sus registros con el DMA entrelazado (`--volcar=a05f8044:20`): `FB_R_CTRL` `0x00800005`
(RGB565), `FB_R_SIZE` `0x00177D3F` —640 de ancho, 480 de alto, módulo 1—, `FB_W_LINESTRIDE`
`0xA0` = 1280 bytes por línea. Todo normal, sin ningún truco que deshaga el entrelazado. Y la
captura entrelazada sale duplicada a lo ancho y aplastada a la mitad, que es exactamente lo
que describen los documentos.

Quedan dos candidatos, los dos por medir:

- **Dónde cae el destino.** mame4all escribe en el offset 0, que es el cuadro que el PVR
  muestra; el ROM escribe arriba de `0x413000`, que son texturas. dcemu ya sabe calcular esa
  región: `armar_volcado_si_muestrea_framebuffer()` en `graficos.c` la deriva de `FB_W_SOF1`,
  `FB_R_SOF1`, `FB_W_LINESTRIDE` y `PCLIP_Y`. La regla sería "si el destino cae dentro del
  cuadro, lineal; si no, entrelazado".
- **El tamaño de la transferencia**: 614400 es exactamente un cuadro.

Ninguna de las dos es una explicación *física* —el chip no puede saber "esto es un cuadro"—,
así que las dos son modelos que encajan, no la verdad del hardware. Antes de elegir una
conviene documentación real del Holly sobre cómo el CH2 DMA direcciona la ventana `0x11`.

**Cómo se arma el `.iso` de mame4all**, que hacía falta para todo esto y no estaba: las dos
subcarpetas (`roms/`, `samples/`) están **vacías**, así que la imagen es sólo los 17 archivos
de la raíz, en ISO9660 nivel 1 (nombres 8.3 en mayúsculas con `;1`, que es justo lo que
`min_iso_name_translate()` deshace). Con `pycdlib`:

```python
import pycdlib, os, glob
iso = pycdlib.PyCdlib()
iso.new(interchange_level=1, vol_ident='MAME4ALL')
for p in sorted(glob.glob('roms/mame4all/*')):
    if os.path.isfile(p):
        iso.add_file(p, iso_path='/' + os.path.basename(p).upper() + ';1')
iso.write('roms/mame4all.iso'); iso.close()
```

Queda en `roms/mame4all.iso`, que está fuera de git. Arranca y dibuja su menú de selección
—*AGED*, *CLASSIC*, *GOLD*— con 93797 colores, y ése es el patrón de referencia contra el que
comparar cualquier cambio del CH2 DMA.

Está descartado, por medición y no por razonamiento, que sea de esta corrida: la captura del
menú con `master` y con la rama entera es **byte a byte idéntica**, mismo md5.

### C.8 — Las áreas imagen del mapa de memoria

La tabla 2-2 del documento de arquitectura —*"the addresses shown in parentheses are an image
area"*— es una lista de comprobación contra el mapa de dcemu, y destapó un hueco:

| documentado | imagen | acceso | dcemu |
| --- | --- | --- | --- |
| `0x00000000` boot ROM, `0x00200000` flash | `0x02000000`, `0x02200000` | R/- | falta la imagen |
| `0x0C000000` memoria del sistema | `0x0E000000` | R/W | ✓ |
| `0x10000000` conversor de polígonos [TA FIFO] | `0x12000000` | **-/W** | falta la imagen |
| `0x10800000` conversor YUV [TA FIFO] | `0x12800000` | **-/W** | falta la imagen |
| `0x11000000` textura [TA FIFO] | `0x13000000` | **-/W** | ✓, y solo escritura, correcto |
| `0x04000000` textura, acceso de 64 bits | `0x06000000` | R/W | **arreglado** |
| `0x05000000` textura, acceso de 32 bits | `0x07000000` | R/W | **arreglado** |
| `0x01000000` área externa del G2 | `0x03000000`, `0x14000000-0x17FFFFFF` | según el dispositivo | falta |

`0x06` y `0x07` ya estaban en `mem_zone[]` como alias desde siempre, pero **no en
`mem_hash_read`/`mem_hash_write`**, así que un guest que las usara caía en `mem_read_error` en
vez de leer la RAM de vídeo. Van con sus formas P2 (`0xA6`, `0xA7`), y `0x06` entra además en
`VRAM_VENTANA_64()` porque es la imagen de la de 64 bits y entrelaza igual. Verificado que no
mueve nada: 544 casos en verde, el texto de la BIOS y mame4all idénticos, y
`pvr-strided_texture` en sus 240000 píxeles.

**Y explica el `0xA1000400` de Virtua Tenis 2**: `0x01000000-0x01FFFFFF` es el **área externa
del bus G2**, o sea un dispositivo de expansión. En una consola de serie no hay ninguno, así
que lo que sondea no existe — igual que el `0x03010000` que miraba Crazy Taxi. No se mapeó
porque el documento dice "depends on device" y no define qué contesta un bus vacío; inventar
un valor es justo lo que hace falta no hacer. Pero deja de ser una dirección misteriosa.

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

### D.1 — Los bits RM y DN de FPSCR — **resuelto el 1 de agosto de 2026**

Los trajo la pasada de SingleStepTests, que además los pilló sola: RM se aplica desde
`UpdateFPSCR()` con `_controlfp`/`fesetround` y DN en `fpu_dn_s()`/`fpu_dn_d()`, aplastando
desnormalizados de entrada y de salida. Era exactamente lo que decía esta entrada —el valor de
reset de RM es 01, truncar, así que **todo** lo que corre en la consola usaba el redondeo
equivocado— y de paso salieron nueve cosas más del núcleo. Ver `docs/sh4-conformidad.md`, "La
segunda pasada".

Lo que quedó de esa lista: la causa I por sí sola y el patrón de qNaN que genera el chip
(`H'7FBFFFFF` en simple, `H'7FF7FFFF FFFFFFFF` en doble). Los dos siguen siendo baratos de
describir y caros o inocuos de arreglar, en ese orden.

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
4. ~~**Vía D.1**~~ — **hecho el 1 de agosto de 2026**, y no costó una llamada: la pasada de
   SingleStepTests trajo RM y DN y otros nueve arreglos del núcleo. Queda **D.2**, las dos
   filas de `SGR`, que sigue siendo lo más barato de la lista.
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

| vía | sesiones | estado |
| --- | --- | --- |
| C.1-C.4 (residuos que alimentan A) | 1 | hecho el 31 de julio |
| A.0 (la tabla) | 0,5 | hecha, y rehecha el 1 de agosto — ver A.0b |
| A.1-A.2 (el hito D) | 2 a 8, sin piso claro | **pendiente, y es lo que importa** |
| D.1 (RM, DN) | 1 | hecho el 1 de agosto |
| D.2 (las dos filas de `SGR`) | 0,5 | pendiente, lo más barato de todo |
| B (el AICA completo) | 8 a 15 | hasta la fase 4; quedan CDDA y el DSP |
| C.5 (revisar las 33) | 1 | pendiente |
| D.3-D.5, E | 3 | pendiente |

El rango de A es honesto: puede ser un registro sin caso —como fueron el `REVISION` del PVR y
el `SB_G1SYSM`, media hora cada uno una vez encontrados— o puede ser una función del juego que
depende de algo que no está. Lo que acota el rango es A.0.
