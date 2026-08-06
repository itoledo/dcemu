/****************************************************************************

	GLMODERNO - las entradas de GL que opengl32.dll no exporta, y el FBO

	Ver glmoderno.h. Etapa 2.a de docs/rendimiento-plan.md.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include <SDL/SDL.h>
/* SDL_opengl.h y no <GL/gl.h> a secas: en Windows el gl.h del SDK usa
   WINGDIAPI y APIENTRY, que los define <windows.h>, y SDL_opengl.h es quien lo
   arrastra. Incluir gl.h solo da veinte paginas de errores de sintaxis dentro
   de la cabecera del sistema. Es lo mismo que hace graficos.c. */
#include <SDL/SDL_opengl.h>

#include "glmoderno.h"
#include "traza.h"

/*
	Las constantes van definidas aca y no se toman de un GL/glext.h.

	No es pereza: el arbol compila con el <GL/gl.h> de 1.1 que trae el SDK de
	Windows y no tiene glext.h, y arrastrar uno --o la biblioteca de carga de
	turno-- seria una dependencia nueva para diez enteros que el estandar fija
	para siempre. Es la misma decision que ya tomo offset_iniciar() con
	GL_COLOR_SUM.
*/
#define GL_FRAMEBUFFER					0x8D40
#define GL_READ_FRAMEBUFFER				0x8CA8
#define GL_DRAW_FRAMEBUFFER				0x8CA9
#define GL_RENDERBUFFER					0x8D41
#define GL_COLOR_ATTACHMENT0			0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT		0x821A
#define GL_DEPTH24_STENCIL8				0x88F0
#define GL_RGBA8						0x8058
#define GL_FRAMEBUFFER_COMPLETE			0x8CD5

typedef void (APIENTRY * PFN_GEN_FB)(GLsizei, GLuint *);
typedef void (APIENTRY * PFN_DEL_FB)(GLsizei, const GLuint *);
typedef void (APIENTRY * PFN_BIND_FB)(GLenum, GLuint);
typedef void (APIENTRY * PFN_GEN_RB)(GLsizei, GLuint *);
typedef void (APIENTRY * PFN_DEL_RB)(GLsizei, const GLuint *);
typedef void (APIENTRY * PFN_BIND_RB)(GLenum, GLuint);
typedef void (APIENTRY * PFN_RB_STORAGE)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (APIENTRY * PFN_FB_RB)(GLenum, GLenum, GLenum, GLuint);
typedef void (APIENTRY * PFN_FB_TEX2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY * PFN_CHECK_FB)(GLenum);
typedef void (APIENTRY * PFN_BLIT_FB)(GLint, GLint, GLint, GLint,
									  GLint, GLint, GLint, GLint,
									  GLbitfield, GLenum);

static PFN_GEN_FB		p_glGenFramebuffers;
static PFN_DEL_FB		p_glDeleteFramebuffers;
static PFN_BIND_FB		p_glBindFramebuffer;
static PFN_GEN_RB		p_glGenRenderbuffers;
static PFN_DEL_RB		p_glDeleteRenderbuffers;
static PFN_BIND_RB		p_glBindRenderbuffer;
static PFN_RB_STORAGE	p_glRenderbufferStorage;
static PFN_FB_RB		p_glFramebufferRenderbuffer;
static PFN_FB_TEX2D		p_glFramebufferTexture2D;
static PFN_CHECK_FB		p_glCheckFramebufferStatus;
static PFN_BLIT_FB		p_glBlitFramebuffer;

static int		hay_fbo = 0;
static int		version = 0;

static GLuint	fbo = 0;
static GLuint	fbo_color = 0;		/* textura, no renderbuffer: ver abajo */
static GLuint	fbo_prof = 0;
static int		fbo_w = 0;
static int		fbo_h = 0;
static int		ligado = 0;

/*
	Resuelve un nombre con su respaldo EXT.

	Los dos sufijos importan y no son intercambiables: la version sin sufijo
	es la de GL 3.0 (ARB_framebuffer_object) y la EXT es la extension previa,
	que ademas tiene reglas mas estrictas -- todos los adjuntos del mismo
	tamano y formato. Se prefiere la primera y se cae a la segunda, igual que
	offset_iniciar() con glSecondaryColorPointer.
*/
static void * resolver(const char * nombre)
{
	char	con_ext[64];
	void *	p = SDL_GL_GetProcAddress(nombre);

	if (p != NULL)
		return p;

	snprintf(con_ext, sizeof(con_ext), "%sEXT", nombre);

	return SDL_GL_GetProcAddress(con_ext);
}

