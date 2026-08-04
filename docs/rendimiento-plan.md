# Plan: rendimiento

Estado: **propuesto**. Escrito el 2026-08-01 a partir de una revisión del código en
`pendientes-c-y-banco` (`bc11bf7`). Nada de esto está implementado todavía.

## El punto de partida

Crazy Taxi corre a **40 fps** en un i9-13900. La consola va a 60, así que falta un 50 %
de velocidad — y eso es lo que hay que encontrar, no un 500 %.

El pedido venía con una hipótesis: *"seguramente tendrá que usar multithread"*. Puede ser,
pero **no es por donde se empieza**, por dos razones que la revisión deja claras:

1. **El binario que se está midiendo es el de depuración.** En `build-x64/` solo existe
   `Debug/`; no hay `Release/`. `dcemu.exe` es de ayer, 671 KB, con su `.pdb` al lado, y
   todos los barridos (`barrido.log` … `barrido5.log`) salieron de ahí. MSVC en Debug
   compila con `/Od` y `/RTC1`; para un intérprete enhebrado — que es todo llamadas
   indirectas y acceso a estructuras — eso cuesta típicamente entre 4× y 10×.
   `CMakeLists.txt` no agrega ningún flag de optimización propio, pero tampoco hace falta:
   la configuración `Release` de CMake ya trae `/O2 /Ob2 /DNDEBUG`. Y el comentario de
   `CMakeLists.txt:165` explica por qué se quedó así: *"conviene validar primero el binario
   sin optimizar"*, por el type-punning de `mem.c` y `floatsimple.c` sin equivalente a
   `-fno-strict-aliasing`. Esa validación es justo lo que la fase 1 tiene que hacer.

2. **Hay una medida que ya está en el árbol y apunta a otro lado.** `CLAUDE.md` registra
   que el tiempo emulado corre **~2,5× más rápido que el real** sin `--limitar` en las
   demos de KOS. En Crazy Taxi corre a 0,66×. O sea que el mismo núcleo, en el mismo
   binario, va casi cuatro veces peor cuando lo que cambia no es el SH-4 sino **la carga
   del PVR**: ~2600 tiras por escena, cientos de texturas, mipmaps, punch-through. Eso
   convierte al pipeline de gráficos en el primer sospechoso, no al intérprete.

Las dos cosas se miden antes de tocar nada. Un 5× de la fase 1 cambia la pregunta entera.

> **Corrección (2026-08-01, medido).** El punto 2 estaba equivocado y la medición lo
> desmiente: en las pantallas medidas de Crazy Taxi `dibujar_escena()` cuesta **0,4-0,8 %**
> del tiempo real, texturas incluidas. El PVR no es el cuello. Lo que sí apareció es un
> **20-37 % en el bloque periódico de `main_loop()`**, del que se recuperó un **19,5 %
> cambiando una constante**. El detalle está en [`hilos-plan.md`](hilos-plan.md),
> "Resultado del paso 0"; las fases 3.1 a 3.5 de este documento quedan despriorizadas
> hasta que alguien mida una escena 3D pesada y demuestre lo contrario.

## Las barandas

Este árbol ya tiene con qué demostrar que una optimización no rompió nada, y es más de lo
que suele haber. Cada fase tiene que dejarlas verdes **antes** de considerarse hecha:

- `ctest` — las suites de `tests/`, incluida `cobertura`.
- `dcemu_sh4json` contra SingleStepTests/sh4, **bit a bit en punto flotante**. Es la
  baranda que hace que optimizar la FPU no sea adivinar: cualquier cambio de flags que
  altere el redondeo aparece aquí de inmediato.
- El barrido de `docs/demos-kos.md`, comparando los BMP de `--captura-gl` **byte a byte**
  contra la corrida anterior. Una optimización del pipeline que cambie un pixel se ve.
- `--captura-audio` sobre una demo de sonido: el ADPCM es determinista, así que dos
  corridas dan un `.wav` idéntico. Sirve igual de baranda para la fase del AICA.

Y una regla de método que el resto del proyecto ya sigue: **medir antes y después de cada
cambio, y anotar el número**. Este documento se va llenando con esos números.

---

## Lo que quedó ganado, medido contra la rama sin rendimiento

Rehecho el 2026-08-02 después de los tres merges, con
[`herramientas/rama-ab.ps1`](../herramientas/rama-ab.ps1). La rama de referencia es
`origin/pendientes-c-y-banco` (`08870a2`), que trae **todo** el contenido nuevo —render,
arranque, los cuatro juegos— y nada del trabajo de rendimiento: así el número aísla lo que
aportó esta rama y no se le cuelan las mejoras de render.

Crazy Taxi, 180 s emulados, en 3D en movimiento (1182 tiras por escena, picos de 1889):

| | tiempo real | velocidad |
| --- | --- | --- |
| `pendientes-c-y-banco` | 400 458 ms | 0,45× |
| **`rendimiento-hilos`** | **314 617 ms** | **0,57×** |

**21,4 %, o sea 1,27×.** Dos vueltas alternadas en la misma tanda, y la rama nueva repite
dentro del 0,04 % (314 500 y 314 617).

La prueba de que las dos corridas hicieron el mismo trabajo es la más fuerte que ha dado
este proyecto: **10 001 escenas rendidas en las dos**, y las tiras de las últimas doce
coinciden número por número —`1692 1692 1692 1692 1680 1680 1680 1680 1680 1682 1682
1682`—. No es "la misma cantidad de trabajo": es la misma ejecución, cuadro por cuadro.

### El banco de pruebas: **sin `--bios`**

Esta receta decía `--bios` y estaba mal. Con `--bios` el boot ROM arranca el disco pero
Crazy Taxi se cuelga en `LOADING (31K)`: el hito D se alcanzó por los hooks de syscall, que
es donde `GDROM_CHECK_DRIVE` contesta el tipo de disco que Katana espera (ver `CLAUDE.md`).
Lo que se medía era el menú del propio ROM.

**Cómo se descubrió, porque el método vale más que el error**: una sonda corrió el mismo
banco con dos juegos distintos y dio `7111 escenas, 53 tiras/escena, máximo 178` en los
dos, idénticos al dígito. Dos juegos no pueden dar el mismo número; lo que corría era el
ROM, que es igual para cualquier disco. La captura confirmó el panel `Set Date/Clock`.

Dos cosas más que salieron de ahí y valen para cualquier medición futura:

- **El flash configurado es estado que se pierde al recopiar `bios/`.** Con un flash virgen
  el ROM pide la fecha en cada arranque y no llega a ningún disco. La navegación automática
  (`DCEMU_PULSAR_A`) corría **una sola vez**, entre los sondeos 300 y 560 —5 a 9 segundos
  emulados—, mucho antes de que el ROM terminara su animación y pusiera el panel: las cinco
  flechas se gastaban contra una pantalla que no existía. El comentario prometía que la
  secuencia se repetía; el código no lo hacía. Ahora se repite cada 600 sondeos.
- **Y 60 segundos no alcanzan, ni 120.** A los 120 la corrida está en `MODE SELECTION` —479
  tiras por escena—, y recién pasados los 180 hay 3D en movimiento. Con 53, 479 y 1182
  tiras por escena se miden tres cosas distintas.

---

## Resultado por fase, al 2026-08-02

Todo lo de abajo es Debug, en el i9-13900, sobre Crazy Taxi **en juego** (~1000 tiras por
escena) con `--salir-tras=60` y la secuencia de teclas que llega al gameplay. Las corridas
que se comparan son idénticas: mismos cuadros, mismas escenas, mismas tiras.

| | tiempo real | velocidad | gana |
| --- | --- | --- | --- |
| punto de partida | 211 266 ms | 0,28× | — |
| + grano del bloque periódico (fase 2.5) | 187 867 ms | 0,31× | 11,1 % |
| + camino directo de memoria y despacho (2.1 y 2.3a) | **166 939 ms** | **0,35×** | 11,1 % |

**Acumulado: 21 %**, o sea 1,27×. Las tres corridas dan 3070 cuadros, así que son la
misma ejecución y los tiempos se comparan sin reservas.

(El grano había dado 19,5 % en una corrida de 40 s que se quedaba en los menús, y aquí da
11,1 %. No se contradicen: en juego el intérprete pesa más y el bloque periódico
proporcionalmente menos. Es la razón por la que las mediciones se hacen en gameplay.)

El reparto quedó así:

```
  AICA total          32900 ms   19.7 %
  bloque periodico    39347 ms   23.5 %   <- de los cuales 32900 son el AICA
  dibujar_escena()     ~5500 ms    3.3 %
  resto (interprete) 119624 ms   71.6 %
```

O sea que **el bloque periódico ya no pesa**: descontado el AICA, que está anidado ahí,
queda en un 3,8 %. Era el 21 %. Lo que queda por delante es el intérprete, y el PVR sigue
sin aparecer.

Dos cosas que la medición cerró y ahorran trabajo futuro:

- **La fase 3 entera (el pipeline de GL) no tiene caso.** `dibujar_escena()` cuesta 2,1 %
  con mil tiras por escena. Las 40 000 llamadas a GL por cuadro que este documento estimaba
  no existen: con 1016 tiras son unas 15 000, y no se notan.
- **La fase 2.3b (accesores tipados) es redundante.** Su objetivo era que
  `memread(dir, &v, 4)` no tuviera que materializar `v` en memoria; con el macro de 2.3a el
  acceso ya se expande a un guardado directo sobre el registro destino.

### Cómo se valida

`herramientas/barrido.ps1` corre las 150 demos de KOS con `--captura-gl` y
`--salir-tras=8`, y `comparar.ps1` compara **hash del BMP**, no cuenta de colores — que
tiene dos trampas anotadas en `docs/demos-kos.md` y además puede dar el mismo número para
dos capturas distintas.

El grano 400 pasó así: **141 de 150 byte a byte idénticas**. De las nueve restantes, una
corrida de control con el mismo binario mostró que **cinco son no deterministas por sí
solas** (`pvr-cheap_shadow` y `pvr-modifier_volume` ya estaban documentadas: colocan la
geometría con `rand()`). Las otras cuatro cambian sin romperse: tres son fase de animación
—`basic-threading-once` resultó ser la pantalla de fecha del boot ROM, porque la demo ya
había terminado— y `modem-ppp` pasó de 1 color a 11, o sea que dibuja **más** que antes.

**La corrida de control es parte del método, no un extra.** Sin ella las nueve parecían
regresiones.

La fase 2 pasó igual: **144 de 150 idénticas**, las mismas cinco no deterministas, y una
sexta —`sh4zam-bruces_balls`— que pasó de `rv=0` a acceso inválido y **no era regresión de
la fase 2**. El bisecto lo demostró en tres pasos: apagando el camino directo de memoria
seguía cayendo, apagando además el despacho directo seguía cayendo, y **recompilando el
árbol completo del commit anterior también caía**, aunque el binario guardado de ese mismo
estado pasaba.

Mismo código, mismo `RELOJ_GRANO`, distinto binario, distinto comportamiento: era una
corrupción de memoria latente —`VertexBuffer[]` y `TriangleStrip[]` sin control de límite—
cuyo efecto depende del layout del binario. Está arreglada.

> **Un crash reproducible dentro de un binario puede no serlo entre binarios.** El control
> decisivo fue recompilar el commit anterior, no razonar sobre el diff. Y el barrido
> encontró un error que no era el que estaba validando: sin él seguía latente, esperando
> otro layout.

