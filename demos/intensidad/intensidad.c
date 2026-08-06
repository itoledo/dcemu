/*
	intensidad.c -- el color de cara y los vertices en modo intensidad, con una
	prueba que no necesita imagen de referencia.

	Un vertice del PVR puede traer su color de cuatro formas (bits 5-4 de la
	palabra de control): empaquetado en ARGB de 32 bits, en cuatro flotantes, o
	en **modo intensidad**, donde el vertice trae un solo numero y el color sale
	del COLOR DE CARA que puso el encabezado. El modo 3 ni siquiera lo trae: usa
	el que dejo el ultimo encabezado en modo 2.

	Existe porque es lo que usan los juegos de verdad para geometria iluminada:
	censado, **Virtua Tennis 2 manda 826 414 encabezados en modo intensidad
	contra 56 576 empaquetados**, o sea el 64 % de todo lo que dibuja. Y es el
	ultimo sospechoso abierto de su sombra (docs/notas-graficos.md), justamente
	porque no habia forma de comprobarlo: ninguna demo de KOS lo ejercita.

	**La verificacion compara los dos sabores entre si.** Los dos dibujan la
	misma figura con los mismos colores; uno los manda empaquetados y el otro
	como color de cara mas una intensidad por vertice, elegidos para dar
	exactamente lo mismo. Las dos capturas tienen que salir byte a byte iguales.

	Dos filas, y cada una prueba una mitad de la regla:

	  - la de arriba, en la lista opaca, prueba el RGB: color de cara
	    (0x80,0x40,0xC0) por intensidades 1/4, 2/4, 3/4 y 4/4;
	  - la de abajo, en la translucida y mezclada sobre la de arriba, prueba de
	    donde sale el ALFA. En el chip sale del color de cara y es constante en
	    el poligono; si saliera de la intensidad, los cuatro cuadros de abajo
	    tendrian transparencias distintas y los sabores no coincidirian.

	Compilar los dos:

	    make                    -> int-empaquetado.elf
	    make INTENSIDAD=1       -> int-intensidad.elf
*/

#include <stdio.h>

#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

/* El color de cara, en bytes exactos para que el sabor empaquetado pueda
   reproducirlo sin redondeo: cada componente es multiplo de 4. */
#define CR	0x80
#define CG	0x40
#define CB	0xC0

/* El alfa de la fila translucida, tambien exacto. */
#define CA	0x80

static const float intens[4] = { 0.25f, 0.50f, 0.75f, 1.00f };

static pvr_poly_hdr_t hdr_op[4], hdr_tr[4];

/*
	El vertice de intensidad sin textura (tipo 2). KOS no lo declara, y no es lo
	mismo que el empaquetado: **la intensidad va en la palabra 4** --donde el
	empaquetado tiene una reservada-- y el color empaquetado va en la 6. Los dos
	miden 32 bytes.
*/
typedef struct {
	uint32_t	flags;
	float		x, y, z;
	float		base;			/* palabra 4: intensidad base */
	float		offset;			/* palabra 5: intensidad de offset */
	uint32_t	d1, d2;
} vert_int_t;

/* Un cuadrilatero en el orden de tira que espera el TA. */
static void quad(float x0, float y0, float x1, float y1, float z,
				 uint32_t argb, float i)
{
	pvr_vertex_t *	v;
	vert_int_t *	w;
	int				k;
	const float		x[4] = { x0, x0, x1, x1 };
	const float		y[4] = { y1, y0, y1, y0 };

	for(k = 0; k < 4; k++)
	{
#ifdef INTENSIDAD
		(void) argb; (void) v;
		w = (vert_int_t *) pvr_dr_target();
		*w = (vert_int_t){
			.flags = (k == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.x = x[k], .y = y[k], .z = z,
			.base = i, .offset = 0.0f,
			.d1 = 0, .d2 = 0,
		};
		pvr_dr_commit(w);
#else
		(void) w; (void) i;
		v = pvr_dr_target();
		*v = (pvr_vertex_t){
			.flags = (k == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.x = x[k], .y = y[k], .z = z,
			.u = 0.0f, .v = 0.0f,
			.argb = argb, .oargb = 0,
		};
		pvr_dr_commit(v);
#endif
	}
}

static void armar(void)
{
	pvr_poly_cxt_t	cxt;
	int				k;

	for(k = 0; k < 4; k++)
	{
		/* Fila opaca. */
		pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
		pvr_poly_compile(&hdr_op[k], &cxt);

		/* Fila translucida, mezclada sobre la de arriba. */
		pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
		cxt.blend.src = PVR_BLEND_SRCALPHA;
		cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		pvr_poly_compile(&hdr_tr[k], &cxt);

#ifdef INTENSIDAD
		/*
			El color de cara va en el encabezado, y el modo intensidad en la
			palabra de control. `pvr_poly_compile` no los pone porque el
			contexto de KOS no los expone, asi que se escriben aca.

			El tipo 2 es "intensidad", que es el que TRAE el color de cara.
		*/
		hdr_op[k].m0.color_fmt = PVR_CLRFMT_INTENSITY;
		hdr_op[k].a = 1.0f;
		hdr_op[k].r = CR / 255.0f;
		hdr_op[k].g = CG / 255.0f;
		hdr_op[k].b = CB / 255.0f;

		hdr_tr[k].m0.color_fmt = PVR_CLRFMT_INTENSITY;
		hdr_tr[k].a = CA / 255.0f;
		hdr_tr[k].r = CR / 255.0f;
		hdr_tr[k].g = CG / 255.0f;
		hdr_tr[k].b = CB / 255.0f;
#endif
	}
}

/* El color empaquetado equivalente a la intensidad k. */
static uint32_t empaquetado(int k, int alfa)
{
	int r = (int)(CR * intens[k] + 0.5f);
	int g = (int)(CG * intens[k] + 0.5f);
	int b = (int)(CB * intens[k] + 0.5f);

	return ((uint32_t) alfa << 24) | ((uint32_t) r << 16)
		 | ((uint32_t) g << 8) | (uint32_t) b;
}

static void cuadro(void)
{
	int k;

	pvr_scene_begin();
	pvr_dr_init(NULL);

	pvr_list_begin(PVR_LIST_OP_POLY);

	for(k = 0; k < 4; k++)
	{
		pvr_prim(&hdr_op[k], sizeof(hdr_op[k]));
		quad(40.0f + k * 150.0f, 60.0f, 170.0f + k * 150.0f, 220.0f, 2.0f,
			empaquetado(k, 0xFF), intens[k]);
	}

	pvr_list_finish();

	pvr_list_begin(PVR_LIST_TR_POLY);

	for(k = 0; k < 4; k++)
	{
		pvr_prim(&hdr_tr[k], sizeof(hdr_tr[k]));
		quad(40.0f + k * 150.0f, 180.0f, 170.0f + k * 150.0f, 400.0f, 3.0f,
			empaquetado(k, CA), intens[k]);
	}

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

#ifdef INTENSIDAD
	printf("--- color de vertice: MODO INTENSIDAD (color de cara) ---\n");
#else
	printf("--- color de vertice: EMPAQUETADO ---\n");
#endif
	printf("Las dos capturas tienen que salir byte a byte iguales.\n");

	pvr_init_defaults();
	armar();

	while(!salir())
		cuadro();

	return 0;
}
