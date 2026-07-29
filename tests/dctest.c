/****************************************************************************

	DCTEST - implementacion del micro framework. Ver dctest.h.

*****************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dctest.h"

#define MAX_DIFERENCIAS	20	/* por caso; mas que eso es ruido */

static int diferencias;			/* del caso en curso */
static int diferencias_vistas;	/* incluye las que no se imprimieron */
static int silencio;			/* 1 mientras corre un XFAIL */

void dc_empezar_caso(void)
{
	diferencias = 0;
	diferencias_vistas = 0;
}

int dc_fallas_del_caso(void)
{
	return diferencias_vistas;
}

static void reportar(const char * arch, int linea, const char * fmt, va_list args)
{
	const char * base;

	diferencias_vistas++;

	if (silencio)
		return;

	if (diferencias >= MAX_DIFERENCIAS)
	{
		if (diferencias == MAX_DIFERENCIAS)
		{
			printf("        (mas diferencias omitidas)\n");
			diferencias++;
		}
		return;
	}

	diferencias++;

	/* solo el nombre del archivo, sin la ruta del build */
	base = strrchr(arch, '\\');
	if (!base)
		base = strrchr(arch, '/');
	base = base ? base + 1 : arch;

	printf("        %s:%d: ", base, linea);
	vprintf(fmt, args);
	printf("\n");
}

static void diferencia(const char * arch, int linea, const char * fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	reportar(arch, linea, fmt, args);
	va_end(args);
}

void dc_anotar(const char * arch, int linea, const char * fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	reportar(arch, linea, fmt, args);
	va_end(args);
}

void dc_cmp_u32(const char * arch, int linea, const char * expr,
				unsigned int obtenido, unsigned int esperado)
{
	if (obtenido != esperado)
		diferencia(arch, linea, "%s = 0x%08X (%u), se esperaba 0x%08X (%u)",
				   expr, obtenido, obtenido, esperado, esperado);
}

void dc_cmp_i32(const char * arch, int linea, const char * expr,
				int obtenido, int esperado)
{
	if (obtenido != esperado)
		diferencia(arch, linea, "%s = %d (0x%08X), se esperaba %d (0x%08X)",
				   expr, obtenido, (unsigned int) obtenido,
				   esperado, (unsigned int) esperado);
}

void dc_cmp_f32(const char * arch, int linea, const char * expr,
				float obtenido, float esperado, float tolerancia)
{
	float delta = obtenido - esperado;

	if (delta < 0)
		delta = -delta;

	/* NaN no es igual a nada, ni a si mismo: se compara por patron de bits */
	if (obtenido != obtenido || esperado != esperado)
	{
		unsigned int a, b;

		memcpy(&a, &obtenido, sizeof(a));
		memcpy(&b, &esperado, sizeof(b));

		if (a != b)
			diferencia(arch, linea, "%s = 0x%08X, se esperaba 0x%08X (comparacion por bits, hay NaN)",
					   expr, a, b);
		return;
	}

	if (delta > tolerancia)
		diferencia(arch, linea, "%s = %.9g, se esperaba %.9g (tolerancia %.9g)",
				   expr, (double) obtenido, (double) esperado, (double) tolerancia);
}

void dc_cmp_f64(const char * arch, int linea, const char * expr,
				double obtenido, double esperado, double tolerancia)
{
	double delta = obtenido - esperado;

	if (delta < 0)
		delta = -delta;

	if (obtenido != obtenido || esperado != esperado)
	{
		diferencia(arch, linea, "%s: hay NaN (obtenido %.17g, esperado %.17g)",
				   expr, obtenido, esperado);
		return;
	}

	if (delta > tolerancia)
		diferencia(arch, linea, "%s = %.17g, se esperaba %.17g (tolerancia %.17g)",
				   expr, obtenido, esperado, tolerancia);
}

void dc_cmp_bytes(const char * arch, int linea, const char * expr,
				  const void * obtenido, const void * esperado, size_t n)
{
	const unsigned char * a = (const unsigned char *) obtenido;
	const unsigned char * b = (const unsigned char *) esperado;
	size_t i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i])
		{
			diferencia(arch, linea, "%s: byte %u = 0x%02X, se esperaba 0x%02X",
					   expr, (unsigned int) i, a[i], b[i]);
			return;
		}
}

void dc_verificar(const char * arch, int linea, const char * expr, int cond)
{
	if (!cond)
		diferencia(arch, linea, "%s es falso", expr);
}

/* ------------------------------------------------------------------------ */

static int corrida_completa;

int dc_corrida_completa(void)
{
	return corrida_completa;
}

static int nombre_pedido(const char * nombre, int argc, char ** argv)
{
	int i;
	int hay_filtros = 0;

	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
			continue;

		hay_filtros = 1;

		if (strstr(nombre, argv[i]) != NULL)
			return 1;
	}

	return !hay_filtros;
}

int dc_correr(const dc_suite * const * suites, int cantidad,
			  int argc, char ** argv)
{
	int s, c;
	int total = 0, pasaron = 0, fallaron = 0, xfail = 0, xpass = 0, omitidos = 0;

	corrida_completa = nombre_pedido("", argc, argv);

	for (s = 0; s < cantidad; s++)
	{
		const dc_suite * suite = suites[s];
		int imprimio_titulo = 0;

		for (c = 0; c < suite->cantidad; c++)
		{
			const dc_caso * caso = &suite->casos[c];
			int fallas;

			if (!nombre_pedido(suite->nombre, argc, argv) &&
				!nombre_pedido(caso->nombre, argc, argv))
			{
				omitidos++;
				continue;
			}

			if (!imprimio_titulo)
			{
				printf("\n== %s ==\n", suite->nombre);
				imprimio_titulo = 1;
			}

			total++;

			/* Un XFAIL falla a proposito: sus diferencias no se imprimen,
			   solo se cuentan. */
			silencio = (caso->tipo == DC_XFAIL);

			dc_empezar_caso();
			caso->funcion();
			fallas = dc_fallas_del_caso();

			silencio = 0;

			if (caso->tipo == DC_XFAIL)
			{
				if (fallas > 0)
				{
					xfail++;
					printf("  xfail  %-44s %s\n", caso->nombre, caso->nota);
				}
				else
				{
					xpass++;
					printf("  XPASS  %-44s ya no falla: %s\n", caso->nombre, caso->nota);
					printf("         (arreglado? sacar el CASO_XFAIL y actualizar tests/README.md)\n");
				}
			}
			else
			{
				if (fallas > 0)
				{
					fallaron++;
					printf("  FALLA  %s (%d diferencia%s)\n",
						   caso->nombre, fallas, fallas == 1 ? "" : "s");
				}
				else
				{
					pasaron++;
					printf("  ok     %s\n", caso->nombre);
				}
			}
		}
	}

	printf("\n----------------------------------------------------------------\n");
	printf("%d casos: %d ok, %d fallas, %d xfail (desviaciones documentadas)",
		   total, pasaron, fallaron, xfail);
	if (xpass)
		printf(", %d XPASS", xpass);
	if (omitidos)
		printf(", %d omitidos por filtro", omitidos);
	printf("\n");

	if (fallaron == 0 && xpass == 0)
	{
		printf("RESULTADO: OK\n");
		return 0;
	}

	printf("RESULTADO: ERROR\n");
	return 1;
}
