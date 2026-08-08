# Plan: el recompilador dinámico

Estado: **fase 0 hecha** (2026-08-07, el mismo día en que se escribió el plan); el resto,
propuesto. Las dos sondas de la fase 4 de [`rendimiento-plan-2.md`](rendimiento-plan-2.md)
le dieron a este plan sus números y son la vara de aceptación de la fase 0. El resultado de
esa fase está al final, en «La fase 0, hecha»: **el emisor, el caché y la convención
existen y no mienten**, el bloque de Katana quedó **en paridad con el C fusionado** y el de
MMU en el 71 % — y la medición corrigió una premisa de este documento: la llamada al
ayudante de memoria **sí** era el costo.

La forma es **un JIT de verdad**: traducción de bloques del SH-4 a código x64 emitido en
tiempo de ejecución. La alternativa —bloques traducidos por anticipado a C, compilados
con el emulador— se consideró y se descartó por decisión de alcance: cubre solo el
parque conocido y exige una pasada de generación por juego. El JIT traduce lo que corra,
sin pasada previa. Lo que esa elección paga, y cómo se desarma cada costo, está en su
sección; lo que no cambia con la forma es todo lo que las sondas ya probaron.

## Por qué existe, en cuatro números

El perfil con contadores de hardware (el 0.1) dijo que el intérprete va limitado por
**volumen** — ~90 instrucciones de anfitrión por emulada en Katana, ~180 con MMU, con el
predictor y la LLC limpios — y las sondas de fusión midieron cuánto se recupera
traduciendo:

| medida | valor | dónde |
| --- | --- | --- |
| factor por porción, Katana (lazo de cargas) | **1,6×** (5,3 → 3,3 ns) | fase 4 |
| factor por porción, con MMU (blit de DOOM) | **2,2×** (9,3 → 4,3 ns) | la sonda con MMU |
| bloques que cubren el 90 % de las instrucciones | **91** (DCDoom) / 168 (Crazy Taxi) | la forma, 2026-08-04 |
| ejecuciones por bloque | 24 154 / 334 549 | ídem |

La aritmética que esto habilita: **2,2× sobre el 90 % del volumen de DCDoom ≈ 1,9× del
intérprete → de 0,80× a ~1,5×** — consola cruzada con margen y los 60 fps (1,42×) a la
vista. Sega Rally 2 (0,77×) es el mismo caso. Los Katana no lo necesitan y lo reciben
igual.

Y las dos incógnitas que un dynarec no podía asumir sin medir ya no lo son:
**la reejecución tras una falta funciona sin instantánea** (el volcado pre-acceso deja
el contexto en el estado pre-instrucción exacto — la «salida 3» de la fase 5, lograda
estáticamente) y **la validez del código bajo la MMU es barata** (las palabras
verificadas por entrada, por la página que la búsqueda ya resolvió). Las dos con la
ejecución **idéntica al dígito**: 5 433 038 875 instrucciones, 850 557 faltas incluidas,
capturas byte a byte.

Dos advertencias del propio árbol que el diseño carga desde el arranque:

- **El despacho nunca fue el costo** (caché de bloques 0, tabla compacta −2 %, inline
  −19 %). Lo que paga es *fusionar*: registros del guest en registros del anfitrión,
  PC y ciclos por bloque, sin frontera de llamada por instrucción. Un JIT que solo
  enhebre manejadores repetiría el cero del caché de bloques.
- **El tamaño del código caliente es de primer orden** (el −19 % del inline). El emisor
  tiene que producir código chico y el caché tenerlo junto.

## La regla que no se negocia

**Un bloque traducido es invisible.** La misma cuenta de instrucciones al dígito, los
mismos cuadros, escenas y tiras, la misma captura y el mismo `.wav`. Las sondas
establecieron cómo, y esto asciende de técnica a invariante del JIT:

- **Los cortes del bloque periódico caen en cada frontera de instrucción**
  (`cycles >= RELOJ_GRANO || intc_sh4_reintentar`), y la salida deja el PC en la
  instrucción siguiente. Es lo que hace que la entrega de interrupciones no se corra ni
  un ciclo. Tras una instrucción de cero ciclos el corte se omite: la condición no pudo
  cambiar.
- **Los ciclos son los del manejador de cada instrucción**, rarezas incluidas: `mov3`
  no suma, el `NOP` de una ranura tampoco, la ranura va pegada a su salto sin corte en
  el medio. Un ciclo de más ya cambió un `.wav` entero en otra sonda.
- **Antes de cada acceso a memoria: volcar los registros anfitrión sucios, los ciclos y
  el PC de esa instrucción al contexto, con la instantánea invalidada.** Una falta sale
  por `longjmp` — que desenrolla también el marco del código emitido, y por eso el
  código emitido no guarda nada propio en la pila que deba sobrevivir — con el contexto
  en el estado pre-instrucción exacto. El intento se cuenta como lo cuenta `run()`,
  antes del acceso: el error de conteo de la sonda CE, ya pagado, enseñó que contarlo a
  la salida pierde las entradas que faltan.
- **La memoria es la de siempre.** El código emitido no reimplementa `memread`: llama a
  ayudantes (`jit_leer32()`, `jit_escribir8()`, ...) que envuelven los macros reales —
  alineación, la traducción con su avance de URC, watchpoints, UBC de operandos, ruta
  lenta, todo viaja gratis.

  > **Corregido por la fase 0.** Este plan decía «la llamada no es el costo: está medido»,
  > apoyándose en la fase 6.6 de `rendimiento-plan.md`. **Es falso para el código emitido**,
  > y Crazy Taxi lo aisló: ese bloque no tiene MMU, ni sincronizaciones, ni registros fuera
  > de los cacheados, así que su única diferencia estructural con el C de la sonda era que
  > el C **expande el macro** y el JIT **llamaba** — y eso valía **2,2 ns (≈9 ciclos) por
  > acceso, la mitad de la ganancia del bloque**. El camino rápido en línea dejó de ser
  > una optimización de fase tardía y pasó a ser parte del emisor. Lo que no cambia es la
  > regla de fondo: **en línea va solo el caso plano** (alineación, MMU apagada, UBC de
  > operandos apagado, zona con base directa) y **todo lo demás cae al ayudante**, que es
  > el macro entero. No hay una segunda copia de las reglas de `mem.h` que pueda derivar.
- **Las palabras del bloque se verifican en cada entrada** contra la copia que la
  traducción guardó, por el puntero de página que la búsqueda de `main_loop()` ya
  resolvió. Código automodificado, otro proceso en la misma VA, otra imagen: la
  comparación falla, el bloque se invalida y se cae al intérprete. **Esta es también la
  política de invalidación de la v1** — verificar al entrar, no vigilar las escrituras:
  un gancho en `memwrite` es el camino más caliente del árbol y no se toca sin un
  número que lo obligue.
- **La traza, el UBC y el modo de depuración apagan el JIT.** Ven instrucción por
  instrucción; el intérprete es la semántica de referencia y la vista de diagnóstico.
  `--traza-desde`, el anillo de PC y el UBC siguen enteros porque al activarse el JIT
  se hace a un lado.
- **Todo lo no cubierto sale al intérprete en la instrucción exacta.** Un opcode sin
  plantilla corta el bloque; una guarda que falla sale limpia. La cobertura degrada; la
  corrección nunca.

## La arquitectura

### El traductor: por identidad de manejador, no por decodificador propio

El frente del traductor no decodifica dos veces: resuelve cada palabra por
`OP_HANDLER(oplist, instr)` — la expansión real de `opcodes[]` — y emite **por identidad
de manejador**: `add39` tiene su plantilla de emisión, `movl9` la suya, con los ciclos y
las rarezas copiados del cuerpo real. No existe un segundo decodificador que pueda
divergir del primero — la regla de una sola tabla, otra vez. Una palabra cuyo manejador
no tiene plantilla termina el bloque ahí.

Sin IR en la v1: emisión directa por plantilla con dos pliegues que las sondas ya
validaron — las direcciones de los literales de PC-relativo son constantes del bloque
(el *valor* se sigue leyendo: el literal es dato y sus páginas cambian), y los saltos
internos de destino constante se fusionan como aristas del bloque (el BF del lazo, el
BF/S del blit). Los indirectos, solo con guarda de destino visto (el JSR→RTS de Crazy
Taxi); lo demás termina el bloque.

**La FPU no entra en la v1.** Una traducción de FPU depende de PR/SZ y de los Enable de
FPSCR, y su conformidad bit a bit costó dos pasadas de SingleStepTests: un opcode de FPU
corta el bloque y se interpreta. DOOM es de enteros y no la necesita; Sega Rally 2 sí, y
por eso las plantillas de FPU son una fase propia, con la guarda de modo en la entrada
del bloque y `dcemu_sh4json` como juez de cada plantilla.

### El emisor: x64 propio, mínimo, sin dependencias

Un emisor de x64 escrito en el árbol (`jit_x64.c`), del tamaño del subconjunto que las
plantillas usan: mov/aritmética/lógica/corrimientos sobre registros y memoria, call,
saltos cortos, setcc. Nada de biblioteca externa — `msvc-build-plan.md` documenta lo que
costó sacarse de encima las dependencias, y un emisor de este tamaño son cientos de
líneas, no miles. Compiladores: el emisor es C portable; el código emitido es x64, que
es donde viven los dos toolchains del árbol. Otra arquitectura anfitriona es un backend
nuevo el día que exista el motivo.

**El mapa de registros es fijo**: los registros del SH-4 más calientes del bloque en
registros anfitrión no volátiles (r12-r15, rbx, rbp, rsi, rdi dan ocho), el resto en el
contexto. Fijo por bloque, no asignación global: es lo que las sondas modelaron con
locales y MSVC, y rindió 1,6-2,2× sin nada más fino. `T` se materializa en el contexto
al escribirse, como en las sondas.

**Convención de llamada**: el bloque emitido es una función `void (*)(void)` llamada
desde `main_loop`; prologa cargando sus registros mapeados del contexto, epiloga
volcándolos. Los ayudantes de memoria se llaman con la convención del anfitrión. El
`longjmp` de una falta desenrolla el marco emitido sin cooperación — por eso el volcado
pre-acceso es al contexto y no a la pila.

**Y por eso el código emitido lleva información de desenrollado** (fase 0). En Windows x64
`longjmp` no restaura registros y ya: desenrolla de verdad, preguntándole a
`RtlLookupFunctionEntry` por cada marco. Un marco sin entrada se toma por hoja — RIP en
`[RSP]`, `RSP += 8` —, que no es el de un bloque que empuja ocho registros y baja `rsp`:
**sin la tabla, la primera falta dentro de un bloque se lleva el proceso**, y ese es el
camino normal del guest con MMU (850 557 faltas en el banco). Se instala con
`RtlAddFunctionTable` sobre el arena, describiendo el prólogo; con cientos de bloques lo
que corresponde es `RtlInstallFunctionTableCallback`. De paso es lo que le permite a
`traza_caida_instalar()` cruzar el código emitido.

### El caché de código y el ciclo de vida

- **Un arena de tamaño fijo** (16 MB, `VirtualAlloc` con `PAGE_EXECUTE_READWRITE` en
  Windows, `mmap` con `PROT_READ|WRITE|EXEC` fuera; si algún día el W^X estricto
  importa, el flip RW→RX por lote de emisión es local a `jit_cache.c`). Lleno → se
  vacía entero y se retraduce lo que vuelva a calentar; con 91-168 bloques por juego no
  va a pasar, y el contador de vaciados lo vigila.
- **Umbral de calentura**: un contador por PC de entrada; a las N ejecuciones (N≈50,
  a barrer) se traduce. La traducción es síncrona en `main_loop` — a 24 000-334 000
  ejecuciones por bloque, traducir puede costar mil veces una ejecución y amortizar
  igual (la forma lo midió).
- **La emisión es determinista**: la emulación es determinista, así que el orden de
  calentamiento, la disposición del caché y el código emitido se reproducen corrida a
  corrida sobre el mismo banco. Es lo que mantiene válida la disciplina de medición de
  este árbol — el layout del código emitido no es una variable nueva entre corridas del
  mismo binario, que es donde se mide.
- **Metadatos por bloque**: PC de entrada, las palabras originales (la verificación de
  entrada), la dirección emitida, el modo (SR.MD y, cuando entre la FPU, PR/SZ/Enables)
  y el contador de invalidaciones.

### El despacho

Las sondas pagaron **una comparación de PC** por despacho y la vara está puesta: la
sonda de bloques predecodificados costó **−3,3 % en los menús**, así que el despacho del
JIT se mide también en menús y si no da ~0 ahí, se rediseña antes de seguir. La forma:
un mapa de bits de 8 KB indexado por `(PC >> 1) & 0xFFFF` como primer filtro — una carga
y una prueba de bit — y solo con el bit puesto la tabla real (mapeo directo por PC, una
comparación completa). Los contadores de calentura viven detrás del mismo filtro.

