# El JIT hacia el estado del arte — la segunda vuelta

Estado: **plan**. Escrito el 2026-08-18 sobre la rama `rendimiento-hilos`. Es la
continuación de `estado-del-arte-plan.md` (cuyas fases 0-6 están hechas; las 7 y 8
se heredan acá con techos frescos) y se apoya en `recompilador-plan.md` para el
estado, las reglas y los veredictos del traductor. La meta no cambia: **las
tecnologías del estado del arte adaptadas a la disciplina de este árbol, sin
carrera de cifras contra flycast** — la vara son las tandas propias, y cada fase
entra sola con su palanca y su veredicto.

## Dónde está el árbol hoy

Marcas vigentes (tanda 2026-08-15, binario `037F3330A41E0FA0`): DCDoom
**26 561 ms, −39,1 %, 1,32× tiempo real**; Crazy Taxi **103 426 ms, −16,5 %,
1,74×**; Sega Rally 2 **53 533 ms, −27,8 %, 1,12×**. **Los tres guests del banco
corren sobre tiempo real.** Cobertura 93,8 / 96,2 / 95,2 %.

Ya al nivel del estado del arte, medido y encendido (el detalle en
`recompilador-plan.md`):

- traducción por identidad de manejador, 122 plantillas elegidas por censo;
- enlace de bloques con épocas, índice por PC destino (el barrido cuadrático fue
  la mayor pérdida de la serie), puente entre páginas bajo MMU;
- pares de rama completos (BRA/JMP/BRAF/RTS/BSR/JSR/BSRF con su ranura);
- camino de memoria emitido: caché de TLB en línea, atajo P1/P2, guardas muertas
  plegadas, rejilla de 64 bytes para la escritura sobre código;
- hogares canónicos con costuras; clave FPU con SR.MD y FD;
- el ARM7 en tres escalones (predecodificación, bloques, traductor x64);
- el reloj por eventos sobre la grilla intacta;
- PGO con banco fijo y ponderado, un perfil por binario.

El reparto vigente (2026-08-13, binario `D06C1A67C6F9E863`):

| | DCDoom 35 s | CT 180 s | SR2 60 s |
| --- | --- | --- | --- |
| SH-4 (emitido + despacho + intérprete + MMU) | **78,0 %** | **63,1 %** | **81,0 %** |
| AICA — ARM7 | 10,1 % | **19,9 %** | 8,4 % |
| AICA — mezclador | 4,1 % | 7,2 % | 3,1 % |
| bloque periódico neto | 7,1 % | 1,7 % | 4,5 % |
| GL entero | 0,7 % | 6,2 % | 2,0 % |

## Qué significa «estado del arte» en este plan

Cuatro criterios, en orden de peso:

1. **El SH-4 emitido sin residuo grande de intérprete**: el reparto dice que el
   63-81 % sigue ahí adentro, y el resumen `jit:` nombra al residuo. Hoy el
   nombre propio es **la FPU** (abajo).
2. **Las tecnologías de la lista cubiertas o descartadas con medición.** Quedan
   dos: la elisión de lazos ociosos (fase 7 del maestro) y nada más — fastmem ya
   fue reescrito por su censo (techo real 1,78 %) y los hilos perdieron su A/B.
3. **La adopción por omisión**: un solo binario con el traductor encendido, el
   parque entero como compuerta (fase 8 del maestro). Mientras el JIT sea build
   aparte, el rendimiento del árbol *que se entrega* es el del intérprete.
4. **El criterio de parada del maestro**: cuando el techo de la fase siguiente
   quede bajo ~2-3 % del tiempo de corrida, el plan se da por cumplido.

## El material nuevo: las CHD (y dónde quedó cada juego — releído el 2026-08-18)

El estado real tras la limpieza de disco: **ocho CHD en
`E:\Juegos\roms\dreamcast`** — 18 Wheeler, Capcom vs. SNK, **Capcom vs. SNK 2**,
Crazy Taxi, Crazy Taxi 2, **Tony Hawk's Pro Skater 2**, Virtua Tennis y Virtua
Tennis 2 —, y en `roms/` del árbol quedaron DCDoom más **los dos del banco que
se restauraron de la Papelera** porque la serie de medición depende de ellos:
el `.cdi` de Crazy Taxi (el CHD es otro rip — pistas de audio reales donde el
`.cdi` trae relleno — y cambiarlo rompería el banco otra vez) y **Sega Rally 2,
que no tiene CHD** y es el guest MMU+FPU de la fase B.

