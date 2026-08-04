# Notas: la lectora GD-ROM y las imágenes de disco

Detalle de `gdrom.c`, `iso.c`, `cdi.c` e `iso9660_min.c`. `CLAUDE.md` tiene el resumen.

---

## La lectora como la ve el hardware

`gdrom.c/h` es el bloque de registros ATA, la máquina de estados (BSY/DRQ/DRDY/CHECK, razón de
interrupción, estado de la unidad y tipo de disco) y los comandos de paquete SPI que usa el boot
ROM. Los sectores siguen viniendo de `iso.c`. `mem.c` enruta los dos rangos de direcciones hacia
ahí; nada más la toca excepto `dcopcodes.c`, que reutiliza `gdrom_construir_toc()` para el hook de
syscall de modo que ambos caminos reporten el mismo disco.

El mapa de registros y los códigos de comando están comprobados contra dos fuentes independientes
— el driver de GD-ROM del kernel Linux y el núcleo de reicast. Los datos salen o como bloques DRQ
encadenados por el registro de datos o por el G2 DMA (`SB_GDSTAR`/`SB_GDST`), según el bit 0 de
FEATURES en el momento del comando `PACKET`.

`SB_GDSTARD` y `SB_GDLEND` (`0x005F74F4`/`0x005F74F8`) son los contadores del DMA: dónde va y
cuánto de *el comando* ha movido. El driver de la ROM mantiene `SB_GDLEND` en su bloque de comando
como "cuánto ha llegado", así que dejarlos al almacén de respaldo del bloque de control le da
memoria sin inicializar.

---

## Cinco cosas de la lectora estaban mal, y cada una escondía la siguiente

Vale la pena listarlas porque cuatro de las cinco tienen la misma forma que todo lo demás en este
proyecto: algo que el guest lee y que dcemu contesta sin querer decirlo.

- **La respuesta de `REQ_SES` estaba corrida un byte.** Su segundo byte es reservado y faltaba,
  así que la cuenta de sesiones caía en `[1]` y el byte alto del FAD en `[2]`. El driver de la ROM
  le entrega al llamador precisamente el byte `[2]`, así que el manejador MIL-CD leía **5** — el
  byte alto del lead-out — donde quería **2 sesiones**, y se rendía. Esto es lo que mantuvo
  cerrada toda la rama de CD.
- **`CD_READ` rechazaba todo lo que estuviera por encima de FAD 45150 en un disco que no fuera
  GD-ROM.** Un CD no tiene *área* de alta densidad, pero sí tiene esos *sectores*: un CD de 700 MB
  llega más allá del FAD 358000, y ahí es donde estas conversiones ponen `1ST_READ.BIN`. El límite
  ahora es el lead-out, el mismo número que reporta `REQ_SES(0)`.
- **El G2 DMA terminaba el comando en su primera ráfaga.** `disparar_dma()` mueve `SB_GDLEN`
  bytes y levanta el evento de fin de DMA en cada una, pero el *comando* solo termina cuando se
  acaban los datos. Desde la segunda encontraba "nada
  pendiente" y no movía nada, mientras el guest seguía programando destinos y mirando terminar el
  DMA. El bootstrap del IP.BIN trae el ejecutable **cifrado**, en 45882 ráfagas de 32 bytes a
  direcciones dispersas — así es como lo descifra al vuelo — así que solo llegaban los primeros 32
  bytes y los otros 1,4 MB eran basura.
- **`SB_GDSTARD` y `SB_GDLEND` no existían.** Ver arriba.
- **El bit `ABRT` del registro ERROR.** `fallar()` escribía solo la clave de sentido. El bootstrap
  termina la carga sin tomar el relleno del último sector — lee el largo del archivo, no los 717
  sectores — y cierra con un `NOP` de ATA; el driver lee ERROR, prueba `& 4` y, sin ABRT, trata el
  aborto como si no hubiera pasado y deja el comando "transfiriendo" para siempre.
  `GD_ERR_*` en `gdrom.h`.

Los tres detalles del bloque que cuestan un arranque cada uno, resumidos: `REQ_SES` tiene un
segundo byte reservado (`[0]` estado, `[1]` cero, `[2]` la cuenta de sesiones o la primera pista
de la sesión, `[3..5]` el FAD); una lectura DMA puede tomar muchas ráfagas y el *comando* solo
termina cuando se acaban los datos; y el registro ERROR no es solo la clave de sentido.

