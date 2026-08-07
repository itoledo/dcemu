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

**Hecho el 2026-08-07** — ver la sección final de este documento, «0.1, por fin»: IPC
3,4-3,9, el despacho predicho casi siempre, la LLC sin fallar. Lo que sigue es la historia
de por qué esperó.

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
| 0.1 | perfil de muestreo | **hecho el 2026-08-07** (ver la sección final): decidió — el intérprete va limitado por volumen, no por fallos |
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
| 2.1 | `mem_base_directa` | ~~parte del 38,6 %~~ **hecha y ≈0 en Release** | medio: el gancho del watchpoint |
| 3.1 | reordenar `context_t` | ~~a medir~~ **hecha (d029a1d) y ≈0 en Release** | bajo |
| 3.2 | caché del puntero de búsqueda | a medir | medio: invalidación |

**Ojo con esta tabla**: es la propuesta de la fase 3, no el estado. 2.1 y 3.1 se
implementaron y se midieron —`rendimiento-plan.md`, «lo que no valió»: 1-2 % cada una en
Debug y **≈0 en Release**— y la tabla nunca se actualizó. Leerla como pendientes costó
media sesión: ver «La alineación del contexto» al final de este documento.

Las de siempre, y una que ahora pesa más: **`dcemu_sh4json` bit a bit**, porque 0.2, 0.3 y
todo lo de la FPU cambian el código generado de los flotantes. Después `ctest`, el barrido
de las 150 demos con su corrida de control, y el `.wav` de `--captura-audio` para la fase 1,
que es determinista y es la única baranda real del ARM7.

Y la regla de método, que esta vez se cobró una sesión entera: **el banco de pruebas es sin
`--bios` y con 180 segundos**, y toda comparación verifica cuadros, escenas y tiras por
escena antes de mirar el reloj.

---

## Estado al 2026-08-04

El baseline de los tres bancos y el desglose de la MMU están en
[`rendimiento-plan.md`](rendimiento-plan.md), fase 6. Lo que le toca a este documento:

- **De la fase 1 (el ARM7) están hechas 1.1 y 1.2**: `(op >> 28) == 0xE` corta antes de
  llamar a `condicion()`, y el arreglo de cobertura `arm7_usada[]` corre detrás de
  `arm7_cobertura`. Quedan **1.3** —`aica_fiq_pendiente()` cruzando unidad de traducción una
  vez por instrucción de ARM— y **1.4** —un camino corto en `arm7_leer()`, que siempre busca
  4 bytes y casi siempre en RAM de onda—.
- **El ARM7 sigue siendo el segundo bloque del emulador en los guests sin MMU**: 15,3 % de
  Crazy Taxi y 13,6 % de Virtua Tennis, contra 7-9 % del camino gráfico entero. En DCDoom es
  3,3 %, así que la baranda del `.wav` de `--captura-audio` sigue siendo la única que lo
  cubre.
- **El intérprete ya no es el 71,6 % de un emulador lento**: los dos guests sin MMU corren a
  1,54× y 1,36× de la consola. El 71 % que se lleva es de un total que ya sobra, y por eso
  los candidatos B, C y D de este plan valen menos que antes de medir. El guest que sí está
  lento es el de MMU, y lo que le pesa no es el despacho.

---

# La forma de ejecución del guest, medida (2026-08-04)

Es el paso 1.2 de `docs/rendimiento-plan.md`, "llevar dcemu al estado del arte": **medir antes
de decidir si un caché de bloques o un recompilador pueden pagar**. Hasta aquí este árbol no
tenía ninguna de estas cifras, y sin ellas «un dynarec da 3×» es una cita de otro proyecto.

Los contadores viven en `perf.c` y `PERF_BLOQUE` se engancha en los tres caminos de despacho
de `main_loop()`. **Se compilan aparte** —`cmake -DDCEMU_FORMA=ON`— y se encienden con
`DCEMU_FORMA=1`; el resumen sale con `--perf`. El porqué de que no vengan en el binario
normal está abajo, y costó una tanda de A/B averiguarlo:

```sh
cmake -S . -B build -DDCEMU_FORMA=ON && cmake --build build --config Release --target dcemu
DCEMU_FORMA=1 dcemu --perf --salir-tras=180 "roms/Crazy Taxi (USA).cdi"
```

## Los números

Release + PGO, i9-13900, los bancos canónicos. Las dos corridas reproducen su cuenta de
instrucciones **al dígito** contra lo documentado en la fase 6, así que son la misma
ejecución: 22 279 918 813 y 9994 escenas en Crazy Taxi, 5 433 052 826 en DCDoom.

| | Crazy Taxi (Katana, sin MMU) | DCDoom (Windows CE, con MMU) |
| --- | --- | --- |
| corridas secuenciales | 3 451 875 808 | 443 413 460 |
| despachos por corrida | 5,91 | 11,52 |
| **instrucciones por bloque** (con las ranuras) | **6,45** | **12,25** |
| la corrida más larga | 213 | 161 |
| bloques distintos | 10 318 | 18 358 |
| ejecuciones por bloque | **334 549** | **24 154** |
| bloques que cubren el 50 % de las instrucciones | 5 | 9 |
| **... el 90 %** | **168** | **91** |
| ... el 99 % | 1285 | 696 |

Reparto de longitudes:

| | 1 | 2 | 3 | 4 | 5-8 | 9-16 | 17-32 | 33+ |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Crazy Taxi | 16,4 % | 4,3 % | 27,2 % | 21,7 % | 4,9 % | 20,7 % | 2,9 % | 1,5 % |
| DCDoom | 6,4 % | 4,3 % | 14,1 % | 8,0 % | 29,7 % | 25,3 % | 2,9 % | 9,3 % |

Cero corridas perdidas por la tabla en las dos, así que los bloques distintos no están
subestimados.

## Qué dicen

**La forma acompaña, y por los dos lados.**

