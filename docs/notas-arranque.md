# Notas: el arranque, los syscalls del BIOS y Windows CE

Detalle de cómo arranca un juego, por los dos caminos (`--bios` y los hooks de syscall).
`docs/bios-boot-plan.md` es la bitácora del trabajo; `docs/pendientes-plan.md` tiene los apartados
A.x que se citan aquí. Esto es la referencia.

---

## Dónde está el arranque del BIOS

**Arranca, y la animación de intro se ve.** Con `--bios` reproduce la animación de la espiral — el
espiral en relieve sobre el fondo `0xBFBFBF` con su estela naranja — y llega al menú; desde ahí
Play, File, Music y Settings funcionan todos.

Lo que faltaba para el *arranque* eran tres cosas, todas de la misma forma — algo que el guest
pide y que dcemu acepta sin hacer nada y sin decir nada (ver `docs/bios-boot-plan.md`, "Tercera
corrida"):

- **el CH2 DMA** (`SB_C2DSTAT`/`SB_C2DLEN`/`SB_C2DST`, `0x005F6800-08`) — ver
  `docs/notas-graficos.md`;
- **un descriptor de Maple cuya primera palabra es legítimamente cero** — ver más abajo;
- **eventos del ASIC descartados cuando ninguna máscara los cubría** — ver
  `docs/notas-tiempo.md`.

Lo que faltaba para la *animación* eran dos más, ambas encontradas por la ROM y por ninguna demo
de KOS: el **plano de fondo** del PVR y el tamaño verdadero del encabezado **Polygon Type 1** del
TA — ambos en `docs/notas-graficos.md`.

### Los hitos

**Hito C: el boot ROM arranca un juego desde un `.cdi` por su cuenta.** Con `--bios` y la imagen
presentada como lo que es — **un CD selfboot, sin `DCEMU_COMO_GD`** — la ROM recorre todo el
camino: reconoce el disco, lee IP.BIN desde el comienzo de la pista de datos, el descriptor de
volumen y el directorio raíz, encuentra `1ST_READ.BIN`, carga sus 1 468 208 bytes y salta. El PC
cae dentro del ejecutable, alrededor de `0x0C14xxxx`. Ese es el mismo lugar al que llega el camino
de siempre (sin `--bios`, por los hooks de syscall).

**Hito D (2026-08-01): Crazy Taxi corre.** En el camino de hooks corren ambos rips — pantalla de
carga, aviso de VMU, Start funciona, pantalla de título, attract 3D. Lo que lo destrabó fue el
tipo de disco que contesta `GDROM_CHECK_DRIVE` más la función 3 de `SYSINFO`.

**Virtua Tennis también** (2026-08-02): aviso de VMU, título, menú principal y el attract 3D a
2400 tiras por escena — lo que lo destrabó fue la entrega por nivel del ASIC más el retardo de fin
de transferencia del CH2 DMA (`docs/notas-tiempo.md` y `docs/pendientes-plan.md`, A.6).

**Capcom vs. SNK el mismo día**: aviso de Memory Card y la pantalla de título de Millennium Fight
2000 — un bloqueo distinto: el hook de GD aceptaba toda petición al instante donde el driver del
BIOS toma una a la vez, y la lectura larga sin CHECK del juego quedaba huérfana (ver los hooks más
abajo y A.7).

**Virtua Tennis 2 cayó el mismo día** (aviso de VMU, logo de título y secuencia de intro): los
arreglos del día destrabaron su arranque — sus sondas del área externa G2 (el Broadband Adapter,
que soporta) fallan limpiamente contra los ceros deterministas de `mem_read_error()`, así que el
área queda sin mapear a propósito — y su único bloqueo propio era la regla de lista actual del TA
(ver `docs/notas-graficos.md` y A.8).

**Las cuatro imágenes comerciales que se interpretan corren.**

---

## El `bios.bin` importa, pero no como parecía al principio

El que está en el repositorio es `KABUTO Ver.1.004 ... 1998` (cadena en `bios.bin+0x7cc`), de
antes de MIL-CD; con él la ROM lee 17 sectores en el FAD 45150 y nada más. **Con la 1.01d (1999)
el camino se abre** — así que "imposible con esta ROM" valía solo para la 1.004.

Las revisiones se distinguen de un vistazo: la 1.022, que abandonó MIL-CD, tiene una tabla de
arranque menos que las anteriores. Ojo con la flash: los volcados vírgenes hacen que la ROM pida
la fecha en cada arranque, y **parchear el código de región a mano rompe la suma de comprobación
de esa partición**, con el mismo efecto. Conviene dejar que la ROM configure la fecha una vez —
dcemu guarda la flash y `bios/rtc.txt` al salir.

---

## Dónde decide la ROM, y qué mira

Todo esto desensamblado desde la RAM con `--traza-desde`, porque el código de la ROM no está en la
misma dirección dentro de `bios.bin`:

- `0x8C000AE0` es la rutina de arranque de alto nivel. `*(u32*)0x8C0000E4` es la razón de reinicio
  (1 = "volvió de `menu(1)`", que es lo que la manda al menú). `*(u32*)0x8C00004C` es el tipo de
  disco: `0x20` elige la rama MIL-CD en `0x8C000B44`, cualquier otra cosa la rama GD en
  `0x8C000B6C`.
- `0x8C0004F4` valida el encabezado del IP.BIN — hardware ID, maker ID, la cadena de región contra
  el índice de región de la flash — y deja el puntero al nombre de archivo de arranque en
  `GBR+0x9C`. `*(u8*)0x8C000024` es el bit 0 del dígito hexadecimal en el offset `0x3E` del
  IP.BIN.
- `0x8C000D40` lee el descriptor de volumen, después el directorio raíz, y recorre los registros
  ISO9660 comparando cada nombre contra ese nombre de archivo. Al coincidir calcula el FAD y la
  cuenta de sectores y, **salvo que `fad >= 0x6DDD0` o `*(u8*)0x8C000024 == 2`, llama a
  `menu(1)`** — y `== 2` es exactamente lo que pone la rama MIL-CD. Esa es la compuerta que la presentación
  como GD no puede pasar.
- La ROM deja la dirección de carga en `0x8C0000F8` (`0x8C010000`), el FAD en `0x8C0000F0` y la
  cuenta de sectores en `0x8C0000F4`.

---

## DCDoom es un juego de Windows CE

(2026-08-02.) El binario de arranque es `0WINCEOS.BIN`, no `1ST_READ.BIN` — el nombre viene del
offset 0x60 del IP.BIN, que el cargador directo ahora lee en vez de cablearlo (la ROM siempre lo
hizo; es el puntero que deja en GBR+0x9C).

CE es lo que finalmente ejercitó la fase 7 de la MMU (traducción de búsqueda de instrucción, con
caché de página — ver `docs/mmu-plan.md`), el avance de `MMUCR.URC` del que depende su manejador
de recarga de TLB, la imagen P2 del área 7 (zona `0xBF` — su HAL arranca el tic de sistema
escribiendo TSTR en `0xBFD80004`), el disparo por hardware de Maple (`SB_MDTSEL=1`: el DMA se
camina solo en cada vblank; CE nunca escribe `SB_MDST`), y **la entrada de servicio de GD-ROM fija
de la ROM en `8C0010F0`** — ver la sección de syscalls: su maple.dll la llama como constante, sin
leer ningún vector, y con esa RAM en cero su inicialización de driver fallaba y se descargaba
sola, dejando un hilo trabajador zombi que fallaba sobre la DLL sin mapear.

Con todo eso en su lugar el kernel de CE arranca entero — MMU encendida, recargas de TLB por
software, syscalls por saltos a `0xFFFFFxxx` (trampas de error de dirección), contexto de FPU
perezoso sobre `SR.FD`, tics, el pad sondeado a 60 Hz por el auto-poll de Maple, el ARM del AICA
corriendo el firmware que CE sube — y **CE monta el ISO9660 del disco por los syscalls de GD**:
descriptor de volumen, directorio raíz, todo `DCDOOM.EXE` en una lectura de 645 sectores, y los
módulos de `\WINCE` paginados por demanda desde el CD en piezas de 16 KB.

**DCDoom corre y se ve**: DOOM completa todo su arranque, lo imprime por `DCEMU_TRAZA_DEPURACION`
y reproduce la demo attract de E1M1 — paredes, sprites, la piscina de nukage, el arma y toda la
barra de estado, 868 escenas en 32 segundos emulados. Lo que costó, más allá del trabajo de CE de
arriba: el **Sort-DMA** (cómo el ddraw de CE alimenta el TA), `FB_R_SOF1` leyendo de vuelta lo que
se le escribió, el destino de vaciado de SQ saliendo de la UTLB con la MMU encendida, todo el
**protocolo de flujo PIO** y — al final — entregar su COMPLETED una consulta tarde, que es lo que
hace que el `read()` del llamador devuelva la cuenta de bytes en vez de cero.
`docs/pendientes-plan.md`, A.9, tiene la historia completa y las herramientas.

---

## Maple

El bus de mandos vive dentro de `pvr_write()`, en el case de `SB_MDST` (`0x005F6C18`): escribir 1
recorre la lista de comandos en `SB_MDSTAR` y contesta cada transferencia en el lugar. Solo el
puerto A tiene dispositivo — un mando HKT-7700 estándar — y los otros puertos reciben
`0xFFFFFFFF`, que es "aquí no hay nada". Hay dos comandos implementados: `Device Request` (1) y
`GetCondition` (9).

**El disparo por hardware existe también**: con `SB_MDTSEL=1` y `SB_MDEN=1` el chip arranca el DMA
por su cuenta en cada vblank — `maple_vblank()` (mem.c), llamada por `main_loop()` al lado del
evento de vblank, sintetiza la escritura de `SB_MDST` para que ambos caminos recorran el mismo
código. Windows CE sondea el pad exactamente así y nunca escribe `SB_MDST`; KOS y el boot ROM
patean por software, que es por lo que el disparo nunca se echó de menos antes de DCDoom.

**La primera palabra de un descriptor de transferencia es legítimamente cero.** Lleva el largo en
los bits 0-7, el patrón en 8-15, el puerto en 16-17 y el fin de lista en el bit 31, así que una
trama de una palabra al puerto A que no es la última de la lista deja los cuatro en cero. El
código leía eso como "`SB_MDSTAR` está mal" y se rendía — y esa es *exactamente* la primera sonda
del boot ROM, un `Device Request` al puerto A, así que el BIOS nunca encontraba el mando. KOS
nunca lo muestra porque siempre marca la última transferencia y sus listas de un elemento salen
`0x80000000`. La rendición ahora prueba la dirección de respuesta, que el guest siempre pone en la
RAM del sistema.

El descriptor de dispositivo también era casi todo ceros. `function_data[0]` es el campo que dice
**qué botones y ejes tiene el mando**, y los campos de nombre se rellenan con espacios hasta su
ancho completo en el bus, no terminan en NUL.

---

## Los hooks de syscall del BIOS

Con `BIOS_HACKS` habilitado en `options.h` **y** `opciones.hacks_bios` puesto en tiempo de
ejecución, `main()` parcha las ranuras del vector de syscall (`0x8C0000B0`-`0x8C0000E0`) para que
apunten a código stub que escribe en `HACK_BASE`. La mayoría de los stubs son un opcode ilegal en
la ranura de retardo de un `RTS`, que mapea a `BIOS_HACK` en `dcopcodes.c`.

**El stub de GD-ROM es la excepción: su opcode ilegal está en el offset 0, sin RTS, y
`hack_gdrom()` fija el PC de retorno él mismo** — normalmente `PC = PR`, pero un MAINLOOP (r7=2)
con una pieza PIO enganchada copia los datos y "llama" al callback PIO del guest en su lugar
(PC = callback, R4 = su argumento, PR sin tocar), exactamente como el `gdGdcExecServer` real. La
forma de RTS lo hacía imposible: `rts112` engancha su destino antes de que corra la ranura de
retardo.

Windows CE hace streaming de DOOM.WAD por ese callback (`MULTI_PIOREAD`, comando 39, más las
funciones de vector 11/12/13 — el gemelo de copia por CPU del flujo DMA de 38/r7=6; numeración
verificada contra el `gdrom_hle.h` de flycast, semántica fundada en `CD_READ2 (31h)` de
`docs/cdif131e.pdf`). Dos detalles costaron un kernel panic cada uno: la copia y el callback
ocurren en el MAINLOOP que llama la propia bomba de wsegacd — su propio hilo y proceso, el único
contexto donde la VA del argumento del callback significa lo que debe — y una escritura traducida
de más de una página tiene que trocearse (`memwrite_paginado()`: `memwrite` traduce una vez por
llamada, y una pieza de 36 KB roció datos del WAD sobre el propio directorio de páginas del
proceso — CE se detuvo con "Halting system" por excepción anidada).

### `hack_gdrom()`

Sirve `GDROM_SEND_COMMAND` (lecturas de sector, TOC) directamente desde la imagen montada por
`iso.c`.

**`GDROM_CHECK_DRIVE` contesta tipo de disco GD-ROM (`0x80`) cada vez que hay un disco puesto**,
no lo que sea la imagen: este camino reemplaza consola, BIOS y lectora para correr el juego
montado, y el disco original de un juego comercial es un GD-ROM. El `gdFsInit()` de Katana compara
esa palabra contra un literal `0x80` y cualquier otra cosa lo hace devolver −5 y reintentar toda
la inicialización de la unidad para siempre — Crazy Taxi se quedaba en `LOADING (31K)` en un lazo
infinito de syscalls INIT/SEND(CMD_INIT)/MAINLOOP/CHECK/CHECK_DRIVE, en *ambos* rips (disposición
GD y disposición MIL-CD: ningún selfboot se distribuye con ese chequeo parcheado). Arreglarlo es
lo que llevó al juego de su pantalla de carga al título y al attract 3D — hito D. El camino
`--bios` no pasa por aquí y sigue viendo el CD que la imagen realmente es, que es lo que necesita
su rama MIL-CD.

**El hook acepta una petición viva a la vez, como el driver del BIOS** (`com_viva`): un
`SEND_COMMAND` mientras otro está sin CHECK devuelve 0 sin hacer el trabajo, y `CHECK_COMMAND` lo
consume. Aceptar todo al instante parecía inocuo hasta Capcom vs. SNK: manda su lectura larga de
CRI y, antes de comprobarla, manda el sondeo periódico de subcódigo — en la consola ese segundo
SEND rebota y Katana lo reintenta mientras sondea la lectura; aceptado, la ranura de comando
actual de Katana avanzaba y la lectura quedaba huérfana, su terminación nunca se observaba, y la
capa CRI esperaba para siempre.

Los ids de petición crecen de uno en uno (un id fijo confunde peticiones consecutivas),
`CHECK_COMMAND` reporta la cuenta de bytes transferidos en la tercera palabra de estado, y
**`GETSCD` (comando 34) se contesta** con el encabezado SPI — estado de audio `0x15`, "no audio
info" — y el subQ de la pista de datos; COMPLETED con un buffer sin escribir dejaba estado `0x00`,
que no es ningún código.

#### El COMPLETED de un flujo PIO se entrega una consulta tarde, a propósito

`gdGdcGetCmdStat` lo reporta *una vez* — desensamblado desde la RAM en `8C003072` con `--bios`:
escribe la cuenta transferida en `status[2]`, devuelve 2 y pone en cero el estado del bloque, así
que la consulta siguiente coincide con el id, encuentra estado 0 y contesta NO_ACTIVE con las
cuatro palabras en cero (las limpia antes de mirar nada; y un id que no es la petición actual
contesta −1 con `status[0] = 5`, no 0).

Al final de un flujo PIO hay **dos** consultas: la de la bomba de wsegacd, que ya desarmó su
callback, y la del llamador desde otra ranura de CE — y es el llamador el que necesita la cuenta.
Cargarle el COMPLETED a la primera le daba al llamador "no existe esa petición" y **`read()`
devolvía 0** con los bytes ya en su buffer. Eso era el `W_ReadLump: only read 0 of 17544` de
DCDoom. Así que la consulta que *descubre* el flujo vacío contesta CONTINUE y marca; la siguiente
carga el COMPLETED.

Tres cosas de esto son fáciles de invertir, cada una medida:

- el aplazamiento es **solo PIO** (un flujo DMA termina su comando cuando transfiere la última
  pieza — su señal es la interrupción de fin de DMA — y aplazar ahí dejó a Windows CE sin poder
  cargar una sola DLL);
- la respuesta aplazada tiene que ser **CONTINUE (3), no PROCESSING (1)**, o la bomba gira
  `MAINLOOP`+`CHECK` para siempre;
- y solo una lectura en todo el arranque lo mostró, porque los datos siempre llegaban — una
  lectura corta va por el comando 17 y nunca toca el flujo, y el directorio del WAD sí va por él
  pero `W_AddFile` nunca comprueba la cuenta.

#### La entrada fija en `8C0010F0`

**El mismo stub está instalado también en `8C0010F0`, la entrada de servicio de GD fija de la ROM,
y la palabra en `8C0000C0` — un quinto vector que los hooks nunca llenaban — apunta ahí**
(`SYSCALL_GDROM_FIJO`/`HACK_GDROM_FIJO`). Medido contra la 1.01d real con `--bios`: la ROM instala
un despachador de 16 funciones exactamente en `8C0010F0` (tabla autorelativa en `8C001180`) y deja
su dirección en `8C0000C0`. La maple.dll de Windows CE llama a la dirección como constante de
compilación sin leer ningún vector; con esa RAM en cero el guest se deslizaba por ~28 KB de NOIMP
hasta los bytes del IP.BIN y su inicialización de driver fallaba.

**`GDROM_INIT` (función 3) contesta `R0 = 0`** — dejaba R0 sin tocar, y CE lo comprueba.

**El destino de la lectura DMA (función 17) se escribe físico** (`memwrite_fisico`): el G1 DMA de
la consola no pasa por la MMU, y traducirlo como virtual mandaba los buffers de CE — pasados como
direcciones físicas tipo `0x0CF77000` — al espacio sin mapear de otra ranura. La función 16 (PIO,
las escrituras por CPU del driver) sigue traducida. Los juegos Katana pasan direcciones P1 y no
pueden distinguir las dos.

### `hack_romfont()`

Sirve el syscall de la fuente de la ROM. **Este toma su número de función en `R1`, no en `R7`**
(ver `syscall_font.s` de KOS): 0 devuelve la dirección de la fuente, 1 toma el mutex, 2 lo suelta.
El bloqueo tiene que contestar **0** para significar concedido.

**El stub solía ser `RTS` + `MOV.L @(0,PC),R0` con la dirección como literal, así que contestaba
la dirección a las tres funciones.** Eso devolvía un valor distinto de cero para el bloqueo, y
`lock_bfont()` sondea `thd_poll(bfont_lock, ...)` hasta que el syscall conteste 0 — así que giraba
para siempre. `bfont_draw_ex()` toma ese bloqueo antes de traducir el carácter, lo que significa
que *toda* llamada a `bfont_draw_*` se colgaba ahí. El síntoma era una demo que pintaba su fondo y
después se congelaba sin texto y sin error: `video/bfont`, `video/multibuffer` y
`video/screenshot` parecían todas un framebuffer roto. `video/minifont` no se veía afectada porque
`minifont_draw_str` usa una fuente incorporada a KOS y nunca llama al syscall — esa asimetría es
lo que identifica este bug.

Con `USE_BIOS_FONT` definido —lo está— la dirección de fuente que el stub reporta es
`0x00100020`, la fuente real dentro de `bios/bios.bin`, e `inicializar_fonts()` se compila fuera
por completo. El camino alternativo rasteriza una fuente en la RAM del guest en `FONT_BASE`. KOS
indexa la fuente como `direccion + (ch - 32) * 36` para ASCII 33-126 (`bfont_find_char`), que los
datos reales de la ROM cumplen exactamente.

### `hack_flashrom()`

Sirve el syscall de la flash ROM — info, lectura, escritura, borrado. La convención es
`syscall(r4, r5, r6, func)`, así que el número de función llega en `R7` y el resultado vuelve en
`R0`. La escritura solo limpia bits (`&=`), porque es lo que la flash puede hacer sin un borrado y
los bloques de configuración del guest dependen de eso. Sin esto, el `flashrom_get_region()` de
KOS reportaba `can't find partition 0` — le pide al BIOS los offsets de partición en vez de
interpretar la flash.

Nota que `flashrom_get_region()` solo reconoce tres cadenas exactas — `00000` (Japón), `00110`
(EE.UU.) y `00211` (Europa). Un volcado de flash con cualquier otro código, como el `00111` de
`bios/flash.bin` aquí, hace que KOS registre `unknown code`. Eso es KOS siendo estricto, no un bug
de dcemu.

### `hack_sysinfo()`

`SYSINFO` se contesta ahora, con la numeración confirmada contra
`kernel/arch/dreamcast/hardware/syscalls.c` de KOS: la función 0 es INIT (en la ROM copia el ID de
consola de la flash a `0x8C000068`, que `main()` ya deja hecho — ver `SYSID_BASE`), la 2 es ICON
(sigue sin implementar, sigue reportándose), y **la 3 es ID, que devuelve en `R0` un puntero al ID
de 8 bytes, no el ID en sí** — KOS lo desreferencia. Contestar 0 ahí no era neutro: Crazy Taxi
sigue el puntero y copia su "ID" a través de él, escribiendo alrededor de la dirección `0x10`.

El vector sin nombre en `0x8C0000E0` (`UNKNOWN`) sigue sin hacer nada, pero **lo dice**:
`hack_mudo()` reporta el nombre, los cuatro argumentos, el PC y el PR por `--traza-mem`, y deja
`R0` en 0 en vez de en lo que hubiera — un puntero basura es peor que uno nulo, porque el guest lo
sigue. Estos solían ser `RTS` + `NOP`, es decir la forma exacta de todos los demás agujeros de
este árbol: algo que el guest pide, contestado sin querer decirlo, sin dejar rastro.

---

## El bloque que el boot ROM deja en RAM baja

**Los stubs de syscall no son lo único que el boot ROM deja: también escribe el código de máquina
de la flash — cinco dígitos y un NUL — en `0x8C000070` (`REGION_BASE`) antes de entregar la
consola al juego.** Sin `--bios` nadie lo escribía y quedaba lo que hubiera.

Los juegos sí lo miran: Crazy Taxi compara esa *palabra* contra `0x3030` — los dos primeros
dígitos — y, si coincide, trata la máquina como conocida; si no, va y le pregunta a un dispositivo
del bus G1 externo en `0x03010000` que una consola de tienda no tiene, y espera su bit 7 para
siempre. Esa sola palabra sin inicializar era toda la diferencia entre el juego colgado en un lazo
ocioso y el juego llegando a su pantalla `LOADING CRAZY TAXI`. Se copia de
`flash_mem[FLASH_PART0_OFF]`, así que sigue la flash que esté en uso en vez de ser una constante.

**El resto de ese bloque es la copia que la ROM hace de la flash, no constantes**, que es lo que
lo hace derivable en vez de mágico. Medido arrancando `--bios` con la 1.01d y volcando
`0x8C000000-0x8C0000FF` en el menú (dos corridas, idénticas salvo un byte): `0x8C000068` lleva el
ID binario de 8 bytes de la consola, que es la partición 0 de la flash en `+0x56`, y `0x8C000078`
lleva 8 bytes de configuración del sistema tomados del **último** registro de 16 bytes de la
partición 2 de la flash. El primero se reproduce (`SYSID_BASE`, al lado de `REGION_BASE`, desde la
misma flash); el segundo no, porque el formato de registro de esa partición no está resuelto. Las
dos palabras en `0x8C000060`/`0x64` son `0x00C0C0C0` y algo que cambia entre corridas, y ninguna
viene de la flash — sin explicar, así que se dejan en paz en vez de cablearlas.

`--bios` apaga los hooks: los stubs se escriben en `0x8C000100`-`0x8C000500`, que es exactamente
donde el boot ROM se instala.

---

## La flash y el RTC son escribibles y persisten

Y tienen que serlo: sin eso el BIOS pide la fecha y la hora en *cada* arranque, porque nunca logra
registrar que ya está configurada. `sistema.c` guarda las tres piezas de estado de sistema que el
boot ROM pide antes que la lectora: el apretón de manos de cable PDTRA/PCTRA, la flash ROM
(cargada de `bios/flash.bin` o sintetizada) y el RTC. Está libre de SDL a propósito, para que
`tests/` lo pueda enlazar.

- **La flash no es memoria, es un chip con un juego de comandos** (compatible AMD/Fujitsu).
  Escribir un byte significa `AA` en `0x5555`, `55` en `0x2AAA`, `A0` en `0x5555`, y después el
  dato; borrar un sector es una secuencia de seis escrituras terminada en `30`. Programar solo
  puede limpiar bits — de ahí el `&=`, la misma regla que ya seguía el hook de syscall de
  flashrom. `sistema_flash_guardar()` reescribe `bios/flash.bin` al salir, y solo si algo cambió.
- **La escritura del RTC se guarda como un offset contra el reloj del anfitrión**, no como una
  marca de tiempo congelada, así que el tiempo sigue corriendo. Cae en `bios/rtc.txt` — un número,
  en texto, para poder leerlo y borrarlo a mano.
