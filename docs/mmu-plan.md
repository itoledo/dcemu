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

## Fase 6 — Store queues

`sq_write` resuelve hoy por `QACR0`/`QACR1`. Con `AT=1` las direcciones de SQ se traducen
por la UTLB como cualquier otra, y `MMUCR.SQMD` decide si el modo usuario puede tocarlas.
Hay que respetar ambas cosas sin romper `pref142()`, que es el camino por el que entra toda
la geometría al TA.

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

### Lo que sigue faltando

- **Fase 6**: las store queues resuelven por `QACR0`/`QACR1` y no respetan `MMUCR.SQMD`.
- **Fase 7**: la búsqueda de instrucción sigue siendo `get_memory_pointer(PC)` directo, sin
  traducir y sin ITLB. Los arreglos de la ITLB se leen y escriben, pero nadie los consulta.
- `MMUCR.URC` no se incrementa por búsqueda, así que no hay reemplazo por LRU.
- La traducción recorre las 64 entradas desempaquetando al vuelo. Es lo más lento posible y
  está bien por ahora; si molesta, el paso siguiente es un arreglo decodificado en paralelo,
  actualizado en los cuatro sitios que mutan la TLB.
- No hay chequeo de alineación, así que los errores de dirección por acceso desalineado no
  se levantan. dcemu tampoco los chequeaba antes.

Y sigue sin arreglar el arranque de KOS, que era el punto de la advertencia inicial.
