#include "main.h"
#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include "options.h"
#include "graficos.h"
#include "intc.h"
#include "gui.h"
#include "glops.h"

#define TEXTURE_CACHING

SDL_Surface *screen;
// SDL_Surface *backscreen;
// SDL_Surface *glscreen;
SDL_Surface *outputscreen;

typedef Uint16 pcon_func(Uint16 src);

int pvr_vertextype;
int pvr_texture_surface;
int pvr_texture_pixelformat;
int pvr_texture_pixelpack;
int pvr_texture_components;
int pvr_texture_size_usize;
int pvr_texture_size_vsize;
int pvr_texture_twiddled;
int pvr_listtype;
int pvr_registering = -1;
int pvr_listdone = 0;
int pvr_srcblend;
int pvr_dstblend;
int pvr_srcblendmode;
int pvr_dstblendmode;
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

// sync pulse generator
DWORD pvr_spg_vblank_int = 0x00280208;	// VGA 640x480
DWORD pvr_spg_load = 0x020C0359;		// VGA 640x480
DWORD pvr_spg_load_vcount = 0x20C;		// VGA 640x480

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
struct cached_texture
{
	int		usize;		// tamaño horizontal
	int		vsize;		// tamaño vertical
	DWORD	memorypos;	// posición en memoria de la textura
	void *	data;		// datos de la textura 'twiddled'
 	GLuint	texture;
 	bool	twiddled;
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

void get_texture(int usize, int vsize, DWORD memorypos, int twiddled)
{
	Uint16 * q, * v;
	int i, j;

	if (cur_tex_count > 0)
    	for (i = 0; i < cur_tex_count; i++)
    	{
    		if (cached_textures[i].usize == usize
    		&&  cached_textures[i].vsize == vsize
    		&&  cached_textures[i].memorypos == memorypos)
    		{
          		logxmsg(LOG_PVR, "get_texture: retornando textura %d en cache\n", i);
          		GLOP_BINDTEXTURE(GL_TEXTURE_2D, cached_textures[i].texture);
//    			glBindTexture(GL_TEXTURE_2D, cached_textures[i].texture);
    			return;
    		}
    	}

	logxmsg(LOG_PVR, "get_texture: creando textura %d\n", cur_tex_count);

	// si llegamos aquí, la textura no está.
	cached_textures[cur_tex_count].usize = usize;
	cached_textures[cur_tex_count].vsize = vsize;
	cached_textures[cur_tex_count].memorypos = memorypos;
	cached_textures[cur_tex_count].texture = pvr_textures[cur_tex_count];
	v = (Uint16 *) get_memory_pointer(memorypos | 0xA5000000);

	// ahora al twiddle
	if (twiddled)
	{
		cached_textures[cur_tex_count].data = (void *) malloc(sizeof(Uint16) * usize * vsize);
		q = cached_textures[cur_tex_count].data;
    	for (i = 0; i < vsize; i++)
    		for (j = 0; j < usize; j++)
    		{
    			*(q++) = v[TWIDOUT(j,i)];
    		}
    	cached_textures[cur_tex_count].twiddled = true;
	}
	else
	{
		cached_textures[cur_tex_count].data = v;
		cached_textures[cur_tex_count].twiddled = false;
	}
//		memcpy(q, v, usize * vsize * sizeof(Uint16)); */

//    glBindTexture(GL_TEXTURE_2D, cached_textures[cur_tex_count].texture);
	GLOP_BINDTEXTURE(GL_TEXTURE_2D, cached_textures[cur_tex_count].texture);
/*    if (glGetError() != GL_NO_ERROR)
    	logxmsg(LOG_PVR, "get_texture: error en glBindTexture\n"); */
    logxmsg(LOG_PVR, "get_texture: size %dx%d, mempos %x\n", usize, vsize, memorypos | 0xA5000000);
//    glTexImage2D(GL_TEXTURE_2D, 0, pvr_texture_components, usize, vsize, 0, pvr_texture_pixelformat, pvr_texture_pixelpack, cached_textures[cur_tex_count].data);
	GLOP_TEXIMAGE2D(GL_TEXTURE_2D, 0, pvr_texture_components, usize, vsize, 0, pvr_texture_pixelformat, pvr_texture_pixelpack, cached_textures[cur_tex_count].data);
/*    if (glGetError() != GL_NO_ERROR)
    	logxmsg(LOG_PVR, "get_texture: error en glTexImage2D\n"); */

/*	FILE * fp = fopen("texture.raw", "wb");
	fwrite(cached_textures[cur_tex_count].data, usize * vsize, sizeof(Uint16), fp);
	fclose(fp); */

/*    if (twiddled)
    	free(cached_textures[cur_tex_count].data); */

    cur_tex_count++;

	return;
}

void * texture_find_or_create(int usize, int vsize, DWORD memorypos)
{
	Uint16 * q, * v;
	int i, j;

	for (i = 0; i < cur_tex_count; i++)
	{
		if (cached_textures[i].usize == usize
		&&  cached_textures[i].vsize == vsize
		&&  cached_textures[i].memorypos == memorypos)
		{
//			logxmsg(LOG_PVR, "texture_find_or_create: retornando textura ya procesada\n");
			return cached_textures[i].data;
		}
	}
	
	logxmsg(LOG_PVR, "texture_find_or_create: creando textura %d\n", cur_tex_count);

	// si llegamos aquí, la textura no está.
	if (cached_textures[cur_tex_count].data != NULL)
		free(cached_textures[cur_tex_count].data);

	cached_textures[cur_tex_count].usize = usize;
	cached_textures[cur_tex_count].vsize = vsize;
	cached_textures[cur_tex_count].memorypos = memorypos;
	q = cached_textures[cur_tex_count].data = (void *) malloc(sizeof(Uint16) * usize * vsize);
	v = (Uint16 *) get_memory_pointer(memorypos | 0xA5000000);
	
	// ahora al twiddle
	for (i = 0; i < vsize; i++)
		for (j = 0; j < usize; j++)
		{
/* #ifdef DEBUG_VERTEX
			if (TWIDOUT(j,i) > sizeof(Uint16) * pvr_texture_size_usize * pvr_texture_size_vsize)
			{
				logmsg("TWIDOUT!!!\n");
				exit(1);
			}
#endif */
			*(q++) = v[TWIDOUT(j,i)];
		}
		
	return cached_textures[cur_tex_count++].data;
}

void limpiar_pantalla()
{
	// dejemos todo listo para la siguiente pantalla
//	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GLOP_CLEAR(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

	logxmsg(LOG_PVR, "tag address: %08x\n", (pvr_isp_backgnd_t >> 3) & 0x1FFFFF);
	logxmsg(LOG_PVR, "param_base:  %08x\n", (pvr_param_base));

	if (bgplane > 0)
	{
    	memread(bgplane, &bkg, sizeof(pvr_bkg_poly_t));

    	logxmsg(LOG_PVR, "bkg: (%f,%f,%f), (%f,%f,%f), (%f,%f,%f)\n",
    		bkg.x1, bkg.y1, bkg.z1,
    		bkg.x2, bkg.y2, bkg.z2,
    		bkg.x3, bkg.y3, bkg.z3);
	}
}

void cb_tastart(DWORD addr, void * p, size_t size)
{
	DWORD dw;

//	DibujarFramebuffer();
	glop_process();
	gui_refresh();

	logxmsg(LOG_PVR, "cb_tastart: SDL_GL_SwapBuffers\n");
	SDL_GL_SwapBuffers();
	pvr_framebufferdisplay = false;
	
//	SET_BIT(ASIC_ACK_A, 0x80); // fin de proceso ??? VBLINT?

	logxmsg(LOG_PVR, "cb_tastart\n");
	pvr_listdone = 0;
	limpiar_texturas();
//	cur_tex_count = 0;

	// ???
	memread(0xa05f8128, &dw, sizeof(DWORD)); // leer TA_ISP_BASE
	memwrite(0xa05f8138, &dw, sizeof(DWORD)); // grabar en TA_ITP_CURRENT

	limpiar_pantalla();

//	logxmsg(LOG_PVR, "ISP_BACKGND_T: %08x\n", pvr_isp_backgnd_t);
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
		case 0:	logxmsg(LOG_PVR, "0555KRGB\n");	break;
		case 1: logxmsg(LOG_PVR, "565RGB\n"); break;
		case 2: logxmsg(LOG_PVR, "4444ARGB\n"); break;
		case 3: logxmsg(LOG_PVR, "1555ARGB\n"); break;
		case 4: logxmsg(LOG_PVR, "888RGB\n"); break;
		case 5: logxmsg(LOG_PVR, "0888KRGB\n"); break;
		case 6: logxmsg(LOG_PVR, "8888ARGB\n"); break;
		case 7: logxmsg(LOG_PVR, "reserved\n"); break;
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

void ta_check(DWORD addr)
{
/*	DWORD d = *(DWORD *) &ta_mem[0];
	DWORD * p = (DWORD *) &ta_mem[0];
	DWORD options = d & 0x1FFFFFFF;
	long cmd = (d >> 29) & 0x7; */

	DWORD * p = (DWORD *) &ta_mem[addr & 0xFF]; // 0x00 o 0x20
	DWORD d = *p;
	DWORD options = d & 0x1FFFFFFF;
	long cmd = (d >> 29) & 0x7;

/*	if ((addr & 0xFF) == 0x20)
		return; */

	switch(cmd)
	{
		case 0:
  		{
    		logxmsg(LOG_PVR, "TA: END_OF_LIST\n");
/*    		SDL_GL_SwapBuffers();
			SET_BIT(ASIC_ACK_A, 0x80); // fin de proceso

			// dejemos todo listo para la siguiente pantalla
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); */
//			pvr_registered |= (1 << pvr_listtype);
			pvr_listdone |= (1 << pvr_listtype);
   			intc_add(pvr_lists[pvr_listtype], 100);
			if (pvr_listdone == pvr_registered)
				intc_add(ASIC_EVT_PVR_RENDERDONE, 200);
		}
		break;
		
		case 1:
  		{
    		logxmsg(LOG_PVR, "TA: USER_CLIP\n");
    		logxmsg(LOG_PVR, "USER_CLIP: Xmin: %x\n", p[4]);
    		logxmsg(LOG_PVR, "USER_CLIP: Ymin: %x\n", p[5]);
    		logxmsg(LOG_PVR, "USER_CLIP: Xmax: %x\n", p[6]);
    		logxmsg(LOG_PVR, "USER_CLIP: Ymax: %x\n", p[7]);
		}
		break;

		case 4:
  		{
  			int listtype = (options >> 24) & 0x7;
  			int striplength = (options >> 18) & 0x3;
  			int clipmode = (options >> 16) & 0x3;
  			int modifier = (options >> 7) & 0x1;
  			int modifier_mode = (options >> 6) & 0x1;
  			int colour_type = (options >> 4) & 0x3;
  			int texture = (options >> 3) & 0x1;
  			int specular = (options >> 2) & 0x1;
  			int shading = (options >> 1) & 0x1;
  			int uv_format = options & 0x1;

    		logxmsg(LOG_PVR, "TA: POLYGON / MODIFIER_VOLUME\n");

			pvr_vertextype = -1;
			pvr_listtype = listtype;
			pvr_registering = listtype;

    		switch(listtype)
    		{
    			case 0:	logxmsg(LOG_PVR, "P/MV: opaque polygon\n");		break;
    			case 1: logxmsg(LOG_PVR, "P/MV: opaque modifier\n");	break;
    			case 2: logxmsg(LOG_PVR, "P/MV: transparent polygon\n");	break;
    			case 3: logxmsg(LOG_PVR, "P/MV: transparent modifier\n");	break;
    			case 4: logxmsg(LOG_PVR, "P/MV: punchthru polygon\n");		break;
			}
			

			switch(striplength)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: striplength 1\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: striplength 2\n");	break;
				case 2: logxmsg(LOG_PVR, "P/MV: striplength 4\n");	break;
				case 3: logxmsg(LOG_PVR, "P/MV: striplength 6\n");	break;
			}
			
			switch(clipmode)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: disable user clip\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: reserved\n");	break;
				case 2: logxmsg(LOG_PVR, "P/MV: user clip inside\n");	break;
				case 3: logxmsg(LOG_PVR, "P/MV: user clip outside\n");	break;
			}
		
			switch(modifier)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: polygon is not affected by modifier volume\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: polygon is affected by modifier volume\n");		break;
			}
			
			switch(modifier_mode)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: cheap shadow modifier\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: normal modifier\n");		break;
			}
			
