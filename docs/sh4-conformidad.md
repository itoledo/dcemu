# Conformidad del núcleo SH-4 con el manual

Estado: **ejecutado**. Julio de 2026, sobre `master` (`a04be29`). Se escribió una batería de
pruebas unitarias para los opcodes (`tests/`), se corrigieron las 16 desviaciones que
encontró y se implementaron las 29 instrucciones que faltaban. La tabla de `opcodes.c` ya
no tiene ninguna fila en `NOIMP` salvo el comodín.

## Contexto

El núcleo SH-4 es el único subsistema del emulador que se puede probar en aislamiento. Todo
lo demás —PVR, TA, Maple, interrupciones— depende de SDL, de OpenGL o de una imagen de
disco, y se valida corriendo un demo y mirando la pantalla. Los opcodes, en cambio, son
funciones puras sobre un estado de CPU: entra un patrón de bits, sale un registro, un bit T
y un PC. Eso se verifica contra el manual sin abrir una ventana.

Hasta ahora no había nada de eso. El código es de 2004-2007, se escribió transcribiendo el
pseudocódigo del manual de Hitachi a mano, y ese proceso deja el tipo de error que no se
nota corriendo un demo: una fórmula transcrita al revés, un desplazamiento de más, un
`memcpy` de 4 bytes donde iban 8. Errores que no cuelgan nada, solo dan el número
equivocado en un caso de borde que quizá el demo nunca ejercita.

La apuesta era que escribir las pruebas iba a encontrar varios. Encontró 16.

## Cómo se probaron

`mem.c` no se puede enlazar en un binario de pruebas: `pvr_write()` llama a los callbacks
de `graficos.c`, que arrastra SDL y OpenGL entero. La salida fue reemplazar solo eso:

- **`tests/memoria_prueba.c`** reproduce la estructura de `mem.c` —las dos tablas de 256
  entradas indexadas por el byte alto de la dirección— con RAM del sistema, RAM de video,
  FIFO del TA y store queues. Las zonas sin mapear cuentan el acceso en vez de reventar.
- **`tests/dobles.c`** cubre los símbolos de `graficos.c`, `iso.c` e `intc.c` que los
  handlers referencian. Los del tile accelerator cuentan llamadas, así que `PREF` se puede
  verificar hasta el despacho por tipo de parámetro.

Todo lo demás es el emulador tal cual: los handlers reales y la tabla real de `opcodes.c`.
Las cabeceras de SDL siguen haciendo falta para *compilar* —`opcodes.h` incluye `main.h`—
pero no se enlaza ninguna función de SDL.

Dos mecanismos del arnés que resultaron más útiles de lo esperado:

- **`CASO_XFAIL`**: un caso que describe lo que dice el manual y se espera que falle. Si
  empieza a pasar, el runner lo reporta como `XPASS` y termina con error. Sirvió para
  dejar cada desviación escrita y verificada antes de arreglarla, y después para confirmar
  de golpe que los 16 arreglos funcionaban: las 16 marcas pasaron a `XPASS` en la misma
  corrida.
- **Suite `cobertura`**: recorre `opcodes[]` y falla si alguna fila implementada nunca se
  ejecutó. Es lo que obliga a que una instrucción nueva venga con prueba, y lo que avisó
  de las 28 filas sin probar apenas se conectaron los handlers nuevos.

## Las 16 desviaciones

Ordenadas por impacto.

### DIV1: toda división sin signo daba cero

`arith.c:475`. El paso de división sin restauración terminaba con `T = Q && M` en vez de
`T = (Q == M)`. La línea correcta estaba ahí mismo, comentada, dos renglones abajo.

Las dos fórmulas coinciden mientras `M = 1` y difieren en todo el tramo `M = 0`, que es
justo el que usa la división sin signo: `DIV0U` deja `M` en cero y nunca se vuelve a tocar,
así que T quedaba siempre apagado y el cociente salía 0. La división con divisor negativo
—donde `DIV0S` prende `M`— sí funcionaba.