- **La amortización es de 6,45× y 12,25×.** Todo lo que un caché de bloques saca del camino
  —la búsqueda en la tabla de 65536 entradas, la búsqueda de la instrucción, y las ocho
  comprobaciones del cuerpo del bucle— se paga una vez por bloque en vez de una vez por
  instrucción. No es el 30× de un guest con lazos largos, pero tampoco el 1,5× que haría
  descartarlo.
- **El caché de código es diminuto: 168 y 91 bloques cubren el 90 %**, y menos de 1300 cubren
  el 99 %. Cabe entero en caché de la máquina anfitriona, que es justamente lo que la tabla de
  despacho de 512 KB no hace.
- **La traducción se amortiza sola**: 334 549 ejecuciones por bloque en Crazy Taxi y 24 154 en
  DCDoom. Traducir un bloque puede costar mil veces lo que ejecutarlo y seguir siendo gratis.
- **El guest con MMU tiene la mejor forma de los dos** —12,25 contra 6,45—, que es el que está
  lento. Windows CE compila con bloques más largos que el SDK de Katana.

## El error de denominador que casi se publica

La primera versión pesaba la cobertura **por veces ejecutado** y reportaba que **4 bloques
cubren el 50 %** en Crazy Taxi. Es una cifra sobre el sondeo, no sobre el trabajo: un lazo de
espera de una sola instrucción —un `bra` a sí mismo, un `SLEEP`— se ejecuta millones de veces
y no es donde se va el tiempo. Con el peso correcto —**instrucciones ejecutadas**, que es lo
que un caché de código tiene que servir— el corte del 90 % pasó de 113 a 168 bloques y el del
50 % de 4 a 5.

Es el mismo error que costó tres hipótesis en la fase 6 de `rendimiento-plan.md`, donde
`datos_acierto` usaba como denominador traducciones que nunca miraban la caché. Queda anotado
en `bloque_cmp()` junto al comparador, para que la próxima persona lo lea antes de cambiarlo.

## Lo que estas cifras **no** contestan

**Cuánto de los 5,0 ns por instrucción es sobrecosto del bucle.** La forma dice que la
amortización sirve; no dice sobre qué. Eso lo contesta el paso 1.1 —el perfil de Release con
contadores de hardware, que nunca se tomó— o directamente el prototipo de 1.3.

Y hay una advertencia en contra que sigue en pie, de este mismo documento: **la tabla compacta
perdió 1,2-2,5 % tres veces de tres**, y el mismo despacho cuesta 8,4 ns en los menús contra
14,4 en juego. El costo por instrucción no está dominado por el despacho sino por el conjunto
de trabajo del guest, y un caché de bloques no mejora la localidad de los datos del guest.

## Lo que no se midió, y por qué

**La volatilidad del código** —escrituras sobre páginas desde las que ya se ejecutó, o sea lo
que obligaría a invalidar código traducido—. Medirla exige un gancho en el macro de
`memwrite`, que está expandido en cientos de sitios y es el camino más caliente del árbol.
Cuando exista el caché de bloques la invalidación será suya y contarla ahí sale gratis.

## El instrumento cuesta 4,4 %, así que no viene en el binario normal

Es la única sonda del árbol que se compila aparte (`-DDCEMU_FORMA`, apagada por omisión), y
la excepción se ganó midiendo.

La primera versión colgaba de `--perf`. Eso ya estaba mal por una razón que se vio antes de
medir nada: `--perf` es el instrumento cuyos ns por instrucción se comparan contra todo el
historial de `rendimiento-plan.md`, y agregarle trabajo por despacho lo habría hecho medir
otra cosa en silencio. Pasó a `DCEMU_FORMA=1`, una variable leída una vez al arrancar como el
resto de las sondas.

**Y con eso seguía costando 4,4 %.** Alternando los dos binarios en una misma tanda, que es lo
único que este árbol acepta:

| | ms reales | velocidad |
| --- | --- | --- |
| HEAD | 115 688 / 114 142 | 1,55× / 1,57× |
| con el gancho en el binario | 119 854 / 119 935 | **1,50×** |
| **con el gancho compilado fuera** | **116 314 / 115 875** | **1,54× / 1,55×** |

Una rama por despacho sobre 22 280 millones de instrucciones a ~25 ciclos cada una da
exactamente esa magnitud. Compilada fuera, la diferencia contra HEAD queda en **0,3 %**, con
las cuatro corridas haciendo el mismo trabajo al dígito: 22 279 918 813 instrucciones, 9994
escenas, 1183 tiras.

(Esas seis corridas están todas con el perfil de PGO al 67 %, que es lo que quedaba tras
entrenar sobre la otra variante; por eso las dos columnas salen ~4 % por debajo de lo que da
el árbol reentrenado. No importa para el A/B —los dos binarios cargan el mismo handicap—,
pero sí para no leer 1,55× como la cifra del árbol.)

### El estado final, con el perfil reentrenado

| banco | ms reales | velocidad | fps | documentado |
| --- | --- | --- | --- | --- |
| **Crazy Taxi** | 111 836 / 111 962 / 111 863 | **1,60×** | **89,4** | 1,59× · 88,5 |
| **DCDoom** | 46 840 / 46 941 | **0,75×** | **31,6** | 0,73× · 31,1 |

Tres vueltas dentro del 0,1 % y dos dentro del 0,2 %, con el trabajo idéntico al dígito.
Los dos bancos quedan **apenas por encima** de su línea documentada.

Ese margen es tentador de atribuir al `.pgd` limpio —la línea documentada pudo haberse
construido con el entrenamiento sin ponderar todavía fundido dentro—, pero es una comparación
entre tandas y **por la regla de este árbol eso no se puede leer**. Queda como coincidencia
anotada, no como resultado.

### Por qué compilarla aparte es legítimo aquí, y no lo sería para otra sonda

