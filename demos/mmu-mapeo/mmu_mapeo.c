/*
	mmu_mapeo.c -- mapeo de una pagina y reejecucion, de punta a punta.

	`basic/mmu/nullptr` de KallistiOS prueba que un acceso a una pagina sin
	mapear levanta el fallo de TLB con el PC correcto, pero su callback devuelve
	NULL a proposito, asi que **nunca llega a mapear ni a reejecutar**: termina
	en el panic que la demo espera.

	Esto cubre la otra mitad, que es la que `docs/mmu-plan.md` deja anotada como
	"probada solo por construccion":

	  1. se mapea una pagina virtual a una fisica de la RAM del sistema,
	  2. se escribe por la direccion virtual, lo que falla en la TLB,
	  3. el manejador de KOS carga la entrada y hace RTE,
	  4. la instruccion se reejecuta y la escritura llega,
	  5. se lee por la direccion **fisica** (P1, sin traducir) y se compara.

	El paso 5 es el que importa: si la traduccion resolviera a otra pagina, o si
	la reejecucion no rehiciera la escritura, el valor no estaria ahi.

	Compilar con el toolchain de KOS y correr el .bin resultante en dcemu.
	Ver demos/mmu-mapeo/README.md.
*/

#include <kos.h>
#include <arch/mmu.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* La pagina virtual que se mapea. No se usa la 0 porque KOS necesita distinguir
   un mapeo legitimo de un puntero nulo, y porque la demo tiene que poder
   imprimir por el serial mientras tanto. */
#define PAGINA_VIRTUAL	0x10000000u

/* Un buffer alineado a pagina en RAM del sistema, que hace de destino fisico.
   Se lee despues por P1 (0x8C...), que nunca pasa por la TLB. */
static uint8_t destino[PAGESIZE * 2] __attribute__((aligned(PAGESIZE)));

static uint32_t fisica_de(const void *p) {
    /* Quitar la ventana: P1 es 0x8C000000 y la fisica es 0x0C000000. */
    return ((uint32_t)p) & 0x1FFFFFFFu;
}

int main(int argc, char **argv) {
    mmucontext_t *cxt;
    volatile uint32_t *virt = (volatile uint32_t *)PAGINA_VIRTUAL;
    volatile uint32_t *fisico;
    uint32_t base_fisica;
    bool bien = true;

    (void)argc;
    (void)argv;

    printf("Prueba de mapeo y reejecucion de la MMU\n");

    mmu_init();

    cxt = mmu_context_create(0);
    if(!cxt) {
        fprintf(stderr, "\nNo se pudo crear el contexto\n");
        fprintf(stderr, "\nTEST FAILED!\n");
        return EXIT_FAILURE;
    }

    mmu_use_table(cxt);
    mmu_switch_context(cxt);

    base_fisica = fisica_de(destino);
    printf("\tvirtual  %08lx\n", (unsigned long)PAGINA_VIRTUAL);
    printf("\tfisica   %08lx (destino en %p)\n",
           (unsigned long)base_fisica, (void *)destino);

    mmu_page_map(cxt,
                 PAGINA_VIRTUAL >> PAGESIZE_BITS,
                 base_fisica >> PAGESIZE_BITS,
                 1,                     /* una sola pagina */
                 MMU_ALL_RDWR,
                 MMU_NO_CACHE,
                 0,                     /* no compartida */
                 1);                    /* sucia: escribir no da primera escritura */

    /* El acceso que falla en la TLB. Al volver del manejador la instruccion se
       repite y la escritura llega. */
    printf("Escribiendo por la direccion virtual...\n");
    virt[0] = 0xDEADBEEF;
    virt[1] = 0x12345678;

    /* Y la lectura de control, por P1: sin traducir. */
    fisico = (volatile uint32_t *)(0x8C000000u | base_fisica);

    printf("\tvirt[0] = %08lx, fisico[0] = %08lx\n",
           (unsigned long)virt[0], (unsigned long)fisico[0]);
    printf("\tvirt[1] = %08lx, fisico[1] = %08lx\n",
           (unsigned long)virt[1], (unsigned long)fisico[1]);

    if(fisico[0] != 0xDEADBEEF) {
        fprintf(stderr, "\tFALLA: fisico[0] = %08lx, se esperaba deadbeef\n",
                (unsigned long)fisico[0]);
        bien = false;
    }

    if(fisico[1] != 0x12345678) {
        fprintf(stderr, "\tFALLA: fisico[1] = %08lx, se esperaba 12345678\n",
                (unsigned long)fisico[1]);
        bien = false;
    }

    /* Leer por la virtual tiene que dar lo mismo: la entrada ya esta cargada,
       asi que esto ya no falla en la TLB. */
    if(virt[0] != 0xDEADBEEF || virt[1] != 0x12345678) {
        fprintf(stderr, "\tFALLA: la lectura por la virtual no coincide\n");
        bien = false;
    }

    /* Escribir por la fisica y leer por la virtual cierra el circulo en la otra
       direccion: confirma que son la misma memoria y no dos copias. */
    fisico[0] = 0xCAFEBABE;

    if(virt[0] != 0xCAFEBABE) {
        fprintf(stderr, "\tFALLA: virt[0] = %08lx tras escribir por la fisica,"
                        " se esperaba cafebabe\n", (unsigned long)virt[0]);
        bien = false;
    }
    else {
        printf("\tla escritura por la fisica se ve por la virtual\n");
    }

    mmu_shutdown();

    if(bien) {
        printf("\nTEST SUCCEEDED!\n");
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "\nTEST FAILED!\n");
    return EXIT_FAILURE;
}