## Fase 0 — Medir

Sin esto lo demás es adivinanza. Tres instrumentos, del más barato al más caro.

### 0.1 Una métrica de velocidad en el propio emulador

Ya existe media: `fps_marcar_cuadro()` cuenta cuadros presentados y `reloj_total` cuenta
ciclos emulados. Falta juntarlos y sacarlos por `traza_resumen()`:

- **MHz emulados** = `reloj_total / segundos reales`. Contra los 199,5 MHz de
  `DC_CPU_HZ` da directamente el factor de velocidad, que es el número que importa y hoy
  solo se conoce de reojo.
- **Instrucciones por segundo**, que no es lo mismo (los ciclos por instrucción varían).
- Y el reparto: cuántos milisegundos de cada segundo se van en el intérprete, en
  `cb_tastart()`, en `get_texture()` y en `aica_tick()`.

Ese reparto se saca con `QueryPerformanceCounter` alrededor de cuatro bloques y un
acumulador por bloque, detrás de una opción nueva `--perf` (misma forma que `--traza-mem`:
apagada cuesta una comparación). No es un profiler, pero responde la única pregunta de la
fase 0 — *¿CPU o gráficos?* — sin instalar nada.

### 0.2 Un profiler de muestreo sobre el binario Release

Para lo fino hace falta un perfil real. El de Visual Studio (CPU Usage) alcanza y ya está
instalado. Alternativas gratuitas que funcionan bien con PDB de MSVC: Superluminal (pago),
Very Sleepy, o el propio VTune.

Lo que se busca es el reparto entre: despacho de opcodes, `memread`/`memwrite`,
`ta_procesar_bloque()`, `dibujar_escena()`, `get_texture()`, `mezclar_una_muestra()` y
`arm7_paso()`.

### 0.3 Una escena fija y repetible

Un perfil sobre "juego a Crazy Taxi un rato" no se puede comparar contra el de la semana
que viene. `--salir-tras=N` ya da corte determinista por tiempo **emulado**, así que
`dcemu --salir-tras=180 crazytaxi.cdi` llega siempre al mismo punto del juego. Ese es el
banco de pruebas; el resumen de `--perf` al salir es la medida.

(Decía `--bios --salir-tras=40` y las dos cosas estaban mal: con `--bios` el juego no
arranca y 40 segundos no salen del menú. Ver arriba, "El banco de pruebas".)

Conviene tener dos: la atracción de Crazy Taxi (carga de PVR alta) y una demo de KOS con
poca geometría (carga de CPU pura), para poder distinguir qué mejoró.

**Además, confirmar tres cosas del entorno antes de creerle a cualquier número:** que
`--captura-gl` esté apagado (es un `glReadPixels` de pantalla completa por cuadro), que
`--traza-mem` esté apagado, y qué hace el intervalo de intercambio del driver. dcemu nunca
llama a nada equivalente a `SDL_GL_SetSwapInterval`, así que el vsync lo decide el panel de
control de la placa: con vsync forzado el techo son 60 y 40 significa otra cosa que con
vsync apagado.

---

## Fase 1 — El build

Barata, mecánica, y probablemente la más grande de todo el plan.

### 1.1 Compilar en Release

`cmake --build build-x64 --config Release`. Nada que escribir. Pero hay que **validarlo**,
que es lo que quedó pendiente en `docs/msvc-build-plan.md`: `/O2` habilita análisis de
aliasing que `-fno-strict-aliasing` desactiva en MinGW, y `mem.c` y `floatsimple.c` hacen
type-punning por puntero. Las tres barandas de arriba son exactamente el aparato para
detectarlo, y `dcemu_sh4json` en particular: si `/O2` rompe el punning de floats, 233
encodings × 500 casos bit a bit no lo dejan pasar.

Si aparece un problema real de aliasing, la salida limpia es convertir el punning a
`memcpy` (que MSVC y GCC compilan a un `mov`) en los pocos lugares que lo hagan, no
desactivar la optimización.

### 1.2 Los flags que faltan, en orden de riesgo

- `/GL` + `/LTCG` (optimización de todo el programa). **Esto importa más de lo normal
  aquí**: los handlers de opcodes viven en `mov.c`, `arith.c`, `logic.c`… — una unidad de
  traducción por categoría — y el bucle está en `main.c`. Sin LTCG el compilador no puede
  inlinear ni un `memread` de RAM dentro de un handler. Con LTCG, sí.
  **Hecho, y viene en ON.** Sobre una demo valía 1,4 % de la corrida; sobre un juego vale
  **6,6 %**, y el 14 % es del ARM. Virtua Tennis, 60 s emulados hasta el atractivo en 3D:
  0,97x contra 1,03x de tiempo emulado sobre real, o sea cruzar la velocidad de consola.
  El `.wav`, el BMP de `--captura-gl`, las 3562 escenas y las 8 068 071 255 instrucciones
  salen idénticos en las dos compilaciones. Dos trampas de la medición, cada una costó un
  número inservible: **`--captura-gl` se lleva el 40 % del tiempo real**, así que comparar
  con ella puesta mide el volcado de BMP; y **la primera pasada después de un
  `--clean-first` sale lenta** —124,9 MIPS contra 138,7 de las dos siguientes— y hay que
  descartarla. Conviene alternar los dos binarios dentro de la misma tanda: los números de
  LTCG se repiten al 0,1 % mientras los de la base derivan según se carga la máquina.
- **PGO** (`/GENPROFILE` … `/USEPROFILE`), entrenado con la corrida fija de 0.3. Un
  intérprete es el caso de libro: el layout de código y la predicción de saltos del
  despacho es todo lo que hay. Vale entre un 10 % y un 20 % de forma típica.
- `/arch:AVX2`. Seguro **mientras se mantenga `/fp:precise`** (el valor por omisión): MSVC
  no contrae a FMA salvo con `/fp:fast`, así que la aritmética escalar no cambia de
  resultado, solo de codificación. Igual pasa por `dcemu_sh4json` antes de quedarse.
- **`/fp:fast` no.** Cambia el redondeo y rompe la conformidad que costó dos pasadas
  conseguir (`docs/sh4-conformidad.md`). Y `fpu_aplicar_redondeo()` manipula el modo de
  redondeo del anfitrión en tiempo de ejecución, que es justo lo que `/fp:fast` se permite
  ignorar.

### 1.3 De paso, `Makefile.win`

Compila con `-march=athlon-xp -m3dnow`: un procesador de 2001, sin SSE2. En la ruta de
MinGW eso vale lo mismo que Debug en la de MSVC. `-march=x86-64-v3` (o `-mtune=native`
para un binario local) y sacar `-m3dnow`, que ni siquiera existe en los GCC actuales.

**Punto de decisión.** Con la fase 1 hecha se vuelve a medir. Si Crazy Taxi ya pasa de 60,
el resto del plan es opcional y conviene parar aquí: cada fase siguiente agrega complejidad
a un código que hoy es legible y está documentado, y eso tiene un costo real.

---

## Fase 2 — El intérprete

Solo si la fase 0 dice que el SH-4 pesa. Ordenadas por relación beneficio/riesgo.

### 2.1 Sacar una llamada indirecta por instrucción

`main_loop()` hace `core.execute(...)`, que es un puntero a función en una estructura, que
llama a `run()`, que hace `oplist[arg](arg)`. Son **dos llamadas indirectas y una prueba**
(`fpu_deshabilitada && es_instruccion_fpu(arg)`) por cada instrucción emulada.

`core.execute` tiene que quedarse: `branch.c` y `rte143()` lo usan para las ranuras de
retardo, y ahí la prueba de `SR.FD` sí hace falta — es lo que distingue 0x800 de 0x820
(ver "Excepciones síncronas" en `CLAUDE.md`). Pero el camino rápido de `main_loop()` puede
llamar `oplist[instr](instr)` derecho cuando `fpu_deshabilitada` está en cero, que es
siempre. Cambio de tres líneas.

### 2.2 Cachear el puntero de búsqueda de instrucción

`get_memory_pointer(PC)` son dos cargas dependientes: `mem_zone[PC >> 24]` y después
`base + (PC & 0xFFFFFF)`. Se hace por instrucción y casi siempre sobre la misma zona.

Guardar `(zona_actual, base_actual)` y recalcular solo cuando `PC >> 24` cambia convierte
eso en una comparación y una suma. Ojo con dos cosas: cualquier escritura que reasigne
`mem_zone[]` tiene que invalidar la caché, y el guest puede escribir código (los stubs de
`BIOS_HACKS` lo hacen) — pero eso solo mueve bytes, no punteros de zona.

### 2.3 El camino rápido de memoria — el grande

Hoy **cada carga y cada guardado del guest es una llamada indirecta**:
`memread(dir, &destino, tam)` → `mem_hash_read[dir >> 24](dir, p, tam)` → el handler hace
`switch (size)` y escribe a través de un puntero. En `ram_read()` eso son tres
instrucciones de trabajo útil envueltas en una llamada que el compilador no puede inlinear
ni ver a través. Entre un 30 % y un 40 % de las instrucciones SH-4 son accesos a memoria.

Dos pasos, independientes:

**(a) Un camino directo para las zonas planas.** Una tabla `mem_zona_directa[0x100]` con 1
en las zonas que son memoria respaldada sin efectos secundarios — RAM del sistema
(`0x0C-0x0F`, `0x8C-0x8F`, `0xAC-0xAF`) y poco más — y el macro `memread` prueba esa
bandera primero:

```c
if (mem_zona_directa[_d >> 24])   *(DWORD *) target = *(DWORD *) get_memory_pointer(_d);
else                              (*mem_hash_read[_d >> 24])(_d, target, size);
```

La rama predicha correctamente casi siempre, y el acceso queda inlineado en el handler del
opcode. **No** se toca la RAM de video, ni el PVR, ni los registros: esos tienen efectos
secundarios y siguen por la llamada indirecta, que es donde deben estar.

**(b) Accesores tipados.** `memread(dir, &v, 4)` obliga a materializar `v` en memoria; un
`DWORD memread32(dir)` que devuelve en registro le ahorra al compilador el viaje.
Es una conversión mecánica pero toca muchos archivos, así que va después de (a) y solo si
el perfil todavía la pide. Se puede hacer incremental: convertir `ReadMemoryB/W/L` y
`WriteMemoryB/W/L` (por donde pasan **todos** los handlers) y dejar `memread`/`memwrite`
como están para los usos internos.

Las dos preservan el gancho de la MMU, el del UBC y el del watchpoint: son comparaciones
contra cero que quedan donde están.

### 2.4 Menos trabajo por instrucción en `main_loop()` — **medida: no vale nada**

Antes de pagar el riesgo de esta fase se midió el techo, con el mismo método que el grano:
un binario desechable con **las cuatro comprobaciones quitadas** (`DebugMode`,
`traza_activa`, `ubc_activa`, `excepcion_vigilar`), corrido contra el normal en la misma
tanda:

| | tiempo real |
| --- | --- |
| con las cuatro comprobaciones | 104 376 ms |
| sin ninguna | 105 888 ms |

**No hay nada que ganar** — la corrida sin comprobaciones sale 1,4 % *más lenta*, o sea que
la diferencia es ruido entre corridas. Cuatro ramas por instrucción que el predictor acierta
siempre no cuestan nada medible.

