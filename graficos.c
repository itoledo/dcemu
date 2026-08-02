#include "main.h"
#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include "options.h"
#include "graficos.h"
#include "intc.h"
#include "gui.h"
#include "traza.h"
#include "opciones.h"		/* --captura-gl */
#include "ta.h"				/* clasificacion de los parametros del TA */
#include "vram.h"			/* las dos ventanas de la RAM de video */
#include <math.h>			/* log2f, profundidad_ta() */
//#include "glops.h"
#include "render.h"


#define TEXTURE_CACHING


SDL_Surface *screen;
SDL_Surface *outputscreen;

/* Lo que va en la barra de titulo. screeninit() se vuelve a llamar cada vez que
   el guest cambia el modo de video, asi que el titulo tiene que estar guardado y
   no armarse ahi. */
char titulo_ventana[256] = APPTITLE;

/*
	El contador de FPS: cuadros presentados por segundo de reloj real, en la
	barra de titulo junto al nombre. Se marca en cada SDL_GL_SwapBuffers --
	los tres caminos que presentan: la escena del TA, el framebuffer 2D y la
	vista de depuracion -- y el titulo se refresca una vez por segundo, que
	es lo que cuesta. La tecla `f` lo alterna; arranca prendido.
*/
int fps_visible = 1;

void fps_marcar_cuadro(void)
{
	static Uint32	marca = 0;
	static int		cuadros = 0;
	static int		mostrando = 0;
	Uint32			ahora = SDL_GetTicks();

	cuadros++;

	if (marca == 0)
	{
		marca = ahora;
		return;
	}

	if (ahora - marca < 1000)
		return;

	if (fps_visible)
	{
		char con_fps[sizeof(titulo_ventana) + 32];

		snprintf(con_fps, sizeof(con_fps), "%s | %lu FPS", titulo_ventana,
			(unsigned long) ((cuadros * 1000u + (ahora - marca) / 2)
				/ (ahora - marca)));
		SDL_WM_SetCaption(con_fps, NULL);
		mostrando = 1;
	}
	else if (mostrando)
	{
		/* Recien apagado: dejar el titulo limpio una sola vez. */
		SDL_WM_SetCaption(titulo_ventana, NULL);
		mostrando = 0;
	}

	marca = ahora;
	cuadros = 0;
}

/*
	Deja el nombre de lo que se esta ejecutando en la barra de titulo. Con un
	barrido de demos abiertas una tras otra, saber cual esta corriendo es la
	diferencia entre mirar la pantalla y adivinar.
*/
void titulo_poner(const char * ruta)
{
	const char * nombre = ruta;
	const char * p;

	if (ruta == NULL)
		return;

	/* Solo el nombre del archivo: la ruta completa no cabe y no aporta. */
	for (p = ruta; *p; p++)
		if (*p == '/' || *p == '\\')
			nombre = p + 1;

	snprintf(titulo_ventana, sizeof(titulo_ventana), "%s - dcemu", nombre);

	if (outputscreen != NULL)
		SDL_WM_SetCaption(titulo_ventana, NULL);
}

typedef Uint16 pcon_func(Uint16 src);

// int pvr_vertextype;
int global_parameter;
int vertex_parameter;

// int pvr_texture_surface;
int pvr_texture_pixelformat;
int pvr_texture_pixelpack;
int pvr_texture_components;
int pvr_texture_size_usize;
int pvr_texture_size_vsize;
// int pvr_texture_twiddled;
// int pvr_texture_vq;
// int pvr_listtype;
int pvr_registering = -1;

/* 1 si la lista en curso trajo algun encabezado con el bit de volumen o de
   sombra: su cierre emite tambien el evento de la lista modificadora
   asociada. Lo prende taPolyModifier() y lo apaga taListEnd(). */
static int lista_con_volumen = 0;
int pvr_listdone = 0;
int pvr_srcblend;
int pvr_dstblend;
int pvr_srcblendmode;
int pvr_dstblendmode;
GLenum pvr_depthmode;
int pvr_framebufferdisplay = true;
int screenbits = 16;
int screenwidth = 320; // en unidades de 32 bits
int screenancho = 640; // ancho en pixeles
float screentexwidth = 1024.0f;
float screentexheight = 512.0f;
int screenheight = 480;
int screenformat = FRAMEBUFFER_RGB565;
int framebuffer_size = 640*480*2;
int pvr_scanline = 0;
int pvr_3dscene = 0;
DWORD pvr_registered = 0;
DWORD pvr_lists[] = {
		ASIC_EVT_PVR_OPAQUEDONE,
		ASIC_EVT_PVR_OPAQUEMODDONE,
		ASIC_EVT_PVR_TRANSDONE,
		ASIC_EVT_PVR_TRANSMODDONE,
		ASIC_EVT_PVR_PTDONE };
DWORD pvr_fb_r_ctrl = (1 << 23) | (1 << 2) | 1; // VGA, enabled, RGB565
DWORD pvr_fb_r_sof1 = 0x0;

/* ta_address_pointer vive en ta.c: apunta al parametro ya completo, que puede
   ser el bloque que llego o el buffer donde se juntaron sus dos mitades. */

DWORD total_polygon_count=0;
DWORD strip_polygon_count = 0;
DWORD strip_count =0;

/* Contadores de diagnostico del TA, solo activos con --traza-mem. Contestan la
   pregunta que hay que hacer primero cuando no se dibuja nada: llega
   geometria, se cierran las tiras, y las coordenadas caen en pantalla. */
static int   traza_ta_vertices = 0;
static int   traza_ta_fin_tira = 0;
static int   traza_ta_tipos[TA_TIPOS];
static float traza_ta_min[3], traza_ta_max[3];

/* El 1/w mas chico y mas grande que el guest mando en toda la corrida, antes
   de la transformacion de abajo. Es el numero que hay que mirar para saber en
   que rango vive la profundidad de un juego, y salio de Crazy Taxi: sus z
   llegan a bajar de 0.001. */
static float traza_ta_z_crudo[2] = { 0.0f, 0.0f };
static int   traza_ta_z_visto = 0;

/*
	De 1/w a la coordenada z que se le da a GL.

	La z del TA es 1/w y compararla tal cual respeta el orden (mayor = mas
	cerca), pero **entregarsela lineal a glOrtho tira la precision donde mas se
	necesita**. Un juego de verdad trabaja con w de camara: su mundo lejano
	queda en 1/w de 0.001 a 0.1, con diferencias de 1e-4 entre superficies. Un
	rango lineal de +-32768 sobre un buffer de 24 bits da un paso de
	65536 / 2^24 = 0.0039: la ciudad entera de Crazy Taxi cabia en veinte
	pasos, dos paredes vecinas caian en el mismo valor, y con GEQUAL ganaba la
	que se dibujara despues aunque estuviera detras -- calles enteras
	desaparecian dejando ver el cielo.

	El chip no tiene este problema porque compara 1/w en punto flotante, con
	la resolucion pegada al cero. Esto lo imita: log2(1 + z) reparte los pasos
	del buffer en proporcion al valor, el rango queda en +-PROFUNDIDAD_RANGO
	(log2 de un 1/w de 4e9, mas que cualquier w real), y como la funcion es
	monotona el orden -- que es lo unico que las pruebas del PVR miran -- no
	cambia: todo lo que este comentario y los de vertice_nuevo() argumentan
	sobre GREATER y la inversion de near/far sigue valiendo igual.

	z = 0 es legitimo (infinitamente lejos) y queda en 0, que es tambien el
	valor del glClearDepth(). Un z negativo no es un 1/w valido; pasa lineal,
	que preserva la monotonia y no obliga a decidir aca que hacer con el.

	Lo que se pierde: GL interpola esta z linealmente en pantalla, y la
	transformacion no es lineal, asi que un poligono largo en profundidad se
	curva un poco frente al plano exacto. Dos superficies que se intersectan
	pueden mover su borde un pixel; contra perder la superficie entera, es el
	costo correcto.
*/
#define PROFUNDIDAD_RANGO 32.0

static float profundidad_ta(float z)
{
	if (traza_activa)
	{
		if (!traza_ta_z_visto || z < traza_ta_z_crudo[0]) traza_ta_z_crudo[0] = z;
		if (!traza_ta_z_visto || z > traza_ta_z_crudo[1]) traza_ta_z_crudo[1] = z;
		traza_ta_z_visto = 1;
	}

	return (z > 0.0f) ? log2f(1.0f + z) : z;
}

/*
	El color de cara vigente, en A,R,G,B. Los vertices en modo intensidad no
	traen color propio: traen un multiplicador que se aplica a este.

	Sobrevive al encabezado a proposito -- el modo intensidad 2 usa el que dejo
	el ultimo poligono en modo intensidad 1 --, asi que solo se pisa cuando
	llega un encabezado que lo trae. Blanco de partida, que es lo que deja la
	intensidad tal cual si el guest nunca lo puso.
*/
static float color_cara[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

/* "Use Alpha" del encabezado vigente (TSP bit 20): con 0, el alfa de vertice
   sale 1.0. Lo escribe taPolyModifier() y lo aplican los constructores de
   color de vertice. */
static int poly_usa_alfa = 1;

/*
	Volumenes modificadores de la escena en curso.

	`vol_lista` y `vol_instruccion` los deja el encabezado y valen para los
	triangulos que vengan detras. `vol_count` se pone en cero al empezar cada
	escena, igual que strip_count.
*/
DWORD	vol_count = 0;
static DWORD	vol_lista = 1;
static DWORD	vol_instruccion = 0;

/* El bit Shadow del ultimo encabezado de poligono. */
static int		poly_sombra = 0;

/* El color base del ultimo encabezado de sprite: en un sprite el color viene
   en el encabezado y no en los vertices. */
static DWORD	sprite_color = 0xFFFFFFFF;

/*
	Modo de sombra barata: el volumen no cambia el juego de parametros, escala
	la intensidad. FPU_SHAD_SCALE (0x005F8074) trae el factor en los bits 7-0 y
	el permiso en el 8. Se lee al armar el vertice, que es cuando se necesita.
*/
static float sombra_escala(void)
{
	DWORD reg = 0;

	memread_fisico(0xA05F8074, &reg, 4);

	if (!(reg & 0x100))
		return 1.0f;

	return (float) (reg & 0xFF) / 256.0f;
}

static int blend_modes [ ] = {GL_ZERO,GL_ONE,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA};

static int depth_modes [ ] = {GL_NEVER,GL_LESS,GL_EQUAL,GL_LEQUAL,GL_GREATER,GL_NOTEQUAL,GL_GEQUAL,GL_ALWAYS };
// ta control stuff  

tile_accell TA;
texture_info TexInfo;
renderer_control RendCtrl;

// END 

// sync pulse generator
// DWORD pvr_spg_vblank_int = 0x00280208;	// VGA 640x480
// DWORD pvr_spg_load = 0x020C0359;		// VGA 640x480
// DWORD pvr_spg_load_vcount = 0x20C;		// VGA 640x480

DWORD pvr_isp_backgnd_t = 0x0;
DWORD pvr_param_base = 0x0;
DWORD pvr_region_base = 0x0;
/*
	El puntero de escritura del TA dentro del area ISP/TSP y donde empieza esa
	area. Los dos se declaraban aqui y no los tocaba nadie: el guest los
	programaba --la escritura va al respaldo del bloque de control-- y al
	leerlos se llevaba un cero, para siempre.

	Eso importa porque el TA_ITP_CURRENT es como el guest sabe **cuanta**
	geometria proceso el chip. El boot ROM lo usa para decidir si el render que
	monto llego a hacerse; viendolo siempre en cero concluia que no habia nada
	pendiente y se rendia. Y KOS calcula ISP_BACKGND_T restando contra el, que
	es de donde salia el valor imposible que color_de_fondo() tiene que
	descartar.

	Aqui no se escribe la salida del TA en la RAM de video, asi que el avance es
	una aproximacion: un parametro, el tamano de un parametro. Lo que el guest
	necesita es que **avance**, no el valor exacto.
*/
DWORD pvr_ta_itp_current = 0x0;
DWORD pvr_ta_isp_base = 0x0;

#define TA_PARAMETRO_TAM	32

void ta_avanzar_itp(void)
{
	pvr_ta_itp_current += TA_PARAMETRO_TAM;
}
WORD pvr_spg_vblank_int_in = 0x208;
WORD pvr_spg_vblank_int_out = 0x028;

pcon_func * pvr_texture_pixelconvert;

Uint16 pcon_argb4444_to_rgba4444(Uint16 src)
{
	return (((src >> 4) & 0x0FFF) | ((src << 12) & 0xF000));
}

#define TWIDTAB(x) ( (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)| \
        ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9) )
#define TWIDOUT(x, y) ( TWIDTAB((y)) | (TWIDTAB((x)) << 1) )
#define MIN(a, b) ( (a)<(b)? (a):(b) )
struct cached_texture
{
	int		usize;		// tama�o horizontal
	int		vsize;		// tama�o vertical
	DWORD	memorypos;	// posici�n en memoria de la textura
	void *	data;		// datos de la textura 'twiddled'
 	GLuint	texture;
 	bool	twiddled;
 	bool	vq;

	/* Una textura indexada con otra paleta es otra textura aunque los indices
	   esten en la misma direccion, asi que el banco entra en la clave. */
	DWORD	paleta;
	DWORD	bpp;

	/* La cache es persistente: la entrada guarda la suma de generaciones de
	   su huella en la RAM de video (vram.h) y la generacion de la paleta al
	   decodificarse. Si alguna cambio al volver a pedirla, se decodifica de
	   nuevo en el mismo slot; si no, la textura de GL ya subida sirve. */
	DWORD	gen;
	DWORD	gen_pal;

	/* El bit de mipmap entra a la clave: la misma direccion decodificada con
	   y sin cadena son texturas distintas -- sin esto, una tira que filtra
	   con mipmaps podia ligar un objeto subido sin niveles: incompleta, y GL
	   la muestrea BLANCA (el "mundo blanco" intermitente del juego). */
	DWORD	con_mip;
};

typedef struct cached_texture cached_texture;
cached_texture cached_textures[MAX_TEXTURE_COUNT];
int cur_tex_count = 0;

/* Generacion de la RAM de paleta y su formato; la incrementa pvr_write(). */
DWORD pvr_paleta_gen = 0;

/*
	Indice hash de la cache por direccion de textura: con la cache persistente
	las 1024 entradas estan siempre llenas, y recorrerlas linealmente unas
	2600 veces por cuadro (una por tira) costaba mas que lo que la cache
	ahorraba. Cadenas por -1; una entrada reemplazada se saca de su cadena
	vieja y se mete en la nueva.
*/
#define TEX_HASH_BALDES	512
static int tex_hash[TEX_HASH_BALDES];
static int tex_hash_sig[MAX_TEXTURE_COUNT];
static int tex_hash_listo = 0;

static int tex_hash_balde(DWORD memorypos)
{
	return (int) ((memorypos >> 3) & (TEX_HASH_BALDES - 1));
}

static void tex_hash_sacar(int slot)
{
	int b = tex_hash_balde(cached_textures[slot].memorypos);
	int * p = &tex_hash[b];

	while (*p >= 0)
	{
		if (*p == slot)
		{
			*p = tex_hash_sig[slot];
			return;
		}

		p = &tex_hash_sig[*p];
	}
}

static void tex_hash_meter(int slot)
{
	int b = tex_hash_balde(cached_textures[slot].memorypos);

	tex_hash_sig[slot] = tex_hash[b];
	tex_hash[b] = slot;
}

GLuint pvr_textures[MAX_TEXTURE_COUNT];
GLuint background_texture;

bool vertexstart = true;

void limpiar_texturas()
{
	int i;
	for (i = 0; i < cur_tex_count; i++)
		if (cached_textures[i].twiddled == true)
			free(cached_textures[i].data);
	cur_tex_count = 0;
}

/* (c) Dan Potter */
unsigned short * twiddled;
unsigned short * detwiddled;
int ptr;
int imgsize;

static INT32 twiddletab[1024];

static void init_twiddletab(void)
{
  int x;
  for(x=0; x<1024; x++)
    twiddletab[x] = (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)|
      ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9);
}

/* Linear read: used for decoding */
unsigned short read_pixel() {
	return twiddled[ptr++];
}


/* ------------------------------------------------------------------------ */
/* Convertidor YUV del TA                                                   */
/* ------------------------------------------------------------------------ */

/*
	El TA tiene una entrada aparte, en 0x10800000, que recibe video en planos
	YUV420 o YUV422 y deja en la RAM de video una textura YUV422 empaquetada.
	Es como se sube video sin gastar CPU en la conversion, y es lo que usan las
	dos demos de yuv_converter.

	Se alimenta por macrobloques de 16x16 pixeles. La cuenta la lleva el propio
	chip y la reporta en PVR_YUV_STAT; el destino y el tamano de la imagen salen
	de PVR_YUV_ADDR y PVR_YUV_CFG. Nada de eso estaba emulado: la zona 0x10
	entera iba a ta_write(), que solo guarda 64 bytes para la FIFO de poligonos,
	asi que los macrobloques se perdian.

	El orden dentro del macrobloque no es "U entero, V entero, Y entero": va
	por mitades de 16x8, cada una con su U, su V y su Y. Para YUV420 hay una
	sola tanda de croma para las 16 filas y el macrobloque mide 384 bytes; para
	YUV422 hay una por mitad y mide 512.
*/

#define YUV_MB_MAX		512			/* el macrobloque mas grande, YUV422 */

static BYTE	yuv_buffer[YUV_MB_MAX];
static int	yuv_pos = 0;
DWORD		pvr_yuv_convertidos = 0;	/* lo que devuelve PVR_YUV_STAT */

void pvr_yuv_reiniciar(void)
{
	yuv_pos = 0;
	pvr_yuv_convertidos = 0;
}

/* Y, U y V de 0 a 255 -> un pixel. BT.601 en rango completo, que es el que
   usa el PVR. */
static void yuv_a_yuv422(BYTE * destino, int y0, int y1, int u, int v)
{
	destino[0] = (BYTE) u;
	destino[1] = (BYTE) y0;
	destino[2] = (BYTE) v;
	destino[3] = (BYTE) y1;
}

static void yuv_convertir_macrobloque(int es422)
{
	DWORD	cfg = 0, base = 0;
	int		ancho_mb, alto_mb, ancho_px;
	int		mbx, mby, x, y;

	memread_fisico(0xA05F814C, &cfg, 4);
	memread_fisico(0xA05F8148, &base, 4);

	ancho_mb = (int) ((cfg & 0x3F) + 1);
	alto_mb  = (int) (((cfg >> 8) & 0x3F) + 1);
	ancho_px = ancho_mb * 16;

	mbx = (int) (pvr_yuv_convertidos % (DWORD) ancho_mb);
	mby = (int) (pvr_yuv_convertidos / (DWORD) ancho_mb);

	pvr_yuv_convertidos++;

	if (mby >= alto_mb)
		return;					/* imagen completa: el resto se descarta */

	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x += 2)
		{
			/*
				Y va en cuatro subbloques de 8x8, en el orden (0,0) (8,0)
				(0,8) (8,8); en 422 los dos primeros son la mitad de arriba y
				los dos ultimos la de abajo, cada mitad detras de su croma.
			*/
			int	mitad = y / 8;
			int	sub   = (y / 8) * 2 + (x / 8);
			int	fila  = y % 8;
			int	col   = x % 8;

			int	off_y, off_c, u, v, y0, y1;

			if (es422)
			{
				/* 64 U + 64 V + 128 Y por cada mitad de 16x8. */
				off_c = mitad * 256;
				off_y = mitad * 256 + 128 + (sub % 2) * 64 + fila * 8;
				u = yuv_buffer[off_c + fila * 8 + col / 2];
				v = yuv_buffer[off_c + 64 + fila * 8 + col / 2];
			}
			else
			{
				/* 64 U + 64 V para las 16 filas, y despues 256 de Y. */
				off_y = 128 + sub * 64 + fila * 8;
				u = yuv_buffer[(y / 2) * 8 + x / 4];
				v = yuv_buffer[64 + (y / 2) * 8 + x / 4];
			}

			y0 = yuv_buffer[off_y + col];
			y1 = yuv_buffer[off_y + col + 1];

			{
				BYTE	pixel[4];
				DWORD	destino = (base & 0xFFFFFF) +
					(DWORD) (((mby * 16 + y) * ancho_px + (mbx * 16 + x)) * 2);

				yuv_a_yuv422(pixel, y0, y1, u, v);

				/* TA_YUV_TEX_BASE es una direccion de textura: numeracion del
				   area de 64 bits, la misma por la que get_texture() la va a
				   juntar. Ver vram.h. */
				vram64_escribir(destino, pixel, 4);
			}
		}
	}
}

/*
	Un bloque de 32 bytes entrando por 0x10800000. Lo llama pref142() al vaciar
	una store queue; el guest tambien puede mandarlos por el CH2 DMA.
*/
void pvr_yuv_bloque(void * datos)
{
	DWORD	cfg = 0;
	int		es422, tam;

	memread_fisico(0xA05F814C, &cfg, 4);

	es422 = (cfg & (1u << 24)) ? 1 : 0;
	tam   = es422 ? 512 : 384;

	if (yuv_pos + 32 > tam)
		yuv_pos = 0;			/* desincronizado: se empieza de nuevo */

	memcpy(&yuv_buffer[yuv_pos], datos, 32);
	yuv_pos += 32;

	if (yuv_pos < tam)
		return;

	yuv_pos = 0;

	yuv_convertir_macrobloque(es422);

	if (traza_activa && pvr_yuv_convertidos == 1)
	{
		DWORD base = 0;

		memread_fisico(0xA05F8148, &base, 4);

		fprintf(stderr, "traza: convertidor YUV%s a %08lx, %lu x %lu"
			" macrobloques\n",
			es422 ? "422" : "420",
			(unsigned long) (base & 0xFFFFFF),
			(unsigned long) ((cfg & 0x3F) + 1),
			(unsigned long) (((cfg >> 8) & 0x3F) + 1));
	}
}

