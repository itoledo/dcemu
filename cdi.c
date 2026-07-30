/****************************************************************************

	CDI - lector del header de una imagen DiscJuggler. Ver cdi.h.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cdi.h"

/* Las tres versiones que existen. Van en los ultimos 8 bytes del archivo,
   junto con el desplazamiento del header. */
#define CDI_V2			0x80000004u
#define CDI_V3			0x80000005u
#define CDI_V35			0x80000006u

static unsigned int leer_le32(const unsigned char * p)
{
	return  (unsigned int) p[0]        |
	       ((unsigned int) p[1] << 8)  |
	       ((unsigned int) p[2] << 16) |
	       ((unsigned int) p[3] << 24);
}

static long long tamano_de(FILE * fp)
{
#if defined(_MSC_VER)
	if (_fseeki64(fp, 0, SEEK_END) != 0)
		return -1;
	return _ftelli64(fp);
#else
	if (fseeko(fp, 0, SEEK_END) != 0)
		return -1;
	return (long long) ftello(fp);
#endif
}

static int posicionar(FILE * fp, long long donde)
{
#if defined(_MSC_VER)
	return _fseeki64(fp, donde, SEEK_SET);
#else
	return fseeko(fp, (off_t) donde, SEEK_SET);
#endif
}

/*
	Los campos de una pista estan a desplazamientos fijos **desde el final de su
	nombre de archivo**, con un solo salto condicional en el medio (los ocho
	bytes que agrego DiscJuggler 4). El header no dice donde empieza cada pista
	de forma que se pueda seguir sin ambiguedad -- el paso entre sesiones varia
	entre versiones --, asi que en vez de caminarlo se buscan los nombres y cada
	candidato se **valida**: una pista de verdad cumple que el largo total es el
	largo mas el pregap, y tiene modo y tamano de sector dentro de rango. Con
	eso, una posicion que no sea una pista se descarta sola.

	Devuelve 0 si no valida, o la posicion donde sigue el header si valida.
*/
static int leer_pista(const unsigned char * h, unsigned int tam, unsigned int i,
                      struct cdi_pista_t * pista, unsigned int * siguiente)
{
	unsigned int	largo_nombre = h[i];
	unsigned int	q;
	unsigned int	pregap, sectores, modo, lba, total, tam_sector;

	if (largo_nombre == 0)
		return 0;

	q = i + 1 + largo_nombre + 19;

	if (q + 4 > tam)
		return 0;

	/* DiscJuggler 4 mete ocho bytes mas, y lo delata este marcador. */
	q += (leer_le32(&h[q]) == 0x80000000u) ? 12 : 4;
	q += 2;

	if (q + 58 > tam)
		return 0;

	pregap		= leer_le32(&h[q]);
	sectores	= leer_le32(&h[q + 4]);
	modo		= leer_le32(&h[q + 14]);
	lba			= leer_le32(&h[q + 30]);
	total		= leer_le32(&h[q + 34]);
	tam_sector	= leer_le32(&h[q + 54]);

	/* La validacion. Sin esto habria que confiar en haber caminado bien todo el
	   header, que es justo lo que no se puede. */
	if (total != sectores + pregap || modo > 2 || tam_sector > 2 || sectores == 0)
		return 0;

	pista->lba			= lba;
	pista->sectores		= sectores;
	pista->modo			= modo;
	pista->sector_crudo	= (tam_sector == 0) ? 2048 : ((tam_sector == 1) ? 2336 : 2352);

	/*
		Donde estan los 2048 bytes de usuario dentro del sector crudo:

		  2048  ya son los datos
		  2336  modo 2: 8 bytes de subheader delante
		  2352  sector completo: 16 de cabecera en modo 1, 24 en modo 2
	*/
	if (pista->sector_crudo == 2048)
		pista->desplazamiento = 0;
	else
	if (pista->sector_crudo == 2336)
		pista->desplazamiento = 8;
	else
		pista->desplazamiento = (modo == 2) ? 24 : 16;

	/* El offset lo pone el llamador, que es quien lleva la cuenta de lo que
	   ocupan las pistas anteriores. Aca queda el tamano con pregap. */
	pista->offset = (long long) pregap * pista->sector_crudo;

	*siguiente = q + 58;

	return (int) total;
}

int cdi_abrir(const char * ruta, struct cdi_t * dest)
{
	FILE *			fp;
	long long		tamano, pos_header, ocupado = 0;
	unsigned char *	h;
	unsigned char	cola[8];
	unsigned int	version, desplazamiento, i;

	memset(dest, 0, sizeof(*dest));

	fp = fopen(ruta, "rb");

	if (fp == NULL)
	{
		fprintf(stderr, "cdi_abrir: no se pudo abrir %s\n", ruta);
		return 1;
	}

	tamano = tamano_de(fp);

	if (tamano < 8 || posicionar(fp, tamano - 8) != 0 ||
	    fread(cola, 1, 8, fp) != 8)
	{
		fclose(fp);
		return 1;
	}

	version			= leer_le32(&cola[0]);
	desplazamiento	= leer_le32(&cola[4]);

	if ((version != CDI_V2 && version != CDI_V3 && version != CDI_V35) ||
	    desplazamiento == 0 || (long long) desplazamiento > tamano)
	{
		fprintf(stderr, "cdi_abrir: %s no parece un .cdi (version %08x)\n",
			ruta, version);
		fclose(fp);
		return 1;
	}

	/* En 3.5 el desplazamiento se cuenta desde el final; en 2.0 y 3.0 es la
	   posicion directa. */
	pos_header = (version == CDI_V35) ? (tamano - desplazamiento) : desplazamiento;

	h = (unsigned char *) malloc(desplazamiento);

	if (h == NULL || posicionar(fp, pos_header) != 0 ||
	    fread(h, 1, desplazamiento, fp) != desplazamiento)
	{
		free(h);
		fclose(fp);
		return 1;
	}

	fclose(fp);

	for (i = 0; i + 120 < desplazamiento && dest->n < CDI_PISTAS_MAX; )
	{
		struct cdi_pista_t	pista;
		unsigned int		siguiente = 0;
		int					total = leer_pista(h, desplazamiento, i, &pista, &siguiente);

		if (total <= 0)
		{
			i++;
			continue;
		}

		/* leer_pista() dejo en offset lo que ocupa el pregap; lo que va delante
		   es todo lo que ocupan las pistas anteriores. */
		pista.offset += ocupado;
		ocupado += (long long) total * pista.sector_crudo;

		dest->pistas[dest->n++] = pista;
		i = siguiente;
	}

	free(h);

	if (dest->n == 0)
	{
		fprintf(stderr, "cdi_abrir: no se encontro ninguna pista en %s\n", ruta);
		return 1;
	}

	/*
		La comprobacion que cierra el circulo: los datos de las pistas ocupan
		exactamente desde el byte 0 hasta donde empieza el header. Si no cuadra,
		alguna pista se leyo mal o falta alguna, y mas vale decirlo que montar
		la imagen corrida.
	*/
	if (ocupado != pos_header)
		fprintf(stderr, "cdi_abrir: aviso, las pistas ocupan %lld bytes y el"
			" header empieza en %lld\n", ocupado, pos_header);

	return 0;
}

int cdi_pista_de_datos(const struct cdi_t * cdi)
{
	int i, mejor = -1;

	for (i = 0; i < cdi->n; i++)
		if (cdi->pistas[i].modo != 0)
			if (mejor < 0 || cdi->pistas[i].lba > cdi->pistas[mejor].lba)
				mejor = i;

	return mejor;
}
