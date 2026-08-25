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
correctos y repite a 7,5/s. **Y el `.gdi` USA de tres pistas también** (comprobado el 2026-08-05,
jugado): el daño es de *ese archivo `.cdi`*, no de la versión USA ni de la estirpe del volcado, así
que la regla no es «evitar la USA» sino «cambiar de rip». flycast y Deecy reproducen la ametralladora idéntica con
el archivo dañado (los tres emuladores contestan el Maple byte por byte igual, verificado contra
ambas fuentes), que es lo que probó que era el archivo y no el emulador: cuando un juego se
comporta igual de mal en emuladores independientes, hay que comparar el rip contra otra estirpe
antes de culpar a la emulación — la búsqueda de aguja-ancla + geometría de sector en los bytes de
este archivo es como se establecieron ambos hechos (el daño, y su extensión exacta: 3 bytes en
±1KB) sin arrancar nada.

---

## El backend `.gdi` (2026-08-05)

`gdi.c`/`gdi.h`. Es, con `.chd`, uno de los dos formatos en que está preservada la biblioteca
de Dreamcast, y el único de los dos que no exige una dependencia nueva: un `.gdi` es **texto
plano** y las pistas son archivos crudos al lado.

```
3
1 0     4 2048 track01.iso 0
2 1860  0 2352 track02.raw 0
3 45000 4 2352 track03.bin 0
```

Primera línea el número de pistas; después `numero LBA tipo tamaño_de_sector archivo offset`.
`tipo` es 4 para datos y 0 para audio —son los bits de control del subcanal Q, no un modo— y
el LBA es el del disco, o sea FAD menos 150.

**Lo que el `.gdi` no dice es si una pista de datos de 2352 es modo 1 o modo 2**, y de eso
depende dónde empiezan los 2048 de usuario (16 o 24). Se lee del propio sector: byte 15,
detrás de los 12 de sincronismo y los 3 de dirección, y sólo si el sincronismo está donde
tiene que estar. Suponerlo desplaza cada lectura y el volumen no se monta.

De ahí para arriba **no se distingue de un `.cdi`**: la misma tabla de pistas (`struct cdi_t`,
reusada a propósito), la misma TOC, las mismas sesiones y el mismo `min_iso_open_pista()`. La
única línea propia del formato es cuál archivo se abre — la pista de datos vive en el suyo, no
dentro del índice—. Por eso `iso.c` distingue con `ES_MULTIPISTA()` en vez de repetir seis
accesores.

### Qué se logró y qué no

Con el `.gdi` de DCDoom, verificado:

- las tres pistas se leen con su geometría exacta (LBA 45000, 504 150 sectores de 2352, modo 1
  detectado del sector);
- la lectora reporta **2 sesiones y 3 pistas**, que es lo que un GD-ROM tiene;
- el sistema de archivos monta, y `0WINCEOS.BIN` se encuentra, se lee y se descifra
  —2 154 496 bytes—.

**Y no arranca.** El guest corre de verdad —el ritmo cae de 1,49× a 0,44 %, que es la firma de
trabajo real y no de un lazo de espera— pero termina ejecutando fuera de mapa. Le falta el
vector de syscall sin nombre, que este camino llama **con parámetros** (`R4=1 R5=8cffffc8
R7=1`) mientras que en el camino del `.cdi` nunca se usa.

Eso **no es del formato**: es que un GD-ROM prensado ejerce caminos del boot que un selfboot
en CD no. El primero ya apareció y está resuelto:

### REQ_MODE y SET_MODE, que sólo pide un GD-ROM de verdad

Los comandos 30 y 31 del syscall del GD-ROM. El driver hace `REQ_MODE`, retoca los 32 bytes de
parámetros de la lectora y los devuelve con `SET_MODE`. **Con un selfboot en CD no aparecen**,
y por eso el camino del `.cdi` nunca los necesitó.

Sin ellos el guest se llevaba lo que hubiera en la pila y se quedaba dando vueltas en un lazo
de una instrucción. `REQ_MODE` contesta ahora los mismos 32 bytes que el paquete SPI
—`gdrom_copiar_modo()` sobre el `modo[]` de `gdrom.c`, misma razón que
`gdrom_construir_toc()`: dos copias del mismo bloque se separan—. `SET_MODE` acepta y no
guarda: velocidad, tiempo de espera y reintentos no existen cuando los sectores salen de un
archivo, y **lo que colgaba no era no aplicar el modo sino no contestar**.

