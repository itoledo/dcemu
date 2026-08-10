# Plan: el núcleo ARM del AICA

Estado: **investigación medida, sin decidir el rumbo**. Escrito el 2026-08-03 sobre
`rendimiento-hilos`, después de que
[`rendimiento-plan.md`](rendimiento-plan.md) cerrara la fase de gráficos y dejara al ARM7
como la partida más grande que no es el intérprete del SH-4.

## Por qué el ARM

El reparto de `--perf` en Crazy Taxi en juego (1183 tiras por escena, 180 s emulados,
Release, i9-13900):

| | % del tiempo real |
| --- | --- |
| intérprete SH-4 | 72,9 |
| **ARM7 del AICA** | **14,2 – 15,3** |
| camino gráfico entero | 7,6 |
| mezcla del AICA | 0,9 |

O sea que **el ARM solo cuesta el doble que todo el pipeline de gráficos**, y es lo único
del emulador que se puede atacar sin tocar el intérprete.

La cifra cruda: **2 161 263 641 pasos** en unos 18 000 ms, o sea **8,3 ns por instrucción
del ARM** — del orden de 40 ciclos del anfitrión por instrucción emulada. Un intérprete de
ARM7 razonable anda entre 5 y 15.

Y el ARM corre en **todo**, no solo en las demos de sonido: `spu_init()` suelta el reset en
cualquier programa de KOS y el boot ROM lo arranca tres veces antes de llegar al menú.

## El instrumento que faltaba, y la cifra que engañaba

`perf_arm_ocioso` contaba los pasos en que el PC no se movió, o sea **el salto a sí mismo**
— que es como espera el firmware de KOS, y lo que `spu_init()` deja en la dirección 0. Con
un juego esa cifra da **0,0 %**, y de ahí salía la lectura de que el ARM está trabajando.

**Es falsa.** Cero ahí solo significa "no espera de *esa* forma". Un lazo de cuatro
instrucciones que sondea una tabla tampoco mueve la cifra y es igual de salteable.

`DCEMU_PERFIL_ARM=1` (ver `arm7.h`) agrega los dos histogramas que hacían falta: por
dirección y por fila de la tabla de despacho.

## Lo que el ARM hace de verdad

Por fila de la tabla, sobre 2 161 083 660 pasos:

| fila | pasos | % |
| --- | --- | --- |
| B/BL | 555 131 721 | 25,6 |
| LDR/STR # | 397 183 787 | 18,3 |
| ALU # | 364 301 337 | 16,8 |
| ALU2 | 234 496 766 | 10,8 |
| CMP # | 206 348 567 | 9,5 |
| LDM/STM | 183 217 467 | 8,4 |
| ALU2 # | 85 971 620 | 3,9 |
| MRS | 35 975 215 | 1,6 |
| MSR | 35 975 215 | 1,6 |
| CMP | 25 681 946 | 1,1 |
| ALU | 24 866 927 | 1,1 |
| MUL/MLA | 5 939 449 | 0,2 |
| LDR/STR R | 5 993 643 | 0,2 |

Un cuarto de todo son saltos: la firma de lazos cortos. Y `MRS` y `MSR` dan **exactamente
la misma cuenta**, que es la firma de una sección crítica — guardar el CPSR, enmascarar,
restaurar — treinta y seis millones de veces.

Las direcciones lo confirman. Las veinte más ejecutadas se llevan el **51,8 %** de los
pasos, y 3729 direcciones distintas el 48,2 % restante. Las veinte no son veinte lugares:
son **tres lazos**.

### Lazo A — 0x6294…0x658c, 94 439 120 vueltas, ~26 % de los pasos

```
6294  e5d83000  LDRB R3,[R8]        ; el primer byte del registro
6298  e1b03003  MOVS R3,R3
629c  0a0000b8  BEQ  0x6584         ; si es cero, saltarse el cuerpo entero
  ...
6584  e2888030  ADD  R8,R8,#0x30    ; el siguiente, 48 bytes mas alla
6588  e2599001  SUBS R9,R9,#1
658c  1affff40  BNE  0x6294
```

La cuenta del `BEQ` y la de la cola son **la misma al dígito**: 94 439 120. O sea que el
cuerpo **no se ejecuta nunca** en toda la corrida. Es un barrido puro de "¿hay algo que
hacer?" sobre una tabla de registros de 48 bytes.

### Lazo B — 0x0a04…0x0a1c, ~20 % de los pasos

```
0a04  e5da0000  LDRB R0,[R10]
0a08  e3100080  TST  R0,#0x80
0a0c  1a000008  BNE  0x0a34         ; hay trabajo
0a10  e28aa040  ADD  R10,R10,#0x40
0a14  e2899001  ADD  R9,R9,#1
0a18  e3590040  CMP  R9,#0x40       ; 64 entradas
0a1c  1afffff8  BNE  0x0a04
```

64 entradas de 64 bytes, probando el bit 7. Encuentra trabajo en el **4,5 %** de las
vueltas (63 542 335 cargas contra 60 688 464 colas).

### Lazo C — 0x09d4…0x09ec, ~3 %

El mismo patrón con paso 0x60 y un límite en R4.

### La conclusión

