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

Actualizado el 2 de agosto de 2026.

| | |
| --- | --- |
| Demos de KOS que funcionan | **104** de 135 (100 en lo visual y cuatro que además suenan) |
| Fallan por algo que falta emular | **3**: CDDA y dos de sonido con causa propia |
| No aplican: piden periféricos | 28 |
| Filas de `opcodes[]` implementadas | 239 de 239, con **615 casos unitarios** en verde |
| El núcleo contra SingleStepTests/sh4 | **116.500 casos, 0 fallos** |
| Arranque por boot ROM | llega al menú, arranca el juego del disco y salta |
| mame4all | arranca desde el `.iso` y dibuja su menú |
| **Los cuatro juegos comerciales** | **corren**: Crazy Taxi (A.3), Virtua Tennis (A.6), Capcom vs. SNK (A.7) y Virtua Tenis 2 (A.8) — todos con título y attract/menú |

Ya no falla nada del PVR ni del núcleo SH-4. Lo que queda se reparte en cinco vías que casi
no se tocan entre sí, así que el orden es negociable salvo donde se dice lo contrario.

## Los hitos

Siguiendo la letra donde quedó `bios-boot-plan.md` (el hito C es "el boot ROM arranca el
juego del disco"):

| hito | qué se ve | vía | estado |
| --- | --- | --- | --- |
| **D** | un juego comercial dibuja su primer cuadro | A | **alcanzado el 1 de agosto de 2026 (Crazy Taxi, A.3) y por cuadruplicado el 2 (VT A.6, CvS A.7, VT2 A.8)** |
| **E** | una demo de KOS suena | B | **alcanzado el 1 de agosto de 2026: suenan cuatro** |
| **F** | las 135 demos revisadas una por una, sin deuda de verificación | C | pendiente |

El hito D era el que importaba: es lo único que separaba a dcemu de "corre homebrew" a "corre
un juego". Crazy Taxi (los dos rips) muestra su pantalla de carga, el aviso de VMU, responde
al Start, pasa a su pantalla de título y corre el modo attract en 3D — el motor entero, a 528
tiras por escena. **Virtua Tennis cayó el 2 de agosto** (aviso de VMU, título, Main Menu y
attract en 3D a 2400 tiras por escena — la entrega por nivel del ASIC más la demora del CH2
DMA, ver A.6), **Capcom vs. SNK el mismo día** (aviso de Memory Card y pantalla de título —
era otro bloqueo: el hook del GD aceptaba toda petición al instante donde el driver de la
BIOS acepta una por vez, ver A.7), **y Virtua Tenis 2 cayó solo con esos arreglos** (aviso
de VMU y secuencia de título; sus sondeos de la BBA son benignos, ver A.8). **Las cuatro
imágenes comerciales que se parsean corren.**

---

## Vía A — El juego arranca y no dibuja (hito D) — **cerrada el 2 de agosto de 2026: los cuatro juegos corren**

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
| Virtua Tenis 2 (USA) | 2, LBA 45000 | 0 | 16384 / 8388608 | `a1000400`… ← PC `8c28cd86` | 9, último a 0,82 s | franja — **corre desde el 2 de agosto, ver A.8** |
| DCDoom | 2, LBA 11702 | — | — | — | — | **corre y es jugable desde el 2 de agosto** — es Windows CE, ver A.9 |
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
**Resuelto el 2 de agosto: no había tal archivo — el binario de arranque se llama
`0WINCEOS.BIN` y el nombre lo declara el IP.BIN. Es un juego de Windows CE; ver A.9.**

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
- **Espera 4** (donde quedó ese día; su resolución es A.6): es `while ((obj = cola_pop()) == NULL) yield;` — la rutina
  `8c1f9960` entrega búferes de comando que el pipeline de cuadro recicla, y el reciclador es
  la **cadena enlazada de callbacks** que recorre el handler del render-done (entrada 2,
  `8c1e84c0`: `for (n = [8c1e8598]; n; n = n->sig) n->fn(n->arg)`). El juego encadena un nodo
  al armar cada render y el callback, al correr, des-encadena y libera; dcemu emitió sus
  render-done antes de que el nodo existiera y la cadena corrió una sola vez (medido con
  watchpoint-lectura sobre `8c1e8598`). El bit que se sondea es el **25** de `[0x8C45F940]`
  (no el 0: el volcado de registros engaña según el instante), marcado en `8c1fa0de`.

  De perseguirla quedaron dos semánticas corregidas de paso, las dos del papel:

  - **RENDERDONE sale de STARTRENDER** (cb_tastart, los tres bits del documento: 0 TSP,
    1 ISP, 2 Video), no de "todas las listas cerradas" — un juego que habilita listas que no
    manda no lo recibía nunca, y KOS lo recibía antes de tiempo.
  - **La demora del render** (~2M ciclos): el juego arma su espera ~800K ciclos después de
    escribir STARTRENDER; el evento instantáneo se le adelantaba.

  **La regresión de la demora del render, medida con precisión**: los demos estáticos del
  subconjunto quedan byte a byte (conio, rtt_sized, strided, bumpmap); los animados driftean
  en decenas de píxeles/colores porque la fase de cuadro corre ~10 ms — KOS espera el
  render-done por semáforo y ahora llega cuando el render "terminó", no al cerrar la lista.
  Es el costo esperable de la semántica correcta, no una rotura; si un barrido completo lo
  confirma, la línea base de capturas se re-toma.

  Y una herramienta nueva: **`DCEMU_RTC_FIJO=N`** — el RTC arranca en N y avanza con el
  tiempo emulado. El RTC del anfitrión era la última fuente de no-determinismo (semilla y
  fase del segundo: el juego espera el tic en su arranque, congelarlo del todo lo cuelga);
  con esto una corrida se reproduce exacta, que es lo que permitió medir todo lo anterior.
  La "rama mala" resultó ser un timeout de ~10.5 s emulados, no un cuelgue.

  Lo que falta: que el pipeline de cuadro del juego gire — su cadena de render-done tiene
  que correr una vez POR render con el nodo ya puesto, y el juego consume además los eventos
  por el estado de `SB_ISTNRM` (los acks). Es trabajo del modelo de eventos del ASIC, no un
  registro suelto. **Resuelto en A.6**: era exactamente eso — la entrega por nivel más la
  demora del fin del CH2 DMA.

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
| Virtua Tennis (los dos rips) | ~~0 escenas~~ → **corre desde el 2 de agosto**: aviso de VMU, título, Main Menu y attract en 3D — ver A.6 |
| Capcom vs. SNK | ~~2 escenas~~ → **corre desde el 2 de agosto**: aviso de Memory Card y pantalla de título — ver A.7 |
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

### A.5 — El "mundo blanco" de Crazy Taxi en juego, acorralado (1 de agosto, noche)

Reaparecido jugando (no en el attract), con reproductor determinista propio:
`DCEMU_RTC_FIJO=2345678901 DCEMU_PULSAR_START=300,1100 DCEMU_PULSAR_A=1 DCEMU_SOLO_A=1`
llega al juego real en ~35 s emulados con el mundo blanco. Eliminado por medición, en orden:
la clave del caché sin el bit de mip (arreglado igual, era real), el muestreo de mipmaps
(`DCEMU_SIN_FILTRO_MIP`: sigue blanco), el caché entero (`DCEMU_SIN_CACHE_TEX`: sigue), el
alpha test del punch-through (`DCEMU_SIN_ALPHATEST`: sigue), el fondo (`DCEMU_FONDO_MAGENTA`:
no aparece magenta ⇒ todo lo blanco está DIBUJADO), errores de GL en la subida (cero), las
UV (sanas), y el contenido de VRAM de las texturas sospechadas (`DCEMU_VOLCAR_TEX=addr`
vuelca los bytes que consumió el decodificador EN el draw: una "dominante" resultó ser la
cara de un peatón, perfecta).

**Lo que quedó clavado**: los píxeles blancos pertenecen a 1040 tiras por escena con textura
`0x00400000` (8×8, 565, TCW `0x08080000` — tamaño en campos cero, dirección justo en el
borde del banco 1) y **todas con `vol=1`**: son los polígonos de dos volúmenes — los que
reciben la sombra del taxi — que el attract no usa. Dos sondas deciden:

1. `DCEMU_VOLCAR_TEX=400000` en el reproductor: si esos 128 bytes son blancos, el juego
   de verdad muestrea una textura blanca diminuta y el problema es el **color de vértice**
   o la **segunda pasada de volumen** (dibujar_escena repinta las tiras `vol=1` con el
   juego 1 dentro de la plantilla — si los volúmenes degenerados marcan la pantalla entera,
   esa pasada repinta el mundo).
2. El parseo del encabezado de dos volúmenes (POLY3/POLY4, TSP0/TCW0 en palabras 2-3 según
   §3.7.5.2) contra lo que llega: la traza nueva imprime el TCW crudo por tira.

**Resuelto el misterio de la intermitencia (2026-08-01, madrugada) — es determinista, y el
blanco es el resultado normal; lo raro es la corrida limpia.** Medido con el reproductor
(5 corridas byte a byte idénticas en totales de VRAM y flujo de syscalls):

- **El emulador es determinista**: b1≡b2≡c2≡c3 exactas. La corrida "limpia" difería en una
  sola cosa: `mando: gamepad conectado` (el pad del usuario estaba encendido). Con pad, el
  flujo de syscalls corre **una iteración de sondeo adelantado** (el par MAIN_LOOP +
  CHECK_DRIVE aparece en la línea 30 en vez de la 1242) — bytes de entrada distintos mueven
  el timing del guest — y esa fase decide una carrera **interna del juego**.
- **La lectora queda absuelta**: las 487 lecturas (32 MB) son idénticas en corrida blanca y
  limpia — los datos SÍ llegan a RAM. Lo que jamás ocurre en las blancas es la **subida
  RAM→VRAM**: ~900 KB por CH2 DMA a `0x111A0000-0x113F0000` (las texturas de la ciudad,
  ~600 transferencias) que la limpia hace durante la carga. A los 90 s la ventana 11 sigue
  clavada en el mismo byte: la fase de subida no es lenta, **nunca arranca**.
- Con las texturas ausentes el juego dibuja su mundo de respaldo: 2104 tiras/escena atadas
  a la 8×8 de `0x00400000` moduladas por color de vértice gris — ESO es el blanco. El flujo
  del TA es idéntico entre corridas blancas (volcados de escena byte a byte iguales), o sea
  que no hay corrupción de parseo.
- Niebla y volúmenes, absueltos de esto: `DCEMU_SIN_NIEBLA`/`DCEMU_SIN_VOLUMEN` no cambian
  el resultado (la pasada de niebla además dibuja cero tiras en carrera: densidad `0x807F`
  satura el índice a la ranura 127, cuya alfa es 0 — el juego la deja "apagada" así). Los
  volúmenes causan el OTRO síntoma: el manto oscuro de media pantalla (ver abajo).

**El gamepad NO es workaround** (probado en vivo la misma noche: pad conectado y jugando,
sigue blanco). Lo que la corrida limpia demuestra es otra cosa: la fase de subida EXISTE en
el juego y una perturbación de timing de entrada la destrabó UNA vez — la carrera interna
cae del lado malo en casi todas las líneas de tiempo de dcemu, y en la consola real caía
del bueno. Hay que encontrar qué espera el cargador, no perturbarlo.

**Los datos duros para retomar** (todos medidos, reproducibles con el reproductor de
arriba + `--traza-mem`, 60 s):

- Discriminador instantáneo al salir: `bytes a RAM de video por ventana:` — **blanca:
  `11=5565728`; limpia: `11=6481248`**. Equivalente: en la limpia hay `CH2 DMA ... ->
  111a0000-113fxxxx` (~600 transferencias, las texturas de ciudad); en la blanca los
  destinos saltan de `1140xxxx` para arriba. Los autos, taxi y HUD son del lote base y
  se ven bien siempre — lo que falta es solo el lote de ciudad.
- El flujo de syscalls GD (`hack:` en stderr) es el MISMO multiconjunto en ambas (8654
  llamadas, 487 lecturas, 32 MB): los datos llegan a RAM. La única diferencia es de fase:
  un par MAIN_LOOP (r7=2) + CHECK_DRIVE (r7=4) corrido de la línea 30 a la 1242.
- En la limpia, las subidas CH2 aparecen intercaladas entre tandas de lecturas durante la
  carga; en la blanca las lecturas corren seguidas y la fase de subida no llega nunca.
  A los 90 s la ventana 11 sigue en el mismo byte: **no es lenta, no arranca**.
- Los PR de los sitios de llamada del cargador del juego (para `--traza-desde`):
  SEND=`0c15d42a`, CHECK=`0c15d4e8`, CHECK_DRIVE=`0c15d5f4`, MAIN_LOOP=`0c15d6ce`
  (PC siempre `8c000202`, el stub).

**Las sondas que siguen, en orden de costo:**

1. **`CHECK_DRIVE` contesta STANDBY (2) siempre**, porque en el camino de syscalls nadie
   mueve `gdrom.unidad`. En la consola el drive queda en **PAUSE (1)** al terminar una
   lectura y el cargador de Katana puede estar esperando esa transición para procesar el
   lote y disparar las subidas. Experimento de una línea en `hack_gdrom()` (vector r7=4):
   contestar 1 tras una lectura completada (o probar 1 fijo) y mirar el discriminador.
2. **`--watchpoint=5F6808:4` (SB_C2DST)** en el reproductor: da el PC/PR del código que SÍ
   programa las subidas del lote base en la corrida blanca. Con ese PC, `--traza-desde`
   sobre la rutina muestra la decisión que salta el lote de ciudad — qué variable mira.
3. **Variar la demora del fin de DMA de Maple en `INTC_DEMORAS`** (la fase de entrada es
   lo único que movió la aguja): si una demora más realista hace caer la carrera siempre
   del lado bueno, es la señal de que el modelo de eventos es el que manda aquí — la misma
   familia que la espera 5 de Virtua Tennis (reciclaje de buffers por render-done).

Residuos menores reportados en vivo, para después: el borde de los neumáticos sin
transparencia (alfa del punch-through en texturas chicas, probablemente el piso del
umbral), y texturas de autos que se rompieron pasados ~80 s de una corrida larga
(¿desalojo del caché con el juego avanzado? medir con `DCEMU_SIN_CACHE_TEX`).

**Resuelto (2026-08-02): no era la lectora — era `SB_SBREV` leída de basura de heap, y
el "es determinista" de arriba era falso.** Al retomar para correr la sonda 1, la línea
base se negó a reproducirse: mismo binario, mismos parámetros, sin gamepad — una corrida
limpia (`11=6481248`) y la siguiente blanca (`11=5565728`). Ocho corridas de 1 s dieron
exactamente **dos líneas de tiempo** (5 y 3), con la bifurcación siempre en el mismo
sitio. La cacería, con una herramienta nueva (`DCEMU_TRAZA_EN_MS=N[:M]`, en `traza.c`:
checkpoints de PC y registros por ms emulado, y la traza de instrucciones armada al
cruzar un instante en vez de un PC):

- Checkpoints idénticos hasta el ms 118; en el 119 una línea va exactamente una
  escritura (4 bytes) adelante de la otra en el mismo bucle. La primera interrupción se
  entrega recién a ~330 ms, así que las interrupciones quedaron absueltas; `--sin-audio`
  y `--sin-aica` tampoco cambiaron nada.
- La traza del ms completo da la instrucción exacta: `0x0C073944 MOV.L @R1,R3` con
  `R1=0xA05F689C`. Una línea leyó 0; la otra, `0x0BC4C2C9`.
- **`0x005F689C` es `SB_SBREV`, la revisión del System Block del Holly** (`0x0B` en una
  consola de serie; así la inicializa reicast). Sin caso de lectura caía al respaldo de
  `control_mem` — que era un **malloc sin limpiar**: 64 KB del heap del CRT con basura
  reciclada de las cargas del arranque, distinta según la historia de asignaciones del
  proceso. Katana la compara contra 8 a los 118 ms de encendido para elegir su camino de
  inicialización: **con < 8 toma el camino de Holly viejo y la fase de subida RAM→VRAM
  del lote de ciudad no arranca nunca — ese es el mundo blanco**; con ≥ 8 arranca. La
  madrugada de las cinco corridas "deterministas" el heap repartió ceros cinco veces; la
  corrida limpia con gamepad fue el heap repartiendo basura ≥ 8 — la correlación con el
  pad era coincidencia, y la "carrera interna del juego" era este volado de moneda en el
  arranque del proceso.

Los dos arreglos: **todos los bloques de `inicializar_memoria()` con `calloc`** (un
registro sin caso debe contestar su valor de reset, no la historia del heap; además es
lo que vuelve reproducible una corrida), y **`SB_SBREV` contestada de verdad con
`0x0B`** — la tercera de la familia `REVISION` / `SB_G1SYSM`. Verificado: seis corridas
cortas con checkpoints byte a byte idénticos (única línea distinta: los ms reales del
resumen final), y dos de 60 s con `11=6481248` y 8654 syscalls exactos las dos — **la
fase de subida arranca siempre; piso y edificios con textura, confirmado en vivo**. Las
sondas 1-3 de arriba quedan superadas sin correrse: `CHECK_DRIVE` sigue contestando
STANDBY y el juego carga igual (PAUSE tras lectura queda como nota de fidelidad, no como
pendiente).

Residuos vigentes tras jugar con esto puesto (2026-08-02, los tres estables ahora que el
emulador es determinista): la sombra de los autos oscurece el techo en vez del piso — la
simplificación documentada de `marcar_volumenes()`, el arreglo sigue siendo el conteo
por caras de abajo; las ruedas sin transparencia (ya anotado arriba); **los árboles a lo
lejos se ven como un triángulo invertido** — nuevo, huele a los niveles chicos de mipmap
de los sprites VQ mal decodificados u orientados; y las texturas de autos que se rompen
avanzada la corrida (ya anotado arriba, sospecha de desalojo del caché).

**Los árboles-triángulo y los autos deslavados cayeron juntos (2026-08-02): eran un
use-after-free, no un error de offsets.** `DCEMU_SIN_FILTRO_MIP` partió el problema en dos
(sin filtro de mipmaps las palmeras salían con sus frondas y los autos nítidos ⇒ los
niveles chicos contenían basura), la auditoría de offsets dio limpia contra la tabla de
pvrtex en los tres formatos… y la respuesta estaba en el orden: `free(plano)` corría entre
la decodificación del nivel grande y el bucle de niveles chicos, que lee `plano` — el
`malloc` de cada nivel reciclaba el bloque recién liberado y salía basura estructurada,
dependiente de la historia del heap (por eso también "se pudrían" texturas avanzada la
corrida). El free va después del bucle ahora. Verificado: el cuadro 40 s del reproductor
con palmeras verdes y tráfico nítido CON mipmaps activos, confirmado en vivo jugando, y
los diez demos KOS de control byte a byte idénticos (ninguno usa el bit de mipmap — por
eso el barrido nunca podía ver esto). La sombra del taxi, confirmada bien en vivo tras el
conteo por caras. **Queda pendiente de esa lista: las ruedas sin transparencia.**

**Y de validar en vivo salió otro clásico (2026-08-02): las sombras de los peatones como
"un cuarto de círculo repetido cuatro veces".** Esa frase es la firma: el juego guarda UN
cuarto del círculo y lo espeja con los bits Flip U/V del TSP (18/17), que dcemu ignoraba
— como también los Clamp (16/15, que le ganan al Flip) — dejando todo en el `GL_REPEAT`
de fábrica. `aplicar_filtros()` pone ahora `GL_MIRRORED_REPEAT`/`GL_CLAMP_TO_EDGE`/
`GL_REPEAT` por eje según el TSP. Los diez demos de control, byte a byte idénticos
(ninguno usa Flip ni Clamp). Probable causa también de "la sombra del taxi se calcula
mal" — mismo truco del cuarto espejado — pendiente de confirmar en vivo.

**Y el "borde de los neumáticos sin transparencia" cayó al medirlo (2026-08-02): no era el
punch-through — era el alfa del decal.** La disección con `DCEMU_TRAZA_ESCENA` +
`DCEMU_VOLCAR_TEX` mostró que el costado entero de cada auto del tráfico es UN quad sobre
un atlas ARGB4444 (carrocería, vidrios y ruedas juntos, con anillo de alfa 0 alrededor de
la silueta), en decal (env 2) sobre la lista translúcida con mezcla srcalpha. `GL_DECAL`
deja pasar el alfa del VÉRTICE (1.0) y el anillo salía como color de vértice opaco — el
parche gris pegado a la rueda. El decal es `GL_COMBINE` ahora: RGB interpolado por el alfa
del texel, alfa = textura × vértice. `pvr-bumpmap`, el único demo en ese camino, byte a
byte idéntico. De paso quedaron volcados de referencia decodificados: el farol trasero del
taxi (`5c0ba0`), el follaje (`79c520`) y los dos atlas de autos (`1f2420`, `5f17c0`),
todos VQ+mip impecables — la cadena de decodificación quedó absuelta.

**Dos lecciones de método de esta tanda, para el que retome:** (1) el reproductor NO es
aislable mientras se juega con gamepad en otra instancia — XInput se lee global, sin
importar el foco, así que una captura minimizada recibe el input del jugador y la ventana
05 sale distinta (la 04 y la 11 aguantan, porque la carga termina antes de que el input
pese); (2) los números del discriminador dependen del contenido de `bios/flash.bin`, que
dcemu persiste al salir si el juego lo escribió — hasta ahora no ha cambiado (sigue con
fecha 2004), pero si cambia, la línea de tiempo entera del reproductor se mueve.

**El manto oscuro con volúmenes activos es la simplificación documentada de
`marcar_volumenes()`**: unión de triángulos en vez de conteo de caras. La sombra del taxi es
un volumen cerrado (extruido); marcar cada triángulo enciende todo lo que sus caras cubren —
media pantalla — y la segunda pasada oscurece eso. El arreglo real: plantilla por conteo
(`GL_INCR` caras delanteras, `GL_DECR` traseras, dentro = cuenta > 0), re-marcando entre la
tanda opaca y la translúcida para tener los 8 bits enteros por clase.

**Hecho (2026-08-02): el conteo por caras contra la profundidad quedó implementado.** Los
detalles que el plan de arriba no decía: la cuenta va con `GL_INCR_WRAP`/`GL_DECR_WRAP`
(sin wrap, una cara trasera que rasteriza antes que su delantera clava el 0 y el par no se
cancela), dentro = cuenta **≠ 0** (así el sentido de giro da igual: cerrado cancela de a
pares, abierto queda ±1), y contar contra la profundidad obliga a marcar **después** de
resolverla — `dibujar_escena()` quedó en fases: opaca+PT con juego 0, conteo de la lista 1
y repintado de las afectadas solo dentro (`GL_EQUAL` contra su propia z; sin mezcla es
seguro), re-marca de la lista 3, y la translúcida con sus afectadas partidas fuera/dentro
como antes. Verificado: los seis demos de control (`fb_tex`, `rtt_sized`, `tunnel`,
`libdream-ta`, `conio-basic`, `bumpmap`) **byte a byte idénticos** antes/después con
`DCEMU_RTC_FIJO`; los tres demos de volumen plano idénticos (con el cuadrado delante de
todo, unión y conteo coinciden); `pvr-modifier_volume_zclip` — el único con un volumen 3D
cerrado de verdad, un cubo rotando — pasa del bloque recto en pantalla al oscurecido que
abraza el piso y la pared que atraviesa. El flujo del guest, intacto (discriminador
`11=6481248` idéntico).

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

### A.6 — Virtua Tennis arranca: la entrega por nivel y la demora del CH2 DMA (2 de agosto de 2026)

Retomada la espera 4 con todo lo de A.5 puesto, el mapa cambió: `cola_pop()` ya entregaba su
búfer y el juego quedaba en la **cola del mismo armado** — espera que la operación 0 de su
"clase A" complete: `[8C45F940]` es una máscara de operaciones en vuelo, un handler escribe
la máscara a limpiar en el mailbox `[8C45F94C]`, y nadie lo escribía jamás. Perseguirlo
exigió desarmar el despacho de interrupciones de Katana entero, y lo que salió vale para
todos los juegos del SDK:

- **El ISR de Katana despacha UN bit por entrada** (el más bajo de `ISTNRM & máscara
  blanda`), lo acusa, y retorna confiando en que la línea siga afirmada para re-entrar por
  los demás. La entrega de dcemu sacaba de la cola **todos** los bits cubiertos de una vez:
  el juego atendía el render-done de video y los de ISP/TSP quedaban puestos sin volver a
  interrumpir nunca. **La entrega es por nivel ahora** (`check_ints()`): la petición se
  deriva de `SB_ISTNRM` contra las máscaras, no consume nada, y solo el acuse del guest (o
  enmascarar) la baja — la tercera forma del error que ya tuvieron los temporizadores y el
  descarte sin máscara.
- La tabla del ISR traduce bit→índice (`8c20c960`) con handlers de 16 bytes; el fin de
  lista va a `8c202fa0` con sub-id, que marca una máscara de "hechas" y la compara contra
  la esperada — **0x8001: fin de lista opaca (bit 0) más fin del CH2 DMA (bit 15, del bit
  19 de ISTNRM vía el sub-id 0x12)**. El bit 7 de ISTNRM llegaba (dcemu lo emite al cerrar
  la lista); el 19 estaba puesto desde el arranque y su despacho no corría nunca.
- **El fin del CH2 DMA salía instantáneo** (`intc_add(..., 0)` dentro de la escritura de
  `SB_C2DST`): la interrupción le ganaba la carrera al driver, que todavía no había vuelto
  del disparo ni dejado su estado en "transferencia en vuelo", así que la descartaba por
  espuria — y el fin que esperaba ya había pasado. La misma familia que la demora del
  render y los fines de lista de A.4, para el DMA: **demora proporcional al tamaño**
  (`largo/200 + 10` cuentas de 50 ciclos) en `ch2_dma_ejecutar()`.

Con las dos cosas, **Virtua Tennis (USA) arranca**: el pipeline de cuadro gira (1150
escenas en los primeros 10 s), sube 2 MB de texturas por la ventana 11, muestra su aviso de
VMU con el texto perfecto, y con dos `DCEMU_PULSAR_START` (900 y 1650) pasa por su pantalla
de título hasta el **Main Menu** completo — ARCADE / EXHIBITION / WORLD CIRCUIT / OPTIONS,
texturado, a 147 tiras por escena. El mismo punto del flujo donde Crazy Taxi alcanzó el
hito D.

De la cacería quedaron dos lecciones de método y una herramienta:

- **La compuerta de la entrega va en el bloque de 50 ciclos** (`intc_asic_pendiente()`):
  con nivel, "hay algo pendiente" es casi siempre verdadero, y llamar a `check_ints()` por
  instrucción costó 9× en Crazy Taxi. En el mismo compás que `intc_revisar_sh4()`, el costo
  desaparece y el tic de las demoras pasa a valer lo que dice.
- **Un `getenv()` en un camino caliente cuesta 5×**, y el centinela que lo cachea tiene que
  distinguir "sin resolver" (−1) de "resuelto y apagado" (−2): el guard `< 0` los confundía
  y el diagnóstico nuevo re-consultaba el entorno 2,4 millones de veces por segundo. Medido
  con QueryPerformanceCounter alrededor de `check_ints`: 12,7 s de pared por segundo
  emulado, que eran TODO el costo.
- **`DCEMU_TRAZA_ENTREGAS=1`**: cada entrega del ASIC con su ciclo, sin dedup (tope 600 +
  total por segundo). Es lo que respondió "¿se está re-entregando?" cuando el dedup por
  valor de la traza normal tapaba la cadencia — y lo que mostró las ráfagas de 6 entregas
  sin acuse que identificaron el descarte por espuria.

La regresión, completa: los 7 demos de control byte a byte (los 2 animados driftean un
cuadro y caen exactamente en los números de referencia: fb_tex 93, libdream-ta 65480),
Crazy Taxi con su discriminador exacto (`11=6481248`, 60 s del reproductor, en juego con
1671 tiras/escena) a 0,57x contra 0,53x de la base, mame4all en rango (93689 colores), el
menú del boot ROM byte a byte idéntico, y las 21 suites en verde.

Lo que queda de la vía A tras esto: Capcom vs. SNK (caído el mismo día — ver A.7) y Virtua
Tenis 2 (caído solo, sin un cambio propio — ver A.8).

### A.7 — Capcom vs. SNK arranca: una petición viva por vez en el hook del GD (2 de agosto de 2026)

Compartía el síntoma con Virtua Tennis (2 escenas de 0 tiras) pero no la causa. El anillo de
PC lo mostró clavado en un bucle **del juego** (`8c0112e8`) que sondea el estado de un pedido
de su capa de CRI (ADXF: 3 = READEND, 4 = ERROR) con timeout de 18M iteraciones, mientras su
servidor de gdFs repite para siempre `SEND_COMMAND(34)` — **GETSCD, el subcódigo Q** — que el
hook contestaba "sin implementar". Tres cosas salieron, en orden de descubrimiento:

- **GETSCD (comando 34) está implementado**: parámetros `{formato, tamaño, destino}` como los
  pasa el driver de la BIOS, respuesta con el encabezado del SPI — estado de audio 0x15 ("sin
  información de audio"), y para el formato 1 la Q del track de datos parado. Contestar
  COMPLETED sin escribir el búfer dejaba el estado en 0x00, que no es ningún código.
- **El id de petición ya no es fijo** (`0x6969` para todo): crece de a uno por SEND. Con id
  fijo, dos peticiones consecutivas se confunden en la contabilidad del guest.
- **La de fondo, que era la causa**: el hook aceptaba toda petición al instante, y el driver
  de la BIOS acepta **una por vez**. El log lo delató: el juego manda su lectura larga (29
  sectores desde 237128, el archivo de CRI), y **sin consultarla todavía** manda el GETSCD —
  en la consola ese segundo SEND rebota con 0 y Katana lo reintenta mientras consulta la
  lectura pendiente; en dcemu se aceptaba, el "comando actual" de Katana pasaba al GETSCD y
  la lectura quedaba huérfana: su CHECK_COMMAND no llegaba nunca, y la capa de CRI esperaba
  para siempre el fin de una lectura ya hecha. Ahora `com_viva` modela el driver ocupado:
  SEND con otra viva devuelve 0 sin ejecutar el trabajo, y CHECK_COMMAND la consume y libera.
  De paso CHECK informa lo transferido en el tercer word de estado.

Con eso el juego corre: aviso de Memory Card, y con dos Start la pantalla de título
**CAPCOM VS. SNK Millennium Fight 2000** completa — el logo y la ciudad animada de fondo, a
3035 tiras por escena. Regresión: Crazy Taxi y Virtua Tennis con sus perfiles exactos (386 y
1148 escenas, mismas tiras), mame4all byte a byte idéntico, y las 21 suites en verde.

### A.8 — Virtua Tenis 2 arranca: los arreglos del día más el latch de la lista en curso (2 de agosto de 2026)

Re-medido con A.6 y A.7 puestos, el que era "el único motor distinto" pasó de 0 escenas a
1110 en 10 segundos sin un cambio propio, y las tres direcciones que marcaban su bloqueo
viejo quedaron en nada. La lectura por `0x185d0b24` y la escritura en `0x00000000` eran
síntomas del descarrilamiento de antes, no causas. Los cuatro sondeos de
`0xA1000400`-`0xA1001800` (la Broadband Adapter en el área externa del G2 — VT2 tenía juego
online) resultaron **benignos**: se leen una vez al arranque, `mem_read_error()` contesta
ceros deterministas, la detección del puente GAPS falla limpio y el juego sigue sin red. La
postura de C.8 —no inventar un dispositivo que la consola de serie no tiene— queda validada
por la medición: no hay que mapear el área.

Con eso llegó a su aviso de VMU y al logo del título… y se congeló ahí (el conteo de
escenas clavado en 2350), en **la misma espera de operación clase A** que tuvo Virtua
Tennis — con los arreglos de VT ya puestos: era otra causa. La traza del TA la mostró: el
último cuadro abre su lista translúcida y adentro manda un **encabezado de sprite con el
campo de lista en 0** (PCW `0xA0800000`); dcemu tomaba el campo de cada encabezado como "la
lista en curso", así que el fin de lista siguiente cerró "la 0" (ya cerrada), la translúcida
quedó abierta para siempre, el evento 9 no salió nunca y la máquina de operaciones del juego
esperó sin fin. **En el chip la lista en curso la fija el primer parámetro global tras
`TA_LIST_INIT` o tras un fin de lista, y el campo de los encabezados siguientes se ignora
hasta el próximo fin** (§3.7.4.1). `taPolyModifier()` hace eso ahora: latchea solo con la
lista cerrada, la tira aterriza en la lista abierta, y un encabezado discrepante deja una
línea de traza (una por corrida). Ningún demo ni los otros tres juegos mandan el campo
incoherente — por eso nunca se vio.

Con el latch el juego sigue de largo: pasa el logo y entra a su secuencia de intro. **Con
esto, las cuatro imágenes comerciales que se parsean corren.**

### A.9 — DCDoom carga y resulta ser Windows CE: el kernel arranca, el arranque se queda a medias (2 de agosto de 2026)

El "no carga por este camino" de A.0 tenía una causa de una línea y un mundo entero
detrás. La causa: el cargador directo buscaba `1st_read.bin` **con el nombre cableado**, y
el directorio raíz de DCDoom no tiene tal archivo — tiene `0WINCEOS.BIN`, `DCDOOM.EXE`,
`DOOM.WAD`, `AUTORUN.REG` y un directorio `WINCE`. **DCDoom es un juego de Windows CE.**
El nombre del binario de arranque lo declara el IP.BIN en su cabecera (offset `0x60`, 16
bytes rellenos con espacios) y es el mismo campo que usa el boot ROM — el puntero que deja
en `GBR+0x9C`; la tabla de `bios-boot-plan.md` ya decía que el ROM "encuentra
1ST_READ.BIN" en FAD 11895, que es exactamente donde vive `0WINCEOS.BIN`. `main.c` toma el
nombre de ahí ahora, con `1st_read.bin` de valor por omisión.

Con eso el binario carga, se descifra y corre — y pide todo lo que ningún juego de Katana
pidió jamás. **Lo que se le dio, en orden de aparición:**

- **La fase 7 de la MMU** (la búsqueda de instrucciones por la TLB) y **el avance de
  `MMUCR.URC`** — ver `mmu-plan.md`, que documenta ambos. CE ejecuta sus procesos de
  usuario en P0 traducido, recarga la TLB por software con el manejador en `VBR+0x400`, y
  su manejador cuenta con que cada `LDTLB` caiga en una entrada distinta.
- **La zona `0xBF`** en `mem_hash_read/write`: el área 7 física (`0x1F000000`) vista por
  P2. El HAL de CE arranca el tick del sistema escribiendo `TSTR` en `0xBFD80004`; dcemu
  solo tenía enlazadas `0x1F` y `0xFF`, la escritura caía al vacío sin aviso y el
  planificador quedaba ocioso para siempre. La forma de siempre: algo que el guest pide,
  contestado sin querer decirlo.
- **El disparo por hardware del Maple** (`maple_vblank()` en mem.c): con `SB_MDTSEL=1` y
  `SB_MDEN=1` el chip camina la lista de comandos solo, en cada vblank. CE no escribe
  `SB_MDST` jamás — arma el disparo y espera los fines de DMA. Se reusa el caso de
  `SB_MDST` de `pvr_write()` escribiendo el registro como lo haría el guest, así el
  recorrido es idéntico por los dos caminos. El boot ROM y los juegos de Katana usan el
  arranque por software — por eso nunca se vio.

**Dónde queda DCDoom con todo eso** (medido con `--traza-mem` y el histograma de
excepciones por segundo): el kernel de CE arranca entero — MMU encendida, recargas de TLB
a miles por segundo, syscalls por salto a `0xFFFFFxxx` (error de dirección en la búsqueda,
que la fase 7 levanta y CE atrapa: así implementa CE sus llamadas al sistema), contexto
perezoso de FPU por `SR.FD` con los códigos 0x800/0x820, tick del TMU cada 25 ms acusado
por su ISR, el mando enumerado por el sondeo automático del Maple, el ARM del AICA
corriendo el firmware que CE le sube (63 M de instrucciones, cero indefinidas) y el driver
de pantalla pintando su primer cuadro por la ventana de 32 bits. Del segundo 2 en
adelante el sistema queda **ocioso en el despachador del kernel**: todos los hilos
esperan, y la lectora no recibe ni un comando ATA en 25 segundos — `DCDOOM.EXE` nunca se
llega a cargar del disco.

**La frontera está mapeada al hilo y al ciclo.** El bloqueo de fondo: a los 118.8 ms el
loader de CE construye el mapeo de la sección de código de `maple.dll` (PTE `8cfcb42c` en
la tabla del proceso, PC `8c02447e`); a los 119.4 ms **la misma corrida lo desmonta** — la
rutina de teardown de secciones (`8c02587c`) barre la tabla entera, patrón de creación de
proceso deshecha —; a los 133.4 ms un hilo de coredll salta a `01df17c4` (maple.dll+0x7c4,
su punto de entrada: la sección va de `03df1000` a `03df5d73` según la o32 del XIP, sin
comprimir, con los bytes en RAM en `8c0ac000`), la búsqueda falla en la TLB, el recorrido
por software del propio CE confirma PTE=0, y la paginación por demanda **nunca lo
repone** (el PTE sigue en 0 a los 20 segundos, medido con watchpoint). El hilo sobrevive
—reaparece a los 146 ms en un bucle `ResetEvent` + `WaitForMultipleObjects(1, evt, 0,
5000)` que reintenta para siempre— y el resto del arranque queda serializado detrás:
`wsegacd.dll` (el driver de CD) registró su interrupción (IML4 bit 14) y no llegó a tocar
el drive.

Lo descartado con medición, para no volver a recorrerlo: el ack de `SB_ISTNRM` nunca llega
del guest porque su ISR real sí corre (el enmascaramiento de IML4 `0x7000→0x4000` es el
patrón normal de CE: la fuente se re-habilita en `InterruptDone`, que nunca ocurre porque
el IST de maple queda detrás del mismo bloqueo); el evento que ese hilo espera no está en
la tabla de eventos por interrupción (`8c1119a0`: [10]/[11] maple, [12] GD-DMA — espera a
otro hilo, no a una interrupción); CE no usa las store queues con la MMU activa (la fase 6
no es esto); y el firmware ARM que CE sube corre sano. La imagen XIP se parseó entera
desde la RAM emulada (ROMHDR en `8c10c478`, 19 módulos listados en la sección A.9 del
histórico de esta corrida): `nk.exe`, `coredll.dll`, `filesys.exe`, `gwes.exe`,
`wsegacd.dll`, `maple.dll`, `sndcore.dll`, `dinputx.dll`, etc.

**El siguiente paso concreto**: entender por qué el teardown de los 119.4 ms deja un hilo
vivo apuntando a una sección desmontada — si la creación del proceso falló por algo que
dcemu contesta mal en esos 0.55 ms, o si el desmontaje es normal y lo roto es que la
paginación posterior no repone la sección. La corrida es determinista: los ciclos de arriba
se repiten exactos, y `--traza-desde` con los PC anotados (`8c02447e` el que mapea,
`8c02587c` el que desmonta, `8c01bc68` el despachador de kcalls/excepciones) retoma donde
esto quedó.

#### Resuelto el mismo día: el init de maple.dll llamaba al ROM por su dirección fija

Seguido el hilo hasta el final, el "teardown" resultó ser **FreeLibrary tras un init
fallido** — la rutina de `8c02587c` no desmonta una sección: libera los bloques de 64 KB
de un DLL cuyo contador dice que solo los usa este proceso, con la etiqueta del bloque en
la media palabra +6 de cada tabla. El hilo zombi era el **worker que el propio DllMain de
maple.dll creó** (`CreateThread` con threadproc `01df17c4` = maple.dll+0x7c4) antes de
fallar: el DLL se descargó, el hilo arrancó a los 133.4 ms sobre código ya desmapeado.

**Y el init fallaba por esto** (la traza lo muestra ejecutándose): al final de su
secuencia, maple.dll hace `JSR` a **`8C0010F0` — una constante en su literal pool — con
(r4,r5,r6,r7) = (0,0,0,3)**: la convención de syscall de Sega, función 3 = `GDROM_INIT`.
`8C0010F0` es la **entrada fija del servicio del GD-ROM que el boot ROM instala en RAM**
— un despachador de 16 funciones con tabla auto-relativa en `8C001180`, verificado
desensamblándolo con `--bios` — y el word que el ROM real deja en **`8C0000C0`** (un
quinto vector que los hooks nunca poblaron) apunta exactamente ahí. La plataforma CE de
Sega la llama por la dirección, sin leer el vector. En el camino de hooks esa RAM estaba
en cero: el guest ejecutaba NOIMP tras NOIMP deslizándose ~28 KB hasta caer en los bytes
del IP.BIN, algo con forma de RTS lo devolvía con basura en R0, y maple.dll daba su init
por fallido. La forma de siempre, en su variante más pura: una llamada que dcemu recibía
sin tener nada instalado donde el guest sabe que la consola real tiene código.

Tres arreglos salieron de ahí, y con los tres **la pila completa de CD de Windows CE
funciona por los hooks**:

- **El stub del GD-ROM se instala también en `8C0010F0`**, y el word de `8C0000C0` queda
  apuntándolo (`SYSCALL_GDROM_FIJO` / `HACK_GDROM_FIJO` en mem.h). Mismo `hack_gdrom()`
  por los dos caminos, como en el chip.
- **`GDROM_INIT` (función 3) contesta `R0 = 0`** — dejaba R0 con lo que hubiera, y el
  init de CE es quien lo mira.
- **El destino de la lectura por DMA (función 17) es una dirección física** — el G1 DMA
  de la consola no pasa por la MMU — y el hook la escribía con `memwrite`, que con la MMU
  de CE encendida la traducía como virtual de otra ranura, sin mapear. Ahora la 17 escribe
  con `memwrite_fisico` y la 16 (PIO, escritura por CPU del driver) sigue traducida:
  el espejo exacto del hardware. Katana pasa P1 y no distingue los dos caminos.

Con eso, medido con el censo de syscalls (`DCEMU_TRAZA_EXC=2`): CE monta el ISO9660 del
disco por los syscalls del GD (PVD, directorio raíz, `DCDOOM.EXE` entero — 645 sectores
de una vez — y módulos de `\WINCE` paginados **bajo demanda desde el CD** en trozos de
16 KB), maple.dll inicializa completo (su IST re-habilita las máscaras: IML4 vuelve a
`0x7000` con `InterruptDone`), el sondeo automático del mando corre a 60 Hz con
`GetCondition`, y wdmlib/ddraw ciclan vivos en régimen estacionario.

**La frontera nueva** (donde quedó el 2 de agosto por la noche): el sistema entra a un
régimen de 284 syscalls/s — el latido del anfitrión de drivers (`8cf8c000`: maple, wdmlib
y el bucle ocioso de ddraw a 6 Hz) — y ahí se queda. `DCDOOM.EXE` cargó y su proceso
duerme sin hacer una sola llamada más; `DOOM.WAD` se tocó a lo sumo una vez; y un segundo
proceso (`8cfa5000`) repite cada 5 segundos el patrón WaitForAPIReady — `ResetEvent` +
`WaitForMultipleObjects(1, evt, 0, 5000)` desde `PR=0001aa54` — esperando un API set que
nunca se registra. Nadie desenmascara jamás el vblank (bits 3/4/5 de las IML): el ddraw
de CE-DC no lo usa, su bucle es idle normal. Qué API set espera `8cfa5000` y por qué su
servidor no arranca es la pregunta que sigue; el censo por (destino, llamador, proceso)
del `DCEMU_TRAZA_EXC=2` es la herramienta con la que se retoma.

**Afinado la misma noche, hasta ver morir al juego.** Lo de "su proceso duerme" era
mejor de lo que parecía y peor a la vez: el proceso del juego **existió, corrió mucho, y
salió solo**. La secuencia, medida con el censo y con trazas armadas desde los ganchos:

- El proceso del juego corre en el **slot 5** (pilas `0A02xxxx`). En el segundo 0 hace
  miles de llamadas: su DLL de audio importado se termina de paginar desde el CD, corre
  el DllMain de ese DLL (la excepción 0x800 — primer uso de FPU con `SR.FD` — desde
  `01d24bb2`, un bloque de DLL cargado del disco, no del XIP), y **escanea una tabla
  propia de 256 entradas de 4 bytes buscando medias palabras distintas de `0xFFFF` — y
  está toda vacía** (256 = 128+128: forma de banco de parches MIDI; el módulo 18 del XIP
  se llama `default.fdf`). El escaneo termina, el init **sigue** — la tabla vacía no es
  fatal por sí sola.
- Poco después el proceso ejecuta una secuencia lineal de ~12 syscalls de coredll
  (`01e690cc`..`01e69188`) que cierra asas y termina: **ExitProcess voluntario, sin
  excepción fatal**. Para el segundo 1 el juego ya no existe (la tabla de secciones en
  `8c1118a0` muestra solo nk, `8cfb9000`, `8cfa5000` y GWES; los slots 5+ apuntan al
  centinela `8c012b0c`).
- El que reintenta cada 5 s (`8cfa5000`, slot 3) es **quien lo lanzó** — y es el mismo
  proceso cuyo hilo cargó maple.dll al principio: el lanzador de la plataforma. Su
  espera de 5 s encaja con "esperar la señal del lanzado" (el patrón SignalStarted de
  CE): el juego murió sin señalar y el lanzador espera para siempre.
- De paso: maple.dll resultó ser **la capa entera de servicios Sega de CE** — todos los
  PC que emiten los syscalls del GD (`01df29xx`, `01df2dxx`) viven en ella, además del
  maple propiamente dicho.

**El siguiente paso concreto**: la decisión de salida. Los últimos sitios de syscall del
juego antes del desenlace están censados (consultas a la familia de registro
`ffffd5xx`, una llamada al apiset 5 método 2 desde `01e23452`, `fffffd97`/`0001136c`) y
la corrida es determinista: armar `traza_arrancar` sobre el primero de la secuencia de
salida (`fffffd51`, PR=`01e690cc`) con una ventana hacia atrás — o sobre los candidatos
de decisión uno a uno — nombra el chequeo exacto que devuelve el error. Sospechosos por
forma: algo que el juego consulta del registro o del entorno (¿configuración de video?
¿idioma? ¿un dispositivo de la tabla `0xFFFF`?) y que en dcemu contesta distinto que la
consola. El histograma y el censo (`DCEMU_TRAZA_EXC`) más los ganchos temporales de esta
sesión (documentados aquí) son el camino de vuelta.

#### La decisión de salida resuelta: era el lanzador, y detrás la lectura en flujo que dcemu no implementaba (2 de agosto, tercera sesión)

Los ganchos temporales son permanentes ahora — **`DCEMU_TRAZA_EXC=3`** vuelca el flujo
completo de syscalls (uno por línea con ms, destino, PR y proceso: lo que el censo
resume, y que del segundo 0 no veía nada) y **`DCEMU_TRAZA_SYSCALL=dest[:pr[:N[:K]]]`**
arma la traza de instrucciones en la K-ésima aparición de un syscall (hex:hex:dec:dec;
es `DCEMU_TRAZA_ATA` para las trampas de CE) — y con ellos la "decisión de salida" se
leyó entera. No era del juego:

- **El proceso que salía era un stub de autorun** (el que el censo llamaba "el juego"):
  su rutina final hace GetTickCount, `RegOpenKeyExW(HKLM, "init")`,
  `RegQueryValueExW(.., "Autorun")` — ambas devuelven 0, éxito; el dato es
  **`"dcdoom.exe"`, importado del AUTORUN.REG del disco** — y le pasa ese nombre a
  `CreateProcessW`. Todo leído de la traza: la clave y el valor carácter por carácter en
  el servidor de registro, el dato en el memcpy de vuelta.
- **`CreateProcessW("dcdoom.exe") devolvía 0**: el stub hace `TST R0` y salta al mismo
  epílogo de error que usa para el registro; su ExitProcess es el fin normal de un
  lanzador. El hijo real (`8cf5f000`, pilas de slot 1) nacía, el loader le cargaba todo,
  y a los 188 ms el attach de un DLL ejecutaba basura y el kernel deshacía el proceso
  entero.
- **La basura tenía firma**: la página del entry de `WINCE/CEMM.DLL` (el DLL multimedia,
  entry RVA `0x4b00` → `01d24b00`, reubicado desde base `0x10000000`) era ceros con
  `F1D2` — `FMUL FR13,FR1` — exactamente en las medias palabras altas de los 25 slots
  HIGHLOW de su `.reloc` en ese rango, uno por uno: **la página quedó en cero y el loader
  aplicó las reubicaciones encima** (0 + delta `0xF1D20000`). La excepción FPU 0x800 que
  la sesión anterior tomó por "primer uso de FPU" era el primer word de basura con pinta
  de instrucción, con `SR.FD` puesto. La imagen física entera (`0x0CCF9000+`) estaba así.
- **Los datos nunca llegaban porque viajan por un protocolo que el hook no hablaba**: el
  loader de CE (wsegacd/maple.dll) carga las secciones grandes con **`MULTI_DMAREAD`
  (comando 38)** — `{sector, cuenta, adelanto}`, sin destino: la petición queda en
  **CONTINUE (3, el "STREAMING" del comentario del propio hook)** y el guest tira de a
  pedazos con **`REQ_DMA_TRANS` (función 7=6 del vector)** `{destino, tamaño}`, consulta
  lo que queda con **`CHECK_DMA_TRANS` (r7=7)** y registra un callback con **`G1_DMA_END`
  (r7=5)**. dcemu contestaba el 38 con un id válido y COMPLETED sin mover un byte — el
  `default:` mudo de siempre — y las funciones 5-8 caían a otro `default` mudo. La
  numeración y la semántica están verificadas contra el HLE de flycast
  (`core/reios/gdrom_hle.*`, que corre esta misma plataforma): 36=`REQ_STAT` (el sondeo
  de estado con cuatro punteros que CE repite en régimen), 38=`MULTI_DMAREAD`,
  40=`GET_VERSION` — consistente con el caso 40 que el hook ya tenía.
- **Y el segundo bug lo tapaba el primero**: implementado el flujo, todo el sistema se
  paraba a los 173 ms. La bomba de wsegacd (SEND/MAINLOOP/CHECK) consulta una vez, ve
  CONTINUE, y **duerme hasta el fin de comando — la interrupción externa del GD
  (`SB_ISTEXT` bit 0, que CE habilita en `SB_IML6EXT`)** para volver a consultar y ver
  el COMPLETED. `check_ints()` **descartaba en silencio los eventos externos que no podía
  entregar en el momento** (`SR.BL`, IMASK, máscara apagada): el mismo descarte que ya se
  curó en los timers y en el registro normal, vivo en la cola externa. El evento queda en
  cola ahora y se reintenta; el drenaje del flujo levanta `ASIC_EVT_EXT_GDROM`.

Con las dos cosas, medido con el censo y el histograma: **CreateProcess funciona, DCDoom
vive 2.3 segundos** — cuatro flujos servidos (el `.text` y el `.data` del EXE, CEMM.DLL
entero, y DSOUND.DLL, que antes ni se llegaba a cargar), nueve DLLs con sus attach, uso
de FPU sostenido (6/s) y una ráfaga de paginación en el segundo 2 — y a los 2440 ms hace
su ExitProcess voluntario, ahora sí el suyo: la ráfaga de `fffffe01` desde `8c0149ea`
son los detach de todos sus módulos, y la cadena cierra en `01e69188` como siempre.
**DOOM.WAD no se tocó jamás** (ni un sector de 13227..19285), así que la decisión es
anterior a los datos del juego. Lo último que su propio código hace: **seis llamadas al
apiset 17 método 42 desde `0x39c98`** (código del EXE), con toda la pinta del init de
video/DirectDraw de la plataforma.

**El siguiente paso concreto**: nombrar el apiset 17 y leer qué devuelve el método 42 —
`DCEMU_TRAZA_SYSCALL=ffffdbad:39c98:N` cae exactamente ahí, y el retorno al EXE muestra
el chequeo. El sospechoso por forma es el ddraw/GAPI de la plataforma contra el
framebuffer o el vblank de dcemu (el ddraw del anfitrión de drivers ocioso a 6 Hz es
normal, pero el juego pide superficies). La regresión guardiana: los cuatro juegos de
Katana pasan por el mismo `hack_gdrom()` — Capcom vs. SNK es el sensible a la semántica
del CHECK (A.7) — y la suite entera más esa corrida están verdes con todo lo de arriba.

#### El apiset 17 nombrado y cinco capas más abajo: DCDoom vive, carga su WAD y su ddraw somete al TA (2 de agosto, cuarta sesión)

El "apiset 17 método 42" resultó ser **DefWindowProcW** — y las seis llamadas, la rutina
de creación de su ventana. Lo que las nombró es la herramienta nueva de la sesión y vale
para todo lo que venga: **la imagen XIP de `0WINCEOS.BIN` se parsea entera desde el
archivo** (`xip.py` y `nombres.py` en el scratchpad; ROMHDR con firma `ECEC` en imagen
+0x40, 19 módulos con sus `o32_realaddr`), la tabla de exports de coredll.dll (2100
funciones) da el nombre del API que contiene cualquier PR de `01e6xxxx`, y el trampolín de
cada export — `MOV.W` de un literal `ffffxxxx` + `JMP` — da la tabla **destino de trampa →
nombre**: 380 syscalls de CE decodificados de una vez. El mapa de módulos en memoria:
coredll `01e61000`, wdmlib `01e11000`, wdmoem `01e01000`, wsegacd `01df1000`, maple
`01de1000`, ddraw `01dc1000`, **ddhal `01d81000`** (su `.data` en `01da5000`), sndcore
`01d71000`; CEMM (del CD) en `01d24000`.

Con nombres, la muerte de 2440 ms se leyó entera y era una cascada de **cinco causas, cada
una tapando la siguiente**:

1. **El volcado de una store queue con la MMU activa enmascaraba antes de traducir.**
   `pref142()` aplicaba siempre la fórmula de QACR y el `memwrite` traducía esa dirección
   ya mutilada: el manejador de recarga de CE recibía un fallo por una VPN de la ranura 1
   que sus tablas no mapean, confirmaba PTE 0, y el blit de `ddhal.dll` — que usa las SQ
   para TODO: píxeles y geometría — moría por violación de acceso `c0000005` en su primer
   `PREF` (VA `e3000200`). El `__except` de nivel superior de coredll (la zona tras
   `IsProcessDying`: `GetProcName` + `LoadStringW` ×2 + WMGR.50) mostraba el cartel fatal
   y ExitProcess. La excepción FPU 0x800 que el plan anterior apuntaba era inocente: el
   primer uso de FPU del hilo (un `FDIV` legítimo de ddhal), manejado limpio por el
   contexto perezoso. **Arreglo**: `mmu_traducir_sq()` (mmu.c) — la UTLB traduce la VA
   completa del `PREF`, con `MMUCR.SQMD`; ver `mmu-plan.md`, fase 6, con el detalle de la
   rama `8c012540` del manejador de CE y la plantilla de `SetStoreQueueBase`.
2. **`FB_R_SOF1` se leía como la constante `0x00100203`, cableada desde 2005.** El flip de
   ddraw lee el registro y decide con eso; la constante envenenaba el ciclo — reescribía
   `0x00100203` cuadro tras cuadro y el display nunca apuntó a una superficie del juego.
   El DevBox lo lista **RW** (bits 23-2 la dirección en unidades de 32 bits, bits 1-0 en
   00 — la constante hasta violaba el formato). Con la lectura real, ddhal alterna
   `00107280`/`00507280`: doble buffer entre los dos bancos, un flip por vblank.
3. **DOOM.WAD viaja por el flujo PIO, que faltaba entero**: `MULTI_PIOREAD` (39) +
   `SET_PIO_CALLBACK`/`REQ_PIO_TRANS`/`CHECK_PIO_TRANS` (r7=11/12/13, numeración
   verificada contra el `gdrom_hle.h` de flycast). La semántica de fondo es el **CD_READ2
   (31h)** del protocolo SPI (`docs/cdif131e.pdf`, 8.2): el "adelanto" es su Next Address,
   la posición de pre-lectura cuyo error no se informa en este comando. Tres sutilezas que
   costaron una corrida cada una: el pedazo se **latchea** en el r7=12 y la copia con el
   aviso van en el **MAINLOOP siguiente** — que la bomba de wsegacd llama desde su propio
   hilo, único contexto donde la VA del argumento del callback significa lo que debe
   (avisar en el MAINLOOP de otro proceso mató al kernel); el aviso es **llamar al
   callback** como lo hace el `gdGdcExecServer` real — PC al callback, R4 su argumento, PR
   intacto — y para poder desviar ese retorno **el stub del GD ya no es RTS + ranura**: el
   ilegal va en el offset 0 y `hack_gdrom()` fija el PC él mismo (main.c; la prueba
   `el_hack_de_la_bios_lee_sectores` documenta el contrato nuevo); y el `GET_CMD_STAT` con
   un pedazo latcheado contesta **PROCESSING (1)**, no CONTINUE — con 3 la bomba se duerme
   a esperar un callback que necesita el MAINLOOP que ella misma dejó de llamar (flycast:
   "Bust-a-move 4 likes this").
4. **La escritura traducida de más de una página rociaba físico contiguo.** `memwrite()`
   traduce UNA vez por llamada; la pieza de 36 KB del flujo, escrita a una pila de usuario,
   cruzó nueve páginas virtuales no contiguas en físico y pisó **el directorio de páginas
   del propio proceso** con datos del WAD — la entrada que el kernel leyó después decía
   `"SW17"`, un nombre de lump de DOOM — y CE moría con `"Halting system"` por doble
   excepción (el contador de anidamiento en KData+0x85, el `DT` de `8c0122dc`).
   **Arreglo**: `memwrite_paginado()` en dcopcodes.c — pedazos que no cruzan páginas de
   1 KB, la mínima del SH-4 — usado por el flujo PIO y por la lectura 16.
5. **RENDERDONE salía de `TA_LIST_INIT` y no de STARTRENDER.** En el chip los bits 0-2 de
   `SB_ISTNRM` son la consecuencia del strobe; dcemu los levantaba al iniciar la lista —
   KOS no distingue (espera tras su STARTRENDER y lo recibe igual; el dibujo GL sigue
   saliendo del próximo LIST_INIT), pero el ddraw de CE recibía un "render terminado" que
   nunca pidió, antes de su primer vértice. Movido a `cb_renderstart()`.

#### El Sort-DMA: el TA gira, DCDoom rinde cuadros, y ahora sale por la puerta de adelante

La espera del estado 7 era **el Sort-DMA, que dcemu no implementaba**. El ddraw de CE no
alimenta al TA por las store queues —eso es solo su blit de píxeles— sino por la vía de
cadenas enlazadas que documenta el DevBox en 2.6.5.3 y las figuras 2-10 a 2-13: el guest
arma la lista de despliegue en RAM (una tabla de Start Link Addresses de 16 o 32 bits
según `SB_SDWLT`, y dentro de cada Global Parameter el séptimo word con el tamaño en
unidades de 32 bytes y el octavo con el próximo enlace, más los códigos 1 = fin de lista y
2 = fin del DMA), y escribe 1 en `SB_SDST`. Los seis registros del bloque
(`0x005F6810`-`0x6820` más `SB_SDDIV`) caían en `control_mem` sin lector: la forma de
siempre, y la que el diagnóstico nuevo —"registro leído sin caso propio"— saca de una
pasada. `sort_dma_ejecutar()` en mem.c recorre los enlaces y entrega a
`ta_procesar_bloque()`.

Con eso el pipeline gira entero: dos Sort-DMA por cuadro (uno de 640 bytes y 5 entradas de
tabla, otro de 320 y 3), las **cinco listas cierran** (`hechas=1f`), STARTRENDER dispara,
y **el "Timeout for Tile Accelerator" desaparece por completo** (de 13/s a cero). DCDoom
rinde 42 cuadros.

**Y a los 9.4 s sale por la puerta de adelante**: su WinMain **retorna 0** y el epílogo del
CRT hace lo que corresponde —recorre su tabla de atexit liberando (`free`), llama dos
rutinas de platutil.dll y termina en `TerminateProcess(handle, 0)` desde `0x3bf5c`—. No es
una excepción ni un error: el juego decidió terminar. Antes de eso corre su bucle de
mensajes (`PeekMessageW`), carga recursos (`FindResource`/`LoadResource`/`SizeofResource`,
todos con éxito) y consulta `GetTickCount` en caliente. **Por qué su main loop se acaba es
la pregunta nueva** y es la primera vez que la frontera está dentro de la lógica del juego
y no en algo que dcemu contesta mal. Para retomar: la cadena de retorno es
`0x39676` (fin del main) → `0x3be9a` → `0x3bef0` (exit) → `0x3bf5c`; hay que subir desde
`0x395xx` hacia el bucle. `DCEMU_TRAZA_SYSCALL=ffffdb3d:395d0:400000` cubre desde el
`PeekMessageW` hasta la salida en una sola traza.

#### El guest cuenta su propio arranque: DOOM entero, y lo que faltaba dicho en una línea

**Windows CE tira su salida de depuración.** Su OAL no escribe ni al SCI ni al SCIF
(medido con watchpoint sobre los dos TDR), así que los cientos de líneas en las que el
guest narra lo que hace no salían por ningún lado. **`DCEMU_TRAZA_DEPURACION=1` las
imprime**: intercepta `OutputDebugStringW` y `NKvDbgPrintfW` (apiset 0, métodos 14 y 23 —
numeración de CE, no de esta imagen) al entrar a la excepción, **antes** de que el cambio
de banco se lleve el R4 del guest, y lee la cadena con `mmu_traducir_mirar()`, una
traducción nueva que mira sin fallar: no entra a ninguna excepción y no mueve URC, porque
un `longjmp` desde adentro de `excepcion_entrar()` dejaría al emulador a medio entrar.

Con eso DCDoom dijo en una línea lo que costaba días:

```
Error:
W_ReadLump: only read 0 of 17544 on lump 1968
Exiting windoom...
```

**La causa**: el destino de un pedazo del flujo PIO es una dirección virtual de otra
ranura de CE (el buffer del llamador, en la ranura 6; el de rebote del propio driver, en
la 0) y su página puede no estar en la TLB. La escritura fallaba a mitad de la copia,
abortaba la instrucción, y el guest reejecutaba el stub con la bomba del driver ya a
mitad de camino — los bytes llegaban a su buffer y el conteo volvía en cero. Ahora se
**toca el destino antes de mover nada**: el fallo ocurre con el estado del flujo intacto,
CE recarga la TLB, reejecuta el stub y la copia entera pasa de una. Dos correcciones más
del mismo protocolo, las dos como el HLE de flycast: `CHECK_COMMAND` escribe sus cuatro
words **siempre** (el driver hace una consulta final ya sin petición viva, y de ahí saca
el conteo que devuelve a su llamador), y `CHECK_DMA_TRANS`/`CHECK_PIO_TRANS` no matan el
flujo al vaciarlo — eso lo hace el COMPLETED.

Dos medidas de esta sesión que parecían resolverlo y no: escribir el destino del PIO como
**físico** hace que el conteo salga bien (no falla, luego no aborta) pero manda los datos
a otra página, y el juego muere después con `W_GetNumForName: PNAMES not found!`; y
partir la regla por rango de dirección tampoco, porque **las dos son virtuales** — 0x00160000
es la ranura 0 y 0x0c2d6afc la 6, no RAM física. La regla correcta es traducir siempre,
y lo que había que arreglar era el momento del fallo.

**DOOM arranca entero ahora** y lo dice él mismo: `W_Init` con `\CD-ROM\doom.wad`,
"Ultimate Doom WAD - fourth episode enabled", la pantalla de startup v1.9, `M_Init`,
`R_Init` (las texturas, que es donde moría), `I_Init`, `D_CheckNetGame` ("player 1 of 1"),
`S_Init`, `HU_Init`, `ST_Init`, `CO_Init` — pero **el `W_ReadLump: only read 0 of 17544`
seguía ahí**, en medio del log y no al final, y eso fue una lectura mía apurada: la cola
del log terminaba en `CO_Init` y leí "arranca entero" donde el error estaba veinte líneas
más arriba. DOOM no muere en él —su `I_Error` imprime, corre el `atexit` de DirectDraw y
la ejecución sigue—, así que el resto del arranque salía igual, de un juego ya zombi. La
lección de medición: **en un log largo, buscar el error, no leer el final**.

#### El COMPLETED se cobra una sola vez, y al final del flujo hay dos consultas: DCDoom se ve (2 de agosto, sexta sesión)

`gdGdcGetCmdStat` entrega el COMPLETED **una sola vez**. Está medido, no supuesto:
`--bios --desensamblar=8c003072` saca de la RAM el `gdGdcGetCmdStat` del ROM real, y su
rama de "estado 3" escribe el conteo transferido en `status[2]`, devuelve 2 y **pone el
estado del bloque en cero**; la consulta siguiente encuentra el id igual pero el estado en
cero, contesta 0 (NO_ACTIVE) y deja las cuatro words en cero, porque las limpia antes de
mirar nada. También se aprendió ahí que un id que no es el de la petición en curso se
contesta con −1 y `status[0] = 5`, no con 0.

Y al final de un flujo PIO hay **dos** consultas: la de la bomba de wsegacd —que ya soltó
su callback y solo quiere el estado— y la del llamador, en otra ranura de CE (buffer
`0809fe5c` contra `0c2df694`), que es quien necesita el conteo. dcemu cobraba el COMPLETED
en la primera, así que el llamador recibía "esa petición no existe" y le devolvía **cero
bytes a `read()`** con los 18432 ya en su buffer. Ahora la consulta que descubre el flujo
vacío contesta CONTINUE y marca; la siguiente cobra el COMPLETED con el conteo. Es lo que
hace el HLE de flycast, cuyo comentario dice "Fixes NBA 2K".

Tres detalles del arreglo, cada uno medido porque el primer intento los erró:

- **Solo el flujo PIO se difiere.** El de DMA termina el comando al transferir su última
  pieza —su aviso es la interrupción de fin de DMA— y diferirlo ahí deja al driver
  esperando un evento ya pasado: Windows CE se quedó sin cargar ninguna DLL.
- **La respuesta del diferimiento es CONTINUE (3), no PROCESSING (1).** Con 1 la bomba
  entiende "sigo trabajando" y gira `MAINLOOP`+`CHECK` para siempre; lo mismo pasa si se
  contesta 1 mientras el flujo todavía tiene datos. La bomba decide que terminó por el
  `CHECK_PIO_TRANS`, no por esto.
- **Por qué solo lo mostró un lump.** El dato siempre llegaba bien; lo que salía mal era el
  código de retorno. Una lectura corta va por el comando 17 (DMA de 8 sectores) y no toca
  el flujo, y el directorio del WAD sí va por el flujo pero `W_AddFile` **no mira el
  conteo**. El lump 1968 —17544 bytes— es la primera lectura que cumple las dos
  condiciones. Por eso "cargaba el WAD" y moría igual.

**DCDoom se ve.** Corre la demo de atracción de E1M1: paredes, sprites, el enemigo, la
piscina de nukage, el arma y la barra de estado entera, y el log del guest cierra con
`-playdemo: demo1`. 868 escenas en 32 s de tiempo emulado (frente a 42), la captura con
5661 colores distintos y ningún píxel negro. Un detalle que parece un error y no lo es: una
captura salió entera rojiza — es el destello de paleta de DOOM al recoger un ítem
("PICKED UP THE ARMOR"), y la siguiente sale con los grises y marrones correctos.

**Y es jugable.** Medido en la misma corrida: `DCEMU_PULSAR_START=1500` abre el menú
principal de DOOM sobre la demo —o sea que la cadena entera de entrada llega: XInput o
teclado, `entrada_leer()`, el Maple disparado por hardware en cada vblank, el driver de CE
y el juego—, y con `DCEMU_PULSAR_A=1` más `DCEMU_SOLO_A=1` (una pulsación cada 200 sondeos)
se recorren "New Game" → "Which Episode?" → dificultad y **empieza E1M1 con 100% de salud**.
El sonido también sale: `--captura-audio` da 2,16 millones de muestras no nulas de 2,9,
silencio hasta el segundo 7 (CE arrancando), un tramo fuerte entre el 8 y el 12 y actividad
desde el 21, con 1785 muestras al riel (0,06%). El ritmo es de ~27 escenas por segundo
emulado, que para un renderizador por software a 320×200 subido por DirectDraw es lo
esperable.

Lo que queda son detalles y ninguno bloquea. La corrida reporta nueve registros "leídos sin
caso propio" —`FB_W_SOF1`, `FB_W_LINESTRIDE`, `PT_ALPHA_REF`, `ISP_FEED_CFG`…—, y **eso por
sí solo no es sospechoso**: el respaldo de `control_mem` se escribe arriba de todo, antes
del `switch`, así que devuelven exactamente lo que el guest escribió. El diagnóstico sirve
para el otro caso, el que ya costó tres cuelgues: un registro cuyo valor **no** viene de una
escritura del guest (`REVISION`, `SB_G1SYSM`, `SB_SBREV`). Queda además una única dirección
sin emular en toda la corrida (`01d91b40`, dentro de ddhal, leída una vez por el kernel).

Lo que hizo legible todo esto fue subir de 8 a 24 el tope de las trazas del flujo
(`REQ_PIO_TRANS`, la copia del `MAINLOOP`) —con 8 no entra una sola transacción completa— y
una línea nueva por consulta de flujo con quién pregunta, qué queda y qué se lleva
transferido. `DCEMU_TRAZA_GDFIN=N` desensambla las N instrucciones que siguen a la consulta
final, que es donde el guest cobra el resultado.

**Dónde quedó DCDoom antes de este arreglo**: CE arranca entero, CreateProcess funciona,
el juego abre DOOM.WAD por `CreateFileW`/`ReadFile` — el montaje ISO9660 de CE sirviendo
por los dos flujos —, lee sus lumps (paleta, colormaps, flats: sectores 15024-15033,
17735+, el flujo de 18041), crea su ventana (las seis DefWindowProcW), su IST de vblank
corre a 60 Hz y el proceso vive indefinidamente a ~1030 syscalls/s. **La pantalla sigue
negra y la frontera es la máquina de estados del ddraw HAL**: somete al TA — por las SQ
traducidas — `TA_ALLOC_CTRL=00010113`, el soft reset, bases ISP/OL, `TA_LIST_INIT`, un
encabezado (`pcw=80040008`, lista opaca) y cuatro vértices (`pcw=e0000000`, el quad de
pantalla armado por FPU)... y ahí espera, sin cerrar listas ni escribir STARTRENDER,
reintentando cada ~76 ms con **"Timeout for Tile Accelerator"** (13/s por
`NKvDbgPrintfW`; el formato vive en ddhal+0x29b4, el que sigue es "Timeout on render.
Frame %d"). Su función de render rehúsa arrancar porque `[ctx+0x17e8] = 7` — un estado
del pipeline cuyo manejador (`01d92222`, despachado desde `01d92186` sobre eventos del
IST) espera algo que no llega. Qué evento espera el estado 7 — y si dcemu debe emitir
alguno de los fines de lista de otra forma — es la pregunta abierta. Herramientas para
retomar: la tabla de trampas (`nombres.py trampas`), el flujo anotado
(`... | python nombres.py flujo`), y los desensamblados de ddhal desde la copia XIP
(`staging = 8c0c7000 + (VA - 01d81000)`).

### A.10 — Crazy Taxi bajo --bios: el tipo de disco en el traspaso, y la lectura que no sale (2 de agosto de 2026)

El "--bios con juego se congela" de siempre (554 escenas y quieto) se bisectó y son dos
capas:

**La resuelta: `SECTNUM` cambia de cara en el traspaso del arranque.** El juego reintentaba
`gdGdcGetDrvStat` a 2.4/s para siempre sin emitir un SPI — el chequeo del tipo de disco de
`gdFsInit` (el mismo del hito D), ahora contra el driver del ROM real, que lee el registro
`SECTNUM` de la lectora emulada. La mentira permanente no sirve: la detección del propio
ROM (~82 ms, PC `8c002xxx`) elige la rama MIL-CD con este nibble, y con GD-ROM fijo todo
disco terminaba en `menu(1)` — cuya pantalla animada da capturas "ricas" (~480000 píxeles,
~18500 colores) que ya se confundieron una vez con el attract de un juego: números de
escena idénticos entre dos discos distintos son el menú. La separación es estructural: el
**único ATA NOP de la corrida** — el cierre del bootstrap del IP.BIN, a los 12.4 s, 12 ms
antes de los primeros comandos del juego — marca el traspaso (`gdrom_traspaso` en gdrom.c,
que sobrevive a los reset del drive a propósito: el gdFsInit del juego resetea la lectora).
Honesto antes, GD-ROM después; en el menú sin juego el NOP no llega y el registro queda
honesto. flycast concilia lo mismo por otra vía: su GET_DRV_STAT saca el tipo del IP.BIN
(`ip_meta.isGDROM()`), no de la imagen. Con esto el juego corre su gdFsInit entero — 0x70,
el verificador 0x71 (1024 bytes), `GET_TOC` de las dos áreas —, arranca Maple, configura
el TA (`TA_ALLOC_CTRL=00101313`), sube texturas por CH2 DMA y dibuja su pantalla LOADING
(las 21 tiras).

**La abierta: su primera lectura de datos nunca se emite.** Tras el init, cero comandos al
GD en 77 segundos: el juego encola su lectura por el vector y sondea el estado en su capa
gdFs (el anillo: `0c14xxxx`/`0c15xxxx`) mientras el driver del ROM no la procesa jamás —
ni un `PACKET`. El despachador del vector está mapeado (tabla auto-relativa en `8C001180`:
ReqCmd=`8c002ff4`, GetCmdStat=`8c003072`, ExecServer=`8c001918`, GetDrvStat=`8c003174` —
verificado contra el cuerpo ya trazado) y la corrida es determinista; el siguiente paso es
trazar ReqCmd/ExecServer pasado el segundo 13 y ver dónde muere la petición — el flag de
comando activo (`[base+0xC4]`, visto en 0 en el GetDrvStat trazado) huele a GetCmdStat
contestando NO_ACTIVE a una petición que nunca entró. Ojo con dos trampas de medición que
esta sesión pagó: `--salir-tras` va en DECIMAL (`5a` son 5 segundos), y una sección de
`stderr.txt` con dos corridas mezcladas inventó un "bucle de retardo en el ROM" que no
existe.

### A.11 — La mitad de los cuadros era negra: Katana inicializa el TA dos veces por cuadro (3 de agosto de 2026)

Reportado mirando Capcom vs. SNK en vivo: parpadea. El resumen del TA lo dijo en una línea —
`607 escenas rendidas; tiras de las últimas 12: 190 0 208 0 208 0 208 0 208 0 208 0` —, y el
estado de la máquina de listas dio la forma exacta:

```
[geometría: abre y cierra las listas 0..4]
STARTRENDER: hechas=1f habilitadas=1f
TA_ALLOC_CTRL=00121313
TA_LIST_INIT: hechas=1f  <- presenta la escena buena (208 tiras)
TA_ALLOC_CTRL=00121313
TA_LIST_INIT: hechas=00  <- limpiaba, dibujaba cero tiras y hacía swap: NEGRO
[geometría del cuadro siguiente]
```

**607 STARTRENDER contra 1216 TA_LIST_INIT en 12 segundos**: exactamente dos
inicializaciones por cuadro, y dcemu presentaba en cada una. El juego corre a ~50 cuadros por
segundo emulado y se veían 101, uno de cada dos vacío.

En el chip `TA_LIST_INIT` no dibuja —deja al TA listo para recibir un juego de listas nuevo, y
el que dibuja es `STARTRENDER`—; dcemu la usa como frontera de cuadro, y eso vale mientras haya
una por cuadro, que es lo que hace KOS. Ahora **una inicialización que llega sin nada registrado
desde la anterior no presenta**: inicializar un TA vacío es una operación nula en el chip, así
que lo es aquí. Conserva lo que le toca al TA (el medio parámetro de 64 bytes colgando y el
puntero de escritura del área ISP/TSP) y se salta limpiar, dibujar y presentar.

**El discriminante es `pvr_listdone`, no `strip_count`**, y la diferencia importa: una escena
deliberadamente vacía igual abre y cierra su lista —abrir una y no mandar nada es un error de
hardware, por eso `pvr_list_finish()` de KOS manda siempre un encabezado en blanco—, así que
esa se sigue presentando y se sigue viendo negra, que es lo que el guest pidió. **El boot ROM
manda justamente esas** (sus tiras salen `0 0 0 ... 21 0`, con las listas abiertas y cerradas) y
no cambió nada para él.

**No es de Capcom: es de Katana.** Virtua Tennis y Virtua Tenis 2 tenían lo mismo, y el conteo
lo confirma sin ambigüedad — sus cifras documentadas de 1148 y 1110 escenas son exactamente las
de hoy sumadas con las inicializaciones vacías salteadas (573 + 575 y 554 + 556). Crazy Taxi no
lo tiene (7 vacías en toda una corrida), así que el mismo SDK llega al chip de las dos maneras.

Regresión, con el método fuerte de `docs/demos-kos.md` (RTC fijo y comparación byte a byte
contra el binario de antes del cambio): **los diez demos del juego de control idénticos byte a
byte**, las 21 suites en verde, Crazy Taxi en sus 524 tiras exactas, DCDoom en sus 867 escenas,
el menú del boot ROM en 18572 colores, y en los demos de KOS **una sola** inicialización vacía
por corrida —la primera—, que es la confirmación de que KOS manda una por cuadro. La pantalla de
título de Capcom sale con 76911 colores y ninguna escena vacía.

De paso, una trampa del instrumental: **`DCEMU_CAPTURA_TODAS` ponía el número delante de la
ruta entera** (`f0000-C:/tmp/f.bmp`), así que con una ruta absoluta no escribía nada y la única
queja iba a `stderr.txt` — que es justo donde nadie mira cuando lo que se iba a mirar eran los
BMP. Costó una corrida de 480 cuadros. Ahora el número va delante del nombre del archivo.

### A.12 — Las sombras opacas de Virtua Tenis 2: el factor de destino de la mezcla usaba la tabla del origen (3 de agosto de 2026)

Reportado en vivo: la sombra sobre la cancha «se ve casi opaca». En el attract del propio
juego —al que se llega **sin tocar nada**, entre los cuadros 1520 y 2960— salen como
trapecios y cruces **negros y sólidos** encima de la cancha.

`DCEMU_TRAZA_ESCENA` sobre esa escena (3027 tiras) y un filtro por la caja de pantalla donde
está la sombra dio las tiras culpables en una pasada:

```
tira 2669: tipo=2 blend=0001/0001 env=1 rgba=(0.01,0.00,0.00,1.00)
tira 2668: tipo=2 blend=0307/0307 env=3 rgba=(0.00,0.00,0.00,0.00) .. (0,0,0,0.11)
```

`0x0307` es `GL_ONE_MINUS_DST_COLOR`, o sea el código **3** del TSP de los dos lados. Y ahí
estaba: `blend_modes[]` era **una sola tabla para el factor de origen y el de destino**. Los
ocho códigos son 0 Zero, 1 One, 2 «Other Color», 3 Inverse «Other Color», 4 SRC Alpha, 5
Inverse SRC Alpha, 6 DST Alpha, 7 Inverse DST Alpha: los cuatro últimos nombran su operando
de forma absoluta y valen igual de los dos lados, pero **2 y 3 no** — «el otro color» es el
del destino cuando es el factor del origen y el del origen cuando es el del destino. La tabla
compartida le daba al destino `GL_DST_COLOR` donde va `GL_SRC_COLOR`.

La cuenta cierra con el píxel. Con origen negro y códigos (3,3):

- correcto: `src·(1−dst) + dst·(1−src)` = `0 + dst·1` = **dst**, no toca nada;
- como estaba: `src·(1−dst) + dst·(1−dst)` = `dst·(1−dst)`, que sobre el verde de la cancha
  (~0,45) da 0,25 — **la mitad de brillo en toda el área del cuadrilátero**. El trapecio negro.

Con las dos tablas separadas cambian **exactamente 35836 píxeles** del cuadro, todos dentro
de las sombras, y ni uno fuera (diferencia byte a byte de la misma escena, corrida
determinista con `DCEMU_RTC_FIJO`). Regresión: los diez demos del juego de control idénticos
byte a byte —ningún demo de KOS usa los códigos 2 ni 3, otra vez algo que solo un juego
muestra—, los otros tres juegos con sus perfiles exactos y las 21 suites en verde.

**Lo que queda abierto, dicho como es**: ahora la sombra no se dibuja **en absoluto**. Eso es
lo correcto para lo que esas tiras traen —con origen negro, (3,3) es una operación nula— y
sigue sin ser lo que muestra la consola, así que el oscurecimiento sale de algo que dcemu
tira. **No es el volumen modificador**: medido con `DCEMU_SIN_VOLUMEN=1`, la escena queda byte
a byte idéntica. Quedan dos sospechosos:

- **los bits 25 y 24 del TSP**, que eligen el *búfer de acumulación secundario* en vez del
  framebuffer como operando de la mezcla, y que dcemu solo registra en el log
  (`pvr_srcblendmode` / `pvr_dstblendmode`) — la forma de agujero de siempre;
- **el color de cara de un vértice de intensidad**: esas tiras llegan negras puras con solo el
  alfa por vértice variando (0,00, 0,11, 0,15), que es justo lo que se ve si el color de cara
  del encabezado se perdió.

**Y del método salieron tres herramientas**, porque llegar a una pantalla de juego sin nadie
delante costó cinco corridas de siete minutos:

- `DCEMU_CAPTURA_TODAS=N` guarda **un cuadro de cada N** (antes, todos: dos minutos de juego
  son 7 GB) y con `--traza-mem` cada uno deja su número y su instante emulado. Con eso se arma
  una hoja de contactos del arranque entero y se ve en qué instante aparece cada menú.
- `DCEMU_TRAZA_ESCENA=+K[:M]` elige la escena **por peso** —las M primeras de más de K
  tiras— en vez de por índice. El índice no sobrevive de una corrida a otra; el peso separa
  el partido (miles de tiras) de los menús (cinco). Perseguir el índice tiró dos corridas
  enteras que cayeron en una pantalla de transición.
- `DCEMU_PULSAR_START` y `DCEMU_SOLO_A` aceptan una **lista** de sondeos. Apretar A cada 200
  sondeos llega a un partido y después lo abandona.

**Y una trampa de medición que costó tres corridas**: la primera que «llegó sola al partido»
no llegó sola — la navegó el usuario con el mando mientras miraba. **XInput se lee global y
sin foco de ventana**, así que las pulsaciones de quien esté jugando entran a la medición. Ya
estaba anotado para el sonido; vale igual para la navegación.

### A.13 — El color de offset, contra la especificación; y por qué la sombra de VT2 sigue sin salir (3 de agosto de 2026)

Perseguido con el volcado de escena, ampliado para informar tres campos nuevos: los bits 25 y
24 del TSP (`sel=`), el bit Offset de la palabra ISP (`off=`) y el color de offset por vértice.
Lo que dio la escena 1600 del attract, de 3029 tiras:

| combinación de mezcla | tiras | qué es |
| --- | --- | --- |
| `0001/0000` (SRC=1, DST=0) | 2457 | opacas — y el documento **exige** justo eso para una opaca |
| `0001/0304` | 275 | |
| `0302/0303` (SRC=4, DST=5) | 258 | lo que el documento **exige** para punch-through |
| **`0307/0301` (SRC=3, DST=3)** | **27** | **las sombras** |
| `0001/0001` | 8 | segunda pasada de las sombras |
| `0000/0000` | 4 | degeneradas, fuera de pantalla |

Las dos filas en negrita del documento —opaca = (1,0), punch-through = (4,5)— confirman de
paso, y de forma independiente, que dcemu lee los campos en el orden correcto: bits 31-29 el
origen y 28-26 el destino.

**Tres mecanismos quedaron descartados por medición, no por lectura:**

- **volumen modificador**: con `DCEMU_SIN_VOLUMEN=1` la escena sale byte a byte idéntica;
- **búfer de acumulación secundario** (§3.4.6.1, bits 25 y 24 del TSP): `sel=0/0` en las 3029;
- **alfa del destino**: limpiar el búfer con alfa 1 en vez de 0 no cambia un solo píxel.

**Lo que sí faltaba y ahora está: el color de offset.** La tabla de la instrucción de
textura/sombreado lo suma DESPUÉS de combinar el texel con el color base —`PIXRGB = COLRGB ×
TEXRGB + OFFSETRGB` en los cuatro modos— y dcemu no lo leía en ninguna parte, con `off=1` en
3022 de las 3029 tiras. En GL eso es el **color secundario** con `GL_COLOR_SUM`, que suma
exactamente donde corresponde; plegarlo en el color del vértice no sirve, porque
`(COL+OFF) × TEX` no es `COL × TEX + OFF` salvo con textura blanca. La entrada es de GL 1.4,
así que se pide por `SDL_GL_GetProcAddress`. Se analiza en todos los tipos de vértice
texturados, incluidos los de dos volúmenes y los de intensidad —estos traen una **segunda**
intensidad que multiplica el *color de cara de offset* del encabezado, que solo trae el
encabezado de tipo 2, en sus palabras 12-15—. Regresión: los diez del juego de control byte a
byte, los cuatro juegos con sus perfiles exactos, DCDoom en sus 867 escenas, 21 suites en
verde. Efecto visible: en Virtua Tenis 2 le devuelve los brillos a la piel de los jugadores.

**Y aun así la sombra no sale, y ahora se puede decir por qué con precisión.** Las 27 tiras
traen, medido: color base **negro**, color de offset **cero**, y una textura RGB565 de 8×8
—sin canal alfa—, o sea `PIXRGB = 0 × TEX + 0 = 0`. Con el origen en negro y códigos (3,3) la
ecuación del propio documento da

```
DST_rgb' = SRC_rgb·(1−DST_rgb) + DST_rgb·(1−SRC_rgb) = 0 + DST_rgb·1 = DST_rgb
```

es decir **no toca nada**, y no hay color de origen negro que pueda oscurecer por esa vía. Así
que el oscurecimiento no está en esas tiras tal como dcemu las decodifica. Queda una hipótesis
sola, y es la que hay que atacar: que las palabras ISP/TSP de esos encabezados no se estén
leyendo de donde corresponde —el resto del pipeline ya está descartado— o que la sombra la
dibuje algo que en esta escena del attract no aparece. El instrumental para contestarlo ya
está puesto: el volcado imprime `sel=`, `off=` y el offset por vértice.

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
PC: es la cuarta sospecha de A.2, y aquí sale sola. Sin veredicto por el serial. **Cerrado en
A.8**: era la detección de la Broadband Adapter, es benigna, y el juego corre.

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

### C.9 — kgl-tunnel: la niebla salió, el texto no

De mirar kgl-tunnel con el ojo (2026-08-01) salieron tres cosas, dos cerradas:

- **La niebla de tabla del PVR no estaba emulada** — densidad, color y la curva de 128
  entradas caían en `control_mem` sin lector, y el túnel terminaba en un pozo negro que
  tapaba los arcos y columnas de sus paredes. **Resuelto**: `dibujar_niebla_tira()` en
  `graficos.c` (ver CLAUDE.md, "Fog"). La niebla por vértice (modo 1 del TSP) sigue sin
  emular; ninguna demo del parque la usa.
- **El moiré del piso cercano** es submuestreo: la KGL nueva sube `tile.pcx` sin mipmaps y
  la minificación fuerte con `GL_LINEAR` hace franjas horizontales. En hardware pasa igual
  (y la niebla lo atenúa); `DCEMU_MIP_AUTO=1` genera mipmaps para texturas que no traen y
  lo suaviza, como diagnóstico, no por omisión.
- **El texto no se dibuja** — ni el `plprint()` del título ("Tunnel V1.5...") ni las seis
  líneas del menú de ayuda; el panel translúcido de `do_help()` sí sale, y por eso se ve
  como un rectángulo oscuro sin nada dentro (se confundía con "una ventana en la pared").
  La fuente la dibuja PLIB (`plprint.h` en la demo). **Pendiente**: falta medir qué manda
  — si es una textura de fuente con un formato que dcemu decodifica mal, o vértices que no
  llegan. `DCEMU_TRAZA_ESCENA` sobre un cuadro del túnel es el primer paso.

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
**Zanjado por medición el 2 de agosto (ver A.8)**: es la detección de la Broadband Adapter,
los ceros deterministas de `mem_read_error()` la hacen fallar limpio, y el juego sigue sin
red. No hay que mapear el área.

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
2. ~~**Vía A.0**~~ — hecha, y rehecha el 1 de agosto (A.0b).
3. ~~**Vía A.1 y A.2**~~ — **hecho: el hito D cayó por cuadruplicado** (Crazy Taxi el 1 de
   agosto, A.3; Virtua Tennis, Capcom vs. SNK y Virtua Tenis 2 el 2, A.6-A.8). La vía A
   queda cerrada; lo que sigue de los juegos es jugarlos y anotar residuos, que es C.5 con
   otro parque.
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

- ~~**Que la vía A no sea una cosa sola.**~~ No lo era, y el pronóstico acertó: cuatro
  causas, cada una tapando a la siguiente (el banco de registros, el modelo de eventos del
  ASIC, el hook de la lectora, la lista en curso del TA). A.0 fue lo que permitió verlas
  por separado.
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
| A.1-A.2 (el hito D) | 2 a 8, sin piso claro | **hecho: 1-2 de agosto, los cuatro juegos (A.3, A.6-A.8)** |
| D.1 (RM, DN) | 1 | hecho el 1 de agosto |
| D.2 (las dos filas de `SGR`) | 0,5 | pendiente, lo más barato de todo |
| B (el AICA completo) | 8 a 15 | hasta la fase 4; quedan CDDA y el DSP |
| C.5 (revisar las 33) | 1 | pendiente |
| D.3-D.5, E | 3 | pendiente |

El rango de A resultó honesto en las dos puntas: cada causa una vez encontrada fue barata
(un syscall, una demora, un latch), y encontrarlas costó lo que costó — cuatro causas
distintas en dos días, ninguna repetida.
