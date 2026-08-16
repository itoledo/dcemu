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
es un volumen modificador (medido: `DCEMU_SIN_VOLUMEN=1` deja la escena byte a byte idéntica).

**Los dos sospechosos que quedaban están descartados, los dos por medición (2026-08-06).**

- El **buffer de acumulación secundario** del TSP (bits 25 y 24). Censado: sobre 1,16 millones de
  tiras de Crazy Taxi en juego, las doce demos de control y los nueve juegos, **no hay una sola tira
  que lo seleccione** — y Virtua Tennis 2 menos que ninguna, con sus 5581 tiras todas en 0/0. Está
  implementado igual (ver más abajo), así que si algún día aparece una que lo pida, funciona.
- El **color de cara en modo intensidad**. Ahí el censo dice lo contrario y por eso era el candidato
  fuerte: Virtua Tennis 2 manda **826 414 encabezados en modo intensidad contra 56 576
  empaquetados**, el 64 % de todo lo que dibuja. Pero el camino está bien: `demos/intensidad/`
  compara el mismo dibujo mandado empaquetado y mandado como color de cara más intensidad, y salen
  **byte a byte iguales**, tanto el RGB como la regla del alfa —que sale del color de cara y es
  constante en el polígono, no de la intensidad—.

O sea que el oscurecimiento no viene de ninguno de los tres sitios que se habían anotado, y la
pregunta vuelve a estar abierta sin candidato. Lo que sí quedó del intento son tres cosas
utilizables: las dos demos que fabrican el contenido que faltaba, el censo de tipos de color y de
bits del TSP que se informa en el resumen de `--traza-mem`, y la certeza de que las tiras de sombra
—las 53 de una escena de partido, con `SRC_ALPHA/INV_SRC_ALPHA` y negro al 0.40 sobre la cancha— sí
dibujan sombra hoy. La nota anterior es de antes de arreglar la tabla de mezcla y el color de
offset.

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

Con `--render=shader` la suma la hace el fragment shader desde `gl_SecondaryColor`; el arreglo de
cliente se sigue encendiendo y apagando igual, porque es de donde sale esa variable. Lo que deja
de tener efecto es `GL_COLOR_SUM`, que no hace nada con un programa puesto.

---

## El camino programable: los cuatro modos escritos como son

`--render=shader` reemplaza `GL_COMBINE`, `glAlphaFunc` y `GL_COLOR_SUM` por un par de shaders en
GLSL 1.20 de compatibilidad (`glmoderno.c`). La tabla del DevBox entra tal cual en cuatro líneas,
que es la ganancia real aunque no se vea: **el alfa de salida es una regla distinta en cada modo**,
y decirlo con el entorno de textura costaba hasta nueve `glTexEnvi` seguidos porque los modos 1 y 2
no se pueden expresar sin `COMBINE`.

Los uniformes cuelgan de la **sombra de estado**, no del bucle de dibujo: `gl_textura()`,
`gl_alpha_test()`, `offset_estado()` y el `switch` del entorno los llevan al día. Así el shader y
la función fija no pueden discrepar sobre qué estado está puesto. Lo que toque
`GL_TEXTURE_2D`/`GL_ALPHA_TEST` a mano tiene que pasar por esas funciones o el uniforme miente.

Dos cosas que hay que respetar, y que no avisan si se rompen:

- **`marcar_volumenes()` saca el programa.** Manda triángulos por `glBegin/glEnd` con sólo la
  posición, así que el color y las UV que le llegarían al shader son el estado actual de GL. El
  color no importa —se escribe con la máscara cerrada— pero un `discard` por un uniforme viejo
  dejaría la plantilla a medio marcar.
- **Con el programa puesto, `GL_ALPHA_TEST` queda apagado.** El descarte por alfa es una operación
  por fragmento *posterior* al shader, así que en un contexto de compatibilidad se aplica encima y
  de las dos reglas gana la más estricta: la aproximación. La regla exacta del punch-through —«alfa
  ≥ umbral **y** distinto de cero», que `glAlphaFunc` no sabe decir— no se habría notado nunca.

Las ocho demos de PVR de control salen byte a byte idénticas entre `--render=fbo` y
`--render=shader`, incluidas `pvr-texture_render`, `pvr-fb_tex` y `pvr-modifier_volume_zclip`.

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

**Con `--render=shader` la textura sube con los dos ángulos crudos** (S en el canal R, R en el G) y
la intensidad se evalúa por píxel. La fórmula es la misma, y en `pvr-bumpmap` las dos versiones
coinciden **con una diferencia máxima de 1 nivel**, que es el redondeo de hornear a bytes contra
calcular en float. Lo que arregla es otra cosa: **K1..K3 y Q no son de la textura sino del
polígono**, y la caché se indexa por dirección, así que dos polígonos que comparten mapa de relieve
con parámetros distintos —una misma pared con dos luces— recibían los dos la intensidad del primero
que la subió, sin que nada lo delatara. Con los ángulos crudos los parámetros viajan por uniforme y
el problema desaparece por construcción; por eso `gl_bump()` compara también los parámetros, no
sólo el encendido. Lo que sigue faltando es la combinación con la otra capa, que es arquitectura y
no shader.

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

#### Una tira cuyas UV no salen de [0,1] no está pidiendo repetición

**Es el arreglo de la costura del logo de Crazy Taxi**, y todo el mecanismo está en lo que manda el
guest. Esa pantalla —la primera, donde aparece el aviso de la VMU— son cuadros de 16×16 pegados
borde con borde (`x = 639,9 / 655,9 / 671,9 …`), cada uno con su textura de 16×16 y UV **exactamente
0..1**, `tsp=208824c9`: sin Clamp y sin Flip, o sea `GL_REPEAT`, bilineal. A 1:1 eso es exacto —16
texeles sobre 16 píxeles, sin mezcla—, pero el camino de ventana estira 640 sobre 800, cada cuadro
pasa a medir 20 píxeles y **en el borde de cada cuadro GL envuelve y trae el texel opuesto**: una
línea cada 20 píxeles. Eso es lo que se veía como huecos entre las baldosas del logo.

La regla que se aplica no toca ninguna UV —el intento anterior sí lo hacía y rompió Street Fighter
III—: al cerrar la tira se mide el rango de U y de V, y **si cabe en [0,1] la tira no puede repetir
nunca**, así que `GL_REPEAT` y `GL_CLAMP_TO_EDGE` sólo pueden diferir en el filtro del borde mismo.
Es seguro por construcción: una tira con UV 0..4 conserva REPEAT, y una de atlas con 0,2..0,8 no
llega al borde. Se aplica sólo donde el TSP no dijo nada (`wrap == 0`), así que no pisa un Clamp ni
un Flip declarados.

La medida tiene la forma que esa regla predice: de los 15 juegos **11 salen byte a byte iguales**
(SF3 entre ellos), los otros cuatro cambian donde deben —DCDoom y 4x4 EVO cambian **2796 píxeles
exactos**, que es el perímetro de 800×600 al píxel—, y de las 139 demos cambian 13 fuera del piso
de ruido, todas de textura a pantalla completa, sin un solo cambio de veredicto en `serial.txt`.
`DCEMU_SIN_CLAMP_BORDE=1` vuelve a la conducta anterior byte a byte.

