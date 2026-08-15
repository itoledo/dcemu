# El recompilador dinámico (JIT)

Estado y pendientes. **La bitácora completa — cada ronda con sus tablas, los
expedientes enteros de cada caza — está en git** (hasta `aaff874`, 2026-08-09); este
archivo guarda las conclusiones que hacen falta para trabajar, no el camino.

## Qué es hoy

`-DDCEMU_JIT=ON` (build aparte, `build-jit/`, con su propio perfil
`build-pgo/dcemu-jit.pgd` — ver la sección de PGO de CLAUDE.md). `DCEMU_JIT=2` es el
traductor automático; `=1`, los dos bloques de la fase 0 emitidos a mano.

- **122 plantillas**, seleccionadas por censo de qué corta bloques (no por completar
  `opcodes[]`) — las últimas cinco las pidió **el censo de la frontera por peso**
  (abajo): las dos `MOV.W` que picaban el lazo de columnas de DOOM en ocho bloques de
  2-10 instrucciones corridos 9,4 M de veces cada uno, `NEGC` (por manejador) y
  `LDC Rm,GBR`, más `MOV.L @(disp,GBR),R0` (C6xx). Lo que corta hoy: palabras de
  datos (deben cortar), escritores de SR, `TRAPA` y lo que la clave FPU gobierna.
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
- **Exacto al dígito con capturas byte a byte en tres guests**: DCDoom (MMU, **93,8 %**
  de cobertura, 48,4 por entrada), Crazy Taxi (**96,2 %**, 20,1) y Sega Rally 2
  (MMU+FPU, **95,2 %**, 24,8). Tanda del 2026-08-14, binario `A15BA7445BC385EF`,
  con el atajo de P1/P2 puesto: DCDoom **28 866 ms, −32,5 %** y **1,21× tiempo
  real**; CT **87 069 ms, −21,5 %** (2,07×); SR2 **64 356 ms, −12,0 %** (0,93×).
  La tanda anterior (2026-08-10, `4BDA443CB17BD1B6`) daba −26,5/−19,3/−8,4 %: lo
  que se movió en los dos guests con MMU es el atajo, y lo de CT —que no emite
  traducción alguna— es su capa de reentrenamiento, que vale ±1-2 %. Los pares de
  llamada solos valen ~1,0/5,5/0,4 %; C6xx es neutra en SR2 (~0,1 %) y queda
  porque elimina la frontera sin costo.
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
| El barrido lineal de `jit_enlazar()` | **era cuadrático, y eran los tirones** — `DCEMU_JIT_ENLACE_LINEAL=1` lo revive | avisarle al bloque nuevo quién lo esperaba recorría todos los ya traducidos: 97 % del tiempo de traducir, 12,8 s de 120 s emulados en CT. Índice por PC destino: **12 802 → 259 ms**, cuadros lentos **14,9 % → 1,5 %** |
| La sonda de tirones (`DCEMU_SONDA_CUADROS=1`) | **el instrumento que lo encontró** | una tanda da la media y la media es lo único que un tirón no mueve; la distribución por cuadro con el tiempo **emulado** al lado separa «dcemu se frenó» de «el guest hizo un cuadro largo» |
| Sonda de conservación de URC (`-DDCEMU_SONDA_URC`) | **el instrumento que cerró la caza en 3 corridas** | uc/ue/uv en los puntos de control; conservación con dirección, no hipótesis |

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
1. **La época por página** (en vez de global): los rechazos de DCDoom quedaron en
   8,2 M — cada movimiento de mapeo de WinCE invalida TODOS los bloques y la
   revalidación por palabras falla ~7 % de las entradas. Una generación por página
   de guest dejaría en pie lo que no se movió.
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
