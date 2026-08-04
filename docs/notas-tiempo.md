# Notas: interrupciones, relojes y DMA

Detalle de `intc.c`, `tmu.c`, `wdt.c` y el planificador de `main_loop()`. `docs/clock-plan.md` es
la bitácora del trabajo de relojes; `CLAUDE.md` tiene el resumen.

---

## Las líneas de interrupción normales del ASIC son por nivel

**Se derivan de `SB_ISTNRM` contra sus tres máscaras — la entrega no consume nada.** Solo el
acknowledge del guest (el case de `SB_ISTNRM` de `pvr_write()`) o el enmascarado bajan una línea.

Esto reemplazó una cola en dos pasos, ambos el mismo error que tenían los temporizadores antes de
`intc_revisar_sh4()` (ver `docs/clock-plan.md`):

1. Primero `check_ints()` descartaba los eventos que ninguna máscara cubriera en ese instante — el
   boot ROM habilita la máscara del DMA de Maple *después* de arrancarlo, perdía el fin de DMA
   siempre, y no volvía a sondear el bus nunca.
2. Después la entrega consumía de la cola *todos* los bits que una máscara cubriera de una vez.
   La ISR de Katana despacha **un** bit por entrada — el más bajo puesto en `ISTNRM & softmask` —,
   reconoce solo ese y vuelve, confiando en que la línea todavía asertada la haga entrar de nuevo
   por el resto: con semántica de cola Virtua Tennis recibía el render-done de video y los de
   ISP/TSP quedaban puestos sin volver a interrumpir jamás, así que su pipeline de cuadro nunca
   giraba.

`intc_asic_pendiente()` es la compuerta que usa `main_loop`, y corre en el bloque de 50 ciclos al
lado de `intc_revisar_sh4()`: con semántica de nivel la compuerta es verdadera cada vez que hay un
bit sin reconocer — casi siempre — y comprobarla por instrucción costaba 9× en Crazy Taxi.

**Los eventos con tiempo de transferencia conservan su retardo** (`intc_add(evt, cnt)`, cnt×50
ciclos): el bit de estado se enciende cuando el evento *ocurre*, no cuando el guest patea la
operación. El fin del CH2 DMA se retrasa por tamaño (`largo/200 + 10` cuentas) por la misma razón
que los fines de lista y de render: instantánea, la interrupción le ganaba al driver de Katana de
vuelta desde el camino de pateo — su propio estado decía que no había transferencia en vuelo, así
que la ISR descartaba el fin como espurio y Virtua Tennis esperaba para siempre una terminación
que ya había pasado.

El ASIC tiene **dos** registros de estado. `intc_add()` cubre el normal (`SB_ISTNRM`,
`ASIC_ACK_A`); el fin de comando de la GD-ROM llega por el externo (`SB_ISTEXT`, `ASIC_ACK_B`),
que es para lo que están `intc_add_ext()` / `intc_queuemask_ext` — `check_ints()` drena esa cola
contra las máscaras `_B`, igual que drena `intc_queuemask` contra las `_A`. `intc_remove_ext()` existe porque leer el registro de estado de la
lectora desasserta su línea de interrupción, como exige ATA.

---

## Todo lo periódico deriva de una constante y un contador

`DC_CPU_HZ = 199499520` en `tmu.h` — el mismo número que usa KOS en
`kernel/arch/dreamcast/kernel/timer.c`, así que ambos extremos concuerdan por construcción.
`reloj_total` acumula ciclos de CPU y solo sube; los consumidores periódicos guardan su propia
marca y comparan contra él en vez de acumular y restar. Vive fuera de `core.context`
deliberadamente: la instantánea de re-ejecución de la MMU restaura todo el contexto, y el reloj no
debe retroceder. `reloj_us()` / `reloj_ms()` convierten.

El reloj de periféricos es `CPU/4`, y el `TPSC` de un canal TMU lo divide otra vez, así que desde
ciclos de CPU el divisor es 16, 64, 256, 1024 o 4096. `docs/clock-plan.md` tiene la historia:
`timer_check()` solía decrementar cada `TCNT` una vez por llamada e ignorar `TPSC` por completo,
lo que hacía que el reloj de milisegundos de KOS corriera 3,125× lento — `50/16` exactamente.

La tasa de línea de barrido sale de la misma constante: `reloj_ciclos_por_linea()` en `tmu.c`
calcula `DC_CPU_HZ / (líneas × campos por segundo)` desde `SPG_LOAD.vcount` y `SPG_CONTROL` —
6345 ciclos para VGA, 6351 para NTSC, 6394 para PAL. `main_loop()` compara contra
`pvr_ciclos_linea` (`reg.c`), que `mem.c` recalcula cuando el guest escribe cualquiera de los dos
registros. Solía ser un 978 cableado, así que los cuadros venían **6,5× demasiado rápido** — lo
que significaba que `cb_tastart()` disparaba antes de que el guest terminara de enviar una escena,
rindiendo cuadros parciales. Esa era la causa de la banda corrupta a lo largo del borde inferior
de la demo `tunnel`.

---

## Los periféricos nunca entregan su propia interrupción

`tmu_tick()` y `wdt_tick()` solo fijan `TCR.UNF` / `WTCSR.IOVF`; `intc_revisar_sh4()` deriva la
petición de esos flags y la entrega cuando `SR` lo permite.