**Cerca de la mitad de los 2161 millones de pasos son barridos de sondeo que casi nunca
encuentran nada.** Eso es lo que hay que decidir qué hacer con.

## Los tres caminos, y lo que se sabe de cada uno

### 1. Acelerar el intérprete

Lo obvio, y **mucho menos rentable de lo que el código sugiere**. Dos cosas se pagaban en
cada uno de los 2161 millones de pasos:

- **la búsqueda de instrucción armaba la palabra byte a byte** — cuatro cargas, tres
  desplazamientos y tres OR — por independencia del orden de bytes del anfitrión;
- **`aica_fiq_pendiente()` es una llamada entre unidades de traducción** (el build no tiene
  LTCG) que adentro arma dos registros de 16 bits byte a byte.

Lo primero ya está hecho y medido: ver abajo. Lo segundo está sin medir.

### 2. No ejecutar el sondeo

Es donde está la mitad del tiempo, y `perf.h` ya lo anticipaba: *"si domina, lo que hay que
hacer no es mover el ARM de hilo sino no ejecutarlo — que es mucho más barato"*.

La forma honesta no es un caso especial para estas direcciones —eso ata el emulador al
firmware de un juego— sino la misma disciplina que ya usa la caché de texturas: **un
contador de generación**. Si el ARM vuelve a entrar al mismo lazo con el mismo estado de
registros y **nadie escribió la memoria que el lazo lee** desde la vuelta anterior, el
resultado es idéntico por construcción y las vueltas restantes del lote se pueden saltar.

Lo que hay que resolver antes de escribir una línea:

- **Quién escribe lo que el lazo lee.** El SH-4 escribe RAM de onda 7 492 481 veces en la
  corrida (62 984/s) contra los ~878 000 sondeos por segundo del ARM, así que el margen
  existe; pero hay que demostrar que las tablas están en RAM de onda y no en el archivo de
  registros del AICA, que cambia con cada muestra.
- **Cómo se reconoce un lazo sin atarse a direcciones.** Un salto hacia atrás corto cuyo
  cuerpo no escribió memoria ni cambió más registros que los del propio lazo.
- **La baranda.** El `.wav` de `--captura-audio` es determinista y bit a bit: si saltarse el
  sondeo cambia una muestra, el diseño está mal. Esa es la prueba de aceptación y no hay
  que inventarla.

Riesgo alto, techo alto (hasta un 7 % de la corrida entera).

### 3. LTCG

`CMakeLists.txt` no tiene `/GL` ni `/LTCG`. Es la fase 1.2 de `rendimiento-plan.md`, nunca
hecha, y aquí importa por una razón concreta: **`aica_fiq_pendiente()` y `aica_arm_leer()`
viven en `aica.c` y el bucle en `arm7.c`**, así que hoy no se pueden inlinear. Es un flag de
build, riesgo de código cero, y las barandas del árbol (21 suites, SingleStepTests bit a
bit, el barrido de 150 demos) son exactamente el aparato para validarlo.

Y no beneficia solo al ARM: el intérprete del SH-4 —el 72,9 %— tiene la misma forma, con los
manejadores en `mov.c`, `arith.c`, `logic.c`… y el bucle en `main.c`.

### 4. El hilo, que ya existe y pierde

`--hilos` saca el AICA y el ARM7 a su propio hilo (`hilo_aica.c`) y está **apagado por
omisión porque medía más lento** (−4 a −5 %). `--perf` estima el techo en 1,19×. Con el
sondeo identificado, el orden correcto es al revés de como se intentó: **primero no ejecutar
lo que no hace falta, y recién después considerar mover a otro hilo lo que quede**. Enhebrar
un trabajo que la mitad del tiempo no hay que hacer es pagar sincronización por nada.

## Lo medido hasta ahora

### La búsqueda de instrucción: 1,9 % del ARM, 0,28 % de la corrida

`DCEMU_ARM_POR_BYTES=1` vuelve al armado byte a byte, en el **mismo binario**, para poder
alternar en la misma tanda —`perf_ns_arm` varía un 10 % entre corridas idénticas, así que
comparar contra otro binario de otro momento no decide nada—. Los dos caminos pagan la misma
rama, o sea que la diferencia **subestima** la ganancia real.

Tres vueltas alternadas, con las mismas 9994 escenas y 1183 tiras en las seis:

| | ARM (ms) | total (ms) |
| --- | --- | --- |
| una palabra de una vez | 17 932 / 17 996 / **18 089** | 119 039 / **120 133** / 120 835 |
| byte a byte | **18 321** / 18 336 / 18 375 | 119 705 / **119 905** / 120 064 |

Los dos grupos **no se solapan** —el peor de los rápidos, 18 089, está por debajo del mejor
de los lentos, 18 321— así que la diferencia es real: **340 ms sobre las medianas, el 1,9 %
del tiempo del ARM y el 0,28 % de la corrida**.

Dos cosas que aprender de aquí, y las dos valen más que la cifra:

- **La forma del código exageraba el problema.** Cuatro cargas de byte con sus
  desplazamientos y sus OR *parecen* caras, y MSVC ya reconocía buena parte del patrón. Un
  cambio que "obviamente" tenía que valer mucho vale poco.
