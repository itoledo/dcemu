# Notas: el PVR2, el TA y el camino a OpenGL

Detalle del subsistema gráfico: qué se encontró, cómo se midió y por qué la lectura equivocada
parecía plausible. `CLAUDE.md` tiene el resumen de qué hace cada pieza; esto es la arqueología.

**Dónde está el PVR: ninguna demo de KOS falla por el PVR.** `pvr-fb_tex`, la última, necesitó
las dos ventanas de RAM de video *más* la reescritura del framebuffer *más* el arreglo del signo
de profundidad en `glOrtho` — y ese último también resolvió los marcadores de `pvr_rtt_sized`.
Están todos los formatos de textura, todos los tipos de vértice, los sprites, los volúmenes
modificadores y el render a textura. El único residuo documentado es `tsunami-genmenu`, cuya
geometría llega correcta pero cae en y 631..1458 sobre una pantalla de 480 líneas y nunca entra
desplazándose — del lado del guest, ya que dcemu no toca las coordenadas de los vértices.

---

## El camino de entrada: colas de almacenamiento y el TA

El guest envía geometría por las colas de almacenamiento del SH-4, no por una escritura normal:
`pref142()` en `syscontrol.c` vacía SQ0/SQ1, y cuando el destino cae en la FIFO del TA
(`0x10000000`) entrega el bloque de 32 bytes a `ta_procesar_bloque()` en `ta.c`, que despacha por
el para-type a `taListEnd()`, `doUserClip()`, `objectListSet()`, `taPolyModifier()`, `taSprite()`
o `taVertexHandler()` en `graficos.c`. El CH2 DMA (`mem.c`) alimenta la misma función.

`pref142()` despacha al decodificador del TA solo cuando el destino resuelto de la cola cae en la
FIFO de polígonos, probado como `(addr & 0xFF800000) == 0x10000000`. Antes probaba
`addr & 0x10000000`, un AND que también acertaba con `0x11xxxxxx` — así que cada bloque de 32
bytes de subida de textura se interpretaba como una palabra de control de polígono.

### Los dos DMA que alimentan el TA

**`0x005F6800-0x005F6808` es el CH2 DMA, y es como el guest alimenta el TA.** Lo maneja el Holly,
no el DMAC: el SH-4 solo pone el origen en `SAR2` y arma `CHCR2` para petición externa, y escribir
1 en `SB_C2DST` es lo que lo arranca. Así que `dma_check()` nunca lo ve — solo maneja canales de
autopetición, correctamente — y los tres registros no tenían case en `pvr_write()`, caían al
almacén de respaldo `control_mem` y se desvanecían. El boot ROM anota la transferencia en su
propia tabla de descriptores con los bits 0-1 de `+0x18` en "en curso" y espera a que el fin de
DMA los limpie, cosa que nunca llegaba: ese es el lazo alrededor de `0x8C0D9C50` en que se
quedaba para siempre. `ch2_dma_ejecutar()` en `mem.c` lo hace, y el mismo arreglo hace funcionar
`parallax-serpent_dma`.

**`0x005F6810-0x005F6820` es el Sort-DMA, la *otra* manera de alimentar el TA** — y la que usa el
ddraw de Windows CE para toda su geometría. DevBox de Sega §2.6.5.3 y figuras 2-10 a 2-13: el
guest arma la lista de despliegue en RAM como cadenas enlazadas (una tabla de Start Link Address
de entradas de 16 o 32 bits según `SB_SDWLT`, y dentro de cada Global Parameter la séptima palabra
lleva el tamaño actual en unidades de 32 bytes y la octava el siguiente enlace, con 1 = fin de
lista y 2 = fin de DMA), y después escribe 1 en `SB_SDST`. `sort_dma_ejecutar()` en `mem.c`
recorre las cadenas hacia `ta_procesar_bloque()`; el fin levanta el bit 20 de `SB_ISTNRM`
(DTDESINT) con el retardo proporcional de siempre. Los seis registros tenían almacén de respaldo
en `control_mem` y ningún lector — la misma enfermedad que todo lo demás aquí — y el síntoma era
el HAL de ddraw de DCDoom atascado en su estado 7 imprimiendo "Timeout for Tile Accelerator" 13
veces por segundo, habiendo enviado un encabezado y cuatro vértices que nunca llegaron.

### Tres bugs que dejaron la salida 3D en blanco

Los tres están arreglados, y la forma de ellos vale la pena recordarla porque ninguno produjo un
mensaje de error.

- `sq_write()` ignoraba su argumento `size` y guardaba un solo `DWORD`. KOS llena las colas con
  `fmov.d` (8 bytes), así que cada guardado perdía su mitad alta: dwords pares bien, impares
  rancios. En un vértice del TA eso significa x y z basura (dwords 1 y 3) con y bien (dword 2) —
  medible como `x 0.0..0.0 ... z inf..inf` en la traza.
- La FIFO del TA es una FIFO, pero `ta_write()` indexaba `ta_mem[direccion - 0x10000000]`
  mientras `pref142()` releía `ta_mem[addr & 0xFF]`. KOS escribe la FIFO a direcciones
  *crecientes*, así que pasado `0x100` las dos divergían y pasado `TA_SIZE` se descartaba todo —
  `pref142()` entonces releía un registro rancio para siempre, cada PCW salía `0xE0000000`, el
  fin de tira nunca disparaba y `strip_count` quedaba en 0, así que `glDrawArrays` nunca se
  llamaba. Ahora ambos enmascaran con `& 0x3F`: dos ranuras de 32 bytes, una por cola.
- El parámetro de vértice tipo 3 (texturado, color empaquetado) leía sus coordenadas de
  `ta_address_pointer[6]` — `+0x18`, el color base — en vez de `[1]`, desbordando el registro de
  32 bytes por 12 bytes. El tipo 5 ya lo hacía bien, que es lo que delata el caso.

`cb_tastart()` además indexaba `TriangleStrip[strip_count]` para culling y escritura de z dentro
de un lazo sobre `i`, es decir una posición más allá de la última válida.

Dos más de la misma familia, ambos arreglados: los factores de mezcla de origen y destino de la
palabra TSP (bits 31-29 y 28-26) se asignaban **ambos** a `pvr_srcblend`, así que `pvr_dstblend`
nunca se escribía y `glBlendFunc()` recibía un enum inválido — que GL ignora, dejando la mezcla
que estuviera puesta antes. Y el vértice tipo 3 tomaba su alfa de los bits 23-16, el mismo campo
que el rojo, en vez de 31-24.

### `ta.c` existe porque no todo parámetro del TA mide 32 bytes

Los encabezados que llevan *dos* colores de cara, los vértices con color en punto flotante, los
seis vértices texturados de dos volúmenes, ambos vértices de sprite y el vértice de volumen
modificador miden 64, y llegan como *dos* bloques — uno por cola de almacenamiento. Despachar
cada bloque por separado lee la segunda mitad como una palabra de control de parámetro, y como su
primera palabra suele ser un float el para-type sale de la basura: cuando ese float es `0.0` el
tipo es **0**, que es fin de lista, así que se cierra una lista que el guest nunca cerró.
`ta_clasificar()` es la tabla — PCW → tipo de parámetro global y el tipo de vértice que deja en
vigor — y `ta_procesar_bloque()` une las mitades antes de despachar. Tanto `taPolyModifier()`
como el ensamblador de bloques usan esa única tabla, así que no pueden divergir. `ta.c` está
libre de SDL y GL a propósito, como `sistema.c`, para que `tests/` lo enlace de verdad.