			switch(colour_type)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: 32bit ARGB packed colour\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: 32bit * 4 floating point colour\n");	break;
				case 2: logxmsg(LOG_PVR, "P/MV: intensity\n");	break;
				case 3: logxmsg(LOG_PVR, "P/MV: intensity from previous face\n"); break;
			}
			
			switch(texture)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: disable texture\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: enable texture\n");		break;
			}
			
			switch(specular)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: disable specular highlight\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: enable specular highlight\n");	break;
			}
			
			switch(shading)
			{
				case 0: logxmsg(LOG_PVR, "P/MV: flat shading\n");	break;
				case 1: logxmsg(LOG_PVR, "P/MV: gouraud shading\n");	break;
			}
			
			switch(uv_format)
			{
				case 0: logxmsg(LOG_PVR, "32 bit float\n");	break;
				case 1: logxmsg(LOG_PVR, "16 bit float\n");	break;
			}
			
/*			if (listtype == 0 || listtype == 2 || listtype == 4)
			{
				DWORD p1 = *(DWORD *) &ta_mem[1];
				int depthmode = (p1 >> 29) & 0x7;
				int cullingmode = (p1 >> 27) & 0x3;
				int zwrite = (p1 >> 26) & 0x1;
				int texture = (p1 >> 25) & 0x1;
				// 3 parámetros inútiles (specular, shading, uvformat)
				int d_calc_exact = (p1 >> 20) & 0x1;
			} */

