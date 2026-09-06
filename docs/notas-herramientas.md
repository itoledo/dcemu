# Notas: las herramientas de diagnóstico

Referencia de las herramientas con que se depura dcemu, y de las lecciones de medición que
cada una costó. `CLAUDE.md` resume cuál usar para qué; esto es el detalle.

Regla general del proyecto: **los números de las opciones de diagnóstico van en hexadecimal**
(salvo los segundos de `--salir-tras`), y **las variables de entorno `DCEMU_*` van en decimal**
(`atoi`). Pasarle `3e8` a `DCEMU_PULSAR_START` se lee como 3, y el Start cae en el cuadro
equivocado sin ningún aviso — ya costó una corrida.

---

## `--traza-mem`, la herramienta del arranque del BIOS

Imprime cada dirección sin emular una vez, con el PC que la pidió, y cuando los últimos 96 PCs
se reducen a 64 o menos valores distintos a lo largo de cuatro millones de instrucciones vuelca
el anillo, **desensambla el lazo** e imprime los registros. `gdrom.c` también reporta comandos
ATA y paquetes SPI por ahí. Ver `docs/bios-boot-plan.md`.

**No todo lazo que reporta es un cuelgue** — un `memset` sobre 600 KB y una espera de vsync
disparan la misma heurística. Hay que leer el desensamblado, no el hecho de que se disparó.

Reporta cada dirección sin emular una vez, pero la deduplicación es por dirección: un guest que
se va por un puntero perdido recorre millones de direcciones *distintas*, así que hay un tope de
4096 informes. Sin él el log llegaba a gigabytes y la ventana dejaba de responder.

**Un registro leído sin case propio se reporta a sí mismo bajo `--traza-mem`** — una línea por
dirección distinta, con lo que contestó el almacén de respaldo `control_mem`. `REVISION`,
`SB_G1SYSM`, `SB_SBREV` y `FB_R_SOF1` se encontraron cada uno por las malas, un cuelgue cada uno;
esto los lista en una sola pasada.

Además reporta: la actividad del TA en los tres primeros render (tiras, vértices, cuenta de fin
de tira, tipos de vértice y el mínimo/máximo de las coordenadas), la máquina de estados de listas
(`TA_ALLOC_CTRL`, `TA_LIST_INIT`, cada fin de lista, `STARTRENDER`) y el estado GL con que sale
cada tira, los bytes escritos a la RAM de video **por ventana** con el PC de la primera escritura
a cada una, el selector de disparo y el enable de Maple con una línea por par (puerto, comando),
el PC y el PR de cada paquete SPI, qué contestó cada `REQ_SES` y el formato de disco en que quedó
la lectora.

El TA recibe vértices ya en coordenadas de pantalla, así que x tiene que caer en `0..ancho` e y
en `0..alto`; si no, la falla está en la disposición del vértice o en las colas de almacenamiento,
no en el rasterizador. Al salir también imprime cuántas escenas se rindieron y la cuenta de tiras
de las últimas doce — que es lo que separa "la demo dejó de enviar" de "la captura está mal", una
distinción que ya costó un barrido entero.

## `--watchpoint=DIR[:TAM]` — quién escribe esto

Reporta cada escritura que toque una dirección dada, con **el valor escrito** y el valor que la
dirección lee de vuelta después (difieren en cualquier registro con semántica de acknowledge —
`SB_ISTNRM`, la flash — y confundir los dos ya costó una pista falsa), más el PC y el PR que la
hicieron. El gancho está en `memwrite_fisico()` (`mem.h`) — el único lugar por donde pasa *toda*
escritura, del guest e interna por igual — y la implementación en `traza.c`. Cuesta una
comparación contra cero por escritura cuando está apagado. Es lo que contesta "quién escribe esta
variable", que es la pregunta que el arranque del BIOS no deja de plantear;
`docs/bios-boot-plan.md` recorre los dos casos que resolvió.