Cómo se aisló: dos casos de un paso, uno con divisor positivo y otro negativo, más la
secuencia completa del manual (`DIV0U` + 32 × `ROTCL`/`DIV1`) corrida dos veces, una tal
cual y otra corrigiendo T desde C después de cada paso. La versión corregida daba el
cociente correcto, lo que probaba que el resto del paso —la suma/resta y el cálculo de Q—
estaba bien y el único defecto era la fórmula de T.

### MAC.L con S=1

`arith.c:727`. La rama de saturación tenía tres transcripciones erradas: `MACH = Res2`
donde va `MACH = (Res2 & 0xFFFF) | (MACH & 0xFFFF0000)`, un `MACL & 0x7FFF` donde va
`MACH & 0xFFFF` —con un comentario `// MACH o MACL?` al lado— y una constante de saturación
cambiada. El acumulador con `S = 1` es de 48 bits: `MACH[15:0]:MACL`, y `MACH[31:16]` no
participa ni se modifica.

Se reescribió con aritmética de 64 bits, que es corta y obviamente correcta, en vez de
seguir la descomposición en palabras de 32 del pseudocódigo.

Aparte, `MAC.L` con `n == m` leía dos veces la misma dirección. El manual lee `@Rn+`,
incrementa, y recién después `@Rm+`: son dos posiciones consecutivas.

### MOV.B R0,@(disp,GBR) no escribía

`mov.c:672`. Estaba implementada como una **carga**: leía de `(GBR + disp)` a R0. La fila
0xC000 de la tabla es la variante de escritura; la de lectura es 0xC400, que sí estaba
bien. Un `memcpy` a través de GBR simplemente no copiaba nada.

Su vecina `MOV.W @(disp,GBR),R0` (`mov.c:684`) leía bien pero no extendía el signo.

### El resto

| dónde | qué pasaba |
| --- | --- |
| `mov.c` `movb10`/`movw11` | `MOV.B/W Rm,@-Rn` copiaban el registro antes de decrementar. Solo se nota con `n == m`, el "push del propio stack pointer". **Este arreglo estaba al revés** y lo corrigió la pasada de SingleStepTests: el manual escribe `R[m]` y decrementa después, así que el valor bueno era el original. Ver más abajo |
| `syscontrol.c:897` | `LDC Rm,DBR` leía de memoria, como si fuera `LDC.L @Rm+,DBR` |
| `syscontrol.c` `trapa169` | no guardaba R15 en SGR, que es parte de la secuencia de excepción |
| `floatsimple.c:492` y `:799` | `FTRC` usaba `floor()` en doble precisión —redondea hacia abajo, no trunca hacia cero: -2.5 daba -3— y un cast crudo en simple, sin saturar fuera de rango |
| `floatsimple.c` `fdiv192` | dividir por cero dejaba el registro sin tocar y logueaba, en vez de entregar infinito |
| `floatsimple.c:207` | `FMOV DRm,@-Rn` escribía `sizeof(DWORD)` bytes: 4 de los 8 del par |
| `floatgraph.c:162` | `FIPR` escribía el producto punto en `FR[n+3]` en vez de `FR[4n+3]`. Solo acertaba con `n = 0`. El producto en sí estaba bien |
| `floatgraph.c:85` | `FMOV @(R0,Rm),XDn` sacaba `n` de los bits 8-11 y `m` de los 5-7; van en 9-11 y 4-7 |
| `floatsimple.c` `fneg196` | `FNEG` multiplicaba por -1 en vez de invertir el bit de signo |

Fuera de los opcodes, `sh4emu.c:87`: `reset()` dejaba `FPSCR = 0x0004001`. Son 0x4001 —le
falta un cero—, así que en vez de DN prendía un bit de Cause. Es inerte, porque dcemu no
mira ninguno de los dos, pero estaba mal.

## Las 29 instrucciones que faltaban

Todas las filas que apuntaban a `NOIMP`. Tres ya tenían handler escrito y solo faltaba
conectarlo a la tabla: `addv()` en `arith.c`, `stc153()` y `stcn154()` en `syscontrol.c`.

