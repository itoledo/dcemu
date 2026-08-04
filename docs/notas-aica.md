# Notas: el AICA, el ARM7DI y el G2-DMA

Detalle del subsistema de sonido. `CLAUDE.md` tiene el resumen; `docs/aica-plan.md` es el plan y
el registro de lo que fue entrando.

**El sonido funciona: cuatro demos de KOS suenan.** `aica.c/h` es el bloque de registros del chip,
sus tres temporizadores, su controlador de interrupciones, su DMA interno y el sintetizador de 64
canales; `arm7.c/h` es el ARM7DI que lleva adentro; `g2dma.c/h` son los cuatro canales de G2-DMA
del Holly; `audio.c/h` es la única pieza que toca SDL. Los primeros tres están libres de SDL a
propósito, como `sistema.c` y `vram.c`, para que `tests/` los enlace de verdad — suites `aica`,
`arm7` y `g2dma`.

La referencia es el *Dreamcast/Dev.Box System Architecture* de Sega: §4.2.2 y §8.4.5 para el mapa,
§8.1.1 para los algoritmos, §8.4.1.4 para el G2-DMA. **El firmware ARM de KOS
(`kernel/arch/dreamcast/sound/arm/`) es la comprobación independiente sobre el papel**, y en un
punto lo desambigua — ver el nivel de interrupción más abajo.

**El ARM corre en las 135 demos, no en las siete de sonido.** `spu_init()` escribe `0xEAFFFFF8`
—un salto a sí mismo— en la dirección 0 de la RAM de sonido y suelta el reset en *todos* los
programas, "so that CD audio works", y el boot ROM mueve `ARMRST` tres veces antes de llegar a su
menú. Así que el barrido de demos es regresión para este subsistema, y `--sin-aica` existe para
apagar el chip entero cuando se aísla una.

---

## Las dos ventanas no son el mismo bloque con otro prefijo

El SH-4 llega al AICA por el G2 en `0x00700000` y el ARM desde adentro en `0x00800000`, y dos
registros existen en solo una de ellas (tabla 8-25): `ARMRST` (`0x2C00`) es del SH-4 solamente —
es el interruptor que mantiene al ARM en reset — y `L`/`M` (`0x2D00`/`0x2D04`), el número de
interrupción y el fin de interrupción, son del ARM solamente.

El tamaño de acceso también es asimétrico: el papel restringe al SH-4 a accesos de 4 bytes con
solo los 16 bits bajos válidos, pero el ARM escribe *bytes* — el `aica.h` de KOS define `CHNREG8`
y lo usa para pan (`+0x24`), nivel de envío (`+0x25`) y volumen (`+0x29`). Así que el archivo es
direccionable por byte y la restricción aplica en la entrada del G2.

## Todo deriva de `reloj_total`, como los temporizadores

`gcd(44100, DC_CPU_HZ) = 60`, así que son exactamente **735 muestras cada 3324992 ciclos de CPU**
— sin deriva, enteros. Y los 22,5792 MHz del bloque de audio son exactamente `44100 × 512`, así
que el ARM no necesita reloj propio: **512 ciclos de ARM por muestra**.

## El nivel de interrupción sale de tres registros, y KOS fija la lectura

`SCILV2:SCILV1:SCILV0` llevan un bit cada uno en la posición de la fuente. Con los valores que
escribe `aica_init()` — `0x18`, `0x50`, `0x08` — la fuente 6 (timer A) sale **2** y la fuente 3
(MIDI in) sale **5**, que son exactamente los dos números contra los que compara `crt0.s`.
Cualquier otra lectura manda al firmware por la rama equivocada de su FIQ. Una fuente pendiente
sin máscara **queda pendiente**, la misma regla que necesitaron los eventos del ASIC.

---

## Tres cosas del ARM7DI costaron un caso de prueba cada una

Las tres están en la suite:

- **Leer R15 da PC+8**, y PC+12 cuando el desplazamiento viene de un registro o cuando se guarda a
  memoria.
- **`SUBS PC, R14, #4` no es una resta**: con `S` puesto y R15 como destino restaura CPSR desde
  SPSR. Así termina la FIQ del firmware; sin eso el ARM entra a su primera interrupción y no sale
  nunca de modo FIQ — perdiendo en el acto los R8-R14 del programa principal.
- **El bus es de 24 bits.** Sin acotar la dirección, el `0xEAFFFFF8` de KOS (un salto a PC−24, o
  sea `0xFFFFFFE8`) cae fuera de ambas regiones del mapa; con el acote, el núcleo camina ceros —
  que decodifican como un `AND` sin efecto — y da la vuelta, que es el lazo infinito que promete
  el comentario de KOS. ARMv3 no tiene Thumb, ni `LDRH`/`STRH`, ni `BX`, ni coprocesadores: esos
  patrones toman el vector de instrucción indefinida, que es lo que hace la pieza real.

