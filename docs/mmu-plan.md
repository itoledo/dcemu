# Plan: emular la MMU del SH-4

Estado: **fases 1 a 5 implementadas**. Julio de 2026, sobre `master` (`39d76c8`). Faltan la
6 (store queues) y la 7 (búsqueda de instrucción). Ver [Lo que quedó](#lo-que-quedó) al
final, incluida la advertencia sobre qué se pudo verificar y qué no.

## Antes que nada: esto no arregla el arranque de KOS

Conviene decirlo al principio para que nadie llegue al final del plan esperando otra cosa.

**KOS no usa la MMU.** `kernel/arch/dreamcast/kernel/init.c` no la menciona; `mmu_init()`
está exportada pero solo la llaman los propios ejemplos `basic/mmu/nullptr` y
`basic/mmu/pvrmap`. El arranque normal deja `MMUCR.AT=0` y trabaja con P1/P2 directo. Lo
mismo vale para el boot ROM y para prácticamente todo el homebrew de Dreamcast.

Y los 512 accesos a `0xF4000000`-`0xF40004A0` que aparecen en la traza al correr un binario
de KOS **no son la MMU**: son el *operand cache address array*, o sea invalidación de caché
escribiendo tags a mano. Comparten la ventana de control P4 con los arreglos de la TLB, y
por eso la fase 1 de este plan los cubre, pero son otro capítulo del manual y muchísimo
menos trabajo.

Entonces, ¿para qué hacer esto? Por tres razones, ninguna urgente:

- Es el último bloque grande del SH-4 que falta. `tests/README.md` ya lista "sin MMU ni
  caché detrás de `LDTLB`/`OCB*`" como una de las tres desviaciones deliberadas respecto
  del manual.
- Habilita los ejemplos `basic/mmu/*` de KOS, que son un banco de pruebas honesto y chico.
- Es el prerrequisito de cualquier intento futuro de correr Linux-dc o dcplaya.

Si lo que se quiere es que arranque KOS, el plan equivocado es este. El correcto empieza por
averiguar quién escribe código ejecutable en `0x8C00B6B8`.

## Objetivo

Que `dcemu` traduzca direcciones por TLB cuando el software enciende `MMUCR.AT`, y que
levante las excepciones correspondientes de forma que la instrucción que falló se pueda
reejecutar.

Cuatro hitos observables, en orden:

| hito | qué se ve | fases |
| --- | --- | --- |
| **A** | la ventana P4 deja de perder accesos; la traza no se inunda con `0xF4` | 1 |
| **B** | `LDTLB` carga de verdad y los arreglos de la TLB se leen igual que se escribieron | 2 |
| **C** | con `AT=1` una dirección P0 traducida llega a la RAM correcta | 3-4 |
| **D** | `basic/mmu/nullptr` de KOS atrapa el acceso a NULL y sigue corriendo | 5-7 |

El hito D es el que importa: es "la MMU funciona". El A y el B son ganancia inmediata
independiente de todo lo demás.

## Punto de partida

Lo que hay hoy, medido sobre el árbol actual:

| pieza | estado |
| --- | --- |
| `LDTLB` | `syscontrol.c:67`, no-op con comentario que lo admite |
| `OCBI` | fila a `nop` en `opcodes.c:190` |
| `OCBP` / `OCBWB` | stubs que solo avanzan el PC |
| registros MMU | solo `PTEL` está enlazado (`mem.c:235`). `PTEH`, `TTB`, `TEA`, `MMUCR`, `PTEA` no existen |
| `EXPEVT` | existe (`regmem[0x24]`) pero nadie lo escribe |
| entrada a excepción | `intc()` solo hace el camino de interrupción: `SSR=SR`, `SPC=PC`, `INTEVT`, `PC = VBR + 0x600` |
| despacho de direcciones | por el byte alto de la dirección **virtual**; P1/P2/P3/U0 se resuelven espejando `mem_zone[]` (`mem.c:362`) |
| búsqueda de instrucción | `main_loop()` hace `core.execute(*(WORD *) get_memory_pointer(PC))`, sin pasar por los handlers |

Ese último punto es el que más duele: hay 41 usos directos de `get_memory_pointer` fuera de
`mem.h` (12 en `mem.c`, 9 en `branch.c`, 8 en `graficos.c`, 7 en `main.c`, 4 en `sh4emu.c`,
1 en `intc.c` y 1 en `syscontrol.c`). Cada uno es un camino que se saltea cualquier
traducción que se agregue.

## El problema de verdad: reejecución

No es la TLB. La TLB son 68 estructuras y un `for`. El problema es que **las excepciones de
MMU del SH-4 son reejecutables**: el manejador arregla la tabla de páginas y hace `RTE`, y
la instrucción que falló se vuelve a ejecutar entera desde cero.

dcemu no está preparado para eso. Cada handler avanza su propio `PC` y varios mutan
registros *antes o alrededor* del acceso a memoria:

- `MOV.L @Rn+, Rm` incrementa `Rn` — si el acceso falla después, `Rn` ya quedó tocado.
- `MOV.L Rm, @-Rn` decrementa `Rn` antes de escribir.
- `MAC.L @Rm+, @Rn+` toca dos registros y hace dos accesos.

Si la excepción se levanta a mitad de camino y el manejador reejecuta, esos registros se
aplican dos veces. Es corrupción silenciosa, del peor tipo para depurar.

Hay tres salidas:

1. **Verificar antes de mutar.** Traducir primero, mutar después, en cada handler que toque
   memoria. Es lo correcto pero obliga a auditar `mov.c` entero (35 accesos), más
   `floatsimple.c`, `logic.c`, `arith.c`, `dcopcodes.c` y `syscontrol.c`.
2. **Instantánea y restauración.** `main_loop()` guarda `R[]`, `PC` y `SR` antes de cada
   instrucción; ante una falla se restaura y se entra a la excepción. Son unos 72 bytes de
   copia por instrucción.
3. Soportar solo el subconjunto que no puede fallar. No sirve: `nullptr` de KOS falla
   justamente en un `MOV.L`.

**Se elige la 2, condicionada a `MMUCR.AT`.** Con la MMU apagada —o sea, siempre, en todo
lo que corre hoy— no se saca ninguna instantánea y el camino rápido queda idéntico al
actual. La copia solo aparece cuando el software encendió la MMU, que es exactamente cuando
está dispuesto a pagarla. Esto también protege el objetivo de no romper `roto.bin`.

La 1 queda como mejora posterior si el costo llega a molestar.

## Fase 0 — Decisiones de base

- **Costo cero con la MMU apagada.** `traducir()` empieza con `if (!mmu_activa) return
  addr;` y `main_loop()` no toma instantáneas. Ninguna corrida actual debe cambiar ni en
  comportamiento ni en velocidad medible.
- **La MMU va en archivos propios**, `mmu.c` / `mmu.h`, no dentro de `mem.c`. `mem.c` ya
  tiene 1800 líneas y el `switch` de `pvr_write`; meterle la TLB lo vuelve inmanejable.
  Mismo criterio que se usó con `gdrom.c`.
- **Sin SDL.** La TLB y la traducción son lógica pura sobre registros, así que `tests/`
  las prueba sin abrir ventana, igual que los opcodes y que `sistema`/`gdrom`.
- **Primero la ventana P4, después la traducción.** Los arreglos de caché y TLB se leen y
  se escriben aunque no haya traducción, y son lo que el software toca hoy. Dan hito
  observable en la fase 1.
- **Reset ante multiple hit.** El manual manda reset del procesador si dos entradas de la
  UTLB coinciden. Emular eso de verdad es inútil para depurar: se registra fuerte por
  `traza.c` y se sigue con la primera coincidencia.

## Fase 1 — La ventana de control P4

Lo más barato y lo único con efecto inmediato sobre lo que corre hoy.

### 1.1 Registros de la MMU

Enlazar en `regmem_setup()` (`mem.c`), junto a `PTEL` que ya está:

| registro | dirección | offset en `regmem` |
| --- | --- | --- |
| `PTEH` | `0xFF000000` | `0x000000` |
| `PTEL` | `0xFF000004` | `0x000004` (ya existe) |
| `TTB` | `0xFF000008` | `0x000008` |
| `TEA` | `0xFF00000C` | `0x00000C` |
| `MMUCR` | `0xFF000010` | `0x000010` |
| `PTEA` | `0xFF000034` | `0x000034` |

Declararlos `extern` en `sh4emu.h` como el resto. Escribir `MMUCR` con `AT=1` por ahora
solo registra un aviso; encenderla de verdad es la fase 3.

### 1.2 Arreglos de caché

Zonas `0xF0` (IC address), `0xF1` (IC data), `0xF4` (OC address) y `0xF5` (OC data).
dcemu no emula caché, así que:

- Escritura: se acepta y se descarta. La *associative purge* (bit 3 de la dirección) no
  tiene nada que purgar.
- Lectura: devolver el tag con `V=0`, o sea "línea inválida". Es la respuesta honesta para
  un emulador sin caché y evita que el software crea que hay algo que volcar.

Esto es lo que apaga las 512 líneas de traza por corrida.

### 1.3 Arreglos de la TLB

Zonas `0xF2` (ITLB address), `0xF3` (ITLB data 1 y 2), `0xF6` (UTLB address) y `0xF7`
(UTLB data 1 y 2). Quedan enchufadas a las estructuras de la fase 2. Hasta entonces,
respaldo plano que devuelve lo que se escribió.

## Fase 2 — La TLB como dato

Sin traducir todavía. Solo estructuras y las dos formas de cargarlas.

### 2.1 Estructuras

ITLB de 4 entradas, UTLB de 64. Campos por entrada: `VPN`, `ASID`, `V`, `PPN`, `SZ`
(1K/4K/64K/1M), `PR`, `C`, `D`, `SH`, `WT`, y en la UTLB además `TC` y `SA`.

Guardarlas desempaquetadas, no como los `DWORD` crudos del manual: el desempaquetado en
cada búsqueda es justo lo que no se quiere pagar por acceso.

### 2.2 `LDTLB`

Reemplazar el no-op de `syscontrol.c:67`. Carga `UTLB[MMUCR.URC]` desde `PTEH` (VPN, ASID),
`PTEL` (PPN, V, SZ, PR, C, D, SH, WT) y `PTEA` (TC, SA). Sin efectos secundarios sobre
`URC` — el contador lo mueve la búsqueda, no la carga.

### 2.3 Acceso por memoria

Las zonas de 1.3 leen y escriben estas mismas estructuras, con empaquetado y
desempaquetado. Ojo con el modo asociativo de la UTLB address array: escribir con el bit
`A` puesto busca por VPN en vez de indexar, y es lo que usa el software para invalidar una
página puntual.

Todo esto es lógica pura: se prueba entero en `tests/test_mmu.c` sin traducir nada.

## Fase 3 — Traducción

Una función, `mmu_traducir(DWORD addr, int tipo)`, con `tipo` en {lectura, escritura,
búsqueda de instrucción}. Devuelve dirección física o señala falla.

### 3.1 Decodificación de regiones

| región | rango | con `AT=0` | con `AT=1` |
| --- | --- | --- | --- |
| P0/U0 | `0x00000000`-`0x7FFFFFFF` | directo | traducido |
| P1 | `0x80000000`-`0x9FFFFFFF` | directo, cacheado | igual, nunca traducido |
| P2 | `0xA0000000`-`0xBFFFFFFF` | directo, sin caché | igual, nunca traducido |
| P3 | `0xC0000000`-`0xDFFFFFFF` | directo | traducido |
| P4 | `0xE0000000`-`0xFFFFFFFF` | control | control |

P1 y P2 nunca pasan por la TLB, que es exactamente por qué KOS puede ignorarla. En modo
usuario (`SR.MD=0`) solo P0/U0 es accesible; tocar el resto es error de dirección.

### 3.2 Búsqueda

Por VPN y ASID, respetando `SH` (páginas compartidas ignoran el ASID) y `MMUCR.SV`. La
máscara depende de `SZ`. La ITLB se consulta primero para instrucciones y, ante fallo, se
recarga desde la UTLB por hardware — ese recargo no genera excepción, es el
comportamiento normal del SH-4.

### 3.3 Protección

`PR` decide lectura/escritura y usuario/privilegiado. `D=0` en una escritura es *initial
page write*, no violación de protección: son códigos distintos y el software los trata
distinto.

## Fase 4 — Excepciones

Generalizar `intc()`, que hoy solo sabe el camino de interrupción.

### 4.1 Entrada genérica

Extraer de `intc.c` una función que reciba código y desplazamiento de vector, y haga:
`SSR=SR`, `SPC=PC`, `SGR=R15`, `EXPEVT=código`, `SR.MD=1`, `SR.BL=1`, `SR.RB=1`,
`PC = VBR + desplazamiento`. `intc()` pasa a ser un caso particular con `INTEVT` y `0x600`.

### 4.2 Vectores y códigos

| excepción | `EXPEVT` | vector |
| --- | --- | --- |
| fallo de TLB en lectura | `0x040` | `VBR + 0x400` |
| fallo de TLB en escritura | `0x060` | `VBR + 0x400` |
| primera escritura a la página | `0x080` | `VBR + 0x100` |
| violación de protección, lectura | `0x0A0` | `VBR + 0x100` |
| violación de protección, escritura | `0x0C0` | `VBR + 0x100` |
| error de dirección, lectura | `0x0E0` | `VBR + 0x100` |
| error de dirección, escritura | `0x100` | `VBR + 0x100` |

Que el fallo de TLB tenga vector propio (`0x400`) es lo que lo hace barato en hardware
real, y es lo que el manejador de KOS espera.

Además hay que dejar `TEA` con la dirección virtual que falló y `PTEH.VPN` con su VPN,
conservando el `ASID` que ya estaba.

### 4.3 Ranura de retardo

Si la instrucción que falla está en la ranura de retardo de un salto, `SPC` debe apuntar a
la **instrucción de salto**, no a la de la ranura, porque la reejecución tiene que rehacer
el salto entero. dcemu ya lleva el estado en la global `delayslot` (`sh4emu.h:146`), así
que el dato está; falta usarlo.

## Fase 5 — Reejecución

La decisión de la sección anterior, implementada.

En `main_loop()`, cuando `MMUCR.AT=1`: copiar `R[]`, `PC`, `SR` y `MACH/MACL/PR/GBR` antes
de `core.execute()`. Ante falla, restaurar y entrar a la excepción con el `PC` original.

El mecanismo de aborto puede ser `setjmp`/`longjmp` o un flag consultado al volver de
`core.execute()`. El flag es más fácil de razonar y de probar, pero obliga a que los
handlers no sigan trabajando después de un acceso fallido; `longjmp` corta de una pero
complica cualquier estado a medio construir. **Empezar por `longjmp`** y medir.

**Actualización.** El mecanismo terminó siendo `longjmp` y se mudó a `excepciones.c/h`, que
lo comparten la MMU y la FPU: `mmu_salto` es hoy `excepcion_salto`, y `mmu_exc_codigo` /
`mmu_exc_vector` son `excepcion_codigo` / `excepcion_vector`. `mmu_activa` como condición
para sacar instantánea es hoy `excepcion_vigilar`, que además mira `SR.FD` y los bits de
Enable de FPSCR. Ver `docs/sh4-conformidad.md`.

Y una corrección: **la instantánea de esta fase no restauraba los registros de punto
flotante.** Era un `memcpy` de `core.context`, y los dos bancos de FP viven fuera — el
contexto solo guarda punteros. Un `FMOV.S @Rm+,FRn` que fallara por MMU dejaba `FRn`
escrito. Lo destapó la trampa de FPU, cuyo requisito explícito es que el destino no se
actualice. Ahora los copian `excepcion_instantanea_tomar()` y `..._restaurar()`.

## Fase 6 — Store queues

`sq_write` resuelve hoy por `QACR0`/`QACR1`. Con `AT=1` las direcciones de SQ se traducen
por la UTLB como cualquier otra, y `MMUCR.SQMD` decide si el modo usuario puede tocarlas.
Hay que respetar ambas cosas sin romper `pref142()`, que es el camino por el que entra toda
la geometría al TA.

**Hecha para el volcado (2 de agosto de 2026).** `mmu_traducir_sq()` en mmu.c: con `AT=1`
el `PREF` sobre `0xE0000000-0xE3FFFFFF` traduce por la UTLB la VA COMPLETA de la SQ —
QACR no participa — como escritura, con SQMD. La primera versión enmascaraba antes de
traducir (la fórmula de QACR) y el manejador de recarga de Windows CE recibía un fallo por
una VPN de la ranura 1 que sus tablas no mapean: el blit de video de ddhal.dll moría por
violación de acceso. El manejador de CE atiende las VA de SQ por una rama propia
(`8c012540`): plantilla de PTE que deja `SetStoreQueueBase` más los bits 25-20 de la VA —
así una misma base sirve `0xE2xxxxxx` → FIFO de polígonos (`0x10000000`), `0xE3xxxxxx` →
camino directo de textura (`0x11xxxxxx`) y `0xE0Cxxxxx` → RAM. Lo que queda de la fase:
SQMD sobre las ESCRITURAS al buffer (la `MOV` a `0xE3xxxxxx`, que entra por `sq_write()`
sin pasar por la traducción); CE corre con SQMD en 0 y no lo distingue.

## Fase 7 — Búsqueda de instrucción

El último eslabón y el más invasivo: `main_loop()` hace hoy
`core.execute(*(WORD *) get_memory_pointer(PC))`, que se saltea todo.

Con `AT=1` hay que traducir el `PC` y contemplar el fallo de ITLB. Para no pagarlo por
instrucción, cachear la traducción de la página actual y revalidarla solo al cruzar el
límite de página o al escribir en `MMUCR`/`PTEH`.

Los 9 usos de `get_memory_pointer` en `branch.c` son parte de esto: los saltos también
buscan instrucciones.

## Cómo se prueba

**Unitario**, en `tests/test_mmu.c`, sin SDL:

- empaquetado y desempaquetado de entradas contra los ejemplos del manual
- `LDTLB` deja en `UTLB[URC]` lo que había en `PTEH`/`PTEL`/`PTEA`
- acceso asociativo a la UTLB address array invalida la entrada correcta
- traducción acierta y falla donde debe, para los cuatro tamaños de página
- `SH` y `ASID`: una página compartida acierta con otro ASID; una no compartida, no
- la matriz completa de `PR` × modo × tipo de acceso
- `D=0` en escritura da `0x080` y no `0x0C0`
- cada excepción deja `EXPEVT`, `TEA` y `PTEH.VPN` correctos y salta al vector correcto
- una falla en ranura de retardo deja `SPC` en la instrucción de salto

La suite `cobertura` va a marcar `LDTLB` en cuanto deje de ser un no-op, así que el caso
`ldtlb_solo_avanza` de `tests/test_syscontrol.c:112` hay que reescribirlo, no borrarlo.

**Extremo a extremo**: los dos ejemplos de KOS, que ya están compilados en
`/opt/toolchains/dc/kos/examples/dreamcast/basic/mmu/`:

- `nullptr` — mapea para atrapar el acceso a NULL. Es la prueba de aceptación real: si el
  manejador arregla y reejecuta, la reejecución funciona.
- `pvrmap` — mapea la RAM del PVR a P0.

Con la salvedad de que hoy **ningún** binario de KOS arranca en dcemu, así que estos dos
ejemplos solo sirven como prueba una vez resuelto ese otro problema. Hasta entonces, el
hito D no es verificable y la fase 7 se valida solo por unitarias.

## El riesgo real

No es la TLB ni la traducción; eso es mecánico y está bien documentado.

1. **La reejecución.** Es la única parte que toca el diseño del intérprete y no solo agrega
   código. Si la instantánea resulta cara o incompleta, hay que caer a la opción 1 de la
   sección de arriba, que es auditar `mov.c` entero.
2. **Los 41 `get_memory_pointer` sueltos.** Cada uno que quede sin traducir es un bug que
   solo aparece con la MMU encendida, o sea en el único caso que nadie prueba a diario.
   Conviene renombrar la macro actual a `get_memory_pointer_fisico()` y que el compilador
   marque los 41 sitios, en vez de buscarlos a ojo.
3. **Que no se pueda verificar.** El hito D depende de que KOS arranque, que es otro
   proyecto. Se puede terminar con la MMU escrita, con unitarias en verde, y sin una sola
   prueba de que sirve para algo real.

El punto 3 es el argumento más fuerte para no empezar esto todavía.

## Estimación

| fase | trabajo |
| --- | --- |
| 1 — ventana P4 | chica, y con beneficio inmediato aunque no se siga |
| 2 — TLB como dato | chica, toda unitaria |
| 3 — traducción | mediana |
| 4 — excepciones | mediana; refactor de `intc()` que sirve igual para lo demás |
| 5 — reejecución | **la grande**, y la de riesgo |
| 6 — store queues | chica |
| 7 — búsqueda de instrucción | mediana, invasiva |

Las fases 1 y 2 se pueden hacer sueltas, tienen valor propio y no comprometen a nada. Las
fases 3 a 7 solo tienen sentido juntas.

## Recomendación

Hacer **la fase 1 ahora** y parar ahí. Es barata, apaga el ruido de la traza que estorba
para depurar el arranque de KOS, y deja los registros de la MMU enlazados para cuando haga
falta.

Las fases 2 a 7 conviene dejarlas hasta que haya un binario de KOS corriendo en dcemu,
porque sin eso el hito D no se puede verificar y se estaría escribiendo a ciegas.

## Lo que quedó

Primero se hizo la fase 1 sola, siguiendo la recomendación de arriba. Después se pidió
corregir `AT` y `LDTLB`, que era exactamente lo que la fase 1 dejaba mintiendo, y eso
arrastró las fases 2 a 5.

### Archivos nuevos

- `mmu.c` / `mmu.h` — la ventana de control P4. Sin SDL ni OpenGL, para que
  `tests/` la pueda enlazar, igual que `sistema.c` y `gdrom.c`.
- `tests/test_mmu.c` — 14 casos, suite `mmu`, prueba de CTest `dc.mmu`.

### Fase por fase

**1.1 — Registros.** `PTEH`, `TTB`, `TEA`, `MMUCR` y `PTEA` enlazados en `regmem_setup()`
junto al `PTEL` que ya estaba, y declarados en `sh4emu.h`. Los atiende `regmap_read` /
`regmap_write` como al resto del bloque; lo único que hacía falta era que existieran.

`regmap_write` gana un `case 0x000010` para MMUCR que hace dos cosas: si viene `TI` invalida
la TLB entera y limpia el bit —es de un solo disparo—, y si viene `AT` avisa una vez por
`stderr`, fuera de `LOGGING`, porque es la señal de que el programa emulado va a hacer algo
que dcemu no sabe hacer.

**1.2 — Arreglos de caché.** Zonas `0xF0`, `0xF1`, `0xF4` y `0xF5`. Las lecturas devuelven
0, o sea etiqueta 0 con `V=0`: toda línea inválida. Las escrituras se descartan. La purga
asociativa no es una simplificación sino la respuesta correcta —dcemu escribe siempre
directo a memoria, así que nunca hay línea sucia que volcar.

**1.3 — Arreglos de la TLB.** Zonas `0xF2`, `0xF3`, `0xF6` y `0xF7`, respaldadas por seis
arreglos de `DWORD` crudos. Índice por bits 9-8 en la ITLB y 13-8 en la UTLB; el bit 23
separa los dos arreglos de datos.

**Una desviación deliberada respecto del plan:** la escritura asociativa a la UTLB
(bit A) se implementó de verdad —búsqueda por VPN y ASID, respetando `SH`— en vez de
dejarla como respaldo plano. Tratarla como acceso indexado habría pisado una entrada
distinta de la que pidió el software, y un respaldo que miente no sirve ni para probar.
Son diez líneas y deja hecha esa parte de la fase 2.

### Qué se verificó

- Las 14 pruebas de la suite `mmu` pasan, y las 14 pruebas de CTest siguen en verde.
- `dcemu --traza-mem hello.bin`, con un binario de KOS: la traza pasó de **44821 bytes y
  730 líneas a 9580 y 220**. Desaparecieron las 512 líneas de `0xF4xxxxxx` del barrido de
  la caché de operandos. Quedan `8dffffff` (×8) y `adffffff` (×2), que son el sondeo del
  tamaño de RAM más allá de los 16 MB y son otro asunto.
- No aparece el aviso de `MMUCR.AT`, lo que **confirma sobre el emulador** lo que este
  documento afirma al principio: KOS no enciende la MMU.
- `dcemu roto.bin` sigue dibujando la rotozoomer, verificado por volcado F5.

### Fases 2 a 5

**2 — LDTLB.** `syscontrol.c` deja de tener un no-op: `mmu_ldtlb()` carga `UTLB[MMUCR.URC]`
desde `PTEH` (VPN, ASID), `PTEL` (PPN, V, SZ, PR, C, D, SH, WT) y `PTEA` (TC, SA). No mueve
`URC`, que lo mueve la búsqueda, no la carga. El caso `ldtlb_solo_avanza` de
`tests/test_syscontrol.c` se reescribió como `ldtlb_carga_la_tlb`, porque su comentario
—"sin MMU emulada no hay TLB que cargar"— pasó a ser falso.

También se modeló que **V y D son un solo bit del chip** visible desde las dos mitades de
la entrada, en posiciones distintas (8 y 9 en direcciones, 8 y 2 en datos 1): escribir una
mitad se ve en la otra.

**3 — Traducción.** `mmu_traducir()` resuelve P0/U0 y P3 recorriendo las 64 entradas de la
UTLB, con los cuatro tamaños de página, `SH`, `ASID`, `MMUCR.SV` y la matriz completa de
`PR`.

Dos decisiones que no estaban en el plan y hacen falta explicar:

- **La traducción sale por la ventana P2**, o sea física `| 0xA0000000`. La tabla de zonas
  de dcemu mezcla bindings físicos (`0x00` BIOS, `0x0C` RAM, `0x10` TA) con bindings P2
  (`0xA0` PVR y bloque de control, `0xA4`/`0xA5` vídeo, `0xAC` RAM), y P2 es la única
  ventana con cobertura completa: el bloque `0x005Fxxxx` lo atiende `pvr_read` en la zona
  `0xA0` y no `bios_read` en la `0x00`. Como no hay caché emulada, salir por la ventana sin
  cachear no tiene efecto observable.
- **P1 y P2 se devuelven sin tocar**, no convertidas a física. Convertirlas rompería el
  despacho justamente por lo anterior: `mem_hash_read[0x80]` es `bios_read` y
  `mem_hash_read[0xA0]` es `pvr_read`, o sea que la misma dirección física se atiende
  distinto según la ventana. Dejarlas como están conserva el comportamiento actual exacto.

**4 — Excepciones.** Se extrajo `excepcion_entrar(codigo, vector)` de `intc.c`: guarda
`SSR`/`SPC`/`SGR`, deja `EXPEVT` y salta a `VBR + vector`. `intc()` pasó a ser el caso
particular con `INTEVT` y `0x600`. El fallo además deja `TEA` con la dirección y `PTEH.VPN`
con su VPN, conservando el `ASID`.

**5 — Reejecución.** `main_loop()` tiene ahora tres caminos. Con `mmu_activa` en cero no
saca instantánea ni arma nada: es el camino de siempre. Con la MMU encendida, copia
`core.context` entero, arma `setjmp` y ejecuta; ante un fallo `mmu_traducir()` hace
`longjmp`, se restaura la instantánea y se entra a la excepción.

**La ranura de retardo salió gratis.** Los saltos de `branch.c` ejecutan la ranura con un
`core.execute()` anidado, así que el `longjmp` desenrolla los dos niveles y la instantánea
restaurada deja `PC` en la instrucción de salto —que es justo lo que `SPC` tiene que valer
para que el `RTE` rehaga el salto entero.

### El punto delicado: qué acceso pasa por la MMU

`memread`/`memwrite` ahora traducen, y se agregaron `memread_fisico`/`memwrite_fisico` para
el acceso interno del emulador. Se eligió que **el camino traductor conserve el nombre
corto** a propósito: así los ~110 accesos de los handlers de instrucciones quedan
traduciendo sin tocarlos, y el modo de fallar es el benigno. Al revés —que los handlers
tuvieran que optar por traducir— olvidarse de uno sería un acceso del programa emulado que
se saltea la MMU en silencio, que es mucho peor de encontrar.

Se convirtieron a `_fisico` los sitios que llevan direcciones ya resueltas **y corren
dentro de una instrucción**, que son los que de verdad importan:

| sitio | por qué |
| --- | --- |
| DMA del Maple en `mem.c` (9 accesos) | lista de comandos con direcciones físicas, disparada por escritura a registro |
| DMA del GD-ROM en `gdrom.c` | `SB_GDSTAR` es física, y lo dispara `SB_GDST` |
| plano de fondo y `TA_ISP_BASE` en `graficos.c` (3) | salen de registros del PVR |
| DMAC en `main.c` | `SAR`/`DAR` son físicas, y corre entre instrucciones |

El resto de los usos internos (carga de archivos, hooks de syscall, vista de depuración)
trabaja con direcciones `0x8C...`, o sea P1, que nunca se traduce: pasarlas por el camino
traductor no cambia nada.

### Qué se verificó

- **24 casos** en la suite `mmu` (14 de la fase 1 más 10 nuevos: LDTLB, propagación de V/D,
  P1/P2 sin traducir, acierto en P2, los cuatro tamaños de página, ASID, fallo con
  `EXPEVT`/vector/`TEA`/`PTEH`, primera escritura contra violación de protección, matriz de
  `PR`, y modo usuario contra página privilegiada). Las 14 pruebas de CTest en verde.
- `dcemu roto.bin` sigue dibujando la rotozoomer, verificado por volcado F5, y **sin
  ningún aviso de MMU** en `stderr`.

### Lo que NO se verificó, y es la advertencia importante

**No hay una sola prueba de que esto funcione sobre un programa real.** El hito D
—`basic/mmu/nullptr` de KOS atrapando el acceso a NULL, arreglando la tabla y reejecutando—
sigue sin poder correrse, porque ningún binario de KOS arranca en dcemu. Es exactamente el
riesgo 3 que este documento anticipó: la MMU está escrita, las unitarias pasan, y no hay
evidencia de que sirva.

En particular, **la reejecución solo está probada por construcción**, no por observación.

**Actualización: ya no, y por dos caminos.**

`demos/fpu-trampa` ejercita el mecanismo compartido —instantánea, `longjmp`, restauración,
entrada a la excepción— de punta a punta sobre KallistiOS: el manejador vuelve sin saltar la
instrucción y la reejecución tiene que completarla. La falta la levanta la FPU y no la MMU,
pero el camino es el mismo. Y encontró el bug de los bancos de punto flotante que la
reejecución arrastraba desde esta fase.

`demos/mmu-mapeo` cierra el hito D por el lado de la MMU: mapea una página virtual a una
física de la RAM del sistema, escribe por la virtual —lo que falla en la TLB—, y comprueba
por la dirección física, en P1 y sin traducir, que la escritura llegó. Después escribe por la
física y lee por la virtual, para descartar que sean dos copias. Reporta `TEST SUCCEEDED!`.

Y `basic/mmu/nullptr` de KOS **ya pasaba**: su `kernel panic` es el final que la demo busca,
porque su callback devuelve `NULL` a propósito. Verifica la otra mitad —que el fallo se
levanta con el código correcto y con `SPC` en la instrucción que falló, no en la siguiente—.
Ver `docs/demos-kos.md`.

Y `basic/mmu/pvrmap` también pasa ahora. No fallaba por la MMU: fallaba por **un `memwrite`
que tenía que ser `memwrite_fisico`** en el DMA del Maple (`mem.c`, el relleno de
`0xFFFFFFFF` para un puerto sin dispositivo). Ese DMA lo dispara `pvr_write()` dentro de la
instrucción que escribe `SB_MDST`, con direcciones físicas que el guest dejó en la lista de
comandos; con `AT=1` se traducían, no estaban mapeadas y levantaban un fallo de TLB en
escritura desde el manejador de VBlank. Todo el resto del bloque ya usaba la versión física.
Ver `docs/demos-kos.md`.

Vale la pena quedarse con la forma del bug: **un `_fisico` que falta no se nota hasta que
alguien enciende la MMU.** Los otros caminos internos —el DMAC en `main.c` y el del GD-ROM en
`gdrom.c`— ya estaban bien; ese quedó atrás. Si aparecen más demos con MMU, es lo primero que
hay que revisar.

Con las dos demos pasando, lo que sigue faltando de la MMU son las fases 6 y 7, que no las
pide ninguna de ellas.

### Fase 7, hecha — y quién la pidió (2026-08-02)

Ninguna demo de KOS la pedía; **Windows CE sí**. DCDoom es un juego de CE (arranca
`0WINCEOS.BIN`, no `1ST_READ.BIN`), su kernel enciende `AT` y ejecuta los procesos de
usuario en P0 por la TLB: sin traducir la búsqueda, el PC aterrizaba en `0x00005b90`
—dentro del boot ROM físico— y ejecutaba bytes de la BIOS como si fueran el proceso.

La forma final es la que la fase anticipaba, con un cache de página en vez de pagarlo por
instrucción:

- `MMU_FETCH_PUNTERO(pc)` (mmu.h) es la macro que usan los tres sitios que buscan
  instrucciones: `main_loop()`, `EJECUTAR_RANURA()` de branch.c y la ranura del RTE. Con
  `mmu_activa` en cero es el `get_memory_pointer()` de siempre; encendida, el acierto es
  una comparación de página más una de modo (`SR.MD` participa porque la protección
  depende de él), y el fallo repuebla vía `mmu_fetch_resolver()`.
- `traducir_busqueda()` comparte con los datos el recorrido de la UTLB
  (`utlb_encontrar()`, extraído para que la regla de coincidencia no pueda divergir) y los
  códigos: fallo `0x040` por el vector `0x400`, protección `0x0A0`, error de dirección
  `0x0E0`. La ITLB real es un cache que el chip rellena solo desde la UTLB, así que se
  busca directo en la UTLB; lo único que se pierde es una escritura directa al arreglo de
  la ITLB por P4, que ningún sistema usa para mapear código.
- P1 y P2 salen sin traducir con la zona entera de 16 MB como "página" —`mem_zone[]`
  resuelve por byte alto, así que el puntero base vale para toda la zona y el kernel de CE
  (que vive en P1) acierta casi siempre.
- Invalidan el cache: `LDTLB`, las escrituras a los arreglos de la UTLB por P4, `MMUCR`, y
  `PTEH` porque lleva el ASID (un `case 0x000000` nuevo en `regmap_write`).
- **La ranura del RTE se busca ANTES de escribir `SR`**: el manual manda que el acceso a
  instrucción de la ranura use el SR previo y sus accesos a datos el nuevo. Con la MMU
  activa la diferencia se ve: el RTE del kernel hacia modo usuario tiene su ranura en una
  página privilegiada, y buscarla ya en modo usuario la rechazaría. `rte143()` captura la
  palabra primero y ejecuta después de `UpdateSR(SSR)`.

**`MMUCR.URC` avanza ahora con cada acceso a la UTLB** —acierte o falle, con vuelta a cero
al alcanzar `URB`—, que es lo que la fase 2 ya declaraba ("lo mueve la búsqueda, no la
carga") y nadie implementaba. No es un detalle: el manejador de recarga de CE no escribe
`URC` jamás y confía en ese avance para que cada `LDTLB` caiga en una entrada distinta.
Sin él, la página de código y la de datos de una misma instrucción se desalojaban
mutuamente —recarga de fetch pisa a la de datos, y al revés— en un ping-pong infinito.

Verificado: 6 casos nuevos en la suite `mmu` (traducción de búsqueda con cache, fallo por
el vector de TLB con `TEA`/`PTEH`, P1 sin traducir, protección en usuario, invalidación
por LDTLB, avance de URC), las 21 pruebas de CTest en verde, y el kernel de DCDoom
arrancando: MMU, recarga de TLB por software, syscalls por error de dirección
(`0xFFFFFxxx`), y el contexto perezoso de FPU por `SR.FD` (0x800/0x820), todo en uso real.
Ver `docs/pendientes-plan.md`, la sección de DCDoom.

### Lo que sigue faltando

- **Fase 6, la mitad que falta**: el volcado del `PREF` ya traduce por la UTLB con SQMD
  (`mmu_traducir_sq()`; el ddraw de Windows CE lo usa para todo — blits de píxeles y la
  geometría al TA), pero las ESCRITURAS al buffer de la SQ (`sq_write()`) siguen sin pasar
  por SQMD. CE corre con SQMD en 0, así que nada de lo que corre lo distingue. La nota de
  antes — "su CE no usa las SQ con la MMU activa" — era falsa: las usa para todo el video,
  y lo que la hacía parecer cierta era que el fallo con la VPN enmascarada mataba a ddhal
  antes de que se viera.
- La traducción recorre las 64 entradas desempaquetando al vuelo, ahora también en cada
  fallo de cache del fetch. Es lo más lento posible y está bien por ahora; si molesta, el
  paso siguiente es un arreglo decodificado en paralelo, actualizado en los cuatro sitios
  que mutan la TLB. Con CE encima el costo ya se ve: ~0.2-0.5× del tiempo real durante el
  arranque, entre la instantánea por instrucción y este recorrido.
- No hay chequeo de alineación, así que los errores de dirección por acceso desalineado no
  se levantan. dcemu tampoco los chequeaba antes.

## Y lo que cuesta, medido

El "~0.2-0.5×" de arriba ya tiene número y culpable, del 2026-08-04: **un guest con MMU
cuesta el doble por instrucción**. DCDoom da 49,0 MIPS —20,4 ns por instrucción— contra los
86,3 MIPS y 11,5 ns de Crazy Taxi, con el intérprete en el 92,8 % del tiempo real y el camino
gráfico en el 0,3 %. Los 8,9 ns de diferencia son la instantánea de la fase 5: `context_t`
más **los dos bancos de coma flotante**, copiados antes de cada instrucción porque
`excepcion_vigilar` está puesto en cuanto la MMU traduce.

El plan para bajarlo —no copiar los bancos de FPU salvo en instrucciones de FPU, y no sacar
instantánea en las que no pueden fallar después de mutar— está en
[`rendimiento-plan.md`](rendimiento-plan.md), fase 5, con sus barandas. La que importa aquí:
**DCDoom es el único guest del árbol que enciende la MMU**, así que el barrido de demos no
puede ver ninguna regresión de este camino.

> **Corrección del mismo 2026-08-04, con el desglose hecho: el culpable no era ése.** La
> instantánea existe, pero sus dos bancos de FPU valen **3,6 %** y el mecanismo entero no
> llega al 10 %. Lo que se lleva el tiempo es **el recorrido de la UTLB en el camino de
> datos**, que no tiene caché ninguna: 1313 millones de recorridos con 34,5 entradas de
> promedio sobre 64. Una caché de traducción de datos de 256 entradas da **1,73× en DCDoom**
> —de 0,33× a 0,58×— con la ejecución idéntica al dígito y el BMP byte a byte igual. El
> párrafo de arriba queda como estaba escrito porque el error vale más documentado que
> borrado: atribuyó los 8,9 ns a lo primero que se tenía a la vista sin separar los sumandos.
> El desglose, las sondas y el plan están en [`rendimiento-plan.md`](rendimiento-plan.md),
> fase 6.

## La etiqueta sin modo de la caché de entrada (2026-08-30)

La caché de 256 delante del recorrido de la UTLB acertaba **43,4 % en DCDoom y 57,9 % en
SR2**, y el fallo restante recorría 18-21 entradas de 64 en promedio. El censo por causa
(el molde del de `mmu_datos`, contadores `perf_mmu_ent_*` bajo `--perf`) fue en tres
escalones, cada uno matando una hipótesis:

1. **No es capacidad ni frío**: 0,1-0,3 % de ranuras sin estrenar; «otra página» 11-17 %.
   El grueso era «otra etiqueta» (73,2 % de los fallos de DOOM) y generación vencida.
2. **No son páginas compartidas**: solo 1,8 % (DOOM) / 5,2 % (SR2) de los fallos por
   etiqueta acaban en una entrada SH. La etiqueta ciega al ASID no habría recuperado nada.
3. **Es el bit de modo**: el **98,3 % (DOOM) / 91,4 % (SR2)** de los fallos por etiqueta
   son el MISMO ASID con solo el modo distinto — el vaivén usuario/privilegiado de los
   syscalls de WinCE tocando las mismas páginas, tirando el acierto en cada cruce.

Con el mismo ASID el recorrido encuentra la misma entrada en los dos modos (el ASID casa
por igualdad y la regla de espacio único no interviene), así que guardar el modo ahí solo
tiraba aciertos. Pero el bit existe por una trampa real: un llenado que casó **vía espacio
único** (sv, privilegiado, ASID de PTEH distinto del de la entrada) no puede acertarse
desde modo usuario con el PTEH casualmente igual. La solución es que la etiqueta tenga dos
formas decididas AL LLENAR: `MMU_CACHE_AMBOS` (sin modo) cuando el ASID de la entrada casó
por igualdad con PTEH o la página es SH, y la forma con modo de siempre cuando casó vía
sv. La búsqueda compara las dos formas (la segunda solo corre si la primera falló).

Medido sobre el mismo binario: aciertos **43,4 → 84,1 % en DOOM** (recorrido medio 20,9 →
8,3) y **57,9 → 72,3 % en SR2** (18,2 → 13,8), con las cuentas de búsquedas idénticas al
dígito — el control de que la ejecución no se movió. Compuerta verde (capturas y
`DCEMU_CP_MS` completos byte a byte en SR2 180 s y DOOM 35 s contra el brazo viejo), y la
tanda sobre el canónico reentrenado (`25B7366C16CED290`): **DOOM −3,0 % con rangos
disjuntos y 4/4 pares**; SR2 dentro de su dispersión (2/4, dirección favorable). CT no
corre el A/B: sin MMU, los brazos son el mismo código. `DCEMU_MMU_ETIQUETA_MODO=1` es la
palanca que reproduce la conducta anterior.

El residuo con nombre: la mitad restante de los fallos de SR2 es **generación vencida** —
los 5,4 M de LDTLB por minuto de WinCE venciendo entradas guardadas — y eso no lo arregla
ninguna etiqueta: la entrada de la UTLB realmente cambió. Es el techo de esta caché.


## El atajo de P1/P2/P4 delante de la caché (2026-09-04): exacto, neutro, y un censo que no cierra

El reparto del 4 de septiembre dejó al SH-4 como el 82,6 % de DCDoom y el 84,3 %
de Sega Rally 2, y el censo de la MMU señaló algo que el código emitido ya
resolvía y el cuerpo en C no: **`mmu_traducir()` consultaba la caché de datos
antes de descartar P1, P2 y P4**. Esas tres no se traducen y **nunca se guardan
en la caché**, así que su ranura queda sin estrenar para siempre y cada acceso
pagaba el índice, la etiqueta y las cuatro comparaciones para fallar siempre. El
código emitido tiene el orden correcto desde la fase 6, y ahí está medido que el
orden importa: detrás del fallo, DCDoom perdía la mitad.

**El cambio** es un retorno temprano delante del bloque de la caché, con el
cuerpo de cada caso idéntico al de siempre, y **P3 privilegiada sigue de largo**
porque sí se traduce por la TLB. `DCEMU_MMU_ATAJO_TARDE=1` reproduce el orden
anterior.

**Exacto**: capturas byte a byte y todos los puntos de `DCEMU_CP_MS` idénticos
entre brazos en los dos guests con MMU (DCDoom 20 000 puntos, SR2 30 000), con
el RTC clavado.

**Y neutro en el reloj, que es el veredicto.** Cuatro rondas con orden rotado
sobre un binario (`4E593E734A469A00`): DCDoom **22 814-23 116 ms** contra
**23 014-23 448** (−1,0 %) y Sega Rally 2 **46 935-47 263** contra
**47 049-47 477** (−0,35 %), los dos con 3 de 4 rondas a favor y **rangos
solapados** — por debajo del estándar estricto y también del débil. La
aritmética lo anticipaba y conviene dejarla escrita para no volver a esperar más:
son **69 584 733 accesos en 20 s emulados de DCDoom**, o sea 3,5 millones por
segundo emulado contra una tabla que vive en L1 y falla siempre igual, así que el
predictor la aprende. Eso son milisegundos sobre veintitrés segundos. Queda
encendido porque es estrictamente menos trabajo y no cuesta nada, no porque se
haya ganado una tanda.

**La pregunta que esto abrió, y su respuesta: el contador estaba roto.** El
perfil de los fallos cambiaba muchísimo entre brazos —con el atajo tarde DCDoom
repartía 10,0 % misma página / 4,4 % otra página / 85,6 % ranura sin estrenar, y
con el atajo temprano 99,3 % / 0,4 % / 0,3 %— mientras el contador de direcciones
que no se traducen daba el mismo número en los dos. Eso no cuadraba, y no cuadraba
porque **el atajo de P1/P2 del código EMITIDO contaba una traducción sin contar un
acierto**, así que cada acceso a P1/P2 —que no sondea nada— entraba al resumen
como un fallo de la caché. En DCDoom eso son **607,8 M de accesos de 1163 M en 20 s
emulados**, contra ~81 M de fallos de verdad: la tasa de aciertos salía siete veces
peor de lo que es, y «el 84,5 % de los fallos no se traduce» describía el contador
y no la caché.

Con el contador arreglado (el atajo emitido cuenta también en «no se traduce», y
el porcentaje va sobre las que sí se traducen), el diagnóstico se da vuelta:

| | DCDoom 20 s | Sega Rally 2 30 s |
| --- | --- | --- |
| accesos | 1 163 M | 2 020 M |
| de esos, no se traducen (P1/P2/P4) | **607,8 M (52 %)** | 235,1 M (12 %) |
| de los que sí, ya resueltos por la caché | **98,6 %** | **99,8 %** |
| faltas de verdad (van al manejador del guest) | 261 497 | 407 028 |

**O sea que la caché de traducciones de datos no tiene nada que ganar**: acierta
98,6 % y 99,8 %, y lo que queda son 7,8 M y 3,6 M de fallos, el 86-99 % de ellos
«misma página con otra etiqueta». Los números viejos de esta caché —incluido el
«43,4 → 84,1 %» de la etiqueta sin modo— están medidos con el denominador roto:
el salto que esa fase midió es real (el A/B fue sobre el reloj), pero su tasa
absoluta no se compara con la de esta tabla.

Y explica de paso por qué el atajo del cuerpo en C salió neutro: de los 607,8 M de
accesos que no se traducen, **534 M los resuelve el atajo emitido** y sólo 69,6 M
llegaban al cuerpo en C. El cambio ataca el 11 % del tráfico, y ese 11 % son
milisegundos.

## La etiqueta viva de la caché de traducciones (2026-09-08)

El reparto de la noche anterior dejó el frente donde había que mirar: con el AICA en su propio
hilo y el reloj por eventos corriendo, **el código emitido es el 89 % de Sega Rally 2 y el 92 %
de DCDoom**, y esos dos guests pagan 5,1 y 4,1 ns por instrucción contra los 2,8 de Crazy Taxi.
La diferencia es la traducción por acceso, y dentro de ella había algo que se hacía en cada uno
sin necesidad.

**La etiqueta de `mmu_datos` — `ASID_DE(*PTEH) | ((SR_MD == 0) << 8) | MMU_CACHE_VALIDA` — se
construía en cada acceso.** En el código emitido son diez instrucciones, y dos de ellas son
**cargas dependientes** (el puntero a PTEH, y con él PTEH) que alimentan justo la comparación
que decide el camino rápido. Es función de dos cosas que casi nunca cambian.

Ahora vive en `mmu_etiqueta` y la mantienen los **cuatro** sitios que pueden moverla: las dos
entradas de `UpdateSR()` —todo cambio de SR.MD pasa por ahí, incluida la entrada a una excepción,
que pone MD a mano y avisa—, la escritura del guest a PTEH (`regmap_write`) y el reset de la CPU.
El emitido la lee con una sola carga. Es la disciplina del límite del corte con la bandera de
reintento (`intc.h`): se mueven juntas, y el bloque periódico **verifica la coherencia una vez
por servicio** con un contador que sale en el resumen sin condición.

**Ese contador encontró un agujero el mismo día que se escribió, y por eso está.** La primera
compuerta salió verde —capturas, 40 000 puntos de control y las listas de entregas idénticas
entre brazos en los tres guests— y aun así marcaba **1 incoherencia en cada uno, Crazy Taxi
incluido, que no usa MMU**. Instrumentado: `mmu: etiqueta incoherente: 00010100 contra 00010000
(PTEH 00000000, MD 1, reloj 401)`. `mmu_reset()` recalculaba con SR todavía en cero —modo
usuario— y el SR de reset se escribe a mano en `initCpuSubSystem()` sin pasar por `UpdateSR()`,
así que **el arranque entero corría con la etiqueta del modo equivocado**. Inofensivo aquí
porque la MMU todavía no traduce, pero es exactamente la forma de falla del árbol: algo que se
acepta sin decir nada. Con el recálculo en el reset, cero.

**Medido** (canónico reentrenado `57BC6AB6B6168F89`, un binario, cuatro rondas rotadas, en
reposo y sin usuario, `herramientas/etiqueta-ab.ps1`):

| guest | etiqueta viva | construida | veredicto |
| --- | --- | --- | --- |
| DCDoom, 35 s | 20 866–20 980 ms | 21 304–21 420 | **−2,1 %, rangos disjuntos, 4/4** |
| Sega Rally 2, 60 s | 42 701–43 488 | 42 908–43 826 | −1,0 %, 3/4, solapados: dirección |
| Crazy Taxi, 180 s | 59 330–59 769 | 58 818–59 624 | inerte por construcción |

Totales de instrucciones al dígito en las 24 corridas. Crazy Taxi es **inerte por construcción
y su emisión es idéntica**: en modo plano `gen_traducir_mmu()` no se emite, así que su ±0,5 % es
el ambiente y nada más.

**Y la emisión baja sólo 64 bytes** (68 420 702 → 68 420 638 en SR2), que es la confirmación del
mecanismo: desde que la traducción se emite como **dos rutinas compartidas** (2026-08-25) la
etiqueta se construía una vez por rutina, no una por sitio, así que lo que se gana no es icache
sino la ejecución de esas diez instrucciones en cada llamada — y sobre todo la cadena de dos
cargas dependientes delante de la comparación. Que DCDoom gane más que SR2 teniendo **menos**
accesos que construyen etiqueta (el 52 % de los suyos son P1/P2 y salen por el atajo, antes de
la etiqueta) dice que lo que se quitó es latencia de camino crítico y no trabajo por volumen.

`DCEMU_MMU_ETIQUETA_CALCULADA=1` la vuelve a construir y reproduce la emisión anterior byte por
byte.

## El avance diferido de URC (2026-09-08)

El mismo camino, un escalón más abajo. `MMUCR.URC` es el contador de reemplazo de la UTLB: la
regla del manual —y el invariante de este árbol— es que **un acierto de caché sigue siendo un
acceso a la UTLB y tiene que avanzarlo**, porque es lo que decide qué entrada reemplaza el
`LDTLB` del guest, o sea su camino de ejecución. Así que cada acceso emitido pagaba, dentro de
la rutina compartida de traducción, un lectura-modificación-escritura de MMUCR: cargar el
puntero al registro, cargar el registro (dependiente), extraer URC y URB, dos ramas para el
envolvimiento y un almacenamiento. Unas veinte instrucciones, y sobre una línea de caché del
bloque de 16 MB de registros que nada más toca.

**Nadie lo mira en el camino caliente.** El valor se observa en exactamente tres sitios: el
`LDTLB` del guest (`syscontrol.c`), una lectura del guest a MMUCR (`regmap_read`) y los puntos
de control de `traza.c`. Entre observación y observación, lo único que importa es *cuántos*
avances hubo. Así que el acceso emitido suma uno a `mmu_urc_pend`, que vive junto al contexto
—un `add64 [CTX+off],1`, una instrucción—, y esos tres sitios lo materializan antes de mirar.
Una escritura del guest a MMUCR **descarta** lo pendiente en vez de aplicarlo: aplicar y después
dejar que lo pisen es pisarlo.

**La forma cerrada está probada antes que el emisor**, que es la lección que dejó DIV1. Avanzar
URC N veces no es sumar N módulo 64: URC se envuelve a cero en 64 y **si URB no es cero se
envuelve en URB**, así que la trayectoria tiene un tramo inicial distinto del régimen periódico.
`mmu_urc_tras(urc, urb, n)` cuenta los pasos hasta el primer cero (`k0`) y después toma el resto
módulo URB; `tests/test_mmu.c` la compara contra N pasos de a uno sobre el espacio **entero** —
64 valores de URC × 64 de URB × 130 de N — más dos cuentas de mil millones donde el paso a paso
no llega.

El caso viejo `urc_avanza_con_cada_acceso_a_la_utlb` también cambió, y por un motivo que vale
anotar: leía `*MMUCR` directo, que es una puerta que el guest no tiene. Ahora lee por
`urc_visto()`, que materializa primero, y las escrituras directas del arreglo llaman a
`mmu_urc_descartar()` — la misma regla que el guest.

**Medido** (canónico reentrenado `0577D82BE7D5E77B`, un binario, cuatro rondas rotadas, en
reposo y sin usuario, `herramientas/urc-ab.ps1`):

| guest | URC diferido | en cada acceso | veredicto |
| --- | --- | --- | --- |
| Sega Rally 2, 60 s | 41 264–41 634 ms | 42 542–43 133 | **−3,4 %, rangos disjuntos, 4/4** |
| DCDoom, 35 s | 19 997–20 162 | 20 628–20 716 | **−2,7 %, rangos disjuntos, 4/4** |
| Crazy Taxi, 180 s | 59 978–60 784 | 59 314–60 634 | inerte por construcción |

Totales de instrucciones al dígito en las 24 corridas, y Crazy Taxi con **cero pendientes al
salir**: en modo plano la traducción no se emite, así que su +0,4 % solapado es el ambiente.
Es la mayor ganancia de la MMU desde el atajo P1/P2, y la primera de esta serie que gana en los
dos guests con MMU.

**La emisión baja 112 bytes** (68 420 638 → 68 420 526 en SR2), o sea nada, y eso es la
confirmación del mecanismo, igual que con la etiqueta: desde que la traducción vive en dos
rutinas compartidas lo emitido se cuenta dos veces y lo ejecutado una por acceso. Lo que se
quitó son ~56 bytes de código en cada rutina que corrían en **cada** traducción, con su cadena
de cargas dependientes y su escritura a una línea fría.

**La compuerta** (`herramientas/urc-ab.ps1` para el reloj, la compuerta de la palanca para la
exactitud): captura, 25 000-40 000 puntos de `DCEMU_CP_MS` y las listas completas de
`DCEMU_SONDA_ENTREGAS` idénticas entre los dos brazos en los tres guests. Los puntos de control
imprimen MMUCR, así que comparan también el URC materializado — que es exactamente lo que había
que probar. Y encima la compuerta de juegos de cinco brazos volvió a salir verde con el binario
nuevo, con los controles de SR2 distinguiendo como siempre.

`DCEMU_MMU_URC_INMEDIATO=1` vuelve a avanzarlo en cada acceso y reproduce la emisión anterior
byte por byte.
