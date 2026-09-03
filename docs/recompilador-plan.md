# El recompilador dinámico (JIT)

Estado y pendientes. **La bitácora completa — cada ronda con sus tablas, los
expedientes enteros de cada caza — está en git** (hasta `aaff874`, 2026-08-09); este
archivo guarda las conclusiones que hacen falta para trabajar, no el camino.

## Qué es hoy

`-DDCEMU_JIT=ON` (build aparte, `build-jit/`, con su propio perfil
`build-pgo/dcemu-jit.pgd` — ver la sección de PGO de CLAUDE.md). `DCEMU_JIT=2` es el
traductor automático; `=1`, los dos bloques de la fase 0 emitidos a mano.

- **134 plantillas**, seleccionadas por censo de qué corta bloques (no por completar
  `opcodes[]`) — las últimas doce las pidió **el censo ponderado por veces**
  (2026-08-18, `docs/jit-sota-plan.md` fase B.2): PREF (el flush de store queue, el
  cortador más pesado de CT), la geometría FPU (FTRV/FIPR/FMAC/FSCA/FSRRA, por el
  envoltorio ligero), los movedores de FPUL/PR (emisión directa) y XTRCT/ADDC (por
  manejador). Lo que corta hoy, por peso: los escritores de SR y de bancos (DOOM
  ~62 % de las cortadas), la fila FPU en ranura de retardo (CT 35,4 % de las
  entradas), FSCHG/FRCHG, LDTLB, TRAPA y las palabras de datos (deben cortar).
- **Los pares de rama** (`rts`/`bra`/`jmp`/`braf`/`jsr`/`bsr`/`bsrf` + ranura con
  memoria) ya no cortan: la emisión sincroniza con el PC
  de la RAMA antes de tocar nada (la falta reejecuta desde la rama, como la
  instantánea del intérprete), el destino dinámico viaja por el lugar seguro del
  estado, y el par TERMINA la traza (lo que sigue es otra función; dejar la cola le
  costó a SR2 dos puntos y +27 % de arena). En las llamadas, PR se compromete
  **después** de la ranura y el par solo se admite si esta no lee ni escribe PR:
  una falta conserva el PR viejo y el éxito deja `pc+4`, igual que el intérprete.
  `DCEMU_JIT_SIN_PARES=1` los apaga todos; `DCEMU_JIT_SIN_PARES_LLAMADA=1` aísla
  solo los tres nuevos.
- **El censo de la frontera** (en el resumen `jit:`, siempre): en qué termina cada
  bloque, ponderado por las veces que se corrió. Es lo que separa «hay muchos sitios»
  de «por ahí pasa la ejecución», y lo que eligió el lote de arriba. El residuo que
  nombra hoy palabras de datos, `TRAPA` y escritores de SR.
- **Exacto al dígito con capturas byte a byte en tres guests** (y en el trío CHD —
  18 Wheeler, THPS2, CvS2 — desde la fase A de `jit-sota-plan.md`). **Marcas
  vigentes: tanda del 2026-08-19, binario reentrenado `14D6AFDA3C7BF7BE`**, tras
  los lotes B.2 (censo ponderado) y B.2b (filas terminales + FPU en ranura):
  DCDoom **25 520-25 529 ms, −41,5 %, 1,37×**; CT **80 984-82 048 ms, −27,6 %,
  2,21×** (venía de −16,5 % y 1,74× antes de B.2; el banco cambió de camino:
  21,93 G instrucciones — el A/B interno es el veredicto); SR2 **51 745-52 335 ms,
  −29,7 %, 1,16×**. Entradas al despachador: DOOM 56,1 por entrada, CT 34,4,
  SR2 35,2; en SR2 la fila terminal absorbe el 13,4 % de las entradas.
  Contra la tanda anterior (`5B9CAAE45A977784`, índice de enlaces) lo que se
  movió es **SR2, de −25,6 % a −27,8 %**: es la separación de la clave de
  verificación, que se había medido sobre el binario entrenado para el escalón
  previo y aquí entra con su propia capa de PGO. El brazo `lineal` de la tanda
  anterior reproduce las marcas de antes del índice (−32,9 / −2,0 / −11,4 %),
  así que la comparación entre las dos generaciones no depende de la capa de
  reentrenamiento.
  **Ojo con el banco de CT**: el de esta tanda toma otro camino de juego que el de
  las anteriores (20,63 G instrucciones y 13,8 por entrada, contra 21,24 G y 20,1),
  y por eso su marca contra el intérprete no se compara con la de agosto 10 aunque
  el guest sea el mismo. Los pares de llamada solos valen ~1,0/5,5/0,4 %; C6xx es
  neutra en SR2 (~0,1 %) y queda porque elimina la frontera sin costo.
- El traductor emite **por identidad de manejador** (`OP_HANDLER` de la `oplist` real):
  no existe un segundo decodificador que pueda divergir del primero. Los ciclos de cada
  plantilla se copian leyendo el cuerpo ENTERO del manejador — nunca por cercanía: un
  ciclo robado del manejador vecino costó 29 449 instrucciones de divergencia.
- El emisor x64 (`jit_x64.c`) es propio, sin dependencias, y su suite
  (`tests/test_jit_x64.c`) compara byte a byte contra las codificaciones de Intel.
  `DCEMU_JIT_VOLCADO` desarma el riesgo del lado del binario.
- Arena de 192 MB (`VirtualAlloc`, RWX; SR2 usa ~100 con la traducción MMU en línea a
  ~4 KB por bloque), 32 768 bloques, hash de 2^17 con sondeo lineal de 8.

## Las reglas que lo mantienen sano

Cada una cerró una divergencia real; romperlas es silencioso.

- **La época** (`jit_validez = (jit_epoca << 1) | md`): se mueve con un cambio de mapeo
  (`mmu_fetch_invalidar` → `JIT_EPOCA_MAPEO`) o una escritura sobre página con código
  traducido; la verificación por entrada son dos comparaciones (puntero de búsqueda +
  validez), no el bucle de palabras. SR.MD entra en la clave porque un bloque traducido
  en privilegiado y reencontrado en usuario mapea a otro lado y la época no se entera.
- **El puente**: enlace directo solo dentro de la misma ventana de 1 KB — misma página
  bajo cualquier tamaño, para siempre —; entre páginas, un talón por salida hace la
  búsqueda real (avance de URC, repoblado, falta de TLB) antes de saltar. Sin MMU no
  hay búsqueda que reproducir: directo siempre, y la ventana tampoco limita el
  descubrimiento.
- **La clave FPU** (`jit_fpu_visto`): PR/SZ (bits 19-20 de FPSCR), «algún Enable», y
  **SR.FD como bit 3** — el 0x800 del cambio perezoso de contexto FPU de WinCE se alza
  en el despacho ANTES de tocar nada, y un bloque que ejecutara la fila FPU directo
  escribiría el banco del dueño anterior y correría URC en uno (la divergencia de
  633 M del expediente). Los bloques FPU no reciben enlaces; el despachador compara la
  clave en toda entrada; el descubrimiento corta filas FPU con FD puesto.
- **La regla de la falta**: una fila solo entra a la lista blanca de llamada-al-manejador
  si no muta estado antes de poder faltar, no mueve PC más que +2, y no toca SR.MD/RB
  ni FPSCR. `macl62` se reordenó (lecturas antes de mutaciones) para entrar, verificado
  con SingleStepTests. Ninguna fila FPU entra en ranura de retardo: su `PC += 2` sobre
  el contexto pisaría el destino que el salto capturó.
- **El conductor**: sincronización previa solo en filas que acceden a memoria
  (`tr_sync`: registros, PC, el intento contado ANTES — la regla de `run()`), ciclos
  por instrucción, y el corte del bloque periódico en la misma frontera en que el
  intérprete lo evaluaría. Las filas `propia` cuentan el intento por su cuenta: el
  contador que miente con ejecución intacta solo se ve en la verificación de exactitud.