**El Polygon Type 1 mide 32 bytes — su único color de cara cabe en las palabras 4-7.** Solo el
Type 2 (colores de cara *y* de offset) y el Type 4 (un color de cara por volumen) miden 64, con
los colores en las palabras 8-11 y 12-15. dcemu tenía el Type 1 como de 64 con el color leído de
las palabras 8-11 — la disposición del Type 2 — y la suite `ta` tenía la misma lectura equivocada
horneada, así que nunca objetó. Ninguna demo de KOS envía un Type 1; el boot ROM sí: su estela de
espiral y su logo salen como encabezados en modo intensidad 1, el ensamblador pegaba cada
encabezado al primer vértice detrás, el color de cara salía de las coordenadas de ese vértice
(alfa negativo: invisible) y cada quad perdía un vértice. Eso es lo que dejaba en blanco la pasada
de color de la animación de intro.

### Los quince tipos de vértice

**`taVertexHandler()` implementaba cuatro de los quince** — 0, 1, 3 y 5. Los otros once caían a un
`logxmsg` y se descartaban: el vértice nunca entraba a `VertexBuffer`, la tira cerraba con `count`
0, y no se dibujaba nada. Eso es lo que dejaba negras las demos de volumen modificador —
`pvr-modifier_volume` perdía 41 de sus 42 vértices, todos tipo 9. `--traza-mem` nombra el tipo
culpable directamente: `[0]=1 [9]=41` con cada tira en `n=0`.

Los tres ejes son cómo llega el color (empaquetado / flotante / intensidad), si las UV son de 32
o 16 bits, y si hay dos juegos de parámetros. Los tipos de intensidad multiplican el **color de
cara** del encabezado, que sobrevive más allá de su propio encabezado a propósito: el modo
intensidad 2 reutiliza el que dejó el último polígono en modo 1.

Los vértices de dos volúmenes llevan todo dos veces — juego 0 fuera del volumen modificador,
juego 1 dentro — y `struct vertex` guarda ambos. Los tipos con un solo juego reciben el juego 1
como copia, así que la segunda pasada puede dibujar cualquier tira sin preguntar de qué tipo era.

---

## Parámetros globales, culling y listas

**Dos bugs que entre los dos perdieron toda la familia `conio/*`**, que dibuja un quad texturado
por carácter directo por `pvr_prim()`:

- **Los parámetros globales no sobrevivían más allá de la primera tira.** En el PVR un encabezado
  de polígono fija el modo de profundidad, el culling, la escritura de Z, el alfa, ambos factores
  de mezcla y todo lo de la textura, y eso queda en vigor hasta el *siguiente* encabezado.
  `taPolyModifier()` los escribía en la entrada de `TriangleStrip[]` que estuviera abierta, así
  que una segunda tira bajo el mismo encabezado partía de cero: `depthmode` salía `0`, que no es
  un enum válido, así que `glDepthFunc()` se ignoraba y quedaba el valor de la tira anterior — o
  `GL_NEVER`, que no dibuja nada. El fin de tira ahora copia la entrada terminada en la nueva;
  `index` y `count` se rellenan solos.
- **El culling ignoraba el sentido de giro.** El campo del PVR tiene cuatro valores: 0 ninguno,
  1 "descartar si es pequeño" (un umbral de área, que GL no tiene equivalente), 2 descartar área
  negativa, 3 descartar positiva. Cualquier valor distinto de cero se trataba como
  `glCullFace(GL_BACK)` con el `GL_CCW` por omisión de GL. El TA entrega coordenadas en espacio
  de pantalla con y hacia abajo y el `glOrtho()` de `screeninit()` la invierte, así que el sentido
  que ve GL es el opuesto al que el PVR asume — y cada quad que conio dibujaba se descartaba. Los
  modos 2 y 3 ahora fijan `glFrontFace()` explícitamente.

**RENDERDONE (bits 0-2 de SB_ISTNRM) lo levanta `cb_renderstart()` — el STARTRENDER del guest —
no `TA_LIST_INIT`.** En el chip esos bits son consecuencia del strobe; dcemu los levantaba cuando
se inicializaba una lista, cosa que KOS nunca distingue (espera después de su propio STARTRENDER
y el evento llegaba igual — el dibujo GL sigue ocurriendo en el siguiente `TA_LIST_INIT`, que
presenta la escena acumulada) pero el ddraw de Windows CE sí: recibía un "render terminado" que
nunca pidió, antes de su primer vértice, y su máquina de estados de interrupciones descarrilaba.
Solo se movió el momento del EVENT; el pipeline de render está igual.

**Un `TA_LIST_INIT` que llega sin nada registrado desde el anterior no presenta.** dcemu usa el
init de lista como frontera de cuadro — las tiras acumuladas se dibujan y se presentan ahí — y eso
vale mientras haya uno por cuadro, que es lo que hace KOS. **Katana escribe dos**, seguidos y sin
geometría en medio, así que el segundo limpiaba la pantalla y presentaba con cero tiras: **un
cuadro de cada dos salía negro, y eso es el parpadeo**. Medido en Capcom vs. SNK: 607 STARTRENDER
contra 1216 `TA_LIST_INIT` en 12 s, con las cuentas de tiras por escena alternando 208, 0, 208, 0.
Virtua Tennis y Virtua Tenis 2 lo tenían también — sus 1148 y 1110 escenas documentadas son
exactamente los 573 + 575 y 554 + 556 de hoy — y Crazy Taxi no (7 inits vacíos en una corrida
entera), así que el mismo SDK llega al chip de las dos formas.

El discriminante es `pvr_listdone`, **no** `strip_count`: una escena deliberadamente vacía igual
abre y cierra su lista (abrir una y no enviar nada es un error de hardware, que es por lo que
`pvr_list_finish()` de KOS siempre manda un encabezado en blanco), así que igual presenta y igual
sale negra — el boot ROM manda exactamente esas. Inicializar un TA sin nada registrado es un
no-op en el chip, así que lo es aquí. El salto conserva lo que le pertenece al TA (la mitad
colgante de un parámetro de 64 bytes y el puntero de escritura ISP/TSP) y descarta solo el
limpiar/dibujar/presentar. `--traza-mem` los cuenta en el resumen de salida. Todo el conjunto de
control de diez demos queda byte a byte idéntico.

**La lista actual la fija el PRIMER parámetro global después de `TA_LIST_INIT` o después de un
fin de lista; el campo de tipo de lista de todo encabezado posterior se ignora hasta el siguiente
fin** (doc de Sega §3.7.4.1 — un tipo de lista a la vez, y el fin emite el evento de esa lista).
Tomarlo de cada encabezado parecía equivalente — las demos y los otros tres juegos siempre mandan
el campo coherente con la lista abierta — hasta Virtua Tennis 2: dentro de su lista translúcida
envía un encabezado de sprite con el campo en 0, el "cambio" hacía que su fin de lista cerrara
una lista ya cerrada, la translúcida quedaba abierta para siempre, el evento 9 nunca disparaba y
la máquina de operación del juego esperaba sin fin en su pantalla de título. `taPolyModifier()`
fija la lista solo cuando no hay ninguna abierta, la tira cae en la lista ABIERTA como en el chip,
y un encabezado que no coincide deja una línea de traza por corrida.

**`FB_R_SOF1` lee de vuelta lo que se le escribió** (`pvr_fb_r_sof1`, ya que `PVR_WRITE_CB_1`
consume la escritura antes del almacén de respaldo `control_mem`). Un `0x00100203` hardcodeado en
2005 envenenaba el flip de Windows CE — ddhal hace lectura-modificación-escritura del registro,
así que reescribía la constante cada cuadro y la pantalla nunca apuntaba a una superficie
dibujada. El doc del DevBox lo lista RW, dirección en unidades de 32 bits en los bits 23-2, bits
1-0 cableados a 00.

`taListEnd()` levantando una interrupción de fin de lista solo `if (pvr_registering != -1)`
resultó **no** ser un problema en la práctica: `pvr_list_finish()` siempre envía un encabezado de
polígono en blanco con el tipo de lista correcto antes del marcador de fin de lista, precisamente
porque abrir una lista y no enviar nada es un error de hardware. El marcador en sí son 32 bytes en
cero, así que su propio campo de tipo de lista es inútil y `pvr_registering` es la única fuente.

