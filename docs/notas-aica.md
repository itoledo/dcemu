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

## El audio de CD suena, y no es del AICA

`cdda.c/h`. La nota vieja decía que el CDDA no estaba emulado y anotaba que en el camino de KOS
**llega como syscall y no como paquete SPI** — el comando 20 del vector de GD-ROM. Estaba bien
visto: es exactamente por ahí que lo pide Dave Mirra Freestyle BMX, y también ChuChu Rocket.
Hoy están las dos vías.

Lo que hay que entender para que el resto se ordene solo: **una pista de audio no pasa por el
AICA como pasan las voces del juego**. La lectora la decodifica ella y entrega muestras al chip
por una entrada aparte; el guest no las ve nunca. Por eso el módulo es de la lectora, y por eso
lo que el juego manda son órdenes de transporte —«toca del FAD tal al tal, N veces»— y después
sondeos.

Tres cosas lo dejan chico:

- **El formato del CD es el de la salida del mezclador**: 44 100 Hz, estéreo, 16 bits con signo.
  Sale una muestra de CD por cada vuelta de `mezclar_una_muestra()` y no hay remuestreo. Lo
  único que hay que respetar es que el orden de bytes del CD es little endian **del disco**, no
  del anfitrión: se arma explícito.
- **Los sectores se leen crudos**, 2352 bytes, por `iso_leer_audio()`. No pasan por
  `min_iso_*` — ese lector solo conoce pistas de datos y habla en sectores de 2048 con
  encabezado — y en un `.gdi` el archivo de la pista **ni siquiera está abierto** hasta que
  alguien pide su audio.
- **La suma va después de MVOL.** MVOL es el volumen maestro de las 64 voces; el CD-DA no es una
  voz. En el chip entra al mezclador del DSP con sus propios registros de atenuación, y el DSP
  no está emulado, así que sumarlo fuera de MVOL a nivel fijo es lo más parecido a «otra entrada
  del DAC». Un juego que baja MVOL para callar sus efectos no debería quedarse sin música.

### La mitad que no es reproducir

`GET_SCD` y `REQ_STAT`. **Así es como un juego sigue su propia música**: sondea la posición de
la aguja y cambia de tema cuando pasa cierto FAD. Contestar siempre «pista 1, datos, parada en
el 150» es la forma de falla de siempre — una respuesta válida que no quiere decir nada — y era
literalmente lo que había: el estado de audio iba fijo en `0x15`, «sin información».

Ahora los dos contestan el estado real (`0x11` sonando, `0x12` pausado, `0x13` terminado), la
pista, el FAD absoluto y el transcurrido dentro de la pista, y el byte de control dice audio en
vez de datos mientras suena.

`SEEK` deja la cabeza puesta y **pausada**, que es lo que dice el protocolo y lo que espera
quien encadena un SEEK con un RELEASE. Y por el paquete SPI los tipos 3 y 4 de `CD_SEEK` no
llevan posición: son «parar» y «pausar». Sin ellos, un juego que calla su música con un seek de
parar no la callaba nunca.

### Cómo se verificó

Byte a byte, que es lo que este árbol pide para el sonido. `--captura-audio` produce un `.wav`
determinista; el archivo de la pista está ahí al lado en un `.gdi`. Se busca un pedazo de la
pista dentro de la captura:

| juego | pista | dónde aparece | cuánto coincide |
| --- | --- | --- | --- |
| Dave Mirra Freestyle BMX | `track06.raw` | cuadro 960 615 (21,78 s) | 893 760 bytes = 5,07 s |
| ChuChu Rocket! | `track04.raw` | cuadro 273 442 (6,20 s) | 1 340 640 bytes = 7,60 s |

Coincidencia **literal**, no parecido: eso valida de una vez la pista elegida, el offset del
sector, el orden de bytes y que el mezclador no la toca. La coincidencia se corta cuando el
AICA empieza a sonar encima, que es lo esperado.

Dave Mirra pide `FAD 335723 a 345605`, que son exactamente los extremos de su pista 6
(LBA 335573, y la 7 empieza en 345456); ChuChu pide `354902 a 362979`, la pista 4 completa. O
sea que la conversión de número de pista a rango de FAD también está verificada contra la
tabla del disco.

**Trampa a la vista**: la música empieza cuando el juego llega a su menú. En Dave Mirra el
`PLAY` cae a los 39,3 s de tiempo emulado, así que una corrida de 40 s captura 0,7 segundos y
la comparación no encuentra nada. Hay que saltar el FMV con las teclas y correr lo suficiente.