**`DCEMU_WATCHPOINT_MAX`** sube el tope de informes (200 por omisión): el tope fijo ya costó una
conclusión falsa — "cero escrituras en la ventana" que era el corte callado, con el aviso del
corte perdido por un filtro sobre la salida.

## `--watchpoint-lectura=DIR[:TAM]` — quién lo lee

Su gemelo, y contesta la otra mitad. Cuelga de `memread_fisico()` y reporta una vez por PC
distinto — una comparación de cadenas pasa por la misma instrucción cien veces. Es lo que
identifica, en una corrida, el código que evalúa algo que la lectora acaba de entregar.

### Los watchpoints comparan direcciones **físicas** — `DCEMU_TRAZA_TLB` traduce primero

Los dos ganchos están en `memread_fisico`/`memwrite_fisico`, **después** de la MMU. Con un
guest sin MMU da igual (la máscara `0x1FFFFFFF` colapsa P0/P1/P2 sola), pero con Windows CE la
dirección que el guest sondea es virtual y vigilar ese número es vigilar otra cosa. Ya costó
una conclusión falsa entera: «Sega Rally 2 espera a que un contador suyo en `0x00446880`
llegue a 8 y nadie lo escribe nunca». El watchpoint vigilaba la física `0x00446880` —
registros del PVR — mientras el guest leía su virtual, que la MMU mandaba a `0x005F6880`:
**`SB_TFREM`**, el espacio libre del FIFO del TA, un registro de sólo lectura cuyo valor de
reposo es 8 y que dcemu contestaba con el 0 del calloc. No era un contador, no era del juego,
y nadie tenía que escribirlo.

`DCEMU_TRAZA_TLB=DIR` (hex) es la traducción que faltaba: informa por stderr cada vez que un
`LDTLB` instala una página que contiene esa dirección virtual, con la física resultante y el
ASID — una línea por traducción distinta, no por reinstalación. Primero se traduce, después se
vigila la física.

## `--traza-desde=PC[:N[:K]]` — cómo se lee una decisión

Desensambla las N instrucciones posteriores a que el guest llegue a PC — saltándose las K
primeras llegadas, porque la ROM pasa por el mismo código dos veces, al encender y después de un
reset — imprimiendo **solo los registros que cambiaron**. Imprimir los 16 por instrucción es
ilegible y no cabe; los que cambian muestran el flujo de datos. El anillo dice dónde terminó
girando; esto dice cómo llegó ahí, que es lo que se necesita para seguir una rama en el boot ROM.
`traza_arrancar()` arma lo mismo desde dentro del emulador, y `gdrom.c` lo usa con
`DCEMU_TRAZA_ATA=cmd:N` para mirar qué hace el driver del guest con lo que la lectora acaba de
contestar.

## `--desensamblar=D:N` y `--volcar=D:N` — cómo se lee el boot ROM

El código de la ROM vive en RAM — se copia ahí y *no* está en la misma dirección dentro de
`bios.bin` — así que la única manera de leer una rutina o una de sus tablas es desde dentro del
emulador. Ambas imprimen al salir, y por eso **`--salir-tras=N` importa**: sale por el mismo
camino que cerrar la ventana, así que `traza_resumen()` corre. Matar el proceso desde afuera se
lleva el desensamblado y el volcado con él.

### `MOV.W @(disp,PC)` y `MOV.L @(disp,PC)` no resuelven su literal igual

`disasm()` trataba ambos como long. La forma word escala `disp` por 2 y *no* alinea el PC; la
forma long escala por 4 sobre `PC & ~3`. Compartir un tipo de operando hacía que el
desensamblador nombrara un literal que no era el que se leía — desviado por una cantidad
arbitraria, y de aspecto plausible, que es la clase cara de estar equivocado: es lo que hizo que
el arranque por `.cdi` se leyera como una comparación contra `0x1ab0` cuando el guest en realidad
comparaba contra `0x3030`. Ahora son tipos de operando separados (`OP_T_AT_DISP_PC_RN_W`), así
que las dos filas de `opcodes[]` no pueden divergir. `MOVA` sigue imprimiendo el `disp` crudo en
vez de resolverlo.