---

## El orden de bytes del TOC en el cable no es el de la estructura

Cada entrada sale con **el byte de control primero** y el FAD detrás en big-endian, como toda
respuesta SPI; `struct TOC` lo guarda al revés — control en los bits 31-28, FAD en 23-0 — porque
eso es lo que quiere el *receptor*: el driver de GD-ROM de la ROM invierte cada palabra antes de
entregarla a su llamador, y esa forma invertida es la que lee KOS (`TOC_CTRL`, `TOC_LBA`).

El hook de syscall se salta el driver, así que ahí la estructura sale tal cual; `cmd_get_toc()`
intercambia. Mandarlo sin intercambiar hace que el guest lea el FAD donde espera el byte de
control, y el manejador MIL-CD de la ROM — que comprueba en `0x8CE003B6` si la primera pista es de
datos — rechazaba discos perfectamente buenos. Esa era toda la razón por la que la disposición
datos/datos no arrancaba.

---

## Formato `.cdi`

**Un `.cdi` es como circula prácticamente toda imagen de Dreamcast**, y ni el lector plano ni
libcdio leen uno. Guarda los datos de las pistas al frente — una pista tras otra, con sectores
**crudos** de 2336 o 2352 bytes en vez de 2048 — y la tabla de sesiones/pistas en un encabezado al
*final* del archivo; los últimos 8 bytes dan la versión y dónde empieza ese encabezado.

**El campo `desplazamiento` de la cola significa dos cosas distintas.** En 3.5 es el **tamaño** del
encabezado, contado hacia atrás desde el final del archivo; en 2.0 y 3.0 es la **posición**
absoluta del encabezado. `cdi_abrir()` lo usaba como tamaño en las tres, lo que da la casualidad de
funcionar para 3.5 —donde las dos lecturas coinciden— y pide la imagen entera como si fuera el
encabezado en las otras dos. `Virtua Tenis 2 (USA).cdi` es una 3.0: 749 MB de `malloc` y una
lectura corta, y la función devolvía fallo **sin ningún mensaje**, así que se leía como "no
encuentra pistas". El encabezado llega hasta el final del archivo en las tres versiones, así que
el tamaño ahora se deriva de la posición y nunca del campo, y ambas salidas dicen qué pasó.

**El recorrido de pistas se valida en vez de confiar en él.** El paso entre sesiones varía entre
versiones y no hay manera inequívoca de seguirlo, así que `cdi.c` busca el nombre de archivo por
pista y comprueba cada candidato: una pista real cumple `total == length + pregap`, y el modo y el
tamaño de sector están en rango. Una posición que no es una pista falla eso por sí sola.
`cdi_abrir()` después comprueba que las pistas juntas ocupen exactamente hasta donde empieza el
encabezado — si no, algo se leyó mal y lo dice.

`iso.c` elige backend por extensión. `.iso` es un ISO9660 plano leído por `iso9660_min.c`; `.cdi`
(DiscJuggler) va por `cdi.c`; cualquier otra cosa necesita `USE_LIBCDIO`, que esta construcción no
tiene.

---

## Dos sistemas de coordenadas se encuentran aquí y es fácil confundirlos

- **FAD = LBA + 150.** El `CD_READ` de la lectora habla FAD; `min_iso_*` habla LBA.
- **Dentro del área de alta densidad de un GD-ROM, los LBA propios del ISO9660 son direcciones
  absolutas de disco** — el directorio raíz está en 45023, no en 23 — así que
  `min_iso_open_pista()` toma el LBA base de la pista y trabaja en esa numeración. Para un `.iso`
  plano la base es 0 y todo se reduce a lo que era.

`iso_es_gdrom()` es verdadero cuando la pista de datos empieza en o después del LBA 45000. La
lectora reporta `GD_DISCO_GDROM` en vez de `GD_DISCO_CDROM` para esos, y
`gdrom_construir_toc_area()` construye un **TOC separado por área de densidad** — densidad simple
por debajo del FAD 45150, alta densidad por encima. Antes de eso la lectora contestaba el mismo
TOC de una pista a ambas, y el boot ROM concluía que no había juego: *"please insert game disc"*.

---

## Qué imágenes arrancan