La regla del árbol —`excepciones.c:413`— es que las sondas viven en el binario normal y se
eligen con una variable de entorno, **porque comparar dos compilaciones mete la disposición
del binario como variable en una medida de tiempo**. Esta rama ya costó una sesión.

Esta sonda es la excepción porque **no mide tiempo: cuenta bloques del guest**. La corrida con
el binario que la lleva reproduce las mismas 5 433 052 826 instrucciones y las mismas 443 413 460
corridas que cualquier otra, y los cortes del 50/90/99 % salen idénticos. Una cuenta del
programa emulado no depende de cómo se compiló el emulador; un cronómetro sí.

### Y una hipótesis que la medición mató por el camino

Antes del A/B, la sospecha era el perfil de PGO: `herramientas/pgo.ps1` borraba los `.pgc`
viejos pero **nunca limpiaba el `.pgd`**, y `pgomgr /merge` acumula sobre lo que ya había, así
que cada reentrenamiento se fundía encima del anterior. Como el `.pgd` vive fuera de `build/`
a propósito para sobrevivir a un borrado, el perfil pasaba a describir la suma de dos
programas.

**El error era real y está arreglado** (`pgomgr /clear` antes de fundir), pero **no era la
causa**: con el `.pgd` limpio Crazy Taxi dio 119 996 / 119 874 / 119 785 ms, o sea lo mismo. La
causa era el gancho, y sólo apareció al alternar dos binarios.

También murió una segunda lectura por el camino: el ARM7 parecía 12,6 % más lento que en la
sesión documentada, sobre código que este cambio no toca, y eso apuntaba a deriva de la
máquina. En la tanda alternada el ARM7 de los dos binarios es comparable. **Un porcentaje
contra una cifra de otra tanda no es un dato**, ni siquiera cuando la conclusión que sugiere
es cómoda.


---

# El cache de bloques predecodificados: implementado, medido, y no sirve (2026-08-04)

Es el paso 1.3 del plan. La forma de ejecucion lo justificaba —6,45 instrucciones por bloque en
Crazy Taxi, 12,25 en DCDoom, menos de 1300 bloques cubriendo el 99 %— y la pregunta que venia a
contestar era **cuanto vale de verdad el despacho**: la busqueda de la palabra por
`get_memory_pointer()` (dos cargas dependientes) y la del manejador en una tabla de 65536
punteros.

La respuesta es **nada**.

## El A/B

Mismo binario, `DCEMU_SONDA_BLOQUES` en 0 y en 1, alternados en una tanda. Trabajo identico al
digito en las cuatro corridas del banco: 22 279 918 813 instrucciones, 9994 escenas, 1183 tiras.

| banco | sin bloques | con bloques | |
| --- | --- | --- | --- |
| **Crazy Taxi, en juego (180 s)** | 125 148 / 125 234 ms | 125 634 / 125 353 ms | **+0,1 a +0,4 %** |
| Crazy Taxi, menus (20 s) | 11 506 / 11 564 ms | 11 884 / 11 946 ms | **+3,3 %** |

En juego es ruido; en los menus pierde. **Sacar del camino la busqueda de la palabra y la de la
tabla de 512 KB no devuelve tiempo medible.**

## Por que, y por que era predecible

No contradice nada de lo que este documento ya tenia; lo confirma desde el cuarto lado:

- la tabla **compacta** —128 KB en vez de 512— perdio 1,2-2,5 %, tres veces de tres;
- quitar las cuatro comprobaciones por instruccion del bucle dio **cero**;
- el mismo despacho cuesta **8,4 ns en los menus y 14,4 en juego**, con la misma tabla;
- y ahora, quitar el despacho entero: **cero**.

Las cuatro dicen lo mismo. El codigo caliente del guest es chico —168 bloques cubren el 90 % de
las instrucciones—, asi que la tabla de 512 KB **esta en cache**: indexarla no cuesta lo que
parece. Lo que cuesta es el cuerpo de los manejadores y el conjunto de trabajo de datos del
guest, que un cache de bloques no toca.

## Lo que esto le hace al recompilador dinamico (1.4)

**Le saca su argumento principal.** La expectativa de 2-2,5x del plan salia de suponer que el
despacho y la busqueda eran el impuesto del interprete. Estan medidos y valen cero.

Lo que a un recompilador le quedaria por ganar es otra cosa, y mas dificil de estimar: mantener
registros del SH-4 en registros del anfitrion entre instrucciones, no actualizar `PC` ni el
contador de ciclos en cada una, plegar constantes, eliminar banderas muertas. Puede ser real,
pero **ya no hay una cifra en este arbol que lo respalde**, y el proyecto es de meses y cambia
el modelo de ejecucion. Antes de entrar ahi hace falta el paso 1.1 —el perfil de Release con
contadores de hardware— que diga en que se van los ~25 ciclos por instruccion, porque ahora
sabemos que **no es en llegar al manejador**.

## Tres errores de implementacion, y lo que ensena cada uno

Valen anotados porque los tres estaban ocultos y los tres los encontro una medicion, no una
lectura del codigo.

**1. La entrega de una interrupcion mueve el PC despues del despacho.** La primera version
verificaba la continuidad del PC justo despues del manejador. Pero el manejador no es el unico
que mueve el PC: el bloque periodico corre **despues**, y `intc_revisar_sh4()` entra a la
excepcion. El cursor quedaba vivo apuntando a la instruccion siguiente del bloque y la
reproducia en el PC del manejador de interrupcion. El guest se desbarrancaba y la corrida no
terminaba: 20 segundos emulados no salieron en 400 reales. La verificacion tiene que estar **al
principio de la vuelta**, donde cubre cualquier desvio venga de donde venga.

