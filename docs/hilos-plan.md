# Plan: paralelizar el emulador

Estado: **propuesto**. Escrito el 2026-08-01 sobre la rama `rendimiento-hilos`, a partir
de `bc11bf7`. Es la fase 4 de [`rendimiento-plan.md`](rendimiento-plan.md) desarrollada
en detalle; el resto de aquel plan (build, intérprete, pipeline de GL) es independiente y
no lo bloquea.

## La regla que ordena todo lo demás

Este árbol se verifica comparando salidas **byte a byte**: los BMP de `--captura-gl` contra
el barrido anterior, el `.wav` de `--captura-audio` entre dos corridas, los floats de
`dcemu_sh4json` bit a bit. Esa metodología es lo que hace que los errores de este proyecto
sean encontrables, y es exactamente lo que la concurrencia mal puesta destruye: convierte
un error reproducible en uno que aparece una corrida de cada cien.

Así que el plan tiene **una sola regla, y no es negociable**:

> **La salida no cambia. Ni un pixel, ni una muestra.** Lo que se paraleliza es *cuándo en
> tiempo real* ocurre el trabajo, nunca *en qué orden en tiempo emulado*.

Todos los diseños de abajo están construidos para que eso sea cierto **por construcción**,
no por suerte. Donde no se pueda garantizar, la fase no se hace.

## Lo que ya es concurrente

Conviene decirlo porque cambia el punto de partida: **dcemu ya tiene dos hilos**. La
callback de audio de SDL corre en el suyo y vacía `aica_salida[]`, un anillo de un
productor y un consumidor con índices `volatile` (`aica.h:195-212`, `audio.c:80`). El
comentario de `aica.h` ya explica el patrón y por qué ahí no hay nada de SDL.

O sea que el patrón que hace falta ya está establecido en el árbol, y lo que sigue es
moverle el productor de hilo, no inventar una superficie de concurrencia nueva.

---

## La capa de portabilidad: SDL, no Boost

La pregunta era «¿boost?». La respuesta es **no**, y hay una alternativa mejor que ya está
enlazada en las tres plataformas.

### Por qué no Boost

- Es una dependencia nueva que hay que compilar por plataforma. `docs/msvc-build-plan.md`
  documenta el trabajo que costó **sacarse de encima** las dependencias de 2004-2005
  (guichan sin releases desde 2010, libcdio con autotools, `.a` de MinGW incompatibles con
  el linker de MSVC). Volver a meter una biblioteca grande para conseguir `crear_hilo()` y
  un mutex va en la dirección contraria.
- El emulador es C. Boost.Thread es C++ y arrastraría los módulos que la usen a C++, y
  `aica.c`/`arm7.c` son justamente los que **`tests/` enlaza**.
- No hace falta: lo que este plan necesita son hilos, mutex, variables de condición y
  quizá un semáforo. Es la intersección de todas las APIs de hilos que existen.

### Qué usar

**SDL 1.2, que ya es dependencia dura en Windows, Linux y macOS**, y ya provee todo:

| necesidad | SDL 1.2 |
| --- | --- |
| hilo | `SDL_CreateThread`, `SDL_WaitThread` |
| exclusión | `SDL_mutex` (`SDL_CreateMutex`, `SDL_mutexP/V`) |
| espera con condición | `SDL_cond` (`SDL_CondWait`, `SDL_CondSignal`, `SDL_CondBroadcast`) |
| contador | `SDL_sem` |

Cero dependencias nuevas, compila con MSVC y con GCC, corre donde ya corre el emulador —
y en la ruta de Windows el SDL real es sdl12-compat sobre SDL2 sobre SDL3, así que por
debajo son hilos modernos del sistema. `main.c:1385` incluso tiene un
`SDL_CreateThread(timer_check, NULL)` comentado de 2005: la idea ya estaba.

### Pero detrás de `hilo.h`

**El código del emulador no ve SDL.** Se agrega un par `hilo.c` / `hilo.h` con una API
mínima y neutra, y todo lo demás habla con eso:

