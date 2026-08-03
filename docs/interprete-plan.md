# Plan: el intérprete del SH-4

Estado: **en curso**. Escrito el 2026-08-02 sobre la rama `rendimiento-hilos`, después de
que [`rendimiento-plan.md`](rendimiento-plan.md) dejara al intérprete como lo único que
queda pesando. Lo hecho está al final, en "Lo que se midió".

## Dónde estamos

Medido con `--perf` en Crazy Taxi en juego (~1000 tiras por escena), Debug, i9-13900:

```
  resto (interprete) 119624 ms   71.6 %
  AICA total          32900 ms   19.7 %
  bloque periodico     6400 ms    3.8 %   (descontado el AICA, que esta anidado ahi)
  dibujar_escena()     ~5500 ms    3.3 %
```

Todo lo demás ya se atacó o se descartó con medida: el bloque periódico bajó de 21 % a
3,8 %, el PVR nunca fue el cuello (2,1 %), los hilos pierden tiempo y las cuatro
comprobaciones por instrucción no cuestan nada. **Queda esto y nada más.**

## La regla que este proyecto ya aprendió a la fuerza

Cuatro veces en la sesión anterior la intuición dijo una cosa y la medida dijo otra: el PVR
no era el cuello, los hilos no ganaban, las comprobaciones del bucle no costaban nada, y un
crash "de la fase 2" no era de la fase 2. Las cuatro veces el que decidió fue un
**experimento desechable de una tarde**, no un razonamiento.

> **Ninguna fase de este plan se implementa antes de medir su techo con un binario
> desechable.** El patrón está probado: se rompe deliberadamente lo que haga falta —
> constantes, comprobaciones, ramas enteras — se corre contra el binario normal **en la
> misma tanda**, y recién ahí se decide. Costo típico: dos compilaciones y seis minutos.

Y la otra regla, que costó una medición entera: **solo son comparables los binarios
corridos seguidos en la misma tanda.** La misma escena dio 0,35× y 0,57× según el estado de
la caché de archivos y la temperatura de la máquina.

---

## Fase 0 — Medir *adentro* del intérprete

Hoy sabemos que el intérprete es el 71,6 % y **nada** de cómo se reparte ese 71,6 %. Sin
eso, todo lo de abajo son candidatos, no un plan.

### 0.1 Un profiler de muestreo — lo único del plan original que nunca se usó

El de Visual Studio (CPU Usage) alcanza y ya está instalado; con el PDB del build de Debug
da funciones directamente. Lo que se busca es el reparto entre:

- el despacho (`main_loop` + la lectura de la tabla),
- los handlers, por familia (`mov.c`, `arith.c`, `floatsimple.c`, `branch.c`),
- lo que quede de `memread`/`memwrite` tras el camino directo,
- `pref142` y el TA.

**Esto es lo primero y no tiene sustituto.** Todo lo demás de esta fase son proxies.

### 0.2 Instrucciones por segundo

`--perf` cuenta ciclos emulados pero no instrucciones, y son cosas distintas: el CPI varía
entre 1 y 5. Un contador de instrucciones da **ns por instrucción**, que es la única cifra
con la que comparar una optimización del despacho contra otra.

Va en el mismo sitio que el resto de `--perf`, y como el bucle ya no paga nada por las
comprobaciones (medido, ver `rendimiento-plan.md` 2.4), un incremento más no cambia nada.

### 0.3 El experimento que separa despacho de handlers

El proxy barato para 0.1, por si el profiler no está disponible: un binario desechable
donde **todos** los `oplist[]` apuntan a un handler mínimo (`PC += 2; cycles++`). El guest
no arranca, así que no sirve para una corrida real, pero sí para medir el ritmo de despacho
puro sobre un número fijo de instrucciones — y contra el ritmo real da la parte que se van
los handlers.

Es menos fino que un perfil y hay que leerlo con cuidado (el guest sintético no ensucia las
cachés igual), pero contesta la pregunta gruesa: *¿despacho o trabajo?*

---

## Los candidatos, con lo que ya se sabe de cada uno

### A. La tabla de despacho ocupa 512 KB

**Es el hallazgo más concreto de la revisión.** `oplist` es
`opcode_f * oplist_pr0_sz0[65536]` — una tabla completamente expandida, indexada por la
palabra de instrucción cruda. En x64 son **65536 × 8 = 512 KB por tabla**, y hay cuatro
(una por combinación de `PR`/`SZ` de FPSCR): **2 MB en total**, de los que 512 KB están
activos.

Un P-core del 13900 tiene 48 KB de L1d y 2 MB de L2. Indexar 512 KB con una palabra de
instrucción que salta por todo el rango significa que **el despacho falla L1 casi siempre y
L2 a menudo** — una carga dependiente antes de cada llamada indirecta.

Lo interesante: **la alternativa ya está escrita en el árbol**, detrás de
`#ifdef old_oplist` en `opcodes.h`, y es exactamente el diseño compacto:

```c
#ifdef old_oplist
extern short * oplist;              /* 65536 x 2 = 128 KB */
...
(opcodes[oplist[arg]].funcion)(arg);
```

Con solo 240 filas en `opcodes[]`, un índice de 16 bits sobra. La tabla baja de 512 KB a
**128 KB** — cuatro veces menos — a cambio de una indirección más (índice → puntero, sobre
un arreglo de 240 entradas que sí entra en L1).

**No está claro cuál gana**, y por eso es el candidato ideal para el método: las dos
implementaciones existen, el cambio es una bandera de compilación, y la medida decide. Hay
una tercera variante que probablemente sea la mejor de las tres y no está escrita: índice de
16 bits a un arreglo **de punteros** de 240 entradas (`funcion[oplist[arg]](arg)`), que
evita indexar la estructura `st_cmd` entera.

### B. La lectura de la instrucción

`*(WORD *) get_memory_pointer(PC)` son dos cargas dependientes: `mem_zone[PC >> 24]` y
después `base + (PC & 0xFFFFFF)`. Era la fase 2.2 del plan anterior y quedó sin hacer.

Cachear `(zona, base)` y recalcular solo cuando `PC >> 24` cambia convierte una carga en una
comparación. **Cuánto vale es dudoso**: la tabla `mem_zone[]` son 2 KB y está caliente. Pero
combinado con A —donde el problema es justamente la presión de caché del despacho— puede
cambiar de signo. Se mide junto con A, no antes.

Ojo con la invalidación: cualquier cosa que reasigne `mem_zone[]` tiene que invalidarla. Hoy
eso pasa una sola vez, en `mem_hash_setup()`.

### C. Todo el estado pasa por una estructura global

`PC` es `core.context.PC_REG`, `R(n)` es `core.context.registers[n]`, y `cycles` es
`core.context.cycles`. Cada handler los lee y los escribe a través de la global `core`, y
**el compilador no puede mantenerlos en registros a través de la llamada indirecta**: cada
instrucción recarga.

En Debug esto es peor de lo que será nunca, y ahí está la trampa: **es el candidato cuyo
valor medido hoy más se va a mover con optimización**. Con `/O2` el compilador registeriza
dentro del handler y lo que queda es la carga/almacenamiento en los bordes; sin ella, cada
acceso es una ida a memoria.

Por eso, y solo por eso, este plan **no propone reorganizar `core`** hasta que exista una
medida sobre un binario optimizado. Es mucho trabajo y su beneficio es justamente el que
peor se estima desde Debug.

### D. El acumulador de ciclos

`core.context.cycles += n` en cada handler, más la comparación en el bucle. Es una fracción
pequeña, pero es el tipo de cosa que un perfil pone en su lugar en cinco minutos. Se mira
después de 0.1, no antes.

---

## Lo que este plan deliberadamente no hace

- **Recompilación dinámica.** Es la respuesta "de verdad" al costo de un intérprete y
  también un proyecto entero. Cambia el modelo de ejecución, rompe la relación uno a uno
  entre `opcodes[]` y su suite —que es lo que hace verificable a este núcleo— y deja sin
  sentido buena parte de las herramientas de diagnóstico (`--traza-desde`, el anillo de PC,
  el UBC). No para llegar de 0,35× a 0,6×.
- **Despacho por `computed goto`.** Es la técnica clásica para intérpretes y **MSVC no la
  tiene**; esta rama existe para que dcemu corra con MSVC y con GCC. Las llamadas de cola
  garantizadas tampoco están.
- **Reordenar `core`** antes de tener una medida optimizada. Ver C.

## Y la salvedad que atraviesa todo

**Todos los números de arriba son Debug**, por decisión tuya de no perseguir Release ahora.
Para las fases anteriores eso no cambiaba las conclusiones: el bloque periódico entraba 160
millones de veces y el PVR costaba 2 %, y ningún nivel de optimización mueve eso de lugar.

**Para el intérprete sí cambia el orden.** Con `/O2` y LTCG, C y D se encogen mucho —
inlining y registerización son exactamente lo que les falta— mientras que A **no se encoge
nada**: 512 KB de tabla siguen siendo 512 KB y los fallos de caché no dependen del
optimizador.

O sea que A es la apuesta robusta —vale lo mismo en Debug que en Release— y C es la que
podría estar sobrevalorada hoy. No es motivo para hacer Release ahora; sí es motivo para
**no invertir en C basándose en un perfil de Debug**.

## Orden

| # | Qué | Riesgo | Decide |
| --- | --- | --- | --- |
| 0.1 | Perfil de muestreo | ninguno | todo lo demás |
| 0.2 | Instrucciones por segundo en `--perf` | ninguno | la unidad de comparación |
| A | Tabla de despacho compacta (las dos variantes ya existen) | bajo | medida contra la actual |
| B | Cachear el puntero de búsqueda | medio | se mide junto con A |
| D | El acumulador de ciclos | bajo | lo que diga 0.1 |
| C | Reorganizar `core` | alto | **no**, hasta que haya perfil optimizado |

