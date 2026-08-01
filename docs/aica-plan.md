# Plan: emular el AICA

Escrito el 1 de agosto de 2026. Es la vía B de [pendientes-plan.md](pendientes-plan.md),
desarrollada contra la documentación de Sega en vez de por deducción.

La fuente es **Dreamcast/Dev.Box System Architecture** (Sega, 99/09/03), que **no va en el
repositorio** —son 2,5 MB de binario— y se baja de
<https://segaretro.org/images/7/78/DreamcastDevBoxSystemArchitecture.pdf>; conviene dejarlo en
`docs/`, que ya está ignorado para ese nombre. Las secciones que importan:

| sección | qué trae |
| --- | --- |
| §4.2 | el bus G2: direcciones, DMA, restricciones de acceso |
| §4.2.2 | el AICA: especificación, mapa de memoria, procedimiento de acceso |
| §4.2.3 | el RTC, que vive **dentro** del AICA |
| §8.1.1 | los algoritmos: bucle, ADPCM, AEG, PG, LFO, mezclador, FEG y DSP |
| §8.4.1.4 | los registros del G2-DMA en la Holly (`SB_AD*`) |
| §8.4.5 | el mapa de registros del AICA, campo por campo |
| §8.5.2 | las interrupciones: `G2DEAINT` (fin de DMA) y `G2AICINT` (del AICA) |

El otro testigo es el árbol de KOS que ya está instalado
(`C:\dcsdk\opt\toolchains\dc\kos`): `kernel/arch/dreamcast/hardware/spu.c`,
`kernel/arch/dreamcast/hardware/g2dma.c` y sobre todo
`kernel/arch/dreamcast/sound/arm/` —`crt0.s`, `aica.c`, `main.c`—, que es el firmware que
correría el ARM emulado. Vale la pena leerlos: **son la única comprobación independiente del
papel**, y en un punto lo corrigen (ver la fase 1.1).

---

## Punto de partida

Lo que ya está, y no es poco:

- **Los 2 MB de RAM de sonido en `0x00800000` funcionan.** `libdream-spu` reporta
  `Load OK, starting ARM`, o sea que la subida del firmware llega entera.
- **La ventana física (zona `0x00`) y la P2 (`0xA0`) resuelven igual**, porque
  `pvr_read`/`pvr_write` normalizan con `fisica | 0xa0000000`.
- **El bit del FIFO del G2** (`AICA_FIFO` en `G2_FIFO`, `0x005F688C`) se levanta al escribir la
  RAM de sonido y se baja al leer el registro, que es lo que `g2_fifo_wait()` mira.
- **El RTC ya está**, y es un registro del AICA: `0x00710000`/`04`/`08`, §4.2.3. `sistema.c` lo
  guarda en `bios/rtc.txt` y la BIOS ya no pide la fecha en cada arranque.
- **`aica_mem`**: 32 KB de respaldo en `0x00700000`, reservados desde la fase 5 del arranque por
  BIOS. Las escrituras del guest llegan; nadie las lee.

Lo que falta, en orden de lo que tapa a lo siguiente:

| pieza | estado | qué se pierde |
| --- | --- | --- |
| G2-DMA (`SB_AD*`, `0x005F7800`) | cae en `control_mem` | `SB_ADST` se lee 1 para siempre: el guest cree que la DMA nunca terminó |
| ARM7DI | no existe | la cola de comandos nunca pasa a `valid` |
| 64 canales | no existen | nada suena aunque el ARM corra |
| mezclador y salida | no existen | no hay `SDL_INIT_AUDIO` siquiera |
| DSP de audio | no existe | no hay reverberación; casi nadie lo nota |
| CDDA | no existe | `sound-cdda-basic_cdda` |

Y siete demos de KOS fallan por esto: `sound-multi-stream`, `sound-sfx`, `sound-sfxbuf`,
`sound-hello-adx`, `sound-hello-opus`, `sound-cdda-basic_cdda` y `libdream-spu`.

### Una cosa que hay que saber antes de decidir nada

**`spu_init()` de KOS saca al ARM de reset en *todos* los programas, no solo en los de sonido.**
El código es literal (`hardware/spu.c`):

```c
/* Load a default "program" into the SPU that just executes
   an infinite loop, so that CD audio works. */
g2_write_32(SPU_RAM_UNCACHED_BASE, 0xeafffff8);
spu_enable();
```

`0xeafffff8` es un `B` hacia atrás: un bucle infinito. O sea que en cuanto exista un ARM7DI que
respete `ARMRST`, **las 135 demos van a estar ejecutándolo**, no las siete. Eso condiciona dos
cosas del plan: el núcleo tiene que costar poco cuando no hace nada, y el barrido de las 135
demos pasa a ser regresión obligatoria de esta vía, no una cortesía.

---

## Objetivo

Que una demo de KOS suene, medido con un archivo y no de oído (hito E de
[pendientes-plan.md](pendientes-plan.md)), sin mover ninguna de las 100 demos que hoy funcionan.

Y, por debajo de eso: que lo que se escriba sirva para un juego comercial, no solo para KOS. Es
la razón por la que el plan es LLE y no HLE.

---

## Fase 0 — Decisiones de base

### 0.1 LLE, no HLE

- **HLE** sería reconocer el protocolo de `aica_queue_t` (`snd_iface.c`) y sintetizar el sonido
  en el anfitrión. Arregla las siete demos con mucho menos trabajo.
- **No sirve para un juego**: cada juego sube su propio firmware ARM y habla su propio
  protocolo. El de KOS es de KOS.

Se emula el chip. El ARM7DI es chico —ARMv3, sin Thumb, sin MMU, sin coprocesadores— y este
árbol ya tiene el patrón para escribirlo.

### 0.2 Una sola base de tiempo, otra vez

La lección de [clock-plan.md](clock-plan.md) aplica sin cambios: **nada de un segundo reloj**.
Todo se deriva de `reloj_total`, el contador monótono de ciclos de CPU que ya lleva `tmu.c`, y
cada consumidor guarda su propia marca en vez de acumular y restar.

Los dos ritmos nuevos salen exactos, lo cual es una suerte y conviene aprovecharla:

- **La frecuencia de muestreo son 44100 Hz.** `DC_CPU_HZ` es 199499520 y
  `gcd(44100, 199499520) = 60`, así que la relación exacta es **735 muestras cada 3324992
  ciclos de CPU**. Sin deriva, con enteros.
