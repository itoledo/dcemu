/****************************************************************************

	Pruebas de ta.c -- formato de los parametros del tile accelerator

	Dos cosas que no se ven desde afuera y que costaron demos enteras:

	  - **La clasificacion.** De la palabra de control salen dos cosas: que
		tipo de parametro global es el encabezado y que tipo de vertice deja
		vigente. La tabla del manual cruza cuatro campos -- textura, tipo de
		color, volumen y ancho de las UV -- y equivocarse en una casilla
		significa leer el vertice desde el desplazamiento equivocado.

	  - **El rearmado de los de 64 bytes.** Llegan en dos bloques de 32, uno
		por store queue. Si cada bloque se despacha por separado, la segunda
		mitad se lee como palabra de control; cuando su primera palabra es un
		float que vale 0.0, el tipo sale 0 -- fin de lista -- y se cierra una
		lista que el guest nunca cerro. Las pruebas de aca comprueban que un
		parametro de 64 bytes despache **una** vez y no dos, y que lo haga con
		las dos mitades juntas.

	Los dobles de tests/dobles.c cuentan los despachos por tipo.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "suites.h"
#include "ta.h"

/* ------------------------------------------------------------ auxiliares */

/*
	Arma una palabra de control. Los campos van donde los pone el manual:
	bit 0 UV de 16 bits, 2 offset, 3 textura, 4-5 tipo de color, 6 volumen,
	24-26 tipo de lista, 29-31 tipo de parametro.
*/
static DWORD pcw(int tipo, int lista, int textura, int col, int volumen,
				 int uv16, int offset)
{
	return ((DWORD) tipo << 29) | ((DWORD) lista << 24) |
		   ((DWORD) volumen << 6) | ((DWORD) col << 4) |
		   ((DWORD) textura << 3) | ((DWORD) offset << 2) | (DWORD) uv16;
}

/* Un encabezado de poligono con esos campos, y detras un vertice. */
static void clasificar(int textura, int col, int volumen, int uv16, int offset,
					   int * global, int * vertice)
{
	ta_clasificar(pcw(4, 0, textura, col, volumen, uv16, offset),
				  global, vertice);
}

/* ------------------------------------------------------- clasificacion */

static void sin_textura_da_los_tipos_del_manual(void)
{
	int g, v;

	clasificar(0, 0, 0, 0, 0, &g, &v);	/* color empaquetado */
	ESPERAR_I32(g, TA_GLOBAL_POLY0);
	ESPERAR_I32(v, 0);

	clasificar(0, 1, 0, 0, 0, &g, &v);	/* color en punto flotante */
	ESPERAR_I32(g, TA_GLOBAL_POLY0);
	ESPERAR_I32(v, 1);

	clasificar(0, 2, 0, 0, 0, &g, &v);	/* intensidad modo 1: trae color de cara */
	ESPERAR_I32(g, TA_GLOBAL_POLY1);
	ESPERAR_I32(v, 2);

	/* Intensidad modo 2: usa el color de cara del poligono anterior, asi que
	   su encabezado NO lo trae y por eso mide 32 y no 64. */
	clasificar(0, 3, 0, 0, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY0);
	ESPERAR_I32(v, 2);
}

static void con_textura_separa_las_uv_de_16_bits(void)
{
	int g, v;

	clasificar(1, 0, 0, 0, 0, &g, &v);
	ESPERAR_I32(v, 3);
	clasificar(1, 0, 0, 1, 0, &g, &v);
	ESPERAR_I32(v, 4);

	clasificar(1, 1, 0, 0, 0, &g, &v);
	ESPERAR_I32(v, 5);
	clasificar(1, 1, 0, 1, 0, &g, &v);
	ESPERAR_I32(v, 6);

	/* El bit de offset elige entre POLY1 y POLY2, no cambia el vertice. */
	clasificar(1, 2, 0, 0, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY1);
	ESPERAR_I32(v, 7);
	clasificar(1, 2, 0, 0, 1, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY2);
	ESPERAR_I32(v, 7);
	clasificar(1, 2, 0, 1, 1, &g, &v);
	ESPERAR_I32(v, 8);
}

