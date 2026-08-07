# Plan: rendimiento, segunda etapa

Estado: **en curso**. Escrito el 2026-08-07 sobre `rendimiento-hilos`, después de que el
perfil con contadores de hardware (el 0.1, en [`interprete-plan.md`](interprete-plan.md))
cerrara el diagnóstico: el intérprete va **limitado por volumen** —IPC 3,4-3,9, el
predictor y la LLC limpios, ~90 instrucciones de anfitrión por emulada en Katana y ~180
con MMU—, no por fallos. Este documento es el plan de lo que queda; los resultados se
anotan acá abajo a medida que salgan, como en las bitácoras anteriores.

## Dónde estamos

Release + PGO, i9-13900, bancos canónicos:

| banco | velocidad | fps | estado |
| --- | --- | --- | --- |
| Crazy Taxi (Katana) | 1,69× | 94,8 | sobra margen |
| Virtua Tennis (Katana) | 1,48× | — | sobra margen |
| DCDoom (Windows CE, MMU) | 0,73-0,75× | 31,6 | **debajo de consola**; 1,00× = 42,3 fps |
| Sega Rally 2 (Windows CE, MMU) | 0,51× | — | **debajo de consola**, sin banco propio |

**Todo el déficit conocido está del lado de la MMU.** Sega Rally 2 no es un misterio
Katana: es Windows CE —su «contador propio» de `0x00446880` era `SB_TFREM` mapeado a
espacio de usuario, o sea traducción activa— y encima con carga real de PVR, que DCDoom
casi no tiene (una tira por escena). Por eso queda aún más abajo que DCDoom.

Lo agotado está medido y no se vuelve a intentar sin una razón nueva: despacho 0, caché
de bloques 0, inline −19 %, tabla compacta −2 %, comprobaciones del bucle 0, hilos del
AICA −4/−5 %, memoización del ARM +0,5 %, AVX2 −2,1 %. Lo que rindió ya está cobrado.

## Fase 1 — La instantánea, bien hecha esta vez (6.3 de `rendimiento-plan.md`)

El intento anterior clasificó por **tipo de operando** con una lista blanca, y rompió a
DCDoom en silencio con las 21 suites y SingleStepTests en verde. El agujero más probable,
visto desde el código: **los saltos con ranura de retardo cuyo tipo de operando parece
inofensivo**. `RTS` es `OP_T_NA` y `BRA`/`BSR` son `OP_T_LABEL12` —tipos «sin memoria»
que una lista blanca por operando exime—, pero su ranura ejecuta una instrucción
arbitraria con `core.execute()` anidado, y si esa instrucción falta, la instantánea que
se restaura es **la del salto**. Sin ella, el estado queda a medio mutar y no hay
síntoma. La exclusión por mnemónico del intento cubría `BRAF`/`BSRF` y nada más.

Lo que cambia esta vez, en tres piezas:

1. **La clasificación es por manejador auditado, no por tipo de operando.** Una lista
   explícita en `opcodes.c`, al lado de `opcodes[]`, de los manejadores que una lectura
   de su cuerpo demostró incapaces de llegar a `excepcion_abortar()` o
   `excepcion_direccion()`: sin `memread`/`memwrite`, sin FPU, sin ranura de retardo,
   sin excepción propia (`TRAPA`, ilegales, privilegiadas). En la práctica: los ALU de
   registro a registro (`MOV`/`ADD`/`CMP`/`TST`/lógicos/corrimientos/extensiones/
   multiplicaciones/`DT`/`MOVT`/`MOVA`/`NOP`...). Todo lo demás —memoria, saltos,
   control de sistema, FPU, `NOIMP`— conserva la instantánea. Una fila nueva de
   `opcodes[]` queda como «necesita» por omisión: el mecanismo no puede aflojarse solo.
2. **`initopcodes()` expande la lista a una tabla de un byte por codificación**, junto a
   las cuatro tablas de despacho, con la misma semántica de solapamiento. Sólo las filas
   sin restricción de PR/SZ pueden eximir (las restringidas son todas de FPU).
3. **El aborto sobre una instrucción exenta es un error ruidoso, no un silencio.** El
   camino frío del aborto comprueba si la instrucción en curso estaba exenta y lo grita
   con PC y opcode. Es la diferencia con el intento anterior: una clasificación
   equivocada pasaba de «medio registro escrito sin síntoma» a un reporte inmediato.
   Y antes de encender la elisión, un modo de verificación
   (`DCEMU_SONDA_ELISION_VERIFICAR=1`) toma la instantánea **siempre** —corrección
   intacta— y sólo contrasta la clasificación contra los abortos reales de una corrida
   entera de DCDoom, de Sega Rally 2 y de las demos de MMU.