```c
typedef struct hilo      hilo;
typedef struct hilo_mtx  hilo_mtx;
typedef struct hilo_cond hilo_cond;

hilo *  hilo_crear(int (*cuerpo)(void *), void * dato, const char * nombre);
void    hilo_esperar(hilo * h);

hilo_mtx * hilo_mtx_crear(void);
void       hilo_mtx_tomar(hilo_mtx *);
void       hilo_mtx_soltar(hilo_mtx *);

hilo_cond * hilo_cond_crear(void);
void        hilo_cond_esperar(hilo_cond *, hilo_mtx *);
void        hilo_cond_avisar(hilo_cond *);
void        hilo_cond_avisar_a_todos(hilo_cond *);

int     hilo_nucleos(void);     /* cuántos trabajadores tiene sentido crear */
```

Tres razones, y la primera es la que manda:

1. **`aica.c`, `arm7.c`, `g2dma.c`, `ta.c`, `vram.c` y `sistema.c` están libres de SDL a
   propósito**, para que `tests/` los enlace de verdad (ver `tests/CMakeLists.txt:5-11`:
   el binario de pruebas no enlaza ni una función de SDL). Meter `SDL_mutexP` dentro de
   `aica.c` rompe eso y se lleva puestas las suites `aica`, `arm7` y `g2dma`. Con `hilo.h`,
   `tests/` enlaza una implementación trivial de un solo hilo — o ninguna, si el diseño
   deja el AICA sin saber que hay hilos, que es lo que se busca (ver abajo).
2. Si SDL se va algún día — es plausible, la cadena SDL→sdl12-compat→SDL2→sdl2-compat→SDL3
   es frágil — cambiar a pthreads/Win32 o a C11 `<threads.h>` es **un archivo**.
3. `hilo.c` es el único lugar donde puede haber `#ifdef` de plataforma.

### Atómicos: por ahora, ninguno

SDL 1.2 no tiene atómicos, y C11 `<stdatomic.h>` es sólido en GCC/Clang pero reciente y
con banderas en MSVC. **Este plan está diseñado para no necesitarlos**: los puntos de
sincronización son poco frecuentes por construcción, así que un mutex alcanza y sobra. El
único dato que cruza sin mutex es el anillo `aica_salida[]`, que ya funciona así hoy con
índices `volatile` y un productor y un consumidor.

Si el perfil llegara a mostrar el mutex caliente, ahí se agrega `hilo_atomico.h` con
`_InterlockedExchangeAdd64` / `__atomic_*`. No antes.

---

## Resultado del paso 0

Medido el 2026-08-01 en el i9-13900, **binario Debug**, con `--perf` (`perf.c`/`perf.h`,
que se agrego para esto). Escena: `roms/Crazy Taxi (USA).cdi` por la ruta de los hooks,
`--salir-tras=40`, o sea corte por tiempo **emulado** y por lo tanto repetible — las tres
corridas dieron 2267 cuadros exactos, asi que se comparan entre si sin reservas.

```
perf: 79328 ms reales, 40014 ms emulados (0.50x)
perf: 2267 cuadros presentados (28.5 por segundo real)
perf:   AICA (mezcla)               205 ms    0.2 %
perf:   AICA (ARM7)               12465 ms   15.7 %
perf:   dibujar_escena()            687 ms    0.8 %
perf:     de eso texturas           192 ms    0.2 %
perf:   presentar                   166 ms    0.2 %
perf:   bloque periodico          29148 ms   36.7 %
perf:   TA (store queue)            148 ms    0.1 %
perf:   resto (interprete)        49177 ms   61.9 %
perf: ARM7: 522477391 pasos, 0 ociosos (0.0 %)
perf:   registro vivo                 0  (0/s)      <- lecturas del SH-4 al AICA
perf:   RAM de onda leida        417531  (5263/s)
perf:   RAM de onda escrita      378185  (4767/s)
perf:   ---- alcances forzados   795719  (10030/s)
```

### Lo que dice, en orden de importancia

**1. El PVR no es el cuello, y la hipotesis de `rendimiento-plan.md` estaba equivocada.**
`dibujar_escena()` cuesta 0,8 % del tiempo real, texturas incluidas. Las 40 000 llamadas a
GL por cuadro que ese plan estimaba no aparecen por ningun lado. **Con la reserva de que
las pantallas medidas son la advertencia de VMU y el menu de seleccion de modo, no el
atractivo 3D** — ver "Lo que falta medir".

**2. El bloque periodico de `main_loop()` cuesta 36,7 %, y eso se recupera casi entero
cambiando una constante.** El `if (core.context.cycles >= 50)` entra unas **160 millones
de veces** en 40 segundos emulados, y adentro llama a `timer_check()`, `wdt_tick()`,
`aica_tick()`, `intc_revisar_sh4()` y `dma_check()`. Descontando el AICA, que esta anidado
ahi, el resto son ~20 % del tiempo real gastados en descubrir que no hay nada que hacer.

