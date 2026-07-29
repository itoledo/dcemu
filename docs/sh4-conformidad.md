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
| `mov.c` `movb10`/`movw11` | `MOV.B/W Rm,@-Rn` copiaban el registro antes de decrementar. Solo se nota con `n == m`, el "push del propio stack pointer". `movl12` estaba bien |
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

## Lo que sigue sin cumplir el manual, a propósito

Lo que se corrigió es el **resultado** de cada instrucción: registros, memoria, T y los
registros de sistema. Tres cosas quedan afuera, y no por descuido:

- **La maquinaria de excepciones de la FPU.** Los campos Cause, Flag y Enable de FPSCR no
  se actualizan nunca, y los bits DN (desnormalizados a cero) y RM (modo de redondeo) se
  ignoran: la aritmética usa la del host, que redondea al más cercano. Un programa que lea
  FPSCR después de una operación inválida no verá lo que vería en hardware. Emular eso es
  un proyecto aparte y no lo pide ningún homebrew conocido.
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

387 casos, todos en verde, sobre 239 filas implementadas, todas ejercitadas. Once pruebas
de CTest —una por suite más la corrida completa—, medio segundo en total.

```sh
cmake --build build --config Debug --target dcemu_tests
ctest --test-dir build -C Debug --output-on-failure
```

Además de las unitarias, `demos/roto` —el rotozoomer de 256 bytes, que usa `FSCA`, `FDIV`,
`FTRC`, `FLOAT` y `MUL.L`— arranca y renderiza igual que antes con la BIOS real. Es la
comprobación de que nada de esto rompió el camino que sí funcionaba.

## Pendiente

- Confirmar contra el manual si `LDC Rm,SGR` y `LDC.L @Rm+,SGR` existen. Si no, sacar las
  dos filas de la tabla.
- Pasar más homebrew. `DIV1` afecta a cualquier binario que divida, así que el
  comportamiento de demos que "funcionaban" puede cambiar —para mejor, pero cambia—. La
  colección de KallistiOS sería el próximo banco de pruebas.
- Los ciclos por instrucción (`core.context.cycles`) son valores aproximados que no
  corresponden a la temporización real del SH-4. Las pruebas no los verifican. Si alguna
  vez importa la sincronía fina con el PVR o el TMU, hay que revisarlos con la tabla del
  manual.
