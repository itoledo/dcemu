# Estado de las demos de KallistiOS

Estado medido el **30 de julio de 2026** y **vuelto a medir entero el 1 de agosto**, sobre los 135
binarios compilados desde `kos/examples/dreamcast` más `demos/roto`. Este documento es la línea base
de regresión: si un cambio rompe algo, aquí está lo que funcionaba.

La segunda medición no fue por gusto: el 1 de agosto se corrigió el sentido de la profundidad, que
afecta a **toda** demo con geometría superpuesta, así que la línea base heredada dejaba de valer.
Confirmó el inventario salvo en un punto, `tsunami-genmenu`, que está anotado más abajo.

| | |
| --- | --- |
| Funcionan | **97** binarios |
| Fallan por algo que falta emular | **10** |
| No aplican: piden periféricos o herramientas del anfitrión | **28** |

**De las diez que fallan, solo una es del PVR** —`pvr-fb_tex`, y por las dos ventanas de la RAM de
vídeo, no por el rasterizado—. Las otras nueve son siete de sonido, que piden el AICA, y dos del
SH-4: `basic-breaking` (UBC) y `basic-dma-speedtest`.

El 1 de agosto de 2026 pasaron seis: `pvr-modifier_volume`, `pvr-modifier_volume_tex` y
`pvr-cheap_shadow` —con los tipos de vértice que faltaban, el rearmado de los parámetros de 64
bytes y el volumen modificador por plantilla—, `pvr-pvr_rtt_sized` con el render a textura,
`pvr-bumpmap` con los sprites y el modo de textura, y `pvr-pvrline`, que **nunca estuvo roto**.

El 31 de julio de 2026 pasaron siete de la segunda fila a la primera: `parallax-serpent_dma`
con el CH2 DMA, las tres de paleta al decodificar los formatos indexados, las dos de
`yuv_converter` con el convertidor YUV del TA y `pvr-strided_texture` con el stride — estas
tres últimas también con el arreglo del filtro de textura, que era lo que de verdad las
dejaba en blanco. El resto del inventario es el del 30.

## Cómo se mide

**Desde el 31 de julio de 2026, con `--captura-gl`**, no capturando la ventana:

```sh
dcemu demo.bin --salir-tras=8 --captura-gl=cap.bmp
```

El emulador vuelca a un BMP lo que OpenGL rasterizó, justo antes de presentar cada cuadro, y sale
solo a los 8 segundos de tiempo emulado. Después se cuentan los colores distintos del BMP. El
inventario de abajo se midió con el método viejo, así que sus cifras no son comparables una a una
con las de este, pero el veredicto sí lo es.

**El volcado recortaba, y eso invalidó un barrido entero.** `volcar_gl()` leía 640×480 desde
(0,0) —el tamaño de la pantalla emulada— pero la ventana de GL es de **800×600** y `screeninit()`
estira los 640×480 del guest sobre ella entera con `glOrtho`. Así que salía el rectángulo de abajo
a la izquierda: se perdía el 20% de arriba y el 20% de la derecha. Cualquier demo que escriba en la
franja superior —toda la familia `conio`, que pone ahí su texto— daba un BMP casi negro y parecía
que no dibujaba nada. `conio-basic` pasó de 8 píxeles no negros a 2742 con el arreglo, sin tocar
nada del PVR. Ahora lee `outputscreen->w/h`, o sea la ventana de verdad.

**Por qué cambió**: capturar la ventana depende del compositor del anfitrión, y eso se rompe sin
aviso. En una misma sesión, `tunnel` pasó de 3036 colores a 4 sin que cambiara una línea del
emulador, y con `--captura-gl` seguía dando 1837. Un barrido entero puede salir en negro y parecer
una regresión masiva.

**Lo que este método no ve**: `--captura-gl` vuelca desde `cb_tastart()`, o sea sólo cuando el
guest dibuja por el TA. Una demo que use el framebuffer 2D —`video-bfont` y compañía— no
produce archivo, y eso significa «no pasó por el TA», no «falló». Para esas está F5, que vuelca
la RAM de vídeo. Se vuelca además sólo el último cuadro **con geometría**, porque una demo que
dibuja una vez y se queda esperando un botón sigue generando cuadros vacíos.