Encadenamiento bloque a bloque, superbloques, asignación de registros entre bloques:
**fase tardía y solo si el perfil lo pide**. Las sondas rindieron volviendo al bucle en
cada bloque.

## Las fases

| # | qué | prueba de aceptación | decide |
| --- | --- | --- | --- |
| 0 | **hecha** — el emisor mínimo y el caché; los dos bloques de `fusion.c` emitidos a mano por el JIT | ejecución al dígito ✓ en los dos guests; el A/B: Crazy Taxi **en paridad** con la sonda ✓, DCDoom al 71 % | que el emisor y la convención existen y no mienten ✓ |
| 1 | El traductor automático con ~30 plantillas de enteros; DCDoom entero | cuentas al dígito, `198B396F…`, `mmu-mapeo`, `basic/mmu/*`, A/B alternado con PGO reentrenado | **la fase entera: si DCDoom no cruza 1,0×, parar y entender** |
| 2 | Sega Rally 2 y el resto de los Windows CE; las plantillas de FPU con guarda de modo | banco de SR2 (`1B28D0D9…`), `dcemu_sh4json` bit a bit por plantilla de FPU, `.wav` donde aplique | la generalidad: segundo guest sin tocar el motor |
| 3 | El parque entero y el barrido de 150 demos | barrido con corrida de control; capturas de los juegos byte a byte | la adopción por omisión |
| 4 | Encadenamiento, camino rápido de RAM inlineado, superbloques | ídem, cada uno con su A/B | solo si el perfil lo pide |

Con los puntos de decisión escritos antes de medir: en la fase 1, primero el top-20 de
DCDoom (~40 % de cobertura); **si el factor por porción sostiene ≥1,8×, seguir al 91;
si baja de 1,3×, parar** — el 2,2× salió de un bloque y los bloques no son todos
iguales. Y en la fase 0, si el JIT no reproduce los números de las sondas sobre los
mismos bloques, el problema es del emisor y se arregla ahí, no se tapa con cobertura.

## Los riesgos, cada uno con su desarme

| riesgo | desarme |
| --- | --- |
| una plantilla infiel (ciclos, T, un caso borde) | la ejecución al dígito la delata en una corrida — el método atrapó un error de conteo del 0,12 % con los demás contadores en verde; y `dcemu_sh4json` sigue vigilando los manejadores de los que las plantillas copian |
| el emisor mismo (codificación x64 mal) | fase 0: los bloques de las sondas como oráculo, más un volcado desensamblable del caché (`DCEMU_JIT_VOLCADO=archivo`) para leer lo emitido |
| PGO no ve el código emitido | lo que PGO protege es la comparación entre binarios; el código emitido es determinista corrida a corrida y el A/B es de un solo binario (`DCEMU_JIT=0/1`), donde el layout emitido no varía. El binario portador se reentrena por fase, como siempre |
| el tamaño del código caliente (la lección del −19 %) | emisión compacta, bloques contiguos por orden de calentura, y el A/B de cada fase incluye los bancos que no usan el JIT |
| el despacho cuesta en los flacos | la vara de los menús (−3,3 % del antecedente); el mapa de bits primero; A/B en menús y en juego |
| código automodificado / procesos que comparten VA | la verificación por entrada, ya medida dentro del 2,2×; escrituras vigiladas solo si un perfil futuro demuestra que la verificación pesa |
| depurar código emitido | el JIT se apaga con la traza, el UBC y `DCEMU_JIT=0`: toda pregunta de diagnóstico se contesta con el intérprete, que ejecuta idéntico al dígito por construcción |
| el barrido no ve la MMU | las capturas de DCDoom y SR2 son la baranda ahí, como en toda la rama |
| `SIGSEGV`/`0xC0000005` dentro del caché | `traza_caida_instalar()` ya reporta; se le enseña a reconocer direcciones del arena y a nombrar el bloque (PC de entrada) al que pertenecen |

## Lo que este plan deliberadamente no hace

- **Tocar el intérprete.** Es la referencia, el fallback, la vista de diagnóstico y lo
  que corre todo lo no traducido. La relación `opcodes[]` ↔ suite ↔ manejador no se
  altera: las plantillas copian de los manejadores, no los reemplazan.
- **Un IR y un optimizador.** El pliegue de constantes de PC-relativo y la fusión de
  saltos internos salen gratis de la forma del bloque; lo demás espera a que un perfil
  lo pida con un número.
- **FPU en la v1.** Guarda de modo o nada, y `dcemu_sh4json` como juez cuando entre.
- **Perseguir el 99 %.** 696-1285 bloques compran el último 9 %; la cola no paga.
- **Otro backend de arquitectura.** x64 es donde viven los dos toolchains del árbol;
  ARM64 el día que haya una máquina que lo pida.

## La expectativa, para medirla contra esto

| guest | hoy | proyección (90 % × factor medido) | umbral de éxito |
| --- | --- | --- | --- |
| DCDoom | 0,80× · 34 fps | **~1,5×** (2,2× por porción) | ≥1,0× fase 1; 60 fps (1,42×) es el premio |
| Sega Rally 2 | 0,77× | ~1,4-1,5× (con FPU, fase 2) | ≥1,0× fase 2 |
| Crazy Taxi | 1,51-1,55× | ~2,3× (1,6× por porción) | no es objetivo; no debe empeorar |

Las proyecciones son aritmética sobre factores de UN bloque por guest, medidos con el
compilador del anfitrión como emisor de referencia; el JIT primero tiene que igualarlas
sobre esos mismos bloques (fase 0) y después reemplazarlas por medidas, fase por fase,
con las barandas de siempre: `ctest`, `dcemu_sh4json` bit a bit, el barrido con su
corrida de control, las capturas y el `.wav`, y ninguna cifra de velocidad antes de que
el trabajo salga idéntico al dígito.

---

# La fase 0, hecha (2026-08-07)

El emisor (`jit_x64.c/h`, con su suite en `tests/test_jit_x64.c`), el arena con su
información de desenrollado, los ayudantes de memoria, el despacho por mapa de bits y los
**dos bloques de `fusion.c` emitidos a mano por el JIT** (`jit.c/h`). Detrás de
`-DDCEMU_JIT=ON` y encendido con `DCEMU_JIT=1`, igual que la fusión y por el mismo motivo:
el A/B corre sobre una sola imagen. Compilando con `-DDCEMU_FUSION=ON -DDCEMU_JIT=ON` a la
vez, el mismo binario da la comparación de tres — intérprete, C fusionado, JIT.

## La ejecución es idéntica al dígito, y esa es la mitad que importa

Tres modos, dos guests, contadores y capturas:

| banco | instrucciones | cuadros | escenas | captura | porción cubierta |
| --- | --- | --- | --- | --- | --- |
| DCDoom, 35 s | 5 433 038 875 | 1482 | 977 | `198B396F…` | 476 702 040 en 2 090 557 entradas |
| Crazy Taxi, 60 s | 7 571 150 058 | 3051 | 3044 | `95FC0052…` | 3 408 680 950 en 15 533 821 entradas |

**Las tres columnas de cada fila son el mismo número en los tres modos**, la porción
cubierta incluida: el JIT y el C fusionado entran y salen de sus bloques en los mismos
sitios, la misma cantidad de veces. Cero rechazos por verificación de palabras en todas las
corridas. Y `198B396F…` es la línea base documentada del árbol, o sea que el JIT tampoco se
desvió del intérprete.

Eso cubre lo que la fase venía a decidir: la convención de llamada, el volcado pre-acceso,
los cortes del bloque periódico en cada frontera, los ciclos por manejador, el conteo del
intento antes del acceso y la verificación por entrada **son correctos tal como el plan los
escribió**. Y el camino de falta también: el bloque con MMU sale por `longjmp` 850 557 veces
por corrida desde adentro del código emitido, y el proceso no se cae ni la cuenta se corre.

## El A/B: paridad con el C en Katana, 71 % con MMU

Un binario, órdenes rotados dentro de cada ronda, sin `--captura-gl` ni `--perf`. Tres
rondas en Crazy Taxi, cuatro en DCDoom.

| banco | intérprete | C fusionado | JIT |
| --- | --- | --- | --- |
| Crazy Taxi, 180 s | 132 333 ms | 108 662 ms (**−17,89 %**) | **108 575 ms (−17,95 %)** |
| DCDoom, 35 s | 53 773 ms | 51 274 ms (**−4,65 %**) | 51 996 ms (−3,30 %) |

**Crazy Taxi está en paridad**: el JIT quedó 87 ms por debajo del C fusionado sobre 108 000,
o sea dentro del ruido, y ganó en dos de las tres rondas. La sonda reprodujo de paso su
propio número documentado (17,89 % contra 17,4 %). DCDoom queda en el **71 % de la ganancia
de la sonda**, con una dispersión bastante mayor: la diferencia por ronda va de −365 a
+1636 ms.

## Los tres hallazgos, en el orden en que aparecieron

### 1. La llamada al ayudante **era** el costo

La primera versión daba 119 742 ms en Crazy Taxi — la mitad de la ganancia. Y ese bloque es
el que permite atribuirlo sin adivinar: no tiene MMU, no tiene sincronizaciones, y sus cinco
registros vivos están todos cacheados en registros del anfitrión igual que en el C. **La
única diferencia estructural era que el C expande el macro de `memread` y el JIT llamaba a
un ayudante.** 11 261 ms sobre 5137 M de accesos son **2,2 ns (≈9 ciclos) por llamada**.

Con el camino rápido plano emitido en línea la brecha cayó de 11 261 a 344 ms. Eso
contradice una premisa escrita en este mismo plan («la llamada no es el costo: está
medido»); la medición que la sostenía era de otra cosa.

### 2. Con MMU hay que emitir la traducción, y eso vale otro tercio

El bloque con MMU quedó primero al 60 %: ahí el camino plano no sirve —con la MMU encendida
nunca se tomaría— y los seis accesos por vuelta seguían pagando la llamada. Emitir
`MMU_TRADUCIR_EN_SITIO` en línea llevó la brecha de 1007 a 722 ms por corrida: **el 60 % pasó
al 71 %**.

Lo que hace correcta esa traducción emitida, y no una segunda implementación de la MMU, es
que **solo se emite el acierto**: las cuatro comparaciones (etiqueta, permiso, VPN,
generación), el avance de URC y la composición de la física. Cualquiera que falle cae al
ayudante, y de ahí a `mmu_traducir()`, que decide todo lo demás como siempre.

Y el guardarraíl que lo prueba no es la captura: es que **los contadores de traducción de
`--perf` salen idénticos al dígito** — 2 018 173 538 traducciones, 64,4 % ya resueltas,
850 557 faltas —, lo mismo que el intérprete. De URC depende qué entrada de la UTLB
reemplaza el `LDTLB` del guest, o sea su camino de ejecución: un acierto que no lo avanzara
no se vería en una captura, se vería mil millones de instrucciones después.

### 3. Dos cosas que solo aparecen corriendo

- **`longjmp` desenrolla de verdad en Windows x64.** Ver «Convención de llamada» arriba: sin
  `RtlAddFunctionTable` la primera falta dentro de un bloque se lleva el proceso. No aparece
  en el guest sin MMU y aparece en el primer segundo del que la tiene.
- **`PTEH` y `MMUCR` no se pueden direccionar desde el contexto.** Viven adentro de `regmem`,
  que es un `calloc` de 16 MB, y en Windows una reserva de ese tamaño no sale del montón
  chico: cayó a terabytes de la imagen y ningún desplazamiento de 32 bits la alcanza. Van por
  puntero guardado en el estado del JIT. **Lo descubrió el emisor negándose a emitir el
  bloque**, que es exactamente para lo que existe esa comprobación — la alternativa habría
  sido un bloque que lee basura. El mismo motivo hace que el `CALL` directo (cinco bytes, sin
  carga) no alcance nunca: `VirtualAlloc` sin dirección preferida deja el arena igual de
  lejos, así que hoy todas las llamadas van por la tabla. Pedir el arena cerca de la imagen
  es un pendiente barato.

Y una decisión de diseño que la medición enderezó: **la suma a `perf_instrucciones` y los dos
contadores de la MMU se especializan al emitir**. `perf_activa` no puede cambiar durante la
corrida y el bloque se emite después de leerla, así que se emiten o no se emiten. La primera
versión llevaba un puntero al destino para evitar la rama y pagaba una carga dependiente más
un segundo lee-modifica-escribe en cada sincronización.

## Lo que queda de la fase 0