Esa distinción importa: el código viejo llamaba a `intc()` en el momento del subdesbordamiento, e
`intc()` devuelve falso cuando `SR.BL` está puesto o `IMASK` es demasiado alto — así que el evento
se **descartaba en silencio**. En hardware real la petición queda asertada hasta que el guest
limpia el flag. Descartarlas hacía que KOS perdiera tiempo (`basic/watchdog` medía 8 s donde pedía
10) y mataba de hambre al planificador de hilos lo bastante como para colgar
`basic/threading/atomics`. Ambas pasan ahora.

### Pero la entrega no puede esperar al bloque periódico, y eso es lo que rompió DCDoom

`RELOJ_GRANO` subió ese bloque de 50 a 400 ciclos por un 19% medido, y con él se fue la cadencia a
la que `intc_revisar_sh4()` llega a intentar. DOOM entonces nunca salía de su pantalla de título:
977 escenas en 35 segundos emulados se volvieron **170**, y la cuenta dejaba de crecer — una
imagen fija.

Nada más se movía; todo el log de arranque del guest, su perfil de syscalls de GD-ROM (692 contra
655 SEND, 64 contra 60 del comando `0x24`) y sus lecturas de sector son iguales, y las dos
corridas son idénticas segundo a segundo hasta el segundo emulado 12. No es latencia — 400 ciclos
son 2 µs — sino **cuántas veces se intenta la entrega**: lo que una petición necesita es una
fuente asertando *y* `SR` permitiendo, y un guest que vive con `BL` puesto (Windows CE, miles de
excepciones por segundo, su ISR volviendo por `RTE` para tomar la siguiente) solo lo permite en
ventanas cortas.

`intc_sh4_reintentar` lo restaura: los momentos que pueden abrir una ventana lo arman — `tmu.c` en
`TCR.UNF`, `wdt.c` en `WTCSR.IOVF`, `dma_canal()` en `CHCR.TE`, y ambas entradas de `UpdateSR()` —
y la condición del propio bloque periódico lo prueba, así que el bloque corre temprano cada vez
que una petición se vuelve entregable.

DCDoom vuelve a 977 con una captura byte a byte idéntica; Crazy Taxi, Virtua Tennis, Capcom vs.
SNK y Virtua Tenis 2 conservan sus cuentas de escenas, sus cuentas de instrucciones y **capturas
byte a byte idénticas**; las 21 suites pasan; Crazy Taxi paga **5,3%** (59 493 ms contra 56 493 en
una máquina ociosa), así que la mayor parte del 19% sobrevive. `tests/dobles.c` tiene que definir
el flag también — reemplaza a `intc.c` en el arnés.

**Dos cosas de esto se midieron mal primero, y ambas vale la pena conservarlas.** El flag se prueba
**dentro de la condición del bloque periódico** (`>= RELOJ_GRANO || intc_sh4_reintentar`), no en
un `if` propio: con su propia rama Crazy Taxi pagaba **8,5%** (62 007 ms contra 57 152 sin el flag
en absoluto), plegado en la rama que ya se estaba evaluando no paga nada medible (56 790). Traer
todo el bloque hacia adelante ante una petición es inocuo — ocurre unas mil veces por segundo
emulado y sus consumidores llevan sus propios restos.

Y **el flag, no la cadencia, es lo que carga la corrección**: DCDoom da las mismas 977 escenas y la
misma captura con `RELOJ_GRANO` en 400, 800 y 1600. Eso es lo que lo hace un arreglo y no una
constante afortunada — imita lo que hace el chip, entregando en el primer límite de instrucción
donde las condiciones se cumplen, en vez de acertarle al grano correcto. Subir el bloque más allá
de 400 compra 0,6% y no vale la agitación.

Ambos hechos solo salieron después de que una remedición limpia matara dos lecturas falsas, y las
dos trampas costaron tres números de esta sección: un `dcemu.exe` **huérfano de una corrida
detenida** se estaba comiendo un núcleo durante el primer lote de perf, y `git checkout -- <file>`
restaura desde el **índice**, así que una construcción que se suponía que llevaba el flag no lo
llevaba. **Hay que matar procesos sueltos y hacer `git reset --hard` antes de cualquier A/B
aquí.**

---

## El DMAC

`dma_check()` en `main.c` corre el DMAC del SH-4 de verdad (tamaño de unidad, modos de dirección
de origen/destino), pero solo en canales en modo de autopetición: las transferencias pedidas por
periférico en la Dreamcast las hace el ASIC, no el DMAC.

**El fin de transferencia del DMAC sigue el mismo patrón** que los periféricos: `dma_canal()` deja
`CHCR.TE` puesto (y no toca `DE`, según el manual), e `intc_revisar_sh4()` deriva DMTE0-3 (INTEVT
`0x640`/`0x660`/`0x680`/`0x6A0`, prioridad en los bits 11-8 de IPRC) de `TE && IE && DMAOR.DME`;
el guest reconoce limpiando el CHCR, que es lo que hace el driver de KOS.

Dos cosas aquí costaron un cuelgue cada una: la interrupción solía ser una línea de log que decía
"no implementado", y **`DMAOR` arranca en `0x8201`** (DDT, prioridad CH2>CH0>CH1>CH3, DME) porque
es lo que deja el boot ROM en una consola de tienda — el `dma_init()` de KOS solo escribe DMAOR en
NAOMI, *"these are set by the bios on Dreamcast"*, así que con los hooks de syscall nadie más
ponía DME y `basic-dma-speedtest` armaba un canal 1 perfectamente válido que nunca corría.