/*
	RAM de paleta del PVR: 1024 entradas de 32 bits en 0x005F9000, y el formato
	de esas entradas en PAL_RAM_CTRL (0x005F8108), bits 0-1. Los dos viven en el
	respaldo del bloque de control, asi que las escrituras del guest ya estaban
	llegando: lo que faltaba era leerlas.

	Se devuelve siempre RGBA8888, que es lo que get_texture() le entrega a GL
	para las texturas indexadas.
*/
static DWORD paleta_entrada(int indice)
{
	DWORD crudo = 0, formato = 0;

	memread_fisico(0xA05F9000 + indice * 4, &crudo, 4);
	memread_fisico(0xA05F8108, &formato, 4);

	/* GL recibe GL_RGBA / GL_UNSIGNED_BYTE, o sea R en el byte 0 y A en el 3,
	   que en little endian es un DWORD 0xAABBGGRR. Los formatos del PVR vienen
	   todos en orden ARGB, asi que R y B cambian de lado. */
	switch (formato & 3)
	{
		case 0:		/* ARGB1555 */
			return  ((crudo & 0x7C00) >> 7) | ((crudo & 0x03E0) << 6) |
					((crudo & 0x001F) << 19) | ((crudo & 0x8000) ? 0xFF000000 : 0);

		case 1:		/* RGB565: sin alfa, siempre opaco */
			return  ((crudo & 0xF800) >> 8) | ((crudo & 0x07E0) << 5) |
					((crudo & 0x001F) << 19) | 0xFF000000;

		case 2:		/* ARGB4444 */
			return  ((crudo & 0x0F00) >> 4) | ((crudo & 0x00F0) << 8) |
					((crudo & 0x000F) << 20) | ((crudo & 0xF000) << 16);

		default:	/* ARGB8888 */
			return  ((crudo & 0x00FF0000) >> 16) | (crudo & 0xFF00FF00) |
					((crudo & 0x000000FF) << 16);
	}
}

/*
	Textura indexada -> RGBA8888. El twiddle es el mismo de siempre, pero opera
	sobre indices de pixel y no sobre palabras de 16 bits: en 8 bpp eso es un
	byte y en 4 bpp medio, que es justo lo que el camino de arriba no sabia
	hacer.

	El banco de paleta viene del propio texture control word y mide 16 entradas
	en 4 bpp y 256 en 8 bpp, siempre dentro de las mismas 1024.
*/
static DWORD * decodificar_paleta(const BYTE * origen, int usize, int vsize,
	int bpp, DWORD banco, int twiddled)
{
	DWORD *	destino = (DWORD *) malloc(sizeof(DWORD) * usize * vsize);
	DWORD	base = (bpp == 4) ? (banco * 16) : (banco * 256);
	DWORD	tabla[256];
	int		entradas = (bpp == 4) ? 16 : 256;
	int		i, j, min, mask;

	if (destino == NULL)
		return NULL;

	for (i = 0; i < entradas; i++)
		tabla[i] = paleta_entrada((int) ((base + i) & 0x3FF));

	if (traza_activa)
	{
		/* Una vez por cada (bpp, banco): la paleta se anima, asi que sin
		   deduplicar es una linea por cuadro. */
		static unsigned char vistos[2][64];
		int fila = (bpp == 4) ? 0 : 1;

		if (!vistos[fila][banco & 0x3F])
		{
			DWORD formato = 0;

			vistos[fila][banco & 0x3F] = 1;
			memread_fisico(0xA05F8108, &formato, 4);

			fprintf(stderr, "traza: textura %d bpp %dx%d, banco %lu (entradas"
				" %lu..%lu), PAL_RAM_CTRL=%lu\n",
				bpp, usize, vsize, (unsigned long) banco,
				(unsigned long) base, (unsigned long) (base + entradas - 1),
				(unsigned long) (formato & 3));
		}
	}

	min  = MIN(usize, vsize);
	mask = min - 1;

	for (i = 0; i < vsize; i++)
	{
		for (j = 0; j < usize; j++)
		{
			int	pos = twiddled
				? (TWIDOUT(j & mask, i & mask) + (j / min + i / min) * min * min)
				: (i * usize + j);
			int	indice;

			if (bpp == 4)
				indice = (pos & 1) ? (origen[pos >> 1] >> 4) : (origen[pos >> 1] & 0x0F);
			else
				indice = origen[pos];

			destino[i * usize + j] = tabla[indice];
		}
	}

	return destino;
}

/* Un componente de color, recortado a 0..255. */
static BYTE recortar(int v)
{
	return (BYTE) ((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

/*
	Textura BUMP -> RGBA8888 en escala de grises.

	Un texel de bump no es un color: son **dos angulos** de 8 bits, la elevacion
	S en la mitad alta y la rotacion R en la baja. La intensidad la calcula el
	chip por pixel combinandolos con cuatro parametros que vienen en el color de
	offset del poligono -- K1, K2, K3 y Q, un byte cada uno:

	    I = K1 + K2 * sin(S) + K3 * cos(S) * cos(R - Q)

	S recorre 0..pi/2, R y Q recorren 0..2*pi, y K1..K3 son 0..1.

	**Lo que esto NO hace**: en el chip esa intensidad *modula* al poligono
	texturado que viene detras, y eso es matematica por fragmento que OpenGL de
	funcion fija no tiene. Aca la intensidad se resuelve al subir la textura y
	sale como un gris. Es exacto mientras los parametros sean del encabezado --
	que es el caso de un sprite, donde el color de offset esta ahi -- y lo que
	se pierde es la combinacion con la otra capa.
*/
static DWORD * decodificar_bump(const Uint16 * origen, int usize, int vsize,
								int twiddled, DWORD parametros)
{
	/* M_PI no es estandar y MSVC no la define sin _USE_MATH_DEFINES. */
	static const double PI = 3.14159265358979323846;

	DWORD *	destino = (DWORD *) malloc((size_t) usize * vsize * sizeof(DWORD));
	double	k1 = ((parametros >> 24) & 0xFF) / 255.0;
	double	k2 = ((parametros >> 16) & 0xFF) / 255.0;
	double	k3 = ((parametros >> 8)  & 0xFF) / 255.0;
	double	q  = ((parametros >> 0)  & 0xFF) / 255.0 * 2.0 * PI;
	int		i, j, min, mask;

	if (destino == NULL)
		return NULL;

	min = MIN(usize, vsize);
	mask = min - 1;

	for (i = 0; i < vsize; i++)
	{
		for (j = 0; j < usize; j++)
		{
			int		pos = twiddled
						? (TWIDOUT(j & mask, i & mask) + (j / min + i / min) * min * min)
						: (i * usize + j);
			Uint16	texel = origen[pos];
			double	s = ((texel >> 8) & 0xFF) / 255.0 * (PI / 2.0);
			double	r = ((texel >> 0) & 0xFF) / 255.0 * 2.0 * PI;
			double	intensidad;
			BYTE	g;

			intensidad = k1 + k2 * sin(s) + k3 * cos(s) * cos(r - q);

			g = recortar((int) (intensidad * 255.0));

			/* GL_RGBA con bytes: R en el byte 0. Gris opaco. */
			destino[i * usize + j] =
				((DWORD) 0xFF << 24) | ((DWORD) g << 16) | ((DWORD) g << 8) | g;
		}
	}

	return destino;
}

/*
	Textura YUV422 -> RGBA8888. Cada 32 bits son dos pixeles que comparten
	croma: U, Y0, V, Y1. La matriz es BT.601 en rango completo, que es la que
	usa el PVR -- Y no se escala, asi que un Y de 0 sale negro y uno de 255
	blanco.
*/
static DWORD * decodificar_yuv422(const DWORD * origen, int usize, int vsize)
{
	DWORD *	destino = (DWORD *) malloc(sizeof(DWORD) * usize * vsize);
	int		i, j;

	if (destino == NULL)
		return NULL;

	if (traza_activa)
	{
		static int visto = 0;

		if (!visto)
		{
			visto = 1;
			fprintf(stderr, "traza: textura YUV422 de %dx%d\n", usize, vsize);
		}
	}


	for (i = 0; i < vsize; i++)
	{
		for (j = 0; j < usize; j += 2)
		{
			DWORD	par = origen[(i * usize + j) / 2];
			int		u  = (int) ( par        & 0xFF);
			int		y0 = (int) ((par >>  8) & 0xFF);
			int		v  = (int) ((par >> 16) & 0xFF);
			int		y1 = (int) ((par >> 24) & 0xFF);
			int		du = u - 128, dv = v - 128;
			int		k;

			/* Los dos pixeles del par solo se diferencian en Y. */
			for (k = 0; k < 2; k++)
			{
				int		y = k ? y1 : y0;
				BYTE	r = recortar(y + ((11 * dv) >> 3));
				BYTE	g = recortar(y - ((11 * du) >> 5) - ((11 * dv) >> 4));
				BYTE	b = recortar(y + ((11 * du) >> 3));

				/* GL_RGBA/GL_UNSIGNED_BYTE: R en el byte 0. */
				destino[i * usize + j + k] =
					(DWORD) r | ((DWORD) g << 8) | ((DWORD) b << 16) | 0xFF000000;
			}
		}
	}

	return destino;
}

/*
	Los filtros de la textura que este ligada en este momento.

	Iban en cb_tastart() **antes** de llamar aca, o sea antes del glBindTexture:
	glTexParameteri no toma un nombre de textura, se aplica a la ligada, asi que
	los parametros caian sobre la del cuadro anterior y la recien creada se
	quedaba con los de fabrica. Y el de fabrica de GL_TEXTURE_MIN_FILTER es
	GL_NEAREST_MIPMAP_LINEAR, que **exige mipmaps**: sin ellos la textura esta
	incompleta y GL la muestrea blanca.

	Con una demo que dibuja muchos cuadros no se nota -- a partir del segundo,
	la textura ya quedo ligada del cuadro anterior y recibe los parametros --,
	pero una que dibuja uno solo y espera un boton sale entera en blanco. Eso es
	lo que le pasaba a las dos de yuv_converter.
*/
static void aplicar_filtros(int strip)
{
	GLint modo = TriangleStrip[strip].texture.filtermode ? GL_LINEAR : GL_NEAREST;
	GLint min  = modo;

	/* Con mipmaps (GL los genero al subir la textura; ver get_texture()), la
	   minificacion los usa: es lo que corta el alias del piso lejano. */
	if ((TriangleStrip[strip].texture.mipmapped || getenv("DCEMU_MIP_AUTO"))
		&& !getenv("DCEMU_SIN_FILTRO_MIP"))
		min = TriangleStrip[strip].texture.filtermode
			? GL_LINEAR_MIPMAP_LINEAR
			: GL_NEAREST_MIPMAP_NEAREST;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, modo);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
}

/*
	El volcado del framebuffer a la RAM de video, armado bajo demanda.

	dcemu manda el 3D a OpenGL y la escena nunca pasa por la RAM de video. A
	casi ninguna demo le importa, pero pvr-fb_tex muestrea su propio front
	buffer como textura: necesita que el cuadro exista en memoria **y** que las
	dos numeraciones (vram.h) esten bien, porque la textura lo lee por la
	ventana de 64 bits y el framebuffer se escribio por la de 32.

	Se arma aca abajo y lo consume volcar_escena_a_framebuffer(); antes de
	armarse no cuesta nada, para no cobrarle un glReadPixels por cuadro a
	todas las demas demos.
*/
static int volcado_fb_armado = 0;

/*
	El disparador: una textura cuya direccion, convertida a la numeracion de
	32 bits, cae dentro del cuadro que el PVR escribe o muestra, esta
	muestreando el framebuffer.
*/
static void armar_volcado_si_muestrea_framebuffer(DWORD memorypos)
{
	DWORD	a32 = vram_64_a_32(memorypos);
	DWORD	sof_w = 0, paso = 0, pclip_y = 0;
	DWORD	sof_r = pvr_fb_r_sof1 & 0x007FFFFF;
	DWORD	filas, alto, tam;

	memread_fisico(0xA05F8060, &sof_w, 4);

	/* Con el bit 24 FB_W apunta a una textura, no al framebuffer. */
	if (sof_w & 0x01000000)
		return;

	memread_fisico(0xA05F804C, &paso, 4);
	memread_fisico(0xA05F806C, &pclip_y, 4);

	filas = (paso & 0x1FF) * 8;
	alto  = ((pclip_y >> 16) & 0x3FF) + 1;
	tam   = alto * filas;

	if (tam == 0)
		return;

	sof_w &= 0x007FFFFF;

	if ((a32 >= sof_w && a32 < sof_w + tam) ||
		(a32 >= sof_r && a32 < sof_r + tam))
	{
		volcado_fb_armado = 1;

		if (traza_activa)
			fprintf(stderr, "traza: textura en %06x (32 bits: %06x) muestrea el"
				" framebuffer (escribe %06x, muestra %06x): se arma el volcado"
				" por escena\n",
				(unsigned) memorypos, (unsigned) a32,
				(unsigned) sof_w, (unsigned) sof_r);
	}
}

void get_texture(int usize, int vsize, DWORD memorypos, int twiddled, int vq,int strip)
{
	Uint16 * q, * v;
	unsigned char * plano;
	size_t plano_bytes;
	size_t mip_salto = 0;
	int i, j;
	int slot, slot_nuevo;
	int rehash = 0;
	int cuenta_guardada;
	DWORD gen_ahora, pal_ahora;
	DWORD bpp    = TriangleStrip[strip].texture.pvr_texture_bpp;
	DWORD paleta = TriangleStrip[strip].texture.pvr_texture_paleta;
	DWORD stride = TriangleStrip[strip].texture.pvr_texture_stride;

	if (!volcado_fb_armado)
		armar_volcado_si_muestrea_framebuffer(memorypos);

	/*
		Una textura con mipmaps guarda sus niveles **del 1x1 al grande**, con 6
		bytes de relleno adelante: el nivel de lado 2^n empieza en
		6 + 2*(4^n - 1)/3 bytes -- la tabla de pvrtex de KOS, verificada contra
		sus constantes --, en unidades de 16 bpp. dcemu usa solo el nivel
		grande, asi que hay que saltar los chicos; decodificar desde el offset
		0 entrega esos niveles revueltos, que es el ruido en bloques que tenian
		el tranvia, el suelo y los edificios de Crazy Taxi mientras el taxi y
		el HUD (sin mipmaps) salian bien. La escala por formato: los indices VQ
		son 1 byte por bloque de 2x2 texels (un octavo de 16 bpp, y el salto
		corre despues del codebook, que queda al principio), la paleta de
		4 bpp un cuarto y la de 8 la mitad. Una textura con mipmaps es
		cuadrada, asi que el lado es usize.

		El salto y el tamano se calculan ANTES de buscar en la cache porque
		son la huella de la textura en la RAM de video, y la huella es lo que
		decide si la entrada cacheada sigue valida.
	*/
	if (TriangleStrip[strip].texture.mipmapped)
	{
		size_t ofs16 = 6;
		int lado;

		for (lado = 1; lado < usize; lado <<= 1)
			ofs16 += (size_t) lado * lado * 2;

		if (vq)
			mip_salto = ofs16 / 8;
		else if (bpp != 0)
			mip_salto = ofs16 * bpp / 16;
		else
			mip_salto = ofs16;
	}

	if (vq)
	{
		/* El codebook de 2 KB mas los indices. El indexado twiddled de un
		   rectangulo se sale del producto, asi que se junta el cuadrado. */
		size_t lado = (size_t) (usize > vsize ? usize : vsize);

		plano_bytes = 0x800 + mip_salto + lado * lado / 4;
	}
	else if (bpp != 0)
		plano_bytes = (size_t) usize * vsize * bpp / 8;
	else if (!twiddled && stride != 0 && stride != (DWORD) usize)
		plano_bytes = (size_t) vsize * stride * 2;
	else
		plano_bytes = (size_t) usize * vsize * 2;

	/* Con mipmaps el bloque juntado cubre la cadena entera: los niveles
	   chicos primero y el grande al final del salto. */
	if (!vq)
		plano_bytes += mip_salto;

	/* La generacion de la huella (vram.h) y la de la paleta si es indexada:
	   si ninguna cambio desde que se decodifico, la textura de GL sirve. */
	gen_ahora = vram_gen_rango64(memorypos, plano_bytes);
	pal_ahora = (bpp != 0) ? pvr_paleta_gen : 0;

	if (!tex_hash_listo)
	{
		for (i = 0; i < TEX_HASH_BALDES; i++)
			tex_hash[i] = -1;

		tex_hash_listo = 1;
	}

	slot = -1;

	if (!getenv("DCEMU_SIN_CACHE_TEX"))
	for (i = tex_hash[tex_hash_balde(memorypos)]; i >= 0; i = tex_hash_sig[i])
	{
		if (cached_textures[i].memorypos == memorypos
		&&  cached_textures[i].usize == usize
		&&  cached_textures[i].vsize == vsize
		&&  cached_textures[i].bpp == bpp
		&&  cached_textures[i].paleta == paleta
		&&  cached_textures[i].con_mip == TriangleStrip[strip].texture.mipmapped)
		{
			if (cached_textures[i].gen == gen_ahora
			&&  cached_textures[i].gen_pal == pal_ahora)
			{
				logxmsg(LOG_PVR, "get_texture: retornando textura %d en cache\n", i);
				glBindTexture(GL_TEXTURE_2D, cached_textures[i].texture);
				aplicar_filtros(strip);
				return;
			}

			/* La clave esta pero alguien escribio adentro (o cambio la
			   paleta): se decodifica de nuevo en el mismo slot, que ya esta
			   en la cadena correcta. */
			slot = i;
			break;
		}
	}

	/*
		La cache es persistente -- ya no se vacia por escena -- asi que un
		slot nuevo sale del final mientras haya lugar y despues por rueda,
		reemplazando la entrada mas vieja por orden de insercion. La textura
		reemplazada no necesita despedida: su id de GL se reusa al subir esta.
	*/
	if (slot < 0)
	{
		static int tex_rueda = 0;

		if (cur_tex_count < MAX_TEXTURE_COUNT)
			slot = cur_tex_count;
		else
		{
			slot = tex_rueda++ % MAX_TEXTURE_COUNT;
			tex_hash_sacar(slot);	/* la victima deja su cadena vieja */
		}

		rehash = 1;					/* clave nueva: entra al hash al final */
	}

	slot_nuevo = (slot == cur_tex_count);

	if (cached_textures[slot].twiddled)
	{
		free(cached_textures[slot].data);
		cached_textures[slot].data = NULL;
		cached_textures[slot].twiddled = false;
	}

	/*
		De aca hasta el final, `cur_tex_count` ES el slot que se esta llenando:
		el cuerpo de decodificacion entero indexa con el y queda como estaba.
		Se restaura al salir de `cuenta_guardada` -- variable propia, porque
		`i` y `j` los pisan los bucles del decodificador -- y crece solo si el
		slot era nuevo.
	*/
	cuenta_guardada = cur_tex_count;
	cur_tex_count = slot;

	logxmsg(LOG_PVR, "get_texture: creando textura %d\n", cur_tex_count);

	cached_textures[cur_tex_count].usize = usize;
	cached_textures[cur_tex_count].vsize = vsize;
	cached_textures[cur_tex_count].memorypos = memorypos;
	cached_textures[cur_tex_count].texture = pvr_textures[cur_tex_count];
	cached_textures[cur_tex_count].bpp = bpp;
	cached_textures[cur_tex_count].paleta = paleta;
	cached_textures[cur_tex_count].gen = gen_ahora;
	cached_textures[cur_tex_count].gen_pal = pal_ahora;
	cached_textures[cur_tex_count].con_mip = TriangleStrip[strip].texture.mipmapped;

	if (rehash)
		tex_hash_meter(cur_tex_count);

	/*
		`memorypos` sale de la palabra de control de textura, o sea que esta en
		la numeracion del area de 64 bits -- la que el chip usa para leer
		texturas -- y el bloque se guarda en la de 32. Se junta aca a un buffer
		contiguo y los decodificadores quedan como estaban. Ver vram.h.
	*/
	plano = (unsigned char *) malloc(plano_bytes);

	if (plano == NULL)
	{
		cur_tex_count = cuenta_guardada;
		return;
	}

	vram64_leer(memorypos, plano, plano_bytes);

	/* DCEMU_VOLCAR_TEX=hex: la primera vez que se decodifica la textura de
	   esa direccion, el bloque juntado tal cual sale a tex-<addr>.bin. Es la
	   verdad de que bytes consumio el decodificador EN EL MOMENTO del draw,
	   que un --volcar al salir no puede dar si la region se reescribe. */
	if (traza_activa)
	{
		const char * e = getenv("DCEMU_VOLCAR_TEX");

		if (e != NULL && (DWORD) strtoul(e, NULL, 16) == memorypos)
		{
			static int hecho = 0;

			if (!hecho)
			{
				char nom[64];
				FILE * fp;

				hecho = 1;
				snprintf(nom, sizeof(nom), "tex-%06lx.bin",
					(unsigned long) memorypos);
				fp = fopen(nom, "wb");

				if (fp != NULL)
				{
					fwrite(plano, 1, plano_bytes, fp);
					fclose(fp);
					fprintf(stderr, "traza: textura %06lx volcada a %s"
						" (%lu bytes, mip_salto %lu)\n",
						(unsigned long) memorypos, nom,
						(unsigned long) plano_bytes,
						(unsigned long) mip_salto);
				}
			}
		}
	}

	/* El nivel grande: tras el salto de los chicos en los formatos planos; en
	   VQ el salto corre sobre los indices y el codebook queda al principio. */
	v = (Uint16 *) (plano + (vq ? 0 : mip_salto));

	// ahora al twiddle
	if (TriangleStrip[strip].texture.pvr_texture_bump)
	{
		cached_textures[cur_tex_count].data =
			decodificar_bump((const Uint16 *) v, usize, vsize, twiddled,
				TriangleStrip[strip].texture.pvr_texture_bump_param);

		cached_textures[cur_tex_count].twiddled = true;
	}
	else
	if (bpp != 0)
	{
		cached_textures[cur_tex_count].data =
			decodificar_paleta((const BYTE *) v, usize, vsize, (int) bpp, paleta, twiddled);

		/* twiddled aca significa "el buffer es nuestro y hay que liberarlo",
		   que es lo unico que mira limpiar_texturas(). */
		cached_textures[cur_tex_count].twiddled = true;
	}
	else
	if (TriangleStrip[strip].texture.pvr_texture_yuv)
	{
		cached_textures[cur_tex_count].data =
			decodificar_yuv422((const DWORD *) v, usize, vsize);

		cached_textures[cur_tex_count].twiddled = true;
	}
	else
	if (vq)
	{
		cached_textures[cur_tex_count].data = (void *) malloc(sizeof(Uint16) * usize * vsize);

		cached_textures[cur_tex_count].vq = true;
		cached_textures[cur_tex_count].twiddled = true;

		// leamos el codebook
		unsigned char * codebook = (unsigned char *) v;
		BYTE * index = plano + 0x0800 + mip_salto;

		int upos, vpos;

		q = cached_textures[cur_tex_count].data;
		
		for (vpos = 0; vpos < vsize/2; vpos++)
		{
			for (upos = 0; upos < usize/2; upos++)
			{
				unsigned char * cbsrc = codebook+(index[(twiddletab[upos]<<1)|twiddletab[vpos]]<<3);
				unsigned int p = cbsrc[0]|(cbsrc[1]<<8);
				q[0] = p;
				p = cbsrc[4]|(cbsrc[5]<<8);
				q[1] = p;
				p = cbsrc[2]|(cbsrc[3]<<8);
				q[usize] = p;
				p = cbsrc[6]|(cbsrc[7]<<8);
				q[usize+1] = p;
				q+=2;
			}

			q += usize;
		}
	}
	else
	if (twiddled)
	{
		cached_textures[cur_tex_count].data = (void *) malloc(sizeof(Uint16) * usize * vsize);
		q = cached_textures[cur_tex_count].data;


		int min, mask, yout;
		
		min = MIN(usize, vsize);
		mask = min - 1;
		
    	for (i = 0; i < vsize; i++)
	   	{
    		for (j = 0; j < usize; j++)
    		{
				*(q++) = v[TWIDOUT(j&mask,i&mask) + (j/min + i/min)*min*min];
    		}
		}
    	cached_textures[cur_tex_count].twiddled = true;
	}
	else
	if (stride != 0 && stride != (DWORD) usize)
	{
		/*
			Textura rectangular: en memoria las filas miden `stride` texels y no
			`usize`, asi que entregarle el bloque crudo a GL sale con la imagen
			corrida un poco mas en cada fila. Se copia fila a fila.

			Nunca va twiddled -- el bit de stride y el de orden de barrido son
			excluyentes en el chip --, y si el stride es menor que el ancho
			declarado lo que sobra queda en cero en vez de leerse de la fila
			siguiente.
		*/
		Uint16 *	d;
		int			copiar = (stride < (DWORD) usize) ? (int) stride : usize;

		cached_textures[cur_tex_count].data =
			(void *) calloc((size_t) usize * vsize, sizeof(Uint16));

		d = cached_textures[cur_tex_count].data;

		if (d != NULL)
			for (i = 0; i < vsize; i++)
				memcpy(&d[(size_t) i * usize], &v[(size_t) i * stride],
					(size_t) copiar * sizeof(Uint16));

		cached_textures[cur_tex_count].twiddled = true;	/* hay que liberarlo */

		if (traza_activa)
		{
			static DWORD ultimo = 0;

			if (stride != ultimo)
			{
				ultimo = stride;
				fprintf(stderr, "traza: textura con stride %lu en %dx%d,"
					" se copian %d por fila\n",
					(unsigned long) stride, usize, vsize, copiar);
			}
		}
	}
	else
	{
		/* Sin transformar: el buffer juntado ya es la textura, y es propio --
		   antes apuntaba a la RAM de video y no habia que liberarlo. Si el
		   nivel grande esta ADENTRO de plano (mipmaps), copia propia: liberar
		   un puntero interior corrompe el heap. */
		if ((unsigned char *) v != plano)
		{
			size_t n = plano_bytes - mip_salto;

			cached_textures[cur_tex_count].data = malloc(n);

			if (cached_textures[cur_tex_count].data != NULL)
				memcpy(cached_textures[cur_tex_count].data, v, n);
		}
		else
			cached_textures[cur_tex_count].data = v;

		cached_textures[cur_tex_count].twiddled = true;
	}

	   glBindTexture(GL_TEXTURE_2D, cached_textures[cur_tex_count].texture);
	   aplicar_filtros(strip);

	/*
		La cadena de mipmaps. Sin los niveles chicos la minificacion fuerte
		hace alias -- el chisporroteo del piso lejano en angulos rasantes.
		Los niveles se suben DEL GUEST: estan ahi mismo en el bloque juntado,
		son los del artista (los juegos meten trucos de LOD en ellos) y salen
		mas baratos que generarlos en cada re-subida de una textura que rota.
		GL los genera solo (GL_GENERATE_MIPMAP, GL 1.4) unicamente en los
		formatos que dcemu resuelve a RGBA y cuyo nivel chico no vale la pena
		decodificar aparte: BUMP y YUV.

		VQ no baja de 2x2 -- el indice de 1x1 comparte byte con el de 2x2 --
		asi que la cadena se recorta con GL_TEXTURE_MAX_LEVEL (GL 1.2): sin
		el recorte la textura queda incompleta y GL la muestrea blanca.
	*/
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
	{
		int con_mip = TriangleStrip[strip].texture.mipmapped;
		int gen_gl  = con_mip
			&& (TriangleStrip[strip].texture.pvr_texture_bump
			 || TriangleStrip[strip].texture.pvr_texture_yuv);

		/* DCEMU_MIP_AUTO: genera mipmaps tambien para texturas que no los
		   traen. Diagnostico -- el chip real no lo hace: sirve para separar
		   "submuestreo sin mipmaps" (el moire del piso cercano de kgl-tunnel)
		   de una lectura mal hecha de la textura. */
		if (!con_mip && getenv("DCEMU_MIP_AUTO"))
			gen_gl = 1;

		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP,
			gen_gl ? GL_TRUE : GL_FALSE);

		logxmsg(LOG_PVR, "get_texture: size %dx%d, mempos %x (area de 64 bits)\n", usize, vsize, memorypos);
		glTexImage2D(GL_TEXTURE_2D, 0, TriangleStrip[strip].texture.pvr_texture_components, usize, vsize, 0, TriangleStrip[strip].texture.pvr_texture_pixelformat, TriangleStrip[strip].texture.pvr_texture_pixelpack, cached_textures[cur_tex_count].data);

		/* Los objetos de textura se reusan: el tope vuelve a su valor por
		   omision y el recorte de VQ lo baja si corresponde. */
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);

		if (con_mip && !gen_gl)
		{
			int nivel = 1, s, ultimo = 0;

			for (s = usize >> 1; s >= 1; s >>= 1, nivel++)
			{
				/* El offset del nivel de lado s, en unidades de 16 bpp:
				   6 + 2*(4^k - 1)/3, la tabla de pvrtex de KOS. */
				size_t of16 = 6;
				int lado;
				void * buf = NULL;

				for (lado = 1; lado < s; lado <<= 1)
					of16 += (size_t) lado * lado * 2;

				if (vq)
				{
					const unsigned char * cb = plano;
					const BYTE * idx = plano + 0x800 + of16 / 8;
					Uint16 * salida;
					int upos, vpos;

					if (s < 2)
						break;		/* la cadena VQ termina en 2x2 */

					buf = malloc((size_t) s * s * sizeof(Uint16));

					if (buf == NULL)
						break;

					salida = (Uint16 *) buf;

					for (vpos = 0; vpos < s / 2; vpos++)
					{
						for (upos = 0; upos < s / 2; upos++)
						{
							const unsigned char * cbsrc =
								cb + (idx[(twiddletab[upos] << 1) | twiddletab[vpos]] << 3);

							salida[0] = (Uint16) (cbsrc[0] | (cbsrc[1] << 8));
							salida[1] = (Uint16) (cbsrc[4] | (cbsrc[5] << 8));
							salida[s] = (Uint16) (cbsrc[2] | (cbsrc[3] << 8));
							salida[s + 1] = (Uint16) (cbsrc[6] | (cbsrc[7] << 8));
							salida += 2;
						}

						salida += s;
					}
				}
				else if (bpp != 0)
				{
					buf = decodificar_paleta(plano + of16 * bpp / 16, s, s,
						(int) bpp, paleta, 1);

					if (buf == NULL)
						break;
				}
				else
				{
					/* 16 bpp twiddled: un mipmap siempre lo es. */
					const Uint16 * org = (const Uint16 *) (plano + of16);
					Uint16 * salida;
					int fi, co, mask = s - 1;

					buf = malloc((size_t) s * s * sizeof(Uint16));

					if (buf == NULL)
						break;

					salida = (Uint16 *) buf;

					for (fi = 0; fi < s; fi++)
						for (co = 0; co < s; co++)
							*(salida++) = org[TWIDOUT(co & mask, fi & mask)];
				}

				glTexImage2D(GL_TEXTURE_2D, nivel,
					TriangleStrip[strip].texture.pvr_texture_components, s, s, 0,
					TriangleStrip[strip].texture.pvr_texture_pixelformat,
					TriangleStrip[strip].texture.pvr_texture_pixelpack, buf);

				free(buf);
				ultimo = nivel;
			}

			if (vq)
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
					ultimo > 0 ? ultimo : 0);

			/* Si alguna subida de nivel fallo, la textura queda incompleta y
			   GL la muestrea blanca: es la primera pregunta ante un "mundo
			   blanco". Una linea por (tamano, error) distinto. */
			if (traza_activa)
			{
				GLenum e = glGetError();

				if (e != 0)
				{
					static unsigned vistos[8];
					int i;

					for (i = 0; i < 8 && vistos[i]; i++)
						if (vistos[i] == ((unsigned) e ^ (unsigned) usize))
							break;

					if (i < 8 && !vistos[i])
					{
						vistos[i] = (unsigned) e ^ (unsigned) usize;
						fprintf(stderr, "traza: error GL %04x subiendo mipmaps"
							" de %dx%d (vq=%d ultimo=%d)\n",
							(unsigned) e, usize, vsize, vq, ultimo);
					}
				}
			}
		}
	}

	/* El plano recien ahora: los niveles chicos de la cadena de mipmaps se
	   decodifican de el DESPUES del nivel grande, en el bloque de arriba.
	   Liberarlo antes de ese bloque -- donde estaba -- era un use-after-free:
	   el malloc de cada nivel reciclaba el bloque recien liberado y los
	   niveles chicos salian de basura estructurada. Era el triangulo
	   invertido tramado en las palmeras lejanas de Crazy Taxi y sus autos
	   deslavados a media distancia; el nivel grande, decodificado antes del
	   free, siempre estuvo bien, y por eso todo se veia sano de cerca. */
	if (cached_textures[cur_tex_count].data != (void *) plano)
		free(plano);

	/* GL ya tiene su copia: la de CPU no se guarda -- mil texturas
	   persistentes de a cientos de KB serian memoria muerta. */
	if (cached_textures[cur_tex_count].twiddled)
	{
		free(cached_textures[cur_tex_count].data);
		cached_textures[cur_tex_count].data = NULL;
		cached_textures[cur_tex_count].twiddled = false;
	}

	/* Restaurar la cuenta: solo crece si el slot era nuevo. */
	cur_tex_count = slot_nuevo ? cuenta_guardada + 1 : cuenta_guardada;

	return;
}