- **En un par, los ciclos de la rama van DESPUÉS de la ranura**, como el manejador del
  intérprete. No es cosmético: la sync vuelca CYC y una ranura por manejador (NEGC)
  lo RECARGA del contexto — ciclos sumados al registro después de la sync y antes de
  la ranura se evaporan en la recarga. Costó ±15-19 k instrucciones de divergencia
  con la captura intacta: los cortes corridos mueven la entrega, no la salida, y solo
  el total al dígito lo delata.
- **Escrituras de más de una página**: no aplica al JIT directamente, pero los
  ayudantes van por `memread`/`memwrite` reales — la semántica es la de `mem.h`,
  watchpoints incluidos. Los ayudantes `_fis` existen porque traducir tiene efecto
  colateral (URC): el camino rápido con traducción en línea no puede caer al ayudante
  que traduce de nuevo.
- **Hogares canónicos + costuras**: la SELECCIÓN de qué cachear es codiciosa por uso
  (calidad intra-bloque intacta); la COLOCACIÓN fija r0/r2/r3/r4/r15 (presencias
  74/46/48/46/35 % ponderadas por veces). En un enlace directo, si B⊆A el salto va
  directo a `b->cuerpo` (post-prólogo, costura vacía); si no, prólogo entero.

## Cómo se mide (las reglas de lectura)

- El ciclo es `herramientas/ciclo-jit.ps1` (GEN → entrenar → USE) y la tanda
  `herramientas/ab-jit.ps1` (DCDoom 35 s ×3 rondas, CT 180 s ×2, SR2 60 s ×2);
  **todo cambio de emisión reentrena antes de su tanda**. Dispersión esperable entre
  rondas: 0,1-0,4 %.
- **Los absolutos se comparan dentro de un binario o no se comparan.** La capa de
  reentrenamiento vale ±1-2 % en CT; la de SR2 **era ±3-6 % — la mayor del parque —
  hasta que entró al banco** (2026-08-09: 60 s sin teclas por sus dos formas, entrada
  `soloJit` para no tocar el banco del binario normal). En su primera tanda entrenada
  SR2 igualó su mejor marca histórica (65 429 ms, −8,3 %, 0,915×) sin costarle nada a
  DCDoom (−21,7 %) ni a CT (−14,8 %). DCDoom pesa 7× en el perfil y resuelve 0,1-0,6 %.
- **Una cadena de verificación por máquina**, y `Get-Process dcemu` antes de creerle a
  una tanda. El jitter del mando quieto vale hasta ±1 672 instrucciones en CT con la
  captura intacta (bimodal, no disperso); DCDoom y SR2 son los árbitros inmunes. Los
  dos expedientes, en `notas-herramientas.md`.
- `DCEMU_JIT_PLANTILLAS=N` recorta la tabla a las primeras N: la palanca de bisección,
  que reproduce las cuentas de un juego anterior de plantillas al bit.
- La exactitud es **la mitad que importa**: totales de instrucciones al dígito y
  capturas byte a byte contra el intérprete, en los tres guests, antes de citar
  cualquier tiempo. Los cortes de época/validez hacen que el conteo de fallos de
  búsqueda salga byte-idéntico también.

## Los veredictos medidos (cada uno con su expediente en git)

Lo probado y descartado no se reintenta sin releer su porqué.

| experimento | veredicto | el porqué, en una línea |
| --- | --- | --- |
| El superbloque por flujo (seguir BRA/BSR/RTS) | **neutro** — apagado, `DCEMU_JIT_FLUJO=1` lo revive | entradas −1/−4 % y el tiempo no las siguió: el viaje al despachador no es el costo (otra vez) |
| Rehacer ese veredicto con el enlazado ya arreglado | **se sostiene, y ahora hay un guest que lo rechaza** | DOOM y CT solapados, **SR2 +0,9 % con rangos disjuntos** (tanda `037F3330A41E0FA0`). CT ahorra **68 millones de entradas al despachador (−4,6 %)** y el tiempo no se mueve: la tercera medición que dice lo mismo |
| Por qué había que rehacerlo igual | **la sospecha era legítima y quedó descartada con datos** | cuando se archivó, `jit_enlazar()` era cuadrático en la cantidad de bloques y el flujo cambia esa cantidad; además la verificación por entrada dejó de comparar palabra por palabra, que es lo que más caro le salía a una traza no contigua. Dos motivos independientes, ninguno alcanzó |
| Que el flujo ACORTE los bloques | **el efecto que nadie esperaba** | instrucciones por bloque 22,7→19,8 en DOOM y 26,3→24,0 en SR2: seguir una arista desemboca en código que la traza ya tiene y ahí se corta. En DOOM las entradas SUBEN 3,8 % con el flujo encendido |
| El par de retorno con la cola dentro | **perdió** (SR2 −6,3 % contra −8,3) | la caminata seguía de largo tras el RTS y anexaba la función siguiente: +27 % de arena, el código frío dispersa lo caliente (la lección del tope de 96) |
| El par de retorno terminando la traza | **mixto-marginal, quedó encendido** | DOOM −21,9 y CT −15,6/−16,7 (mejor), SR2 −7,5 (−0,8 pt, dentro de su dispersión); cobertura +2,5/+2,8 pt y entradas −9,6/−12,7 % |
| El lote del censo de la frontera (4 plantillas + pares BRA/JMP/BRAF) | **ganó en los tres, el mayor salto de la serie** | DOOM −25,5 % (1,10×, entradas −50 %), CT −16,7 %, SR2 −9,6 % (92,9 % de cobertura): el censo por peso encontró el residuo interpretado que el flujo no |
| C6xx + pares BSR/JSR/BSRF | **ganó en los tres; encendido** | PR se compromete tras una ranura que no lo toca; los pares solos valen ~1,0/5,5/0,4 % en DOOM/CT/SR2 y llevan las entradas a 48,4/20,1/24,8 instrucciones |
| El atajo de P1/P2 en la traducción emitida | **ganó en los dos guests con MMU; encendido** (`DCEMU_JIT_SIN_ATAJO_P1P2=1`) | esas direcciones no se traducen, así que no son clientela de la caché: su ranura queda sin estrenar y cada acceso repagaba el ayudante. DOOM **−8,1 %**, SR2 **−2,1 %**, rangos disjuntos |
| El atajo detrás del fallo de caché en vez de delante | **perdió** (DOOM −2,8 % contra −8,1) | ahorra el test al que acierta, pero obliga al tercio de accesos de P1/P2 a recorrer índice, etiqueta y cuatro comparaciones antes de salir |
| Índice de `mmu_datos[]` con los bits altos mezclados | **neutro, revertido** | la hipótesis era choque por alias; el censo la mató (64,1 % antes y después). Los fallos no eran de ranura ocupada sino de ranura **nunca escrita** |
| Agrandar la caché de traducciones (64 → 8192) | **no mueve nada** (33,0 % contra 33,2 % de fallos) | la firma de que el problema no era capacidad — con capacidad, el tamaño manda |
| Ciclos de la rama antes de la ranura en un par | **la divergencia de los ±15-19 k** | tr_manejador recarga CYC del contexto: lo sumado tras la sync se evapora; después de la ranura, como el intérprete |
| Llamar al ayudante en cada acceso | **perdió** — el camino rápido se emite en línea | 2,2 ns (~9 ciclos) por acceso, la mitad de la ganancia del bloque |
| Redespacho en el arena por ayudante C | **perdió dos veces** (+2,2/+4,6 % y +0,5/+0,6 %) | las llamadas fallidas superan a los aciertos |
| El buscador (despacho enteramente emitido) | **neutro** aun sirviendo la salida dominante — `DCEMU_JIT_BUSCADOR=1` lo revive | el viaje al despachador C no es el costo; tres mediciones |
| Talón de costura con cargas parciales | **perdió** (CT +0,7 %, rangos disjuntos) — `DCEMU_JIT_COSTURAS=2` | el salto extra y la línea fría de icache superan a 4-5 cargas L1-calientes |
| Tope de instrucciones a 96 | **perdió** (SR2 +9,4 %, más lento que su intérprete) | el largo va a colas frías (entradas −1-3 %) y el código +13-34 % dispersa lo caliente |
| Hogares canónicos puros | **descartado antes de escribirlo** (censo) | perderían 35-45 % del cacheo: r8 pesa 50 % en CT, r5 44 % en DOOM |
| FPU bajo MMU sin FD en la clave | **la divergencia de 633 M** | el 0x800 perezoso de WinCE: run() lo alza antes de tocar nada; el bloque no |
| Corte de época tras escrituras | apagado — `DCEMU_JIT_CORTE_EPOCA=1` | el agujero teórico del orden de búsqueda no se observa en ningún banco y cuesta 6-8 % de cobertura |
| `JIT_MAX_SALIDAS` fijo en 64 | **fósil cazado** — hoy escala con el tope | un bloque con más cortes desbordaba la emisión ENTERA y quedaba interpretado para siempre |
| Parche de desborde del emisor | **así se cayó SR2** — `fijar()` anula el sitio | el productor devolvía uno-más-allá del arena lleno |
| La época global por escritura de SR | reemplazada por MD en la clave | 8,2 M de movimientos «modo» churneaban los enlaces |
| Las dos guardas muertas plegadas (modo del lado MMU, UBC de operando) | **ganó en DCDoom: −1,6 %**, rangos disjuntos; SR2 y CT dentro de su dispersión | el censo las dio en 0,00 % en los tres guests; el UBC se pliega en las tablas base como el watchpoint y el modo tiene respaldo en el vaciado de `mmu_datos`. Emisión −1 132 176 bytes (−3,5 %) |
| La rejilla de 64 bytes consultada en línea antes de desviar una escritura | **ganó en DCDoom: −1,3 % más**, disjunto del anterior (−2,8 % los dos juntos) | la página dice si hay código en 4 KB, no si lo escrito ES código: **90 616 485 desvíos cada 20 s de DCDoom y ninguno hacía falta** |
| Medir los dos juntos y no por separado | **casi cuesta el veredicto** | el combinado dio solapado en DOOM y disjunto en SR2; con los tres brazos DOOM separa las dos mitades y SR2 resulta ser el que no distingue |
| Conectar el gancho de la época moviendo la época por **página** | **no se probó, y menos mal** | habría movido la época 90 millones de veces cada 20 s, desatando todos los enlaces: la página sirve para desviar barato, no para invalidar |
| El barrido lineal de `jit_enlazar()` | **era cuadrático, y era la mayor pérdida de la serie** — `DCEMU_JIT_ENLACE_LINEAL=1` lo revive | avisarle al bloque nuevo quién lo esperaba recorría todos los ya traducidos. Índice por PC destino: **DOOM −9,3 %, CT −14,5 %, SR2 −16,0 %**, los tres con rangos disjuntos (tanda reentrenada `5B9CAAE45A977784`), y los cuadros lentos de **14,9 % a 0,65 %** |
| La clave de la verificación por entrada, separada de la del encadenado | **ganó en los dos guests con MMU** — `DCEMU_JIT_VERIF_COMPLETA=1` lo revive | el puntero de búsqueda ya identifica página, ASID y modo; pedirle además la clave re-comparaba palabra por palabra bloques intactos (20-22 % de las entradas, y en SR2 el 96,7 % acertaba). **DOOM −1,6 %, SR2 −3,2 %**, disjuntos; CT no distingue |
| Poner esa palanca dentro de `jit_verificar()` | **midió la palanca, no el cambio** | esa función corre por entrada — 1490 M de veces en CT —, y CT medía **+1,0 % con rangos disjuntos** con los contadores idénticos en los dos brazos. La palanca se mudó a los movimientos de época, que son miles |
| Reproducir la clave con un contador en vez del propio `jit_validez` | **habría sobreestimado la ganancia** | subir un contador invalida TODOS los bloques en cada cambio de modo; la clave empaquetada sólo invalida los verificados en el otro modo. Con `jit_epoca_escr = jit_validez` la palanca reproduce los números anteriores al dígito |
| La firma que lo delata en la propia tabla | **el costo por traducción crece con el banco** | en el brazo lineal: 0,174 ms en DOOM (35 s), 0,365 en SR2 (60 s), 0,490 en CT (180 s); con el índice, 0,008-0,014 en los tres. Un costo por unidad que depende de cuánto lleve corrido la tanda es cuadrático, se mire lo que se mire |
| La sonda de tirones (`DCEMU_SONDA_CUADROS=1`) | **el instrumento que lo encontró** | una tanda da la media y la media es lo único que un tirón no mueve; la distribución por cuadro con el tiempo **emulado** al lado separa «dcemu se frenó» de «el guest hizo un cuadro largo» |
| Sonda de conservación de URC (`-DDCEMU_SONDA_URC`) | **el instrumento que cerró la caza en 3 corridas** | uc/ue/uv en los puntos de control; conservación con dirección, no hipótesis |
| La fuga de la tabla hash de bloques | **cerrada (2026-08-30): el traductor de SR2 moría a mitad de corrida** — `DCEMU_JIT_TABLA_VIEJA=1` revive la conducta anterior | una inserción sin lugar no volvía al intérprete: se retraducía **en cada visita**, quemando 12 904 ranuras por **9 PCs** hasta chocar el tope de 32 768 — y chocado, no se traduce nunca más, retraducciones por remapeo incluidas. Ver la sección de abajo |