**`pvr_prim: attempt to submit to unopened list` no es un bug de dcemu** — estuvo listado como uno
por un tiempo. Es estado del guest de punta a punta: `pvr_list_begin()` fija
`pvr_state.list_reg_open`, `pvr_list_finish()` lo limpia, y `pvr_prim()` avisa cuando está en
`PVR_LIST_NONE`. Nada que haga el emulador puede fijarlo. Dos mediciones lo zanjan: de 31 demos
con logs seriales solo `tunnel` lo emite (278674 veces en 8 s), y nunca emite el compañero
`pvr_list_begin: attempt to open already closed list` — así que el guest está enviando sin
ninguna lista abierta. `tunnel` es la demo de KGL que se restauró y se portó aquí, y su propio
fuente documenta el cambio de API que lo causa (el KGL actual abre la lista de forma perezosa
desde el estado GL y no tiene `glKosFinishList`). El sospechoso antes de eso era el parámetro de
64 bytes cuya segunda mitad decodificaba como fin de lista — ver `ta.c` — pero arreglar eso no
cambió nada, que es lo que llevó a medirlo de verdad.

Nota: `pvr_registered` es `DWORD` en `graficos.c` pero `extern int` en `intc.c`.

El contexto GL además reporta sus bits de alfa **reales** (`GL_ALPHA_BITS`) al lado de los
pedidos: pedir `SDL_GL_ALPHA_SIZE` no garantiza un canal alfa de destino, y sin uno GL contesta
1.0 en silencio para `GL_DST_ALPHA` y descarta lo que se le escriba.

---

## Los ocho factores de mezcla son dos tablas, no una

Los códigos son 0 Zero, 1 One, 2 "Other Color", 3 Inverse "Other Color", 4 SRC Alpha, 5 Inverse
SRC Alpha, 6 DST Alpha, 7 Inverse DST Alpha. Los últimos cuatro nombran su operando de forma
absoluta, así que se leen igual de cualquier lado; **el 2 y el 3 no** — "el otro color" es el del
*destino* cuando es el factor de origen y el del *origen* cuando es el factor de destino.

Una sola tabla compartida le daba al destino `GL_DST_COLOR` donde va `GL_SRC_COLOR`, lo que
convierte cualquier receta `dst × algo` en `dst × dst`: un oscurecimiento uniforme de todo el
polígono, con la forma del polígono. Eso es lo que dibujaba las sombras de los jugadores de Virtua
Tenis 2 como trapecios negros opacos sobre la cancha — los códigos (3,3) con un origen negro se
componen a "dejar el destino en paz", y el mapeo equivocado los hacía `dst·(1−dst)`, o sea media
luminosidad en todo lo que el quad cubría. Arreglarlo cambia exactamente esos 35836 píxeles del
cuadro y nada más, y ninguna demo de KOS usa los dos códigos (las diez del conjunto de control
quedan byte a byte idénticas) — otro que solo muestra un juego.

Las sombras ahora dibujan *nada*, que es lo correcto para lo que esas tiras llevan y sigue sin ser
lo que muestra la consola: el oscurecimiento tiene que venir de algún lado que dcemu descarta. No
es un volumen modificador (medido: `DCEMU_SIN_VOLUMEN=1` deja la escena byte a byte idéntica). Los
dos sospechosos abiertos son los bits del **buffer de acumulación secundario** del TSP (25 y 24),
que eligen ese buffer en vez del framebuffer como operando de mezcla y que dcemu solo registra, y
el color de cara de un vértice en modo intensidad, ya que esas tiras llegan negro puro con solo el
alfa por vértice variando (0.00, 0.11, 0.15).

## El Offset Color es el color secundario de GL

La Texture/Shading Instruction lo suma *después* de combinar el texel con el color base —
`PIXRGB = COLRGB × TEXRGB + OFFSETRGB` en los cuatro modos (DevBox, la tabla de Texture/Shading
Instruction) — que es exactamente `GL_COLOR_SUM` con `glSecondaryColorPointer`, y no hay manera de
plegarlo en el color del vértice: `(COL+OFF) × TEX` no es `COL × TEX + OFF` salvo que la textura
sea blanca.

El punto de entrada es de GL 1.4, así que en Windows viene de `SDL_GL_GetProcAddress` (opengl32.dll
solo exporta 1.1 y el ICD sirve el resto); si falta se salta el offset y `--traza-mem` lo dice una
vez. Se interpreta para todo tipo de vértice texturado, incluidos los de dos volúmenes y las
variantes de intensidad — esas llevan una *segunda* intensidad que multiplica el **color de cara
de offset** del encabezado, que solo trae un encabezado Type 2 (palabras 12-15; un Type 4 pone ahí
el color de cara del otro volumen). Nada del conjunto de control de diez demos lo usa (todas byte a
byte idénticas); en Virtua Tenis 2 es lo que devuelve los brillos a la piel de los jugadores.
`usa_offset` (el bit Offset de la palabra ISP) lo habilita por tira, y la pasada de niebla lo
apaga — esa pasada dibuja el color de niebla y nada más.

---

## Sprites y el entorno de textura

**Un sprite es un rectángulo entero en un parámetro de 64 bytes** — cuatro esquinas de las cuales
la última se deriva completando el paralelogramo, D = A − B + C — y su color vive **en el
encabezado** (palabra 4), no en los vértices. `taSprite()` reutiliza `taPolyModifier()` porque las
palabras 1-3 significan lo mismo, y después recoge los colores base y de offset.
`vertice_sprite()` emite A, B, D, C, que como triangle strip da (A,B,D) y (B,D,C), es decir el
rectángulo. Ojo con la palabra 12: hay una palabra sin usar entre `Dy` y las UV, así que las tres
palabras de textura son la 13, la 14 y la 15 — leerlas una antes deja `u` en cero para las cuatro
esquinas y la textura se muestrea a lo largo de una línea.

**Un sprite es una primitiva completa y nunca encadena** — la tira cierra después de cada uno
aunque el parámetro no lleve el bit de fin de tira. Confiar en el bit costó los árboles de Crazy
Taxi: envía racimos de hojas como sprites sin fin de tira, dcemu los encadenaba en una sola tira,
y los triángulos puente entre las esquinas de un sprite y el siguiente eran rectángulos negros
detrás del follaje y polígonos gigantes cruzando el cielo (vértices a ±200000 píxeles).

### El entorno de textura nunca estuvo emulado

Todo recibía el `GL_MODULATE` por omisión de GL. El chip tiene cuatro modos en los bits 7-6 del
TSP: 0 decal, 1 modulate, **2 decal alpha**, 3 modulate alpha. Decal alpha es el 2, no el 0 —
confundirlos manda una superficie cuyo color de vértice es negro por modulate y sale negra, que es
lo que mantenía en blanco `pvr-bumpmap`.

**Y decal alpha no puede ser `GL_DECAL`, por el alfa.** El decal de GL saca el alfa del vértice
sin tocar; el juego depende de que el alfa de la **textura** llegue al mezclador. Crazy Taxi
dibuja todo el costado de cada auto de tráfico como un quad sobre un atlas ARGB4444 (carrocería,
ventanas y ruedas juntas, con un anillo de alfa 0 alrededor de la silueta): en hardware ese anillo
desaparece contra la calle, bajo `GL_DECAL` salía como píxeles opacos con el color del vértice —
un parche gris pegado a cada rueda. Ahora es `GL_COMBINE`, con RGB interpolando textura/vértice
por el alfa del texel (la mezcla del decal).

