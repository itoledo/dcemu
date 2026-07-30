# fpu-trampa — la excepción de la FPU, de punta a punta

Programa de KallistiOS escrito para dcemu. `basic/fpu/exc` del árbol de KOS prueba los
campos **Cause** y **Flag** de FPSCR, pero nunca prende un bit de **Enable**, así que nunca
llega a la excepción. Este sí: instala un manejador para `EXC_FPU` (0x120), habilita una
causa por vez y comprueba que la trampa llega con la causa correcta y con Flag intacto.

Lo que lo hace algo más que un mapeo de registros es que el manejador **no salta la
instrucción**: apaga los bits de Enable en `ctx->fpscr` y vuelve. La excepción de FPU es de
reejecución, así que la misma instrucción se repite y esta vez completa. Si el emulador no
restaurara el estado antes de entrar —el contexto *y los dos bancos de registros de punto
flotante*— el resultado final saldría mal, o la instrucción no se repetiría y se colgaría.
El último caso, `probar_reejecucion`, verifica justamente que `1.0f / 0.0f` termina valiendo
`+infinito` después de la vuelta.

## Lo que no prueba, y por qué

**La excepción de FPU deshabilitada (`SR.FD`, códigos 0x800 y 0x820) no se puede probar desde
KOS.** Lo primero que hace su manejador de excepciones es `sts.l fpscr,@-r0`
(`kernel/arch/dreamcast/kernel/entry.s`), que es una instrucción de FPU: con FD puesto se
dispararía a sí misma para siempre. Eso vale igual en hardware real — KOS simplemente no usa
FD. Esa parte queda cubierta por las pruebas unitarias de dcemu, suite `fpu-excepciones`.

## Tres trampas de compilador que costaron tiempo

Las tres son de C, no del emulador, y las tres hacían que la prueba diera un falso negativo:

- **Una división cuyo resultado se descarta desaparece.** Para el estándar de C una división
  no tiene efectos observables —las excepciones de FPU no existen en su modelo—, así que GCC
  la borra entera. Las cuatro operaciones son `noinline` y su resultado va a `sumidero`, que
  es `volatile`.
- **`volatile` en los operandos no basta.** Fija las lecturas de memoria, no la operación
  aritmética que las usa.
- **`__builtin_sh_get_fpscr` y `__builtin_sh_set_fpscr` no son barrera.** Con la operación
  escrita en línea, GCC mueve el `lds fpscr` al otro lado: Enable se prende *después* de la
  división y la trampa no llega nunca. Por eso el programa usa `leer_fpscr()` y
  `escribir_fpscr()`, que son la misma instrucción dentro de un `__asm__ volatile` con
  clobber de memoria.

## Compilar

Necesita el toolchain de KallistiOS. En este equipo está en `C:\dcsdk` y **solo compila desde
`mingw64.exe`**, no desde `msys2.exe`:

```sh
source /opt/toolchains/dc/kos/environ.sh
cd demos/fpu-trampa
make bin          # deja fpu_trampa.elf y fpu_trampa.bin
```

## Correr

```sh
cp demos/fpu-trampa/fpu_trampa.bin build/Release/fpu-trampa.bin
cd build/Release && ./dcemu.exe fpu-trampa.bin
```

El veredicto sale por el serial, en `logs/serial.txt`. Lo que tiene que decir:

```
Probando division por cero...
	OK (Cause 08, Flag 00)
Probando operacion invalida...
	OK (Cause 10, Flag 00)
Probando desbordamiento...
	OK (Cause 05, Flag 00)
Probando subdesbordamiento...
	OK (Cause 03, Flag 00)
Probando que sin Enable no atrapa...
	OK (Flag 08, sin trampa)
Probando la reejecucion...
	OK (la instruccion se repitio y dio +infinito)

TEST SUCCEEDED!
```

`Flag 00` en los cuatro primeros no es un descuido: el manual del SH-4 dice que en una
excepción de FPU el campo Flag **no** se actualiza. Solo Cause.
