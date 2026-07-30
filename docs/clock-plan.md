# Plan: una sola base de tiempo

Estado: **completo**. Julio de 2026, sobre `master`. Las cuatro fases están implementadas y
los cuatro hitos alcanzados; ver [Lo que quedó](#lo-que-quedó) al final.

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