---

## Dos bugs de envolvente que produjeron silencio sin ningún mensaje de error

Y solo la medición los encontró:

- **La tasa 0 de la tabla 8-5 significa ∞ — la envolvente no se mueve — no "instantáneo".** Leerlo
  al revés apagaba un canal en la primera muestra del decaimiento. El síntoma era un `.wav` de
  ocho segundos con **pico 14 sobre 32767**: el equivalente audible de un BMP negro. Con la tabla
  bien el mismo archivo pica en 16533.
- **La envolvente avanza antes de usar la muestra, no después.** Al revés, la primera muestra de
  cada canal sale con la atenuación de reposo, es decir muda.

## Un canal que llega a LEA sin bucle limpia su propio KYONB

Y eso es lo que hacía que los efectos de sonido se repitieran solos. KYONEX es un disparador
*global* — "all slots are made KEY_ON or OFF when 1 is written" — así que cada key-on que el
driver emite para una voz recorre las 64 ranuras y enciende las que todavía tengan KYONB puesto.
Una muestra de un solo disparo que simplemente se acabó dejaba `activo` en 0 con KYONB en 1, así
que el *siguiente* KYONEX de **cualquier otro canal** la reiniciaba, y el siguiente, y el
siguiente.

Medido en Crazy Taxi: una muestra de 1,19 s (SA `10db60`, LEA `665e`, sin bucle) se reprodujo seis
veces de más, cada una enganchada al key-on o key-off de otra voz, con sus registros sin tocar en
el medio. En Virtua Tennis los tres one-shots cortos que el juego pide **4** veces a lo largo de
dos minutos se reprodujeron **69**. Eso es el golpe de pelota de tenis y el sonido de moneda
repitiéndose.

El papel no dice que el bit se autolimpie — solo llama a KYONB el bit que "registers KEY_ON or
OFF" — pero sin eso no hay manera de que una ranura terminada deje de ser elegible, y es lo que
hace flycast (limpia KYONB cada vez que el AEG entra en release). Nada de lo que ya estaba bien se
mueve: `sound-sfx`, `sound-sfxbuf` y el arranque `--bios` dan `.wav` byte a byte idénticos antes y
después.

La regla vecina tenía el error simétrico: **un key-on para un canal en release tiene que
reiniciarlo.** La guarda era `!activo`, y un canal sigue activo mientras se desvanece, así que una
voz que el guest apagó y volvió a pedir enseguida se descartaba en silencio. Ahora es
`!activo || eg == RELEASE`, que es la condición de flycast. Suites
`una_muestra_terminada_limpia_su_kyonb` y `un_canal_en_release_vuelve_a_arrancar`. `--traza-mem`
imprime cada key-off y cada fin de muestra al lado de los key-on, que es como se encontraron los
dos.

## El ADPCM es determinista

Sigue §8.1.1.2 al pie de la letra y se hace en enteros — los ocho factores de la tabla 8-4 son
exactos en 256avos — así que dos corridas dan un `.wav` idéntico bit a bit. Nota que el papel
tiene una errata: la entrada 31 de la columna de decaimiento dice `90.` entre `920.` y `690.`; la
progresión es geométrica y el término correcto es 790.

---

## Medir el sonido

**`--captura-audio=ARCHIVO.wav` es el gemelo de `--captura-gl`, y va antes del mezclador, no
después.** Vuelca lo que el *mezclador produjo*, no lo que la tarjeta de sonido hizo con ello — la
misma lección que sacó los gráficos de las capturas de ventana. Hay que medirlo como se miden los
BMP: muestras distintas de cero, valores distintos, RMS y pico. Un `.wav` silencioso es un BMP
negro. Cierra por `traza_resumen()`, así que `--salir-tras` importa tanto como para el
desensamblado. `--sin-audio` se salta la tarjeta y conserva el volcado; escuchar y medir conviven,
porque con el dispositivo abierto el volcado sale de un segundo anillo que llena el callback.

**El propio firmware del boot ROM suena, y es la mejor comprobación que hay sobre la afinación.**
Con `--bios` el chime sale a los 6,46 s: 70 key-ons repartidos en 48 canales, todos PCM16 de la
*misma* muestra (`SA 01852a`, con bucle `LSA 0001`..`LEA 00ab`) a diez alturas distintas. Esas
diez caen en la tabla 8-7 del papel con un LSB de tolerancia — C2, F♯3, G3, B3, D4, E4, F4, G♯4,
A♯4, D♯5. Si el incremento de fase estuviera mal no se sentarían en la escala temperada; se irían
desviando más cuanto más lejos de la nota base, y no lo hacen. Ninguna demo de KOS da esa
comprobación, porque ninguna toca un acorde. Ocho muestras de 1,4 millones tocan el riel, así que
el mezclador tampoco está saturando.