Las barandas siguen siendo las de siempre: `ctest`, `dcemu_sh4json` bit a bit —que aquí
importa más que nunca, porque tocar el despacho toca **todas** las instrucciones— y el
barrido de las 150 demos con `herramientas/barrido.ps1` y `comparar.ps1`, con su corrida de
control.

---

# Lo que se midió

## 0.2 — Instrucciones por segundo: hecho

`--perf` cuenta instrucciones ahora y reporta **ns por instrucción**, MIPS y instrucciones
por ciclo emulado. Se cuenta en los dos lugares que despachan: el camino rápido de
`main_loop()` y `run()`, que es por donde pasan las ranuras de retardo. Dejarlas fuera
sesgaría la cifra hacia abajo del orden del 10 %.

La primera lectura, sobre el arranque de Crazy Taxi:

```
perf: 626877587 instrucciones, 8.3 ns cada una (120.4 MIPS, 0.62 por ciclo emulado)
```

**120 MIPS en Debug** es más de lo que este plan asumía. El SH-4 de la consola retira del
orden de una instrucción por ciclo a 200 MHz, así que el intérprete solo —sin AICA, sin
PVR— corre a poco más de la mitad del ritmo de la máquina real. Eso reordena el problema:
lo que falta no es un factor de diez.

Y **0,60 instrucciones por ciclo emulado** es un dato que no teníamos: el CPI medio del
código que corre es 1,67, así que "ciclos por segundo" y "instrucciones por segundo" se
separan bastante. Era la razón de agregar el contador.

## 0.1 — El perfil de muestreo: pendiente, y por qué

**Necesita una consola elevada** y por eso no se hizo desde acá. El logger del kernel de
ETW es lo que toma las muestras y Windows no lo habilita sin privilegios: `wpr` contesta
`Failed to enable the policy to profile system performance` y no graba nada.

No hace falta instalar nada —`wpr.exe` viene con Windows y `xperf.exe` con el Windows
Performance Toolkit del SDK, que ya está en esta máquina—, así que quedó armado en
[`herramientas/perfil.ps1`](../herramientas/perfil.ps1): graba, corre el banco de pruebas,
resuelve los símbolos contra el PDB de Debug y deja un CSV. Se corre desde una consola de
administrador y nada más.

## A — La tabla de despacho: implementada y verificada

Lo primero que apareció al ir a buscar la alternativa que el plan daba por escrita:
**nunca compiló**. `opcodes.h` la guardaba con `#ifdef old_oplist` y `opcodes.c` con
`#ifdef oplist_old` —dos nombres distintos, invertidos—, así que definir cualquiera de los
dos dejaba `initopcodes()` escribiendo punteros a función en un arreglo de `short`. Estuvo
así desde 2004. O sea que el candidato "las dos implementaciones ya existen, es una bandera
de compilación" era falso; había que escribirlo.

Se escribió la tercera variante, que es la que el plan señalaba como probablemente mejor y
la única que no estaba: **índice de 16 bits a un arreglo plano de punteros**. La tabla baja
de 512 KB a 128 KB, y el arreglo son 240 × 8 = 1,9 KB, que sí viven en L1d. La variante
vieja —`opcodes[oplist[arg]].funcion`— se descartó sin medirla: `st_cmd` son 48 bytes, así
que indexar la estructura entera son 11,5 KB en vez de 1,9 y trae a la caché seis campos
que el despacho no mira.

Se eligen al compilar, con `-DDCEMU_DESPACHO_COMPACTO=ON`, y no con un `#define` en
`options.h`, precisamente porque lo que decide es correr los dos binarios en la misma
tanda: para eso tienen que poder existir a la vez.

`initopcodes()` quedó en **un solo cuerpo** para las dos formas, con un macro que decide
qué se guarda en la tabla. Tener dos cuerpos es exactamente lo que dejó divergir al
original.

### Las barandas, antes de mirar el reloj

| | expandida | compacta |
| --- | --- | --- |
| `ctest` | 21/21 | 21/21 |
| `dcemu_sh4json` | 113 191 ok, **0 fallan** | 113 191 ok, **0 fallan** |

Idénticos, incluidas las 3306 divergencias deliberadas contra Reicast y los 3 casos
descartados. Es la baraja que importa acá: cambiar el despacho toca las 233 codificaciones.

La suite `decodificacion` —la que verifica que la expansión resuelve el handler correcto—
necesitó adaptarse, porque comparaba entradas de la tabla como punteros. Se hizo con un
`OP_HANDLER(tabla, instr)` en `opcodes.h`, que resuelve al puntero en las dos formas, de
modo que la prueba se escribe una vez y vale para ambas.

### El veredicto: **la tabla compacta pierde**

