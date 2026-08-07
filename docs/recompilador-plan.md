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