### Para escucharlo hace falta `--limitar`, y el emulador ahora lo dice

Primer reporte de uso real: «se escuchó la música, pero el juego estaba algo acelerado así que
no se escuchaba perfecto». Las dos mitades de esa frase son **el mismo hecho**.

El AICA produce muestras al ritmo del **tiempo emulado** —735 cada 3 324 992 ciclos de CPU— y la
tarjeta consume 44 100 por segundo de **tiempo real**. Si el emulador corre a 1,33×, el chip
genera unas 58 200 por segundo real y el anillo de salida descarta lo que sobra. No se oye como
un cambio de tono: se oye como cortes.

Medido en Dave Mirra, 30 segundos emulados:

| | velocidad | cuadros tirados | rachas | audio perdido |
| --- | --- | --- | --- | --- |
| sin `--limitar` | 1,33× | 348 690 | 622 | **7,9 s de 30** |
| con `--limitar` | 0,99× | 2 919 | 5 | 0,1 s |

Las cinco rachas que quedan con el limitador son del arranque, mientras la referencia de tiempo
real se asienta.

Tirar es lo correcto —pisar lo que el hilo de SDL está leyendo sería peor— pero **hacerlo en
silencio convierte «el emulador va rápido» en «el sonido de dcemu está mal»**, que son dos
problemas distintos. Ahora `traza_resumen()` cuenta los cuadros y las rachas y lo informa al
salir.

**El aviso NO está detrás de `--traza-mem`, y ese detalle es la parte interesante**: la traza
cuesta lo bastante como para frenar el emulador por debajo del tiempo real —0,73× contra
1,33×—, o sea que el síntoma desaparece justo cuando se enciende lo que lo reportaría. Un
diagnóstico que solo existe cuando el problema no ocurre no sirve de nada. Es una línea, y solo
si de verdad se perdió algo.

`--captura-audio` no se ve afectado por nada de esto: el `.wav` lo escribe el emulador desde el
anillo, no la tarjeta. Por eso la verificación byte a byte de arriba salió limpia aunque la
corrida fuera rápida.

## El DSP de efectos (2026-08-06)

**Emulado** (`aicadsp.c/h`, fase 6 del plan): los 128 pasos del microprograma tal como los
enumera el DevBox §8.1.1.8, contrastados con las implementaciones que descienden de las notas
de Corlett — el formato del paso, el orden de las operaciones y el flotante de 16 bits del
anillo son los mismos en MAME, nullDC y reicast, y ese acuerdo es lo más parecido a una segunda
fuente que tiene este chip.

Las decisiones que importan:

- **El microprograma, los coeficientes y las direcciones se leen de `aica_reg[]` directamente**,
  sin copia: los registros son el almacenamiento, así que la subida por DMA interno y la
  relectura del guest funcionan solas. Un aviso (`aicadsp_tocar`) marca el programa como sucio y
  el reescaneo —contar pasos con contenido— pasa una vez, no por muestra.
- **El costo con el programa en cero es un retorno temprano**, que es lo que corre el parque
  entero de KOS: su driver no programa el DSP nunca. Verificado con el `.wav`: `cpp-modplug_test`
  —la única demo de sonido que produce señal en el arnés sin disco— sale **byte a byte idéntico**
  con y sin el módulo. (`sound-sfx`, `sound-hello-mp3/adx/ogg` capturan silencio en este arnés y
  no guardan nada: la regla del `.wav` silencioso, aplicada.)
- **El dato de un MRD lo entrega el IWT de dos pasos después** (`dsp_memval[4]`), que es la regla
  que el ensamblador de Sega da por sentada.
- **El DSP es el cuarto escritor de la RAM de onda**: cada MWT marca su página
  (`onda_marcar_escritura`), o el ARM se saltearía un barrido que debía rehacer.
- El envío de un canal (ISEL/IMXL, +0x20) va **antes del DISDL y sin paneo**: DISDL/DIPAN son de
  la salida directa, y el paneo del efecto lo pone EFPAN a la salida. Las 16 EFREG y las 2 EXTS
  se componen con EFSDL/EFPAN (0x2000-0x2044), la misma tabla de atenuación que un canal, antes
  de MVOL.