/*
	El color del plano de fondo del PVR.

	El chip no limpia la pantalla a negro: dibuja un "background plane", un
	poligono que vive en la RAM de video y al que apunta ISP_BACKGND_T
	(0x005F808C) -- tag en palabras sobre PARAM_BASE en los bits 23-3, skip en
	los 26-24 --. El registro guarda 3 palabras de encabezado (ISP/TSP/TCW) y
	tres vertices de 3+skip palabras; para el caso plano el color empaquetado
	es la ultima palabra del vertice. dcemu lo usa como color de clear, que
	cubre el fondo liso -- lo que ponen pvr_set_bg_color() de KOS y el boot
	ROM, que limpia a 0xBFBFBF para su animacion y sin esto arrancaba sobre
	negro.

	La direccion va por la ventana de 32 bits: medido con el boot ROM, el
	poligono aparece contiguo por la de 32 y revuelto por la de 64.
*/
static void color_de_fondo(float * r, float * g, float * b)
{
	static float	ultimo[3] = { 0.0f, 0.0f, 0.0f };

	/* Diagnostico: pintar el clear de magenta separa "esta superficie se
	   dibujo blanca" de "aqui no se dibujo nada y se ve el fondo". */
	if (getenv("DCEMU_FONDO_MAGENTA"))
	{
		*r = 1.0f; *g = 0.0f; *b = 1.0f;
		return;
	}

	DWORD tag, skip, dir, color = 0;

	*r = ultimo[0];
	*g = ultimo[1];
	*b = ultimo[2];

	if (pvr_isp_backgnd_t == 0)
		return;			/* nunca escrito: negro, como antes */

	tag  = (pvr_isp_backgnd_t >> 3) & 0x1FFFFF;
	skip = (pvr_isp_backgnd_t >> 24) & 0x7;

	/*
		Validar antes de creer. dcemu no escribe la salida del TA en la RAM de
		video, asi que TA_ITP_CURRENT no avanza de verdad; KOS calcula este
		registro restando contra ese puntero y en una de las dos paridades del
		doble buffer la resta le da negativa: queda 0xFF800000, con skip 7 y
		los bits altos encendidos. En el chip ese cuadro dibujaria un fondo de
		basura tapado por la escena; aca el color de clear se ve, asi que un
		valor que no puede ser un plano de fondo real se descarta y se
		conserva el ultimo bueno -- sin esto, medio parque de demos alternaba
		fondos de colores al azar, un buffer si y otro no.
	*/
	if ((pvr_isp_backgnd_t >> 27) != 0 || skip == 0 || skip > 4)
		return;

	/* Encabezado de 3 palabras + primer vertice: el color es su ultima
	   palabra, la 3 + (3 + skip) - 1 del bloque. */
	dir = (pvr_param_base + tag * 4 + (5 + skip) * 4) & 0x007FFFFF;

	memread_fisico(0xA5000000 + dir, &color, 4);

	/* Las primeras veces: de donde salio el color. Si un fondo sale de un
	   color absurdo, esto es lo primero que hay que mirar. */
	if (traza_activa)
	{
		static int vistos = 0;

		if (vistos < 4)
		{
			vistos++;
			fprintf(stderr, "traza: fondo: backgnd_t=%08lx param_base=%08lx"
				" dir=%06lx color=%08lx\n",
				(unsigned long) pvr_isp_backgnd_t,
				(unsigned long) pvr_param_base,
				(unsigned long) dir, (unsigned long) color);
		}
	}

	*r = ((color >> 16) & 0xFF) / 255.0f;
	*g = ((color >>  8) & 0xFF) / 255.0f;
	*b = ((color >>  0) & 0xFF) / 255.0f;

	ultimo[0] = *r;
	ultimo[1] = *g;
	ultimo[2] = *b;
}

void limpiar_pantalla()
{
	float r, g, b;

	/*
		**glClear del buffer de profundidad lo enmascara glDepthMask.** Si la
		ultima tira de la escena anterior venia con "Z Write Disable" puesto -- y
		la geometria translucida siempre lo trae --, la mascara quedo en FALSE y
		el clear no borra nada: la escena siguiente arranca con las
		profundidades de la anterior y descarta lo que caiga detras de ellas.
	*/
	glDepthMask(GL_TRUE);

	/* El alfa queda en 0: el blend por DSTALPHA de pvr-fb_tex cuenta con que
	   las zonas sin dibujar arranquen transparentes. */
	color_de_fondo(&r, &g, &b);
	glClearColor(r, g, b, 0.0f);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

typedef struct pvr_bkg_poly {
        DWORD	flags1, flags2;
        DWORD	dummy;
        float	x1, y1, z1;
        DWORD	argb1;
        float	x2, y2, z2;
        DWORD	argb2;
        float	x3, y3, z3;
        DWORD 	argb3;
} pvr_bkg_poly_t;

void cb_renderstart(DWORD addr, void * p, size_t size)
{
	pvr_bkg_poly_t bkg;
	DWORD bgplane = 0;

	if (traza_activa)
		fprintf(stderr, "traza: STARTRENDER: hechas=%02x habilitadas=%02x tiras=%d\n",
			(unsigned) pvr_listdone, (unsigned) pvr_registered, strip_count);

	logxmsg(LOG_PVR, "tag address: %08x\n", (pvr_isp_backgnd_t >> 3) & 0x1FFFFF);
	logxmsg(LOG_PVR, "param_base:  %08x\n", (pvr_param_base));

	if (bgplane > 0)
	{
    	/* bgplane sale de los registros del PVR: ya es una direccion resuelta. */
	memread_fisico(bgplane, &bkg, sizeof(pvr_bkg_poly_t));

    	logxmsg(LOG_PVR, "bkg: (%f,%f,%f), (%f,%f,%f), (%f,%f,%f)\n",
    		bkg.x1, bkg.y1, bkg.z1,
    		bkg.x2, bkg.y2, bkg.z2,
    		bkg.x3, bkg.y3, bkg.z3);
	}
}

/*
	Ordena las tiras por tipo de lista, ascendente: opaca (0) primero y
	translucida (2) despues, que es el orden en que hay que dibujarlas.

	**El desempate importa y no estaba.** qsort no es estable: dos tiras del
	mismo tipo quedaban en un orden que depende del estado interno del
	algoritmo, o sea distinto de un cuadro al siguiente aunque la escena fuera
	identica. Para geometria opaca da igual -- decide el test de profundidad --,
	pero la translucida se dibuja sin escribir Z y el resultado depende del
	orden: el mismo cuadro salia distinto cada vez y la pantalla parpadeaba.

	El PVR dibuja dentro de una lista en el orden en que el guest la entrego, y
	ese orden es el de `index`, que solo crece dentro de un cuadro. Asi que
	desempatar por index es a la vez estabilizar el sort y respetar al chip.
*/
/* 1 si el guest apago el autosort de la lista translucida (ISP_FEED_CFG bit
   0, "pre-sort mode"): entonces el orden de envio ES el orden. Lo lee
   cb_tastart() al empezar cada escena. */
static int trans_presort = 0;

int compare(const void *  f,const void * s)
{
	const TSI * a = (const TSI *) f;
	const TSI * b = (const TSI *) s;

	if (a->type != b->type)
		return (a->type > b->type) ? 1 : -1;

	/*
		El autosort de la lista translucida. El chip ordena esa lista por
		profundidad **por pixel** y el orden de envio no significa nada; dcemu
		aproxima por tira: de lejos (z chica, 1/w) a cerca, para que cada capa
		se mezcle sobre la anterior. Salio del menu de Crazy Taxi, medido: la
		pastilla (alfa 0.79, z 0.99) entra a la lista ANTES que la llama del
		logo que tiene detras (alfa 0.49, z 0.014), y en orden de envio la
		llama caia encima de la pastilla y la ensuciaba. Con ISP_FEED_CFG en
		pre-sort se respeta el envio, que es lo que ese modo significa.

		Solo la lista translucida (tipo 2): la opaca y el punch-through
		resuelven por z-test, y ordenarlos daria lo mismo.
	*/
	if (a->type == 2 && !trans_presort && a->prof != b->prof)
		return (a->prof > b->prof) ? 1 : -1;

	if (a->index != b->index)
		return (a->index > b->index) ? 1 : -1;

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Volumenes modificadores                                                   */
/* ------------------------------------------------------------------------ */

/*
	El PVR calcula, pixel por pixel, si esta dentro del volumen, y con eso elige
	entre los dos juegos de parametros del poligono. En OpenGL de funcion fija
	eso es el buffer de plantilla.

	**La cuenta es por caras contra la profundidad de la escena, como en el
	chip.** Un pixel esta dentro del volumen si entre el ojo y su superficie
	las caras del volumen que miran hacia aca y las que miran hacia alla no se
	compensan: las que pasan la prueba de profundidad (GL_GREATER: mas cerca
	que la superficie) suman en un sentido y restan en el otro, y dentro =
	cuenta != 0. Probar contra != 0 vuelve ademas irrelevante cual de los dos
	sentidos de giro es "la cara delantera": en un volumen cerrado los cruces
	se cancelan de a pares y en uno abierto -- el cuadrado plano de las demos
	de KOS -- queda +-1.

	La version anterior era una union de triangulos en pantalla, sin mirar la
	profundidad: alcanzaba para ese cuadrado plano, pero la sombra extruida del
	taxi de Crazy Taxi marcaba todo lo que sus caras cubrieran -- el techo del
	propio taxi oscurecido por su sombra y un manto sobre media pantalla.

	Contar contra la profundidad obliga a marcar DESPUES de resolverla, no
	antes de dibujar: ver el orden en dibujar_escena(). La plantilla entera es
	de una sola lista de volumen a la vez -- se marca la 1 para la tanda opaca
	y se re-marca la 3 para la translucida -- asi la cuenta tiene los 8 bits.

	La instruccion 2 ("cerrar excluyendo") sigue siendo la aproximacion de
	antes -- pone en cero lo que cubre, ahora tambien solo delante de la
	superficie -- porque ninguna demo nuestra la ejercita.
*/

/* Los WRAP son de GL 1.4 y el gl.h de MSVC es 1.1; todo driver los trae. Sin
   wrap la cuenta se arruina cuando una cara trasera rasteriza antes que su
   delantera: GL_DECR en 0 se queda en 0 y el par ya no se cancela. */
#ifndef GL_INCR_WRAP
#define GL_INCR_WRAP	0x8507
#define GL_DECR_WRAP	0x8508
#endif

/* Hay triangulos de volumen de esta lista? (1 opaca, 3 translucida) */
static int hay_volumen_de(DWORD lista)
{
	DWORD v;

	for (v = 0; v < vol_count; v++)
		if (VolumeBuffer[v].lista == lista)
			return 1;

	return 0;
}

/*
	Cuenta en la plantilla el volumen de `lista` contra la profundidad que ya
	quedo en el buffer. Deja cuenta != 0 en los pixeles cuya superficie esta
	dentro del volumen.
*/
static void marcar_volumenes(DWORD lista)
{
	int paso;
	DWORD v;

	glEnable(GL_STENCIL_TEST);
	glStencilMask(0xFF);
	glClearStencil(0);
	glClear(GL_STENCIL_BUFFER_BIT);

	/* Solo la plantilla: ni color ni profundidad. El volumen no se ve, pero
	   SI se compara: "delante de la superficie" es GL_GREATER, la misma
	   convencion de las tiras (mas grande = mas cerca). */
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GREATER);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	for (paso = 0; paso < 2; paso++)
	{
		glCullFace(paso ? GL_FRONT : GL_BACK);
		glStencilOp(GL_KEEP, GL_KEEP, paso ? GL_DECR_WRAP : GL_INCR_WRAP);

		glBegin(GL_TRIANGLES);

		for (v = 0; v < vol_count; v++)
		{
			const VolTri * t = &VolumeBuffer[v];
			int k;

			if (t->lista != lista || t->instruccion == 2)
				continue;

			for (k = 0; k < 3; k++)
				glVertex3f(t->x[k], t->y[k], t->z[k]);
		}

		glEnd();
	}

	/* La exclusion, si la hay, despues de la cuenta y sin culling. */
	glDisable(GL_CULL_FACE);
	glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);

	glBegin(GL_TRIANGLES);

	for (v = 0; v < vol_count; v++)
	{
		const VolTri * t = &VolumeBuffer[v];
		int k;

		if (t->lista != lista || t->instruccion != 2)
			continue;

		for (k = 0; k < 3; k++)
			glVertex3f(t->x[k], t->y[k], t->z[k]);
	}

	glEnd();

	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

/*
	Deja la prueba de plantilla como la necesita la tira `i`: `dentro` en 0
	dibuja fuera del volumen (cuenta 0) y en 1 dentro (cuenta != 0). Una tira
	que ningun volumen afecta se dibuja entera. `hay_volumen` es el de la
	lista que rige la fase en curso: la plantilla vale para una a la vez.
*/
static void plantilla_para(int i, int dentro, int hay_volumen)
{
	if (!TriangleStrip[i].volumen || !hay_volumen)
	{
		glDisable(GL_STENCIL_TEST);
		return;
	}

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(dentro ? GL_NOTEQUAL : GL_EQUAL, 0, 0xFF);
}

/*
	Apunta los arreglos de color y de coordenadas de textura al juego 0 o al 1.
	Las posiciones no cambian: es la misma geometria.
*/
static void juego_de_parametros(int juego)
{
	if (juego)
	{
		glColorPointer   (4, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].r1);
		glTexCoordPointer(4, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].u1);
	}
	else
	{
		glColorPointer   (4, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].r);
		glTexCoordPointer(4, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].t1);
	}
}

