# El plan del estado del arte

Estado: **en curso**. Escrito el 2026-08-09 sobre la rama `rendimiento-hilos`.
**La segunda vuelta — fases 7 y 8 heredadas con techos frescos, la FPU emitida y
el material CHD — vive en `jit-sota-plan.md` (2026-08-18)**; este archivo queda
como el registro de las fases 0-6. La meta,
acordada: **implementar las tecnologías del estado del arte en emulación** — las que
usan flycast y sus pares — adaptadas a la disciplina de este árbol, buscando
rendimiento comparable, **sin carrera directa de cifras contra flycast** (no se lo
mide; la vara son las tandas propias, y cada fase entra sola con su palanca y su
veredicto).

Lo que ya está al nivel del estado del arte y no se toca: el traductor con enlace de
bloques por épocas, emisión por identidad de manejador, camino rápido de memoria en
línea con caché de TLB, hogares canónicos, PGO con banco fijo
(`docs/recompilador-plan.md` — las fases de traducción viven allá). Lo que falta es
lo que este plan fasea.

**La regla de todas las fases** (la del árbol): exactitud primero — totales al
dígito, capturas y `.wav` byte a byte contra el intérprete — antes de citar tiempo;
toda tanda tras `ciclo-jit.ps1`; absolutos dentro de un binario; veredictos (ganen o
pierdan) a los planes, con la bitácora en git.

## Fase 0 — el reparto, hecha (2026-08-09)

`herramientas/perfil-jit.ps1` (nueva: `--perf` + sonda de cruces sobre los tres
guests con `DCEMU_JIT=2`). La tabla completa está en `recompilador-plan.md`; lo que
decide el orden:

- **El SH-4 es el 60-79 %** del tiempo real → el superbloque por flujo sigue primero.
- **El ARM7 es la segunda porción: 9-18,2 %** — en CT ya es 18,2 % con techo medido
  de 1,34×, porque al acelerarse el SH-4 la porción del ARM crece → el ARM7 va
  **antes** que el reloj por eventos.
- **El bloque periódico neto quedó en 5-7,4 %** (el grano 400 ya tomó lo grande) →
  el reloj por eventos vale eso más el alargue de cadenas, no más: baja de prioridad.
- El mezclador del AICA pesa 2,9-7,4 % — anotado, sin fase propia todavía.

## Las fases, en el orden que la fase 0 fijó