**Un aviso sobre el 1:1.** La predicción antes de medir era que a `--render=fbo --escala=1` el
recorte fuera un no-op, porque ahí el muestreo caería en centros de texel y nunca llegaría al borde.
No es así: a 1:1 el recorte cambia la imagen, y con `DCEMU_SIN_MEDIO_PIXEL=1` deja de cambiarla. O
sea que el corrimiento del `glOrtho` es lo que pone la muestra sobre el borde del texel — la primera
señal de que el medio píxel mueve el muestreo de textura y no sólo la cobertura. Ver la sección del
medio píxel más abajo.

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

#### Cuatro cosas que faltaban, y el FMV de Dave Mirra las encontró a las cuatro

El juego abre con un video de 320×240 y no pasaba de ahí. Cada fallo tapaba al siguiente, así
que salieron de a uno.

**1. El CH2 DMA no conocía el convertidor.** `pref142()` despacha los tres destinos de la zona
`0x10`; `ch2_dma_ejecutar()` en `mem.c` solo el primero, y mandaba el resto a
`memwrite_fisico()`. Son **las dos entradas al mismo chip** y el guest elige una u otra por
conveniencia suya: el juego sube sus 115 200 bytes por DMA porque son 3600 bloques de 32 y
vaciar colas de a uno costaría un mundo. Con la ruta faltando, el cuadro entraba a la zona
`0x10` como si fuera memoria, el fin de DMA se informaba puntual, y no se convertía un solo
macrobloque. Sin un aviso. La forma de falla de siempre.

**2. Faltaba el fin de transferencia YUV**, `SB_ISTNRM` bit 6. **No es el fin del CH2 DMA**, que
es el 19: uno dice que los bytes llegaron y el otro que la textura quedó escrita, y un guest
puede esperar cualquiera de los dos. La biblioteca de DMA de Dave Mirra marca la transferencia
como «en vuelo» y espera el bit 6 para sacarla de su cola; con el 19 solo, la cola se trababa en
su primera entrada de tipo YUV y no volvía a moverse — 21 escenas con cero tiras, para siempre.
Va con demora, como el fin del CH2 y por el mismo motivo: quien disparó la transferencia todavía
tiene que volver y anotarla, y una interrupción instantánea le gana esa carrera y se lee como
espuria.

**3. La textura del video se declara 512×512 con stride 320.** Un video no se declara con su
tamaño: el lado es la potencia de dos que exige el chip y el ancho real viaja en el stride.
`get_texture()` ya lo sabía y juntaba `vsize*stride*2` bytes; `decodificar_yuv422()` no, y
recorría `usize` por fila. **327 680 juntados contra 524 288 leídos**, y el emulador se caía. La
rama de stride que ya existía trata bien las texturas de 16 bits, pero el YUV se decide antes en
la cadena de `if` y nunca la veía.

El arreglo no es restar en el decodificador: es que el paso se calcule **una vez** (`paso_16`) y
mande sobre las dos cuentas. Son justo las dos que no pueden discrepar — cuando lo hacen, el que
junta menos gana y el que recorre más se sale del buffer.

**4. La mitad derecha del croma no se leía nunca**, y esto afecta a todo el que use el
convertidor. El plano U de un macrobloque de 16×16 mide 8×8: una muestra cada dos píxeles a lo
ancho. El bucle avanza de a dos píxeles, o sea una columna de croma por vuelta — `x/2` —, y
estaba escrito `x/4`. El índice llegaba hasta 3 de 8: se descartaban 32 de los 64 bytes de U y
otros tantos de V, y la mitad izquierda salía estirada al doble. En la rama de 422, el mismo
error escrito distinto (`col/2` sobre `col = x % 8`).

Lo delató la cuenta, no la mirada: las dos demos de KOS decodifican **la misma imagen** en los
dos formatos, que solo se diferencian en la resolución vertical del croma, así que sus salidas
tienen que parecerse mucho.

| | 420 contra 422, media | máximo | colores distintos |
| --- | --- | --- | --- |
| antes | 2,560 | **28** | 80 632 / 81 585 |
| después | 1,084 | **3** | 65 797 / 65 765 |

El máximo de 28 era el estiramiento horizontal; el de 3 es la diferencia real entre los
formatos. A ojo, sobre la pared de ladrillos de las demos, las dos versiones se parecen — por
eso pasó.

**Lo que queda abierto**: el FMV muestra rayas de croma de 16 píxeles de ancho y una fila de
alto, iguales antes y después del arreglo del croma, así que no vienen del desempaquetado. O las
produce el decodificador del propio juego, o algo del camino a la RAM de video. Es cosmético y
no está perseguido.

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
nada a las otras demos.

### Y con `--render=shader` es por píxel, que es como la aplica el chip

El fragment shader la evalúa por fragmento y **antes de salir, o sea antes de la mezcla**, que son
las dos cosas que la función fija no puede hacer. `q` sale interpolado por el rasterizador sin
costo: viaja en `gl_TexCoord[0].w`, que es el mismo 1/w que el TA entrega para la corrección de
perspectiva. La tabla va como un uniforme de 128 `vec2` (alfa lejano, alfa cercano) subido una vez
por escena, junto con el color y la densidad; nada de eso cambia en medio de un render.

Las dos diferencias contra la pasada por vértice son reales y las dos favorecen al shader:

- **Dentro de polígonos grandes que abarcan mucha profundidad** —las paredes de un túnel, el piso
  de un juego de autos— interpolar linealmente el alfa entre los vértices de una curva exponencial
  está muy mal en el medio. En `kgl-tunnel` la versión por vértice tapa los pilares y el arco que
  las paredes sí tienen; la de por píxel los muestra. Es la misma demo que descubrió que la niebla
  no estaba emulada.
- **En geometría translúcida**, aplicarla antes de la mezcla en vez de después cambia el resultado,
  y antes es donde la aplica el chip.

`DCEMU_SIN_NIEBLA=1` es lo que aísla el cambio: con la niebla apagada los dos caminos vuelven a
salir byte a byte idénticos, así que la única diferencia entre ellos es ésta.

Y se lleva una verruga por delante: la segunda pasada llamaba a `gl_estado_olvidar()`, o sea que
**cada tira con niebla destruía la sombra de estado de la siguiente**. Con el camino programable no
hay segunda pasada.

**La niebla por vértice (modo 1) sigue sin emularse.** Su coeficiente es el alfa del color de
offset, que el vértice de dcemu no guarda — `vertex` lleva `ro,go,bo` sin alfa. Emularla es tocar
la estructura y el armado del TA, no el shader.

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

**El caso que parecía más rico de esa aproximación era Sega Rally 2, las pantallas de selección
(2026-08-10) — y cerrar la mezcla de la OIT desmintió la atribución ese mismo día.** El auto que
gira en SELECT CAR / SELECT TRANSMISSION es un modelo 3D de ~726 tiras que el juego mete **entero
en la lista translúcida** con la escritura de Z apagada — carrocería con alfa 1.0, vidrios a
0.74-0.90, sombra a 0.17 — y a tamaño normal se lee «semitransparente». El diagnóstico original
culpó al orden por tira, con la OIT de entonces como prueba («por píxel sale sólido»); con la
mezcla de la resolución corregida (la quinta falla, sección de la transparencia ordenada) el
experimento se dio vuelta: **ventana, shader y OIT coinciden a ≤2 niveles en toda la pantalla,
banda oscura incluida**, así que el orden por tira quedó absuelto en este cuadro — el «auto
sólido» de la OIT rota era su propia mezcla borrando el fondo acumulado, no el orden. Lo que la
banda es de verdad, tira por tira (escena 1600 del flujo determinista): la línea de ventanillas,
cubierta por el vidrio (atlas de reflejos 128×128, alfa 0.90, con sus vetas oscuras) y por tiras
de reflejo env-mapeadas — UV casi constante estirada sobre quads largos (3×2 texeles sobre 95×8
píxeles), color de vértice 0.58, modulate-alpha contra un atlas 256×256 que contiene **dos vistas
completas del auto** (con pinta de RTT del propio juego usado como mapa de reflejo). Ninguna de
las tres vías la dibuja distinto — y **el árbitro llegó ese mismo día: en consola real el auto es
opaco**. Un longplay en hardware real (YouTube `MGJDPzvvekE`, «Sega Rally 2 10 Year Championship
PAL Dreamcast Actual Hardware», t≈215 s, pantalla CAR SETTINGS con el mismo fondo de baldosas)
muestra la carrocería sólida, sin una baldosa a través del cuerpo — mientras el F6 del usuario en
dcemu muestra el texto del fondo legible a través del guardabarros, dependiente de la pose.