static void el_bit_de_volumen_da_los_tipos_de_dos_juegos(void)
{
	int g, v;

	clasificar(0, 0, 1, 0, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY3);
	ESPERAR_I32(v, 9);

	clasificar(0, 2, 1, 0, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY4);
	ESPERAR_I32(v, 10);

	clasificar(1, 0, 1, 0, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY3);
	ESPERAR_I32(v, 11);
	clasificar(1, 0, 1, 1, 0, &g, &v);
	ESPERAR_I32(v, 12);

	clasificar(1, 2, 1, 0, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY4);
	ESPERAR_I32(v, 13);
	clasificar(1, 3, 1, 1, 0, &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_POLY3);
	ESPERAR_I32(v, 14);
}

/*
	Las listas 1 y 3 son de volumen modificador. Su encabezado no tiene campos
	de textura ni de color: interpretarlo como un poligono dejaba parametros
	globales sacados de palabras reservadas, y esos sobreviven hasta el
	encabezado siguiente.
*/
static void las_listas_de_volumen_tienen_su_propio_encabezado(void)
{
	int g, v;

	ta_clasificar(pcw(4, 1, 0, 0, 0, 0, 0), &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_VOLUMEN);
	ESPERAR_I32(v, TA_VERTICE_VOLUMEN);

	ta_clasificar(pcw(4, 3, 0, 0, 0, 0, 0), &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_VOLUMEN);
	ESPERAR_I32(v, TA_VERTICE_VOLUMEN);
}

static void el_sprite_se_reconoce_aunque_no_se_dibuje(void)
{
	int g, v;

	ta_clasificar(pcw(5, 0, 0, 0, 0, 0, 0), &g, &v);
	ESPERAR_I32(g, TA_GLOBAL_SPRITE);
	ESPERAR_I32(v, TA_VERTICE_SPRITE0);

	ta_clasificar(pcw(5, 0, 1, 0, 0, 0, 0), &g, &v);
	ESPERAR_I32(v, TA_VERTICE_SPRITE1);
}

/* --------------------------------------------------------------- tamanos */

static void los_tamanos_son_los_del_manual(void)
{
	/* Solo llevan 64 los que traen DOS colores: POLY2 (cara y offset) y
	   POLY4 (uno por volumen). El POLY1 lleva uno y cabe en 32, en las
	   palabras 4-7 -- lo ejercita el boot ROM, no los demos de KOS. */
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_POLY0), 32);
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_POLY1), 32);
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_POLY2), 64);
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_POLY3), 32);
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_POLY4), 64);
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_VOLUMEN), 32);
	ESPERAR_I32(ta_tam_global(TA_GLOBAL_SPRITE), 32);

	ESPERAR_I32(ta_tam_vertice(0), 32);
	ESPERAR_I32(ta_tam_vertice(3), 32);
	ESPERAR_I32(ta_tam_vertice(5), 64);
	ESPERAR_I32(ta_tam_vertice(6), 64);
	ESPERAR_I32(ta_tam_vertice(7), 32);

	/* Los de dos volumenes sin textura caben en 32: los dos colores son una
	   palabra cada uno. Con textura ya no, porque se duplican tambien las UV. */
	ESPERAR_I32(ta_tam_vertice(9), 32);
	ESPERAR_I32(ta_tam_vertice(10), 32);
	ESPERAR_I32(ta_tam_vertice(11), 64);
	ESPERAR_I32(ta_tam_vertice(14), 64);

	ESPERAR_I32(ta_tam_vertice(TA_VERTICE_VOLUMEN), 64);
}

/* ------------------------------------------------------------- rearmado */

/* Manda un bloque de 32 bytes cuya primera palabra es `w0`. */
static void bloque(DWORD w0, DWORD relleno)
{
	DWORD b[8];
	int i;

	b[0] = w0;
	for (i = 1; i < 8; i++)
		b[i] = relleno + (DWORD) i;

	ta_procesar_bloque(b);
}

static void un_parametro_de_32_despacha_al_llegar(void)
{
	dobles_reset();

	bloque(pcw(4, 0, 0, 0, 0, 0, 0), 0);	/* encabezado POLY0: 32 bytes */

	ESPERAR_I32(dobles_poly_modifier, 1);

	bloque(pcw(7, 0, 0, 0, 0, 0, 0), 0);	/* vertice tipo 0: 32 bytes */

	ESPERAR_I32(dobles_vertex_handler, 1);
}