**El parque CHD creció a 32 imágenes (2026-08-18, segunda carga)**: volvieron
como CHD los juegos que habían quedado solo en la Papelera (ChuChu, 4x4 EVO,
DOA2, Mat Hoffman, Quake III, SF3 Double Impact), apareció **Sega Rally 2
(USA).chd** (antes sin CHD — es otro rip que el `.gdi` v1.003 del banco, así
que sirve de barandilla de doble contenedor, no de reemplazo del banco), se
sumó Capcom vs. SNK 1 (nunca validado), y entraron **~15 juegos que dcemu
jamás corrió**: Soulcalibur, Sonic Adventure 1 y 2, Sonic Shuffle, Jet Set
Radio, MSR, Daytona USA, Power Stone 1 y 2, SF Alpha 3, SF3 3rd Strike, Space
Channel 5, Re-Volt, Rush 2049, MK Gold, Ooga Booga y Tech Romancer. El barrido
de validación es `herramientas/chd-barrido.ps1`: 40 s emulados por los dos
brazos con captura + `DCEMU_CP_MS` + RTC clavado y vigía anti-cuelgue por
corrida. **Resuelto (2026-08-19)**: todo lo que quedaba en la Papelera con
origen en `roms\` — Dave Mirra, Tennis 2K2, Quake III, DOA2, CvS, SF3 3rd
Strike TOSEC, los `.cdi` de Virtua Tennis (incluida la carpeta del rip bueno
de tres pistas) y demás, 18 elementos, ~12 GB — se movió a
`E:\Juegos\roms\dreamcast` con sus nombres originales
(`herramientas/papelera-a-e.ps1`). La Papelera se puede vaciar sin perder
parque, y el material de la fase F está completo.

**El veredicto del barrido (2026-08-18, binario `D018E6606A6B996F`): 24 de 25
exactos bajo el traductor** — captura byte a byte idéntica y todos los puntos
de control iguales, incluyendo los 15 juegos que dcemu jamás había corrido.
Las cadenas en material nuevo son notables: SF Alpha 3 a 205,5 instrucciones
por entrada, Sonic Adventure 199,3, Rush 2049 187,5, Soulcalibur 146,9, Power
Stone 136,4, JSR 127,4, DOA2 124,3. Dos expedientes nuevos, **ninguno del
traductor** (los dos brazos fallan simétrico):

- **Mortal Kombat Gold no arranca**: su CHD lista 32 pistas y la lectura del
  binario de arranque cae fuera de toda pista de datos (`chd: el sector 545928
  no cae en ninguna pista de datos`; tampoco encuentra `ip.bin` en el
  directorio raíz). El guest queda girando en `8c00f500`. Es un expediente del
  backend CHD/GD-ROM (`docs/notas-gdrom.md` es su casa), con la forma clásica
  del árbol: la lectura falla, dcemu contesta algo sin quererlo decir, y el
  guest espera para siempre.
- **Ooga Booga tira abajo al emulador en los dos brazos en el mismo punto**
  (25 340 puntos de control idénticos y después la caída): `dibujar_escena`,
  `graficos.c:4281`, llegando desde la escritura del guest a `TA_LIST_INIT` —
  por `movl18` en el intérprete y por `jit_escribir32_fis` en el traductor.
  Expediente de gráficos; la simetría absuelve al traductor.

Tres de los juegos CHD **solo existen como CHD** (18 Wheeler, THPS2, CvS2) y
ninguno de los tres había corrido jamás bajo `DCEMU_JIT=2`. Dos usos, ninguno
de cronómetro:

- **material de exactitud** (fase A): tres guests nuevos con caminos que los tres
  del banco no pisan — THPS2 es el único con datos repartidos en dos pistas de
  alta densidad, CvS2 llega a su pantalla de memory card;
- **doble contenedor como barandilla**: Crazy Taxi 2 y Virtua Tennis ya salieron
  byte a byte idénticos `.chd` contra `.gdi`; cualquier divergencia nueva entre
  contenedores es del backend, no del traductor.

El banco de PGO y de tiempos **no se toca**: sumar un guest al banco reentrena el
perfil y rompe la comparabilidad con toda la serie histórica. Si alguna vez entra
uno, entra como entró SR2 — decidido antes de los números, con su propia entrada
en `pgo.ps1`.

## Las fases

La regla de siempre encabeza: **exactitud primero** — totales al dígito, capturas
y `.wav` byte a byte contra el intérprete — antes de citar tiempo; todo cambio de
emisión reentrena (`ciclo-jit.ps1`) antes de su tanda; los absolutos se comparan
dentro de un binario; cada fase con su palanca; tras cada fase se rehace el
reparto (`perfil-jit.ps1`).

### Fase A — el reparto y los censos, rehechos; la red de exactitud, ampliada

Todo lo de abajo toma su techo de acá. Sobre el binario vigente (o el
reentrenado del día):

- `perfil-jit.ps1` — el reparto es del 2026-08-13, anterior al atajo P1/P2, al
  pliegue de guardas y a la rejilla;
- **el censo de la frontera por peso** en los tres guests, leído sobre el banco
  vigente (ojo: el banco de CT de la tanda 2026-08-15 toma otro camino de juego
  que el histórico — 20,63 G instrucciones y 13,8 por entrada contra 21,24 G y
  20,1 —, así que el censo se lee contra sí mismo, no contra los de agosto);
- `DCEMU_JIT_SONDA_ACCESOS=1` en corrida aparte (cambia la emisión);
- `DCEMU_SONDA_CUADROS=1` para la distribución, no solo la media;
- **el reparto módulo/arena**: cuánto del tiempo corre en el código emitido y
  cuánto en el C compilado. Es trivial de separar en un perfil de muestreo — el
  arena vive fuera del módulo (`VirtualAlloc`) — y es **el techo de la fase G**:
  el compilador solo puede mover la mitad compilada.

Y la parte barata que ensancha la red antes de emitir más: **las compuertas de
exactitud sobre el trío CHD** — 18 Wheeler, THPS2 y CvS2, `DCEMU_JIT=2` contra
intérprete, captura byte a byte + `DCEMU_CP_MS` + total al dígito, con las reglas
de VMU/render de CLAUDE.md. Tres guests nuevos validan caminos que DOOM/CT/SR2 no
pisan, y valen una corrida cada uno.

**Gate**: los censos publicados en este plan; el trío CHD exacto o su expediente
abierto.

#### Resultados (2026-08-18, binario `93AFB567430D540C`)

El binario es el árbol vigente recompilado en USE con el `.pgd` de la tanda
(99,9 % de las instrucciones optimizadas por el perfil: el único cambio de
`jit.c` desde entonces era un comentario, así que la emisión es la misma y no
hizo falta reentrenar).

**El trío CHD salió exacto entero** (`herramientas/chd-exactitud.ps1`: captura
byte a byte + `DCEMU_CP_MS` punto por punto + `--sin-vmu`, intérprete contra
`DCEMU_JIT=2` sobre la misma imagen):

- **18 Wheeler** (60 s, START en el sondeo 2700): captura idéntica, 60 000
  puntos idénticos. Y corre a **283,7 instrucciones por entrada** al
  despachador — las cadenas más largas vistas en guest alguno.
- **Capcom vs. SNK 2** (40 s): captura idéntica, 40 000 puntos idénticos;
  32,2 por entrada.
- **Tony Hawk's Pro Skater 2** (40 s): captura idéntica y 40 000 puntos
  idénticos **con `DCEMU_RTC_FIJO`**; es Windows CE (1,46 M de rechazos por
  verificación, la firma de DCDoom) y 23,6 por entrada.
- **Tony Hawk's Pro Skater 1** (40 s, sumado el 2026-08-19 sobre el binario
  `F3D04A260D8D9D13` cuando su CHD apareció en `E:`): captura idéntica
  (`C089D90F…`) y 40 000 puntos idénticos; el traductor le vale **−29,8 %**
  (89,7 → 63,0 s reales). Cuarto guest de `chd-exactitud.ps1`, que ahora
  acepta `-Solo <nombre>` para correr uno sin repetir el resto.

#### La carga del 2026-08-19: 14 juegos nuevos, 11 exactos a la primera

La lista pedida para ensanchar cobertura (más Windows CE, streaming ADX,
RTT y mezcla, multipass del TA, RTC de verdad, FMV Sofdec) entró a `E:` y
corrió por `chd-barrido.ps1 -Tanda nuevos` (binario `CCFE5F3D77A5C6F6`,
40 s, int contra jit, RTC clavado). **Exactos de punta a punta** — captura
byte a byte y todos los puntos idénticos —: Ikaruga, F355 Challenge
(**238,0 por entrada**, el récord del parque), Test Drive Le Mans, Sword
of the Berserk, Grandia II (189,6), Skies of Arcadia (138,9), PSO v2
(109,0), **Shenmue II** (137,1), Resident Evil 2, Tomb Raider IV y
Rayman 2. Tres expedientes abiertos:

- **Rez: CERRADO — exacto, y el 0,04× era el ambiente, no el árbol**
  (2026-08-20, la extensión). Sobre `A8AD7F95EFA41D02`: 22 s emulados a
  **0,58× parejo con la traza encendida** (que de por sí cuesta), 60
  cuadros por segundo emulado, UNA excepción de TLB en toda la corrida
  (sobre `00100044` — la fuente ROM, P0) — ni tormenta ni precipicio,
  cruzando de largo el punto donde ambos brazos del barrido murieron.
  `DCEMU_MEMO_ARM=1` (la omisión vieja del ARM7) tampoco lo reproduce:
  0,58× idéntico. Y la validación 2×2 completa salió **exacta**: cuatro
  brazos con captura idéntica y los 40 000 puntos int≡jit al dígito (la
  captura a 40 s es negra — el mismo hash que 18 Wheeler, que arranca
  igual —, así que los puntos son el testigo fuerte). El 0,04× de la
  noche queda como lección de ambiente: los dos brazos pagaron el primer
  barrido EN FRÍO del CHD de 1 GB en `E:` — un veredicto de rendimiento
  sobre la primera lectura de una imagen nueva no vale hasta repetirlo
  caliente.
- **Marvel vs. Capcom 2: divergencia de punto con captura intacta** (punto
  8550: mismos ciclos al dígito, PC 4 bytes atrás en el brazo jit,
  `r4` una iteración detrás, Q/T distintos — la firma de estar en otra
  vuelta de la división por software en el mismo ciclo). La captura de
  40 s sale byte a byte. Clase «desfase de contabilidad» — piso de ruido
  primero (el pad y el método del expediente de THPS2), bisección después.
- **Shenmue (disco 1): divergencia de punto con `ciclos` corridos en 1**
  (7314650218 contra …219, punto 36664, `sr=70000000` — adentro de una
  entrada de excepción con BL). Captura de 40 s byte a byte. Un camino
  compartido cobra un ciclo distinto entre las dos formas; mismo trato
  que MvC2, y probablemente la misma causa raíz.

**El expediente MvC2/Shenmue, resuelto al ciclo (2026-08-20, la
extensión).** El piso de ruido 2×2 (`herramientas/piso-expediente.ps1`)
primero: los cuatro brazos se reproducen a sí mismos exactos — int-a≡int-b,
jit-a≡jit-b, capturas idénticas las cuatro — y int≠jit diverge en LOS
MISMOS puntos que en el binario anterior (8550 y 36664): real, de forma,
y no es el pad. Y las divergencias **reconvergen**: MvC2 difiere en 11 de
40 000 puntos (8550-16683), Shenmue en 4 (36664-39064), con `ciclos`
incluido — es un desfase transitorio de contabilidad, no una bifurcación
del guest. Las seis palancas de emisión (`herramientas/
expediente-palancas.ps1`: pares, pares-llamada, terminales, ranura FPU,
puentes, atajo P1/P2) salieron **todas con los mismos 4 puntos**: no es
ninguna plantilla.

Lo que lo nombró fue el **modo fino** (`DCEMU_CP_MS=36665:1`) más el
desensamblado: 1515 pasadas por brazo y UNA distinta. En una ventana de
espera (el lazo de sondeo en `0c0eff8e`), una interrupción entra en las
dos formas en la misma frontera; el manejador (`VBR+0x600 = 8c00fa02`)
empieza con **cinco NOP** y el push recién en `fa0c`. El intérprete toma
su pasada de reintento en la frontera de la entrada (fino: `+0 ciclos,
pc=fa02`); el bloque emitido **se desliza por el prefijo de NOPs** —
`gen_corte` se elide tras instrucciones sin ciclos, y la justificación
(«sin ciclos nuevos la condición no puede haberse vuelto cierta») tiene
un agujero de exactamente este caso: `intc_sh4_reintentar` quedó armado
POR LA PROPIA ENTRADA, antes del bloque — así que honra el reintento en
la primera instrucción con ciclos: 5 NOP + 1 push = `+1 ciclo, pc=fa0e`,
la aritmética exacta del fino. **Una pasada después los dos brazos son
idénticos hasta el reloj** (…6822, mismo PC): difiere la posición de una
pasada de contabilidad, no la ejecución del guest — por eso las capturas
salen byte a byte y los puntos reconvergen. MvC2 es la misma clase en
ventana más ruidosa (su lazo de espera divide por software con DIV1, y
el muestreo por ms lo pesca en fases distintas).

**Veredicto**: clase entendida al ciclo y acotada — visible solo para el
muestreo, exacta en salida, 2 de 17 juegos, autoconsistente. El arreglo
conocido tiene dos formas y ninguna es gratis: chequear `reintentar` al
entrar a `jit_despachar` es una rama más en el camino más caliente (el
precedente de `VERIF_COMPLETA`: una rama por entrada costó +1,0 % en CT),
y emitir el corte también tras instrucciones sin ciclos infla los bloques
para un caso que la salida no ve. Queda **documentado y abierto a
propósito**, con el fino como testigo y la regla de lectura nueva: una
divergencia de puntos que reconverge con `ciclos` incluido y captura
intacta es esta clase, y el modo fino + desensamblado la nombra en tres
corridas.

**El expediente del RTC, y la lección para la fase F.** Sin el RTC clavado,
THPS2 divergía en un punto: mismo `reloj_total`, mismo PC (`8c1671ce`, adentro
de la división por software de 32 pasos del compilador) y **un registro
distinto** — más un ciclo de deriva después, porque el epílogo de la división
toma la rama según el valor. El desensamblado nombró a la llamadora: divide por
60, por 60 y por 24 con almacenes de byte en un struct — **una conversión de
hora**. La absolución del traductor fue el método del piso de ruido: **el mismo
brazo intérprete corrido dos veces con 20 s de pared entre medio diverge en el
mismo punto y el mismo PC** (`r1 = 5700c469` contra `6e00c469`), o sea que era
el RTC siguiendo el reloj del host — el mecanismo ya conocido por las demos del
menú BIOS, visto por primera vez en un juego comercial en medio de la corrida.
Regla nueva: **las compuertas de juegos llevan `DCEMU_RTC_FIJO`**, no solo las
demos; y una divergencia que reconverge con captura intacta pide el piso de
ruido antes que la bisección.

**El reparto, rehecho** (`perfil-jit.ps1`, mismo binario; los tiempos llevan los
relojes de `--perf` y no son tanda):

| | DCDoom 35 s | CT 180 s | SR2 60 s |
| --- | --- | --- | --- |
| resto (emitido + despacho + intérprete + MMU) | **73,8 %** | **62,5 %** | **77,7 %** |
| AICA — ARM7 | 12,0 % | **20,2 %** | 10,4 % |
| AICA — mezclador | 4,9 % | 7,2 % | 3,5 % |
| bloque periódico neto | 8,3 % | 1,3 % | 4,6 % |
| GL entero (cuadro) | 1,0 % | 6,7 % | 2,6 % |

Sin novedad estructural contra el del 2026-08-13: el SH-4 sigue siendo el
62-78 % y el ARM7 la segunda porción. Nota: la corrida de CT del perfil (el
`.cdi` restaurado, con las teclas del banco) da 21,24 G instrucciones y 21,1
por entrada — **el camino histórico**, no el 13,8 de la tanda del 08-15.

**El censo de la frontera, por peso** (% de las entradas):

| fin | DOOM | CT | SR2 |
| --- | --- | --- | --- |
| sin plantilla | 50,8 % | **55,7 %** | 56,7 % |
| par de rama | 37,3 % | 19,4 % | 29,8 % |
| rama/FPU en ranura | 0,2 % | **18,9 %** | 2,6 % |
| tope de 64 | 8,8 % | 2,0 % | 3,7 % |
| ranura con memoria (sin par) | 1,7 % | 3,7 % | 2,8 % |

La lista de cortadores del resumen era **sin ponderar** (cuenta sitios de
traducción, no entradas), y eso la hacía mentir por omisión: los cortadores de
verdad son pocos sitios corridos millones de veces. B.1 exigió su instrumento
— el resumen ahora agrega, por palabra de corte, las `veces` del bloque cortado
(«lo que más cortó, ponderado por veces», tabla indexada por la palabra entera,
sin truncar); el campo viaja en el bloque (`corte`), se fija en los seis sitios
de corte del descubrimiento, y en un corte de ranura la palabra anotada es **la
de la ranura**, que es la que nombra qué plantilla o regla falta. DOOM y SR2
validan el instrumento al dígito (mismas instrucciones y entradas que la
corrida anterior); CT muestra su bimodalidad conocida (~31 k instrucciones,
sin gamepad — sospechoso de RTC tras el expediente de THPS2).

**El censo ponderado** (binario `E046081666308C06`, % de las entradas
cortadas):

- **CT**: `PREF` **15,8 %** — el flush de store queue, sin plantilla —, FTRV
  9,5 %, FIPR 10,2 %, FSCHG 4,1 %, FMAC 4,1 %, movedores `LDS Rn,FPUL` /
  `STS FPUL,Rn` ~7,6 %, y filas CON plantilla que cortan por caer **en la
  ranura** de una rama sin par (FSTS 3,4 %, FLDI1 2,4 %, FDIV 1,9 %, BT/S).
- **SR2**: FTRV **12,6 %**, PREF 7,5 %, FIPR 5,9 %, **FRCHG 5,3 %**, XTRCT
  7,9 %, FMAC 5,6 %, FSCHG 3,0 %, movedores FPUL ~8,9 %, LDTLB 4,4 %, RTE
  2,1 %.
- **DOOM**: **los escritores de SR y de bancos ~51 %** — `LDC R1,SR` 12,1 %,
  `LDC R14,R4_BANK` 11,5 %, `STC SR,R0` 9,8 %, más STC SSR/STS PR/LDS PR,
  RTE 2,2 % y LDTLB 2,1 % (las transiciones de kernel de WinCE) —, ADDC 5,5 %,
  palabras de datos ~13 %.

**Un hallazgo colateral con cliente**: CT compara **782,9 M de palabras** en la
verificación por entrada (38,7 M de veces por el camino largo, 20,2 por vez) —
sus 103 737 movimientos de época de escritura invalidan `epoca_escr` global y
cada movimiento re-verifica ~373 bloques. **La época por página (pendiente 1
del recompilador) encontró a su cliente, y no era DOOM: es CT.**

**El reparto módulo/arena, medido** (2026-08-18, `herramientas/perfil-arena.ps1`
desde consola elevada del usuario, binario `D018E6606A6B996F`, sobre el **hilo
principal** — el proceso carga además los workers del driver de video, que no
son ni C compilado ni arena):

| hilo principal | DOOM 35 s | CT 180 s |
| --- | --- | --- |
| dcemu.exe (C compilado) | **47,4 %** | **59,0 %** |
| arena (código emitido) | **36,4 %** | **31,4 %** |
| kernel (ntoskrnl + ntdll) | 13,0 % | 4,2 % |
| CRT (memcpy y cía.) | 1,5 % | 3,7 % |
| GL (nvoglv64) | 0,4 % | 0,7 % |

**Es el techo de la fase G**: clang solo puede mover el 47-59 % compilado — el
arena es invariante al compilador por construcción y el kernel tampoco. Un
+10 % de codegen sobre esa mitad valdría ~5-6 % de punta a punta: la fase vale
intentarse, pero no es grande. Dos notas del instrumento: el arena sale como
`"Unknown"` (con comillas) en la columna de imagen del dumper de xperf — la
primera lectura lo perdió por eso, ya corregido en el script —, y el CSV de
SR2 no se generó (xperf falló ahí; DOOM y CT acotan el rango por los dos
extremos, MMU-pesado y FPU-pesado, así que no se repitió). Los CSV pesan
3-7 GB cada uno y se borran tras extraer las cifras.

### Fase B — la FPU de verdad (el hueco grande con nombre)

Lo que hay hoy: la aritmética escalar sz0 va por **envoltorio ligero** (llamada
al manejador, sin sync — puro sobre FR/FPUL/T), los FMOV de sz0 y los pares de
sz1 están emitidos, y un bloque con fila FPU queda **atado al modo y sin
enlaces**. Lo que **no** hay: plantilla para **FMAC, FIPR, FTRV, FSCA, FSRRA ni
FSCHG** — todos cortan el bloque por `JIT_FIN_PLANTILLA`, y FSCHG además mueve la
clave. El síntoma está en el resumen: **Crazy Taxi hace 86,4 M de transiciones de
PR/SZ/Enable y 1 489 M de entradas al despachador a 13,8 instrucciones por
entrada** (DOOM: 48,4). CT y SR2 son los guests FPU; CT es el que menos mejoró de
los tres (−16,5 %).

Escalones, reordenados por el censo ponderado de la fase A (los porcentajes de
abajo son «de las entradas cortadas» de cada guest), cada uno con su palanca y
su tanda:

- **B.1 — el censo dirigido: hecho** (fase A, arriba). Lo que nombró, en orden
  de peso, es el lote de B.2; y mató de paso la versión ingenua de «FSCHG
  dentro de la traza» como primer movimiento — FSCHG pesa 4,1/3,0 %, menos que
  PREF o FTRV.
- **B.2 — el lote de plantillas del censo**, de mayor a menor techo:
  - **PREF Rn** (CT 15,8 %, SR2 7,5 %, y es LA vía de la geometría — el flush
    de store queue): manejador con sync previa, como toda fila que accede.
  - **La geometría de `floatgraph.c`: FTRV, FIPR, FMAC** (CT ~24 %, SR2 ~24 %
    sumadas) más FSCA/FSRRA si el censo de después las sube: por el envoltorio
    ligero de FADD — puras sobre los bancos con Enables=0 garantizado por
    `b->fpu` — con los ciclos leídos del cuerpo ENTERO del manejador (la
    lección de los 633 M).
  - **Los movedores**: `LDS Rn,FPUL` / `STS FPUL,Rn` (fila FPU, emisión
    directa contra `O_FPUL`), `LDS Rn,PR` / `STS PR,Rn` (con su interacción
    con el rastreo de PR de los pares: el LDS invalida `pr_conocido`).
  - **Los enteros sueltos**: XTRCT (SR2 7,9 %), ADDC (DOOM 5,5 %).
- **B.2 — HECHO (2026-08-18): el mayor salto de la serie desde agosto 9.**
  Las 12 plantillas del lote (134 en total): PREF por manejador con
  `escribe=1` (su única falta va antes de toda mutación, verificado en el
  cuerpo); FTRV/FIPR/FMAC/FSCA/FSRRA por el envoltorio ligero (FSCA **no suma
  ciclos** y el envoltorio lo respeta — la lección del cuerpo entero); los
  movedores de FPUL/PR por emisión directa (molde `pl_sts164`; los de FPUL
  con `fpu=1` por el 0x800 de FD; `LDS Rm,PR` con `escribe_pr=1`); XTRCT y
  ADDC por manejador, como NEGC y por lo mismo. **Exactitud completa**:
  capturas byte a byte y todos los puntos de `DCEMU_CP_MS` idénticos en los
  tres guests (35 000/180 000/60 000, con RTC clavado — y CT salió exacto al
  dígito, otro indicio de que su bimodalidad era el RTC) más el trío CHD.
  **Tanda reentrenada `D018E6606A6B996F`, rangos disjuntos en los tres**:
  DOOM **25 715-26 219 contra 43 279-43 387 ms (−40,3 %, 1,35×)**; CT
  **80 925-81 802 contra 111 650-111 795 (−27,3 %, 2,22× — venía de −16,5 %
  y 1,74×)**; SR2 **51 966-52 078 contra 72 772-72 987 (−28,6 %, 1,15×)**.
  Entradas al despachador: DOOM 95,2 M (54,0 por entrada), CT 651 M (33,4),
  SR2 234 M (35,6). Ojo comparando con tandas viejas: el banco de CT volvió a
  cambiar de camino (21,79 G instrucciones) — el A/B interno es el veredicto.
- **El censo rehecho tras B.2** (% de las entradas cortadas) nombra el lote
  siguiente: en **CT** la frontera dominante ya es la **fila FPU en la ranura
  de retardo (35,4 % de las entradas totales)** y los cortadores pesados de
  ranura son filas de **emisión directa** — FSTS 10,0 %, FMOV FRm,FRn 6,6 %,
  FLDI1 5,5 % — que no llaman manejador y no cargan el `PC += 2` que motivó
  la prohibición general; FSCHG quedó como el cortador «sin plantilla» más
  pesado (12,7 %). En **DOOM** los escritores de SR/bancos subieron a ~62 %
  de las cortadas (`LDC Rn,SR` 23,4 %). En **SR2**: LDTLB 9,6 %, FSCHG
  6,6 %, RTE 4,6 %. Y el camino largo de verificación de CT subió a 5,7 % de
  las entradas (901 M de palabras): **la época por página es cada vez más un
  ítem de CT** (fase D).
- **B.2b — el lote siguiente, en dos mitades que el censo pesó**:
  1. **Filas FPU de emisión directa admisibles en ranura** (~22 %+ de las
     cortadas de CT): la prohibición «ninguna fila FPU entra en ranura» existe
     por el `PC += 2` de los manejadores; una fila emitida directo no lo
     tiene. Pide una marca por plantilla («no toca PC»), atar el bloque a la
     clave FPU también cuando la fila FPU viaja en la ranura, y dejar afuera
     por ahora las de memoria (FMOV.S @Rm) fuera de los pares.
  2. **Las filas terminales**: los escritores de SR y de bancos (DOOM ~62 %
     de las cortadas), LDTLB, y FSCHG/FRCHG como caso particular (mueven la
     clave FPU y el bloque siguiente se traduce en el modo nuevo). La regla
     que los excluye («no toca SR.MD/RB ni FPSCR») es para filas EN MEDIO del
     bloque; una fila terminal — sync completa, manejador, y el bloque
     TERMINA ahí sin enlace, con el despachador re-evaluando la clave entera —
     la esquiva por construcción. RTE queda afuera: es una rama con ranura,
     otra maquinaria.
- **B.2b — HECHO (2026-08-19): las dos mitades quedan encendidas.** La tanda
  limpia de cuatro brazos (mañana del 19, dispersión 0,1-0,7 %): las
  **terminales ganan chico y consistente** — DOOM −0,6 % con rangos
  disjuntos, SR2 −0,6 % disjunto por 39 ms, CT no distingue —, la **ranura
  FPU gana en DOOM** (−0,7 % disjunto) y es neutra en CT y SR2. **Marcas
  contra su propio intérprete: DOOM −41,5 % (1,37×), CT −27,6 % (2,21×),
  SR2 −29,7 % (1,16×)** — las tres mejores que B.2. Y CT con 43 M de
  entradas menos y el tiempo quieto es la cuarta medición de que el viaje al
  despachador no es el costo. El trío CHD salió exacto de nuevo con este
  binario (capturas canónicas, todos los puntos idénticos — THPS2 limpio con
  el RTC clavado en el script). Implementación: **exacto al dígito** (capturas y 275 000 puntos idénticos en los tres guests, binario
  reentrenado `14D6AFDA3C7BF7BE`): cinco filas terminales (`LDC Rm,SR`, LDTLB,
  TRAPA, FSCHG, FRCHG — el bloque termina en ellas, salida sin enlace y PC del
  contexto porque el manejador es su dueño), cinco filas de sistema por
  manejador común (`LDC SSR/SPC/Rn_BANK`, `STC SSR/SPC` — no tocan SR.MD/RB ni
  registros activos), y ocho filas FPU directas admisibles en ranura
  (`sin_pc`: puros movs, sin el `PC += 2` que motivó la prohibición). Palancas
  `DCEMU_JIT_SIN_TERMINALES` y `DCEMU_JIT_SIN_RANURA_FPU`. En SR2 la fila
  terminal absorbe el **13,4 % de las entradas** y «sin plantilla» cae de
  30,6 % a 18,1 %.
  **Y un expediente de medición que costó la tanda nocturna**: la tanda de
  cuatro brazos corrida en la noche del 18 dio a `sin-ranura` ganando 4 % en
  SR2 con rangos disjuntos — y la repetición de la mañana la desmintió
  (completo 52 103, sin-ranura 52 209, solapados, y TODOS ~4 s más rápidos
  que la noche). La ronda nocturna estaba contaminada por carga de la máquina
  y la cadena misma murió a las 23:11 cuando el cierre de la consola se llevó
  al proceso desacoplado. Dos reglas que salen de esto: **una tanda cuyos
  brazos corren más lento que su propia repetición se descarta entera** (no
  se cherry-pickea la ronda buena), y **un proceso desacoplado no sobrevive
  el cierre de la consola** — las cadenas largas piden la sesión abierta o
  una tarea programada. El diagnóstico de la falsa asimetría quedó hecho
  igual y absuelve a la emisión: la admisión de ranura solo agrega +1,5 % de
  bytes en SR2.
- **B.2c — HECHO (2026-08-19): exacto, tiempo neutro, queda encendido — y
  cierra la serie de frontera.** Tanda reentrenada `854C904DFB407D5E`: DOOM
  **−41,7 %** (marginalmente mejor que B.2b), CT −27,1 % y SR2 −28,8 %
  (marginalmente peores, con el intérprete también corrido entre tandas:
  ruido de máquina, no señal). DOOM llegó a **59,4 instrucciones por
  entrada** y el reloj no lo siguió. **La lectura que importa: tres lotes
  seguidos de frontera — B.2 ganó grande, B.2b chico, B.2c neutro. La
  frontera dejó de ser donde está el tiempo** (quinta confirmación de que el
  viaje al despachador no es el costo), lo que degrada el techo esperado de
  B.2d y de B.3: antes de escribirlos, rehacer el reparto. Contenido: **RTE como fila terminal** — su manejador es
  autocontenido (busca la ranura ANTES de escribir SR, la ejecuta por dentro
  con `core.execute`, deja PC en SPC), así que cae gratis en el mecanismo
  terminal y su contrato de falta es el del intérprete (la sync previa hace
  reejecutable al RTE entero, que es lo que la instantánea hacía). Pesaba
  18,8 % de las cortadas de DOOM y 9,8 % de SR2. Más `LDS Rm,FPSCR` terminal
  (el otro escritor de FPSCR, SR2 3,5 %), y por manejador común `LDS.L
  @Rm+,MACH` (¡27,9 % de las cortadas de DOOM!), `MOV.W @(d,Rm),R0` y
  MULS.W. 149 plantillas. Exacto al dígito en los tres guests; DOOM sube a
  59,4 instrucciones por entrada.
- **B.2d — el envoltorio FPU en ranura (diseñado, pendiente de B.2c)**: los
  envoltorios (FDIV 16,2 %, FIPR 7,1 %, FTRC 7,0 % de las cortadas de CT)
  siguen vetados de la ranura por el `PC += 2` del manejador. El veneno
  exacto está localizado: `tr_salto_dinamico` captura el destino **en
  `O_PC` antes de la ranura**. El diseño: bandera `en_ranura` en el
  generador — suprime el corte periódico interno del envoltorio (en el
  camino tomado llevaría el PC de continuación equivocado) y activa
  guardar/restaurar `O_PC` alrededor de la llamada vía un scratch propio —,
  `tr_emitir_ranura` deja de contar el intento de las filas `propia` (el
  envoltorio ya lo cuenta), y la admisión del recorte se amplía a
  `propia && !accede && fpu`. Palanca propia para su A/B.
- **B.3 — los pares condicionales (BT/S, BF/S con ranura de memoria)** y
  recién después **FSCHG dentro de la traza** si su residuo sobrevive a B.2b.
  El censo mostró filas con plantilla cortando por caer en la ranura de una
  rama sin par (FSTS, FLDI1, FDIV en CT): los pares hoy cubren solo las ramas
  incondicionales y de llamada.
- **B.4 — enlaces entre bloques FPU de la misma clave.** Sin cambios: solo si
  tras B.2/B.3 el reparto le da techo — el viaje al despachador no es el
  costo, tres mediciones.
- **B.5 — la aritmética en SSE en línea.** Sin cambios: el último, con el
  riesgo de MXCSR contra RM/DN y Cause/Flag, y solo si el envoltorio aparece
  con nombre en el reparto de después.

**Gates**: totales al dígito y capturas byte a byte en CT y SR2 (los árbitros
FPU) más DOOM (el árbitro inmune al pad); `.wav` de CT con reverb; el trío CHD de
la fase A repasado si algún escalón toca emisión general. Palanca por escalón.

### El reparto tras la serie de frontera (2026-08-19, binario `854C904DFB407D5E`)

| | DOOM 35 s | CT 180 s | SR2 60 s |
| --- | --- | --- | --- |
| resto (emitido + despacho + intérprete + MMU) | 72,2 % | **57,3 %** | 77,0 % |
| AICA — ARM7 | 12,7 % | **22,7 %** | 10,5 % |
| AICA — mezclador | 5,1 % | **8,1 %** | 3,5 % |
| bloque periódico neto | 9,1 % | 2,0 % | 5,2 % |
| GL entero | 1,0 % | 7,5 % | 2,7 % |

**Lo que reordena**: en CT el AICA entero ya es el **30,9 %** de la corrida
(el propio `--perf` estima el techo de moverlo: de 1,91× a 2,77×). La serie
de frontera achicó el SH-4 de CT de 62,5 % a 57,3 % y ahí se agotó (B.2c
neutro). El orden que sale: **la fase E (ARM7 residual + mezclador) sube a
primera**, B.5 (la aritmética FPU en SSE, el cuerpo de los bloques de CT)
queda como el movimiento SH-4 con techo por medir, y B.2d/B.3/B.4 bajan —
la frontera ya no paga.

### Fase C — la elisión de lazos ociosos (la fase 7 heredada, condicional)

El precedente es la memoización del ARM7: **salida idéntica, la cuenta se reporta
como elisión**. Condición de entrada intacta: que un perfil muestre sondeo
dominante en algún guest — el instrumento es el build `-DDCEMU_FORMA=ON` (cuenta
bloques del guest, inmune a la disposición del binario) más el reparto de la
fase A. El mecanismo, si entra: reconocer el bloque ocioso (se salta a sí mismo y
su único efecto es leer estado que no cambia) y avanzar el tiempo emulado hasta
el **próximo vencimiento** — la infraestructura ya existe, es el reloj por
eventos de la fase 5. **Gate**: capturas y `.wav` intactos; el total de
instrucciones cambia y se reporta como elisión, como el ARM7.

**Hecha el 2026-09-03, y más fuerte que lo que esta fase pedía**: la condición
de entrada la dio el censo del contrato (el lazo de espera de Crazy Taxi es el
47,22 % de sus instrucciones), y el mecanismo escrito no avanza el reloj hasta el
próximo vencimiento sino que **saltea vueltas enteras dentro del grano**, así que
la grilla no se mueve y **el total de instrucciones queda al dígito** (no se
reporta como elisión: se cuenta). CT elide el 37 % de sus instrucciones y gana
−4,7 % con rangos disjuntos, compuerta verde contra el intérprete con 60 000
puntos. El expediente entero — piezas, aritmética del corte, compuerta, tanda y
lo que queda para la máquina del banco — está en `docs/recompilador-plan.md`,
«La elisión de lazos ociosos».

### Fase D — los residuos chicos del camino de memoria (solo si la fase A los sostiene)

Los tres pendientes numerados de `recompilador-plan.md`, cada uno con su techo ya
anotado y **la regla de no escribir nada bajo ~1,5 % sin que el reparto nuevo lo
suba**:

- **zona no plana**: 1,78 % en DOOM — el techo real de fastmem. Antes que VEH y
  `VirtualProtect`, la alternativa barata: una segunda entrada plana para las
  ventanas de VRAM en la tabla de zonas emitida.
- **etiqueta de la caché de traducciones**: 1,24 %; **permiso**: 0,34 %.
- **la época por página**: los rechazos de verdad (8,2 M en DOOM, 2,35 M en SR2)
  donde la página sí se remapeó; con el camino largo en 0,9 % de las entradas, el
  techo es chico — medir antes.
- **elisión de recarga en reentradas**: los no volátiles sobreviven el viaje C;
  falta la marca de «contexto ensuciado». Mismo trato.

### Fase E — la segunda porción: el ARM7 residual y el mezclador

**El censo del ARM7 de CT (2026-08-19, 2,0 G de pasos)** reordenó la fase:
solo el **36,8 % de los pasos corre en bloques** (2,9 por bloque — B/BL es el
25,1 % del despacho: código ramoso), así que ensanchar plantillas del
traductor x64 tiene techo bajo; los dos lazos calientes (~35 % de los pasos)
son barridos de sondeo cuyo estado cambia por muestra (la memoización no
puede reponer). **El arreglo estructural del ARM7 son bloques que sigan la
rama** — el análogo de los pares del SH-4, con los cuatro teoremas a
re-probar.

**El segundo escalón hecho: la cola de salto de los bloques (2026-08-19,
`DCEMU_SIN_RAMA_ARM=1` la apaga).** Si lo que cortó el tramo recto es un
B/BL — `d_salto`, con cualquier condición —, entra al bloque como última
entrada y corre adentro; y si el salto vuelve a la propia cabecera, el
bloque **da la vuelta en el lugar** sin pasar por `arm7_blq_intentar()`.
Lo que la hace exacta, punto por punto:

- la cola ejecuta por su manejador (`d_salto`), así que **el borde de la
  memoización corre idéntico por construcción** — filtro, umbral, veneno,
  reposición (que deja `pc_cambio` en 1 y el PC en el propio salto, o sea
  que el bloque corta y el intérprete lo re-ejecuta, como siempre) y
  arranque de grabación (el bloque corta y replica la contabilidad
  `memo_ciclos`/`memo_instr` del intérprete para ese salto);
- sin chequeo de FIQ en la cola ni entre vueltas, **por el teorema 1**: las
  rectas no tocaron el archivo (la salida lateral corta antes — y manda
  incluso con las rectas completas, porque el acceso de la última pudo
  caer en el archivo con `ult_pasos == rectas`) ni el CPSR de control
  (`d_msr` no entra en bloques; la ALU con S solo escribe NZCV);
- la vuelta re-verifica **el presupuesto (teorema 2)** y **las palabras**
  (`memcmp` de las n, cola incluida) — las palabras porque una recta puede
  escribir sobre el propio bloque, que el intérprete vería en la búsqueda
  siguiente;
- el emitido queda intacto: cubre las rectas como siempre (la cola corre
  en C), y en la vuelta se reentra al arena con `r15 == base` garantizado;
- **los ciclos del cuerpo se comprometen a `arm7.ciclos` ANTES de correr la
  cola**, y el bloque devuelve solo lo no comprometido. Este es el agujero
  que la primera compuerta cazó — no las capturas ni el `.wav`, que
  salieron byte a byte, sino **el histograma con perfil entre brazos**: el
  brazo con cola ejecutaba 177 004 pasos menos en 30 s de CT. La reposición
  de la memoización compara el costo del barrido contra `arm7.ciclos` (sus
  «dos condiciones de tiempo»), el intérprete llega al salto con el cuerpo
  ya cobrado, y la cola le mostraba el saldo de la *entrada* del bloque —
  más grande —, aceptando reposiciones que el paso a paso rechaza. Lección
  doble: una elisión que preserva el estado puede igual mover la frontera
  del lote (eso es lo que la condición de tiempo protege), y **la compuerta
  de un mecanismo de elisión necesita comparar los pasos ejecutados, no
  solo la salida** — la salida absuelve al resultado, no al mecanismo.

El descubrimiento anexa la cola solo detrás de al menos una recta (un B a
secas no gana nada por ser bloque), y `ciclos_max` suma sus 3 ciclos de
salto tomado. Suites 22/22 verdes (`sh4.todo` lleva `arm7`/`arm7jit`
adentro). **Compuerta verde entera** (binario `37F48D53A1F1CEFE`,
`herramientas/rama-arm-gate.ps1`): DOOM/CT/SR2 con capturas y puntos
idénticos, el `.wav` de CT byte a byte, y el histograma con perfil idéntico
entre brazos. Efecto en CT: pasos en bloque de 22,0 % a **34,5 %**.

**El tercer escalón: el sondeo en bloque (2026-08-19, `DCEMU_SIN_SONDEO_ARM=1`
lo apaga).** La cola sola dejó solo 53 891 vueltas en el lugar — los dos
lazos calientes de sondeo no se convirtieron, porque **leen el archivo de
registros** y el teorema 4 cortaba por el costado en cada lectura. Pero el
teorema es conservador de más para las lecturas: dentro de un lote la FIQ
pendiente solo puede cambiar por **escrituras** del ARM — `aica_tick()` y el
SH-4 corren entre lotes, y `leer_registro()` no toca ni `int_nivel` ni los
pendientes (su único efecto de lado, el `dio_la_vuelta` del monitor EG, es
un bit de monitoreo que el propio flujo de instrucciones limpia igual desde
cualquier camino). Refinamiento: `arm7_leer()` ya no marca `arm7_toco_reg`
(la escritura sí, como siempre), así que los lazos de sondeo caben enteros
en un bloque con cola y **dan la vuelta en el lugar** — que es exactamente
la mitad de los pasos que la memoización no puede reponer (lo leído cambia
por muestra, pero *entre* lotes: adentro del lote el lazo gira sobre valores
quietos, y eso el bloque lo hace más barato que el intérprete, no distinto).
El emitido no se re-emite: `aj_toco` mira el mismo global, y si la lectura
no lo enciende, el talón no salta. Abre además la puerta de la fase C del
ARM7: un lazo cuya vuelta deja los registros idénticos es un giro puro y el
saldo se puede consumir de un golpe — diseño aparte, no esta noche sin
medir primero.

**El cuarto escalón: el encadenado en el lugar (2026-08-19, bajo la misma
palanca `DCEMU_SIN_RAMA_ARM`).** El sondeo relajado solo fusionó corridas
partidas (mismos pasos en bloque, mismas 53 891 vueltas): el perfil por PC
mostró por qué — **el lazo dominante de CT (38,6 % de los pasos del ARM) es
el barrido de los 64 canales y es un ciclo de DOS bloques**, porque su
salida de en medio (`BNE` adelante en `0xa0c`) corta el tramo: `a04
LDRB/TST` + cola adelante, y `a10 ADD/ADD/CMP` + cola atrás a `a04` — y la
vuelta en el lugar exigía `destino == base`. La generalización correcta no
son ramas en el medio del bloque sino **encadenar**: después de la cola
(salte o caiga) — o del final de un tramo sin cola —, buscar el bloque del
destino en la tabla y seguir corriéndolo dentro de la misma llamada,
re-verificando presupuesto y palabras por salto. La FIQ queda cubierta por
la inducción del teorema 1 (una escritura al archivo corta por el costado
antes de encadenar; la grabación corta antes), el descubrimiento se queda
en `intentar()` detrás de su chequeo de FIQ (solo se encadena a bloques ya
descubiertos), y la vuelta en el lugar pasa a ser el caso `b2 == b` del
mismo camino. Es, de paso, el «encadenar los bloques del ARM7» que la fase
4 dejó nombrado — resuelto sin despachador emitido. **Compuerta verde
entera** (binario `CCFE5F3D77A5C6F6`): tres brazos exactos e histogramas
idénticos; las corridas pasan de 3,0 a **6,4 pasos por corrida** con
19,4 M de encadenados y los despachos de bloque bajan de 25,3 M a 18,5 M.

**La tanda de los tres escalones (2026-08-20, binario reentrenado
`20547C545594A328`, `herramientas/rama-ab.ps1`: tres brazos, tres rondas,
orden rotado).** En Crazy Taxi los tres brazos salen **escalonados con
rangos disjuntos**: viejo 83 405-83 836 ms, solo-cola 82 502-83 054, todo
82 136-82 468 — la cola con su encadenado vale **−1,0 %** y el sondeo
relajado **−0,6 %** encima: **−1,6 % total** en el guest AICA-pesado.
DOOM y SR2 dentro de su dispersión (su ARM pesa 10-13 % contra el 22,7 %
de CT). En los totales de CT aparece la bimodalidad documentada del pad
ocioso (dos valores discretos a ±71 instrucciones, cruzando brazos — el
ambiente, no los mecanismos; DOOM y SR2, los árbitros inmunes al pad,
salen al dígito en las nueve corridas). Los tres escalones quedan
**encendidos por omisión**.

**Lo que quedó abierto, con dos datos ya medidos.** La cobertura se plantó
en 34,5 % de los pasos: `con ≡ medio` al dígito (mismos pasos en bloque,
mismos encadenados ±6), o sea que **el sondeo relajado no era el freno del
lazo dominante — su `LDRB` lee RAM de onda, no el archivo** (el `--perf`
lo confirma: cero grabaciones abortadas por registro). Y la memoización
convive sana: 318 k reposiciones × 89,7 instrucciones = 7,6 % de los pasos
elididos, con 784 k grabaciones (durante las cuales los bloques no corren
— ~20 % de los pasos, el costo estructural de grabar). Para la mitad
interpretada restante quedó implementado **el censo de rechazos de
`intentar()`** (bajo `DCEMU_PERFIL_ARM`, con el contador de
descubrimientos que delata desalojos por colisión de ranura).

**El censo habló (CT 60 s, binario `20547C545594A328`): el freno es la
memoización.** De 680 M de pasos: cobertura 42,7 % (6,3 por corrida,
46,3 M de encadenados), descubrimiento frío (4031 — cero colisiones de
ranura), FIQ y presupuesto despreciables (0,0/0,2 %), marca negativa
12,1 % (tramos encabezados por filas no admitidas: MSR/MUL/LDR-R) — y
**«grabando» el 44,7 % de los pasos**: el memo graba el barrido de
canales, la muestra siguiente lo invalida (repone solo 7,6 % con 89,7 por
reposición), y vuelve a grabar — y mientras graba, los bloques están
apagados. El trade se invirtió: el memo valía +0,5 % cuando los bloques
no existían y hoy suprime al mecanismo que cubre esos mismos lazos mejor.
El A/B (`herramientas/memo-ab.ps1`, `DCEMU_SIN_MEMO_ARM=1` sobre el mismo
binario, CT y SR2) decidió: **CT −1,7 % con rangos disjuntos apagándola**
(81 762-82 355 contra 80 420-80 684 ms), SR2 sin distinguir. **La
memoización queda apagada por omisión cuando los bloques con cola corren**
(`arm7_reset()`; `DCEMU_MEMO_ARM=1` la fuerza, el brazo de vuelta), y la
compuerta final sobre el binario `988E7E153E8607D2` salió verde entera:
tres brazos exactos con memo clavada (histogramas de ejecución idénticos,
37 líneas), y la omisión nueva exacta a nivel de salida a través del
cambio de estado (bmp, `.wav`, 60 000 puntos). El punto medio (grabar con
retroceso tras reposiciones fallidas) queda anotado como refinamiento si
alguien quiere recuperar el 7,6 % de elisión sin pagar el 44,7 % de
grabación — techo chico: la elisión a velocidad de bloque vale ~0,5 %.

Con el censo ya limpio, el residuo del ARM7 queda ordenado: **marca
negativa 9,7 %** (tramos encabezados por filas no admitidas — ensanchar
`arm7_blq_cabe` hacia MUL/MLA y LDR/STR-R con rd≠15 es el siguiente
escalón mecánico), FIQ y presupuesto despreciables, y el resto es el
costo por paso de los bloques mismos (el lazo en C de la cola, el
`memcmp` por salto — candidatos del emisor x64 si el reparto lo pide).

**Las formas anchas (2026-08-20, la mañana siguiente).** El escalón se
apuntó con una sonda nueva antes de escribir código: el censo de marcas
negativas **por PC** (líneas `arm7 neg:` bajo `DCEMU_PERFIL_ARM`, una
cuenta por ranura — prefijo propio a propósito: las compuertas comparan
las líneas `^arm7:` entre brazos y una sonda de mecanismo no debe entrar
en esa cuenta). Y la sonda corrigió la puntería dos veces. Primero, la
foto del párrafo de arriba estaba vieja: con la memoización ya apagada
por omisión la cobertura real era **90,4 %** y la marca negativa 9,0 %.
Segundo, las doce ranuras más golpeadas (~722 k cada una — el mismo lazo)
no eran «MUL y LDR-R» a secas: un LDR con desplazamiento por registro, un
MUL, tres `LDM sp!,{pc}` (retornos) y **seis `STMFD sp!,{pc}`** — y el
STM no necesitaba forma nueva, porque el rechazo de `cabe` por «PC en la
lista» es correcto para cargas y sobreancho para almacenamientos:
guardar el PC escribe PC+12 y el bloque sigue derecho.

Lo que entró: cuatro manejadores nuevos (`d_ldr_reg`/`d_str_reg`,
`d_alu_rr_s0/s1`, `d_mul` — transcripciones de `op_transferencia`,
`op_datos` y `op_multiplicar`, con la asimetría de `op_datos` conservada:
rn y rm como PC+12, rs normal), su admisión en `arm7_blq_cabe` con las
guardas de sus parientes (destino o writeback sobre R15, afuera) y costos
exactos (3/2, 2, 4+A), la relajación del STM con PC, y los dos casos
nuevos del emisor (`ARM7_DF_LDR_REG`/`STR_REG`: llamada genérica más
`aj_toco`, porque tocan memoria y el acceso pudo caer en el archivo;
`d_mul` y `d_alu_rr` quedan en `ARM7_DF_OTRA`). Dos palancas porque son
dos efectos: `DCEMU_SIN_FORMAS_ARM=1` devuelve el intérprete anterior
entero y `DCEMU_SIN_CABE_ARM=1` decodifica las formas pero deja los
bloques con la admisión vieja — el brazo del medio del A/B.

**Compuerta verde entera, dos veces** (`herramientas/formas-arm-gate.ps1`,
sobre `14B07E12CA5479D7` y de nuevo sobre el reentrenado
`09ECCD2A14E01F21`, con los contadores del mecanismo idénticos al dígito
entre ambos binarios): tres brazos exactos al byte en los tres guests
(bmp, `.wav` de CT, 110 000 puntos de `DCEMU_CP_MS`), con los
histogramas de ejecución **idénticos** en ambos pares — nada de esto
elide, así que los pasos tenían que salir iguales, y salieron. El
efecto: cobertura 90,4 → **94,0 %** de los pasos, corridas de 18,0 a
**23,5 pasos**, despachos de bloque **−20 %** (18,8 → 15,0 M), marca
negativa 9,0 → **5,3 %**. Suites 23/23 con tres casos nuevos (las
esquinas del desplazador por registro — cantidad 0 conserva el acarreo,
33 lo vacía —, la transferencia con registro con writeback, y el STM que
guarda PC+12). El brazo `medio` salió con los contadores de bloque
idénticos al `viejo`, como debía: las formas solas no mueven un bloque.

**La tanda de las formas anchas (binario reentrenado `09ECCD2A14E01F21`,
`herramientas/formas-ab.ps1`: tres brazos, tres rondas, orden rotado).**
En Crazy Taxi todo 74 933-75 015 ms contra viejo 75 132-75 868: **−0,8 %
con rangos disjuntos**. En Sega Rally 2, 47 737-47 790 contra
47 903-48 209: **−0,6 %, también disjuntos**. DOOM neutro (su ARM es el
liviano del banco). Y el brazo del medio dice de quién es la ganancia:
solo-formas (75 503-76 188 en CT) **no se separa del viejo** — el
intérprete ancho solo no vale nada, coherente con el censo (las filas
nuevas eran el 0,4 % de los pasos), y todo el efecto es la admisión en
bloques. La bimodalidad del pad ocioso reapareció con su firma exacta
(±71 instrucciones cruzando brazos); DOOM y SR2, los árbitros inmunes,
salen al dígito idénticos en sus nueve corridas cada uno. **Las formas
anchas quedan encendidas por omisión.**

**El siguiente escalón, con su censo ya hecho: el retorno como cola
generalizada.** El residuo de 5,3 % es mayormente `LDM sp!,{pc}`. La
maquinaria ya lo soporta casi entero: la cola ejecuta por `e->fn(e)`
genérico, `pc_cambio` sale de `poner_r(15,...)`, y el encadenado lee
`r15` después de la cola — un retorno encadenaría con el bloque de la
continuación del llamador, que es el premio. Lo que falta: aceptarlo en
el descubrimiento (sin el bit S — un `LDM ^` con PC escribe CPSR y
cambia de modo, ése no entra), chequear `arm7_toco_reg` antes de
encadenar (el LDM lee memoria y pudo caer en el archivo, cosa que la
cola B/BL no puede hacer), y el costo del tramo en `ciclos_max`. No se
hizo en esa ventana por la regla de anoche: los cambios al corredor de
bloques no se apuran a fin de ventana.

**La cola de retorno (2026-08-20, la extensión de la mañana).** El
diseño de arriba, hecho con la ventana ya abierta de nuevo, más una
pieza que el censo exigió: las tres ranuras `LDM sp!,{pc}` eran
**destinos de salto directos** (retornos tempranos que saltan al
epílogo), así que **la terminal puede estar sola** — «un B a secas no
gana nada por ser bloque» dejó de ser cierto cuando apareció el
encadenado, porque una cola sola vale por el bloque al que encadena.
Tres cambios y ninguno toca al emisor: el descubrimiento acepta como
terminal el `LDM` que carga el PC **sin el bit S** (`e->b2 & 4` afuera:
con S escribe CPSR y cambia de modo — ése no encadena) y permite `n == 1`
con terminal (la marca negativa pasa a `n < MIN && !salto`; una terminal
sola no emite, cero rectas); la cola ya ejecutaba por `e->fn(e)`
genérico y `poner_r(15,…)` arma `pc_cambio`, así que corre sin tocar el
corredor — la reposición del memo también asigna PC y arma `pc_cambio`
por su lado, verificado antes de confiar en ello —; y **después de la
cola se chequea `arm7_toco_reg` antes de encadenar**, porque el retorno
lee la pila y pudo caer en el archivo de registros (un B/BL no puede, y
para él el chequeo es una rama predecible en cero). Palanca propia
`DCEMU_SIN_RETORNO_ARM=1`, porque es una pregunta nueva; no depende de
las formas anchas (es admisión, no decodificación).

**Compuerta de cuatro brazos verde entera** (binario `74362FE86542880B`,
`formas-arm-gate.ps1` extendida: con ⊃ sinret ⊃ medio ⊃ viejo): exactos
al byte los tres guests en los tres pares, `.wav` idéntico, **cuatro
histogramas de ejecución idénticos** — y el brazo `sinret` reproduce al
dígito los contadores de la compuerta de la mañana, la verificación de
que las capas componen. La escalera del efecto: cobertura
90,4 → 94,0 → **98,0 %** de los pasos, corridas 18,0 → 23,5 → **59,5**,
despachos de bloque 18,8 → 15,0 → **6,2 M** (−67 % del total), 108,7 M
de encadenados, marca negativa 9,0 → 5,3 → **1,2 %**. El residuo que la
sonda nombra es estructural: seis `MSR` de CPSR (~722 k cada uno —
cambios de modo que legítimamente cortan) y el camino de la FIQ
(29 650 — exactamente la cuenta de FIQ del banco). El rechazo por
presupuesto subió a 0,7 % como efecto lateral esperable: los bloques
crecieron y `ciclos_max` con ellos.

**La tanda del retorno necesitó dos intentos.** El primero corrió con la
máquina en uso y quedó ilegible: todo el banco 5-13 % más lento que la
tanda de las formas de la misma mañana, con deriva interna (CT bajó 8 s
entre rondas) y Outlook/Webex activos al mirar — el precedente del DSP,
aplicado igual: se descarta entero. El segundo (misma `retorno-ab.ps1`,
mismo binario `A8AD7F95EFA41D02`, carga muestreada en 0 % cuatro veces
antes de lanzar) salió legible en DOOM y CT: **la pila completa de la
mañana (formas + retorno) contra el árbol de ayer da CT −1,6 % con
rangos disjuntos** (75 582-75 915 contra 76 482-77 430) — consistente
con el −0,8 % ya medido de las formas más otro tanto del retorno. El
incremento del retorno solo: `todo` **gana las tres rondas** contra
`sinret` (−324/−437/−1123 ms) pero los rangos se solapan por 9 ms —
dirección clara, sin el estándar estricto de rangos disjuntos. DOOM
neutro en el retorno (su ARM es el liviano); SR2 ilegible también en el
segundo intento (un brazo saltó a 54 s y la ronda 3 derivó — la máquina
volvió a moverse a mitad). El retorno queda **encendido**: exactitud
doble verde, despachos −59 %, y el tiempo en dirección consistente con
~−0,8 % en CT. El cierre de pila completa también salió: la
compuerta de cuatro brazos volvió a salir verde sobre el reentrenado
`A8AD7F95EFA41D02` (contadores al dígito idénticos a los del binario
anterior) y el **cuarteto CHD** (18 Wheeler, THPS2, CvS2, THPS1) exacto
int/jit — con las cuatro capturas en los mismos hashes de la tanda de
anoche, la confirmación externa de que la pila entera del ARM7 de esta
mañana no movió un byte observable.

**El primer escalón hecho: la predecodificación del DSP (2026-08-19)**. El
paso del DSP releía y redecodificaba las 4 palabras × 128 pasos del
microprograma **por cada muestra** (1 016 M de extracciones en el banco de
CT, que corre 78 pasos reales). La tabla de predecodificación se recalcula
en la rama sucia de `aicadsp_activo()` — hereda la invalidación probada de
`dsp_pasos`: todo escritor de 0x2800-0x3BFF pasa por `aicadsp_tocar()` (el
registro común y el DMA interno), y el reset arranca sucio. **Compuerta
verde**: `.wav` de CT byte a byte idéntico entre binarios y suite `dsp` 1/1.
**Aviso de medición**: la lectura rápida con `--perf` sin reentrenar dio la
mezcla SUBIENDO (8,1 → 11,3 %) — el `.pgd` viejo sobre el `aicadsp.c` nuevo
mide la disposición, no el cambio; el veredicto es el de la tanda
reentrenada. **La tanda reentrenada (`F3D04A260D8D9D13`) corrió con la
máquina cargándose a mitad**: DOOM limpio con **−42,3 %** (su mejor delta;
consistente con «sin DSP, neutro + máquina más rápida»), CT en dirección
positiva (−29,1/−30,9 % contra −27,1 % de B.2c, dispersión 2,8 s entre sus
dos corridas), SR2 ilegible (sus brazos intérprete saltaron +11 %). Por las
reglas del árbol: **la predecodificación queda** (exactitud al byte, DOOM
confirma la neutralidad donde no hay DSP, CT apunta a ~2-3 pt) y **el número
de CT/SR2 queda pendiente de una tanda con la máquina quieta**. Refinamiento
en la manga si esa tanda lo pide: separar el flag de la tabla (0x3000-)
del de `dsp_pasos` (0x2800-).

El ARM7 sigue siendo **19,9 % de CT** después de sus tres escalones, y el
mezclador 3,1-7,2 %. Lo que la fase 4 dejó nombrado en `arm7-plan.md`:

- **ensanchar las plantillas de `arm7jit.c`**: LDM/STM, la ALU con desplazamiento
  por registro y las tres formas con acarreo de entrada con S hoy se emiten como
  llamada al manejador — el censo por fila de despacho (`DCEMU_PERFIL_ARM=1`)
  dice cuáles pesan;
- **encadenar los bloques del ARM7** (hoy cada bloque vuelve a
  `arm7_blq_correr()`), con la misma advertencia del despachador;
- **el mezclador por lotes**: entre vencimientos del reloj por eventos las
  muestras son independientes; mezclar N de una vez amortiza el techo de 7,2 %
  de CT. La lección de `DCEMU_ARCH=AVX2` (+2,1 % por tamaño de código) manda:
  nada de ensanchar el binario entero por esto.

**Gates**: los del plan del ARM7 — pasos e histograma idénticos, `.wav` byte a
byte, capturas canónicas.

### Fase G (transversal) — el compilador como palanca: clang-cl/LLVM

Puede entrar en cualquier punto después de la fase A (que le da su techo con el
reparto módulo/arena) y conviene cerrarla antes de la F, para que la adopción
entregue el binario del compilador ganador.

**Paso 0 — el toolchain, hecho el 2026-08-18.** Antes no había ninguno: ni
`clang.exe` ni `clang-cl.exe` en el PATH, ni en `Program Files\LLVM`, ni en el
registro de instalaciones, ni en el MSYS2 de `C:\dcsdk` (el entorno `clang64`
existe vacío), y el toolset de VS trae **solo** `clang-format` y `clang-tidy` —
`llvm-objdump`/`llvm-objcopy` desaparecieron de `VC\Tools\Llvm\x64\bin` en un
update, que es lo que dejó rota la receta de `DCEMU_JIT_VOLCADO`.

Lo instalado, y **dónde**: `E:\llvm\22.1.8`, **LLVM 22.1.8** (`clang version
22.1.8`, commit `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`, target
`x86_64-pc-windows-msvc`), 2,9 GB. Es el árbol del instalador oficial
`LLVM-22.1.8-win64.exe` **extraído con 7-Zip en vez de ejecutado**: sin UAC, sin
registro, sin desinstalador, fuera de `Program Files` y fuera del árbol de VS
—que es de donde ya se evaporaron dos herramientas—, y se borra con un `rm -rf`.
El instalador queda en `E:\llvm\descarga` para poder re-extraer sin volver a
bajar: 455 545 840 bytes, SHA-256
`16e5709785fef73c854646241c4a92c5cd574318d1b33c63330dd7721903e55c`, firma GPG
buena (clave `FFB3368980F3E6BB5737145A316C56D064CACBA5`, Douglas Yung) y —lo que
la propia doc de LLVM llama la verificación canónica— atestación de sigstore
válida: `gh attestation verify --owner llvm` sale 0, y el control negativo con
`--owner microsoft` da 404.

**El entorno se arma con `herramientas\llvm-entorno.ps1`** (punteado;
`-Verificar` corre las tres pruebas de abajo), y existe por una trampa que costó
dos intentos: clang-cl no trae cabeceras del sistema, las toma del MSVC
instalado, y para encontrarlo le pregunta a vswhere por la instalación más nueva
— que en esta máquina **es SQL Server Management Studio 22**, la cáscara de
Visual Studio, con versión 18.9.0 y sin compilador. El síntoma es
`'stdio.h' file not found` al compilar un hola mundo, que se lee como un LLVM mal
instalado y no lo es. El script pide la instalación que *tenga* el componente de
C++ (`-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64`, que elige
`C:\Program Files\Microsoft Visual Studio\18\Enterprise`) y entra por
`vcvars64.bat`, que fija INCLUDE/LIB y con eso clang-cl ya no pregunta nada.

**Y esa instalación es la misma con la que está configurado el árbol**, que no
estaba dicho y es del protocolo del A/B: `build/` y `build-jit/` llevan el
generador «Visual Studio 18 2026» sobre `C:\Program Files\Microsoft Visual
Studio\18\Enterprise`, MSVC 14.51.36231. O sea que la fase G compara clang-cl
contra MSVC **sobre el mismo CRT y el mismo SDK**, sin esa variable de más —
que era el riesgo de que el script eligiera una instalación de VS distinta de la
que compila el binario de control.

Lo que quedó comprobado el mismo día, cada cosa contra una respuesta que ya se
sabía:

- **`clang-cl` compila y enlaza** un ejecutable contra el MSVC importado, y corre;
- **la receta de `DCEMU_JIT_VOLCADO` vuelve a funcionar**: `llvm-objcopy -I binary
  -O elf64-x86-64` más `llvm-objdump -D --triple=x86_64 --section=.data`
  desensambla bytes elegidos a mano exactamente como se escribieron;
- **`llvm-mc` no viene, y no hace falta**: la release oficial no lo instala nunca
  (es herramienta de test). El segundo oráculo de `tests/test_jit_x64.c` es el
  **ensamblador integrado de clang**, que es la misma capa MC —
  `clang -c -x assembler` y de vuelta `llvm-objdump -d` cierra el lazo en los dos
  sentidos, texto→bytes y bytes→texto, sobre las mismas instrucciones;
- **PGO y sanitizers están**: `llvm-profdata` y los runtimes
  `clang_rt.asan_dynamic-x86_64` / `clang_rt.ubsan_standalone-x86_64`, o sea las
  dos mitades de la fase que no dependen de que clang gane el A/B. También vienen
  `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-cov`, `llvm-symbolizer` y `lldb`.

Falta lo que el paso 0 pide y sólo tiene sentido cuando haya binarios que
comparar: **hashear qué binario compila qué** — la lección del A/B que midió el
mismo binario diez veces.

**La segunda máquina (2026-09-03).** El mismo árbol —el mismo instalador
`LLVM-22.1.8-win64.exe`, verificado contra la SHA-256 de arriba, extraído con
7-Zip— vive en `C:\llvm\22.1.8` en la segunda máquina (VS 18 Enterprise, MSVC
14.51 también; aquí el generador por omisión de CMake es NMake, así que el
ejecutable queda en la raíz del directorio de compilación). Con dos raíces
posibles la ruta dejó de estar escrita en los guiones: `herramientas\banco.ps1`
prueba `DCEMU_LLVM`, `C:\llvm\22.1.8` y `E:\llvm\22.1.8` en ese orden, y
`llvm-entorno.ps1` y `pgo.ps1 -Clang` le preguntan a él. Dos cosas que costaron
tiempo aquí y no allá: el CDN de releases de GitHub entrega ~60 KB/s por conexión
desde esa red (la línea da 2,6 MB/s contra Cloudflare), así que el instalador se
bajó por rangos en paralelo; y el binario MSVC de esta máquina fue el primero en
disparar la guarda del ICF de `jit_iniciar()` — el toolset 14.51 pliega — con lo
que los dos enlaces van con `/OPT:NOICF` (ver el invariante en CLAUDE.md).

**Paso 1 — el binario clang, hecho el 2026-08-20.** El CMakeLists ganó una rama
clang (detectada por `CMAKE_C_COMPILER_ID`, porque `MSVC` es TRUE también con
clang-cl): `-ffp-contract=off` y `-fno-strict-aliasing` globales —el primero
porque el `/fp:precise` de clang, a diferencia del de MSVC, contrae a FMA
dentro de una sentencia, y la conformidad se compara al bit; el segundo porque
los makefiles siempre lo pasaron y el type-punning de `mem.c` está nombrado
como el riesgo—, ThinLTO en lugar de `/GL`+`/LTCG` (exige `lld-link`, con
guarda en el configure), y el esquema LLVM de PGO (`-fprofile-generate` →
`llvm-profdata` → `-fprofile-use`, perfil propio `dcemu-jit-clang.profdata`,
banco compartido vía `pgo.ps1 -Clang`). Se configura con
`-G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_LINKER=lld-link` en `build-clang/`.

Compilar costó exactamente cinco arreglos, todos deuda real que MSVC dejaba
pasar: cuatro llamadas sin prototipo (C4013 como aviso en MSVC, error en clang)
— `reloj_ms()` en `graficos.c` sin `tmu.h`, `traza_ta_resumen()` en `traza.c`
sin `graficos.h`, y `shader_activo()`/`volumen_px()` usadas antes de sus
definiciones estáticas — y **una deriva del arnés que el enlazador de C nunca
iba a atrapar**: el doble de `watchpoint_escritura()` en `tests/dobles.c`
conservaba la firma vieja de dos parámetros; la real ganó `const void * valor`
y MSVC lo enlazaba igual (inofensivo por ser un no-op, pero es exactamente la
deriva que los dobles prometen no tener). Con eso: **las 23 suites verdes y la
conformidad SingleStepTests al dígito — 113 191 ok, 0 fallan, los mismos
totales que MSVC** — o sea que `-ffp-contract=off` cumplió su parte.

**El expediente del ICF, que es la lección de la fase.** La primera sonda de
exactitud dio captura y 1000 puntos de control **idénticos** con los contadores
del traductor **distintos**: clang traducía bloques más largos (30,2 contra
26,4 instrucciones en CT), atravesaba palabras que no son instrucciones, y en
DCDoom los rechazos por verificación salían 12,0 M contra 4,6 M — estable al
dígito consigo mismo, o sea sistemático, no ruido. La bisección con
`DCEMU_JIT_PLANTILLAS` cayó en la 46 (BRA, el primer par de rama, que es el
primero que clasifica la ranura), y el mecanismo lo nombraron los PDB:
**`/OPT:ICF` pliega funciones byte-idénticas en una sola dirección, y
`jit_plantilla_de()` clasifica cada palabra comparando el puntero de su
manejador** — el riesgo que la documentación de ICF advierte con su nombre.
lld-link pliega `nop` con `NOIMP` (con el logging compilado fuera, NOIMP *es*
un nop) y `shal91` con `shll94` (operaciones idénticas); MSVC no pliega
ninguno de los dos (sólo `ocbwb141`/`ocbp140`, que no tienen plantilla, así
que su clasificación es la verdadera). Bajo clang una ranura basura `0000`
clasificaba como NOP y el par se formaba donde MSVC cortaba. **Exacto de
casualidad** — sólo se pliega código idéntico, por eso las capturas no lo ven —
pero rompe dos doctrinas del árbol: los conteos invariantes entre
compilaciones, y «el código del arena es byte-idéntico bajo cualquier
compilador», que es lo que mantiene constante la mitad JIT del A/B. El arreglo:
**la rama clang enlaza con `/OPT:NOICF`** (MSVC conserva su configuración
canónica), más una guarda en `jit_iniciar()` que NOMBRA el plegado si vuelve
(clasificar `0x0000` con plantilla, o `SHAL`≡`SHLL` en punteros), porque el
síntoma —conteos distintos con captura exacta— no se parece en nada a la
causa. Verificado tras el arreglo: **los cuatro casos de la sonda salen
idénticos al dígito en todos los contadores** (bloques, bytes emitidos,
enlaces, épocas, rechazos), DCDoom a 2/5/10 s y CT a 10 s.

**Paso 2 — PGO propio, compuerta y tanda, mismo día. Clang gana en los tres
guests con rangos disjuntos.** El banco es el mismo de `-Jit` por la misma
razón que el banco es uno (`pgo.ps1 -Clang`: cada guest dos veces, una por
forma, DCDoom ×7, SR2 adentro), recogido con `LLVM_PROFILE_FILE` por corrida y
fundido con `llvm-profdata merge -weighted-input` — el equivalente exacto de
`pgomgr /merge:N`, sin el problema del `/clear` porque escribe el `.profdata`
entero de una vez. El control MSVC se recompiló del mismo fuente (la guarda
ICF y las declaraciones nuevas van en los dos) y se reentrenó, como toda
generación. Binarios: **clang `6D0D8B6DDE5E72A8`, MSVC `C1F4DC470406BC5C`**.

La compuerta, verde entera antes de cronometrar nada:

- los tres guests por tres brazos (clang-int, clang-jit, msvc-jit): captura
  **byte a byte idéntica entre los tres** y `DCEMU_CP_MS` **idéntico punto por
  punto** — 20 000/40 000/60 000 puntos (`herramientas/clang-compuerta.ps1`);
- el `.wav` de Crazy Taxi **byte a byte igual entre compiladores** (el
  mezclador del AICA es coma flotante y era el riesgo restante);
- el trío CHD + THPS1 bajo el binario clang: int≡jit exacto en los cuatro,
  180 000 puntos idénticos, las mismas capturas canónicas
  (`75C732EB…`/`411393D6…`/`B779DACF…`/`C089D90F…`).

La tanda (`herramientas/clang-ab.ps1`: 4 corridas por brazo y guest,
calentamiento por binario descartado, orden alternado, máquina muestreada en
0 % antes de lanzar):

| guest | clang (rango) | MSVC (rango) | Δ medias |
| --- | --- | --- | --- |
| DCDoom 35 s | 23 303–23 601 | 24 062–24 306 | **−2,7 %** |
| Crazy Taxi 180 s | 74 421–75 387 | 78 013–78 532 | **−4,3 %** |
| Sega Rally 2 60 s | 48 425–49 611 | 49 634–50 376 | **−2,3 %** |

Tres rangos disjuntos (SR2 por 23 ms, pero disjunto). Las marcas de esta
generación: DOOM 1,49×, CT 2,40× (75,0 s el banco de 180), SR2 1,23×. Los
totales del traductor salieron **al dígito entre compiladores también durante
la tanda** — DOOM y SR2 idénticos en las 16 corridas; CT con su bimodalidad
de pad documentada, presente en los dos brazos por igual — o sea que la
invariancia que el arreglo del ICF restauró aguantó el protocolo entero. La
ganancia es coherente con el techo medido: el arena es idéntico por
construcción, así que el −2,3/−4,3 % sale entero de la mitad C (intérprete
residual, ayudantes de memoria/MMU, despachador, lazo del ARM7, mezclador), y
el guest que más C corre (CT, con el ARM7 y el mezclador más pesados del
banco) es el que más gana.

**Consecuencias.** La serie histórica de absolutos se corta aquí, como manda
la regla de cada generación: los números de esta tanda son la línea base
nueva y no se comparan contra los de `A8AD7F95…` sueltos. La adopción — qué
binario entrega la fase F — queda para la fase F, que era el orden previsto
(«conviene cerrarla antes de la F, para que la adopción entregue el binario
del compilador ganador»).

**Paso 3 — los sanitizers, mismo día: el banco entero sale LIMPIO bajo UBSan
y bajo ASan.** `DCEMU_SAN=ASAN|UBSAN` en el CMakeLists (solo rama clang;
`build-ubsan/` y `build-asan/`, con `DCEMU_LTCG=OFF` y sin PGO — este binario
se escucha, no se mide). Un detalle de enlace cada uno: el runtime de UBSan
viene compilado con CRT estático, así que ese build va entero con
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`; el de ASan es dinámico y su DLL
se copia junto al ejecutable en el post-build. Las corridas: DOOM 20 s
int+jit, CT 40 s jit con teclas, SR2 60 s jit — **cero reportes en las ocho**,
con la sonda verificada en los dos sentidos (los `__ubsan_handle_*` y
`__asan_init` están en los PDB: el silencio es limpieza, no un sanitizer que
no entró). Que UBSan no encuentre nada cuadra con cómo está escrito el árbol
— la aritmética del guest va en DWORD, donde el desborde es definido — y deja
la red tendida: cualquier fase de emisión futura puede correr su compuerta
bajo `build-asan` con solo cambiar `-Exe`.