- **Enteros**: `ADDV`, `SUBV`, `MAC.W`, `SHAL`.
- **Lógicas sobre memoria**: `AND.B`, `OR.B`, `TST.B` y `XOR.B` con `#imm,@(R0,GBR)`.
- **Control**: `CLRMAC`, `CLRS`, `SETS`, `LDC` a GBR/SPC/SGR, `STC` de SPC/SGR, `LDTLB`,
  `OCBP`, `MOVCA.L`.
- **FPU**: `FABS` simple y doble, `FCMP/EQ DRm,DRn`, `FMUL DRm,DRn`, `FNEG DRn`,
  `FSUB DRm,DRn`, `FCNVDS`, `FCNVSD`, `FMOV @(R0,Rm),DRn` y `FMOV DRm,@Rn`.

`SHAL` es idéntica a `SHLL`: el desplazamiento aritmético a la izquierda y el lógico son la
misma operación con dos encodings distintos.

## Cause y Flag: los campos de excepción de FPSCR

El SH-4 rearma el campo **Cause** (bits 17-12) en cada operación de la FPU y acumula las
mismas causas en **Flag** (bits 6-2), que solo se limpia escribiendo FPSCR. Los dos campos
llevan las causas en el mismo orden, diez bits de distancia: I (inexacto), U
(subdesbordamiento), O (desbordamiento), Z (división por cero) y V (inválida).

Nada de eso se escribía. Lo destapó `basic/fpu/exc` de KallistiOS, que hace subdesbordar,
desbordar, dividir por cero y producir un NaN, y después revisa un bit de Flag por cada
caso: los cuatro daban cero y la demo terminaba en `TEST FAILED!`.

**Las dos macros que existían para esto no compilaban.** `FPSCR_CAUSE` buscaba un miembro
`FPSCR_BITS` que no existe —la unión lo llama `FPSCR_REG_BITS`— y `FPSCR_FLAG` arrancaba en
`core.context.FPSCR`, que tampoco existe y además choca con la macro `FPSCR`, definida ocho
líneas más abajo como `core.context.FPSCR_REG.FPSCR_ALL`. Ningún archivo las referenciaba,
así que el compilador nunca las vio.

`floatsimple.c` clasifica ahora las causas a partir de los operandos y del resultado, sin
tocar el entorno de punto flotante del anfitrión. Las reglas:

| causa | cuándo |
| --- | --- |
| V | el resultado sale NaN sin que ninguna entrada lo fuera (`inf-inf`, `0*inf`, `0/0`, `inf/inf`, raíz de un negativo), o cualquier NaN en `FCMP`, o `FTRC` de NaN o fuera de rango |
| Z | divisor cero con un dividendo finito distinto de cero |
| O | el resultado es infinito y ninguna entrada lo era |
| U | el resultado queda bajo el menor normal del formato con las dos entradas no nulas |
| I | solo acompañando a O y a U |

Tres distinciones que el camino obvio se lleva por delante:

- **`inf/0` no es división por cero.** El resultado es infinito y está definido.
- **Un NaN que entra y sale se propaga sin levantar nada**, que es lo que IEEE 754 pide
  para un NaN silencioso. Por eso la regla de V mira el resultado y no las entradas.
- **Las sumas y las restas nunca subdesbordan.** Si `a + b` cae por debajo del mínimo
  normal, el resultado es exacto, y sin inexactitud no hay subdesbordamiento. Solo la
  multiplicación, la división, `FMAC` y `FCNVDS` pueden levantar U.

Se instrumentaron las 16 operaciones aritméticas y de conversión de `floatsimple.c` —
`FADD`, `FSUB`, `FMUL`, `FDIV`, `FSQRT`, `FMAC`, `FLOAT`, `FTRC`, `FCMP/EQ` y `FCMP/GT` en
simple y doble, más `FCNVDS` y `FCNVSD`—. `FABS`, `FNEG`, `FLDI`, `FMOV`, `FLDS` y `FSTS`
no son operaciones aritméticas y no rearman Cause. `FIPR`, `FTRV` y `FSRRA` (`floatgraph.c`)
y `FSCA` (`dcopcodes.c`) quedaron afuera a propósito: el manual mismo las describe como
aproximaciones que no siguen el redondeo de IEEE.

