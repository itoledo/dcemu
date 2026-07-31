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

/* Ciclos de CPU por linea de barrido, derivado de SPG_LOAD y SPG_CONTROL.
   Lo recalcula mem.c cuando el guest los escribe. */
extern	DWORD	pvr_ciclos_linea;
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

int glinit(void);
int screeninit(void);
SDL_Surface * draw_backscreen(void);
void DibujarFramebuffer();

/* Vuelca la RAM de video a un BMP, sin pasar por OpenGL. Tecla F5. */
int volcar_framebuffer(const char * ruta);

/* Lo que GL rasterizo, no lo que hay en la RAM de video: es la unica forma de
   verificar una demo 3D sin depender de que la captura de la ventana funcione.
   Ver el comentario en graficos.c. */
int volcar_gl(const char * ruta);

/* Cuantas escenas se rindieron y con cuantas tiras las ultimas. Lo llama
   traza_resumen() al salir; solo dice algo con --traza-mem. */
void traza_ta_resumen(void);

/* Barra de titulo: el nombre de la imagen o del binario que se esta corriendo.
   Hay que llamarla antes de screeninit(), que es quien la aplica. */
extern	char	titulo_ventana[256];
void titulo_poner(const char * ruta);

/* El contador de FPS del titulo: se marca en cada SDL_GL_SwapBuffers y la
   tecla `f` lo alterna. Arranca prendido. */
extern	int		fps_visible;
void fps_marcar_cuadro(void);

/* Convertidor YUV del TA (0x10800000). Un bloque de 32 bytes; cuando se junta
   un macrobloque entero se convierte y se escribe en PVR_YUV_ADDR. */
void pvr_yuv_bloque(void * datos);
void pvr_yuv_reiniciar(void);

extern DWORD pvr_yuv_convertidos;	/* lo que devuelve PVR_YUV_STAT */
void DibujarGL(SDL_Surface * sfc);
void limpiar_pantalla();

// callbacks
void cb_tastart(DWORD addr, void * p, size_t size);
void cb_isp_backgnd_t(DWORD addr, void * p, size_t size);

// Estaban tras un #if defined(POSX), asi que en Windows mem.c los llamaba sin
// prototipo. Declararlos siempre.
void cb_renderstart(DWORD addr, void * p, size_t size);
void cb_ta_isp_base(DWORD addr, void * p, size_t size);
void ta_avanzar_itp(void);
void cb_param_base(DWORD addr, void * p, size_t size);
void cb_region_base(DWORD addr, void * p, size_t size);
void cb_fb_w_ctrl(DWORD addr, void * p, size_t size);
void cb_ppblocksize(DWORD addr, void * p, size_t size);
void cb_fb_r_sof1(DWORD addr, void * p, size_t size);


extern	SDL_Surface *screen;
extern SDL_Surface *outputscreen;

/* ta_address_pointer lo declara ta.h, que es donde vive. */

// it is better to have ta_check split up into smaller functions

void	 taListEnd();
void	 doUserClip();
void objectListSet();
void taPolyModifier();
void  taVertexHandler();
void taSprite();

#endif // _GRAFICOS_H