**El censo que cambió el veredicto del plan.** «Casi nadie lo nota» era cierto para KOS y falso
para Katana: de los juegos censados, **Crazy Taxi programa 78 pasos, Tennis 2K2 y Virtua
Tennis 2 programan 110**, con las 16 ranuras EFSDL activas — reverberación real que dcemu venía
descartando. Dead or Alive 2 manda 1,4 M de muestras a MIXS **sin programa y sin EFSDL**, o sea
que en el chip tampoco sonarían. El `.wav` del banco de Crazy Taxi cambia como cambia una
reverberación: mismo largo, RMS +3 % (4718 → 4871), sin recorte nuevo, y **bit a bit
reproducible** entre corridas.

**El CD-DA tiene dos caminos y la traza dice cuál corrió.** En el chip entra por EXTS y suena
por los EFSDL de las ranuras 16 y 17, pasando por MVOL como todo; eso es lo que pasa si el guest
programó esos registros. Pero dcemu con hooks de syscall no corre la inicialización de sonido
del boot ROM, así que un guest que confía en lo que el ROM dejó puede no escribirlos nunca: con
la regla del chip a secas se quedaría sin música aquí y no en la consola. Si ninguna de las dos
ranuras tiene EFSDL, corre el camino de antes — nivel fijo, fuera de MVOL.

La suite `dsp` (7 casos) ensambla microprogramas a mano y verifica la aritmética del paso, la
línea de retardo TEMP con su decremento, el anillo en crudo y el flotante de ida y vuelta sobre
los 65536 patrones.

## El filtro FEG

Emulado desde 2026-08-06, el mismo día en que el censo encontró a su único cliente. Es el paso
bajo IIR por canal de la sección 8.1.1.7, con una envolvente propia de cuatro estados que mueve
la frecuencia de corte entre FLV0 y FLV4 al ritmo de FAR/FD1R/FD2R/FRR. Lo que costó saber, en
orden:

- **La envolvente sale entera de los papeles, pero hay que cuadrarlos entre sí.** La tabla 8-14
  del DevBox («Change Time from 0x0008 to 0x1FF8») es **la tabla de decaimiento del AEG
  multiplicada por 4, entrada por entrada** (472800 = 4×118200, 405200 = 4×101300, …,
  12,4 = 4×3,1) — así que en `aica.c` no se copia: se deriva. El AICA_E dice «same as AEG» y
  reimprime la tabla del AEG tal cual; el DevBox da el barrido completo medido, y es el que
  vale. flycast reusa los pasos del AEG sin escalar y su barrido tarda 8× (el rango del FLV es
  ocho veces el del AEG); ahí el papel gana. Una sola tabla para los cuatro estados: el ataque
  del AEG tiene curva propia, el del FEG no.
- **La ecuación del IIR no está en ningún papel** — las figuras 8-13/8-15 que la definían se
  perdieron en texto — así que la aritmética es la de la ingeniería inversa (Highly Theoretical
  de Neill Corlett, vía el `sgc_if.cpp` de flycast): el valor de 13 bits se lee como flotante de
  4 bits de exponente y 9 de mantisa con bit implícito, y en Q30
  `y[n] = −a0·x[n] + (2−f−a0)·y[n−1] − (1−f)·y[n−2]`, con el error de truncado realimentado a la
  muestra siguiente. La Q entra escalando `f` desde una tabla de 32 entradas cuyo cero cae en
  Q = 4 — exactamente el «pasante» del papel, que es lo que ata la tabla invertida a la
  documentación. **El filtro invierte el signo** (y → −x en continua); es del chip.
- **LPOFF existe y no está documentado**: bit 5 de `+0x28`. KOS escribe `0x24` ahí con el
  comentario «turn off Low Pass Filter», y ese bit es lo que protege al parque entero de demos.
- **Cuándo filtrar es una decisión, y está anotada en `feg_decidir()`.** No se filtra con LPOFF,
  ni con los cinco FLV en cero (el archivo de registros que nadie escribió; el papel sólo define
  0x0008–0x1FF8), ni en el pasante — que aquí incluye el `0x1FF7` de Katana, un LSB debajo del
  documentado. El chip real sí corre el filtro en el pasante, casi transparente; pagarlo en cada
  voz de cada juego Katana por una parte en mil de mantisa no vale, y el A/B del `.wav` vigila
  la aproximación.