**Qué puede comprar, y su frontera exacta.** El compilador solo mueve el C
compilado: **el código del arena es byte-idéntico bajo cualquier compilador**
(`jit_x64.c` emite los mismos bytes), lo que además mantiene constante la mitad
JIT del A/B. Los candidatos son el intérprete residual, los ayudantes de
memoria/MMU, el despachador, el lazo C del ARM7 y el mezclador — es decir, el
reparto módulo/arena de la fase A es el techo, medido antes de compilar nada.

**Las reglas que hacen creíble el A/B**, que es de dos binarios por construcción
(el caso `DCEMU_SIN_ALINEAR`: la contaminación de layout no se puede evitar, se
carga con ella y se mira la dispersión):

- **PGO se rehace en el esquema de clang** (`-fprofile-generate` →
  `llvm-profdata` → `-fprofile-use`), mismo banco y mismos pesos (DCDoom ×7,
  el banco del JIT con sus dos formas); ThinLTO en lugar de LTCG. Un clang sin
  perfil contra un MSVC con perfil no mide el compilador, mide el perfil.
- Alternado dentro de una tanda, orden alternado, calentamiento por guest, hash
  de ambos binarios, dispersión reportada — el protocolo entero.
- **Exactitud primero, con un riesgo con nombre**: la contracción de FP (fusionar
  `a*b+c` en FMA) y las opciones `/fp` pueden mover resultados del intérprete
  FPU al bit. `-ffp-contract=off` y semántica estricta antes de comparar nada;
  las compuertas son las de siempre — capturas canónicas, `.wav`, totales al
  dígito.