## La caída del emulador reporta el estado del guest

`traza_caida_instalar()`, instalado como primera cosa en `main()`. Un guest que salta al vacío
ejecuta lo que haya ahí y tarde o temprano se lleva dcemu abajo; sin esto el proceso simplemente
desaparece, SDL se lleva `stderr` con él, y el único dato que dice dónde descarriló — el PC del
guest — se pierde. El manejador imprime la excepción del anfitrión, después los registros del
guest, después el anillo de PCs con su desensamblado, después los rangos de
`--volcar`/`--desensamblar`: lo mismo que habría impreso una salida normal, en ese orden para que
una segunda falla mientras desensambla memoria rota igual deje afuera la parte importante. No
intenta continuar, no es un cazador de bugs del anfitrión, y no cuesta nada mientras nada se cae.
Windows usa `SetUnhandledExceptionFilter` (SDL no lo pisa) y todo lo demás `signal()`,
relanzando con el manejador por omisión para que igual se deje un core.

### Y en Windows, la pila del anfitrión con nombres

Detrás del informe del guest va la pila de dcemu, simbolizada con dbghelp: función,
desplazamiento, archivo y línea. Release ya se compila con `/Zi`, así que el PDB queda al lado
del ejecutable y no hay nada que configurar.

**Faltaba y costó una sesión.** El informe decía «acceso inválido a memoria en
`00007FF76CD11102` del anfitrión», que sin la base del módulo y sin el PDB a mano no nombra
nada. Lo único accionable eran los interruptores de aislamiento —`DCEMU_SIN_DIBUJO`,
`DCEMU_SIN_VOLUMEN`— o sea adivinar por bisección, una corrida por hipótesis; y en la caída que
se estaba persiguiendo **ninguno de los dos la movió**, así que no dijeron nada. Con la pila, la
misma caída sale así:

```
 8  decodificar_yuv422 + 230  (graficos.c:876)
 9  get_texture + 139453      (graficos.c:1407)
10  tira_estado + 332         (graficos.c:3229)
11  dibujar_escena + 218      (graficos.c:3296)
12  cb_tastart_cuerpo + 598   (graficos.c:2718)
```

Tres saltos hasta el sitio, en una corrida.

Dos detalles del orden, los dos por la misma razón que el resto del informe: va **después** del
informe del guest —si simbolizar cae con el proceso ya roto, lo del emulado ya salió— y está
acotado a 32 cuadros con los buffers en el marco. Si dbghelp falla se imprimen las direcciones
crudas, que es lo que se tenía antes. Las tres primeras entradas son el propio manejador y no
se saltan a propósito: restarlas a ojo obliga a saber cuántas son.

**La línea que sale puede no ser la exacta.** Con optimización el compilador mueve las cargas;
en el ejemplo de arriba la línea 876 es la escritura y el acceso que falla es la lectura de dos
líneas antes. Nombra la función y eso alcanza.

---

## Capturas de pantalla

**F5 escribe `captura.bmp` desde la RAM de video, no desde el buffer GL**
(`volcar_framebuffer()` en `graficos.c`), así que muestra lo que dibujó el guest y no lo que
rasterizó GL. Con `--traza-mem` también vuelca el anillo de PCs y lo desensambla.

**F6 escribe `captura-gl.bmp` desde el buffer GL** (`volcar_gl()`), que es la otra mitad: la
salida 3D nunca pasa por la RAM de video en dcemu, así que para una demo de PVR F5 siempre sale
negro. `--captura-gl=ARCHIVO` hace lo mismo automáticamente antes de cada swap, así que el
archivo tiene el último cuadro cuando el emulador sale — combinado con `--salir-tras`, un barrido
no necesita ventana.