//			switch((pvr_srcblend = (p[2] >> 29) & 0x7)) // srcblend
			switch((p[2] >> 29) & 0x7) // srcblend
			{
				case 0:		pvr_srcblend = GL_ZERO;			logxmsg(LOG_PVR, "srcblend: zero\n");	break;
				case 1:		pvr_srcblend = GL_ONE;			logxmsg(LOG_PVR, "srcblend: one\n");	break;
				case 2:		pvr_srcblend = GL_DST_COLOR;	logxmsg(LOG_PVR, "srcblend: dst colour\n");	break;
				case 3:		pvr_srcblend = GL_ONE_MINUS_DST_COLOR;	logxmsg(LOG_PVR, "srcblend: inverse dst colour\n");	break;
				case 4:		pvr_srcblend = GL_SRC_ALPHA;	logxmsg(LOG_PVR, "srcblend: src alpha\n"); break;
				case 5:		pvr_srcblend = GL_ONE_MINUS_SRC_ALPHA;	logxmsg(LOG_PVR, "srcblend: inverse src alpha\n");	break;
				case 6:		pvr_srcblend = GL_DST_ALPHA;	logxmsg(LOG_PVR, "srcblend: dst alpha\n");	break;
				case 7:		pvr_srcblend = GL_ONE_MINUS_DST_ALPHA;	logxmsg(LOG_PVR, "srcblend: inverse dst alpha\n");	break;
			}
			