Crazy Taxi en 3D en movimiento, 180 s emulados, tres vueltas alternadas en la misma tanda
(`herramientas/despacho-ab.ps1`):

| vuelta | expandida | compacta | |
| --- | --- | --- | --- |
| 1 | 319 427 ms | 326 517 ms | −2,2 % |
| 2 | 318 417 ms | 326 275 ms | −2,5 % |
| 3 | 321 015 ms | 325 008 ms | −1,2 % |

Tres pares, tres veces el mismo signo, **ninguna inversión**. Y el control es el mejor que
ha dado el proyecto: las seis corridas alternan entre exactamente dos cuentas de
instrucciones —22 280 016 491 y 22 280 016 747— sobre 22 mil millones, con las mismas
10 008 cuadros, 10 001 escenas y 1182 tiras por escena. Es la misma ejecución.

**La hipótesis de la presión de caché queda refutada, y de la forma más fuerte posible: el
resultado no depende del tamaño del conjunto activo.** La misma medida sobre el menú del
boot ROM —53 tiras por escena, un lazo estrecho que toca pocas codificaciones— también dio
−2,1 %. Si los 512 KB de tabla estuvieran fallando L1 en el juego y no en el menú, el signo
o al menos la magnitud tendrían que haberse movido. No se movieron nada.

La aritmética que explica por qué, y que hay que hacer *antes* la próxima vez:

- La compacta agrega una carga **dependiente** —índice, después puntero— sobre `opfuncion[]`,
  que son 1,9 KB y siempre está en L1: unos 4-5 ciclos que se suman al camino crítico de
  **cada** instrucción, sin excepción.
- Lo que ahorra sólo aparece cuando la tabla grande falla L1, y vale la diferencia entre L2
  y L1, unos 10 ciclos.
- O sea que necesita que **la mitad** de los despachos fallen L1 para empatar. Con el
  predictor de saltos indirectos y el prefetcher del 13900 sobre una tabla de 8192 líneas,
  no se acerca.

**El costo por instrucción no es el despacho.** El dato que lo dice de frente: la misma
tabla, el mismo binario, cuesta **8,4 ns por instrucción en los menús y 14,4 en el juego**
—un 71 % más—. Si el despacho fuera el que manda, esa cifra sería plana; lo que la mueve es
el conjunto de trabajo del *guest* y lo que el PVR y las texturas desalojan de la caché.
Eso es presión de caché de verdad, pero no la de la tabla.

Las dos formas quedan en el árbol. La compacta no es una mejora pendiente —es un resultado
negativo medido—, pero se queda porque `initopcodes()` tiene ahora **un solo cuerpo** para
las dos y la opción de CMake está cableada hasta las pruebas: es el vehículo para medir la
próxima variante sin volver a escribir el andamiaje. Lo que la mató al `#ifdef` original
fue estar fuera de toda compilación, no existir.

### Lo que sigue

Con A refutado y 0.1 esperando una consola elevada, el orden que queda es:

| # | qué | estado |
| --- | --- | --- |
| 0.1 | perfil de muestreo | **lo único que puede decidir el resto**; necesita elevación |
| B | cachear el puntero de búsqueda | el plan lo ataba a A, y A murió. Solo, la cuenta no cierra: `mem_zone[]` son 2 KB y está caliente, así que la carga que ahorra ya es un acierto de L1 |
| D | el acumulador de ciclos | esperando 0.1 |
| C | reorganizar `core` | **no**, hasta que haya perfil optimizado |

Y una conclusión que este experimento deja sin haberla buscado: **120 MIPS en Debug con
0,60 instrucciones por ciclo emulado** dice que el intérprete no está lejos de lo razonable.
Lo que falta para 1,0× no está escondido en el despacho.

---

# La premisa del plan quedó vencida

Este documento arranca diciendo que el intérprete es el 71,6 % y "queda esto y nada más".
Medido de nuevo el 2026-08-02, sobre el banco corregido y después de los tres merges, eso
ya no es cierto — y en un binario optimizado no lo es ni de lejos.

Las cuatro corridas hacen **el mismo trabajo exacto**: 22 280 016 491 instrucciones, 10 001
escenas, 1182 tiras por escena, 10 008 cuadros. Así que los absolutos se comparan directo.

| | Debug | Release | |
| --- | --- | --- | --- |
| total | 318 219 ms | 252 550 ms | −20,6 % |
| **intérprete** | **189 011 ms** | **84 207 ms** | **−55,4 %** |
| bloque periódico | 70 201 | 23 679 | −66,3 % |
| AICA (ARM7) | 53 245 | 17 178 | −67,7 % |
| **`dibujar_escena()`** | **53 398** | **142 120** | **+166 %** |
| de eso texturas | 27 608 | 81 206 | +194 % |

Y el reparto, que es lo que decide dónde trabajar:

| | Debug | Release |
| --- | --- | --- |
| intérprete | 59,3 % | **33,3 %** |
| `dibujar_escena()` | 16,7 % | **56,2 %** |
| de eso texturas | 8,6 % | **32,1 %** |
| bloque periódico | 22,0 % | 9,3 % |
| AICA | 17,2 % | 7,1 % |

## Tres cosas que salen de ahí

**1. Compilar con `/O2` le saca al intérprete más del doble de lo que podría cualquier
candidato de este plan.** De 189 s a 84 s, un 55 %. Por instrucción son **8,48 ns → 3,78 ns**,
o sea unos 42 → 19 ciclos. Diecinueve ciclos por instrucción emulada es una cifra normal
para un intérprete de tabla; el margen que queda ahí es modesto. Y no hay excusa de
corrección: Release da 21/21 en `ctest` y **113 191 ok, 0 fallan** en SingleStepTests, lo
mismo que Debug.

**2. `dibujar_escena()` es *más lento* en Release, y eso no es un absurdo: es el síntoma de
que el cuello pasó a la GPU.** En Debug el emulador va tan despacio que el driver siempre
tiene la cola vacía y las llamadas a GL vuelven enseguida; en Release el CPU la alimenta
más rápido y las llamadas empiezan a bloquear. `presentar` sigue en 0,2 %, así que no es el
vsync del intercambio: es adentro de las llamadas de dibujo, y sobre todo de las de textura.

**3. "El PVR nunca fue el cuello (2,1 %)" era verdad y dejó de serlo.** Esa medida es
anterior a los tres merges que trajeron toda la cadena de texturas —mipmaps subidos del
guest, VQ nivel por nivel, la caché persistente, bump, paletas—. Hoy `get_texture()` sola
es el **32,1 %** del tiempo real: 81 s en 10 001 escenas, unos 8 ms por escena. Con una
caché de 1024 entradas que debería evitar las resubidas, esa cifra pide explicación.

## El orden, corregido

| # | qué | por qué |
| --- | --- | --- |
| 1 | **Correr en Release** | 55 % del intérprete, gratis, ya verificado |
| 2 | **Medir aciertos y fallos de la caché de texturas** | el 32 % del tiempo real no tiene todavía una explicación |
| 3 | Reordenar `context_t` | ver abajo: es barato y es lo único del plan que ataca lo que `/O2` no puede |
| 4 | 0.1, el perfil de muestreo | ahora dentro de un 33 %, no de un 71 % |
| ~~A~~ | ~~tabla compacta~~ | **refutado**, pierde 1,2-2,5 % |
| ~~B~~ | ~~cachear el puntero de búsqueda~~ | sólo tiene sentido junto con (3) |

### Lo que queda del candidato C, y por qué es barato

`/O2` registeriza dentro de cada handler, pero **no puede evitar el tráfico de líneas de
caché entre una instrucción y la siguiente**: el estado vive en la global `core` y cada
handler lo escribe antes de volver. Ahí sí queda algo, y la disposición actual lo empeora
sin necesidad:

```
  0   cycles                <- CADA instruccion lo escribe     linea 0
  4   cycles_v_int          <- SIN USAR
  8   cycles_v_int_total    <- SIN USAR
 12   registers[24]         R0..R15 en 12..75                  lineas 0 y 1
                            R0_BANK..R7_BANK en 76..107  (frios)
108   banco_activo .. 148 PR
152   PC_REG                <- CADA instruccion lo escribe     linea 2
156   SR_REG                <- SR_T lo tocan muchisimas        linea 2
```

Cada instrucción toca la línea 0 (`cycles`), una de registros y la línea **2** (`PC`): dos o
tres donde alcanzarían una o dos. Y ocho bytes de la línea más caliente los ocupan dos
campos que el comentario declara sin usar, conservados "para no cambiar el tamaño del
contexto" — motivo que no se sostiene, porque la instantánea copia con `sizeof`.

Poner `cycles, PC_REG, SR_REG, registers[0..15]` primero son 76 bytes: dos líneas, y lo frío
detrás. **No es refactorizar `core`, es cambiar el orden de los campos**, y nada depende de
él. Eso además habilita al candidato B, que solo no cerraba: si el par (zona, base) de la
búsqueda de instrucciones vive en esa misma línea, se lee gratis con el `PC` que ya se
cargó.

---

# Plan 2: lo que el perfil dijo

Escrito el 2026-08-02 con el perfil de muestreo en la mano —el paso 0.1, que hasta ahora
era el único del plan anterior sin hacer—. **Todo lo de arriba fue razonamiento; esto es
medición**, y no coincidieron: de los cuatro candidatos originales, uno se midió y perdió,
dos quedaron sin objeto y el hallazgo grande no estaba en la lista.

## El reparto, por fin

WPR a 1 kHz sobre Crazy Taxi en 3D en movimiento, Debug, 180 s emulados. De las ~325 000
muestras del hilo principal, **256 878 caen en código de dcemu** (79 %); el resto está en el
driver de GL y en el kernel. Repartidas por subsistema:

| grupo | % del código de dcemu |
| --- | --- |
| handlers del SH-4 | **38,6 %** |
| ARM7 del AICA | **19,6 %** |
| `main_loop` + `run` | 16,6 % |
| `fpu_dn_s` / `fpu_dn_d` | **9,6 %** |
| resto de la FPU | 6,9 % |
| `_RTC_*` (andamiaje de Debug) | 3,2 % |
| gráficos y TA | 2,3 % |
| memoria y PVR | 1,9 % |
| reloj y periféricos | 1,3 % |

Las funciones más caras, una por una:

```
main_loop            15,71 %      movl21                1,80 %
fpu_dn_s              9,61 %      arm7_paso             1,79 %
op_datos     (ARM7)   5,99 %      add40                 1,49 %
movl2                 4,74 %      reg16        (ARM7)   1,18 %
movl9                 4,45 %      dma_canal             1,15 %
_RTC_CheckStackVars   3,22 %      fipr                  0,93 %
ftrv                  2,70 %      jsr111                0,91 %
op_transferencia      2,51 %      mov3                  0,85 %
condicion    (ARM7)   2,16 %      run                   0,84 %
arm7_leer             2,11 %      add39                 0,78 %
op_bloque    (ARM7)   2,03 %
```

## El patrón, que es uno solo

Todo lo caro que el perfil encontró es **trabajo barato pagado como llamada a función**:
cinco instrucciones de cuenta detrás de un `call`, un prólogo, un epílogo y una barrera
para el optimizador. No hay ningún algoritmo malo. Eso explica también por qué las
hipótesis anteriores fallaron: buscaban un problema de *estructura* —la tabla de despacho,
los hilos, las comprobaciones del bucle— donde lo que hay es un problema de *frontera de
compilación*.

Y explica el resultado de Release: `/O2` recupera el 55 % del intérprete inlineando lo que
está en la misma unidad de traducción, y deja intacto todo lo que la cruza.

## La trampa: en Debug el inline no existe

Antes de nada, porque invalida la primera medición que se intentó de este plan y va a
invalidar las que siguen si no se tiene presente:

**MSVC en Debug compila con `/Od`, que desactiva el inlining por completo.** `DC_INLINE` es
`__inline`, que es una *sugerencia*: con `/Od` no se aplica nunca, ni siquiera para una
función de cinco instrucciones. Así que mover algo a una cabecera no cambia nada en Debug —
sigue siendo una llamada, sólo que ahora hay una copia por unidad de traducción.

Medido: pasar `fpu_dn_s` a la cabecera dio **0,4 %** en Debug, con los rangos de las dos
variantes superpuestos. Es ruido, y no refuta la hipótesis: no la prueba.

De ahí sale una corrección al orden de este plan que no es cosmética. **La fase 0.1 no es
"una ganancia gratis más": es la precondición para poder medir 0.2, 0.3 y buena parte de la
fase 1.** Todo lo que este plan propone contra fronteras de compilación es invisible en
Debug por construcción. Las fases 2 y 3 —el camino de memoria y la disposición de
`context_t`— sí se miden en Debug, porque son cargas y líneas de caché, no inlining.

Y el corolario incómodo: **el perfil que originó este plan es de Debug**, así que el 9,6 %
de `fpu_dn_s` es en parte un artefacto de un binario donde nada se inlinea. Conviene
repetirlo sobre Release —`herramientas\perfil.ps1 -Exe build-x64\Release\dcemu.exe`— antes
de invertir en las fases 1 y 2, aceptando que el inlining borronea la atribución.

## Fase 0 — Lo que no cuesta diseño

### 0.1 Compilar en Release *(medido: −55 % del intérprete)*

189 011 ms → 84 207 ms, o sea 8,48 → 3,78 ns por instrucción. Ya verificado: 21/21 en
`ctest` y **113 191 ok, 0 fallan** en SingleStepTests, idéntico a Debug. Nada que escribir,
sólo dejar de medir y jugar en Debug.

De paso se lleva el 3,2 % de `_RTC_CheckStackVars`, que no es código nuestro.

### 0.2 `fpu_dn_s` y `fpu_dn_d` en la cabecera *(hecho, y da 0,4 %, no 9,6 %)*

El perfil le daba **9,6 %**, la función más cara después del bucle. Cinco instrucciones
llamadas sobre cada operando de cada operación de punto flotante —25 sitios, cuatro sólo en
la entrada de `ftrv`—, y en otra unidad de traducción, así que ni `/O2` la inlinea. Movida a
`floatsimple.h` como `static DC_INLINE`, con el caso desnormalizado —rarísimo en un juego—
fuera de línea.

Medido en Release, dos pares alternados con trabajo idéntico (10 001 escenas, 1182 tiras,
las cuentas de instrucciones dentro de 612 sobre 22 mil millones):