Se probo subir la constante a 400 — sigue siendo veinte veces mas fino que el consumidor
mas rapido, que es el AICA con una muestra cada 4524 ciclos — y el resultado es
**exactamente la misma ejecucion, 19,5 % mas rapida**:

| | ciclos ≥ 50 | ciclos ≥ 400 |
| --- | --- | --- |
| tiempo real | 79 328 ms | **66 392 ms** |
| velocidad | 0,50× | **0,60×** |
| cuadros | 2267 | 2267 |
| bloque periodico | 29 148 ms | 15 930 ms |
| pasos del ARM7 | 522 477 391 | 522 477 391 |

Misma cantidad de cuadros y **el mismo numero de pasos del ARM7 hasta la unidad**: no es
que corriera menos, es que dejo de preguntar. **No esta comiteado**: cambiar la granularidad
del reloj toca `docs/clock-plan.md` y necesita el barrido de KOS antes de entrar. Pero es
la fase 2.5 de `rendimiento-plan.md` y ahora tiene un numero.

**3. El techo de esta fase es 1,19×, y el trabajo es del ARM7, no del mezclador.**
El AICA total son 15,9 %, de los cuales el mezclador es 0,2 % y el ARM7 15,7 %. Sacarlos a
otro hilo llevaria de 0,50× a 0,60× — real, pero del mismo tamano que el cambio de una
constante del punto 2, y muchisimo mas caro de escribir.

**4. El ARM7 no esta ocioso: 522 millones de pasos, cero saltos a si mismo.** Era la salida
barata — si el ARM estuviera esperando en el bucle que `spu_init()` deja puesto, no habria
que moverlo de hilo sino no ejecutarlo, y eran cinco lineas. No aplica: Crazy Taxi corre su
propio driver de sonido ahi y el trabajo es genuino.

**5. Los alcances forzados serian ~10 000 por segundo, y ninguno por registro vivo.**
Es el numero que decide si el diseno de la fase 1 funciona, y sale mejor de lo temido: el
SH-4 **nunca** lee `MONITOR_CA` ni `MONITOR_EG` (0 en toda la corrida), y toca al AICA solo
por la RAM de onda. A 10 000 sincronizaciones por segundo, con un ida y vuelta de variable
de condicion de unos pocos microsegundos, el costo se come entre un cuarto y un tercio del
beneficio — salvo que las escrituras vengan en rafagas dentro del mismo instante emulado,
en cuyo caso una sola sincronizacion sirve para toda la rafaga. **Eso hay que medirlo antes
de escribir el hilo**: contar instantes distintos, no accesos.

**6. `--sin-aica` no sirve para medir esto, y por poco no se descubre.** Con esa opcion
Crazy Taxi **no avanza**: 12 cuadros contra 2267, y **1118 millones** de lecturas de RAM de
onda (19,7 M/s). El juego se queda sondeando un handshake que el ARM nunca contesta. La
corrida "sale" 40 % mas rapida y es un espejismo completo. El desglose interno de `--perf`
existe justamente porque comparar dos corridas es fragil; aqui lo demostro por una razon
distinta de la prevista (no fue el vsync, fue la divergencia del guest).

### Y en el juego, no en los menus

La primera tanda media la advertencia de VMU y el menu de modos, las dos 2D, asi que el
punto 1 quedaba sin demostrar donde importa. Se repitio llegando al juego con
`DCEMU_PULSAR_START=300,1100 DCEMU_PULSAR_A=1 DCEMU_SOLO_A=1` y `--salir-tras=120`:
**1016 tiras por escena en promedio y 1889 la mayor**, sobre 6570 escenas — geometria 3D
de verdad, del orden que `CLAUDE.md` reporta para el atractivo.

| | menus (40 s) | **juego (120 s)** |
| --- | --- | --- |
| velocidad | 0,50× | **0,45×** |
| tiras por escena | — | **1016 (max 1889)** |
| `dibujar_escena()` | 0,8 % | **2,1 %** |
| de eso texturas | 0,2 % | 0,3 % |
| presentar | 0,2 % | 0,1 % |
| bloque periodico | 36,7 % | **33,6 %** |
| AICA (casi todo ARM7) | 15,9 % | **14,5 %** |
| resto (interprete) | 61,9 % | **63,2 %** |