//			switch((pvr_dstblend = (p[2] >> 26) & 0x7)) // dstblend
			switch((p[2] >> 26) & 0x7) // dstblend
			{
				case 0:		pvr_dstblend = GL_ZERO;			logxmsg(LOG_PVR, "dstblend: zero\n");	break;
				case 1:		pvr_dstblend = GL_ONE;			logxmsg(LOG_PVR, "dstblend: one\n");	break;
				case 2:		pvr_dstblend = GL_DST_COLOR;	logxmsg(LOG_PVR, "dstblend: dst colour\n");	break;
				case 3:		pvr_dstblend = GL_ONE_MINUS_DST_COLOR;	logxmsg(LOG_PVR, "dstblend: inverse dst colour\n");	break;
				case 4:		pvr_dstblend = GL_SRC_ALPHA;	logxmsg(LOG_PVR, "dstblend: src alpha\n"); break;
				case 5:		pvr_dstblend = GL_ONE_MINUS_SRC_ALPHA;	logxmsg(LOG_PVR, "dstblend: inverse src alpha\n");	break;
				case 6:		pvr_dstblend = GL_DST_ALPHA;	logxmsg(LOG_PVR, "dstblend: dst alpha\n");	break;
				case 7:		pvr_dstblend = GL_ONE_MINUS_DST_ALPHA;	logxmsg(LOG_PVR, "dstblend: inverse dst alpha\n");	break;
			}
			
			pvr_srcblendmode = (p[2] >> 25) & 0x1;
			pvr_dstblendmode = (p[2] >> 24) & 0x1;

			if (texture == 1)
			{
				DWORD p3 = p[3];
				int mipmap = (p3 >> 31) & 0x1;
				int vq = (p3 >> 30) & 0x1;
				int pixelformat = (p3 >> 27) & 0x7;
				DWORD texture_surface = (p3 & 0xFFFFF) << 3; // 20 bits
				int twiddled = (p3 >> 26) & 0x1;
				int stride = (p3 >> 25) & 0x1;
				
				pvr_texture_surface = texture_surface;
//				pvr_texture_twiddled = !twiddled; // 0: twiddled
//				pvr_texture_twiddled = (twiddled ? 0 : 1);

				if (mipmap)
					logxmsg(LOG_PVR, "texture: enable mipmap\n");
				else
					logxmsg(LOG_PVR, "texture: disable mipmap\n");

				if (vq)
					logxmsg(LOG_PVR, "texture: enable VQ compression for texture\n");
				else
					logxmsg(LOG_PVR, "texture: disable compression\n");
	
#define CTT() { pvr_texture_pixelformat = -1; pvr_texture_components = -1; pvr_texture_pixelpack = -1; }

				switch (pixelformat)
				{
					case 0:
     				logxmsg(LOG_PVR, "texture: ARGB1555\n");
     				pvr_texture_pixelformat = GL_RGBA;
     				pvr_texture_components = 4;
     				pvr_texture_pixelpack = GL_UNSIGNED_SHORT_5_5_5_1;
     				pvr_texture_pixelconvert = NULL;
         			break;

					case 1:
     				logxmsg(LOG_PVR, "texture: RGB565\n");
     				pvr_texture_components = 3;
     				pvr_texture_pixelformat = GL_RGB;
     				pvr_texture_pixelpack = GL_UNSIGNED_SHORT_5_6_5;
     				pvr_texture_pixelconvert = NULL;
     				break;

					case 2:
				    logxmsg(LOG_PVR, "texture: ARGB4444\n");
				    pvr_texture_pixelformat = GL_RGBA;
				    pvr_texture_pixelconvert = pcon_argb4444_to_rgba4444;
				    pvr_texture_pixelpack = GL_UNSIGNED_SHORT_4_4_4_4_EXT;
     				pvr_texture_components = 4;
     				break;

					case 3: logxmsg(LOG_PVR, "texture: YUV422\n");	CTT();	break;
					case 4: logxmsg(LOG_PVR, "texture: BUMP\n");	CTT();	break;
					case 5: logxmsg(LOG_PVR, "texture: 4BPP_PALETTE\n");	CTT(); break;
					case 6: logxmsg(LOG_PVR, "texture: 8BPP_PALETTE\n");	CTT(); break;
				}
				
				logxmsg(LOG_PVR, "texture: surface %08x\n", texture_surface);

				if (twiddled)
				{
					logxmsg(LOG_PVR, "texture: non-twiddled texture\n");
					pvr_texture_twiddled = 0;
				}
				else
				{
					logxmsg(LOG_PVR, "texture: twiddled texture\n");
					pvr_texture_twiddled = 1;
				}
	
				if (stride)
					logxmsg(LOG_PVR, "texture: stride\n");
				else
					logxmsg(LOG_PVR, "texture: no stride\n");
	
/*				pvr_texture_size_usize = (((p[2] >> 3) & 0x7) << 4);
				pvr_texture_size_vsize = ((p[2] & 0x7) << 4); */
				switch((p[2] >> 3) & 0x7)
				{
					case 0:		pvr_texture_size_usize = 8;		break;
					case 1:		pvr_texture_size_usize = 16;	break;
					case 2:		pvr_texture_size_usize = 32;	break;
					case 3:		pvr_texture_size_usize = 64;	break;
					case 4:		pvr_texture_size_usize = 128;	break;
					case 5:		pvr_texture_size_usize = 256;	break;
					case 6:		pvr_texture_size_usize = 512;	break;
					case 7:		pvr_texture_size_usize = 1024;	break;
				}
				switch(p[2] & 0x7)
				{
					case 0:		pvr_texture_size_vsize = 8;		break;
					case 1:		pvr_texture_size_vsize = 16;	break;
					case 2:		pvr_texture_size_vsize = 32;	break;
					case 3:		pvr_texture_size_vsize = 64;	break;
					case 4:		pvr_texture_size_vsize = 128;	break;
					case 5:		pvr_texture_size_vsize = 256;	break;
					case 6:		pvr_texture_size_vsize = 512;	break;
					case 7:		pvr_texture_size_vsize = 1024;	break;
				}

				logxmsg(LOG_PVR, "texture size: %d x %d\n", pvr_texture_size_usize, pvr_texture_size_vsize);
    		}
			else
				pvr_texture_surface = 0;

			// revisemos los parámetros para la lista de vertex
			if (texture == 0)
   			{
				switch(colour_type)
				{
					case 0:		pvr_vertextype = 0;		break;	// non-textured, packed colour
					case 1:		pvr_vertextype = 1;		break;	// non-textured, floating colour
					case 2:		pvr_vertextype = 2;		break;	// non-textured, intensity
				}
			}
			else // textured
			{
				if (colour_type == 0) // packed colour
				{
					if (uv_format == 0)
     					pvr_vertextype = 3;		// 32bit UV
 					else
 						pvr_vertextype = 4;		// 16bit UV
				}
				else
				if (colour_type == 1) // floating colour
				{
					if (uv_format == 0)
						pvr_vertextype = 5;	// 32bit UV
					else
						pvr_vertextype = 6;	// 16bit UV
				}
				else
				if (colour_type == 2) // intensity
				{
					if (uv_format == 0)
						pvr_vertextype = 7;	// 32bit UV
					else
						pvr_vertextype = 8;	// 16bit UV
				}
			}
			// continúa
			logxmsg(LOG_PVR, "pvr_vertextype: %d\n", pvr_vertextype);
		}
		break;
       		
		case 5:		logxmsg(LOG_PVR, "TA: SPRITE\n");				break;
		case 7:
  		{
#ifdef DEBUG_VERTEX
    		logxmsg(LOG_PVR, "TA: VERTEX\n");
#endif
    		
			if (vertexstart == true)
			{
				if (pvr_texture_surface)
				{
        			logxmsg(LOG_PVR, "llamando a get_texture(%d, %d, %d, %d)\n", pvr_texture_size_usize, pvr_texture_size_vsize, pvr_texture_surface, pvr_texture_twiddled);
     				get_texture(pvr_texture_size_usize, pvr_texture_size_vsize, pvr_texture_surface, pvr_texture_twiddled);
				}
//				glBlendFunc(pvr_srcblend, pvr_dstblend);
				GLOP_BLENDFUNC(pvr_srcblend, pvr_dstblend);
//				glBegin(GL_TRIANGLE_STRIP);
				GLOP_BEGIN(GL_TRIANGLE_STRIP);
				vertexstart = false;
			}

			switch (pvr_vertextype)
			{
				case 0:
				{
					float datos[3];
//					Uint8 r, g, b, a;
					float r, g, b, a;
					DWORD base_colour;

//					DWORD dwdatos[8];
//					memcpy(&dwdatos[0], &ta_mem[0], sizeof(DWORD)*8);
/*					logxmsg(LOG_PVR, "dwdatos: %08x %08x %08x %08x %08x %08x %08x\n",
						dwdatos[1], dwdatos[2], dwdatos[3], dwdatos[4], 
						dwdatos[5], dwdatos[6], dwdatos[7]); */

					memcpy(&datos[0], &p[1], sizeof(float) * 3);
					memcpy(&base_colour, &p[6], sizeof(DWORD));
					
#define PVR_PACK_COLOR(a, r, g, b) ( \
        ( ((uint8)( a * 255 ) ) << 24 ) | \
        ( ((uint8)( r * 255 ) ) << 16 ) | \
        ( ((uint8)( g * 255 ) ) << 8 ) | \
        ( ((uint8)( b * 255 ) ) << 0 ) )

//					SDL_GetRGBA(base_colour, backscreen->format, &r, &g, &b, &a);
					a = ((base_colour >> 24) & 0xFF) / 255.0;
					r = ((base_colour >> 16) & 0xFF) / 255.0;
					g = ((base_colour >> 8)  & 0xFF) / 255.0;
					b = ((base_colour >> 0)  & 0xFF) / 255.0;
					
/*					logxmsg(LOG_PVR, "vertex tipo 1: color=%02x%02x%02x%02x coords=%f,%f,%f\n",
						r, g, b, a, datos[0], datos[1], datos[2]);
					glColor4ub(r, g, b, a); */
#ifdef DEBUG_VERTEX
					logxmsg(LOG_PVR, "vertex tipo 0: color=%f,%f,%f,%f coords=%f,%f,%f\n",
						r, g, b, a, datos[0], datos[1], datos[2]);
#endif
//					glColor4f(r, g, b, a);
					GLOP_COLOR4F(r, g, b, a);
//					glVertex3f(datos[0], datos[1], datos[2] - 100);
					GLOP_VERTEX3F(datos[0], datos[1], datos[2] - 100);
				}
				break;

				case 1:
				{
					float datos[7];
				
					memcpy(&datos[0], &p[1], sizeof(float)*7);

#ifdef DEBUG_VERTEX
					logxmsg(LOG_PVR, "vertex tipo 1: color %f %f %f %f\n", datos[4], datos[5], datos[6], datos[3]);
					logxmsg(LOG_PVR, "posición: %f %f %f\n", datos[0], datos[1], datos[2]);
#endif
//					glColor4f(datos[4], datos[5], datos[6], datos[3]);
					GLOP_COLOR4F(datos[4], datos[5], datos[6], datos[3]);
//					glVertex3f(datos[0], datos[1], datos[2] - 100);
					GLOP_VERTEX3F(datos[0], datos[1], datos[2] - 100);
				}
				break;
				
				case 3: // polígono opaco con textura
				{
					float coords[5];
//					Uint8 r, g, b, a;
					float r, g, b, a;
					DWORD base_colour;
					
					memcpy(&base_colour, &p[6], sizeof(DWORD));
					
					memcpy(&coords[0], &p[1], sizeof(float)*5);

					a = ((base_colour >> 24) & 0xFF) / 255.0;
					r = ((base_colour >> 16) & 0xFF) / 255.0;
					g = ((base_colour >> 8)  & 0xFF) / 255.0;
					b = ((base_colour >> 0)  & 0xFF) / 255.0;

#ifdef DEBUG_VERTEX
					logxmsg(LOG_PVR, "seteando colores rgba: %f %f %f %f\n", r, g, b, a);
					logxmsg(LOG_PVR, "seteando texturas en %fx%f\n", coords[3], coords[4]);
					logxmsg(LOG_PVR, "coordenadas: %f,%f,%f\n", coords[0], coords[1], coords[2]);
#endif
//					glColor4f(r, g, b, a);
					GLOP_COLOR4F(r, g, b, a);
//					glTexCoord2f(coords[3], coords[4]);
					GLOP_TEXCOORD2F(coords[3], coords[4]);
//					glVertex3f(coords[0], coords[1], coords[2] - 100);
					GLOP_VERTEX3F(coords[0], coords[1], coords[2] - 100);
				}
				break;

				case 5:
    			{
					float coords[5];
					float colores[4];
					
					memcpy(&coords[0], &p[1], sizeof(float)*5);

					memcpy(&colores[0], &p[8], sizeof(float)*4);

#ifdef DEBUG_VERTEX
//					logxmsg(LOG_PVR, "seteando colores rgba: %f %f %f %f\n", r, g, b, a);
					logxmsg(LOG_PVR, "seteando texturas en %fx%f\n", coords[3], coords[4]);
					logxmsg(LOG_PVR, "coordenadas: %f,%f,%f\n", coords[0], coords[1], coords[2]);
#endif
//					glColor4f(colores[1], colores[2], colores[3], colores[0]);
					GLOP_COLOR4F(colores[1], colores[2], colores[3], colores[0]);
//					glTexCoord2f(coords[3], coords[4]);
					GLOP_TEXCOORD2F(coords[3], coords[4]);
//					glVertex3f(coords[0], coords[1], coords[2] - 100);
					GLOP_VERTEX3F(coords[0], coords[1], coords[2] - 100);
				}
				break;
			}

    		if (options && (1 << 28))
    		{
#ifdef DEBUG_VERTEX
    			logxmsg(LOG_PVR, "VERTEX: end-of-strip\n");
#endif
//    			glEnd();
				GLOP_END();
	    		pvr_registering = -1;
    			vertexstart = true;
//    			SDL_GL_SwapBuffers();
			}
      	}
       	break;
       	
		default:	logxmsg(LOG_PVR, "TA: cmd %d desconocido\n");	break;
	}

	
}

