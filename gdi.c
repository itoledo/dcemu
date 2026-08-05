/*
	gdi.c -- lector del indice de una imagen GD-ROM (.gdi). Ver gdi.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lnxdefs.h"
#include "gdi.h"

/*
	La ruta de una pista es relativa al directorio del .gdi, no al directorio de
	trabajo. Correr el emulador desde la raiz del repo con la imagen en roms/ es
	el caso normal, asi que resolverla mal no es un detalle.
*/
static void gdi_ruta_pista(const char * gdi, const char * archivo,
                           char * dest, size_t n)
{
	const char * barra = NULL;
	const char * p;
	size_t       largo;

	for (p = gdi; *p; p++)
		if (*p == '/' || *p == '\\')
			barra = p;

	if (barra == NULL)
	{
		/* El .gdi esta en el directorio de trabajo. */
		snprintf(dest, n, "%s", archivo);
		return;
	}

	largo = (size_t) (barra - gdi) + 1;

	if (largo >= n)
		largo = n - 1;

	memcpy(dest, gdi, largo);
	dest[largo] = '\0';

	strncat(dest, archivo, n - strlen(dest) - 1);
}

/*
	El nombre del archivo puede venir entre comillas si lleva espacios, que es lo
	que hacen los rippers cuando el titulo del juego esta en el nombre de pista.
	Devuelve donde sigue la linea, o NULL si no hay nombre.
*/
static const char * gdi_nombre(const char * s, char * dest, size_t n)
{
	size_t i = 0;

	while (*s == ' ' || *s == '\t')
		s++;

	if (*s == '"')
	{
		s++;

		while (*s && *s != '"' && i + 1 < n)
			dest[i++] = *s++;

		if (*s == '"')
			s++;
	}
	else
	{
		while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n'
		       && i + 1 < n)
			dest[i++] = *s++;
	}

	dest[i] = '\0';

	return i ? s : NULL;
}

/*
	Las rutas de las pistas de la ultima imagen abierta.

	Es estado de modulo y no parte de `struct cdi_t` a proposito: esa estructura
	la comparten los dos formatos y un .cdi no tiene un archivo por pista. Vive
	lo que vive la imagen montada, que es toda la corrida.
*/
static char gdi_rutas[CDI_PISTAS_MAX][1024];
static int  gdi_n_rutas = 0;

const char * gdi_ruta_de(int i)
{
	return (i >= 0 && i < gdi_n_rutas) ? gdi_rutas[i] : NULL;
}

static long long gdi_tamano(const char * ruta)
{
	FILE *    f = fopen(ruta, "rb");
	long long n;

	if (f == NULL)
		return -1;

	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return -1;
	}

	n = (long long) ftell(f);
	fclose(f);

	return n;
}