## La fuga de la tabla de bloques (2026-08-30)

Salió del único pendiente con nombre que quedaba en la lista de rendimiento: el censo de los
«sin lugar en la tabla» de SR2. La cuenta que no cerraba: a carga ≤25 % con sondeo lineal
de 8, una falla de inserción al azar es rarísima, y SR2 reportaba 12 904. El censo por PC
(el patrón de `jit_rechazo_sitios`, escrito **antes** de tocar nada) dio el veredicto en una
línea: **12 904 inserciones sobre 9 PCs distintos, y las ranuras de bloque en 32 768 de
32 768** — la tabla entera quemada a mitad de la corrida de 60 s.

El mecanismo, en tres pasos y los tres silenciosos:

1. **La inserción fallida no hacía lo que su comentario prometía.** «No se indexa y su PC
   sigue interpretado» era la intención; la realidad era que el despachador lo **retraducía
   en cada visita** (buscar → NULL → `tr_traducir`, cuyo dedup usa el mismo buscar), quemando
   una ranura de `jit_bloques[]` y arena por visita. Tres PCs cargaban el 82 %
   (0x1176c × 5540, 0x1179c × 2743, 0x117b2 × 2246).
2. **Los cúmulos que hacían fallar el sondeo eran las propias lápidas.** Los 9 PCs eran
   sitios remapeados por WinCE: cada retraducción deja una lápida (`pc == 1`) que nunca sale
   de la tabla, así que el vecindario del hash de un sitio caliente remapeado se llena solo.
3. **Chocado el tope de 32 768, `tr_traducir` devuelve NULL para siempre** — ninguna
   traducción nueva, las retraducciones por remapeo incluidas: el «interpretado PARA SIEMPRE»
   del expediente de DOOM, de vuelta por otra puerta, y sin un contador que lo dijera.

El arreglo son tres piezas de un solo mecanismo (por eso una palanca): el **sondeo a 32**
(la falla al azar pasa a ~0,25³², extinta; costo cero en régimen — el lazo sale en el primer
vacío o en el match), el **reuso de lápidas** al insertar (el reuso clásico de tumbas:
buscar las trata como ocupadas-que-no-matchean, así que pisarlas no corta ninguna cadena — y
corta de raíz el crecimiento de los cúmulos), y el **desmarcar al fallar** (si alguna vez
vuelve a pasar, el PC va al intérprete de verdad, una ranura y no una fuga). El tope de
bloques ahora **avisa y se cuenta** al chocarse, como el tope de pistas del lector. La
emisión no cambia con nada de esto — es puro lado anfitrión — así que el A/B corre entero
sobre un binario.

Con el arreglo, SR2 queda en **0 sin lugar y 29 073 ranuras de 32 768**, con el traductor
vivo la corrida entera y ~9 200 bloques más traducidos (los que la muerte del traductor
dejaba interpretados). DOOM y CT tenían 0 sin lugar y van de testigos.