void PutPixel(Uint32 pos, Uint32 pixel)
{
	Uint8 * p = (Uint8 *) screen->pixels;

	// tenemos una pantalla de 640x480 que empieza en video_base.
	// el loop para limpiar la pantalla es de 640*480/2 = 307200/2 = 153600 ciclos.
	// avanzando de 4 en 4 r0 (que parte en video_base).
	// r2 se va decrementando en 1.

	// si tenemos 2 bytes por pixel: 614400 bytes para la pantalla.
	// si guardamos 4 bytes por MOV: 614400 / 4 = 153600 ciclos.
	// por lo tanto, cada MOV guarda 4 bytes = 2 pixeles.

	// asi que, para determinar la pos. de la pantalla en la que estamos guardando
	// el pixel:

	// x = pos % (640*2);
	// y = pos / (640*2);

//	int x = pos % (640 * 2);
//	int y = pos / (640 * 2);

//	p += y * screen->pitch + (x/2) * screen->format->BytesPerPixel;
	p += pos;
	
//	fprintf(fp, "pos: %d\r\n", pos);
	
	//	pixel = SDL_MapRGB(screen->format, 0xff, 0xff, 0x00);

	if ( SDL_MUSTLOCK(screen) )
		SDL_LockSurface(screen);

/*	if (pos > 640*480*screen->format->BytesPerPixel)
	{
		fprintf(fp, "pos: %d, %x", pos, pos);
	}
	else
	{ */
//		p[x % 2] = pixel;
		*p = pixel;
//	}

	if ( SDL_MUSTLOCK(screen) )
		SDL_UnlockSurface(screen);
}