**El alfa de salida es una regla distinta en cada uno de los cuatro modos, y ahí estaba el segundo
bug.** La tabla del propio DevBox (p. 210) dice `PIXA = TEXA` para 0 y 1, `PIXA = COLA` para 2 y
`PIXA = COLA × TEXA` para 3 — tres reglas, no una — y dcemu emitía `TEXA × COLA` tanto para 1 como
para 2. **El modo 2 es el caro**: ahí el alfa del texel se *gasta* como factor de mezcla del RGB y
no llega al mezclador, así que reutilizarlo como opacidad deja translúcido todo lo que tenga alfa
parcial en su atlas. Así se pinta exactamente un vehículo de tráfico de Crazy Taxi, y el síntoma
era ver las ruedas del lado lejano **a través** de la carrocería, con la pintura lavada hacia la
calle de atrás. El modo 1 tiene el error simétrico y está mejor escondido: su alfa es solo el del
texel, y el alfa del vértice solo difiere de 1.0 cuando Use Alpha está encendido.

GL no tiene un token único para esto — el 0 es `GL_REPLACE` y el 3 es `GL_MODULATE`, pero el 1 y
el 2 necesitan `GL_COMBINE` para declarar RGB y alfa por separado. Medido: las diez demos de
control, Virtua Tennis, Virtua Tenis 2, Capcom vs. SNK y DCDoom quedan **byte a byte idénticos**;
solo cambia el tráfico de Crazy Taxi, y solo en cuadros de juego. Otra vez un camino que ninguna
demo ejercita.

### Punch-through

**Tenía `GL_LEQUAL` hardcodeado.** El buffer de profundidad se limpia a 0.0 y los valores z de una
escena caen alrededor de 0.5, así que "menor o igual" falla contra cualquier píxel sin tocar — la
lista prácticamente no podía dibujar nunca. Usa el modo de comparación de su propia palabra ISP,
como la lista opaca; lo que distingue al punch-through en el chip es que descarta por alfa — **y
ese descarte está implementado**: las tiras de la lista 4 dibujan con
`GL_ALPHA_TEST`/`GL_GEQUAL` contra `PT_ALPHA_REF` (`0x005F811C`, bits 7-0) **con un piso de medio
paso de 8 bits**, que es lo que deja a un cartel recortado escribir Z como la lista opaca. Sin la
prueba, el fondo de alfa 0 de los carteles de árboles de Crazy Taxi se rendía como cajas negras
sólidas. El alfa comparado es el modulado, así que una textura sin canal alfa igual corta por el
del vértice.

Se cometieron dos errores afinando esto, y ambos vale la pena conservarlos:

1. Un `GL_GREATER` **estricto** parece igual de plausible y rompe el mundo — la geometría en juego
   de Crazy Taxi es punch-through con alfa 1.0, y cualquier sesión donde el juego sube
   `PT_ALPHA_REF` a 255 descarta *todo* (la ciudad entera cayó a su respaldo sin texturas: calles
   y edificios blancos). `GEQUAL` con el piso epsilon pasa alfa ≥ ref y sigue matando el cero
   exacto.
2. Las píldoras grises detrás de los ítems del menú de Crazy Taxi parecían este bug y **no son un
   bug**: el hardware real las dibuja exactamente así (comprobado contra las capturas de Dreamcast
   de The King of Grabs) — hay que medir contra una referencia antes de perseguir una pantalla
   "mal". El juego deja `PT_ALPHA_REF` en 0 en algunas pantallas y escribe 0x17 en los menús, así
   que el registro no se puede suponer constante.

`conio-basic` (un quad punch-through por glifo) se mantiene en sus 2742 píxeles de referencia.

**TSP bit 20 es "Use Alpha" y solo fuerza el alfa del *vértice* a 1.0** — el alfa de textura sigue
vivo y la mezcla sigue encendida. Estaba cableado como el interruptor de mezcla de GL, y eso costó
los árboles también, la otra mitad: las hojas son texturas ARGB1555 VQ en la lista translúcida con
use-alpha apagado y factores srcalpha — el chip las mezcla (el alfa de textura recorta el fondo),
dcemu las dibujaba opacas y el fondo de alfa 0 salía como cajas negras. La mezcla ahora la decide
la lista — las listas translúcidas mezclan con los factores del TSP (ONE/ZERO es "sin mezcla" por
sí mismo), las opacas y punch-through nunca mezclan — y el bit se aplica donde corresponde, en los
constructores de color de vértice (`poly_usa_alfa`).

---

## Formatos de textura

`taPolyModifier()` mapea el `pixelformat` de la palabra de control de textura a un trío de formato
GL; `get_texture()` destuerce y sube. ARGB1555, RGB565, ARGB4444, YUV422 y los dos formatos de
paleta están soportados, y VQ y stride tienen sus propios caminos.

**Una textura con mipmaps guarda sus niveles desde 1×1 hacia arriba, así que el nivel grande NO
está en la dirección de la textura** — el nivel de lado 2^n empieza en `6 + 2·(4^n − 1)/3` bytes
en unidades de 16 bpp (la tabla del `pvrtex` de KOS, `MipMapOffset()`), escalado por el formato:
los índices VQ son un byte por bloque de 2×2 (un octavo, contado después del codebook, que se
queda en la base), la paleta de 4 bpp un cuarto, la de 8 bpp la mitad. El bit de mipmap del TCW
se interpretaba y solo se registraba; decodificar desde el offset 0 lee los niveles chicos como si
fueran la imagen, lo que sale como ruido en bloques estructurado. Eso era el teleférico, el suelo
y los edificios de Crazy Taxi — mientras el taxi, la calle y el HUD (sin mipmaps) decodificaban
bien, que es lo que apuntó al bit.

**BUMP: los texels no son un color**: son dos ángulos de 8 bits, elevación S en el byte alto y
rotación R en el bajo, que el chip combina por píxel con cuatro parámetros que lleva el color de
offset del polígono — K1, K2, K3 y Q — como `I = K1 + K2·sin(S) + K3·cos(S)·cos(R − Q)`. En el
chip esa intensidad después *modula* el polígono texturado de atrás, que es matemática por
fragmento que el GL de función fija no tiene. `decodificar_bump()` lo resuelve al subir y le
entrega a GL un gris. Eso es exacto mientras los parámetros vengan del encabezado — cierto para un
sprite, donde el color de offset vive ahí — y lo que se pierde es la combinación con la otra capa.

**`glTexParameteri` aplica a la textura que esté ligada, y los filtros se fijaban antes de
`glBindTexture`.** Así que caían sobre la textura del cuadro *anterior* y la nueva se quedaba con
los valores por omisión de GL — y el `GL_TEXTURE_MIN_FILTER` por omisión es
`GL_NEAREST_MIPMAP_LINEAR`, que **exige mipmaps**. Sin ellos la textura está incompleta y GL la
muestrea como blanco sólido. Una demo que dibuja muchos cuadros esconde esto (del segundo cuadro
en adelante la textura ya está ligada y sí recibe los parámetros); una que dibuja un solo cuadro y
después espera un botón salía enteramente blanca. Eso eran `pvr-yuv_converter-*` y
`pvr-strided_texture` — tres demos, una línea. `aplicar_filtros()` ahora corre dentro de
`get_texture()`, después de ambos binds (textura nueva y acierto de caché).

**Los modos de repetición por eje del TSP también están emulados: Clamp (bits 16/15) gana a Flip
(bits 18/17), y sin ninguno el chip repite.** Nada de eso estaba cableado, así que toda textura se
muestreaba con el `GL_REPEAT` por omisión de GL. Flip mapea a `GL_MIRRORED_REPEAT` — y importa
porque guardar **un cuarto** de una imagen simétrica y espejarla es como un juego arma toda una
mancha de sombra suave: las sombras de peatones de Crazy Taxi salían como ese cuarto de círculo
mosaicado cuatro veces sin espejar. Ninguna demo de KOS del conjunto de control usa ninguno de los
dos bits (las diez quedan byte a byte idénticas), así que un juego es otra vez la única prueba de
regresión.

**Stride** (bit 25) significa que las filas en memoria miden `TEXT_CONTROL & 0x1F` × 32 texels de
ancho en vez del `usize` declarado — es como se guarda una textura que no es potencia de dos, y el
tamaño declarado se redondea hacia arriba (640×480 se envía como 1024×512). `get_texture()` copia
fila por fila; entregarle a GL el bloque crudo sesga la imagen un poco más en cada fila.
`pvr-strided_texture` dibuja su tablero con los cuadros cuadrados y alineados, que es exactamente
lo que un stride equivocado arruinaría.