### Regresión

Ninguna: `ctest` 21/21, la captura de DCDoom por `.cdi` sigue en `36578F59…` byte a byte, y
Crazy Taxi, Virtua Tennis y Capcom vs. SNK siguen dibujando.

### Por qué el `.gdi` de DCDoom todavía se rinde, hasta donde se llegó

El guest llama al syscall `0x8C0000E0`, que es el reinicio (ver `notas-arranque.md`), desde una
secuencia de cinco llamadas en `0x8C00D820`. La cuarta, en `0x8C00DAE0`, es una **verificación
de transferencia**:

```
    llama a 0x8C00D8C6; si devuelve 0, sale bien
    R1 = 0x8CE01010                  ; tabla de bloques pedidos
    R0 = [R1]                        ; cuantos
    si R0 == 0            -> reinicio
    si R0 > 168           -> reinicio
    ultima = R1 + 0xC + 12*(R0-1)    ; entradas de 12 bytes
    si ultima.inicio + ultima.largo != [0xA05F74F4]  -> reinicio
```

`0xA05F74F4` es **`SB_GDSTARD`**, el contador de dirección de la DMA del G1. O sea: el juego
lleva su propia lista de bloques pedidos y comprueba que la DMA haya terminado donde debía.

Eso destapó un agujero real —**el hook de syscall movía los datos y no los contadores**,
porque copia los sectores por su cuenta sin pasar por el camino de hardware de `gdrom.c`— y va
arreglado (`gdrom_dma_contadores()`). Es la forma de falla de siempre: los datos llegaban y el
rastro no.

**Pero no era la rama que falla.** Volcada la memoria, la tabla de `0x8CE01010` está **en
cero**: el contador es 0 y la comprobación se va por su primera rama, antes de mirar
`SB_GDSTARD`. El juego nunca llegó a pedir esos bloques, así que el fallo está más atrás — en
alguna de las tres llamadas anteriores de la secuencia (`0x8C00D940`, `0x8C00D900`,
`0x8C00D888`), que es por donde hay que seguir.

### Lo que destapó un GD-ROM de verdad, con catorce pistas (2026-08-05)

`Dave Mirra Freestyle BMX` tiene 14 pistas y encontró tres cosas que el rip de DCDoom —tres
pistas, una sola de datos arriba— no podía encontrar:

```
14
1  0      4 2352 track01.bin 0      <- datos, area de densidad simple
2  756    0 2352 track02.raw 0      <- audio
3  45000  4 2352 track03.bin 0      <- **el ISO9660**, LBA 45000..315894
4..13     0 2352 ...                <- diez pistas de audio CDDA
14 414528 4 2352 track14.bin 0      <- **mas datos**, LBA 414528..549149
```

**1. La pista del volumen no es la de LBA más alto.** `cdi_pista_de_datos()` toma esa, que es
correcto para un `.cdi` —una sola pista de datos, arriba de todo— y aquí elegía la 14. La regla
buena es **la primera pista de datos del área de alta densidad**, que es donde el GD-ROM pone
su ISO9660 y donde el boot ROM da por sentado que está el IP.BIN. Verificado: `CD001` en el
sector 16 de la pista 3.

**2. Los datos se reparten entre pistas, y cada una es un archivo.** El `1ST_READ.BIN` de este
juego está en el **LBA 547102**, que cae en la pista 14 — mientras que el sistema de archivos
que lo describe está en la 3. Con una sola pista abierta, leerlo era buscar más allá del fin
del archivo: **ni datos ni error, el emulador colgado**. `min_iso_agregar_pista()` registra
todas las pistas de datos y `posicionar()` enruta cada sector a la suya; un sector que no cae
en ninguna informa y falla, que es lo que no pasaba antes.

**3. El ejecutable de un rip en `.gdi` no está cifrado.** dcemu descifraba siempre, heredado
del `.cdi`, donde un selfboot sí lo trae cifrado. En los dos `.gdi` a mano el archivo en el
disco **ya es código SH-4 válido** —DCDoom empieza con un cargador auto-relocalizante, Dave
Mirra con seis NOP y un JMP— y descifrarlo lo vuelve basura: el guest terminaba girando en
`0x0000011C`, memoria baja. Es una regla de formato (`iso_ejecutable_cifrado()`), no una
heurística sobre el contenido.