**La compuerta, y la lección del pad que costó tres corridas extra.** SR2 y DOOM salieron
byte a byte en captura y en **todos** los puntos de `DCEMU_CP_MS` (60 000 y 35 000; binario
final `FAE1FE0AA6A5AC94`). CT salió con la captura idéntica y los cp divergiendo **un ciclo
de `reloj_total`** desde los 115 s, registros intactos — y reproducible: tres corridas del
brazo nuevo idénticas entre sí, tres del viejo idénticas entre sí. Una divergencia
determinista y correlacionada con el brazo, en el único guest donde los brazos son
demostrablemente el mismo código (0 lápidas, 0 colisiones: la tabla queda idéntica). La
resolución fue el **replay de mando**: grabada una receta y reproducida bajo los dos brazos,
**180 000 puntos idénticos** — la divergencia era el pad por XInput (CT ramifica por los
análogos), y la correlación con el brazo, coincidencia del momento de cada corrida. La
lección: «reproducible» no basta como descarte del pad — el jitter es bimodal y puede
correlacionar por accidente con lo que se está midiendo; el descarte de verdad es el replay,
que reemplaza la entrada entera.

**La tanda (4 rondas alternadas, un binario): neutra, y eso es el veredicto esperado.**
SR2 solapado (nueva 46 189-46 596 ms contra vieja 44 614-46 217 — nueva traduce ~9 200
bloques más y son fríos a 60 s), DOOM al dígito, CT ilegible (el ambiente se degradó en las
últimas rondas). Esto **no es una optimización: es el cierre de un modo de falla** que el
banco de 60 s apenas roza — el tope se chocaba al final de la ventana — y que una sesión de
juego real paga entero, con el traductor muerto y cada remapeo posterior cayendo al
intérprete para siempre.

Los residuos, nombrados: (a) SR2 destapó **12 019 rechazos con el tope de retraducción**
(16 por PC) ahora que el traductor vive para verlos — sitios cuyo contenido alterna más de
16 veces, el caso que el expediente de la retraducción llamó «variantes por ASID» y cuyo
censo ya corre solo; 0,005 % de las entradas (44 213 a 180 s). (b) Las ranuras de bloque de
las lápidas no se reusan (~443-579 por corrida de SR2, menor; el aviso del tope diría si
crece). (c) ~~El tope de 32 768~~ — **saldado el mismo día, porque el sondeo de 180 s lo
convirtió en defecto medido**: SR2 llenaba las 32 768 ranuras legítimamente (9577 propuestas
rechazadas con la tabla llena, y una lápida que no puede renacer pierde el bloque entero).
El tope pasó a **65 536** con la tabla hash de ints a 18 bits (1 MB, misma carga del 25 %) y
el arena a 256 MB; `DCEMU_JIT_BLOQUES=N` recorta el tope en runtime y `=32768` es el brazo
del A/B. A 180 s SR2 queda en 39 406 ranuras con el traductor vivo (128,7 MB de arena), y la
tanda sobre el canónico reentrenado (`25B7366C16CED290`) dio **SR2 −1,4 % a 180 s con rangos
disjuntos y 4/4 pares** — la ganancia es entera del tramo donde el tope viejo mataba al
traductor, que es por qué el banco de 60 s no la veía. Compuerta verde (capturas y cp
completos, SR2 180 s y DOOM). El mismo censo de la caché de entrada de la MMU de esa noche
está en `docs/mmu-plan.md` («La etiqueta sin modo», DOOM −3,0 % disjunto).

## El gancho que nunca estuvo conectado (2026-08-14)

Es el hallazgo de la sesión y no es de rendimiento: **la época no se movía nunca
por escritura**, en ninguna corrida, desde que el traductor existe.

El diseño era correcto y está escrito en `jit.h`: un guest que escribe sobre
código ya traducido tiene que invalidarlo, el camino rápido emitido no pasa por
`memwrite()` —por eso mira un mapa de páginas y desvía al ayudante si la página
tiene código—, y el ayudante escribe por `memwrite()`, **que mueve la época**.
La última mitad nunca se escribió: `JIT_ESCRITURA()` quedó definido y sin un
solo llamador. `grep` sobre el árbol entero da la definición y nada más, y el
resumen de cada corrida lo venía diciendo en voz alta — `0 escritura` — sin que
nadie leyera ese 0 como lo que era.

Lo que sostenía la corrección mientras tanto era la verificación palabra por
palabra de `jit_verificar()`, que corre cada vez que la clave de validez cambia;
y en DCDoom cambia sin parar (4,2 M de transiciones de modo cada 20 s). En Crazy
Taxi, con **cero** rechazos por verificación, no la sostenía nada.

**Y hay clientes.** Con el gancho conectado, Sega Rally 2 mueve la época
**18 876 veces** cada 20 s emulados y Crazy Taxi **25 743**: los dos escriben
sobre su propio código traducido. Que las tandas salieran exactas igual no
absuelve al agujero — dice que no se cobró en la ventana medida.

**La página no alcanza para invalidar, y ese es el motivo de la rejilla.** En
Windows CE los datos viven en las mismas páginas de 4 KB que el código: de las
90 616 485 escrituras de DCDoom que caen en una página con código cada 20 s,
**ninguna** toca una línea de 64 bytes que lo tenga. Conectar el gancho a secas
habría movido la época noventa millones de veces. Así que hay dos rejillas y
cada una hace lo suyo: la de páginas —4096 entradas útiles, en L1— decide barato
si hay que preguntar, y la de 64 bytes —256 KB, tocada sólo detrás de la otra—
decide de verdad. La fina se consulta **en línea en el código emitido**, que es
lo que convierte los noventa millones de llamadas al ayudante en noventa
millones de cuatro instrucciones.

**Lo que cuesta y lo que se gana**, con los tres brazos (`herramientas/rejilla-ab.ps1`,
binario `4800AC12E8BF0C6C`, un calentamiento **por guest** — el caché frío de la
imagen se paga por guest, no por tanda, y eso fue lo que dejó solapado el par de
Crazy Taxi en la primera tanda): en DCDoom el pliegue de guardas vale **−1,6 %**
y la rejilla **−1,3 % más**, con los tres rangos disjuntos y **−2,8 % juntos**;
Sega Rally 2 queda dentro de su dispersión (63 620–65 317 ms en un mismo brazo)
y Crazy Taxi solapado. Que gane justo DCDoom es lo que el censo predecía: es el
guest con el 8,4 % de accesos sobre página con código y el único con la guarda
de modo del lado MMU.

**Y la lección de medición está en cómo se leyó primero.** La tanda combinada
—las dos palancas a la vez— dio DOOM solapado y SR2 disjunto en −0,8 %, o sea
exactamente al revés de lo que es. Un combinado neutro puede ser dos efectos que
se cancelan, o uno real escondido bajo el ruido del otro guest; sin el tercer
brazo no hay forma de saber cuál. El −0,8 % de SR2 no se reprodujo.

Tres cosas quedaron por construcción, no por suerte: la rejilla fina se marca
**entera** por bloque (hasta cuatro líneas: la cabeza y la cola dejarían el
medio abierto), el acceso del guest va alineado y por eso no cruza el límite de
64 —el error de dirección lo filtra antes— pero la versión en C mira cabeza y
cola porque las escrituras internas copian bloques, y `jit_ep_pag_vista` se
imprime siempre: sin ese contador, «0 movimientos por escritura» no distingue
«el guest no escribe su código» de «el gancho no está conectado», que es
exactamente la confusión que duró toda la vida del traductor.

## Dónde está el tiempo (rehecho tras las fases 4 y 5, 2026-08-13)

Binario `D06C1A67C6F9E863`, mismo método (`perfil-jit.ps1`). Los porcentajes
descuentan el AICA, que corre anidado en el bloque periódico.

| | DCDoom 35 s | CT 180 s | SR2 60 s |
| --- | --- | --- | --- |
| resto (emitido + despacho + intérprete + MMU) | **78,0 %** | **63,1 %** | **81,0 %** |
| AICA — ARM7 | 10,1 % | **19,9 %** | 8,4 % |
| AICA — mezclador | 4,1 % | 7,2 % | 3,1 % |
| bloque periódico neto | 7,1 % | 1,7 % | 4,5 % |
| GL entero | 0,7 % | 6,2 % | 2,0 % |
| cruces de enlace | 66,8 % | 66,0 % | 60,2 % |
| instrucciones por entrada al despachador | 48,4 | 20,1 | 24,8 |