void PutPixelW(Uint32 pos, WORD pixel)
{
	Uint8 * p = (Uint8 *) screen->pixels + pos;

	// tenemos una pantalla de 640x480 que empieza en video_base.
	// el loop para limpiar la pantalla es de 640*480/2 = 307200/2 = 153600 ciclos.
	// avanzando de 4 en 4 r0 (que parte en video_base).
	// r2 se va decrementando en 1.

	// si tenemos 2 bytes por pixel: 614400 bytes para la pantalla.
	// si guardamos 4 bytes por MOV: 614400 / 4 = 153600 ciclos.
	// por lo tanto, cada MOV guarda 4 bytes = 2 pixeles.

	// asi que, para determinar la pos. de la pantalla en la que estamos guardando
	// el pixel:

	// x = pos % (640*2);
	// y = pos / (640*2);

/*	int x = pos % (640 * 2);
	int y = pos / (640 * 2);

	p += y * screen->pitch + (x/2) * screen->format->BytesPerPixel; */

//	p += pos;
	
	//	pixel = SDL_MapRGB(screen->format, 0xff, 0xff, 0x00);

	if ( SDL_MUSTLOCK(screen) )
		SDL_LockSurface(screen);

/*	if (pos > 640*480*screen->format->BytesPerPixel)
	{
		fprintf(fp, "pos: %d, %x", pos, pos);
	}
	else
	{ */
/*		p[0] = pixel & 0xff;
		p[1] = (pixel >> 8) & 0xff; */
		*(WORD *) p  = (WORD) pixel;
//	}

	if ( SDL_MUSTLOCK(screen) )
		SDL_UnlockSurface(screen);
}

