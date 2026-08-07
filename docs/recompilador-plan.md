# Plan: el recompilador dinámico

Estado: **propuesto**. Escrito el 2026-08-07 sobre `rendimiento-hilos`, el mismo día en
que las dos sondas de la fase 4 de [`rendimiento-plan-2.md`](rendimiento-plan-2.md) le
dieron a este plan sus números. Nada de esto está implementado; las dos sondas
(`fusion.c`) son el precedente, la prueba de concepto y la vara de aceptación de la
fase 0.

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
  lenta, todo viaja gratis. **La llamada no es el costo: está medido** (6.6 quitó 688
  millones de llamadas y el reloj no se movió; el 2,2× de la sonda CE ya carga la
  traducción por acceso). Inlinear el camino rápido de RAM plana en el código emitido
  es una optimización de fase tardía, con su A/B, no una premisa.
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
| 0 | El emisor mínimo y el caché; **los dos bloques de `fusion.c` emitidos a mano por el JIT** (traduce esos PC y nada más) | reproduce los A/B de las sondas: mismos números, ejecución al dígito | que el emisor y la convención existen y no mienten |
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