- **En el tiempo total la mediana sale al revés** (120 133 el rápido contra 119 905 el
  lento). El ruido entre corridas de esta máquina es de ±1 % y se traga un efecto de 0,3 %:
  sin el contador propio del ARM, esta medición habría concluido lo contrario de lo que pasa.
  Un desglose interno no es un lujo, es lo que hace medible un cambio chico.

### LTCG: 10,0 % del ARM y 1,4 % de la corrida — el más grande de los tres

`cmake -S . -B build-ltcg -A x64 -DDCEMU_LTCG=ON`, alternado contra el build normal. Las
seis corridas hicieron **exactamente** el mismo trabajo: 22 280 053 420 instrucciones, 9994
escenas y 1183 tiras en las seis, al dígito.

| vuelta | total sin / con | ARM sin / con |
| --- | --- | --- |
| 1 | 119 505 / 118 223 | 18 143 / 16 402 |
| 2 | 119 855 / 117 773 | 18 228 / 16 374 |
| 3 | 119 404 / 117 665 | 18 107 / 16 321 |

**Los tres pares no se solapan en ninguna de las dos columnas** —la peor corrida con LTCG es
mejor que la mejor sin él—, así que aquí no hace falta discutir el ruido. Sobre las medianas:
**1632 ms totales (1,4 %) y 1826 ms del ARM (10,0 % de él)**.

Y el reparto de esa ganancia es la parte interesante: **está casi toda en el ARM**. Los
nanosegundos por instrucción del SH-4 pasan de 5,3 a 5,2-5,3, o sea nada apreciable, y el
cuadro gráfico baja un 1 %. Eso **contradice la expectativa de `rendimiento-plan.md`**, que
esperaba «15-30 % sobre Release» sobre todo por los manejadores del intérprete: el
optimizador ya resolvía bien ese caso, y donde LTCG paga es exactamente donde el perfil dijo
que estaba el problema —una llamada entre unidades de traducción en un bucle apretado—.

#### Validado con las barandas del árbol

- **Las 20 suites de `ctest` pasan** bajo LTCG (`arm7` 20/20 y `aica` 31/31 entre ellas).
- **SingleStepTests: 113 191 ok, 0 fallan, 3306 divergen a propósito, 3 descartados** — y el
  build **sin** LTCG da la misma línea al dígito. O sea que el flag no mueve un bit del
  núcleo del SH-4, floats incluidos, que es lo que un cambio de flags del compilador obliga
  a demostrar. (De paso: el documento dice 3221 divergencias y hoy son 3306; la deriva es
  anterior a esto y no la causa LTCG, porque los dos builds coinciden.)
- **Una captura de Crazy Taxi sale con el mismo SHA-256** con LTCG y sin él
  (`DDD470A4…`), que además es el mismo que daba el árbol antes de todo este trabajo.
- El dato de SingleStepTests está en `D:\dev\sh4-tests` en esta máquina; `ctest` lo salta
  porque `DCEMU_SH4_JSON` no está configurado en ninguno de los dos directorios de build.
  **Conviene configurarlo**: sin él, la baranda más fuerte del árbol no corre sola.

Lo que **falta** antes de encender LTCG por omisión es el barrido de 150 demos. No se corrió.
Por eso la opción queda en OFF: el número está medido, la adopción no está validada.

## Lo que hay que medir antes de decidir el rumbo

1. **Cuánto vale la comprobación de FIQ por instrucción**, ahora que LTCG la inlinea. Buena
   parte de ese 10 % probablemente sea eso; si lo es, hacerlo explícito —una línea de nivel
   mantenida por `aica.c`, como `aica_linea_asic`— lo gana **sin** depender del flag y sin
   atarse a MSVC. Una sonda que la mire una vez por lote da el techo sin comprometerse.
2. **Dónde viven las tablas que los tres lazos sondean.** Un watchpoint del lado del ARM
   sobre `R8`/`R10` contesta si es RAM de onda o el archivo de registros, y de eso depende si
   el camino 2 —no ejecutar el sondeo, hasta un 7 % de la corrida— es viable.
3. **El barrido de 150 demos con LTCG**, que es lo único que falta para poder adoptarlo.

## Resumen de lo medido

| | del ARM | de la corrida |
| --- | --- | --- |
| LTCG | −10,0 % | −1,4 % |
| la búsqueda de una palabra de una vez | −1,9 % | −0,28 % |
| no ejecutar el sondeo (sin implementar) | hasta −50 % | hasta −7 % |
| el sondeo, implementado (la memoización, abajo) | −6,8 % de los pasos | −0,49 % |
| **la predecodificación** (última sección) | **−25 a −33 %** | **−3,3 a −5,1 %** |

---

# El censo de la RAM de onda: el camino 2 es viable, pero no por donde parecía (2026-08-05)

El camino 2 —no ejecutar el sondeo— tenía tres cosas que resolver **antes de escribir una
línea**, y la primera era la que decidía: *«hay que demostrar que las tablas están en RAM de
onda y no en el archivo de registros del AICA»*, y que quien escribe no pisa lo que el lazo
lee. Ya está medido.

## El instrumento

