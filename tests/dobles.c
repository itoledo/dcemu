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

/* --- traza.c ------------------------------------------------------------- */

/* gdrom.c reporta los paquetes por stderr cuando la traza esta activa.
   traza.c no se puede enlazar aca -- desensambla, y eso arrastra debug.c con
   SDL --, y en las pruebas la traza estorbaria de todos modos. */
int traza_activa = 0;

/* El gancho de mem.h llama a esto cuando WATCHPOINT esta encendido en
   options.h. traza.c no se enlaza aca, asi que el doble lo calla: encender el
   watchpoint no tiene que romper la compilacion de las pruebas. */
#ifdef WATCHPOINT
void watchpoint_escritura(unsigned long direccion, size_t tam)
{
	(void) direccion;
	(void) tam;
}
#endif

/* --- intc.c -------------------------------------------------------------- */

bool inside_int = false;

DWORD	intc_queuemask = 0;
DWORD	intc_queuemask_ext = 0;

/* La lectora avisa el fin de cada comando; las pruebas del GD-ROM miran estos
   contadores para verificar que interrumpio cuando correspondia. */
int dobles_int_normal;
int dobles_int_ext;
DWORD dobles_ultima_int_normal;
DWORD dobles_ultima_int_ext;

void intc_add(DWORD inttoadd, int cnt)
{
	(void) cnt;

	dobles_int_normal++;
	dobles_ultima_int_normal = inttoadd;
	intc_queuemask |= inttoadd;
}

void intc_add_ext(DWORD inttoadd)
{
	dobles_int_ext++;
	dobles_ultima_int_ext = inttoadd;
	intc_queuemask_ext |= inttoadd;
}

void intc_remove_ext(DWORD int2remove)
{
	intc_queuemask_ext &= ~int2remove;
}

/* --- iso.c --------------------------------------------------------------- */

#define DOBLE_LBA		11700
#define DOBLE_MODO		2
#define DOBLE_SECTORES	1000

/* Las pruebas del GD-ROM cambian esto para simular la bandeja vacia. */
int dobles_hay_disco = 1;

int iso_get_lba()
{
	return DOBLE_LBA;
}

int iso_get_mode()
{
	return DOBLE_MODO;
}

int iso_hay_disco()
{
	return dobles_hay_disco;
}

int iso_num_sectores()
{
	return dobles_hay_disco ? DOBLE_SECTORES : 0;
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

	dobles_int_normal			= 0;
	dobles_int_ext				= 0;
	dobles_ultima_int_normal	= 0;
	dobles_ultima_int_ext		= 0;
	dobles_hay_disco			= 1;

	intc_queuemask		= 0;
	intc_queuemask_ext	= 0;
}