**El reparto casi no se mueve.** Con mil tiras por escena el PVR se triplica y sigue
costando 2,1 %: 5,5 de 261 segundos. Las 40 000 llamadas a GL por cuadro que estimaba
`rendimiento-plan.md` no existen — con 1016 tiras son unas 15 000, y no se notan. La
conclusion 1 queda demostrada donde hacia falta.

Dos cosas que salieron de paso y valen para cualquier medicion futura:

- **`--captura-gl` cuesta 42,7 % de la corrida** (un `glReadPixels` de pantalla completa
  por cuadro). Sin cronometrarlo aparte, ese 42,7 % se habria repartido dentro de "resto
  (interprete)" y el desglose entero habria estado mal. Ahora se descuenta solo.
- **`--captura-gl` no perturba la emulacion**, solo el reloj: la corrida con captura y la
  limpia dieron los mismos 6577 cuadros, las mismas 6570 escenas, los mismos 1 448 670 936
  pasos del ARM y los mismos 5 146 966 accesos al AICA. Y el desglose limpio coincidio con
  el estimado por resta dentro del 0,5 %.

### Lo que falta medir

- **Release.** Todo esto es Debug. El interprete y el ARM7 se aceleran los dos, asi que el
  reparto probablemente aguante, pero las llamadas a GL no se aceleran nada y su fraccion
  crecera.
- **Rafagas contra accesos** en el punto 5, que en el juego se volvio la pregunta que
  decide la fase entera: 5 146 966 accesos en 120 segundos emulados son **0,97 por muestra
  de audio**. Si cada uno fuera un alcance, el hilo del AICA se detendria una vez por
  muestra y no quedaria ventana para correr en paralelo. La fase 1 depende por completo de
  que vengan en rafagas dentro del mismo instante emulado.

### Que cambia en el plan

El orden de `rendimiento-plan.md` estaba bien pero por las razones equivocadas. Con lo
medido, lo barato y grande esta todo **fuera** de los hilos: el bloque periodico (19,5 %
verificado, una linea) y despues el interprete, que es el 62 % restante. La fase 1 de este
documento sigue en pie y vale 1,19×, pero **deja de ser lo primero**.

## Resultado de la fase 1

**Implementada, correcta, y no gana tiempo: pierde entre un 4 y un 5 %.** Queda en el
arbol detras de `--hilos`, **apagada por omision**. Medido el 2026-08-01, Debug,
i9-13900, Crazy Taxi en juego, `--salir-tras=60` (3070 cuadros, corte por tiempo emulado
y por lo tanto la misma ejecucion en las dos corridas).

| | sin hilos | con hilos |
| --- | --- | --- |
| tiempo real | **214 886 ms** | 222 617 ms |
| velocidad | **0,27×** | 0,26× |
| AICA total | 31 544 ms (14,6 %) | **45 993 ms (20,6 %)** |
| bloque periodico | 72 791 ms | 53 792 ms |
| resto (interprete) | **134 236 ms** | 160 892 ms |
| esperando al AICA | — | 928 ms (0,4 %) |

### Lo que funciono

- **El protocolo es correcto y el determinismo se sostiene.** El `.wav` sale **bit a bit
  identico** al del camino sin hilos sobre **2 646 565 muestras**. Es la prueba de
  aceptacion de la fase y la pasa.
- **La sincronizacion es barata**: el SH-4 pasa **0,4 %** del tiempo bloqueado. La medida
  del paso 0 acerto — las escrituras vienen en rafagas, 0,21 alcances por acceso — y el
  diseno de "alcance a pedido" hace lo que promete.
- **El reparto se movio como debia**: el bloque periodico bajo 19 s, que es
  aproximadamente el AICA que ya no corre ahi.

### Por que igual pierde

El trabajo se fue del hilo principal, pero **se encarecio en los dos lados**:

- El mismo AICA cuesta **46 % mas** en el segundo hilo (31,5 s → 46,0 s).
- Y el interprete del hilo principal, haciendo exactamente el mismo trabajo, se frena un
  **20 %** (134,2 s → 160,9 s).