`DCEMU_SONDA_ONDA=1`, un censo por páginas de 1 KB de los 2 MB de RAM de onda. Cuenta, por
página, las **lecturas de datos del ARM** y las **escrituras de quien sea** — los tres que
escriben están enganchados: el propio ARM, el SH-4 y el DMA del G2 (los dos por `mem.c`), y el
DMA interno del AICA. Un `memcpy` que cruza páginas marca todas las que toca: contar de menos
las páginas sucias es justamente el error que haría parecer segura una elisión que no lo es.

Para que el censo signifique algo hubo que **separar la búsqueda de instrucción de la lectura
de datos** (`arm7_buscar()`): el ARM ejecuta desde la misma RAM que sondea, así que mezclarlas
tapaba exactamente la separación que se venía a buscar. Es de paso el camino corto que pedía
el punto 1.4 de este documento.

## Los números

| | Crazy Taxi (60 s) | DCDoom (35 s) |
| --- | --- | --- |
| lecturas de datos del ARM | 141 261 596 | 93 122 054 |
| ... en páginas **nunca** escritas | **0 (0,00 %)** | **0 (0,00 %)** |
| ... **sin cambio desde la lectura anterior** | **110 905 948 (78,51 %)** | **70 115 839 (75,29 %)** |
| lecturas del ARM a registros del AICA | 3 854 875 (2,65 %) | 71 592 (0,08 %) |
| páginas leídas / escritas | 66 / 2031 | 57 / 1781 |

## Lo que dicen, que no es lo que la pregunta original suponía

**El sondeo no vive en el archivo de registros.** 97,4 % y 99,9 % de las lecturas de datos del
ARM van a RAM de onda. Eso era lo que podía matar el camino 2 de entrada, y no pasa.

**La pregunta pesimista da cero, y no significa nada.** Ninguna página que el ARM lee quedó
sin escribir en toda la corrida — pero una página escrita una vez al cargar el juego cuenta
como sucia para siempre bajo ese criterio. La pregunta que el mecanismo hace de verdad es otra
—*¿la escribió alguien desde que la leí la vez pasada?*— y ahí da **75-79 %**.

**Y el mapa por página es la evidencia fuerte.** El ARM lee 66 páginas de 2048; se escriben
2031. Las tablas de sondeo se leen entre cientos y decenas de miles de veces por escritura:

| página (Crazy Taxi) | lecturas | escrituras | proporción |
| --- | --- | --- | --- |
| `00B000` | 46 985 478 | 37 048 068 | **1,3 : 1** |
| `00DC00` | 29 508 712 | 60 436 | 488 : 1 |
| `000800` | 15 118 057 | 256 | **59 055 : 1** |
| `00C800` / `00CC00` | 7 548 848 | 528 | 14 297 : 1 |

En DCDoom la página `00AC00` tiene **33 909 390 lecturas y 33 909 391 escrituras** —una más que
la otra— que es la firma de un lee-modifica-escribe sobre la misma posición, 34 millones de
veces. Es el borrador del propio ARM, no una tabla, y no se va a poder limpiar nunca.

## Consecuencia de diseño, que cambia lo que había que escribir

**La invalidación tiene que ser por página, y una global sería inútil.** El borrador del ARM se
escribe tanto como se lee; un solo contador de generación para toda la RAM de onda estaría
sucio permanentemente y el mecanismo no dispararía jamás. Por página funciona porque el cuerpo
del sondeo no toca el borrador: lee la tabla y nada más.

## Lo que queda por resolver, y una salvedad honesta del instrumento

**El 75-79 % es por lectura, no por barrido.** Un barrido sólo se puede saltear si **todas** las
páginas que recorre están limpias, y el lazo A recorre una tabla de 48 bytes de paso que puede
cruzar varias. Si cada página estuviera limpia con probabilidad independiente, cinco páginas
darían 0,78⁵ ≈ 29 %, no 78 %. La cifra real está entre esos dos extremos y **no se puede
separar sin detectar los barridos**, que es justo lo que falta implementar. O sea: esto
demuestra que el mecanismo es viable y por dónde, no cuánto va a rendir.

Siguen abiertas las otras dos preguntas del camino 2: **cómo se reconoce un lazo sin atarse a
direcciones** —un salto hacia atrás corto cuyo cuerpo no escribió memoria ni cambió más
registros que los del propio lazo— y la memoización en sí: clave `(PC de cabecera, estado de
registros de entrada)`, validez `(generaciones de las páginas leídas)`, y de resultado el
estado de salida más los ciclos. La baranda no hay que inventarla: el `.wav` de
`--captura-audio` es determinista bit a bit.

---

# El camino 2, implementado: elide 6,8 % de los pasos del ARM y vale 0,5 % (2026-08-05)

Está en el árbol, encendido, y `DCEMU_SIN_MEMO_ARM=1` lo apaga. **Rinde bastante menos de lo
que este documento estimaba** —0,5 % contra «hasta un 7 %»— y el porqué está abajo, porque es
más interesante que la cifra.

## Qué hace

Memoiza barridos enteros. Cuando el ARM toma un **salto hacia atrás** corto, lo que falta del
barrido está determinado por (registros de entrada, memoria); si ya se vio ese mismo estado y
nadie escribió las páginas que el barrido lee, se repone el estado de salida de una vez en vez
de interpretar noventa instrucciones.