Queda descartada, y con eso se evita un refactor de cuatro banderas globales con escritores
en cuatro módulos distintos cuyo modo de fallo era saltarse un chequeo **en silencio**.

> El experimento costó dos compilaciones y seis minutos. El refactor habría costado un día
> y habría dado cero.

### 2.4 (texto original del plan)

El cuerpo del bucle interno hace, además del despacho: `DebugMode == DBG_STOP`,
`traza_activa`, `ubc_activa`, `excepcion_vigilar`, `cycles >= 50`,
`intc_queuemask || intc_queuemask_ext`, `DebugMode == DBG_STEP` y
`reloj_total - marca_linea >= pvr_ciclos_linea`. Ocho ramas por instrucción encima del
handler.

Dos cambios, y el segundo depende del primero:

- **Juntar las banderas raras en una palabra.** `traza_activa`, `ubc_activa`,
  `excepcion_vigilar` y `DebugMode != DBG_RUN` valen cero en todo lo que corre; una sola
  `if (modo_especial)` cubre las cuatro y adentro se decide cuál era. Quien las escribe
  actualiza la palabra, igual que `excepcion_actualizar_vigilancia()` ya deriva
  `fpu_deshabilitada` de `SR.FD` en un solo lugar.
- **Ejecutar por bloques.** Calcular cuántos ciclos faltan para el próximo evento — el
  fin de línea de barrido es el más cercano, ~6345 ciclos — y correr un bucle apretado
  hasta ahí, con solo el despacho adentro. El servicio periódico sale del camino caliente.

### 2.5 El bloque de servicio cada 50 ciclos

Con 199,5 MHz emulados, el `if (cycles >= 50)` dispara **4 millones de veces por segundo**,
y adentro llama a `timer_check()`, `wdt_tick()`, `aica_tick()`, `intc_revisar_sh4()` y
`dma_check()`. `dma_check()` lee cuatro CHCR cada vez; `aica_tick()` hace una división de
64 bits en `muestras_hasta_ahora()` para descubrir que no hay nada que hacer.

Los 50 son una constante de 2004 sin fundamento (el comentario que la acompaña habla de
otra cosa). El período real más corto de todos esos consumidores es el del AICA: una
muestra cada 4524 ciclos. Subir el grano a, digamos, 200 ciclos divide el costo por cuatro
y sigue siendo veinte veces más fino que el consumidor más rápido.

Lo correcto de verdad es una **cola de vencimientos**: cada periférico publica en cuántos
ciclos quiere que lo llamen, y el bucle avanza hasta el mínimo. Es lo que hace cualquier
emulador moderno y encaja bien con el contador monótono `reloj_total` que ya existe
(`docs/clock-plan.md`). Pero es un cambio de arquitectura del scheduler y hay que
justificarlo con el perfil; subir la constante no.

---

## Fase 3 — El pipeline de gráficos

Si la inferencia de arriba es correcta (2,5× en demos contra 0,66× en Crazy Taxi), esta es
la fase que importa. Todo lo de aquí es de un solo hilo y no cambia un pixel de la salida,
así que la comparación byte a byte de los BMP es baranda exacta.

### 3.1 Sombrear el estado de GL

`dibujar_escena()` programa, **por tira**: `glDepthFunc`, `glEnable`/`glDisable` de
`GL_CULL_FACE` + `glCullFace` + `glFrontFace`, `glDepthMask`, `glEnable`/`glDisable` de
`GL_BLEND`, `glBlendFunc`, `glEnable`/`glDisable` de `GL_ALPHA_TEST` + `glAlphaFunc`,
`glEnable`/`glDisable` de `GL_TEXTURE_2D`, `glTexEnvi`, más el `glBindTexture` y los cuatro
`glTexParameteri` que pone `get_texture()`. Son del orden de **quince llamadas a GL por
tira, con 2600 tiras por escena**: unas 40 000 llamadas por cuadro, la mayoría reprogramando
un valor que ya estaba puesto.

En el chip un encabezado de polígono fija ese estado para **todas** las tiras que vienen
detrás, así que las tiras consecutivas comparten estado casi siempre. Guardar el último
valor de cada estado en una estructura y emitir la llamada solo cuando cambia es local,
mecánico y no altera la salida. Es el cambio con mejor relación de toda la fase 3.

### 3.2 Sacar el `memread_fisico` de adentro del bucle

En la rama de punch-through, `dibujar_escena()` lee `PT_ALPHA_REF` (`0xA05F811C`) **por
tira**, con la llamada indirecta del despachador de memoria. El registro no cambia dentro
de una escena: se lee una vez en `cb_tastart()` y listo.

### 3.3 Un VBO en vez de arreglos de cliente

`glinit()` deja `glVertexPointer`/`glColorPointer`/`glTexCoordPointer` apuntando derecho a
`VertexBuffer[]`, memoria del proceso. Con arreglos de cliente el driver tiene que copiar y
validar el rango en **cada** `glDrawArrays`, o sea 2600 veces por cuadro.

Subir `VertexBuffer` a un VBO una vez por escena (`glBufferData` con `NULL` primero, para
orphaning, y después los vértices que realmente se usaron) y dibujar desde ahí cambia 2600
validaciones por una transferencia. Requiere `GL_ARB_vertex_buffer_object`, que cualquier
driver de este siglo tiene; hay que cargar los punteros de extensión, cosa que el árbol
todavía no hace en ningún lado.

### 3.4 Juntar tiras en menos llamadas de dibujo

Con el estado sombreado (3.1) queda visible cuántas tiras seguidas comparten estado
completo. Dos formas de aprovecharlo, de menor a mayor:

- `glMultiDrawArrays` con las corridas que comparten estado y textura. Una llamada por
  corrida en vez de una por tira, sin tocar los datos.
- Unir las tiras de una corrida en una sola con **triángulos degenerados** (repetir el
  último vértice de una y el primero de la siguiente). Menos llamadas todavía, pero cambia
  el contenido de `VertexBuffer` y hay que cuidar el sentido de giro, que aquí no es
  cosmético: el culling depende de él (ver "Culling ignoraba el sentido de giro" en
  `CLAUDE.md`).

Antes de hacer cualquiera de las dos, medir cuántas corridas hay: si el promedio es de 1,2
tiras por corrida no vale la pena.

### 3.5 Revisar el costo de la caché de texturas

La caché es persistente y se invalida por generación, y cada consulta suma las generaciones
de las páginas de 8 KB que cubre la textura. Para una textura de 1 MB son 128 lecturas por
consulta, **por tira**. Probablemente esté bien — son lecturas secuenciales de un arreglo
chico — pero es lo único de la caché que escala con el tamaño de la textura y merece una
medición antes de darlo por descartado. Si pesa, la salida es un resumen jerárquico
(generación por bloque de 64 páginas) o recordar el resultado por entrada y revalidar solo
cuando cambió el contador global.

---

## Fase 4 — Hilos

Ahora sí. Con el perfil en la mano y las tres fases anteriores hechas, lo que quede es lo
que justifica pagar el costo de la concurrencia — que en un emulador es alto, porque
convierte un error reproducible en uno que aparece una vez cada cien corridas.

**Regla:** nada de hilos sin que la baranda correspondiente siga siendo determinista. El
`.wav` del AICA es idéntico entre corridas hoy; si después de enhebrarlo deja de serlo, el
diseño está mal, no la baranda.

### 4.1 El AICA y el ARM7 en su propio hilo — el candidato claro

Es el mejor porque el corte ya está hecho: `aica.c`, `arm7.c` y `g2dma.c` **no tocan SDL**
a propósito, para que `tests/` los enlace de verdad, y todo lo periódico deriva de
`reloj_total`. El trabajo es real: 44 100 muestras/s × 64 canales, más
`faltan * 512` ciclos de ARM — hasta 22,6 millones de pasos de ARM interpretados por
segundo, en un núcleo que hoy comparte con el SH-4. Y el ARM corre en **todas** las demos,
no solo en las de sonido (`spu_init()` lo suelta siempre).

La superficie de contacto con el SH-4 son tres cosas: los 2 MB de RAM de sonido, el archivo
de registros del AICA y las interrupciones. De ellas, las escrituras del SH-4 a RAM de
sonido son ráfagas de DMA poco frecuentes, y las de registros son puntuales. O sea que un
diseño simple alcanza:

- El hilo del AICA corre con un **retraso acotado** respecto de `reloj_total` (por ejemplo,
  nunca más de un cuadro atrás), y produce muestras a un anillo que ya existe — `audio.c`
  ya trabaja con uno para poder tener `--captura-audio` con la tarjeta abierta.
- Las escrituras del SH-4 (registros y RAM de sonido) entran por una **cola con marca de
  tiempo en ciclos**, y el hilo del AICA las aplica cuando su reloj llega a esa marca. Eso
  es lo que preserva el determinismo: el orden de aplicación no depende de cómo se
  entrelazaron los hilos, sino del reloj emulado.
- Las interrupciones vuelven por la cola que ya existe (`intc_add_ext()`), que de por sí es
  "dejar la bandera puesta y que el otro lado la mire".

`--sin-aica` sigue siendo el interruptor para aislar una regresión, y ahora también sirve
para medir exactamente cuánto vale este hilo antes de escribirlo: la diferencia de fps
entre con y sin es el techo del beneficio.

### 4.2 Decodificar texturas en un grupo de trabajadores

`get_texture()` hace, en el hilo principal: juntar el bloque con `vram64_leer()`,
destwiddlear, resolver VQ o paleta o YUV o BUMP, y decodificar la cadena de mipmaps nivel
por nivel. Todo eso es CPU pura sobre un buffer intermedio — **sin GL, sin estado del
guest** — y solo el `glTexImage2D` final tiene que correr en el hilo del contexto.

O sea que se puede: recorrer las tiras de la escena antes de dibujar, encolar las texturas
que la caché reporta inválidas, decodificarlas en paralelo, y después dibujar subiendo los
buffers ya listos. El corte es limpio y no necesita sincronización fina: una barrera por
escena.

Pesa sobre todo en cambio de escena y con texturas que el juego regenera cada cuadro —
que es el caso peor que `CLAUDE.md` ya identifica (el atractivo de Crazy Taxi).

### 4.3 Un hilo de render — el grande, y el último

El TA ya construye la escena entera en memoria (`VertexBuffer[]`, `TriangleStrip[]`,
`VolumeBuffer[]`) antes de que `cb_tastart()` dibuje nada. Eso permite el modelo clásico:
el hilo del guest termina la escena, la entrega, y sigue emulando mientras otro hilo la
dibuja y la presenta.

Lo que hace falta: doblar los tres buffers (son ~9 MB entre los tres, no es problema),
mover el contexto de GL al hilo de render, y — la parte incómoda — resolver que
`get_texture()` lee RAM de video **en el momento de dibujar**, no en el de submitir. Si el
guest ya escribió encima, se decodifica la textura equivocada. Salidas: instantanear la
huella de VRAM de la escena al cerrarla (caro), o hacer que la caché tome su decisión de
invalidación en el momento de la submisión y no en el del dibujo (más barato y encaja con
el contador de generaciones que ya existe).

Es la fase con más riesgo del plan y la que puede introducir el tipo de error que cuesta
semanas. Va última, y solo si el perfil dice que hilo principal y GPU se están esperando
mutuamente.

### 4.4 Lo que NO conviene enhebrar

- **El núcleo SH-4.** Es un intérprete secuencial sobre estado compartido; partirlo no
  tiene sentido.
