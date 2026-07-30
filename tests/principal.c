/****************************************************************************

	Runner de las pruebas unitarias de opcodes del SH-4.

	  dcemu_tests				corre todo
	  dcemu_tests arith shift	corre las suites (o casos) que contengan eso

*****************************************************************************/

#include <stdio.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

/* main.h arrastra <SDL/SDL.h>, que hace "#define main SDL_main" en Windows.
   El emulador lo aprovecha (SDLmain.lib aporta el WinMain), pero aqui no se
   enlaza SDL: el punto de entrada tiene que llamarse main de verdad. */
#undef main

static const dc_suite * const suites[] =
{
	&suite_mov,
	&suite_arith,
	&suite_logic,
	&suite_shift,
	&suite_branch,
	&suite_syscontrol,
	&suite_fpu_simple,
	&suite_fpu_control,
	&suite_fpu_graph,
	&suite_fpu_excepciones,
	&suite_decodificacion,
	&suite_sistema,
	&suite_gdrom,
	&suite_mmu,
	&suite_wdt,
	&suite_tmu,
	&suite_cobertura,	/* al final: mira lo que ejecutaron las demas */
};

int main(int argc, char ** argv)
{
	printf("dcemu - pruebas unitarias de opcodes SH-4\n");

	arnes_iniciar();

	return dc_correr(suites, (int) (sizeof(suites) / sizeof(suites[0])), argc, argv);
}