- **El bloque de audio corre a 22,5792 MHz** (§4.2.2: los 33,8688 MHz que entrega la lectora
  pasan por un PLL). Eso es exactamente `44100 × 512`.
  `gcd(22579200, 199499520) = 3840`, así que son **5880 ciclos de ARM cada 51953 de CPU**.

Cuidado con el desbordamiento: `reloj_total * 22579200` desborda un `unsigned long long` a los
68 minutos de tiempo emulado. Se divide primero, o se lleva la cuenta al revés —ciclos de ARM
pendientes— como hacen `tmu.c` y `wdt.c` con su resto.

### 0.3 Archivos, y qué queda libre de SDL

Siguiendo la convención del árbol (`sistema.c`, `ta.c`, `vram.c`: sin SDL ni GL, para que
`tests/` los enlace de verdad):

| archivo | qué es | SDL |
| --- | --- | --- |
| `aica.c/h` | el archivo de registros, los 64 canales, la envolvente, el mezclador | no |
| `arm7.c/h` | el núcleo ARM7DI: estado, despacho, excepciones | no |
| `arm7ops.c` | los manejadores por categoría | no |
| `aicadsp.c/h` | el DSP de 128 pasos (fase 6, opcional) | no |
| `audio.c/h` | la salida: `SDL_OpenAudio`, el búfer circular, el volcado a `.wav` | sí |

Solo `audio.c` toca SDL. `aica.c` produce muestras a un búfer y no sabe quién las consume, que
es lo que permite volcarlas a un archivo sin abrir el dispositivo de audio.

### 0.4 El tamaño de los accesos, que no es simétrico

§8.4.5 dice, textual: *"register accesses by the SH4 are 4-byte accesses only, and only the
lower 16 bits are valid"*. Pero el ARM sí escribe bytes: `aica.h` de KOS define `CHNREG8` y lo
usa para el paneo (`+0x24`), el nivel de envío (`+0x25`) y el volumen (`+0x29`).

O sea: **el archivo de registros es direccionable por byte y las que están restringidas son las
lecturas y escrituras que vienen del SH-4.** Implementarlo al revés —guardar palabras de 16
bits— funciona hasta que corre el firmware.

---

## Fase 1 — El archivo de registros y la ventana del G2

Sacar `0x00700000-0x00707FFF` del `memcpy` a `aica_mem` que hoy hay en `pvr_read`/`pvr_write` y
mandarlo a `aica_leer()`/`aica_escribir()`. El respaldo sigue existiendo —casi todos los
registros se leen tal como se escribieron—, pero un puñado dispara trabajo.

El mapa es la tabla 4-8 y hay que respetarlo entero, porque **las direcciones del G2 y las del
ARM no son la misma ventana con otro prefijo**:

| área | desde el G2 (SH-4) | desde el ARM |
| --- | --- | --- |
| datos de canal | `0x00700000`-`0x007027FF` | `0x00800000`-`0x008027FF` |
| datos comunes | `0x00702800`-`0x00702FFF` | `0x00802800`-`0x00802FFF` |
| datos del DSP | `0x00703000`-`0x00707FFF` | `0x00803000`-`0x00807FFF` |
| RTC | `0x00710000`-`0x0071000B` | — |
| RAM de sonido | `0x00800000`-`0x009FFFFF` | `0x00000000`-`0x001FFFFF` |

Dos registros existen en **una sola** de las dos ventanas, y eso es del papel, no una
simplificación:

- **`ARMRST` en `0x00702C00`, bit 0** — solo del lado del G2. *"This register can only be
  controlled by the system (SH4)."* Es el interruptor del ARM: 1 lo tiene en reset, 0 lo suelta.
  Es lo que escriben `spu_enable()`/`spu_disable()`.
- **`L[7:0]` en `0x00802D00` y `M[7:0]` en `0x00802D04`** — solo del lado del ARM. `L` es el
  número de la interrupción que está entrando y `M` el fin de proceso. El `crt0.s` de KOS los
  llama `intreq` e `intclr` y son literalmente las dos primeras cosas que toca su FIQ.

### 1.1 Un campo que la tabla del PDF no deja leer, y KOS sí

El registro 0 de cada canal aparece en la tabla 8-21 como
`KX KB -- SS LP PCMS SA[22:16]`, que suman 14 bits de los 16. La extracción de texto perdió el
ancho de los `--`, así que la tabla sola **no dice en qué bit está cada campo**. Y equivocarse
ahí es del tipo caro: `PCMS` mal ubicado hace que un PCM de 16 bits se lea como ADPCM.

`aica.c` de KOS lo fija sin ambigüedad:

```c
playCont = (mode << 7) | (smpptr >> 16);
if(loopflag) playCont |= 0x0200;
CHNREG32(ch, 0) = 0xc000 | playCont;    /* key on */
```

De donde:

| bits | campo |
| --- | --- |
| 15 | `KYONEX` |
| 14 | `KYONB` |
| 13-11 | — |
| 10 | `SSCTL` |
| 9 | `LPCTL` |
| 8-7 | `PCMS[1:0]` |
| 6-0 | `SA[22:16]` |

El resto de la tabla 8-21 sí coincide con KOS byte a byte, lo que da confianza en las dos
fuentes: `+0x24` bajo es `DIPAN[4:0]` y alto `DISDL[3:0]`; `+0x28` bajo es `Q[4:0]` y alto
`TL[7:0]`; `+0x18` es `OCT` en 14-11 y `FNS` en 9-0.

### 1.2 Los registros comunes que hacen algo

Del bloque `0x00702800` en adelante (tabla 8-25), los que no son puro respaldo:

- **`0x2800`**: `MVOL[3:0]` volumen maestro, `VER[3:0]` versión del chip (solo lectura),
  `DAC18B`, `MEM8MB` (bit 9) y `MONO` (bit 15). KOS escribe `BIT(9)` cuando *no* es una consola
  de serie, decidido con `hardware_sys_mode()` —el mismo `SB_G1SYSM` que ya está arreglado—, así
  que si aquello se rompe esto se rompe detrás.
- **`VER[3:0]` hay que decidirlo con cuidado.** Es exactamente la forma de `REVISION` del PVR
  (`0x005F8004`), que devolvía cero y dejaba al boot ROM en un `BRA` a sí mismo para siempre.
  Contestar 0 puede ser correcto o puede ser un cuelgue; queda anotado como cosa a medir contra
  el boot ROM real, no a inventar.
