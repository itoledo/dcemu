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

/*
	El color de cara vigente, en A,R,G,B. Los vertices en modo intensidad no
	traen color propio: traen un multiplicador que se aplica a este.

	Sobrevive al encabezado a proposito -- el modo intensidad 2 usa el que dejo
	el ultimo poligono en modo intensidad 1 --, asi que solo se pisa cuando
	llega un encabezado que lo trae. Blanco de partida, que es lo que deja la
	intensidad tal cual si el guest nunca lo puso.
*/
static float color_cara[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

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
DWORD pvr_ta_itp_current = 0x0;
DWORD pvr_ta_isp_base = 0x0;
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
};

typedef struct cached_texture cached_texture;
cached_texture cached_textures[MAX_TEXTURE_COUNT];
int cur_tex_count = 0;

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
				memwrite_fisico(0xA5000000 | destino, pixel, 4);
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

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, modo);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, modo);
}

void get_texture(int usize, int vsize, DWORD memorypos, int twiddled, int vq,int strip)
{
	Uint16 * q, * v;
	int i, j;
	DWORD bpp    = TriangleStrip[strip].texture.pvr_texture_bpp;
	DWORD paleta = TriangleStrip[strip].texture.pvr_texture_paleta;
	DWORD stride = TriangleStrip[strip].texture.pvr_texture_stride;

	if (cur_tex_count > 0)
    	for (i = 0; i < cur_tex_count; i++)
    	{
    		if (cached_textures[i].usize == usize
    		&&  cached_textures[i].vsize == vsize
    		&&  cached_textures[i].memorypos == memorypos
    		&&  cached_textures[i].bpp == bpp
    		&&  cached_textures[i].paleta == paleta)
    		{
          		logxmsg(LOG_PVR, "get_texture: retornando textura %d en cache\n", i);
 			glBindTexture(GL_TEXTURE_2D, cached_textures[i].texture);
			aplicar_filtros(strip);
    			return;
    		}
    	}

	logxmsg(LOG_PVR, "get_texture: creando textura %d\n", cur_tex_count);

	// si llegamos aqu�, la textura no est�.
	cached_textures[cur_tex_count].usize = usize;
	cached_textures[cur_tex_count].vsize = vsize;
	cached_textures[cur_tex_count].memorypos = memorypos;
	cached_textures[cur_tex_count].texture = pvr_textures[cur_tex_count];
	cached_textures[cur_tex_count].bpp = bpp;
	cached_textures[cur_tex_count].paleta = paleta;
	v = (Uint16 *) get_memory_pointer(memorypos | 0xA5000000);

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
		BYTE * index = (BYTE *) get_memory_pointer((memorypos + 0x0800) | 0xA5000000);

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
		cached_textures[cur_tex_count].data = v;
		cached_textures[cur_tex_count].twiddled = false;
	}

	   glBindTexture(GL_TEXTURE_2D, cached_textures[cur_tex_count].texture);
	   aplicar_filtros(strip);

    logxmsg(LOG_PVR, "get_texture: size %dx%d, mempos %x\n", usize, vsize, memorypos | 0xA5000000);
   glTexImage2D(GL_TEXTURE_2D, 0, TriangleStrip[strip].texture.pvr_texture_components, usize, vsize, 0, TriangleStrip[strip].texture.pvr_texture_pixelformat, TriangleStrip[strip].texture.pvr_texture_pixelpack, cached_textures[cur_tex_count].data);

    cur_tex_count++;

	return;
}