Aparte se guarda `logs/serial.txt` y se extrae la última línea con marca de resultado. Secuencial
a propósito: dos instancias se pelean por `logs/serial.txt` y los resultados salen cruzados.

**Cuatro trampas del método viejo, las cuatro ya costaron tiempo:**

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

### Tipos de vértice del TA (0)

Estaban implementados **cuatro de quince**: el 0, el 1, el 3 y el 5. Los once que faltaban se
descartaban en silencio —el vértice no entraba al buffer, la tira cerraba con `count` 0 y no se
dibujaba nada—, y eso es lo que dejaba en negro a las demos de volumen modificador:
`pvr-modifier_volume` perdía así 41 de sus 42 vértices, todos de tipo 9.

El diagnóstico sale entero de `--traza-mem`, que reporta los tipos de vértice vistos por escena:
`[0]=1 [9]=41` con todas las tiras en `n=0` no deja lugar a dudas.

Detrás había un segundo problema, y es el que hacía falta para `modifier_volume_tex`: **los
parámetros de 64 bytes no se rearmaban.** Llegan en dos bloques de 32 —uno por store queue— y cada
uno se despachaba por separado, así que la segunda mitad se leía como si fuera una palabra de
control. Como su primera palabra suele ser un float, el tipo salía de la basura; cuando ese float
vale 0.0 el tipo sale **0**, que es fin de lista. Ahora `ta.c` sabe cuánto mide cada parámetro
—de la palabra de control más el tipo de vértice que dejó vigente el último encabezado— y junta las
dos mitades antes de despachar.

Nota: **esto no arregló el `attempt to submit to unopened list` de `tunnel`**, que era la sospecha.
Sigue apareciendo miles de veces por corrida aunque la demo dibuje bien.

### Formatos de textura del PVR (0)

**`pvr-bumpmap` funciona desde el 1 de agosto de 2026**, y para llegar ahí hicieron falta cuatro
cosas, de las que el formato BUMP era la menos importante:

1. **Sprites.** La demo dibuja con `pvr_sprite_txr_t`, y dcemu no dibujaba sprites: `taSprite()` era
   un stub que solo logueaba. Un sprite es un rectángulo entero en un parámetro de 64 bytes —
   cuatro esquinas de las que la última se deduce completando el paralelogramo, D = A − B + C— y su
   color va **en el encabezado**, no en los vértices. Cuidado con la palabra 12: entre `Dy` y las UV
   hay una sin usar, así que las tres de textura son la 13, la 14 y la 15. Leerlas una antes deja la
   `u` en cero para las cuatro esquinas y la textura se muestrea sobre una línea.
2. **El modo de textura.** No se emulaba, o sea que todo salía con el `GL_MODULATE` de fábrica.
   La demo usa `PVR_TXRENV_DECAL`, que **vale 2 y no 0** —0 es replace—, y con el color de vértice
   en negro que trae el sprite, multiplicar da negro. Es lo que dejaba la pantalla vacía.
3. **El modo de profundidad de la lista punch-through**, que estaba fijo en `GL_LEQUAL`. El buffer
   se limpia a 0,0 y las z de una escena caen alrededor de 0,5, así que «menor o igual» falla contra
   cualquier píxel sin tocar: la pared de ladrillos no ponía un solo píxel. Usa el modo de su propia
   palabra ISP, como la lista opaca; lo que distingue al punch-through es que descarta por alfa.
4. **El formato BUMP.** Un texel no es un color: son dos ángulos de 8 bits, la elevación S arriba y
   la rotación R abajo, y el chip los combina por píxel con cuatro parámetros que vienen en el color
   de offset —K1, K2, K3 y Q— según `I = K1 + K2·sin(S) + K3·cos(S)·cos(R − Q)`.

**Lo que el punto 4 no hace**: en el chip esa intensidad *modula* al polígono texturado que viene
detrás, y eso es matemática por fragmento que OpenGL de función fija no tiene. `decodificar_bump()`
la resuelve al subir la textura y la deja como un gris. Es exacto mientras los parámetros salgan del
encabezado —que es el caso de un sprite— y lo que se pierde es la combinación con la otra capa; en
esta demo la aproxima el propio blend multiplicativo (`GL_DST_COLOR`/`GL_ZERO`) que ella programa.