**El bloque con MMU está en el 71 % de la sonda y lo que falta ya no es una pieza, es una
lista.** Son peepholes que MSVC hace sobre el C y un emisor por plantillas no hace todavía,
cada uno chico y ninguno un problema de diseño:

- R5, R6 y R7 viven en el contexto y no en registros del anfitrión (los ocho no volátiles
  están tomados; liberar el contador `n` plegándolo a constantes por punto de programa daría
  uno más, a costa de pelar la primera vuelta).
- La sincronización vuelca los cinco registros cacheados siempre, donde un análisis de
  «sucios» volcaría uno o dos.
- `SR.T` se escribe con dos lee-modifica-escribe seguidos sobre el mismo byte.
- `intc_sh4_reintentar` se recarga en cada corte, también entre dos instrucciones de ALU que
  no cruzan ninguna llamada.
- El bloque pasó de ~1000 a ~2950 bytes al emitir seis traducciones en línea. Sigue lejos del
  L1 de instrucciones, pero **el árbol ya midió que el tamaño del código caliente es de primer
  orden** (el −19 % del inline), así que es un sospechoso legítimo y no está descartado.

Ninguno está medido por separado. El orden razonable es medirlos antes de escribirlos, porque
esta fase ya mostró que las estimaciones a ojo sobre este bucle fallan: la especialización de
`perf` se estimó en 250 ms y valió ~70.

## Qué cambia esto para las fases siguientes

- **La fase 1 arranca con el camino de memoria en línea ya en el emisor**, en sus dos formas
  (plana y con traducción), no como optimización de la fase 4. La fase 4 se queda con el
  encadenamiento y los superbloques.
- **La proyección de DCDoom sale bien parada.** Con el bloque con MMU al 71 % de la sonda, la
  aritmética del plan baja de ~1,5× a ~1,4×: sigue cruzando 1,0× con margen y los 60 fps
  (1,42×) quedan justo en el límite en vez de cómodos. La lista de peepholes de arriba es lo
  que decide de qué lado cae.
- **La vara del despacho sigue sin medirse**: los menús no entraron en estas tandas, y con dos
  bloques el mapa de bits no dice nada. Va con la fase 1.

---

# La fase 1: el traductor automático anda, y es invisible (2026-08-07)

**Estado: el traductor existe y ejecuta idéntico al dígito en los dos guests. La cobertura
es la que queda por comprar.** Lo que sigue son las decisiones tomadas, el material
verificado y lo que la primera corrida midió.

## Lo que se generalizó en el emisor

Las ocho operaciones enteras de x86 en sus cinco formas —registro/registro,
registro/memoria, memoria/registro y las dos con inmediato— salen de una sola tabla, porque
**el opcode base de cada una es su número de extensión por ocho**. Con eso una plantilla del
traductor es una línea (`tr_alu_rr(g, t, X64_SUB, n, m)`) y no cinco funciones. Van también
los corrimientos por cuenta inmediata, `NEG`, `NOT`, `IMUL` con inmediato, `MOVSX`/`MOVZX` de
byte y palabra, y las formas con índice escalado. Cada una con su caso en
`tests/test_jit_x64.c`, byte a byte contra el manual: **si esa relación de «base por ocho» se
rompiera, cada plantilla emitiría la operación equivocada con la codificación bien formada**,
que es el peor modo de fallar — el guest divergiría sin que nada se queje.

## Las decisiones del traductor

- **Traducción por identidad de manejador**, como dice la arquitectura: cada palabra se
  resuelve por `OP_HANDLER(oplist, instr)` y se emite con la plantilla de *ese* manejador. La
  fila de la tabla no repite la codificación, así que una fila mal puesta es una plantilla
  que no se usa — nunca una instrucción mal decodificada.
- **El bloque no cruza una frontera de 1 KB**, que es la página más chica del SH-4. Así un
  solo puntero de búsqueda cubre todas sus palabras y la verificación por entrada no puede
  leer de otra página, sea cual sea el tamaño con el que el guest la haya mapeado.
- **Asignación de registros por bloque**: se cuenta cuántas veces toca cada `R0..R15` y los
  cinco más usados van a los registros del anfitrión. Es lo que la fase 0 hizo a mano y
  rindió; nada más fino hasta que un perfil lo pida.
- **El modo del bloque incluye `mmu_activa`**, que es lo que decide si el acceso se emite en
  su forma plana o con la traducción. En la fase 0 eso era una bandera puesta a mano; acá
  sale del estado real al traducir, y el despachador la verifica al entrar.
- **Semilla y crecimiento hacia atrás.** Un muestreo en el bloque periódico (que corre cada
  ~400 ciclos y por lo tanto no cuesta nada en el camino caliente) marca candidatos, y la
  salida de cada bloque siembra el siguiente, que es una frontera de bloque de verdad. Y
  cuando el descubrimiento termina en una rama hacia atrás cuyo destino cae **antes** del
  comienzo, se retraduce desde ahí: es lo que corrige que el muestreo caiga en mitad de un
  lazo en vez de en su cabeza.
- **Ramas**: `BF` y `BF/S` con destino adentro del bloque se pliegan como aristas (hacia
  atrás por etiqueta ya emitida, hacia adelante por parche pendiente); con destino afuera,
  salen al intérprete. Los dos caminos llevan su corte con el PC por donde siguen. En `BF/S`
  la ranura se emite **dos veces**, una por camino, porque al no tomar no corre y la palabra
  siguiente se ejecuta después como instrucción normal. Los saltos con ranura que quedan
  fuera de la v1 —`BRA`, `BSR`, `JMP`, `JSR`, `RTS`— terminan el bloque.
- **El censo de lo que corta.** Cada palabra sin plantilla que termina un bloque se anota; el
  resumen lista las que más cortaron. Sin eso la cobertura degrada en silencio, que es lo que
  este árbol llama un tope callado — y esa lista es la que dice qué plantilla escribir
  después, en vez de adivinarlo.

## Las 29 plantillas de la primera tanda, con sus ciclos verificados

Sacados del cuerpo real de cada manejador, no de memoria. Los que no aparecen acá se dejaron
afuera **porque su cuerpo no se leyó**, no porque no sirvan.

| ciclos | manejadores |
| --- | --- |
| 0 | `nop`, `mov3` (MOV Rm,Rn — la rareza que ya conocían las sondas) |
| 1 | `mov0`, `movl21`, `add39`, `add40`, `and72`, `and73`, `not75`, `tst80`, `tst81`, `cmpeq44`, `cmphs45`, `cmpge46`, `cmphi47`, `cmpgt48`, `extsw59`, `extub60`, `shll2`, `shlr2`, `shlr16` |
| 2 | `movl2`, `movl9`, `movl6`, `movb7`, `movb4`, `movb25`, `bf`, `bfs` |

Dos detalles que se pagan si se copian de la intuición en vez del código: **`movl21`
(`MOV.L @(disp,Rm),Rn`) suma 1 ciclo y no 2**, y **`shlr2` enmascara con `0x3FFFFFFF`** después
del corrimiento.

## La prueba de aceptación

El oráculo es la fase 0, en el mismo binario: `DCEMU_JIT=1` corre los dos bloques escritos a
mano y `DCEMU_JIT=2` correría el traductor. El bloque de DCDoom (`0002ef3e`) se traduce
entero con estas plantillas y sin ninguna rama con ranura, así que **la traducción automática
tiene que dar los mismos dígitos que la manual**: 5 433 038 875 instrucciones, 1482 cuadros,
`198B396F…` y los contadores de traducción de la MMU. El de Crazy Taxi necesita antes la
guarda de destino visto para su `JSR`→`RTS`, así que no sirve de oráculo todavía.

## Lo que la primera corrida dijo

`DCEMU_JIT=2` traduce; `DCEMU_JIT=1` sigue corriendo los dos bloques escritos a mano de la
fase 0, en el mismo binario, como oráculo.

**La corrección está**: con el traductor puesto, DCDoom da **5 433 038 875 instrucciones,
1482 cuadros y `198B396F…`**, y Crazy Taxi **7 571 150 058, 3051 cuadros, 3044 escenas, 1003
tiras y `95FC0052…`** — los mismos dígitos y las mismas capturas byte a byte que el
intérprete. Con 29 plantillas, asignación de registros por bloque, plegado de ramas
internas, muestreo, crecimiento hacia atrás y verificación por entrada.

**La cobertura no.** Los bloques salen de 1 a 2,4 instrucciones y cubren el 0,2 % de DCDoom.
El censo dice exactamente por qué, que es para lo que existe:

| palabra | veces que cortó un bloque | qué es |
| --- | --- | --- |
| `03AE`, `0636`, `0126` | 258 k cada una | `MOV.L` con índice `R0` (`movl27`, `movl24`) |
| `8DC1`, `8D0D` | 135 k | `BT/S` |
| `AFF3` | 93 k | `BRA` |
| `6274` | 93 k | `MOV.B @Rm+,Rn` |
| `000B` | 67 k | `RTS` |
| `880A` | 49 k | `CMP/EQ #imm,R0` |
| `644D`, `9173`, `4C00` | 32-35 k | `EXTU.W`, `MOV.W @(d,PC),Rn`, `SHLL` |

O sea: **las tres formas indexadas por `R0` y las ramas con ranura**. Ninguna sorpresa, y
ninguna adivinada — la lista salió de la corrida.

## Los dos errores que costó, y los dos valen la pena escribir

1. **La ranura de un `BF/S` se emite adentro del camino que toma, y ahí no hay
   sincronización previa.** Si tocara memoria, una falta saldría por `longjmp` con el PC de
   la rama y no el de la ranura, y el guest reejecutaría desde el lugar equivocado. Ahora un
   `BF/S` cuya ranura acceda a memoria termina el bloque antes de él.
2. **El crecimiento hacia atrás registra el bloque en la cabeza del lazo, que no es el PC que
   se pidió** — y correrlo igual es ejecutar desde otro lado. Es el que dio **616
   instrucciones de más sobre 803 millones**, con la captura y los cuadros idénticos: otra
   vez la forma de fallo que solo se ve en el contador. Ahora el bloque queda traducido y esa
   instrucción la hace el intérprete; el bloque se encuentra solo cuando el guest llega a su
   entrada.

> Los dos se detectaron con una corrida de seis segundos emulados contra una cuenta conocida.
> Ese es el bucle de trabajo del traductor: no hace falta el banco entero para saber que una
> plantilla está mal.

## Lo que sigue, en orden

1. **Las plantillas que el censo nombra.** Las tres `MOV.L` indexadas por `R0` primero: 775 k
   cortes entre las tres.
2. **Las ramas con ranura** (`BT/S`, `BRA`, `BSR`, `JMP`, `JSR`, `RTS`). Hoy terminan el
   bloque, y por eso los bloques salen de dos instrucciones. Es lo que más cobertura compra.
3. **El índice de bloques.** Con 4096 bloques hay ~4000 colisiones de mapeo directo: la tabla
   por `(PC>>1) & 0xFFFF` se queda corta y hay que asociarla o agrandarla.
4. **Recién ahí, medir.** Con bloques de dos instrucciones el despacho domina y cualquier
   número de velocidad hoy mediría el andamiaje, no la traducción.

## La segunda tanda: 52 % de cobertura, y una divergencia sin localizar

50 plantillas (las 29 más las que el censo nombró: las tres `MOV.L` indexadas por `R0`,
`@Rm+`, `@-Rn`, `BT`, `BT/S`, `BRA`, `BSR`, `JMP`, `JSR`, `RTS`, `CMP/EQ #imm`, `EXTU.W`,
`EXTS.B` y los corrimientos). Con eso:

- **El censo se secó**: lo que más corta bloques ahora aparece 42 veces, no 258 000.
- Los bloques pasaron de 2,4 a **7,8 instrucciones** y la cobertura de 0,25 % a **51,8 %**
  (2 813 M de 5 433 M en DCDoom).

Y en el camino, **dos fugas del andamiaje que no tienen nada que ver con las plantillas**:

1. **El índice de bloques estaba recortado a `(PC >> 1) & 0xFFFF`**, igual que el filtro. Eso
   distingue 128 KB de espacio de PC y DCDoom ejecuta en cuatro ventanas a la vez, así que
   los bloques se pisaban entre ellos. Ahora es una tabla hash sobre el PC entero — y el
   primer intento tomó los bits **bajos** del producto multiplicativo, que es una permutación
   de los 15 bits de abajo del PC: el mismo recorte que venía a sacar.
2. **El crecimiento hacia atrás duplicaba bloques sin fin.** Cuando el bloque se registra en
   la cabeza del lazo, ese PC no es el que se pidió, así que el despacho devuelve 0 — pero el
   bit seguía puesto, y la visita siguiente volvía a traducir la misma cabeza. 16 384 bloques
   de los que 16 123 ni entraban en la tabla.