**YUV422** empaqueta dos píxeles en 32 bits — U, Y0, V, Y1 — compartiendo croma. GL no tiene ese
formato, así que `decodificar_yuv422()` convierte a RGB al subir, como las paletas. Es lo que
produce el convertidor YUV del TA.

### Los formatos de paleta no encajan con el resto del pipeline, de tres maneras

En `decodificar_paleta()`:

- **La paleta vive en registros, no en memoria de textura.** 1024 entradas en `0x005F9000` con su
  formato en `PAL_RAM_CTRL` (`0x005F8108`). Ambos ya tenían almacén de respaldo en `control_mem` —
  las escrituras del guest venían llegando todo el tiempo, nadie las leía.
- **El twiddling corre sobre índices de píxel, no sobre palabras de 16 bits.** El lazo existente
  indexa un `Uint16 *`; a 8 bpp eso es un byte y a 4 bpp la mitad de uno.
- **El selector de banco se superpone al bit de orden de barrido.** Son los bits 26-21 para 4 bpp
  y 26-25 para 8 bpp, encima de lo que los otros formatos usan como "sin usar", "stride" y "orden
  de barrido". Así que el bit 26 no significa nada aquí y leerlo como orden de barrido sale
  espejado: las texturas indexadas son siempre twiddled.

El decodificador le entrega a GL RGBA8888 plano. Los formatos del PVR son todos ARGB, así que R y
B se intercambian en el camino — GL_RGBA/GL_UNSIGNED_BYTE quiere R en el byte 0, que en
little-endian es `0xAABBGGRR`.

### El convertidor YUV del TA

Una entrada aparte del TA en `0x10800000` que toma YUV420 o YUV422 planar y deja una textura
YUV422 empaquetada en la RAM de video — cómo se sube video sin gastar CPU en la conversión. Se le
alimentan macrobloques de 16×16; el destino y el tamaño de imagen vienen de `TA_YUV_TEX_BASE`
(`0x005F8148`) y `TA_YUV_TEX_CTRL` (`0x005F814C`), y el chip cuenta lo convertido en
`TA_YUV_TEX_CNT` (`0x005F8150`).

Nada de eso estaba emulado: la zona `0x10` iba entera a `ta_write()`, que guarda 64 bytes para la
FIFO de polígonos, así que los macrobloques se descartaban. `pref142()` ahora separa los dos — la
FIFO de polígonos es `0x10000000-0x107FFFFF` y el convertidor de `0x10800000` para arriba — y
`pvr_yuv_bloque()` en `graficos.c` hace el trabajo.

**El orden dentro de un macrobloque no es "todo U, todo V, todo Y"**: va en mitades de 16×8, cada
una con su propio U, V e Y. YUV420 tiene una pasada de croma para las 16 filas y el macrobloque
mide 384 bytes; YUV422 tiene una por mitad y mide 512. Escribir `TA_YUV_TEX_BASE` o
`TA_YUV_TEX_CTRL` reinicia la cuenta de macrobloques — el chip está empezando otra imagen.

---

## La caché de texturas

Llavea por (tamaño, dirección, bpp, banco): los mismos índices con otra paleta son otra textura, y
`pvr-palette-wormhole` anima exactamente eso.

**Tenía 10 entradas, vivía una escena, y no tenía comprobación de límites** — `get_texture()`
escribía `cached_textures[cur_tex_count]` sin comprobar, así que la undécima textura distinta de
una escena escribía más allá de ambos arreglos y ligaba ids GL basura, que en el perfil de
compatibilidad crean en silencio objetos de textura nuevos que se solapan entre sí. Ninguna demo
de KOS pasa de un puñado; una escena de juego usa cientos, y el síntoma era el piso de Crazy Taxi
muestreando el cielo y texturas *rotando* entre objetos a medida que las subidas caían unas sobre
otras.

**Ahora es persistente: 1024 entradas que viven entre escenas e invalidan por generación, no
limpiando.** `vram.c` mantiene un contador de generación por página de 8 KB de RAM de video; los
dos embudos de escritura los incrementan (`vram64_escribir` se marca a sí misma, el camino plano
de 32 bits de `video_write` marca antes de guardar), `pvr_write` mantiene una generación de paleta
para `0x005F9000` y `PAL_RAM_CTRL`, y cada entrada de caché registra la suma de generaciones de su
huella (el rango exacto que juntó `vram64_leer`) más la generación de paleta cuando es indexada.
Una búsqueda que encuentra la llave compara generaciones: sin cambios → sirve la textura GL ya
subida; con cambios → se re-decodifica en la misma ranura. Las búsquedas van por un hash sobre la
dirección de la textura (`tex_hash[]`) — con 1024 entradas siempre llenas, el barrido lineal a
~2600 tiras por cuadro costaba más de lo que la caché ahorraba. Llena, reemplaza round-robin. La
copia en CPU se libera justo después de `glTexImage2D`: los píxeles son de GL. `pvr-fb_tex`
(muestrea su propio framebuffer, necesita invalidación por cuadro) y `pvr-palette-wormhole` (anima
la paleta) son las dos demos que prueban la invalidación, y ambas quedan byte a byte idénticas.

**Las texturas con mipmap suben los niveles del propio guest** — están ahí mismo en el bloque
juntado (el más chico primero, la tabla de offsets de pvrtex), son los del artista (los juegos
hornean trucos de LOD en ellos), y decodificarlos es más barato que `GL_GENERATE_MIPMAP`
regenerando en cada resubida de una textura en streaming: el attract de Crazy Taxi, el peor caso,
mide *más rápido* que la línea base sin mipmaps. Tres decodificadores por nivel viven en
`get_texture()` (VQ por el codebook, twiddled de 16 bpp, paleta); BUMP e YUV se quedan con
`GL_GENERATE_MIPMAP`. **Una cadena VQ para en 2×2** — el índice de 1×1 comparte un byte con ella —
así que `GL_TEXTURE_MAX_LEVEL` recorta la cadena; sin el recorte la textura está incompleta y GL
la muestrea blanca. El filtro MIN elige modos de mipmap en `aplicar_filtros()` cuando la textura
de la tira lleva el bit.

**Los niveles chicos se decodifican desde `plano` — la cadena juntada — así que `plano` tiene que
sobrevivir al lazo de niveles.** `free(plano)` estaba justo después de decodificar el nivel 0,
antes de ese lazo: un uso después de liberar donde el `malloc` de cada nivel reciclaba el bloque
liberado, así que cada nivel chico salía como basura estructurada mientras el nivel 0 seguía
correcto. En pantalla eso era corrupción dependiente de la distancia que una auditoría de código
no encontraba porque cada offset cuadraba contra la tabla de pvrtex: las palmeras lejanas de Crazy
Taxi se rendían como triángulos invertidos con dithering (magenta o turquesa según la textura) y
el tráfico a media distancia salía lavado, mientras todo lo cercano se veía bien.
`DCEMU_SIN_FILTRO_MIP` es lo que lo separó — el artefacto desaparecía sin filtrado mip — y el free
ahora corre después de subir la cadena. Ninguna demo de KOS usa el bit de mipmap, así que el
barrido de demos no puede atrapar nunca una regresión aquí: tiene que hacerlo un juego.

---

## Las dos ventanas de RAM de video

**El PVR ve los mismos 8 MB por dos ventanas que entrelazan sus dos bancos de 4 MB de manera
distinta.** El área de 32 bits (`0x05000000`/`0xA5000000`) los ve contiguos — el banco es el bit
22 — y es donde vive el framebuffer; el área de 64 bits (`0x04000000`/`0xA4000000`, más la FIFO de
textura del TA en `0x11000000`) los alterna cada 4 bytes — el banco es el bit 2 — y es de donde se
leen las texturas. El mismo byte, dos direcciones.