- **`0x280C`/`0x2810`/`0x2814`**: `MSLC[5:0]` elige el canal a monitorear y detrás se leen `SGC`
  (estado de la envolvente), `EG[12:0]`, `LP` (dio la vuelta el bucle) y `CA[15:0]` (posición de
  la muestra). **Esto no es opcional**: `aica_get_pos()` del firmware escribe el byte `0x280D` y
  lee `0x2814`, y el planificador de flujos de KOS decide con eso cuándo rellenar el búfer.
- **`0x2880`-`0x288C`**: la DMA *interna* del AICA (`DMEA`, `DRGA`, `DLG`, `DGATE`, `DDIR`,
  `DEXE`), que mueve entre la RAM de onda y los registros. No es la del G2. `DEXE` se pone en 0
  al terminar y levanta el bit 4 de `SCIPD`/`MCIPD`.
- **`0x2890`/`0x2894`/`0x2898`**: temporizadores A, B y C. Ocho bits que cuentan hacia arriba con
  un divisor de 1 a 128 muestras (`TACTL[2:0]`) y piden interrupción al pasar de todo unos a todo
  ceros. **El temporizador A es el latido del firmware de KOS**: `crt0.s` lo recarga con
  `256 - (44100/4410)` en cada FIQ e incrementa el reloj de milisegundos que vive en
  `0x00021000` de la RAM de sonido.
- **`0x289C`-`0x28B0`**: `SCIEB` (habilitación), `SCIPD` (pendientes), `SCIRE` (reconocimiento) y
  `SCILV0/1/2` (el nivel, tres registros que juntos dan un número de 3 bits por fuente) para las
  interrupciones **al ARM**.
- **`0x28B4`-`0x28BC`**: `MCIEB`, `MCIPD`, `MCIRE`, lo mismo pero **al SH-4**. La entrega es
  `G2AICINT`, que en dcemu es el bit 1 de `SB_ISTEXT` —el mismo registro externo por donde llega
  el fin de comando de la lectora—, o sea `intc_add_ext()`.

`aica_init()` del firmware programa esa secuencia entera en veinte líneas. Es el guion exacto
con el que se maneja la suite de pruebas: la misma idea que `tests/test_gdrom.c`, que maneja la
lectora exactamente como la maneja el boot ROM.

### 1.3 Cómo se prueba la fase 1

`tests/test_aica.c`, enlazando `aica.c` de verdad:

- La secuencia de `aica_init()` completa, comprobando qué queda en cada registro.
- Los tres accesos que el papel restringe: 4 bytes desde el SH-4 con solo los 16 bits bajos
  válidos, byte y palabra larga desde el ARM.
- `ARMRST` visible solo desde el G2; `L`/`M` solo desde el ARM.
- Los tres temporizadores con sus ocho divisores, contra `reloj_total`.
- `SCIPD`/`SCIEB`/`SCIRE`: que una fuente pendiente sin máscara **siga pendiente** —es
  exactamente el error que tenía `check_ints()` con los eventos del ASIC y que costó el arranque
  por BIOS— y que solo `SCIRE` la limpie.

---

## Fase 2 — El G2-DMA de la Holly (`SB_AD*`)

Es la fase con mejor relación entre trabajo y resultado, y **es independiente del resto**: se
puede hacer y verificar antes de que exista una sola muestra de audio.

`0x005F7800`-`0x005F787F` son ocho registros por canal, cuatro canales (§8.4.1.4). El canal 0 es
el del AICA:

| registro | dirección | qué es |
| --- | --- | --- |
| `SB_ADSTAG` | `0x005F7800` | dirección de partida del lado del G2 |
| `SB_ADSTAR` | `0x005F7804` | dirección de partida del lado de la memoria de sistema o de texturas |
| `SB_ADLEN` | `0x005F7808` | largo en unidades de 32 bytes; el bit 31 dice si al terminar se apaga `ADEN` |
| `SB_ADDIR` | `0x005F780C` | dirección de la transferencia |
| `SB_ADTSEL` | `0x005F7810` | qué la dispara: CPU, interrupción, patilla externa |
| `SB_ADEN` | `0x005F7814` | habilitación |
| `SB_ADST` | `0x005F7818` | arranque, y estado al leerlo |
| `SB_ADSUSP` | `0x005F781C` | suspensión |

**Hoy los ocho caen en `ES_CONTROL()` y de ahí a `control_mem`.** O sea que el guest escribe 1
en `SB_ADST`, lo vuelve a leer, le contesta 1 —"DMA en curso"— y se queda ahí. Es exactamente la
misma forma que el CH2 DMA y que `SB_G1SYSM`: algo que el guest pide, que dcemu acepta sin hacer
nada y sin decir nada. `spu_dma_transfer()` es lo que usa `snd_stream.c` para rellenar el búfer,
así que esto tapa a las demos de flujo por sí solo.

Lo que hay que hacer:

1. Un `case` por registro en `pvr_write()`, como los del CH2 DMA.
2. Al escribir 1 en `SB_ADST` con `SB_ADEN` en 1 y `ADTSEL` en modo CPU: mover `SB_ADLEN` bytes
   entre `SB_ADSTAR` y `SB_ADSTAG`, en bloques de 32.
3. **Validar los rangos**, porque el papel los enumera y porque un valor fuera de rango genera
   una interrupción de error en vez de una transferencia: del lado del G2,
   `0x00700000`-`0x00707FE0` (registros), `0x00800000`-`0x009FFFE0` (RAM de onda) y la imagen en
   `0x02700000`; del lado de la raíz, `0x0C000000`-`0x0CFFFFE0` (sistema) y
   `0x04000000`-`0x057FFFE0` (texturas).
4. Al terminar, encolar `G2DEAINT`, que es el **bit 15 de `SB_ISTNRM`** (`ASIC_EVT_SPU_DMA` en
   `asic.h` de KOS vale `0x000f`). Va con `intc_add()`.
5. `SB_ADST` vuelve a 0 y, si el bit 31 de `SB_ADLEN` estaba puesto, `SB_ADEN` también.

Las direcciones internas ya resueltas usan `memread_fisico`/`memwrite_fisico`, como el resto de
las DMA del árbol. La regla está en CLAUDE.md y ya costó un fallo doble en `basic/mmu/pvrmap`.

**Cómo se prueba**: `libdream-spu` y `sound-multi-stream` dejan de trabarse en el `g2_dma_transfer`
—no van a sonar todavía, pero avanzan—, y una suite `g2dma` en `tests/` con la secuencia de
`g2dma.c` de KOS. `--traza-mem` reporta cada transferencia con origen, destino y largo.

---

## Fase 3 — El ARM7DI