**La segunda vuelta de la caza (mismo día) absolvió, con la verdad de base, a todo el resto de la
maquinaria** — y esto es lo que NO hay que volver a sospechar:

- **El TA no pierde geometría.** `DCEMU_VOLCAR_TA` (los bloques crudos del embudo, escenas
  1599-1601) contra el volcado de `TriangleStrip[]`: el juego manda **quads sueltos de 4 vértices**
  (herencia Model 3 — 1058,7 tiras TR por escena, todas de 4, un encabezado `820c000e` compartido
  cada ~5) y el buffer registra exactamente 1058. Sin pérdida, sin recorte de tiras largas (los
  pares idx-contiguos no comparten arista: nunca fueron tiras largas).
- **El JIT es fiel también aquí**: intérprete y traductor dan el cuadro byte a byte idéntico a los
  28 s. La sospecha del camino rápido de memoria en línea, muerta.
- **Los colores y offsets de vértice se leen bien**: en el crudo, la palabra 6 es `ff969696`
  (gris 0.588, el 43 % del auto), `feffffff`, `e6969696` (alfa 0.90), `2cffffff` (0.17) — lo mismo
  que el volcado imprime — y la palabra 7 (color de offset) **es cero de verdad** en el 79 % de los
  vértices: el `off=(0,0,0)` con offset habilitado es del juego, no una lectura corrida.
- **El contenido del atlas y la caché están sanos, y el «rosa» era un fantasma del método**: el
  atlas `467880` es un *slot* que el juego realquila. En SELECT CAR contiene dos vistas del auto
  (el contenido rosa/oliva — presumiblemente arte de esa pantalla); al entrar a SELECT TRANSMISSION
  (el **segundo** A de la receta, ~30,3 s) el juego lee «MTEX» del disco (DMAREAD 0x11, 254
  sectores desde 381886), lo descomprime (~0,2 s en un LZ de 42 instrucciones) y lo sube — y la
  caché **re-decodifica la librea perfecta ~0,5 s después de la entrada** (`DCEMU_VOLCAR_TEX`
  extendido a volcados numerados: el -01 es la librea impecable del 206). El volcado «rosa» del
  primer diagnóstico era la *primera* decodificación — contenido legítimo de la pantalla anterior.
- Y de la primera vuelta: decodificador (destwiddle propio = idéntico), tejido de ventanas,
  `mmu_traducir_sq`, `SB_LMMODE0`, reloj por eventos, culling (modo 1 «cull if small» se dibuja
  entero; 2/3 con conio de testigo), el alfa de lo apilado (sonda 4), y la mezcla misma.

**Lo que queda abierto, bien acotado**: con la librea correcta en su lugar, la pantalla sigue
mostrando la banda oscura y la impresión de transparencia (el F6 del usuario: fondo legible a
través del guardabarros). Los ingredientes medidos: el 43 % del auto lleva color de vértice gris
0.588 modulando la librea (¿iluminación por software por pose? — verificable comparando los
histogramas de la palabra 6 entre dos poses con `DCEMU_VOLCAR_TA`), más el vidrio a 0.90 y quads
de reflejo con UV casi constante (3×2 texeles sobre 95×8 píxeles). `DCEMU_SIN_TEX` separa las
capas (sin `467880` el auto queda en silueta: ese atlas ES la carrocería). **El árbitro fino llegó
(2026-08-10): en hardware real, la MISMA pantalla con el auto girando sale sólida y blanca.** Un
segundo video en consola real (YouTube `ycHDQlu9_1M`, modo arcade JP filmado de la pantalla,
t≈30 s SELECT TRANSMISSION y t≈34 s NAME ENTRY, ambos con el Celica ST205 girando sobre las
baldosas rojas): carrocería blanco brillante — el mismo blanco de las placas AUTOMATIC/MANUAL —
sin banda, sin velo, sin fondo a través del cuerpo, en las dos poses. dcemu, misma pantalla:
grisáceo con la banda y los parches. De los histogramas por pose (`DCEMU_VOLCAR_TA` en las
escenas 1620 y 1990): la cola de grises `ffcccccc`/`ffb3b3b3`/… **cambia con la pose** —
iluminación por software, legítima — mientras `ff969696` (41 %) y `feffffff` son constantes.
Con todas las entradas verificadas fieles, quedaba auditar la COMPOSICIÓN por píxel — y **la
reconstrucción a mano cerró (2026-08-10): dcemu compone exactamente lo que los datos mandan.**
Sobre la escena 1698 (volcado de tiras + TA crudo + BMP de la misma corrida, ancla por hash), la
aritmética capa por capa — barycéntricas en el punto, UV con corrección de perspectiva desde el
crudo (`Σλ·uq / Σλ·q`; ojo: **el volcado de escena imprime `t1,t2` premultiplicadas por `q`**, las
UV reales salen del crudo), muestreo Morton 565/4444 bilineal, modulate-alpha y la cadena de
mezcla en el orden del dibujo — reproduce el render al LSB: el píxel «blanco lavado» (160,212) da
(176,173,176) contra (178,172,178) del BMP, y el negro (217,208) da (15,14,15) contra (0,0,0). El
blanco lavado ES «texel blanco de librea × vértice 0.69 de la iluminación por software»; el negro
ES texeles casi-negros del propio atlas. De paso quedó identificada la maquinaria del auto: el
atlas `489880` es un **mapa de entorno (cielo al atardecer) con máscara especular en el alfa**
(brillos en vetas, alfa 0 = «aquí no hay brillo» — capas que se evaporan por diseño), `491880` el
vidrio con vetas, y flip/clamp del TSP dominante en cero. **El único sospechoso vivo que queda en
dcemu son los cuadrados de ruido del atlas de la librea**: parches de moteado negro (p. ej.
alrededor del texel (231,181) del `467880`) que conviven con arte impecable y de los que el píxel
negro de la banda tomó su última palabra. **La vuelta MTEX (2026-08-10, la quinta) los dejó a un
paso de absueltos.** Lo que quedó probado con el disco en la mano: el LZ del juego está
**revertido y reimplementado exacto** — banderas por byte MSB-primero, bit 1 = match con
`largo = (A&0xF)+3` y `desplazamiento = ((B<<4)|(A>>4))+1` (tope 4096, copia solapable), bit 0 =
literal, y **la recarga del byte de banderas ocurre ANTES del payload del octavo token** (el
error que cuesta cometer al reimplementarlo); cada entrada lleva 16 bytes de encabezado con el
tamaño descomprimido en `+4` y datos en `+16`; los archivos «MTEX» llevan la cuenta de entradas
en el byte 4 y una tabla de `{tipo, formato, offset, tamaño}` de a 16 bytes desde `+0x10`. Con él
se verificó **la cadena entera de dcemu de punta a punta** para el pack del carrusel (254
sectores desde FAD 381886, track21 del `.gdi`): disco → lectora → RAM del guest (byte a byte) →
LZ → subida → VRAM → caché → decodificación, y la entrada 0 descomprime EXACTO a las «dos vistas
rosas» — **el rosa es arte embarcado del juego**, no corrupción. El flujo del juego quedó
mapeado: un lector por streaming con reposicionamientos (`MULTI_DMAREAD`), 31 archivos MTEX
anidados en la región 381798+, los logos de los autos en el de FAD 382140 (cinco entradas,
descomprimen impecables), el modelo en 383021/384143, y **los packs por auto de 301 sectores en
FAD 360392/360994** — un contenedor propio, sin firma MTEX, donde vive la librea (cargada en
SELECT CAR, re-subida a `467880` al entrar a la transmisión desde el caché en RAM). La librea no
está cruda en el disco (barrida) ni en las ~400 primeras entradas LZ de 0x20000 del track. Los
valores de los cuadrados (0x0841/0x4208/0x39C7/0x2945 con claros 0xF79E) son **grises 565
estructurados en trama — material tramado (¿parrilla/carbono?), no basura** — y el 206 del
SELECT CAR (misma librea desde otro slot) muestra el mismo parcheado oscuro en los arcos. Para
el golpe de gracia queda una sola rendija: romper el índice del contenedor de los packs por auto
(FAD 360392) o volcar el caché en RAM del pack tras la carga (~26 s) y comparar los cuadrados
ahí. Con todos los eslabones verificados exactos y los valores con estructura de arte, el
veredicto provisional fue que el auto de dcemu era un retrato fiel de los datos del juego.