| | base | en línea |
| --- | --- | --- |
| vuelta 1 | 256 911 ms | 255 308 ms |
| vuelta 2 | 255 787 ms | 255 147 ms |

**0,44 %.** El signo es consistente en los dos pares, así que la ganancia es real; la
magnitud no se parece a lo que el perfil prometía. Se queda —es gratis, es seguro y está
verificada bit a bit— pero por 0,4 %, no por 9,6.

### La lección, que vale para todo lo que sigue

**Un porcentaje de muestras exclusivas en un perfil de Debug no predice el ahorro en tiempo
real.** Debug infla exactamente aquello que el optimizador elimina de todos modos —el
prólogo, el epílogo, `_RTC_CheckStackVars`—, y lo infla *más* en una hoja llamada
constantemente que en el promedio. El intérprete entero pasa de 189 s a 84 s con `/O2`, un
factor 2,24; una función como `fpu_dn_s` se encoge mucho más que eso, así que su porción del
pastel de Debug no es su porción del pastel de Release.

Esto pone bajo la misma sospecha a **todo lo que este plan derivó del mismo perfil**: el
19,6 % del ARM7, los 2,16 % de `condicion()`, los 2,11 % de `arm7_leer()`, el 7 % largo que
se le atribuye a `/GL /LTCG`. Ninguno está refutado; todos están sin verificar, y el primero
que se verificó rindió una vigésima parte de lo prometido.

**Por eso el orden real es: primero Release, después un perfil *de Release*, y recién ahí
decidir las fases 1 y 2.** Lo que sí se puede seguir midiendo en Debug son las fases 2 y 3
—cargas y líneas de caché, no inlining—, que es la razón de que estén separadas.

### 0.3 Activar `/GL` y `/LTCG` en Release

Es la versión general de 0.2: deja al optimizador inlinear a través de unidades de
traducción. El perfil nombra a los candidatos que quedan —`condicion`, `reg16`, `poner_r`,
`arm7_leer`, `aica_fiq_pendiente`, `fpu_es_nan`, `fpu_causas`—, que juntos son otro 7 %
largo. Son dos líneas de CMake y una medición.

Ojo con dos cosas: alarga el enlace bastante, y **cambia el código generado de la FPU**, así
que `dcemu_sh4json` no es opcional acá.

## Fase 1 — El ARM7 del AICA: 19,6 %

Es el segundo bloque del emulador y **nunca se lo miró**. No es el lazo de espera de KOS:
`--perf` reporta `2 161 263 760 pasos, 0 ociosos` —Crazy Taxi sube su propio firmware y el
ARM corre de verdad—. El promedio da 1,87 ciclos por instrucción, o sea que la contabilidad
de ciclos está bien y no se está sobre-ejecutando.

El camino caliente es `arm7_paso()`, y tiene cuatro cosas de la misma familia que `fpu_dn_s`:

```c
ciclos_op = 1;                                       /* global */
pc_cambio = 0;                                       /* global */
if (!(arm7.cpsr & ARM7_F) && aica_fiq_pendiente())   /* llamada, cada instruccion */
op = arm7_leer(arm7.r[15], 4);                       /* llamada */
if (condicion(op))                                   /* llamada */
{
    idx = ARM7_INDICE(op);
    if (arm7_opfila[idx] >= 0) arm7_usada[arm7_opfila[idx]] = 1;   /* cobertura */
    arm7_oplist[idx](op);
}
```

- **1.1 `condicion()` — 2,16 %.** Extrae N, Z, C y V y entra a un `switch` de 16 casos
  **para cada instrucción**, cuando en código ARM real la enorme mayoría es `AL` (`0xE`).
  Un `if ((op >> 28) == 0xE)` antes de la llamada se salta todo. Es una línea.
- **1.2 `arm7_usada[]` corre en producción.** Es el instrumento de cobertura de la suite
  —lo lee `arm7_fila_usada()`, de `tests/`— y hoy se ejecuta en cada instrucción del ARM en
  una corrida normal. Va detrás de una bandera, como el resto de los instrumentos.
- **1.3 `aica_fiq_pendiente()` en línea.** Una llamada cruzando unidad de traducción por
  instrucción para mirar un par de máscaras. Aparece con 0,42 % propio, más el costo en el
  llamador.
- **1.4 El camino de búsqueda.** `arm7_leer()` (2,11 %) resuelve región y tamaño en cada
  lectura. La búsqueda de instrucción siempre es de 4 bytes y casi siempre en RAM de onda:
  merece su propio camino corto, igual que `mem_zona_directa` en el SH-4.

Nada de esto cambia una decisión del emulador, así que la suite `arm7` y el `.wav` de
`--captura-audio` —que es determinista— alcanzan como baranda.

## Fase 2 — Los handlers del SH-4: 38,6 %

Los cuatro más caros son `movl2` (4,74 %), `movl9` (4,45 %), `movl21` (1,80 %) y `add40`
(1,49 %). Tres de los cuatro son **lecturas de memoria**, y `movl2` —la carga del literal,
la instrucción más común del código SH-4— es la primera. O sea que el tiempo de los
handlers está en el camino de acceso, no en la aritmética.