La suite `fpu-excepciones` cubre esto y `basic/fpu/exc` reporta `TEST SUCCEEDED!`. El
barrido de regresión sobre catorce demos —consola, 3D y `pvrmark`— dio salida idéntica, y
el rendimiento no se movió: la clasificación son tres o cuatro comparaciones por operación,
sin recargas de MXCSR.

## La trampa: Enable y las tres excepciones de la FPU

Con Cause y Flag escritos falta el otro lado del registro. Cuando una causa coincide con su
bit de **Enable** (bits 11-7), el SH-4 no completa la instrucción: entra en la excepción de
FPU y deja el registro destino y el campo Flag **sin tocar**. Son tres códigos, y KOS marca
los tres `[REEXEC]`:

| código | cuándo | vector |
| --- | --- | --- |
| 0x120 | una causa coincidió con su Enable | VBR + 0x100 |
| 0x800 | instrucción de FPU con `SR.FD` puesto | VBR + 0x100 |
| 0x820 | igual, pero en una ranura de retardo | VBR + 0x100 |

### La plomería ya existía, en `mmu.c`

Reejecutar una instrucción es exactamente lo que hace la MMU desde la fase 5 de
`docs/mmu-plan.md`: `main_loop()` saca una instantánea del contexto, arma un `setjmp`, y
quien detecta la falta sale por un `longjmp` que desenrolla hasta ahí. Lo único que hacía
falta era que dejara de llamarse `mmu_*`.

`excepciones.c/h` es ese mecanismo, ahora compartido: `excepcion_entrar()` (que se mudó
desde `intc.c` — es la secuencia del procesador, no del controlador de interrupciones),
`excepcion_abortar()`, la instantánea, y `excepcion_vigilar`, que generaliza la vieja prueba
contra `mmu_activa`. Vale 1 si la MMU traduce, si `SR.FD` está puesto o si hay algún bit de
Enable; en todo lo que corre hoy vale cero y el camino rápido cuesta lo mismo que antes.

**Y ahí apareció un bug que llevaba desde la fase 5.** La instantánea era un `memcpy` de
`core.context`, y los dos bancos de registros de punto flotante **no están dentro**: el
contexto solo guarda punteros a ellos. Es decir que la reejecución nunca restauró los
registros de FP, y un `FMOV.S @Rm+,FRn` que fallara por MMU dejaba `FRn` escrito. No se
notaba porque las dos demos de MMU fallan por otra cosa. La trampa de FPU lo destapó de
inmediato, porque su requisito explícito es que el destino no se actualice:
`excepcion_instantanea_tomar()` y `..._restaurar()` copian los dos bancos además del
contexto.

El segundo hallazgo fue más chico: `fpu_deshabilitada` —la copia de `SR.FD` que mira el
despacho— se escribía en `UpdateSR()`, pero `arnes_reset()` de las pruebas asigna `SR = 0`
directamente. Ahora la deriva `excepcion_actualizar_vigilancia()` y no hay dos fuentes.

### Dónde se dispara cada una

- **0x120** en `fpu_causa()` (`floatsimple.c`): si `causas & FPU_ENABLE_A_CAUSA(FPSCR)`,
  aborta en vez de escribir. `excepcion_reponer()` escribe Cause *después* de restaurar,
  porque la instantánea también se lo lleva; Flag no se toca, que es lo que pide el manual.
- **0x800 y 0x820** en `run()` (`sh4emu.c`), antes del despacho. Va ahí y no en
  `main_loop()` porque así cubre también las ranuras de retardo, que `branch.c` y `RTE`
  ejecutan con un `core.execute()` anidado — y esa es justamente la diferencia entre los dos
  códigos. `en_ranura_retardo` la suben y bajan `EJECUTAR_RANURA()` y `rte143()`.
  `es_instruccion_fpu()` reconoce todo `0xFxxx` más las ocho transferencias de FPUL y FPSCR,
  que no empiezan con 1111 pero el manual lista con ellas.

Que SPC apunte al salto y no a la ranura sale gratis: el `longjmp` desenrolla los dos
niveles y la instantánea deja PC donde empezó.

### Cómo se prueba

Tres niveles, porque ninguno solo alcanza:

1. **Unitario**, suite `fpu-excepciones`. `ejecutar_vigilado()` en el arnés reproduce el
   ciclo entero de `main_loop()` —instantánea, `setjmp`, restaurar, `excepcion_reponer()`,
   `excepcion_entrar()`—, que es lo que hace falta para verificar que el registro destino
   quedó sin tocar, y no solo que EXPEVT y PC son los correctos.
2. **De punta a punta con KallistiOS**, `demos/fpu-trampa`. Instala un manejador con
   `irq_set_handler(EXC_FPU, ...)`, habilita una causa por vez, y el manejador apaga Enable
   y vuelve **sin saltar la instrucción**: la reejecución tiene que completarla. Reporta
   `TEST SUCCEEDED!`.
3. **`basic/fpu/exc` de KOS**, que no habilita nada. Es la prueba de que la trampa no se
   dispara sola.

**La excepción de FD no se puede probar desde KOS, y no por falta de ganas.** Lo primero que
hace su manejador es `sts.l fpscr,@-r0`
(`kernel/arch/dreamcast/kernel/entry.s`), que es una instrucción de FPU: con FD puesto se
dispararía a sí misma para siempre. Eso vale igual en hardware real —KOS simplemente no usa
FD—, así que 0x800 y 0x820 quedan cubiertos solo por las unitarias.

## Lo que sigue sin cumplir el manual, a propósito

Lo que se corrigió es el **resultado** de cada instrucción: registros, memoria, T y los
registros de sistema. Tres cosas quedan afuera, y no por descuido:

- **Lo que queda de las excepciones de la FPU.** Cause, Flag, Enable y las tres excepciones
  están —ver "Cause y Flag" y "La trampa" más abajo—. Faltan dos cosas:

  - **La causa I (inexacto) por sí sola.** Solo se levanta acompañando a O y a U, que son
    los dos casos en que un resultado fuera de rango es inexacto por definición.
    Detectarla en general obligaría a usar `<fenv.h>` y a recargar MXCSR antes de cada
    instrucción emulada, que es lo más caro que se puede hacer en un intérprete. En
    hardware real está encendida casi siempre, así que ningún programa saca información
    de ella.
  **DN y RM ya no están en esta lista**: los implementó la validación contra
  SingleStepTests (ver más abajo). RM cambia el modo de redondeo del anfitrión desde
  `UpdateFPSCR()` y DN aplasta desnormalizados en `fpu_dn_s()`/`fpu_dn_d()`.

  Un NaN de señal que entra tampoco levanta V: la causa inválida se reconoce porque el
  resultado sale NaN sin que ninguna entrada lo fuera, y esa regla no distingue un NaN
  silencioso de uno de señal.

  Y queda un falso positivo nuevo, consecuencia de emular RM: el desbordamiento se
  reconoce por `|resultado| >= FLT_MAX` (o `DBL_MAX`), porque truncando hacia cero un
  desbordamiento produce el mayor normalizado y no infinito. Una cuenta cuyo resultado
  exacto sea justo ese valor se reporta como desbordamiento. Distinguirlo pediría el
  resultado sin redondear, que en doble precisión no hay con qué calcular.

- **El valor del qNaN.** El manual fija los patrones que el chip genera —`H'7FBFFFFF` en
  simple y `H'7FF7FFFF FFFFFFFF` en doble— para cualquier resultado NaN, venga de una
  operación inválida o de propagar un qNaN de entrada. dcemu deja el que produce el
  anfitrión (`H'FFC00000`). No lo miden las pruebas de SingleStepTests, cuyo
  `compare_floats()` da por iguales dos NaN cualesquiera, ni ningún programa que se haya
  corrido: un NaN se propaga igual valga lo que valga su carga útil.
- **`LDTLB`, `OCBP`, `OCBI` y `OCBWB`** avanzan PC y nada más. `LDTLB` carga la TLB y dcemu
  no emula la MMU; las tres de caché no tienen efecto observable sin caché emulada.
  `MOVCA.L` sí escribe, que es su único efecto visible. Se implementaron como
  instrucciones válidas que no hacen nada, en vez de dejarlas como opcode ilegal.