- Si clang gana, la serie histórica de absolutos se corta ahí, como en cada
  generación de emisión: las comparaciones entre generaciones van por tandas con
  brazo de control, nunca por absolutos sueltos.

**Las herramientas valen aunque se siga compilando con MSVC**, y son la mitad
barata de la fase:

- `llvm-objdump`/`llvm-objcopy` reponen la receta de `DCEMU_JIT_VOLCADO`
  (verificado en el paso 0, prueba 2 de `llvm-entorno.ps1 -Verificar`);
- el **segundo oráculo** de `tests/test_jit_x64.c`, que hoy compara contra las
  codificaciones de Intel transcritas a mano: sin `llvm-mc` en la release, el
  lazo es el del paso 0 — el ensamblador integrado de clang (la misma capa MC)
  produce los bytes esperados, o `llvm-objdump` desensambla los emitidos;
- ASan/UBSan vía clang-cl: una corrida de las compuertas de exactitud bajo
  sanitizers por fase de emisión — red de corrección para `jit.c` y el manejo
  del arena, nunca cronómetro.

**Gate**: exactitud completa en los tres guests + el trío CHD con el binario
clang; tanda alternada con ambos binarios hasheados; veredicto por guest con
dispersión. Si pierde o empata, el expediente queda y las herramientas se
quedan igual.

