extern	int		screenbits;
extern	int		screenformat;
extern	int		screenwidth;
extern	int		screenheight;
extern	int		pvr_framebufferdisplay;
extern	DWORD	pvr_fb_r_ctrl;
extern	DWORD	pvr_fb_r_sof1;

#define FRAMEBUFFER_ARGB0555	0
#define FRAMEBUFFER_RGB565		1
#define FRAMEBUFFER_RGB888		2
#define FRAMEBUFFER_ARGB0888	3

int screeninit(void);
SDL_Surface * draw_backscreen(void);

extern	SDL_Surface *screen;
// extern	SDL_Surface *backscreen;
// extern	SDL_Surface *glscreen;