/* "4.6.0 NVIDIA 551.86" -> 46. Se lee de la cadena y no con glGetIntegerv de
   GL_MAJOR_VERSION porque ese enum es de 3.0: en un contexto viejo devuelve
   error y deja el entero sin tocar, o sea con lo que hubiera en la pila. */
static int leer_version(const char * s)
{
	int mayor = 0, menor = 0;

	if (s == NULL)
		return 0;

	if (sscanf(s, "%d.%d", &mayor, &menor) != 2)
		return 0;

	return mayor * 10 + menor;
}

int glmoderno_iniciar(void)
{
	const char * v = (const char *) glGetString(GL_VERSION);

	version = leer_version(v);

	p_glGenFramebuffers			= (PFN_GEN_FB)		resolver("glGenFramebuffers");
	p_glDeleteFramebuffers		= (PFN_DEL_FB)		resolver("glDeleteFramebuffers");
	p_glBindFramebuffer			= (PFN_BIND_FB)		resolver("glBindFramebuffer");
	p_glGenRenderbuffers		= (PFN_GEN_RB)		resolver("glGenRenderbuffers");
	p_glDeleteRenderbuffers		= (PFN_DEL_RB)		resolver("glDeleteRenderbuffers");
	p_glBindRenderbuffer		= (PFN_BIND_RB)		resolver("glBindRenderbuffer");
	p_glRenderbufferStorage		= (PFN_RB_STORAGE)	resolver("glRenderbufferStorage");
	p_glFramebufferRenderbuffer	= (PFN_FB_RB)		resolver("glFramebufferRenderbuffer");
	p_glFramebufferTexture2D	= (PFN_FB_TEX2D)	resolver("glFramebufferTexture2D");
	p_glCheckFramebufferStatus	= (PFN_CHECK_FB)	resolver("glCheckFramebufferStatus");
	p_glBlitFramebuffer			= (PFN_BLIT_FB)		resolver("glBlitFramebuffer");

	hay_fbo = (p_glGenFramebuffers && p_glDeleteFramebuffers && p_glBindFramebuffer
			&& p_glGenRenderbuffers && p_glDeleteRenderbuffers && p_glBindRenderbuffer
			&& p_glRenderbufferStorage && p_glFramebufferRenderbuffer
			&& p_glFramebufferTexture2D && p_glCheckFramebufferStatus
			&& p_glBlitFramebuffer);

	/*
		Sale por stderr y no por logxmsg porque tiene que verse sin LOGGING:
		es lo primero que hay que mirar cuando el camino nuevo no dibuja, y
		este arbol ya perdio tiempo por creerle a una suposicion sobre lo que
		el contexto ofrecia (los planos de alfa).
	*/
	fprintf(stderr, "gl: %s -- version %d.%d, FBO %s\n",
		(v != NULL) ? v : "(sin version)",
		version / 10, version % 10,
		hay_fbo ? "disponible" : "NO disponible");

	if (traza_activa)
	{
		const char * ven = (const char *) glGetString(GL_VENDOR);
		const char * ren = (const char *) glGetString(GL_RENDERER);

		fprintf(stderr, "gl: %s / %s\n",
			(ven != NULL) ? ven : "?", (ren != NULL) ? ren : "?");
	}

	return hay_fbo;
}

int glmoderno_hay_fbo(void)	{ return hay_fbo; }
int glmoderno_version(void)	{ return version; }
int glmoderno_fbo_ancho(void) { return fbo_w; }
int glmoderno_fbo_alto(void)  { return fbo_h; }
int glmoderno_fbo_ligado(void) { return ligado; }

static void fbo_soltar(void)
{
	if (fbo_color)	{ glDeleteTextures(1, &fbo_color); fbo_color = 0; }
	if (fbo_prof)	{ p_glDeleteRenderbuffers(1, &fbo_prof); fbo_prof = 0; }
	if (fbo)		{ p_glDeleteFramebuffers(1, &fbo); fbo = 0; }

	fbo_w = fbo_h = 0;
}