- **`LDC Rm,SGR` (0x403A) y `LDC.L @Rm+,SGR` (0x4036)**: las dos filas venían marcadas
  `// INSERTADA` por los autores originales. El resumen de instrucciones del manual del
  SH-4 lista SGR solo para `STC` y `STC.L`, así que puede que no sean instrucciones
  reales. Quedaron implementadas de la forma obvia, pero conviene confirmarlo contra el
  manual antes de darlas por buenas.

## Verificación

615 casos unitarios, todos en verde, sobre 239 filas implementadas, todas ejercitadas.
Veintiún pruebas de CTest —una por suite, la corrida completa y la de SingleStepTests—,
menos de tres segundos en total.

```sh
cmake -S . -B build -DDCEMU_SH4_JSON=/ruta/al/clon/de/SingleStepTests-sh4
cmake --build build --config Debug --target dcemu_tests dcemu_sh4json
ctest --test-dir build -C Debug --output-on-failure
```

Sin `-DDCEMU_SH4_JSON` la prueba de SingleStepTests sale con 77 y CTest la marca omitida:
son 92 MB de datos que no van en el repositorio.

Además de las unitarias, `demos/roto` —el rotozoomer de 256 bytes, que usa `FSCA`, `FDIV`,
`FTRC`, `FLOAT` y `MUL.L`— arranca y renderiza igual que antes con la BIOS real. Es la
comprobación de que nada de esto rompió el camino que sí funcionaba.

## El banco de registros y una excepción dentro de otra

Encontrado el 31 de julio de 2026, persiguiendo por qué Virtua Tennis no dibuja. Es el
error más gordo que quedaba en el núcleo y ninguna prueba ni ninguna demo lo veía.

`SR.RB` dice qué banco de `R0-R7` **debería** estar puesto. Lo que estaba puesto de verdad no
se registraba en ningún lado: se daba por hecho que coincidía. `UpdateSR()` tiene dos
entradas, y la que usan la entrada a excepción, la entrada a interrupción y `TRAPA` —las tres
escriben `SR` a mano y avisan con el centinela `SH4_SYSTEM_REGISTER_INTC_REWRITTEN`—
**intercambiaba siempre**.

Con `RB` ya en 1 eso está mal: poner un bit que ya estaba no mueve nada. Y `RB` ya vale 1
cuando se toma una excepción **desde dentro de otra**, que es lo que hace cualquier manejador
que baje `BL` para permitir anidamiento. Entonces:

- el manejador anidado veía el banco del código normal como si fuera el suyo;
- guardaba y restauraba `R0-R7` — pero los del banco equivocado;
- el `RTE` no lo deshacía, porque ese camino **sí** compara y `SSR.RB == SR.RB`;
- el código interrumpido seguía con `R0-R7` del otro banco.

Ahora `core.context.banco_activo` lleva la cuenta de lo que está puesto, `swap_registers()` es
lo único que lo mueve y los dos caminos de `UpdateSR()` comparan contra él. Va **dentro** del
contexto a propósito: la instantánea de la MMU restaura el arreglo de registros, así que el
banco tiene que viajar con ella. `reset()` y `arnes_reset()` lo fijan, porque los dos escriben
`SR` sin pasar por `UpdateSR()`.

**Por qué no lo vio nada de lo que ya estaba.** KOS toma sus interrupciones desde `RB=0`, así
que el intercambio siempre es un cambio de verdad y el error no aparece; las 100 demos que
pasan siguen pasando igual. Y la suite tenía el caso de ida (`RB` 0→1 intercambia) pero no el
de vuelta. Está agregado: `trapa_desde_el_banco_1_no_vuelve_a_cambiar`, en `syscontrol`, que
falla con el código viejo y pasa con el nuevo — comprobado en los dos sentidos.

Lo que se ve en el juego: pierde el índice de una tabla de callbacks a través de una
interrupción anidada, llama por un puntero nulo, cae en la dirección 0 —que es el boot ROM—,
el ROM salta a `0x8C000018` y de ahí corre por el bloque bajo del sistema hasta toparse con
los bytes que decodifican como `TRAPA #23`. Ver `docs/pendientes-plan.md`, vía A.