| # | qué | dónde vive el detalle | estado |
| --- | --- | --- | --- |
| 1 | SR2 al banco de PGO del JIT | `recompilador-plan.md`, «Cómo se mide» | **hecha** (2026-08-09): iguala su mejor marca con la capa pineada; DCDoom/CT intactos |
| 2 | **Superbloque por flujo** | `recompilador-plan.md`, tabla de veredictos | **hecha** (2026-08-09): el flujo salió neutro (apagado, `DCEMU_JIT_FLUJO=1`); su residuo ganador es **el par de retorno** (`rts`+ranura con memoria emitidos, encendido): cobertura 86,9/89,7/90,4 %, entradas −9,6/−12,7 % en los guests compilados, tiempo mixto-marginal a favor. **Rehecha el 2026-08-16** porque el veredicto era sospechoso: se había medido con el enlazado cuadrático encima, o sea el único motivo legítimo para dudar de él. Con el índice por PC destino puesto **el veredicto se sostiene** — DOOM y CT solapados, SR2 **+0,9 % con rangos disjuntos**, exacto al dígito en los tres (9000 puntos de `DCEMU_CP_MS` idénticos). Dos cosas que salieron de rehacerlo: CT ahorra **68 millones de entradas al despachador (−4,6 %)** y el tiempo no se mueve —la tercera medición independiente que dice que el viaje al despachador C no es el costo—, y el flujo **acorta** los bloques (22,7→19,8 instrucciones en DOOM), porque seguir una arista desemboca en código que la traza ya tiene y ahí se corta |
| 3 | La frontera residual: **el censo por peso** eligió el camino — 4 plantillas nuevas (121) + pares de rama BRA/JMP/BRAF | `recompilador-plan.md`, tabla y pendientes | **hecha** (2026-08-09 noche): **el mayor salto de la serie** — DOOM −25,5 % y **1,10×** (entradas −50 %), CT −16,7 %, SR2 −9,6 % (92,9 % de cobertura). La elisión de recargas bajó a pendiente medible; el censo queda en el resumen para elegir cada lote siguiente |
| 4 | **El ARM7**: caché de predecodificación primero (hoy decodifica en cada paso), traductor ARM7→x64 sobre `jit_x64.c` si el escalón no alcanza; emisión por identidad de manejador del intérprete de `arm7.c`, que queda libre de SDL y enlazable por `tests/` | `notas-aica.md` / `arm7-plan.md` | **hecha entera** (2026-08-09/10), en tres escalones dentro de sus binarios: **la predecodificación** (validez por comparación, manejadores por forma) −3,6/−5,1/−3,3 %; **los bloques en C** (cuatro teoremas de exactitud, 61,4 % de los pasos) −0,6/−1,8/−0,4 %; **el traductor x64** (`arm7jit.c`, plantillas por forma + respaldo por manejador, suite propia 6/6) −2,4/−2,4/−2,0 %. Todas las compuertas al dígito en cada escalón. Marcas del árbol: **DOOM 1,12×, CT 92,1 s, SR2 0,93×**. Sigue: rehacer el reparto y la fase 5 |
| 5 | **El reloj por eventos**: próximo vencimiento (TMU×3, WDT, muestra de AICA, línea, DMA auto, retardos de `intc_add`) en vez del sondeo cada 400 ciclos; las cadenas del JIT corren hasta el vencimiento. `intc_sh4_reintentar` ya es la mitad event-driven y se conserva | `clock-plan.md`, fase 5 | **hecha** (2026-08-10) como salteo del servicio **sobre la grilla intacta** — alargar las cadenas quedó fuera a propósito: mueve la cuantización de las entregas y rompe la identidad byte a byte, el contrato de las compuertas. Exacta por construcción y verificada entera: 9/9 compuertas canónicas, **barrido KOS 139/139 idéntico** entre palanca 0/1, ctest 23/23. Veredicto del techo conocido (el neto era 5-7,4 %): **SR2 −0,7/−1,0 % (6/6 rondas), CT −0,5 %, DOOM neutro** tras memoizar la inversa del AICA (perdía +0,8 % — es el guest denso en invalidaciones). Tres agujeros encontrados por las compuertas, los tres «mover una entrega sin invalidar»: el recálculo que pisaba el SCANINT posteado por el propio servicio (`reloj_toques`), los ticks aplicados al estado nuevo de una escritura on-chip (sincronizar antes), y la línea ASIC movida por `aica_escribir`. Palanca `DCEMU_SIN_RELOJ_EVENTOS=1` (encendido por omisión). Sigue: rehacer el reparto y la fase 6 |
| 6 | **El camino de memoria emitido.** Estaba escrita como «fastmem»; **el censo la reescribió** (ver abajo) | `recompilador-plan.md`, tabla de veredictos | **escalones 1 y 2 hechos** (2026-08-14): el atajo de P1/P2 (**DOOM −8,1 %, SR2 −2,1 %**, disjuntos) y después las guardas muertas plegadas + la rejilla de 64 bytes en línea (**DOOM −1,6 % y −1,3 %, tres rangos disjuntos, −2,8 % juntos**; SR2 y CT dentro de su dispersión), que de paso destapó y cerró **el gancho de la época desconectado**. Queda la zona no plana, con techo medido de 2,3 % |
| 7 | Elisión de lazos ociosos del SH-4, **condicional** a que un perfil muestre sondeo dominante; el precedente es la memoización del ARM7 (salida idéntica, la cuenta se reporta como elisión) | — | — |
| 8 | El parque entero (135 demos + 14 juegos, `DCEMU_JIT=2` contra control) y la adopción por omisión | `recompilador-plan.md`, pendiente 4 | cierra el plan |

Tras cada fase se rehace el reparto (`perfil-jit.ps1`); cuando el techo de la
siguiente quede bajo ~2-3 % del tiempo, el plan se da por cumplido.

## La fase 6, reescrita por su propio censo (2026-08-14)

La fase estaba escrita como **fastmem** —mapeo directo, VEH, `VirtualProtect`—
sobre la suposición de que lo que costaba en el camino de memoria emitido era
la búsqueda de zona. Antes de escribirla se midió, con una sonda nueva
(`DCEMU_JIT_SONDA_ACCESOS=1`): cuántos accesos emitidos toman el camino rápido
y **por qué guarda** cae el resto al ayudante. La suposición era falsa.

- **La zona no plana es el 2,3 % en DCDoom y el 0,1 % en SR2.** Fastmem venía a
  atacar eso.
- **Lo que costaba era la traducción: el 33,6 % de los accesos de DCDoom.** Y de
  esos fallos, **el 98 %** (96,6 % en SR2) son direcciones que `mmu_traducir()`
  devuelve sin traducir —P1 y P2—, o sea que **no son clientela de la caché**:
  su ranura queda sin estrenar para siempre y cada acceso repaga el viaje al
  ayudante. 636 millones de veces en 35 segundos emulados.
- **Tres guardas no se disparan nunca** en los tres guests: alineación, cambio
  de modo y UBC de operando. Son dos comparaciones por acceso que el árbol paga
  sin que ninguna haya sido cierta jamás.

Dos hipótesis intermedias murieron por medición y quedan anotadas porque cada
una costó corridas: **no era capacidad** (de 64 a 8192 entradas la tasa de fallo
no se mueve — 33,0 % contra 33,2 %) y **no era alias de índice** (mezclar los
bits altos en el índice no cambió nada, 64,1 % antes y después). La firma que
las descartó a las dos fue la misma: los fallos no encontraban la ranura
ocupada, la encontraban **sin estrenar**.