Lo que la lectora dice del disco elige la rama de la ROM, así que `iso.c` tiene que acertar con el
disco: un `.cdi` **es un CD** (`iso_es_gdrom()` solo es verdadero bajo `DCEMU_COMO_GD` ahora), dos
sesiones se distinguen por **el hueco** entre pistas (dentro de una sesión son contiguas salvo el
pregap de 150 sectores; cerrar una y abrir otra cuesta unos 11400), y el TOC **no** se parte en
áreas de densidad salvo que el disco sea de verdad un GD-ROM. `iso_init()` lista cada pista con su
LBA, tamaño, modo y offset de archivo — ese listado es lo primero que hay que mirar.

Las dos disposiciones de selfboot funcionan, y la ROM encuentra `1ST_READ.BIN` en todas:

| imagen | pista 1 | pista 2 | formato |
| --- | --- | --- | --- |
| Crazy Taxi (DCRES) | LBA 0, 302 sectores, **audio** | LBA 11702, 346490, datos | audio/datos |
| DCDoom | LBA 0, 302, **audio** | LBA 11702, 18487, datos | audio/datos |
| Crazy Taxi (USA) | LBA 0, 33600, datos | LBA 45000, 306552, datos | datos/datos |
| Virtua Tennis (USA) | LBA 0, 33600, datos | LBA 45000, 314830, datos | datos/datos |
| Capcom vs. SNK (USA) | LBA 0, 33600, datos | LBA 45000, 314569, datos | datos/datos |
| Virtua Tenis 2 (USA) | LBA 0, 33600, **audio** | LBA 45000, 286564, datos (2336, modo 2) | audio/datos, CDI 3.0 |

### Presentar el disco honestamente

`DCEMU_COMO_GD` anuncia la pista de datos en el FAD 45150 para que el disco parezca el GD-ROM del
que se ripeó; `iso_read_sector()` entonces traduce solo las 17 lecturas del área de arranque de
vuelta al comienzo real de la pista y deja en paz los LBA propios del ISO9660. Eso llegaba más
lejos que nada mientras REQ_SES estaba roto, pero es la **rama equivocada**: la ROM toma su camino
GD, y el archivo que encuentra queda por debajo de un umbral que ese camino rechaza, así que llama
a `menu(1)` y vuelve al menú.

**Dejada en paz, la ROM toma la rama MIL-CD** — tipo de disco CD-ROM/XA — que carga su propio
manejador en `0x8CE00000`, enumera las sesiones y arranca la última. Esa es la rama que usa una
consola real para estos discos.

Nota que dcemu también arranca estos juegos **sin** `--bios` — carga `ip.bin` y `1st_read.bin`
directo de la imagen, y con un `.cdi` ese camino no emite ningún paquete SPI: todo va por los
hooks de syscall. Ambos caminos llegan ahora al mismo lugar.

---

## `Virtua Tennis (USA).cdi` es un rip dañado, y el daño se lee como un bug de entrada del emulador

Sus menús repiten un movimiento de cursor por cuadro (60/s) mientras se mantiene una dirección —
medido con `DCEMU_MANTENER_DERECHA`: 120 key-ons/s del AICA del tic de menú, sin retardo inicial —
porque tres bytes de la función de repetición de teclas del juego están en cero en el archivo: los
índices de dirección de `MOV #imm,R4` (`E4 01/02/03` → `E4 00` ×3, una sola aparición, en
`0x3066e600+18/22/26`).

Con el índice 0 para toda dirección, los contadores de repetición por dirección colapsan sobre la
ranura de ARRIBA, que el barrendero por cuadro recarga a "en reposo" cada vez que ARRIBA *no* está
apretado — así que cada dirección mantenida dispara como una pulsación nueva en cada cuadro.
ARRIBA sola (o cualquier diagonal con ARRIBA) repite a los 7,5/s de diseño, que es la huella.

El rip `Virtua Tennis (2000)(Sega)(US)[cr DCRES][f PAL 60Hz][repack].cdi` tiene los bytes
correctos y repite a 7,5/s — **usar ese**. flycast y Deecy reproducen la ametralladora idéntica con
el archivo dañado (los tres emuladores contestan el Maple byte por byte igual, verificado contra
ambas fuentes), que es lo que probó que era el archivo y no el emulador: cuando un juego se
comporta igual de mal en emuladores independientes, hay que comparar el rip contra otra estirpe
antes de culpar a la emulación — la búsqueda de aguja-ancla + geometría de sector en los bytes de
este archivo es como se establecieron ambos hechos (el daño, y su extensión exacta: 3 bytes en
±1KB) sin arrancar nada.