/* Las tiras que trajo cada una de las ultimas escenas, y cuantas hubo. */
#define TRAZA_ULTIMAS	12

static int traza_rendidas = 0;
static int traza_ultimas[TRAZA_ULTIMAS];

void traza_ta_resumen(void)
{
	int i, desde, n;

	if (!traza_activa)
		return;

	n = (traza_rendidas < TRAZA_ULTIMAS) ? traza_rendidas : TRAZA_ULTIMAS;
	desde = traza_rendidas - n;

	fprintf(stderr, "traza: %d escenas rendidas; tiras de las ultimas %d:",
		traza_rendidas, n);

	for (i = 0; i < n; i++)
		fprintf(stderr, " %d", traza_ultimas[(desde + i) % TRAZA_ULTIMAS]);

	fprintf(stderr, "\n");

	/* Y por que ventana entro cada byte a la RAM de video. En el resumen y no
	   solo en la traza de framebuffer, que una demo del PVR no dibuja por ahi
	   y entonces no se veia nunca. La de 32 bits (0x05, 0x13) deja el bloque
	   plano; la de 64 (0x04, 0x11) lo reparte entre los dos bancos. */
	fprintf(stderr, "traza: bytes a RAM de video por ventana:");

	for (i = 0; i < 0x20; i++)
		if (traza_video_por_ventana[i])
			fprintf(stderr, " %02x=%u", i,
				(unsigned) traza_video_por_ventana[i]);

	fprintf(stderr, "\n");

	/* En que rango vive la profundidad del guest. Es lo que dimensiona
	   profundidad_ta(): un juego con camara manda 1/w de milesimas. */
	if (traza_ta_z_visto)
		fprintf(stderr, "traza: z del TA (1/w crudo) en toda la corrida: %g..%g\n",
			traza_ta_z_crudo[0], traza_ta_z_crudo[1]);
}

static void dibujar_escena(void);
static void terminar_escena(void);
static int  render_a_textura(void);
static void volcar_a_memoria(DWORD destino, DWORD dst_w, DWORD dst_h,
							 DWORD filas_bytes, int formato,
							 DWORD src_w, DWORD src_h, int ventana64);
static void volcar_escena_a_framebuffer(void);

void cb_tastart(DWORD addr, void * p, size_t size)
{
	if (traza_activa)
	{
		traza_ultimas[traza_rendidas % TRAZA_ULTIMAS] = (int) strip_count;
		traza_rendidas++;
	}

	/* Con --traza-mem, reportar las primeras rendidas **con geometria**:
	   cuantas tiras llegaron y cuantos vertices. Es la pregunta que hay que
	   contestar antes de mirar matrices o test de profundidad -- si no llega
	   geometria, el resto sobra. Las escenas vacias no cuentan: el boot ROM
	   rinde varias antes de mandar el primer poligono y se comian la cuota. */
	if (traza_activa)
	{
		static int rendidas = 0;

		if (rendidas < 3 && strip_count > 0)
		{
			int t;

			fprintf(stderr, "traza: render %d: %d tiras, %d vertices en buffer, "
				"%d vertices vistos, %d con fin-de-tira, %d triangulos de volumen\n",
				rendidas, (int) strip_count, (int) total_polygon_count,
				traza_ta_vertices, traza_ta_fin_tira, (int) vol_count);

			fprintf(stderr, "traza:   tipos de vertice:");
			for (t = 0; t < TA_TIPOS; t++)
				if (traza_ta_tipos[t])
					fprintf(stderr, " [%d]=%d", t, traza_ta_tipos[t]);
			fprintf(stderr, "\n");

			/* prof es la z ya pasada por profundidad_ta(); el 1/w crudo de
			   toda la corrida sale en el resumen final. */
			fprintf(stderr, "traza:   encuadre: x %.1f..%.1f  y %.1f..%.1f  prof %g..%g"
				"  (pantalla %dx%d)\n",
				traza_ta_min[0], traza_ta_max[0],
				traza_ta_min[1], traza_ta_max[1],
				traza_ta_min[2], traza_ta_max[2],
				screenancho, screenheight);

			rendidas++;
		}

		traza_ta_vertices = 0;
		traza_ta_fin_tira = 0;
		memset(traza_ta_tipos, 0, sizeof(traza_ta_tipos));
	}

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

	DWORD dw;
	
	// transparent polygons should appear first

	/* La llave del autosort de la lista translucida: la z mas cercana de cada
	   tira, y si el guest pidio pre-sort (ISP_FEED_CFG, bit 0). Ver compare(). */
	{
		DWORD feed = 0, si, k;

		memread_fisico(0xA05F8098, &feed, 4);
		trans_presort = (int) (feed & 1);

		for (si = 0; si < strip_count; si++)
		{
			float m = VertexBuffer[TriangleStrip[si].index].z;

			for (k = 1; k < TriangleStrip[si].count; k++)
			{
				float z = VertexBuffer[TriangleStrip[si].index + k].z;

				if (z > m)
					m = z;
			}

			TriangleStrip[si].prof = m;
		}
	}

	qsort(TriangleStrip,strip_count,sizeof(TSI),compare);

	/* El estado con que sale cada tira. Si la geometria llega bien y no se ve
	   nada, esto es lo que queda por mirar: es lo que descubrio que a conio se
	   le iba la pantalla entera en el culling.

	   Ademas de las dos primeras escenas con geometria, DCEMU_TRAZA_ESCENA=N[:M]
	   en el ambiente vuelca las M escenas (1 por omision) desde la numero N
	   --la cuenta de traza_rendidas, la misma del resumen--, y esas van
	   completas, no cortadas a 8 tiras: es como se le pregunta a UNA escena de
	   un juego que estado llevaba cada quad. Con DCEMU_CAPTURA_TODAS al lado,
	   el numero de cuadro capturado corre casi parejo con el de escena. */
	if (traza_activa && strip_count > 0)
	{
		static int volcadas = 0;
		static int obj_desde = -2, obj_hasta = -2;		/* -2: sin leer */
		int escena = traza_rendidas - 1;
		int pedida;

		if (obj_desde == -2)
		{
			const char * e = getenv("DCEMU_TRAZA_ESCENA");

			if (e != NULL)
			{
				const char * p = strchr(e, ':');

				obj_desde = atoi(e);
				obj_hasta = obj_desde + ((p != NULL) ? atoi(p + 1) : 1) - 1;
			}
			else
				obj_desde = obj_hasta = -1;				/* no pedida */
		}

		pedida = (escena >= obj_desde && escena <= obj_hasta);

		if (volcadas < 2 || pedida)
		{
			int t;
			int tope = pedida ? (int) strip_count : 8;

			if (pedida)
			{
				DWORD ref = 0;

				memread_fisico(0xA05F811C, &ref, 4);
				fprintf(stderr, "traza: escena %d: %d tiras, PT_ALPHA_REF=%02lx\n",
					escena, (int) strip_count, (unsigned long) (ref & 0xFF));
			}

			for (t = 0; t < strip_count && t < tope; t++)
			{
				fprintf(stderr, "traza:   tira %d: tipo=%d idx=%d n=%d alpha=%d "
					"blend=%04x/%04x zfunc=%04x zwrite=%d cull=%d tex=%08x %dx%d tw=%d vq=%d mip=%d fmt=%04x "
					"bpp=%d stride=%d env=%d vol=%d tcw=%08lx\n",
					t, (int) TriangleStrip[t].type, (int) TriangleStrip[t].index,
					(int) TriangleStrip[t].count, (int) TriangleStrip[t].alpha,
					(unsigned) TriangleStrip[t].pvr_srcblend,
					(unsigned) TriangleStrip[t].pvr_dstblend,
					(unsigned) TriangleStrip[t].depthmode,
					(int) TriangleStrip[t].zwrite, (int) TriangleStrip[t].culling,
					(unsigned) TriangleStrip[t].texture.surface,
					(int) TriangleStrip[t].texture.pvr_texture_size_usize,
					(int) TriangleStrip[t].texture.pvr_texture_size_vsize,
					(int) TriangleStrip[t].texture.twiddled,
					(int) TriangleStrip[t].texture.vq,
					(int) TriangleStrip[t].texture.mipmapped,
					(unsigned) TriangleStrip[t].texture.pvr_texture_pixelpack,
					(int) TriangleStrip[t].texture.pvr_texture_bpp,
					(int) TriangleStrip[t].texture.pvr_texture_stride,
					(int) TriangleStrip[t].texture.pvr_texture_env,
					(int) TriangleStrip[t].volumen,
					(unsigned long) TriangleStrip[t].texture.tcw_crudo);

				/* Las esquinas y con que coordenadas de textura se muestrean.
				   Los cuatro vertices y no solo el primero: un cuadrilatero mal
				   armado -- un sprite, por ejemplo -- se ve aca y no en el v0. */
				{
					DWORD k;

					for (k = 0; k < TriangleStrip[t].count && k < 4; k++)
					{
						DWORD ix = TriangleStrip[t].index + k;

						fprintf(stderr, "traza:     v%d=(%.1f,%.1f,%g) uv=(%.3f,%.3f) rgba=(%.2f,%.2f,%.2f,%.2f)\n",
							(int) k,
							VertexBuffer[ix].x, VertexBuffer[ix].y, VertexBuffer[ix].z,
							VertexBuffer[ix].t1, VertexBuffer[ix].t2,
							VertexBuffer[ix].r, VertexBuffer[ix].g,
							VertexBuffer[ix].b, VertexBuffer[ix].a);
					}
				}
			}

			if (!pedida)
				volcadas++;
		}
	}

	/*
		Si esta escena iba a una textura, ya se dibujo y se escribio en la RAM
		de video: no hay nada que presentar en pantalla.
	*/
	if (render_a_textura())
	{
		strip_count = 0;
		total_polygon_count = 0;
		vol_count = 0;
		ta_reiniciar();
		pvr_listdone = 0;
		/* La cache de texturas ya no se vacia por escena: es persistente y
		   se invalida por generaciones (vram.h). La textura que este render
		   acaba de escribir se detecta sola por su huella. */

		/* El final del render, tambien para el render a textura: en el chip
		   RENDERDONE es del RENDER, no de la pantalla. */
		/* Los tres finales del render que lista el documento (SB_ISTNRM bits
	   0, 1 y 2: TSP, ISP y Video): el chip los emite al terminar de
	   rasterizar, ~milisegundos despues de STARTRENDER. Virtua Tennis
	   tiene handler para el 0 y espera su efecto. */
	intc_add((1 << 0) | (1 << 1) | ASIC_EVT_PVR_RENDERDONE, 40000);
		return;
	}

	/*
		El clear va aca, al empezar la escena, y no al presentar la anterior:
		el chip fija su configuracion en STARTRENDER, y el plano de fondo se
		reprograma dos veces por cuadro -- muestrearlo al final del cuadro
		anterior caia en medio de esa reprogramacion y el color salia de un
		valor a mitad de camino, distinto en cada buffer: fondos que
		alternaban colores de basura en casi todos los demos de KOS.
	*/
	limpiar_pantalla();

	dibujar_escena();
	volcar_escena_a_framebuffer();
	terminar_escena();

	/*
		El final del render. En el chip lo emite el ISP/TSP cuando termino de
		rasterizar lo que STARTRENDER le pidio -- incondicional, cerrara el
		guest las listas que cerrara --, un tiempo despues del arranque. La
		demora importa: un juego arma su espera del render justo despues de
		escribir STARTRENDER, igual que con el fin de lista.
	*/
	/* Los tres finales del render que lista el documento (SB_ISTNRM bits
	   0, 1 y 2: TSP, ISP y Video): el chip los emite al terminar de
	   rasterizar, ~milisegundos despues de STARTRENDER. Virtua Tennis
	   tiene handler para el 0 y espera su efecto. */
	intc_add((1 << 0) | (1 << 1) | ASIC_EVT_PVR_RENDERDONE, 40000);
}

/*
	Dibuja las tiras acumuladas. Sale de cb_tastart() porque el render a
	textura necesita exactamente lo mismo pero con otro destino y otro tamano.
*/
/*
	La niebla de tabla del PVR (bits 23-22 del TSP). El guest escribe la
	densidad en FOG_DENSITY (0x005F80B8, un flotante de 16 bits: mantisa
	1.m7 en los bits 14-8 y exponente con signo en 7-0), el color en
	FOG_COL_TABLE (0x005F80B0) y los 128 alfas de la curva en
	0x005F8200-0x005F83FC. Todo caia en el respaldo de control_mem sin que
	nadie lo leyera -- la misma forma de agujero de siempre -- y por eso el
	tunel de kgl-tunnel terminaba en un pozo negro en vez de desvanecerse
	en el gris de su niebla.

	El indice de la tabla es v = densidad * (1/w) -- el mismo 1/w que el TA
	trae como z -- recortado a [1, 256): exponente en los bits altos de la
	ranura, mantisa de 4 bits en los bajos, y cada palabra trae el alfa del
	borde lejano de la ranura en el byte alto y el del cercano en el bajo,
	que el chip interpola con la fraccion. KOS llena esa curva en
	pvr_fog_table_exp2() y afines.

	dcemu la evalua POR VERTICE con el q que el vertice ya guarda, y la
	aplica como una segunda pasada del mismo triangulo mezclada hacia el
	color de la tabla. La pasada reusa la profundidad que la tira acaba de
	dejar: GL_EQUAL si escribio z, y la misma prueba si no la escribio --
	pasa exactamente donde paso la original -- sin escribir el buffer. El
	resto del estado lo repone la vuelta siguiente del bucle; el puntero de
	color es el unico que se programa afuera, y lo devuelve
	juego_de_parametros(0).
*/
static float niebla_rgba[65000][4];

static void dibujar_niebla_tira(DWORD i)
{
	DWORD	reg = 0, col = 0, palabra = 0, k, alguna = 0;
	float	densidad, fr, fg, fb, v, m16, frac, a;
	int	e, m;

	if (TriangleStrip[i].niebla != 0 && TriangleStrip[i].niebla != 3)
		return;

	/* Diagnostico: apaga la pasada de niebla sin tocar nada mas. */
	if (getenv("DCEMU_SIN_NIEBLA"))
		return;

	memread_fisico(0xA05F80B8, &reg, 4);
	memread_fisico(0xA05F80B0, &col, 4);

	/* El formato del registro (System Architecture, FOG_DENSITY): mantisa de
	   8 bits donde el bit 15 ES el bit "1.0" -- 0xFF vale 255/128, sin 1
	   implicito -- y exponente en complemento a dos. KOS lo confirma por el
	   camino raro: float16() arma signo|mantisa7|exponente y "niega" la
	   densidad, con lo que el bit de signo cae exactamente donde el chip
	   espera el bit 1.0. */
	densidad = ldexpf((float) ((reg >> 8) & 0xFF) / 128.0f,
		(int) (signed char) (reg & 0xFF));
	fr = (float) ((col >> 16) & 0xFF) / 255.0f;
	fg = (float) ((col >> 8) & 0xFF) / 255.0f;
	fb = (float) (col & 0xFF) / 255.0f;

	for (k = 0; k < TriangleStrip[i].count; k++)
	{
		vertex * vp = &VertexBuffer[TriangleStrip[i].index + k];

		v = densidad * vp->q;

		if (v < 1.0f)
			v = 1.0f;
		if (v > 255.9999f)
			v = 255.9999f;

		e = 0;
		while (v >= 2.0f)
		{
			v *= 0.5f;
			e++;
		}

		m16 = (v - 1.0f) * 16.0f;
		m = (int) m16;
		frac = m16 - (float) m;

		memread_fisico(0xA05F8200 + (e * 16 + m) * 4, &palabra, 4);
		a = ((float) ((palabra >> 8) & 0xFF) * (1.0f - frac)
			+ (float) (palabra & 0xFF) * frac) / 255.0f;

		niebla_rgba[TriangleStrip[i].index + k][0] = fr;
		niebla_rgba[TriangleStrip[i].index + k][1] = fg;
		niebla_rgba[TriangleStrip[i].index + k][2] = fb;
		niebla_rgba[TriangleStrip[i].index + k][3] = a;

		if (a > 0.0f)
			alguna = 1;
	}

	/* Una tabla que nadie escribio es todo ceros: nada que mezclar. */
	if (!alguna)
		return;

	if (opciones.traza_mem)
	{
		static DWORD avisos = 0;

		if (avisos < 8)
		{
			avisos++;
			fprintf(stderr, "niebla: tira %lu (lista %lu, %lu vertices) "
				"densidad %g alfa[0] %g\n",
				(unsigned long) i, (unsigned long) TriangleStrip[i].type,
				(unsigned long) TriangleStrip[i].count, densidad,
				niebla_rgba[TriangleStrip[i].index][3]);
		}
	}

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	if (!TriangleStrip[i].zwrite)
		glDepthFunc(GL_EQUAL);

	glColorPointer(4, GL_FLOAT, 0, niebla_rgba);
	glDrawArrays(GL_TRIANGLE_STRIP, TriangleStrip[i].index, TriangleStrip[i].count);

	juego_de_parametros(0);
}

/*
	Programa todo el estado de GL que pide la tira `i` -- profundidad, culling,
	escritura de z, mezcla, prueba de alfa y textura. Extraido del bucle de
	dibujar_escena() para poder repetirlo en la segunda pasada de los
	volumenes, que recorre las tiras afectadas fuera de ese bucle.
*/
static void tira_estado(DWORD i)
{
			glDepthFunc(TriangleStrip[i].depthmode);

			/* Iban indexadas con strip_count, que en este bucle ya es la
			   cantidad total: uno mas que el ultimo indice valido. O sea que
			   culling y zwrite salian de un elemento sin escribir y se
			   aplicaban igual a todas las tiras. El indice es i. */
			/*
				El campo de culling del PVR tiene cuatro valores: 0 sin culling,
				1 "cull if small" -- un umbral de area, no una prueba de sentido
				de giro, asi que GL no tiene equivalente --, 2 descarta el area
				negativa y 3 la positiva. O sea que 2 y 3 son la misma prueba con
				el sentido de giro opuesto.

				Antes cualquier valor distinto de cero se trataba como
				glCullFace(GL_BACK) con el GL_CCW por omision de GL, y eso
				descartaba la pantalla entera de conio, que manda todo con
				culling 2. El TA entrega coordenadas de pantalla con la y hacia
				abajo y el glOrtho() de screeninit() la invierte, asi que el giro
				que ve GL sale al revés del que supone el PVR: hay que decirle
				cual es la cara delantera en vez de dejar la de por omision.
			*/
			switch(TriangleStrip[i].culling)
			{
				case 2:
				glEnable(GL_CULL_FACE);
				glCullFace(GL_BACK);
				glFrontFace(GL_CW);
				break;

				case 3:
				glEnable(GL_CULL_FACE);
				glCullFace(GL_BACK);
				glFrontFace(GL_CCW);
				break;

				default:	/* 0 sin culling, 1 sin equivalente en GL */
				glDisable(GL_CULL_FACE);
				break;
			}

			/* El campo lleva el bit "Z Write Disable" de la palabra ISP del
			   PVR, asi que en 1 hay que APAGAR la escritura de profundidad. */
			if(TriangleStrip[i].zwrite)
			{
				glDepthMask(GL_FALSE);
			}
			else
			{
				glDepthMask(GL_TRUE);
			}
	

			/*
				La mezcla es cosa de la lista translucida, y su configuracion
				son los factores del TSP: ONE/ZERO ya es "sin mezcla" por si
				mismo. El interruptor era `alpha`, que es el bit 20 del TSP --
				"Use Alpha", que solo fuerza a 1.0 el alfa DEL VERTICE -- y
				eso costo los arboles de Crazy Taxi: hojas ARGB1555 en la
				lista translucida con use-alpha apagado y factores srcalpha,
				que el chip mezcla (el alfa de la textura sigue vivo) y aca
				salian opacas, con el fondo alfa-0 como caja negra. Las listas
				opaca y punch-through no mezclan en el chip.
			*/
			if (TriangleStrip[i].type == 2 || TriangleStrip[i].type == 1)
			{
				glEnable(GL_BLEND);
			}
			else
			{
				glDisable(GL_BLEND);
			}

			glBlendFunc(TriangleStrip[i].pvr_srcblend, TriangleStrip[i].pvr_dstblend);

			/*
				Lo que distingue al punch-through en el chip es que descarta el
				fragmento por alfa -- pasa lo que supera el umbral de
				PT_ALPHA_REF (0x005F811C, bits 7-0) y el resto no se dibuja --,
				y eso es lo que le permite escribir Z como la lista opaca. Sin
				el descarte, el fondo con alfa 0 de un cartel recortado -- los
				arboles de Crazy Taxi, una reja -- sale como caja negra opaca.
				En las otras listas no corre: la opaca ignora el alfa y la
				translucida lo mezcla.

				La regla exacta salio de dos medidas que se contradicen con una
				sola desigualdad. El menu de Crazy Taxi apaga sus pastillas
				mandandolas punch-through con textura RGB565 -- sin canal
				alfa -- en modulate-alpha y el alfa del vertice en 0, con
				PT_ALPHA_REF en 0: alfa 0 contra umbral 0 tiene que
				DESCARTARSE (con >= pasaba y las pastillas salian grises). En
				el juego el mundo entero es punch-through con alfa 1.0 y
				PT_ALPHA_REF alto: 1.0 contra 1.0 tiene que PASAR (con > se
				descartaba la pasada de textura del piso, los edificios y las
				palmeras, y quedaba la geometria de respaldo: el mundo
				blanco). O sea que el chip pasa "alfa >= umbral, y ademas
				distinto de cero": en GL de funcion fija eso es GEQUAL contra
				el umbral con un piso de medio paso, que solo actua cuando el
				umbral es 0. El alfa comparado es el ya modulado, asi que una
				textura sin canal alfa se recorta igual por el del vertice.
			*/
			if (TriangleStrip[i].type == 0 && !getenv("DCEMU_SIN_ALPHATEST"))
			{
				DWORD	ref = 0;
				GLfloat	umbral;

				memread_fisico(0xA05F811C, &ref, 4);
				umbral = (GLfloat) (ref & 0xFF) / 255.0f;

				if (umbral < 0.5f / 255.0f)
					umbral = 0.5f / 255.0f;

				glEnable(GL_ALPHA_TEST);
				glAlphaFunc(GL_GEQUAL, umbral);
			}
			else
				glDisable(GL_ALPHA_TEST);

			if(TriangleStrip[i].texture.surface)
			{
				glEnable(GL_TEXTURE_2D);

				/*
					Los cuatro modos del chip, con su equivalente en GL:

					  0 replace  px = ARGB(tex)                        -> REPLACE
					  1 modulate px = A(tex) + RGB(col)*RGB(tex)       -> MODULATE
					  2 decal    px = RGB(tex)*A(tex) + RGB(col)*(1-A) -> DECAL
					  3 modulate alpha  px = ARGB(col)*ARGB(tex)       -> MODULATE

					Decal es el 2, no el 0: confundirlos manda a modulate una
					superficie cuyo color de vertice es negro y sale toda negra.
				*/
				switch (TriangleStrip[i].texture.pvr_texture_env)
				{
					case 0:
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
					break;

					case 2:
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
					break;

					default:
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
					break;
				}

				/* Los filtros los pone get_texture(), **despues** de ligar la
				   textura: glTexParameteri afecta a la que este ligada en ese
				   momento, y aca todavia lo esta la del cuadro anterior. Ver
				   el comentario en get_texture(). */
				get_texture(TriangleStrip[i].texture.pvr_texture_size_usize, TriangleStrip[i].texture.pvr_texture_size_vsize,
				TriangleStrip[i].texture.surface, TriangleStrip[i].texture.twiddled, TriangleStrip[i].texture.vq,i);
			}
			else
			{
				glDisable(GL_TEXTURE_2D);
			}

}