**2. Un cursor global cuesta 11,4 %; el mismo cursor en una local, 3,3 %.** El compilador no
puede probar que un manejador no toca un global, asi que lo recarga alrededor de cada llamada;
una local cuya direccion nunca se toma se queda en un registro salvado. La primera medicion
—11,4 % de perdida— era en buena parte del instrumento, no de la idea. Lo unico que quedo
global es `bloques_esperado`, porque hay que poder envenenarlo desde adentro de un manejador
cuando `UpdateFPSCR()` repunta la tabla de despacho.

**3. Una grabacion abandonada tiene que devolver sus entradas.** Sin eso, cada corte filtraba lo
que llevaba escrito: la sonda informaba **86 bloques con 82 536 entradas** —960 por bloque,
cuando el bloque real tiene doce—. La cifra absurda fue lo que delato el error, igual que el
235,9 % de la instantanea en la fase 6.

## Y una limitacion que conviene saber

**El cache no puede servir al guest con MMU, que es justo el que esta lento.** Con la MMU
encendida la busqueda de una instruccion puede levantar una falta de TLB, y un bloque grabado se
la saltearia: el bloque trae la palabra ya decodificada y no vuelve a traducir. Por eso el
camino solo corre con `excepcion_vigilar` en cero, y DCDoom apenas graba 86 bloques antes de
encender la MMU.

## Donde queda

Detras de `-DDCEMU_BLOQUES=ON`, **apagado por omision**, igual que `--hilos`: el codigo es
correcto, esta medido, y la proxima persona que crea que el despacho es el costo puede volver a
correr el A/B sin reconstruir la idea. Compilado fuera no cuesta nada — el binario normal
vuelve a **1,60-1,61x y 89,4 fps**, con 21/21 en `ctest` y 113 191 ok / 0 fallan en
`dcemu_sh4json`.


---

# El perfil de Release, por fin (2026-08-04)

Es el paso 1.1. El unico perfil de muestreo que este arbol habia tomado era de **Debug**, y el
propio documento registra que desvio el plan entero. Este es de Release + PGO, con los
simbolos del PDB que `CMakeLists.txt` deja a proposito, y **grabando solo la parte en juego**:
el script arranca el emulador y espera 80 segundos reales antes de encender la traza, porque a
los 120 segundos emulados Crazy Taxi todavia esta en MODE SELECTION y un perfil de la corrida
entera serian dos tercios de menu.

`herramientas/perfil-pmu.ps1`, modo `tiempo`. Muestras del proceso, en porcentaje de las de
dcemu:

| | DCDoom (con MMU) | Crazy Taxi | Virtua Tennis |
| --- | --- | --- | --- |
| **`main_loop`** | **61,5 %** | **38,7 %** | **36,6 %** |
| `mmu_traducir` | 9,0 % | — | — |
| ARM7 completo | ~5 % | ~10 % | ~10 % |
| driver de GL | 0,0 % | 6,2 % | 3,3 % |
| `utlb_buscar` | 0,9 % | — | — |
| el mayor manejador suelto | 0,5 % | 1,5 % (`movl21`) | 2,0 % (`fmul195`) |

**El tiempo esta en el cuerpo del bucle, no en los manejadores.** Ninguno pasa del 2 %;
`main_loop` se lleva de un tercio a dos tercios. Es la primera vez que este arbol tiene esa
cifra sobre un binario optimizado.

Y un dato de metodo que salio de rebote: **`perf_ahora` es el 1,3-1,6 % de las muestras**. El
propio `--perf` se cobra eso, y toda corrida con el puesto lo lleva.

## Lo que esto le hace al reparto de main_loop

Dentro de ese 38,7 % (Crazy Taxi, sin MMU) las piezas estan casi todas ya medidas, y **casi
todas dieron cero**:

| pieza | cuanto vale |
| --- | --- |
| las cuatro banderas por instruccion | **0** (fase 2.4) |
| la busqueda de la palabra y la tabla de 65536 punteros | **~0** (el cache de bloques) |
| la condicion del bloque periodico | medido con el grano |
| **la linea de barrido** | **2,3 %** — ver abajo |
| **la llamada indirecta al manejador** | **sin medir** |

Queda una sola candidata grande, y es la que el cache de bloques **no** toco: aquel reemplazo
saco la busqueda y la tabla pero seguia haciendo **una llamada indirecta por instruccion**
(`cur->f(cur->instr)`). O sea que lo que queda adentro de `main_loop` es, en buena parte, la
llamada en si: su costo de entrada y salida y su prediccion.

**Eso rehabilita en parte el caso del recompilador dinamico**, que es justo lo que elimina esa
llamada inlineando el cuerpo del manejador. Lo que el cache de bloques refuto fue que el
*despacho* --encontrar el manejador-- costara algo; no dijo nada sobre *llamarlo*.

## Los contadores de hardware no se pudieron tomar

Los modos `cuentas` y `fallos` salieron vacios: cabecera sin filas y un informe de pilas con
todas las tablas en blanco.

**La causa es del sistema, no del script: VBS esta corriendo** —`VirtualizationBasedSecurityStatus`
en 2, con integridad de memoria, y `HypervisorPresent` verdadero—. Con VBS activo el hipervisor
es dueno del PMU y la programacion de contadores desde ETW **falla en silencio**; la ayuda de
`xperf -Pmc` lo dice sin nombrarlo: *"failures while programing the counters will not result in
trace start failure, unless 'strict' is specified"*. `xperf -pmcsources` sigue listando las
fuentes porque esa lista es una capacidad estatica del procesador, no una prueba de que se
puedan programar.

Para tomarlos hay que apagar VBS y reiniciar, que es bajar una proteccion del sistema. Instalar
VTune probablemente no alcance: el bloqueo es el mismo. **Mientras tanto, la via es la de
siempre en este arbol: medir por delecion**, que es lo que dio el 2,3 % de abajo.

## La linea de barrido salia de ese perfil, y vale 2,3 %

`if (reloj_total - marca_linea >= pvr_ciclos_linea)` se evaluaba **por instruccion**: tres
cargas de globales y una comparacion para descubrir, mil veces de cada mil, que el reloj no se
habia movido.