---

## El G2-DMA

**`0x005F7800-0x005F787F` son los cuatro canales de G2-DMA, y antes se desvanecían en
`control_mem`** — así que el guest escribía 1 en `SB_ADST`, lo leía de vuelta, obtenía 1 ("DMA en
curso") y se quedaba ahí. La misma forma que el CH2 DMA y `SB_G1SYSM`. `spu_dma_transfer()` es lo
que usa `snd_stream.c` para rellenar el buffer de stream, así que ese solo bloqueaba las demos de
streaming.

El fin de transferencia son los bits 15-18 de `SB_ISTNRM`, uno por canal; la interrupción propia
del AICA (`G2AICINT`) es el bit 1 de `SB_ISTEXT`, al lado del fin de comando de la lectora.

---

## El costo del ARM7 y el perfil

**El ARM7 es el mayor costo individual del emulador después del intérprete SH-4 — 14-15% de una
corrida — y aproximadamente la mitad es el guest sondeando.** Medido sobre Crazy Taxi en
movimiento: 2 161 263 641 pasos de ARM en ~18 000 ms, o sea 8,3 ns cada uno.

**El 0,0% de `perf_arm_ocioso` es un punto ciego, no una buena noticia**: solo detecta el salto a
sí mismo que deja el `spu_init()` de KOS en la dirección 0, y el driver de un juego espera de otra
manera. **`DCEMU_PERFIL_ARM=1`** (`arm7.h`) agrega los dos histogramas que lo contestan — por
dirección y por fila de la tabla de despacho.

Contra Crazy Taxi las 20 direcciones más altas se llevan **51,8%** de todos los pasos y son *tres
lazos*: un barrido sobre registros de 48 bytes en `0x6294` cuyo cuerpo no se ejecuta ni una vez en
una corrida entera (26%), un barrido de 64 entradas probando el bit 7 en `0x0a04` que encuentra
trabajo el 4,5% de las veces (20%), y un tercero en `0x09d4` (3%). Un cuarto de todas las
instrucciones son saltos; `MRS` y `MSR` salen con la *misma* cuenta, que es una sección crítica en
la que se entra 36 millones de veces.

**LTCG (`DCEMU_LTCG`, ahora encendido por omisión) vale 10,0% del ARM y 1,4% de la corrida** —
casi todo en el ARM, porque `arm7.c` llama a `aica_fiq_pendiente()` en `aica.c` en cada
instrucción; el intérprete SH-4 casi no se mueve, lo que contradice lo que `rendimiento-plan.md`
esperaba de él. Validado: 20/20 suites, SingleStepTests 113 191 ok / 0 fail **idéntico a la
construcción sin LTCG**, mismo hash de captura. No validado: el barrido de 150 demos.
`docs/arm7-plan.md` tiene los números y las tres rutas.

**Sobre un juego el mismo flag vale mucho más: 6,6% de la corrida y 14% del ARM** (Virtua Tennis,
60 segundos emulados por el attract 3D, 0,97x → 1,03x del tiempo real, que es el umbral de
velocidad de consola). El `.wav`, el BMP de `--captura-gl`, la cuenta de escenas y la cuenta de
instrucciones salen idénticos entre las dos construcciones, que es lo que dice que el flag no
cambia nada más que la velocidad.

Dos cosas distorsionan esta medición y ambas costaron una conclusión: **`--captura-gl` se come el
40% del tiempo real**, así que una comparación hecha con eso encendido mide el volcado de BMP; y
**la primera corrida después de un `--clean-first` sale lenta** (124,9 MIPS contra 138,7 de las
dos que siguen), así que hay que descartarla. Conviene alternar los dos binarios dentro de un
mismo lote en vez de confiar en una corrida de cada uno — con `--perf`, las cifras de LTCG se
repiten al 0,1% mientras la línea base deriva a medida que la máquina se carga.

---

## Lo que no está emulado

CDDA, el DSP de audio, el LFO, el filtro FEG (el papel dice cómo dejarlo pasante: `Q = 4`,
`FLV = 0x1FF8`, y el firmware de KOS simplemente lo apaga) y la interrupción de intervalo de
muestra. `docs/aica-plan.md`, "Lo que sigue faltando", tiene el detalle — incluido que en el
camino de KOS **el CDDA llega como syscall, no como paquete SPI**: el comando 20 del vector de
GD-ROM, que `hack_gdrom()` en `dcopcodes.c` tendría que contestar.

Dos valores se contestan sin una medición detrás, y están marcados como tales porque un registro
de identificación contestado a la ligera ya colgó al guest dos veces (`REVISION` y `SB_G1SYSM`):
`VER[3:0]` de `0x2800`, al que se le da 1, y `MEM8MB`, que se acepta y se ignora.
