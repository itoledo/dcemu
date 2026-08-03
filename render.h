#ifndef _render_h_
#define _render_h_

// some typedefs
typedef struct registers_ta  taReg;
typedef struct registers_tex texReg;
typedef struct registers_r  rdReg; 
typedef struct blit blit;

struct registers_ta
{
	// Obj Control
	unsigned pcw_16bit_UV:1;
	unsigned pcw_gouraud:1;
	unsigned pcw_offset:1;
	unsigned pcw_texture:1;
	unsigned pcw_col_type:2;
	unsigned pcw_volume:1;
	unsigned pcw_shadow:1;
	unsigned unk2:8;
	
	// Group Control	
	unsigned pcw_user_clip:2;
	unsigned pcw_strip_len:2;	
	unsigned pcw_unk1:3;
	unsigned pcw_group_en:1;
	
	// Para Control
	unsigned pcw_list_type:3;
	unsigned pcw_unk:1;
	unsigned pcw_end_of_strip:1;
	unsigned pcw_para_type:3;
};

struct registers_tex
{
		unsigned texture_surface:21; // we have to shift this value by 3
		unsigned unk:4;	
		unsigned stride:1;
		unsigned twiddled:1;
		unsigned pixelformat:3;
		unsigned vq:1;
		unsigned mipmap:1;
};

struct registers_r
{
		unsigned unk:20;
		unsigned dcalcctrl:1;
		unsigned cachebypass:1;
		unsigned uv16bit:1;
		unsigned gourad:1;
		unsigned offset:1;
		unsigned texture:1;
		unsigned zwrite:1;
		unsigned cullingmode:2;
		unsigned depthmode:3;
};

// Los tres se leen desde la FIFO del TA como un DWORD, asi que el layout de
// bitfields tiene que dar exactamente 4 bytes.
DC_ASSERT_SIZE(registers_ta, struct registers_ta, 4);
DC_ASSERT_SIZE(registers_tex, struct registers_tex, 4);
DC_ASSERT_SIZE(registers_r, struct registers_r, 4);

typedef union
{
	taReg registers;
	DWORD control;
}tile_accell;

typedef union
{
 	texReg registers;
	DWORD texture;
}texture_info;

typedef union
{
	rdReg registers;
	DWORD control;
}renderer_control;


/*  Blit info.Contains the framebuffer format and other info */
struct blit
{
	GLenum blit_format;
	GLenum blit_type;
};

/*
	Un vertice lleva DOS juegos de color y de coordenadas de textura: el 0 es el
	que se usa fuera del volumen modificador y el 1 dentro.

	Los tipos de vertice "de dos volumenes" (9 a 14) los traen los dos; el resto
	trae uno solo y entonces el juego 1 es una copia -- salvo en modo de sombra
	barata, donde es el juego 0 escalado por FPU_SHAD_SCALE.

	El orden importa: glColorPointer y glTexCoordPointer apuntan dentro de esta
	estructura con sizeof(vertex) de paso, asi que las cuatro componentes de
	cada color y las dos de cada UV tienen que ir consecutivas.
*/
typedef struct vertex
{
	/*
		La correccion de perspectiva va SOLO en las coordenadas de textura:
		cada juego lleva cuatro componentes (u*q, v*q, 0, q) con q = el 1/w
		que entrega el TA, y glTexCoordPointer(4) deja que GL divida por q al
		interpolar -- que es como rasteriza el chip, y para eso el TA recibe
		1/w. Con dos componentes la interpolacion era afin y el piso cerca de
		camara se deformaba como en una PlayStation.

		Premultiplicar las POSICIONES (el otro truco clasico) corrige tambien
		los colores, pero mueve la rasterizacion a nivel sub-pixel: pvr-fb_tex
		-- que reconstruye pixel a pixel su propio framebuffer -- paso de sus
		lineas a un tablero. Las posiciones quedan intactas; los colores
		siguen afines, que no se nota.

		`q` guarda el 1/w crudo del vertice; el cierre de tira premultiplica
		las UV de los dos juegos. El orden de los campos importa: los
		punteros de GL recorren con sizeof(vertex) de paso y cada juego
		(s,t,r,q) tiene que ser contiguo.
	*/
	float x,y,z;
	float q;
	float r,g,b,a;
	float t1,t2,tr,tq;
	float r1,g1,b1,a1;
	float u1,v1,ur,uq;

	/*
		El COLOR DE OFFSET, un juego por parametro. La instruccion de
		textura/sombreado lo SUMA al color del pixel DESPUES de combinar el
		texel con el color base -- PIXRGB = COLRGB x TEXRGB + OFFSETRGB en los
		cuatro modos (DevBox, tabla de Texture/Shading Instruction) --, que es
		exactamente lo que hace el color secundario de GL con GL_COLOR_SUM.
		No lleva alfa: el chip no lo usa (salvo como coeficiente de niebla por
		vertice, que es otra cosa).

		Las tres componentes de cada juego van contiguas porque
		glSecondaryColorPointer recorre con sizeof(vertex) de paso.
	*/
	float ro,go,bo;
	float ro1,go1,bo1;
}vertex;