Las otras seis se resolvieron el 31 de julio de 2026. Las tres de paleta por lo que se cuenta
más abajo; `pvr-yuv_converter-YUV420` y `-YUV422` porque faltaba el convertidor YUV del TA
—la entrada de `0x10800000`, que la zona `0x10` mandaba entera a la FIFO de polígonos— más el
formato de textura YUV422; y `pvr-strided_texture` porque el stride no se aplicaba.

**Pero las tres últimas fallaban además por lo mismo, y eso vale más que los tres formatos
juntos**: `glTexParameteri` se aplica a la textura *ligada*, y los filtros se ponían **antes**
del `glBindTexture`. Caían sobre la textura del cuadro anterior, y la nueva se quedaba con el
`GL_TEXTURE_MIN_FILTER` de fábrica, que es `GL_NEAREST_MIPMAP_LINEAR` y **exige mipmaps**: sin
ellos la textura está incompleta y GL la muestrea **blanca**. Una demo que dibuja muchos
cuadros lo tapa —desde el segundo, la textura ya quedó ligada y sí recibe los parámetros—,
pero una que dibuja uno solo y espera un botón salía entera en blanco.

**Las tres de paleta ya funcionan** (31 de julio de 2026): `pvr-palette-4bpp` dibuja su degradado
radial con las bandas de 16 niveles que corresponden a 4 bpp, `pvr-palette-8bpp` el mismo suave, y
`pvr-palette-wormhole` su remolino nítido. Lo que faltaba era leer la RAM de paleta
(`0x005F9000`, 1024 entradas) y `PAL_RAM_CTRL` (`0x005F8108`) — los dos ya tenían respaldo, sólo
que nadie los leía — y aplicar el twiddle sobre índices de píxel en vez de sobre palabras de 16
bits. El selector de banco va en el propio texture control word, encima de los bits que en los
demás formatos son «sin usar», «stride» y «scan order»; por eso estas texturas van siempre
twiddled.

### Otras rutas del PVR (1)

| demo | qué falta |
| --- | --- |
| `pvr-fb_tex` | **las dos vistas de la RAM de vídeo del PVR** |

`pvr-fb_tex` usa el framebuffer ya rendido como textura: `pvr_get_front_buffer()` y una textura
apuntada ahí. Lo primero que falta es evidente —dcemu manda el 3D a OpenGL y nunca escribe la RAM
de vídeo en el camino normal, así que no hay nada que muestrear—, pero **lo que de verdad lo
bloquea es otra cosa**, y se ve midiendo: la textura de la demo está en `0x0014E900` y
`FB_W_SOF1` vale `0x004A7480`. No se solapan ni de lejos.

La razón es que **la RAM de vídeo del PVR tiene dos ventanas que entrelazan sus dos bancos de forma
distinta**: la de 32 bits (`0xA5000000`), donde se escribe el framebuffer, y la de 64
(`0xA4000000`), desde donde el chip lee las texturas. Son la misma memoria con dos numeraciones. En
dcemu las dos zonas apuntan planas al mismo bloque, así que una dirección de una no corresponde a la
otra y ningún volcado del framebuffer caería donde la textura lo busca.

O sea que esta demo no pide «escribir el framebuffer»: pide emular las dos ventanas, que es un
subsistema y toca todos los caminos de textura y de framebuffer a la vez. Se llegó a escribir el
volcado —con su disparador, para no cobrarle un `glReadPixels` por cuadro a las demás demos— y se
retiró al comprobar que sin las dos ventanas no puede acertar. Lo que quedó es
`volcar_a_memoria()`, que es la mitad reutilizable y la que usa el render a textura.

### Render a textura (0)

`pvr-pvr_rtt_sized` funciona desde el 1 de agosto de 2026, y con él se descubrió que
**`pvr-texture_render` también usa `pvr_scene_begin_rtt`** y hasta ahora «funcionaba» por
accidente: dcemu ignoraba el destino y mandaba a la pantalla la escena que debía ir a la textura.

Lo que lo marca es **el bit 24 de `FB_W_SOF1`** (`0x005F8060`): KOS escribe `dirección | BIT(24)`.
El tamaño sale de los registros de recorte (`PCLIP_X`/`PCLIP_Y`, `0x005F8068` y `0x6C`, con el
máximo en los bits 31-16), el paso entre filas de `FB_W_LINESTRIDE` (`0x005F804C`, en unidades de
8 bytes) y el formato de `FB_W_CTRL` (`0x005F8048`).