Núcleo nuevo, aislado, con su suite propia. Lo que hay que emular, y lo que **no**:

| | |
| --- | --- |
| arquitectura | ARMv3 |
| modos | usuario, FIQ, IRQ, supervisor, abort, undefined |
| Thumb | **no existe** (llegó con el ARM7TDMI) |
| `LDRH`/`STRH`/`LDRSB` | **no existen** (son de ARMv4) |
| `BX` | **no existe** — por eso KOS compila con `--fix-v4bx` |
| MMU, caché, coprocesadores | no hay |
| multiplicación | `MUL`, `MLA` (el ARM7DI los tiene; los largos no) |
| vectores | siete, en `0x00000000`, en el orden reset/undef/swi/pabt/dabt/reservado/irq/fiq |

El espacio de direcciones que ve el ARM es la tabla 4-8: `0x00000000`-`0x001FFFFF` la RAM de
onda, `0x00800000` en adelante sus propios registros. Nada más. Un acceso fuera de eso es un
síntoma, no algo que haya que atender.

**El despacho sigue el patrón del árbol**, que es lo que hace que esto no sea trabajo nuevo sino
trabajo conocido: una tabla maestra de `{patrón, máscara, mnemónico, manejador}` como
`opcodes[]`, expandida a un arreglo de punteros indexado por los bits que deciden —en ARM son
los 27-20 más los 7-4, o sea 4096 entradas— y un archivo por categoría. Igual que `opcodes.c`,
con la diferencia de que en ARM casi toda instrucción es condicional, así que la condición se
evalúa antes del despacho y no dentro de cada manejador.

**Las FIQ son lo que usa el firmware**, no las IRQ. `crt0.s` deja el vector de IRQ en un
`sub pc,r14,#4` y todo el trabajo está en la FIQ: lee `L` (`0x00802D00`), mira los tres bits
bajos, atiende el tipo 2 (temporizador) o el 5 (petición de bus) y escribe cuatro veces `M`
(`0x00802D04`) para reconocer. El nivel que decide ese número de 3 bits sale de `SCILV0/1/2`,
que `aica_init()` programa con `0x18`, `0x50` y `0x08`.

**El presupuesto de ciclos** va en `main_loop()`, junto a `timer_check()` y `wdt_tick()`: 5880
ciclos de ARM cada 51953 de CPU, con su resto propio. Cuando `ARMRST` está en 1 el núcleo no
ejecuta nada y la cuenta ni se lleva.

### Cómo se prueba la fase 3

Tres niveles, de más barato a más caro:

1. **La suite `arm7`** en `tests/`: un caso por fila de la tabla, como el resto del árbol. La
   suite de cobertura del ARM falla si una fila implementada nunca se ejecutó, igual que
   `test_cobertura.c` hace con `opcodes[]`.
2. **El bucle infinito de `spu_init()`**: `0xeafffff8` en la dirección 0. Si el ARM lo ejecuta
   sin salirse y sin costar tiempo medible, la fase no rompió las 135 demos. Es la prueba de
   humo más importante de todo el plan, porque es la que corren todos los programas.
3. **El firmware de KOS**: `stream.drv`, 3344 bytes. Si arranca, la cola pasa a `valid` y la
   aserción de `snd_iface.c:84` —`Queue is not yet valid`— deja de saltar en
   `sound-multi-stream`. **Ese es el primer veredicto medible de esta vía**, y llega antes de
   que exista una sola muestra de audio.

---

## Fase 4 — Los canales y la mezcla

Recién acá suena algo. 64 ranuras de `0x80` bytes, y por cada una el camino
`PG → lector de muestras → AEG → LFO → filtro → mezclador`. Todo está en §8.1.1.

### 4.1 El generador de tono (PG), §8.1.1.4

`OCT[3:0]` es la octava en complemento a dos —de -8 a +7— y `FNS[9:0]` el número F. La
frecuencia efectiva es

```
f = 44100 × 2^OCT × (1 + FNS/1024)
```

que es la misma cuenta que hace `aica_play()` de KOS al revés (`freq_base = 5644800`, que son
`44100 × 128`, o sea `2^7`), y equivale al `P[CENT] = 1200 × log2((2^10 + FNS)/2^10)` del papel.
El incremento de fase es un punto fijo de 10 bits fraccionarios, que es justo lo que dice la
precisión del `FNS`.

### 4.2 El lector de muestras y el bucle, §8.1.1.1

`SA[22:0]` es una dirección de **byte** en la RAM de onda; `LSA` y `LEA` son números de
**muestra** contados desde `SA` —bytes en PCM de 8, pares de bytes en PCM de 16, medios bytes en
ADPCM— y `LPCTL` dice si al llegar a `LEA` se termina o se vuelve a `LSA`. La secuencia del papel
es explícita y conviene copiarla como caso de prueba tal cual:

```
Loop OFF:  D[0] → D[1] → … → D[A]
Loop ON:   D[0] → D[1] → … → D[A] → D[5] → D[6] → … → D[A] → D[5] → …
```

con `LSA = 3`, `LEA = 0xA` y, ojo, la vuelta cayendo en `D[5]` y no en `D[3]`: es un dato del
papel que una implementación "obvia" no produce, y por eso es buen caso de prueba.

`SSCTL` en 1 sustituye la memoria por ruido; es de un renglón y hay demos que lo usan para
percusión.

### 4.3 El ADPCM de Yamaha, §8.1.1.2

Es la única parte del chip que no se puede aproximar: o da bit a bit lo mismo o suena a ruido.
El papel la da entera:

```
X(n)   = (1 - 2·L4) × (L3 + L2/2 + L1/4 + 1/8) × Δ(n) + X(n-1)
Δ(n+1) = f(L3, L2, L1) × Δ(n)
```

con `Δ` inicial 127, mínimo 127 y máximo 24576, y los ocho factores de la tabla 8-4. Y los ocho
son exactos en 256avos —230, 230, 230, 230, 307, 409, 512, 614—, así que **se hace con enteros
sin error de redondeo**, que es lo que hay que hacer para que dos corridas den el mismo `.wav`.

`PCMS = 3` es el modo de flujo largo, con `LSA` y `LEA` alineados a 4. Se puede dejar para
después: KOS no lo usa.

### 4.4 La envolvente (AEG), §8.1.1.3

Cuatro estados —ataque, decaimiento 1, decaimiento 2, liberación— con su tasa (`AR`, `D1R`,
`D2R`, `RR`), el nivel de cambio `DL` y el escalado por tecla `KRS`. La tasa efectiva es