`DCEMU_SIN_ELISION_INSTANTANEA=1` apaga la elisión en el mismo binario, que es la única
forma de A/B que este árbol acepta.

**Expectativa, escrita antes de medir:** el intento anterior bajó las instantáneas de
0,94 a 0,51 por instrucción; la sonda de los bancos de FPU valía 3,6 % sola. Esto debería
rendir del orden de **3-5 % en DCDoom** y nada en Katana (con `excepcion_vigilar` en cero
no se ejecuta). Baranda decisiva: **la captura byte a byte de DCDoom**, que es lo único
que vio el error la vez pasada; más `ctest`, `dcemu_sh4json` bit a bit, `demos/mmu-mapeo`
y `basic/mmu/*`.

## Fase 2 — El banco de Sega Rally 2

Las bitácoras lamentan que DCDoom sea el único guest con MMU del árbol, y la fase 1 se
rompió una vez justamente donde sólo DCDoom podía verlo. Sega Rally 2 da el segundo
banco: MMU **y** geometría real, o sea la interacción MMU+PVR que DCDoom no ejercita
(store queues traducidas, `mmu_traducir_sq()`, el camino del ddraw).

Definir la receta —segundos emulados, teclas si hacen falta, cuentas de control
(instrucciones, escenas, tiras) y el hash de una captura de referencia— y anotarla acá.
A partir de ahí es baranda y banco de todo lo que toque la MMU.

## Fase 3 — Lo que queda de `mmu_traducir()` en el camino de datos

De los ~9,5 ns por instrucción de DCDoom, unos 4,2 son MMU (la diferencia contra los
~5,0-5,2 de un guest sin ella). Con las cachés ya puestas, lo que queda es la entrada a
`mmu_traducir()` con sus comprobaciones de rango, para las 0,37 traducciones por
instrucción: probar la caché de traducciones resueltas **dentro del macro** de
`memread`/`memwrite` y llamar sólo al fallar. Expectativa optimista del documento
anterior: DCDoom cerca de **0,80× / 34 fps**.

Ojo con el antecedente: 6.6 (P1/P2 en línea) dio cero con el layout suelto y no se
volvió a medir con PGO. Se re-mide antes de darlo por cero, pero la expectativa es baja.

## Fase 4 — El prototipo que decide el recompilador

La única palanca grande que queda, y el árbol ya tiene el argumento **a favor** correcto
—volumen: bajar de ~90 a ~15-25 instrucciones de anfitrión por emulada, con expectativa
2-2,5× por Amdahl— y los datos de forma que lo sostienen: 5 bloques cubren el 50 % de las
instrucciones de Crazy Taxi (9 en DCDoom), 168/91 cubren el 90 %, con cientos de miles de
ejecuciones por bloque. También tiene el argumento en contra medido: **el tamaño del
código caliente es de primer orden acá** (el inline de 120 líneas costó 19 %).

Por la regla de la casa —ninguna fase se implementa antes de medir su techo con un
binario desechable—:

**El prototipo: traducir a mano los 5-10 bloques más calientes como funciones C
fusionadas**, compiladas en el binario (sin generación en tiempo de ejecución),
despachadas cuando el PC y las precondiciones coinciden, reutilizando el andamiaje de
A/B que `DCEMU_BLOQUES`/`DCEMU_INLINE` dejaron. Fusionar es lo que el perfil dice que
vale: registros del SH-4 en locales a lo largo del bloque, `PC` y `cycles` una vez al
salir, sin las comprobaciones por instrucción. MSVC asignando registros sobre ese cuerpo
es un proxy honesto de lo que emitiría un JIT.

- Primero sobre Katana (`excepcion_vigilar == 0`): el caché de bloques ya documentó que
  con MMU la búsqueda puede faltar y un bloque grabado se la saltearía.
- Verificación: cuentas de instrucciones idénticas al dígito, capturas byte a byte, y el
  PC verificado **al principio de la vuelta** (la lección del cursor del caché de
  bloques: una interrupción mueve el PC después del despacho).
- **Regla de decisión, escrita antes de medir:** si cubrir ~50 % del volumen rinde
  ≥15 %, el recompilador (o una biblioteca de bloques traducidos por anticipado) se
  justifica y recibe su propio plan; si rinde ≤5 %, la hipótesis del volumen tampoco
  paga en la práctica y el árbol está cerca del piso de su intérprete — y eso también es
  un resultado.