- **El acceso a memoria.** Poner un candado en `memread`/`memwrite` mata exactamente el
  camino rápido que la fase 2 construye.
- **`main_loop()` en general.** El orden de eventos entre CPU, ASIC y periféricos es
  precisamente lo que este emulador tardó dos planes (`clock-plan.md`, `bios-boot-plan.md`)
  en poner bien. Un hilo mal puesto ahí devuelve el proyecto a los bugs de "algo llega
  tarde y no deja rastro" que llenan `CLAUDE.md`.

---

## Orden de ejecución y expectativa

| # | Qué | Riesgo | Esfuerzo | Expectativa |
| --- | --- | --- | --- | --- |
| 0 | Instrumentar y medir | ninguno | bajo | — |
| 1.1 | Compilar Release y validarlo | bajo | muy bajo | **el grande, probablemente** |
| 1.2 | LTCG, PGO, `/arch:AVX2` | bajo | bajo | 15-30 % sobre Release |
| 3.1 | Sombrear el estado de GL | bajo | bajo | alto si el perfil apunta a GL |
| 3.2 | Sacar el `memread_fisico` del bucle | ninguno | trivial | bajo pero gratis |
| 2.1 | Una llamada indirecta menos | bajo | trivial | bajo |
| 2.5 | Subir el grano del bloque de servicio | bajo | bajo | medio |
| 2.3a | Camino directo de memoria para RAM | medio | medio | alto si pesa la CPU |
| 3.3 | VBO | medio | medio | medio |
| 2.2 | Cachear el puntero de instrucción | medio | bajo | bajo-medio |
| 4.1 | AICA + ARM7 en su hilo | medio | alto | medio (medible antes con `--sin-aica`) |
| 3.4 | Juntar tiras | medio | medio | depende de la medición |
| 2.3b | Accesores tipados | medio | alto | medio |
| 4.2 | Decodificar texturas en paralelo | medio | medio | medio |
| 2.5' | Cola de vencimientos | alto | alto | medio |
| 4.3 | Hilo de render | alto | muy alto | alto, si GL es el cuello |

Las tres primeras filas son un día de trabajo entre todas y es muy posible que basten.
Todo lo que está debajo de la línea de la fase 1 se decide **con el perfil**, no con este
documento.

## Lo que este plan deliberadamente no toca

- **Recompilación dinámica.** Es la respuesta "de verdad" al costo de un intérprete y
  también un proyecto entero: cambia el modelo de ejecución, rompe la relación uno a uno
  entre `opcodes[]` y su suite de pruebas, y deja sin sentido buena parte de las
  herramientas de diagnóstico (`--traza-desde`, el anillo de PC, el UBC). No para llegar
  de 40 a 60.
- **Cambiar GL de función fija por shaders.** Resolvería de verdad varias aproximaciones
  que hoy se documentan como pérdidas (bump mapping resuelto al subir la textura, los
  volúmenes modificadores por plantilla, el autosort por tira). Pero es una reescritura del
  pipeline y no es un plan de rendimiento.
- **Bajar la resolución o saltar cuadros.** Esconde el problema en vez de medirlo.

---

# 69,8 fps: cómo se llegó, al 2026-08-03

Crazy Taxi en 3D en movimiento (1183 tiras por escena), `--salir-tras=180`, i9-13900.

| | tiempo real | velocidad | fps |
| --- | --- | --- | --- |
| punto de partida de la rama | 400 458 ms | 0,45× | ~25 |
| Release, sin el cambio de GL | 252 330 ms | 0,71× | 39,6 |
| **Release, con el cambio de GL** | **143 165 ms** | **1,25×** | **69,8** |

Reproduce tres de tres (143 966 / 143 305 / 143 226 ms) con trabajo idéntico:
9994 escenas, 1183 tiras, 22 280 050 873 instrucciones.

## Las dos cosas que valieron, y las que no

**1. Compilar en Release.** El intérprete pasa de 189 s a 84 s, un 55 %. Verificado
aparte: 21/21 en `ctest` y 113 191 ok / 0 fallan en SingleStepTests, igual que Debug.

**2. El estado de GL que ya estaba puesto.** 43,3 % por sí solo — de 39,6 a 69,8 fps.
`dibujar_escena()` baja de 143 040 a 33 637 ms y las texturas de 81 206 a 1 488, un 98 %.
Eran dos cosas, las dos en el camino de las tiras:

- **`getenv()`**, unos 44 millones de barridos del bloque de entorno en tres minutos, por
  cuatro valores de diagnóstico que no cambian nunca después del arranque.
- **Cuatro `glTexParameteri` por tira** —58 millones— reponiendo valores que el objeto de
  textura ya tenía. Son estado del objeto, no del contexto, así que sobreviven al desligado.

**Lo que no valió**, todo medido: la tabla de despacho compacta (−1,2 a −2,5 %), el hilo
para el AICA (−4 a −5 %), sacar las comprobaciones por instrucción (cero), y el trío
ARM7 + `context_t` + `mem_base_directa`, que da 1-2 % cada una en Debug y **≈0 en Release**
—el optimizador ya se lleva eso—.

## Las tres lecciones de método, que costaron una sesión cada una

**Una optimización del lado del driver no se puede evaluar en un binario que no lo satura.**
El cambio de GL vale 11,6 % en Debug y 43 % en Release. No es que Debug atenúe: en Debug el
emulador va tan despacio que el driver siempre tiene la cola vacía y absorbe los 58 millones
de llamadas sin que se noten. El síntoma que lo delataba estaba a la vista y tardé en
leerlo: `dibujar_escena()` medía **más** en Release que en Debug —142 s contra 53—, lo que
sólo puede significar que el cuello pasó al otro lado.

**Y su contracara: un porcentaje de muestras en un perfil de Debug no predice el ahorro.**
`fpu_dn_s` tenía 9,6 % de las muestras y su optimización rindió 0,44 %. Debug infla
exactamente lo que el optimizador elimina igual, y lo infla más en una hoja llamada
constantemente que en el promedio.

**Medir descarta más de lo que confirma, y eso es lo que la hace rentable.** Los contadores
de la caché de texturas se escribieron para elegir entre tres arreglos posibles y
contestaron que dos eran innecesarios: 99,9 % de aciertos y **cero desalojos** significa que
agrandar la caché o afinar el marcado por página de `vram.c` —las dos cosas que iba a
hacer— habrían sido trabajo perdido. El tercero era el bueno.

De cinco hipótesis razonadas en una pizarra, cuatro murieron. Las dos optimizaciones que
funcionaron —un `getenv()` por tira y cuatro parámetros de textura redundantes— no las
habría propuesto nadie sin instrumentar primero.

## El banco de pruebas

Sin `--bios` —con `--bios` el juego no arranca— y con **180 segundos**: a los 120 la corrida
está en `MODE SELECTION`, con 479 tiras por escena contra las 1183 del juego. Toda
comparación verifica cuadros, escenas y tiras antes de mirar el reloj, y descarta la corrida
que no completó los segundos pedidos. Ver `herramientas/despacho-ab.ps1` y `rama-ab.ps1`.

---

# El dibujado de la escena, al 2026-08-03

El perfil de muestreo sobre **Release** —el primero que se pudo tomar, porque hasta
`826779c` esa configuración no dejaba PDB— contra Virtua Tennis, que es el banco pesado
(1837 tiras por escena contra 1183 de Crazy Taxi, y picos de 23 509 vértices):

| dónde | % del hilo |
| --- | --- |
| **fuera de dcemu: driver de GL y kernel** | **34,2 %** |
| handlers del SH-4 | 28,7 % |
| `main_loop` + `run` | 17,1 % |
| ARM7 del AICA | 8,1 % |
| FPU | 6,4 % |
| **código gráfico propio** | **2,9 %** |

**El 34,5 % que `--perf` llamaba `dibujar_escena()` era casi todo tiempo del driver.** La
correspondencia es exacta —34,5 % contra 34,2 % de muestras fuera de dcemu— y la conclusión
es que no había nada que acelerar *dentro*: había que **entrar menos veces al driver**.

Dos llamadas se hacían una vez por tira sin mirar si hacían falta:

- **`glDisable(GL_STENCIL_TEST)`**, que estaba dentro del lazo de dibujo.
- **`glBindTexture`**, reponiendo la textura ya ligada. Con la caché acertando el 99,9 %,
  casi todas las tiras consecutivas del mismo material religaban lo mismo.

Con 26 millones de tiras dibujadas en una corrida, son 52 millones de llamadas al driver
que no hacían nada. Ambas pasaron por la sombra de estado de `tira_estado()`.

| | antes | después | |
| --- | --- | --- | --- |
| **Crazy Taxi** | 1,26× · 70,3 fps | **1,50× · 83,6 fps** | +19 % |
| **Virtua Tennis** | 0,96× · 55,8 fps | **1,33× · 77,6 fps** | +39 % |
| `dibujar_escena()` VT | 86 765 ms (34,5 %) | **12 280 ms (6,8 %)** | −86 % |

(Las primeras medidas dieron 85,3 y 79,1, y son de antes de que la segunda pasada
de volúmenes pasara por la sombra —ver abajo—. Ese arreglo hace trabajo que se
estaba salteando y cuesta un 2 %, que es exactamente lo que hay que pagar.)

## Lo que se probó y no sirvió: agrupar las llamadas de dibujo

`glMultiDrawArrays` parecía el paso obvio: una entrada al driver para todas las tiras que
compartan estado, en vez de una por tira. Se implementó y se midió.

**26 093 408 tiras en 26 005 596 llamadas: 1,0 tiras por llamada.** El lote se cortaba en
casi todas, y la causa está identificada: una escena usa del orden de 1900 texturas
distintas y las tiras llegan ordenadas **por tipo de lista, no por material**, así que casi
ninguna comparte textura con la anterior.

Lo que lo haría pagar es ordenar las tiras opacas por textura dentro de su lista —seguro
ahí, porque decide el test de profundidad, y **no** en la translúcida, donde el orden *es*
el resultado—. Queda anotado en `dibujar_tira()`.

## Y el error que sólo el barrido podía ver

El intento de agrupamiento soltaba el lote **después** de que `tira_estado()` ya había
aplicado el estado nuevo, con lo que las tiras acumuladas bajo el estado viejo salían
dibujadas con el nuevo.

Pasó `ctest` 21/21 y SingleStepTests 113 191 ok / 0 fallan —el error es de orden y sólo se
manifiesta dibujando escenas reales— y el binario roto era **más rápido**: 78,3 fps
dibujando mal, contra los 79,1 del corregido. Reportarlo sin barrer habría sido un récord
falso.

Lo delató un detalle: demos que sólo dibujan texto —`hello`, `basic-exec`, `dev-devroot`—
cambiaron todas del mismo hash al mismo hash. Eso no es fase de animación, es algo
sistémico; con 40 demos moviéndose la única lectura posible era una regresión.

**Regla que queda**: una optimización que salta llamadas de estado de GL no está medida
hasta que el barrido de las 150 demos da limpio, y la cifra de velocidad no cuenta antes de
eso —parte de la velocidad puede ser trabajo que no se hizo—.

## El punto frágil

La sombra guarda estado **del contexto**, no del objeto, y hay una docena de sitios que lo
cambian sin pasar por `tira_estado()`: las dos pasadas de volúmenes modificadores, el
barrido de niebla, los quads del framebuffer, el limpiado. Cada uno llama a
`gl_estado_olvidar()`, y **a la entrada de la función, no a la salida**: si esa función
crece y agrega otra llamada, sigue cubierta.