`vram.c/h` es dueño de la conversión (libre de SDL, así que `tests/` lo enlaza; suite `vram`), el
bloque se queda en numeración de 32 bits, y todo acceso por una ventana de 64 bits convierte:

- `video_read`/`video_write` (`mem.c`) despachan por el byte alto: las zonas `0x04`/`0x11`
  convierten, `0x05`/`0x13` quedan planas. Las subidas por cola de almacenamiento o CH2 DMA pasan
  por aquí, así que convierten solas.
- `get_texture()` junta la textura en un buffer de staging contiguo con `vram64_leer()` antes de
  decodificar — la dirección del TCW está en numeración de 64 bits. Un punto de inserción; ningún
  decodificador cambia.
- El render a textura **dispersa** con `vram64_escribir()`: el bit 24 de `FB_W_SOF1` significa
  "escribir por el camino de 64 bits" y KOS pasa la dirección de textura tal cual.
- El convertidor YUV del TA escribe su salida igual: `TA_YUV_TEX_BASE` es una dirección de
  textura.

`pvr-fb_tex` es la demo que forzó todo esto: muestrea su propio front buffer como textura (textura
en `0x0014E900` = FB en `0x004A7480`, exactamente ×2 con el banco en el bit 2), confiando en el
entrelazado del hardware para producir "dos píxeles correctos, dos basura" que reconstruye con una
máscara y dos pasadas DSTALPHA. Necesita las ventanas **y** la reescritura del framebuffer: dcemu
rinde 3D en OpenGL, así que el front buffer no existe en VRAM salvo que se escriba de vuelta. La
reescritura (`volcar_escena_a_framebuffer()`) la arma `get_texture()` en cuanto la dirección
convertida de una textura cae dentro del cuadro que el PVR escribe o muestra, y desde ahí cada
escena se lee de vuelta con `glReadPixels` y se guarda por la ventana de 32 bits — antes de eso no
cuesta nada, así que ninguna otra demo lo paga.

### Por qué ventana escribe el CH2 DMA no lo implica la dirección

Lo elige el guest con `SB_LMMODE0`/`SB_LMMODE1`. Lo zanja el propio *Dreamcast/Dev.Box System
Architecture* de Sega, §8.4.1.1: la dirección en `SB_C2DSTAT` nombra el **camino** (`0x10000000`
polígonos, `0x10800000` convertidor YUV, `0x11000000` textura directa, con `0x12`/`0x13` como sus
imágenes), y de ese último dice: *"When transferring data to the texture memory via the TA FIFO
buffer and Direct Texture Path, either 64-bit access or 32-bit access can be specified by setting
the SB_LMMODE0 and 1 registers."*

`SB_LMMODE0` (`0x005F6884`) gobierna `0x11000000-0x11FFFFFF` y `SB_LMMODE1` (`0x005F6888`) su
imagen en `0x13000000`; el bit 0 es **0 = 64 bits (por omisión), 1 = 32 bits**. Ambos ya tenían
almacén de respaldo en `control_mem` — las escrituras del guest venían llegando todo el tiempo,
nadie las leía.

Ese solo registro explica dos mediciones que no podrían ser ambas ciertas de otra manera. mame4all
vuelca cuadros enteros a `0x11000000` — 275 transferencias de 614400 bytes en seis segundos, sin
tocar el TA ni una vez — y los muestra como framebuffer, que se lee en numeración de 32 bits: pone
`SB_LMMODE0` en 1 y necesita la escritura plana, o el cuadro se parte entre los bancos y sale
duplicado a lo ancho y aplastado a la mitad de alto. El boot ROM sube sus **texturas** por ese
mismo `0x11000000` — `0x11413000`, `0x1141b000`, `0x1151b000`, de 8 KB a 1 MB cada una — con
`SB_LMMODE0` en su 0 por omisión, y las necesita entrelazadas, porque `get_texture()` lee con
`vram64_leer()`. Decidir por dirección en vez de por el registro le costó al boot ROM todos los
glifos de su menú y su panel de fecha por un tiempo; ver `docs/pendientes-plan.md`, C.7.

**La cola de almacenamiento es otro camino y siempre entrelaza**: `pvr-strided_texture` sube su
textura por la misma ventana en ráfagas de 32 bytes y depende de ello — forzar ese camino a plano
la lleva a negro, de 240000 píxeles no negros a cero. Nota que la misma frase del documento dice
"via the TA FIFO buffer", por donde también pasan las escrituras de cola de almacenamiento a
`0x11000000`, así que `SB_LMMODE0` bien podría gobernarlas también; dcemu cablea el entrelazado
ahí, que es lo que `SB_LMMODE0 = 0` — el valor por omisión, y lo que KOS deja — daría igual. Sin
probar en ninguno de los dos sentidos.

**`0x06` y `0x07` son áreas imagen de `0x04` y `0x05`**, por la tabla 2-2 del mismo documento
("the addresses shown in parentheses are an image area"). Estaban en `mem_zone[]` como alias todo
el tiempo pero faltaban en `mem_hash_read`/`mem_hash_write`, así que un guest que las usara caía
en `mem_read_error`. Ambas están ligadas ahora, con sus formas P2, y `0x06` está en
`VRAM_VENTANA_64()` porque hace imagen de la ventana de 64 bits. Esa tabla vale la pena leerla
como lista de comprobación: las imágenes del boot ROM (`0x02`), de la FIFO de polígonos (`0x12`) y
del convertidor YUV (`0x12800000`) siguen sin ligar, y `0x01000000` — que es el **área externa
G2**, o sea un dispositivo de expansión que una consola de tienda no tiene — explica las sondas de
`0xA1000400`-`0xA1001800` que hace Virtua Tenis 2.

---

## Render a textura

`cb_tastart()` se parte en tres: `render_a_textura()` decide a dónde va la escena,
`dibujar_escena()` dibuja las tiras y `terminar_escena()` presenta. **El marcador es el bit 24 de
`FB_W_SOF1`** (`0x005F8060`) — KOS escribe `address | BIT(24)`. El tamaño viene de los registros
de recorte (`PCLIP_X`/`Y`, `0x005F8068`/`0x6C`, máximo en los bits 31-16), el paso de fila de
`FB_W_LINESTRIDE` (`0x005F804C`, en unidades de 8 bytes) y el formato de píxel de `FB_W_CTRL`
(`0x005F8048`).

**Esos cinco registros se enganchan en `STARTRENDER`, no se leen cuando dcemu dibuja** —
`regs_render_latchear()`, llamada como primera cosa en `cb_renderstart()`. El chip toma su
configuración de salida en el strobe; dcemu dibuja en el `TA_LIST_INIT` *siguiente*, un cuadro
después, y para entonces el guest los reprogramó para lo que venga. Crazy Taxi es lo que lo
expuso: para el fondo de su menú de pausa rinde la escena congelada a una textura de 512×480 con
`FB_W_LINESTRIDE` en 128 (1024 bytes = 512 píxeles) y lo restaura a 160 (640 píxeles) dos
milisegundos después, así que leído tarde el *ancho* venía de un instante y el *paso* del otro —
la textura se escribía con filas de 640 píxeles y se leía con filas de 512, y el fondo de pausa
era el último cuadro rebanado en bandas y repetido a lo ancho. El mismo error que ya había
cometido el plano de fondo, y vale la pena revisarlo cada vez que un registro solo importa durante
un cuadro. Nada más se mueve: las diez demos de control, `pvr-texture_render`,
`pvr-strided_texture` y los cuatro juegos quedan byte a byte idénticos, porque KOS y todos los
demás programan estos registros justo antes del strobe y los dejan en paz.