- **Y la consecuencia de que el déficit viva en los guests con MMU:** si el prototipo
  Katana rinde, el paso siguiente no es «hacer el recompilador» sino un segundo
  prototipo con la verificación de las generaciones de página de la búsqueda al entrar
  al bloque (`mmu_utlb_gen[]` y la caché de fetch ya existen) y la semántica de
  reejecución cuando un acceso de datos falta a mitad de bloque — porque ahí es donde el
  beneficio puede achicarse, y es exactamente donde hace falta.

## Fase 5 — Lo chico y lo pendiente de otros planes

- **Censo de los ~14 juegos**: qué arranca por `0WINCEOS.BIN` (o sea cuántos guests con
  MMU hay en el parque) y la velocidad de cada uno con `--perf`, 60-90 s. Dimensiona a
  quién sirven las fases 1-4. Barato; no bloquea nada.
- **Afinidad de hilos**: `--hilos` perdió 4-5 % y la hipótesis anotada sin probar es la
  colocación en E-cores. `SetThreadAffinityMask` a P-core en `hilo.c`, detrás de una
  opción, una tarde. Techo: el ~15 % del ARM7+mezcla. Si sigue negativo, la fase se
  cierra definitivamente.
- El hilo de render sigue fuera: el camino gráfico entero es 7,6 % y el riesgo no lo
  paga — salvo que el censo muestre un guest atado a GL.

## Barandas y método

Las de siempre, sin cambios y no negociables: PGO siempre (medir sin él ya no significa
nada acá), binarios alternados dentro de una tanda con hash verificado, primera pasada
descartada, la captura de DCDoom como baranda del camino de MMU —el barrido de 150 demos
**no puede ver nada de la MMU**—, `.wav` para lo que toque ARM/AICA, y toda comparación
verifica cuadros, escenas y tiras antes de mirar el reloj.

## Orden

| # | qué | expectativa | riesgo |
| --- | --- | --- | --- |
| 1 | la instantánea, por manejador auditado + cable trampa | 3-5 % en DCDoom | medio |
| 2 | banco de Sega Rally 2 | baranda + banco, no velocidad | bajo |
| 3 | `mmu_traducir` en el macro; re-medir 6.6 con PGO | DCDoom → ~0,80× | medio |
| 4 | prototipo de bloques fusionados | **decide el recompilador** | bajo (desechable) |
| 5 | censo WinCE del parque; afinidad de `--hilos` | información / ≤15 % | bajo |

---

# Fase 1, implementada: la elisión vale 2,5 % en DCDoom (2026-08-07)

Está en el árbol, encendida, y `DCEMU_SIN_ELISION_INSTANTANEA=1` la apaga en el mismo
binario. Elide la instantánea en **0,46 de las instrucciones** de DCDoom (2 508 612 258
de 5433 millones): las tomadas bajan de 0,94 a 0,48 por instrucción.

## Cómo quedó hecha

- **La lista es de manejadores auditados, no de tipos de operando**:
  `manejadores_sin_aborto[]` en `opcodes.c`, 85 funciones cuyo cuerpo se leyó una por una
  — los ALU de registro de `mov.c`/`arith.c`/`logic.c`/`shift.c` enteros, más los puros de
  `syscontrol.c` (`NOP`, `CLRT`/`SETT`/`CLRS`/`SETS`/`CLRMAC`, `LDS`/`STS` de
  MACH/MACL/PR en registro, y OCBP/OCBWB, que aquí son no-ops). Nada que toque memoria,
  nada de FPU, **ningún salto** — la ranura de retardo ejecuta una instrucción arbitraria
  y su aborto restaura la instantánea del salto, que es exactamente el agujero del intento
  anterior (`RTS` es `OP_T_NA`, `BRA` es `LABEL12`: tipos «sin memoria» que una lista
  blanca por operando eximía).
- **La tabla se llena en una pasada final sobre las cuatro tablas de despacho**: una
  codificación solo queda exenta si los cuatro modos de PR/SZ resuelven al mismo manejador
  auditado. Independiente del modo por construcción; una fila nueva de `opcodes[]` nace
  como «necesita».
- **El aborto sobre una exenta grita en vez de corromper**: `excepcion_abortar()` y
  `excepcion_direccion()` comprueban el cable trampa (`excepcion_exenta_en_curso`) en el
  camino frío. Y `DCEMU_SONDA_ELISION_VERIFICAR=1` toma la instantánea igual y solo
  contrasta la clasificación — es la corrida previa a confiar en la lista, con la
  corrección intacta.