**La clave es el borde de atrás y no la entrada al lazo.** Cuesta interpretar una vuelta y
saltear las otras N-1, y a cambio la detección vive en un solo lugar —`op_salto()`— en vez de
tener que reconocer cuándo se entra al lazo desde afuera.

Aborta la grabación cualquier cosa que rompa la pureza: una escritura, un acceso al archivo de
registros del AICA, un cambio de PC que no sea del lazo, una excepción, más de ocho páginas o
más de 8192 instrucciones. Y para reponer hacen falta dos condiciones más: que no haya FIQ
pendiente —reponer se saltea las comprobaciones de cada instrucción, y como el barrido no toca
el AICA su estado no puede cambiar durante él— y que el barrido quepa en los ciclos que quedan.

## Lo que rinde

| Crazy Taxi, banco canónico (3 pares, orden alternado) | media | rango |
| --- | --- | --- |
| sin memoización | 106 591 ms · 1,68× | 106 366 - 106 773 |
| **con memoización** | **106 069 ms · 1,69×** | 105 877 - 106 294 |

**−0,49 %**, cuatro pares de cuatro a favor contando el descartado, y rangos disjuntos por poco.
El A/B es **el mismo binario en las dos ramas** —la elige una variable de entorno— así que por
una vez la disposición del binario no es una variable.

Elide **145 998 803 instrucciones del ARM de 2 161 263 753**, o sea el 6,8 %, a 91,7
instrucciones por reposición. En DCDoom elide **cero**: sus lazos escriben, y un barrido que
escribe no se puede reponer.

## Por qué 0,5 % y no 7 %

La estimación de este documento suponía saltear **la mitad** de los pasos del ARM. Se saltea un
séptimo de eso, y el ARM es el 15 % de la corrida: 6,8 % × 15 % ≈ 1 %, y medido sale 0,5 %.

Lo que come la diferencia son los **222 296 barridos sucios contra 561 969 repuestos**: casi un
tercio de las veces que la clave coincide, alguien escribió una página que el barrido lee. El
censo por páginas ya lo anticipaba —75-79 % por lectura, pero un barrido necesita **todas** sus
páginas limpias a la vez— y esa era la salvedad que quedó anotada.

## Los tres errores, que son lo que hay que leer de todo esto

**Un ciclo de más por reposición cambiaba el audio.** `memo_ciclos` empieza a contar en el mismo
salto hacia atrás que dispara la grabación, así que ya incluye sus tres ciclos; sumarle el 1 con
el que `arm7_paso()` arranca cobraba uno de más. Con 4603 reposiciones son 4603 ciclos en tres
minutos de emulación —nada— y alcanzaba para correr una frontera de muestra y cambiar el `.wav`
entero. **Ninguna otra baranda del árbol lo habría visto**: las 21 suites pasaban, los 113 191
casos de SingleStepTests pasaban, la captura de GL era idéntica y el juego se veía igual.

**El filtro de cabeceras se anulaba a sí mismo.** Al dejar que cualquier cabecera nueva se
quedara con su ranura, dos lazos separados por 4 KB se reseteaban el contador mutuamente y
ninguno llegaba nunca al umbral: la elisión cayó de 8,3 % a **0,03 %** sin que nada más
cambiara. Ahora una cabecera que ya sirvió no se deja desplazar. El filtro sigue haciendo falta:
sin él se intentaba grabar en cada salto hacia atrás y los abortos eran **4 989 019**; con el
envenenamiento de las cabeceras que abortan ocho veces son 284 198.

**Una llamada por escritura del ARM, pagada también con el mecanismo apagado.**
`arm7_escribir()` llamaba a `arm7_memo_abortar()` y a `onda_marcar_escritura()` siempre, y esas
son decenas de millones de llamadas que **no aparecen en el A/B** porque las pagan las dos
ramas. Ahora el centinela se mira antes de llamar y la escritura de una sola página es un
incremento en línea.

## Y una trampa de medición nueva

**`--sin-audio` mueve la corrida de 1,72× a 1,14×**, un 50 %. Se puso para poder capturar el
`.wav` y con eso el primer A/B quedó midiendo en un régimen que no es el del banco — dio −0,09 %,
o sea ruido, contra el −0,49 % consistente del régimen normal. La captura de audio y el
cronómetro **no pueden ir en la misma corrida**, igual que `--captura-gl`.

## Barandas

`.wav` de `--captura-audio` bit a bit en los dos guests, verificado además contra el mismo
binario con `DCEMU_SIN_MEMO_ARM=1` —que es lo que prueba que la comparación aísla el mecanismo—;
`ctest` 21/21; `dcemu_sh4json` 113 191 ok / 0 fallan; y la captura de GL de DCDoom en
`36578F59…`.

---

# La predecodificación: 3,3-5,1 % de la corrida entera (2026-08-09)

Es la fase 4 de `estado-del-arte-plan.md` («caché de predecodificación primero») y **la mayor
ganancia del ARM7 en la historia del árbol** — LTCG valía 1,4 % de la corrida, la memoización
0,5 %. Está encendida por omisión; `DCEMU_SIN_PREDECO_ARM=1` la apaga en el mismo binario, que
es el A/B.

## Qué paga