/*
	El caso que importa: un encabezado de 64 bytes no debe despachar hasta que
	llegue su segunda mitad, y esa segunda mitad no debe despacharse sola.
*/
static void un_encabezado_de_64_espera_la_segunda_mitad(void)
{
	dobles_reset();

	/* Intensidad modo 1 con textura y offset: POLY2, 64 bytes. */
	bloque(pcw(4, 0, 1, 2, 0, 0, 1), 0);

	ESPERAR_I32(dobles_poly_modifier, 0);	/* todavia no */

	bloque(0x3F800000, 0x100);				/* la segunda mitad */

	ESPERAR_I32(dobles_poly_modifier, 1);	/* ahora si, una sola vez */
	ESPERAR_I32(dobles_user_clip, 0);		/* y no como recorte de usuario */
}

/*
	El sintoma que dio origen a todo esto: cuando la primera palabra de la
	segunda mitad vale 0.0, su "tipo de parametro" sale 0, que es fin de lista.
	Sin rearmado se cerraba una lista que el guest no cerro.
*/
static void la_segunda_mitad_en_cero_no_cierra_la_lista(void)
{
	dobles_reset();

	bloque(pcw(4, 0, 1, 1, 0, 0, 0), 0);	/* POLY0 + vertice tipo 5 (64) */
	ESPERAR_I32(dobles_poly_modifier, 1);

	bloque(pcw(7, 0, 0, 0, 0, 0, 0), 0);	/* primera mitad del vertice */
	ESPERAR_I32(dobles_vertex_handler, 0);

	bloque(0x00000000, 0);					/* segunda mitad, toda en cero */

	ESPERAR_I32(dobles_vertex_handler, 1);
	ESPERAR_I32(dobles_ta_list_end, 0);		/* lo que fallaba antes */
}

/* Las dos mitades tienen que quedar contiguas para el que las lee. */
static void las_dos_mitades_quedan_juntas(void)
{
	DWORD primera[8], segunda[8];
	int i;

	dobles_reset();

	bloque(pcw(4, 0, 1, 1, 0, 0, 0), 0);	/* deja vigente el vertice tipo 5 */

	primera[0] = pcw(7, 0, 0, 0, 0, 0, 0);
	for (i = 1; i < 8; i++)
		primera[i] = 0xA0000000 + (DWORD) i;

	for (i = 0; i < 8; i++)
		segunda[i] = 0xB0000000 + (DWORD) i;

	ta_procesar_bloque(primera);
	ta_procesar_bloque(segunda);

	ESPERAR_I32(dobles_vertex_handler, 1);

	/* ta_address_pointer quedo apuntando al parametro entero. */
	ESPERAR_U32(ta_address_pointer[1], 0xA0000001);
	ESPERAR_U32(ta_address_pointer[7], 0xA0000007);
	ESPERAR_U32(ta_address_pointer[8], 0xB0000000);
	ESPERAR_U32(ta_address_pointer[15], 0xB0000007);
}

/*
	Si el guest abandona un parametro por la mitad, la mitad que quedo no debe
	pegarse con el primer bloque de la escena siguiente. ta_reiniciar() se
	llama al empezar cada escena.
*/
static void reiniciar_olvida_la_mitad_pendiente(void)
{
	dobles_reset();

	bloque(pcw(4, 0, 1, 2, 0, 0, 1), 0);	/* POLY2: 64 bytes, queda a medias */
	ESPERAR_I32(dobles_poly_modifier, 0);

	ta_reiniciar();

	/* Sin el reinicio, este bloque seria la segunda mitad del anterior. */
	bloque(pcw(0, 0, 0, 0, 0, 0, 0), 0);	/* fin de lista */

	ESPERAR_I32(dobles_ta_list_end, 1);
	ESPERAR_I32(dobles_poly_modifier, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(sin_textura_da_los_tipos_del_manual),
	CASO(con_textura_separa_las_uv_de_16_bits),
	CASO(el_bit_de_volumen_da_los_tipos_de_dos_juegos),
	CASO(las_listas_de_volumen_tienen_su_propio_encabezado),
	CASO(el_sprite_se_reconoce_aunque_no_se_dibuje),
	CASO(los_tamanos_son_los_del_manual),
	CASO(un_parametro_de_32_despacha_al_llegar),
	CASO(un_encabezado_de_64_espera_la_segunda_mitad),
	CASO(la_segunda_mitad_en_cero_no_cierra_la_lista),
	CASO(las_dos_mitades_quedan_juntas),
	CASO(reiniciar_olvida_la_mitad_pendiente),
};

const dc_suite suite_ta = DEFINIR_SUITE("ta", casos);