- **La instantánea se toma después de buscar la instrucción** (antes era antes), porque la
  clasificación necesita la palabra. Eso obligó a estrenar
  `excepcion_instantanea_invalidar()` —existía del intento revertido, sin llamadores—
  antes de la búsqueda: una falta de búsqueda con la instantánea de la instrucción
  anterior marcada válida la habría restaurado. El contenido de la copia no cambia por el
  orden (la búsqueda no toca `core.context`), y la línea base lo confirma byte a byte.

## Las barandas, todas corridas

- `ctest` **23/23**, incluido un caso nuevo en la suite `decodificacion` con canarios de
  intención: los puros tienen que estar exentos, y `RTS`/`BRA`/`BT/S`/`JSR`/`RTE`,
  `MAC.L`, `TAS.B`, `PREF`, `TRAPA`, `MOVCA.L`, los `.B` de GBR, toda la FPU, `NOIMP` y
  `0xFFFF` tienen que necesitar la instantánea, con nombre propio.
- `dcemu_sh4json`: **113 191 ok, 0 fallan, 3306 divergen a propósito, 3 descartados** —
  la línea documentada, al dígito.
- **DCDoom, captura `--sin-vmu`: `198B396F…` en las tres configuraciones** — elisión
  activa, apagada, y modo verificación — con el mismo trabajo al dígito
  (5 433 038 875 instrucciones, 1482 cuadros, 977 escenas). Es la línea base del árbol.
  (La cifra de instrucciones difiere de la de la fase 6 de `rendimiento-plan.md`
  —5 433 052 826— porque aquélla se tomó con otra configuración de VMU; el hash es el
  que ancla.)
- **El modo verificación corrió el banco entero de DCDoom y el cable trampa no sonó ni
  una vez**: cero abortos sobre instrucciones exentas en 5433 millones.
- `demos/mmu-mapeo`: **TEST SUCCEEDED** con la elisión activa — la falta, la recarga y la
  reejecución completas.
- `basic/mmu/nullptr` y `basic/mmu/pvrmap`: el serial **byte a byte idéntico** con y sin
  elisión, incluido el `kernel panic` que `nullptr` tiene que producir.

## El A/B

Mismo binario, la variable elige la rama, cuatro pares alternados con el orden invertido
en los pares pares, una pasada de calentamiento descartada, trabajo idéntico al dígito en
las ocho corridas. Banco de DCDoom (35 s, `--sin-vmu`, sin captura), **con el PGO viejo**
—el código cambió de forma y el perfil describe el programa anterior, así que los
absolutos van abajo; el A/B no lo sufre porque las dos ramas cargan el mismo handicap—:

| | ms reales | media | ns/instr |
| --- | --- | --- | --- |
| con elisión | 52 180 / 53 101 / 52 922 / 52 893 | **52 774** | 9,6 |
| sin elisión | 54 220 / 54 162 / 53 961 / 54 141 | 54 121 | 10,0 |

**−2,5 %, rangos disjuntos** — el peor «con» (53 101) queda debajo del mejor «sin»
(53 961) — y cuatro pares con el mismo signo en los dos órdenes. La expectativa decía
3-5 % y salió 2,5: la copia de 176 bytes alineados era más barata de lo que su cuenta
sugería, que es la misma lección de la fase 6 (la instantánea entera se midió en <10 %).

En Katana no ejecuta ni una línea (`excepcion_vigilar` en cero), así que el costo ahí es
la disposición del binario y nada más — y eso lo fija el reentrenamiento de PGO, que es
el paso siguiente del método desde la fase 6.

## Con el PGO reentrenado

`pgo.ps1` (tres bancos, pesos 1/1/7, `.pgd` limpio) y recompilado con `USE`. La captura de
DCDoom sigue en `198B396F…` con el binario nuevo, y los absolutos, con el calentamiento
descartado:

| banco | ms reales | velocidad | ns/instr |
| --- | --- | --- | --- |
| **DCDoom** | 45 124 / 44 690 | **0,78×** (~33 fps) | **8,2-8,3** |
| Crazy Taxi | 115 497 / 119 392 / 119 219 | 1,50-1,55× | 5,1-5,3 |

**0,78× es la mejor cifra que DCDoom ha dado en este árbol** (la documentada era 0,73×,
8,7 ns). La atribución exacta entre la elisión y el perfil fresco no se puede separar sin
alternar binarios, pero el 2,5 % causal ya está establecido por el A/B de arriba.

Crazy Taxi lee 1,50-1,55× en esta tanda, dentro de la banda histórica (1,45-1,69× según
la tanda) y por debajo de la mejor documentada (1,69×). Puede ser deriva de la máquina o
disposición; **el mecanismo no ejecuta nada en Katana**, así que si alguien sospecha un
costo real, el control es `rama-ab.ps1` contra el commit anterior a la elisión, cada
binario con su propio perfil entrenado — no una comparación entre tandas, que en este
árbol no es un dato.