Lo que cambió contra el reparto de la fase 0: **los cruces de enlace pasaron de
36-42 % a 60-67 %** — las cadenas se alargaron con los pares de rama, que es lo
que se buscaba — y el bloque periódico neto de CT cayó a 1,7 %. El SH-4 sigue
siendo el 63-81 %, así que la fase siguiente sigue apuntando adentro de él; el
ARM7 de CT sigue siendo la segunda porción con 19,9 % aun después de sus tres
escalones.

## Dónde está el tiempo (fase 0 del plan del estado del arte, 2026-08-09)

El reparto del binario del JIT con `DCEMU_JIT=2` (`herramientas/perfil-jit.ps1`:
`--perf` en una corrida, la sonda de cruces en otra; los porcentajes descuentan el
AICA, que corre anidado en el bloque periódico). Es lo que ordena las fases grandes
de `estado-del-arte-plan.md`:

| | DCDoom 35 s | CT 180 s | SR2 60 s |
| --- | --- | --- | --- |
| resto (emitido + despacho + intérprete + MMU) | **77,1 %** | **60,5 %** | **79,4 %** |
| AICA — ARM7 | 10,6 % | **18,2 %** | 9,0 % |
| AICA — mezclador | 4,0 % | 7,4 % | 2,9 % |
| bloque periódico neto | 7,4 % | 5,0 % | 5,6 % |
| GL entero | 0,8 % | 6,8 % | 2,1 % |
| cruces de enlace / fronteras | 41,7 % | 36,1 % | 41,9 % |
| instrucciones por bloque **corrido** | 11,7 | 7,8 | 9,0 |
| instrucciones por entrada al despachador | 20,2 | 12,1 | 15,5 |

Lo que dice: **el SH-4 sigue siendo el 60-79 %** (superbloque primero), **el ARM7 es
la segunda porción y creció al achicarse el SH-4** (en CT ya es 18,2 %, con techo
medido de 1,34× si se fuera entero — va antes que el reloj), y **el bloque periódico
neto quedó en 5-7 %** (el grano 400 ya se llevó lo grande; el reloj por eventos vale
eso más las cadenas más largas, no más). Dos avisos: las **salidas con enlaces
agotados** existen (96/1171/145 — el tope de 12 va a quedar corto con trazas), y el
mezclador del AICA ya pesa 2,9-7,4 %.

## El censo del contrato (2026-09-02)

La lista de optimizaciones "con techo defendible" se declaro agotada dos veces, y
las dos veces la reabrio un censo. Este es el tercero, y mide algo que ninguno de
los anteriores miraba: **el contrato por instruccion**, o sea lo que el traductor
paga en cada fila del guest ademas del trabajo de la fila.

El instrumento es nuevo (`DCEMU_FORMA` + `--perf`, con `DCEMU_JIT=0` para que
todas las instrucciones pasen por el gancho del interprete): un histograma por
codificacion, agrupado en `jit.c` **por la misma tabla de plantillas que usa el
traductor** -- sin segundo decodificador --, mas el largo de las corridas de
filas directas contadas al EJECUTAR. Y un contador de SLEEP, ese si en el binario
normal.

| clase | DOOM | SR2 | CT |
| --- | --- | --- | --- |
| acceso en linea | **36,55 %** | **39,38 %** | **41,24 %** |
| directa (elegible para un tramo) | 44,95 % | 38,83 % | 34,77 % |
| rama | 10,76 % | 15,37 % | 16,99 % |
| por manejador C | **6,95 %** | 2,90 % | 1,58 % |
| FPU por envoltorio | 0,15 % | 3,07 % | **5,00 %** |
| terminal | 0,44 % | 0,30 % | 0,31 % |
| sin plantilla | 0,20 % | 0,11 % | 0,07 % |
| corrida de filas directas | 1,48 | 1,38 | 1,41 |
| ... de largo 1 | 78,1 % | 75,5 % | 73,0 % |
| SLEEP ejecutados | **0** | **0** | **0** |

Lo que el censo decide, con las reglas escritas antes de mirar:

 - **El avance de SLEEP queda descartado: cero en los tres.** La regla pedia
   2 %. Y el cero esta probado, no supuesto: `tests/test_syscontrol.c` tiene el
   caso que verifica que el contador sube cuando la instruccion corre, porque un
   cero sin control positivo no se distingue de una sonda muerta -- la leccion
   del gancho de epoca que nunca estuvo conectado. **Windows CE no espera con
   SLEEP**; sus bloques mas pesados son lazos de trabajo de 50-80 instrucciones.
 - **Los accesos son la clase mas grande, 36-41 %**, y hoy cada uno paga diez
   instrucciones de sincronizacion **en el camino rapido**, que no puede faltar.
   Es el blanco mayor y el mas barato. El argumento no es el costo de diez
   almacenes --la leccion del servicio partido dice que el volumen predecible no
   compra tiempo-- sino los **bytes**: son ~40 de los ~92 (CT) a ~118 (MMU) que
   se emiten por instruccion, y la presion de icache es lo que pago -7,2 y
   -11,1 % en las rutinas compartidas de la traduccion.
 - **El tramo recto rinde la mitad de lo estimado y baja de prioridad.** Las
   corridas de filas directas son de 1,38-1,48 instrucciones y **tres de cada
   cuatro son de una sola**: un tramo degenera en cambiar seis instrucciones por
   cuatro, no en amortizar el corte entre varias. Sigue siendo positivo (~1,2-1,5
   por instruccion) pero es la fase mas cara de escribir, asi que va despues.
 - **Aparece un blanco que el plan no tenia: las filas por manejador C.** En
   DOOM son el 6,95 % de las instrucciones ejecutadas, y **DIV1 sola es el
   4,91 %** (mas ROTCL al 5,06 %, que si se emite): el guest hace division por
   software y cada paso cuesta sincronizacion completa, llamada, recarga de CYC
   y recarga de las cinco ranuras. Es trabajo de plantillas, conocido y de
   riesgo bajo.
 - **La FPU por envoltorio confirma su clientela**: CT 5,00 % y SR2 3,07 %,
   DOOM 0,15 %. Cada una es una llamada a C que la emision en SSE quitaria.
 - **La cobertura ya no es un problema**: "sin plantilla" es 0,07-0,20 %.

Y el censo confirma el otro cliente, el que decide la elision de ociosos:
**el lazo de espera de Crazy Taxi son el 47,22 % de sus instrucciones**
(`0c158400` 26,24 %, `0c158418` 10,49 %, `0c1583f8` 7,87 % y el retorno
`0c156c30` 2,62 %, los cuatro con 535,8 millones de vueltas). Es el mismo 47,2 %
que midio la fase 4 de `rendimiento-plan-2.md` en su dia, ahora bajo el banco
vigente. DOOM y SR2 no tienen nada parecido, asi que el mecanismo es de Katana y
su compuerta tiene que probar que los otros dos quedan inertes.

## La sincronizacion en el talon lento (2026-09-03)

**El censo del contrato dijo que los accesos son la clase mas grande -- 36,55 %
de DOOM, 39,38 % de SR2, 41,24 % de CT -- y cada uno pagaba diez instrucciones
de volcado en el camino rapido.** Eso es lo que hace `tr_sync`: las cinco
ranuras al contexto, CYC, el PC como inmediato, el contador y su volcado. Estaba
delante de la plantilla, o sea que lo ejecutaba **el acceso que no falta**, que
son todos menos una fraccion minuscula.

El camino rapido emitido **no puede faltar**, y eso ya estaba probado por partes:
la alineacion se verifica antes (es el error de direccion, una funcion), el modo
se resuelve al emitir, la traduccion acertada devuelve fisica, la zona con base
directa solo existe si no hay watchpoint ni break de operando --los dos plegados
en `mem_directo_recalcular()`-- y la pagina con codigo desvia. Lo unico que puede
salir por `longjmp` es el ayudante. Asi que la instantanea hace falta **antes de
la llamada**, no antes del acceso.