Si alguien agrega un sitio nuevo y no lo hace, la sombra miente y se dibuja mal, en silencio
y según la escena. **Pasó dos veces.** Una con `plantilla_para()`, que tocaba el estencil por
fuera. La otra, más grave, en la segunda pasada de volúmenes modificadores: llamaba
`glDepthFunc` y `glDepthMask` directo **dentro del bucle por tira**, así que `gl_e` quedaba
diciendo lo que puso `tira_estado()` mientras GL tenía lo de la pasada, y la tira siguiente
se saltaba los suyos por "ya estaba puesto". Eran 16 234 píxeles de diferencia en el cielo y
las palmeras lejanas de Crazy Taxi.

### El barrido no puede ver esto, y hay que saberlo

Ese error sobrevivió a un barrido de 150 demos con 146 idénticas, **y no por casualidad**:
el único camino que lo ejercita es la pasada de volúmenes, y las únicas demos que la
recorren son `pvr-modifier_volume*`, que están en el censo de no deterministas y por eso
quedan fuera de la comparación por hash. El barrido tiene un punto ciego exactamente ahí.

Lo que sí lo encontró fue **comparar píxel a píxel una captura de Crazy Taxi contra la rama
de referencia**: 16 234 píxeles de diferencia, cero tras el arreglo, con las mismas 9994
escenas. Un juego ejercita esos caminos con geometría real; el parque de KOS no.

Y la aislación vale como método: dos sondas temporales, una puenteando la sombra —la
diferencia desaparecía— y otra puenteando la caché de filtros de textura —no desaparecía—,
que es lo que declaró inocente a la caché.

---

# La fase 3 se cierra: el camino gráfico entero, medido, al 2026-08-03

Crazy Taxi en 3D en movimiento (1183 tiras por escena), `--salir-tras=180`, **Release**,
i9-13900. Las variantes se alternan en la misma tanda y se verifica que hicieron el mismo
trabajo antes de mirar el reloj: **22 280 053 333 instrucciones, 9994 escenas, 1183 tiras y
9 502 399 llamadas de dibujo, idénticas en las cuatro corridas válidas**.

## Primero, el agujero que tenía el reparto

`--perf` medía `dibujar_escena()` y llamaba "resto (intérprete)" a todo lo demás. Pero
`cb_tastart()` hace dos cosas **antes** de llamarla: recorre todos los vértices de la escena
para sacar la profundidad de cada tira, y ordena `TriangleStrip[]` con `qsort` —144 bytes por
elemento, del orden de mil elementos, diez mil veces—. Eso caía entero del lado del
intérprete, y un desglose que le llama "intérprete" a la ordenación de la geometría no puede
contestar si el camino gráfico pesa.

`perf_ns_cuadro` mide ahora `cb_tastart()` entera y `perf_ns_orden` la ordenación aparte:

| | ms | % |
| --- | --- | --- |
| cuadro (`cb_tastart`) | 7045 | 6,0 |
| — de eso ordenar | 704 | 0,6 |
| — de eso escena | 5914 | 5,0 |
| — de eso texturas | 931 | 0,8 |
| — de eso presentar | 338 | 0,2 |
| TA (store queue) | 1917 | 1,6 |
| **el camino gráfico entero** | **8962** | **7,6** |
| bloque periódico | 22 445 | 19,3 |
| resto (intérprete) | 84 680 | 72,9 |

**El camino gráfico entero es el 7,6 %.** El ARM7 del AICA solo, que no es gráfico, es el
14,2 % —casi el doble—.

## Las tres cosas que quedaban de la fase 3, medidas

- **La ordenación: 704 ms, 0,6 %.** Era una hipótesis razonable —`qsort` con comparador por
  puntero moviendo estructuras de 144 bytes, cuando la llave son tres bits de tipo de lista y
  el arreglo ya llega en orden de envío— y no es nada. Cambiarla por un conteo habría ganado
  medio punto.
- **La caché de texturas y su marcado por generaciones (fase 3.5): 931 ms, 0,8 %** con todo
  adentro: `vram_gen_rango64()` por tira, la búsqueda en el hash, el ligado y los filtros.
  14 585 979 consultas con 99,9 % de aciertos, 0 desalojos. Descartada.
- **Las 9,5 millones de llamadas de dibujo: 1,7 %.** Ésta es la que cierra la fase entera.

## Cómo se midió la última

`DCEMU_SIN_DIBUJO=1` saltea el `glDrawArrays` de `dibujar_tira()` y nada más. La escena sale
negra —dibuja mal a propósito—, pero el guest no lee lo que GL rasterizó, así que la ejecución
es idéntica y eso se verifica antes de comparar.

| | ms reales | `cb_tastart` | `dibujar_escena` |
| --- | --- | --- | --- |
| normal | 119 179 / 119 915 | 7104 / 7134 | 5902 / 5946 |
| sin `glDrawArrays` | 117 574 / 117 434 | 5498 / 5439 | 4308 / 4288 |

**2043 ms sobre las medianas: 1,7 %.** Ése es el techo **sumado** de todo lo que quedaba por
hacer del lado de GL:

- **el VBO (fase 3.3) no puede dar más que eso**: lo único que ataca —que el driver recorra y
  valide los arreglos de cliente en cada llamada— vive adentro de esos 1,7 %;
- **agrupar las tiras (fase 3.4) tampoco**, y de hecho bastante menos: `glMultiDrawArrays`
  ahorra la entrada al driver pero sigue teniendo que dibujar los mismos triángulos.

Las dos quedan **descartadas por medición**, no por criterio. La fase 3 está cerrada.

### La sonda que no se pudo hacer, y lo que enseñó

Había una segunda sonda pensada para separar la entrada al driver del recorrido de los
vértices: llamar `glDrawArrays` con `count` 0. **No se puede.** El driver cae con acceso
inválido leyendo `00007FF746E9D974` —pasado el final de `VertexBuffer[]`, que vive en la
imagen de dcemu—, reproducible en las tres corridas que lo intentaron. Calcula el rango como
`first..first+count-1` y con `count` 0 eso da la vuelta.

Y de rebote deja una cosa dicha que no lo estaba: **saltar las tiras de cero vértices en
`dibujar_escena()` no es una optimización, es un requisito**. Un juego deja cientos de esas
por escena —encabezados de sombra que cierran sin geometría— y si una llegara al driver, se
lleva el proceso. Está anotado en las dos guardas.

El manejador de caídas (`traza_caida_instalar()`) es lo que permitió leerlo en una corrida:
sin él el proceso desaparece y SDL se lleva `stderr` con él.

## Y el número que justificaba no agrupar cambió, aunque ya no importe

`dibujar_tira()` documentaba «26 093 408 tiras en 26 005 596 llamadas: 1,0 tiras por llamada»
y de ahí salía que agrupar no servía. Hoy son **59,2 % de las tiras sin cambio de estado, o
sea 2,45 por lote**. La diferencia no es la escena: aquella medida es **anterior** a que
`gl_ligar()` sombreara el `glBindTexture`, así que cada tira religaba y eso contaba siempre
como cambio de estado.

O sea que la razón que el documento daba para no agrupar dejó de ser cierta, y el
agrupamiento sigue sin valer la pena pero por otro motivo y con otro número. Es la segunda vez
en esta rama que una optimización invalida la medida que justificaba a otra;
`perf_tiras_sin_cambio` existe para que la próxima vez la cifra esté al día sola, en vez de
vivir congelada en un comentario.

## Dónde está la leña, entonces

| | % |
| --- | --- |
| intérprete SH-4 | 72,9 |
| ARM7 del AICA | 14,2 |
| **camino gráfico entero** | **7,6** |
| bloque periódico sin el AICA | 4,2 |

Lo que queda por delante no es gráfico. El ARM7 da 2 161 263 641 pasos con **0 % ociosos** —o
sea que no está en el salto a sí mismo de `spu_init()`, está corriendo firmware de verdad— y
cuesta casi el doble que todo el pipeline junto.

---

# Fase 5 — La instantánea de la MMU: un guest con MMU cuesta el doble por instrucción

Propuesto el 2026-08-04, con la medida hecha. **Nada de esto está implementado.**

## El número

Mismo intérprete, mismo binario, dos guests:

| | Crazy Taxi (Katana) | DCDoom (Windows CE) |
| --- | --- | --- |
| MIPS | 86,3 | **49,0** |
| ns por instrucción | 11,5 | **20,4** |
| intérprete, % del tiempo real | ~85 | **92,8** |
| camino gráfico (`cb_tastart`) | 1,4 % | 0,3 % |
| AICA total | ~10 % | 3,8 % |

DCDoom corre a **0,33x** y su pantalla se mueve a unos 10 fps. No es por ser DOOM: el PVR
dibuja **una tira por escena** —el blit por software de DOOM, un cuadro texturado— y cuesta
el 0,3 %. Es por ser Windows CE, que es el único guest del árbol que enciende la MMU.

## De dónde salen los 8,9 ns de diferencia

De `main_loop()`, y está a la vista. Las excepciones generales del SH-4 son de reejecución,
pero los manejadores de dcemu mutan registros alrededor del acceso —`MOV.L @Rn+`, un `FMUL`
sobre su propio destino—, así que antes de **cada** instrucción se saca una instantánea para
poder abortar y rehacer:

```c
memcpy(&instantanea_contexto, &core.context, sizeof(context_t));
memcpy(&instantanea_fr, core.context.FR_BANK, sizeof(FPR_BANK));
memcpy(&instantanea_xf, core.context.XF_BANK, sizeof(FPR_BANK));
```

Tres copias —el contexto entero y **los dos bancos de coma flotante**— más el `setjmp`, por
instrucción. Lo gobierna `excepcion_vigilar`, que vale 1 si la MMU traduce, si `SR.FD` está
puesto o si hay alguna habilitación de excepción de FPU. En todo lo demás del árbol vale 0 y
el camino rápido no copia nada: por eso Katana no lo paga y CE sí.

## Las tres salidas, de menos a más invasiva

1. **No copiar los bancos de FPU salvo en instrucciones de FPU.** Son dos de las tres copias
   y la inmensa mayoría de las instrucciones son de enteros. La clasificación tiene que salir
   de `opcodes[]` y expandirse en `initopcodes()`, junto a las cuatro tablas de despacho, para
   que no pueda divergir de los manejadores —la misma regla que ya sigue todo lo demás en ese
   archivo—.
2. **No sacar instantánea en las instrucciones que no pueden fallar después de mutar.** Una
   falta de búsqueda ocurre *antes* de ejecutar, así que no hay nada que deshacer; sólo las
   que tocan memoria de datos necesitan la red. Entre un 30 y un 40 % de las instrucciones del
   SH-4 son accesos, así que esto quita la instantánea de la mayoría. Misma tabla derivada que
   el punto 1, un bit más.
3. **Deshacer en vez de copiar**: que los manejadores anoten (registro, valor viejo) sólo
   cuando `excepcion_vigilar` esté puesto. Es la que más rinde y la única que toca los
   manejadores uno por uno; no vale la pena hasta agotar las dos anteriores.

Expectativa: si los 8,9 ns son la instantánea y las dos primeras salidas la quitan del 60-70 %
de las instrucciones, quedaría en unos 14 ns, o sea **~1,4x en cualquier guest con MMU**. Hay
que medirlo, no darlo por hecho: el `setjmp` sigue ahí y el reparto puede tener más de un
sumando.

