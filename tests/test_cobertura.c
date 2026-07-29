/****************************************************************************

	Cobertura -- ninguna fila implementada de opcodes[] queda sin ejercitar

	No mide lineas de codigo: recorre la tabla real de opcodes y verifica que
	cada fila con handler propio haya sido ejecutada al menos una vez por
	alguna suite. Las filas que apuntan a NOIMP no cuentan, porque no hay nada
	que ejercitar; la suite de decodificacion se encarga de esas.

	Depende de que hayan corrido todas las suites, asi que se saltea cuando la
	corrida trae filtros.

*****************************************************************************/

#include "arnes.h"
#include "dcopcodes.h"
#include "suites.h"

static void toda_fila_implementada_se_ejercita(void)
{
	long f;
	int sin_cubrir = 0;
	int implementadas = 0;

	if (!dc_corrida_completa())
	{
		printf("         (omitida: la cobertura solo se mide con la corrida completa)\n");
		return;
	}

	for (f = 0; opcodes[f].opdesc; f++)
	{
		if (opcodes[f].funcion == NOIMP)
			continue;

		implementadas++;

		if (!arnes_handler_ejecutado(opcodes[f].funcion))
		{
			if (sin_cubrir < 30)
				dc_anotar(__FILE__, __LINE__, "sin probar: %s (0x%04X, fila %ld)",
						  opcodes[f].opdesc, (unsigned int) opcodes[f].op, f);
			sin_cubrir++;
		}
	}

	printf("         %d filas implementadas, %d sin probar\n",
		   implementadas, sin_cubrir);

	ESPERAR_U32(sin_cubrir, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(toda_fila_implementada_se_ejercita),
};

const dc_suite suite_cobertura = DEFINIR_SUITE("cobertura", casos);