Lo que la hace exacta sin listas que mantener: **toda salida a C pasa por
`gen_llamar()` o por `tr_manejador()`**, y las dos emiten la sincronizacion
pendiente justo antes de la llamada. El conductor la ARMA (no la emite) y al
terminar la fila comprueba que alguien la haya usado; si no, descarta el bloque
y lo cuenta (`jit_sync_sin_consumir`, que se imprime siempre y tiene que ser
cero). Una fila con acceso que no llegara a ningun sitio de llamada seria una que
puede faltar sin instantanea, y ese es el unico modo de falla del cambio.

Dos cosas que la primera version rompio y que quedan como reglas:

 - **El contador de instrucciones vivia adentro de la sincronizacion.** Al
   moverla al talon, el camino rapido dejo de contar: 1 802 498 699 contra
   2 834 974 481 en veinte segundos de DOOM. El arreglo es el orden del
   interprete: **contar el intento ANTES de intentarlo** -- un `inc` de un byte
   delante de la plantilla, que es lo unico del volcado que se queda en el camino
   rapido -- y que el talon solo lo vuelque. Asi una falta sale con el intento
   contado y un retorno normal no lo cuenta dos veces.
 - **Bajo MMU la emision CRECE**, porque un acceso abre dos talones --el fisico y
   el virtual-- y los dos llevan la sincronizacion: DOOM pasa de 39 441 375 a
   44 915 415 bytes (+13,9 %) en la misma corrida. En modo plano hay un solo
   talon y el cambio es neutro en bytes. Aun asi la primera lectura da 4,3 contra
   4,5 ns por instruccion: lo que se ahorra en instrucciones ejecutadas pesa mas
   que lo que se paga en bytes. El talon de sincronizacion **por bloque** --un
   `mov` del PC y un `call` de cinco bytes por talon, contra los cuarenta del
   volcado entero-- es el paso siguiente y esta disenado.

Palanca: `DCEMU_JIT_SYNC_PREVIA=1` vuelve la sincronizacion delante de la
plantilla y reproduce la emision anterior. Compuerta: `herramientas/sync-gate.ps1`
(tres brazos por guest contra el **interprete**, que es el arbitro correcto aqui:
lo que se movio es donde se vuelca el estado antes de una falta).

## El corte en una comparacion (2026-09-03)

El corte del bloque periodico emitido reproduce la condicion de `main_loop` --
`cycles >= RELOJ_GRANO || intc_sh4_reintentar` -- y la emitia tal cual: dos
comparaciones y dos saltos, **en cada frontera de instruccion con ciclos**, o sea
en el 90 % de las instrucciones segun el censo del contrato. Y uno de los dos
saltos era TOMADO en el camino comun, para saltear el talon de salida que quedaba
en medio del codigo caliente.

Ahora es **una sola comparacion contra un limite envenenable**: `intc_corte_limite`
(intc.h) vale `RELOJ_GRANO` normalmente y **cero mientras el reintento este
armado**, asi que `CYC >= limite` es cierta exactamente cuando lo era la
disyuncion. Dos instrucciones en vez de cuatro, sin cambiar la semantica ni la
frontera donde se corta.

Lo que lo hace sano es que las dos cosas se mueven JUNTAS. Los cinco sitios que
arman el reintento --`tmu.c` al poner UNF, `wdt.c` al poner IOVF, `dma_canal()` al
poner TE, y las dos entradas de `UpdateSR`-- pasan por `INTC_PEDIR_REINTENTO()`, y
el bloque periodico limpia con `INTC_LIMPIAR_REINTENTO()`. Un sitio que armara uno
sin el otro dejaria al traductor sin cortar donde el interprete corta, que es una
divergencia silenciosa y tardia: por eso el bloque periodico **comprueba la
coherencia una vez por servicio** y `intc_corte_incoherente` va en el resumen, sin
condicion y en cero.

Palanca: `DCEMU_JIT_CORTE_VIEJO=1` vuelve a las dos comparaciones, byte por byte.

### Dos trampas de medicion que esta fase destapo

Ninguna de las dos es del traductor, y las dos hacen que una compuerta salga
verde sin haber probado nada:

 - **El resumen `jit:` no llegaba a `stderr.txt` salvo con `--perf`.**
   `jit_resumen()` estaba registrado con `atexit()` y corria despues de que SDL
   cerrara la redireccion. Las compuertas corren SIN `--perf`, asi que todos sus
   contadores --incluidos los de control que esta fase y la anterior agregan--
   eran invisibles justo donde hacen falta. Ahora lo llama `main.c` en la
   secuencia de salida, al lado de `arm7jit_resumen()`.
 - **Vaciar una variable de ambiente no es borrarla.** En PowerShell 7,
   `[Environment]::SetEnvironmentVariable($v, $null)` **deja la variable vacia**,
   y `getenv()` la devuelve NO nula: para `DCEMU_JIT`, `atoi("")` es 0, o sea el
   valor de la palanca de aislamiento. La primera version de la compuerta de la
   fase anterior corrio **los tres brazos con el interprete** y salio verde en
   los tres guests sin haber ejercitado una sola instruccion emitida. La forma
   correcta es `Remove-Item "Env:NOMBRE"`, y el control barato que la caza es que
   cada brazo imprima una linea que solo existe si el traductor corrio.

### La tanda del contrato (canonico `668F6E3E434EE8A0`, PGO reentrenado)

Cuatro brazos sobre un binario, que es lo que hace falta para leer dos palancas
--un combinado neutro puede ser dos efectos que se cancelan--: base (las dos
viejas), sync (solo el talon), corte (solo una comparacion) y ambas (la
omision). Calentamiento por guest descartado, orden rotado entre rondas.

| guest | base | sync | corte | **ambas** |
| --- | --- | --- | --- | --- |
| DCDoom 35 s | 22 542 ms | 22 278 (-1,17 %, 3/4) | 22 302 (-1,06 %, 3/4) | **21 850 (-3,07 %, disjunto 4/4)** |
| Sega Rally 2 60 s | 45 120 ms | 44 514 (-1,34 %, 3/4) | 44 481 (-1,42 %, 4/4) | **44 056 (-2,36 %, disjunto 4/4)** |
| Crazy Taxi 180 s | 70 160 ms | **67 574 (-3,68 %, disjunto)** | **69 011 (-1,64 %, disjunto)** | **65 526 (-6,60 %, disjunto 4/4)** |

Marcas: **DOOM 1,55 -> 1,60x, SR2 1,33 -> 1,36x, CT 2,57 -> 2,75x**.

Tres lecturas:

 - **El par se separa limpio en los tres guests**, y en Crazy Taxi cada palanca
   se separa incluso sola. Que CT sea el que mas gana era predecible por el
   censo: es el que mas accesos ejecuta (41,24 %) y, por ser plano, el unico
   donde mover la sincronizacion al talon **no cuesta bytes** -- tiene un solo
   talon por acceso, no dos.
 - **En DOOM y SR2 cada palanca sola queda en el estandar debil** (3/4 con
   solape) y solo el par cruza a rangos disjuntos. Es coherente con que la
   emision crezca 13,9 % bajo MMU: parte de lo que la sincronizacion ahorra en
   instrucciones ejecutadas se paga en bytes, y el talon de sincronizacion por
   bloque --un `mov` del PC y un `call` de cinco bytes contra los cuarenta del
   volcado-- es lo que queda por cobrar ahi.
 - **La tanda es tambien una prueba de exactitud**: las 17 corridas de DOOM y
   las 16 de SR2 dan el total de instrucciones y las entradas **al digito**. CT
   muestra tres valores distintos que **no correlacionan con el brazo** (cada
   brazo ve mas de uno) -- es su bimodalidad de mando conocida, y su compuerta
   ya habia salido byte a byte bajo replay.

## DIV1 emitida sin ramas (2026-09-03)

El censo del contrato destapo un blanco que ningun plan tenia: **DIV1 sola es el
4,91 % de las instrucciones ejecutadas de DCDoom** (con ROTCL al 5,06 % al lado,
que si se emitia). El guest divide por software, y cada paso pagaba
sincronizacion, llamada al manejador, recarga de CYC, recarga de las cinco
ranuras -- y adentro, un manejador con dos switch anidados.