Con las tres, Dave Mirra pasa de no montar a **ejecutar código del juego**. Lo que faltaba para
que dibujara no era del formato sino del convertidor YUV del TA, y está en
`docs/notas-graficos.md`: hoy el juego arranca, reproduce su FMV y se juega.

Y la lectura sin emular desde `0x8C08BF06` —a la dirección `0x2d2d2d0a`, que en ASCII es
`"\n---"`— **no era el problema**. ChuChu Rocket hace exactamente la misma lectura desde otro
PC y anda perfecto: es una biblioteca compartida mirando un byte de una cadena que en una
compilación de release no está inicializada. Anotarla aquí para que no vuelva a parecer una
pista.

---

## El audio de CD

`cdda.c/h`, y la lectora es su dueña: una pista de audio no pasa por el AICA como pasan las
voces del juego, la decodifica la unidad y le entrega muestras al chip por una entrada aparte.
El detalle del mecanismo y su verificación byte a byte están en `docs/notas-aica.md`.

Lo que toca a este archivo son los comandos, y son dos juegos que tienen que contestar lo mismo:

| paquete SPI (`gdrom.c`) | driver del boot ROM (`dcopcodes.c`) |
| --- | --- |
| `CD_PLAY` 0x20 | 20 PLAY_TRACKS, 21 PLAY_SECTORS |
| `CD_SEEK` 0x21 | 27 SEEK, y 33 STOP / 22 PAUSE |
| `CD_SCAN` 0x22 | — |
| `GET_SCD` 0x40, `REQ_STAT` 0x10 | 34 GETSCD, 36 REQ_STAT |
| — | 23 RELEASE |

**Los juegos llegan por el syscall, no por el paquete.** Con los hooks puestos —lo normal— Dave
Mirra pide el comando 20 y ChuChu Rocket también. La vía SPI existe para `--bios` y para un
guest que le hable a la lectora directamente, y está mucho menos ejercitada.

Dos cosas del paquete SPI que no son obvias:

- **El tipo de parámetro va en los tres bits bajos del byte 1**: 1 dice que las posiciones son
  FAD y 2 que son MSF (minuto, segundo, cuadro, 75 cuadros por segundo). La posición de arranque
  está en los bytes 2-4 y la de fin en los 8-10; las repeticiones, en los cuatro bits bajos del
  byte 6, donde 15 quiere decir «para siempre».
- **`CD_SEEK` también es como se para y como se pausa**: los tipos 3 y 4 no llevan posición.
  Sin ellos, un juego que calla su música con un seek de parar no la callaba nunca.

`iso_leer_audio()` es la puerta de lectura, y es otra que la de los datos: entrega sectores
crudos de 2352 bytes —sin volumen, sin encabezado, sin área de usuario de 2048— y abre el
archivo de la pista él mismo, porque `iso_init()` solo registra las de datos en `min_iso_*`. En
un `.gdi` cada pista de audio es su propio archivo y hasta que alguien pide su audio no se abre
nunca.

---

## 2026-08-06 — Cinco imágenes más, sin tocar una línea del emulador

En `roms/` había seis `.zip` sin descomprimir desde siempre. Descomprimidos y probados, **cinco
juegos nuevos arrancan y dibujan a la primera**: 4X4 EVO, Dead or Alive 2, Mat Hoffman's Pro BMX,
Quake III Arena y Tennis 2K2. Con eso las imágenes comerciales que corren pasan de 9 a 14.

Cuatro de los cinco llegan a juego con el banco de botones a ciegas:

| juego | dónde llega en 40-90 s emulados | escenas |
| --- | --- | --- |
| 4X4 EVO | en carrera, con tablero y menú de pausa | 879 |
| Dead or Alive 2 | en combate, con los dos luchadores y el HUD | 2181 |
| Mat Hoffman's Pro BMX | en el half-pipe, con marcador | 2241 |
| Tennis 2K2 | en partido, con público y marcador | 2323 |
| Quake III Arena | pantalla «SELECT DEVICE», no pasa de ahí | 1501 |

**Quake III no está trabado**: el volcado de salida muestra al guest ejecutando su bucle normal, con
`SR` y `PR` sanos. Es una pantalla de selección de puerto que espera una entrada que las pulsaciones
a ciegas del banco no le dan — pregunta de entrada, no de emulación. Queda como lo único de los
cinco que no se pudo ver en juego.

Los accesos sin emular son mínimos: entre uno y cinco por corrida, y el que se repite en tres de
ellos es el sondeo del bus de expansión G2 en `0xA1000400`-`0xA1001800`, el mismo que ya hacía
Virtua Tennis 2 y que es benigno.