static void dibujar_escena(void)
{
	DWORD i;
	int vol_opaca, vol_trans;

	/* DCEMU_SIN_VOLUMEN: descarta los volumenes modificadores de la escena,
	   como si ninguna tira los trajera. Diagnostico para A.5: si el mundo
	   blanco de Crazy Taxi depende de esto, la culpa es de la segunda
	   pasada o del marcado de la plantilla. */
	if (getenv("DCEMU_SIN_VOLUMEN"))
		vol_count = 0;

	vol_opaca = hay_volumen_de(1);
	vol_trans = hay_volumen_de(3);

	/*
		El orden viene de como resuelve el chip, que primero deja la
		profundidad y recien despues decide que pixel cae dentro de que
		volumen: (1) la tanda opaca y punch-through con el juego 0, que es la
		que escribe z; (2) el conteo del volumen de la lista 1 contra esa z y
		el repintado de las tiras afectadas donde la cuenta dio dentro; (3) el
		volumen de la lista 3 re-marcado sobre la misma z; (4) la tanda
		translucida. Marcar antes de dibujar -- lo que se hacia -- no tiene
		contra que comparar: era la union en pantalla que oscurecia el techo
		del taxi con su propia sombra.
	*/

	/* (1) Opaca y punch-through, enteras y con el juego 0. */
	for (i = 0; i < strip_count; i++)
	{
		if (TriangleStrip[i].count == 0 || TriangleStrip[i].type == 2)
			continue;

		glDisable(GL_STENCIL_TEST);
		tira_estado(i);
		glDrawArrays(GL_TRIANGLE_STRIP, TriangleStrip[i].index, TriangleStrip[i].count);
		dibujar_niebla_tira(i);
	}

	/* (2) Dentro del volumen opaco: la misma geometria con el juego 1 (en
	   sombra barata, el 0 escalado por FPU_SHAD_SCALE). Estas listas no
	   mezclan, asi que repintar solo los pixeles de dentro es seguro.
	   GL_EQUAL contra la z que la propia tira dejo limita la pasada a donde
	   esa tira sigue visible; si la tira no escribio z, repite su prueba. */
	if (vol_opaca)
	{
		marcar_volumenes(1);

		for (i = 0; i < strip_count; i++)
		{
			if (TriangleStrip[i].count == 0 || TriangleStrip[i].type == 2
			||  !TriangleStrip[i].volumen)
				continue;

			tira_estado(i);
			glDepthFunc(TriangleStrip[i].zwrite ? TriangleStrip[i].depthmode
											    : GL_EQUAL);
			glDepthMask(GL_FALSE);
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
			juego_de_parametros(1);

			glDrawArrays(GL_TRIANGLE_STRIP, TriangleStrip[i].index,
				TriangleStrip[i].count);

			juego_de_parametros(0);
		}
	}

	/* (3) y (4): la tanda translucida, con su volumen re-marcado contra la
	   profundidad que dejo la opaca. */
	if (vol_trans)
		marcar_volumenes(3);

	for (i = 0; i < strip_count; i++)
	{
		if (TriangleStrip[i].count == 0 || TriangleStrip[i].type != 2)
			continue;

		/* Fuera del volumen, o entera si ninguno la afecta. Con mezcla de
		   por medio cada pixel tiene que salir UNA sola vez, asi que la tira
		   afectada se parte en fuera/dentro en vez de repintarse. */
		plantilla_para(i, 0, vol_trans);
		tira_estado(i);
		glDrawArrays(GL_TRIANGLE_STRIP, TriangleStrip[i].index, TriangleStrip[i].count);

		/* Dentro: la misma geometria con el juego 1, aqui mismo para que
		   herede el estado recien programado -- blend, culling, profundidad
		   y la textura ya ligada. */
		if (TriangleStrip[i].volumen && vol_trans)
		{
			plantilla_para(i, 1, vol_trans);
			juego_de_parametros(1);

			glDrawArrays(GL_TRIANGLE_STRIP, TriangleStrip[i].index, TriangleStrip[i].count);

			juego_de_parametros(0);
		}

		dibujar_niebla_tira(i);
	}

	glDisable(GL_STENCIL_TEST);
}

/*
	Render a textura.

	Normalmente el chip rinde la escena en el framebuffer; con
	pvr_scene_begin_rtt() el destino es memoria de textura, y lo que la marca es
	**el bit 24 de FB_W_SOF1** (0x005F8060) -- KOS escribe `direccion | BIT(24)`.
	El tamano sale de los registros de recorte (PCLIP_X/Y, 0x005F8068 y 0x6C, con
	el maximo en los bits 31-16), el paso entre filas de FB_W_LINESTRIDE
	(0x005F804C, en unidades de 8 bytes) y el formato de FB_W_CTRL (0x005F8048).

	dcemu manda el 3D a OpenGL, asi que la escena no pasa por la RAM de video: hay
	que dibujarla y leerla de vuelta con glReadPixels. El guest entrega los
	vertices en coordenadas del destino -- 0..128 por 0..64 para la demo --, asi
	que el viewport y el glOrtho tienen que ser los de la textura y no los de la
	pantalla, o la escena sale minuscula en una esquina.

	Devuelve 1 si esta escena era para una textura y ya se resolvio.
*/

/*
	Lee lo que GL rasterizo y lo escribe en la RAM de video en el formato del
	PVR. Lo usan las dos rutas que necesitan que la escena exista en memoria:
	el render a textura y el volcado del framebuffer.

	`src_*` es el rectangulo de GL que se lee y `dst_*` el tamano con que se
	guarda; cuando no coinciden se remuestrea por vecino mas cercano, que es lo
	que hace falta porque la ventana no mide lo mismo que la pantalla emulada.

	`ventana64` dice en que numeracion esta `destino`. El render a textura va
	por la de 64 bits: el bit 24 de FB_W_SOF1 significa escribir por ese
	camino, y KOS le pasa la direccion de la textura tal cual (`to_txr_addr |
	BIT(24)`, pvr_misc.c), asi que se reparte con vram64_escribir() para que
	get_texture() junte esos mismos bytes por la misma numeracion. El
	framebuffer va por la de 32, plana, como en el chip. Ver vram.h.
*/
static void volcar_a_memoria(DWORD destino, DWORD dst_w, DWORD dst_h,
							 DWORD filas_bytes, int formato,
							 DWORD src_w, DWORD src_h, int ventana64)
{
	unsigned char *	pixeles;
	WORD *			texeles;
	DWORD			x, y;

	if (dst_w == 0 || dst_h == 0 || src_w == 0 || src_h == 0)
		return;

	pixeles = (unsigned char *) malloc((size_t) src_w * src_h * 4);
	texeles = (WORD *) malloc((size_t) dst_w * sizeof(WORD));

	if (pixeles == NULL || texeles == NULL)
	{
		free(pixeles);
		free(texeles);
		return;
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, src_w, src_h, GL_RGBA, GL_UNSIGNED_BYTE, pixeles);

	/* Para separar "GL no dibujo nada" de "el volcado esta mal": cuantos
	   pixeles no negros hay en lo leido, y cuantos con alfa distinto de FF --
	   si esa segunda cuenta es siempre cero, el contexto no tiene planos de
	   alfa y el blend por DSTALPHA no puede funcionar. Solo los primeros
	   cuadros. */
	if (traza_activa)
	{
		static int vistos = 0;

		if (vistos < 3)
		{
			size_t total = (size_t) src_w * src_h;
			size_t no_negros = 0, alfa_no_ff = 0, k;

			vistos++;

			for (k = 0; k < total; k++)
			{
				const unsigned char * q = pixeles + k * 4;

				if (q[0] | q[1] | q[2])
					no_negros++;
				if (q[3] != 0xFF)
					alfa_no_ff++;
			}

			fprintf(stderr, "traza: volcado %ux%u -> %06x: %lu pixeles no"
				" negros, %lu con alfa != FF\n", src_w, src_h,
				(unsigned) destino, (unsigned long) no_negros,
				(unsigned long) alfa_no_ff);
		}
	}

	if (filas_bytes == 0)
		filas_bytes = dst_w * 2;

	for (y = 0; y < dst_h; y++)
	{
		/* glReadPixels entrega de abajo hacia arriba y tanto una textura como
		   el framebuffer se guardan de arriba hacia abajo. */
		DWORD					sy = (src_h - 1) - (y * src_h) / dst_h;
		const unsigned char *	fila = pixeles + (size_t) sy * src_w * 4;

		for (x = 0; x < dst_w; x++)
		{
			DWORD sx = (x * src_w) / dst_w;
			DWORD r = fila[sx * 4 + 0];
			DWORD g = fila[sx * 4 + 1];
			DWORD b = fila[sx * 4 + 2];
			DWORD a = fila[sx * 4 + 3];

			switch (formato)
			{
				case 0:		/* ARGB1555 */
					texeles[x] = (WORD) (((a >> 7) << 15) | ((r >> 3) << 10) |
										 ((g >> 3) << 5) | (b >> 3));
					break;

				case 2:		/* ARGB4444 */
					texeles[x] = (WORD) (((a >> 4) << 12) | ((r >> 4) << 8) |
										 ((g >> 4) << 4) | (b >> 4));
					break;

				default:	/* 1: RGB565, y el resto se aproxima con el */
					texeles[x] = (WORD) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
					break;
			}
		}

		if (ventana64)
			vram64_escribir(destino + y * filas_bytes, texeles, (size_t) dst_w * 2);
		else
			memwrite_fisico(0xA5000000 + destino + y * filas_bytes,
				texeles, (size_t) dst_w * 2);
	}

	free(pixeles);
	free(texeles);
}

/*
	El volcado de la escena que acaba de dibujarse, cuando esta armado (ver
	volcado_fb_armado). Corre despues de dibujar_escena() y antes de presentar:
	es el unico momento en que el back buffer tiene el cuadro entero. El
	destino y el formato salen de los mismos registros que usa el render a
	textura, solo que sin el bit 24: esto es el framebuffer de verdad, escrito
	por la ventana de 32 bits.
*/
static void volcar_escena_a_framebuffer(void)
{
	DWORD	sof = 0, pclip_x = 0, pclip_y = 0, ctrl = 0, paso = 0;
	DWORD	ancho, alto, filas_bytes;

	if (!volcado_fb_armado)
		return;

	memread_fisico(0xA05F8060, &sof, 4);

	/* Con el bit 24 la escena era para una textura y no llega aca. */
	if (sof & 0x01000000)
		return;

	memread_fisico(0xA05F8068, &pclip_x, 4);
	memread_fisico(0xA05F806C, &pclip_y, 4);
	memread_fisico(0xA05F804C, &paso, 4);
	memread_fisico(0xA05F8048, &ctrl, 4);

	ancho = ((pclip_x >> 16) & 0x7FF) + 1;
	alto  = ((pclip_y >> 16) & 0x3FF) + 1;
	filas_bytes = (paso & 0x1FF) * 8;

	if (ancho == 0 || alto == 0 || ancho > 2048 || alto > 2048)
		return;

	volcar_a_memoria(sof & 0x007FFFFF, ancho, alto, filas_bytes,
		(int) (ctrl & 0x7),
		(DWORD) (outputscreen ? outputscreen->w : 800),
		(DWORD) (outputscreen ? outputscreen->h : 600), 0);
}