Como dcemu manda el 3D a OpenGL, la escena no pasa por la RAM de vídeo: hay que dibujarla y leerla
de vuelta con `glReadPixels`. Dos detalles que no son obvios y que hay que acertar:

- el guest entrega los vértices **en coordenadas del destino** —0..128 por 0..64 para esta demo—,
  así que el viewport y el `glOrtho` tienen que ser los de la textura y no los de la pantalla, o la
  escena sale minúscula en una esquina;
- `glReadPixels` entrega de abajo hacia arriba y la textura se guarda de arriba hacia abajo.

**La comprobación es byte a byte, no «se ve parecido»**: volcando la RAM de vídeo en el destino con
`--volcar` sale `ff ff` en la fila 0 —el borde blanco que la demo dibuja en z=5— y `0c 22` en la
fila 64, que es `0xFF204060` convertido a RGB565. Y `pvr-texture_render` pasa de 47539 colores a
2048 justamente porque ahora el contenido hace la ida y vuelta por una textura RGB565.

**Queda un residuo**: `pvr_rtt_sized` dibuja dentro de la textura dos marcas pequeñas, en z=3 y z=4,
que no aparecen; el fondo (z=1), el rectángulo interior (z=2) y los bordes (z=5) sí. La causa es la
prueba de profundidad —con `GL_ALWAYS` las marcas salen— pero **no es la precisión**: el contexto ya
da 24 bits (`--traza-mem` lo reporta) y estrechar el rango del `glOrtho` de la pasada RTT no cambia
nada. Sin explicación todavía.

### Volúmenes modificadores (0)

Los tres funcionan desde el 1 de agosto de 2026. El PVR decide píxel a píxel si está dentro del
volumen y con eso elige entre los dos juegos de parámetros del polígono; en OpenGL de función fija
eso es el buffer de plantilla, que hay que **pedir** —`SDL_GL_STENCIL_SIZE`—, porque el contexto no
trae ninguno por omisión.

Son dos mecanismos distintos y el encabezado dice cuál:

- **bit Volume**: el vértice trae dos juegos de color y de UV, el 0 para fuera y el 1 para dentro.
  `pvr-modifier_volume` pinta sus cuadrados azules y el trozo cubierto por el volumen sale
  `0xFF00FF00`, que es exactamente el `argb1` que la demo escribe.
- **bit Shadow**: sombra barata, un solo juego cuya intensidad se escala por `FPU_SHAD_SCALE`
  (`0x005F8074`, bits 7-0, con el permiso en el 8). `pvr-cheap_shadow` pide 0,5 y el azul de dentro
  sale `0x7F` — 255 × 0,5.

Cada tira afectada se dibuja **dos veces con la prueba de plantilla invertida** —fuera del volumen
con el juego 0, dentro con el 1— en vez de dibujar el juego 0 entero y encimarle el 1. Así cada
píxel sale una sola vez, que es lo que importa cuando hay mezcla alfa: encimar mezclaría dos veces.

Se usan dos bits de plantilla, no uno: el volumen de la lista 1 afecta a la lista opaca y el de la
lista 3 a la translúcida, y son independientes.

**La limitación**: es una unión de triángulos, no un volumen de verdad. El chip resuelve volúmenes
cerrados en 3D contando caras delanteras y traseras. Alcanza para lo que hacen estas demos —un
cuadrado plano en coordenadas de pantalla— y para una sombra proyectada sobre un plano, que es el
uso corriente; un volumen convexo cerrado visto desde dentro saldría mal.

**Al medirlas, ojo**: las tres colocan su geometría con `rand()`, así que el volumen se solapa con
algún polígono en una corrida y en la siguiente no. Un BMP de dos colores no prueba nada; hay que
correrlas varias veces.

**`pvr-pvrline` nunca necesitó primitivas de línea.** El PVR no las tiene y la demo tampoco las
pide: dibuja cada línea como un cuadrilátero en tira de triángulos, y lo dice en su propio
comentario. Funciona. Lo que la hacía parecer rota era el recorte del volcado más el hecho de que
arranca con **una** línea —`linecount = 1`—, así que un BMP con 632 píxeles no negros es
exactamente lo que corresponde. Esta fila era una conjetura del inventario, no una medición.