## Las barandas, que aquí no son opcionales

Esto toca el camino por el que se reejecuta una instrucción que faltó, y equivocarse deja al
guest con medio registro escrito y sin síntoma inmediato:

- Las 21 suites y **`dcemu_sh4json` bit a bit**, que es la que ve un registro restaurado de
  más o de menos.
- `demos/mmu-mapeo` y `basic/mmu/*`, que ejercitan la falta de verdad.
- **DCDoom con captura byte a byte**: 977 escenas en 35 s emulados, jugando el demo de E1M1.
  Es el único guest del árbol que enciende la MMU, así que es la única prueba de extremo a
  extremo que existe para esto.
- El barrido de 150 demos, que **no puede ver nada de esto** —ninguna enciende la MMU— y por
  eso no sirve de baranda aquí. Conviene saberlo antes de confiarse.

---

# Fase 6 — Baseline del 2026-08-04, y la fase 5 apuntaba al lugar equivocado

Release, i9-13900, con la instrumentación de MMU que este trabajo agregó a `--perf`.
**La conclusión de la fase 5 —que los 8,9 ns de diferencia por instrucción eran la
instantánea— es falsa. La instantánea vale 3,6 %; el recorrido de la UTLB vale 42 %.**

## Los tres bancos, en una sola tanda

| banco | s emulados | ms reales | velocidad | fps | ns/instr | MIPS |
| --- | --- | --- | --- | --- | --- | --- |
| Crazy Taxi (Katana, sin MMU) | 180 | 116 805 | **1,54×** | 85,6 | 5,2 | 190,7 |
| Virtua Tennis PAL (sin MMU) | 180 | 132 576 | **1,36×** | 79,6 | 5,6 | 178,1 |
| DCDoom (Windows CE, con MMU) | 35 | 105 138 | **0,33×** | 14,1 | 19,4 | 51,7 |

Dos vueltas de cada uno, alternadas en la misma tanda, y repiten: 115 764 ms Crazy Taxi
(0,9 %), 132 556 Virtua Tennis (**0,02 %**) y 105 104 DCDoom (**0,03 %**).

> **Una tabla armada con corridas de tandas distintas no se puede leer, y esto casi costó
> otra conclusión falsa.** El primer baseline de Crazy Taxi dio 133 643 ms; el mismo binario
> con el mismo trabajo —22 279 918 868 instrucciones, idénticas a menos de 100— dio 116 805
> minutos después. Un 13 %, más grande que casi todo lo que vale la pena medir. La causa es
> conocida y estaba anotada sólo para `--clean-first`: **hay que descartar la primera pasada
> de cualquier binario recién enlazado**, no sólo después de una compilación limpia.

Y el reparto, que es lo que decide dónde trabajar:

| | Crazy Taxi | Virtua Tennis | DCDoom |
| --- | --- | --- | --- |
| intérprete | 71,6 % | 71,9 % | **93,0 %** |
| ARM7 del AICA | 15,3 % | 13,6 % | 3,3 % |
| camino gráfico entero (cuadro + TA) | 6,9 % | 9,2 % | 0,3 % |
| bloque periódico sin el AICA | 5,1 % | 4,4 % | 3,1 % |

El trabajo de cada corrida, que es lo que permite compararlas después:

- Crazy Taxi: 22 279 918 868 instrucciones, 10 001 cuadros, 9994 escenas, 1183 tiras/escena.
- Virtua Tennis: 23 611 808 332 instrucciones, 10 551 cuadros, 10 542 escenas, 1925
  tiras/escena, pico de 22 731 vértices.
- DCDoom: 5 433 014 158 instrucciones, 1482 cuadros, 977 escenas, **1 tira por escena** —el
  blit por software de DOOM es un cuadro texturado, y por eso el PVR no aparece por ningún
  lado en esa columna—.

**Los dos guests sin MMU ya pasan la velocidad de consola.** Lo que queda ahí es margen, no
déficit. El déficit está entero del lado de la MMU, que corre a un tercio.

> La fase 5 daba 86,3 MIPS y 11,5 ns para Crazy Taxi. Sobre el banco canónico —180 s, en
> juego, Release— son 190,7 y 5,2. Con eso se cae también la relación que reportaba: el guest
> con MMU no cuesta el doble por instrucción sino **casi cuatro veces**.

## El desglose de la MMU, que hasta ahora era un solo número

`--perf` le llamaba "resto (intérprete)" al 93 % de DCDoom. Adentro hay tres cosas que piden
acciones distintas, y ahora se cuentan por separado (ver `perf.h`):

```
instantaneas           5105350540 (0.94 por instruccion)
... restauradas           4073050 (1 de cada 1253)
busqueda: fallos         94181047 (1.733 % de las instrucciones)
datos: traducciones    2018178493 (0.37 por instruccion), 846105 faltas
... por la UTLB        1313078123, recorrido medio 34.5 de 64, 33.9 % repiten pagina
```

Las tres lecturas cambian el plan:

- **La instantánea se tira el 99,92 % de las veces.** Se toma en casi toda instrucción —el
  0,94 es 1 menos las ranuras de retardo, que quedan cubiertas por la del salto— y se
  restaura una de cada 1253. Eso no la hace gratis, pero dice que el arreglo correcto es *no
  tomarla*, no tomarla más barata.
- **La caché de página de la búsqueda de instrucciones funciona**: 1,73 % de fallos. Ese
  camino está resuelto y no hay nada que hacer ahí.
- **El camino de datos no tiene caché ninguna**, y se nota: 1313 millones de recorridos con
  **34,5 entradas de promedio sobre 64**, cada vuelta desempaquetando la máscara de página de
  su entrada. Son unas 45 mil millones de vueltas de ese bucle —8,3 por instrucción emulada—.
  Encima, cada acceso hace un read-modify-write de `MMUCR` por el avance de URC.

> Precisión del instrumento: `utlb_pasos` suma las vueltas de **todos** los que recorren la
> UTLB, y el promedio se divide por las traducciones de datos solas. Como la búsqueda de
> instrucciones también recorre en sus 94 millones de fallos, 34,5 es cota superior y el
> promedio real por recorrido ronda 32. La conclusión no depende de eso: el 1,73× está medido
> de frente, no derivado de esta cifra.

## Las tres sondas

Todas en **el mismo binario**, elegidas por una variable de entorno leída una vez al
arrancar. Es a propósito: comparar dos compilaciones mete el layout como variable, y este
árbol ya perdió una sesión por eso (`sh4zam-bruces_balls`). Así la A y la B difieren en un
`getenv` del arranque y en nada más.

### Tanda 1 — la instantánea: 3,6 %

| | ms reales | ns/instr | |
| --- | --- | --- | --- |
| base | 107 774 | 19,8 | — |
| `DCEMU_SONDA_SIN_BANCOS_FPU=1` | 103 871 | 19,1 | **3,6 %** |

Saltea las dos copias de los bancos de coma flotante —128 de los ~310 bytes— en tomar **y**
en restaurar. Trabajo idéntico al dígito, contadores de MMU incluidos.

Extrapolando por bytes copiados, **el mecanismo entero no llega al 10 %**. La fase 5 esperaba
de ahí un 1,4×.

### Tanda 2 — el recorrido de la UTLB: 1,73×

`DCEMU_SONDA_UTLB_CACHE=1` instala una caché de traducción de datos de 256 entradas, mapeo
directo por los bits de 4 KB, con etiqueta completa (página con su propia máscara, ASID,
modo). **No saltea ninguna comprobación**: devuelve el mismo índice de entrada que habría
devuelto el recorrido, y la protección, el bit D y la primera escritura se evalúan igual
sobre la entrada real. El avance de URC se mantiene, porque de él depende qué entrada elige
el `LDTLB` del manejador de recarga de Windows CE —sin eso la corrida con sonda tomaría otro
camino y no sería comparable—. La invalidación cuelga de `mmu_fetch_invalidar()`, que los
cuatro sitios que mutan la TLB ya llaman.

| vuelta | base | con caché | |
| --- | --- | --- | --- |
| 1 | 105 113 ms | 60 551 ms | −42,4 % |
| 2 | 104 612 ms | 60 942 ms | −41,7 % |

**1,73×: de 0,33× a 0,58×, de 19,3 a 11,1 ns por instrucción, de 51,7 a 89,7 MIPS**, y de
14,1 a 24,5 cuadros por segundo real, que es lo que se ve en pantalla. El recorrido medio
baja de 34,5 entradas a 4,2.

La prueba de que las cuatro corridas son la misma ejecución es la más fuerte que admite este
banco: **5 433 014 158 instrucciones, 1482 cuadros, 977 escenas, 2 018 178 493 traducciones,
846 105 faltas y 94 181 047 fallos de búsqueda, idénticos en las cuatro**. Y el BMP de
`--captura-gl` sale **byte a byte igual** con la sonda y sin ella (`36578F59…`), jugando E1M1
con el HUD puesto.

### Tanda 3 — la sonda que rompió el guest, y lo que enseñó

`DCEMU_SONDA_SIN_INSTANTANEA=1` saltea las tres copias. El guest se desbarranca, como estaba
previsto, y el número no sirve: 6 979 557 404 instrucciones en vez de 5433 millones, la
instantánea restaurada **1 de cada 16** y 434 millones de faltas contra 846 mil. Cae en una
tormenta de recargas de TLB de la que no sale, y la pantalla queda negra.

Sirve igual para dos cosas: confirma que el mecanismo es portante —no es andamiaje muerto— y
deja registrado que **el techo de la instantánea no se puede medir apagándola**. Hay que
sacarlo por partes que preserven la corrección, que es lo que hace la tanda 1.

## El plan, en orden de beneficio medido sobre riesgo

| # | qué | medido | riesgo | esfuerzo |
| --- | --- | --- | --- | --- |
| 6.1 | **Caché de traducción de datos** (la sonda, hecha en serio) | **1,73× en DCDoom** | medio | bajo |
| 6.2 | Volver a medir el reparto de DCDoom con 6.1 puesta | — | ninguno | trivial |
| 6.3 | No sacar instantánea en las que no pueden fallar tras mutar | ≤5 % | medio | medio |
| 6.4 | No copiar los bancos de FPU salvo en instrucciones de FPU | 3,6 % | bajo | medio |
| 6.5 | El ARM7: `aica_fiq_pendiente()` en línea y camino corto de búsqueda | a medir, sobre 13-15 % | bajo | bajo |
| — | Deshacer en vez de copiar (salida 3 de la fase 5) | — | alto | alto |

### 6.1 — La caché de traducción de datos

Es la sonda convertida en implementación. Lo que hay que resolver para que deje de serlo:

- **La etiqueta no incluye `MMUCR.SV`**, y de él depende si el ASID participa de la
  comparación. Queda cubierto porque escribir `MMUCR` invalida, pero hay que dejarlo dicho en
  el código: es el invariante que un cambio futuro rompería en silencio.
- **La etiqueta tampoco incluye lectura contra escritura**, y no hace falta: los permisos y
  el bit D se evalúan sobre la entrada real. Es lo que hace que la caché no pueda cambiar una
  decisión, y por eso las cuatro corridas dan la misma cuenta de faltas.
- **Las páginas de 1 KB comparten ranura de a cuatro**, porque el índice usa los bits de
  4 KB. Es conflicto de caché, no error —la etiqueta lo atrapa—, pero conviene medirlo antes
  de fijar la geometría.
