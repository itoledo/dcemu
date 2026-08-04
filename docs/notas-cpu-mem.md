# Notas: el núcleo SH-4, la memoria, la MMU y el UBC

Detalle de `sh4emu.c`, `opcodes.c`, `mem.c`, `mmu.c`, `excepciones.c` y `ubc.c`. `CLAUDE.md` tiene
el resumen y los invariantes; esto es la arqueología.

---

## El banco de registros: `SR.RB` no es `banco_activo`

**`SR.RB` dice qué banco *debería* estar en `registers[0..7]`; `core.context.banco_activo` anota
cuál *está*.** No son lo mismo, y confundirlos costó un juego.

Hay dos entradas — `UpdateSR_ya_escrito()`, para el llamador que escribió `SR` él mismo (entrada
de excepción, entrada de interrupción y `TRAPA`, que fijan MD/RB/BL a mano), y `UpdateSR()`, que
toma el valor nuevo — y la primera intercambiaba **incondicionalmente**. Con `RB` ya en 1 — una
excepción tomada dentro de otra, que es lo que hace cualquier manejador que baja `BL` para
permitir anidamiento — la entrada ponía `RB=1` otra vez e intercambiaba igual: el manejador
anidado veía el banco 0 como propio, y el código interrumpido volvía del `RTE` con los `R0-R7` del
otro banco. El `RTE` no lo deshacía, porque ese camino *sí* compara y `SSR.RB == SR.RB`.

Ambos caminos ahora comparan contra `banco_activo`, que `swap_registers()` es lo único que mueve.
El campo vive **dentro** de `core.context` a propósito: la instantánea de la MMU restaura el
arreglo de registros, así que el estado de banco tiene que viajar con él. `reset()` y
`arnes_reset()` lo fijan, porque ambos asignan `SR` directamente sin pasar por `UpdateSR()`.

**Esas dos entradas solían ser una función distinguida por un centinela, y el centinela era
`0xFFFFFFFF`** — que es exactamente lo que pasa `LDC Rm,SR` con `Rm` todo unos. Esa escritura se
leía como "el llamador ya escribió SR" y **SR quedaba sin tocar**. Separarlas es lo que quitó la
trampa; SingleStepTests es lo que la encontró.

**Escribir SR no es una asignación plana**: `sr_normalizar()` conserva solo `0x700083F3` — la
máscara que el manual pone en el propio `LDC Rm,SR` — y limpia `RB` cuando `MD` es 0, porque `RB`
solo existe en modo privilegiado ("in user mode, R0-R7 always refer to bank 0"). Vive en
`UpdateSR()` y no en los sitios de llamada porque es la regla para *escribir SR*, no la regla de
una instrucción: a `RTE` y a `LDC.L @Rm+,SR` les faltaba, y `RTE` copia `SSR`, que `LDC Rm,SSR`
puede llenar con cualquier cosa.

Ninguna demo de KOS muestra esto — KOS toma sus interrupciones desde `RB=0`, así que el
intercambio siempre es un cambio real. Virtua Tennis es lo que lo expuso: perdió el índice de una
tabla de callbacks a través de una interrupción anidada, llamó por un puntero nulo, cayó en la
dirección 0 — que es el boot ROM — y corrió por el bloque de sistema bajo hasta dar con los bytes
que decodifican como `TRAPA #23`. Suite `syscontrol`, caso
`trapa_desde_el_banco_1_no_vuelve_a_cambiar`.

---

## Las cuatro tablas de despacho

`initopcodes()` expande `opcodes[]` en **cuatro** tablas (`oplist_pr0_sz0`, `oplist_pr0_sz1`,
`oplist_pr1_sz0`, `oplist_pr1_sz1`), una por combinación de los bits `PR`/`SZ` del FPSCR, para que
las variantes de doble precisión y de par de flotantes de la misma codificación resuelvan sin una
comprobación por instrucción. `UpdateFPSCR()` en `sh4emu.c` reapunta `oplist` cuando esos bits
cambian e intercambia los bancos de flotantes cuando `FR` cambia; `UpdateSR()` intercambia los
registros generales bancados cuando `RB` cambia.

---

## Los espejos y las ventanas alternas no son cosméticos

El código del guest llega a la misma memoria física por varias ventanas y depende de que
concuerden:

- El `_start` de KOS dimensiona la RAM escribiendo en `0xACFFFFFF` y `0xADFFFFFF` y comprobando si
  se solapan. Sin la zona `0xAD` ligada concluía 32 MB y ponía su pila en `0x8E000000`, así que
  cada `push` se descartaba y cada `pop` devolvía basura.
