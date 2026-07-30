/****************************************************************************

	OPCIONES - argumentos de la linea de comandos. Ver opciones.h.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "opciones.h"
#include "options.h"

struct opciones_t opciones =
{
	0,					/* arranque_bios: por omision, el camino de siempre */
#ifdef BIOS_HACKS
	1,					/* hacks_bios */
#else
	0,
#endif
	0,					/* traza_mem */
	CABLE_VGA,
	BANDEJA_AUTO,
	NULL
};

void opciones_ayuda(const char * programa)
{
	fprintf(stderr,
		"uso: %s [opciones] [1st_read.bin | imagen.iso | imagen.cue]\n"
		"\n"
		"  --bios                arranca en el vector de reset (0xA0000000) y deja que\n"
		"                        el boot ROM haga el trabajo. El argumento posicional,\n"
		"                        si lo hay, es la imagen que ve la lectora.\n"
		"  --cable=TIPO          vga (por omision), rgb o compuesto.\n"
		"  --bandeja=ESTADO      auto (por omision), disco, vacia o abierta.\n"
		"  --traza-mem           reporta a stderr los accesos a direcciones sin\n"
		"                        emular y donde se traba el PC.\n"
		"  --limitar             no dejar que la emulacion corra mas rapido que\n"
		"                        una consola. Solo frena, nunca acelera.\n"
		"  --hacks-bios          fuerza los hooks de syscall de la BIOS.\n"
		"  --sin-hacks-bios      los desactiva (obligatorio para arrancar de verdad\n"
		"                        por el boot ROM: los hooks pisan la RAM baja).\n"
		"  --ayuda               esto.\n"
		"\n"
		"Sin --bios se carga ip.bin y el ejecutable a mano y se arranca en el\n"
		"bootstrap, que es como funciono dcemu siempre.\n",
		programa);
}

static int parsear_cable(const char * valor)
{
	if (strcmp(valor, "vga") == 0)			return CABLE_VGA;
	if (strcmp(valor, "rgb") == 0)			return CABLE_RGB;
	if (strcmp(valor, "compuesto") == 0)	return CABLE_COMPUESTO;

	return -1;
}

static int parsear_bandeja(const char * valor)
{
	if (strcmp(valor, "auto") == 0)		return BANDEJA_AUTO;
	if (strcmp(valor, "disco") == 0)	return BANDEJA_DISCO;
	if (strcmp(valor, "vacia") == 0)	return BANDEJA_VACIA;
	if (strcmp(valor, "abierta") == 0)	return BANDEJA_ABIERTA;

	return -1;
}

int opciones_parsear(int argc, char ** argv)
{
	int i;

	for (i = 1; i < argc; i++)
	{
		const char * arg = argv[i];

		if (strcmp(arg, "--bios") == 0)
		{
			opciones.arranque_bios = 1;
		}
		else
		if (strcmp(arg, "--traza-mem") == 0)
		{
			opciones.traza_mem = 1;
		}
		else
		if (strcmp(arg, "--limitar") == 0)
		{
			opciones.limitar = 1;
		}
		else
		if (strcmp(arg, "--hacks-bios") == 0)
		{
			opciones.hacks_bios = 1;
		}
		else
		if (strcmp(arg, "--sin-hacks-bios") == 0)
		{
			opciones.hacks_bios = 0;
		}
		else
		if (strncmp(arg, "--cable=", 8) == 0)
		{
			opciones.cable = parsear_cable(&arg[8]);

			if (opciones.cable < 0)
			{
				fprintf(stderr, "cable desconocido: %s\n", &arg[8]);
				return 1;
			}
		}
		else
		if (strncmp(arg, "--bandeja=", 10) == 0)
		{
			opciones.bandeja = parsear_bandeja(&arg[10]);

			if (opciones.bandeja < 0)
			{
				fprintf(stderr, "estado de bandeja desconocido: %s\n", &arg[10]);
				return 1;
			}
		}
		else
		if (strcmp(arg, "--ayuda") == 0 || strcmp(arg, "--help") == 0)
		{
			opciones_ayuda(argv[0]);
			return -1;
		}
		else
		if (arg[0] == '-' && arg[1] != '\0')
		{
			fprintf(stderr, "opcion desconocida: %s\n", arg);
			opciones_ayuda(argv[0]);
			return 1;
		}
		else
		if (opciones.imagen == NULL)
		{
			opciones.imagen = arg;
		}
		else
		{
			fprintf(stderr, "sobra el argumento: %s\n", arg);
			return 1;
		}
	}

	/* Los hooks de syscall escriben codigo en 0x8C000100-0x8C000500, que es
	   justo donde el boot ROM se instala a si mismo. Arrancar por BIOS con
	   los hooks puestos no tiene sentido, asi que se apagan solos. */
	if (opciones.arranque_bios && opciones.hacks_bios)
	{
		fprintf(stderr, "--bios: desactivando los hooks de syscall de la BIOS.\n");
		opciones.hacks_bios = 0;
	}

	return 0;
}