static int render_a_textura(void)
{
	DWORD			sof1 = 0, pclip_x = 0, pclip_y = 0, ctrl = 0, paso = 0;
	DWORD			destino, ancho, alto, filas_bytes;
	int				formato;

	memread_fisico(0xA05F8060, &sof1, 4);

	if (!(sof1 & 0x01000000))
		return 0;

	memread_fisico(0xA05F8068, &pclip_x, 4);
	memread_fisico(0xA05F806C, &pclip_y, 4);
	memread_fisico(0xA05F804C, &paso, 4);
	memread_fisico(0xA05F8048, &ctrl, 4);

	destino = sof1 & 0x007FFFFF;
	ancho = ((pclip_x >> 16) & 0x7FF) + 1;
	alto  = ((pclip_y >> 16) & 0x3FF) + 1;
	formato = ctrl & 0x7;

	/* El paso viene en unidades de 8 bytes y cubre la fila entera, que puede ser
	   mas ancha que la imagen: eso es el stride de la textura. */
	filas_bytes = (paso & 0x1FF) * 8;

	if (ancho == 0 || alto == 0 || ancho > 2048 || alto > 2048)
		return 0;

	/* La escena, a su propio tamano. near/far invertidos, como en
	   screeninit(): asi z de vertice creciente da profundidad creciente. */
	glViewport(0, 0, ancho, alto);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glOrtho(0, ancho, alto, 0, PROFUNDIDAD_RANGO, -PROFUNDIDAD_RANGO);

	/* La mascara va **antes** del clear: glClear de la profundidad la respeta.
	   Ver limpiar_pantalla(). */
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

	/* El render a textura tambien arranca del plano de fondo. */
	{
		float r, g, b;

		color_de_fondo(&r, &g, &b);
		glClearColor(r, g, b, 0.0f);
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	dibujar_escena();

	volcar_a_memoria(destino, ancho, alto, filas_bytes, formato, ancho, alto, 1);

	/* Y se deja todo como estaba: la escena siguiente va a la pantalla. */
	glViewport(0, 0, outputscreen ? outputscreen->w : 800,
					 outputscreen ? outputscreen->h : 600);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glOrtho(0, screenancho, screenheight, 0, PROFUNDIDAD_RANGO, -PROFUNDIDAD_RANGO);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (traza_activa)
		fprintf(stderr, "traza: render a textura %ux%u en %06x, paso %u, formato %d\n",
			(unsigned) ancho, (unsigned) alto, (unsigned) destino,
			(unsigned) filas_bytes, formato);

	return 1;
}

/* La segunda mitad de cb_tastart(): presentar y dejar todo listo para la
   escena siguiente. */
static void terminar_escena(void)
{
	DWORD dw;

	/* Antes de tocar strip_count y antes del swap: es el unico momento en que
	   el buffer tiene el cuadro entero, porque despues de presentar se limpia.
	   Sobrescribe el archivo en cada cuadro, asi que al salir queda el ultimo
	   **con geometria**: una demo que dibuja una vez y se queda esperando un
	   boton sigue generando cuadros vacios, y sin esa condicion el volcado
	   terminaba siendo uno de esos. */
	/* Con DCEMU_CAPTURA_TODAS en el ambiente, cada cuadro sale a un archivo
	   numerado en vez de pisar el anterior: es lo que deja mirar una
	   animacion entera -- la del boot ROM, por ejemplo -- cuadro a cuadro. */
	if (opciones.captura_gl != NULL && strip_count > 0)
	{
		if (getenv("DCEMU_CAPTURA_TODAS"))
		{
			static int nf = 0;
			char nom[128];

			snprintf(nom, sizeof(nom), "f%04d-%s", nf++, opciones.captura_gl);
			volcar_gl(nom);
		}
		else
			volcar_gl(opciones.captura_gl);
	}

	strip_count = 0;

	total_polygon_count = 0;

	vol_count = 0;

	gui_refresh();

	logxmsg(LOG_PVR, "cb_tastart: SDL_GL_SwapBuffers\n");
	SDL_GL_SwapBuffers();
	fps_marcar_cuadro();
	pvr_framebufferdisplay = false;
	
//	SET_BIT(ASIC_ACK_A, 0x80); // fin de proceso ??? VBLINT?

	logxmsg(LOG_PVR, "cb_tastart\n");
	if (traza_activa)
		fprintf(stderr, "traza: TA_LIST_INIT: hechas=%02x habilitadas=%02x, se ponen en cero\n",
			(unsigned) pvr_listdone, (unsigned) pvr_registered);
	pvr_listdone = 0;

	/* Si el guest dejo un parametro de 64 bytes por la mitad, la mitad que
	   quedo no debe pegarse con el primer bloque de la escena siguiente. */
	ta_reiniciar();

	/* La cache de texturas es persistente: se invalida por generaciones
	   (vram.h), no por escena. Vaciarla aca costaba decodificar y volver a
	   subir cada textura en cada cuadro. */

	// ???
	memread_fisico(0xa05f8128, &dw, sizeof(DWORD)); // leer TA_ISP_BASE
	memwrite_fisico(0xa05f8138, &dw, sizeof(DWORD)); // grabar en TA_ITP_CURRENT

	glDisable(GL_DEPTH_TEST);
}

void cb_fb_r_sof1(DWORD addr, void * p, size_t size)
{
	logxmsg(LOG_PVR, "cb_fb_r_sof1\n");
	memcpy(&pvr_fb_r_sof1, p, size);
}

void cb_isp_backgnd_t(DWORD addr, void * p, size_t size)
{
	logxmsg(LOG_PVR, "cb_isp_backgnd_t\n");
	memcpy(&pvr_isp_backgnd_t, p, size);
	logxmsg(LOG_PVR, "tag address = %x\n", (pvr_isp_backgnd_t >> 3) & 0x1FFFFF);
}

/*
	TA_ISP_BASE. Programarlo deja el area ISP/TSP vacia, asi que el puntero de
	escritura vuelve al principio.
*/
void cb_ta_isp_base(DWORD addr, void * p, size_t size)
{
	memcpy(&pvr_ta_isp_base, p, size);
	pvr_ta_itp_current = pvr_ta_isp_base;
	logxmsg(LOG_PVR, "cb_ta_isp_base: %x\n", pvr_ta_isp_base);
}

void cb_param_base(DWORD addr, void * p, size_t size)
{
	memcpy(&pvr_param_base, p, size);
	logxmsg(LOG_PVR, "cb_param_base: %x\n", (pvr_param_base >> 20) & 0xF);
}

void cb_region_base(DWORD addr, void * p, size_t size)
{
	memcpy(&pvr_region_base, p, size);
	logxmsg(LOG_PVR, "cb_region_base: %x\n", (pvr_region_base >> 2) & 0x3FFFFF);
}

void cb_fb_w_ctrl(DWORD addr, void * p, size_t size)
{
	DWORD z;
	logxmsg(LOG_PVR, "cb_fb_w_ctrl\n");
	memcpy(&z, p, size);
	
	switch(z & 0x7)
	{
		case 0:	logxmsg(LOG_PVR, "fb_packmode: 0555KRGB\n");	break;
		case 1: logxmsg(LOG_PVR, "fb_packmode: 565RGB\n"); break;
		case 2: logxmsg(LOG_PVR, "fb_packmode: 4444ARGB\n"); break;
		case 3: logxmsg(LOG_PVR, "fb_packmode: 1555ARGB\n"); break;
		case 4: logxmsg(LOG_PVR, "fb_packmode: 888RGB\n"); break;
		case 5: logxmsg(LOG_PVR, "fb_packmode: 0888KRGB\n"); break;
		case 6: logxmsg(LOG_PVR, "fb_packmode: 8888ARGB\n"); break;
		case 7: logxmsg(LOG_PVR, "fb_packmode: reserved\n"); break;
	}
}

void cb_ppblocksize(DWORD addr, void * p, size_t size)
{
	DWORD dw = *(DWORD *) p;
	int punch_through = (dw >> 16) & 0x3;
	int transmod = (dw >> 12) & 0x3;
	int transpoly = (dw >> 8) & 0x3;
	int opaquemod = (dw >> 4) & 0x3;
	int opaquepoly = (dw >> 0) & 0x3;
	
//	logxmsg(LOG_PVR, "ppblocksize: %08x\n", *(DWORD *) p);
	logxmsg(LOG_PVR, "punch-through: %d\n", punch_through);
	logxmsg(LOG_PVR, "transmod: %d\n", transmod);
	logxmsg(LOG_PVR, "transpoly: %d\n", transpoly);
	logxmsg(LOG_PVR, "opaquemod: %d\n", opaquemod);
	logxmsg(LOG_PVR, "opaquepoly: %d\n", opaquepoly);

	pvr_registered = 0;
	pvr_3dscene = 1;

	if (traza_activa)
		fprintf(stderr, "traza: TA_ALLOC_CTRL=%08x op=%d opmod=%d tr=%d trmod=%d pt=%d\n",
			(unsigned) dw, opaquepoly, opaquemod, transpoly, transmod, punch_through);

	if (punch_through > 0)
		SET_BIT(pvr_registered, 1 << 4);
	else
		REMOVE_BIT(pvr_registered, 1 << 4);

	if (transmod > 0)
		SET_BIT(pvr_registered, 1 << 3);
	else
		REMOVE_BIT(pvr_registered, 1 << 3);

	if (transpoly > 0)
		SET_BIT(pvr_registered, 1 << 2);
	else
		REMOVE_BIT(pvr_registered, 1 << 2);

	if (opaquemod > 0)
		SET_BIT(pvr_registered, 1 << 1);
	else
		REMOVE_BIT(pvr_registered, 1 << 1);

	if (opaquepoly > 0)
		SET_BIT(pvr_registered, 1 << 0);
	else
		REMOVE_BIT(pvr_registered, 1 << 0);
}


void taListEnd()
{
	if (traza_activa)
		fprintf(stderr, "traza: fin de lista: registrando=%d hechas=%02x habilitadas=%02x\n",
			pvr_registering, (unsigned) pvr_listdone, (unsigned) pvr_registered);

	if (pvr_registering != -1)
	{
		/*
			Una lista cuyos encabezados trajeron el bit de volumen o de sombra
			alimenta ademas la maquinaria de su lista modificadora asociada
			(opaca -> modificadora opaca, translucida -> modificadora
			translucida): al cerrarla, si TA_ALLOC_CTRL habilita esa lista y
			no se cerro por su cuenta, su evento de fin sale tambien. Es lo
			que Virtua Tennis necesita, medido: manda UNA lista opaca con el
			bit de volumen (PCW 0x808C0002), la cierra, y espera el evento 8
			-- su handler arma la cadena por cuadro; sin el, el contador de
			vblank de [0x8C45F904] nunca arranca y el juego no pasa del
			arranque. El documento de Sega (3.7.4.1) dice que cada EOL emite
			el evento "del tipo en curso"; los poligonos de dos volumenes son
			de los dos tipos a la vez.
		*/
		int mod = -1;

		if (lista_con_volumen)
		{
			if (pvr_registering == 0)
				mod = 1;
			else if (pvr_registering == 2)
				mod = 3;
		}

		pvr_listdone |= (1 << pvr_registering);
		intc_add(pvr_lists[pvr_registering], 100);

		if (mod >= 0 && (pvr_registered & (1 << mod))
		&&  !(pvr_listdone & (1 << mod)))
		{
			pvr_listdone |= (1 << mod);
			intc_add(pvr_lists[mod], 100);
		}

		pvr_registering = -1;
		lista_con_volumen = 0;
	}

	/* EXPERIMENTO (diagnostico): levantar los eventos de TODAS las listas
	   habilitadas que quedaron vacias. No es lo que hace el hardware
	   (3.7.4.1); queda para aislar esperas de eventos en otros juegos. */
	if (getenv("DCEMU_FIN_TODAS"))
	{
		int l;

		for (l = 0; l < 5; l++)
			if ((pvr_registered & (1 << l)) && !(pvr_listdone & (1 << l)))
			{
				pvr_listdone |= (1 << l);
				intc_add(pvr_lists[l], 100);
			}
	}

	/*
		RENDERDONE ya no sale de aca. En el chip es el final del RENDER -- el
		ISP/TSP termino de rasterizar, despues de STARTRENDER --, no "todas
		las listas cerradas": emitirlo al cerrar la ultima lista era antes de
		tiempo para todos (KOS lo toleraba porque espera en un semaforo), y
		para un juego que habilita listas que no manda -- Virtua Tennis deja
		la translucida habilitada y vacia -- no salia nunca, y su espera del
		render no terminaba jamas. Sale de cb_tastart(), que ES el
		STARTRENDER. Ver ahi la demora.
	*/
}

void doUserClip()
{
	logxmsg(LOG_PVR, "pcw: User Tile Clip\n");
	logxmsg(LOG_PVR, "USER_CLIP: Xmin: %x\n", ta_address_pointer[4]);
	logxmsg(LOG_PVR, "USER_CLIP: Ymin: %x\n", ta_address_pointer[5]);
	logxmsg(LOG_PVR, "USER_CLIP: Xmax: %x\n", ta_address_pointer[6]);
	logxmsg(LOG_PVR, "USER_CLIP: Ymax: %x\n", ta_address_pointer[7]);
	logxmsg(LOG_PVR, "USER_CLIP: NO IMPLEMENTADO\n");
}

void objectListSet()
{
	logxmsg(LOG_PVR, "pcw: Object List Set\n");
	logxmsg(LOG_PVR, "pcw: NO IMPLEMENTADO\n");
}

/*
	Encabezado de sprite.

	Un sprite es un rectangulo que el PVR dibuja con un parametro propio en vez
	de con dos triangulos. Su encabezado tiene el mismo formato que el de un
	poligono en las palabras 1 a 3 -- ISP/TSP, TSP y control de textura --, asi
	que todo eso lo hace taPolyModifier(); lo que cambia es que **el color va
	aqui y no en los vertices**, en las palabras 4 (base) y 5 (offset).
*/
void taSprite()
{
	ta_avanzar_itp();

	taPolyModifier();

	sprite_color = ta_address_pointer[4];

	/* El color de offset de un sprite tambien va en el encabezado, y es de donde
	   salen los cuatro parametros del bump mapping. */
	TriangleStrip[strip_count].texture.pvr_texture_bump_param = ta_address_pointer[5];

	logxmsg(LOG_PVR, "pcw: Sprite, color %08x\n", (unsigned) sprite_color);
}

void taPolyModifier()
{
	ta_avanzar_itp();

	TA.control = *ta_address_pointer;

	logxmsg(LOG_PVR, "pcw: Polygon or Modifier Volume\n");

	/* Que lista abre cada encabezado, con el PCW crudo: es lo que decide que
	   evento de fin de lista se emite, y clasificarla mal manda el evento
	   equivocado. Una linea por cambio de lista, no por encabezado. */
	if (traza_activa && pvr_registering != (int) TA.registers.pcw_list_type)
		fprintf(stderr, "traza: encabezado abre lista %d (pcw=%08lx, PC=%08lx)\n",
			(int) TA.registers.pcw_list_type,
			(unsigned long) TA.control, (unsigned long) PC);

	pvr_registering = TA.registers.pcw_list_type;

	TriangleStrip[strip_count].type = TA.registers.pcw_list_type;
	/*
	switch(TA.registers.pcw_list_type)
	{
			case 0: TriangleStrip[strip_count].type = 4;	logxmsg(LOG_PVR, "pcw: list_type: opaque\n");						break;
			case 1:  TriangleStrip[strip_count].type = 3; logxmsg(LOG_PVR, "pcw: list_type: opaque modifier volume\n");		break;
			case 2: TriangleStrip[strip_count].type = 2;logxmsg(LOG_PVR, "pcw: list_type: translucent\n");					break;
			case 3: TriangleStrip[strip_count].type = 1;logxmsg(LOG_PVR, "pcw: list_type: translucent modifier volume\n");	break;
			case 4: TriangleStrip[strip_count].type = 0;logxmsg(LOG_PVR, "pcw: list_type: punch through\n");				break;
			default:	logxmsg(LOG_PVR, "pcw: list_type: RESERVED\n");					break;
	}
	*/
	// group control
	if (TA.registers.pcw_group_en)
	{
		logxmsg(LOG_PVR, "pcw: group_en\n");
		switch(TA.registers.pcw_strip_len)
		{
			case 0: logxmsg(LOG_PVR, "pcw: strip_len: 1\n");	break;
			case 1: logxmsg(LOG_PVR, "pcw: strip_len: 2\n");	break;
			case 2: logxmsg(LOG_PVR, "pcw: strip_len: 4\n");	break;
			case 3: logxmsg(LOG_PVR, "pcw: strip_len: 6\n");	break;
		}
		switch(TA.registers.pcw_user_clip)
		{
			case 0: logxmsg(LOG_PVR, "pcw: user_clip: disable\n");	break;
			case 1: logxmsg(LOG_PVR, "pcw: user_clip: reserved\n");	break;
			case 2: logxmsg(LOG_PVR, "pcw: user_clip: inside enable\n");	break;
			case 3: logxmsg(LOG_PVR, "pcw: user_clip: outside enable\n");	break;
		}
	}
		
	// obj control

	/*
		Que tipo de parametro global es este encabezado y que tipo de vertice
		deja vigente detras. La tabla esta en ta.c porque la necesitan dos: aca
		para saber que campos leer, y el ensamblador de bloques para saber si el
		parametro mide 32 o 64 bytes.
	*/
	ta_clasificar(TA.control, &global_parameter, &vertex_parameter);

	logxmsg(LOG_PVR, "pcw: global parameter: polygon type %d\n", global_parameter);

	/*
		El color de cara, para los vertices que traen intensidad en vez de
		color. En el POLY1 (intensidad sin offset, 32 bytes) va en las
		palabras 4-7; en el POLY2 y el POLY4 (64 bytes) en las 8-11 -- ahi las
		4-7 estan reservadas y la segunda mitad trae ademas el color de offset
		(POLY2) o el de cara del otro volumen (POLY4), que no se usan todavia.

		Leerlo de 8-11 en un POLY1 -- que ademas estaba clasificado como de 64
		-- daba el color desde el primer vertice pegado detras: la animacion
		del boot ROM perdia su estela y su logo por un alfa negativo.

		En modo intensidad 2 (col_type 3) el encabezado NO lo trae: se usa el
		que dejo el ultimo poligono en modo intensidad 1. Por eso se guarda en
		un estatico y solo se pisa cuando llega uno nuevo.
	*/
	if (global_parameter == TA_GLOBAL_POLY1)
		memcpy(&color_cara[0], &ta_address_pointer[4], sizeof(float) * 4);
	else
	if (global_parameter == TA_GLOBAL_POLY2 || global_parameter == TA_GLOBAL_POLY4)
		memcpy(&color_cara[0], &ta_address_pointer[8], sizeof(float) * 4);

	/*
		Un encabezado de volumen modificador solo trae la palabra ISP/TSP: no
		hay TSP instruction word, ni control de textura, ni blend. Leer sus
		palabras 2 y 3 -- que estan reservadas -- dejaba factores de blend y
		formato de textura sacados de basura, y esos parametros globales
		sobreviven hasta el proximo encabezado, o sea que se los llevaba puestos
		la geometria de la lista siguiente.
	*/
	if (global_parameter == TA_GLOBAL_VOLUMEN)
	{
		logxmsg(LOG_PVR, "pcw: encabezado de volumen modificador\n");

		/* A que lista afecta este volumen y que hace con lo acumulado. La
		   instruccion va en los bits 30-29 de la palabra ISP/TSP. */
		vol_lista = TA.registers.pcw_list_type;
		vol_instruccion = (ta_address_pointer[1] >> 29) & 0x3;

		return;
	}

	/*
		Si esta tira la afecta un volumen modificador. Son dos mecanismos
		distintos y el encabezado dice cual:

		  - Volume: el vertice trae dos juegos de parametros y dentro del
			volumen se usa el 1;
		  - Shadow: sombra barata, un solo juego cuya intensidad se escala.

		Las tiras sin ninguno de los dos se dibujan de una sola pasada.
	*/
	TriangleStrip[strip_count].volumen =
		(TA.registers.pcw_volume || TA.registers.pcw_shadow) ? 1 : 0;

	/* La lista en curso trajo encabezados con volumen/sombra: su cierre
	   emite tambien el evento de la lista modificadora asociada. Ver
	   taListEnd(). */
	if (TriangleStrip[strip_count].volumen)
		lista_con_volumen = 1;

	/* Se guarda aparte porque taVertexHandler() pisa TA.control con la palabra
	   de control del vertice, y ahi el bit 7 ya no es el del encabezado. */
	poly_sombra = TA.registers.pcw_shadow;

	if (TA.registers.pcw_list_type == 0 || TA.registers.pcw_list_type == 2 || TA.registers.pcw_list_type == 4)
	{
		RendCtrl.control  = ta_address_pointer[1];

// 				logxmsg(LOG_PVR, "uv16bit: %d\n", uv16bit);
	
		if (TA.registers.pcw_list_type == 2) // transparent polygon
			TriangleStrip[strip_count].depthmode = GL_GEQUAL;
		else
		{
			/*
				El punch-through tenia GL_LEQUAL fijo, y eso no lo deja dibujar
				casi nunca: el buffer de profundidad se limpia a 0,0 y las z de
				una escena caen alrededor de 0,5, asi que "menor o igual" falla
				contra cualquier pixel que no se haya tocado. pvr-bumpmap manda
				su pared de ladrillos por esa lista y no salia ni un pixel.

				El punch-through usa el modo de comparacion de su propia palabra
				ISP, igual que la lista opaca; lo que lo distingue en el chip es
				que descarta por alfa, no que cambie la prueba de profundidad.
			*/
			TriangleStrip[strip_count].depthmode = depth_modes[RendCtrl.registers.depthmode];
		}
		
		TriangleStrip[strip_count].culling = RendCtrl.registers.cullingmode;
		
		TriangleStrip[strip_count].zwrite = RendCtrl.registers.zwrite;
	}
	
	/* En la palabra TSP: bits 31-29 el factor de origen y 28-26 el de destino.
	   Las dos lineas asignaban a pvr_srcblend, asi que pvr_dstblend nunca se
	   escribia y glBlendFunc() recibia basura -- con un enum invalido la llamada
	   se ignora y queda el blend anterior. Por eso el panel translucido del
	   demo tunnel salia negro opaco. */
	TriangleStrip[strip_count].pvr_srcblend = blend_modes[(ta_address_pointer[2] >> 29) & 0x7];

	TriangleStrip[strip_count].pvr_dstblend = blend_modes[(ta_address_pointer[2] >> 26) & 0x7];
	
	pvr_srcblendmode = (ta_address_pointer[2] >> 25) & 0x1;
	pvr_dstblendmode = (ta_address_pointer[2] >> 24) & 0x1;
	
	if (pvr_srcblendmode)
		logxmsg(LOG_PVR, "srcblend: src select\n");
	if (pvr_dstblendmode)
		logxmsg(LOG_PVR, "dstblend: dst select\n");
	
	 TriangleStrip[strip_count].alpha =  ((ta_address_pointer[2] >> 20) & 0x1); // alpha

	TriangleStrip[strip_count].niebla = (ta_address_pointer[2] >> 22) & 0x3;

	 /* El bit 20 del TSP es "Use Alpha": con 0 el chip fuerza a 1.0 el alfa
	    DEL VERTICE, y nada mas -- el de la textura sigue vivo y la mezcla
	    tambien. Se guarda aparte para aplicarlo al construir los vertices;
	    usarlo como interruptor del blending costo los arboles de Crazy Taxi
	    (ver dibujar_escena()). */
	 poly_usa_alfa = (int) ((ta_address_pointer[2] >> 20) & 0x1);

	
	if (TA.registers.pcw_texture)
	{
		TexInfo.texture = ta_address_pointer[3];

		if (TexInfo.registers.mipmap)
			logxmsg(LOG_PVR, "texture: enable mipmap\n");
		else
			logxmsg(LOG_PVR, "texture: disable mipmap\n");
	
#define CTT() { pvr_texture_pixelformat = -1; pvr_texture_components = -1; pvr_texture_pixelpack = -1; }

		/* Sin paleta ni YUV salvo que el formato diga lo contrario. Va antes
		   del switch porque los parametros globales sobreviven a la tira: sin
		   esto, una textura normal despues de una indexada se seguiria
		   decodificando por la paleta. */
		TriangleStrip[strip_count].texture.pvr_texture_bpp = 0;
		TriangleStrip[strip_count].texture.pvr_texture_paleta = 0;
		TriangleStrip[strip_count].texture.pvr_texture_yuv = 0;
		TriangleStrip[strip_count].texture.pvr_texture_bump = 0;

		switch (TexInfo.registers.pixelformat)
		{
			case 0:
			logxmsg(LOG_PVR, "texture: ARGB1555\n");
			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_BGRA;
			TriangleStrip[strip_count].texture.pvr_texture_components = 4;
			TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_SHORT_1_5_5_5_REV;
			pvr_texture_pixelconvert = NULL;
			break;
	
			case 1:
			logxmsg(LOG_PVR, "texture: RGB565\n");
			TriangleStrip[strip_count].texture.pvr_texture_components = 3;
			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_RGB;
			TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_SHORT_5_6_5;
			pvr_texture_pixelconvert = NULL;
			break;
	
			case 2:
			logxmsg(LOG_PVR, "texture: ARGB4444\n");
    			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_BGRA_EXT;	
	  		TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_SHORT_4_4_4_4_REV;
			TriangleStrip[strip_count].texture.pvr_texture_components = 4;
			break;
	
			/*
				YUV422: cada 32 bits llevan dos pixeles -- U, Y0, V, Y1 --, o
				sea croma compartido de a pares. GL no tiene ese formato, asi
				que se convierte a RGB al subir la textura, como con las
				paletas. Es lo que produce el convertidor del TA.
			*/
			case 3:
			logxmsg(LOG_PVR, "texture: YUV422\n");
			TriangleStrip[strip_count].texture.pvr_texture_yuv = 1;
			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_RGBA;
			TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_BYTE;
			TriangleStrip[strip_count].texture.pvr_texture_components = 4;
			pvr_texture_pixelconvert = NULL;
			break;

			/*
				BUMP: los texels son dos angulos, no un color. Se resuelven a
				gris al subir la textura; ver decodificar_bump().
			*/
			case 4:
			logxmsg(LOG_PVR, "texture: BUMP\n");
			TriangleStrip[strip_count].texture.pvr_texture_bump = 1;
			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_RGBA;
			TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_BYTE;
			TriangleStrip[strip_count].texture.pvr_texture_components = 4;
			pvr_texture_pixelconvert = NULL;
			break;

			/*
				Texturas indexadas. El selector de banco esta metido en el mismo
				texture control word, encima de los bits que en los demas
				formatos son "sin usar", "stride" y "scan order":

				  4 bpp: bits 26-21, 64 bancos de 16 entradas
				  8 bpp: bits 26-25,  4 bancos de 256

				Que el selector pise el bit de scan order es la razon de forzar
				twiddled: en estos dos formatos ese bit no significa nada, y
				leerlo como orden de barrido saca la textura al reves.

				GL recibe RGBA8888 ya resuelto; el trabajo lo hace
				decodificar_paleta() en get_texture().
			*/
			case 5:
			logxmsg(LOG_PVR, "texture: 4BPP_PALETTE\n");
			TriangleStrip[strip_count].texture.pvr_texture_bpp = 4;
			TriangleStrip[strip_count].texture.pvr_texture_paleta =
				(ta_address_pointer[3] >> 21) & 0x3F;
			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_RGBA;
			TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_BYTE;
			TriangleStrip[strip_count].texture.pvr_texture_components = 4;
			pvr_texture_pixelconvert = NULL;
			break;

			case 6:
			logxmsg(LOG_PVR, "texture: 8BPP_PALETTE\n");
			TriangleStrip[strip_count].texture.pvr_texture_bpp = 8;
			TriangleStrip[strip_count].texture.pvr_texture_paleta =
				(ta_address_pointer[3] >> 25) & 0x3;
			TriangleStrip[strip_count].texture.pvr_texture_pixelformat = GL_RGBA;
			TriangleStrip[strip_count].texture.pvr_texture_pixelpack = GL_UNSIGNED_BYTE;
			TriangleStrip[strip_count].texture.pvr_texture_components = 4;
			pvr_texture_pixelconvert = NULL;
			break;
		}
		
	
		/*
			Textura rectangular. El bit dice "el ancho en memoria no es el
			declarado, sino el de TEXT_CONTROL (0x005F80E4), en unidades de 32
			texels"; se usa para texturas cuyo ancho no es potencia de dos. Solo
			se logueaba.

			En los formatos indexados ese mismo bit 25 es parte del selector de
			banco, asi que ahi no significa nada.
		*/
		if (TriangleStrip[strip_count].texture.pvr_texture_bpp)
			TriangleStrip[strip_count].texture.pvr_texture_stride = 0;
		else
		{
			DWORD control = 0;

			if (TexInfo.registers.stride)
				memread_fisico(0xA05F80E4, &control, 4);

			TriangleStrip[strip_count].texture.pvr_texture_stride =
				(control & 0x1F) * 32;
		}

		if (TexInfo.registers.stride)
			logxmsg(LOG_PVR, "texture: stride\n");
		else
			logxmsg(LOG_PVR, "texture: no stride\n");
	
		TriangleStrip[strip_count].texture.filtermode = (ta_address_pointer[2] >> 13) & 0x3;

		/*
			Como se combina el texel con el color del vertice. No se emulaba, o
			sea que quedaba el GL_MODULATE de fabrica para todo -- y en decal el
			texel *reemplaza* al color. Una superficie con color de vertice
			negro, que en decal se ve, multiplicando salia negra: es lo que
			dejaba invisible al sprite del mapa de relieve de pvr-bumpmap.
		*/
		TriangleStrip[strip_count].texture.pvr_texture_env = (ta_address_pointer[2] >> 6) & 0x3;

		TriangleStrip[strip_count].texture.pvr_texture_size_usize = 0x8 <<  ((ta_address_pointer[2] >> 3) & 0x7);

		TriangleStrip[strip_count].texture.pvr_texture_size_vsize = 0x8 <<  (ta_address_pointer[2] & 0x7);
	
		logxmsg(LOG_PVR, "texture size: %d x %d\n", TriangleStrip[strip_count].texture.pvr_texture_size_usize, TriangleStrip[strip_count].texture.pvr_texture_size_vsize);
	}
	else
		{
		TriangleStrip[strip_count].texture.surface = 0;
		TexInfo.registers.texture_surface = 0;
		}
}

/*
	Abre un vertice nuevo en el buffer con la posicion, que es lo unico que
	todos los tipos traen en el mismo sitio: x, y, z en las palabras 1 a 3.

	z se guarda como su reciproco porque el TA entrega 1/w y GL espera
	profundidad.
*/
static vertex * vertice_nuevo(void)
{
	float xyz[3];
	vertex * v;

	memcpy(xyz, &ta_address_pointer[1], sizeof(float) * 3);

	total_polygon_count++;

	v = &VertexBuffer[total_polygon_count];

	/*
		La z del TA es 1/w: **mayor quiere decir mas cerca**, y las pruebas de
		profundidad del PVR estan escritas asi -- GREATER deja pasar lo que esta
		mas cerca. El glOrtho de screeninit() mapea z de ojo creciente a
		profundidad creciente, asi que la z del TA se guarda tal cual y el orden
		sale solo.

		Aca se guardaba **su reciproco**, que invierte el orden. Con dos capas
		todavia se salva -- la de arriba gana igual porque la de abajo no
		escribio nada --, pero con tres el resultado es que solo se ve la
		primera. pvr_rtt_sized dibuja un fondo en z=1, un interior en z=2, dos
		marcas en z=3 y z=4 y los bordes en z=5, y de todo eso salia el fondo y
		nada mas. Otra medida del mismo cambio: libdream-ta pasa de 43333 colores
		a 65227.

		Y hay un caso que el reciproco rompe del todo: **z = 0 es legitimo** y
		significa infinitamente lejos, pero 1/0 se va a infinito y GL recorta el
		vertice. pvr_rtt_sized manda su rectangulo de fondo justamente con z = 0.

		"Tal cual" en el orden, no en el valor: pasa por profundidad_ta(), que
		es monotona -- ver su comentario, la precision lineal no alcanzaba.
	*/
	v->x = xyz[0];
	v->y = xyz[1];
	v->z = profundidad_ta(xyz[2]);

	/* El q de la correccion de perspectiva de las texturas (render.h): el
	   1/w del TA tal cual, acotado por si llega 0 ("infinitamente lejos").
	   El cierre de tira premultiplica las UV con el. */
	v->q = (xyz[2] > 1e-6f) ? xyz[2] : 1e-6f;

	/* Sin coordenadas de textura mientras el tipo no diga otra cosa: si no,
	   quedan las del vertice anterior. */
	v->t1 = 0.0f;
	v->t2 = 0.0f;

	return v;
}

/* Color empaquetado: una palabra ARGB, alpha en 31-24. */
static void vertice_color(vertex * v, DWORD argb)
{
	v->a = poly_usa_alfa ? ((argb >> 24) & 0xFF) / 255.0 : 1.0;
	v->r = ((argb >> 16) & 0xFF) / 255.0;
	v->g = ((argb >> 8)  & 0xFF) / 255.0;
	v->b = ((argb >> 0)  & 0xFF) / 255.0;
}

/* Color en punto flotante: cuatro floats A, R, G, B desde la palabra `w`. */
static void vertice_color_flotante(vertex * v, int w)
{
	float c[4];

	memcpy(c, &ta_address_pointer[w], sizeof(float) * 4);

	v->a = poly_usa_alfa ? c[0] : 1.0f;
	v->r = c[1];
	v->g = c[2];
	v->b = c[3];
}

/*
	Modo intensidad: el vertice no trae color sino un multiplicador sobre el
	color de cara que dejo el encabezado. El alpha sale del color de cara tal
	cual -- la intensidad no lo toca.
*/
static void vertice_intensidad(vertex * v, DWORD palabra)
{
	float i;

	memcpy(&i, &palabra, sizeof(float));

	v->a = poly_usa_alfa ? color_cara[0] : 1.0f;
	v->r = color_cara[1] * i;
	v->g = color_cara[2] * i;
	v->b = color_cara[3] * i;
}

/* UV de 32 bits: u y v en dos palabras consecutivas desde `w`. */
static void vertice_uv(vertex * v, int w)
{
	memcpy(&v->t1, &ta_address_pointer[w],     sizeof(float));
	memcpy(&v->t2, &ta_address_pointer[w + 1], sizeof(float));
}

/* --- El juego 1: lo que se usa dentro del volumen modificador ------------ */

static void juego1_color(vertex * v, DWORD argb)
{
	v->a1 = ((argb >> 24) & 0xFF) / 255.0;
	v->r1 = ((argb >> 16) & 0xFF) / 255.0;
	v->g1 = ((argb >> 8)  & 0xFF) / 255.0;
	v->b1 = ((argb >> 0)  & 0xFF) / 255.0;
}

static void juego1_intensidad(vertex * v, DWORD palabra)
{
	float i;

	memcpy(&i, &palabra, sizeof(float));

	v->a1 = color_cara[0];
	v->r1 = color_cara[1] * i;
	v->g1 = color_cara[2] * i;
	v->b1 = color_cara[3] * i;
}

static void juego1_uv(vertex * v, int w)
{
	memcpy(&v->u1, &ta_address_pointer[w],     sizeof(float));
	memcpy(&v->v1, &ta_address_pointer[w + 1], sizeof(float));
}

static void juego1_uv16(vertex * v, DWORD palabra)
{
	DWORD u = palabra & 0xFFFF0000;
	DWORD s = palabra << 16;

	memcpy(&v->u1, &u, sizeof(float));
	memcpy(&v->v1, &s, sizeof(float));
}

/*
	Para los tipos que traen un solo juego: el 1 es una copia del 0, escalada
	por FPU_SHAD_SCALE si el encabezado pidio sombra barata.

	El alpha no se escala -- la sombra oscurece, no transparenta.
*/
static void juego1_copiar(vertex * v, int sombra)
{
	float k = sombra ? sombra_escala() : 1.0f;

	v->a1 = v->a;
	v->r1 = v->r * k;
	v->g1 = v->g * k;
	v->b1 = v->b * k;
	v->u1 = v->t1;
	v->v1 = v->t2;
}

/* Los seis tipos que traen dos juegos de parametros. */
static int tipo_de_dos_volumenes(int t)
{
	return (t >= 9 && t <= 14);
}

/*
	Un sprite: el rectangulo entero en un solo parametro de 64 bytes.

	  +0x04 Ax Ay Az   +0x10 Bx By Bz   +0x1C Cx Cy Cz   +0x28 Dx Dy
	  +0x30 sin usar   +0x34 AU/AV      +0x38 BU/BV      +0x3C CU/CV

	**Ojo con la palabra 12**: entre Dy y las UV hay una sin usar, asi que las
	tres de textura son la 13, la 14 y la 15. Leerlas una antes deja la u en
	cero para las cuatro esquinas y la textura sale muestreada sobre una linea.

	De la cuarta esquina solo vienen x e y: z y las UV salen de completar el
	paralelogramo, D = A - B + C, que es lo que hace el chip. Las UV son de 16
	bits, empaquetadas dos por palabra.

	El color no esta en el vertice sino en el encabezado, y es el mismo para las
	cuatro esquinas.
*/
static void vertice_sprite(int con_textura)
{
	float		p[11];		/* Ax..Cz y Dx, Dy */
	float		u[4], v[4];
	float		x[4], y[4], z[4];
	int			k;

	memcpy(p, &ta_address_pointer[1], sizeof(float) * 11);

	for (k = 0; k < 3; k++)
	{
		x[k] = p[k * 3 + 0];
		y[k] = p[k * 3 + 1];
		z[k] = p[k * 3 + 2];
	}

	x[3] = p[9];
	y[3] = p[10];
	z[3] = z[0] - z[1] + z[2];

	for (k = 0; k < 4; k++)
	{
		u[k] = 0.0f;
		v[k] = 0.0f;
	}

	if (con_textura)
	{
		for (k = 0; k < 3; k++)
		{
			DWORD w = ta_address_pointer[13 + k];
			DWORD hu = w & 0xFFFF0000;
			DWORD hv = w << 16;

			memcpy(&u[k], &hu, sizeof(float));
			memcpy(&v[k], &hv, sizeof(float));
		}

		u[3] = u[0] - u[1] + u[2];
		v[3] = v[0] - v[1] + v[2];
	}

	/* A, B, D, C: una tira con esos cuatro da (A,B,D) y (B,D,C). */
	{
		static const int orden[4] = { 0, 1, 3, 2 };

		for (k = 0; k < 4; k++)
		{
			int		s = orden[k];
			vertex * ve;

			total_polygon_count++;

			ve = &VertexBuffer[total_polygon_count];

			ve->x = x[s];
			ve->y = y[s];
			ve->z = profundidad_ta(z[s]);	/* igual que vertice_nuevo() */
			ve->q = (z[s] > 1e-6f) ? z[s] : 1e-6f;
			ve->t1 = u[s];
			ve->t2 = v[s];

			vertice_color(ve, sprite_color);
			juego1_copiar(ve, poly_sombra);
		}
	}
}

/*
	UV de 16 bits: las dos coordenadas van en una sola palabra, y cada una es la
	**mitad alta** de un float de 32 bits. O sea que se recuperan desplazando,
	no convirtiendo: se pierde precision en la mantisa y nada mas.
*/
static void vertice_uv16(vertex * v, DWORD palabra)
{
	DWORD u = palabra & 0xFFFF0000;
	DWORD s = palabra << 16;

	memcpy(&v->t1, &u, sizeof(float));
	memcpy(&v->t2, &s, sizeof(float));
}

void taVertexHandler()
{
	ta_avanzar_itp();

	DWORD antes;

#ifdef DEBUG_VERTEX_NEW
	logxmsg(LOG_PVR, "pcw: vertex parameter: polygon type %d, pcw: %08x\n", vertex_parameter, TA.control);
#endif

	TA.control = *ta_address_pointer;

	if (traza_activa)
	{
		traza_ta_vertices++;

		if (TA.registers.pcw_end_of_strip)
			traza_ta_fin_tira++;

		if (vertex_parameter >= 0 && vertex_parameter < TA_TIPOS)
			traza_ta_tipos[vertex_parameter]++;
	}

	if(vertexstart == true)
	{
		TriangleStrip[strip_count].index = total_polygon_count+1;

		vertexstart = false;
	}

	/*
		Los quince tipos de vertice del manual. Todos empiezan igual -- x, y, z
		en las palabras 1 a 3 -- y se diferencian en como traen el color y las
		coordenadas de textura:

		  - color empaquetado: una palabra ARGB;
		  - color en punto flotante: cuatro floats A, R, G, B;
		  - intensidad: un float que multiplica el color de cara del encabezado;
		  - UV de 32 bits: dos palabras; de 16, las dos en una.

		Y los seis de "dos volumenes" traen todo dos veces: el juego 0 es el que
		se usa fuera del volumen modificador y el 1 dentro.

		Estaban implementados solo el 0, 1, 3 y 5. Los que faltaban se
		descartaban en silencio: el vertice no entraba al buffer, la tira
		terminaba con count 0 y no se dibujaba nada. Las demos de volumen
		modificador perdian asi 41 de sus 42 vertices.
	*/
	antes = total_polygon_count;

	switch (vertex_parameter)
	{
		case 0:		/* sin textura, color empaquetado */
			vertice_color(vertice_nuevo(), ta_address_pointer[6]);
			break;

		case 1:		/* sin textura, color en punto flotante */
			vertice_color_flotante(vertice_nuevo(), 4);
			break;

		case 2:		/* sin textura, intensidad */
			vertice_intensidad(vertice_nuevo(), ta_address_pointer[4]);
			break;

		case 3:		/* texturado, color empaquetado, UV de 32 bits */
		{
			/* Layout: +0x04 x, +0x08 y, +0x0C z, +0x10 u, +0x14 v, +0x18 color
			   base, +0x1C color de offset. Las coordenadas leian desde [6] --
			   o sea +0x18, el color -- y se pasaban 12 bytes del registro de
			   32. El caso 5 ya lo hacia bien desde [1]. */
			vertex * v = vertice_nuevo();

			vertice_uv(v, 4);
			/* El color empaquetado es ARGB: alpha en 31-24, no en 23-16. Estaba
			   tomando el alpha del mismo campo que el rojo, asi que la
			   geometria texturada quedaba con alpha = rojo. */
			vertice_color(v, ta_address_pointer[6]);
		}
		break;

		case 4:		/* texturado, color empaquetado, UV de 16 bits */
		{
			vertex * v = vertice_nuevo();

			vertice_uv16(v, ta_address_pointer[4]);
			vertice_color(v, ta_address_pointer[6]);
		}
		break;

		case 5:		/* texturado, color en punto flotante, UV de 32 bits */
		{
			vertex * v = vertice_nuevo();

			vertice_uv(v, 4);
			vertice_color_flotante(v, 8);
		}
		break;

		case 6:		/* texturado, color en punto flotante, UV de 16 bits */
		{
			vertex * v = vertice_nuevo();

			vertice_uv16(v, ta_address_pointer[4]);
			vertice_color_flotante(v, 8);
		}
		break;

		case 7:		/* texturado, intensidad, UV de 32 bits */
		{
			vertex * v = vertice_nuevo();

			vertice_uv(v, 4);
			vertice_intensidad(v, ta_address_pointer[6]);
		}
		break;

		case 8:		/* texturado, intensidad, UV de 16 bits */
		{
			vertex * v = vertice_nuevo();

			vertice_uv16(v, ta_address_pointer[4]);
			vertice_intensidad(v, ta_address_pointer[6]);
		}
		break;

		/*
			Los seis de dos volumenes. El juego 0 va donde los demas y el 1
			detras: para los sin textura es la palabra siguiente, y para los
			texturados el bloque entero de UV, color y offset se repite en la
			segunda mitad del vertice de 64 bytes.
		*/
		case 9:		/* sin textura, color empaquetado, dos volumenes */
		{
			vertex * v = vertice_nuevo();

			vertice_color(v, ta_address_pointer[4]);
			juego1_color(v, ta_address_pointer[5]);
		}
		break;

		case 10:	/* sin textura, intensidad, dos volumenes */
		{
			vertex * v = vertice_nuevo();

			vertice_intensidad(v, ta_address_pointer[4]);
			juego1_intensidad(v, ta_address_pointer[5]);
		}
		break;

		case 11:	/* texturado, color empaquetado, dos volumenes, UV de 32 */
		{
			vertex * v = vertice_nuevo();

			vertice_uv(v, 4);
			vertice_color(v, ta_address_pointer[6]);
			juego1_uv(v, 8);
			juego1_color(v, ta_address_pointer[10]);
		}
		break;

		case 12:	/* texturado, color empaquetado, dos volumenes, UV de 16 */
		{
			vertex * v = vertice_nuevo();

			vertice_uv16(v, ta_address_pointer[4]);
			vertice_color(v, ta_address_pointer[6]);
			juego1_uv16(v, ta_address_pointer[8]);
			juego1_color(v, ta_address_pointer[10]);
		}
		break;

		case 13:	/* texturado, intensidad, dos volumenes, UV de 32 */
		{
			vertex * v = vertice_nuevo();

			vertice_uv(v, 4);
			vertice_intensidad(v, ta_address_pointer[6]);
			juego1_uv(v, 8);
			juego1_intensidad(v, ta_address_pointer[10]);
		}
		break;

		case 14:	/* texturado, intensidad, dos volumenes, UV de 16 */
		{
			vertex * v = vertice_nuevo();

			vertice_uv16(v, ta_address_pointer[4]);
			vertice_intensidad(v, ta_address_pointer[6]);
			juego1_uv16(v, ta_address_pointer[8]);
			juego1_intensidad(v, ta_address_pointer[10]);
		}
		break;

		/*
			Los dos de sprite. Un solo parametro trae el rectangulo entero:
			cuatro esquinas A, B, C y D en orden alrededor, con las UV de las
			tres primeras cuando lleva textura.

			De D solo vienen x e y; el chip deduce z y las UV completando el
			paralelogramo, que es lo que hace `d_de()`. El color no esta aqui
			sino en el encabezado.

			Se emiten en el orden A, B, D, C porque una tira de triangulos con
			esos cuatro da (A,B,D) y (B,D,C), o sea el rectangulo completo.
		*/
		case TA_VERTICE_SPRITE0:
		case TA_VERTICE_SPRITE1:
			vertice_sprite(vertex_parameter == TA_VERTICE_SPRITE1);
			break;

		/*
			Un vertice de volumen modificador trae un triangulo entero -- tres
			puntos en 64 bytes -- y no es geometria que se dibuje: define la
			region donde los poligonos cambian de parametros. Va a su propio
			buffer, que cb_tastart() marca en la plantilla antes de dibujar.
		*/
		case TA_VERTICE_VOLUMEN:
		{
			if (vol_count < sizeof(VolumeBuffer) / sizeof(VolumeBuffer[0]))
			{
				VolTri * t = &VolumeBuffer[vol_count++];
				float p[9];
				int k;

				memcpy(p, &ta_address_pointer[1], sizeof(float) * 9);

				for (k = 0; k < 3; k++)
				{
					t->x[k] = p[k * 3 + 0];
					t->y[k] = p[k * 3 + 1];
					/* La misma transformacion que los vertices que dibuja:
					   el volumen se compara contra sus profundidades. */
					t->z[k] = profundidad_ta(p[k * 3 + 2]);
				}

				t->lista = vol_lista;
				t->instruccion = vol_instruccion;
			}
		}
		break;

		default:
		{
			logxmsg(LOG_PVR, "VERTEX: tipo %d de vertex no implementado!\n", vertex_parameter);
		}
		break;
	}

	/*
		Los tipos de un solo juego dejan el 1 sin escribir. Se copia del 0, con
		la escala de sombra si el encabezado la pidio, para que la segunda
		pasada pueda dibujar cualquier tira sin preguntar de que tipo era.

		La condicion mira si de verdad entro un vertice: el tipo de volumen y
		los no implementados no incrementan el contador, y sin esto pisarian el
		juego 1 del vertice anterior.
	*/
	if (total_polygon_count != antes && !tipo_de_dos_volumenes(vertex_parameter))
		juego1_copiar(&VertexBuffer[total_polygon_count], poly_sombra);

	if (traza_activa && vertex_parameter >= 0)
	{
		/* Rango de las coordenadas que quedaron en el buffer. El TA recibe
		   vertices ya en coordenadas de pantalla, asi que x tiene que caer en
		   0..ancho, y en 0..alto, y z es 1/w. Si no, el problema esta en el
		   layout del vertice o en la transformacion, no en el rasterizado. */
		vertex * v = &VertexBuffer[total_polygon_count];
		float c[3];
		int k;

		c[0] = v->x; c[1] = v->y; c[2] = v->z;

		for (k = 0; k < 3; k++)
		{
			if (traza_ta_vertices == 1 || c[k] < traza_ta_min[k]) traza_ta_min[k] = c[k];
			if (traza_ta_vertices == 1 || c[k] > traza_ta_max[k]) traza_ta_max[k] = c[k];
		}
	}

	/*
		Un sprite es una primitiva completa: el chip nunca encadena dos
		consecutivos, asi que la tira se cierra aunque el parametro no traiga
		el bit de fin. Confiar en el bit costo los arboles de Crazy Taxi:
		manda sus hojas como sprites sin fin-de-tira, dcemu los encadenaba de
		a dos o mas en una sola tira, y los triangulos puente entre las
		esquinas de un sprite y el siguiente eran los rectangulos negros
		detras del follaje y los poligonos gigantes que cruzaban el cielo
		(vertices a +-200000 pixeles: la tira 768 de la escena 1452, n=8, dos
		sprites lejanos unidos).
	*/
	if (TA.registers.pcw_end_of_strip
	||  vertex_parameter == TA_VERTICE_SPRITE0
	||  vertex_parameter == TA_VERTICE_SPRITE1)
	{
#ifdef DEBUG_VERTEX_NEW
		logxmsg(LOG_PVR, "VERTEX: end-of-strip\n");
#endif

		TriangleStrip[strip_count].count = ((total_polygon_count+1) - TriangleStrip[strip_count].index);

		/* La correccion de perspectiva de las texturas (render.h): las UV de
		   los dos juegos se premultiplican por el q del vertice, una sola
		   vez, con la tira ya completa. */
		{
			DWORD kq;

			for (kq = 0; kq < TriangleStrip[strip_count].count; kq++)
			{
				vertex * vp = &VertexBuffer[TriangleStrip[strip_count].index + kq];

				vp->t1 *= vp->q;	vp->t2 *= vp->q;
				vp->tr  = 0.0f;		vp->tq  = vp->q;
				vp->u1 *= vp->q;	vp->v1 *= vp->q;
				vp->ur  = 0.0f;		vp->uq  = vp->q;
			}
		}

		if (TexInfo.registers.texture_surface)
		{
			
			/* En 4 y 8 bpp el bit 26 es parte del selector de banco, no el
			   orden de barrido: leerlo ahi saca la textura al reves. Los
			   formatos indexados van siempre twiddled. */
			TriangleStrip[strip_count].texture.twiddled =
				TriangleStrip[strip_count].texture.pvr_texture_bpp
					? 1
					: (TexInfo.registers.twiddled ? 0 : 1);

			TriangleStrip[strip_count].texture.vq = TexInfo.registers.vq;
			TriangleStrip[strip_count].texture.mipmapped = TexInfo.registers.mipmap;
			TriangleStrip[strip_count].texture.tcw_crudo = TexInfo.texture;

			TriangleStrip[strip_count].texture.surface = TexInfo.registers.texture_surface << 3;
		}
		
		/*
			Los parametros globales del PVR -- modo de profundidad, culling,
			escritura de Z, alpha, los dos factores de blend y todo lo de la
			textura -- siguen vigentes hasta el proximo encabezado de poligono,
			no solo para la primera tira que venga detras.

			taPolyModifier() los escribe en la entrada que estaba abierta en ese
			momento, asi que la tira siguiente arrancaba en cero: depthmode salia
			0, que no es un enum valido y hace que glDepthFunc() se ignore y
			quede el de la tira anterior, o GL_NEVER, con lo que no se dibujaba
			nada. Por eso conio salia negro: manda un encabezado y detras una
			tira por caracter.

			index y count se rellenan solos: el primero al llegar el proximo
			vertice, el segundo al cerrar esta tira.
		*/
		if (strip_count + 1 < sizeof(TriangleStrip) / sizeof(TriangleStrip[0]))
			TriangleStrip[strip_count + 1] = TriangleStrip[strip_count];

		strip_count++;

		vertexstart = true;
	}
}

struct rgbmask
{
	Uint32 rmask;
	Uint32 gmask;
	Uint32 bmask;
	Uint32 amask;
};

struct rgbmask mask[] =
{
	{ 0x1f << 10, 0x1f << 5, 0x1f, 0 },	// ARGB0555
	{ 0x1f << 11, 0x3f << 5, 0x1f, 0 }, // RGB565
	{ 0xff << 16, 0xff << 8, 0xff, 0 }, // RGB888
	{ 0xff << 16, 0xff << 8, 0xff, 0 }  // ARGB0888
};

//void draw_backscreen()
SDL_Surface * draw_backscreen()
{
	SDL_Surface * tmp;
//	int ancho = screenwidth * ((screenbits == 32) ? 1 : 2);
	tmp = SDL_CreateRGBSurfaceFrom(
 			get_memory_pointer(0xA5000000 + pvr_fb_r_sof1),
    		screenancho,
      		screenheight,
        	screenbits,
			screenancho * (screenbits / 8),
         	mask[screenformat].rmask,
         	mask[screenformat].gmask,
         	mask[screenformat].bmask,
         	mask[screenformat].amask);
 	if (tmp == NULL)
 	{
 		logxmsg(LOG_PVR, "error al crear backscreen\n");
 		exit(1);
	}
	return tmp;
}

/*
	Vuelca el framebuffer emulado a un BMP de 24 bits.

	Lee la RAM de video, no el buffer de OpenGL: es lo que la Dreamcast tiene
	para mostrar, independiente de si la ventana lo llega a presentar. Sirve
	para verificar el arranque sin depender de una captura de pantalla, y para
	distinguir un problema de emulacion de uno de presentacion.

	Solo tiene sentido con el PVR en modo 2D (framebuffer plano), que es como
	arrancan la BIOS y los demos que escriben directo a la RAM de video.
*/
/*
	Vuelca lo que **GL rasterizo**, no lo que hay en la RAM de video.

	volcar_framebuffer() (F5) sirve para el camino 2D, donde el guest escribe
	pixeles; el 3D no pasa por ahi -- dcemu lo manda a OpenGL directamente --,
	asi que para una demo de PVR ese volcado siempre sale negro y la unica
	verificacion posible era capturar la ventana. Y capturar la ventana depende
	del compositor del anfitrion: en la misma sesion, la misma demo que daba
	3036 colores paso a dar 4 sin que cambiara una linea del emulador.

	Esto lo saca del medio: glReadPixels sobre el buffer que se acaba de
	presentar, a un BMP, sin pasar por la ventana.
*/
int volcar_gl(const char * ruta)
{
	FILE *			fp;
	/*
		El tamano de la **ventana**, no el de la pantalla emulada.

		Son distintos: la ventana es de 800x600 y screeninit() estira los
		640x480 del guest sobre ella entera con glOrtho. Leyendo 640x480 desde
		(0,0) salia el rectangulo de abajo a la izquierda, o sea que se perdia
		el 20% de arriba y el 20% de la derecha. Y como glOrtho invierte la y,
		"arriba" en la imagen es arriba en la ventana: cualquier demo que
		escriba en la franja superior -- toda la familia conio, que pone su
		texto ahi -- salia en negro y parecia que no dibujaba nada.
	*/
	int				ancho = (outputscreen != NULL) ? outputscreen->w : screenancho;
	int				alto  = (outputscreen != NULL) ? outputscreen->h : screenheight;
	int				x, y, relleno;
	unsigned char	cabecera[54];
	unsigned char *	pixeles;
	long			tam_datos;

	if (ancho <= 0 || alto <= 0)
		return 1;

	pixeles = (unsigned char *) malloc((size_t) ancho * alto * 3);

	if (pixeles == NULL)
		return 1;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, ancho, alto, GL_RGB, GL_UNSIGNED_BYTE, pixeles);

	fp = fopen(ruta, "wb");

	if (!fp)
	{
		fprintf(stderr, "no se pudo crear %s\n", ruta);
		free(pixeles);
		return 1;
	}

	relleno = (4 - ((ancho * 3) % 4)) % 4;
	tam_datos = (long) (ancho * 3 + relleno) * alto;

	memset(cabecera, 0, sizeof(cabecera));
	cabecera[0] = 'B';	cabecera[1] = 'M';
	*(DWORD *) &cabecera[2]  = (DWORD) (54 + tam_datos);
	*(DWORD *) &cabecera[10] = 54;
	*(DWORD *) &cabecera[14] = 40;
	*(long  *) &cabecera[18] = ancho;
	*(long  *) &cabecera[22] = alto;		/* GL ya entrega de abajo hacia arriba */
	*(WORD  *) &cabecera[26] = 1;
	*(WORD  *) &cabecera[28] = 24;
	*(DWORD *) &cabecera[34] = (DWORD) tam_datos;

	fwrite(cabecera, 1, sizeof(cabecera), fp);

	for (y = 0; y < alto; y++)
	{
		unsigned char * fila = pixeles + (long) y * ancho * 3;

		for (x = 0; x < ancho; x++)
		{
			/* GL entrega R,G,B y el BMP guarda B,G,R. */
			unsigned char rgb[3];

			rgb[0] = fila[x * 3 + 2];
			rgb[1] = fila[x * 3 + 1];
			rgb[2] = fila[x * 3 + 0];

			fwrite(rgb, 1, 3, fp);
		}

		for (x = 0; x < relleno; x++)
			fputc(0, fp);
	}

	fclose(fp);
	free(pixeles);

	fprintf(stderr, "buffer de GL volcado a %s (%dx%d)\n", ruta, ancho, alto);

	return 0;
}