La tabla de despacho ya evitaba decodificar el patrón, pero **cada manejador volvía a extraer
sus campos de la palabra en cada ejecución**: `op_salto` extendía el signo del desplazamiento
555 millones de veces por corrida, `op_datos` rotaba el inmediato, separaba la forma del
operando y calculaba acarreo y desborde aun con S=0, y `op_bloque` contaba los bits de la
lista en un lazo de dieciséis vueltas — 183 millones de veces. Nada de eso depende del estado:
es función pura de la palabra.

## Cómo funciona

Una entrada de 24 bytes por palabra de la RAM de onda (512 K entradas, 12 MB en `.bss`):
la palabra cruda, los campos ya extraídos y un manejador especializado por forma
(`d_salto`, `d_alu_imm_s0/s1`, `d_alu_reg_s0/s1`, `d_ldr_imm`, `d_str_imm`, `d_bloque`,
`d_mrs`, `d_msr`; lo frío cae en `d_generico`, que despacha por la tabla vieja). El núcleo
de la ALU está factorizado en `alu_nucleo()` para que las tres formas no puedan derivar; la
especialización S=0/S=1 elimina el cálculo muerto de acarreo/desborde.

Tres decisiones que no son de gusto, cada una con su porqué:

- **La validez es comparar la palabra guardada contra la que la memoria tiene ahora** — la
  regla de `jit_verificar()` — y no un gancho de invalidación, porque tres escritores tocan
  la RAM de onda sin pasar por `arm7_escribir()`: el DMA interno del AICA (`aica.c`), el DSP
  (`aicadsp.c`) y la suite, que mete los programas con `memcpy`. La búsqueda ya carga la
  palabra en cada paso, así que validar cuesta una comparación, no una carga extra.
- **El salto guarda el desplazamiento relativo, no el destino**: las entradas se indexan por
  palabra física y los 2 MB se repiten en la ventana de 16 — dos PC distintos pueden ejecutar
  la misma palabra (el lazo de `spu_init()` que da la vuelta al bus lo hace de verdad).
- **La tabla arranca entera como la decodificación de la palabra 0** (AND EQ R0,R0,R0, lo que
  esos bytes significan): no existe un valor de palabra imposible con el que marcar una
  entrada fría, así que la única forma de que la comparación sea la única condición es que
  «frío» y «palabra 0 de verdad» decodifiquen igual — y los ceros se ejecutan, ver arriba.

## Las compuertas, todas en verde

- `ctest` 23/23 con la predecodificación puesta (la suite del ARM la ejercita: `arnes.c`
  llama a `arm7_init()`).
- **Los pasos del ARM y el histograma entero, idénticos al dígito** con la palanca en las dos
  posiciones: 376 860 131 pasos en DCDoom 35 s, 37 líneas de perfil iguales. Y el mecanismo
  trabaja: **2449 palabras decodificadas en esos 377 millones de pasos** — ~154 000 reusos por
  decodificación.
- Capturas GL **canónicas** en los árbitros inmunes al pad, en las dos formas: DCDoom
  `198B396F…` (traductor e intérprete), Sega Rally 2 `1B28D0D9…`; totales `jit:` al dígito.
- `.wav` byte a byte: Crazy Taxi con el reverb del banco (`405689A4…`) y cpp-modplug
  (`81C62F1C…`). En CT el conteo del SH-4 osciló +101/−843 instrucciones en 20 mil millones
  con el `.wav` intacto: la bimodalidad documentada del pad XInput, no una divergencia — y
  modplug, que es puro ARM, salió al dígito.

## La tanda (binario `58A1EF76…`, reentrenado, traductor, orden alternado)

| guest | predeco | sin | ganancia |
| --- | --- | --- | --- |
| DCDoom 35 s | 31 878 / 32 053 / 32 263 | 33 072 / 33 234 / 33 816 | **−3,6 %** |
| Crazy Taxi 180 s | 92 734 / 93 277 | 97 904 / 98 126 | **−5,1 %** |
| Sega Rally 2 60 s | 65 864 / 66 674 | 68 509 / 68 531 | **−3,3 %** |

**Rangos disjuntos en los tres** — el peor con predecodificación queda por debajo del mejor
sin ella — con el trabajo idéntico al dígito dentro de cada modo. Contra el propio ARM
(9-18 % de la corrida bajo el JIT), el escalón le quitó **entre un cuarto y un tercio de su
costo**.

## Lo que cambia del mapa

El camino 1 («acelerar el intérprete») pasó de «mucho menos rentable de lo que el código
sugiere» a **el más rentable que ha tenido este subsistema**, y la diferencia con las
mediciones de arriba es dónde ataca: la búsqueda byte a byte valía 1,9 % del ARM porque
MSVC ya reconocía el patrón; la extracción de campos y el popcount no los podía quitar
ningún compilador, porque rehacerlos era la semántica del código.

## El reparto rehecho, y una confirmación cara de la regla de los binarios

El corchete del ARM bajo `--perf`, **dentro del binario reentrenado** (DOOM 35 s, palanca):
**5074 ms sin predecodificación → 3759 ms con ella, −25,9 %** — coherente con la tanda. La
lectura ingenua contra los logs de la fase 0 decía lo contrario (+1 a +7 % de ARM), porque
esos logs son de **otro binario** — anterior al superbloque, al lote del censo y a dos
reentrenamientos —: es la comparación que la regla del árbol prohíbe, y aquí se la vio
fabricar un empeoramiento de la nada. Los `--perf` de referencia con la predecodificación
puesta quedan en `logs/perfil-jit-*.txt`.