**Bajo `--bios` los cinco quedan en el menú del boot ROM** (Play / File / Music / Settings), con
3496 escenas y capturas prácticamente idénticas entre sí. Eso no dice nada de estas imágenes: es la
frontera conocida de ese camino, donde cae cualquier disco. Ver `docs/bios-boot-plan.md`.

**Por qué valía la pena y no era sólo inventario**: el parque de KOS no ejercita mipmaps, ni los
modos de repetición del TSP, ni los códigos de mezcla 2 y 3, ni el Offset Color — todo eso sólo lo
muestra un juego. Cinco juegos más son cinco sitios más donde esos caminos se recorren, y el trabajo
gráfico reciente se validó contra seis. Cuesta espacio: el disco pasó de 13,8 GB libres a 8,3.

## El backend `.chd` (2026-08-07)

`chd.c`/`chd.h`, sobre libchdr (vendorizada en `deps/libchdr` con sus tres dependencias — lzma,
miniz, zstd — porque sus binarios publicados son de MinGW y aquí se compila con MSVC; commit
`6cde5348` del upstream). Es el otro formato en que está preservada la biblioteca, y el de las
colecciones actuales: un solo archivo comprimido por juego, los sectores en «hunks» (aquí de 8
frames de 2448 bytes: 2352 de sector más 96 de subcanal) y las pistas descritas en metadatos de
texto, una entrada por pista.

### La interpretación de los metadatos se validó antes de escribir el backend

La aritmética no está en ninguna especificación: está en cómo chdman escribe y en cómo la leen
los consumidores probados (la referencia fue flycast, `core/imgread/chd.cpp`). Antes de tocar
dcemu, una sonda aparte volcó los metadatos de las ocho imágenes y aplicó esa aritmética, y la
tabla resultante se comparó contra los `.gdi` del árbol — **el mismo juego en los dos
contenedores tiene que dar la misma tabla**. Crazy Taxi 2 (`.gdi`: LBA 0/450/45000) y Virtua
Tennis (0/600/45000) calzaron exactos. Las reglas, todas confirmadas por esa comparación:

- el FAD de cada pista se **acumula** desde 150, y el campo `FRAMES` **incluye el relleno**
  (`PAD:`), que es como la pista 3 cae sola en el LBA 45000 y el total cierra en 549 300;
- `sectores` para la TOC es `FRAMES` menos `PAD`;
- dentro del archivo cada pista empieza en un frame múltiplo de 4 (`CD_TRACK_PADDING` de MAME):
  con pistas no múltiplo de 4, el frame inicial de la pista 3 es 45004, no 45000;
- hay **cuatro tags de metadatos** y se prueban en orden (`CHT2`, `CHTR`, `CHGT`, `CHGD`); los
  dos últimos dicen GD-ROM, y las ocho imágenes de la mano son `CHGD` v5;
- con el tag `CHGD` el **audio está guardado con los bytes de cada muestra invertidos** (el
  orden del Red Book); con el `CHGT` viejo no, porque salió de un chdman parcheado anterior.

### Dónde encaja en el árbol: un lector por callback

Un `.chd` no tiene un archivo que posicionar — los sectores salen de hunks comprimidos — así que
`min_iso_*` ganó una tercera forma de abrir: `min_iso_open_lector()`, donde cada sector de 2048
lo entrega una función. Toda la geometría (pistas, modos, desplazamientos, el enrutado entre
pistas de datos que en un `.gdi` hace `min_iso_agregar_pista()`) queda del lado de `chd.c`, que
además hereda dos reglas del `.gdi`: la pista del volumen es **la primera de datos del área de
alta densidad**, y el modo de una pista de 2352 se lee **del byte 15 del propio sector**, no de
los metadatos. Las reglas de formato siguen al contenido, no a la extensión: un `.chd` de GD-ROM
trae el ejecutable en claro como un `.gdi`, uno de MIL-CD lo traería cifrado como un `.cdi`
(`iso_ejecutable_cifrado()`, `iso_es_gdrom()`).

### Verificación

- **Crazy Taxi 2 y Virtua Tennis, `.chd` contra `.gdi`, byte a byte**: la captura a los 20 s
  emulados (`--sin-audio --sin-vmu`) es idéntica por los dos contenedores.