```
tasa_efectiva = (KRS[3:0] + OCT[3:0]) × 2 + FNS[9] + tasa[registro] × 2
```

y la tabla 8-5 la convierte en milisegundos, con dos columnas distintas: una para el ataque
(-96 dB a 0 dB) y otra para el resto (0 dB a -96 dB). **Las dos tablas se copian tal cual**, con
sus 64 entradas cada una; deducirlas de una fórmula es exactamente el tipo de trabajo que este
documento existe para evitar.

`LPSLNK` engancha el paso a decaimiento 1 con que el lector cruce `LSA`.

### 4.5 El LFO, §8.1.1.5

`LFOF[4:0]` elige una de 32 frecuencias, de 0,17 a 172,3 Hz —tabla, no fórmula—, con dos
moduladores independientes: `PLFOWS`/`PLFOS` sobre el tono y `ALFOWS`/`ALFOS` sobre el volumen,
cada uno con cuatro formas de onda (sierra, cuadrada, triangular, ruido) y ocho profundidades.
Las tres tablas están en 8-8 y 8-9. Se puede dejar para el final de la fase: casi ninguna demo
lo usa, y con `LFOF` en cero no hace nada.

### 4.6 El mezclador, §8.1.1.6

Es donde están las tres tablas de volumen, y son distintas entre sí:

- **`TL[7:0]`** es atenuación con peso por bit: el bit 0 vale -0,4 dB y cada bit siguiente el
  doble, hasta -48 dB el bit 7 (tabla 8-10).
- **`DISDL[3:0]`, `EFSDL[3:0]`, `IMXL[3:0]`, `MVOL[3:0]`** van de -MAX dB (valor 0) a 0 dB
  (valor 0xF) en pasos de 3 dB (tabla 8-11).
- **`DIPAN[4:0]`, `EFPAN[4:0]`** son 32 posiciones: 0x00-0x0F atenúan la izquierda, 0x10-0x1F la
  derecha, 0 dB en el centro (tabla 8-12).

La salida es estéreo a 44100 Hz. `MONO` (bit 15 de `0x2800`) desactiva el paneo, y el papel
advierte que entonces hay que bajar `MVOL` porque el volumen se duplica.

### 4.7 El filtro (FEG), §8.1.1.7

Un pasabajos IIR por canal con `Q[4:0]` fijo (`Q[dB] = 0,75 × valor - 3`) y cinco niveles de
corte (`FLV0`-`FLV4`) recorridos por su propia envolvente (`FAR`, `FD1R`, `FD2R`, `FRR`) con la
tabla 8-14.

**Es lo primero que se puede omitir sin que nadie lo note**, y el papel dice cómo dejarlo en
"pasa todo": `Q = 4` y `FLV = 0x1FF8`. El firmware de KOS directamente lo apaga
(`CHNREG8(ch, 40) = 0x24`). Así que la fase 4 puede entregar sin filtro y añadirlo después.

### Cómo se prueba la fase 4

- **La suite `aica`** cubre lo que es cuenta pura: la secuencia de bucle de la tabla 8-1 sample a
  sample, el ADPCM contra un vector conocido, las dos tablas de la AEG, las tres de volumen y la
  conversión `OCT`/`FNS` → frecuencia contra la tabla 8-7 (do central, 44100 Hz, doce notas).
- **`--captura-audio=ARCHIVO.wav`**, el gemelo de `--captura-gl`: se emparejan con
  `--salir-tras` y una demo se mide sin ventana. Ver "Cómo se prueba" más abajo.
- `sound-sfx` y `sound-sfxbuf` son las primeras que deberían dar un `.wav` con contenido: son
  PCM sin flujo.

---

## Fase 5 — CDDA

No hace falta otro chip. §8.1.1.6 dice que las ranuras 0x10 y 0x11 del mezclador de efectos son
`EXTS[0]` izquierda y derecha, o sea la entrada digital externa, que en la Dreamcast **viene de
la lectora**. Sus `EFSDL`/`EFPAN` viven en `0x00702040` y `0x00702044`, que es exactamente lo que
escriben `spu_cdda_volume()` y `spu_cdda_pan()` de KOS. La correspondencia cierra.

Entonces:

1. La lectora ya sabe qué pistas son de audio: `iso_init()` las lista con su modo.
2. Falta el comando SPI de reproducción (`CD_PLAY`, `CD_SEEK`, `CD_SCAN`) y el estado que informa
   por dónde va, en `gdrom.c`.
3. Y la ruta: leer los sectores de audio de la pista y meterlos por `EXTS[0]` al mezclador, con
   su `EFSDL`/`EFPAN`.

Los sectores de audio son PCM de 16 bits con signo, estéreo, a 44100 Hz, 2352 bytes por sector,
que es la frecuencia de salida del AICA: no hay remuestreo.

`sound-cdda-basic_cdda` es la demo, y **necesita una imagen con pista de audio**, que las de
`audio/datos` de la tabla de `bios-boot-plan.md` ya tienen.

---

## Fase 6 — El DSP de audio (opcional)

§8.1.1.8 lo trae completo: 128 pasos de microprograma de 55 bits útiles (`MPRO[63:0]`), un búfer
circular en la RAM de onda (`RBP`/`RBL`), 128 coeficientes de 13 bits (`COEF`), 64 direcciones
(`MADRS`), 128 palabras de trabajo (`TEMP`), 32 de entrada desde memoria (`MEMS`), 16 de mezcla
(`MIXS`) y 16 de salida (`EFREG`). Los campos del microprograma están enumerados uno por uno.

**Se puede escribir desde el papel, y aun así va último**, por una razón concreta: el
controlador de KOS no lo programa nunca, y mientras todos los `EFSDL` estén en 0 el camino de
efectos no aporta nada a la salida. Lo que se pierde por no emularlo es la reverberación de un
juego comercial —audible, no funcional—, y el hito E no depende de eso.

---

## Cómo se prueba

**El riesgo de esta vía es que el sonido es lo único del árbol que no se puede verificar leyendo
un volcado.** La contramedida se construye *antes* de escribir el mezclador, no después, y es la
misma que resolvió lo mismo del lado gráfico.

### `--captura-audio=ARCHIVO.wav`

El gemelo exacto de `--captura-gl`: vuelca lo que el mezclador produjo, no lo que la tarjeta de
sonido reprodujo. Un WAV de 44100 Hz, 16 bits, estéreo, escrito incrementalmente y cerrado en
`traza_resumen()` —o sea que **`--salir-tras` importa igual que para el desensamblado**: matar el
proceso desde afuera se lleva el encabezado del WAV.

