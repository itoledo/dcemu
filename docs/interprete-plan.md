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
