/*
	acumulador.c -- el buffer de acumulacion secundario del TSP, y una prueba
	que no necesita imagen de referencia.

	El chip lleva DOS buffers de acumulacion por pixel. Dos bits de la palabra
	TSP eligen cual usa cada tira: el 24 ("blend to the 2nd accumulation
	buffer") como destino de la mezcla y el 25 ("blend from") como origen.
	Existe --DevBox 3.4.6.1-- para tratar el resultado de superponer varios
	poligonos como si fuera uno solo: se acumula el grupo en el secundario y
	despues se compone de una vez sobre el primario.

	dcemu los registraba desde siempre y no los consultaba nunca. Se podia
	porque **no hay contenido que los use**: sobre 1,16 millones de tiras de
	Crazy Taxi en juego, las doce demos de control y los nueve juegos del arbol
	no hay una sola tira que seleccione el secundario. Por eso existe esto.

	**La verificacion compara los dos sabores entre si, no contra una imagen.**
	Los dos dibujan los mismos dos cuadrados superpuestos con mezcla ADITIVA
	sobre negro:

	  - "directo" los suma al buffer primario, como cualquier poligono;
	  - "acumulado" los suma al SECUNDARIO y despues compone el secundario
	    entero sobre el primario con un cuadrilatero de pantalla completa que
	    lleva el bit 25 puesto y suma (ONE, ONE).

	Sumar sobre negro es asociativo, asi que las dos capturas tienen que salir
	byte a byte iguales. Y las tres formas de equivocarse dan imagenes
	distintas, que es lo que hace que la prueba sirva:

	  - si se ignora el bit 24, los cuadrados van al primario Y ADEMAS se
	    compone encima -> mas brillante;
	  - si se ignora el bit 25, el cuadrilatero de composicion aporta su propio
	    color (verde oscuro a proposito) en vez del acumulado -> toda la
	    pantalla se tine;
	  - si el secundario no se limpia o es el mismo que el primario, sale doble.

	Compilar los dos:

	    make                   -> acum-directo.elf
	    make ACUMULAR=1        -> acum-acumulado.elf
*/

#include <stdio.h>

#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

/* Los dos cuadrados, fijos: la prueba tiene que dar lo mismo en cada corrida. */
#define AX0	120.0f
#define AY0	100.0f
#define AX1	400.0f
#define AY1	300.0f

#define BX0	240.0f
#define BY0	180.0f
#define BX1	520.0f
#define BY1	380.0f

static pvr_poly_hdr_t hdr_a, hdr_b, hdr_comp;

/* Un cuadrilatero de pantalla, en el orden de tira que espera el TA. */
static void quad(float x0, float y0, float x1, float y1, float z, uint32_t argb)
{
	pvr_vertex_t *	v;
	int				i;
	const float		x[4] = { x0, x0, x1, x1 };
	const float		y[4] = { y1, y0, y1, y0 };

	for(i = 0; i < 4; i++)
	{
		v = pvr_dr_target();
		*v = (pvr_vertex_t){
			.flags = (i == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.x = x[i], .y = y[i], .z = z,
			.u = 0.0f, .v = 0.0f,
			.argb = argb, .oargb = 0,
		};
		pvr_dr_commit(v);
	}
}

static void armar(void)
{
	pvr_poly_cxt_t cxt;

	/* Los dos cuadrados: aditivos y sin textura. */
	pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
	cxt.blend.src = PVR_BLEND_ONE;
	cxt.blend.dst = PVR_BLEND_ONE;
	cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
	pvr_poly_compile(&hdr_a, &cxt);
	pvr_poly_compile(&hdr_b, &cxt);

#ifdef ACUMULAR
	/* Al secundario, no al primario. */
	hdr_a.m2.blend_dst_acc2 = true;
	hdr_b.m2.blend_dst_acc2 = true;

	/*
		Y el cuadrilatero que compone: su ORIGEN es el secundario (bit 25), su
		destino el primario, y suma. El color del vertice es un verde oscuro
		bien visible **a proposito**: si dcemu ignorara el bit 25 usaria ese
		color y la pantalla entera se tenria, que es justo lo que hay que poder
		distinguir de "no compuso nada".
	*/
	pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
	cxt.blend.src = PVR_BLEND_ONE;
	cxt.blend.dst = PVR_BLEND_ONE;
	cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
	pvr_poly_compile(&hdr_comp, &cxt);

	hdr_comp.m2.blend_src_acc2 = true;
#endif
}

static void cuadro(void)
{
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_TR_POLY);

	pvr_dr_init(NULL);

	pvr_prim(&hdr_a, sizeof(hdr_a));
	quad(AX0, AY0, AX1, AY1, 2.0f, 0xFF4060A0);

	pvr_prim(&hdr_b, sizeof(hdr_b));
	quad(BX0, BY0, BX1, BY1, 3.0f, 0xFFA06040);

#ifdef ACUMULAR
	pvr_prim(&hdr_comp, sizeof(hdr_comp));
	quad(0.0f, 0.0f, 640.0f, 480.0f, 4.0f, 0xFF008000);
#endif

	pvr_list_finish();
	pvr_scene_finish();
}

static int salir(void)
{
	maple_device_t *	c = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
	cont_state_t *		e;

	if(c == NULL)
		return 0;

	e = (cont_state_t *) maple_dev_status(c);

	return (e != NULL && (e->buttons & CONT_START)) ? 1 : 0;
}

int main(int argc, char *argv[])
{
	(void) argc; (void) argv;

#ifdef ACUMULAR
	printf("--- buffer de acumulacion secundario: ACUMULADO ---\n");
#else
	printf("--- buffer de acumulacion secundario: DIRECTO ---\n");
#endif
	printf("Las dos capturas tienen que salir byte a byte iguales.\n");

	pvr_init_defaults();
	armar();

	while(!salir())
		cuadro();

	return 0;
}
