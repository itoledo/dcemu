# El recompilador dinámico (JIT)

Estado y pendientes. **La bitácora completa — cada ronda con sus tablas, los
expedientes enteros de cada caza — está en git** (hasta `aaff874`, 2026-08-09); este
archivo guarda las conclusiones que hacen falta para trabajar, no el camino.

## Qué es hoy

`-DDCEMU_JIT=ON` (build aparte, `build-jit/`, con su propio perfil
`build-pgo/dcemu-jit.pgd` — ver la sección de PGO de CLAUDE.md). `DCEMU_JIT=2` es el
traductor automático; `=1`, los dos bloques de la fase 0 emitidos a mano.

- **121 plantillas**, seleccionadas por censo de qué corta bloques (no por completar
  `opcodes[]`) — las últimas cuatro las pidió **el censo de la frontera por peso**
  (abajo): las dos `MOV.W` que picaban el lazo de columnas de DOOM en ocho bloques de
  2-10 instrucciones corridos 9,4 M de veces cada uno, `NEGC` (por manejador) y
  `LDC Rm,GBR`. Lo que corta hoy: palabras de datos (deben cortar), escritores de SR,
  `TRAPA`, lo que la clave FPU gobierna, y las ranuras con memoria de las ramas sin
  par (abajo).
- **Los pares de rama** (`rts`/`bra`/`jmp`/`braf` + ranura con memoria — el epílogo
  estándar de Katana y sus parientes) ya no cortan: la emisión sincroniza con el PC
  de la RAMA antes de tocar nada (la falta reejecuta desde la rama, como la
  instantánea del intérprete), el destino dinámico viaja por el lugar seguro del
  estado, y el par TERMINA la traza (lo que sigue es otra función; dejar la cola le
  costó a SR2 dos puntos y +27 % de arena). **JSR/BSR/BSRF quedan sin par**: escriben
  PR antes de la ranura, y una falta lo revertiría por instantánea en el intérprete
  pero no en el emitido — el marco de excepción del guest vería el PR nuevo.
  `DCEMU_JIT_SIN_PARES=1` los apaga.
- **El censo de la frontera** (en el resumen `jit:`, siempre): en qué termina cada
  bloque, ponderado por las veces que se corrió. Es lo que separa «hay muchos sitios»
  de «por ahí pasa la ejecución», y lo que eligió el lote de arriba. El residuo que
  nombra hoy: `MOV.L @(disp,GBR)` (C6xx) y el 15-18 % de ranuras sin par (los JSR).
- **Exacto al dígito con capturas byte a byte en tres guests**: DCDoom (MMU, **89,8 %**
  de cobertura, **43,2 por entrada** — las entradas bajaron 50 % con el lote del
  censo), Crazy Taxi (**90,1 %**, 12,4) y Sega Rally 2 (MMU+FPU, **92,9 %**, 21,3).
  Mejores marcas (tanda 2026-08-09 noche): DCDoom **31 826 ms, −25,5 %, 1,10× tiempo
  real**; CT **92 510 ms, −16,7 %** su mejor cociente; SR2 **65 781 ms, −9,6 %**
  (0,91×).
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
| Sonda de conservación de URC (`-DDCEMU_SONDA_URC`) | **el instrumento que cerró la caza en 3 corridas** | uc/ue/uv en los puntos de control; conservación con dirección, no hipótesis |

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

(El superbloque por flujo y el lote del censo se hicieron enteros — 2026-08-09, con
el agujero de la página del anfitrión cerrado de paso —; los veredictos están en la
tabla y lo encendido por omisión son los pares de rama y las 121 plantillas.)

1. **El próximo mini-lote del censo**: `MOV.L @(disp,GBR)` y su familia (C6xx, lo
   que el censo nombra hoy), y el par para `jsr`/`bsr`/`bsrf` — que exige resolver
   el peligro de PR (escriben PR antes de la ranura; una falta lo revertiría por
   instantánea en el intérprete y no en el emitido). La salida probable: retrasar la
   escritura de PR a después de la ranura SOLO si la ranura no lee PR (el censo dirá
   si el caso con lectura existe).
2. **La época por página** (en vez de global): los rechazos de DCDoom quedaron en
   8,2 M — cada movimiento de mapeo de WinCE invalida TODOS los bloques y la
   revalidación por palabras falla ~7 % de las entradas. Una generación por página
   de guest dejaría en pie lo que no se movió.
3. **Elisión de recarga en reentradas por despachador**: los no volátiles sobreviven
   el viaje C; falta la marca de «contexto ensuciado». Con las entradas de DOOM a la
   mitad, su techo bajó — medir antes de escribirla.
4. **La fase 3 original: el parque entero** — el barrido de 135 demos y los 14 juegos
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