- KOS sube el firmware del AICA a la RAM de sonido por la ventana *física* (`0x00800000`), no por
  `0xA0800000`. La zona `0x00` va por eso a `pvr_read`/`pvr_write`, que separan por dirección
  física y delegan el rango del boot ROM y la flash de vuelta a `bios_read()`.
- KOS sube texturas por la FIFO de textura del TA en `0x11000000` con `sq_fast_cpy()`.

Como `pvr_read`/`pvr_write` etiquetan sus casos de `switch` en forma P2 (`0xa0...`), ambos hacen
switch sobre `fisica | 0xa0000000` para que toda ventana resuelva igual.

La zona `0xA0` es una ventana entera de 16 MB que `pvr_read`/`pvr_write` separan por dirección
física después del `switch` grande: `0x005F0000-0x005FFFFF` está respaldado por `control_mem`,
`0x005F7000-0x005F70FF` y `0x005F7400-0x005F74FF` van a `gdrom.c`, `0x00600000-0x006007FF` es el
área de dispositivo externo G2 (devuelve un "aquí no hay nada" fijo), `0x00700000-0x00707FFF` son
los registros del AICA (`aica_mem`), `0x00800000-0x009FFFFF` son los 2 MB de RAM de sonido, y
`0x00000000-0x001FFFFF` más `0x00200000-0x0021FFFF` son el boot ROM y la flash, ambos servidos por
`bios_read()`.

---

## La familia de registros de identificación

Cuatro registros costaron un cuelgue cada uno, todos con la misma forma: sin case propio, la
lectura caía al almacén de respaldo `control_mem` y contestaba algo que el guest se creía.

**`0x005F74B0` es `SB_G1SYSM`, el registro de modo de sistema del bus G1, no un registro de
GD-ROM**, aunque cae dentro del bloque `0x005F7400-0x005F74FF` que `mem.c` entrega entero a
`gdrom.c`. Hay que atraparlo antes de ese despacho. Su nibble alto es el tipo de máquina y el bajo
la región, y una consola de tienda contesta cero en ambos (`G1_SYSM_RETAIL` en `sistema.h`) — la
región real viene de la flash. Con el valor que la lectora devolvía antes, `0x2422211F`,
`hardware_sys_mode()` reportaba tipo 1, KOS concluía que era un devkit Set5, y `spu_init()`
limpiaba **8 MB** de RAM de sonido en vez de 2 — así que 6 MB de escrituras de cola de
almacenamiento caían fuera de todo lo mapeado.

**`0x005F8004` es el registro `REVISION` del PVR y una consola de tienda contesta `0x11`.** El
boot ROM comprueba `COREID` contra `0x17FD11DB` *y* exige revisión `>= 17` y `!= 1`; no tenía case
de lectura, caía al respaldo del bloque de control, leía cero, y se estacionaba en un `BRA`
deliberado a sí mismo en `0x8C0DBDFC` para siempre.

**`0x005F689C` es `SB_SBREV`, la revisión del System Block del Holly, y una consola de tienda
contesta `0x0B`** (reicast lo inicializa así). Tercero de la familia y el peor de los tres: sin
case de lectura la lectura caía a `control_mem`, que era un malloc nunca limpiado, así que
contestaba basura reciclada del heap que **variaba por instancia de proceso**. El arranque de
Katana lo compara contra 8 (Crazy Taxi: PC `0x0C073944`, 118 ms después de encender) para elegir
su camino de inicialización, y en el lado que lee < 8 la fase de subida de texturas RAM→VRAM del
juego nunca arranca — el "mundo blanco" intermitente de `docs/pendientes-plan.md` A.5 era esta
moneda al aire al arrancar el proceso, no una carrera de la lectora.

**Cada bloque de `inicializar_memoria()` es calloc ahora**: un registro sin case tiene que
contestar su valor de reset, no la historia del heap, y el respaldo en cero es también lo que hace
que dos corridas del reproductor determinista salgan byte a byte idénticas (la única línea que
difiere es la del reloj de pared en el resumen de salida). `DCEMU_TRAZA_EN_MS=N[:M]` es la
herramienta que lo encontró — ver `docs/notas-herramientas.md`.