**Y con eso apareció una divergencia: −11 002 instrucciones sobre 5433 millones**, con la
captura `198B396F…` y los 1482 cuadros intactos. La bisección por `DCEMU_JIT_PLANTILLAS=N`
—que se agregó para esto— dice lo que importa: **con las 29 plantillas de la primera tanda,
que estaban verificadas exactas, la cuenta también diverge ahora**. O sea que no son las
plantillas nuevas: es que al dejar de duplicar bloques se empezaron a ejercitar, por primera
vez, los bloques que **empiezan en la cabeza de un lazo y pliegan su rama hacia atrás como
arista interna**. El plegado (`tr_seguir_en`) es la parte más nueva y menos ejercitada.

### La divergencia, localizada: traducir tiene efecto colateral

La bisección con `DCEMU_JIT_PLANTILLAS=N` sobre el banco corto de seis segundos, contra la
cuenta conocida (803 038 036):

| N | qué agrega | cuenta |
| --- | --- | --- |
| 3 | `NOP`, `MOV #imm`, `MOV Rm,Rn` — **ningún acceso a memoria** | 803 038 036 ✓ |
| 5 | las dos primeras lecturas | 803 038 433 |
| 7 y arriba | todo lo demás | 803 037 883 |

O sea: **aparece con el primer acceso a memoria**, no con las ramas.

Antes de acusar a una plantilla hubo que descartar algo más básico, y ahí salió un hallazgo
suelto: **`DCEMU_SIN_CACHE_MMU=1` cambia la cuenta del intérprete** (803 037 786 contra
803 038 036). Ese interruptor debería ser semánticamente transparente — apaga cachés — y es
el que la rama usa para su A/B. Con una palanca de velocidad de verdad neutra
(`--captura-gl`, que cuesta 40 % de tiempo real) la cuenta **no se mueve** en cuatro
corridas, así que el banco es determinista y no depende del reloj: el que cambia la
ejecución es el interruptor. Queda anotado como pendiente propio.

Y la causa en el JIT, que es de la familia que este árbol conoce: **algo que hace trabajo de
más sin decirlo**.

> El camino rápido con MMU emite la traducción en línea, y **traducir tiene efecto
> colateral**: avanza `MMUCR.URC`. Si después la zona resulta no tener base directa
> —registros, PVR, GD-ROM, AICA, colas de almacenamiento— el acceso caía al ayudante de
> siempre, que **traduce otra vez**. URC avanzaba dos veces, y de URC depende qué entrada de
> la UTLB reemplaza el `LDTLB` del guest: su camino de ejecución.

Los bloques escritos a mano de la fase 0 no lo mostraron nunca porque sólo tocan RAM plana
—la textura, el mapa de color y el framebuffer—; el traductor toca de todo. El arreglo es un
segundo camino lento que entra por la **dirección física** (`memread_fisico`/
`memwrite_fisico`, con sus watchpoints), sin volver a traducir: la alineación y el UBC ya los
comprobó el camino rápido, así que lo único que faltaba era el despacho por zona.

### Con eso, el traductor es exacto y cubre la mitad

| guest | instrucciones | cuadros | escenas | captura | cobertura |
| --- | --- | --- | --- | --- | --- |
| DCDoom, 35 s | 5 433 038 875 | 1482 | 977 | `198B396F…` | **51,8 %** |
| Crazy Taxi, 60 s | 7 571 150 058 | 3051 | 3044 | `95FC0052…` | **67,1 %** |

Y el contador que delataba el error también vuelve al dígito: **2 018 173 538 traducciones,
64,4 % ya resueltas, 850 557 faltas**, lo mismo que el intérprete.

## El primer número honesto del traductor

Un binario, órdenes rotados, cuatro rondas en DCDoom y tres en Crazy Taxi. `DCEMU_JIT=1` son
los dos bloques escritos a mano de la fase 0; `DCEMU_JIT=2`, el traductor.

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 54 049 ms | 52 272 (−3,3 %) | **46 602 (−13,8 %)** |
| Crazy Taxi, 180 s | 133 312 ms | 110 985 (−16,8 %) | **138 560 (+3,9 %)** |

**Gana 13,8 % en el guest con MMU y pierde 3,9 % en Katana**, con 51,8 % y 65 % del volumen
traducido. Y la causa está en la misma línea del informe:

| | instrucciones por entrada |
| --- | --- |
| bloque a mano de Crazy Taxi | **219,4** |
| traductor, Crazy Taxi | **4,3** |
| traductor, DCDoom | 6,7 |

**Lo que se paga por entrada es lo que decide.** Cada entrada cuesta el filtro del mapa de
bits, la búsqueda en la tabla, la verificación de las palabras (un `memcmp`), el prólogo de
ocho empujes más la carga de los registros mapeados, y el epílogo entero. Repartido entre
4,3 instrucciones eso se come la traducción; repartido entre 219 desaparece. Por eso el
bloque a mano de Crazy Taxi rinde 16,8 % y el traductor sobre el mismo juego pierde: **no es
que traduzca peor, es que entra y sale 3371 millones de veces**.

Y Crazy Taxi es el caso extremo por un motivo conocido: su lazo caliente es el `JSR`→`RTS`
del callback, o sea un salto indirecto cada pocas instrucciones. El bloque a mano lo pliega
con una guarda de destino conocido; el traductor todavía no.

Por porción, DCDoom pasa de ~9,95 a ~7,30 ns por instrucción cubierta: **1,36×**, contra el
2,2× que la sonda de la fase 0 midió sobre su bloque de 17 instrucciones sin entradas. La
diferencia entre 1,36× y 2,2× **es** el costo por entrada.

## Lo que sigue, ordenado por lo que la medición dice

1. **Encadenar bloques.** Saltar de un bloque al siguiente sin volver a `main_loop` es lo que
   quita el prólogo, el epílogo, la búsqueda y la verificación de la mayoría de las entradas.
   Estaba escrito como fase 4 «solo si el perfil lo pide»: el perfil lo pide.
2. **La verificación por entrada.** 3371 millones de `memcmp` en Crazy Taxi. La regla —
   verificar al entrar, no vigilar las escrituras — sigue siendo la correcta, pero con
   encadenamiento la mayoría de las entradas desaparecen y las que queden pueden verificarse
   contra una generación de página en vez de palabra por palabra.
3. **El prólogo, a medida.** Ocho empujes por entrada cuando el bloque usa dos registros. Cada
   bloque ya tiene su propia `RUNTIME_FUNCTION`; darle su propio `UNWIND_INFO` permite empujar
   solo lo que usa.
4. **La guarda de destino visto para los saltos indirectos**, que es lo que alarga los bloques
   de Katana.

Nada de esto es un problema de diseño ni de las plantillas: **el traductor es exacto y cubre
la mitad del volumen**. Lo que falta es amortizar la entrada.

## Encadenar en el despachador: rinde, y dice dónde está el resto

Lo primero de la lista era encadenar. La forma barata y segura: **el despachador corre el
bloque siguiente él mismo** mientras el corte no corresponda, en vez de volver a `main_loop`
por cada bloque. La condición es exactamente la de `main_loop` y se evalúa donde `main_loop`
la evaluaría, así que la ejecución es la misma — y lo es: los dos guests siguen dando sus
dígitos y sus capturas.

Mismo binario, órdenes rotados, cuatro rondas en DCDoom y tres en Crazy Taxi:

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 53 737 ms | 52 228 (−2,8 %) | **46 130 (−14,15 %)** |
| Crazy Taxi, 180 s | 133 323 ms | 110 794 (−16,9 %) | **136 263 (+2,20 %)** |

Contra el A/B anterior —−13,78 % y +3,94 %— el encadenamiento vale **0,4 puntos en DCDoom y
1,7 en Crazy Taxi**. Rinde, y es poco: lo que quita es el viaje a `main_loop` (buscar la
instrucción, el filtro, la llamada), y eso resulta ser la parte chica.

**Y eso mismo dice dónde está el resto.** El costo por entrada sigue en unos 10 ns —44
ciclos— y lo que queda adentro de la frontera del bloque es, por aritmética sobre 3393
millones de entradas de Crazy Taxi (no medido pieza por pieza):

| pieza | por entrada | sobre la corrida |
| --- | --- | --- |
| la llamada indirecta `b->codigo()` y su `ret` | ~20 ciclos de fallo de predicción | **~12 %** |
| el lee-modifica-escribe del contador | ~6 ciclos serializados | ~3,5 % |
| ocho empujes y ocho sacadas | ~16 operaciones | ~2,5 % |
| la búsqueda en la tabla y el `memcmp` de verificación | | resto |

**El salto indirecto es la pieza grande, y la única forma de quitarlo es el encadenamiento de
verdad**: un `jmp rel32` directo del bloque al sucesor, parcheado cuando el sucesor existe,
con los registros no volátiles establecidos una sola vez por un trampolín en vez de por
bloque. Eso quita de un saque la predicción fallida, los empujes y las sacadas, y deja el
contador y la verificación como lo único por entrada.

Es un cambio de forma —los bloques dejan de ser funciones de C— y por eso no entró acá. Pero
la medición ya no deja dudas de que es el próximo paso, y de que sin él el traductor se queda
donde está: **gana donde el intérprete es caro (la MMU) y pierde donde es barato**.

## Dos costos por entrada que sí se podían quitar

El encadenamiento de verdad —`jmp rel32` directo de bloque a bloque— tiene un problema de
diseño que no se resuelve escribiéndolo: **saltar directo se salta la verificación de
palabras del sucesor**, y en DCDoom la verificación falla **1 234 992 veces por corrida**
(reuso de direcciones virtuales entre procesos de Windows CE). Un enlace que no revalida
ejecutaría código viejo. Enlazar necesita antes una política de invalidación —una generación
de código, o un gancho de escritura que el plan rechaza con razón— y esa decisión merece su
propia medida.

Mientras tanto, dos cosas del camino de entrada que son costo puro y se quitan sin tocar la
semántica:

1. **La verificación se hacía con `memcmp`.** Corre una vez por entrada —434 millones de
   veces en DCDoom, 3393 en Crazy Taxi— sobre 16 bytes de media. La llamada a la de la
   biblioteca, con su despacho por tamaño, cuesta más que comparar a mano.
2. **El muestreo tocaba su tabla de 64 KB en cada salida de bloque.** El mapa de bits son
   8 KB y ya se consultó; un PC ya marcado no tiene nada que ganar muestreándose. Lo único
   que se pierde es una siembra cuando el bit lo puso otro PC que aliasa, y eso sólo retrasa
   un descubrimiento.

Mismo binario, órdenes rotados, cuatro rondas en DCDoom y dos en Crazy Taxi:

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 53 751 ms | 52 098 (−3,1 %) | **45 568 (−15,22 %)** |
| Crazy Taxi, 180 s | 132 631 ms | 110 740 (−16,5 %) | **133 531 (+0,68 %)** |

La serie de las tres tandas, que es lo que se lee:

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| traductor, primera medida | −13,78 % | +3,94 % |
| con el encadenado del despachador | −14,15 % | +2,20 % |
| **con estos dos** | **−15,22 %** | **+0,68 %** |

En Katana el traductor quedó **en el ruido del intérprete** —una de las dos rondas dio −0,05 %
y la otra +1,4 %— desde el +3,9 % del principio. Y DCDoom pasa de 0,651× a **0,768×** en este
binario sin perfil: 1,18× del intérprete, todo con la ejecución idéntica al dígito y las
capturas byte a byte.

Lo que queda es lo mismo de antes y ahora con más margen encima: **la llamada indirecta al
bloque con su `ret`**, que sigue siendo la pieza grande del costo por entrada y que sólo el
encadenamiento de verdad quita. Con 4,3 instrucciones por entrada en Crazy Taxi contra las
219 del bloque escrito a mano, ahí está la diferencia entre empatar y el −16,5 %.

## El encadenamiento de verdad

Lo que faltaba, y lo que hacía falta antes: **una forma sana de saber que un bloque sigue
valiendo sin comparar sus palabras**. Sin eso, un salto directo ejecuta código viejo, y en
DCDoom la verificación falla 1 234 992 veces por corrida.

### La época

Una época global que se mueve con las dos únicas cosas que pueden invalidar un bloque:
**un cambio de mapeo** —las dos invalidaciones de la MMU, que es por donde pasan `LDTLB`, las
escrituras a `PTEH`, a `MMUCR` y a los arreglos por P4— y **una escritura sobre una página que
tiene código traducido**. Un bloque guarda la época con la que se verificó entero; mientras
la global no se mueva, sus palabras son las mismas y su página sigue donde estaba.