**`DCEMU_CAPTURA_TODAS=1`** manda cada cuadro a su propio archivo numerado
(`DIR/f0000-ARCHIVO`, ...) en vez de sobrescribir — es como se inspecciona una animación cuadro a
cuadro, y lo que permitió diagnosticar la intro del boot ROM. **`=N` conserva un cuadro de cada
N**, que es lo que la hace usable sobre un juego: dos minutos de juego son 7 GB a un cuadro cada
uno, y uno de cada cuarenta alcanza para ver *dónde está el juego* — con `--traza-mem` cada
cuadro guardado registra su número y su instante emulado, así que "el menú aparece en esta
imagen" se convierte en un número de sondeo para `DCEMU_PULSAR_START`.

**El número va delante del nombre del archivo, no delante de la ruta**: era `"f%04d-%s"` sobre el
argumento entero, así que `--captura-gl=C:/tmp/f.bmp` producía `f0000-C:/tmp/f.bmp`, que no es
ruta de nada — 480 cuadros silenciosamente no escritos, con la única queja en `stderr.txt`, que
es precisamente donde nadie mira cuando lo que iba a mirar son los BMP.

### Lee la ventana, que es 800×600, no los 640×480 emulados

Y equivocarse en eso invalidó un barrido entero. `screeninit()` estira los 640×480 del guest
sobre la ventana completa con `glOrtho`, así que un `glReadPixels(0, 0, 640, 480)` devuelve el
rectángulo inferior izquierdo: el 20% superior y el 20% derecho simplemente faltan. Cualquier
cosa que dibuje en la banda superior — toda la familia `conio`, que pone ahí su texto — salía un
BMP casi negro y se leía como "no dibuja nada". `conio-basic` pasó de 8 píxeles no negros a 2742
con esa sola línea, y sin ningún cambio en el PVR. La lección generaliza: cuando la captura dice
en blanco, revisar las cuentas de tiras al salir antes de creerle.

### No captures la ventana: usa `--captura-gl`

Capturar el área de cliente funciona, hasta que no. En una sesión `tunnel` pasó de 3036 colores
distintos a 4 sin ningún cambio en el emulador, mientras `--captura-gl` seguía reportando 1837.
Depende del compositor del anfitrión y falla en silencio, así que un barrido entero puede salir
negro y leerse como una regresión masiva. Tres cosas más hacen que una captura *de ventana* salga
negra, y todas costaron tiempo:

- **La demo ya salió.** KOS limpia la pantalla al salir, así que cualquier cosa que termine sola
  (`video/minifont` duerme 10 s y vuelve) está negra cuando un arnés de 14 segundos mira. Hay que
  capturar antes, y tratar una ventana ausente como "terminó", no como "falló".
- **El tiempo del guest corre unas 2,5× rápido sin `--limitar`**, así que una espera del lado del
  guest transcurre antes en tiempo real de lo que el fuente sugiere: los 10 s de `thd_sleep` de
  `minifont` se acaban en unos 4 s.

Para separar "GL nunca recibió la imagen" de "la captura sale negra", `DibujarFramebuffer()` hace
`glReadPixels` de cuatro puntos del back buffer bajo `--traza-mem` (cada 300 cuadros) y los
imprime al lado de los bytes que leyó de la RAM de video.

### `stdout` y `stderr` van a archivos junto al ejecutable en Windows

`stdout.txt` y `stderr.txt` **junto al ejecutable**, no en el directorio de trabajo: la ruta se
arma con `GetModuleFileName`, así que con la construcción CMake caen en `build/Release/` (o en la
raíz de `build-clang/`) aun corriendo desde la raíz del repositorio. Buscarlos en el directorio
de trabajo se lee como "el emulador no dijo nada", y lo mismo pasa redirigiendo la salida del
proceso desde el shell (o con `-RedirectStandardError` de PowerShell), que captura cero bytes.
`--traza-mem` y los avisos de la MMU salen por ahí. Dos instancias comparten ese archivo: la
segunda en arrancar lo trunca, así que una corrida medida y una sesión de juego en vivo se pisan
los logs.