dcemu manda 3D a OpenGL, así que la escena nunca pasa por la RAM de video: hay que dibujarla y
leerla de vuelta con `glReadPixels`. Dos cosas son fáciles de equivocar — el guest envía vértices
**en las coordenadas del destino** (0..128 por 0..64 para `pvr_rtt_sized`), así que el viewport y
el `glOrtho` tienen que ser los de la textura y no los de la pantalla; y `glReadPixels` devuelve
de abajo hacia arriba mientras la textura se guarda de arriba hacia abajo.

**`pvr-texture_render` pasaba por accidente**: usa `pvr_scene_begin_rtt` también, y dcemu ignoraba
el destino y mandaba la escena destinada a la textura directo a la pantalla. Su cuenta de colores
cayó de 47539 a 2048 cuando esto entró — que es el viaje de ida y vuelta por una textura RGB565,
o sea el número *correcto*.

**No pidas `SDL_GL_DEPTH_SIZE`.** Pedir 24 junto con el stencil y `BUFFER_SIZE 32` hace que SDL
elija otro formato de píxel; el contexto concede 24 bits igual sin pedirlos.

---

## El plano de fondo

**El chip no limpia la pantalla a negro: dibuja un polígono de fondo.** `ISP_BACKGND_T`
(`0x005F808C`) lo apunta — tag en palabras sobre `PARAM_BASE` en los bits 23-3, skip en 26-24 — y
el polígono vive en la RAM de video (por la ventana de **32 bits**, medido contra el boot ROM):
3 palabras de encabezado, después tres vértices de 3+skip palabras cuya última palabra es el color
empaquetado. `color_de_fondo()` lee ese color y lo usa como color de limpieza, lo que cubre el
caso plano — el `pvr_set_bg_color()` de KOS y el `0xBFBFBF` del boot ROM.

La limpieza ocurre **al comienzo de la escena** (`cb_tastart`), porque el chip engancha su
configuración en STARTRENDER: muestrear al presentar la escena anterior caía a mitad de la
programación de registros del cuadro siguiente.

**El valor del registro hay que validarlo antes de creerle.** dcemu no escribe la salida del TA en
la VRAM, así que `TA_ITP_CURRENT` nunca avanza de verdad; KOS calcula `ISP_BACKGND_T` restando
contra ese puntero y en una de las dos paridades del doble buffer la resta se va a negativo —
`0xFF800000`, skip 7, bits altos puestos. En hardware ese cuadro dibuja un fondo basura escondido
detrás de la escena; aquí se ve el color de limpieza, así que un valor imposible conserva el
último color bueno. Sin esa comprobación medio parque de demos alternaba colores de fondo
aleatorios, un buffer sí y uno no. `libdream` nunca programa el plano (sus demos cubren la
pantalla con geometría), que la misma alternativa absorbe.

---

## Profundidad

**La z del TA es 1/w — más grande significa más cerca — y se guarda por `profundidad_ta()`, que es
monótona, así que el orden es el del chip.** Los modos de comparación del PVR están escritos en
esos términos (`GREATER` pasa lo que está más cerca), y el `glOrtho` de `screeninit()` mapea z de
ojo creciente a profundidad creciente, así que el orden sale bien sin más transformación.

**Ese mapeo exige near/far *invertidos* en `glOrtho` — `(RANGO, -RANGO)` — porque GL niega la z de
ojo** (`z' = -2z/(far-near)`). Con el orden de aspecto natural una z de vértice más grande salía
con un valor de profundidad *más chico*, así que `GL_GREATER` conservaba lo *más lejano*: una tira
cercana perdía contra una lejana ya dibujada. `pvr-fb_tex` lo midió — su máscara de pantalla
completa en z=1 dejaba invisible el cubo en z=4, 0 píxeles no negros en toda la escena — y los
marcadores de `pvr_rtt_sized` en z=3/z=4 perdiendo contra el interior en z=2 eran el mismo bug.
Ambas llamadas a `glOrtho` (pantalla y RTT) llevan la inversión.

**Alimentar 1/w a `glOrtho` linealmente tira precisión exactamente donde vive un juego.** El chip
compara 1/w en punto flotante, con resolución concentrada cerca de cero; un rango ortográfico
lineal sobre un buffer de profundidad entero de 24 bits da un paso por `rango/2^24`. La 1/w cruda
de Crazy Taxi abarca **0.01..1000** (ahora impresa al salir por `--traza-mem`), así que con el
viejo rango de ±32768 el paso era 0.0039 y toda la ciudad lejana (z 0.01..0.1) cabía en 23 pasos:
paredes vecinas caían en el mismo valor de profundidad y, enviadas con `GEQUAL`, ganaba la que
dibujara *después* — las calles desaparecían dejando cielo, los edificios se caían según el ángulo
de cámara.

`profundidad_ta()` guarda `log2(1+z)` en su lugar (rango ±32, `PROFUNDIDAD_RANGO`), que reparte
los pasos del buffer en proporción al valor — el mismo par de paredes ahora queda a unos 75 pasos.
Monótona, así que todo modo de comparación y todo lo anterior sigue valiendo; z = 0
(infinitamente lejos) sigue siendo 0, que es también el `glClearDepth`. Los sprites y los
triángulos de volumen modificador pasan por la misma función — los volúmenes se comparan contra
las profundidades de la escena. El costo: GL interpola profundidad linealmente en espacio de
pantalla y el mapa no es lineal, así que polígonos largos en profundidad se arquean levemente
contra planos exactos; los bordes de intersección se pueden mover un píxel. Los quads 2D fijos
(`DibujarFramebuffer`, `DibujarGL`) dibujan con la prueba de profundidad apagada, así que su z
solo importa para el recorte — dentro de ±RANGO.

Antes guardaba el **recíproco**, que lo invierte. Dos capas sobreviven a eso — la de arriba gana
igual porque la de abajo nunca escribió — pero tres no, y el resultado es que solo la primera es
visible. `pvr_rtt_sized` dibuja un fondo, un interior, dos marcadores y bordes en cinco niveles de
z y mostraba solo el fondo. `libdream-ta` pasó de 43333 colores a 65227 con el mismo cambio, y las
esferas de `parallax-serpent_dma` empezaron a ocluirse bien.

**z = 0 es legal y significa infinitamente lejos**, que el recíproco convertía en infinito — y
entonces GL recorta el vértice de plano. `pvr_rtt_sized` envía su rectángulo de fondo con
exactamente z = 0.

**El `glClear` del buffer de profundidad lo enmascara `glDepthMask`.** Una tira con "Z Write
Disable" puesto — que la geometría translúcida siempre tiene — deja la máscara en `GL_FALSE`, y
entonces la limpieza no hace nada y la escena siguiente parte con las profundidades de la
anterior. `limpiar_pantalla()` y la pasada RTT fijan la máscara antes de limpiar.

---

## Niebla

**La niebla por tabla está emulada; la niebla por vértice no.** Los bits 23-22 del TSP eligen el
modo (0 tabla, 1 vértice, 2 ninguna, 3 tabla 2); el guest escribe la densidad en `FOG_DENSITY`
(`0x005F80B8`, un float de 16 bits: mantisa 1.m7 en los bits 14-8, exponente con signo en 7-0), el
color en `FOG_COL_TABLE` (`0x005F80B0`) y la curva de 128 entradas en `0x005F8200-0x005F83FC`.
Todo eso caía en `control_mem` sin lector — el agujero de siempre — que es por lo que `kgl-tunnel`
terminaba en un pozo negro en vez de desvanecerse en su niebla gris, escondiendo los arcos y
pilares que sus paredes sí tienen.

El índice de la tabla es `densidad × (1/w)` acotado a `[1, 256)` — exponente en los bits altos de
la ranura, mantisa de 4 bits debajo — y cada palabra lleva el alfa del borde lejano en el byte
alto y el del cercano en el bajo, interpolados por la fracción (KOS la llena en
`pvr_fog_table_exp2()` y compañía).