Con eso, una demo se mide sin ventana y sin escuchar nada:

| medida | qué separa |
| --- | --- |
| muestras distintas de cero | "no sonó" de "sonó" |
| valores distintos | silencio con ruido de un tono de verdad |
| RMS y pico | volumen equivocado de silencio |
| bit a bit entre dos corridas | determinismo — el ADPCM y la AEG tienen que darlo |

Es la misma lista con la que se miden los BMP, y la lección de `--captura-gl` aplica igual: **la
medida hay que hacerla sobre lo que produjo el emulador, no sobre lo que el sistema anfitrión
hizo con ello.** Capturar la salida de la tarjeta de sonido depende del mezclador del sistema y
falla en silencio, exactamente como capturar la ventana.

### Lo que reporta `--traza-mem`

Siguiendo lo que ya hacen el TA, la lectora y el Maple:

- El ARM saliendo de reset y entrando, con el tiempo emulado.
- Cada `KEY_ON`: canal, formato, dirección de la muestra, frecuencia, bucle, volumen y paneo.
- Cada transferencia del G2-DMA: origen, destino y largo.
- La cola de comandos: `head`, `tail`, `valid`, `process_ok`.
- Al salir: muestras producidas, canales que sonaron alguna vez, y cuántas veces el búfer de
  salida se quedó vacío —que es el equivalente auditivo de una tira que no se dibujó.

### Las suites

`tests/` gana tres: `aica` (registros, bucle, ADPCM, envolventes, tablas de volumen), `arm7`
(una por categoría de instrucción, más cobertura) y `g2dma`. Ninguna necesita SDL, por la
decisión 0.3.

### La regresión que no es opcional

**El barrido de las 135 demos de KOS**, comparado contra
[demos-kos.md](demos-kos.md). No porque el sonido pueda romper el vídeo, sino por lo que dice el
punto de partida: a partir de la fase 3 las 135 corren con un ARM ejecutando. Cualquier fase que
mueva un número de ese documento hacia abajo se revierte antes de seguir.

---

## El riesgo real

Cinco cosas, ordenadas por lo que costaría cada una:

1. **El ARM corre en todos los programas, no en los siete.** Si el núcleo se cuelga, consume
   tiempo o se sale de su espacio de direcciones, se caen 100 demos que hoy funcionan. Mitigación:
   `--sin-aica` para apagarlo entero, y que el presupuesto de ciclos por vuelta de `main_loop()`
   esté acotado por arriba pase lo que pase.
2. **El ritmo del temporizador A es el reloj del firmware.** Si no dispara a la tasa correcta, el
   controlador de flujos de KOS se queda sin datos o los pisa, y el síntoma es audio entrecortado
   que se parece mucho a un mezclador mal escrito. Por eso el temporizador se prueba contra
   `reloj_total` en la fase 1, antes de que exista el ARM.
3. **La callback de audio de SDL 1.2 corre en otro hilo.** No puede tocar estado del emulador. El
   mezclador escribe a un búfer circular y la callback solo lo consume; lo que haya que proteger
   se protege con `SDL_LockAudio`, no con nada inventado.
4. **`VER[3:0]` y `MEM8MB` son dos valores que hay que contestar y que nadie mide.** Ya pasó dos
   veces —`REVISION` del PVR y `SB_G1SYSM`— y las dos veces el síntoma fue un cuelgue lejos del
   sitio. Se contestan a conciencia y se anota qué se contestó.
5. **Verificar de oído no es verificar.** Si en algún momento la respuesta a "¿funciona?" es
   "suena bien", el `.wav` no se está mirando.

Y una que **no** es un riesgo, aunque lo parezca: la precisión del sintetizador. El AICA tiene
64 canales a 44100 Hz; el mezclador es aritmética de enteros sobre 2 MB de RAM y no compite con
nada. El costo está en el ARM, y el ARM del firmware de KOS pasa casi todo el tiempo dentro de
un `b .`.

---

## Estimación

| fase | qué entrega | tamaño |
| --- | --- | --- |
| 0 | decisiones, archivos, `--captura-audio` vacío | chica |
| 1 | archivo de registros, temporizadores, interrupciones del AICA | mediana |
| 2 | G2-DMA — **independiente, se puede hacer primero** | chica |
| 3 | ARM7DI | **la grande** |
| 4 | canales, PCM, ADPCM, envolvente, mezcla | grande |
| 5 | CDDA | mediana, y toca `gdrom.c` |
| 6 | DSP | mediana, prescindible |

La fase 3 es la mitad del trabajo. Las fases 1 y 2 juntas son menos que la 4.

---

## Recomendación

**El orden es 2 → 1 → 3 → 4 → 5 → 6, y no el numérico.**

La fase 2 va primera porque es chica, es independiente de todo lo demás, se verifica con demos
que ya existen y quita un bloqueo que hoy tapa a los otros. Es la misma forma que tenía el CH2
DMA: tres registros que se perdían en un respaldo.

Después la 1, que es la que hace medible a la 3.

Y hay un punto de corte que conviene reconocer si el tiempo se acaba: **al terminar la fase 3,
`sound-multi-stream` deja de fallar su aserción y `libdream-spu` corre su firmware de verdad.**
Eso no es sonido, pero es una demo menos en la lista de las que fallan y es un resultado
completo por sí mismo. La fase 4 es la que convierte eso en el hito E.

---

## Lo que quedó

Escrito el 1 de agosto de 2026, después de implementar las fases 0 a 4. **El hito E está
alcanzado: cuatro demos de KOS suenan**, y el sonido se mide en un archivo, no de oído.

### Archivos nuevos

| archivo | qué es | SDL |
| --- | --- | --- |
| `g2dma.c/h` | los cuatro canales de DMA del bus G2 (`SB_AD*`) | no |
| `aica.c/h` | registros, temporizadores, interrupciones, DMA interno, 64 canales y mezclador | no |
| `arm7.c/h` | el ARM7DI: 22 filas en la tabla, expandidas a 4096 | no |
| `audio.c/h` | la salida: `SDL_OpenAudio` y el volcado a `.wav` | sí |

Los tres primeros son libres de SDL a propósito, como `sistema.c`, `ta.c` y `vram.c`, así que
`tests/` los enlaza de verdad. Tres suites nuevas: `g2dma` (9 casos), `aica` (29) y `arm7` (20,
con su propia cobertura de filas). **602 casos en verde**, contra 544 al empezar.

### Fase 2 — el G2-DMA