Ninguna de las dos es sincronizacion — eso son 0,9 s. Es contencion de memoria entre los
dos hilos (el ARM7 y el mezclador barren 2 MB de RAM de onda mientras el interprete barre
16 MB de RAM del sistema) y, con toda probabilidad, **colocacion de hilos**: en un
i9-13900 hay ocho nucleos de rendimiento y dieciseis de eficiencia, y nada le dice al
planificador que este hilo no va en uno de eficiencia. Un factor cercano a 1,5 es
exactamente lo que costaria esa diferencia.

**Release no lo rescata**: encoge el trabajo del emulador y deja igual el costo del
sistema operativo, o sea que empeora la relacion.

### Lo que se probo para arreglarlo, y la trampa que aparecio

Dos afinaciones del protocolo, las dos correctas y las dos insuficientes:

- **No avisar a la condicion cuando nadie espera** (un contador `esperando`). Quita 44 100
  llamadas al sistema por segundo emulado. Se queda.
- **Avanzar de a cuatro muestras** en vez de una, para dividir por cuatro el costo de
  sincronizar. Bajo la perdida de 4,9 % a 3,6 %... **y rompio el determinismo**: el `.wav`
  divergia a los 15,6 segundos con un corrimiento de dos cuadros.

Lo segundo es el hallazgo que importa, porque no es un error de implementacion sino el
**unico punto del diseno donde el determinismo no estaba garantizado por construccion**, y
estaba anotado como tal desde el principio: la entrega de la interrupcion del AICA al ASIC.
El chip levanta `aica_linea_asic` en un instante emulado y `main_loop()` la cobra en el
bloque periodico en que se entere, que depende del reloj real. Cuanto mas desacoplados van
los dos hilos, mas se corre esa entrega, y el ISR del guest escribe el AICA en otro momento.

Hacerlo exacto exigiria que el SH-4 no pase de un instante emulado sin que el AICA lo haya
procesado, o sea **lockstep**, o sea ningun paralelismo. Asi que el paso se deja en 1, que
es donde el `.wav` sale identico.

### Que hacer con esto

- **Queda en el arbol, detras de `--hilos`, apagado.** El codigo es correcto, esta medido y
  documentado, y `hilo.c`/`hilo.h` sirven igual para cualquier otra fase.
- **Vale la pena volver si** alguien mide con afinidad de hilo forzada a un nucleo de
  rendimiento — es la hipotesis mas probable y no se probo, porque fijar afinidad es
  especifico de cada plataforma y va contra el objetivo de portabilidad de esta rama.
- **Pero no es donde esta el tiempo.** El paso 0 ya lo habia dicho y la fase 1 lo confirma
  desde el otro lado: el 63 % esta en el interprete y el 33 % en el bloque periodico, del
  que ya hay un 19,5 % verificado con **una linea**. Eso es lo que sigue.

## Fase 1 — El AICA y el ARM7 en su propio hilo

El candidato claro, y el que da el número más grande de los tres.

### Qué se mueve y cuánto vale

`aica_tick()` se llama desde `main_loop()` cada 50 ciclos y hace dos cosas caras:
`mezclar_una_muestra()` por cada muestra que corresponda (44 100 por segundo emulado,
recorriendo los 64 canales) y `arm7_ejecutar(faltan * 512)` — hasta **22,6 millones de
pasos de ARM interpretados por segundo**. Y el ARM corre en **todas** las demos, no solo
en las de sonido: `spu_init()` escribe un salto a sí mismo en la dirección 0 de la RAM de
onda y suelta el reset siempre.

**Cuánto vale ya está medido: 1,19×** (15,9 % del tiempo real), y casi todo es el ARM7.
Ver "Resultado del paso 0" arriba.

~~La diferencia de fps entre una corrida normal y una con `--sin-aica` es el techo exacto.~~
**Eso era falso y la medición lo mostró**: con `--sin-aica` Crazy Taxi no avanza — se queda
sondeando un handshake que el ARM nunca contesta — así que la corrida "rápida" no está
ejecutando el mismo programa. El techo sale del desglose interno de `--perf`, no de
comparar dos corridas.

### El diseño: atrasado y con sincronización a pedido

El hilo del AICA **nunca va adelante del SH-4**. Corre detrás, poniéndose al día hasta
`reloj_total`, con un retraso máximo acotado (un cuadro).

