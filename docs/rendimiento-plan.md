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