`cheap_shadow` ya dibujaba antes de este cambio: sus polígonos son de tipo 0, porque el modo de
sombra barata no necesita dos juegos de parámetros. `pvr-modifier_volume_zclip` también, y por la
misma razón: su geometría es casi toda de tipos ya implementados (`[0]=15 [3]=18` contra un solo
vértice de tipo 9). Los que pasaron de pantalla negra a dibujar son `modifier_volume` —tipo 9— y
`modifier_volume_tex` —tipo 11, que además necesitaba el rearmado.

**`parallax-serpent_dma` ya funciona** (31 de julio de 2026). La sospecha de este documento era
correcta: entregaba la geometría por el CH2 DMA (`pvr_dma_load_ta`), que no estaba emulado —
sus tres registros caían en el respaldo del bloque de control y se perdían en silencio. Dibuja
la serpiente de esferas con su contador de cuadros. El mismo arreglo es el que hace arrancar la
BIOS; ver [bios-boot-plan.md](bios-boot-plan.md).

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

**El `pvr_prim: attempt to submit to unopened list` no es de dcemu**, y esta tabla lo daba por tal.
Es estado del guest de punta a punta: `pvr_list_begin()` pone `pvr_state.list_reg_open`,
`pvr_list_finish()` lo limpia y `pvr_prim()` avisa cuando está en `PVR_LIST_NONE`; el emulador no
tiene forma de tocarlo. Dos medidas lo zanjan: de 31 demos con serial guardado **solo `tunnel`** lo
emite —278674 veces en 8 segundos— y nunca aparece el aviso hermano
`pvr_list_begin: attempt to open already closed list`, o sea que se está enviando sin ninguna lista
abierta. `tunnel` es la demo de KGL que se restauró y portó aquí, y su propio fuente documenta el
cambio de API que lo provoca: la KGL actual abre la lista de forma perezosa según el estado de GL y
no tiene `glKosFinishList`.

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

### Gráficos verificados a ojo (24 binarios, 22 demos)

`video-bfont` = `bfont`, `video-minifont`, `video-multibuffer`, `video-screenshot`,
`video-palmenu`, `conio-basic`, `conio-kosh`, `conio-wump`, `conio-adventure`,
`kgl-tunnel` = `tunnel`, `libdream-ta`, `parallax-bubbles`, `png`, `pvr-texture_render`,
`tsunami-font`, `basic-mmu-pvrmap`, `pvr-palette-4bpp`, `pvr-palette-8bpp`,
`pvr-palette-wormhole`, `pvr-yuv_converter-YUV420`, `pvr-yuv_converter-YUV422`,
`pvr-strided_texture`.

Las seis últimas se verificaron el 31 de julio de 2026 con `--captura-gl`: las de paleta
dibujan su degradado radial —con las bandas de 16 niveles que le corresponden a 4 bpp— y el
remolino; las de `yuv_converter`, una pared de ladrillos con el color correcto; y
`pvr-strided_texture`, su tablero de ajedrez con las casillas cuadradas y alineadas, que es
justo lo que un stride mal aplicado arruinaría.

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
`parallax-rotocube`, `parallax-serpent_dma`, `parallax-sinus`, `plasma`, `pthread-general`,
`pvr-modifier_volume_zclip`, `pvr-plasma`, `pvr-pvrmark`, `pvr-pvrmark_strips`,
`pvr-pvrmark_strips_direct`, `roto`, `rumble`, `sound-ghettoplay-vorbis`, `sound-hello-mp3`,
`sound-hello-ogg`, `tsunami-banner`, `vmu-vmu_beep`, `vmu-vmu_game`, `vmu-vmu_pkg`.

**`tsunami-genmenu` salió de esta lista el 1 de agosto de 2026**, y no por una regresión: la
geometría llega perfectamente —160 tiras, 640 vértices, todos de tipo 3, con textura y color— pero
sus coordenadas caen en **y 631..1458 sobre una pantalla de 480**, o sea completamente por debajo.
Es un menú que entra deslizándose y nunca sube: a los 40 segundos de tiempo emulado sigue igual.
Como dcemu no toca las coordenadas —el tipo 3 lee los mismos desplazamientos que antes— eso lo
manda el guest, y lo más probable es que el menú espere una pulsación que el barrido no da. Queda
aquí para que la próxima medición no lo lea como algo roto del PVR.