Y no podia haberse movido: **`reloj_total` solo avanza dentro del bloque periodico**, que es el
unico sitio del arbol que lo toca. Asi que la comprobacion se movio ahi adentro.

| | ms reales | velocidad | fps | ns/instr |
| --- | --- | --- | --- | --- |
| antes | 111 836 / 111 962 / 111 863 | 1,60× | 89,4 | 5,0 |
| **despues** | **109 424 / 109 203** | **1,64×** | **91,4** | **4,9** |

**2,3 %**, con la ejecucion identica al digito: 22 279 918 813 instrucciones, 10 001 cuadros,
9994 escenas, 1183 tiras.

Lo unico que puede volver cierta la condicion sin que `reloj_total` avance es que el guest
escriba SPG_LOAD o SPG_CONTROL y cambie `pvr_ciclos_linea`; en ese caso la linea sale hasta
RELOJ_GRANO ciclos mas tarde, que es la misma granularidad con la que ya corren el TMU, el WDT
y el AICA.

Barandas, que aqui no son opcionales porque el cambio mueve **cuando** se cuenta una linea:
`ctest` **21/21**, `dcemu_sh4json` **113 191 ok / 0 fallan** bit a bit, y **DCDoom con
`--captura-gl` en el mismo SHA-256 de siempre (`36578f59…`)** con sus 1482 cuadros y 977
escenas.


---

# La llamada indirecta: medida, y cuesta menos que su codigo (2026-08-04)

Era lo unico del cuerpo de `main_loop` que quedaba sin medir, y lo que decidia si un
recompilador dinamico tiene de donde sacar su ganancia. El cache de bloques habia quitado la
busqueda de la palabra y la tabla de 65536 punteros sin ganar nada, pero **seguia haciendo una
llamada indirecta por instruccion**. Esto la quita.

## El experimento

Los **diez manejadores mas frecuentes en linea dentro del bucle** (`despacho_inline()` en
`main.c`, detras de `-DDCEMU_INLINE=ON`): `MOV Rm,Rn`, `MOV #imm,Rn`, `ADD Rm,Rn`,
`ADD #imm,Rn`, `MOV.L @(disp,Rm),Rn`, `MOV.L Rm,@(disp,Rn)`, `MOV.L @Rm,Rn`, `MOV.L Rm,@Rn`,
`TST Rm,Rn` y `CMP/EQ Rm,Rn`. Cuerpos copiados **literalmente**, rarezas incluidas: `MOV Rm,Rn`
no suma ciclos en su manejador y aqui tampoco.

Se escribio como optimizacion y no como sonda a proposito: si rendia, se quedaba; y rindiera o
no, la fraccion cubierta permite extrapolar a todas las instrucciones.

**Cubre el 35,3 %** — 7 207 962 747 de 20 416 265 135 instrucciones.

## El resultado

Los dos binarios con su **propio** perfil de PGO al 100 %, para que la comparacion no arrastre
un perfil desalineado:

| | ms reales | velocidad | fps |
| --- | --- | --- | --- |
| **sin inline** | 109 017 / 109 122 / 108 454 | **1,65×** | **91,8** |
| con inline, 35,3 % cubierto | 130 018 / 129 735 / 129 684 | **1,38×** | 77,0 |

**Cuesta 19 %.** Tres vueltas de cada uno dentro del 0,5 %, y la prueba de que las copias de
los manejadores son exactas es que las seis corridas dan **22 279 918 813 instrucciones,
10 001 cuadros, 9994 escenas y 1183 tiras**, idénticas al digito.

## Por que, y lo que ensena

No es la logica: es el **tamano del codigo caliente**. `main_loop` engorda con 120 lineas
forzadas en linea y eso se paga en cache de instrucciones mas de lo que ahorran las llamadas.

Dos senales lo confirman:

- **Con el perfil viejo iba mejor que con el fresco** (122 298 contra 129 700 ms). Un perfil
  desalineado normalmente cuesta; aqui compensaba, porque dejaba parte del codigo nuevo fuera
  del camino caliente.
- **Sin forzar el inline sale 20 % peor todavia** (131 376 ms). `DC_INLINE` es `__inline`, que
  es una *pista*, y MSVC la ignora en una funcion de este tamano: la primera version quedo como
  llamada de verdad y agregaba una llamada encima de la que ya habia. **Sin `__forceinline`
  este experimento no medía lo que decía medir**, y habría dado la respuesta correcta por la
  razon equivocada.

## Con esto, el cuerpo del bucle esta agotado

| pieza de `main_loop` | cuanto vale |
| --- | --- |
| las cuatro banderas por instruccion | **0** (fase 2.4) |
| la busqueda de la palabra y la tabla de 65536 punteros | **0** (cache de bloques) |
| **la llamada indirecta al manejador** | **negativo** (esto) |
| **la linea de barrido** | **+2,3 %**, cobrado |

Las cuatro piezas que se pueden atacar sin cambiar el modelo de ejecucion estan medidas y solo
una rindio.

## Y lo que le hace al recompilador dinamico

Le quita el ultimo argumento que le quedaba en este arbol, y le agrega uno en contra.

El que le quedaba era que la llamada por instruccion fuera el impuesto. **No lo es**: quitarla
para un tercio de las instrucciones sale 19 % mas caro. Un recompilador seguiria teniendo de
donde ganar --mantener registros del SH-4 en registros del anfitrion entre instrucciones, no
actualizar `PC` ni el contador de ciclos una por una, plegar constantes--, pero eso ya no es
"quitar el sobrecosto del interprete": es otra cosa, mas dificil de estimar y sin ninguna cifra
de este arbol que la respalde.