El interruptor `jit_vigila_codigo` está en cero salvo con el traductor encendido, así que en
el binario del árbol el gancho de escritura desaparece entero.

Dos agujeros, los dos encontrados por la cuenta y ninguno visible en la captura:

1. **El camino rápido de escritura emitido no pasa por `memwrite()`**, así que escribía sin
   mover la época. Ahora mira el mapa de páginas con código traducido —un byte por página del
   anfitrión, para que sea una comparación— y baja al ayudante si la tiene.
2. **La época no cubre el cambio de mapeo por modo**: un bloque traducido en modo privilegiado
   y reencontrado en modo usuario mapea a otro lado. Costó **61 568 instrucciones de
   divergencia**. La comparación es contra el puntero que devuelve `MMU_FETCH_PUNTERO`, que es
   lo que identifica el mapeo entero —página, ASID y modo—, y la época cubre las escrituras.

Con eso la verificación por entrada pasó del bucle de palabras a **dos comparaciones**.

### El salto

En cada salida con sucesor constante se emite: el corte del bloque periódico primero —si
corresponde cortar hay que salir pase lo que pase—, después la guarda de la época, y recién
entonces el volcado, las sacadas de la pila y un `jmp rel32`.

**Es un tail jump**: en ese punto `rsp` está exactamente como al entrar al bloque, que es lo
que el prólogo del sucesor espera, y el epílogo del sucesor hará el `ret` que le corresponde a
quien llamó. La pila queda balanceada y la información de desenrollado de cada bloque sigue
describiendo su propio marco — no hubo que tocar nada de eso.

Se parchean dos sitios cuando el sucesor existe: el desplazamiento del `cmp`, que pasa a
apuntar al campo `epoca` del sucesor, y el `rel32`. Sin parchear, el `cmp` mira una constante
que vale cero y nunca iguala a la época, así que la guarda falla y el bloque sale como salía.

### El número

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 54 160 ms | 51 991 (−4,0 %) | **45 184 (−16,57 %)** |
| Crazy Taxi, 180 s | 134 335 ms | 110 695 (−17,6 %) | **125 172 (−6,82 %)** |

Las entradas al despacho bajaron de 434,8 a **353,7 millones** en DCDoom y de 3392 a **2464**
en Crazy Taxi, con 4499 y 6217 enlaces atados.

La serie completa de las cuatro tandas:

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| traductor, primera medida | −13,78 % | +3,94 % |
| encadenado del despachador | −14,15 % | +2,20 % |
| verificación sin `memcmp`, muestreo por el mapa | −15,22 % | +0,68 % |
| **época y salto directo** | **−16,57 %** | **−6,82 %** |

**Crazy Taxi por fin va más rápido que el intérprete**, de +3,9 % a −6,8 %. Y DCDoom pasa de
0,646× a **0,775×** en este binario sin perfil: 1,20× del intérprete, con la ejecución
idéntica al dígito y las capturas byte a byte en los dos guests.

### Lo que queda

Sigue faltando la mitad: el bloque escrito a mano de Crazy Taxi hace **219 instrucciones por
entrada** contra las **6,0** del traductor, y por eso rinde 17,6 % donde el traductor rinde
6,8 %. Los enlaces sólo cubren los sucesores **constantes**; los saltos indirectos —el
`JSR`→`RTS` del callback, que es el lazo caliente de ese juego— siguen saliendo a C. La guarda
de destino visto para los indirectos es lo que falta, y ahora tiene toda la maquinaria puesta:
la época ya dice cuándo un bloque vale, y el sitio del salto ya se parchea.

## Los saltos indirectos, y el efecto colateral de buscar una instrucción

El `JSR`→`RTS` del callback es el lazo caliente de Crazy Taxi y no tiene sucesor constante,
así que sus enlaces necesitan una **guarda de destino visto**: el PC calculado contra un
inmediato que el sitio aprende corriendo. El destino no se sabe al traducir; lo aprende el
despachador la primera vez que el bloque sale por ahí —el código emitido le deja el número de
sitio— y entonces parchea. Hasta ese momento el inmediato vale 1, que ningún PC iguala porque
todos son pares.

Tres errores, y los tres dicen algo:

1. **`cmp eax, 1` se codifica con inmediato de 8 bits.** El emisor elige siempre la
   codificación más corta, y eso es exactamente lo que un sitio parcheable no tolera: el
   supuesto `imm32` caía en medio de la instrucción siguiente. El proceso se cayó con
   instrucción privilegiada, que es lo que pasa cuando se escribe encima del código. Ahora hay
   dos formas de **ancho fijo** (`cmp_ri32`, `cmp_rm32`) para los sitios que se parchean.
2. **Los tres sitios del parche tienen que escribirse juntos.** Si el destino nuevo no se
   podía atar y el inmediato ya se había actualizado, la guarda dejaba pasar un PC nuevo hacia
   el bloque viejo: **815 millones de instrucciones de divergencia y una captura distinta**, la
   primera de esta serie que se vio a simple vista.
3. **Buscar una instrucción tiene efecto colateral: `traducir_busqueda()` avanza `URC`.** El
   despachador, al verificar un bloque, busca su primera instrucción; el salto encadenado se
   salta esa búsqueda. Dentro de la misma página no cambia nada —la búsqueda habría acertado
   la página única y no habría avanzado—, pero **un `JSR` se va a otra parte**, y ahí se pierde
   un avance de `URC`, del que depende qué entrada reemplaza el `LDTLB` del guest. Costó 7095
   instrucciones. La regla queda: **sólo se enlaza dentro de la misma página**, medida con la
   máscara que la búsqueda tiene resuelta — sana porque cualquier cambio de mapeo mueve la
   época y desata todos los enlaces.

### El número, y el canje

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 52 696 ms | 50 314 (−4,5 %) | **45 534 (−13,59 %)** |
| Crazy Taxi, 180 s | 141 474 ms | 112 454 (−20,5 %) | **127 174 (−10,11 %)** |

**Los absolutos de esta tanda no se comparan con los de la anterior** —el intérprete solo se
movió un 2,7 % en DCDoom y un 5,3 % en Crazy Taxi entre binarios, que es disposición—, así que
lo que se lee es la relación dentro de cada tanda:

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| salto directo, sin restricción de página | 0,834 | 0,932 |
| **con indirectos y restricción de página** | **0,864** | **0,899** |

Es un canje, y hay que decirlo así: **Crazy Taxi gana 3,3 puntos por los indirectos y DCDoom
pierde 3 por la restricción de página**. La restricción no es opcional —el mecanismo del
`URC` vale igual para los enlaces estáticos, aunque en las corridas medidas no los hiciera
divergir—, así que la versión anterior era más rápida en DCDoom por hacer algo que no se
puede sostener.

Entradas al despacho: Crazy Taxi baja de 2464 a **1906 millones** (7,8 por entrada); DCDoom
sube de 353,7 a 386,2, que es justo lo que la restricción quita.

## La época se movía en cada escritura de SR

`SR.MD` cambia el mapeo, así que mover la época al escribir `SR` era necesario — pero se
movía **siempre**, no cuando `MD` cambiaba de verdad. En un guest sin MMU esa es la única
fuente de movimiento, así que las cadenas se rompían a cada interrupción y el traductor
volvía a entrar y salir por C. Ahora se lleva el último modo visto y se compara.

Las entradas al despacho de DCDoom bajan de 386,2 a **355,7 millones** (8,0 por entrada).

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 54 302 ms | 52 332 (−3,6 %) | **46 281 (−14,77 %)** |
| Crazy Taxi, 180 s | 135 815 ms | 111 723 (−17,7 %) | **126 123 (−7,13 %)** |

**Y aquí hay que decir algo incómodo sobre el método.** Entre esta tanda y la anterior el
**intérprete solo** se movió +3,0 % en DCDoom y −4,0 % en Crazy Taxi. Eso es disposición del
binario, no emulación. Con las dos puntas moviéndose así, **ni los absolutos ni la relación
entre tandas se pueden leer**: lo único comparable es lo que pasa dentro de una misma tanda,
y lo único atribuible a este cambio es la cuenta de entradas, que no depende del reloj.

Así que la lectura honesta es: **el cambio baja las entradas un 8 % en DCDoom, mantiene la
ejecución idéntica al dígito en los dos guests, y la mejora de tiempo que produce queda por
debajo del ruido de disposición entre binarios**. Para medirla habría que reentrenar el PGO
y correr las dos versiones alternadas en una sola tanda, que es la receta que este árbol ya
tiene escrita.

## Dónde está el traductor, y qué falta

Con todo lo de esta serie, y midiendo dentro de una tanda: **DCDoom −14,8 % y Crazy Taxi
−7,1 %**, con el 52,6 % y el 67 % del volumen traducido, la ejecución idéntica al dígito y
las capturas byte a byte.

El bloque escrito a mano sigue rindiendo más en Crazy Taxi (−17,7 %) porque hace **219
instrucciones por entrada** contra las **7,8** del traductor. La cuenta de entradas es la que
manda, y lo que la limita ahora ya no son los enlaces —estáticos e indirectos están— sino:

- **`JIT_MAX_ENLACES` = 12 por bloque**, que con bloques de 7,8 instrucciones y una salida
  por rama se agota;
- **la restricción de página** para los indirectos con MMU;
- y sobre todo **el costo por entrada que queda**: la llamada indirecta al bloque con su
  `ret`, los ocho empujes y sacadas, y el lee-modifica-escribe del contador. Eso solo lo quita
  un trampolín — entrar al mundo emitido una vez y que el despacho viva ahí adentro —, que es
  un cambio de forma y no una mejora incremental.

## El trampolín: los bloques dejan de ser funciones de C

Cada bloque empujaba ocho registros, armaba su marco, cargaba el contexto y al salir lo
deshacía todo — y **un salto encadenado pagaba las ocho sacadas del que salía y los ocho
empujes del que entraba**, más el `mov rbx, imm64` y el volcado del contador. Con 1900
millones de entradas eso era el costo por entrada que la medición venía señalando.

Ahora hay un trampolín: se entra al mundo emitido **una vez**, se arma el marco una vez, y
los bloques son tramos de código que se saltan entre sí. Lo que un bloque hace al entrar es
cargar los registros del guest que mapea; lo que hace al salir es volcarlos. El contexto
(`rbx`), los ciclos (`rbp`) y el contador (`rsi`) viven en registros durante toda la cadena, y
el contador se vuelca en la salida común — y en cada sincronización previa a un acceso, que
es donde hace falta para el `longjmp`.

**La información de desenrollado pasa a ser una sola**, la del trampolín, cubriendo todo el
arena: los bloques no tocan `rsp`, así que para el desenrollador cualquier PC de ahí adentro
está «después del prólogo» del trampolín y el marco que hay que deshacer es el suyo. Eso es lo
que mantiene sano el `longjmp` de una falta — 850 557 por corrida en DCDoom, y sigue exacto.

El código emitido bajó de 16,15 a **15,08 MB** en DCDoom y de 8,62 a **7,42 MB** en Crazy
Taxi: eso es el prólogo y el epílogo que se fueron.

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 54 309 ms | 52 377 (−3,6 %) | **45 950 (−15,39 %)** |
| Crazy Taxi, 180 s | 132 814 ms | 110 346 (−16,9 %) | **126 142 (−5,02 %)** |

### Y otra vez el mismo problema de método

Entre esta tanda y la anterior el **intérprete solo** se movió −0,0 % en DCDoom y **−2,2 % en
Crazy Taxi**. Con la punta de referencia moviéndose así, la relación entre tandas no se puede
leer: DCDoom marca 0,846 contra 0,852 y Crazy Taxi 0,950 contra 0,930, y esas diferencias son
del mismo tamaño que el ruido de disposición.

Lo que sí se puede afirmar del trampolín, sin depender del reloj:

- **la ejecución sigue idéntica al dígito** en los dos guests, capturas incluidas, con una
  sola entrada de desenrollado y el camino de falta intacto;
- **el código emitido bajó un 7 % y un 14 %**, que es exactamente el prólogo y el epílogo por
  bloque;
- y **un salto encadenado dejó de costar dieciséis operaciones de pila y un lee-modifica-
  escribe**.

Para medir su efecto en tiempo hace falta lo que este árbol ya tiene escrito y esta serie no
usó: **reentrenar el PGO y alternar las dos versiones dentro de una sola tanda**. Con un
cambio de forma como éste las dos versiones no conviven en un binario, así que habría que
dejar las dos formas de emisión detrás de un interruptor — que es trabajo, y es el que
corresponde antes de seguir apretando el costo por entrada.

## El modo entra en la clave, y la época deja de churnear