int volcar_framebuffer(const char * ruta)
{
	FILE *			fp;
	unsigned char *	origen = (unsigned char *) get_memory_pointer(0xA5000000 + pvr_fb_r_sof1);
	int				ancho = screenancho;
	int				alto = screenheight;
	int				bpp, x, y, relleno;
	unsigned char	cabecera[54];
	long			tam_datos;

	switch (screenformat)
	{
		case FRAMEBUFFER_ARGB0555:
		case FRAMEBUFFER_RGB565:	bpp = 2;	break;
		case FRAMEBUFFER_RGB888:	bpp = 3;	break;
		case FRAMEBUFFER_ARGB0888:	bpp = 4;	break;
		default:					return 1;
	}

	if (ancho <= 0 || alto <= 0)
		return 1;

	fp = fopen(ruta, "wb");

	if (!fp)
	{
		fprintf(stderr, "no se pudo crear %s\n", ruta);
		return 1;
	}

	/* Cada fila del BMP se alinea a 4 bytes. */
	relleno = (4 - ((ancho * 3) % 4)) % 4;
	tam_datos = (long) (ancho * 3 + relleno) * alto;

	memset(cabecera, 0, sizeof(cabecera));
	cabecera[0] = 'B';	cabecera[1] = 'M';
	*(DWORD *) &cabecera[2]  = (DWORD) (54 + tam_datos);
	*(DWORD *) &cabecera[10] = 54;
	*(DWORD *) &cabecera[14] = 40;				/* BITMAPINFOHEADER */
	*(long  *) &cabecera[18] = ancho;
	*(long  *) &cabecera[22] = alto;			/* positivo: de abajo hacia arriba */
	*(WORD  *) &cabecera[26] = 1;				/* planos */
	*(WORD  *) &cabecera[28] = 24;
	*(DWORD *) &cabecera[34] = (DWORD) tam_datos;

	fwrite(cabecera, 1, sizeof(cabecera), fp);

	for (y = alto - 1; y >= 0; y--)
	{
		unsigned char * fila = origen + (long) y * ancho * bpp;

		for (x = 0; x < ancho; x++)
		{
			unsigned char * p = fila + x * bpp;
			unsigned char	rgb[3];		/* el BMP los guarda como B, G, R */

			switch (screenformat)
			{
				case FRAMEBUFFER_ARGB0555:
				{
					WORD v = *(WORD *) p;

					rgb[0] = (unsigned char) (((v      ) & 0x1F) << 3);
					rgb[1] = (unsigned char) (((v >>  5) & 0x1F) << 3);
					rgb[2] = (unsigned char) (((v >> 10) & 0x1F) << 3);
				}
				break;

				case FRAMEBUFFER_RGB565:
				{
					WORD v = *(WORD *) p;

					rgb[0] = (unsigned char) (((v      ) & 0x1F) << 3);
					rgb[1] = (unsigned char) (((v >>  5) & 0x3F) << 2);
					rgb[2] = (unsigned char) (((v >> 11) & 0x1F) << 3);
				}
				break;

				default:	/* RGB888 y ARGB0888 ya vienen en B, G, R */
					rgb[0] = p[0];
					rgb[1] = p[1];
					rgb[2] = p[2];
				break;
			}

			fwrite(rgb, 1, 3, fp);
		}

		for (x = 0; x < relleno; x++)
			fputc(0, fp);
	}

	fclose(fp);

	fprintf(stderr, "framebuffer volcado a %s (%dx%d, formato %d)\n",
		ruta, ancho, alto, screenformat);

	return 0;
}

