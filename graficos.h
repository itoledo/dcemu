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
extern	DWORD	pvr_spg_vblank_int;
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
void limpiar_pantalla();

// callbacks
void cb_tastart(DWORD addr, void * p, size_t size);

extern	SDL_Surface *screen;
// extern	SDL_Surface *backscreen;
// extern	SDL_Surface *glscreen;