Los ~14 000 de diferencia en la cuenta de instrucciones contra la fase 6
(5 433 038 875 contra 5 433 052 826) son de configuración de VMU, no del cambio: el hash
de la captura —que es lo que ancla la línea base `--sin-vmu`— sale idéntico en las tres
configuraciones de la elisión y con los dos perfiles de PGO.

## Lo que queda de la fase 1

Nada bloqueante. Dos refinamientos anotados, ninguno urgente:

- **La FPU aritmética de registro (`FADD FRm,FRn`...) queda como «necesita» a propósito**:
  con bits de Enable en FPSCR sí puede abortar, y la clasificación es estática. Una
  exención condicionada a «no hay Enables» sería un bit dinámico más en la prueba; para
  DCDoom no mueve la aguja (poca FPU), para Sega Rally 2 podría — medirlo cuando exista su
  banco (fase 2).
- Las formas de registro de `STC`/`LDC` de registros de control quedaron fuera por
  conservadurismo; son raras y el beneficio sería ruido.

---

# Fase 2, hecha: el banco de Sega Rally 2 (2026-08-07)

## La receta

```sh
dcemu.exe --perf --salir-tras=60 --sin-vmu \
    "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"
```

Sin teclas: en 60 segundos emulados el juego llega solo a 3D real. Las cuentas de control,
idénticas al dígito en las cuatro corridas de hoy (dos con captura, dos sin):

- **8 607 000 273 instrucciones, 3255 cuadros, 3112 escenas, 1268 tiras por escena
  (la mayor, 5693)** — más geometría por escena que Crazy Taxi en juego.
- Captura de referencia (`--captura-gl`, `--sin-vmu`): **`1B28D0D9…`**, reproducida en dos
  corridas — una de ellas con la sonda de verificación puesta, o sea que la sonda no
  perturba.

## Lo que el banco dijo el primer día

- **La sonda de verificación calló también aquí**: cero abortos sobre exentas en 8607
  millones de instrucciones de un guest Windows CE con FPU pesada y PVR real — el perfil
  de código que DCDoom no ejercita. La lista de la fase 1 queda contrastada contra los
  dos guests con MMU del árbol.
- **La elisión cubre 0,33 por instrucción** (contra 0,46 en DCDoom): el código de SR2
  tiene más mezcla de memoria y FPU. El refinamiento anotado en la fase 1 —eximir la FPU
  aritmética de registro cuando no hay Enables— tiene aquí su clientela; medirlo contra
  este banco.
- **Velocidad hoy: 0,64-0,74× en limpio** (80 695 / 93 667 ms), con una dispersión del
  16 % entre corridas consecutivas idénticas — la máquina estaba movida en esta tanda,
  así que el absoluto queda anotado como banda, no como cifra. Presenta 54,2 cuadros por
  segundo emulado; para 1,0× le falta un 1,35-1,55×. Con `--captura-gl` y la sonda:
  0,48× — la captura sigue comiéndose ~un tercio del reloj, como siempre.
- El 0,51× que este plan citaba venía de la medición de `--escala` con otra
  configuración; los números comparables son los de esta receta.

Cualquier A/B sobre SR2: pares alternados como siempre, y con esta dispersión, más pares
que los cuatro habituales o esperar una máquina quieta.

---

# Fase 3, implementada: el sondeo en el macro vale 1,6 % en DCDoom (2026-08-07)

El acierto de la caché de traducciones resueltas (`mmu_datos[]`) se sondea ahora **dentro
del macro** de `memread`/`memwrite` — `MMU_TRADUCIR_EN_SITIO` en `mmu.h`, el mismo
precedente que `MMU_FETCH_PUNTERO` — y `mmu_traducir()` solo se llama al fallar.
`DCEMU_SIN_MMU_MACRO=1` lo apaga en el mismo binario.

## Lo que compra y lo que no cambia

- El bit de permiso es **constante en el sitio de expansión** (lectura o escritura se
  pliega al compilar), y el acierto queda dentro del manejador sin cruzar la frontera de
  la llamada.
- **El acierto sigue avanzando URC** (`MMU_URC_AVANZAR()`, un solo cuerpo compartido con
  `mmu.c`): de URC depende qué entrada reemplaza el `LDTLB` del guest, o sea su camino de
  ejecución. Es el invariante que la sonda de la fase 6 ya protegía.