Hasta el 2026-09-05 lo hacía SDLmain (SDL 1.2); SDL3 no trae SDL_main ni redirige nada, así que
desde el paso a SDL3 lo hace `main.c` en `salida_redirigir()`, lo primero de `main()`, con las
mismas reglas — y un interruptor que antes no existía: `DCEMU_SIN_REDIRECCION=1` deja las dos
salidas en la consola, para usar el emulador a mano. Ningún guion del banco la pone: todos leen
`stderr.txt`.

---

## Cómo se llega a una pantalla de juego sin nadie delante

`DCEMU_PULSAR_START=N[,N2,...]` y `DCEMU_SOLO_A=N[,N2,...]` (con `DCEMU_PULSAR_A=1` al lado)
aprietan el botón durante 20 sondeos a partir de cada número de la lista, y hay **60 sondeos por
segundo emulado**. `DCEMU_SOLO_A=1` es el caso aparte: aprieta A cada 200 sondeos para siempre,
que llega a un menú pero después **se sale del partido** — por eso existe la lista. La forma de
encontrar los números es la película: `DCEMU_CAPTURA_TODAS=40` con `--traza-mem` deja una imagen
cada 40 cuadros con su instante emulado al lado.

**Ojo con XInput: se lee global y sin foco de ventana**, así que si alguien toca un mando mientras
corre la medición, sus pulsaciones entran — una corrida que "llegó sola al partido" resultó ser
eso.

---

## Las trazas de excepciones y syscalls (Windows CE y cualquier guest con MMU)

Tres variables existen por Windows CE y sirven para cualquier guest con MMU o syscalls por
trampa.

**`DCEMU_TRAZA_EXC=1`** (con `--traza-mem`) imprime un histograma de excepciones por segundo
emulado — recargas de TLB (040/060), syscalls de CE (0e0), FPU (820) — que es la vista macro de
si el sistema vive, quedó ocioso o tormenta, y en qué segundo cambió. **`=2`** añade el censo de
sitios de syscall pasado el arranque: una línea por (destino, llamador, proceso) distinto, que es
como se identifica qué hilo sigue vivo y qué espera. **`=3`** añade el flujo completo — cada
syscall en orden con su instante en ms — que es lo que el censo resume y la única vista del
segundo 0, donde DCDoom decidía morir.

**`DCEMU_TRAZA_SYSCALL=dest[:pr[:N[:K]]]`** (hex:hex:dec:dec) arma la traza de instrucciones en
la K-ésima aparición del syscall a `dest` desde `pr`: es `DCEMU_TRAZA_ATA` para las trampas de
CE, y la manera de leer qué hizo el guest con lo que un syscall le contestó. El flujo de `=3`
provee el dest, el pr y la K. Nota: los destinos de CE decodifican como
`0xFFFFFE01 - dest = (apiset << 9) | (método << 1)`.

**`DCEMU_TRAZA_DEPURACION=1` imprime lo que el guest manda a su propia salida de depuración.**
Windows CE la tira — su OAL no escribe ni al SCI ni al SCIF (medido con un watchpoint sobre
ambos TDR) — así que cientos de líneas en que el guest narra lo que hace no iban a ninguna parte.
dcemu intercepta `OutputDebugStringW` y `NKvDbgPrintfW` (apiset 0, métodos 14 y 23 — la
numeración de CE, no la de esta imagen) al entrar a la excepción, *antes* de que el cambio de
banco se lleve el R4 del guest, y lee la cadena con `mmu_traducir_mirar()`: una traducción que
mira sin fallar y sin avanzar URC, porque un `longjmp` desde dentro de `excepcion_entrar()`
dejaría al emulador a medio camino de una excepción. Todo el log de arranque de DOOM de DCDoom
sale por ahí, y nombró su propio bloqueo en una línea — `W_ReadLump: only read 0 of 17544 on
lump 1968`.