Los ocho registros por canal caían en `ES_CONTROL()` y de ahí al respaldo del bloque de
control, así que `SB_ADST` se leía 1 —"DMA en curso"— para siempre. Ahora hay archivo de
registros propio, validación de los rangos de §8.4.1.4 y el evento de fin en el bit que
corresponde: 15 a 18 de `SB_ISTNRM`, uno por canal.

Se ve funcionando en `--traza-mem`: `sound-multi-stream` mueve 16352 bytes por transferencia a
`0x00830000`, `0x00840000`, `0x00850000`… que es `spu_dma_transfer()` rellenando el búfer del
flujo.

### Fase 1 — el archivo de registros

Los 32 KB de `aica_mem` eran respaldo mudo. Ahora los lleva `aica.c`, con las dos ventanas
asimétricas del papel (`ARMRST` solo desde el G2; `L` y `M` solo desde el ARM), acceso por byte
desde el ARM y de 16 bits válidos desde el SH-4, los tres temporizadores contra `reloj_total`,
el controlador de interrupciones completo y el DMA interno.

**El caso de prueba que más dice es el del nivel de interrupción.** Con los `SCILV0/1/2` que
escribe `aica_init()` del firmware de KOS —`0x18`, `0x50`, `0x08`—, la fuente 6 (temporizador A)
tiene que dar **2** y la 3 (entrada MIDI) **5**, que son exactamente los dos números contra los
que compara el `crt0.s`. Eso fija la lectura de los tres registros sin ambigüedad.

Y el primer hallazgo nuevo: **el boot ROM conduce `ARMRST` tres veces** antes de llegar al menú
—a los 82 ms, a los 759/851 y a los 4387/4478—, o sea que carga y arranca su propio firmware.

### Fase 3 — el ARM7DI

22 filas, expandidas a una tabla de 4096 por los bits 27-20 y 7-4, como `initopcodes()` hace con
`opcodes[]`; `arm7_init()` avisa si dos filas se pisan. Sin Thumb, sin `LDRH`/`STRH`, sin `BX`,
sin coprocesadores: esos patrones entran por el vector de instrucción indefinida, que es lo que
hace un ARM7DI de verdad.

Tres cosas costaron un caso de prueba cada una y las tres están en la suite:

- **R15 leído da PC+8**, y PC+12 cuando el desplazamiento viene de un registro o cuando se
  guarda en memoria.
- **`SUBS PC, R14, #4` no es una resta**: con `S` puesto y R15 como destino restaura el CPSR
  desde el SPSR. Así termina la FIQ del `crt0.s`, y sin eso el firmware entra a su primera
  interrupción y no sale nunca del modo FIQ —perdiendo R8-R14 del programa principal en el acto.
- **El bus son 24 bits.** `spu_init()` de KOS escribe `0xEAFFFFF8` en la dirección 0, que es un
  salto a PC−24, o sea a `0xFFFFFFE8`. Sin recortar la dirección eso no cae en ninguna de las
  dos regiones del mapa; con el recorte el núcleo recorre ceros —que decodifican como un `AND`
  sin efecto— y da la vuelta, que es el bucle infinito que el comentario de KOS promete.

El veredicto medible llegó donde el plan lo esperaba: **la aserción de `snd_iface.c:84`
—`Queue is not yet valid`— dejó de saltar**, porque el firmware corre y marca la cola.

### Fase 4 — los canales y la mezcla

64 canales con PCM16, PCM8 y ADPCM de Yamaha, envolvente de amplitud, bucle, TL, DISDL, DIPAN,
MVOL y MONO, mezclados a 44100 Hz estéreo. El ADPCM se hace con enteros —los ocho factores de la
tabla 8-4 son exactos en 256avos— y el resultado es **determinista**: dos corridas dan un `.wav`
idéntico bit a bit.

**Dos defectos que solo se vieron por medir, y ninguno daba error:**

1. **Una tasa 0 de la envolvente es "∞", no "instantáneo".** Las tasas 0 y 1 de la tabla 8-5
   valen infinito: la envolvente *no se mueve*. Tomarlas por instantáneas apagaba el canal en la
   primera muestra del decaimiento. El síntoma era un `.wav` de ocho segundos con **pico 14 sobre
   32767** — el equivalente auditivo de un BMP negro. Con la tabla bien, el mismo `.wav` da pico
   16533.
2. **La envolvente avanza antes de emitir, no después.** Al revés, la primera muestra de cada
   canal sale con la atenuación del reposo, o sea muda. Con un ataque instantáneo —que es lo que
   pide el firmware de KOS— el chip ya está a volumen pleno en la primera muestra.

Y una tercera, más chica: la muestra de `LEA` es válida. El canal termina **después** de
entregarla, no en vez de entregarla.

Una errata del papel, corregida y anotada: la entrada 31 de la columna de decaimiento de la
tabla 8-5 figura como `90.` entre `920.` y `690.`. La progresión es geométrica y el término que
corresponde es 790.

### La medida, que es lo que faltaba

`--captura-audio=ARCHIVO.wav` vuelca lo que **el mezclador produjo**, no lo que la tarjeta hizo
con ello, y se cierra por `traza_resumen()` —el mismo camino que el desensamblado— así que
`--salir-tras` importa igual. `--sin-audio` no abre la tarjeta y `--sin-aica` apaga el chip
entero, que es la mitigación del riesgo 1: el ARM corre en las 135 demos, no en las siete.

Escuchar y medir conviven: con la tarjeta abierta el volcado sale de un segundo anillo que la
callback deja lleno, así que ninguno le roba muestras al otro.

### Qué se verificó

**Las cuatro demos que suenan**, medidas en ocho segundos de tiempo emulado:

| demo | muestras no nulas | valores distintos | RMS | pico (L / R) |
| --- | --- | --- | --- | --- |
| `sound-multi-stream` | 692577 / 707024 | 13882 | 1467,6 | 16533 / 14541 |
| `sound-sfx` | 125886 / 707024 | 1784 | 4604,0 | 23065 / 23065 |
| `sound-sfxbuf` | 125886 / 707024 | 1784 | 4604,0 | 23065 / 23065 |
| `sound-hello-adx` | 110147 / 707024 | 10738 | 768,9 | 15732 / 15732 |

`sound-multi-stream` es el mejor de los cuatro como prueba: son dos flujos de ADPCM estéreo
simultáneos, paneados a lados opuestos, alimentados por el G2-DMA. Toca las cuatro fases.
`sound-sfx` y `sound-sfxbuf` necesitan que se pulse un botón (`DCEMU_PULSAR_A=1
DCEMU_SOLO_A=1`); sin eso su `.wav` sale mudo y no es un fallo.