void DibujarFramebuffer()
{
	/* Formato y tipo de GL que corresponden al formato del framebuffer. */
	GLenum formato, tipo;

	/* Lo ultimo que se reservo, para no rehacer la textura cada frame. */
	static int ultimo_formato = -1;
	static int ultimo_ancho   = 0;
	static int ultimo_alto    = 0;

	int traza_este_frame = 0;

	switch(screenformat)
	{
		case FRAMEBUFFER_ARGB0555:	formato = GL_RGB;		tipo = GL_UNSIGNED_SHORT_5_5_5_1;	break;
		case FRAMEBUFFER_RGB565:	formato = GL_RGB;		tipo = GL_UNSIGNED_SHORT_5_6_5;		break;
		case FRAMEBUFFER_RGB888:	formato = GL_RGB;		tipo = GL_UNSIGNED_BYTE;			break;
		case FRAMEBUFFER_ARGB0888:	formato = GL_BGRA_EXT;	tipo = GL_UNSIGNED_INT_8_8_8_8_REV;	break;

		default:
		logxmsg(LOG_PVR, "DibujarFramebuffer: formato %d desconocido\n", screenformat);
		return;
	}

	if (traza_activa)
	{
		static int vueltas = 0;

		traza_este_frame = (vueltas < 2 || vueltas % 300 == 0);

		/* Los primeros frames y despues de tanto en tanto: lo interesante pasa
		   cuando el demo ya lleva un rato dibujando, no al arrancar. */
		if (traza_este_frame)
		{
			/* De donde lee dcemu, en que formato, y que hay ahi. */
			BYTE * base = get_memory_pointer(0xA5000000 + pvr_fb_r_sof1);
			int i;

			fprintf(stderr, "traza: framebuffer %d: sof1=%08x formato=%d %dx%d tex %dx%d\n",
				vueltas, (unsigned) pvr_fb_r_sof1, screenformat,
				screenancho, screenheight,
				(int) screentexwidth, (int) screentexheight);

			fprintf(stderr, "traza:   primeros 16 bytes:");
			for (i = 0; i < 16; i++)
				fprintf(stderr, " %02x", base[i]);
			fprintf(stderr, "\n");

			fprintf(stderr, "traza:   el guest escribio %u veces / %u bytes, "
				"offsets %lx..%lx, ventanas %04x\n",
				(unsigned) traza_video_escrituras, (unsigned) traza_video_bytes,
				traza_video_min, traza_video_max,
				(unsigned) traza_video_ventanas);

			/* Y por cual: la de 32 bits (0x05, 0x13) deja el bloque plano y la
			   de 64 (0x04, 0x11) lo reparte entre los dos bancos. */
			fprintf(stderr, "traza:   por ventana:");
			for (i = 0; i < 0x20; i++)
				if (traza_video_por_ventana[i])
					fprintf(stderr, " %02x=%u", i,
						(unsigned) traza_video_por_ventana[i]);
			fprintf(stderr, "\n");
		}

		vueltas++;
	}

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, background_texture);

	/*
		La textura tiene que ser potencia de dos y el framebuffer del guest no lo
		es, asi que son 1024x512 para 640x480.

		Antes esto hacia un glTexImage2D() de 1024x512 leyendo directo del
		framebuffer con GL_UNPACK_ROW_LENGTH en 640: GL pedia 512 filas de 1024
		pixeles avanzando 640 por fila, o sea que leia muy por fuera del
		framebuffer y solapaba filas entre si. El resultado no era la pantalla
		del guest sino un patron regular armado con lo que hubiera en la RAM de
		video -- tanto que dos demos distintos mostraban la misma imagen.

		Ahora la textura se reserva entera una sola vez y cada frame se sube solo
		el rectangulo que existe de verdad. Las coordenadas del quad de abajo ya
		limitan el muestreo a ese rectangulo.
	*/
	if (ultimo_formato != screenformat
	||  ultimo_ancho   != (int) screentexwidth
	||  ultimo_alto    != (int) screentexheight)
	{
		logxmsg(LOG_PVR, "DibujarFramebuffer: reservando textura %dx%d, formato %d\n",
			(int) screentexwidth, (int) screentexheight, screenformat);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
			(GLsizei) screentexwidth, (GLsizei) screentexheight, 0,
			formato, tipo, NULL);

		ultimo_formato = screenformat;
		ultimo_ancho   = (int) screentexwidth;
		ultimo_alto    = (int) screentexheight;
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, screenancho);

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
		screenancho, screenheight,
		formato, tipo,
		get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
/* Con el test de profundidad apagado la z solo importa para el recorte: tiene
   que caer dentro del +-PROFUNDIDAD_RANGO del glOrtho. Era -1000. */
#define PROFUNDIDAD (-1.0f)
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, screenheight / screentexheight); glVertex3f(0.0f, (float) screenheight, PROFUNDIDAD);
	glTexCoord2f(screenancho / screentexwidth, screenheight / screentexheight); glVertex3f((float) screenancho, (float) screenheight, PROFUNDIDAD);
	glTexCoord2f(screenancho / screentexwidth, 0.0f); glVertex3f((float) screenancho, 0.0f, PROFUNDIDAD);
	glTexCoord2f(0.0f, 0.0f); glVertex3f(0.0f, 0.0f, PROFUNDIDAD);
	glEnd();

	/* Diagnostico temporal: que quedo de verdad en el buffer de GL. Si la RAM de
	   video tiene la imagen y esto sale negro, el problema es la subida o el
	   quad; si esto tiene color y la pantalla no, es el swap o la captura. */
	if (traza_este_frame)
	{
		static const int px[4] = {  20, 320, 100, 600 };
		static const int py[4] = {  20, 240, 400, 460 };
		GLenum err;
		int i;

		fprintf(stderr, "traza:   GL leyo:");
		for (i = 0; i < 4; i++)
		{
			BYTE p[4] = { 0, 0, 0, 0 };

			/* glReadPixels tiene el origen abajo, el quad arriba. */
			glReadPixels(px[i], screenheight - 1 - py[i], 1, 1,
				GL_RGBA, GL_UNSIGNED_BYTE, p);
			fprintf(stderr, " (%d,%d)=%02x%02x%02x", px[i], py[i], p[0], p[1], p[2]);
		}
		err = glGetError();
		fprintf(stderr, "  glGetError=%04x\n", (unsigned) err);
	}

	glEnable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
}

/*
	--captura-gl desde el camino 2D.

	El del PVR captura en terminar_escena() y solo si la escena trajo tiras. Un
	juego que dibuja escribiendo el framebuffer --un emulador de arcade, por
	ejemplo-- no manda ninguna, asi que no se capturaba nunca y su pantalla no
	habia forma de verla sin mirar la ventana. Se captura solo mientras no haya
	habido ni una escena del PVR, para no pisar el volcado bueno de una demo 3D
	con un cuadro 2D vacio.
*/
void capturar_gl_framebuffer(void)
{
	if (opciones.captura_gl == NULL || traza_rendidas > 0)
		return;

	if (getenv("DCEMU_CAPTURA_TODAS"))
	{
		static int	nf = 0;
		char		nom[128];

		snprintf(nom, sizeof(nom), "f%04d-%s", nf++, opciones.captura_gl);
		volcar_gl(nom);
	}
	else
		volcar_gl(opciones.captura_gl);
}

int glinit(void)
{
	int i;

	logxmsg(LOG_PVR, "glinit: entrando");
	
	init_twiddletab();

	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute( SDL_GL_BUFFER_SIZE, 32 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	/* Los volumenes modificadores se marcan en el buffer de plantilla, que hay
	   que pedir: por omision el contexto no trae ninguno. Dos bits alcanzan --
	   uno para el volumen opaco y otro para el translucido. */
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

	/*
		**No se pide SDL_GL_DEPTH_SIZE.** Pedir 24 bits parece lo obvio -- la
		profundidad comprime todas las z del guest en una parte chica del rango
		y la precision importa, ver profundidad_ta() --, pero pedirlo junto con
		la plantilla y BUFFER_SIZE 32 hace que SDL caiga en otro formato de
		pixel: pvr-texture_render pasa de 47539 colores a 2048. Sin el pedido,
		el contexto que da igual sirve.
	*/

	outputscreen = SDL_SetVideoMode(800, 600, 32, SDL_HWSURFACE|SDL_OPENGL|SDL_HWACCEL);
	
	if (outputscreen == NULL)
	{
		logxmsg(LOG_PVR, "screeninit: outputscreen = NULL!!!!\n");
		exit(1);
	}

	/*
		Que contexto dio SDL de verdad, que no tiene por que ser el que se pidio.
		Importa sobre todo la profundidad: aun con el log2 de profundidad_ta()
		las z de una demo ocupan una fraccion del rango, asi que con 16 bits dos
		capas contiguas caen en el mismo valor y la de arriba desaparece --
		pvr_rtt_sized perdia asi sus dos marcas.
	*/
	if (traza_activa)
	{
		int prof = 0, stencil = 0;

		SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &prof);
		SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil);

		fprintf(stderr, "traza: contexto GL: profundidad %d bits, plantilla %d\n",
			prof, stencil);
	}

    SDL_WM_SetCaption(titulo_ventana, NULL);
    SDL_WM_SetIcon(SDL_LoadBMP("dcemu.bmp"), NULL);
	SDL_EnableUNICODE(1);

	gui_init();

	logxmsg(LOG_PVR, "glinit: seteando viewport a %dx%d\n", 800, 600);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glViewport(0, 0, 800, 600);
	
//	glTranslatef(0, 0, 100.0f);
	
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);		// This Will Clear The Background Color To Black
	glClearDepth(0.0);				// Enables Clearing Of The Depth Buffer
	glDepthFunc(GL_LESS);				// The Type Of Depth Test To Do
	glEnable(GL_DEPTH_TEST);			// Enables Depth Testing
	glShadeModel(GL_SMOOTH);			// Enables Smooth Color Shading

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();				// Reset The Projection Matrix
//	gluPerspective(45.0f,(GLfloat)800/(GLfloat)600,0.001f,1024.0f);	// Calculate The Aspect Ratio Of The Window

//	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

    	glGenTextures(MAX_TEXTURE_COUNT, pvr_textures);
   	 glGenTextures(1, &background_texture);
/*		glGenTextures(1, &pvr_textures[0]);
	glGenTextures(1, &pvr_textures[1]); */
	glBindTexture(GL_TEXTURE_2D, background_texture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering
	
/*	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP); */

	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	for (i = 0; i < MAX_TEXTURE_COUNT; i++)
	{
		glBindTexture(GL_TEXTURE_2D, pvr_textures[i]);
/*		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering */
	}

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);			// Clear The Screen And The Depth Buffer

	logxmsg(LOG_PVR, "glinit: saliendo");

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer			(3, GL_FLOAT,		   sizeof(vertex), &VertexBuffer);
	glColorPointer			(4, GL_FLOAT,  sizeof(vertex), &VertexBuffer[0].r);
	glTexCoordPointer		(4, GL_FLOAT,		   sizeof(vertex), &VertexBuffer->t1);

	return 0;
}

// parametros globales:
//		screenformat
//		screenwidth
//		screenbits
//		screenheight

int screeninit(void)
{
	Uint32 rmask, gmask, bmask, amask;
	Uint32 glrmask, glgmask, glbmask, glamask;
	int asize, rsize, gsize, bsize;

	logxmsg(LOG_PVR, "screeninit: entrando a screeninit()\n");

//	logxmsg(LOG_PVR, "bitdepth del framebuffer: %d bits\n", screenbits);

	switch(screenformat)
	{
		case FRAMEBUFFER_ARGB0555:
		{
			logxmsg(LOG_PVR, "screeninit: formato del framebuffer: argb0555\n");
			amask = 0;
			rmask = 0x1f << 10;
			gmask = 0x1f << 5;
			bmask = 0x1f;
			glamask = 0;
			glrmask = 0x003e;
			glgmask = 0x07c0;
			glbmask = 0xf800;
			asize = 0;
			rsize = 5;
			gsize = 5;
			bsize = 5;
		}
		break;

		case FRAMEBUFFER_RGB565:
		{
			logxmsg(LOG_PVR, "screeninit: formato del framebuffer: rgb565\n");
			amask = 0;
			rmask = 0x1f << 11;
			gmask = 0x3f << 5;
			bmask = 0x1f;
			glamask = 0;
			glrmask = 0x001f;
			glgmask = 0x07e0;
			glbmask = 0xf800;
			asize = 0;
			rsize = 5;
			gsize = 6;
			bsize = 5;
		}
		break;
		
		case FRAMEBUFFER_RGB888:
		{
			logxmsg(LOG_PVR, "screeninit: formato del framebuffer: rgb888\n");
			amask = 0;
			rmask = 0xff << 16;
			gmask = 0xff << 8;
			bmask = 0xff;
			glamask = 0;
			glrmask = 0x0000ff00;
			glgmask = 0x00ff0000;
			glbmask = 0xff000000;
			asize = 0;
			asize = 8;
			gsize = 8;
			bsize = 8;
		}
		break;
		
		case FRAMEBUFFER_ARGB0888:
		{
			logxmsg(LOG_PVR, "screeninit: formato del framebuffer: argb0888\n");
			amask = 0;
			rmask = 0xff << 16;
			gmask = 0xff << 8;
			bmask = 0xff;
			glamask = 0;
			glrmask = 0x00ff0000;
			glgmask = 0x0000ff00;
			glbmask = 0x000000ff;
			asize = 0;
			rsize = 8;
			gsize = 8;
			bsize = 8;
		}
		break;
		
		default:
		{
			logxmsg(LOG_PVR, "screeninit: formato del framebuffer desconocido: %d\n", screenformat);
			amask = 0xff << 24;
			rmask = 0xff << 16;
			gmask = 0xff << 8;
			bmask = 0xff;
			glamask = 0xff000000;
			glrmask = 0x00ff0000;
			glgmask = 0x0000ff00;
			glbmask = 0x000000ff;
			asize = 8;
			rsize = 8;
			gsize = 8;
			bsize = 8;
		}
		break;
	}

	screenancho = screenwidth * ((screenbits == 32) ? 1 : 2);

	// definamos el tama�o de la textura
	if (screenancho > 512)
		screentexwidth = 1024.0f;
	else
	if (screenancho > 256)
		screentexwidth = 512.0f;

	if (screenheight > 512)
		screentexheight = 1024.0f;
	else
	if (screenheight > 256)
		screentexheight = 512.0f;
	else
	if (screenheight > 128)
		screentexheight = 256.0f;

	if (screen != NULL)
		SDL_FreeSurface(screen);

	screen = SDL_CreateRGBSurface(SDL_HWSURFACE, screenancho, screenheight, 32, rmask, gmask, bmask, amask);

	if (screen == NULL)
	{
		logxmsg(LOG_PVR, "screeninit: screen == NULL!!!!\n");
		exit(1);
	}

	logxmsg(LOG_PVR, "screeninit: glOrtho %dx%d\n", screenancho, screenheight);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	/*
		near y far van invertidos a proposito. glOrtho mapea z de ojo con signo
		cambiado (z' = -2z/(far-near)), asi que con near/far en el orden
		natural una z de vertice mas grande daba una profundidad MENOR: la z
		del TA es 1/w -- mayor es mas cerca -- y sus pruebas van en esos
		terminos (GREATER deja pasar lo mas cerca), o sea que el orden quedaba
		al reves y una tira cercana perdia contra una lejana ya dibujada.
		pvr-fb_tex lo midio: la mascara en z=1 dejaba invisible al cubo en z=4.
		Invertirlos hace la profundidad creciente con z y el orden sale solo,
		que es lo que este comentario siempre pretendio.

		El rango es el de profundidad_ta(), que comprime 1/w con un log2: ver
		su comentario. Con el rango lineal de antes (+-32768) la ciudad de un
		juego entraba en veinte pasos del buffer.
	*/
	glOrtho(0, screenancho, screenheight, 0, PROFUNDIDAD_RANGO, -PROFUNDIDAD_RANGO);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	logxmsg(LOG_PVR, "screeninit: saliendo\n");

	//init_sOglP();

	return 0;
}

void DibujarGL(SDL_Surface * sfc)
{
	glEnable(GL_TEXTURE_2D);
		
	glBindTexture(GL_TEXTURE_2D, background_texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, sfc->w);

	SDL_LockSurface(sfc);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1024, 512, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, sfc->pixels);
	SDL_UnlockSurface(sfc);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 480.0 / 512.0); glVertex3f(0.0f, (float) screenheight, -1.0f);
	glTexCoord2f(640.0 / 1024.0, 480.0 / 512.0); glVertex3f((float) screenancho, (float) screenheight, -1.0f);
	glTexCoord2f(640.0 / 1024.0, 0.0f); glVertex3f((float) screenancho, 0.0f, -1.0f);
	glTexCoord2f(0.0f, 0.0f); glVertex3f(0.0f, 0.0f, -1.0f);
	glEnd();
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	// fin textura 1024

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	logxmsg(LOG_PVR, "DibujarGL: SDL_GL_SwapBuffers\n");
	SDL_GL_SwapBuffers();
	fps_marcar_cuadro();
}