**Y el golpe de gracia lo dio vuelta (2026-08-10, la sexta vuelta): el CELICA en dcemu contra el
Celica del video de hardware — mismo auto, misma librea Castrol, misma pantalla, mismo modo — y
dcemu lo saca hecho un collage de confeti** (F6 del usuario, `f6-celica` en el expediente):
fragmentos rojos/verdes/azules regados por la carrocería donde la consola muestra blanco Castrol
limpio y sólido. Ya no hay excusa de arte: **la página de texturas que dcemu compone para el auto
está mal**, en el Celica de forma flagrante y en el 206 de forma leve — los «cuadrados de ruido»
pasan de «probablemente arte» a **probablemente la forma leve del mismo bug**. Las coordenadas
del sospechoso que deja la vuelta MTEX: la página del auto NO existe textual en el disco (ni
cruda ni comprimida con el LZ — barrido entero con filtro de prefijo), o sea que **el juego la
compone en RAM a partir de piezas**, y las piezas viajan por lo único no verificado: el flujo
(`MULTI_DMAREAD`/`REQ_DMA_TRANS` — cuyo consumidor en `dcopcodes.c` se lee correcto función por
función) MEZCLADO con DMAREADs sueltos de 1 sector sobre la misma zona, con el compositor del
guest corriendo entre medio.

**La séptima vuelta (mismo día) llegó más lejos que el plan por un camino mejor: la grabadora y
el replay del mando** (`DCEMU_GRABAR_MANDO`/`DCEMU_MANDO`, ver la tabla — validados en lazo
cerrado: receta grabada → replay → captura byte a byte). El usuario grabó su navegación al Celica
una vez y la receta quedó autónoma en el repo: `herramientas/mando-sr2-celica.txt` +
`herramientas/vmu-sr2-menus.bin` (la tarjeta de la que arranca — la regla de siempre), replay con
`DCEMU_MANDO=` y `--salir-tras=36`. Con ella cayeron en cadena: **la página del Celica llega
PRISTINA a la VRAM** (volcado `-01`: el atlas Castrol impecable — capó, frente TOYOTA, puertas,
placas «6»), sus tiras llevan los MISMOS estados que las del 206 (565, α=1, env=3, z coherentes
0,20-0,33, colores de vértice 0,58-0,81 — censos idénticos), **y sin embargo el render es
confeti**. Y la reconstrucción aritmética — exacta al LSB en el 206 — **falla en el flanco del
Celica**: el modelo predice gris (95,93,95) con el cuerpo presente; el render da azul oscuro
(36,31,97). Primera divergencia modelo-contra-render del expediente. Las capas de encima quedaron
absueltas por palanca (`DCEMU_SIN_TEX` de `489880` cielo, `491880` vidrio y `39a080` — que
resultó ser el atlas de los paneles de UI — no cambian el auto): **el velo está en cómo se dibuja
el cuerpo mismo**, en algo que el modelo aritmético no captura.

**RESUELTO (2026-08-10, la novena vuelta): la clave de la caché de texturas no incluía el formato
de píxel.** El desenlace lo destapó el usuario mirando el atlas «rosa»: «¿no será un problema de
ARGB/RGBA?» — y sí: los bytes «rosas» son **fotos ARGB1555 perfectas** (el 306 Maxi y el Corolla
WRC del carrusel) que solo eran rosas a través de un visor que asumía 565; en dcemu nunca se
vieron rosas. Pero la observación destapó lo real: **los menús de SR2 usan la misma dirección
(`467880`) como fotos 1555 en SELECT CAR y como página de librea 565 en SELECT TRANSMISSION — y
la clave de la caché ({dirección, tamaño, bpp, paleta, mip}) no distinguía el formato**, así que
una sola entrada servía a los dos declarantes y su decodificación era la del último que la
regeneró: las tiras del cuerpo muestreaban la página re-empaquetada con los bits corridos
(1555↔565), que es exactamente el confeti del Celica, la banda y los «cuadrados de ruido» del
206, y el «auto semitransparente» que abrió el expediente. El arreglo es la misma regla que ya
tenía el bit de mipmap: **el formato entra a la clave** (`fmt` en `cached_textures`), con
`DCEMU_SIN_FMT_CLAVE=1` como interruptor que reproduce el comportamiento anterior. Compuertas:
**la canónica de DOOM intacta al byte (198B396F…, sobre el `.cdi` — ojo: la referencia es sobre
el CDI, no el GDI del mismo directorio, que arranca distinto y costó una bisección en falso)**,
las doce demos de control byte a byte sin cambio, y los dos autos quedan **idénticos al hardware
real**: el Celica blanco Castrol sólido y el 206 blanco con su león. La divergencia
modelo-render del flanco era esto mismo (el modelo muestreaba el volcado de la decodificación
correcta; el render, la entrada aliased), y la sonda del ligado no podía verlo porque el id de GL
era el mismo — lo que cambiaba era el contenido decodificado adentro. Dos
trampas de método que costaron horas: **el estado de la VMU cambia el flujo de menús** (misma
receta de teclas, otra pantalla — fijar `--vmu=` a una copia por corrida), y **las direcciones de
las baldosas son de un asignador del guest** — el mapa de una corrida no vale para otra. El rayado
horizontal fino del fotomontaje del título quedó caracterizado aparte: está en los datos que el
guest compone en VRAM (el volcado lo muestra), con camino de escritura absuelto — puede ser el
arte mismo; sin veredicto.

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

---

## El medio píxel: dónde muestrea el chip y dónde muestrea OpenGL