## La segunda pasada: 116.500 casos de SingleStepTests

Agosto de 2026. Las unitarias de `tests/` se escribieron **leyendo el manual**, así que
cubren lo que uno se acuerda de mirar. [SingleStepTests/sh4](https://github.com/SingleStepTests/sh4)
es lo contrario: 233 codificaciones × 500 casos con estado inicial y final completos y
valores al azar en todos los registros. Encontró once cosas más.

Ojo con qué son: las generó el intérprete de **Reicast**, no el manual. Donde discrepan
gana el manual, y las discrepancias están contadas y clasificadas —ver la tabla del final—.
Cómo se corre está en `tests/README.md`.

### Lo que encontró

Ordenado por lo que costaría en un juego.

| dónde | qué pasaba |
| --- | --- |
| `sh4emu.c` `UpdateSR()` | **el centinela era 0xFFFFFFFF**, o sea exactamente lo que deja un `LDC Rm,SR` con Rm en todos unos: esa escritura se tomaba por "el llamador ya escribió SR" y **SR quedaba sin tocar**. Ahora la entrada del emulador es `UpdateSR_ya_escrito()` |
| `sh4emu.c` `UpdateSR()` | **con `MD` en cero, `RB` tiene que quedar en cero**: en modo usuario R0-R7 son siempre los del banco 0. No se miraba, y el propio comentario decía que quedaba "igual que antes" por no poder verificarlo. Las 1500 pruebas de `LDC Rm,SR`, `LDC.L @Rm+,SR` y `RTE` lo fijan |
| `syscontrol.c` `rte143`, `ldcl122` | **la máscara 0x700083F3 estaba solo en `LDC Rm,SR`**. `RTE` copiaba SSR entero —y SSR se escribe con `LDC Rm,SSR`, que no filtra nada— y `LDC.L @Rm+,SR` tampoco filtraba |
| `syscontrol.c` `ldcl122` | el `+4` de `@Rm+` iba **después** de escribir SR, así que con cambio de banco caía en el registro del banco nuevo en vez de en el que se leyó |
| `mov.c` `movb10`/`movw11`/`movl12` | `MOV.B/W/L Rm,@-Rn` con `n == m` escribía el valor **decrementado**. El manual escribe `Rm` y decrementa después: `Write_Byte(R[n]-1, R[m]); R[n]-=1`. Las unitarias tenían el error consagrado en tres casos |
| `syscontrol.c` `sleep116` | **`SLEEP` avanzaba PC**, o sea que era un `NOP`: un guest que espere una interrupción ahí seguía de largo. El manual no le pone `PC += 2` |
| `sh4emu.c` `UpdateFPSCR()` | **`FPSCR.RM` se ignoraba.** RM = 01 —truncar hacia cero— es el valor de reset y el que deja KOS, o sea el modo en que corre *todo*; dcemu redondeaba al más cercano en cada operación de la FPU |
| `floatsimple.c` | **`FPSCR.DN` se ignoraba**: los desnormalizados no se aplastaban a cero ni a la entrada ni a la salida |
| `floatsimple.c` `fmac194` | **`FMAC` redondeaba dos veces.** El manual, 6.4: "Rounding is performed once in FMAC, but twice in FADD, FSUB, and FMUL" |
| `floatgraph.c` `fipr`/`ftrv` | acumulaban en simple precisión: dos productos parciales que desbordan con signos opuestos daban NaN donde el chip entrega un infinito |
| `dcopcodes.c` `fsca` | **tomaba FPUL entero como ángulo**, y además sin signo (es `DWORD`). Son los **16 bits bajos**: el ángulo es una fracción de vuelta en punto fijo. Las 500 pruebas fallaban todas |
| `dcopcodes.c` `NOIMP` | **leía la instrucción de memoria otra vez** para nombrarla en un `logmsg()` —que es una función, no una macro, así que el argumento se evaluaba siempre—. Una lectura que la instrucción no hace la ve un watchpoint, y con la MMU encendida puede fallar |

Las tres de la FPU que más se van a notar son RM, DN y FMAC, porque afectan a **toda**
operación de punto flotante que no dé un resultado exacto.

### Las divergencias, y por qué se dejan

3221 casos de 116.500 no coinciden y no se van a arreglar: son sitios donde Reicast
contradice al manual. El corredor los clasifica uno por uno y los cuenta aparte, así que
no se confunden con un fallo.

| casos | qué | quién tiene razón |
| --- | --- | --- |
| 2110 | **Cause y Flag de FPSCR no se escriben.** Reicast no los emula | dcemu: lo pide el manual y lo verifica `basic/fpu/exc` de KOS |
| 1000 | **FPSCR guarda los bits reservados.** `LDS Rm,FPSCR` sin la máscara `FPSCR_MASK` | el manual define `#define FPSCR_MASK 0x003FFFFF` en la propia operación |
| 500 | **`TRAPA` no entra en la excepción**: el estado final es el de cuatro instrucciones seguidas | el propio repositorio avisa que no modela excepciones |
| 200 | **`RTE`: la ranura de retardo ve el SR viejo.** Reicast escribe SR después de la ranura | el manual: "The SR value accessed by the instruction in the RTE delay slot is the value restored from SSR by the RTE instruction" |
| 184 | **`FTRC` fuera de rango no satura**: Reicast recorta a 0x7FFFFF80, el mayor entero representable en float | el manual: `ftrc_invalid()` entrega 0x7FFFFFFF o 0x80000000 y levanta V |
| 43 | **`DIV1` con `n == m`.** Reicast lee Rm después de desplazar Rn y le da cero | el manual guarda `tmp2 = R[m]` **antes** del desplazamiento; QEMU también |
| 11 | **`FIPR`/`FTRV`: tres unidades del último bit.** Reicast acumula en simple | son instrucciones aproximadas y el manual dice que "the same result as SH-4 is not guaranteed" |

Y tres casos se descartan: la cuarta instrucción resulta ser un salto —el destino de la
probada volvió a la ventana—, el generador corta a los cuatro accesos y el estado final
queda a mitad de una instrucción, que no hay cómo reproducir.

### Cómo se comparan los flotantes

Del bit, salvo dos excepciones, y las dos con razón escrita:

- **Un NaN vale por cualquier otro.** Es la regla que publica el propio repositorio en su
  `compare_floats()`, y hace falta porque el SH-4, el anfitrión y Reicast escriben tres
  patrones distintos.
- **`FIPR`, `FTRV`, `FSRRA` y `FSCA` se comparan contra una cota de error**, la del manual
  en 6.6.1 para las dos primeras y 2^-21 para las otras dos. Son aproximaciones por
  diseño.

En todo lo demás la comparación es exacta **a propósito**: es lo que dejó ver que se
ignoraba RM. Aquellas diferencias eran de un ulp, y cualquier tolerancia las habría tapado.

## Pendiente

- Confirmar contra el manual si `LDC Rm,SGR` y `LDC.L @Rm+,SGR` existen. Si no, sacar las
  dos filas de la tabla.
- El qNaN que genera la FPU debería ser `H'7FBFFFFF` (simple) y `H'7FF7FFFF FFFFFFFF`
  (doble), no el del anfitrión. Ver "Lo que sigue sin cumplir el manual".
- Pasar más homebrew. `DIV1` afecta a cualquier binario que divida, así que el
  comportamiento de demos que "funcionaban" puede cambiar —para mejor, pero cambia—. La
  colección de KallistiOS sería el próximo banco de pruebas.
- Los ciclos por instrucción (`core.context.cycles`) son valores aproximados que no
  corresponden a la temporización real del SH-4. Las pruebas no los verifican. Si alguna
  vez importa la sincronía fina con el PVR o el TMU, hay que revisarlos con la tabla del
  manual.
- Revisar el camino de reejecución de la MMU ahora que la instantánea sí restaura los
  bancos de punto flotante. `basic-mmu-nullptr` y `basic-mmu-pvrmap` siguen cayendo por
  otra cosa (ver `docs/demos-kos.md`), pero el bug que se encontró aquí estaba en ese
  camino y conviene volver a mirarlas con esto arreglado.