/*
	Un triangulo de volumen modificador. No es geometria que se dibuje: define
	la region donde los poligonos cambian de juego de parametros.

	`lista` es 1 para el volumen opaco y 3 para el translucido, que afectan
	respectivamente a la lista 0 y a la 2. `instruccion` sale de los bits 30-29
	de la palabra ISP/TSP: 0 acumula, 1 cierra incluyendo y 2 cierra excluyendo.
*/
typedef struct
{
	float	x[3], y[3], z[3];
	DWORD	lista;
	DWORD	instruccion;
} VolTri;

typedef struct TriangleStripInfo
{
	DWORD count;
	DWORD alpha;
	GLenum depthmode;
	DWORD pvr_srcblend;
	DWORD pvr_dstblend;

	/* Bits 25 y 24 del TSP: eligen el BUFER DE ACUMULACION SECUNDARIO como
	   origen y como destino de la mezcla, en vez del primario --que es el que
	   termina en el framebuffer--. DevBox 3.4.6.1: existe para tratar el
	   resultado de superponer varios poligonos como si fuera uno solo. */
	DWORD srcselect;
	DWORD dstselect;

	/* Bit "Offset" de la palabra ISP/TSP: el vertice trae un color de offset
	   que la instruccion de textura/sombreado SUMA al color del pixel
	   (DevBox, tabla de Texture/Shading Instruction: PIXRGB = ... + OFFSETRGB
	   en los cuatro modos). */
	DWORD usa_offset;

	DWORD zwrite;
	DWORD culling;
	DWORD type;
	struct
	{
		DWORD surface; // if 1 then this polygon will have a texture bound to it
		DWORD pvr_texture_pixelformat;
		DWORD pvr_texture_pixelpack;
		DWORD pvr_texture_components;
		DWORD twiddled;
		DWORD vq;

		/* 1 si la textura trae mipmaps: los niveles van del 1x1 al grande y
		   el grande NO esta al principio -- get_texture() salta los chicos. */
		DWORD mipmapped;
		DWORD filtermode;

		/* Repeticion por eje, de los bits del TSP: 0 repite (por omision del
		   chip), 1 espeja (Flip, bits 18/17), 2 recorta (Clamp, bits 16/15,
		   que le gana al Flip). Guardar un cuarto de circulo y espejarlo es
		   como un juego arma un circulo de sombra entero. */
		DWORD wrap_u;
		DWORD wrap_v;
		DWORD pvr_texture_size_usize;
		DWORD pvr_texture_size_vsize;

		/* Texturas indexadas: 4 u 8 bits por pixel, 0 si no lo son, y cual de
		   los bancos de la RAM de paleta usan. Ver get_texture(). */
		DWORD pvr_texture_bpp;
		DWORD pvr_texture_paleta;

		/* Textura rectangular: el ancho en memoria sale de TEXT_CONTROL y no
		   del tamano declarado. 0 si no la usa. */
		DWORD pvr_texture_stride;

		/* 1 si los texels son YUV422 y hay que convertirlos a RGB al subirlos. */
		DWORD pvr_texture_yuv;

		/* 1 si son BUMP -- dos angulos por texel en vez de un color -- y el
		   color de offset que trae K1, K2, K3 y Q para resolverlos. */
		DWORD pvr_texture_bump;
		DWORD pvr_texture_bump_param;

		/* Instruccion de sombreado de la palabra TSP, bits 7-6: como se combina
		   el texel con el color del vertice. 0 decal, 1 modulate, 2 decal alpha,
		   3 modulate alpha. */
		DWORD pvr_texture_env;

		/* La palabra de control de textura tal como llego, para el volcado de
		   --traza-mem: cuando un formato parece mal leido, esto es lo que
		   permite mirar los bits sin intermediarios. */
		DWORD tcw_crudo;
	}texture;
	DWORD index;

	/*
		1 si esta tira la afecta un volumen modificador, o sea si su encabezado
		traia el bit Volume (dos juegos de parametros) o el bit Shadow (sombra
		barata). Las demas se dibujan de una sola pasada.
	*/
	DWORD volumen;

	/*
		La z mas cercana de la tira (el maximo, porque z es 1/w), ya pasada por
		profundidad_ta(). Es la llave del autosort de la lista translucida:
		el chip la ordena por profundidad por pixel, y dcemu aproxima por
		tira. La calcula cb_tastart() antes de ordenar.
	*/
	float prof;

	/*
		Los bits 23-22 del TSP: 0 niebla de tabla, 1 por vertice, 2 sin
		niebla, 3 tabla modo 2. dibujar_niebla_tira() resuelve los modos de
		tabla; el por vertice no esta emulado.
	*/
	DWORD niebla;
}TSI;


vertex VertexBuffer[65000];
TSI TriangleStrip[10000];

/* Los triangulos de volumen modificador de la escena en curso. */
VolTri VolumeBuffer[4096];

float coords[5];
float colors[4];
float data[7];
DWORD base_colour;

#endif