**El PVR toma la muestra de un píxel en su coordenada entera; OpenGL la toma en el centro,
`(X+0,5; Y+0,5)`.** Con la misma geometría, los dos interpolan coordenadas de textura corridas
medio píxel. Para casi todo es invisible —medio píxel dentro del mismo texel da el mismo texel— y
por eso el árbol vivió doce años sin notarlo. Se ve cuando una tira tiene **más de un texel por
píxel**, porque ahí el medio píxel vale un texel entero o más.

La corrección va en el `glOrtho` de `screeninit()` (`medio_pixel()` en `graficos.c`), no en la
geometría: mover el sistema de coordenadas deja el vértice que el guest puso en X justo donde GL
muestrea el píxel X. Tiene tres sutilezas y **las tres fueron errores cometidos y medidos**:

- **Es medio píxel del DESTINO, expresado en unidades del guest.** Con `--render=ventana` la
  pantalla emulada de 640 se estira sobre 800, así que medio píxel de destino son 0,4 unidades. Con
  0,5 fijo el centro del primer píxel del destino cae *fuera* de una geometría que empieza en 0:
  DCDoom, Virtua Tennis y Dave Mirra perdieron su columna izquierda y su fila superior enteras, y
  sólo en el modo por omisión, que es justo el que se mira en vivo. Con la razón puesta, el centro
  del primer píxel del destino cae exactamente sobre el borde a cualquier resolución.
- **Es un pelo menos que medio.** Justo en el medio, el punto de muestreo cae exactamente sobre el
  borde de toda geometría alineada a enteros —o sea casi toda— y la cobertura queda a merced del
  desempate del rasterizador. En x salía bien por casualidad; en y no, porque el `glOrtho` invierte
  el eje y con él se invierte el desempate: el primer cubo de `pvr-fb_tex` caía en las filas 1..64
  en vez de 0..63. Quitarle 1/64 saca el empate en los dos ejes y mueve la interpolación menos de
  una centésima de píxel.
- **Los quads propios de dcemu tienen que sacárselo.** `DibujarFramebuffer()` es una copia 1:1
  filtrada con `GL_LINEAR`; medio píxel ahí no la corre, la **mezcla con el vecino**. El quad se
  dibuja en `-h .. ancho-h` para volver a cubrir el destino exacto.

**A qué no equivale**: no es media coordenada de textura. Un corrimiento fijo de medio texel daría
la mitad de lo que hace falta en `fb_tex` —que tiene dos texeles por píxel— y de más en cualquier
ampliación. La corrección es de pantalla y su tamaño en texeles depende de cada tira.

`DCEMU_SIN_MEDIO_PIXEL=1` vuelve al comportamiento anterior y **reproduce cualquier línea base
previa byte a byte**, verificado contra el SHA de DCDoom.

### `pvr-fb_tex` es lo único del parque que puede medirlo

Lee su propio front buffer como textura con stride, a **dos texeles por píxel de pantalla**, así
que ahí el medio píxel vale un texel exacto. Y su verificación no necesita imagen de referencia,
que es lo que la hace utilizable: la demo se realimenta del framebuffer, o sea que **si la copia es
1:1, dos cuadros consecutivos sólo pueden diferir dentro de la caja de 64×64 del cubo nuevo**. Con
la convención mal, difieren 12 000 píxeles repartidos por toda la pantalla; con la convención bien,
4096 y ni uno fuera de la caja. En pantalla la diferencia es un rastro continuo del arcoíris contra
dos copias de media anchura.

La aritmética de la demo, que es de donde salió el signo: sus dos pasadas mezclan por `DST_ALPHA` e
`INV_DST_ALPHA` contra una máscara opaca que alterna columnas, y la fila de textura `2Y` frente a
`2Y+1` decide si se lee la mitad izquierda o la derecha del framebuffer —una fila con stride 640 son
1280 bytes en la numeración de 64 bits, o sea 640 bytes y 320 píxeles en la de 32—. Sólo cierra si
el índice de texel trunca a `2X`/`2Y`; GL, muestreando en el centro, obtenía `2X+1`/`2Y+1`. El caso
`hi_chip` —con desplazamientos de U de +2 y +1 texeles, donde 2 texeles son 4 bytes, o sea
exactamente el bit del banco— sale bien con la misma regla, y eso es lo que la fija.

**La ventana, medida (2026-08-16).** Esa aritmética predice más que un signo: escribiendo `s` para
el punto de muestreo dentro del píxel (`s = 0,5` es el centro, o sea sin corrección), la coordenada
de textura en el píxel `p` es `2(p+s)` y el texel elegido es `floor(2p+2s)`, que da `2p` **si y sólo
si `0 ≤ s < 0,5`**. O sea que la demo no dice «hay que corregir», dice «el punto de muestreo está en
esta ventana de medio píxel», y tiene dos bordes. `DCEMU_MEDIO_PIXEL_MIL=N` la barre (`s = 0,5 −
N/1000`) y `herramientas/fbtex-ventana.ps1` la mide sin imagen de referencia por el modo de falla
—la pantalla sale como dos copias de media anchura, así que basta la diferencia media entre las dos
mitades—:

| N | 0 | 125 | 250 | 375 | 484 (árbol) | 499 | 500 | 501 | 600 | 750 | 999 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `s` | 0,500 | 0,375 | 0,250 | 0,125 | 0,016 | 0,001 | 0,000 | −0,001 | −0,100 | −0,250 | −0,499 |
| mitades | **1,92** | 52,75 | 52,75 | 52,75 | 52,75 | 52,70 | 52,70 | 52,70 | **6,37** | **6,37** | **6,37** |

Los dos bordes caen donde la aritmética los pone y el árbol queda en el medio. Vale la pena saber
también **qué NO puede decir**: su ventana es de un texel entero, así que medio texel de corrimiento
del lado de la textura sólo la corre un cuarto de píxel y `s ≈ 0,016` sigue adentro. Por eso sale
byte a byte igual con y sin `DCEMU_MEDIO_TEXEL` a 1:1 — no porque las dos correcciones se cancelen.

**Y qué no es un segundo testigo.** El conteo de colores distintos de una pantalla 2D se derrumba
al mover `s` a 0,5 (el logo de Crazy Taxi, de 5721 colores a 612; Crazy Taxi 2 −86,9 %; Capcom vs.
SNK −83,6 %), y eso durante una sesión entera se leyó como evidencia en contra de la corrección. No
lo es: el volcado de escena muestra esa pantalla como baldosas de 16 píxeles sobre una grilla cuyo
origen está en `x,9`, con UV exactamente 0..1 sobre texturas de 16×16 y filtro bilineal
(`tsp=208824c9`). Muestrear en la coordenada entera —el chip— deja la muestra 0,1 texel adentro de
la baldosa, o sea una mezcla 60/40 con el vecino: **la pantalla también sale suave en consola**, y
5721 es la respuesta fiel. Poner `s = 0,5` la afila porque cancela el corrimiento del propio juego,
que no es un arreglo. La regla general está en `CLAUDE.md`: una métrica que se mueve con la palanca
no está por eso midiendo la palanca.

**Residuo conocido**: la copia pierde la columna 0 y la fila 0. Es el propio desplazamiento de
`-1/1024` de la demo leyendo fuera de la textura en el borde izquierdo; dcemu no pierde la fila
superior en general —cinco demos de pantalla completa siguen pintando sus 640 píxeles de la fila 0—.

### El medio texel del lado de la textura: se despachó y se retiró en un día