**`DCEMU_TRAZA_EN_MS=N[:M]`** deja puntos de control por milisegundo de PC y registros para los
primeros 200 ms, más la traza de instrucciones de `--traza-desde` armada al cruzar un *instante*
emulado en vez de un PC: lo que se necesita cuando dos corridas divergen y nada dice dónde. Es la
herramienta que encontró `SB_SBREV`.

---

## `DCEMU_TRAZA_ESCENA` — una escena entera, tira por tira

Es la herramienta para "la geometría llega y aun así se ve mal". `=N[:M]` toma M escenas a partir
del número N (la cuenta `traza_rendidas`, la misma que usa el resumen de salida); **`=+K[:M]`
toma las primeras M escenas con más de K tiras**, que es como se elige una escena *de juego* sin
conocer su número — el índice no sobrevive de una corrida a la otra (un cuadro más en un menú y
se mueve), mientras que el peso separa un partido (miles de tiras) de un menú (cinco). Perseguir
el índice costó dos corridas de siete minutos que cayeron en una pantalla de transición.

Por escena imprime `PT_ALPHA_REF`, `FOG_CLAMP_MIN`/`MAX` y `FPU_SHAD_SCALE`; por tira el tipo de
lista, los factores de mezcla, los bits SRC/DST Select, el bit Offset, el modo de profundidad, el
culling, todo el estado de textura y **las palabras TSP y TCW crudas**; y por vértice la
posición, las UV, el color y el color de offset. Las palabras crudas son lo que zanja "¿este
campo se está leyendo del lugar correcto?" — decodificar `tsp=` contra la tabla de bits de
§3.7.9.2 es lo que probó que dcemu lee cada campo del TSP donde el documento lo pone.
`--traza-mem` tiene que estar encendido.

**Una escena pedida por nombre imprime todos los vértices; el volcado automático de las dos
primeras escenas sigue cortando en cuatro.** Cuatro alcanzan para ver un quad malformado, que es
para lo que estaba, y no sirven para la otra pregunta que el volcado contesta: *qué* tiras cubren
un lugar dado de la pantalla. El cuadro delimitador real de una tira de 22 vértices no se parece
en nada al que describen sus primeros cuatro, así que con el corte la caja cae en otro lado y la
búsqueda sale vacía: perseguir así un auto transparente de Crazy Taxi apuntó a una reja del fondo
durante dos rondas. Cuidado con que algunas tiras traen vértices a ±15 millones con `z` cerca de
2,1 — geometría que el chip recorta contra el plano cercano y dcemu no — así que toda caja
calculada de un volcado tiene que descartar las absurdas antes de creerle.

---

## Las palancas del muestreo, y con qué se juzgan

Tres interruptores y cuatro guiones cubren la convención de muestreo. Todos exigen
`--render=fbo --escala=1`: el camino de ventana estira 640 sobre 800 y ahí no hay muestreo 1:1 que
juzgar, así que una medida hecha en ventana no contesta la pregunta (y a la inversa, un barrido del
parque corre en ventana, que es por qué una demo puede aparecer «movida» ahí y salir idéntica a
1:1 — pasó con `pvr-fb_tex`).

| palanca | qué mueve |
| --- | --- |
| `DCEMU_SIN_MEDIO_PIXEL=1` | quita el corrimiento del `glOrtho` entero. Reproduce cualquier línea base anterior al 2026-08-06 byte a byte |
| `DCEMU_MEDIO_PIXEL_MIL=N` | lo fija en `N` milésimos en vez del valor del árbol (484). El punto de muestreo queda en `s = 0,5 − N/1000` |
| `DCEMU_MEDIO_TEXEL=1` | suma medio texel del lado de la textura. Apagado; línea base del 2026-08-14/15 |
| `DCEMU_SIN_CLAMP_BORDE=1` | las tiras cuyas UV no salen de [0,1] vuelven a `GL_REPEAT`. Línea base anterior al recorte de borde |