**`0x005F810C` es `SPG_STATUS`, y no es solo la línea de barrido.** Los bits 9:0 son la línea, el
10 el número de campo, el 11 el blanking vertical, el 12 hsync y el 13 **vsync**. Antes devolvía
`pvr_scanline` y nada más, así que cualquiera que esperara vsync esperaba para siempre — ahí es
donde se sentaba el boot ROM real (`0x8C00CB2E`, `TST #0x2000` sobre este registro). El vsync está
ahora encendido durante las primeras `SPG_WIDTH.vswidth` líneas del cuadro y el blanking va desde
`SPG_VBLANK.vbstart` hasta `vbend`, dando la vuelta pasado el final; hsync y campo quedan en cero,
ya que dcemu no sigue posición horizontal ni entrelazado. Encontrar esto también sacó a luz que
**`SPG_VBLANK` y `SPG_WIDTH` tenían case de lectura pero no de escritura**, así que
`pvr_spg_vblank` y `pvr_spg_width` conservaban los valores por omisión de `reg.c` sin importar lo
que el guest programara.

`regmap_read()` tiene un caso especial: `0xFF800030` (PDTRA) va por `sistema_pdtra()`, el apretón
de manos del detector de cable de video. Sin él el boot ROM duerme para siempre.

---

## La MMU y las colas de almacenamiento

**Con la MMU encendida, el destino de vaciado de la SQ sale de la UTLB, no de QACR.** Manual del
SH-4 §4.6: el `PREF` sobre `0xE0000000-0xE3FFFFFF` traduce la dirección virtual completa de la SQ;
un fallo levanta el write-miss con ESE VPN. `mmu_traducir_sq()` en `mmu.c` lo hace — ver
`docs/mmu-plan.md`, fase 6.

Es cómo el ddraw de Windows CE llega a todo: su manejador de recarga sirve las VA de SQ desde la
plantilla de PTE que deja `SetStoreQueueBase`, más los bits 25-20 de la VA, así que una sola base
se abre en abanico hacia la FIFO de polígonos (`0xE2xxxxxx` → `0x10000000`), el camino de textura
directa (`0xE3xxxxxx` → `0x11xxxxxx`) y el staging en RAM (`0xE0Cxxxxx`). Enmascarar primero — la
fórmula de QACR, que es la regla con la MMU apagada — le daba a CE un fallo para un VPN de la
ranura 1 que sus tablas nunca mapean, y ddhal.dll moría por violación de acceso en su primer blit:
esa sola línea era la mayor parte de por qué el proceso de juego de DCDoom salía a los 2,4 s.

**La búsqueda de instrucción también traduce** (fase 7): `main_loop()`, las ranuras de retardo y el
RTE buscan por `MMU_FETCH_PUNTERO()`, una caché de página que cuesta una comparación cuando la MMU
está apagada. La ranura del RTE se busca *antes* de escribir `SR` — la regla del manual, y con la
MMU encendida importa: el retorno a usuario de un kernel tiene su ranura en una página
privilegiada. `MMUCR.URC` avanza en cada acceso a la UTLB, que es lo que le permite al `LDTLB` de
un manejador de fallo de TLB por software tomar una entrada fresca cada vez — el de Windows CE
depende de ello.

**Un `memwrite` del DMA de Maple se pasó por alto** (`mem.c`, el relleno `0xFFFFFFFF` para un
puerto sin dispositivo) y le costó a `basic/mmu/pvrmap` un doble fallo. Estos solo se portan mal
con la MMU encendida, así que nada más del árbol los muestra — hay que hacer grep antes de suponer
que están todos convertidos.

## Excepciones síncronas

`excepciones.c/h` es dueño del camino de excepción general, compartido por la MMU y la FPU. Dos
piezas: `excepcion_entrar()` (guardar SSR/SPC/SGR, fijar EXPEVT, saltar a `VBR + vector` — se mudó
aquí desde `intc.c`, ya que es la secuencia del procesador y no la del controlador de
interrupciones), y el **aborto de instrucción**.

Las excepciones generales del SH-4 son de tipo re-ejecución, pero los manejadores de dcemu mutan
registros alrededor del acceso (`MOV.L @Rn+`, `FMUL` sobre su propio destino), así que
`main_loop()` toma una instantánea del estado antes de cada instrucción y arma un `setjmp`; quien
detecte la falla llama a `excepcion_abortar()`, que no vuelve. El `longjmp` desenrolla los dos
niveles de una ranura de retardo gratis, así que SPC cae en el salto — que es lo que quiere el
manual.

- **`excepcion_vigilar`** decide si el lazo toma instantánea siquiera. Generaliza la vieja prueba
  contra `mmu_activa`: 1 si la MMU traduce, `SR.FD` está puesto, o cualquier bit Enable del FPSCR
  está encendido. Cero en todo lo que corre hoy, así que el camino rápido cuesta lo que costaba.