La corrección de arriba parecía tener una compañera, y no la tiene. La idea era que `u = 0` nombra
el **centro** del texel 0 en el chip y su **borde** en GL (índice `u·W − 0,5`), o sea que a las UV
les faltaba medio texel. Se despachó el 2026-08-14 porque quitaba las costuras de un píxel del logo
de Crazy Taxi y movía la imagen entera de DCDoom un píxel —la columna 0 pasaba de 39 618 de tinta
contra los 78 180 de la columna 1, a 78 312 con progresión suave—.

**Estuvo apagada de nuevo al día siguiente**, porque la pasada por los juegos comerciales encontró
el fondo en degradado limpio de Street Fighter III convertido en una rejilla de costuras: medido,
`0 → 5012` picos de costura por columna y `0 → 2633` por fila (`herramientas/costuras.ps1`), y
ChuChu Rocket +20 %. El volcado de escena nombra el mecanismo sin lugar a discusión: **las UV crudas
de SF3 son `0,001953 = 0,5/256`, `0,064453 = 16,5/256` y `0,126953 = 32,5/256` — el juego ya
direcciona centros de texel**. Si el chip pusiera `u = 0` en el centro del texel 0, pedir `(k+0,5)/W`
caería justo sobre la frontera entre dos texeles: una mezcla 50/50 en consola, la peor elección
posible para un atlas de interfaz. Así que la convención del chip es la de GL, y sumar medio texel
es lo que pone el muestreo sobre la frontera.

El síntoma que la había motivado —la costura del logo de Crazy Taxi— resultó tener la hipótesis
mejor que ya estaba escrita entonces, y ésa es la que se despachó: UV 0..1 bajo `GL_REPEAT`, donde
el filtro envuelve en el borde. Es una pregunta de modo de direccionamiento del borde y no de dónde
cae `u = 0`; ver «Una tira cuyas UV no salen de [0,1]» más arriba. `DCEMU_MEDIO_TEXEL=1` queda como
palanca y como línea base anterior.

**Dos lecciones, las dos pagadas.** Un arreglo que explica un síntoma no es por eso correcto: la
costura y el corrimiento de un píxel tenían una causa común plausible y era la equivocada. Y **el
parque de KOS no puede arbitrar un cambio de muestreo de textura**: las 37 demos que la corrección
movió se veían todas bien, y la contradicción sólo apareció en un juego.

La única evidencia que apunta al otro lado es débil, y se anota nada más para que no se
redescubra como novedad: la grilla del logo de Crazy Taxi está en `x,9`, que bajo la convención de
centro muestrearía 0,1 pasado el centro del texel 0 (casi nítido) y bajo la de GL muestrea 0,1
pasado su borde (mezcla 60/40). Igual que las de SF3, ésa es la creencia *del autor del juego*
sobre el chip, no el chip.

---

## La prueba de profundidad y la transparencia ordenada

**Un shader con `discard` obliga a probar la profundidad después de ejecutarlo.** Hasta no
ejecutarlo el driver no sabe si el fragmento existe, así que la prueba se atrasa — y con ella se
atrasa *sólo la prueba*, no los efectos laterales. El epílogo de la lista por píxel apila y después
descarta, o sea que **apilaba también lo que la profundidad iba a rechazar**: geometría translúcida
tapada por geometría opaca entraba en la lista y la resolución la mezclaba encima de lo que la
tapaba. No es el detalle de una demo: es toda la tanda translúcida de cualquier escena con paredes.

Lo destapó `pvr-fb_tex`, donde el cubo opaco quedaba borrado por los dos cuadriláteros de pantalla
completa que están detrás de él; y como esa demo se realimenta del framebuffer, el cubo borrado se
llevaba puesto el rastro del cuadro siguiente y la pantalla entera terminaba en negro. La sonda de
capas lo confirma al revés: con el arreglo, `n = 0` en exactamente los 4096 píxeles del cubo y
`n = 2` en los otros 303 104.

Se arregla con `layout(early_fragment_tests)`, que corre las pruebas **antes** del shader, y por
eso el apilado necesita un **programa propio**: con la prueba adelantada la profundidad se escribe
antes del shader, así que un fragmento de punch-through que se descarta por alfa dejaría su z
escrita igual. Los dos programas son el mismo fuente salvo esa línea y se mantienen al día con
`glProgramUniform*`, que escribe un uniforme sin ligar el programa — sin esa entrada no hay segundo
programa y no hay transparencia ordenada, que es preferible a dibujar lo que debería estar tapado.

El arreglo subió de 5 a 9 (de 12) las demos de control byte a byte iguales entre `--render=shader`
y `--render=oit`, y en ese momento las tres que quedaban —`2ndmix`, `kgl-tunnel`, `tsunami-banner`—
se atribuyeron a que el orden por píxel *debe* diferir del orden por tira. La quinta falla (abajo)
demostró que la atribución era en su mayor parte falsa: también eran la mezcla.

### La quinta falla silenciosa: los factores 4-7 son absolutos de los dos lados (2026-08-10)

Los códigos de mezcla del TSP 4-7 nombran su operando de forma absoluta — alfa del ORIGEN, alfa del
DESTINO — y valen igual como factor de origen que como factor de destino; sólo 2 y 3 («el otro
color») dependen del lado. Es exactamente la regla que separa las dos tablas de `blend_modes` en
`graficos.c`, escrita en el comentario de esas tablas… y el shader de resolución la violaba: pasaba
propio/otro simétricos a las dos llamadas, así que del lado del destino el código 4 (`SRC_ALPHA`)
leía el alfa del **destino** y el 6 (`DST_ALPHA`) el del **origen**. Como la tanda opaca deja el
fondo con alfa 1.0 en toda la pantalla (lo dice la sonda 3), el factor de destino de la mezcla
clásica (4,5) salía `1-dst.a = 0` en vez de `1-src.a`: **toda tira con alfa < 1 borraba el fondo
acumulado** y quedaba su color multiplicado por su alfa sobre negro. La pantalla SELECT
TRANSMISSION de Sega Rally 2 — cuya grilla ARCADE es un quad translúcido a pantalla completa —
salía casi negra, y fue el caso que lo destapó; la sonda 4 (el color apilado, brillante y correcto)
apuntó la culpa a la mezcla en una sola corrida.

Por qué nueve demos de control no lo delataron: sus mezclas coinciden por casualidad — con alfa 1.0
el factor de destino da 0 por las dos vías, y el destino `ONE` de las aditivas no consume alfa.
Con la corrección, **once de las doce salen byte a byte idénticas a `--render=shader`** —
`kgl-tunnel` incluida, cuya diferencia nunca fue el orden — y las dos que quedan (`2ndmix`,
`tsunami-banner`) difieren en ≤2 niveles en <0.7 % de los píxeles: el residuo de cuantización (el
nodo empaqueta el fragmento a 8 bits con `packUnorm4x8` y la resolución acumula en flotante,
mientras GL mezcla el fragmento sin cuantizar y redondea al escribir cada tira). La pantalla de
SR2 queda igual: ≤2 niveles contra shader en los 307 200 píxeles. Condiciones de la compuerta,
para reproducirla: binario `build-jit`, 8 s emulados, `--sin-vmu`, `DCEMU_RTC_FIJO=1000000`.

### Las sondas de `DCEMU_OIT_SOLO_FONDO`, y la que mintió

1 emite el fondo, 2 cuenta capas, 3 el **alfa** del fondo, 4 el color del fragmento más cercano sin
mezclar. La 3 existe porque la mezcla por `DST_ALPHA` consume un canal que una captura RGB no
muestra; la 4 separa «el color apilado está mal» de «la mezcla lo usa mal».