Y esta es la parte que da el determinismo: **cualquier acceso del SH-4 que toque el estado
del AICA fuerza primero un alcance.** El hilo del AICA se pone al día hasta el
`reloj_total` exacto de ese acceso, y recién entonces el acceso se aplica. Visto desde el
chip, la secuencia de eventos y sus instantes en tiempo emulado son **idénticos** a los del
emulador de un hilo. No parecidos: los mismos. El `.wav` sigue siendo bit a bit el mismo
entre corridas, y también contra la versión de un hilo — que es la prueba de aceptación de
la fase.

Lo que fuerza un alcance:

- **Leer un registro vivo.** `AICA_MONITOR_CA` devuelve `aica_canales[canal].pos`, la
  posición de reproducción; `AICA_MONITOR_EG` devuelve el estado de envolvente y **limpia
  `dio_la_vuelta` al leerlo**, o sea que una lectura del SH-4 es también una escritura al
  estado del AICA. Van los dos, más los contadores de los tres temporizadores, `SCIPD`,
  `MCIPD` e `INT_L`. El resto de `leer_registro()` sale de respaldo plano y no necesita
  nada.
- **Escribir cualquier registro.** `aica_escribir()` entero. Un KEY ON tiene que caer en el
  ciclo en que cayó.
- **Escribir la RAM de onda** desde el SH-4: las ráfagas de G2 DMA de `spu_dma_transfer()`
  y las escrituras por store queue. Son pocas por cuadro (rellenos de buffer de flujo), así
  que un alcance por ráfaga es barato — y evita tener que copiar el dato a una cola, que
  para 64 KB sería peor que sincronizar.
- **Leer la RAM de onda** desde el SH-4, por si acaso: el ARM la escribe.
- **`ARMRST`**, que es la palanca del reset del ARM.

Lo que **no** fuerza nada — y es la mayor parte del tiempo: el SH-4 ejecutando cualquier
otra cosa. Ahí los dos hilos corren de verdad en paralelo.

Hay un detalle que juega a favor y conviene decirlo: **el ARM y el AICA van en el mismo
hilo**, así que las lecturas del ARM al estado vivo del AICA no sincronizan nada. Y el
`aica_get_pos()` que consulta el planificador de flujos de KOS es del **firmware del ARM**,
no del SH-4. O sea que el lazo de sondeo más apretado que hay queda entero del lado del
hilo nuevo.

### El protocolo, en concreto

Un mutex, una variable de condición y tres campos. El hilo del AICA mezcla de a **una
muestra** y revisa el estado entre muestra y muestra: son 4524 ciclos emulados, así que la
latencia peor de un alcance es el trabajo de una muestra — unos pocos microsegundos — y el
costo de tomar el mutex 44 100 veces por segundo es ruido.

```
  estado compartido (bajo el mutex):
    objetivo      -- hasta qué reloj_total puede avanzar el AICA
    alcanzado     -- hasta dónde llegó de verdad
    quieren_alcance

  hilo del AICA:
    tomar mutex
    mientras (alcanzado >= objetivo)  esperar en la condición
    soltar mutex
    mezclar una muestra + 512 ciclos de ARM
    tomar mutex; alcanzado = ...; si quieren_alcance avisar; soltar

  SH-4, en el punto de servicio periódico:
    tomar mutex; objetivo = reloj_total; avisar; soltar     (no espera)

  SH-4, antes de un acceso al AICA:
    tomar mutex
    objetivo = reloj_total; quieren_alcance = 1; avisar
    mientras (alcanzado < reloj_total) esperar en la condición
    ... el acceso se hace aquí, con el mutex tomado ...
    quieren_alcance = 0; soltar
```

`aica.c` no se entera de nada de esto: sigue exportando `aica_tick()`, `aica_leer()` y
`aica_escribir()` tal como están. El protocolo vive en un archivo nuevo, **`hilo_aica.c`**,
que es el único que incluye `hilo.h`, y `mem.c` llama a `hilo_aica_leer()` /
`hilo_aica_escribir()` en vez de a los de `aica.c`. Con eso `aica.c` y `arm7.c` siguen
libres de SDL y `tests/` no cambia ni una línea.

### Lo que devuelve el AICA hacia afuera

Dos cosas cruzan en sentido contrario, y las dos se resuelven sin candados nuevos:

- **Las muestras**, por `aica_salida[]`, que ya es un anillo de un productor y un
  consumidor. Antes lo llenaba el hilo principal y lo vaciaba el de audio de SDL; ahora lo
  llena el del AICA y lo vacía el de audio. Sigue siendo uno y uno.