- **DCDoom por `.cdi` sigue dando su hash canónico** (`198B396F…`), que es lo que prueba que la
  reestructuración de `min_iso` no movió el parque existente. Las 23 suites en verde.
- **Tres juegos nuevos arrancan**: 18 Wheeler llega **a juego** (vista de cabina, 6102 tiras por
  escena — ojo: sus primeros ~45 s emulados son una secuencia de arranque que dibuja 1 tira por
  escena y la captura sale negra; no está colgado, espera START), Tony Hawk's Pro Skater 2
  muestra su intro (y ejercita el reparto de datos en dos pistas del área alta — la forma de
  Dave Mirra — con datos en la 3 y en la 5), y Capcom vs. SNK 2 llega a su pantalla de tarjeta
  de memoria. Capcom vs. SNK y Virtua Tennis 2 (Europe) arrancan a las suyas.
- **El audio se validó por datos, no de oído**: la pista 2 de Crazy Taxi 2 extraída del `.chd`
  con la inversión aplicada calza byte a byte contra el `track02.raw` del `.gdi`... corrida
  **1456 bytes = 364 muestras**. El corrimiento es sub-sector — un error de mapeo de frames
  daría múltiplos de 2352 — y es la corrección de offset de lectora que redump aplica y el rip
  TOSEC no: **los dos rips difieren, los dos backends entregan fielmente el suyo**. Por eso el
  A/B de `basic_cdda` con `--disco=` da un `.wav` distinto por contenedor (mismo largo, misma
  envolvente, pico 32132 idéntico) y eso no es un bug.

### Lo que queda anotado sin ejercitar

- **Un `.chd` de MIL-CD** (tag `CHT2`, multisesión): la última pista se anuncia tras el hueco
  estándar entre sesiones (11 400 frames, la regla de flycast), porque chdman guarda las pistas
  pegadas y el ISO9660 de un selfboot lleva sus LBA absolutos de donde la grabadora lo puso.
  Ninguna imagen a mano lo ejercita.
- **Un pregap distinto de 0** en los metadatos diría que delante de la pista hay frames que no
  están en el archivo; ninguna imagen que circule lo trae y el lector lo rechaza con aviso en
  vez de inventar la resta.
- El tag `CHGT` viejo (audio sin invertir) está contemplado y sin material que lo pruebe.

## El tope de pistas era el "colgado" de Mortal Kombat Gold (2026-08-24)

`CDI_PISTAS_MAX` era 32 y el lazo de metadatos de `chd_abrir()` paraba ahi **sin
decir palabra**. El CHD de Mortal Kombat Gold trae **53 pistas** (una tanda de
audio por personaje mas las dos de datos del area alta), y la que se caia era
justamente la ultima de datos — donde vive su `1ST_READ.BIN`, en el LSN 545 928.
La cadena completa de la falla, que se archivo meses como incompatibilidad:

1. El stat del ISO9660 encontraba el archivo (el volumen esta en la pista 3,
   que si entraba), asi que la carga arrancaba con LSN y tamano validos.
2. Cada lectura caia fuera de toda pista (`chd: el sector %u no cae...`, una
   linea por sector) y la carga **seguia igual**: el binario quedaba en ceros.
3. El guest arrancaba sobre memoria sin inicializar, el PC se deslizaba en
   NOIMP desde `ACFFFFAE` hasta el espejo `AD00006C`, unos bytes decodificaban
   como `TRAPA`, y la tormenta contra el manejador Katana (`VBR=8C00F400`)
   parecia un cuelgue del juego. Igual en interprete y traductor — lo que
   correctamente se leyo como "no es el JIT", pero no era compatibilidad: era
   el lector.

El arreglo y sus dos guardas (la regla del arbol: decirlo):

- `CDI_PISTAS_MAX` = **99**, el tope del formato. Lo comparten `.cdi`, `.gdi`
  y `.chd` (una tabla de pistas para los tres).
- Llegar al tope ahora avisa (`chd_abrir`), porque un disco de verdad con 99
  seria sospechoso de por si.
- Una lectura fallida del binario de arranque ahora avisa (`iso.c`): "el guest
  va a arrancar sobre memoria sin inicializar" es exactamente lo que pasa.

Con el tope subido MKG llega **a la pelea** (Cyrax contra Tanya con el banco
ciego de botones, 534 tiras por escena) y sale **exacto int contra traductor**
(capturas identicas y 40 000 puntos al digito). El material queda 47/48 — solo
MvC2, el expediente entendido al ciclo.