void PutPixelL(Uint32 pos, DWORD pixel)
{
	Uint8 * p = (Uint8 *) screen->pixels + pos;

	// tenemos una pantalla de 640x480 que empieza en video_base.
	// el loop para limpiar la pantalla es de 640*480/2 = 307200/2 = 153600 ciclos.
	// avanzando de 4 en 4 r0 (que parte en video_base).
	// r2 se va decrementando en 1.

	// si tenemos 2 bytes por pixel: 614400 bytes para la pantalla.
	// si guardamos 4 bytes por MOV: 614400 / 4 = 153600 ciclos.
	// por lo tanto, cada MOV guarda 4 bytes = 2 pixeles.

	// asi que, para determinar la pos. de la pantalla en la que estamos guardando
	// el pixel:

	// x = pos % (640*2);
	// y = pos / (640*2);

/*	int x = pos % (640 * 2);
	int y = pos / (640 * 2);

	p += y * screen->pitch + (x/2) * screen->format->BytesPerPixel; */

//	p += pos;
	
	//	pixel = SDL_MapRGB(screen->format, 0xff, 0xff, 0x00);

	if ( SDL_MUSTLOCK(screen) )
		SDL_LockSurface(screen);

/*	if (pos > 640*480*screen->format->BytesPerPixel)
	{
		fprintf(fp, "pos: %d, %x", pos, pos);
	}
	else
	{ */
/*		p[0] = pixel & 0xff;
		p[1] = (pixel >> 8) & 0xff; */
		*(DWORD *) p  = (DWORD) pixel;
//	}

	if ( SDL_MUSTLOCK(screen) )
		SDL_UnlockSurface(screen);
}

void PutPixelN(Uint32 pos, void * data, size_t size)
{
	Uint8 * p = (Uint8 *) screen->pixels + pos;

/*	if ( SDL_MUSTLOCK(screen) )
		SDL_LockSurface(screen); */

	memcpy(p, data, size);

/*	if ( SDL_MUSTLOCK(screen) )
		SDL_UnlockSurface(screen); */
}