- **256 entradas es el número de la sonda, no una medida.** Con 33,9 % de aciertos para una
  sola entrada y 4,2 de recorrido medio con 256, barrer tamaños es una tarde y dice si 64
  alcanzan.
- **Los otros dos caminos que recorren la UTLB deberían compartirla**: `traducir_busqueda()`,
  que son 94 millones de recorridos completos, y `mmu_traducir_sq()`, el destino de las store
  queues con MMU —que es justo lo que usa el ddraw de CE—.

### 6.2 — Volver a medir antes de seguir

Con la caché puesta DCDoom queda en 11,1 ns contra los 5,2 de Crazy Taxi. Esos ~6 ns que
sobran son la instantánea, el `setjmp`, la traducción de la búsqueda y el trabajo propio de
CE —94 millones de fallos de búsqueda, 846 mil faltas y 4 millones de reejecuciones son
código del guest que hay que interpretar igual—. **No están repartidos todavía.** Elegir
entre 6.3 y 6.4 sin ese reparto es exactamente el error que este documento acaba de corregir.

### 6.3 y 6.4 — La instantánea, con la expectativa corregida

Siguen valiendo la pena y siguen siendo lo que decía la fase 5 —clasificación derivada de
`opcodes[]` y expandida en `initopcodes()`, para que no pueda divergir de los manejadores—,
pero **por un 5-9 % entre las dos, no por un 1,4×**. El orden va al revés que en la fase 5:
6.3 (no tomarla) domina a 6.4 (tomarla más barata), y si 6.3 cubre el 60-70 % de las
instrucciones, 6.4 casi no agrega.

#### 6.3 se intentó y se revirtió: la clasificación tiene un agujero

Escrita como decía el plan: una tabla de un byte por codificación, derivada de `opcodes[]` en
`initopcodes()`, con la omisión en «sí necesita» y una lista blanca corta de tipos de operando
sin memoria y sin FPU, más la exclusión por mnemónico de los saltos con operando de registro
(`BRAF`, `BSRF`). Bajó las instantáneas de 0,94 a **0,51 por instrucción**, o sea que la
clasificación hacía lo suyo.

**Y rompe a DCDoom.** 5 291 902 937 instrucciones en vez de 5 433 052 826, 2100 cuadros en vez
de 1482, y la captura con otro hash. Alguna instrucción que sí puede abortar quedó marcada
como incapaz, y el guest se corrompe en silencio.

Lo que hay que saber antes de volver a intentarlo:

- **Las 21 suites, los 113 191 casos de SingleStepTests, `demos/mmu-mapeo` y las dos demos de
  MMU de KOS pasaron todas, en verde, con el árbol roto.** Lo único que lo vio fue la captura
  byte a byte de DCDoom. Ninguna de esas pruebas ejerce el camino: `dcemu_sh4json` no enciende
  la MMU, y las demos de MMU levantan una falta y no cientos de miles.
- La sospecha que se descartó por el camino: la tabla **asignaba** mal —limpiaba la marca sin
  reponerla, así que una fila posterior que reclamara la misma codificación se quedaba con la
  bandera de la anterior—. Se corrigió y el guest siguió rompiéndose igual, con la cuenta de
  instrucciones idéntica al dígito, así que las filas de `opcodes[]` no se solapan y el
  agujero es otro.
- Queda como trabajo de auditoría fila por fila, no de lista blanca por tipo de operando: hay
  que ir a los manejadores y ver cuáles pueden llegar a `excepcion_abortar()`. Los tres únicos
  orígenes de aborto son `mmu.c`, `floatsimple.c` y `run()`, así que la pregunta está acotada
  —pero el modo de fallo es medio registro escrito sin síntoma, y eso no se audita por
  mnemónico—.

### 6.6 — Las direcciones que no se traducen, en línea *(hecho, y no dio nada)*

`mmu_traducir()` devuelve P1 y P2 sin tocar después de dos comparaciones, y con Windows CE eso
es **el 35 % de los accesos a datos**: 688 de los 2018 millones de una corrida. Probarlo en el
macro de `memread`/`memwrite` (`MMU_SIN_TRADUCIR`) los saca del todo —las traducciones bajan a
1330 millones— y **el reloj no se movió**.

Eso vale como resultado: **la llamada no era el costo**. Con 688 millones de llamadas menos y
cero diferencia medible, lo que queda del sobrecosto de la MMU no está en entrar y salir de
`mmu_traducir()`. Se deja puesto porque además arregla la cifra de aciertos de la caché de
traducciones resueltas, que pasa a leerse **97,7 %** en vez de 64,4 %: el denominador dejó de
incluir las que nunca necesitaron caché.

### 6.5 — El ARM7, que es lo único grande que queda en los guests sin MMU

Con el camino gráfico cerrado en 7-9 % y el bloque periódico sin AICA en 4-5 %, el ARM7 es el
15,3 % de Crazy Taxi y el 13,6 % de Virtua Tennis. De la fase 1 de `interprete-plan.md` ya
están hechas 1.1 (`(op >> 28) == 0xE` antes de `condicion()`) y 1.2 (la cobertura detrás de
`arm7_cobertura`); quedan **1.3**, `aica_fiq_pendiente()` cruzando unidad de traducción una
vez por instrucción de ARM, y **1.4**, un camino corto de búsqueda en `arm7_leer()` —siempre
4 bytes y casi siempre RAM de onda—.

## Las barandas, que aquí no son opcionales

Ya corridas contra la instrumentación de este trabajo:

- **21/21 en `ctest`** y **113 191 ok / 0 fallan en `dcemu_sh4json`** bit a bit, idéntico a la
  línea documentada.
- **DCDoom con captura byte a byte**: el BMP con la sonda y sin ella dan el mismo SHA-256.

Pendientes para cuando 6.1 deje de ser sonda:

- `demos/mmu-mapeo` y `basic/mmu/{nullptr,pvrmap}`, que ejercitan la falta de verdad y la
  reejecución —lo único que prueba que la traducción resuelve a la página correcta y no a
  otra—.
- El barrido de 150 demos **no puede ver nada de esto**: ninguna enciende la MMU. Es la única
  parte del árbol donde el barrido no sirve de baranda, y conviene saberlo antes de
  confiarse.

---

# Fase 6, implementada: DCDoom de 14,1 a 29,0 fps

Todo medido sobre DCDoom, 35 s emulados, Release, i9-13900. Cada paso se alterna con el
anterior en la misma tanda y con una pasada de calentamiento descartada.

| paso | ms reales | velocidad | fps | ns/instr |
| --- | --- | --- | --- | --- |
| punto de partida | 105 138 | 0,33× | 14,1 | 19,4 |
| + caché de traducción sobre la UTLB | 60 610 | 0,58× | 24,5 | 11,2 |
| + `setjmp` fuera del bucle de instrucciones | 55 083 | 0,64× | 26,9 | 10,1 |
| + caché de búsqueda de 64 y de traducciones resueltas | 53 385 | 0,66× | 27,8 | 9,8 |
| + permisos por separado, y los bancos de FPU sólo si hacen falta | 51 700 | 0,68× | 28,7 | 9,5 |
| **+ generación por entrada de UTLB** | **51 932** | **0,67×** | **28,5** | **9,6** |

**2,03× en total**, medido alternando los dos binarios en una sola tanda —ver abajo, que es
donde está el número que vale, junto con el 8 % que esto le cuesta hoy a los guests sin MMU
por disposición del binario—.

(Las filas intermedias salen de tandas distintas y sirven para atribuir cada paso, no para
sumarlas: la máquina derivó un 10 % a lo largo de la sesión.)

## Los cinco cambios

1. **La caché de traducción sobre la UTLB** (`utlb_buscar`), compartida por los tres caminos
   que traducen. Guarda el **índice de entrada**, no la traducción, que es lo que la hace
   segura: la protección, el bit D y la primera escritura se siguen evaluando sobre la
   entrada real.
2. **El `setjmp` fuera del bucle.** Era uno por instrucción emulada —5100 millones en 35 s—
   y en MSVC guarda el contexto de registros y el marco de SEH. Ahora se arma una vez por
   vuelta del bucle exterior y el `longjmp` aterriza ahí. Vale **1,135×** por sí solo.
3. **Un segundo nivel para la búsqueda de instrucciones**, de 64 entradas, detrás de la
   página única que ya existía. Atiende el 78 % de sus 94 millones de fallos.
4. **Una caché de traducciones ya resueltas** (`mmu_datos`), que además de la entrada guarda
   la física compuesta y **qué tipos de acceso ya pasaron todas las pruebas**. El permiso va
   en un campo aparte y no en la etiqueta: en la primera versión iba en la etiqueta y leer y
   escribir la misma página se expulsaban a cada acceso —el patrón exacto de un blit—.
5. **Generación por entrada de UTLB.** `LDTLB` reemplaza una entrada de 64 y vaciaba las
   tres cachés enteras: **1 091 201 veces**, una cada 1850 traducciones. Ahora incrementa la
   generación de esa entrada y nada más; los vaciados completos bajan a **461**.

Y una que no es caché: **los dos bancos de coma flotante entran en la instantánea sólo si la
instrucción puede escribirlos**, decidido por `es_instruccion_fpu()` —la misma definición del
manual que gobierna la trampa de `SR.FD`— y evaluado dentro de `run()`, que es lo que cubre
también las ranuras de retardo.

## Cuatro hipótesis medidas, tres refutadas

Vale anotarlas porque cada una parecía obvia:

- **Que las escrituras a PTEH mataban la caché.** Se separó la invalidación en dos —la
  página única, que no lleva ASID, y las cachés etiquetadas por ASID, que no la necesitan— y
  el resultado fue **cero**: la tasa de aciertos no se movió ni una décima.
- **Que faltaba tamaño.** Barrido dentro de un mismo binario: 1024 → 62,4 %, 4096 → 63,9 %,
  8192 → 63,9 %. Ocho veces más entradas compran punto y medio. Saturado.
- **Que faltaba asociatividad.** El contador de tipo de fallo dijo que sólo el 19-34 % eran
  la misma página con otra etiqueta. Tampoco era eso.
- **Que el vaciado por `LDTLB` era la causa.** Esta sí era real —de un millón a 461— pero la
  tasa de aciertos subió apenas medio punto, y ahí apareció el error de medición.

**Y un instrumento que tampoco sirvió.** Se cronometraron `mmu_traducir()` y la instantánea
con el muestreo de `PERF_MARCA_MUESTRA`, que es lo que mide bien el bloque periódico. A esta
escala no funciona: aquello dura microsegundos y esto nanosegundos, así que el par de
`QueryPerformanceCounter` cuesta más que lo que envuelve y multiplicar por 1021 lo agranda. La
instantánea informó **235,9 % del tiempo real**, que es la manera en que un instrumento avisa
de que no sirve. Queda anotado en `perf.h`: a esta escala hay que medir implementando el
cambio y alternando dos binarios, no cronometrando adentro.

**El error de medición, que es la lección de todo esto.** `datos: traducciones ... % ya
resueltas` usaba como denominador *todas* las traducciones, y el **35 %** de ellas son
direcciones de P1 o P2 que no pasan por la TLB y salen por un `return` temprano sin mirar
ninguna caché. Contadas como fallos, hacían parecer que la caché rendía el 64 % cuando contra
lo que de verdad hay que traducir sirve el **99 %** —de 1300 millones de traducciones por TLB,
sólo 14,9 millones llegan a buscar entrada—. Tres de las cuatro hipótesis se persiguieron
contra una cifra que medía otra cosa. Está anotado en `perf.h`, junto al contador.

