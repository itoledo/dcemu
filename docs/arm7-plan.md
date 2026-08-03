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
