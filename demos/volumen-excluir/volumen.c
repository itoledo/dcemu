/*
	volumen.c -- la instruccion de cierre de un volumen modificador, y una
	prueba que no necesita imagen de referencia.

	El TA cierra un volumen modificador con una de dos instrucciones (bits 30-29
	de la palabra ISP/TSP): "cerrar incluyendo" afecta a los pixeles que quedan
	DENTRO del volumen, y "cerrar excluyendo" a los que quedan FUERA. dcemu
	registraba las dos y trataba la segunda como una aproximacion --ponia en cero
	lo que sus caras cubrian-- porque **ninguna demo ni ninguno de los nueve
	juegos del arbol la usa**: se censaron 1,16 millones de tiras de Crazy Taxi
	en juego y no hay una sola.

	Por eso existe esto. Dibuja una pantalla entera con un poligono que trae los
	dos juegos de parametros --azul el 0, rojo el 1-- y un volumen cuadrado en el
	centro, y se compila en dos sabores que solo difieren en la instruccion de
	cierre.

	**La verificacion no compara contra una imagen de referencia: compara los dos
	sabores entre si.** Incluir y excluir son complementos exactos, asi que las
	dos capturas tienen que ser una el negativo de la otra pixel a pixel -- rojo
	donde la otra tiene azul y al reves, sin un solo pixel en comun. Eso lo
	comprueba una regla del chip en vez de la opinion de quien mira.

	Compilar los dos:

	    make                        -> volumen-incluir.elf
	    make EXCLUIR=1              -> volumen-excluir.elf
*/

#include <stdio.h>

#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#ifdef EXCLUIR
#define CIERRE	PVR_MODIFIER_EXCLUDE_LAST_POLY
#else
#define CIERRE	PVR_MODIFIER_INCLUDE_LAST_POLY
#endif

/* El cuadrado del volumen. Fijo y en enteros: la prueba tiene que dar lo
   mismo en cada corrida, asi que nada de rand() ni de mando. */
#define VX0	160.0f
#define VY0	120.0f
#define VX1	480.0f
#define VY1	360.0f

static pvr_poly_mod_hdr_t	phdr;
static pvr_mod_hdr_t		mhdr;
static pvr_vertex_pcm_t		verts[4];

static void armar(void)
{
	pvr_poly_cxt_t	cxt;
	int				i;
	/* Las coordenadas del cuadrilatero de pantalla completa, en el orden de
	   tira que espera el TA: (0,480) (0,0) (640,480) (640,0). */
	static const float x[4] = {   0.0f,   0.0f, 640.0f, 640.0f };
	static const float y[4] = { 480.0f,   0.0f, 480.0f,   0.0f };

	pvr_poly_cxt_col_mod(&cxt, PVR_LIST_OP_POLY);
	pvr_poly_mod_compile(&phdr, &cxt);

	/* Un solo triangulo de acumulacion no alcanza para cerrar un cuadrado:
	   van dos, y el segundo lleva la instruccion de cierre. */
	pvr_mod_compile(&mhdr, PVR_LIST_OP_MOD, PVR_MODIFIER_OTHER_POLY,
		PVR_CULLING_NONE);

	for(i = 0; i < 4; i++)
	{
		verts[i].flags = (i == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
		verts[i].x = x[i];
		verts[i].y = y[i];
		verts[i].z = 10.0f;
		verts[i].argb0 = 0xFF0000FF;	/* fuera del volumen: azul */
		verts[i].argb1 = 0xFFFF0000;	/* dentro: rojo */
		verts[i].d1 = verts[i].d2 = 0;
	}
}

static void triangulo(float ax, float ay, float bx, float by,
					  float cx, float cy)
{
	pvr_modifier_vol_t m;

	m.flags = PVR_CMD_VERTEX_EOL;
	m.ax = ax; m.ay = ay; m.az = 20.0f;
	m.bx = bx; m.by = by; m.bz = 20.0f;
	m.cx = cx; m.cy = cy; m.cz = 20.0f;
	m.d1 = m.d2 = m.d3 = m.d4 = m.d5 = m.d6 = 0;

	pvr_prim(&m, sizeof(m));
}

static void cuadro(void)
{
	pvr_mod_hdr_t cierre;

	pvr_scene_begin();

	pvr_list_begin(PVR_LIST_OP_POLY);
	pvr_prim(&phdr, sizeof(phdr));
	pvr_prim(&verts[0], sizeof(verts[0]));
	pvr_prim(&verts[1], sizeof(verts[1]));
	pvr_prim(&verts[2], sizeof(verts[2]));
	pvr_prim(&verts[3], sizeof(verts[3]));
	pvr_list_finish();

	pvr_list_begin(PVR_LIST_OP_MOD);

	/* Primer triangulo: acumula. */
	pvr_prim(&mhdr, sizeof(mhdr));
	triangulo(VX0, VY0, VX1, VY0, VX0, VY1);

	/* Segundo: cierra, y su instruccion es lo unico que cambia entre los dos
	   sabores de esta demo. */
	pvr_mod_compile(&cierre, PVR_LIST_OP_MOD, CIERRE, PVR_CULLING_NONE);
	pvr_prim(&cierre, sizeof(cierre));
	triangulo(VX1, VY0, VX1, VY1, VX0, VY1);

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

static pvr_init_params_t params = {
	{ PVR_BINSIZE_16, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
	  PVR_BINSIZE_0 },
	512 * 1024, 0, 0, 0, 0, 0
};

int main(int argc, char *argv[])
{
	(void) argc; (void) argv;

	printf("--- volumen modificador: cierre %s ---\n",
		CIERRE == PVR_MODIFIER_EXCLUDE_LAST_POLY ? "EXCLUYENDO" : "INCLUYENDO");
	printf("El cuadrado central sale %s y el resto %s.\n",
		CIERRE == PVR_MODIFIER_EXCLUDE_LAST_POLY ? "azul" : "rojo",
		CIERRE == PVR_MODIFIER_EXCLUDE_LAST_POLY ? "rojo" : "azul");
	printf("Las dos capturas tienen que ser complementos exactas.\n");

	pvr_init(&params);
	armar();

	while(!salir())
		cuadro();

	return 0;
}