- **La instantánea tiene que incluir los bancos de flotantes.** `core.context` guarda solo
  *punteros* a ellos, así que un `memcpy` plano del contexto no restaura nada de FR/XF — el camino
  de la MMU tenía ese bug desde la fase 5 de `docs/mmu-plan.md` y nadie lo notó.
  `excepcion_instantanea_tomar()` y `..._restaurar()` copian ambos bancos también.
- **0x800/0x820 se comprueban en `run()`**, no en `main_loop()`, así que las ranuras de retardo
  quedan cubiertas — `branch.c` y `rte143()` las corren por un `core.execute()` anidado, y eso es
  exactamente lo que distingue los dos códigos. `en_ranura_retardo` lo levanta `EJECUTAR_RANURA()`;
  `main_loop()` lo limpia al abortar porque el `longjmp` se salta la bajada.
- **`fpu_deshabilitada` se deriva solo en `excepcion_actualizar_vigilancia()`.** Es una copia de
  `SR.FD` para que el despachador no tenga que extraer un campo de bits por instrucción; escribirla
  en otro lado deja que las dos diverjan (`arnes_reset()` asigna `SR = 0` directamente).

Probarlo necesita el lazo entero, no solo una llamada al manejador: `ejecutar_vigilado()` en el
arnés de pruebas repite instantánea → `setjmp` → restaurar → `excepcion_entrar()`. De punta a
punta, `demos/fpu-trampa` instala un manejador de KOS para `EXC_FPU` y `demos/mmu-mapeo` mapea una
página y comprueba que la escritura cayó en la dirección física.

**`SR.FD` no se puede probar desde KOS en absoluto** — su entrada de excepción empieza con
`sts.l fpscr,@-r0`, una instrucción de FPU, así que con FD puesto fallaría para siempre. Cierto en
hardware real también; KOS nunca usa FD.

**Ambas demos de MMU pasan**, y ninguna fallaba por la MMU. El `kernel panic` de
`basic/mmu/nullptr` es el final previsto de la demo (`catchnull` devuelve `NULL` a propósito) —
leer solo la última línea serial la clasificó mal durante un barrido entero. `basic/mmu/pvrmap`
moría por un `memwrite` que debía ser `memwrite_fisico`.

**La fuente de la ROM vive en `0x00100020`, una dirección P0, así que la MMU la traduce.** Eso es
lo que contesta el boot ROM real — `bios.bin` lleva esa constante dos veces y nunca `0xA0100020` —
así que es correcto, pero un guest que mapee las páginas bajas tapa su propia fuente.
`basic/mmu/pvrmap` hace exactamente eso y pierde su texto; en hardware pasaría igual.

---

## UBC (puntos de interrupción por hardware)

`ubc.c/h` es el user break controller del SH-4, manejado como lo maneja el driver de KOS
(`kernel/arch/dreamcast/hardware/ubc.c`): dos canales con máscaras de dirección y ASID opcional,
el canal B comparando opcionalmente el dato transferido (`BDRB`/`BDMRB` bajo `BRCR.DBEB`), y
`BRCR.SEQ` encadenándolos — la coincidencia de A solo arma B. `CMFA`/`CMFB` se ponen al coincidir
y **solo el guest los limpia**; su manejador los lee para saber qué canal disparó.

La excepción es EXPEVT `0x1E0` por el vector general; los breaks de instrucción respetan
`PCBA`/`PCBB` (antes/después de ejecutar), los de operando son siempre "después": quedan
pendientes y se entregan en el siguiente límite de instrucción, así que SPC cae en la instrucción
siguiente — KOS imprime `PC - 2` exactamente por eso. Con `SR.BL` puesto la entrega espera sin
perderse.

Los breaks de instrucción se evalúan en el límite de `main_loop()` (una prueba de flag cuando no
hay canal armado); los de operando cuelgan de las macros `memread`/`memwrite` del camino del guest
en `mem.h`, que comparan la dirección **virtual** — los accesos internos van por `*_fisico` y
quedan fuera solos. Un break sobre una instrucción en ranura de retardo no se detecta (la ranura
corre dentro del `core.execute()` anidado del salto); el manual restringe ese caso de todas
formas.

**`basic-breaking` pasa cuatro de sus cinco grupos; el quinto falla en el binario, no en el UBC.**
GCC 15.2 en -O2 elimina la llamada `test_function("Sega", "Sony")` en `break_on_sequence` — una
función estática pura cuyo resultado se descarta (la primera prueba sobrevive porque asigna a un
`volatile`) — así que la condición A de secuencia es inalcanzable, en hardware real también.
Reconstruida con un `volatile` de una línea, la demo imprime
`***** Breakpoint Test: SUCCESS *****` de punta a punta.