- **Las interrupciones** (`intc_add_ext()`, `G2AICINT`). El hilo del AICA **no toca
  `intc_queuemask_ext`**: deja su propia máscara pendiente privada, y el SH-4 la incorpora
  en el punto de servicio periódico, que ya toma el mutex. Eso además la deja llegar en un
  instante determinista, que es lo que hace falta.

### Riesgos, y qué los desactiva

| riesgo | qué lo desactiva |
| --- | --- |
| El guest sondea el AICA seguido y se sincroniza todo el tiempo | Un contador de alcances forzados por segundo en el resumen de `--perf`. Se mide **antes**, con un parche de diez líneas sobre el emulador de un hilo |
| Una escritura a RAM de onda que no pasa por el gancho | El gancho va en `pvr_write()`/`pvr_read()`, donde ya está el reparto de `SOUND_BASE` (`mem.c:1006` y `2072`), que es por donde pasan las dos rutas |
| `traza`/`stderr` desde dos hilos | Un mutex en `traza.c` para las líneas del AICA, o aceptar el entrelazado. No afecta la salida medida |
| Una regresión difícil de aislar | `--sin-hilos` apaga la fase entera y vuelve al camino de hoy, igual que `--sin-aica` |

### Entregable

- `hilo.c` / `hilo.h` (la capa) y `hilo_aica.c` / `hilo_aica.h` (el protocolo).
- `--sin-hilos` en `opciones.c`.
- El contador de alcances en el resumen.
- **Prueba de aceptación:** `--captura-audio` de las siete demos de sonido y de la campana
  del boot ROM, `fc /b` contra el `.wav` de la rama `master`. Idénticos. Más `ctest` y el
  barrido de `docs/demos-kos.md` sin cambios en los BMP.

---

## Fase 2 — Decodificar texturas en paralelo

Más chica, más contenida y sin ningún problema de determinismo.

### Qué se mueve

`get_texture()` hace, en el hilo del contexto de GL: juntar el bloque con `vram64_leer()`,
destwiddlear, resolver VQ / paleta / YUV / BUMP, y decodificar la cadena de mipmaps nivel
por nivel. Todo eso es **CPU pura sobre buffers propios: sin GL, sin estado del guest**.
Solo el `glBindTexture` + `glTexImage2D` del final tiene que correr en el hilo del
contexto, porque un contexto de GL 1.x pertenece a un hilo y punto.

Pesa donde `CLAUDE.md` ya identifica el caso peor: el atractivo de Crazy Taxi, con
texturas que el juego regenera y una escena de cientos de texturas distintas.

### El diseño: una barrera por escena

1. `cb_tastart()`, antes de dibujar, recorre las tiras y consulta la caché de cada textura.
   **La asignación de ranura y la decisión de invalidación se hacen aquí, en el hilo
   principal y en orden de tira** — así el resultado no depende de cómo corrieron los
   trabajadores.
2. Las que resultan inválidas se encolan como trabajos de decodificación, cada uno con su
   ranura ya asignada y su buffer de destino ya reservado.
3. Los trabajadores decodifican en paralelo. Cada trabajo escribe **solo** en su propio
   buffer: no hay estado compartido que proteger.
4. Barrera. Se espera a que terminen todos.
5. `dibujar_escena()` corre como hoy, y `get_texture()` encuentra todo listo y solo sube.

Determinista por construcción: la decodificación es una función pura de (bytes juntados,
formato, tamaño), y el orden de asignación de ranuras lo fija el hilo principal.

### El trabajo real

Partir `get_texture()` en dos: `textura_decodificar()` (pura, sin GL — la que corre en el
trabajador) y `textura_subir()` (la que toca GL). Son ~400 líneas y el corte natural ya
existe en el código: todo lo que produce `cached_textures[i].data` de un lado, el
`glBindTexture` / `aplicar_filtros` / `glTexImage2D` / cadena de mipmaps del otro.

Dos cosas a cuidar:
- `decodificar_bump()` usa el color de offset del polígono, que es dato de la tira. Va en el
  trabajo, no se lee del global.
- La cadena de mipmaps se decodifica nivel por nivel a buffers separados; todos tienen que
  viajar en el trabajo hasta el momento de subir.

El grupo de trabajadores es un `hilo_grupo` genérico en `hilo.c` (N hilos, una cola de
trabajos, una barrera), reutilizable — la fase 3 lo usaría también.

