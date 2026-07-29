# Pruebas unitarias de los opcodes del SH-4

Ejercitan los handlers reales del emulador -- `arith.c`, `logic.c`, `shift.c`,
`mov.c`, `branch.c`, `syscontrol.c`, `floatsimple.c`, `floatcontrol.c`,
`floatgraph.c`, `dcopcodes.c` -- con la tabla real de `opcodes.c`, sin SDL, sin
OpenGL y sin imagen de disco. Cada caso arma una instruccion, la ejecuta como
lo hace `main_loop()` y compara el estado resultante contra lo que dice el
manual del SH-4.

Estado actual: **387 casos, todos en verde**, sobre **239 filas implementadas
de `opcodes[]`, todas ejercitadas y sin desviaciones pendientes**.

## Compilar y correr

```sh
cmake -S . -B build                       # una vez
cmake --build build --config Debug --target dcemu_tests
ctest --test-dir build -C Debug --output-on-failure
```

Hay una prueba de CTest por suite (`sh4.arith`, `sh4.mov`, ...) mas `sh4.todo`,
que corre todo junto y ademas mide la cobertura de la tabla de opcodes. La
corrida completa toma menos de un segundo.

Tambien se puede correr el binario a mano, con filtros por subcadena sobre el
nombre de la suite o del caso:

```sh
build/tests/Debug/dcemu_tests                    # todo
build/tests/Debug/dcemu_tests arith shift        # dos suites
build/tests/Debug/dcemu_tests div1               # los casos de DIV1
```

Para dejar las pruebas fuera del build: `cmake -S . -B build -DDCEMU_BUILD_TESTS=OFF`.

## Como esta armado

| archivo | que hace |
| --- | --- |
| `dctest.{h,c}` | micro framework: suites, casos, aserciones blandas, runner |
| `arnes.{h,c}` | reset determinista del nucleo, `ejecutar()`, constructores de instrucciones |
| `memoria_prueba.c` | reemplazo de `mem.c`: las dos tablas de 256 entradas, RAM, video, store queues |
| `dobles.c` | reemplazos de `graficos.c`, `iso.c` e `intc.c` |
| `test_*.c` | una suite por archivo de handlers |
| `principal.c` | enumera las suites |

`mem.c` no se puede enlazar porque `pvr_write()` llama a los callbacks de
`graficos.c`, que arrastra SDL y OpenGL. `memoria_prueba.c` reproduce la misma
estructura -- despacho por el byte alto de la direccion -- con RAM del sistema
en `0x0C/0x8C/0xAC`, RAM de video en `0x04/0x05/0xA4/0xA5`, FIFO del TA en
`0x10` y store queues en `0xE0-0xE3`. Las zonas sin mapear cuentan el acceso en
`prueba_accesos_invalidos` en vez de escribir.

Las cabeceras de SDL 1.2 igual hacen falta para *compilar* (`opcodes.h` incluye
`main.h`, que incluye `<SDL/SDL.h>`), pero no se enlaza ninguna funcion de SDL:
el ejecutable de pruebas no necesita las DLL.

## Convenciones

Un caso se escribe asi:

```c
static void addc_genera_acarreo(void)
{
	arnes_reset();

	SR_T = 0;
	R(1) = 0xFFFFFFFF;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300E, 1, 2));	/* ADDC R2, R1 */

	ESPERAR_U32(R(1), 0x00000000);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}
```

- `arnes_reset()` deja registros, PC, SR, FPSCR y una ventana de memoria en
  cero. Todos los casos empiezan igual.
- Las aserciones son **blandas**: la que falla registra la diferencia y el caso
  sigue, para ver todas las diferencias de una vez y no la primera.
- Los ciclos (`core.context.cycles`) no se verifican: los valores de los
  handlers son aproximados y no corresponden a la temporizacion real del SH-4.

### `CASO_XFAIL` para desviaciones conocidas

Hoy no hay ninguno, pero el mecanismo esta y conviene usarlo cuando aparezca
una diferencia que no se pueda arreglar en el momento. Un caso marcado con
`CASO_XFAIL` describe lo que dice el manual y **se espera que falle**: la
desviacion queda escrita y verificada, no como comentario suelto.

Si un `CASO_XFAIL` empieza a pasar, el runner lo reporta como `XPASS` y termina
con error: alguien arreglo el opcode y hay que sacar la marca. Es a proposito --
una nota que dice "esto esta roto" cuando ya no lo esta es peor que no tenerla.

## Que se corrigio

Las 16 desviaciones que estas pruebas encontraron, ya arregladas. Los casos que
las documentaban siguen ahi, ahora en verde.

### Enteros

