/****************************************************************************

	DOBLES - reemplazos de graficos.c, iso.c e intc.c para las pruebas

	Los handlers de opcodes referencian tres cosas que viven fuera del nucleo:

	  - syscontrol.c: PREF despacha a las funciones del tile accelerator en
		graficos.c, y RTE apaga inside_int (intc.c).
	  - dcopcodes.c: el hack de la BIOS lee sectores por iso.c.

	Los dobles cuentan las llamadas para que las pruebas puedan afirmar que
	PREF despacho al tipo de parametro correcto, y devuelven datos
	deterministas en vez de tocar una imagen de disco.

*****************************************************************************/

#include <string.h>

#include "main.h"
#include "arnes.h"

/* --- graficos.c ---------------------------------------------------------- */

DWORD * ta_address_pointer;

int dobles_ta_list_end;
int dobles_user_clip;
int dobles_object_list_set;
int dobles_poly_modifier;
int dobles_vertex_handler;

void taListEnd()		{ dobles_ta_list_end++; }
void doUserClip()		{ dobles_user_clip++; }
void objectListSet()	{ dobles_object_list_set++; }
void taPolyModifier()	{ dobles_poly_modifier++; }
void taVertexHandler()	{ dobles_vertex_handler++; }

/* --- intc.c -------------------------------------------------------------- */

bool inside_int = false;

/* --- iso.c --------------------------------------------------------------- */

#define DOBLE_LBA	11700
#define DOBLE_MODO	2

int iso_get_lba()
{
	return DOBLE_LBA;
}

int iso_get_mode()
{
	return DOBLE_MODO;
}

/* Patron reproducible: el byte i del sector s vale (s * 7 + i) & 0xFF. Deja
   verificar en la prueba que se leyo el sector pedido y no otro. */
int iso_read_sector(char * target, int secstart, int secnum)
{
	int s, i;

	for (s = 0; s < secnum; s++)
		for (i = 0; i < 2048; i++)
			target[s * 2048 + i] = (char) (((secstart + s) * 7 + i) & 0xFF);

	return 0;
}

void dobles_reset(void)
{
	dobles_ta_list_end		= 0;
	dobles_user_clip		= 0;
	dobles_object_list_set	= 0;
	dobles_poly_modifier	= 0;
	dobles_vertex_handler	= 0;

	inside_int = true;	/* RTE debe dejarlo en false */

	ta_address_pointer = NULL;
}