Hoy una lectura del guest hace esto:

```c
if (mmu_activa) ...                                           /* 1 carga global */
if (mem_zona_directa[dir >> 24] && !watchpoint_lectura_dir)   /* 2 cargas globales */
    MEM_DIRECTO_LEER(...)   /* get_memory_pointer -> mem_zone[dir >> 24]   1 mas */
if (ubc_operando_activa) ...                                  /* 1 mas */
```

**2.1** Una sola tabla `mem_base_directa[256]` que guarde **el puntero base o NULL** da la
prueba y la base en una carga en vez de tres. Se pone en NULL cuando la zona no es RAM
plana, cuando hay watchpoint armado o cuando el UBC vigila operandos: armar y desarmar
reescribe 256 entradas, que pasa una vez por corrida.

Hay que tener cuidado con una cosa: `memread_fisico` es el único punto por el que pasan
**todas** las escrituras y es donde vive el gancho de `--watchpoint`. Saltearlo dejaría al
watchpoint sin ver la RAM, que es justo lo que vigila; por eso el NULL y no una bandera
aparte.

## Fase 3 — El bucle y el contexto: 16,6 %

`main_loop` con 15,71 % es la función más cara del emulador. Contiene el bloque periódico
—ya atacado con `RELOJ_GRANO`— y el andamiaje por instrucción, que se midió en cero. Lo que
queda es el tráfico de memoria contra la global `core`, que `/O2` no puede evitar porque
cada handler tiene que dejarla escrita antes de volver.

**3.1 Reordenar `context_t`.** Hoy:

```
  0   cycles                <- CADA instruccion lo escribe     linea 0
  4   cycles_v_int          <- SIN USAR
  8   cycles_v_int_total    <- SIN USAR
 12   registers[24]         R0..R15 en 12..75                  lineas 0 y 1
                            R0_BANK..R7_BANK en 76..107  (frios)
108   banco_activo .. 148 PR
152   PC_REG                <- CADA instruccion lo escribe     linea 2
156   SR_REG                <- SR_T lo tocan muchisimas        linea 2
```

Cada instrucción toca la línea 0 (`cycles`), una de registros y la línea **2** (`PC`): dos o
tres donde alcanzarían una o dos. Y ocho bytes de la línea más caliente los ocupan dos
campos que el propio comentario declara sin usar, conservados "para no cambiar el tamaño del
contexto" —motivo que no se sostiene, porque la instantánea copia con `sizeof`—.

Poner `cycles, PC_REG, SR_REG, registers[0..15]` primero son 76 bytes: dos líneas, con lo
frío detrás. **No es refactorizar `core`, es cambiar el orden de los campos.**

**3.2 Cachear el puntero de búsqueda de instrucciones**, y sólo después de 3.1. Solo, cambia
una carga por otra carga más una comparación, o sea nada; pero si el par (zona, base) vive
en la línea caliente del contexto se lee gratis con el `PC` que ya se cargó.

## Lo que ya se midió y no hay que volver a intentar

| | resultado |
| --- | --- |
| tabla de despacho compacta (128 KB en vez de 512) | **−1,2 a −2,5 %**, y no depende del conjunto activo |
| hilo aparte para el AICA | **−4 a −5 %**; el techo que `--perf` calcula es 1,07× |
| sacar las comprobaciones por instrucción del bucle | **cero** |
| optimizar los decodificadores de textura | el 2,3 % es código nuestro; el resto está en el driver de GL |

## Orden y barandas

| # | qué | esperado | riesgo |
| --- | --- | --- | --- |
| 0.1 | Release | −55 % del intérprete (medido) | ninguno |
| 0.2 | `fpu_dn_s` en línea | hasta 9,6 % | bajo |
| 0.3 | `/GL /LTCG` | 7 % largo, a medir | bajo, pero toca la FPU |
| 1.1-1.4 | ARM7 | de 19,6 % a la mitad, a medir | bajo |
| 2.1 | `mem_base_directa` | parte del 38,6 % | medio: el gancho del watchpoint |
| 3.1 | reordenar `context_t` | a medir | bajo |
| 3.2 | caché del puntero de búsqueda | a medir | medio: invalidación |

Las de siempre, y una que ahora pesa más: **`dcemu_sh4json` bit a bit**, porque 0.2, 0.3 y
todo lo de la FPU cambian el código generado de los flotantes. Después `ctest`, el barrido
de las 150 demos con su corrida de control, y el `.wav` de `--captura-audio` para la fase 1,
que es determinista y es la única baranda real del ARM7.

Y la regla de método, que esta vez se cobró una sesión entera: **el banco de pruebas es sin
`--bios` y con 180 segundos**, y toda comparación verifica cuadros, escenas y tiras por
escena antes de mirar el reloj.
