# Plan: el intérprete del SH-4

Estado: **propuesto**. Escrito el 2026-08-02 sobre la rama `rendimiento-hilos`, después de
que [`rendimiento-plan.md`](rendimiento-plan.md) dejara al intérprete como lo único que
queda pesando.

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
