/*
	fpu_trampa.c -- la trampa de excepcion de la FPU del SH-4, de punta a punta.

	`basic/fpu/exc` de KallistiOS prueba los campos Cause y Flag de FPSCR, pero
	nunca prende un bit de Enable, asi que no llega a la excepcion. Esto si:
	instala un manejador para EXC_FPU (0x120), habilita una causa por vez y
	comprueba que la excepcion llega con la causa correcta.

	Lo que hace interesante al manejador es que **no salta la instruccion**:
	apaga los bits de Enable en el contexto y vuelve. La excepcion de FPU es de
	reejecucion, asi que la misma instruccion se repite y ahora si completa. Si
	el emulador no restaurara el estado antes de entrar, el resultado que quede
	al final estaria mal, o la instruccion no se repetiria y se colgaria.

	No se prueba aca la excepcion de FPU deshabilitada (SR.FD, 0x800/0x820), y
	no por falta de ganas: lo primero que hace el manejador de excepciones de
	KOS es `sts.l fpscr,@-r0` (kernel/arch/dreamcast/kernel/entry.s), que es una
	instruccion de FPU. Con FD puesto se dispararia a si misma para siempre, en
	hardware real igual que en el emulador. Esa queda cubierta por las pruebas
	unitarias de dcemu, suite `fpu-excepciones`.

	Compilar con el toolchain de KOS y correr el .bin resultante en dcemu.
	Ver demos/fpu-trampa/README.md.
*/

#include <kos.h>
#include <arch/irq.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Los cinco bits de Enable de FPSCR, bits 11-7. */
#define ENABLE_I	(1u << 7)
#define ENABLE_U	(1u << 8)
#define ENABLE_O	(1u << 9)
#define ENABLE_Z	(1u << 10)
#define ENABLE_V	(1u << 11)
#define ENABLE_TODOS	0x00000F80u

/* Cause son los bits 17-12, con las mismas causas en el mismo orden. */
#define CAUSE_DE(f)	(((f) >> 12) & 0x3Fu)
#define FLAG_DE(f)	(((f) >> 2) & 0x1Fu)

#define CAUSA_I		0x01u
#define CAUSA_U		0x02u
#define CAUSA_O		0x04u
#define CAUSA_Z		0x08u
#define CAUSA_V		0x10u

/*
	Accesores propios en vez de __builtin_sh_get_fpscr / __builtin_sh_set_fpscr.
	Los builtins no son barrera para el compilador, y con la operacion escrita
	en linea GCC mueve el `lds fpscr` al otro lado de ella -- se prende Enable
	despues de la division y la trampa no llega. Con `volatile` y el clobber de
	memoria no se puede reordenar. Costo cero: es la misma instruccion.
*/
static inline unsigned leer_fpscr(void) {
    unsigned v;
    __asm__ volatile("sts fpscr, %0" : "=r"(v) : : "memory");
    return v;
}

static inline void escribir_fpscr(unsigned v) {
    __asm__ volatile("lds %0, fpscr" : : "r"(v) : "memory");
}

static volatile unsigned trampas;
static volatile unsigned codigo_visto;
static volatile unsigned cause_visto;
static volatile unsigned flag_visto;

static void manejador(irq_t code, irq_context_t *ctx, void *data) {
    (void)data;

    trampas++;
    codigo_visto = code;
    cause_visto  = CAUSE_DE(ctx->fpscr);
    flag_visto   = FLAG_DE(ctx->fpscr);

    /* La excepcion es de reejecucion: volver sin cambiar nada repetiria la
       misma instruccion y la misma excepcion, para siempre. Apagar Enable en
       el contexto deja que la segunda vez complete. */
    ctx->fpscr &= ~ENABLE_TODOS;
}

/*
	Las cuatro operaciones que disparan cada causa.

	Tres precauciones, y las tres hicieron falta de verdad:

	  - `volatile` en los operandos, para que no se resuelva en compilacion.
	  - `noinline`, porque una division cuyo resultado se descarta no tiene
	    efectos observables para el estandar de C y GCC la borra entera. Con la
	    operacion detras de una llamada que no se puede inlinear, se ejecuta.
	  - el resultado va a `sumidero`, por la misma razon.
*/
static volatile float sumidero;

static float __attribute__((noinline)) division_por_cero(void) {
    volatile float a = 1.0f, b = 0.0f;
    return a / b;
}

static float __attribute__((noinline)) invalida(void) {
    volatile float a = 0.0f, b = 0.0f;
    return a / b;
}

static float __attribute__((noinline)) desbordamiento(void) {
    volatile float a = 3.0e38f, b = 2.0f;
    return a * b;
}

static float __attribute__((noinline)) subdesbordamiento(void) {
    volatile float a = 1.0e-30f, b = 1.0e-20f;
    return a * b;
}