- **Los contadores de `--perf` se incrementan igual** que por la llamada, así que la
  verificación de trabajo entre corridas no cambia de significado con el interruptor —
  y lo demostró: 2 018 173 538 traducciones y 850 557 faltas, al dígito en los dos modos.
- Todo lo que no es un acierto limpio — P1/P2/P4, permisos sin validar, generación
  vencida, cachés apagadas — cae en `mmu_traducir()`, que decide igual que siempre.
  `DCEMU_SIN_CACHE_MMU=1` apaga también el sondeo, porque sin caché no acertaría nunca.

## Barandas

`ctest` 23/23; `dcemu_sh4json` **113 191 ok / 0 fallan**; DCDoom `198B396F…` con el
sondeo activo y apagado, mismas cuentas al dígito; `demos/mmu-mapeo` **TEST SUCCEEDED**.

## El A/B

Mismo binario, cuatro pares alternados con el orden invertido, calentamiento descartado,
banco de DCDoom:

| | ms reales | media |
| --- | --- | --- |
| con el sondeo en el macro | 45 461 / 45 580 / 45 461 / 45 421 | **45 481** |
| sin (todo por `mmu_traducir`) | 46 241 / 46 253 / 46 312 / 46 151 | 46 239 |

**−1,6 %, rangos disjuntos**, cuatro pares con el mismo signo en los dos órdenes. Es la
mitad de lo que valió la elisión, y consistente con lo que 6.6 ya había enseñado: la
*llamada* no era el costo — lo que paga aquí es el permiso constante y el acierto
integrado en el manejador.

El riesgo anotado es el tamaño del código: el sondeo se expande en cada sitio de
`memread`/`memwrite` detrás de `if (mmu_activa)` — Katana no lo ejecuta nunca, pero
ocupa lugar. El control es el banco de Crazy Taxi tras reentrenar PGO, abajo.

## Con el PGO reentrenado: el estado al cierre del 2026-08-07

Perfil fresco (tres bancos, pesos 1/1/7), captura de DCDoom en `198B396F…`, trabajo al
dígito en todo:

| banco | ms reales | velocidad | ns/instr | al abrir la sesión |
| --- | --- | --- | --- | --- |
| **DCDoom** | 44 383 / 43 864 | **0,79-0,80× (~34 fps)** | 8,1-8,2 | 0,73× · 31,1 fps |
| **Sega Rally 2** | 77 211 / 77 661 | **0,77×** | 8,9-9,0 | ~0,5× |
| Crazy Taxi | 115 506 / 119 036 / 116 492 | 1,51-1,55× | 5,1-5,3 | banda 1,45-1,69× |

- **DCDoom queda en el 0,80× que el plan viejo daba como techo optimista de esta línea**
  («Y por qué 60 fps no sale de aquí», `rendimiento-plan.md`). Lo que suma la sesión
  entera —elisión 2,5 % causal + sondeo en el macro 1,6 % causal + perfil fresco— lo
  lleva de 31 a ~34 fps.
- **Sega Rally 2 a 0,77×**, estable al 0,6 % en esta tanda. La banda de la mañana
  (0,64-0,74×) era la máquina movida; la mejora sobre el ~0,5× de la referencia vieja es
  de esta sesión más el cambio de receta.
- **Crazy Taxi no se movió** con el sondeo en el macro (1,51-1,55× antes y después de la
  fase 3, mismas cuentas): el costo de tamaño de código en Katana no aparece.
- Para 1,0× a DCDoom le falta ~1,25× del intérprete; para 60 fps, ~1,8×. Eso ya no está
  del lado de la MMU: es la fase 4 — el prototipo de bloques fusionados que decide el
  recompilador.

## Lo que queda de la fase 3

- **La fetch-cache y `mmu_traducir_sq()` siguen entrando por llamada** — sus frecuencias
  son órdenes de magnitud menores (1,7 % de las búsquedas; PREF de SQ) y el mismo
  razonamiento de 6.6 dice que no pagan. No se tocan sin un número que lo pida.
- 6.6 (P1/P2 en línea) queda cerrado: ya estaba adoptado por la corrección del contador,
  y su cero de velocidad es consistente con lo medido aquí — la llamada no es el costo.

---

# Fase 4, medida: la fusión vale 17,4 % cubriendo el 46 % — el recompilador se justifica (2026-08-07)

El prototipo desechable existe (`fusion.c`, detrás de `-DDCEMU_FUSION`, elegido con
`DCEMU_FUSION=1` dentro del binario) y contestó la pregunta que venía a contestar. La
regla de decisión estaba escrita antes de medir: ≥15 % cubriendo ~50 % del volumen
justifica el recompilador. **Salió 17,4 %.**

## Qué se tradujo

