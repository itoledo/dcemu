/****************************************************************************

	TA - ver ta.h.

*****************************************************************************/

#include <string.h>

#include "main.h"
#include "ta.h"
#include "graficos.h"

/*
	El parametro completo. Cuando mide 32 bytes apunta al bloque que llego;
	cuando mide 64, al buffer donde se juntaron las dos mitades.

	Lo declara ta.h y lo lee graficos.c, que es quien decodifica los campos.
*/
DWORD * ta_address_pointer;

/* El buffer de armado y la mitad que falta, si hay una a medias. */
static DWORD	ta_buffer[16];
static int		ta_faltan;		/* bytes que faltan; 0 = no hay nada a medias */

/*
	El tipo de vertice vigente: lo deja el ultimo encabezado y decide cuanto
	mide cada vertice que venga detras. Sin esto no hay forma de saber si un
	bloque de 32 bytes es un vertice entero o la primera mitad de uno.

	-1 mientras no haya llegado ningun encabezado.
*/
static int		ta_vertice_vigente = -1;

void ta_reiniciar(void)
{
	ta_faltan = 0;
}

void ta_clasificar(DWORD pcw, int * global, int * vertice)
{
	int textura	= (pcw >> 3) & 1;
	int col		= (pcw >> 4) & 3;
	int volumen	= (pcw >> 6) & 1;
	int uv16	= pcw & 1;
	int offset	= (pcw >> 2) & 1;
	int lista	= (pcw >> 24) & 7;
	int tipo	= (pcw >> 29) & 7;

	*global = TA_GLOBAL_POLY0;
	*vertice = 0;

	/*
		Un sprite no tiene tipo de color ni volumen: su encabezado mide 32 y su
		vertice 64. No esta implementado en graficos.c, pero clasificarlo igual
		es lo que evita que la segunda mitad de cada vertice se despache como
		si fuera un parametro.
	*/
	if (tipo == 5)
	{
		*global = TA_GLOBAL_SPRITE;
		*vertice = textura ? TA_VERTICE_SPRITE1 : TA_VERTICE_SPRITE0;
		return;
	}

	/*
		Listas 1 y 3: volumen modificador opaco y translucido. El encabezado es
		otro -- 32 bytes, sin nada de textura ni de color -- y cada vertice
		trae un triangulo entero en 64 bytes.

		Sin este caso el encabezado se clasificaba con los campos de un
		poligono, que en un volumen valen cero, y sus vertices de 64 bytes se
		leian como dos de 32.
	*/
	if (lista == 1 || lista == 3)
	{
		*global = TA_GLOBAL_VOLUMEN;
		*vertice = TA_VERTICE_VOLUMEN;
		return;
	}

	/*
		La tabla del manual, "Polygon Type" contra "Vertex Parameter Type".

		Lo unico que no se lee directo: en modo intensidad 2 (col == 3) el
		color de cara es el que dejo el poligono anterior en modo intensidad 1,
		asi que este encabezado no lo trae y mide 32 -- de ahi que caiga en
		POLY0 y no en POLY1.
	*/
	if (!textura)
	{
		if (!volumen)
		{
			switch (col)
			{
				case 0:	*global = TA_GLOBAL_POLY0;	*vertice = 0;	break;
				case 1:	*global = TA_GLOBAL_POLY0;	*vertice = 1;	break;
				case 2:	*global = TA_GLOBAL_POLY1;	*vertice = 2;	break;
				case 3:	*global = TA_GLOBAL_POLY0;	*vertice = 2;	break;
			}
		}
		else
		{
			switch (col)
			{
				case 0:	*global = TA_GLOBAL_POLY3;	*vertice = 9;	break;
				case 2:	*global = TA_GLOBAL_POLY4;	*vertice = 10;	break;
				case 3:	*global = TA_GLOBAL_POLY3;	*vertice = 10;	break;
			}
		}
	}
	else
	{
		if (!volumen)
		{
			switch (col)
			{
				case 0:	*global = TA_GLOBAL_POLY0;	*vertice = uv16 ? 4 : 3;	break;
				case 1:	*global = TA_GLOBAL_POLY0;	*vertice = uv16 ? 6 : 5;	break;
				case 2:	*global = offset ? TA_GLOBAL_POLY2 : TA_GLOBAL_POLY1;
						*vertice = uv16 ? 8 : 7;	break;
				case 3:	*global = TA_GLOBAL_POLY0;	*vertice = uv16 ? 8 : 7;	break;
			}
		}
		else
		{
			switch (col)
			{
				case 0:	*global = TA_GLOBAL_POLY3;	*vertice = uv16 ? 12 : 11;	break;
				case 2:	*global = TA_GLOBAL_POLY4;	*vertice = uv16 ? 14 : 13;	break;
				case 3:	*global = TA_GLOBAL_POLY3;	*vertice = uv16 ? 14 : 13;	break;
			}
		}
	}
}