### Fase F — el parque entero y la adopción por omisión (cierra el plan)

La fase 8 del maestro, con el material nuevo adentro:

1. **El barrido**: 139 demos + los 14 juegos del árbol + el trío CHD,
   `DCEMU_JIT=2` contra corrida de control, mismas reglas de VMU/render/piso de
   ruido de CLAUDE.md, veredicto serial antes que hash.

**F.1, mitad de demos — hecha el 2026-08-20, y perfecta.** El conductor es
`herramientas/barrido-jit.ps1`: tres brazos sobre el binario MSVC nuevo
(`C1F4DC470406BC5C`) — int-a, int-b (el piso de ruido del mismo día, la regla
del 40-de-139) y jit-a (`DCEMU_JIT=2`) —, los tres con `DCEMU_RTC_FIJO` y la
VMU fresca por demo de `barrido.ps1 -Vmu`. Resultado, sobre los 131 binarios
del parque:

- **piso de ruido: 0 de 131** — el piso de 40 que documentaba CLAUDE.md era
  entero el reloj del host en el menú del BIOS más el estado de la tarjeta;
  con las dos fuentes clavadas el parque entero es determinista corrida a
  corrida, hilos y `rand()` incluidos (a 8 s emulados);
- **señal int↔jit: 0 de 131** — el traductor byte a byte idéntico al
  intérprete en todas las capturas;