void ReadPixelN(Uint32 pos, void * data, size_t size)
{
	Uint8 * p = (Uint8 *) screen->pixels + pos;

/*	if ( SDL_MUSTLOCK(screen) )
		SDL_LockSurface(screen); */

	memcpy(data, p, size);

/*	if ( SDL_MUSTLOCK(screen) )
		SDL_UnlockSurface(screen); */
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
/*	SDL_LockSurface(backscreen);
	memcpy(backscreen->pixels, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1), framebuffer_size);
	SDL_UnlockSurface(backscreen); */
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

void DibujarFramebuffer()
{
	glEnable(GL_TEXTURE_2D);
	
	glBindTexture(GL_TEXTURE_2D, background_texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, screenancho);
	switch(screenformat)
	{
		case FRAMEBUFFER_ARGB0555:
		{
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_5_5_1, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;

		case FRAMEBUFFER_RGB565:
		{
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;

		case FRAMEBUFFER_RGB888:
		{
			// FIXME?
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_RGB, GL_UNSIGNED_BYTE, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;

		case FRAMEBUFFER_ARGB0888:
		{
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, screentexwidth, screentexheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;
	}
//	glEnable(GL_BLEND);
//	glDisable(GL_DEPTH_TEST);
//	glClear(GL_DEPTH_BUFFER_BIT);
//	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
//	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#define PROFUNDIDAD (-1000.0f)
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, screenheight / screentexheight); glVertex3f(0.0f, (float) screenheight, PROFUNDIDAD);
	glTexCoord2f(screenancho / screentexwidth, screenheight / screentexheight); glVertex3f((float) screenancho, (float) screenheight, PROFUNDIDAD);
	glTexCoord2f(screenancho / screentexwidth, 0.0f); glVertex3f((float) screenancho, 0.0f, PROFUNDIDAD);
	glTexCoord2f(0.0f, 0.0f); glVertex3f(0.0f, 0.0f, PROFUNDIDAD);
	glEnd();
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glEnable(GL_BLEND);
//	glEnable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	// fin textura 1024

//	logxmsg(LOG_PVR, "DibujarFramebuffer: SDL_GL_SwapBuffers\n");
//	SDL_GL_SwapBuffers();
}

int glinit(void)
{
	int i;

	logxmsg(LOG_PVR, "glinit: entrando");

	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute( SDL_GL_BUFFER_SIZE, 32 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	outputscreen = SDL_SetVideoMode(800, 600, 32, SDL_HWSURFACE|SDL_OPENGL|SDL_HWACCEL);
	
	if (outputscreen == NULL)
	{
		logxmsg(LOG_PVR, "screeninit: outputscreen = NULL!!!!\n");
		exit(1);
	}

    SDL_WM_SetCaption(APPTITLE, NULL);
    SDL_WM_SetIcon(SDL_LoadBMP("dcemu.bmp"), NULL);
	SDL_EnableUNICODE(1);

	gui_init();

	logxmsg(LOG_PVR, "glinit: seteando viewport a %dx%d\n", 800, 600);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glViewport(0, 0, 800, 600);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);		// This Will Clear The Background Color To Black
	glClearDepth(1.0);				// Enables Clearing Of The Depth Buffer
	glDepthFunc(GL_LESS);				// The Type Of Depth Test To Do
	glEnable(GL_DEPTH_TEST);			// Enables Depth Testing
	glShadeModel(GL_SMOOTH);			// Enables Smooth Color Shading

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();				// Reset The Projection Matrix
//		gluPerspective(45.0f,(GLfloat)width/(GLfloat)height,1.0f,100.0f);	// Calculate The Aspect Ratio Of The Window

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

    glGenTextures(MAX_TEXTURE_COUNT, pvr_textures);
    glGenTextures(1, &background_texture);
/*		glGenTextures(1, &pvr_textures[0]);
	glGenTextures(1, &pvr_textures[1]); */
	glBindTexture(GL_TEXTURE_2D, background_texture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering
	for (i = 0; i < MAX_TEXTURE_COUNT; i++)
	{
		glBindTexture(GL_TEXTURE_2D, pvr_textures[i]);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering
	}

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);			// Clear The Screen And The Depth Buffer

	logxmsg(LOG_PVR, "glinit: saliendo");
	
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

	// definamos el tamaño de la textura
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
//		glOrtho(0, width, height, 0, 0.1, 100.0);
//		glOrtho(0, 640, 480, 0, 0, 1024.0);
	glOrtho(0, screenancho, screenheight, 0, 0, 1024.0);

//		glScalef(1.0/640.0,1.0/480.0,1.0);
//		glTranslatef(-320.0f, -240.0f, -1.0f);

//		glOrtho(0, (GLdouble)width, (GLdouble)height, 0.0, 0.1, 100.0);
//		glPixelZoom(1.0, -1.0);

	logxmsg(LOG_PVR, "screeninit: saliendo\n");

	return 0;
}

void DibujarGL(SDL_Surface * sfc)
{
//	glPushAttrib(GL_TEXTURE_BIT);

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
	
//	glPopAttrib();

	logxmsg(LOG_PVR, "DibujarGL: SDL_GL_SwapBuffers\n");
	SDL_GL_SwapBuffers();
//	glFlush();
}