El mapa vigente (instrumentado, metodología de la fase 0): ARM7 **10,9 %** en DOOM,
**18,9 %** en CT, **8,9 %** en SR2 — la porción se sostiene aunque el ARM se abarató,
porque el SH-4 se aceleró 17-25 % entre medio (fases 2-3 del plan). Sigue por encima del
umbral de parada del plan (~2-3 %), así que **el traductor ARM7→x64 sobre `jit_x64.c` queda
abierto como segundo escalón de la fase 4**: emisión por identidad de manejador del
intérprete de este archivo — la receta que ya funcionó dos veces —, con la tabla de
predecodificación como forma decodificada de entrada, suite nueva comparando traductor
contra intérprete paso a paso, y las mismas compuertas de esta ronda (`.wav`, histogramas,
capturas canónicas). Palanca prevista: `DCEMU_SIN_JIT_ARM=1`.

## El diseño del escalón 2, y los cuatro teoremas que lo hacen exacto

El escalón intermedio es **bloques en C sobre la predecodificación** — la escalera de la
fusión → JIT del SH-4: construye toda la infraestructura del traductor (descubrimiento,
validez, presupuesto, salidas laterales, despachador) siendo medible por sí sola, y la
emisión x64 la reutiliza entera si el reparto la sigue pidiendo. Un bloque es un tramo
recto de instrucciones **que no pueden tocar PC ni el modo** (análisis estático sobre la
entrada predecodificada), ejecutado con una verificación por entrada y un solo chequeo de
FIQ, avanzando PC de a 4 sin preguntar.

Los teoremas, cada uno con su porqué:

1. **La FIQ solo puede cambiar de estado, dentro de un lote, cuando el ARM toca el archivo
   de registros del AICA o escribe CPSR.** `aica_tick()` corre entre lotes, no adentro; el
   SH-4 tampoco (un solo hilo). Es la misma observación que ya usa la memoización. Entonces:
   chequear FIQ una vez a la entrada del bloque + terminar el bloque en los escritores de
   CPSR (estático) + salida lateral tras cualquier acceso al archivo (dinámico, ver 4) ≡
   chequear en cada frontera.
2. **El presupuesto: entrar al bloque solo si sus ciclos máximos caben en los que quedan.**
   El intérprete arranca la instrucción i si le queda saldo; si el costo máximo del bloque
   entero ≤ saldo, todo prefijo también cabía, así que el punto donde el lote se detiene es
   el mismo. Sin esta compuerta, el ARM se adelantaría dentro de la ventana de muestra y la
   FIQ del lote siguiente lo encontraría en otro PC — la misma regla que la reposición de
   la memoización.
3. **La memoización es transparente y convive**: reponer suma las mismas cuentas que
   ejecutar. Los bloques no corren mientras se graba (`arm7_memo_fin != ~0`), y el borde de
   atrás — donde se decide grabar y reponer — es un salto, que siempre ejecuta por el
   intérprete. Nada de la memoización cambia.
4. **Un acceso con dirección dinámica puede caer en el archivo de registros** (bit 23), y
   ahí el estado de FIQ pudo cambiar: el manejador de memoria deja una bandera y el bloque
   sale por el costado en esa frontera — que es exactamente la frontera donde el intérprete
   habría mirado. El censo dice que es el 0,08-2,65 % de las lecturas: la salida es fría.

Terminan el bloque (estático, sobre la entrada predecodificada): `d_salto`, `d_msr`,
`d_generico` entero (MUL/SWP/SWI/indefinidas/formas Rs pueden escribir PC o levantar
excepción), ALU con rd=15 que escribe (códigos fuera de 8-11), LDR con rd=15, writeback
sobre R15, LDM con PC en la lista o con CPSR final. Las condicionales entran (la condición
se evalúa adentro, fallada = 1 ciclo, igual que el intérprete). PC avanza +4 por
instrucción **siempre** — los manejadores leen `LEER_R(15)` relativo al paso, así que no es
elidible en C; bakearlo es trabajo de la emisión x64.

## Los bloques en C, medidos: −0,4 a −1,8 %, y la infraestructura queda (2026-08-10)

Implementados en `arm7.c` (`arm7_blq_*`: 4096 ranuras directas, bloques de 2 a 12,
verificación por `memcmp` de las palabras a la entrada — acotada para no cruzar ni hacia el
archivo de registros ni un espejo de los 2 MB, porque el memcmp es lineal). Encendidos por
omisión; `DCEMU_SIN_BLOQUES_ARM=1` los apaga (y apagar la predecodificación los apaga
también: ejecutan por sus entradas).

**Compuertas, todas en verde**: ctest 23/23; capturas canónicas en DOOM (las dos formas) y
SR2; `.wav` byte a byte en CT y modplug; totales al dígito en las dos ramas; histograma del
ARM idéntico (la única línea que difiere es el contador de decodificaciones: 2594 contra
2449, el descubrimiento decodifica también los terminadores). El engagement: **61,4 % de
los pasos corren en bloques, 3,3 instrucciones por bloque** — los cuerpos de los lazos de
sondeo sin su salto, como el censo predecía.