void limpiar_pantalla()
{
	/*
		**glClear del buffer de profundidad lo enmascara glDepthMask.** Si la
		ultima tira de la escena anterior venia con "Z Write Disable" puesto -- y
		la geometria translucida siempre lo trae --, la mascara quedo en FALSE y
		el clear no borra nada: la escena siguiente arranca con las
		profundidades de la anterior y descarta lo que caiga detras de ellas.
	*/
	glDepthMask(GL_TRUE);

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
int compare(const void *  f,const void * s)
{
	const TSI * a = (const TSI *) f;
	const TSI * b = (const TSI *) s;

	if (a->type != b->type)
		return (a->type > b->type) ? 1 : -1;

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

	Se usan dos bits, porque hay dos volumenes independientes: el de la lista 1
	afecta a la lista opaca (0) y el de la lista 3 a la translucida (2).
*/
#define PLANTILLA_OPACO		0x1
#define PLANTILLA_TRANS		0x2

/* Que bit le toca a una tira segun la lista en que vino. */
static GLuint plantilla_bit(DWORD tipo_lista)
{
	return (tipo_lista == 2 || tipo_lista == 3) ? PLANTILLA_TRANS : PLANTILLA_OPACO;
}

/*
	Marca en la plantilla la region que cubren los triangulos de volumen.

	**Es una union de triangulos, no un volumen de verdad.** El chip resuelve
	volumenes cerrados en 3D contando caras delanteras y traseras; aca cada
	triangulo prende su bit y se acabo. Alcanza para lo que hacen las demos de
	KOS -- un cuadrado plano en coordenadas de pantalla, dos triangulos -- y
	para cualquier sombra proyectada sobre el plano, que es el uso corriente.
	Un volumen convexo cerrado visto desde dentro saldria mal.

	La instruccion 2 ("cerrar excluyendo") apaga el bit en vez de prenderlo, que
	es lo mas parecido a lo que hace el chip sin llevar la cuenta.
*/
static void marcar_volumenes(void)
{
	DWORD v;

	if (vol_count == 0)
		return;

	glEnable(GL_STENCIL_TEST);
	glClearStencil(0);
	glClear(GL_STENCIL_BUFFER_BIT);

	/* Solo la plantilla: ni color ni profundidad. El volumen no se ve. */
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	glStencilFunc(GL_ALWAYS, 0xFF, 0xFF);

	glBegin(GL_TRIANGLES);

	for (v = 0; v < vol_count; v++)
	{
		const VolTri * t = &VolumeBuffer[v];
		GLuint bit = plantilla_bit(t->lista);
		int k;

		/* glStencilMask limita la escritura al bit de esta lista, asi que los
		   dos volumenes no se pisan. Va fuera de glBegin/glEnd. */
		glEnd();
		glStencilMask(bit);
		glStencilOp(GL_KEEP, GL_KEEP,
			(t->instruccion == 2) ? GL_ZERO : GL_REPLACE);
		glBegin(GL_TRIANGLES);

		for (k = 0; k < 3; k++)
			glVertex3f(t->x[k], t->y[k], t->z[k]);
	}

	glEnd();

	glStencilMask(0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

/*
	Deja la prueba de plantilla como la necesita la tira `i`: `dentro` en 0
	dibuja fuera del volumen y en 1 dentro. Una tira que ningun volumen afecta
	se dibuja entera.
*/
static void plantilla_para(int i, int dentro)
{
	GLuint bit;

	if (!TriangleStrip[i].volumen || vol_count == 0)
	{
		glDisable(GL_STENCIL_TEST);
		return;
	}

	bit = plantilla_bit(TriangleStrip[i].type);

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(dentro ? GL_EQUAL : GL_NOTEQUAL, bit, bit);
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
		glTexCoordPointer(2, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].u1);
	}
	else
	{
		glColorPointer   (4, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].r);
		glTexCoordPointer(2, GL_FLOAT, sizeof(vertex), &VertexBuffer[0].t1);
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
}

static void dibujar_escena(void);
static void terminar_escena(void);
static int  render_a_textura(void);
static void volcar_a_memoria(DWORD destino, DWORD dst_w, DWORD dst_h,
							 DWORD filas_bytes, int formato,
							 DWORD src_w, DWORD src_h);

void cb_tastart(DWORD addr, void * p, size_t size)
{
	if (traza_activa)
	{
		traza_ultimas[traza_rendidas % TRAZA_ULTIMAS] = (int) strip_count;
		traza_rendidas++;
	}

	/* Con --traza-mem, reportar las primeras rendidas: cuantas tiras llegaron y
	   cuantos vertices. Es la pregunta que hay que contestar antes de mirar
	   matrices o test de profundidad -- si no llega geometria, el resto sobra. */
	if (traza_activa)
	{
		static int rendidas = 0;

		if (rendidas < 3)
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

			fprintf(stderr, "traza:   encuadre: x %.1f..%.1f  y %.1f..%.1f  z %g..%g"
				"  (pantalla %dx%d)\n",
				traza_ta_min[0], traza_ta_max[0],
				traza_ta_min[1], traza_ta_max[1],
				traza_ta_min[2], traza_ta_max[2],
				screenancho, screenheight);
		}

		traza_ta_vertices = 0;
		traza_ta_fin_tira = 0;
		memset(traza_ta_tipos, 0, sizeof(traza_ta_tipos));

		rendidas++;
	}

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

	DWORD dw;
	
	// transparent polygons should appear first
	
	qsort(TriangleStrip,strip_count,sizeof(TSI),compare);

	/* El estado con que sale cada tira. Si la geometria llega bien y no se ve
	   nada, esto es lo que queda por mirar: es lo que descubrio que a conio se
	   le iba la pantalla entera en el culling. */
	if (traza_activa && strip_count > 0)
	{
		static int volcadas = 0;

		if (volcadas < 2)
		{
			int t;

			for (t = 0; t < strip_count && t < 8; t++)
			{
				fprintf(stderr, "traza:   tira %d: tipo=%d idx=%d n=%d alpha=%d "
					"blend=%04x/%04x zfunc=%04x zwrite=%d cull=%d tex=%08x %dx%d tw=%d vq=%d fmt=%04x "
					"bpp=%d stride=%d\n",
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
					(unsigned) TriangleStrip[t].texture.pvr_texture_pixelpack,
					(int) TriangleStrip[t].texture.pvr_texture_bpp,
					(int) TriangleStrip[t].texture.pvr_texture_stride);

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
		limpiar_texturas();
		return;
	}

	dibujar_escena();
	terminar_escena();
}

/*
	Dibuja las tiras acumuladas. Sale de cb_tastart() porque el render a
	textura necesita exactamente lo mismo pero con otro destino y otro tamano.
*/
static void dibujar_escena(void)
{
	DWORD i;
	DWORD debug = TriangleStrip[1].type;

	/* Los volumenes se marcan antes de dibujar nada: la prueba de plantilla
	   decide, tira por tira, con que juego de parametros sale cada pixel. */
	marcar_volumenes();

	for(i=0; i < strip_count; i++)
		{

			if(debug < TriangleStrip[i].type)
			{
				printf("Oops Debug - %d Type %d\n",debug,TriangleStrip[i].type);
			}
			else debug = TriangleStrip[i].type;

			/*
				Fuera del volumen. Si a esta tira la afecta uno, se dibuja solo
				donde el bit correspondiente esta apagado y la segunda pasada
				cubre el resto; asi cada pixel sale una sola vez, que es lo que
				importa cuando hay mezcla alfa de por medio.
			*/
			plantilla_para(i, 0);

			//printf(" Index %d count %d\n",TriangleStrip[i].index,TriangleStrip[i].count);

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
	

			if(TriangleStrip[i].alpha)
			{
				glEnable(GL_BLEND);
			}
			else
			{
				glDisable(GL_BLEND);
			}

			glBlendFunc(TriangleStrip[i].pvr_srcblend, TriangleStrip[i].pvr_dstblend);

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

			glDrawArrays(GL_TRIANGLE_STRIP,TriangleStrip[i].index,TriangleStrip[i].count);

			/*
				Dentro del volumen: la misma geometria con el juego 1. Se
				repite el dibujo en vez de hacer otra vuelta entera al final
				porque asi hereda el estado que se acaba de programar --
				blend, culling, profundidad y la textura ya ligada.
			*/
			if (TriangleStrip[i].volumen && vol_count > 0)
			{
				plantilla_para(i, 1);
				juego_de_parametros(1);

				glDrawArrays(GL_TRIANGLE_STRIP,TriangleStrip[i].index,TriangleStrip[i].count);

				juego_de_parametros(0);
			}
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
	PVR. Lo usan las dos rutas que necesitan que la escena exista en memoria: el
	render a textura y el volcado del framebuffer.

	`src_*` es el rectangulo de GL que se lee y `dst_*` el tamano con que se
	guarda; cuando no coinciden se remuestrea por vecino mas cercano, que es lo
	que hace falta porque la ventana no mide lo mismo que la pantalla emulada.
*/
static void volcar_a_memoria(DWORD destino, DWORD dst_w, DWORD dst_h,
							 DWORD filas_bytes, int formato,
							 DWORD src_w, DWORD src_h)
{
	unsigned char *	pixeles;
	DWORD			x, y;

	if (dst_w == 0 || dst_h == 0 || src_w == 0 || src_h == 0)
		return;

	pixeles = (unsigned char *) malloc((size_t) src_w * src_h * 4);

	if (pixeles == NULL)
		return;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, src_w, src_h, GL_RGBA, GL_UNSIGNED_BYTE, pixeles);

	if (filas_bytes == 0)
		filas_bytes = dst_w * 2;

	for (y = 0; y < dst_h; y++)
	{
		/* glReadPixels entrega de abajo hacia arriba y tanto una textura como
		   el framebuffer se guardan de arriba hacia abajo. */
		DWORD					sy = (src_h - 1) - (y * src_h) / dst_h;
		const unsigned char *	fila = pixeles + (size_t) sy * src_w * 4;
		DWORD					base = destino + y * filas_bytes;

		for (x = 0; x < dst_w; x++)
		{
			DWORD sx = (x * src_w) / dst_w;
			DWORD r = fila[sx * 4 + 0];
			DWORD g = fila[sx * 4 + 1];
			DWORD b = fila[sx * 4 + 2];
			DWORD a = fila[sx * 4 + 3];
			WORD  texel;

			switch (formato)
			{
				case 0:		/* ARGB1555 */
					texel = (WORD) (((a >> 7) << 15) | ((r >> 3) << 10) |
									((g >> 3) << 5) | (b >> 3));
					break;

				case 2:		/* ARGB4444 */
					texel = (WORD) (((a >> 4) << 12) | ((r >> 4) << 8) |
									((g >> 4) << 4) | (b >> 4));
					break;

				default:	/* 1: RGB565, y el resto se aproxima con el */
					texel = (WORD) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
					break;
			}

			memwrite_fisico(0xA5000000 + base + x * 2, &texel, 2);
		}
	}

	free(pixeles);
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

	/* La escena, a su propio tamano. */
	glViewport(0, 0, ancho, alto);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glOrtho(0, ancho, alto, 0, -32768.0, 32768.0);

	/* La mascara va **antes** del clear: glClear de la profundidad la respeta.
	   Ver limpiar_pantalla(). */
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	dibujar_escena();

	volcar_a_memoria(destino, ancho, alto, filas_bytes, formato, ancho, alto);

	/* Y se deja todo como estaba: la escena siguiente va a la pantalla. */
	glViewport(0, 0, outputscreen ? outputscreen->w : 800,
					 outputscreen ? outputscreen->h : 600);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glOrtho(0, screenancho, screenheight, 0, -32768.0, 32768.0);

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
	if (opciones.captura_gl != NULL && strip_count > 0)
		volcar_gl(opciones.captura_gl);

	strip_count = 0;

	total_polygon_count = 0;

	vol_count = 0;

	gui_refresh();

	logxmsg(LOG_PVR, "cb_tastart: SDL_GL_SwapBuffers\n");
	SDL_GL_SwapBuffers();
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

	limpiar_texturas();
//	cur_tex_count = 0;

	// ???
	memread_fisico(0xa05f8128, &dw, sizeof(DWORD)); // leer TA_ISP_BASE
	memwrite_fisico(0xa05f8138, &dw, sizeof(DWORD)); // grabar en TA_ITP_CURRENT

	limpiar_pantalla();

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
		pvr_listdone |= (1 << pvr_registering);
		intc_add(pvr_lists[pvr_registering], 100);
		pvr_registering = -1;
	}

	if (pvr_listdone == pvr_registered)
	{
//		logxmsg(LOG_PVR, "pcw: taListEnd: RENDER_DONE\n");
		if (traza_activa)
			fprintf(stderr, "traza: fin de lista: todas hechas, RENDERDONE\n");
		intc_add(ASIC_EVT_PVR_RENDERDONE, 200);
	}
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
	taPolyModifier();

	sprite_color = ta_address_pointer[4];

	/* El color de offset de un sprite tambien va en el encabezado, y es de donde
	   salen los cuatro parametros del bump mapping. */
	TriangleStrip[strip_count].texture.pvr_texture_bump_param = ta_address_pointer[5];

	logxmsg(LOG_PVR, "pcw: Sprite, color %08x\n", (unsigned) sprite_color);
}

void taPolyModifier()
{
	TA.control = *ta_address_pointer;
	
	logxmsg(LOG_PVR, "pcw: Polygon or Modifier Volume\n");

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
		color. Va en la segunda mitad del encabezado, o sea que solo existe en
		los de 64 bytes.

		En modo intensidad 2 (col_type 3) el encabezado NO lo trae: se usa el
		que dejo el ultimo poligono en modo intensidad 1. Por eso se guarda en
		un estatico y solo se pisa cuando llega uno nuevo.
	*/
	if (global_parameter == TA_GLOBAL_POLY1 || global_parameter == TA_GLOBAL_POLY2 ||
		global_parameter == TA_GLOBAL_POLY4)
	{
		memcpy(&color_cara[0], &ta_address_pointer[8], sizeof(float) * 4);

		/* En POLY4 la segunda mitad es el color de cara del otro volumen; en
		   POLY2 es el color de offset. Ni uno ni otro se usan todavia. */
	}

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

	v->x = xyz[0];
	v->y = xyz[1];

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
	*/
	v->z = xyz[2];

	/* Sin coordenadas de textura mientras el tipo no diga otra cosa: si no,
	   quedan las del vertice anterior. */
	v->t1 = 0.0f;
	v->t2 = 0.0f;

	return v;
}

/* Color empaquetado: una palabra ARGB, alpha en 31-24. */
static void vertice_color(vertex * v, DWORD argb)
{
	v->a = ((argb >> 24) & 0xFF) / 255.0;
	v->r = ((argb >> 16) & 0xFF) / 255.0;
	v->g = ((argb >> 8)  & 0xFF) / 255.0;
	v->b = ((argb >> 0)  & 0xFF) / 255.0;
}

/* Color en punto flotante: cuatro floats A, R, G, B desde la palabra `w`. */
static void vertice_color_flotante(vertex * v, int w)
{
	float c[4];

	memcpy(c, &ta_address_pointer[w], sizeof(float) * 4);

	v->a = c[0];
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

	v->a = color_cara[0];
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
			ve->z = z[s];		/* tal cual, igual que vertice_nuevo() */
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
					t->z[k] = p[k * 3 + 2];
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

	if (TA.registers.pcw_end_of_strip)
	{
#ifdef DEBUG_VERTEX_NEW
		logxmsg(LOG_PVR, "VERTEX: end-of-strip\n");
#endif
	
		TriangleStrip[strip_count].count = ((total_polygon_count+1) - TriangleStrip[strip_count].index);

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
#define PROFUNDIDAD (-1000.0f)
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
		**No se pide SDL_GL_DEPTH_SIZE.** Pedir 24 bits parece lo obvio -- el
		glOrtho de screeninit() abarca -32768..32768 y aplasta las z de una demo
		en una franja minima alrededor de 0,5, asi que la precision importa --,
		pero pedirlo junto con la plantilla y BUFFER_SIZE 32 hace que SDL caiga
		en otro formato de pixel: pvr-texture_render pasa de 47539 colores a
		2048. Sin el pedido, el contexto que da igual sirve.
	*/

	outputscreen = SDL_SetVideoMode(800, 600, 32, SDL_HWSURFACE|SDL_OPENGL|SDL_HWACCEL);
	
	if (outputscreen == NULL)
	{
		logxmsg(LOG_PVR, "screeninit: outputscreen = NULL!!!!\n");
		exit(1);
	}

	/*
		Que contexto dio SDL de verdad, que no tiene por que ser el que se pidio.
		Importa sobre todo la profundidad: el glOrtho de screeninit() abarca
		-32768..32768 y aplasta las z de una demo en una franja minima alrededor
		de 0,5, asi que con 16 bits dos capas contiguas caen en el mismo valor y
		la de arriba desaparece -- pvr_rtt_sized pierde asi sus dos marcas.
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
	glTexCoordPointer		(2, GL_FLOAT,		   sizeof(vertex), &VertexBuffer->t1);

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

	glOrtho(0, screenancho, screenheight, 0, -32768.0, 32768.0);

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
}