**Las ramas eran el problema, no la llamada.** Los switch de `div1s52()` miran Q
y M: M es fijo durante una division, pero Q alterna con los bits del cociente,
asi que emitir el switch tal cual habria cambiado una llamada por una prediccion
fallada de cada dos.

**La forma cerrada** que permite emitirlo recto -- y que se probo ANTES de
escribir el emisor:

    qs   = bit 31 de Rn, antes del corrimiento
    tmp0 = (Rn << 1) | T
    se RESTA si (Q == M), y se suma si no
    tmp1 = el prestamo de la resta, o el acarreo de la suma
    Q'   = qs ^ tmp1 ^ M
    T'   = (Q' == M)

Los cuatro casos del manual colapsan en esas dos lineas. `tests/test_arith.c`
(`div1_la_forma_cerrada_coincide`) la compara contra el manejador real sobre
4096 estados al azar mas los ocho bordes, y **paso a la primera**: el riesgo
algebraico quedo cerrado sin depender de que el emisor estuviera compilado, que
es lo que este arbol pide de una plantilla nueva.

Dos trucos la vuelven recta sin `cmov`, que el emisor no tiene: el operando se
niega con mascara -- `b' = (b ^ (sel-1)) - (sel-1)`, que da `b` cuando hay que
sumar y `-b` cuando hay que restar -- asi que **siempre se suma**; y el acarreo
se corrige con `tmp1 = CF ^ !sel`. Eso vale para todo `b` salvo **cero**, donde
acarreo y prestamo dejan de ser complementarios: de ahi el `and` final contra
`(b != 0)`, que es el unico caso que la forma cerrada no cubre sola y el que la
prueba fuerza a mano.

La fila cambia de forma, no solo de emision: pasa de `{ciclos 0, accede 1}` --lo
que necesita una fila por manejador, porque el manejador suma sus ciclos por
dentro-- a `{ciclos 1, accede 0}`. Por eso la palanca `DCEMU_JIT_SIN_DIV1=1` no
cambia solo el emisor: `jit_iniciar()` devuelve **la fila entera** a su forma
anterior, o los ciclos se contarian dos veces.

Compuerta verde en los tres guests (capturas byte a byte y 155 000 puntos de
control contra el interprete), con el control diciendo en cada brazo si DIV1
salio emitida o por manejador. DCDoom ejecuta 267 millones de DIV1 en esos 35
segundos: si la forma cerrada tuviera un caso mal, no habria por donde
esconderse.

### Y perdio la tanda: **+3,6 %, revertida** (canonico `E1E565F833F4738B`)

| brazo | ms (4 rondas) |
| --- | --- |
| emitida sin ramas | 22 440 / 22 463 / 22 498 / 22 564 |
| por manejador | 21 698 / 21 752 / 21 683 / 21 686 |

Rangos disjuntos, 4 de 4, con el total de instrucciones al digito en las nueve
corridas. **La emision es 3,6 % mas lenta que la llamada.**

Y el arena descarta la explicacion facil: **49 474 303 bytes contra 49 239 871,
o sea +0,48 %**. No son los bytes ni la presion de icache. Lo que queda es lo
unico que cambio de verdad: **las ramas del manejador se predicen bien** --M es
fijo durante una division y el patron de Q lo aprende el predictor-- y la
version sin ramas las cambia por una cadena de dependencias de quince pasos, con
dos `setcc`/`movzx` y un lee-modifica-escribe de SR al final.

Es **la leccion del cuerpo rapido del DSP, ahora en el SH-4**: quitar ramas que
ya se predicen no compra nada, y la aritmetica sin ramas que las reemplaza se
paga en latencia. Con una diferencia que vale anotar: alli el candidato era
saltear trabajo, aca era saltear una LLAMADA, y tampoco alcanzo.

Queda apagada y con palanca (`DCEMU_JIT_DIV1_EMITIDA=1`) porque la forma cerrada
esta probada y porque la variante que **no** se midio --emitir el switch tal
cual, con sus ramas, para ahorrar solo la llamada y las cinco recargas-- tiene
aca la mitad del trabajo hecha. El censo la sigue senalando: 4,91 % de DCDoom.

## El talon de sincronizacion por bloque: neutro en tiempo, -15 % de emision (2026-09-03)

La tanda del contrato dejo una pista: en Crazy Taxi las dos palancas se separan
solas, y en los dos guests con MMU solo el par cruza a rangos disjuntos. La
diferencia entre unos y otro es que **bajo MMU cada acceso abre DOS talones** --el
fisico y el virtual-- y los dos llevaban el volcado entero, asi que la emision
crecia 13,9 %.

El volcado es el mismo para todos los accesos de un bloque: las ranuras que ese
bloque mapea, mas CYC. Lo unico propio de la instruccion es el PC. Asi que se
emite **una vez por bloque**, al final --inalcanzable por caida, porque el
epilogo termina en un salto-- y cada talon queda en el `mov` del PC y cinco bytes
de `call`. El `call` es seguro adentro de un bloque aunque el arena tenga una
sola informacion de desenrollado: el talon no llama a nadie ni toca la pila mas
alla del retorno, y nada puede faltar mientras esta corrido.

**El efecto en la emision es grande y el efecto en el tiempo es nulo:**

| | arena con talon | arena entero | contra la linea base previa a la fase |
| --- | --- | --- | --- |
| DCDoom 35 s | 41 651 935 | 49 239 871 (**-15,4 %**) | 39 441 375 (+5,6 %) |
| Sega Rally 2 60 s | 81 585 480 | 96 788 863 (**-15,7 %**) | 95 850 022 (**-14,9 %**) |

| tanda (canonico `E7DB8F4BF1DFF538`) | con talon | sin |
| --- | --- | --- |
| DCDoom | 21 722 / 21 581 / 21 982 / 22 001 | 21 848 / 21 748 / 21 633 / 21 831 |
| Sega Rally 2 | 44 746 / 43 281 / 43 433 / 43 516 | 43 353 / 43 297 / 43 200 / 43 333 |

DOOM 2 de 4 y solapado; SR2 1 de 4. **Neutro**, y eso mismo es el hallazgo: los
talones son codigo frio, sus bytes no se buscan nunca, y quince por ciento menos
de emision no se nota en el reloj. La presion de icache que pago -7,2 y -11,1 %
en las rutinas compartidas de la traduccion era de bytes **en el camino
caliente**; estos no lo son.

**Queda encendido igual, y no como optimizacion sino como capacidad.** Este arbol
ya destapo dos topes silenciosos --el arena de 192 MB y la tabla de 32 768
bloques, los dos chocados por SR2 a 180 s-- y con el talon SR2 emite menos de lo
que emitia antes de esta fase, no mas. `DCEMU_JIT_SYNC_EN_CADA_TALON=1` es el
brazo del A/B.

## El pliegue de las direcciones constantes: CT -2,22 % (2026-09-03)

El censo del contrato tenia un blanco mas, y era el segundo mas pesado de Crazy
Taxi: **`MOV.L @(d,PC),Rn` es el 13,87 % de sus instrucciones ejecutadas**
(DCDoom 6,21 %, Sega Rally 2 6,82 % sumando las dos anchuras). Es el literal de
PC-relativo, y su direccion **esta decidida al traducir**: `(pc & ~3) + 4 +
disp*4`.

Eso ya se aprovechaba a medias --la direccion se cargaba como inmediato-- pero el
camino hasta el dato seguia siendo el generico: probar la alineacion, sacar el
byte alto, indexar la tabla de zonas, enmascarar el desplazamiento. Con la
direccion conocida, **tres de esas cuatro cosas son constantes**:

 - la **alineacion** no puede fallar: la formula del literal ya alinea, asi que
   la prueba no se emite (y si alguna vez llegara una constante desalineada, se
   emite igual y el ayudante levanta el error de direccion por el camino de
   siempre);
 - el **indice de zona** se pliega en el desplazamiento de la carga de la tabla;
 - el **desplazamiento dentro de la zona** es un inmediato.

Lo unico que sigue mirando el estado es la prueba de base nula, y tiene que
seguir: la tabla cambia cuando se arma un watchpoint o un break de operando.
El valor, por supuesto, se sigue leyendo -- el literal es dato.