La época es un contador, así que basta reportarla para saber cuántas veces se desataron
todos los enlaces. **DCDoom la movía 9 332 831 veces en 35 segundos emulados** — una cada 38
entradas al despacho — y Crazy Taxi 5. Separadas por fuente:

| fuente | DCDoom | qué es |
| --- | --- | --- |
| escritura sobre página con código | **0** | el temor del aliasing del mapa era infundado |
| mapeo | 1 134 971 | `LDTLB` y escrituras a `PTEH`, que es la cifra documentada |
| **modo** | **8 197 860** | Windows CE entrando y saliendo de privilegiado |

O sea: el 88 % de los movimientos eran cambios de modo, y cada uno invalidaba **todos** los
enlaces, incluidos los de bloques que el cambio no afectaba.

El arreglo es que el modo **no mueva la época sino que entre en la clave**: cada bloque guarda
`(epoca << 1) | MD` y el salto compara contra la global. Así un cambio de modo sólo invalida
los bloques verificados en el otro modo — que es lo correcto — y los de éste siguen valiendo.
Sigue siendo **una comparación** en el salto.

Movimientos de época en DCDoom: **9 332 831 → 1 134 971**. Entradas al despacho: 355,7 →
**339,3 millones** (8,4 por entrada).

| banco | intérprete | 2 bloques a mano | traductor |
| --- | --- | --- | --- |
| DCDoom, 35 s | 51 137 ms | 49 055 (−4,1 %) | **43 097 (−15,72 %)** |
| Crazy Taxi, 180 s | 134 648 ms | 109 037 (−19,0 %) | **122 026 (−9,37 %)** |

## Y aquí el método se quedó sin resolución

Es la tercera tanda seguida en que **el intérprete solo se mueve más que el cambio que se
quiere medir**: entre ésta y la anterior se movió **−5,8 % en DCDoom** y +1,4 % en Crazy Taxi,
sin que su código cambiara una línea. Eso es disposición del binario, y es más grande que
todo lo que se viene midiendo.

Las relaciones de las últimas cuatro tandas, para que se vea:

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| indirectos + restricción de página | 0,864 | 0,899 |
| época sólo cuando MD cambia | 0,852 | 0,930 |
| trampolín | 0,846 | 0,950 |
| modo en la clave | 0,843 | 0,906 |

Crazy Taxi va de 0,899 a 0,950 y vuelve a 0,906 **sin que nada de lo que se toca la afecte**
(mueve la época 4 veces en toda la corrida). Esa columna no está midiendo el traductor: está
midiendo dónde cayó cada función en cada enlace.

**Lo que se puede afirmar de este cambio sin depender del reloj**, y es lo único que
corresponde afirmar: la ejecución sigue idéntica al dígito en los dos guests con las capturas
byte a byte, los movimientos de época bajan un 88 %, y las entradas al despacho un 4,6 %.

## Lo que hay que hacer antes de seguir apretando

**Reentrenar el PGO para el binario del JIT y dejar las dos formas que se quieran comparar
detrás de un interruptor de tiempo de ejecución.** Es exactamente la receta que este árbol ya
tiene escrita para el intérprete —«en este tree el layout mueve un banco tanto como una
optimización»— y esta serie la fue estirando sin aplicarla porque cada cambio daba puntos
enteros. Ya no los da: lo que queda es del tamaño del ruido que el método actual no separa.

Y con el perfil puesto habrá además un número que hoy no existe: **cuánto va DCDoom de
verdad**. Todas las cifras de esta serie salen de un binario sin perfil, que según el propio
árbol corre ~10 % por debajo del normal.

### El techo que queda, medido

Aun así el censo dice dónde está el resto, y no es un misterio: el bloque escrito a mano de
Crazy Taxi hace **219 instrucciones por entrada** contra las **7,8** del traductor, y el de
DCDoom **228** contra **8,4**. Con 5014 enlaces atados sobre 9290 bloques, **la mitad de las
salidas no tiene enlace** — y en DCDoom eso es la restricción de página, que es obligatoria
mientras el salto no haga la búsqueda de instrucción que avanza `URC`. Emitirla en línea es
el trabajo que sigue, y es el que devolvería las cadenas largas al guest que más las necesita.

## El perfil, por fin: los números con PGO

El binario del recompilador lleva ahora **su propio `.pgd`** (`build-pgo/dcemu-jit.pgd`,
elegido por CMake cuando `DCEMU_JIT=ON`): es otro programa —los ganchos de época en
`memwrite` y `UpdateSR` son código real, `jit.c` entero está dentro— y fundir sus corridas
sobre el `.pgd` del árbol degradaría el binario por omisión sin que nadie lo pida.

Su banco de entrenamiento también es otro (`pgo.ps1 -Jit`): **cada guest corre dos veces, una
por forma** —intérprete y traductor, con el mismo peso—, porque ese binario existe para el
A/B entre las formas y entrenar sólo una dejaría a la otra desordenada: el A/B mediría la
disposición, que es exactamente lo que el PGO viene a eliminar. Tras cada corrida del
traductor el guion exige el resumen `jit:` en stderr — un JIT que no se enganche dejaría un
perfil que describe al intérprete dos veces, y nadie lo notaría.

El guion se cobró dos bugs propios antes de entrenar nada, y los dos son la forma de fallo
recurrente de este árbol llegando a las herramientas:

- **Asignar `$formas` desde una expresión `if` pasa por la tubería de PowerShell, que
  desenvuelve `@($null)` a `$null` pelado — y `foreach` sobre `$null` itera cero veces.** Cero
  corridas, cero fusiones, y el `/clear` borró el perfil anterior igual, con el guion saliendo
  con 0 y diciendo «perfil listo». Ahora el centinela es `""` y hay una guarda que aborta con
  el banco vacío antes de tocar el `.pgd`.
- **Los `.pgc` heredan el nombre base del `.pgd`, no el del ejecutable**: con `dcemu-jit.pgd`
  las corridas dejan `dcemu-jit!N.pgc`, que el filtro `dcemu!*` perdía. La guarda de «la
  corrida no dejó perfil» lo atrapó en la primera corrida.

### Los números

La tanda: cuatro modos rotados en cuadrado latino —el árbol (su `.pgd`, sin JIT compilado) y
las tres formas del binario del JIT—, con calentamiento descartado por ejecutable y los dos
binarios hasheados. Promedios; la dispersión del traductor en DCDoom es **0,33 %** entre
rondas, así que el método volvió a tener resolución.

| banco | árbol | intérprete (bin. JIT) | 2 bloques a mano | traductor |
| --- | --- | --- | --- | --- |
| DCDoom, 35 s | 39 783 ms | 41 227 (+3,6 %) | 40 819 | **36 213 (−12,2 % / −9,0 %)** |
| Crazy Taxi, 180 s | 103 313 ms | 107 838 (+4,4 %) | 91 642 | **100 858 (−6,5 % / −2,4 %)** |

Los dos porcentajes del traductor son contra el intérprete de su binario y contra el árbol;
los del intérprete, contra el árbol.

### Las tres lecturas

**DCDoom corre a 0,966× tiempo real.** La serie entera lo tenía en 0,775×: el binario sin
perfil escondía un 16 % del propio traductor. Este es el número que no existía.

**La ganancia real contra el árbol es −9,0 % y −2,4 %**, no el −15,7 %/−9,4 % que la serie
venía citando: aquello comparaba contra un intérprete sin perfil dentro del mismo binario.
Parte de la diferencia es el punto siguiente.

**Llevar el JIT compilado cuesta +3,6 % / +4,4 % al intérprete.** Es la paridad entre
binarios que el método anterior no podía separar del ruido: los ganchos de época con
`jit_vigila_codigo` en cero, la comprobación de despacho en `main_loop()`, y la disposición
con `jit.c` adentro. Queda como número propio porque es el precio de hacer del binario del
JIT el binario por omisión, si algún día se quiere.

Las cuentas de DCDoom salen **idénticas al dígito** a las de la serie sin perfil
(2 857 129 096 instrucciones en 339 340 638 entradas, 1 234 992 rechazos): el PGO no tocó la
semántica, sólo dónde cayó cada función. Y de aquí en adelante cada A/B de este plan corre
sobre binarios con perfil, reentrenando tras cada cambio de emisión — que con `pgo.ps1 -Jit`
es un comando.

## El puente entre páginas

La restricción de página existía porque el salto encadenado se saltea la búsqueda de
instrucción del despachador, y `traducir_busqueda()` avanza `URC`. La respuesta ya no es
restringir sino **reproducir la búsqueda**: cada salida enlazable de un bloque con MMU lleva
un **talón** — volcar el contador, llamar a `jit_busqueda_puente()` y saltar al cuerpo del
sucesor. El ayudante es una línea: `MMU_FETCH_PUNTERO(PC)`, **la misma búsqueda que hace el
despachador**, con su avance de `URC`, su repoblado de la caché y su falta de TLB. Se llama
con el contexto ya sincronizado —el PC del contexto ya es el destino en toda salida
enlazable, los registros y los ciclos los volcó la salida, el contador lo vuelca el talón—,
que es exactamente el estado con el que el intérprete llega a la cabecera de su bucle: una
falta aquí sale por el `longjmp` con el mismo estado que allí. Exacto por construcción, no
por argumento: en la corrida de verificación **hasta el conteo de fallos de búsqueda salió
byte-idéntico** (94 191 286 en ambos modos).

El enlace **directo** —sin talón— queda para la única medida que no depende del mapeo
vigente: **la misma ventana de 1 KB** (`JIT_LIMITE_PAG`, la página más chica del SH-4). Dos
PC en la misma ventana están en la misma página bajo cualquier tamaño, para siempre. Eso
cierra de paso dos agujeros latentes del criterio anterior, que medía con
`mmu_fetch_mascara` en el momento de parchear: en la dirección «los enlaces ajenos hacia el
bloque nuevo» esa máscara era la del bloque nuevo y no la del que salta, y un parche hecho
bajo un mapeo **revivía tras el cambio de época sin re-evaluarse** — la guarda del salto
compara la clave, no el criterio con el que se parcheó. WinCE mapea todo en 4 KB, así que
ninguno llegó a morder; los cierra la regla nueva, no una corrección aparte. Sin MMU no hay
búsqueda que reproducir: directo siempre, cero talones, y Crazy Taxi sale con las cuentas
**idénticas al bit** — el control limpio.

Todos los sitios de un enlace se escriben juntos, el talón incluido — que apunta siempre al
cuerpo del sucesor vigente aunque el parche haya salido directo, para que un reparcheo no
deje un talón rancio. `DCEMU_JIT_SIN_PUENTES=1` es la palanca de aislamiento.

### El número, y la lectura honesta

Enlaces en DCDoom: 5014 → **8049, 4136 por puente**. Entradas al despacho: 339,3 → **311,2
millones** (−8,3 %), 9,2 instrucciones por entrada. Ejecución idéntica al dígito en los dos
guests, capturas incluidas (`198B396F…`, la línea base documentada).

| banco | intérprete | traductor | tanda anterior |
| --- | --- | --- | --- |
| DCDoom, 35 s | 40 815 ms | **35 965 (−11,9 %)** | 36 213 |
| Crazy Taxi, 180 s | 109 627 ms | 101 355 (−7,5 %) | 100 858 |

El tiempo que devuelve es chico — **~0,7 % en DCDoom**, al borde de la resolución; Crazy
Taxi se mueve dentro del ruido de reentrenamiento en las dos puntas. La aritmética cierra:
28 millones de cruces ahorran el despacho pero pagan el talón, y la diferencia son décimas.
El puente vale por lo otro: **elimina la restricción de página como techo estructural**,
cierra los dos agujeros, y deja las cadenas libres para lo que sí es grande — que ahora es
cobertura.

### Lo que la verificación encontró de paso: los topes

La corrida de exactitud dejó a la vista dónde está el resto. **Crazy Taxi satura la tabla
de bloques**: 16 384 traducidos (el tope justo), **5269 candidatos calientes sin lugar**, o
sea cobertura esperando capacidad — y su 33 % no traducido son 7,4 mil millones de
instrucciones interpretadas. DCDoom tiene el arena al 98 % (15,68 de 16 MB, 23 emisiones
fallidas). Con el 47,4 % de DCDoom y el 33 % de Crazy Taxi todavía interpretados, Amdahl
dice que la palanca es esa, no el costo por entrada.

## La capacidad, y lo que la saturación escondía

Bloques al doble (32 768, el máximo que el `short` de la tabla direcciona), arena a 32 MB, y
la tabla hash a **131 072 ranuras** — no al doble sino al cuádruple, porque los 5269 «sin
lugar» ocurrieron a carga del 50 % y duplicar ambos habría mantenido la misma carga y la
misma cola del sondeo lineal. El corrimiento del hash pasa a derivarse de `JIT_HASH_BITS`:
estaba escrito `32 - 15` como literal, y con la tabla más grande media tabla habría quedado
inalcanzable sin que nada lo reporte.

