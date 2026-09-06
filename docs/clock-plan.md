# Plan: una sola base de tiempo

Estado: **completo**. Julio de 2026, sobre `master`. Las cuatro fases están implementadas y
los cuatro hitos alcanzados; ver [Lo que quedó](#lo-que-quedó) al final. En agosto se sumó
la **fase 5, el reloj por eventos** (la fase 5 de `estado-del-arte-plan.md`): el servicio
del bloque periódico corre solo cuando un vencimiento llegó, sobre la misma grilla — la
última sección de este documento.

## El problema, con números

`core.context.cycles` sí es un contador de ciclos de CPU razonable: los 220 sitios que lo
incrementan reparten costes de 1 a 24 según la instrucción, que es el orden correcto para el
SH-4. El problema no es la cuenta, es que **cada consumidor la convierte a tiempo con una
constante propia, y ninguna coincide con las otras**.

El reloj de la Dreamcast, con los números que usa KOS (`kernel/arch/dreamcast/kernel/timer.c`):

| reloj | frecuencia |
| --- | --- |
| CPU | 199 499 520 Hz |
| periférico (Pck) | CPU/4 = 49 874 880 Hz |
| TMU con `TPSC = Pck/4` | Pck/4 = **12 468 720 Hz** |

O sea: KOS programa el TMU esperando que `TCNT` baje 12,47 millones de veces por segundo,
que es **CPU/16**.

Lo que hace dcemu hoy:

| consumidor | ritmo actual | ritmo correcto | error |
| --- | --- | --- | --- |
| TMU (`timer_check`) | una cuenta cada 50 ciclos, o sea CPU/50 | CPU/16 con el `TPSC` de KOS | **3,125× lento** |
| línea de barrido | 978 ciclos por línea | ≈6333 (199,5 MHz / (525 × 60)) | **6,5× rápido** |
| WDT (`wdt_tick`) | `32 << CKS` ciclos por cuenta | igual | correcto |
| RTC (`sistema_rtc_*`) | reloj del anfitrión | — | independiente |

`50/16 = 3,125`, y eso es **exactamente** el desfase que midió `basic/watchdog`: pidió 20
avisos en 10 segundos y los recibió en 3. El WDT está bien; el que va lento es el reloj de
milisegundos de KOS, que sale del TMU. La prueba de que el WDT es correcto: con `CKS=0` da
una interrupción cada `256 × 32 = 8192` ciclos, que a 199,5 MHz son 41,06 µs — justo el
`WDT_INT_DEFAULT` de 41 µs que el ISR de KOS asume.

### Dos cosas más que están mal en el mismo sitio

**El acumulador de líneas se cuenta doble.** En `main_loop()`:

```c
if (core.context.cycles >= 50)
{
    core.context.cycles_v_int += core.context.cycles;   // suma TODO lo acumulado
    core.context.cycles = core.context.cycles - 50;     // pero solo resta 50
    ...
}
```

El resto que sobra de 50 queda en `cycles` y se vuelve a sumar a `cycles_v_int` en la vuelta
siguiente. Con instrucciones de 1 a 24 ciclos el sobrante medio es de unos pocos ciclos, así
que `cycles_v_int` corre un porcentaje de más — chico, pero sistemático y gratis de arreglar.

**`timer_check()` ignora el prescaler.** `sh4emu.h` define exactamente dos bits de `TCR`:
`TMU_TCR_UNF` y `TMU_TCR_UNIE`. Los tres bits `TPSC` no existen en el código. Por eso el
ritmo es una constante y no depende de lo que programó el guest.

## Objetivo

Que todo evento periódico se derive de **un solo contador de ciclos de CPU**, con la
frecuencia real del SH-4 como única constante de conversión.

Cuatro hitos observables:

| hito | qué se ve | fases |
| --- | --- | --- |
| **A** | `basic/watchdog` pasa: 20 avisos en 10 segundos | 1-2 |
| **B** | `timer_ms_gettime()` de KOS coincide con el reloj de pared | 2 |
| **C** | los demos animados corren a velocidad plausible, no 6,5× rápido | 3 |
| **D** | `--traza-mem` puede reportar tiempo emulado, no solo instrucciones | 4 |

El hito A es el que importa: es la primera vez que un demo de KOS mide el reloj y da un
veredicto.

## Fase 0 — Decisiones de base

- **Una constante y una sola.** `DC_CPU_HZ = 199499520`, el mismo número que usa KOS. Todo
  lo demás se deriva: `DC_PCK_HZ = DC_CPU_HZ / 4`. Nada de constantes sueltas por archivo.
- **`cycles` son ciclos de CPU.** Ya lo son en intención; el plan lo hace explícito y lo
  documenta, en vez de que cada consumidor invente su factor.
- **No romper lo que corre.** `roto.bin` y los demos que ya pasan tienen que seguir igual.
  El riesgo real es que cambiar el ritmo del TMU 3× altere el equilibrio de cualquier cosa
  que hoy funcione por casualidad, así que cada fase se verifica contra el barrido de
  `basic/*` completo, no solo contra el demo que la motivó.
- **Presupuesto de tiempo, no de llamadas.** El patrón actual —"si pasaron 50 ciclos, llamá
  a todos"— fuerza a todos los consumidores al mismo cuanto. Conviene que cada uno tenga su
  propio umbral en ciclos y su propio resto, como ya hace `wdt.c`.
- **Sin números mágicos.** Si un ritmo no se puede derivar de `DC_CPU_HZ` y de un registro
  del guest, va con un comentario que diga de dónde sale y por qué.

## Fase 1 — El prescaler del TMU

Lo más chico con efecto medible inmediato.

### 1.1 Los bits que faltan

Agregar a `sh4emu.h` los tres bits `TPSC` de `TCR` y su tabla de divisores. Sobre el
periférico:

| `TPSC` | fuente | divisor desde ciclos de CPU |
| --- | --- | --- |
| 000 | Pck/4 | 16 |
| 001 | Pck/16 | 64 |
| 010 | Pck/64 | 256 |
| 011 | Pck/256 | 1024 |
| 100 | Pck/1024 | 4096 |
| 101 | reservado | — |
| 110 | salida del RTC | aparte |
| 111 | reloj externo | aparte |

El divisor desde ciclos de CPU es `4 × TDIV` porque Pck ya es CPU/4. Los casos 110 y 111 se
registran y caen al 000: la Dreamcast no cablea el reloj externo, y ningún software conocido
los usa.

### 1.2 Un resto por canal

`timer_check()` pasa a recibir los ciclos transcurridos y a llevar un acumulador por canal:

```
acumulado[n] += ciclos;
while (acumulado[n] >= divisor(TCR[n]))
{
    acumulado[n] -= divisor(TCR[n]);
    ...bajar TCNT[n], y en el subdesborde recargar de TCOR[n] y pedir la interrupción...
}
```

Mismo patrón que `wdt_tick()`, que ya funciona. Ojo con `TSTR`: solo cuentan los canales
arrancados, y al arrancar uno hay que poner su resto en cero.

### 1.3 El acumulador de líneas

Arreglar el doble conteo de `cycles_v_int`: restar de `cycles` lo mismo que se suma, o mejor,
llevar un solo contador monótono de ciclos y que cada consumidor guarde su propia marca.

## Fase 2 — El contador monótono

Lo que hace que el resto sea fácil en vez de repetitivo.

Un `unsigned long long ciclos_totales` que solo sube, y una función de conversión:

```
#define DC_CPU_HZ	199499520u
#define DC_PCK_HZ	(DC_CPU_HZ / 4)

unsigned long long reloj_ciclos(void);
unsigned long long reloj_us(void);		/* ciclos -> microsegundos emulados */
```

`main_loop()` lo incrementa una vez por instrucción y los consumidores comparan contra su
propia marca. Ventajas concretas:

- desaparecen los `- 50` y `- 978`, que son la fuente de los restos mal llevados;
- `--traza-mem` puede decir "se trabó a los 3,4 s de tiempo emulado" en vez de "cuatro
  millones de instrucciones", que es mucho más útil para comparar corridas;
- se puede medir la relación entre tiempo emulado y tiempo real, o sea cuánto más lento que
  una consola corre dcemu, que hoy no se sabe.

## Fase 3 — El barrido de pantalla

El 978 sale de algún ajuste empírico de 2004 y hoy hace que los frames vayan 6,5× rápido.

La cuenta correcta es `DC_CPU_HZ / (líneas por campo × campos por segundo)`. Las líneas ya
las tiene el guest en `pvr_spg_load_vcount`; los campos por segundo salen de los registros
`SPG_*` del PVR según sea VGA progresivo (525×60) o compuesto entrelazado (525×59,94, o 625
en PAL).

Derivarlo de esos registros en vez de una constante es lo correcto, y además hace que el
modo de vídeo del guest se respete. Lo que hay que cuidar: `RedibujarPantalla()` y el bombeo
de eventos de SDL cuelgan del fin de frame, así que bajar la tasa de frames 6,5× cambia la
latencia de entrada y hay que confirmar que el emulador sigue respondiendo.

## Fase 4 — Lo que se puede medir recién ahora

Con base de tiempo única, dos cosas que hoy no se pueden hacer:

- **Reportar tiempo emulado** en `traza.c`, y la relación con el tiempo real.
- **Limitar la velocidad** para que el emulador no corra más rápido que una consola cuando
  la máquina alcanza. Hoy no hay con qué compararlo.

Ninguna de las dos es necesaria para los hitos A-C; van acá para que no se cuelen antes.

## Cómo se prueba

**Unitario**, en `tests/`. La conversión de `TPSC` a divisor y la lógica del acumulador son
funciones puras, así que se prueban igual que `wdt.c`:

- cada valor de `TPSC` da su divisor
- el resto no se pierde entre llamadas, con cuentas que no son múltiplos del divisor
- el subdesborde recarga de `TCOR` y pide la interrupción solo si `UNIE` está puesto
- `TSTR` para y arranca canales, y arrancar uno pone su resto en cero
- los canales son independientes: tres divisores distintos a la vez

**Extremo a extremo**, y acá está lo bueno de esta fase: hay demos de KOS que dan veredicto
numérico sobre el reloj.

- `basic/watchdog` — pide 20 avisos en 10 segundos. Hoy da 3. Es el hito A.
- `basic/threading-*` — varios usan esperas temporizadas; ya pasan, así que sirven de
  no-regresión sobre el TMU.
- El barrido completo de `basic/*`, que ya está hecho y da 13 aprobados: cualquiera que se
  caiga después de esto es una regresión del reloj.

## El riesgo real

1. **Cosas que hoy funcionan por casualidad.** Cambiar el TMU 3,125× y el barrido 6,5× altera
   el equilibrio entre interrupciones y ejecución. Es perfectamente posible que algo que hoy
   anda dependa del ritmo equivocado. Por eso el barrido de `basic/*` completo es parte de la
   verificación de cada fase y no del final.
2. **Tormenta de interrupciones.** Con el TMU 3,125× más rápido, KOS recibe 3 veces más
   interrupciones de temporizador por instrucción emulada. Si el manejador de KOS cuesta más
   que el intervalo, el guest deja de avanzar. Es el modo de fallar que hay que vigilar, y se
   detecta con el mismo barrido.
3. **Que el emulador se vuelva injugable.** Bajar la tasa de frames 6,5× es correcto pero
   también significa 6,5 veces menos volcados de pantalla y de eventos SDL por instrucción.
   Si la emulación ya es más lenta que la consola, la entrada se puede volver intratable.

El punto 1 es el que más cuesta: no hay forma de saber de antemano qué depende del ritmo
actual, solo medirlo después.

## Estimación

| fase | trabajo |
| --- | --- |
| 1 — prescaler del TMU | chica, y es la que cierra el hito A |
| 2 — contador monótono | mediana; refactor de `main_loop()` |
| 3 — barrido de pantalla | mediana, y la de más riesgo de regresión visible |
| 4 — medición y límite | chica, opcional |

## Recomendación

Hacer **la fase 1 sola** y medir con el barrido completo de `basic/*` antes de seguir. Es la
que tiene un veredicto numérico esperándola (`basic/watchdog`), la que arregla el error más
grande y mejor entendido, y la que menos toca.

La fase 3 conviene dejarla para después de tener el barrido de `pvr/*` y `video/*` hecho: hoy
no sabemos qué demos gráficos funcionan, así que no habría con qué comparar si algo se rompe.

## Lo que quedó

Se hizo la fase 1, y resultó que hacían falta **dos** arreglos, no uno.

### Archivos nuevos

- `tmu.c` / `tmu.h` — los tres canales, con `DC_CPU_HZ` como única constante. Sin SDL, para
  que `tests/` los enlace.
- `tests/test_tmu.c` — 12 casos, suite `tmu`, prueba de CTest `dc.tmu`.

### El prescaler (lo planificado)

`sh4emu.h` definía dos bits de `TCR` y ninguno del prescaler. Ahora `tmu_divisor()` traduce
`TPSC` a ciclos de CPU (16, 64, 256, 1024, 4096) y cada canal lleva su propio resto, así que
ninguna fracción de ciclo se pierde entre llamadas. Arrancar un canal en `TSTR` le pone el
resto en cero.

También se arregló el doble conteo de `cycles_v_int`, que sumaba todo lo acumulado pero solo
restaba 50 de `cycles`.

Eso bajó el error de **3,33× a 1,25×**: `basic/watchdog` pasó de 20 avisos en 3 segundos a 20
en 8. Mejor, pero todavía mal.

### El descarte de interrupciones (lo que no estaba en el plan)

Si el TMU y el WDT salen del mismo contador de ciclos, la relación entre ambos tiene que ser
exacta. Que quedara un 1,25× decía que había otra cosa, y la había en `intc.c`:

```c
if (IS_SH4_REG_SET(SR_BL) || VBR == 0)
    return false;        // el evento se PERDIA
```

Cada periférico llamaba a `intc()` en el momento del subdesborde. Si `SR.BL` estaba puesto —o
`IMASK` era demasiado alto— `intc()` devolvía false y **el evento desaparecía**. En el chip la
petición queda asertada hasta que el software limpia la bandera del periférico.

El arreglo no necesitó estado nuevo, y ahí está lo bueno: `TCR.UNF` de cada canal e
`WTCSR.IOVF` **ya son** la fuente de verdad, y los limpia el manejador del guest. Así que
`tmu_tick()` y `wdt_tick()` pasaron a solo marcar su bandera, e `intc_revisar_sh4()` deriva la
petición de las banderas y la entrega cuando `SR` lo permite. Si no se puede, la bandera sigue
puesta y se reintenta.

### Qué se verificó

- **`basic/watchdog`: `***** WATCHDOG TIMER TEST SUCCEEDED! *****`** — 20 avisos en 10
  segundos exactos. Es el hito A.
- **`basic/threading/atomics`: `***** C11 ATOMICS TEST PASSED! *****`** — el que se colgaba
  con 7,6 MB de `Waiting to atomic flag lock`. El descarte de interrupciones dejaba sin
  avanzar al planificador de hilos. La hipótesis anterior (el contrato gUSA de
  `-matomic-model=soft-gusa`) era **equivocada**.
- **16/16 en CTest**, con los 12 casos nuevos, y la suite de cobertura en verde.
- **Diez demos de `basic/*` sin regresión**: `hello`, `memtest32`, `threading-once`,
  `threading-tls`, `threading-barrier`, `threading-rwsem`, `threading-general`,
  `posix_resource`, `stackprotector`, `stacktrace`.
- `threading/spinlock_test` progresa (hilos retornando, Fibonacci del 38 recursivo mientras
  hace spin) pero es demasiado lento para terminar en la ventana de prueba. No está colgado.

El riesgo 1 —"cosas que hoy funcionan por casualidad"— no se materializó: cambiar el ritmo del
TMU 3,125× no rompió ninguno de los diez.

## Fase 3, y un efecto secundario que no esperaba

El 978 se reemplazó por `reloj_ciclos_por_linea(vcount, spg_control)`, que calcula
`DC_CPU_HZ / (líneas × campos por segundo)` con los campos según la norma: 50 en PAL, 59,94 en
NTSC y 60 en VGA. Da 6345, 6351 y 6394 ciclos por línea respectivamente, contra los 978 de
antes.

El valor vive en `pvr_ciclos_linea` (`reg.c`) y se recalcula cuando el guest escribe `SPG_LOAD`
o `SPG_CONTROL`, así que el bucle caliente compara contra una variable y no llama a una
función por instrucción.

**El efecto secundario:** desapareció la banda corrupta de abajo del demo `tunnel`, y el
corredor se ve mucho más profundo. Con los frames 6,5× rápidos, `cb_tastart()` se disparaba
antes de que el guest terminara de enviar su geometría, así que se rendían frames parciales.
No lo buscaba y explica un artefacto que llevaba varias sesiones sin explicación.

### Qué se verificó en la fase 3

- **16/16 en CTest**, con 15 casos en la suite `tmu` (3 nuevos para el barrido: las tres
  normas, que un campo dure lo que debe, y que sin `vcount` no divida por cero).
- **`basic/watchdog` sigue en `SUCCEEDED`**: el hito A no se rompió.
- **Once demos de `basic/*` pasan**, incluidos los dos que la fase 1 recuperó.
- **`roto.bin`** dibuja, y con más colores que antes (993 contra 470), coherente con que ya no
  se parta el frame.
- **`tunnel.bin`** dibuja mejor que antes, sin la banda corrupta.

El riesgo 3 del plan —"que el emulador se vuelva injugable" por 6,5 veces menos volcados de
pantalla y de eventos SDL— no se materializó: la ventana sigue respondiendo.

## Fases 2 y 4

**El contador monótono.** `reloj_total` en `tmu.c` acumula ciclos de CPU y solo sube. Vive
fuera de `core.context` a propósito: la instantánea que saca `main_loop()` para reejecutar una
instrucción que falló por MMU restaura el contexto entero, y el reloj no tiene que retroceder.

El barrido pasó a comparar `reloj_total - marca_linea` contra `pvr_ciclos_linea` en vez de
acumular y restar, así que `cycles_v_int` quedó sin usar — y con él la clase de error que tenía
(sumar una cantidad y restar otra). `cycles_v_int_total` nunca se había leído.

`reloj_us()` y `reloj_ms()` convierten multiplicando antes de dividir: con `/199` el error
sería del 0,25%.

**La medición.** `traza_volcar()` estampa el tiempo emulado en cada volcado del anillo, así que
dos corridas se pueden comparar: "se trabó a los 1,855 s" en vez de "a los cuatro millones de
instrucciones". Y `traza_resumen()` reporta la relación con el tiempo real.

Lo primero que dijo esa medición, que nadie sabía:

```
traza: 26481 ms emulados en 20436 ms reales (1.30x)
```

**dcemu corre 1,3× más rápido que una Dreamcast real** en `hello`. Con eso, `--limitar` deja de
ser decorativo.

**El límite.** `--limitar` duerme al final de cada frame la diferencia entre el tiempo emulado
y el real. Solo frena, nunca acelera, así que donde dcemu ya es más lento no hace nada. Tiene
un techo de 100 ms por frame: si la cuenta se desmadra es mejor ir rápido que congelarse
esperando. Medido: **0,98×** contra el 1,30× sin la opción.

Va apagado por omisión para no cambiar el comportamiento de lo que ya funcionaba.

### Qué se verificó en las fases 2 y 4

- **16/16 en CTest**, con 16 casos en la suite `tmu` (uno nuevo para la conversión de ciclos a
  tiempo, incluido que 199 ciclos no se redondeen a cero y 200 sí den 1 µs).
- `basic/watchdog` sigue en `SUCCEEDED` y `basic/threading/atomics` en `PASSED`.
- `roto.bin` y `tunnel.bin` siguen dibujando.

### Lo que sigue faltando

- `TPSC` 110 y 111 (reloj del RTC y reloj externo) caen al valor por omisión con aviso. La
  Dreamcast no los cablea, pero no está verificado que nada los use.
- El entrelazado (`SPG_CTRL_INTERLACE`) se ignora: en NTSC y PAL entrelazados un frame son dos
  campos, y acá se cuenta por campo. Para el ritmo del barrido da igual, pero un programa que
  distinga campo par de impar no lo vería.
- `core.context.cycles` sí retrocede al restaurar la instantánea de la MMU, así que los ciclos
  de la instrucción que falló se cuentan dos veces. Es lo que pasa en el chip también —la
  instrucción se ejecuta dos veces— pero conviene tenerlo presente.
- El límite duerme por frame, o sea con granularidad de ~16 ms. Para un ritmo más parejo habría
  que frenar por línea, que es mucho más caro.

## Fase 5 — El reloj por eventos (2026-08-10)

La fase 5 de `estado-del-arte-plan.md`, cerrada en forma de conclusiones. La bitácora
completa está en git.

### Qué es, y qué no es

Hoy el bloque periódico corre entero en cada frontera de `RELOJ_GRANO` (400) ciclos y
descubre, mil veces de cada mil, que no hay nada que hacer. El reloj por eventos calcula el
**próximo vencimiento** — el mínimo entre la línea de barrido (`marca_linea +
pvr_ciclos_linea`, siempre presente, ≤ ~6400 ciclos), la muestra siguiente del AICA (la
inversa `aica_reloj_de_muestra()`, memoizada porque su argumento solo cambia al producirse
una muestra), `tmu_proximo()`, `wdt_proximo()` y las demoras del INTC
(`intc_proximo_vence()`) — y **saltea el servicio** mientras `reloj_total` no lo alcance y
nadie lo invalide. Con el DMAC propio activo en auto-request (`dma_auto_activo()`) no se
saltea nada, porque `dma_check()` avanza por sondeo.

**La grilla no cambia.** La frontera sigue cada 400 ciclos, `reloj_total` avanza igual, el
muestreo del JIT (`jit_muestrear`) y la cadencia de `DCEMU_CP_MS` quedan en la frontera
barata, y toda entrega cae en el mismo ciclo emulado que antes: lo único que se ahorra es el
cuerpo del servicio. Eso es lo que hace a la fase **exacta por construcción** — quedarse
corto en un vencimiento solo corre el bloque de más, que es lo de hoy; cada fórmula es la
aritmética exacta de su subsistema y las suites `tmu` y `wdt` prueban que aciertan el
instante (un ciclo antes no pasa nada, en el ciclo pasa).

La mitad que **no** entró, a propósito: alargar las cadenas del JIT hasta el vencimiento
(cortan en `RELOJ_GRANO`, unas 130 instrucciones). Mover ese corte cambia la grilla de
cuantización de las entregas — un evento caería en el ciclo exacto del vencimiento en vez
del múltiplo de 400 siguiente — y eso **rompe la identidad byte a byte contra el
intérprete**, que es el contrato que ancla todas las compuertas del árbol. Si algún día se
hace, exige su propia línea base canónica nueva.

### Las reglas (las que costaron una divergencia cada una)

**Todo lo que pueda mover un vencimiento o una entrega llama a `reloj_tocar()`**: las
escrituras on-chip (`regmap_write`), las del PVR/ASIC (`pvr_write`: máscaras SB, acuses de
ISTNRM, SPG, arranques de DMA, lectora, Maple), las de la ventana de registros del AICA
(`aica_escribir`) y los eventos nuevos del INTC (`intc_add`, `intc_add_ext`,
`intc_remove_ext`). `intc_sh4_reintentar` se conserva tal cual y la condición del servicio
lo mira (`reloj_total >= reloj_vencimiento || intc_sh4_reintentar`).

Las tres compuertas rojas de la primera pasada fueron tres agujeros del mismo tipo —
momentos que mueven una entrega sin invalidar — y valen como lección:

1. **El servicio se postea eventos a sí mismo y el recálculo pisaba la invalidación.**
   SCANINT1/SCANINT2 se postean en la comparación de línea, *después* de que
   `intc_revisar_sh4()` y `check_ints()` ya corrieron en ese mismo servicio; su
   `reloj_tocar()` ponía el vencimiento en 0 y el recálculo del final lo sobreescribía. El
   vblank llegaba hasta una línea entera tarde (~6350 ciclos contra ≤400), cada cuadro, en
   todos los guests — se vio como menos instrucciones ejecutadas con signo consistente. El
   arreglo es el contador `reloj_toques`: el servicio lo muestrea al entrar y **solo
   recalcula si nadie tocó en el medio**; si alguien tocó, el 0 queda y la frontera
   siguiente corre el bloque completo — la cadencia de hoy.
2. **Los ticks pendientes deben aplicarse antes de que una escritura on-chip aterrice.**
   `regmap_write` ahora llama a `reloj_sincronizar_ticks()` antes de despachar: el delta
   acumulado desde el último servicio corre sobre el estado *viejo* del registro, y tras
   sincronizar el único pendiente es el grano en curso — igual que hoy, donde nunca hay más
   de un grano pendiente. Sin esto, arrancar un canal por TSTR le acreditaba de una hasta
   16 granos de cuenta.
3. **Una escritura del SH-4 al AICA puede mover la línea hacia el ASIC ahora mismo**
   (`pedir_int()` con MCIEB habilitada, el bit 5 de MCIPD, el acuse de MCIRE que la baja) y
   la entrega vive en el bloque periódico: `aica_escribir()` invalida. El ARM no necesita
   nada — corre adentro de `aica_tick()`, dentro del servicio, y la entrega sale en ese
   mismo servicio.

**La lectura de TCNT/WTCNT sincroniza primero** (`regmap_read` → `reloj_sincronizar_ticks()`),
así el guest que sondea ve el valor de la última frontera consumida — ni más fresco ni más
viejo que hoy. La sincronización es segura a mitad de instrucción porque `tmu_tick()` y
`wdt_tick()` solo dejan banderas; la entrega sigue siendo del bloque (la regla de siempre:
los periféricos no entregan su propia interrupción).

### Los números

- **Exactitud**: las nueve compuertas canónicas (capturas de DOOM ×3 formas, SR2, `.wav` de
  CT y modplug, palanca 0/1) con totales al dígito en los árbitros inmunes al pad; CT dentro
  de su bimodalidad documentada con el `.wav` byte a byte — una corrida dio otro `.wav` en
  la ventana ruidosa de la máquina y las dos re-corridas en quieto devolvieron el canónico,
  que es exactamente el criterio del árbol para el pad. **Barrido KOS completo: 139 de 139
  demos idénticas** entre palanca 0 y 1. `ctest` 23/23 con cinco casos nuevos
  (`tmu_proximo`/`wdt_proximo`).
- **Tiempo** (tanda reentrenada, palanca dentro del mismo binario, orden alternado): **SR2
  −0,7/−1,0 %** (6/6 rondas limpias a favor), **CT −0,5 %** (rangos disjuntos en la tanda
  con la máquina quieta), **DOOM neutro** — perdía +0,8 % consistente hasta memoizar la
  inversa del AICA en `reloj_calcular()` (dos divisiones de 64 bits que solo cambian al
  producirse una muestra); DOOM es el guest denso en invalidaciones (tick de WinCE por
  `regmap_write`, flujo de la lectora) y por eso paga más servicios.
- **El techo era conocido**: el reparto de la fase 0 dejó el bloque periódico *neto* en
  5-7,4 % — la mayor parte del balde del perfil es `aica_tick` con el ARM adentro, trabajo
  exacto que no se puede saltear. Lo elidible es la tajada ociosa fina, y eso es lo que la
  fase recoge.

### La palanca

`DCEMU_SIN_RELOJ_EVENTOS=1` deja el vencimiento en 0 para siempre: el bloque completo corre
en cada frontera, **el comportamiento anterior bit a bit** (el barrido lo probó sobre las
139). Viene **encendido por omisión**. Con `--hilos` el camino del hilo del AICA no calcula
vencimientos y el interruptor queda inerte.

## El reloj por eventos bajo `--hilos` (2026-09-05, noche)

Desde la fase 5 el hilo del AICA apagaba el reloj por eventos («ese camino publica trabajo en
cada frontera y no tiene vencimiento que calcular»), y mientras `--hilos` fue una palanca eso
no importó. La misma noche en que pasó a ser la omisión, el reparto `--perf` del canónico
nuevo puso el bloque periódico en **12,6 % de Crazy Taxi, 10,9 % de DCDoom y 9,1 % de Sega
Rally 2** — un servicio por grano, 90 millones en 180 s de CT contra 16 millones con el reloj
sin hilos —, y el candidato se escribía solo.

**El cambio son diez líneas de `reloj_calcular()`.** La premisa de la fase 5 era falsa a
medias: la publicación al hilo no necesita cada grano, necesita cada vencimiento — el hilo
mezcla hasta el objetivo publicado, sea de hace un grano o de hace dieciséis, y `entrar()`
publica por su cuenta antes de un acceso. Lo único que cambia bajo hilos es de dónde sale el
término del AICA: sin hilos es la muestra que el tick de este hilo no mezcló todavía
(`aica_muestras_hechas() + 1`, cuyo piso se autocorrige un grano después si cae un ciclo
antes del borde); con hilos esa cuenta es del otro hilo y va atrasada, así que el término sale
**del reloj** — `aica_primer_reloj_de_muestra(aica_muestras_al_reloj() + 1)`, el primer ciclo
en que la cuenta de muestras del reloj llega a la siguiente —, que es exactamente el borde en
que la entrega determinista de la línea avanza su horizonte y el borde que cruza el objetivo
que despierta al hilo. Puro valor calculado, función de `reloj_total`. La palanca es la de
siempre: `DCEMU_SIN_RELOJ_EVENTOS=1` reproduce bajo hilos la conducta anterior (un servicio por
grano).

**La compuerta**, sobre el canónico reentrenado con el cambio (`FF19A0810D05593E`; clang
avisó al enlazar que el perfil viejo ya no calzaba con `main_loop` — «control flow change
detected, count discarded» — y una tanda sin reentrenar habría medido el lazo caliente sin
perfil): cinco brazos verdes, d0 IGUAL a `build-ref` en SR2, DCDoom y CT hasta la lista de
entregas, d1-con ≡ d1-sin, d1-conB ≡ d1-con, y el control d0-con de SR2 sigue DISTINTO.

**El humo**, Crazy Taxi 60 s con `--perf`: servicios **30 millones → 5,3 millones**, bloque
periódico 12,6 % → 8,0 %, 3,52× tiempo real. **Y la tanda no lo vio** (un binario, `--hilos`
en los dos brazos, eventos contra `DCEMU_SIN_RELOJ_EVENTOS=1`, cuatro rondas rotadas, en
reposo y sin usuario):

| guest | eventos | cada grano | veredicto |
| --- | --- | --- | --- |
| DCDoom, 35 s | 20 990–21 129 ms | 20 963–21 094 | +0,2 %, 0/4, solapados: inerte |
| Sega Rally 2, 60 s | 42 618–43 313 | 43 232–44 099 | −1,5 %, **4/4**, solape de 81 ms: dirección clara |
| Crazy Taxi, 180 s | 58 261–59 037 | 58 911–59 402 | −0,8 %, 3/4, solapados |

Rondas: DCDoom 21 129 / 20 990 / 21 022 / 21 101 contra 21 094 / 20 976 / 20 963 / 21 075;
SR2 42 618 / 42 934 / 42 810 / 43 313 contra 43 494 / 43 433 / 43 232 / 44 099; CT 58 812 /
58 261 / 58 543 / 59 037 contra 58 924 / 59 391 / 59 402 / 58 911. Totales al dígito en las
24 corridas.

**Por qué el humo mintió, y es la lección de B.3 con el instrumento como protagonista.**
`--perf` cronometra el bloque por muestreo — una entrada de cada 1021 paga dos marcas y el
intervalo se multiplica —, así que la estimación de **cada** servicio lleva adentro la latencia
de una marca: 23 ns calibrados, que sobre 90 millones de servicios son 2,1 s de los 7,9 que el
reparto atribuía al bloque. Desde esa noche `--perf` calibra esa latencia al arrancar, la
imprime bajo el bloque («de eso el instrumento») y la descuenta del resto. Y los 5,8 s que
quedan tampoco eran del bloque, o la tanda los habría visto: un servicio **medido** corre
serializado detrás de la marca, mientras que el servicio vacío por grano — publicar un
`volatile`, mirar el horizonte memoizado, cuatro comparaciones — son cargas y comparaciones
predecibles que el desorden del procesador ejecuta en la sombra del trabajo vecino, exactamente
lo que el servicio partido de B.3 encontró del lado sin hilos. El reparto vale para ordenar
candidatos; el reloj decide, y aquí decidió que el bloque por grano costaba un 1 % y no un 12.

**Queda encendido**, como el atajo P1/P2 de la MMU: exacto por construcción, estrictamente
menos trabajo (seis veces menos servicios en el hilo principal), y quita un caso especial — los
dos caminos, con y sin hilos, corren ahora el mismo reloj —, con la palanca para el A/B. Y la
regla nueva para el reparto: **un porcentaje de `--perf` sobre un contador de decenas de
millones de marcas lleva adentro el costo de marcar**, y antes de creerle hay que restarle el
observador o, mejor, preguntarle al reloj.