/*
	Modo 1 o modo 2 de una pista de datos con sectores de 2352.

	El .gdi no lo dice: su campo `tipo` son los bits de control del subcanal Q --
	4 es "datos" y nada mas --. El sector si lo dice, en el byte 15: doce de
	sincronismo, tres de direccion en MSF y despues el modo.

	Si no se puede leer se supone modo 1, que es lo que traen las pistas de alta
	densidad de los GD-ROM que se ven en la practica.
*/
static unsigned int gdi_modo_de_pista(const char * ruta, long long offset)
{
	unsigned char cab[16];
	FILE *        f = fopen(ruta, "rb");
	unsigned int  modo = 1;

	if (f == NULL)
		return modo;

	if (fseek(f, (long) offset, SEEK_SET) == 0
	 && fread(cab, 1, sizeof(cab), f) == sizeof(cab))
	{
		/* Con el sincronismo en su lugar el byte 15 es de fiar. */
		static const unsigned char sync[12] =
			{ 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

		if (memcmp(cab, sync, sizeof(sync)) == 0
		 && (cab[15] == 1 || cab[15] == 2))
			modo = cab[15];
	}

	fclose(f);

	return modo;
}

int gdi_abrir(const char * ruta, struct cdi_t * dest,
              char * ruta_datos, size_t ruta_datos_n, int * cual)
{
	FILE * f = fopen(ruta, "r");
	char   linea[1024];
	int    n_pistas = 0;
	int    i;
	int    mejor = -1, respaldo = -1;
	char   ruta_respaldo[1024];

	if (f == NULL)
	{
		fprintf(stderr, "gdi_abrir: no se pudo abrir %s\n", ruta);
		return 1;
	}

	memset(dest, 0, sizeof(*dest));
	gdi_n_rutas = 0;

	if (fgets(linea, sizeof(linea), f) == NULL
	 || (n_pistas = atoi(linea)) <= 0 || n_pistas > CDI_PISTAS_MAX)
	{
		fprintf(stderr, "gdi_abrir: %s no empieza con un numero de pistas "
			"razonable (%d)\n", ruta, n_pistas);
		fclose(f);
		return 1;
	}

	for (i = 0; i < n_pistas; i++)
	{
		struct cdi_pista_t * p = &dest->pistas[dest->n];
		char                 archivo[512];
		char                 completa[1024];
		const char *         resto;
		int                  num, lba, tipo, tam;
		long long            tamano, offset;

		if (fgets(linea, sizeof(linea), f) == NULL)
		{
			fprintf(stderr, "gdi_abrir: %s dice %d pistas y trae %d\n",
				ruta, n_pistas, i);
			break;
		}

		/* Los cuatro numeros de adelante, despues el nombre --que puede llevar
		   comillas-- y por ultimo el offset. */
		{
			int leidos = 0;

			if (sscanf(linea, "%d %d %d %d%n", &num, &lba, &tipo, &tam,
			           &leidos) != 4)
			{
				fprintf(stderr, "gdi_abrir: no entiendo la linea %d: %s",
					i + 2, linea);
				continue;
			}

			resto = gdi_nombre(linea + leidos, archivo, sizeof(archivo));
		}

		if (resto == NULL)
		{
			fprintf(stderr, "gdi_abrir: la pista %d no nombra archivo\n", num);
			continue;
		}

		offset = atoll(resto);

		if (tam != 2048 && tam != 2352)
		{
			fprintf(stderr, "gdi_abrir: pista %d con sectores de %d bytes, que "
				"no es 2048 ni 2352\n", num, tam);
			continue;
		}

		gdi_ruta_pista(ruta, archivo, completa, sizeof(completa));

		snprintf(gdi_rutas[dest->n], sizeof(gdi_rutas[0]), "%s", completa);
		gdi_n_rutas = dest->n + 1;

		tamano = gdi_tamano(completa);

		if (tamano < 0)
		{
			fprintf(stderr, "gdi_abrir: falta el archivo de la pista %d (%s)\n",
				num, completa);
			fclose(f);
			return 1;
		}

		p->lba			 = (unsigned int) lba;
		p->sector_crudo	 = (unsigned int) tam;
		p->offset		 = offset;
		p->sectores		 = (unsigned int) ((tamano - offset) / tam);

		/* `tipo` son los bits de control: 4 dice datos, 0 dice audio. El modo de
		   verdad --1 o 2-- hay que sacarlo del sector. */
		if ((tipo & 4) == 0)
		{
			p->modo			  = 0;
			p->desplazamiento = 0;
		}
		else if (tam == 2048)
		{
			p->modo			  = 1;
			p->desplazamiento = 0;
		}
		else
		{
			p->modo			  = gdi_modo_de_pista(completa, offset);
			p->desplazamiento = (p->modo == 2) ? 24 : 16;
		}

		/*
			**La primera pista de datos del area de alta densidad**, que es la
			que trae el IP.BIN y el sistema de archivos. Ver gdi.h: no es la de
			LBA mas alto, que es la regla de los .cdi y elegia mal en cuanto la
			imagen tiene mas de una pista de datos arriba.
		*/
		if (p->modo != 0 && p->lba >= GDI_LBA_ALTA_DENSIDAD
		 && (mejor < 0 || p->lba < dest->pistas[mejor].lba))
		{
			mejor = dest->n;
			snprintf(ruta_datos, ruta_datos_n, "%s", completa);
		}

		/* Y por si no hay ninguna ahi arriba: la de datos de mas abajo, que es
		   lo unico que se puede intentar en un .gdi que no describa un GD-ROM. */
		if (p->modo != 0 && (respaldo < 0 || p->lba < dest->pistas[respaldo].lba))
		{
			respaldo = dest->n;
			snprintf(ruta_respaldo, sizeof(ruta_respaldo), "%s", completa);
		}

		dest->n++;
	}

	fclose(f);

	if (mejor < 0 && respaldo >= 0)
	{
		fprintf(stderr, "gdi_abrir: %s no tiene pistas de datos en el area de "
			"alta densidad; se intenta con la pista %d, en el LBA %u\n",
			ruta, respaldo + 1, dest->pistas[respaldo].lba);

		mejor = respaldo;
		snprintf(ruta_datos, ruta_datos_n, "%s", ruta_respaldo);
	}

	if (mejor < 0)
	{
		fprintf(stderr, "gdi_abrir: %s no tiene pistas de datos\n", ruta);
		return 1;
	}

	*cual = mejor;

	return 0;
}