`dibujar_niebla_tira()` evalúa eso por **vértice** — desde la misma `q` que guarda la corrección
de perspectiva — y dibuja la tira una segunda vez, sin textura, mezclada hacia el color de niebla.
La pasada reutiliza la profundidad que la tira acaba de dejar: `GL_EQUAL` si escribió z, la
comparación propia de la tira si no (pasa exactamente donde pasó la original), y nunca escribe el
buffer. Una tabla nunca escrita es todo ceros, así que la pasada se salta sola y no les cuesta
nada a las otras demos. El chip aplica niebla por píxel; por vértice difiere solo dentro de
triángulos grandes.

---

## Volúmenes modificadores

El PVR decide por píxel si está dentro del volumen y elige entre los dos juegos de parámetros del
polígono. En GL de función fija eso es el **buffer de stencil**, que hay que pedirlo
(`SDL_GL_STENCIL_SIZE`) — el contexto no viene con ninguno.

Dos mecanismos, y el encabezado dice cuál: el bit **Volume** del PCW significa que el vértice
lleva dos juegos de parámetros y el juego 1 aplica adentro; el bit **Shadow** significa sombra
barata — un solo juego cuya intensidad escala `FPU_SHAD_SCALE` (`0x005F8074`, factor en los bits
7-0, enable en el bit 8). `cheap_shadow` pide 0.5 y el azul de adentro sale `0x7F`, que es como se
sabe que funciona.

**El adentro se decide contando caras contra la profundidad de la escena, como hace el chip.** La
superficie de un píxel está dentro del volumen cuando las caras del volumen más cercanas que ella
(prueba de profundidad `GL_GREATER`) no se cancelan: las caras frontales incrementan el stencil,
las traseras decrementan (`GL_INCR_WRAP`/`GL_DECR_WRAP` — acotar rompería un par cuya cara trasera
se rasteriza primero), y adentro = cuenta ≠ 0. Probar ≠ 0 además hace irrelevante la convención de
sentido de giro: en un volumen cerrado los cruces se cancelan de a pares, en uno abierto (el
cuadrado plano de las demos de KOS) deja ±1.

Antes era una **unión en espacio de pantalla** de triángulos sin ninguna profundidad — suficiente
para ese cuadrado plano, pero las sombras extruidas de los autos de Crazy Taxi marcaban todo lo
que sus caras cubrían: el techo del taxi oscurecido por su propia sombra y una manta sobre media
pantalla. `pvr-modifier_volume_zclip` — la única demo de KOS con un volumen 3D genuinamente
cerrado, un cubo que gira — es lo que muestra la diferencia: su oscurecimiento ahora abraza el
suelo y la pared que interseca. La instrucción 2 ("cerrar excluyendo") conserva la vieja
aproximación — pone en cero lo que cubre, ahora solo delante de la superficie — porque nada de lo
que corremos la ejercita.

Contar contra profundidad fuerza el orden: la profundidad tiene que estar resuelta **antes** de
marcar, así que `dibujar_escena()` corre por fases — primero las tiras opacas y punch-through con
el juego 0 (eso escribe z), después se cuenta el volumen de la lista 1 y las tiras afectadas se
**superponen** con el juego 1 donde la cuenta dice adentro (`GL_EQUAL` contra la profundidad que
la tira misma dejó; seguro porque esas listas nunca mezclan), después se limpia el stencil y se
recuenta el volumen de la lista 3 para la fase translúcida — una clase de volumen es dueña de todo
el stencil de 8 bits a la vez. Las tiras translúcidas afectadas igual se **dibujan dos veces con
la prueba de stencil invertida** — afuera con el juego 0, adentro con el juego 1 — porque con
mezcla alfa cada píxel tiene que escribirse exactamente una vez: superponer mezclaría dos veces.
El estado GL por tira vive en `tira_estado()` para que la pasada de superposición pueda repetirlo.

**Al medir esas demos, ojo con que colocan su geometría con `rand()`**, así que el volumen se
solapa con un polígono en una corrida y no en la siguiente. Un BMP de dos colores no prueba nada;
hay que correrlas varias veces.

---

## El orden de dibujo

`cb_tastart()` ordena las tiras para que la geometría translúcida se dibuje al final, fija el
estado GL por tira, sube/liga texturas por `get_texture()` y emite
`glDrawArrays(GL_TRIANGLE_STRIP, ...)`.

**Ese orden tiene que ser estable y no lo era.** `qsort` no lo es, así que dos tiras del mismo
tipo de lista salían en un orden que depende del estado interno del algoritmo — distinto de un
cuadro al siguiente aun con la escena idéntica. A la geometría opaca no le importa, decide la
prueba de profundidad; la translúcida dibuja con la escritura de Z apagada y el orden *es* el
resultado, así que el mismo cuadro salía distinto cada vez y la pantalla parpadeaba. `compare()`
ahora rompe el empate con `index`, que solo crece dentro de un cuadro, así que los empates se
resuelven en orden de envío — que es también lo que hace el chip dentro de una lista.

**Dentro de la lista translúcida el chip ordena por profundidad por píxel — autosort — y el orden
de envío no significa nada.** `compare()` lo aproxima por tira: la z más cercana de cada tira, de
lejos a cerca, salvo que el guest haya puesto el bit 0 de `ISP_FEED_CFG` (modo pre-sort), en cuyo
caso el orden de envío es el contrato. Medido en el menú de Crazy Taxi: la píldora (alfa 0.79,
z 0.99) entra a la lista *antes* que el logo de llama que está detrás (alfa 0.49, z 0.014), y en
orden de envío la llama se mezclaba encima de la píldora — toda translucidez apilada salía con las
capas compuestas al revés. Por tira es una aproximación: geometría translúcida que se interpenetra
igual puede ordenarse mal donde por píxel no lo haría.

**Las tiras con cero vértices se saltan al dibujar, y ese salto sostiene el peso** — los
encabezados de sombra de un juego dejan cientos de registros vacíos de fin de tira por escena. No
dibujaban nada pero pagaban toda la agitación de estado GL, que es por lo que entró el salto; lo
que se encontró después es que dejar pasar una **tumba el proceso**.
`glDrawArrays(GL_TRIANGLE_STRIP, first, 0)` falla dentro del ICD, leyendo más allá del final de
`VertexBuffer[]`: el driver calcula el rango como `first..first+count-1` y con `count` 0 eso da la
vuelta. Reproducible; `traza_caida_instalar()` es lo que lo hizo legible.

---

## Costo del camino gráfico

**7,6% de una corrida, y ese es el techo de cualquier cosa que quede ahí.** Medido sobre Crazy
Taxi en movimiento (1183 tiras/escena, 180 segundos emulados, Release): todo `cb_tastart()` es
6,0% — ordenar tiras 0,6, `dibujar_escena()` 5,0, de lo cual texturas 0,8 — y la decodificación de
colas de almacenamiento del TA otro 1,6%. Para comparar, el ARM7 del AICA solo es 14,2% y el
intérprete SH-4 72,9%.

**`DCEMU_SIN_DIBUJO=1` se salta el `glDrawArrays` y nada más** (la escena queda negra a propósito;
el guest nunca lee de vuelta lo que rasterizó GL, así que la ejecución es idéntica y eso se
verifica antes de comparar): los 9,5 millones de llamadas de dibujo valen **1,7%**, que es el
techo sumado de un VBO y de agrupar tiras — ambos descartados ahora por medición, no por juicio.
`docs/rendimiento-plan.md`, "La fase 3 se cierra", tiene los números y las dos hipótesis que
murieron con ellos.

---

## `glops.c` es un camino viejo y sin usar

Una lista de despliegue grabada de comandos `GLOP_*` reproducida por `glop_process()`.
`graficos.c` tiene su `#include "glops.h"` comentado y llama a OpenGL directamente; `glops.c`
igual se compila y se enlaza. No supongas que un cambio ahí afecta el render.

`DibujarFramebuffer()` maneja el caso 2D, subiendo la RAM de video del PVR como textura sobre un
quad del tamaño de la pantalla (formatos `FRAMEBUFFER_*`).
