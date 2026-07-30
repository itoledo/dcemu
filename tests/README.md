# Pruebas unitarias

Ejercitan los handlers reales del emulador -- `arith.c`, `logic.c`, `shift.c`,
`mov.c`, `branch.c`, `syscontrol.c`, `floatsimple.c`, `floatcontrol.c`,
`floatgraph.c`, `dcopcodes.c` -- con la tabla real de `opcodes.c`, sin SDL, sin
OpenGL y sin imagen de disco. Cada caso arma una instruccion, la ejecuta como
lo hace `main_loop()` y compara el estado resultante contra lo que dice el
manual del SH-4.

Ademas de los opcodes hay dos suites que no son del nucleo, `sistema` y
`gdrom`: son las piezas del arranque por BIOS que resultaron ser logica pura
sobre registros. Ver "Mas alla de los opcodes" al final.

Estado actual: **516 casos, todos en verde**, sobre **239 filas implementadas
de `opcodes[]`, todas ejercitadas y sin desviaciones pendientes**.

## Compilar y correr

```sh
cmake -S . -B build                       # una vez
cmake --build build --config Debug --target dcemu_tests
ctest --test-dir build -C Debug --output-on-failure
```

Hay una prueba de CTest por suite (`sh4.arith`, `sh4.mov`,
`sh4.fpu-excepciones`, ..., `dc.sistema`, `dc.gdrom`) mas `sh4.todo`, que corre
todo junto y ademas mide la cobertura de la tabla de opcodes. La corrida
completa toma menos de un segundo.

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
| `dobles.c` | reemplazos de `graficos.c`, `iso.c`, `intc.c` y `traza.c` |
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

Y en `sh4emu.h`, las macros `FPSCR_CAUSE` y `FPSCR_FLAG` no compilaban: la
primera buscaba un miembro `FPSCR_BITS` que no existe -- es `FPSCR_REG_BITS` --
y la segunda arrancaba en `core.context.FPSCR`, que tampoco existe, ademas de
chocar con la macro `FPSCR`. Nadie las usaba, asi que el compilador nunca las
vio. Se notaron al ir a escribir los campos de excepcion.

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

## Los campos Cause y Flag de FPSCR

La suite `fpu-excepciones` (`test_fpu_excepciones.c`) cubre los dos campos que
el SH-4 escribe en cada operacion de la FPU: **Cause** se rearma en cada
instruccion y **Flag** acumula hasta que alguien escriba FPSCR. Las causas que
dcemu detecta son V (invalida), Z (division por cero), O (desbordamiento) y U
(subdesbordamiento); I aparece solo acompanando a O y a U.

Los casos que mas costaron son los que distinguen una causa de algo que se le
parece:

- **`inf/0` no es division por cero**: el resultado esta definido. Solo lo es un
  dividendo finito distinto de cero.
- **Un NaN que entra y sale no levanta V.** La causa invalida se reconoce porque
  el resultado sale NaN sin que ninguna entrada lo fuera -- eso cubre `inf-inf`,
  `0*inf`, `0/0`, `inf/inf` y la raiz de un negativo.
- **Un infinito que entra y sale no desborda**, por la misma razon.
- **`x*0` no subdesborda** aunque el resultado sea cero.
- **Las sumas y las restas nunca subdesbordan**: cuando su resultado cae por
  debajo del minimo normal es exacto, y sin inexactitud no hay
  subdesbordamiento. `fsub_con_resultado_subnormal_no_subdesborda` lo fija.

Los valores de doble precision muy grandes y muy chicos se arman elevando al
cuadrado, porque `FCNVSD` no deja entrar nada que no quepa en un float. Ojo con
la cuenta: `FLT_MAX` es 2^128 *menos un poco*, y ese poco alcanza para que el
tercer cuadrado quede por debajo de `DBL_MAX` en vez de desbordar.

La verificacion de punta a punta es `basic/fpu/exc` de KallistiOS, que prueba
las cuatro causas y ahora reporta `TEST SUCCEEDED!`.

### La trampa: Enable y las tres excepciones

La misma suite cubre el otro lado del registro: cuando una causa coincide con su
bit de **Enable**, el SH-4 entra en la excepcion de FPU (0x120) y deja el
registro destino y el campo Flag **sin tocar**. Mas las dos de FPU deshabilitada
(`SR.FD`): 0x800, y 0x820 si la instruccion estaba en una ranura de retardo.

Estos casos no se pueden escribir con `ejecutar()`: hace falta el ciclo entero
de `main_loop()` -- instantanea, `setjmp`, restaurar, entrar a la excepcion --,
porque lo que hay que verificar no es solo EXPEVT y el salto a VBR, sino que el
destino quedo como estaba. Eso es `ejecutar_vigilado()` en el arnes, que
devuelve 1 si la instruccion aborto y 0 si no.

