/****************************************************************************

	OPCIONES - argumentos de la linea de comandos. Ver opciones.h.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
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
	0,					/* limitar */
	0,					/* salir_tras */
	-1,					/* cifrado: mirar el prologo */
	CABLE_VGA,
	BANDEJA_AUTO,
	NULL,
	NULL,				/* captura_gl */
	0,					/* watchpoint: apagado */
	4,					/* watchpoint_tam */
	0,					/* watchpoint_lect: apagado */
	4,					/* watchpoint_lect_tam */
	0,					/* traza_desde: apagada */
	0,					/* traza_desde_n */
	0,					/* traza_desde_salto */
	{ { 0, 0 } },		/* desensamblar */
	0,
	{ { 0, 0 } },		/* volcar */
	0
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
		"  --captura-gl=ARCHIVO  vuelca a un BMP lo que OpenGL rasterizo, en cada\n"
		"                        cuadro. Verifica el 3D sin capturar la ventana.\n"
		"  --traza-desde=PC[:N[:K]]\n"
		"                        desensambla las N instrucciones que siguen a la\n"
		"                        llegada a PC, saltandose las K primeras, con los\n"
		"                        registros que cambian. Necesita --traza-mem.\n"
		"  --watchpoint-lectura=D[:T]\n"
		"                        lo mismo para las lecturas: una linea por cada\n"
		"                        PC distinto que mire esa direccion.\n"
		"  --watchpoint=D[:T]    informa cada escritura que toque la direccion D\n"
		"                        (hexadecimal), de T bytes (1, 2 o 4; 4 por\n"
		"                        omision), con el PC y el PR que la hicieron.\n"
		"  --salir-tras=N        sale solo a los N segundos de tiempo emulado,\n"
		"                        por el mismo camino que cerrar la ventana.\n"
		"  --desensamblar=D:N    al salir, desensambla N instrucciones desde la\n"
		"                        direccion D (hexadecimal). Repetible.\n"
		"  --volcar=D:N          al salir, vuelca N bytes desde D en hexadecimal.\n"
		"                        Repetible.\n"
		"  --cifrado             el .bin suelto viene cifrado (como en un disco).\n"
		"  --sin-cifrado         no lo esta. Por omision se mira su prologo.\n"
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

/*
	"D:N" o "D" -- direccion y cantidad, las dos en hexadecimal porque todo lo
	que se mira en el arranque son direcciones. Sin ":N" se usa el valor por
	omision que pase el llamador.
*/
static int parsear_rango(const char * valor, struct rango_t * rangos, int * n,
	unsigned long por_omision)
{
	char *			fin;
	unsigned long	direccion, cantidad = por_omision;

	if (*n >= RANGOS_MAX)
	{
		fprintf(stderr, "no caben mas de %d rangos\n", RANGOS_MAX);
		return 1;
	}

	direccion = strtoul(valor, &fin, 16);

	if (fin == valor)
		return 1;

	if (*fin == ':')
	{
		const char * resto = fin + 1;

		cantidad = strtoul(resto, &fin, 16);

		if (fin == resto)
			return 1;
	}

	if (*fin != '\0' || cantidad == 0)
		return 1;

	rangos[*n].direccion = direccion;
	rangos[*n].cantidad  = cantidad;
	(*n)++;

	return 0;
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
		if (strcmp(arg, "--cifrado") == 0)
		{
			opciones.cifrado = 1;
		}
		else
		if (strcmp(arg, "--sin-cifrado") == 0)
		{
			opciones.cifrado = 0;
		}
		else
		if (strncmp(arg, "--captura-gl=", 13) == 0)
		{
			opciones.captura_gl = &arg[13];

			if (opciones.captura_gl[0] == '\0')
			{
				fprintf(stderr, "--captura-gl necesita un nombre de archivo\n");
				return 1;
			}
		}
		else
		if (strncmp(arg, "--watchpoint=", 13) == 0)
		{
			struct rango_t	r[1];
			int				n = 0;

			/* Misma sintaxis "D:N" que los rangos, con el tamano en lugar de
			   la cantidad: 1, 2 o 4 bytes. */
			if (parsear_rango(&arg[13], r, &n, 4) != 0 ||
				(r[0].cantidad != 1 && r[0].cantidad != 2 && r[0].cantidad != 4))
			{
				fprintf(stderr, "watchpoint invalido: %s (DIR[:1|2|4])\n", &arg[13]);
				return 1;
			}

			opciones.watchpoint     = r[0].direccion;
			opciones.watchpoint_tam = r[0].cantidad;
		}
		else
		if (strncmp(arg, "--watchpoint-lectura=", 21) == 0)
		{
			struct rango_t	r[1];
			int				n = 0;

			if (parsear_rango(&arg[21], r, &n, 4) != 0 ||
				(r[0].cantidad != 1 && r[0].cantidad != 2 && r[0].cantidad != 4))
			{
				fprintf(stderr, "watchpoint de lectura invalido: %s (DIR[:1|2|4])\n",
					&arg[21]);
				return 1;
			}

			opciones.watchpoint_lect     = r[0].direccion;
			opciones.watchpoint_lect_tam = r[0].cantidad;
		}
		else
		if (strncmp(arg, "--traza-desde=", 14) == 0)
		{
			/* "PC[:N[:K]]": K llegadas a saltar antes de trazar. El boot ROM
			   pasa dos veces por el mismo codigo -- al encender y al reiniciar
			   --, asi que sin el salto siempre se traza la primera. */
			char *			fin;
			unsigned long	pc, n = 0x1000, k = 0;

			pc = strtoul(&arg[14], &fin, 16);

			if (fin == &arg[14])
			{
				fprintf(stderr, "traza-desde invalida: %s (PC[:N[:K]])\n", &arg[14]);
				return 1;
			}

			if (*fin == ':')
				n = strtoul(fin + 1, &fin, 16);

			if (*fin == ':')
				k = strtoul(fin + 1, &fin, 16);

			if (*fin != '\0' || n == 0)
			{
				fprintf(stderr, "traza-desde invalida: %s (PC[:N[:K]])\n", &arg[14]);
				return 1;
			}

			opciones.traza_desde       = pc;
			opciones.traza_desde_n     = n;
			opciones.traza_desde_salto = k;
		}
		else
		if (strncmp(arg, "--salir-tras=", 13) == 0)
		{
			opciones.salir_tras = atoi(&arg[13]);

			if (opciones.salir_tras <= 0)
			{
				fprintf(stderr, "segundos invalidos: %s\n", &arg[13]);
				return 1;
			}
		}
		else
		if (strncmp(arg, "--desensamblar=", 15) == 0)
		{
			if (parsear_rango(&arg[15], opciones.desensamblar,
					&opciones.desensamblar_n, 32) != 0)
			{
				fprintf(stderr, "rango invalido: %s\n", &arg[15]);
				return 1;
			}
		}
		else
		if (strncmp(arg, "--volcar=", 9) == 0)
		{
			if (parsear_rango(&arg[9], opciones.volcar,
					&opciones.volcar_n, 0x100) != 0)
			{
				fprintf(stderr, "rango invalido: %s\n", &arg[9]);
				return 1;
			}
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