**La tanda** (binario `81AB3D79…`, reentrenado, palanca, orden alternado): DOOM
32 635/32 885/32 897 contra 32 899/33 079/33 084 (**−0,6 %**), CT 97 115/97 854 contra
98 825/99 749 (**−1,8 %**), SR2 68 720/69 107 contra 69 136/69 267 (**−0,4 %**). Rangos
disjuntos en los tres — dos por poco (2 ms en DOOM, 29 en SR2) — y dirección uniforme en
las siete parejas: real, y modesto.

**La lectura honesta**: con bloques de 3,3, lo que el lazo en C ahorra por instrucción — el
chequeo de FIQ, las máscaras de la búsqueda, la rama del avance, los centinelas — es poco,
y cada salto (un cuarto de los pasos) paga un intento fallido a la entrada. Lo que sigue en
la mesa es exactamente lo que la emisión x64 cobra y el lazo en C no puede: la llamada
indirecta por instrucción, los operandos leídos de la entrada en vez de bakeados, `LEER_R`
con su ternario del PC, y el propio PC+8 como constante por instrucción. **El escalón que
queda de la fase 4 es esa emisión**, sobre esta misma infraestructura: `arm7_blq_correr()`
es el punto único donde un puntero a código emitido reemplaza al lazo — descubrimiento,
clasificación, validez, presupuesto y salidas laterales ya están pagados y probados.

---

# El traductor a x64, y el cierre de la fase 4: −2,4/−2,4/−2,0 % más, todo exacto (2026-08-10)

`arm7jit.c`, sobre el emisor `jit_x64.c` (que ganó dos primitivas con sus casos byte a byte:
`shift_cl` para la rotación de la carga desalineada y la condición `O` para leer V). Compila
solo con `DCEMU_JIT`, como `jit.c` y por lo mismo; `DCEMU_SIN_JIT_ARM=1` lo deja sin
instalar, que es el A/B. Se enchufa donde el escalón anterior lo dejó previsto:
`arm7_blq_correr()` corre el puntero emitido si existe — con `arm7.r[15] == base` exacto,
porque el PC viaja bakeado — y el lazo en C queda de respaldo (y es el camino del perfil y
del censo, que llevan sus ganchos).

**Plantillas por forma**: la ALU con inmediato en sus dos S (con MOV/MVN S=1 resolviendo N,
Z y C en constantes de emisión), la ALU con desplazamiento inmediato S=0 (los casos de
cantidad cero resueltos al emitir; RRX con `rcr` tras materializar C con `shl 3` del CPSR —
el último bit expulsado es el 29), LDR/STR con inmediato (writeback adelantado — `arm7_leer`
no mira `arm7.r` — para que solo la dirección sobreviva la llamada; la rotación desalineada
por CL sin rama, porque rotar por cero es inocuo) y MRS. Lo demás — LDM/STM, los tres con
acarreo de entrada con S, la forma con desplazamiento por registro — se emite como llamada
al manejador de la entrada privada del bloque: idéntico al lazo por construcción. Las
banderas del ARM se arman de las del anfitrión (N=SF, Z=ZF, V=OF, y C es CF invertido en la
clase de las restas), con los receptores en cero **antes** de la operación, porque `xor`
pisa lo que se viene a capturar.

**La suite nueva** (`tests/test_arm7jit.c`, 6 casos): cada programa corre dos veces por
`arm7_ejecutar()` — lazo en C contra emitido — y compara los dieciséis registros, CPSR,
SPSR, banco, ciclos, instrucciones y la RAM de onda entera. Cubre los bordes que la emisión
resuelve distinto: las banderas, la cantidad cero, la desalineada, el PC como operando y
guardado (+12) y cargado relativo, el respaldo adentro del bloque, la salida lateral.
**6/6 a la primera**, y las compuertas del emulador igual: capturas canónicas en DOOM y
SR2, `.wav` de referencia en CT y modplug, totales al dígito. El emitido trabaja: 2560
bloques en DOOM (1,3 MB de arena de 16), 5232 en CT, **cero declinados**.

**La tanda** (binario `AD1E5DB1…`, reentrenado, palanca, orden alternado): DOOM
31 333/31 164/31 185 contra 31 965/31 939/31 694 (**−2,4 %**), CT 92 135/92 413 contra
94 701/94 470 (**−2,4 %**, los totales del pad apareados por ronda), SR2 64 815/64 943
contra 66 197/69 029 (**−2,0 a −4,0 %**). Rangos disjuntos en los tres.

**El cierre de la fase**: los tres escalones dentro de sus propios binarios suman
−3,6/−0,6/−2,4 en DOOM, −5,1/−1,8/−2,4 en CT y −3,3/−0,4/−2,0 en SR2 — y las marcas
absolutas del árbol quedaron en **DOOM 31 164 ms (1,12× tiempo real), CT 92 135 ms
(1,95×), SR2 64 815 ms (0,93×)**, las tres mejores que se hayan medido. El siguiente
movimiento del plan maestro es rehacer el reparto (`perfil-jit.ps1`) y entrar a la fase 5,
el reloj por eventos.