int ta_tam_global(int global)
{
	/* Los que llevan color de cara son los unicos de 64. */
	switch (global)
	{
		case TA_GLOBAL_POLY1:
		case TA_GLOBAL_POLY2:
		case TA_GLOBAL_POLY4:
			return 64;

		default:
			return 32;
	}
}

int ta_tam_vertice(int vertice)
{
	/*
		De 64: los de color en punto flotante (5 y 6), los seis de dos
		volumenes con textura (11 a 14), los dos de sprite y el de volumen
		modificador. Los de dos volumenes sin textura -- 9 y 10 -- caben en 32,
		porque los dos colores son una palabra cada uno.
	*/
	switch (vertice)
	{
		case 5:  case 6:
		case 11: case 12: case 13: case 14:
		case TA_VERTICE_SPRITE0:
		case TA_VERTICE_SPRITE1:
		case TA_VERTICE_VOLUMEN:
			return 64;

		default:
			return 32;
	}
}

/* Despacha un parametro ya completo. */
static void ta_despachar(DWORD * parametro)
{
	DWORD pcw = *parametro;

	ta_address_pointer = parametro;

	switch ((pcw >> 29) & 0x7)
	{
		case 0:	taListEnd();		break;
		case 1:	doUserClip();		break;
		case 2:	objectListSet();	break;
		case 4:	taPolyModifier();	break;
		case 5:	taSprite();			break;
		case 7:	taVertexHandler();	break;
	}
}

void ta_procesar_bloque(void * bloque)
{
	DWORD	pcw;
	int		tipo, tam;
	int		global, vertice;

	/* La mitad que faltaba: se pega y sale el parametro entero. */
	if (ta_faltan)
	{
		memcpy(&ta_buffer[8], bloque, 32);
		ta_faltan = 0;
		ta_despachar(ta_buffer);
		return;
	}

	pcw = *(DWORD *) bloque;
	tipo = (pcw >> 29) & 0x7;

	if (tipo == 7)
	{
		/*
			Un vertice mide lo que diga el encabezado vigente. Si todavia no
			llego ninguno no hay como saberlo; se toma como de 32, que es lo
			que hacia antes y no empeora nada.
		*/
		tam = (ta_vertice_vigente < 0) ? 32 : ta_tam_vertice(ta_vertice_vigente);
	}
	else
	if (tipo == 4 || tipo == 5)
	{
		ta_clasificar(pcw, &global, &vertice);

		/* El encabezado deja vigente el tipo de vertice de lo que sigue. */
		ta_vertice_vigente = vertice;

		tam = ta_tam_global(global);
	}
	else
		tam = 32;	/* fin de lista, user clip y object list set */

	if (tam == 64)
	{
		memcpy(ta_buffer, bloque, 32);
		ta_faltan = 32;
		return;
	}

	ta_despachar((DWORD *) bloque);
}