## El A/B que decide, y la regresión que destapó

Todo lo de arriba se midió comparando corridas de tandas distintas, y **esta rama ya sabía
que eso no se puede**. La máquina derivó a lo largo de la sesión: Crazy Taxi, con el mismo
trabajo al dígito, pasó de 116 805 a 128 217 ms en unas horas de banco —un 10 %— sin que
nadie tocara su camino. Toda conclusión sacada de comparar tandas es sospechosa por
construcción.

Lo único que vale es alternar los dos binarios en la misma tanda. Se construyó el de HEAD
(`git stash` de los ocho archivos de código, compilar, guardar el `.exe`, `git stash pop`) y
se corrieron alternados:

Y destapó una regresión que ninguna medición cruzada había podido ver, con el bucle de
instrucciones partido en su propia función (`bucle_instrucciones()`):

| banco | antes | después | |
| --- | --- | --- | --- |
| **DCDoom** | 111 900 / 121 661 ms · 12,7 fps | 50 956 / 52 501 ms · 28,6 fps | 2,25× |
| **Crazy Taxi** | 123 897 / 124 053 ms · 1,45× | 129 684 / 129 953 ms · 1,38× | **−4,7 %** |

**Un 4,7 % en un guest que no ejecuta una sola línea de lo que se agregó** —con `MMUCR.AT` en
cero no se llama a `mmu_traducir()` ni se toma instantánea—, así que sólo podía ser código
generado. La partición se había hecho para sacar el `setjmp` del cuerpo de la función caliente
(MSVC compila conservadoramente toda función que lo contenga), y esa hipótesis venía de una
comparación entre tandas, o sea de nada.

**Revertida la partición, esa regresión desapareció entera:**

| banco | antes | después | |
| --- | --- | --- | --- |
| **DCDoom** | 107 342 / 107 122 ms · 0,33× · 13,8 fps | **50 221 / 50 202 ms · 0,70× · 29,5 fps** | 2,13× |
| **Crazy Taxi** | 123 497 / 122 623 ms · 1,45× | 123 564 / 123 044 ms · 1,45× | ±0,3 % |

**El `setjmp` fuera del bucle sigue puesto y sigue valiendo su 13,5 %**: nunca hizo falta
partir la función para eso. La partición era una hipótesis sobre el código generado que costó
un 4,7 % y no daba nada, y la única herramienta que podía verla fue alternar dos binarios en
una misma tanda.

## Pero la regresión volvió, y ese es el estado real

Repetido el mismo A/B sobre el árbol final —después de agregar y **revertir** los intentos de
6.3 y 6.6, o sea con el código ejecutable de vuelta en el estado del ±0,3 %—:

| banco | antes | después | |
| --- | --- | --- | --- |
| **DCDoom** | 106 071 / 105 471 ms · 0,33× · 14,0 fps | **52 081 / 51 932 ms · 0,67× · 28,5 fps** | **2,03×** |
| **Crazy Taxi** | 121 577 / 121 324 ms · 1,48× · 82,3 fps | 131 324 / 131 472 ms · 1,36× · 76,1 fps | **−8,0 %** |

Los cuatro pares con trabajo idéntico: 22 279 918 813 instrucciones y 9994 escenas en Crazy
Taxi; 977 escenas y 1482 cuadros en DCDoom, con la captura en el mismo SHA-256 de siempre.

**Es disposición del binario, y ya van dos veces.** Con `MMUCR.AT` en cero no se ejecuta una
sola línea de lo agregado, así que el 8 % no puede ser lógica; lo que cambia es dónde cae el
código caliente del intérprete cuando `mmu.c` y `main.c` crecen. La primera vez el culpable
era identificable —la partición del bucle— y revertirla lo recuperó. Esta vez el código
ejecutable es el mismo que dio ±0,3 % y la cifra volvió a −8 %: o sea que **aquel ±0,3 % fue
en parte suerte de layout**, y la variable no está bajo control.

Lo que corresponde hacer con eso, y en este orden:

1. **PGO** (`/GENPROFILE` … `/USEPROFILE`), que este plan lista desde su primera versión como
   pendiente y estima en 10-20 %. Es exactamente la herramienta para esto: ordena el código
   por el perfil de una corrida real en vez de por el orden de enlace, que es lo que hoy
   decide. Entrenado con el banco fijo de 0.3, debería absorber la varianza **y** dar de más.
2. Recién con el layout bajo control tiene sentido volver a medir 6.3 y 6.6, porque hoy
   cualquier cambio de tamaño de `mmu.c` mueve la cifra más que la optimización que se mide.

## PGO, que era la respuesta: el canje desaparece y los tres bancos mejoran

Hecho. `-DDCEMU_PGO=GEN` → `herramientas\pgo.ps1` → `-DDCEMU_PGO=USE`, con el `.pgd` fuera de
`build/` para que sobreviva a un borrado. Medido alternando el mismo código con y sin perfil:

| banco | sin PGO | con PGO | |
| --- | --- | --- | --- |
| **Crazy Taxi** | 134 448 / 132 346 ms · 1,33-1,36× · 74,9 fps | **112 985 / 113 053 ms · 1,59× · 88,5 fps** | **+15,2 %** |
| **DCDoom** | 52 852 / 52 301 ms · 0,66× · 28,2 fps | **48 010 / 47 371 ms · 0,73× · 31,1 fps** | **+9,4 %** |

Y contra el punto de partida de toda la fase 6:

| banco | HEAD | final (fase 6 + PGO) | |
| --- | --- | --- | --- |
| **DCDoom** | 0,33× · **14,0 fps** | **0,73× · 31,1 fps** | **2,22×** |
| **Crazy Taxi** | 1,48× · 82,3 fps | **1,59× · 88,5 fps** | **+7,5 %** |

**El 8 % de regresión no sólo se recupera: queda a favor.** Era disposición del binario, y PGO
es exactamente la herramienta que la fija, porque ordena por lo que la corrida hizo y no por el
orden en que el enlazador encontró los `.obj`.

### Las dos trampas del entrenamiento, las dos medidas

**El binario instrumentado no arranca sin `pgort140.dll`**, y falla con `0xC000007B` —«formato
de imagen inválido», que no sugiere nada—. Peor: `& $exe ... | Out-Null` no mira el código de
retorno, así que el primer entrenamiento reportó las tres corridas «ok» y terminó sin un solo
`.pgc`. `pgo.ps1` copia la DLL y **verifica que cada corrida haya dejado su archivo de perfil**,
que es la única prueba de que corrió. Y los `.pgc` caen junto al **ejecutable**, no junto al
`.pgd`.

**Un perfil sin ponderar reproduce el canje que se quería eliminar.** PGO ordena por cuentas, y
las cuentas son instrucciones ejecutadas: 90 s emulados de Crazy Taxi más 90 de Virtua Tennis
son unos 22 mil millones, contra 3,1 de los 20 s de DCDoom. Con pesos iguales el guest con MMU
queda en el 12 % del perfil y su código se va al fondo. Medido así:

| banco | sin PGO | con PGO sin ponderar | |
| --- | --- | --- | --- |
| Crazy Taxi | 1,38× · 77,0 fps | 1,60× · 89,2 fps | +15,9 % |
| **DCDoom** | 0,67× · 28,5 fps | **0,63× · 26,8 fps** | **−6,3 %** |

`pgomgr /merge:N` pondera al fundir, así que DCDoom entra con peso 7 y las dos mitades del
perfil quedan parejas. Multiplicar sus segundos emulados por siete habría dado lo mismo y
costado doce minutos de corrida instrumentada.

### Lo que esto cambia para el resto del plan

**Medir sin PGO ya no tiene sentido en este árbol.** Cualquier cambio que mueva el tamaño de
un `.c` mueve la cifra más que la optimización que se está midiendo, y eso invalidó tres
mediciones de esta misma fase. De acá en adelante el A/B se hace entre dos binarios con
perfil, y el perfil se reentrena cuando el código cambia de forma —el `.pgd` viejo sigue
aplicándose, pero describe otro programa—.

Con eso vuelven a estar sobre la mesa 6.3 (la instantánea, que rompió el guest y necesita
auditoría fila por fila) y 6.6 (P1/P2 en línea, que dio cero): las dos se midieron con el
layout suelto y ninguna de las dos cifras es confiable.

## Las barandas, en cada paso

- `ctest` **21/21** y `dcemu_sh4json` **113 191 ok / 0 fallan** bit a bit.
- **`demos/mmu-mapeo`: `TEST SUCCEEDED`** con la misma dirección física. Es la única prueba de
  extremo a extremo de la reejecución, y estos cambios tocan justo ese camino.
- `basic/mmu/nullptr` y `basic/mmu/pvrmap`: el serial **byte a byte idéntico**, incluido el
  `kernel panic: unhandled MMU exception` que `nullptr` tiene que producir.
- **DCDoom con `--captura-gl`: el mismo SHA-256 (`36578F59…`) que antes de tocar nada**, en
  cada uno de los cinco pasos. Jugando E1M1, con el HUD.

Las cuentas de instrucciones se mueven ~0,001 % (5 433 014 158 → 5 433 052 826). Es el
bloque periódico, que después de una falta se evalúa una instrucción más tarde que antes:
la reposición ahora vuelve al tope del bucle en vez de caer al final del cuerpo. Los cuadros
(1482), las escenas (977) y la captura son idénticos.

## Y por qué 60 fps no sale de aquí

DCDoom presenta **42,3 cuadros por segundo emulado**. Los fps reales son eso por la velocidad
de emulación, así que:

| para ver | hace falta | o sea |
| --- | --- | --- |
| 31,1 fps (hoy, con PGO) | 0,73× | 8,7 ns por instrucción |
| 42,3 fps | 1,00× | 6,4 ns |
| **60 fps** | **1,42×** | **4,6 ns** |

Y el techo que eso choca: **Crazy Taxi, sin MMU en absoluto, corre a 5,0 ns por instrucción**
—ése es el número con PGO; sin él eran 5,5—. Aun con una MMU de costo cero DCDoom quedaría en
unos 5,0 ns, o sea **~1,28× y ~54 fps**. Los
60 fps no están del otro lado de la MMU: están del otro lado del intérprete, y pedirían que
el despacho y los manejadores fueran aproximadamente el doble de rápidos **para todos los
guests**.

Eso es un recompilador dinámico, que este documento descarta desde su primera versión y por
razones que siguen valiendo: cambia el modelo de ejecución, rompe la relación uno a uno entre
`opcodes[]` y su suite —que es lo que hace verificable a este núcleo— y deja sin sentido
`--traza-desde`, el anillo de PC y el UBC.

**Lo que sí queda por delante, y cuánto vale como mucho:** de los 9,4 ns actuales, unos 4,2
son MMU (la diferencia contra los 5,2 de un guest sin ella). Ahí adentro quedan la instantánea
de contexto —190 bytes por instrucción, ~1 ns— y la llamada a `mmu_traducir()` con sus
comprobaciones de rango, que se podría meter en el macro de `memread`/`memwrite` para las
0,37 traducciones por instrucción. Optimista, eso deja DCDoom cerca de **0,80× y 34 fps**.