- **veredictos serial distintos: 0** sobre el parque entero, que es el
  veredicto que manda para las demos de consola.

Nota del RTC clavado que el usuario observó en vivo: con `RTC_FIJO=10⁹`
(≈1981, fecha inválida) las demos de consola que salen al boot ROM aterrizan
en la **pantalla de poner la hora** en vez del menú. Es la conducta esperada
— el precedente es el barrido canónico del 08-07, corrido con `RTC_FIJO=1` y
«la fecha rara es el RTC fijo» — y no daña el A/B: los tres brazos ven la
misma pantalla, y el piso medido en 0 demuestra que además es determinista.

**F.1, mitad de juegos — corrida el mismo día** (`herramientas/juegos-jit.ps1`):
todo el material comercial disponible — las 4 imágenes que quedan en `roms/`
más los ~44 CHD de primer disco de `E:\Juegos\roms\dreamcast`, que ya incluye
una docena nunca barrida (Jet Set Radio, MSR, Sonic Adventure 1/2, Soulcalibur,
Space Channel 5, Power Stone 1/2, SF Alpha 3, SFIII 3rd Strike, Mortal Kombat
Gold, Re-Volt, Rush 2049, Tech Romancer…) y dos segundos contenedores de
baranda (Crazy Taxi y Virtua Tennis en CHD) — int contra `DCEMU_JIT=2`, 20 s
emulados, captura + `DCEMU_CP_MS` punto por punto, discos 2+ fuera a
propósito (tope explícito). **Veredicto: 46 de 48 exactas** — captura byte a
byte y todos los puntos al dígito, la docena nunca barrida incluida. Los dos
restantes, cada uno con su nombre:

- **Marvel vs. Capcom 2**: captura idéntica y ciclos idénticos, un punto
  corrido (8550) con la firma exacta del expediente ya entendido al ciclo —
  la elisión de `gen_corte` tras instrucciones sin ciclos con `reintentar`
  armado por la propia entrada (una pasada de contabilidad corrida, guest
  intacto, abierto a propósito). Consistente con lo documentado, nada nuevo.
- **Mortal Kombat Gold: COLGADO en los DOS brazos** — no es del traductor, es
  un expediente de compatibilidad nuevo. Primera evidencia (`--traza-mem`):
  a los ~8000 ciclos el PC está ejecutando **basura (NOIMP) en el espejo
  8D/AD de la RAM** (`0x8D00006C`, PR `0xAC00E0B2`) con tormenta de `TRAPA`
  #17/#23 hacia `0x8C00F500` — la forma recurrente del árbol: algo del
  arranque que dcemu aceptó sin hacerlo. Cero segundos emulados avanzados en
  3 min de pared. Es material para una sesión de compatibilidad, no de esta
  fase.

Con eso la **F.1 queda cerrada entera**: parque de demos perfecto (piso 0,
señal 0, serial 0) y el material comercial 46/48 exacto con los dos residuos
nombrados y entendidos (uno documentado de antes, uno nuevo y ajeno al
traductor).
2. **La adopción**: `DCEMU_JIT=ON` deja de ser build aparte — un solo binario,
   traductor encendido por omisión, el intérprete como palanca de aislamiento.
   Consecuencias que hay que decidir ahí y no antes: el `.pgd` único (el del JIT
   pasa a ser el del árbol, con su banco de dos formas), y que todas las palancas
   `DCEMU_JIT_*` ya viven en el binario por diseño, así que la regla «el A/B
   corre sobre una sola imagen» se conserva sola.

**F.2 — HECHA el 2026-08-20, decidida por el usuario: clang + traductor por
omisión.** Lo que cambió, cada pieza con su porqué:

- **`jit_iniciar()` invierte la omisión**: sin variable corre el traductor;
  `DCEMU_JIT=0` es la palanca de aislamiento; `1`/`2` conservan su sentido,
  así que toda receta vieja con `=2` significa lo mismo. El costo que la
  regla le cobra al árbol: **todo guion cuyo brazo de control borraba la
  variable pasó a poner el `0` explícito** — siete guiones corregidos
  (`chd-exactitud`, `exactitud-lote`, `chd-barrido`, `piso-expediente`,
  `clang-compuerta`, `barrido-jit`, `juegos-jit`); sin eso, sus brazos «int»
  habrían corrido el jit en silencio, que es exactamente la forma de fallo
  recurrente de este árbol.
- **`DCEMU_JIT=ON` es la omisión del CMakeLists**; OFF queda para el binario
  de control sin recompilador. El `.pgd`/`.profdata` siguen por binario como
  estaban (la selección ya estaba keyed en la opción).
- **El binario que se entrega es el de clang** (`build-clang\dcemu.exe`),
  fase G mediante; el MSVC (`build-jit/`) queda como control entre cadenas.
  Binarios tras el cambio: clang `BCEF41ACDEC2761C`, MSVC `B31F9B9982D79E8C`
  (recompilados con el perfil vigente — el cambio es del init, frío; el
  próximo ciclo de medición reentrena como siempre).
- **Verificado en las tres caras** (`herramientas/adopcion-verificar.ps1`):
  sin variable el traductor se engancha y su captura + 20 000 puntos de
  control salen **idénticos al brazo jit de la compuerta**; con `0` no hay
  una sola línea `jit:` — el intérprete solo.

Con esto el plan queda **cerrado**: fases 0-6 del maestro, A-E, G y F
completas. Lo abierto vive en sus expedientes: MvC2/Shenmue (entendido,
abierto a propósito), Mortal Kombat Gold (compatibilidad, no traductor), y
los márgenes nombrados del ARM7 (giro puro, costo por paso del corredor).

### Post-plan — la verificación por generación de onda del ARM7 (2026-08-20/21)

El primer margen del «costo por paso del corredor», atacado la misma noche de
la adopción. El corredor de bloques re-verificaba las palabras del destino
**en cada encadenado** — un `memcmp` de hasta 52 bytes, 117,6 millones de
veces en 30 s de CT (108,7 M de encadenados más las entradas por
`intentar()`). El reemplazo vive en un contrato que el árbol ya tenía:
`onda_gen[]`, el contador por página de 1 KB que **todo** escritor de la RAM
de onda mantiene (aica.h lo documenta; el ARM, el SH-4 y el G2-DMA por
`mem.c`, el DMA interno del AICA y el DSP), y del que la memoización de
barridos ya dependía. El sello del bloque guarda las generaciones de sus (a
lo sumo dos) páginas; la verificación pasa a ser dos comparaciones, y como
**las páginas con código casi nunca se escriben, el sello sobrevive a los
lotes** — la misma separación código/datos que la rejilla fina le dio al jit
del SH-4.

El camino hasta ahí dejó una medición que vale sola: **la v1 (generación
global invalidada por frontera de lote + escritura del ARM) solo elidía
58,9 %**, y el refinamiento «no invalidar por escrituras de registro» no
movió el censo — el invalidador dominante era la **frontera del lote** (512
ciclos, una por muestra: 1,3 M por 30 s). La v2 por páginas la elimina y
elide **99,99 %: 16 045 memcmp contra 117 626 602** en el mismo banco.

Los límites, heredados y documentados en `arm7.c`: una escritura a
`sound_mem` que no marque (la suite) no invalida — igual que con la
memoización y con la misma baranda —, y el agujero teórico de todo contador
que envuelve (2³² escrituras exactas de una página entre dos visitas).
`DCEMU_SIN_VERIF_ONDA=1` es el brazo del A/B.

**Compuerta verde entera** (binario `322C271F1B1D9A59`,
`herramientas/verif-onda-gate.ps1`): capturas, `.wav` de CT y 110 000 puntos
de control idénticos entre brazos, histogramas del ARM idénticos, y los
conteos de bloques/encadenados al dígito — el mecanismo no elide pasos, solo
memcmps, y la compuerta lo atestigua. Suites 23/23.

**La tanda** (`herramientas/verif-onda-ab.ps1`, binario canónico clang
reentrenado `F7DBB92730EFF1D8`, 4 corridas por brazo, orden alternado,
máquina en 0 %):

| guest | con (rango) | sin (rango) | Δ medias |
| --- | --- | --- | --- |
| DCDoom 35 s | 23 301–23 467 | 23 598–23 749 | **−1,1 %** |
| Crazy Taxi 180 s | 73 123–74 720 | 75 573–76 238 | **−2,7 %** |
| Sega Rally 2 60 s | 47 617–48 902 | 49 174–49 502 | **−1,6 %** |

**Tres rangos disjuntos** — la mayor ganancia del ARM7 desde la
predecodificación, y en los TRES guests (el memcmp pesaba también donde el
ARM es el 10-13 %). DOOM y SR2 al dígito en las 16 corridas; CT con su
bimodalidad de pad, cruzando brazos como siempre. Queda **encendida por
omisión**; `DCEMU_SIN_VERIF_ONDA=1` es el brazo del A/B. Las marcas de esta
generación: DOOM 35/23,4 = **1,50×**, CT 180/73,8 = **2,44×**, SR2 60/48,5 =
**1,24×**.

### La fase C del ARM7 (el giro puro), cerrada por censo: no hay material

La pregunta que la fase dejó escrita — «un lazo cuya vuelta deja los
registros idénticos es un giro puro y el saldo se puede consumir de un
golpe» — nunca se había medido, y la sonda la contesta en una corrida: en
`arm7_blq_correr`, bajo perfil, instantánea de r0-r14+CPSR a la entrada y
comparación cada vez que el encadenado cierra el ciclo volviendo a la base
(`arm7 giro:` en el resumen; tras una impura se re-toma la instantánea, así
que un lazo con preámbulo cierra impuro una vez y puro las demás — la sonda
no puede subestimar por eso). El veredicto, en los tres guests:

| guest | cierres de ciclo | vueltas puras | pasos puros |
| --- | --- | --- | --- |
| Crazy Taxi 30 s | 6 048 116 | **2** | 18 |
| DCDoom 20 s | 130 007 | **0** | 0 |
| Sega Rally 2 30 s | 169 000 | **0** | 0 |

**Todos los ciclos calientes mutan registros en cada vuelta** — el barrido
de canales avanza su índice, y hasta los lazos de sondeo dejan algo distinto
por vuelta. La fase C queda descartada con la evidencia en la mano, que es
mejor final que implementarla y medir cero: el mecanismo habría necesitado
detección de ciclos, contabilidad de saldo al ciclo exacto y su propia
compuerta, para consumir 18 pasos por medio minuto. La sonda queda en el
árbol (costo cero sin perfil), como todo censo que contesta una pregunta que
alguien puede volver a hacer.

### El lote B.3, medido y revertido: la serie B queda cerrada por medición

El censo de la frontera con el binario adoptado seguía mostrando 12-20 % de
entradas terminadas «sin plantilla», y el ponderado señalaba blancos
concentradísimos: **`0x4717 LDC.L @R7+,GBR` con el 53,3 % de las entradas
cortadas de DOOM** (CT: `FDIV` en ranura 19,7 %; SR2: una palabra `FD8F` no
identificada, 40 %). El de DOOM era la fila perfecta — GBR no es SR, fila de
manejador común, el patrón exacto del tercer lote — y se implementó, con
compuerta verde entera (captura y 275 000 puntos int≡jit en los tres
guests), «sin plantilla» de DOOM cayendo de 20,4 % a **9,4 %**, ciclo de PGO
y tanda (`herramientas/b3-ab.ps1`, palanca natural
`DCEMU_JIT_PLANTILLAS=149`, binario `35A9ED02664D1EA4`).

**La tanda salió neutra en los tres guests, con dirección leve en contra en
DOOM** (rangos solapados; con 23 466-23 618 contra sin 23 345-23 508, 3 de 4
rondas para el brazo viejo). La primera pista ya estaba en los conteos: las
entradas al despachador **no bajaron** (88 463 482 → 88 447 756, −16 k de
88 M) — o sea que los cortes «sin plantilla» no eran viajes al despachador,
eran **salidas enlazadas**, y la fila solo ahorraba el salto encadenado con
su comparación de clave — que las tres mediciones del árbol ya habían
declarado barato («el viaje al despachador no es el costo») — al precio de
una entrada más en el barrido lineal de plantillas y de correr LDC.L por el
envoltorio de manejador.

### Dónde vive el tiempo hoy (2026-08-21, binario `563A0A95EA78A42F`)

La pregunta que quedaba tras agotar los márgenes nombrados, contestada con
dos instrumentos que se corrigen entre sí:

**La cota por aislamiento miente por diseño y quedó medida mintiendo.** El
A/B `--sin-aica` (`herramientas/sonido-cota.ps1`, con el guardián de
instrucciones por brazo) dio: DOOM y SR2 **inválidos** — el guest diverge
sin chip (DOOM: −17 % de instrucciones, entradas ×1,5, rechazos ×2,4, y sale
MÁS LENTO; SR2: entradas ×3,1 en un lazo de sondeo) — y CT aparentando
~48 % (73,9 → 37,8 s con −6 % de trabajo). Pero el `--perf` dice 23,6 %: la
diferencia es que **quitar el chip también quita la conducta de sondeo del
guest** — las entradas al despachador caen de 627 M a 95 M (35,0 → 215
instrucciones por entrada), o sea que la mitad de lo que la cota atribuía al
AICA era el guest fragmentando bloques para sondearlo. Regla nueva pagada:
**el interruptor de aislamiento mide el subsistema MÁS la reacción del guest
a su ausencia**, y sin el guardián de instrucciones la cota se habría leído
como techo real.

