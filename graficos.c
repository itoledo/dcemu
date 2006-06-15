#include "main.h"
#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include "options.h"
#include "graficos.h"
#include "intc.h"
#include "gui.h"
#include "glops.h"
#include "render.h"


#define TEXTURE_CACHING
// #define DEBUG_VERTEX
// #define DEBUG_VERTEX_NEW

SDL_Surface *screen;
SDL_Surface *outputscreen;

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

DWORD * ta_address_pointer;

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
struct cached_texture
{
	int		usize;		// tamaño horizontal
	int		vsize;		// tamaño vertical
	DWORD	memorypos;	// posición en memoria de la textura
	void *	data;		// datos de la textura 'twiddled'
 	GLuint	texture;
 	bool	twiddled;
 	bool	vq;
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

void subdivide_and_move(int x1, int y1, int size, int op) {
	if (size == 1) {
		detwiddled[y1*imgsize+x1] = read_pixel();
	} else {
		int ns = size/2;
		subdivide_and_move(x1, y1, ns, op);
		subdivide_and_move(x1, y1+ns, ns, op);
		subdivide_and_move(x1+ns, y1, ns, op);
		subdivide_and_move(x1+ns, y1+ns, ns, op);
	}
}

void get_texture(int usize, int vsize, DWORD memorypos, int twiddled, int vq)
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

/*
#define MIN(a, b) ( (a)<(b)? (a):(b) )
		int min, mask, yout;
		
		min = MIN(usize, vsize);
		mask = min - 1;
		
		q = cached_textures[cur_tex_count].data;

    	for (i = 0; i < vsize; i++)
	   	{
    		for (j = 0; j < usize; j++)
    		{
				*(q++) = twid[TWIDOUT(j&mask,i&mask) + (j/min + i/min)*min*min];
    		}
		}

		free(twid); */
	}
	else
	if (twiddled)
	{
		cached_textures[cur_tex_count].data = (void *) malloc(sizeof(Uint16) * usize * vsize);
		q = cached_textures[cur_tex_count].data;
/*		twiddled = v;
		detwiddled = cached_textures[cur_tex_count].data;
		ptr = 0;
		imgsize = usize;
		subdivide_and_move(0, 0, usize, 0); */
		
#define MIN(a, b) ( (a)<(b)? (a):(b) )
		int min, mask, yout;
		
		min = MIN(usize, vsize);
		mask = min - 1;
		
    	for (i = 0; i < vsize; i++)
	   	{
//			yout = ((vsize-1) - i);
//			yout = i;
    		for (j = 0; j < usize; j++)
    		{
//				*(q++) = v[i*usize + j];
//				*(q++) = v[TWIDOUT(j,i)];
//				q[TWIDOUT(j&mask,yout&mask) + (j/min + yout/min)*min*min] = v[i*usize + j];
//				q[i*usize + j] = v[TWIDOUT(j&mask,yout&mask) + (j/min + yout/min)*min*min];
				*(q++) = v[TWIDOUT(j&mask,i&mask) + (j/min + i/min)*min*min];
    		}
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

#ifdef OLD_TA_CHECK
void ta_check(DWORD addr)
{
/*	DWORD d = *(DWORD *) &ta_mem[0];
	DWORD * p = (DWORD *) &ta_mem[0];
	DWORD options = d & 0x1FFFFFFF;
	long cmd = (d >> 29) & 0x7; */

	DWORD * p = (DWORD *) &ta_mem[addr & 0xFF]; // 0x00 o 0x20
	DWORD pcw = *p;
	int pcw_para_type = (pcw >> 29) & 0x7;

/*	if ((addr & 0xFF) == 0x20)
		return; */

	switch(pcw_para_type)
	{
		// Control Parameter
		
		case 0: // End Of List
  		{
    		logxmsg(LOG_PVR, "pcw: End Of List\n");
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
		
		case 1: // User Tile Clip
  		{
    		logxmsg(LOG_PVR, "pcw: User Tile Clip\n");
    		logxmsg(LOG_PVR, "USER_CLIP: Xmin: %x\n", p[4]);
    		logxmsg(LOG_PVR, "USER_CLIP: Ymin: %x\n", p[5]);
    		logxmsg(LOG_PVR, "USER_CLIP: Xmax: %x\n", p[6]);
    		logxmsg(LOG_PVR, "USER_CLIP: Ymax: %x\n", p[7]);
    		logxmsg(LOG_PVR, "USER_CLIP: NO IMPLEMENTADO\n");
		}
		break;
		
		case 2: // Object List Set
		{
			logxmsg(LOG_PVR, "pcw: Object List Set\n");
			logxmsg(LOG_PVR, "pcw: NO IMPLEMENTADO\n");
		}
		break;

		case 4: // POLYGON/MODIFIER VOLUME
  		{
			// para control
  			int pcw_list_type = (pcw >> 24) & 0x7;
  			
  			// group control
			int pcw_group_en  = (pcw >> 23) & 0x1;
  			int pcw_strip_len = (pcw >> 18) & 0x3;
  			int pcw_user_clip = (pcw >> 16) & 0x3;
  			
			// obj control
  			int pcw_shadow		= (pcw >> 7) & 0x1;
  			int pcw_volume		= (pcw >> 6) & 0x1;
  			int pcw_col_type	= (pcw >> 4) & 0x3;
  			int pcw_texture		= (pcw >> 3) & 0x1;
  			int pcw_offset		= (pcw >> 2) & 0x1;
  			int pcw_gouraud		= (pcw >> 1) & 0x1;
  			int pcw_16bit_UV	= (pcw >> 0) & 0x1;

    		logxmsg(LOG_PVR, "pcw: Polygon or Modifier Volume\n");

			pvr_listtype = pcw_list_type;
			pvr_registering = pcw_list_type;

    		switch(pcw_list_type)
    		{
    			case 0:	logxmsg(LOG_PVR, "pcw: list_type: opaque\n");						break;
    			case 1: logxmsg(LOG_PVR, "pcw: list_type: opaque modifier volume\n");		break;
    			case 2: logxmsg(LOG_PVR, "pcw: list_type: translucent\n");					break;
    			case 3: logxmsg(LOG_PVR, "pcw: list_type: translucent modifier volume\n");	break;
    			case 4: logxmsg(LOG_PVR, "pcw: list_type: punch through\n");				break;
    			default:	logxmsg(LOG_PVR, "pcw: list_type: RESERVED\n");					break;
			}

			// group control
			if (pcw_group_en)
			{
				logxmsg(LOG_PVR, "pcw: group_en\n");
				switch(pcw_strip_len)
				{
					case 0: logxmsg(LOG_PVR, "pcw: strip_len: 1\n");	break;
					case 1: logxmsg(LOG_PVR, "pcw: strip_len: 2\n");	break;
					case 2: logxmsg(LOG_PVR, "pcw: strip_len: 4\n");	break;
					case 3: logxmsg(LOG_PVR, "pcw: strip_len: 6\n");	break;
				}
				switch(pcw_user_clip)
				{
					case 0: logxmsg(LOG_PVR, "pcw: user_clip: disable\n");	break;
					case 1: logxmsg(LOG_PVR, "pcw: user_clip: reserved\n");	break;
					case 2: logxmsg(LOG_PVR, "pcw: user_clip: inside enable\n");	break;
					case 3: logxmsg(LOG_PVR, "pcw: user_clip: outside enable\n");	break;
				}
			}
		
			// obj control
			
			// acá tenemos que determinar qué tipo de parámetros hay que leer
			global_parameter = vertex_parameter = -1;

			// tenemos que:
			// bit 6: volume		var: pcw_volume
			// bit 5-4: col_type	var: pcw_col_type
			// bit 3: texture		var: pcw_texture
			// bit 2: offset		var: pcw_offset
			// bit 1: gouraud		var: pcw_gouraud
			// bit 0: 16bit_UV		var: pcw_16bit_UV
				
			// revisemos los parámetros para la lista de vertex
			if (pcw_texture == 0)
   			{
				if (pcw_volume == 0)
				{
					switch(pcw_col_type)
					{
						case 0:
						global_parameter = 0;
						vertex_parameter = 0;
						break;
						
						case 1:
						global_parameter = 0;
						vertex_parameter = 1;
						break;
						
						case 2:
						global_parameter = 1;
						vertex_parameter = 2;
						break;
						
						case 3:
						global_parameter = 0;
						vertex_parameter = 2;
						break;
					}
				}
				else
				{
					switch (pcw_col_type)
					{
						case 0:
						global_parameter = 3;
						vertex_parameter = 9;
						break;
						
						case 2:
						global_parameter = 4;
						vertex_parameter = 10;
						break;
						
						case 3:
						global_parameter = 3;
						vertex_parameter = 10;
						break;
					}
				}
			}
			else // textured
			{
				if (pcw_volume == 0)
				{
					switch(pcw_col_type)
					{
						case 0:
						if (pcw_16bit_UV == 0)
						{
							global_parameter = 0;
							vertex_parameter = 3;
						}
						else
						{
							global_parameter = 0;
							vertex_parameter = 4;
						}
						break;
						
						case 1:
						if (pcw_16bit_UV == 0)
						{
							global_parameter = 0;
							vertex_parameter = 5;
						}
						else
						{
							vertex_parameter = 6;
							global_parameter = 0;
						}
						break;
						
						case 2:
						if (pcw_16bit_UV == 0)
						{
							if (pcw_offset == 0)
							{
								global_parameter = 1;
								vertex_parameter = 7;
							}
							else
							{
								global_parameter = 2;
								vertex_parameter = 7;
							}
						}
						else
						{
							if (pcw_offset == 0)
							{
								global_parameter = 1;
								vertex_parameter = 8;
							}
							else
							{
								global_parameter = 2;
								vertex_parameter = 8;
							}
						}
						break;
						
						case 3:
						if (pcw_16bit_UV == 0)
						{
							global_parameter = 0;
							vertex_parameter = 7;
						}
						else
						{
							global_parameter = 0;
							vertex_parameter = 8;
						}
						break;
					}
				}
				else
				{
					switch(pcw_col_type)
					{
						case 0:
						if (pcw_16bit_UV == 0)
						{
							global_parameter = 3;
							vertex_parameter = 11;
						}
						else
						{
							global_parameter = 3;
							vertex_parameter = 12;
						}
						break;
						
						case 2:
						if (pcw_16bit_UV == 0)
						{
							global_parameter = 4;
							vertex_parameter = 13;
						}
						else
						{
							global_parameter = 4;
							vertex_parameter = 14;
						}
						break;
						
						case 3:
						if (pcw_16bit_UV == 0)
						{
							global_parameter = 3;
							vertex_parameter = 13;
						}
						else
						{
							global_parameter = 3;
							vertex_parameter = 14;
						}
						break;
					}
				}
			}

			logxmsg(LOG_PVR, "pcw: global parameter: polygon type %d\n", global_parameter);
//			logxmsg(LOG_PVR, "pcw: vertex parameter: polygon type %d\n", vertex_parameter);

			if (pcw_list_type == 0 || pcw_list_type == 2 || pcw_list_type == 4)
			{
				DWORD p1 = *(DWORD *) &p[1];
				int depthmode = (p1 >> 29) & 0x7;
				int cullingmode = (p1 >> 27) & 0x3;
				int zwrite = (p1 >> 26) & 0x1;
				int texture = (p1 >> 25) & 0x1;
				int offset = (p1 >> 24) & 0x1;
				int gouraud = (p1 >> 23) & 0x1;
				int uv16bit = (p1 >> 22) & 0x1;
				int cachebypass = (p1 >> 21) & 0x1;
				int dcalcctrl = (p1 >> 20) & 0x1;
				// 3 parámetros inútiles (specular, shading, uvformat)
//				int d_calc_exact = (p1 >> 20) & 0x1;

				logxmsg(LOG_PVR, "uv16bit: %d\n", uv16bit);

				if (pcw_list_type == 2) // transparent polygon
					pvr_depthmode = GL_GEQUAL;
				else
				if (pcw_list_type == 4) // punch-through polygon
					pvr_depthmode = GL_LEQUAL;
				else
				{
					switch(depthmode)
					{
						case 0:	pvr_depthmode = GL_NEVER;	logxmsg(LOG_PVR, "depthmode: never\n"); break;
						case 1:	pvr_depthmode = GL_LESS;	logxmsg(LOG_PVR, "depthmode: less\n"); break;
						case 2:	pvr_depthmode = GL_EQUAL;	logxmsg(LOG_PVR, "depthmode: equal\n"); break;
						case 3:	pvr_depthmode = GL_LEQUAL;	logxmsg(LOG_PVR, "depthmode: lequal\n"); break;
						case 4:	pvr_depthmode = GL_GREATER;	logxmsg(LOG_PVR, "depthmode: greater\n"); break;
						case 5:	pvr_depthmode = GL_NOTEQUAL;logxmsg(LOG_PVR, "depthmode: notequal\n"); break;
						case 6:	pvr_depthmode = GL_GEQUAL;	logxmsg(LOG_PVR, "depthmode: gequal\n"); break;
						case 7:	pvr_depthmode = GL_ALWAYS;	logxmsg(LOG_PVR, "depthmode: always\n"); break;
	
	/*
						case 0:	pvr_depthmode = GL_NEVER;	logxmsg(LOG_PVR, "depthmode: never\n"); break;
						case 1:	pvr_depthmode = GL_GREATER;	logxmsg(LOG_PVR, "depthmode: less\n"); break;
						case 2:	pvr_depthmode = GL_EQUAL;	logxmsg(LOG_PVR, "depthmode: equal\n"); break;
						case 3:	pvr_depthmode = GL_GEQUAL;	logxmsg(LOG_PVR, "depthmode: lequal\n"); break;
						case 4:	pvr_depthmode = GL_LESS;	logxmsg(LOG_PVR, "depthmode: greater\n"); break;
						case 5:	pvr_depthmode = GL_NOTEQUAL;logxmsg(LOG_PVR, "depthmode: notequal\n"); break;
						case 6:	pvr_depthmode = GL_LEQUAL;	logxmsg(LOG_PVR, "depthmode: gequal\n"); break;
						case 7:	pvr_depthmode = GL_ALWAYS;	logxmsg(LOG_PVR, "depthmode: always\n"); break;
	*/
					}
				}
				
				GLOP_DEPTHFUNC(pvr_depthmode);
				
				switch(cullingmode)
				{
					case 0: // desactivar culling
					{
							logxmsg(LOG_PVR, "cull: disable\n");
//							GLOP_DISABLE(GL_CULL_FACE);
					}
					break;
					
					case 1:
					case 2:
					case 3:
					{
							logxmsg(LOG_PVR, "cull: enable\n");
//							GLOP_ENABLE(GL_CULL_FACE);
					}
					break;
				}
				
				switch(zwrite)
				{
					case 0: logxmsg(LOG_PVR, "zwrite: enable\n"); GLOP_DEPTHMASK(GL_TRUE); break;
					case 1: logxmsg(LOG_PVR, "zwrite: disable\n"); GLOP_DEPTHMASK(GL_FALSE); break;
				}
			}

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
			
			if (pvr_srcblendmode)
				logxmsg(LOG_PVR, "srcblend: src select\n");
			if (pvr_dstblendmode)
				logxmsg(LOG_PVR, "dstblend: dst select\n");
			
			if ((p[2] >> 20) & 0x1) // alpha
			{
				GLOP_ENABLE(GL_BLEND);
			}
			else
			{
				GLOP_DISABLE(GL_BLEND);
			}

			if (pcw_texture == 1)
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
				{
					logxmsg(LOG_PVR, "texture: enable VQ compression for texture\n");
					pvr_texture_vq = 1;
				}
				else
				{
					logxmsg(LOG_PVR, "texture: disable compression\n");
					pvr_texture_vq = 0;
				}
	
#define CTT() { pvr_texture_pixelformat = -1; pvr_texture_components = -1; pvr_texture_pixelpack = -1; }

				switch (pixelformat)
				{
					case 0:
     				logxmsg(LOG_PVR, "texture: ARGB1555\n");
     				pvr_texture_pixelformat = GL_BGRA;
     				pvr_texture_components = 4;
     				pvr_texture_pixelpack = GL_UNSIGNED_SHORT_1_5_5_5_REV;
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
				    pvr_texture_pixelformat = GL_BGRA_EXT;
/*				    pvr_texture_pixelconvert = pcon_argb4444_to_rgba4444;
				    pvr_texture_pixelpack = GL_UNSIGNED_SHORT_4_4_4_4_EXT; */
//				    pvr_texture_pixelpack = GL_UNSIGNED_SHORT_4_4_4_4;
					pvr_texture_pixelpack = GL_UNSIGNED_SHORT_4_4_4_4_REV;
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
	
				switch((p[2] >> 13) & 0x3) // Filter Mode
				{
					case 0:
						GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
						GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
						break;							// Point Sampled

					case 1:
						GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
						GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
						break;							// Bilinear Filter

					case 2:		break;							// Tri-linear Pass A
					case 3:		break;							// Tri-linear Pass B
				}

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
		}
		break;

		case 5: // SPRITE
		{
			logxmsg(LOG_PVR, "TA: SPRITE\n");
			logxmsg(LOG_PVR, "SPRITE: NO IMPLEMENTADO\n");
		}
		break;
		
		case 7:
  		{
#ifdef DEBUG_VERTEX
    		logxmsg(LOG_PVR, "TA: VERTEX\n");
#endif

			logxmsg(LOG_PVR, "pcw: vertex parameter: polygon type %d\n", vertex_parameter);
    		
			if (vertexstart == true)
			{
				if (pvr_texture_surface)
				{
        			logxmsg(LOG_PVR, "llamando a get_texture(%d, %d, %d, %d)\n", pvr_texture_size_usize, pvr_texture_size_vsize, pvr_texture_surface, pvr_texture_twiddled);
     				get_texture(pvr_texture_size_usize, pvr_texture_size_vsize, pvr_texture_surface, pvr_texture_twiddled, pvr_texture_vq);
				}
//				glBlendFunc(pvr_srcblend, pvr_dstblend);
				GLOP_BLENDFUNC(pvr_srcblend, pvr_dstblend);
//				glBegin(GL_TRIANGLE_STRIP);
				GLOP_BEGIN(GL_TRIANGLE_STRIP);
				vertexstart = false;
			}

			switch (vertex_parameter)
			{
				case 0: // Non-Textured, Packed Color
				{
					float datos[3];
					float r, g, b, a;
					DWORD base_colour;
//					BYTE * base_color = &p[6];
//					float * x, *y, *z;
					
/*					x = &p[1];
					y = &p[2];
					z = &p[3]; */

					memcpy(&datos[0], &p[1], sizeof(float) * 3);
					memcpy(&base_colour, &p[6], sizeof(DWORD));

					a = ((base_colour >> 24) & 0xFF) / 255.0;
					r = ((base_colour >> 16) & 0xFF) / 255.0;
					g = ((base_colour >> 8)  & 0xFF) / 255.0;
					b = ((base_colour >> 0)  & 0xFF) / 255.0;

#ifdef DEBUG_VERTEX
					logxmsg(LOG_PVR, "vertex tipo 0: color=%f,%f,%f,%f coords=%f,%f,%f\n",
						r, g, b, a, datos[0], datos[1], datos[2]);
#endif
					GLOP_COLOR4F(r, g, b, a);
					GLOP_VERTEX3F(datos[0], datos[1], datos[2]);
/*					GLOP_COLOR4F(base_color[0] / 255.0, base_color[1] / 255.0, base_color[2] / 255.0, base_color[3] / 255.0);
					GLOP_VERTEX3F(*x, *y, *z); */
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
					GLOP_VERTEX3F(datos[0], datos[1], datos[2]);
				}
				break;
				
				case 3: // Packed Color
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
					logxmsg(LOG_PVR, "seteando texturas en %fx%f %02xx%02x\n", coords[3], coords[4], p[4], p[5]);
					logxmsg(LOG_PVR, "coordenadas: %f,%f,%f\n", coords[0], coords[1], coords[2]);
#endif
//					glColor4f(r, g, b, a);
					GLOP_COLOR4F(r, g, b, a);
//					glTexCoord2f(coords[3], coords[4]);
					GLOP_TEXCOORD2F(coords[3], coords[4]);
//					glVertex3f(coords[0], coords[1], coords[2] - 100);
					GLOP_VERTEX3F(coords[0], coords[1], coords[2]);
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
					GLOP_VERTEX3F(coords[0], coords[1], coords[2]);
				}
				break;
				
				default:
				{
					logxmsg(LOG_PVR, "VERTEX: tipo %d de vertex no implementado!\n", vertex_parameter);
				}
				break;
			}

    		if (pcw & (1 << 28))
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
       	
		default:	logxmsg(LOG_PVR, "TA: cmd %d desconocido - no implemetado\n");	break;
	}
}
#endif

void taListEnd()
{
/*	TA.control = *ta_address_pointer; */ // viene en blanco!

	pvr_listdone |= (1 << TA.registers.pcw_list_type);
	intc_add(pvr_lists[TA.registers.pcw_list_type], 100);
	
//	logxmsg(LOG_PVR, "pcw: taListEnd: End Of List: ld: %08x, reg: %08x, TA: %08x\n", pvr_listdone, pvr_registered, TA.control);
	
	if (pvr_listdone == pvr_registered)
	{
//		logxmsg(LOG_PVR, "pcw: taListEnd: RENDER_DONE\n");
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

void taPolyModifier()
{
	TA.control = *ta_address_pointer;
	
	logxmsg(LOG_PVR, "pcw: Polygon or Modifier Volume\n");

	pvr_registering = TA.registers.pcw_list_type;
	
	switch(TA.registers.pcw_list_type)
	{
			case 0:	logxmsg(LOG_PVR, "pcw: list_type: opaque\n");						break;
			case 1: logxmsg(LOG_PVR, "pcw: list_type: opaque modifier volume\n");		break;
			case 2: logxmsg(LOG_PVR, "pcw: list_type: translucent\n");					break;
			case 3: logxmsg(LOG_PVR, "pcw: list_type: translucent modifier volume\n");	break;
			case 4: logxmsg(LOG_PVR, "pcw: list_type: punch through\n");				break;
			default:	logxmsg(LOG_PVR, "pcw: list_type: RESERVED\n");					break;
	}

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

	// acá tenemos que determinar qué tipo de parámetros hay que leer
	global_parameter = vertex_parameter = -1;
				
	// revisemos los parámetros para la lista de vertex
	if (TA.registers.pcw_texture == 0)
	{
		if (TA.registers.pcw_volume == 0)
		{
			switch(TA.registers.pcw_col_type)
			{
				case 0:
				global_parameter = 0;
				vertex_parameter = 0;
				break;
			
				case 1:
				global_parameter = 0;
				vertex_parameter = 1;
				break;
			
				case 2:
				global_parameter = 1;
				vertex_parameter = 2;
				break;
			
				case 3:
				global_parameter = 0;
				vertex_parameter = 2;
				break;
			}
		}
		else
		{
			switch (TA.registers.pcw_col_type)
			{
				case 0:
				global_parameter = 3;
				vertex_parameter = 9;
				break;
			
				case 2:
				global_parameter = 4;
				vertex_parameter = 10;
				break;
			
				case 3:
				global_parameter = 3;
				vertex_parameter = 10;
				break;
			}
		}
	}
	else // textured
	{
		if (TA.registers.pcw_volume == 0)
		{
			switch(TA.registers.pcw_col_type)
			{
				case 0:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					global_parameter = 0;
					vertex_parameter = 3;
				}
				else
				{
					global_parameter = 0;
					vertex_parameter = 4;
				}
				break;
				
				case 1:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					global_parameter = 0;
					vertex_parameter = 5;
				}
				else
				{
					vertex_parameter = 6;
					global_parameter = 0;
				}
				break;
				
				case 2:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					if (TA.registers.pcw_offset == 0)
					{
						global_parameter = 1;
						vertex_parameter = 7;
					}
					else
					{
						global_parameter = 2;
						vertex_parameter = 7;
					}
				}
				else
				{
					if (TA.registers.pcw_offset == 0)
					{
						global_parameter = 1;
						vertex_parameter = 8;
					}
					else
					{
						global_parameter = 2;
						vertex_parameter = 8;
					}
				}
				break;
					
				case 3:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					global_parameter = 0;
					vertex_parameter = 7;
				}
				else
				{
					global_parameter = 0;
					vertex_parameter = 8;
				}
				break;
			}
		}
		else
		{
			switch(TA.registers.pcw_col_type)
			{
				case 0:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					global_parameter = 3;
					vertex_parameter = 11;
				}
				else
				{
					global_parameter = 3;
					vertex_parameter = 12;
				}
				break;
				
				case 2:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					global_parameter = 4;
					vertex_parameter = 13;
				}
			else
				{
					global_parameter = 4;
					vertex_parameter = 14;
				}
				break;
				
				case 3:
				if (TA.registers.pcw_16bit_UV == 0)
				{
					global_parameter = 3;
					vertex_parameter = 13;
				}
				else
				{
					global_parameter = 3;
					vertex_parameter = 14;
				}
				break;
			}
		}
	}

	logxmsg(LOG_PVR, "pcw: global parameter: polygon type %d\n", global_parameter);

	if (TA.registers.pcw_list_type == 0 || TA.registers.pcw_list_type == 2 || TA.registers.pcw_list_type == 4)
	{
		RendCtrl.control  = ta_address_pointer[1];

// 				logxmsg(LOG_PVR, "uv16bit: %d\n", uv16bit);
	
		if (TA.registers.pcw_list_type == 2) // transparent polygon
			pvr_depthmode = GL_GEQUAL;
		else
		if (TA.registers.pcw_list_type == 4) // punch-through polygon
			pvr_depthmode = GL_LEQUAL;
		else
		{
			switch(RendCtrl.registers.depthmode)
			{
				case 0:	pvr_depthmode = GL_NEVER;	logxmsg(LOG_PVR, "depthmode: never\n"); break;
				case 1:	pvr_depthmode = GL_LESS;	logxmsg(LOG_PVR, "depthmode: less\n"); break;
				case 2:	pvr_depthmode = GL_EQUAL;	logxmsg(LOG_PVR, "depthmode: equal\n"); break;
				case 3:	pvr_depthmode = GL_LEQUAL;	logxmsg(LOG_PVR, "depthmode: lequal\n"); break;
				case 4:	pvr_depthmode = GL_GREATER;	logxmsg(LOG_PVR, "depthmode: greater\n"); break;
				case 5:	pvr_depthmode = GL_NOTEQUAL;logxmsg(LOG_PVR, "depthmode: notequal\n"); break;
				case 6:	pvr_depthmode = GL_GEQUAL;	logxmsg(LOG_PVR, "depthmode: gequal\n"); break;
				case 7:	pvr_depthmode = GL_ALWAYS;	logxmsg(LOG_PVR, "depthmode: always\n"); break;
			}
		}
		
		GLOP_DEPTHFUNC(pvr_depthmode);
		
		switch(RendCtrl.registers.cullingmode)
		{
			case 0: // desactivar culling
			{
					logxmsg(LOG_PVR, "cull: disable\n");
					GLOP_DISABLE(GL_CULL_FACE);
			}
			break;
			
// 			case 1: break;
// 			case 2:

			case 3:
			{
					logxmsg(LOG_PVR, "cull: enable\n");
					GLOP_CULL_FACE(GL_BACK);
					GLOP_ENABLE(GL_CULL_FACE);
//							GLOP_ENABLE(GL_CULL_FACE);
			}
			break;
		}
		
		switch(RendCtrl.registers.zwrite)
		{
			case 0: logxmsg(LOG_PVR, "zwrite: enable\n"); GLOP_DEPTHMASK(GL_TRUE); break;
			case 1: logxmsg(LOG_PVR, "zwrite: disable\n"); GLOP_DEPTHMASK(GL_FALSE); break;
		}
	}
	
//			switch((pvr_srcblend = (ta_address_pointer[2] >> 29) & 0x7)) // srcblend
	switch((ta_address_pointer[2] >> 29) & 0x7) // srcblend
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
	
//			switch((pvr_dstblend = (ta_address_pointer[2] >> 26) & 0x7)) // dstblend
	switch((ta_address_pointer[2] >> 26) & 0x7) // dstblend
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
	
	pvr_srcblendmode = (ta_address_pointer[2] >> 25) & 0x1;
	pvr_dstblendmode = (ta_address_pointer[2] >> 24) & 0x1;
	
	if (pvr_srcblendmode)
		logxmsg(LOG_PVR, "srcblend: src select\n");
	if (pvr_dstblendmode)
		logxmsg(LOG_PVR, "dstblend: dst select\n");
	
	if ((ta_address_pointer[2] >> 20) & 0x1) // alpha
	{
		GLOP_ENABLE(GL_BLEND);
	}
	else
	{
		GLOP_DISABLE(GL_BLEND);
	}
	
	if (TA.registers.pcw_texture)
	{
		TexInfo.texture = ta_address_pointer[3];
	
		if (TexInfo.registers.mipmap)
			logxmsg(LOG_PVR, "texture: enable mipmap\n");
		else
			logxmsg(LOG_PVR, "texture: disable mipmap\n");
	
#define CTT() { pvr_texture_pixelformat = -1; pvr_texture_components = -1; pvr_texture_pixelpack = -1; }
	
		switch (TexInfo.registers.pixelformat)
		{
			case 0:
			logxmsg(LOG_PVR, "texture: ARGB1555\n");
			pvr_texture_pixelformat = GL_BGRA;
			pvr_texture_components = 4;
			pvr_texture_pixelpack = GL_UNSIGNED_SHORT_1_5_5_5_REV;
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
    		pvr_texture_pixelformat = GL_BGRA_EXT;	
	  		pvr_texture_pixelpack = GL_UNSIGNED_SHORT_4_4_4_4_REV;
			pvr_texture_components = 4;
			break;
	
			case 3: logxmsg(LOG_PVR, "texture: YUV422\n");	CTT();	break;
			case 4: logxmsg(LOG_PVR, "texture: BUMP\n");	CTT();	break;
			case 5: logxmsg(LOG_PVR, "texture: 4BPP_PALETTE\n");	CTT(); break;
			case 6: logxmsg(LOG_PVR, "texture: 8BPP_PALETTE\n");	CTT(); break;
		}
		
// 				logxmsg(LOG_PVR, "texture: surface %08x\n", texture_surface);
	
		if (TexInfo.registers.stride)
			logxmsg(LOG_PVR, "texture: stride\n");
		else
			logxmsg(LOG_PVR, "texture: no stride\n");
	
		switch((ta_address_pointer[2] >> 13) & 0x3) // Filter Mode
		{
			case 0:
			GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			break;							// Point Sampled
	
			case 1:
			GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			GLOP_TEXPARAMETERI(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			break;							// Bilinear Filter
	
			case 2:		break;							// Tri-linear Pass A
			case 3:		break;							// Tri-linear Pass B
		}
	
		switch((ta_address_pointer[2] >> 3) & 0x7)
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
		
		switch(ta_address_pointer[2] & 0x7)
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
		TexInfo.registers.texture_surface = 0;
}

void taVertexHandler()
{
//	logxmsg(LOG_PVR, "pcw: vertex parameter: polygon type %d, pcw: %08x\n", vertex_parameter, TA.control);
	
	TA.control = *ta_address_pointer;

	if (vertexstart == true)
	{
		if (TexInfo.registers.texture_surface)
		{
			logxmsg(LOG_PVR, "llamando a get_texture(%d, %d, %d, %d)\n", pvr_texture_size_usize, pvr_texture_size_vsize, TexInfo.registers.texture_surface << 3, TexInfo.registers.twiddled ? 0 : 1);
			get_texture(pvr_texture_size_usize, pvr_texture_size_vsize, TexInfo.registers.texture_surface << 3, TexInfo.registers.twiddled ? 0 : 1, TexInfo.registers.vq);
		}
//				glBlendFunc(pvr_srcblend, pvr_dstblend);
		GLOP_BLENDFUNC(pvr_srcblend, pvr_dstblend);
//				glBegin(GL_TRIANGLE_STRIP);
		GLOP_BEGIN(GL_TRIANGLE_STRIP);
		vertexstart = false;
	}

	switch (vertex_parameter)
	{
		case 0: // Non-Textured, Packed Color
		{
			memcpy(&coords[0], &ta_address_pointer[1], sizeof(float) * 3);
			memcpy(&base_colour, &ta_address_pointer[6], sizeof(DWORD));

			colors[0] = ((base_colour >> 24) & 0xFF) / 255.0;
			colors[1] = ((base_colour >> 16) & 0xFF) / 255.0;
			colors[2] = ((base_colour >> 8)  & 0xFF) / 255.0;
			colors[3] = ((base_colour >> 0)  & 0xFF) / 255.0;

#ifdef DEBUG_VERTEX_NEW
			logxmsg(LOG_PVR, "vertex tipo 0: color=%f,%f,%f,%f coords=%f,%f,%f\n",
				colors[0], colors[1], colors[2], colors[3], coords[0], coords[1], coords[2]);
#endif
			GLOP_COLOR4F(colors[1], colors[2], colors[3], colors[0]);
			GLOP_VERTEX3F(coords[0], coords[1], coords[2]);
		}
		break;

		case 1:
		{			
			memcpy(&data[0], &ta_address_pointer[1], sizeof(float)*7);

#ifdef DEBUG_VERTEX_NEW
			logxmsg(LOG_PVR, "vertex tipo 1: color %f %f %f %f\n", data[4], data[5], data[6], data[3]);
			logxmsg(LOG_PVR, "posición: %f %f %f\n", data[0], data[1], data[2]);
#endif
//					glColor4f(datos[4], datos[5], datos[6], datos[3]);
			GLOP_COLOR4F(data[4],data[5],data[6], data[3]);
//					glVertex3f(datos[0], datos[1], datos[2] - 100);
			GLOP_VERTEX3F(data[0], data[1],data[2]);
		}
		break;
				
		case 3: // Packed Color
		{				
			memcpy(&base_colour, &ta_address_pointer[6], sizeof(DWORD));
			
			memcpy(&coords[0], &ta_address_pointer[1], sizeof(float)*5);

			colors[0] = ((base_colour >> 24) & 0xFF) / 255.0;
			colors[1] = ((base_colour >> 16) & 0xFF) / 255.0;
			colors[2] = ((base_colour >> 8)  & 0xFF) / 255.0;
			colors[3] = ((base_colour >> 0)  & 0xFF) / 255.0;

#ifdef DEBUG_VERTEX_NEW
			logxmsg(LOG_PVR, "seteando colors rgba: %f %f %f %f\n", colors[0], colors[1], colors[2], colors[3]);
//			logxmsg(LOG_PVR, "seteando texturas en %fx%f %02xx%02x\n", coords[3], coords[4], ta_address_pointer[4], ta_address_pointer[5]);
			logxmsg(LOG_PVR, "coordenadas: %f,%f,%f\n", coords[0], coords[1], coords[2]);
#endif
//					glColor4f(r, g, b, a);
			GLOP_COLOR4F(colors[1], colors[2], colors[3], colors[0]);
//					glTexCoord2f(coords[3], coords[4]);
			GLOP_TEXCOORD2F(coords[3],coords[4]);
//					glVertex3f(coords[0], coords[1], coords[2] - 100);
			GLOP_VERTEX3F(coords[0], coords[1], coords[2]);
		}
		break;

		case 5:
		{			
			memcpy(&coords[0], &ta_address_pointer[1], sizeof(float)*5);

			memcpy(&colors[0], &ta_address_pointer[8], sizeof(float)*4);

#ifdef DEBUG_VERTEX_NEW
//					logxmsg(LOG_PVR, "seteando colors rgba: %f %f %f %f\n", r, g, b, a);
			logxmsg(LOG_PVR, "seteando texturas en %fx%f\n", coords[3], coords[4]);
			logxmsg(LOG_PVR, "coordenadas: %f,%f,%f\n", coords[0], coords[1], coords[2]);
#endif
//					glColor4f(colors[1], colors[2], colors[3], colors[0]);
			GLOP_COLOR4F(colors[1], colors[2], colors[3], colors[0]);
//					glTexCoord2f(coords[3], coords[4]);
			GLOP_TEXCOORD2F(coords[3], coords[4]);
//					glVertex3f(coords[0], coords[1], coords[2] - 100);
			GLOP_VERTEX3F(coords[0], coords[1],coords[2]);
		}
		break;
				
		default:
		{
			logxmsg(LOG_PVR, "VERTEX: tipo %d de vertex no implementado!\n", vertex_parameter);
		}
		break;
	}

	if (TA.registers.pcw_end_of_strip)
	{
#ifdef DEBUG_VERTEX_NEW
		logxmsg(LOG_PVR, "VERTEX: end-of-strip\n");
#endif
//    			glEnd();
		GLOP_END();
		pvr_registering = -1;
		vertexstart = true;
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
			logxmsg(LOG_PVR, "DibujarFramebuffer: ARGB0555\n");
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_5_5_1, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;

		case FRAMEBUFFER_RGB565:
		{
			logxmsg(LOG_PVR, "DibujarFramebuffer: RGB565\n");
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;

		case FRAMEBUFFER_RGB888:
		{
			// FIXME?
			logxmsg(LOG_PVR, "DibujarFramebuffer: RGB888\n");
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_RGB, GL_UNSIGNED_BYTE, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;

		case FRAMEBUFFER_ARGB0888:
		{
			logxmsg(LOG_PVR, "DibujarFramebuffer: ARGB0888\n");
	  		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screentexwidth, screentexheight, 0, GL_BGRA_EXT, GL_UNSIGNED_INT_8_8_8_8_REV, get_memory_pointer(0xA5000000 + pvr_fb_r_sof1));
		}
		break;
	}
//	glEnable(GL_BLEND);
//	glDisable(GL_DEPTH_TEST);
//	glClear(GL_DEPTH_BUFFER_BIT);
//	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
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
	glEnable(GL_DEPTH_TEST);
	// fin textura 1024

//	logxmsg(LOG_PVR, "DibujarFramebuffer: SDL_GL_SwapBuffers\n");
//	SDL_GL_SwapBuffers();
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
	glOrtho(0, screenancho, screenheight, 0, -32768.0, 32768.0);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
//	glScalef(1.0f, 1.0f, -1.0f);

//		glScalef(1.0/640.0,1.0/480.0,1.0);
//		glTranslatef(-320.0f, -240.0f, -1.0f);

//		glOrtho(0, (GLdouble)width, (GLdouble)height, 0.0, 0.1, 100.0);
//		glPixelZoom(1.0, -1.0);

	logxmsg(LOG_PVR, "screeninit: saliendo\n");

	init_sOglP();

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