Y el argumento nuevo en contra es el propio resultado: **el tamano del codigo caliente es un
factor de primer orden aqui**, y un recompilador genera muchisimo mas codigo que 120 lineas.
Con 168 bloques cubriendo el 90 % de las instrucciones el codigo traducido seria chico en
bloques pero grande en bytes, y este experimento dice que eso importa.

## Donde queda

Detras de `-DDCEMU_INLINE=ON`, **apagado**, como el cache de bloques y como `--hilos`: el
codigo es correcto, esta medido, y la proxima persona que quiera atacar la llamada indirecta
puede correr el A/B sin rehacer el experimento.

---

# La alineación del contexto, medida (2026-08-05)

## La fase 3.1 ya estaba hecha, y la tabla decía que no

Este trabajo empezó por un error de lectura que conviene dejar anotado, porque la trampa
sigue puesta para el que venga: **la tabla de «Orden y barandas» de este documento es la
propuesta original de la fase 3, no un estado**, y su fila 3.1 decía «a medir». Reordenar
`context_t` está hecho desde el commit `d029a1d` y medido en `rendimiento-plan.md` junto al
ARM7 y `mem_base_directa`: **1-2 % cada una en Debug y ≈0 en Release**.

O sea que la recomendación de «atacar el layout de datos» se apoyaba en una fila vencida.
La tabla queda corregida arriba.

## Lo que sí faltaba: la alineación

Lo que el reordenamiento no podía garantizar por sí solo es dónde empieza la estructura.
`sh4_cpu` no tenía alineación declarada, así que su alineación natural son 8 bytes —el mayor
miembro es un puntero— y **el enlazador puede dejar `core` en cualquier frontera de 8**. Los
primeros 80 bytes calientes caen en dos líneas de caché o en tres según cómo haya quedado, y
eso vuelve a sortearse en cada enlace. Es la misma lotería de disposición que PGO existe para
cerrar, salvo que PGO ordena **código** y esto es un dato.

Lo mismo del otro lado del `memcpy` más caro del árbol: `excepcion_instantanea_tomar()` copia
`core.context` a `instantanea_contexto` **una vez por instrucción** en el guest con MMU —
5 105 400 739 veces en los 35 segundos del banco de DCDoom, 0,94 por instrucción—. Y los dos
bancos de coma flotante, de 64 bytes justos, salían de `malloc()`, que no respeta alineación
extendida: caían en dos líneas tres de cada cuatro veces.

## Dos variantes, porque la primera se pagaba sola

**Alinear el tipo** (`struct DC_ALINEADO(64) context_t`) es lo obvio y tiene un efecto que no
lo es: redondea `sizeof` hacia arriba, de **176 a 192**. Esos 16 bytes de más los copia la
instantánea 5100 millones de veces.

**Alinear las variables** —`core`, `instantanea_contexto`— coloca los objetos igual de bien y
deja `sizeof` en 176. Los bancos sí llevan la alineación en el tipo, porque ya medían 64 y
redondear no cambia nada; a cambio `BANK0`/`BANK1` dejaron de salir de `malloc()` y son dos
objetos estáticos.

## Los números en DCDoom: nada

Banco de DCDoom, `--perf --salir-tras=35`, Release + PGO, i9-13900. Alternando los dos
binarios dentro de una misma tanda y descartando la primera pasada de cada uno, cuatro pares
por tanda. Trabajo idéntico verificado en las tres: **5 433 052 826 instrucciones, 1482
cuadros y 5 105 400 739 instantáneas**, al dígito.

| | media | rango | contra su propio A |
| --- | --- | --- | --- |
| base (sin alinear) | 48 242 ms | 47 861 - 48 992 | — |
| **alineando el tipo** (sizeof 192) | 48 601 ms | 48 051 - 49 142 | **+0,7 % más lento** |
| base (sin alinear), segunda tanda | 47 520 ms | 47 321 - 47 672 | — |
| **alineando las variables** (sizeof 176) | 47 370 ms | 47 332 - 47 402 | **−0,32 %** |

**Las dos son ruido**, y el piso de ruido salió medido por accidente: una tanda de diez
corridas que creí que alternaba binarios y en realidad corrió **el mismo diez veces** dio
47 842 - 48 991 ms, o sea **2,4 % de dispersión sin que nada cambiara**. Ni el 0,7 % ni el
0,32 % salen de ahí.

Un detalle que sí dice algo: **la dispersión de la variante alineada es 70 ms contra los 351
de la base** (0,15 % contra 0,74 %) en la misma tanda. Es exactamente la forma que tendría el
efecto si existiera —no correr más rápido, correr más parejo—, y con lo que viene abajo se
entiende mejor.

Y de paso: **la misma base midió 48 242 ms en una tanda y 47 520 en la siguiente**, un 1,5 %
de deriva entre tandas separadas por veinte minutos. Vuelve a confirmar que sólo se puede
comparar dentro de una tanda, que es la regla que este árbol ya pagó tres veces.

## Los números en los guests sin MMU: −2,4 % y −1,9 %

Y aquí está lo que DCDoom escondía. Mismo par de binarios, bancos canónicos de 180 segundos,
con **el orden dado vuelta dentro de los pares impares** por si el primero de cada par pagaba
algo por serlo.

| Crazy Taxi (4 pares) | media | rango |
| --- | --- | --- |
| base | 108 050 ms | 107 194 - 108 683 |
| **alineado** | **105 440 ms** | 104 885 - 106 556 |

| Virtua Tennis (3 pares) | media | rango |
| --- | --- | --- |
| base | 123 616 ms | 123 364 - 123 757 |
| **alineado** | **121 261 ms** | 120 600 - 121 606 |

**−2,4 % en Crazy Taxi** (de 1,65× a 1,69×, o 91,7 a 94,8 fps) y **−1,9 % en Virtua Tennis**
(de 1,45× a 1,48×). En las dos, los rangos son **disjuntos**: la peor corrida alineada es
mejor que la mejor sin alinear. Contando la tanda anterior de Crazy Taxi son **diez pares de
diez** a favor, con las dos ordenaciones.