**Tanda sobre el canonico reentrenado `3E7AF60F89EE8F1A`:**

| guest | con pliegue | sin |
| --- | --- | --- |
| Crazy Taxi 180 s | 64 423 / 64 067 / 63 935 / 64 671 | 65 434 / 65 234 / 64 971 / 67 298 |
| DCDoom 35 s | 21 926 / 21 729 / 21 685 / 22 630 | 21 968 / 21 898 / 22 016 / 21 915 |

**Crazy Taxi -2,22 %, rangos disjuntos y 4 de 4.** DCDoom neutro, y por
construccion: el pliegue de zona vale **solo en modo plano**, porque bajo MMU la
fisica sale de la rutina de traduccion y no es constante. A los guests con MMU
les queda la elision de la guarda de alineacion, que son dos instrucciones sobre
el 6 % de sus filas -- bajo el ruido.

Marca de CT: **2,80x**. Palanca `DCEMU_JIT_SIN_DIR_CONSTANTE=1`; compuerta verde
en los tres guests.

## Lo pendiente, en orden

(El mini-lote C6xx + pares de llamada se cerró el 2026-08-10, y el atajo de
P1/P2 el 2026-08-14; lo encendido por omisión son todos los pares de rama, las
122 plantillas y el atajo.)

0. **Lo que queda del censo de accesos.** Las dos guardas muertas y la página
   con código se cerraron el 2026-08-14 (ver «El gancho que nunca estuvo
   conectado», abajo) y el camino rápido de DCDoom pasó de **88,2 % a 96,6 %**,
   con «página con código» en **0,00 %**. Lo que queda, en orden: **zona no
   plana 1,78 %**, **etiqueta de la caché de traducciones 1,24 %** y permiso
   0,34 %. La zona no plana es el único sitio donde fastmem tendría algo que
   hacer, y ese 1,78 % es su techo medido. Y la guarda de alineación **tampoco
   se dispara jamás** pero no se puede plegar: es la comprobación del error de
   dirección, o sea una función y no una optimización.
1. **La época por página** (en vez de global): **la mitad barata de esto ya se
   cobró** separando la clave de la verificación por entrada de la del encadenado
   (arriba), que se llevó el 96 % de las palabras comparadas. Lo que queda son los
   rechazos de verdad — 8,2 M en DCDoom, donde el bloque SÍ dejó de valer porque su
   página se remapeó — y para esos una generación por página de guest dejaría en
   pie lo que no se movió. Medir antes de escribirla: con el camino largo en 0,9 %
   de las entradas, el techo es mucho menor que cuando se anotó este punto.
2. **Elisión de recarga en reentradas por despachador**: los no volátiles sobreviven
   el viaje C; falta la marca de «contexto ensuciado». Con las entradas de DOOM a la
   mitad, su techo bajó — medir antes de escribirla.
3. **La fase 3 original: el parque entero** — el barrido de 135 demos y los 14 juegos
   con `DCEMU_JIT=2` contra su corrida de control, que es lo que decide la adopción
   por omisión (hoy el JIT es build aparte a propósito: el A/B corre sobre una sola
   imagen).

Las fases que exceden al recompilador — ARM7, reloj por eventos, fastmem, elisión de
ociosos — viven en `docs/estado-del-arte-plan.md`, con este reparto como su fase 0.

## La expectativa original, saldada

El plan proyectaba DCDoom ~1,5× (llegó a 1,078×; el 91 % de cobertura del top-20 no
compró el factor por porción del bloque único — los bloques no son todos iguales, como
el propio plan advertía), CT ~2,3× («no es objetivo; no debe empeorar» — mejoró 31 %,
de 134,6 a 92,3 s), y SR2 ≥1,0× en fase 2 (va 0,92×, con la compuerta resuelta y su
capa de PGO pendiente). La regla que el plan sí clavó: ninguna cifra de velocidad antes
de que el trabajo salga idéntico al dígito — y esa disciplina es la que encontró cada
uno de los bugs de la tabla de veredictos.

## Lo que sigue: la elision de ociosos, disenada y sin escribir

El censo del contrato dejo el blanco mas grande del arbol con nombre y numero:
**el lazo de espera de Crazy Taxi son el 47,22 % de sus instrucciones**
(`0c158400` 26,24 %, `0c158418` 10,49 %, `0c1583f8` 7,87 % y el retorno
`0c156c30` 2,62 %, los cuatro con 535,8 millones de vueltas). DOOM y SR2 no
tienen nada parecido: sus bloques pesados son trabajo de 50 a 80 instrucciones.

El principio: **dentro de un grano no corre nada externo al guest** --ni ticks,
ni DMA, ni AICA, ni lineas de video: todo eso vive en el bloque periodico-- y en
modo plano el camino rapido emitido solo entra en zonas de RAM del sistema (PVR,
TMU, RAM de sonido, VRAM y colas de almacenamiento van todas por ayudante). Asi
que si una vuelta de un lazo devuelve el mismo estado de registros que al entrar
y no escribio memoria ni paso por C, **todas las vueltas siguientes hasta el
corte son identicas**: se saltean k vueltas sumando k por ciclos y k por
instrucciones, y la ultima parcial corre de verdad para que el corte caiga en la
misma instruccion que en el interprete. No se agregan ni se quitan servicios --la
grilla del grano no se mueve-- y **el total de instrucciones queda igual al
digito**, que es mas fuerte que el precedente del ARM7, donde la cuenta se
informa como elision.

Las piezas, en orden de riesgo:

 - **Una generacion de impureza, no una bandera.** Una bandera limpiada por una
   arista dejaria a otra comparando contra una instantanea anterior a
   escrituras que ya nadie recuerda. La incrementan, por FILA y no por bloque
   --el descubridor no corta tras un RTS o un BRA sin par, asi que los bloques
   arrastran colas muertas y una marca en el prologo daria falsos impuros--:
   toda escritura emitida, toda llamada a manejador, los aterrizajes lentos de
   las LECTURAS (una lectura por ayudante puede tener efectos: FIFO, RTC, la
   espera del hilo del AICA) y la entrada al despachador.
 - **Dos formas de arista.** El lazo tipico de KOS cierra DENTRO de un bloque
   --`tr_traducir` crece hacia atras para poner la cabeza como entrada-- y el de
   CT es entre bloques solo porque el par JSR lo parte. Hacen falta las dos: el
   enlace hacia atras con destino constante, y el salto interno hacia atras.
 - **El punto fijo, comparado en C** sobre un conjunto que basta para una vuelta
   pura: R0-R15, SR, PR, GBR, MACH/MACL. Todo lo que lee SSR/SPC/VBR/bancos pasa
   por manejador o es terminal --y bumpea la generacion--, y las filas FPU no
   reciben enlaces.
 - **La retirada**, que es lo que la hace barata: a los 16 fallos consecutivos
   sin elidir, la arista se reparchea directo. Sin ella, cada arista de retroceso
   del guest pagaria una llamada por vuelta.
 - **Bajo MMU no entra en la v1**: URC avanza por vuelta en los aciertos
   emitidos y en los puentes entre paginas, y el arbol no tiene un contador de
   avances siempre encendido.

El caso de CT, ya trazado: tres bloques --A `0c1583f8` con el par JSR que escribe
PR con el mismo valor cada vuelta, B `0c156c30` que es RTS+NOP, y C
`0c158400..0c15841e` cuyo `BF` final es la salida enlazable hacia atras, o sea la
arista de la sonda--. R0-R4 se recargan de RAM, T se recalcula igual, R12/R14
solo se tocan en el camino de salida: punto fijo desde la segunda visita. 35
ciclos y 20 instrucciones por vuelta, 11,4 vueltas por grano, de las que
quedarian ~2,3 ejecutadas.

**Lo que hay que medir antes de creerle a la aritmetica**: la sonda cuesta una
llamada y ~22 comparaciones cuando NO elide, y el bump de generacion cuesta un
almacen por escritura emitida en todos los guests. Por eso la palanca tiene tres
posiciones y no dos: apagada, solo los bumps, y entera.