### Entregable

- `hilo_grupo` en `hilo.c`.
- `get_texture()` partida en dos, con la fase de decodificación paralelizada.
- **Prueba de aceptación:** el barrido completo de `docs/demos-kos.md` con `--captura-gl`,
  byte a byte contra `master`. Cualquier diferencia es un error, no una aproximación.

---

## Fase 3 — El hilo de render

La grande, la de más riesgo, y **la última**. Solo si el perfil muestra que el hilo
principal y la GPU se están esperando mutuamente; si el cuello está en las 40 000 llamadas
a GL por cuadro, la respuesta es la fase 3.1 de `rendimiento-plan.md` (sombrear el estado),
que es un décimo del trabajo y ningún riesgo.

### La idea

El TA ya construye la escena entera en memoria — `VertexBuffer[]`, `TriangleStrip[]`,
`VolumeBuffer[]` — antes de que `cb_tastart()` dibuje nada. Así que el hilo del guest puede
cerrar la escena, entregarla y seguir emulando mientras otro la dibuja y la presenta.

### El problema que hay que resolver antes

**`get_texture()` lee la RAM de video en el momento de dibujar, no en el de submitir.** Si
el guest ya escribió encima — y con render diferido lo habrá hecho, porque para eso se
difiere — se decodifica la textura equivocada. Es exactamente el tipo de error que este
proyecto documenta una y otra vez: algo que se acepta sin decir nada y sale plausible.

Dos salidas, y la segunda es la buena:

- Instantanear la huella de VRAM de la escena al cerrarla. Correcto y caro.
- **Mover la decisión de invalidación al momento de la submisión**, no al del dibujo. El
  contador de generaciones por página de 8 KB de `vram.c` ya existe justamente para esto; lo
  que falta es que la escena se lleve consigo el juego de texturas ya decodificadas — que
  es precisamente lo que la fase 2 construye. **La fase 3 no es viable sin la fase 2**, y
  con la fase 2 hecha es en buena medida gratis.

### El resto

Doblar los tres buffers (~9 MB entre los tres; no es problema), mover el contexto de GL al
hilo de render — en SDL 1.2 eso significa que el hilo de render es el que llama a
`SDL_SetVideoMode` y a `SDL_GL_SwapBuffers`, y hay que revisar qué hace sdl12-compat con
eso en cada plataforma (macOS históricamente exige el hilo principal para la ventana) —, y
decidir qué pasa cuando el guest cierra una escena antes de que la anterior se haya
dibujado: se espera, que es lo correcto y lo que hace el chip.

Queda **fuera del alcance de esta rama** salvo que las fases 1 y 2 lleguen y el perfil lo
pida.

---

## Lo que no se enhebra

Repetido de `rendimiento-plan.md` porque es la parte del plan que hay que sostener cuando
aparezca la tentación:

- **El núcleo SH-4.** Intérprete secuencial sobre estado compartido.
- **El acceso a memoria.** Un candado en `memread`/`memwrite` mata el camino rápido que la
  fase 2 de `rendimiento-plan.md` construye, y el beneficio sería negativo.
- **`main_loop()` en general.** El orden de eventos entre CPU, ASIC y periféricos es lo que
  costó dos planes enteros (`clock-plan.md`, `bios-boot-plan.md`) poner bien. Un hilo mal
  puesto ahí devuelve el proyecto a la clase de error que llena `CLAUDE.md`: algo que llega
  tarde, que nadie reclama y que no deja rastro.

## Orden

| # | Qué | Bloquea a | Prueba de aceptación |
| --- | --- | --- | --- |
| 0 | Medir el techo con `--sin-aica`; contar alcances forzados | fase 1 | — |
| 1a | `hilo.c` / `hilo.h` sobre SDL | todo | `ctest` sin cambios |
| 1b | `hilo_aica.c`: el protocolo, el hilo, `--sin-hilos` | — | `.wav` bit a bit contra `master` |
| 2a | `hilo_grupo` (cola de trabajos + barrera) | 2b | — |
| 2b | Partir `get_texture()` y paralelizar la decodificación | 3 | barrido de KOS byte a byte |
| 3 | Hilo de render | — | fuera de alcance por ahora |

El paso 0 puede decidir que la fase 1 no vale la pena, y está bien que así sea: es una
tarde de trabajo contra varios días.