El escalón 1 es el atajo: la misma decisión de `mmu_traducir()` emitida en
línea. Su posición también se midió, y la primera elección era la mala —
detrás del fallo de caché sale más barato para el que acierta, pero DCDoom
perdía la mitad de la ganancia. Se quedó adelante, y **resolviendo el modo al
emitir** (SR.MD vive en la clave de validez, así que un bloque sólo se despacha
en el modo en que se tradujo), que lo bajó de siete instrucciones a cuatro y
sacó a SR2 del ruido: **DOOM −8,1 %, SR2 −2,1 %**, ambos con rangos disjuntos,
palanca `DCEMU_JIT_SIN_ATAJO_P1P2=1`.

Lo que sigue en la fase lo nombra el censo con el atajo puesto, y **no es
fastmem**: en DCDoom quedan 4,9 % de accesos en «página con código» y 2,3 % en
«zona no plana»; y las tres guardas que nunca se disparan se pliegan en las
tablas base con el truco que ya usa el watchpoint. Fastmem sigue existiendo
como idea para la zona no plana, pero su techo medido es ese 2,3 %, no lo que
el plan le atribuía.

### El escalón 2, y lo que encontró debajo (2026-08-14)

Los dos residuos de arriba se cerraron juntos, porque son la misma secuencia
emitida:

- **las dos guardas muertas dejan de emitirse.** El break de operando del UBC
  se pliega en `mem_base_lectura/escritura`, igual que el watchpoint: con un
  break armado la zona entera baja al camino lento sola, y los ayudantes
  físicos corren el gancho con la virtual. La guarda de modo desaparece del
  lado MMU, donde el respaldo ya existía —escribir MMUCR vacía `mmu_datos`
  entero, así que ningún acceso emitido acierta y todos caen al ayudante, que
  mira `mmu_activa` de verdad—; del lado plano se queda, porque ahí la tabla de
  zonas contesta con base directa para 0x0C aunque la traducción se acabe de
  encender. La emisión pesa **1 132 176 bytes menos, un 3,5 %**;
- **la escritura sobre una página con código se pregunta en línea**, contra una
  segunda rejilla de 64 bytes, en vez de bajar al ayudante.

Medidos por separado —tres brazos, porque el combinado leyó al revés— valen
**−1,6 % y −1,3 % más en DCDoom, con los tres rangos disjuntos y −2,8 % juntos**;
Sega Rally 2 y Crazy Taxi quedan dentro de su dispersión. Que gane justo DCDoom
es lo que el censo predecía: es el guest con el 8,4 % de accesos sobre página
con código y el único con la guarda de modo del lado MMU.

Lo segundo destapó lo que estaba debajo, y es lo importante de este escalón:
**el gancho que movía la época por escritura nunca estuvo conectado**. El
expediente completo está en `recompilador-plan.md`, «El gancho que nunca estuvo
conectado»; lo que corresponde repetir acá es la lección de medición, porque es
la del árbol otra vez: el resumen de cada corrida venía imprimiendo `0
escritura` desde el primer día del traductor, y ese 0 se leyó siempre como «los
guests no escriben sobre su código» cuando quería decir «nadie está mirando».
Lo que separa las dos lecturas es un contador de control —cuántas escrituras
llegaron a preguntar— y no existía. Ahora se imprime siempre.

## Gates por fase (prueba de aceptación)

- **2 y 3**: totales al dígito + capturas byte a byte en los tres guests; bajan las
  entradas al despachador y los cruces (la sonda lo mide); tanda completa reentrenada.
- **4**: pasos del ARM idénticos a la unidad, `.wav` byte a byte (las demos de sonido
  + CT con reverb), `DCEMU_PERFIL_ARM=1` antes/después; palanca `DCEMU_SIN_JIT_ARM`.
- **5**: barrido KOS completo byte a byte + `.wav` + capturas de los tres guests —
  cada evento cae en el mismo ciclo emulado, cambia cuándo se pregunta, no cuándo
  ocurre; palanca para volver al grano fijo.
- **6**: exactitud + capturas; palanca `DCEMU_SIN_FASTMEM`; Windows primero, la
  plataforma en un archivo (el patrón de `hilo.c`).
- **7**: capturas y `.wav` intactos; el total de instrucciones cambia y se reporta
  como elisión.
- **8**: el barrido con corrida de control, mismas reglas de VMU/render de CLAUDE.md.

## Lo que este plan NO incluye (medido y perdido, o fuera del cuello)

Hilos para AICA/SH-4/render (`hilos-plan.md`: la fase 1 implementada pierde 4-5 % y
queda tras `--hilos`); VBO/batching de tiras (el pipeline gráfico completo cuesta
7,6 %); el tope estático de 96 instrucciones y el redespacho por ayudante C
(expedientes en `recompilador-plan.md`). No se reintentan sin releer su porqué.