static bool probar(const char *nombre, unsigned enable, unsigned causa_esperada,
                   float (*operacion)(void)) {
    unsigned fpscr;
    bool bien = true;

    trampas = 0;
    codigo_visto = 0;
    cause_visto = 0;
    flag_visto = 0;

    printf("Probando %s...\n", nombre);

    /* Enable se prende justo antes y el manejador lo apaga: cuanto menos
       tiempo este puesto, menos posibilidades de que lo vea otra cosa. */
    fpscr = leer_fpscr();
    escribir_fpscr((fpscr & ~ENABLE_TODOS) | enable);

    sumidero = operacion();

    escribir_fpscr(fpscr & ~ENABLE_TODOS);

    if(trampas != 1) {
        fprintf(stderr, "\tFALLA: %u trampas, se esperaba 1\n", trampas);
        bien = false;
    }

    if(codigo_visto != EXC_FPU) {
        fprintf(stderr, "\tFALLA: codigo %04x, se esperaba %04x\n",
                codigo_visto, (unsigned)EXC_FPU);
        bien = false;
    }

    if(cause_visto != causa_esperada) {
        fprintf(stderr, "\tFALLA: Cause %02x, se esperaba %02x\n",
                cause_visto, causa_esperada);
        bien = false;
    }

    /* El manual es explicito: en una excepcion de FPU el campo Flag no se
       actualiza. Solo Cause. */
    if(flag_visto != 0) {
        fprintf(stderr, "\tFALLA: Flag %02x, se esperaba 0\n", flag_visto);
        bien = false;
    }

    if(bien)
        printf("\tOK (Cause %02x, Flag %02x)\n", cause_visto, flag_visto);

    return bien;
}

/* Sin Enable no tiene que haber trampa: la causa se anota en Flag y ya. */
static bool probar_sin_enable(void) {
    unsigned fpscr;

    trampas = 0;

    printf("Probando que sin Enable no atrapa...\n");

    fpscr = leer_fpscr();
    escribir_fpscr(fpscr & ~ENABLE_TODOS);

    sumidero = division_por_cero();

    fpscr = leer_fpscr();
    escribir_fpscr(fpscr & ~ENABLE_TODOS);

    if(trampas != 0) {
        fprintf(stderr, "\tFALLA: %u trampas, se esperaba 0\n", trampas);
        return false;
    }

    if(!(FLAG_DE(fpscr) & CAUSA_Z)) {
        fprintf(stderr, "\tFALLA: Flag %02x, falta la Z\n", FLAG_DE(fpscr));
        return false;
    }

    printf("\tOK (Flag %02x, sin trampa)\n", FLAG_DE(fpscr));
    return true;
}

/* La instruccion se reejecuta: al volver del manejador tiene que completar y
   dejar el resultado correcto. Es la prueba de que el emulador restauro el
   estado en vez de dejar la operacion a medias. */
static bool probar_reejecucion(void) {
    unsigned fpscr;
    float resultado;

    trampas = 0;

    printf("Probando la reejecucion...\n");

    fpscr = leer_fpscr();
    escribir_fpscr((fpscr & ~ENABLE_TODOS) | ENABLE_Z);

    resultado = division_por_cero();

    escribir_fpscr(fpscr & ~ENABLE_TODOS);

    if(trampas != 1) {
        fprintf(stderr, "\tFALLA: %u trampas, se esperaba 1\n", trampas);
        return false;
    }

    /* 1/0 con la excepcion ya deshabilitada es +infinito, no basura ni cero. */
    if(!isinf(resultado) || resultado < 0.0f) {
        fprintf(stderr, "\tFALLA: resultado %f, se esperaba +infinito\n",
                (double)resultado);
        return false;
    }

    printf("\tOK (la instruccion se repitio y dio +infinito)\n");
    return true;
}

int main(int argc, char **argv) {
    bool bien = true;
    unsigned fpscr;

    (void)argc;
    (void)argv;

    /* Salida de emergencia. */
    cont_btn_callback(0, CONT_START, (cont_btn_callback_t)exit);

    printf("Prueba de la trampa de excepcion de la FPU\n");

    fpscr = leer_fpscr();
    printf("\tFPSCR inicial: %08x\n", fpscr);

    if(irq_set_handler(EXC_FPU, manejador, NULL) < 0) {
        fprintf(stderr, "\nNo se pudo instalar el manejador\n");
        fprintf(stderr, "\nTEST FAILED!\n");
        return EXIT_FAILURE;
    }

    bien &= probar("division por cero", ENABLE_Z, CAUSA_Z, division_por_cero);
    bien &= probar("operacion invalida", ENABLE_V, CAUSA_V, invalida);
    bien &= probar("desbordamiento", ENABLE_O, CAUSA_O | CAUSA_I, desbordamiento);
    bien &= probar("subdesbordamiento", ENABLE_U, CAUSA_U | CAUSA_I, subdesbordamiento);
    bien &= probar_sin_enable();
    bien &= probar_reejecucion();

    irq_set_handler(EXC_FPU, NULL, NULL);

    if(bien) {
        printf("\nTEST SUCCEEDED!\n");
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "\nTEST FAILED!\n");
    return EXIT_FAILURE;
}
