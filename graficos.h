#ifndef _GRAFICOS_H
#define _GRAFICOS_H

#define MAX_TEXTURE_COUNT 10

extern	int		screenbits;
extern	int		screenformat;
extern	int		screenwidth;
extern	int		screenancho; // ancho en pixeles
extern	float	screentexwidth;
extern	float	screentexheight;
extern	int		screenheight;
extern	int		pvr_framebufferdisplay;
extern	int 	pvr_scanline;
extern	DWORD	pvr_fb_r_ctrl;
extern	DWORD	pvr_fb_r_sof1;

// sync pulse generator
extern	DWORD	pvr_spg_load;
extern	DWORD	pvr_spg_load_vcount;
extern	DWORD	pvr_spg_vblank_int;

extern	DWORD	pvr_isp_backgnd_t;
extern	DWORD 	pvr_param_base;
extern	DWORD 	pvr_region_base;
extern	DWORD	pvr_ta_itp_current;
extern	DWORD	pvr_ta_isp_base;
extern	WORD	pvr_spg_vblank_int_in;
extern	WORD	pvr_spg_vblank_int_out;
extern	GLuint	pvr_textures[MAX_TEXTURE_COUNT];
extern	DWORD	pvr_lists[];
extern	int 	pvr_3dscene;
extern	DWORD	pvr_registered;
extern	int		pvr_listdone;

#define FRAMEBUFFER_ARGB0555	0
#define FRAMEBUFFER_RGB565		1
#define FRAMEBUFFER_RGB888		2
#define FRAMEBUFFER_ARGB0888	3

int screeninit(void);
SDL_Surface * draw_backscreen(void);
void DibujarFramebuffer();
void DibujarGL(SDL_Surface * sfc);
void limpiar_pantalla();

// callbacks
void cb_tastart(DWORD addr, void * p, size_t size);
void cb_isp_backgnd_t(DWORD addr, void * p, size_t size);

extern	SDL_Surface *screen;
// extern	SDL_Surface *backscreen;
// extern	SDL_Surface *glscreen;

#endif // _GRAFICOS_H