- **`herramientas/fbtex-ventana.ps1`** barre `DCEMU_MEDIO_PIXEL_MIL` contra `pvr-fb_tex` y es lo que
  cierra la pregunta del medio píxel. El demo mide el punto de muestreo con una ventana de dos lados
  —pasa si y sólo si `0 ≤ s < 0,5`— y el criterio no necesita imagen de referencia porque el modo de
  falla es literalmente **dos copias de media anchura**: alcanza con la diferencia media entre las
  dos mitades de una captura (52,7 cuando pasa contra 1,9 o 6,4 cuando falla). El detalle del
  mecanismo está en `notas-graficos.md`.
- **`herramientas/fbtex-caja.ps1`** intentó el otro criterio —«dos cuadros consecutivos sólo difieren
  dentro de la caja de 64×64 del cubo nuevo»— y **no reproduce**: sobre los cuadros 3..6 da cajas de
  483×85 en los dos brazos, porque ahí la estela todavía crece. Queda con el aviso para no volver a
  derivar el callejón.
- **`herramientas/costuras.ps1`** cuenta picos de costura por columna y por fila. Es lo que detectó
  que el medio texel convertía el fondo de Street Fighter III en una rejilla (`0 → 5012` por columna).
- **`herramientas/colores.ps1`** cuenta colores distintos, y **no es un oráculo de la convención**.
  Mide cuánta mezcla bilineal hay, que depende de dónde puso el guest su geometría: el logo de Crazy
  Taxi cae de 5721 colores a 612 al mover `s` a 0,5, y lo que eso cancela es el corrimiento de `x,9`
  del propio juego. Sirve para comparar dos brazos de una tira que uno ya sabe que es 1:1 y alineada,
  y para nada más. Ojo también con la trampa de PowerShell que trae anotada: `-shl` sobre un `[byte]`
  no promueve y el método viejo contaba mal.

---

## Teclas y título de la ventana

F1 pantalla completa, F2 ventana de log, **F5 volcar el framebuffer**, **F6 volcar el buffer
GL**, F9 paso, F10 parar, F11 correr, F12 vista de depuración, `p` pausa, **`f` alterna el
contador de FPS** (en el título de la ventana, cuadros presentados por segundo real; encendido
por omisión — `fps_marcar_cuadro()` en `graficos.c`), flechas + `a s d w z` = pad, `q`/`e`
gatillos, `y h g j` stick analógico, `+`/`-` del teclado numérico desplazan el volcado de
memoria. Un gamepad también sirve.

El título de la ventana lleva el nombre de lo que está corriendo (`titulo_poner()` en
`graficos.c`, llamado antes de `screeninit()`), que es lo que hace legible un barrido de demos
abiertas una tras otra.

## Dos lecciones de la noche del recompilador (2026-08-09)

**Una cadena de verificación por máquina.** La cadena nocturna original quedó viva sin
terminar y su relanzamiento corrió en paralelo con ella: las dos compartieron `build-jit`
(builds intercalados), `build-jit/Release/stderr.txt` (dos instancias se truncan
mutuamente, regla vieja aplicada a un caso nuevo) y el entrenamiento de PGO (los `.pgc`
se funden en el mismo `.pgd`). La tanda que salió de ahí reportaba los números de la
ronda anterior **bit a bit** — mismas entradas, mismos bytes, mismo milisegundo de media —
y esa imposibilidad fue la señal: seis plantillas nuevas no pueden dejar intactos todos
los contadores. La regla operativa: `Get-Process dcemu` antes de creerle a una tanda, y
jamás una segunda cadena mientras corre la primera. El expediente completo está en
`docs/recompilador-plan.md`, «la noche de las dos cadenas».