| instruccion | que hacia | que hace ahora |
| --- | --- | --- |
| `DIV1 Rm,Rn` | terminaba con `T = Q && M` (la linea correcta estaba comentada justo abajo) | `T = (Q == M)`. Con `M = 0` T nunca se prendia: **toda division sin signo daba cociente 0** |
| `MAC.L` con `S = 1` | pisaba MACH entero y sumaba `MACL & 0x7FFF` donde va MACH | saturacion de 48 bits con `long long`; MACH[31:16] se conserva |
| `MAC.L` con `n == m` | leia dos veces la misma direccion | lee `@Rn+` y despues `@Rm+`, como el manual |
| `MOV.B/MOV.W Rm,@-Rn` con `n == m` | copiaba el registro antes de decrementar | decrementa y despues lee Rm |
| `MOV.B R0,@(disp,GBR)` | estaba implementado como carga: no escribia nada | escribe el byte |
| `MOV.W @(disp,GBR),R0` | no extendia el signo | extiende signo |

### Control del sistema

| instruccion | que hacia | que hace ahora |
| --- | --- | --- |
| `LDC Rm,DBR` | leia de memoria, como si fuera `LDC.L @Rm+,DBR` | copia el registro |
| `TRAPA #imm` | no guardaba R15 en SGR | `SGR = R15`, como la secuencia de excepcion del SH-4 |

### Punto flotante

| instruccion | que hacia | que hace ahora |
| --- | --- | --- |
| `FDIV` con divisor 0 | dejaba el registro sin tocar | divide y entrega infinito o NaN, como IEEE 754 |
| `FTRC` (simple y doble) | `(long)` directo en simple y `floor()` en doble | trunca hacia cero y satura en 0x7FFFFFFF / 0x80000000 fuera de rango; NaN da el minimo |
| `FMOV DRm,@-Rn` | escribia 4 bytes | escribe los 8 del par |
| `FIPR FVm,FVn` | escribia en `FR[n+3]` | escribe en `FR[4n+3]`; solo acertaba con `n = 0` |
| `FMOV @(R0,Rm),XDn` | sacaba `n` y `m` de los bits equivocados | `n` de los bits 9-11, `m` de los 4-7 |
| `FNEG FRn` | multiplicaba por -1 | invierte el bit de signo, exacto tambien con ceros |

Fuera de los opcodes, `reset()` en `sh4emu.c` dejaba `FPSCR = 0x0004001` --
0x4001, un cero de menos: en vez de DN prendia un bit de Cause. Ahora es
`0x00040001`.

## Que se implemento

29 filas de `opcodes[]` apuntaban a `NOIMP`. Ya no queda ninguna: la unica que
usa `NOIMP` es la fila comodin que cubre los patrones de bits que no son
instrucciones del SH-4.

- **Enteros**: `ADDV`, `SUBV`, `MAC.W`, `SHAL`.
- **Logicas sobre memoria**: `AND.B`, `OR.B`, `TST.B` y `XOR.B` con
  `#imm,@(R0,GBR)`.
- **Control**: `CLRMAC`, `CLRS`, `SETS`, `LDC Rm,GBR`, `LDC Rm,SPC`,
  `LDC Rm,SGR`, `STC SPC,Rn`, `STC SGR,Rn`, `LDTLB`, `OCBP`, `MOVCA.L`.
- **FPU**: `FABS` (simple y doble), `FCMP/EQ DRm,DRn`, `FMUL DRm,DRn`,
  `FNEG DRn`, `FSUB DRm,DRn`, `FCNVDS`, `FCNVSD`, `FMOV @(R0,Rm),DRn` y
  `FMOV DRm,@Rn`.

`STC SPC,Rn` y `STC SGR,Rn` ya tenian handler escrito (`stc153` y `stcn154`),
solo faltaba conectarlos a la tabla. Lo mismo `ADDV`.

## Limites de "100% el manual"

Lo que se corrigio es el **resultado** de cada instruccion: registros, memoria,
T y los registros de sistema. Quedan tres cosas afuera, y no por descuido:

- **La maquinaria de excepciones de la FPU.** Los campos Cause, Flag y Enable
  de FPSCR no se actualizan nunca, y los bits DN (desnormalizados a cero) y RM
  (modo de redondeo) se ignoran: la aritmetica usa la del host, que es
  redondeo al mas cercano. Un programa que lea FPSCR despues de una operacion
  invalida no vera lo que veria en hardware.
- **`LDTLB`, `OCBP`, `OCBI` y `OCBWB`** avanzan PC y nada mas. `LDTLB` carga la
  TLB, y dcemu no emula la MMU; las tres de cache no tienen efecto observable
  sin cache emulada. `MOVCA.L` si escribe, que es su unico efecto visible.
- **`LDC Rm,SGR` (0x403A) y `LDC.L @Rm+,SGR` (0x4036)**: las dos filas venian
  marcadas "INSERTADA" por los autores originales. El resumen de instrucciones
  del manual del SH-4 lista SGR solo para `STC` y `STC.L`, asi que puede que no
  sean instrucciones reales. Quedaron implementadas de la forma obvia, pero
  vale la pena confirmarlo contra el manual antes de darlas por buenas.

## Agregar un caso

1. Escribir la funcion en el `test_*.c` que corresponda.
2. Agregarla al arreglo `casos[]` del final del archivo.
3. Compilar y correr.

Para un opcode nuevo no hace falta tocar nada mas: la suite `cobertura` recorre
`opcodes[]` sola y avisa si la fila nueva quedo sin probar.