La sonda de forma, extendida para nombrar los bloques (los 16 más pesados salen ahora en
el resumen de `--perf` con PC, largo y peso), dijo que los cuatro bloques más calientes
de Crazy Taxi son **un solo lazo** — `0c1583f8 → 0c158400 → 0c158418` más el callback
`0c156c30` — con las mismas 535,8 M de ejecuciones y el **47,2 % de todas las
instrucciones**. Desensamblado, es el lazo de espera del juego: sondea dos contadores y
llama por puntero a un callback que es un `RTS` pelado. 20 instrucciones y 35 ciclos por
vuelta, diez de ellas cargas.

Se tradujo a mano como C fusionado: registros del SH-4 en locales, las direcciones de
los literales de PC plegadas a constantes, los ciclos de cada instrucción los de su
manejador (incluida la rareza de la ranura del `RTS`, un `NOP` que no suma), y **los
cortes del bloque periódico en las mismas fronteras de instrucción que el intérprete**
— que es lo que hace la ejecución idéntica al dígito y por tanto medible. Todo lo no
cubierto vuelve al intérprete en esa misma instrucción: el callback que no sea el `RTS`
conocido, una dirección desalineada (el error lo levanta el camino de siempre), las dos
salidas del lazo, la región modificada (verificada entera en la primera entrada y su
primera palabra en cada una). La traza y el UBC apagan la fusión solos: ven instrucción
por instrucción y el lazo fusionado no corre con ellos puestos.

## La verificación y el número

**La ejecución es la misma al dígito**: 22 279 918 865 instrucciones, 10 001 cuadros,
9994 escenas y 1183 tiras por escena con la fusión y sin ella, y la captura de 60 s
byte a byte idéntica. El 46,1 % de las instrucciones corrió fusionado
(10 274 007 985, en 46,8 M de entradas de ~219 instrucciones ≈ 11 vueltas del lazo
entre cortes del reloj — exactamente lo que da `RELOJ_GRANO`/35).

Mismo binario, cuatro pares alternados con el orden invertido, calentamiento descartado:

| | ms reales | velocidad |
| --- | --- | --- |
| con fusión | 98 030 / 97 945 / [97 803] / 98 042 | **1,83-1,84×** |
| sin | 118 384 / 119 098 / 118 826 / 118 258 | 1,51-1,52× |

**17,4 % (medias 97 955 contra 118 642), 4,4 contra 5,3 ns por instrucción**, con una
dispersión del 0,24 % en el lado fusionado. La corrida entre corchetes ejecutó 1672
instrucciones de más —la firma de un evento de XInput, que se lee global— y queda
descartada por la regla de siempre; las tres parejas limpias son unánimes.

## Las dos lecturas honestas, que son lo que la fase venía a comprar

1. **El techo por porción es 1,6×, no los 3-4× de la aritmética de volumen.** Los 20 687
   ms ganados sobre 10 274 M de instrucciones fusionadas son ~2,0 ns menos por
   instrucción: de 5,3 a ~3,3. Este lazo es mitad cargas, y las cargas siguen pagando el
   macro de `memread` entero — la fusión quita el despacho, el PC y los ciclos por
   instrucción, no el acceso a memoria. La extrapolación honesta para código con esta
   mezcla es **1,5-2× sobre el volumen cubierto**; código ALU/FPU denso fusionará mejor.
2. **El 47 % de Crazy Taxi es espera, no trabajo.** El lazo traducido es el idle del
   juego, así que el 17,4 % de CT es real pero CT no lo necesita (ya iba a 1,5×). Los
   guests que sí lo necesitan —los Windows CE, 0,77-0,80×— tienen bloques más largos
   (12,25 instrucciones de media) y de trabajo real. Con 1,6× sobre el 90 % del volumen,
   DCDoom daría ~1,5× del intérprete: **cruza 1,0× con margen**; 60 fps pediría que los
   bloques densos fusionen mejor que este lazo de cargas, que es lo esperable.

## La decisión

**La línea del recompilador se abre** — el plan es
[`recompilador-plan.md`](recompilador-plan.md), escrito el mismo día: un JIT de verdad,
con estas dos sondas como precedente y vara de aceptación de su fase 0. El paso siguiente ya estaba definido: el
segundo prototipo con lo que un guest con MMU exige — verificar las generaciones de
página de la búsqueda al entrar al bloque (`mmu_utlb_gen[]` y la fetch-cache existen) y
la semántica de reejecución cuando un acceso falta a mitad de bloque — porque ahí es
donde el 1,6× puede achicarse y ahí es donde hace falta. La infraestructura de esta
sonda (verificación de región, cortes exactos, salida al intérprete por instrucción) es
exactamente la que ese prototipo reutiliza.