**La 2 estuvo rota desde que se escribió y eso costó una sesión.** Vivía detrás de un
`if (solo_fondo != 0)` que ya había devuelto el fondo, así que devolvía el fondo y no contaba nada.
Su salida —4096 píxeles «con lista» y 303 104 «sin»— era la imagen del cubo sobre negro, y se leyó
como «la escena de pantalla no apila nada». Eso quedó anotado como hecho en el mensaje de commit y
en un comentario del código. La regla que deja: **una sonda es código, y una sonda equivocada
confirma justo aquello para lo que se escribió**; antes de creerle la primera respuesta, hay que
hacerle contestar algo cuya respuesta ya se sabe.

---

## Los volúmenes modificadores por píxel

El chip decide **pixel a pixel** si está dentro del volumen y con eso elige uno de los **dos juegos
de parámetros** que trae el vértice — otro color, otra UV, otro color de offset. En función fija eso
son el buffer de plantilla y **dos pasadas de la misma geometría**, una recortada a fuera y otra a
dentro. Con el camino programable la cuenta de caras va a una imagen que el fragment shader puede
leer, así que la elección se hace donde corresponde: dentro del shader, en un solo dibujo.

- **El juego 1 viaja en las unidades de textura 1, 2 y 3** (UV, color y offset). No es un abuso: son
  cuatro flotantes por unidad y es exactamente lo que hay que pasar. La alternativa —atributos
  genéricos— obligaría a VBO y VAO, que es justo lo que el camino de arreglos de cliente evita.
  `graficos.c` los enciende sólo para las tiras que un volumen afecta.
- **La pasada de marcado sigue existiendo y tiene que seguir existiendo.** La máscara se cuenta
  contra la profundidad ya resuelta, que es el orden del chip: primero la visibilidad, después el
  volumen. Lo que desaparece es la *segunda pasada de geometría*, no el marcado.
- **Una sola pasada de acumulación en vez de dos**, usando `gl_FrontFacing` en lugar de dos recorridos
  con culling opuesto: el sentido se decide en coordenadas de ventana igual que el culling.
- **`layout(early_fragment_tests)`, por lo mismo que la transparencia ordenada.** El `discard` del
  acumulador atrasaría la prueba de profundidad y se contarían caras que están *detrás* de la
  superficie — que es exactamente el error que la versión anterior a la plantilla por profundidad ya
  había cometido, y que dejaba el techo del taxi de Crazy Taxi oscurecido por su propia sombra.

### Las tres cosas que costaron, todas silenciosas

1. **`glFrontFace` hay que fijarlo aunque no se recorte por cara.** `gl_FrontFacing` lo decide contra
   `glFrontFace` igual que el culling, y `gl_cull()` lo cambia **por tira**. Sin fijarlo, la cuenta
   sale con el signo que dejó la última tira dibujada.
2. **Una guarda que se protegía de lo que ella misma tenía que crear.** `glmoderno_vol_empezar()`
   salía temprano con `vol_mascara == 0` —su valor hasta que esa misma llamada la crea— así que la
   imagen no se creaba nunca, `imageAtomicAdd` escribía en el vacío e `imageLoad` devolvía cero. La
   máscara vacía en todas las escenas, sin un solo error. La forma de fallar de siempre en este árbol.
3. **Los uniformes de imagen no se pusieron con `glProgramUniform*`.** La unidad se quedaba en 0
   —donde vive otra imagen—, `imageLoad` devolvía cero y el síntoma era idéntico al anterior. Se
   cerró poniendo `layout(binding = N)` en el fuente: lo que no hay que poner no se puede quedar sin
   poner.

Las dos últimas dan el mismo síntoma y por eso hubo que separar las sondas: `DCEMU_VOL_SONDA=1` pinta
la tira de rojo donde la máscara dio dentro y de verde donde dio fuera —o sea, mira lo que el shader
lee—, y `DCEMU_VOL_SONDA=2` lee la máscara de vuelta con `glGetTexImage` y cuenta los texeles
marcados —o sea, mira lo que la pasada escribió—. La primera decía «vacía» y la segunda «165 000
marcados»: entre las dos, el problema quedó del lado de la lectura en un paso.

### La verificación

Contra el camino de plantilla, con `DCEMU_RTC_FIJO`: `pvr-modifier_volume` y
`pvr-modifier_volume_tex` **byte a byte idénticas**; `pvr-modifier_volume_zclip` difiere en **5
píxeles**, todos de ±1 en un canal. De los seis juegos, cuatro salen byte a byte iguales —DCDoom,
Sega Rally 2, Street Fighter III y Virtua Tennis 2—, Virtua Tennis difiere en 2 píxeles de ±1, y
Crazy Taxi —el único con volúmenes de verdad, 39 677 grupos en 70 segundos de juego— en 315 píxeles
sueltos con mediana de diferencia 1.

**Ese residuo tiene explicación y no es ruido**: el juego 1 ahora viaja como coordenada de textura,
que se interpola en coma flotante, y antes viajaba por `glColorPointer`, que GL puede interpolar con
menos precisión y recorta a [0,1]. O sea que el camino nuevo es el más preciso de los dos, y los ±1
son la cuantización del viejo.

### La instrucción de cierre, y una demo que se verifica sola

Un volumen se cierra con «incluyendo» —afecta a los píxeles de dentro— o con «excluyendo», que
afecta a los de **fuera**. dcemu registraba las dos y trataba la segunda como una aproximación,
poniendo en cero lo que sus caras cubrían.

**No hay contenido en el árbol que la ejercite**: censadas 1,16 millones de tiras de Crazy Taxi en
juego, más las tres demos de volúmenes y los otros ocho juegos, la instrucción 2 no aparece ni una
vez. Por eso `demos/volumen-excluir/` existe: una pantalla entera con los dos juegos de parámetros
—azul el 0, rojo el 1— y un volumen cuadrado en el centro, compilada en dos sabores que sólo
difieren en la instrucción de cierre.

**Su prueba no necesita imagen de referencia, y esa es la gracia**: incluir y excluir son
complementos exactos, así que las dos capturas tienen que ser una el negativo de la otra. Lo son:
**307 200 de 307 200 píxeles**, cero fallas, y el rectángulo afectado sale exactamente en
x 160..479, y 120..359. El camino de plantilla, con la misma demo, da 38 640 píxeles en vez de
230 400 — que es la aproximación, medida.

---

## El buffer de acumulación secundario del TSP

El chip lleva **dos buffers de acumulación por píxel**, y dos bits de la palabra TSP eligen cuál usa
cada tira: el 24 (`blend_dst_acc2`, «blend to the 2nd accumulation buffer») como destino de la mezcla
y el 25 (`blend_src_acc2`, «blend from») como origen. Existe —DevBox 3.4.6.1— para tratar el
resultado de superponer varios polígonos **como si fuera uno solo**: se acumula el grupo en el
secundario y después se compone de una vez sobre el primario, en vez de mezclar cada polígono contra
la escena.

dcemu los registraba desde siempre en `TriangleStrip[]` y **no los consultaba nunca**. Se pudo vivir
así porque no hay contenido que los use, y eso ahora está medido y no supuesto: sobre **1,16 millones
de tiras de Crazy Taxi en juego**, las doce demos de control y los nueve juegos del árbol, no hay una
sola tira que seleccione el secundario. Ni siquiera Virtua Tennis 2 —5581 tiras, todas 0/0—, **lo que
lo saca de la lista de sospechosos de su sombra**, donde `notas-graficos.md` lo tenía anotado.