**El jitter del mando quieto vale hasta ±1672 instrucciones en Crazy Taxi.** La regla
vieja decía «si alguien toca un gamepad durante una medición, esas pulsaciones entran a
la corrida»; la noche demostró que **no hace falta tocarlo**: el ruido analógico de un
mando conectado y en reposo (el clásico 127↔128 en un sondeo) movió corridas de CT en
±51 y en ±1672 instrucciones con la captura byte-idéntica. Lo que lo distingue de una
divergencia real, medido: es **bimodal** — dos totales discretos, no una dispersión —,
el canónico reaparece en una recorrida tranquila, y los guests que no ramifican por
valores analógicos (DCDoom, Sega Rally 2) no lo muestran jamás: ellos son los árbitros
de exactitud inmunes al mando. Una divergencia de traducción real es determinista — el
mismo valor equivocado en cada corrida — y eso es exactamente lo contrario.

## Windows 11 estrangula al emulador con la ventana tapada, y el audio lo exime (2026-09-05)

Un año de notas decía que `--sin-audio` costaba 50 % (Crazy Taxi de 1,72× a 1,14×), y por eso
la captura del `.wav` y el cronómetro no podían compartir corrida. No era `--sin-audio`. Lo que
lo destapó fue la tanda de `--sin-audio` del paso a SDL3: 76 s, 92 s, 112 s y 132 s para la misma
receta de 180 s en distintas horas de la tarde, con los dos brazos moviéndose juntos, y 66 s a la
noche con la ventana libre. Tres sondas de Crazy Taxi, todas con la ejecución idéntica al dígito:

| condición | 60 s | 120 s | 180 s |
| --- | --- | --- | --- |
| ventana libre, tarjeta abierta | 20,2–20,4 s | 40,3–41,0 | 68,0 |
| ventana libre, `--sin-audio` | 18,8–19,1 | 39,7 | 65,9–66,8 |
| minimizada, tarjeta abierta | 19,7–19,8 | — | — |
| minimizada, `--sin-audio` | 18,5–18,7 | — | — |
| **tapada por otra ventana, tarjeta abierta** | — | **40,3** | — |
| **tapada por otra ventana, `--sin-audio`** | — | **66,6** | — |

Tapada y sin audio, 68 % más lento; tapada con la tarjeta abierta, nada; minimizada, nada. Es la
política de energía de Windows 11 para los procesos que considera de segundo plano: los manda a
los núcleos eficientes a baja frecuencia (EcoQoS) y les engrosa la resolución del reloj, y un
proceso que reproduce audio queda exento. Un banco desprendido cae en ese régimen sin que nadie
lo vea — aquí no fue el usuario: la última entrada de teclado o ratón fue la noche anterior, y lo
que tapa las ventanas es la pantalla apagada o el bloqueo — exactamente cuando corre sin audio:
las compuertas, los barridos y cualquier corrida con `--captura-audio`. Por eso el «costo» aparecía y desaparecía
con lo que el usuario tuviera abierto.

El arreglo es pedirle a Windows que no lo haga: `main()` llama a
`SetProcessInformation(ProcessPowerThrottling)` con los bits de velocidad de ejecución y de
resolución del reloj en la máscara de control y en cero en la de estado — «nunca» — antes de
cualquier otra cosa, y deja una línea `arranque:` en `stderr.txt`. Verificado con la misma sonda
(120 s, tapada, `--sin-audio`): 39,4 y 38,9 s con el arreglo, 64,1 s con `DCEMU_ESTRANGULAR=1`,
que es la palanca del A/B y la conducta anterior. Y dos costos que se creían y no existen:
`--captura-audio` (39,9 s escribiendo un `.wav` de 21 MB, contra 39,4 sin captura) y
`--sin-audio` (sale 1-6 % más rápido que con la tarjeta, que es lo que uno esperaría). La regla
que queda: los dos brazos de un A/B con el mismo ajuste de audio, porque la tarjeta sí cuesta
algo (41,0 contra 39,4 s); `--captura-gl` sigue sin poder compartir corrida con el cronómetro.

La lección de método es la de siempre en este árbol, del lado del instrumento: una diferencia
que aparece con una palanca no es de la palanca hasta que se demuestra que depende de ella. Aquí
la palanca cambiaba el régimen del anfitrión —con audio, exento; sin audio, estrangulable— y el
cronómetro medía a Windows.