Y el trabajo es idéntico al dígito en las veintidós corridas: **22 279 918 813 instrucciones,
10 001 cuadros, 9994 escenas y 1183 tiras** en Crazy Taxi; **23 611 808 332, 10 551, 10 542 y
1925** en Virtua Tennis.

## Por qué el guest más lento es el que menos lo nota

Es al revés de lo que uno esperaría, porque DCDoom es el que copia el contexto entero cinco
mil millones de veces. La explicación más simple que encaja con las tres cifras:

**el efecto es de residencia, no de volumen.** En los guests Katana `core` se toca varias
veces por instrucción y nada lo desaloja, así que si sus 80 bytes calientes cruzan a una
tercera línea el costo se paga siempre. En DCDoom el conjunto activo es mucho más grande —la
UTLB, las tres cachés de traducción, `mmu_datos` con sus 4096 entradas, el guest de Windows
CE— y `core` compite por L1 con todo eso; además la instantánea ya recorre 176 bytes de
corrido, que es tráfico de líneas enteras al que la alineación le cambia poco.

Dicho de otra forma: **la alineación ayuda donde la estructura vive en caché, y DCDoom es
justamente el guest donde no termina de vivir**. Es una hipótesis, no una medición: lo
medido son los dos números, y confirmarla pediría contadores de hardware, que en esta máquina
están cerrados por VBS.

## Qué queda en el árbol y por qué

Queda la variante de variables, ahora con dos motivos y no uno: **vale 1,9-2,4 % en los guests
sin MMU** y **hace cierto lo que el reordenamiento suponía**, sin costar nada —mismo `sizeof`,
mismo trabajo, dos `malloc()` menos—. Con ella entran dos comprobaciones de compilación:

```c
DC_ASSERT(context_caliente,
    offsetof(context_t, registers) + 16 * sizeof(DWORD) <= 128);
DC_ASSERT_SIZE(context, context_t, 176);
```

La primera falla si alguien mete un campo frío adelante y empuja los registros fuera de las
dos primeras líneas; la segunda, si el contexto engorda —lo que la instantánea pagaría por
instrucción—. Las dos convierten en error de compilación algo que si no cuesta un 1 % que
nadie iba a atribuir a eso.

`DCEMU_SIN_ALINEAR` deja el A/B corrible. Es de compilación y no una variable de entorno como
el resto de las sondas, y esta vez la excepción es forzosa: lo que se mide **es** la
disposición de los datos, así que no hay forma de tener las dos en un binario.

## Las dos trampas de esta tanda

**El intercambio de binarios falló en silencio.** La primera versión del script tenía un `if`
dentro de un `Join-Path` —PowerShell exige `$(...)`—, `$src` salía nulo, `Copy-Item` fallaba
con un error que se perdía entre la salida, y **las diez corridas midieron el mismo binario**.
Los números se veían perfectos: trabajo idéntico, dispersión razonable, una diferencia
pequeña entre «A» y «B». Es otra vez la forma de falla de siempre en este árbol —algo que no
hace nada y no avisa— y ahora el script verifica el hash del ejecutable después de copiarlo.

**`-DCMAKE_C_FLAGS=/DDCEMU_SIN_ALINEAR` no llegó al compilador.** Configuró sin quejarse y el
binario salió byte a byte idéntico al de la rama contraria, que es lo único que lo delató. El
A/B terminó haciéndose con un `#define` temporal en `lnxdefs.h`. No se investigó más: lo que
importa es que **el hash de los dos binarios es la única prueba de que un A/B compara dos
cosas**, y vale para el compilador tanto como para el script.

## Y lo que esto le agrega al mapa

El cuerpo del bucle ya estaba agotado; ahora también lo está su lado de datos, al menos en lo
que es colocación:

| pieza | cuánto vale |
| --- | --- |
| las cuatro banderas por instrucción | 0 (fase 2.4) |
| la búsqueda de la palabra y la tabla de 65536 punteros | 0 (caché de bloques) |
| la llamada indirecta al manejador | negativo |
| **el orden de los campos de `context_t`** | **≈0 en Release** (fase 3.1, ya medido) |
| **la alineación de `core` y de la instantánea** | **−2,4 % y −1,9 % sin MMU, ≈0 en DCDoom** (esto) |
| la línea de barrido | +2,3 %, cobrado |

Es la segunda cosa que rinde de las seis, y la primera que rinde **sin tocar una sola línea
del camino de ejecución**: no se quitó trabajo, se cambió dónde viven 176 bytes.

Eso corrige de paso la lectura que la fase 3.1 había dejado. «Reordenar `context_t` da ≈0 en
Release» era cierto **y engañoso**: el reordenamiento sin alineación es media medida, porque
deja el resultado a lo que el enlazador haga esa vez. Las dos juntas valen 2,4 %; medida cada
una por su lado, ninguna valía nada. Vale la pena tenerlo presente antes de archivar otra
optimización de disposición como «no rinde».

Lo que sigue abierto es la observación que abrió esta línea: **8,4 ns por instrucción en los
menús contra 14,4 en juego, con la misma tabla de despacho**. Sigue sin explicación, y esto
no la da —si acaso la refuerza, porque el mismo cambio rinde donde el conjunto activo es
chico y no rinde donde es grande—. Si la diferencia es de conjunto de trabajo, el que crece
es el **del guest** —su código, sus datos, sus texturas—, y eso no se arregla ordenando
estructuras del anfitrión.

---

# 0.1, por fin: contadores de hardware sobre Release+PGO (2026-08-07)

La consola elevada llegó y el paso 1.1 se corrió entero: los tres bancos —DCDoom, Crazy
Taxi y Virtua Tennis, con la receta de siempre y la traza arrancando ya en juego—, sobre el
binario Release con el PGO recién reentrenado. Los CSV crudos quedan en la raíz
(`perfil-<banco>-<modo>.csv`, fuera de git) y los `.etl` en `%TEMP%`.