La implementación es el segundo adjunto de color del FBO:

- **el destino** se elige con `glDrawBuffer(GL_COLOR_ATTACHMENT0 + n)`;
- **el origen** leyendo el secundario como textura desde el fragment shader, que es lo único de los
  dos que la función fija no sabe hacer. Con el bit puesto, `dc_pixel()` devuelve el texel del
  acumulado directamente: ni textura, ni entorno, ni offset — lo que se mezcla es el grupo tal cual;
- el secundario **se limpia en cada escena**, como el primario. Si no, un grupo que se acumule ahí
  empieza sobre lo que dejó el cuadro anterior, y como se compone de una vez el fantasma sale entero.

Los dos bits se aplican juntos o ninguno. Aplicar sólo el destino sería peor que no aplicar nada: el
grupo se acumularía en un buffer que después nadie compone, o sea que desaparecería.

### La demo, que también se verifica sola

`demos/acumulador/` dibuja dos cuadrados superpuestos con mezcla **aditiva** sobre negro, en dos
sabores: uno los suma al primario como cualquier polígono, y el otro los suma al **secundario** y
después compone el secundario entero con un cuadrilátero de pantalla completa que lleva el bit 25.
Sumar sobre negro es asociativo, así que **las dos capturas tienen que salir byte a byte iguales**, y
salen: las dos en `334919C1`, con el solapamiento en `E0C0E0` = la suma exacta de `4060A0` y `A06040`.

Lo que hace que la prueba sirva es que las tres formas de equivocarse dan imágenes distintas. Si se
ignora el bit 24 los cuadrados van al primario *y además* se compone encima; si se ignora el 25, el
cuadrilátero aporta su propio color —verde oscuro a propósito— y tiñe la pantalla entera; si el
secundario no se limpia o está aliasado con el primario, sale doble. La segunda no es hipotética:
`--render=fbo`, que no implementa los bits, devuelve exactamente eso —`008000` de fondo y los
cuadrados teñidos—, o sea que la demo mide el efecto y no sólo lo ilustra.

### Con `--render=oit` no se aplica, y lo dice

Con transparencia ordenada el fragmento se apila en la lista de su píxel en vez de dibujarse, así que
el redirigido del destino no ocurre y el grupo nunca llega al secundario. La demo **igual sale bien**
bajo `--render=oit` —sumar sobre negro es asociativo y la composición aporta cero—, que es justo la
clase de acierto por casualidad que en este árbol hay que no dejar pasar por implementación. Una tira
que pida el secundario con la OIT puesta produce un aviso, una vez.

---

## Los dos bits del TSP que dcemu no leía: 19 y 21

`docs/rendimiento-plan.md` los tenía anotados como «residuos menores» de la vía 2.c. El censo dice
que menores no son.

### Bit 19 — «el texel no tiene alfa» (implementado)

Con el bit puesto el chip ignora el canal alfa de la textura y lo toma como 1.0. **No es el bit 20**
(«Use Alpha»), que fuerza el alfa del *vértice*: son dos cosas distintas y el árbol ya se equivocó
una vez usando el 20 como interruptor del blending.

Cambia la regla de salida de los cuatro entornos, y en el modo 2 también el RGB, porque la
interpolación por TEXA se colapsa a la textura sola:

| modo | con TEXA vivo | con TEXA = 1 |
| --- | --- | --- |
| 0 decal | RGB=TEX, A=TEXA | RGB=TEX, A=1 |
| 1 modulate | RGB=COL×TEX, A=TEXA | RGB=COL×TEX, A=1 |
| 2 decal alpha | RGB=mezcla por TEXA, A=COLA | RGB=TEX, A=COLA |
| 3 modulate alpha | RGB=COL×TEX, A=COLA×TEXA | RGB=COL×TEX, A=COLA |

**No se puede hornear en la textura**: es por polígono, y dos polígonos pueden compartir textura con
distinto valor — la misma trampa que ya costó el relieve. En el shader es una línea; en función fija
son ocho ramas de `GL_COMBINE`, con «A = 1» dicho como REPLACE desde `GL_CONSTANT` (por eso el color
del entorno se fija una vez con alfa 1). **Y la clave de la sombra de estado lleva el bit pegado al
modo**: dos tiras con el mismo entorno y distinto bit no comparten estado, y con la clave vieja la
segunda se saltaba su propia programación por «ya estaba puesto».

**Lo pide más de la mitad de las tiras con textura en cinco juegos** — Virtua Tennis 2 el 87 %,
Tennis 2K2 el 82 %, Dead or Alive 2 el 66 %, Virtua Tennis el 53 %, Crazy Taxi el 51 % — y esa cifra
sola engaña. La que vale es cuántas lo ponen sobre una textura que **de verdad tiene** canal alfa,
porque sobre una RGB565 el texel ya sale opaco y apagarle el alfa no cambia un píxel:

| juego | bit 19 | sobre textura con alfa |
| --- | --- | --- |
| Street Fighter III | 54 127 | **44 383** |
| Dead or Alive 2 | 415 525 | 11 441 |
| Crazy Taxi | 220 032 | 3 565 |
| Tennis 2K2 | 159 498 | 2 052 |
| Virtua Tennis 2 | 130 254 | 1 980 |
| Virtua Tennis | 100 549 | 153 |
| 4X4 EVO, Mat Hoffman | 2 630 | **0** |

**Y aun así no cambia ninguna de las capturas medidas**, ni las doce demos de control ni los seis
juegos. Eso es un resultado, no una decepción: quiere decir que en esos cuadros los texeles de las
texturas con alfa ya venían opacos. Queda implementado y correcto —los dos caminos coinciden entre
sí— y Street Fighter III es donde tiene más oportunidad de importar. Un cuadro donde se note pedía
más corridas de las que se hicieron.

### Bit 21 — el recorte de color (implementado en el camino programable, y **inerte**)

Con el bit puesto el color del píxel se recorta entre `FOG_CLAMP_MIN` (`0x005F80C0`) y
`FOG_CLAMP_MAX` (`0x005F80BC`), cada uno ARGB8888, **después de la niebla**. Los límites valen para
la escena entera y el bit es por tira, así que se suben una vez por escena como la tabla de niebla y
por la misma razón.

**Lo pide un solo juego, y masivamente: Dead or Alive 2, 497 412 de sus 633 057 tiras con textura,
el 79 %.** Ninguno de los otros trece lo toca. Con eso parecía el candidato obvio para sus sombras.

**Y no lo es, porque los límites que pone son `MIN = 00000000` y `MAX = ffffffff`.** Recortar al
rango completo es exactamente lo que el hardware hace igual: el bit está encendido en cuatro de cada
cinco tiras del juego y no puede cambiar un solo píxel. Medido, no deducido — `--render=fbo` (sin
recorte) contra `--render=shader` (con recorte) difieren en **123 píxeles de 305 920**, y ésos son
la niebla por píxel, que difiere a propósito.

Eso deja el bit en la misma categoría que el 19: implementado, correcto, y sin efecto en el parque
actual. La diferencia con dejarlo sin hacer es que ahora **se sabe** que no es la causa de nada, en
vez de figurar como sospechoso; y si aparece un juego que ponga límites de verdad, funciona.

Sólo existe en el camino programable: la función fija no lo puede expresar sin una segunda pasada.
Con los límites abiertos eso no crea ninguna discrepancia entre caminos, que era la objeción para no
hacerlo a medias.
