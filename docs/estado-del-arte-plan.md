# El plan del estado del arte

Estado: **en curso**. Escrito el 2026-08-09 sobre la rama `rendimiento-hilos`. La meta,
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
| 2 | **Superbloque por flujo** | `recompilador-plan.md`, tabla de veredictos | **hecha** (2026-08-09): el flujo salió neutro (apagado, `DCEMU_JIT_FLUJO=1`); su residuo ganador es **el par de retorno** (`rts`+ranura con memoria emitidos, encendido): cobertura 86,9/89,7/90,4 %, entradas −9,6/−12,7 % en los guests compilados, tiempo mixto-marginal a favor |
| 3 | La frontera residual: **el censo por peso** eligió el camino — 4 plantillas nuevas (121) + pares de rama BRA/JMP/BRAF | `recompilador-plan.md`, tabla y pendientes | **hecha** (2026-08-09 noche): **el mayor salto de la serie** — DOOM −25,5 % y **1,10×** (entradas −50 %), CT −16,7 %, SR2 −9,6 % (92,9 % de cobertura). La elisión de recargas bajó a pendiente medible; el censo queda en el resumen para elegir cada lote siguiente |
| 4 | **El ARM7**: caché de predecodificación primero (hoy decodifica en cada paso), traductor ARM7→x64 sobre `jit_x64.c` si el escalón no alcanza; emisión por identidad de manejador del intérprete de `arm7.c`, que queda libre de SDL y enlazable por `tests/` | `notas-aica.md` / `arm7-plan.md` | **hecha entera** (2026-08-09/10), en tres escalones dentro de sus binarios: **la predecodificación** (validez por comparación, manejadores por forma) −3,6/−5,1/−3,3 %; **los bloques en C** (cuatro teoremas de exactitud, 61,4 % de los pasos) −0,6/−1,8/−0,4 %; **el traductor x64** (`arm7jit.c`, plantillas por forma + respaldo por manejador, suite propia 6/6) −2,4/−2,4/−2,0 %. Todas las compuertas al dígito en cada escalón. Marcas del árbol: **DOOM 1,12×, CT 92,1 s, SR2 0,93×**. Sigue: rehacer el reparto y la fase 5 |
| 5 | **El reloj por eventos**: próximo vencimiento (TMU×3, WDT, muestra de AICA, línea, DMA auto, retardos de `intc_add`) en vez del sondeo cada 400 ciclos; las cadenas del JIT corren hasta el vencimiento. `intc_sh4_reintentar` ya es la mitad event-driven y se conserva | `clock-plan.md`, fase 5 | **hecha** (2026-08-10) como salteo del servicio **sobre la grilla intacta** — alargar las cadenas quedó fuera a propósito: mueve la cuantización de las entregas y rompe la identidad byte a byte, el contrato de las compuertas. Exacta por construcción y verificada entera: 9/9 compuertas canónicas, **barrido KOS 139/139 idéntico** entre palanca 0/1, ctest 23/23. Veredicto del techo conocido (el neto era 5-7,4 %): **SR2 −0,7/−1,0 % (6/6 rondas), CT −0,5 %, DOOM neutro** tras memoizar la inversa del AICA (perdía +0,8 % — es el guest denso en invalidaciones). Tres agujeros encontrados por las compuertas, los tres «mover una entrega sin invalidar»: el recálculo que pisaba el SCANINT posteado por el propio servicio (`reloj_toques`), los ticks aplicados al estado nuevo de una escritura on-chip (sincronizar antes), y la línea ASIC movida por `aica_escribir`. Palanca `DCEMU_SIN_RELOJ_EVENTOS=1` (encendido por omisión). Sigue: rehacer el reparto y la fase 6 |
| 6 | **Fastmem**: RAM de sistema por acceso directo sin comparación de etiqueta (guests sin MMU primero), VEH + parcheo; páginas con código traducido protegidas por `VirtualProtect` (la época/SMC — hoy la mueve el manejador de escritura, y el store directo se lo saltearía); con watchpoints armados se apaga solo. Convive con `SetUnhandledExceptionFilter` (verificado: es de última instancia) | — | — |
| 7 | Elisión de lazos ociosos del SH-4, **condicional** a que un perfil muestre sondeo dominante; el precedente es la memoización del ARM7 (salida idéntica, la cuenta se reporta como elisión) | — | — |
| 8 | El parque entero (135 demos + 14 juegos, `DCEMU_JIT=2` contra control) y la adopción por omisión | `recompilador-plan.md`, pendiente 4 | cierra el plan |

Tras cada fase se rehace el reparto (`perfil-jit.ps1`); cuando el techo de la
siguiente quede bajo ~2-3 % del tiempo, el plan se da por cumplido.

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