## Cómo se midió, y las dos trampas del camino

De los tres modos de `perfil-pmu.ps1`, **dos volvieron vacíos y el script dijo «listo»
igual**: la acción `-a pmc` de esta versión de xperf agrega cero filas (los eventos sí
están en el `.etl`), y `-PmcProfile` no graba **ningún** evento en este procesador híbrido.
La sonda que confirma lo que vino a probar, otra vez. Se recuperó todo del volcado crudo
(`-a dumper -symbols`): cada interrupción de perfil deja un par de filas
`Pmc`/`SampledProfile` con los cuatro contadores y el PC, y
`herramientas/pmu-analizar.py` las une y agrega — es la agregación que `-a pmc` debía
hacer.

La segunda trampa la delató el propio resultado: **las filas `Pmc` traen lecturas
acumulativas del contador de su núcleo, no deltas.** Sumarlas tal cual dio 7,2
exa-instrucciones y el mismo IPC —3,47— en todas las funciones, que es el cociente de dos
acumulados grandes y es imposible como atribución. El delta real es lectura menos lectura
anterior *del mismo núcleo* (que viene en la fila `SampledProfile` del par), y con eso los
números cierran por tres lados: la frecuencia da los ~5,2 GHz del P-core, los ciclos por
instrucción emulada reproducen los ns/instr de la fase 6, y el reparto por función coincide
con el del modo `tiempo`.

## Los tres números

Hilo principal de dcemu, ventana de traza ya en juego:

| banco | IPC | fallos de salto /1000 instr | fallos de LLC /1000 instr |
| --- | --- | --- | --- |
| DCDoom (MMU) | **3,89** | 0,27 | 0,09 |
| Crazy Taxi | **3,39** | 1,03 | 0,17 |
| Virtua Tennis | **3,41** | 0,91 | 0,16 |

Y el volumen, derivado con los ns/instr de la fase 6 a ~5,2 GHz:

| banco | ciclos / instr emulada | instr de anfitrión / instr emulada |
| --- | --- | --- |
| Crazy Taxi, Virtua Tennis | ~26-27 | **~90** |
| DCDoom | ~46 | **~180** |

El propio encabezado de `perfil-pmu.ps1` dejó escrita la regla de decisión antes de medir:
*«IPC bajo con pocos fallos de caché significa dependencias y llamadas indirectas; alto
significa que simplemente hay mucho trabajo»*. Salió **alto**. El intérprete retira 3,4-3,9
instrucciones por ciclo con el predictor acertando el despacho casi siempre —un fallo cada
1000-3700 instrucciones de anfitrión, o sea **uno cada 11-20 instrucciones emuladas**, que a
~17 ciclos el fallo son un 2 % de los ciclos en DCDoom y un ~6 % en los Katana— y con la
LLC prácticamente sin fallar. No está parado esperando nada: **está ocupado ejecutando ~90
(Katana) o ~180 (DCDoom) instrucciones de anfitrión por cada una emulada.**

## Lo que decide

- **La motivación clásica del recompilador —el salto indirecto que no se predice— queda
  refutada en esta máquina.** El predictor del 13900, con el layout de PGO, se traga la
  tabla de 65536 entradas. Y eso **explica el cero del caché de bloques** (arriba): quitaba
  búsquedas que ya eran aciertos de L1 detrás de un salto ya predicho.
- **El candidato C —reorganizar `core` por caché— queda muerto.** Con 0,09-0,17 fallos de
  LLC por mil no hay presión de DRAM que ordenar; lo que la alineación ya cobró (2,4 %) era
  de líneas L1, y no hay una segunda cosecha visible.
- **La pregunta de los 8,4 contra 14,4 ns se acota**: en juego la LLC casi no falla, así
  que si la diferencia es de conjunto de trabajo vive en L1/L2 (este juego de contadores no
  los ve), no en memoria.
- **Un recompilador sigue siendo la única palanca grande, pero por la razón contraria a la
  esperada**: no arreglaría fallos —no los hay— sino **volumen**. Traducir de verdad —fundir
  la extracción de operandos, el avance de PC, las ocho comprobaciones del bucle— puede
  bajar de ~90 a ~15-25 instrucciones de anfitrión por emulada; cualquier variante que solo
  acelere el despacho no tiene de dónde cobrar, que es exactamente lo que el caché de
  bloques ya midió. La expectativa de 2-2,5× total del plan se sostiene por Amdahl
  (intérprete 71,6-93 %), y **DCDoom es donde más paga**: con el intérprete al 93 % y
  0,73×, llegar a velocidad de consola pide apenas ~1,5× del intérprete — un recorte de un
  tercio del volumen, sin necesitar los 4× de la literatura.

## El reparto adentro, de paso

El modo `tiempo` (muestreo con la traza ya en juego) da el reparto que el plan pedía en
fase 0: `main_loop` con los manejadores calientes inlineados por LTCG+PGO se lleva el
**42 % exclusivo** del hilo en Crazy Taxi y el **60 %** en DCDoom; las islas con nombre son
el ARM7 y sus familias de opcodes (`arm7_paso`, `op_datos`, `op_bloque`,
`op_transferencia`, más `aicadsp_paso` — el ~15 % conocido), la MMU en DCDoom
(`mmu_traducir` 8,6 % + `utlb_buscar` 0,9 %), el driver de GL en Crazy Taxi
(`nvoglv64.dll`, ~6,7 %) y los manejadores de FPU, que son los de peor IPC del árbol (2,86-2,89
en `fmul195`/`ftrv`, con 1,7 fallos/ki — dependencias de datos, no despacho). Y una
verruga honesta: `perf_ahora` —el `QueryPerformanceCounter` de `--perf`— pesa ~1,3 % de la
corrida que lo mide.