int glmoderno_fbo_asegurar(int ancho, int alto)
{
	GLenum estado;

	if (!hay_fbo || ancho <= 0 || alto <= 0)
		return 0;

	/* Solo crece. El guest cambia de modo de video en caliente y recrear el
	   destino en cada cambio tiraria el cuadro a medio dibujar. */
	if (fbo != 0 && ancho <= fbo_w && alto <= fbo_h)
		return 1;

	if (ancho < fbo_w) ancho = fbo_w;
	if (alto  < fbo_h) alto  = fbo_h;

	fbo_soltar();

	/*
		El color va en una **textura** y no en un renderbuffer aunque hoy solo
		se lea con glReadPixels y se copie con glBlitFramebuffer. Es la etapa
		2.b la que la va a querer como entrada de un shader, y una textura
		tambien se puede leer con las dos operaciones de ahora, asi que elegir
		el renderbuffer seria ahorrarse nada y tener que rehacerlo.
	*/
	glGenTextures(1, &fbo_color);
	glBindTexture(GL_TEXTURE_2D, fbo_color);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ancho, alto, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	/*
		Profundidad y plantilla en un solo adjunto empaquetado. **Las dos hacen
		falta**: la plantilla lleva los volumenes modificadores, y la
		profundidad tiene que ser de 24 porque profundidad_ta() comprime las z
		con un log2 y aun asi una escena ocupa una fraccion del rango -- con 16
		bits dos capas contiguas caen en el mismo valor y la de arriba
		desaparece, que es como pvr_rtt_sized perdio sus dos marcas.
	*/
	p_glGenRenderbuffers(1, &fbo_prof);
	p_glBindRenderbuffer(GL_RENDERBUFFER, fbo_prof);
	p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, ancho, alto);
	p_glBindRenderbuffer(GL_RENDERBUFFER, 0);

	p_glGenFramebuffers(1, &fbo);
	p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, fbo_color, 0);
	p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
		GL_RENDERBUFFER, fbo_prof);

	estado = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);

	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	ligado = 0;

	if (estado != GL_FRAMEBUFFER_COMPLETE)
	{
		fprintf(stderr, "gl: el FBO de %dx%d quedo incompleto (0x%04x);"
			" se sigue dibujando en la ventana\n", ancho, alto,
			(unsigned) estado);

		fbo_soltar();
		hay_fbo = 0;

		return 0;
	}

	fbo_w = ancho;
	fbo_h = alto;

	fprintf(stderr, "gl: destino de render propio de %dx%d\n", fbo_w, fbo_h);

	return 1;
}

void glmoderno_fbo_ligar(int puesto)
{
	if (!hay_fbo || fbo == 0)
		return;

	p_glBindFramebuffer(GL_FRAMEBUFFER, puesto ? fbo : 0);
	ligado = puesto ? 1 : 0;
}

void glmoderno_presentar(int ancho, int alto, int ven_ancho, int ven_alto)
{
	int	dx, dy, dw, dh;

	if (!hay_fbo || fbo == 0 || ancho <= 0 || alto <= 0
	||  ven_ancho <= 0 || ven_alto <= 0)
		return;

	/*
		Relacion de aspecto conservada: se escala por el lado que primero
		llena y lo que sobra queda en negro. Hoy 640x480 y 800x600 son los dos
		4:3 y no se ve diferencia, pero un guest que sale en 320x240 o una
		ventana que no sea 4:3 si la ven -- y la version escalada del cuadro
		la vera siempre.
	*/
	if (ancho * ven_alto > ven_ancho * alto)
	{
		dw = ven_ancho;
		dh = (int) ((long long) ven_ancho * alto / ancho);
	}
	else
	{
		dh = ven_alto;
		dw = (int) ((long long) ven_alto * ancho / alto);
	}

	dx = (ven_ancho - dw) / 2;
	dy = (ven_alto  - dh) / 2;

	p_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	/* Las franjas: si no se limpian, queda el cuadro anterior en el borde
	   cuando el destino no llena la ventana entera. */
	if (dw != ven_ancho || dh != ven_alto)
	{
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	/*
		GL_LINEAR y no GL_NEAREST: con escala 1 los dos rectangulos miden lo
		mismo en la mayoria de los casos y da igual, y cuando no --640x480
		sobre 800x600, o el escalado de resolucion interna hacia abajo-- el
		filtrado es lo que evita el aliasing de vecino mas cercano que ya se
		paga en volcar_a_memoria().
	*/
	p_glBlitFramebuffer(0, 0, ancho, alto,
						dx, dy, dx + dw, dy + dh,
						GL_COLOR_BUFFER_BIT, GL_LINEAR);

	p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	ligado = 0;
}