Cuatro casos de esta parte encontraron dos bugs de verdad, los dos en plomeria
que ya existia para la MMU:

- **La instantanea nunca copio los bancos de punto flotante.** Era un `memcpy`
  de `core.context`, que solo guarda *punteros* a los dos bancos. O sea que la
  reejecucion de la MMU tampoco restauraba los registros de FP desde la fase 5
  de `docs/mmu-plan.md`. Ahora la toman y la restauran
  `excepcion_instantanea_tomar()` y `..._restaurar()`.
- **`fpu_deshabilitada` podia quedar desincronizada de `SR.FD`.** La escribia
  `UpdateSR()`, y `arnes_reset()` asigna `SR = 0` directo. La deriva
  `excepcion_actualizar_vigilancia()`, que es la unica que la escribe.

`demos/fpu-trampa` es la version de punta a punta: un binario de KOS que
instala un manejador con `irq_set_handler(EXC_FPU, ...)` y comprueba que la
instruccion se reejecuta. La excepcion de FD no se puede probar asi -- el
manejador de KOS empieza con `sts.l fpscr,@-r0` y con FD puesto se dispararia a
si mismo --, por eso 0x800 y 0x820 solo tienen casos unitarios.

## Limites de "100% el manual"

Lo que se corrigio es el **resultado** de cada instruccion: registros, memoria,
T y los registros de sistema. Quedan tres cosas afuera, y no por descuido:

- **Dos rincones de la FPU.** Cause, Flag, Enable y las tres excepciones estan
  -- suite `fpu-excepciones` --, pero la causa I no se detecta por si sola,
  solo acompanando a O y a U, y los bits DN (desnormalizados a cero) y RM (modo
  de redondeo) se siguen ignorando. El detalle esta en
  `docs/sh4-conformidad.md`.
- **`OCBP`, `OCBI` y `OCBWB`** avanzan PC y nada mas: sin cache emulada no
  tienen efecto observable. `MOVCA.L` si escribe, que es su unico efecto
  visible. `LDTLB` ya no esta en esta lista: carga la UTLB de verdad (suite
  `mmu`, y `ldtlb_carga_la_tlb` en la suite `syscontrol`).
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

## Mas alla de los opcodes

El arranque por el boot ROM (ver `docs/bios-boot-plan.md`) trajo dos piezas que
se prueban con el mismo arnes porque tampoco necesitan SDL: son funciones de
(estado, registro escrito) y nada mas. La suite `cobertura` no aplica --
no son filas de `opcodes[]` --, asi que van con el prefijo `dc.` en CTest.

| suite | archivo | que cubre |
| --- | --- | --- |
| `sistema` | `test_sistema.c` | el handshake de PDTRA/PCTRA, la sintesis de una flash minima y la cuenta de segundos desde 1950 del RTC |
| `gdrom` | `test_gdrom.c` | la maquina de estados de la lectora: fases del comando PACKET, los comandos SPI del arranque, la TOC, la lectura de sectores por PIO y por DMA, y el estado con y sin disco |
| `ta` | `test_ta.c` | el formato de los parametros del tile accelerator: la tabla que cruza textura, tipo de color, volumen y ancho de UV para dar el tipo de parametro global y el de vertice, los tamanos de cada uno, y el rearmado de los que miden 64 bytes |

La suite `ta` cubre una clase de error que no deja rastro. Un parametro de 64
bytes llega en **dos** bloques de 32, uno por store queue, y despachar cada uno
por separado hace que la segunda mitad se lea como palabra de control; cuando su
primera palabra es un float que vale 0.0 el tipo sale 0, o sea fin de lista, y se
cierra una lista que el guest nunca cerro. `la_segunda_mitad_en_cero_no_cierra_la_lista`
es exactamente ese caso. El resto de la suite fija la tabla del manual casilla
por casilla, que es donde un error se paga leyendo el vertice desde el
desplazamiento equivocado.

`dobles.c` crecio para servirlas: cuenta las interrupciones que levanta la
lectora (normales y externas), simula la bandeja vacia con
`dobles_hay_disco` y aporta `traza_activa` en cero, porque `traza.c` no se
puede enlazar aca (desensambla, y eso arrastra `debug.c` con SDL).

La suite del GD-ROM verifica la secuencia completa tal como la ejecuta el boot
ROM, no funciones sueltas: escribe el byte count y el comando `0xA0`, comprueba
que la lectora pida el paquete con CoD=1, manda los 12 bytes de a palabra, y
recorre los bloques DRQ leyendo el registro de datos hasta que el comando
termina. Un cambio que rompa una transicion se ve como un caso rojo, no como
una pantalla negra.