**El reparto de verdad** (CT 120 s bajo `--perf`, el traductor por omisión):

| dónde | ms | % del tiempo real |
| --- | --- | --- |
| AICA (mezcla) | 5 071 | **10,5 %** |
| AICA (ARM7) | 6 243 | **13,0 %** |
| bloque periódico | 12 248 | 25,5 % |
| cuadro (render) | 3 755 | 7,8 % |
| TA (store queue) | 1 019 | 2,1 % |
| resto (la ejecución SH-4) | 30 883 | 64,4 % |

Consecuencias para los próximos escalones: el **mezclador es un techo de
10,5 %** nunca atacado (64 canales × 44 100 con ADPCM/EG/FEG/LFO por canal —
el candidato a vectorizar por archivo que la nota de `DCEMU_ARCH` dejó
prevista, con la advertencia de medirlo por archivo y no encendiendo AVX
global); el ARM7 quedó en 13,0 % tras la semana entera de trabajo, con sus
dos residuos de baja probabilidad; y el margen grande sigue siendo la
ejecución SH-4 misma (64,4 %), donde la serie B acaba de medir que las
plantillas ya no son el camino.

### El mezclador, por fin medido por dentro — y su primer escalón: el corte del DSP

La ventana del 2026-08-21 («sigue hasta las 9 mejorando performance») atacó
el techo de 10,5 % con la sonda antes que el código. El sub-reparto
(`--perf` ganó `de eso canales` / `de eso DSP+EF`, muestreados con
`PERF_MARCA_MUESTRA` — dos QPC por muestra serían el instrumento comiéndose
lo medido — más el censo de canales activos):

- **canales: 1,2 % — hay 2,4 activos de 64.** El lazo de 64 llamadas por
  muestra NO es el costo; la lista de activos que uno imaginaría no tiene
  qué comprar.
- **DSP+EF: 11,1 % — el 92 % del mezclador es el DSP.** Crazy Taxi programa
  78 pasos de reverberación y el lazo corría los 128 siempre.

**El corte del programa** (`DCEMU_SIN_DSP_CORTE=1` lo apaga): el lazo corre
hasta el último paso con efecto observable — twt/iwt (TEMP, MEMS), mwt
(memoria), **mrd** (cuenta porque `dsp_memval` es un anillo de 4 que cruza
muestras: el iwt del paso 1 de la muestra siguiente consume lo que el mrd
del 127 trajo), ewt (EFREG), frcl/yrl/adrl (registros persistentes) —; lo
que sigue solo mueve `acc` y `shifted`, que mueren con la muestra. En CT el
corte real cae en el paso 85 (bits con efecto más allá de los 78 densos; el
resumen de traza lo imprime): corren 86 de 128. Compuerta verde — **el
`.wav` de la reverberación byte a byte entre brazos**, capturas y 110 000
puntos —, ciclo de PGO y tanda (`205A022D8BB91680`): **CT −1,7 % con rangos
disjuntos** (72 458-73 345 contra 73 777-74 548), DOOM y SR2 neutros como
corresponde (no programan el DSP). La parte del DSP bajó de 11,1 % a 9,2 %
y la marca de CT quedó en **2,47×**.

**El segundo escalón — el cuerpo rápido por clase de paso — se midió NEUTRO
y se revirtió.** El censo daba 63 de 86 pasos «MAC simple» y el cuerpo
especializado (mitad del tamaño: sin memoria, sin IWT/EFREG, entrada solo si
XSEL, `shifted` solo con TWT) pasó su compuerta con el `.wav` byte a byte —
y la tanda salió **al milisegundo** en CT (medias 72 941 contra 72 940,
rangos solapados de lleno), el único guest donde el mecanismo actúa. La
explicación que queda escrita: **el patrón de ramas por paso es fijo entre
muestras** — el predictor se aprende la secuencia entera de 86 pasos, así
que el cuerpo nunca estuvo limitado por ramas; el costo real es la cadena
MAC con sus cargas, que ningún cuerpo corto evita. (DOOM salió disjunto a
favor, pero ahí el mecanismo es **inerte por construcción** — sin programa,
`activo()` corta antes del lazo y la clase nunca se lee: es la vara de
cuánta deriva puede fabricar el ambiente en cuatro pares, y una advertencia
para leer tandas.) Revertido; el censo queda en el resumen de traza.

**El tercer escalón — la máscara de canales activos** (2,4 de 64 en CT, el
1,2 % de canales que era casi todo llamadas a canales apagados): un bit por
canal mantenido en los tres únicos sitios que escriben `activo` más el
reset, recorrido por bit más bajo primero (el orden del lazo de siempre).
La suite solo lee `activo` — diez aserciones que ejercitan la coherencia.
`DCEMU_SIN_MASCARA_CANALES=1` es el brazo. Compuerta verde entera; la tanda
(`88D6E76EB962464A`): **CT −0,8 % y DOOM −0,6 %, los dos con 4 de 4 rondas
a favor y rangos solapados** — dirección clara sin el estándar estricto,
el veredicto clase retorno —; SR2 ilegible. DOOM y SR2 al dígito en las 16
corridas; una corrida `sin` de CT trajo un total 600 k instrucciones abajo
(más que la trimodalidad documentada del pad — un tropiezo de esa corrida,
los árbitros intactos). Queda encendida. La foto final del `--perf`: canales
1,2 → **0,7 %**, mezcla 9,7 %, AICA total 23,0 %.

**El cierre de la ventana, por la sonda de tirones** (la media no ve un
cuadro largo): sobre el binario final, CT 120 s con `DCEMU_SONDA_CUADROS=1`
da **7199 cuadros medidos, p50 4,87 / p90 8,95 / p99 9,62 / máx 13,48 ms —
cero por encima de 16,7**. La noche entera de mecanismos no dejó un solo
tirón, y el peor cuadro trabaja 10,6 ms de sus 16,7. El control MSVC quedó
reconstruido y reentrenado con el fuente completo de la noche, así el
próximo A/B entre cadenas no arrastra una generación vieja.

**El cuarto escalón quedó CORRIDO y ADENTRO (2026-08-22) — el encadenado
emitido del ARM7.** El perfil de la noche decía que el corredor cruzaba al
lazo C **cada 3,4 pasos** (367,8 M de pasos / 108,7 M de encadenados en
30 s de CT — granularidad 10× más fina que la del despachador SH-4, así que
los cuatro veredictos «el viaje al despachador no es el costo» no aplicaban
por analogía), pagando por cruce dos llamadas indirectas, presupuesto, la
búsqueda de ranura y el sello de onda; techo estimado **2-4 % de CT**. La
cirugía del emisor se hizo con las reglas del diseño intactas: los ciclos
del cuerpo se comprometen **antes** de la cola (la regla que la memoización
exige), la cola B/BL corre emitida — condición armada de las banderas del
anfitrión, BL escribiendo r14, y el B hacia atrás llamando a
`arm7_memo_borde` con sus tres desenlaces (reposición que sale al C con PC
dinámico, grabación armada que contabiliza y sale, camino normal con r15
constante) — y la costura `aj_cadena` verifica base, longitud, presupuesto
con los pasos aún no comprometidos y el doble sello de onda por página
antes de saltar a la **entrada interna** del sucesor (la etiqueta
post-prólogo: la cadena corre en el mismo marco y no crece la pila).
`arm7_blq_ult_pasos` pasó a acumulador (`+=`, el prólogo lo pone en cero)
para que los pasos crucen la cadena entera, y toda salida al C cae en
frontera de instrucción con el estado consistente — el chequeo extra de FIQ
del despachador ve exactamente lo que veía. Palanca `DCEMU_SIN_CADENA_ARM`;
suites 23/23; compuerta entera verde (`1AAB2099FB59F538`: capturas
idénticas, 110 k puntos de control, `.wav` de CT byte a byte con y sin).
Tanda sobre el canónico reentrenado (`03890F37CB1593C9`, carga 0 %):
**CT −1,8 % con rangos disjuntos y 4/4 pares** (92 303-93 437 contra
93 940-94 851 ms), **SR2 −0,7 % con 4/4 pares y solape** — dirección clara,
el estándar débil —, **DOOM neutro** (2/4; casi no encadena). El guardián
de instrucciones verde: totales idénticos entre brazos dentro de cada par,
con la bimodalidad del pad de CT apareada (dos totales, los dos brazos con
el mismo en cada par). Del techo de 2-4 % se cobró la mitad, y con eso el
único trabajo por cruce que queda en C es el que no se puede plegar: el
borde de la memoización cuando repone. Tras la adopción, el control MSVC
quedó reconstruido y reentrenado del mismo fuente (`4E5F916DFFF529A7`),
para que el próximo A/B entre cadenas no arrastre una generación vieja.

**El microprograma del DSP, emitido (2026-08-23) — el tercer emisor del
árbol.** El perfil fresco tras el encadenado dio el blanco: DSP+EF ≈ 6 s de
83 en CT bajo `--perf` (~14 ns por paso, ~52 ciclos, para un cuerpo cuyo
piso aritmético — la cadena MAC — es 6-8), y la lección del cuerpo rápido
ya nombraba dónde estaba el costo: **las cargas** (los ~24 campos de
`dsp_tabla` por paso, los índices del anillo, el coeficiente), no las
ramas, que el predictor se aprende. El programa es fijo entre escrituras
(la regla de `dsp_sucio`: todo escritor de 0x2800-0x3BFF pasa por
`aicadsp_tocar()`), así que `aicadspjit.c` lo emite **una vez por
reconstrucción** y corre una vez por muestra: los campos como inmediatos,
ysel/shift/fuente resueltos al emitir, el coeficiente y MADRS[masa] como
constantes (viven en el rango que ensucia — la misma regla por la que
RBP/RBL quedaron horneados también para el cuerpo C), los registros que
sobreviven de un paso al otro (acc, shifted, frc, y, dec) en registros
callee-saved del anfitrión, `empacar`/`desempacar` como llamadas y la
marca de onda en línea (dir es par: dos bytes nunca cruzan la página de
1 KB, el camino largo del macro no puede darse). El estado se movió a un
solo bloque (`aicadsp_est`, aicadsp.h) para direccionarlo desde un
registro base, con dos derramaderos para lo que una llamada pisa. El
emisor jit_x64 ganó sus dos primeras instrucciones de 64 bits con datos
(`movsxd`, `imul64`: el producto MAC es 24×13 = 37 bits). Las elisiones
son exactas por construcción: `entrada` y `shifted` son locales del paso
en el cuerpo C (siempre asignadas antes de usarse), así que saltearlas
cuando ese paso no las consume no cambia nada. La baranda es triple: la
suite nueva de equivalencia (12 programas de 28 pasos con las cuatro
palabras **crudas al azar**, 10 muestras con MIXS/EXTS deterministas,
lazo C contra emitido comparando el estado ENTERO — los 2 MB de RAM de
onda y las generaciones de página incluidos — verde a la primera), la
compuerta (capturas idénticas, 110 k puntos, y el `.wav` de la
reverberación de CT **byte a byte** con y sin, en el binario de trabajo
`5D50EE4283E3EDC1` y de nuevo en el reentrenado `05B9569BBE960491`), y el
resumen de traza que ahora dice «programa emitido»/«cuerpo C» — porque un
A/B con el emisor caído mediría C contra C en silencio, la falla clásica
del árbol. Palanca `DCEMU_SIN_JIT_DSP=1`; suites 23/23.

**La primera tanda salió al revés — CT +34 % — y lo que destapó estaba
pagándose desde antes: la tormenta de reconstrucciones.** Los cuatro pares
dieron con ≈126,6 s contra sin ≈94,3, una señal demasiado grande para un
subsistema de 7 %: 32 s / 7,9 M de muestras ≈ 4 µs por muestra, la firma
de una **reemisión por muestra**. El contador de reconstrucciones (recién
agregado al resumen, la sonda antes que la corrección) lo midió:
**370 850 reconstrucciones en 30 s de CT — 12 000 por segundo**. La causa:
`aicadsp_tocar()` se disparaba por toda escritura en 0x2800-0x3BFF, y en
ese rango vive el **bloque común del AICA** — los timers y el INTC que el
driver del ARM golpea miles de veces por segundo — mientras que el DSP
solo relee **0x2804 (RBP/RBL) y 0x3000-0x3BFF (COEF/MADRS/MPRO)**. Cada
ack de interrupción reconstruía la tabla; con el emisor, además reemitía
el programa (~14 µs). Y la mitad vieja de la lección: **el cuerpo C venía
pagando el reescaneo y la predecodificación por cada toque desde que la
predecodificación existe** — invisible porque costaba ~1-2 µs en vez de
14, o sea unos 3 s por corrida de CT que ninguna sonda miraba. El achique
de la ventana a lo que el DSP relee es exacto por construcción
(reconstruir con entradas idénticas es idempotente) y no lleva palanca: su
costo quedó medido directo por el contador, **370 850 → 46**
reconstrucciones en el mismo banco, con los contadores de la corrida
idénticos. Suites 23/23 y compuerta verde de nuevo tras el arreglo
(`C1C444F0B132BF00`, custodia `E45E868C92EADD18` intacta — sexta
generación).

**El veredicto: ADENTRO en los tres guests — la mayor ganancia por
mecanismo desde el índice de enlaces.** Tanda sobre el canónico
reentrenado (`4E25653D7EE69A7A`, compuerta verde previa en el mismo
binario; condición anotada: dos núcleos de 32 clavados por procesos ajenos
estables, que el apareado absorbe y solo puede ensanchar rangos):
**CT −3,8 %** (87 467-87 974 contra 90 658-91 913 ms, disjunto, 4/4),
**DOOM −3,5 %** (27 502-27 759 contra 28 223-29 151, disjunto, 4/4),
**SR2 −2,4 %** (62 095-63 790 contra 63 689-64 614, 4/4 con un solape de
101 ms). DOOM y SR2 exactos al dígito en las 16 corridas. Y el porqué de
que los tres ganen es un descubrimiento propio: **la premisa «DOOM y SR2
no programan el DSP» era falsa** — los dos cargan el programa por omisión
del driver de Katana, **105 pasos con corte en el 111**, que corre 112
pasos por muestra (más que los 86 de CT) con **cero envíos a MIXS**: un
microprograma masticando silencio por el camino de EFSDL (6 ranuras ≠ 0,
así que hay que correrlo — su salida entra a la mezcla). «No programan»
era en realidad «no lo alimentan», y el costo era real e invisible porque
ninguna sonda lo repartía por guest. La aritmética cierra: DOOM ahorra
~0,65 µs por muestra (1,54 M muestras, −1,0 s) y CT ~0,44 (7,9 M,
−3,5 s), consistente con un cuerpo C de ~1 µs bajando a ~0,35. Junto con
el achique de la ventana, la marca de CT contra el canónico del
encadenado queda en ~87,5-88,0 s de 180 (desde 92,3-93,4 — ≈−5 % la fase
entera, y eso con la carga ajena encima). Tras el reparto nuevo el
mezclador quedó en **2,3 %** de CT — cerrado como blanco. El cinturón CHD
salió verde sobre el canónico: los cuatro juegos int≡jit al punto (180 k)
y las capturas **son los hashes canónicos del árbol**
(`75C732EB…`/`411393D6…`/`B779DACF…`/`C089D90F…`) — y tres de esos cuatro
son juegos Katana, o sea que el cinturón ejercitó el programa de 105
pasos por el emisor en los dos brazos. El parque KOS entero salió
**perfecto sobre el canónico: 131/131 con piso de ruido 0, señal
int↔jit 0 y cero veredictos serial distintos** (`barrido-jit.ps1`, tres
brazos, misma madrugada). Y la red de juegos entera
(`juegos-jit.ps1`, todo el material): **46/48 exactos, idéntico a la
línea F.1** — capturas byte a byte y puntos exactos en todos, MvC2 con
captura idéntica y su primer punto en 8550 (el expediente entendido al
ciclo) y Mortal Kombat Gold colgado en ambos brazos (compatibilidad) —
con el emisor del DSP corriendo en los dos brazos de cada juego Katana.
El control MSVC quedó reconstruido y reentrenado del mismo fuente:
`54FC7AA07DBBAC24`.