**Lo que la saturación escondía era peor que falta de espacio: retraducción compulsiva.** Un
bloque que el hash no podía indexar tampoco aparecía en el control de duplicados —es la
misma búsqueda—, así que se retraducía en cada muestreo. Con la tabla holgada, Crazy Taxi
pasa de 16 384 bloques (el tope, con duplicados) a **11 379 reales con 0 sin lugar** y su
arena baja de 12,2 a **8,0 MB**; DCDoom de 9290 a 9200. Los topes ahora tienen aire real:
nada volvió a acercarse.

De paso, el contador nuevo de salidas con los enlaces agotados dice que `JIT_MAX_ENLACES`
= 12 **casi no muerde** (60 sitios en DCDoom, 266 en CT): medido y descartado como palanca.

| banco | intérprete | traductor | tanda anterior |
| --- | --- | --- | --- |
| DCDoom, 35 s | 40 889 ms | **36 008 (−11,9 %)** | 35 965 |
| Crazy Taxi, 180 s | 109 052 ms | **99 863 (−8,4 %)** | 101 355 |

DCDoom no se mueve (sus duplicados eran 91); **Crazy Taxi gana ~1,5 % y baja por primera
vez de 100 segundos**, que es la retraducción que ya no paga más el tercio de arena que ya
no toca. Ejecución idéntica al dígito en los dos guests, capturas incluidas.

### La cuenta que reordena lo que sigue

Bloques ejecutados sobre entradas al despacho: **1,16 en DCDoom y 1,07 en Crazy Taxi**. Las
cadenas casi no encadenan — toda la maquinaria de época, enlaces y puentes carga hoy con el
~14 % de las transiciones. El sitio que rompe la cadena es el salto indirecto polimórfico
—el `RTS` que vuelve a muchos llamadores, cuya guarda de destino aprendido falla y sale a C
en cada vuelta—, así que la palanca del costo por entrada no es abaratar el cruce enlazado:
es **despachar el indirecto sin salir del mundo emitido**. (Se probó esa misma noche; el
resultado, dos secciones más abajo, no es el esperado y vale más que un éxito.)

## Las plantillas que pidió el censo: el push/pop de PR deja de cortar

El censo de cortes de Crazy Taxi encabezaba con `STS.L PR,@-R15` y `LDS.L @R15+,PR`
(1487 + 932 + 166 sitios): **el prólogo y el epílogo de toda función SH-4 cortaban el
bloque**, y por eso los bloques promediaban 7,3 instrucciones. Ocho plantillas nuevas, al
final de la tabla para que la bisección por prefijo de `DCEMU_JIT_PLANTILLAS` siga
valiendo: las dos de PR, `LDS.L @Rm+,MACL`, `STS MACL`, `MOVT`, `MUL.L`, `CMP/PL` y
`ROTCL`. Tres detalles con historia:

- **Los ciclos, copiados de cada manejador** — y `ldsl135` (`LDS.L @Rm+,PR`) no suma
  ninguno. No es un olvido de la plantilla: es la rareza del manejador, protegida por la
  cuenta exacta.
- **El orden de compromiso ante una falta es el de `pl_movl12`**, no el del manejador:
  `stsl168` decrementa `R(n)` antes de escribir y la instantánea del intérprete repone; la
  plantilla calcula en RCX y compromete después de que el acceso volvió, que es el contrato
  del mundo emitido.
- `MUL.L` pidió el `imul` de dos operandos (0F AF) que el emisor no tenía; entró con sus
  casos byte a byte en la suite. `ROTCL` mete el T al acarreo con un `SHR` y deja que `RCL`
  haga la rotación exacta del chip.

Cobertura: DCDoom 2,86 → **3,15 mil millones** (58,0 %) con bloques de 7,9 → **9,3**
instrucciones y los rechazos por verificación de 1,23 M → **414 mil**; Crazy Taxi
14,89 → **15,57 mil millones** (69,9 %), bloques de 7,3 → 8,2. Exactitud al dígito en los
dos guests, capturas incluidas.

| banco | intérprete | traductor | tanda anterior |
| --- | --- | --- | --- |
| DCDoom, 35 s | 41 049 ms | **36 000 (−12,3 %)** | 36 008 |
| Crazy Taxi, 180 s | 108 384 ms | **98 810 (−8,8 %)** | 99 863 |

DCDoom queda igual — sus entradas subieron un 21 % (la cobertura nueva es fragmentada,
bloques chicos donde el costo de entrada come el ahorro) y el neto es cero —; **Crazy Taxi
gana otro 1,1 %** y queda en 98,8 s. La lección de la neutralidad de DCDoom queda dicha:
**cobertura sin cadenas es margen cero cuando los bloques son chicos**.

## El redespacho en el arena: probado, medido dos veces, y revertido

La cuenta de 1,07 bloques por entrada pedía despachar el indirecto sin salir a C. Se probó
en sus dos formas, con la exactitud al dígito verificada en ambas — y **las dos pierden**:

1. **La forma general**: el epílogo común llama a un ayudante C que replica el corazón de
   `jit_despachar()` —corte de grano primero, filtro, tabla, verificación con su búsqueda
   de instrucción— y salta al bloque si existe. **+2,2 % DCDoom, +4,6 % Crazy Taxi.** El
   ayudante acertaba 77 millones de veces… y fallaba **1870 millones**: la mayoría de las
   salidas frías van a un PC sin bloque (la cobertura es 58-70 %), y la llamada fallida en
   cada una costó más que lo que las exitosas ahorraban.
2. **La forma quirúrgica**: la llamada sólo en el camino sin enlace del salto indirecto,
   donde viven prácticamente todos los aciertos (76,86 de los 76,9 millones — el
   diagnóstico era correcto). **+0,5 % y +0,6 %.** Aun sin las llamadas fallidas, cada
   acierto paga volcado, llamada C y un salto indirecto compartido, y eso no bate al viaje
   a C que reemplaza — que tras todo lo de esta serie ya es demasiado barato.

El código queda revertido y la lección escrita: **el viaje al despachador no es hoy el
costo dominante; un redespacho sólo puede pagar si es enteramente emitido** — el hash y la
verificación en línea, sin llamada — y eso es otra clase de trabajo, con la guarda de
época como prerequisito ya puesto. Los parches quedan en la bitácora de la sesión por si
ese trabajo se hace.

## Dónde queda el traductor al cierre de esta fase

| | DCDoom, 35 s | Crazy Taxi, 180 s |
| --- | --- | --- |
| el árbol (intérprete, su PGO) | 39 783 ms | 103 313 ms |
| traductor (binario del JIT, su PGO) | **36 000 ms** | **98 810 ms** |
| contra el árbol | **−9,5 %** | **−4,4 %** |
| contra el intérprete de su binario | −12,3 % | −8,8 % |
| tiempo real | **0,972×** | 0,631× |

**DCDoom corre a 0,972× tiempo real** — arrancó la serie en 0,646× sin perfil. La serie de
la noche sobre el traductor con perfil: 36 213 → 35 965 (puente) → 36 008 (capacidad) →
**36 000** (plantillas); Crazy Taxi 100 858 → 101 355 → 99 863 → **98 810**. Y el método
quedó de pie: dispersiones de 0,14-0,42 % entre rondas, la exactitud al dígito en cada
paso, y el ciclo entero —`pgo.ps1 -Jit`, `ciclo-jit.ps1`, la tanda— es un comando por
etapa.

Lo que el cierre deja señalado, por orden de palanca: **cobertura con cadenas** (el 42 % y
30 % interpretado sigue siendo Amdahl, pero la neutralidad de DCDoom dice que las
plantillas nuevas tienen que venir con bloques que encadenen, no sueltas), el despacho
enteramente emitido, y los registros persistentes a través del enlace (el mapa de slots
igual entre bloques enlazados ahorraría el volcado y la recarga por cruce).

## El segundo lote del censo, y DCDoom cruza el tiempo real

El censo de DCDoom —que nunca se había mirado; cada tanda lo pisaba con el de CT— dijo
tres cosas: **`SUB Rm,Rn` no tenía fila** (47 sitios de un ALU trivial), **`MOV.W` corta en
los dos guests** (64+66 sitios la forma `@Rm,Rn` más la literal por PC — un motor de 16
bits como Doom la pide a gritos), y la familia `MOV.L Rm,@(d,Rn)` que el censo de CT ya
había pedido. Ocho plantillas más (58 en total): las cinco de la segunda lista de CT
(`STS.L MACL,@-Rn`, `MOV.L Rm,@(d,Rn)`, `MOV.B R0,@(d,Rn)`, `OR`, `CMP/PZ`), el `SUB`, y
los dos `MOV.W` — que trajeron **el camino de lectura de 16 bits entero**: ayudantes
`jit_leer16s`/`_fis`, `gen_leer16s` como espejo de `gen_leer8s` con máscara de alineación 1,
y el `movsx` de palabra indexado que el emisor no tenía (0F BF, con su caso byte a byte).

Y de paso el arena a 64 MB: los bloques largos comen más, DCDoom dejó el de 32 al 98,8 %
con **1314 emisiones fallidas** — bloques ya traducidos que no cupieron. Con aire: 168.

**Este es el perfil que la lección del lote 1 pedía** — cobertura que alarga en vez de
fragmentar:

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| cobertura | 58,0 → **70,8 %** | 69,9 → **71,6 %** |
| instrucciones por bloque | 9,3 → **15,6** | 8,2 → **11,9** |
| entradas al despacho | 375,8 → **362,4 M** | 1944 → **1807 M** |

| banco | intérprete | traductor | tanda anterior |
| --- | --- | --- | --- |
| DCDoom, 35 s | 41 184 ms | **34 225 (−16,9 %)** | 36 000 |
| Crazy Taxi, 180 s | 109 632 ms | **97 253 (−11,3 %)** | 98 810 |

**DCDoom queda a 1,023× tiempo real** — 35 segundos emulados en 34,2 reales, con la MMU
encendida y la ejecución idéntica al dígito. La serie del recompilador lo tomó en 0,646×.
Contra el árbol: **−14,0 %**; Crazy Taxi **−5,9 %**. Exactitud verificada en ambos con las
capturas byte a byte, y 23/23 en la suite del emisor.

Los cortadores que quedan arriba en DCDoom son los complejos de verdad —`DIV1` (72
sitios), `MAC.L` (48), `SHAD` (24)— y el `0x0000` de las zonas de datos (457, que corta
bien). El siguiente lote ya no es mecánico.

## El tercer lote: la división por el manejador real, y 1,044×

Para las instrucciones raras y complejas, emitir la semántica a mano no paga. **La
plantilla que llama al manejador real** sincroniza (el `accede=1` del conductor ya lo
hace: registros volcados, PC puesto, el intento contado), pasa la palabra en `ecx`, llama
al manejador del intérprete —que ES la semántica, igual que los ayudantes de memoria lo
son de `mem.h`—, recarga el reloj y los slots, y emite el corte que el conductor no
emitirá (la fila lleva ciclos 0 porque los suma el manejador). El bloque sigue de largo
en vez de cortarse.

**La lista blanca es estricta y el motivo es la falta**: sin instantánea, el contrato del
mundo emitido es que una falta deje el contexto pre-instrucción, así que entran solo
manejadores que no acceden a memoria (no pueden fallar), no tocan el PC más allá del +2,
ni SR.MD/RB, ni FPSCR. Entraron `DIV1`, `DIV0S`, `DIV0U` y `SHAD`; **`MAC.L` queda afuera
con el motivo escrito** — lee `@Rn+`, incrementa, y recién entonces lee `@Rm+`: la segunda
falta dejaría `R(n)` avanzado. Además `BRAF`/`BSRF` como saltos dinámicos propios
(destino `R(n) + PC + 4` capturado antes de la ranura), y el `call_r` que el emisor no
tenía, con sus casos byte a byte.

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| cobertura | 70,8 → **79,0 %** | 71,6 → 72,1 % |
| instrucciones por bloque | 15,6 → **16,8** | 11,9 → 12,7 |
| entradas al despacho | 362,4 → **271,8 M (−25 %)** | 1807 → 1739 M |

DCDoom: **33 517 ms (−2,1 % más), 1,044× tiempo real**, con dispersión de 0,6 % entre
rondas. **La columna de Crazy Taxi de esta tanda no se puede leer y hay que decirlo así**:
sus dos corridas gemelas —cuentas idénticas al bit— difieren 2,5 % en tiempo, la firma de
actividad del anfitrión (la tanda corrió de mañana, con la máquina ya en uso; XInput se
lee global y el resto del anfitrión compite). Su re-medición queda para una ventana
tranquila; la exactitud está verificada y las cuentas (entradas −3,8 %) son atribuibles.