**Dos corridas de `sound-multi-stream` dan el mismo `.wav` bit a bit** (SHA-256 `4251337E…`),
que es lo que hay que exigirle a un ADPCM con estado.

**El arranque por BIOS suena, y con el firmware del propio boot ROM.** Es la comprobación más
independiente que hay de esta vía: ese ARM no es el de KOS, no pasa por `snd_iface.c` ni por la
cola de comandos, y aun así toca. Medido en dieciséis segundos de tiempo emulado:

| | |
| --- | --- |
| muestras no nulas | 386911 / 1411326 |
| valores distintos | 26518 |
| RMS | 2523,0 |
| pico | 32768, en **8 muestras de 1,4 millones** (0,001 %) |
| empieza a | 6,46 s |

Son 70 disparos repartidos por los 48 canales, todos PCM16 de **la misma muestra** —`SA 01852a`,
en bucle de `LSA 0001` a `LEA 00ab`— a diez alturas distintas. Contra la tabla 8-7 del
documento:

| OCT / FNS | nota | OCT / FNS | nota |
| --- | --- | --- | --- |
| −2 / 0x000 | C2 | +0 / 0x10a | E4 |
| −1 / 0x1a8 | F♯3 | +0 / 0x156 | F4 |
| −1 / 0x1fe | G3 | +0 / 0x259 | G♯4 |
| −1 / 0x38d | B3 | +0 / 0x320 | A♯4 |
| +0 / 0x07d | D4 | +1 / 0x0c1 | D♯5 |

**Las diez caen sobre la tabla 8-7 con un LSB de diferencia como mucho**, y eso es lo que hace a
esta medida valiosa: si el incremento de fase estuviera mal, las notas no aterrizarían sobre la
escala temperada — desafinarían de forma creciente hacia los extremos, y no lo hacen. Es una
comprobación de `OCT`/`FNS` que ninguna demo de KOS da, porque ninguna toca acordes.

Y ocho muestras en el riel sobre 1,4 millones dicen que el mezclador **no** está saturando: es
un pico legítimo con 48 canales sumando, no la mezcla pasada de nivel.

**Regresión.** Veinte demos representativas en su línea base, con el ARM corriendo en todas:
`conio-basic` 2742 píxeles no negros, `conio-kosh` 1536, `pvr-strided_texture` 240000,
`parallax-serpent_dma` 480000, `pvr-modifier_volume` 109400, `pvr-cheap_shadow` 118149,
`pvr-bumpmap` 102400, `pvr-texture_render` 479995, `pvr-palette-wormhole` 370195, `tunnel`
480000, `libdream-ta` 479997, `2ndmix` 67481, `video-bfont` 446244, `basic-mmu-pvrmap` 479835,
`roto` 114320, `rumble` 3940. Y el arranque por BIOS, con sus 480000 píxeles y 16258 colores,
idéntico a antes.

### Lo que sigue faltando

**La fase 5 (CDDA) y la fase 6 (el DSP).** Con eso quedan tres de las siete demos de sonido sin
sonar, y cada una por una razón distinta:

- **`sound-cdda-basic_cdda`** necesita la fase 5. Y hay un dato que cambia su forma respecto de
  lo que este plan suponía: **por el camino de KOS el CDDA no llega como paquete SPI sino como
  syscall.** `cdrom_cdda_play()` llama a `cdrom_exec_cmd(CD_CMD_PLAY_TRACKS)`, que es el comando
  **20** del vector de GD-ROM (`dc/syscalls.h`), y en dcemu lo atiende `hack_gdrom()` en
  `dcopcodes.c` —donde hoy solo están el 16 y el 17, las dos lecturas de sector—. Con `--bios` sí
  bajaría a SPI (`SPI_CD_PLAY`, `0x20`, ya declarado en `gdrom.h` y sin implementar). Lo correcto
  es que el estado del CDDA viva en `gdrom.c` y que los dos caminos entren ahí.

  Falta además leer sectores de audio **crudos**: `iso_read_sector()` entrega los 2048 bytes de
  usuario, y una pista de audio son 2352 sin cabecera. `cdi.c` ya guarda por pista el `offset` en
  el archivo, el `sector_crudo` y el `desplazamiento`, así que la pieza que falta es un lector
  que los use. Las imágenes con pista de audio para probarlo están: `Crazy Taxi - DCRES.cdi` y
  `DCDoom CDI.cdi` tienen la pista 1 en audio.

  La entrada al mezclador ya está identificada: `EXTS[0]` L y R son las ranuras 0x10 y 0x11, con
  sus `EFSDL`/`EFPAN` en `0x00702040` y `0x00702044` —exactamente lo que escriben
  `spu_cdda_volume()` y `spu_cdda_pan()`—. Ojo con una cosa: `spu_cdda_pan(0, 31)` deja la fuente
  izquierda con `EFPAN` 0x0F, que según la tabla 8-12 la manda a la **derecha**. La demo de KOS
  lo dice en su propio comentario (*"the channel regs aren't set quite correctly yet"*), así que
  conviene seguir el papel y anotar la diferencia, no seguir a KOS.
- **`sound-hello-opus`** no suena y no es del AICA: la demo no llega a producir audio en ocho
  segundos de tiempo emulado. Sin diagnosticar.
- **`libdream-spu`** carga su propio reproductor de S3M en el ARM y espera en
  `while(*snd_dbg != 3)` sobre `0xa080ffc0`, que es una dirección con un caso especial viejo en
  `mem.c`. Reporta `Load OK, starting ARM` y ahí se queda. Es un ARM distinto del de KOS y
  merece mirarse aparte.

Del resto del chip, lo que no está: **el LFO** (tablas 8-8 y 8-9), **el filtro FEG** —el papel
dice cómo dejarlo en "pasa todo", `Q = 4` y `FLV = 0x1FF8`, y el firmware de KOS directamente lo
apaga—, **el modo de ADPCM de flujo largo** tratado como el normal, y **la interrupción de
intervalo de muestra** (bit 10 de `SCIPD`), que nadie habilita.

Y dos valores contestados sin medida, anotados como tales porque ya pasó dos veces que un
registro de identificación contestado a la ligera dejara al guest colgado lejos de ahí:
**`VER[3:0]` de `0x2800`**, al que se le da 1, y el bit `MEM8MB`, que se acepta y se ignora
porque dcemu solo ofrece los 2 MB de una consola de serie.