**El margen ARM7 que queda, diseñado con los números de esta noche y sin
gastar**: la marca negativa del perfil de CT es **1,6 % de los pasos
(23,8 M en 120 s) y son casi enteros seis sitios de `MSR CPSR`**
(e129f000/1 — los paréntesis de deshabilitar/rehabilitar interrupciones
del driver, ~3,84 M cada uno), más el camino de la FIQ a 120 k. El diseño
que seguiría el patrón de la cola de retorno: **MSR-CPSR como terminal de
bloque** — corre por su manejador como última entrada (el swap de banco ya
lo hace el manejador) y el bloque sale al C **sin encadenar**, porque el
teorema 1 exige re-chequear la FIQ tras tocar CPSR, y esa salida es
exactamente el chequeo del despachador; `MSR SPSR` (e169f008) podría ser
además forma ancha no terminal (escribe `spsr[banco]` y nada más — ni PC,
ni CPSR, ni FIQ). El techo es la razón de no gastarlo esta noche: 23,8 M
de pasos sueltos a ~2-3× el costo de bloque ≈ **0,2-0,4 % de CT** (el ARM7
entero es 9,8 %), sub-ruido para el estándar estricto — solo vale su ciclo
empaquetado con el retroceso del memo (~0,5 % de techo), y un A/B
combinado de dos mecanismos pide el tercer brazo que la lección del
pliegue de guardas ya cobró una vez.

**Las variantes FPU del traductor (2026-08-23, mañana): ADENTRO por el
estándar débil — y el desglose que las encontró vale tanto como ellas.**
La primera sonda del SH-4 fue partir el contador único de «rechazos por
verificación» en sus tres causas con reincidentes por PC (permanente en el
resumen), y el número escondía una sola historia: **el 70 % de los
rechazos de DOOM (5,8 M en 35 s) y el 94 % de los de SR2 (3,9 M en 60 s)
eran modo FPU, y en SR2 dos sitios (`01e38824`/`01e3882e`) cargaban 3,7 M
— entradas de bloque visitadas bajo los dos modos PR/SZ**, la premisa «el
flip encierra la secuencia» rota exactamente en la entrada. La corrección
es una búsqueda consciente del modo: `jit_buscar()` saltea la entrada de
otro modo y sigue el sondeo, así el modo equivocado es un **miss que
traduce la variante hermana** y las dos conviven en el hash abierto — los
enlaces ya no tocaban bloques FPU (`jit_parchear_enlace` los rechaza), el
salto encadenado y los indirectos re-verifican la clave al entrar, y el
dedup de `tr_traducir` usa el mismo buscar, o sea que la variante nace
sola sin código nuevo de inserción. Con ella: **SR2 3 852 994 → 0 rechazos
FPU con DOS variantes traducidas; DOOM 5 792 976 → 0 con cero** (sus
bloques renacen ya en el modo vigente). El desglose también midió el
techo con honestidad: los tramos interpretados eran CORTOS (~1,4
instrucciones por rechazo — el costo era el viaje redondo al despachador,
no tramos largos). Palanca `DCEMU_JIT_SIN_VARIANTES_FPU=1`; suites 23/23;
compuerta verde dos veces (`0BB8811FDF156E38` y el reentrenado
`2ED0992E6125BB2A`). Tanda con el ambiente degradándose a mitad (un brazo
de CT en 106 s): **DOOM −1,1 % con 4/4 pares y solape** — el estándar
débil de la máscara y el retorno —, SR2 −0,6 % con 3/4, CT
neutro/ilegible (inerte: 0 rechazos en ambos brazos). Contadores por
brazo idénticos entre rondas. Queda anotado: una re-tanda con carga 0
puede subirlo al estándar estricto. **El residuo con nombre para la
próxima sesión: los 2,5 M de rechazos por PALABRAS de DOOM** (72 k/s — los
remapeos de WinCE dejan bloques cuyo contenido ya no es el traducido, y
hoy fallan la verificación y corren interpretados para siempre; el sitio
estrella `00013134` con 70 871). La corrección candidata: retraducir tras
N fallos seguidos, o variantes por ASID — con su propio censo primero. El
control MSVC quedó reconstruido y reentrenado del mismo fuente:
`C772294E1EC4A72F`.

**La retraducción por fallo de palabras (2026-08-23/24): ADENTRO — SR2
−2,3 % con rangos disjuntos y 4/4, y la lección vino del guest que NO era
el objetivo.** El mecanismo: cuando `jit_verificar` dice que la memoria ya
no es la traducida, el bloque viejo recibe una **lápida en el pc** y el
`continue` cae en el camino normal del lazo (buscar → miss → traducir el
contenido vigente); la herencia del contador viaja por un derramadero
porque la traducción puede no ocurrir en esa visita ni empezar en ese pc,
y el tope por PC (16) acota el ping-pong si dos contenidos alternan — el
censo dice que **nadie alterna**: DOOM 2 531 592 → **117** rechazos por
palabras con 117 retraducciones y CERO al tope (cada sitio remapeado por
WinCE cambió una vez y una retraducción lo arregló para siempre), SR2
228 293 → 309. Palanca `DCEMU_JIT_SIN_RETRADUCIR=1`; suites 23/23;
compuerta verde dos veces (`2C0069DD6FC57D7C` y el reentrenado
`3FB976C6AE4C55CE`). La tanda (ambiente limpio de vuelta): **SR2 −2,3 %
disjunto 4/4** (56 958-58 360 contra 58 795-59 625 ms), **DOOM neutro**
(2/4 — sus 2,5 M de viajes al despachador eran baratos), CT inerte. Y la
sorpresa que la contabilidad delató antes que el cronómetro: la cobertura
del JIT en SR2 **bajó** 365 M de instrucciones (más entradas largas: 36,0
→ 37,2 por entrada, 17,5 M de entradas menos) y el tiempo MEJORÓ — los
sitios calientes cuyo contenido cambió durante la corrida (los mismos
bimodales del expediente FPU: tras el remapeo el código nuevo ya no lleva
filas FPU) quedan retraducidos a su forma vigente en vez de rebotar entre
variantes y verificaciones. **SR2 cruza el tiempo real por primera vez:
~1,04×** — era el único guest del banco por debajo. El residuo de
rechazos queda en ~1 200 por guest (modo MMU del arranque) — extinto como
categoría. El control MSVC quedó reconstruido y reentrenado del mismo
fuente: `BAFD8519BADC3DD0`.

**Las rutinas compartidas de la traducción (2026-08-24/25): SR2 −11,1 % y
DOOM −7,2 %, ambos con rangos disjuntos y 4/4 — la mayor ganancia del
recompilador desde el índice de enlaces, y la cadena de sondas completa
que la encontró.** El reparto dijo que SR2 pagaba 7,4 ns por instrucción
(CT 3,6); el censo de accesos, que emitía 254 bytes por instrucción (CT
94); y el censo de bytes nuevo (`DCEMU_JIT_SONDA_BYTES=1`, por plantilla y
por rubro, permanente), que el porqué era **la traducción MMU en línea: un
`MOV.L @Rm,Rn` costaba 320 bytes contra los 62 del modo plano** (+260 por
carga, +300 por escritura: la sonda de `mmu_datos`, el avance de URC y la
composición de la física, repetidos en cada sitio), con las plantillas de
acceso cargando ~60 % de los 170-200 MB del arena — presión de icache
pura. La corrección: **el cuerpo de `gen_traducir_mmu` se emite UNA vez
como dos rutinas compartidas** (lectura/escritura) al frente del arena, y
cada sitio conserva el atajo P1/P2 en línea (los mismos bytes,
factorizados en `gen_atajo_p1p2`), llama por rel32 y decide su camino
lento con el EAX que la rutina devuelve — ECX (la virtual) sobrevive en
los dos desenlaces, y las guardas de la rutina aterrizan en una cola
`xor eax,eax; ret` en vez de en talones por sitio. La sonda de accesos
fuerza la forma en línea (sus razones son por sitio); las rutinas se
emiten al frente de `tr_traducir`, nunca en medio de un bloque. Resultado
de tamaño: **SR2 169,6 → 97,4 MB (−43 %)**, cargas a ~100 B y escrituras a
~165. Palanca `DCEMU_JIT_TRAD_EN_LINEA=1`; suites 23/23; compuertas
verdes dos veces (`57E8FD3AAF5ADD04` y el reentrenado `02EE89A0671B8A51`);
tanda: **SR2 53 461-56 025 contra 60 065-62 257 ms** (disjunto por 4 s
enteros) y **DOOM 25 558-26 614 contra 27 448-28 281**, CT inerte por
construcción (modo plano, cero llamadas), contadores idénticos entre
brazos en las 24 corridas. Marcas: **SR2 ~1,10×, DOOM ~1,35×**. Y dos
topes silenciosos destapados de paso, cada uno con su regla: **SR2 venía
chocando el arena de 192 MB con la emisión en línea y dejaba de traducir
por un `desmarcar` que ningún contador contaba** (ahora `sin arena` en el
resumen), y con los bloques a la mitad ahora llena la **tabla de 32 768
bloques** (índice `short`; `13 276 sin lugar`, fríos — los contadores de
entradas no se mueven), que queda como margen nombrado con su censo
pendiente. El control MSVC quedó reconstruido y reentrenado del mismo
fuente: `6CFDD79A3B68DA5F`.

**El servicio partido se midió y se REVIRTIÓ la misma noche — y su censo
queda.** El censo de causa nuevo (`perf: servicios: … por vencimiento, …
solo por reintento`, permanente en `--perf`) midió que **el 86,8 % de los
servicios del bloque periódico de DCDoom (24,6 M en 35 s — 703 000 por
segundo) y el 60,6 % de los de SR2 corren SOLO porque UpdateSR dejó armado
el reintento de entrega** — ticks, AICA y DMA enteros para una entrega que
solo necesita al INTC. El camino partido (saltear los tres en la frontera
no vencida; exacto por las premisas ya probadas del reloj por eventos —
todos sus plazos viven en `reloj_calcular()`, la aritmética de restos es
invariante al tramo, y con DMA auto el vencimiento es 0, o sea nunca se
entra) salió **NEUTRO en los tres guests** con compuertas verdes: 2/4 con
solape en DOOM, SR2 y CT (`B6C6294CD3694FA0`). La lección es la de B.3 y
la del cuerpo rápido del DSP, ahora por tercera vez y con su forma
general: **un volumen enorme de trabajo salteado no compra tiempo si ese
trabajo son cargas y comparaciones predecibles e independientes — el
desorden del procesador ya las corría en la sombra del trabajo vecino.**
Lo que sí compra tiempo, esta noche lo dijo dos veces, es lo que rompe esa
sombra: los 320 bytes por acceso que desalojaban la icache, y los viajes
con dependencia (el rebote de variantes). La lápida está en `main.c`; el
mecanismo queda descrito aquí por si el reparto cambia. El cierre de la
noche: canónico reentrenado tras la reversión **`CFDDAAB73D938FBA`**
(compuerta verde, custodia `E45E868C92EADD18` intacta, suites 23/23) y
control MSVC en paridad **`F486C5147C1FDE48`**.

**El reconocimiento de cierre (2026-08-23, madrugada): el SH-4 traducido
es lo único grande que queda, en los tres guests.** Repartos frescos sobre
el canónico `4E25653D7EE69A7A`: SR2 **85,2 %** de resto (intérprete),
DOOM **82,2 %**, CT **73,9 %** — con el mezclador en 0,8-2,3 %, el ARM7 en
5,4-9,8 % y los gráficos en 1,7-10,2 %, todos con margen medido o
diseñado. Y el número que nombra la pregunta siguiente: **el costo por
instrucción emulada varía 2× entre guests** — CT 3,6 ns, DOOM 5,3, SR2
7,4 — siguiendo el peso MMU+FPU, con SR2 a 134,5 MIPS y 0,93×, el único
guest del banco por debajo del tiempo real. La jugada de apertura de la
próxima sesión está nombrada: `DCEMU_JIT_SONDA_ACCESOS` sobre SR2 (el
censo post-atajo-P1/P2 que reescribió la fase 6 una vez ya) y el desglose
de sus 4,1 M de rechazos de verificación por 60 s contra los 8,3 M de
DOOM por 35 — dónde va cada nanosegundo de esos 7,4 antes de proponer
nada. La sonda de tirones salió sana en el cierre (p99 15,9 ms, 0 cuadros
sobre 33,4; los 43 sobre 16,7 llevan `blq=0` adentro — la carga ajena de
la condición anotada, no el emulador).

**Se revirtió medido, y la lección es la que cierra la serie B: la frontera
por peso ya no predice tiempo.** El censo que eligió los lotes B.2/B.2b
medía cuando los cortes costaban despachador; con el enlazado y los puentes
maduros, el residuo «sin plantilla» es estructural y barato. Los otros dos
blancos del ponderado quedan anotados y sin gastar: el `FDIV` en ranura de
CT es la categoría con riesgo de excepción FPU (la que la admisión de
ranura excluyó a propósito), y el `FD8F` de SR2 ni siquiera es una
instrucción nombrable — un bloque caliente que termina contra datos. Ninguno
justifica su ciclo tras este veredicto. El árbol quedó en 149 plantillas con
los totales al dígito de la línea previa (`563A0A95EA78A42F`, suites 23/23).

## Lo que este plan NO incluye (heredado, medido y perdido)

Hilos para AICA/SH-4/render (pierde 4-5 %, `hilos-plan.md`; **expediente
releído el 2026-08-21** con el AICA ya en 23 % y `--perf` imprimiendo un
techo bruto de 1,30×: el veredicto se sostiene, porque las tres causas de la
pérdida son estructurales y no cambiaron — la contención de memoria que
encareció el AICA 46 % y el intérprete 20 % en la medición original, la
colocación de hilos en un híbrido P/E, y la frontera de determinismo en la
entrega de la interrupción del AICA al ASIC, donde avanzar de a cuatro
muestras ya rompió el `.wav` una vez); el superbloque por
flujo (neutro dos veces, SR2 lo rechaza con rangos disjuntos); el buscador
emitido y el redespacho por ayudante (el viaje al despachador no es el costo,
tres mediciones); las costuras con cargas parciales; el tope de 96; fastmem por
VEH más allá de su techo de 1,78 %; `DCEMU_ARCH=AVX2`; VBO/batching (el pipeline
gráfico entero es 7,6 %). No se reintentan sin releer su expediente.

Y uno nuevo, descartado por análisis antes de escribirlo: **LLVM como backend
del JIT** (ORC/MCJIT en lugar de `jit_x64.c`). Traducir hoy cuesta
0,008-0,014 ms por bloque y ~0,7 % de la corrida; un backend LLVM multiplica ese
costo en ~dos órdenes de magnitud y devuelve los tirones que el índice de
enlaces acaba de matar, para comprar calidad de código en bloques de 20-48
instrucciones donde los hogares canónicos ya la dan — y el veredicto del flujo
dice que las trazas más largas no compran tiempo. Las reglas de exactitud del
árbol (ciclos después de la ranura, puntos de sync, claves de época/FPU) son
sostenibles en un emisor a mano y opacas a través de un IR que reordena. El
estado del arte real emite a mano por estos mismos motivos. Solo se reabre si un
censo futuro muestra regiones rectas largas y calientes donde la calidad del
emitido sea el cuello — hoy no hay ninguna evidencia de eso. El lugar de
clang/LLVM en este plan es la fase G: compilar el emulador y las herramientas,
no emitir los bloques.

## El orden, y por qué

A (censos + red CHD) es barata y ordena todo. B (FPU) es el único hueco grande
con nombre propio y ataca al guest que menos mejoró. C (ociosos) espera su
evidencia de A. D son techos de 0,3-1,8 % y solo entran si el reparto nuevo los
sube. E ataca la segunda porción cuando el SH-4 deje de ser el 63-81 %. G es
transversal: su techo lo da el reparto módulo/arena de A, su mitad de
herramientas (objdump/mc/sanitizers) vale desde el día uno, y conviene resolverla
antes de F. F cierra: sin adopción por omisión no hay «estado del arte» que se
entregue.