Con esto la fase queda: **DCDoom de 0,646× a 1,044×** a lo largo de la serie del
recompilador, cobertura 79 %/72 %, y los cortadores restantes son `MAC.L` (que pide
resolver la falta a mitad de instrucción — una instantánea local o leer las dos posiciones
antes de mutar), los `MOV.W`/`MOV.B` de escritura que el censo aún liste, y el despacho
enteramente emitido como la idea grande pendiente.

## El cuarto lote: MAC.L reordenado, y 1,062×

**`MAC.L` entró reordenando su manejador**, que es la salida que el pendiente pedía: las
dos lecturas van ahora antes de mutar nada (con `n == m`, la segunda dirección es la
palabra siguiente, como si el incremento ya hubiera pasado), así que una falta en
cualquiera deja el contexto pre-instrucción — el contrato del mundo emitido, y de paso la
reejecución del intérprete queda exacta sin depender de la instantánea. Las direcciones y
su orden son los de siempre: URC y los watchpoints ven lo mismo. **Verificado contra
SingleStepTests/sh4** (500 casos aleatorios por codificación, exit 0, con las
discrepancias documentadas de siempre contadas aparte) — y la primera pasada de la cadena
**no corrió la suite y nadie lo dijo**: PowerShell leyó `build\Debug\dcemu_sh4json.exe`
como sintaxis de módulo y el `$LASTEXITCODE` rancio de ctest dejó pasar el guardia. La
forma de fallo recurrente del árbol, esta vez en la cadena de verificación; el binario
además no vive en `build\Debug` sino en `build\tests\Debug`.

Con él: `SHLD` a la lista blanca del manejador real, `OR #imm,R0` (5 ciclos — del
manejador), `MOVA` (su resultado es constante del bloque: un `mov` inmediato), y
`MOV.W Rm,@(R0,Rn)` — que trajo **el camino de escritura de 16 bits** entero
(`jit_escribir16`/`_fis`, `gen_escribir` con máscara de alineación 1 y el `mov` de
palabra indexado del emisor, prefijo 66 antes del REX, con su caso byte a byte).

| | DCDoom | Crazy Taxi |
| --- | --- | --- |
| cobertura | 79,0 → **79,9 %** | 72,1 → 72,4 % |
| instrucciones por entrada | 15,8 → **18,2** | 9,2 |
| entradas al despacho | 271,8 → **238,8 M** | 1747 M |

DCDoom: **32 957 ms — 1,062× tiempo real**, −1,7 % más. La serie completa del
recompilador: **de 0,646× a 1,062×**.

## La frontera FPU: la sonda que decidió el diseño antes de escribirlo

El censo ya muestra FPU (`FSUB` en DCDoom), y la pregunta de diseño era si `FPSCR.PR/SZ`
podían entrar a la clave de validez como entró `SR.MD`. La sonda de transiciones
(`JIT_FPSCR_SONDA`, en `UpdateFPSCR()`, que es el único punto por donde pasa toda
escritura de FPSCR) contestó antes de escribir una plantilla:

- **DCDoom: 0 transiciones.** WinCE no toca PR/SZ/Enable jamás.
- **Crazy Taxi: 13 220 236 en 60 segundos emulados** — 220 mil por segundo. Katana
  conmuta SZ alrededor de cada carga de matriz (los `fmov` apareados).

O sea: **PR/SZ no pueden entrar a la clave** — churnearían los enlaces peor que el
`SR.MD` de la primera noche (13,2 M contra 8,2 M), y a diferencia del modo, aquí el
*código* traducido depende del bit (las cuatro `oplist` reparten manejadores distintos
por la misma palabra). El diseño que los números permiten:

1. **Modo por bloque** (`b->fpu` = PR/SZ/«algún Enable» al traducir, −1 si el bloque no
   tiene filas FPU), chequeado por el despachador como `b->mmu`.
2. **Sin enlaces hacia bloques FPU**: entran siempre por el despachador, que chequea. Lo
   que lo hace viable es que **cada sitio `fmov` corre siempre bajo el mismo modo** — el
   flip encierra la secuencia (`SZ=1; fmovs; SZ=0`) — y las escrituras de FPSCR no tienen
   plantilla, así que ningún bloque cruza un cambio de modo y un bloque por PC alcanza.
3. **El bit FR sale gratis**: `FR(x)` va por el puntero `FR_BANK` del contexto, así que el
   acceso emitido por el puntero vivo sobrevive al intercambio de bancos sin guarda.
4. Los `FMOV` son movimientos enteros (sin SSE): primero los sencillos de `sz0`, después
   los pares de `sz1` que son los que Crazy Taxi martilla. La aritmética
   (`FADD`/`FSUB`/…) puede ir por el manejador real con Enables=0 garantizado por
   `b->fpu` — con algún Enable puesto el manejador puede entrar a la excepción de FPU a
   mitad de bloque, y eso no es una falta sino una redirección, así que esos bloques no
   corren traducidos.

Queda como el trabajo siguiente, con la sonda ya comiteada y contando en el resumen.

### El primer intento (v1), revertido con su diagnóstico

El lote v1 se implementó entero —los siete `FMOV` de `sz0` emitidos por el puntero de
banco, la aritmética por el manejador real, `b->fpu` con su compuerta en el despachador y
el rechazo de enlaces— y **divergió** (DCDoom: 4854 M de instrucciones contra 5433 M, con
la captura distinta). La bisección con `DCEMU_JIT_PLANTILLAS` encontró **dos** cosas:

1. **Una trampa del extractor de ciclos, cazada y corregida**: `fmov172` no suma ciclos
   (clase `mov3`), pero un extractor que busca `cycles +=` por cercanía al `OPCODE(...)`
   le robó el `+= 2` del manejador siguiente. Con la fila en 0, N=78 quedó exacto. La
   regla que deja: **los ciclos se copian leyendo el cuerpo entero del manejador, nunca
   por búsqueda de cercanía** — un cuerpo corto sin línea de ciclos roba la del vecino.
2. **Una divergencia estructural que sobrevivió a seis hipótesis inspeccionadas**:
   `fmovs173` solo (N=79) desvía 633 M de instrucciones, con el total erróneo estable
   (4 800 100 807) — y **emitirlo por el manejador real diverge idéntico**, así que la
   emisión en línea nunca fue el bug: la mera existencia de la fila cambia la ejecución.
   Descartados por inspección: el offset del banco (la estructura lo confirma en 0), el
   contrato de `gen_leer32` (entrega en RAX), la ranura (la regla del corte cubre toda
   fila `accede`), el orden filas↔manejadores, los ciclos de ambas variantes, y la
   compuerta `b->fpu` (0==0 pasa siempre en DCDoom). Los contadores del guest divergente
   (mapeos 189 mil contra 1,13 M; modos 2,9 M contra 8,2 M) dicen que corre un camino
   genuinamente distinto y más ocioso.

**La herramienta que faltó se construyó y funcionó a la primera**: `DCEMU_CP_MS=N` emite
un punto de control por milisegundo emulado —PC, registros, MACL, FR0/FR1— **desde el
bloque periódico y sin encender la traza** (que apaga el JIT; por eso `DCEMU_TRAZA_EN_MS`
no podía ver esto). El bloque periódico corre en las mismas fronteras de ciclo en los dos
modos y con el contexto volcado, así que dos corridas exactas dan puntos idénticos y el
primero distinto acota la bifurcación a un milisegundo. Costo cero en régimen: el
llamador ni siquiera llama cuando está apagado.

**Lo que la herramienta ya dijo** (35 000 puntos por corrida): la bifurcación de N=79 está
en el **milisegundo 15 024**, dentro del lazo del blit de columnas de DOOM (`0002EF3E`,
el mismo de la fase 0) — que en ese instante es **puro entero** (la traza con
`DCEMU_TRAZA_EN_MS=15023:300` lo lista completo: ni un FPU). Los contadores del lazo
difieren desde la entrada (r5/r6: 3/4 contra 2f/30; r7 la coordenada de textura), o sea
que **la divergencia viene de los parámetros calculados antes del lazo**. Y la página es
dinámica: a los 16 s la misma dirección contiene datos que decodifican como basura con
`FADD`/`FMOV.S` adentro — con la fila de `fmovs173` esa basura se vuelve *traducible*
donde antes cortaba, que es la pista de por qué la mera existencia de la fila cambia algo.
La segunda vuelta de la herramienta (`DCEMU_CP_MS=N:1`, el **modo fino**: un punto por
pasada del bloque periódico en la ventana [N−1, N+1], con `reloj_total` en cada línea)
cerró el cerco en tres iteraciones:

1. El primer punto fino distinto difiere **sólo en `reloj_total`: un ciclo**, con PC y
   todos los registros idénticos. La divergencia es de **contabilidad de ciclos**, latente
   durante segundos hasta que una frontera del bloque periódico cae distinto cerca de una
   entrega de interrupción.
2. Con `ciclos=` agregado al punto por milisegundo: el desfase nace **dentro del ms
   15 023**, no antes — todo idéntico hasta ahí, ciclos incluidos.
3. El modo fino sobre ese milisegundo: el desfase (+3 ciclos) nace **cruzando una entrada
   de excepción usuario→kernel** (SR `00008001` → `70008001`), en plena tormenta de
   reintentos (pasadas del periódico cada 40-60 ciclos), con la ventana acotada a ~40
   instrucciones de código entero puro (`0002d77x`).

**Una hipótesis fuerte, probada y refutada**: que el conductor no emitiera el corte tras
instrucciones de 0 ciclos («sin ciclos nuevos la condición no pudo volverse cierta» — regla
verdadera para `CYC ≥ RELOJ_GRANO` pero falsa para `intc_sh4_reintentar`, que el intérprete
evalúa en cada frontera). La sonda —corte tras **toda** instrucción— se corrió: **el
desfase del ms 15 023 persiste idéntico**. El conductor queda descartado, y la regla del
corte por ciclos queda de paso validada como inocente.

**El siguiente paso quedó definido por la refutación, se corrió, y contestó**: con
`mmucr=` en las dos líneas de `DCEMU_CP_MS`, el primer tick divergente muestra
**`00008401` contra `00008801` — URC 33 contra 34, un avance de más en el lado del
traductor**, junto con los +3 ciclos, con el tick anterior idéntico (URC 21 ambos). Al
grano de milisegundo el MMUCR reconverge (idéntico en el punto del desfase de ciclos), o
sea que el avance extra tuerce **cuál** entrada reemplaza un `LDTLB` cercano, eso cambia
una falta de TLB en la tormenta (±ciclos de entrada de excepción con los registros
intactos), y quince segundos después el blit de DOOM arranca con otros parámetros.

**La divergencia entera queda así caracterizada**: un único avance extra de `URC` en una
ventana de ~40 instrucciones conocidas (`0002d782` → entrada de excepción usuario→kernel,
`reloj_total` ≈ 2 997 077 878-917), que sólo ocurre cuando el conjunto de bloques incluye
los de `fmovs173`. Los caminos auditados sin encontrarlo: la búsqueda de la registración
(siempre acierta la página recién despachada), la de `jit_verificar` (espeja la del
intérprete y compensa en los rechazos), el talón del puente (exacto por construcción) y
los dobles avances del camino rápido de datos (el desvío `_fis` existe justamente para
eso, y el esqueleto de 16 bits es copia del de 8 probado). La caza que sigue pide
instrumentar los avances de URC del lado del traductor en la ventana —un contador por
sitio de avance, volcado en el `cpf`— y es trabajo de una sesión fresca con este mapa.

El lote quedó como diff en el scratchpad de la sesión (`fpu-v1.diff`) y el árbol
revertido y exacto.

### La falsa regresión de Crazy Taxi, y lo que la palanca demostró

Entre la tanda del lote 2 y la de este lote, CT parecía +2,2 % (97 253 → 99 349, ambas
tandas tranquilas). La bisección con `DCEMU_JIT_PLANTILLAS=66` —que reproduce el juego de
plantillas del lote 2 **sobre este mismo binario**— dio el veredicto contrario: **el juego
completo es 0,8 % más rápido** (99 226 contra 100 039 ms), y el recorte reprodujo las
cuentas del lote 2 al bit, que es la palanca demostrando su exactitud. La diferencia entre
tandas era la capa de reentrenamiento: cada tanda lleva perfil nuevo, y en CT esa capa
vale ±1-2 % — su resolución real entre tandas, contra el 0,1-0,6 % de DCDoom (que pesa 7×
en el perfil). **Los absolutos de CT se comparan dentro de un binario o no se comparan.**