---

# La sonda con MMU: 2,2× por porción — mejor que en Katana (2026-08-07)

El segundo prototipo corrió el mismo día. Se tradujo el bloque 2 de DCDoom
(`0002ef3e`, 8,9 % de las instrucciones): **el blit de columnas de DOOM** — la textura,
el mapa de color y el framebuffer, seis accesos traducidos por vuelta de 17
instrucciones, en el espacio de usuario de DCDOOM.EXE. Trabajo real, que es la mitad que
el lazo de espera de Crazy Taxi no podía medir.

## Las dos respuestas que la sonda vino a buscar

**1. La semántica de reejecución sin instantánea funciona, y es la «salida 3» de la fase
5 lograda estáticamente.** Antes de cada acceso, el bloque vuelca al contexto los locales
mutados, los ciclos acumulados y el PC de esa instrucción; la instantánea quedó
invalidada a la entrada. Si el acceso falta —fallo de TLB, protección, primera escritura—
el `longjmp` sale por adentro de la función, `falta_reponer()` no restaura nada y el
contexto **ya es** el estado pre-instrucción exacto: la excepción entra igual que en el
intérprete y la instrucción se reejecuta interpretada. El volcado (~10 stores) reemplaza
a la copia de 176 bytes, que es justo lo que el recompilador necesitaba que fuera cierto.

**2. La validez del código con MMU se resuelve barato.** El bloque vive en una página
mínima, así que la traducción de la búsqueda de la entrada —que `main_loop()` ya hizo,
con su falta posible por el camino de siempre— cubre las 17 palabras, y se verifican
todas contra la tabla en cada entrada. Otro proceso en la misma VA, un parche, otra
imagen: la comparación falla y el bloque vuelve al intérprete sin tocar nada.

## El error que costó una tarde, y lo que enseña

La primera corrida dio la captura y los cuadros idénticos pero **6,7 millones de
instrucciones menos** que la línea base. Los demás contadores decidieron el diagnóstico
en una corrida: pasos del ARM, faltas, traducciones y fallos de búsqueda **idénticos al
dígito** — la ejecución era exacta y lo roto era el *conteo*: el contador se volcaba al
salir, y una entrada que terminaba en falta salía por `longjmp` **por encima** del
volcado. Y el intérprete cuenta el intento abortado (`run()` cuenta antes de despachar),
así que el arreglo es contar el intento en el volcado pre-acceso, como hace `run()`.
Con eso: **5 433 038 875 al dígito**, 1482 cuadros, 977 escenas, `198B396F…`.

> Un desglose con varios contadores independientes convierte «diverge y no sé por qué»
> en «diverge exactamente esto» en una corrida. Es la enésima vez que paga.

## El A/B

Mismo binario, cuatro pares alternados, banco de DCDoom. El 8,8 % de las instrucciones
corrió fusionado (476,7 M en 2,09 M de entradas):

| | ms reales | media |
| --- | --- | --- |
| con fusión | 48 254 / 47 541 / 47 823 / 48 127 | **47 936** |
| sin | 50 220 / 50 470 / 50 172 / 50 352 | 50 304 |

**4,7 % cubriendo el 8,8 %, rangos disjuntos.** Por porción: la parte fusionada pasa de
9,3 a ~4,3 ns por instrucción — **2,2×, mejor que el 1,6× de Katana**, porque el
intérprete vigilado paga más por instrucción (la instantánea de las que la conservan, el
armado, `run()`) y el volcado de locales es mucho más barato que todo eso. La traducción
por acceso se paga igual en los dos lados (el macro de la fase 3), así que el 2,2× ya la
carga.

(Los absolutos del binario de fusión van ~10 % debajo del árbol normal — disposición y
código sin perfil; el A/B no lo sufre porque las dos ramas son la misma imagen. Si la
sonda se promoviera a algo más, PGO se reentrena como siempre.)

## Lo que esto cierra

El riesgo grande de la línea del recompilador era la MMU — que la reejecución y la
validez del código se comieran el factor. **Salió al revés: el guest con MMU es donde la
fusión más paga.** Extrapolando con 2,2× sobre el 90 % del volumen que 91 bloques cubren
en DCDoom: ~1,9× del intérprete — DCDoom pasaría de 0,80× a **~1,5×, cruzando consola con
margen y a la vista de los 60 fps** (que piden 1,42×). La extrapolación es aritmética,
no medida; pero las dos incógnitas que podían tumbarla —el costo de la reejecución y el
de la validez— ya no son incógnitas.