- **El estado interno se recorta a 20 bits** (el ancho del mezclador del chip) y lo que vuelve
  al mezclador de dcemu, que es de 16, se recorta aparte: una resonancia de +20 dB sobre una
  muestra a fondo desbordaría `(muestra * ganancia)`.

**La verificación, diseñada antes de creerle**: tres casos en la suite `aica` — la matriz de
activación completa, un filtro cerrado que se come Nyquist (−60 dB) y deja la continua entera
con el signo dado la vuelta, y la envolvente que barre FLV0→FLV1→FLV2→FLV3, retiene, y sólo con
el key-off camina a FLV4. Y el A/B de extremo a extremo, con los binarios hasheados
(E8EBE877 ≠ F6DF31BA): **cpp-modplug byte-idéntico** (LPOFF), **Crazy Taxi byte-idéntico** (el
pasante de Katana se saltea, y de paso confirma que Katana pone Q = 4), y **DOA2 cambia y sólo
lo justo** — 29 de 73 key-on con filtro real (`1f28/1ff4/1c7c/1d30`), RMS 6073 → 6092 (+0,3 %),
**bit a bit reproducible** entre corridas y entre compilaciones.

Dos trampas de esa verificación, pagadas aquí:

- **`aica_tick()` produce a lo sumo 256 muestras por llamada** (`AICA_MUESTRAS_MAX`) **y
  descarta el atraso que sobre** — está hecho para que una pausa del emulador no se vuelva una
  ráfaga. El arnés de pruebas le pedía 2000 de una vez: recibió 256, la cola del buffer quedó en
  basura de pila, y las dos aserciones fallaron con el filtro perfectamente sano.
  `avanzar_muestras()` ahora avanza de a tramos.
- **El `.wav` de comparación se captura con `--sin-vmu`**: el primer A/B de cpp-modplug dio
  hashes distintos por la tarjeta en el bus, no por el filtro — la misma regla que ya costó un
  falso «el shader rompió todo» en los render paths.

## Lo que no está emulado — y ahora está censado, con la sonda probada

El LFO y la interrupción de intervalo de muestra. El LFO lleva desde 2026-08-06 un **centinela
en el key-on**: si un guest lo pide, el resumen de `--traza-mem` lo dice, que es exactamente lo
que al DSP le faltó — «casi nadie lo nota» fue una premisa sin medir y era falsa.

**El censo, sobre siete juegos** (Crazy Taxi, Tennis 2K2, Virtua Tennis 2, DOA2, Dave Mirra,
4X4 EVO, SF3):

- **LFO: cero.** Ni un key-on con PLFOS ni con ALFOS en ninguno.
- **FEG: sólo Dead or Alive 2 lo usa de verdad** — 17 de sus 40 key-on traen envolventes reales
  (`0x1F28`, `0x1C7C`, `0x1D30`), más 1 marginal de Crazy Taxi (`0x1FD3`). Todo lo demás que
  parecía filtro era `0x1FF7`: **el pasante que escribe el driver de Katana**, un LSB debajo del
  `0x1FF8` que documenta el papel. La primera pasada del censo, que sólo miraba FLV0 contra
  `{0, 0x1FF8}`, daba «filtro real en el 100 % de los key-on» de casi todos los juegos — un
  criterio ingenuo convertido en alarma general.

**Y la sonda tiene su prueba, por una razón concreta**: la primera corrida de este censo se hizo
con el contador declarado y el incremento nunca escrito — un error de edición — y reportó «sin
LFO» en seis juegos desde un contador que nada tocaba. Es la misma falla que la sonda de capas de
la OIT ese mismo día. `el_censo_del_lfo_cuenta` (suite `aica`) hace key-on con LFO y FEG puestos
y verifica que los contadores cuenten: la sonda contesta algo cuya respuesta se sabe, antes de
preguntarle lo que no.

El FEG que este censo dejó pendiente está implementado — la sección de arriba. El contador de
key-on con filtro real sigue en el resumen de la traza, ahora como registro de uso: dice en qué
corrida el filtro trabajó.

Del CD-DA falta el `CD_SCAN` de verdad: se acepta y la reproducción sigue donde estaba, que es
lo que ve un juego que adelanta y después suelta.

Dos valores se contestan sin una medición detrás, y están marcados como tales porque un registro
de identificación contestado a la ligera ya colgó al guest dos veces (`REVISION` y `SB_G1SYSM`):
`VER[3:0]` de `0x2800`, al que se le da 1, y `MEM8MB`, que se acepta y se ignora.
