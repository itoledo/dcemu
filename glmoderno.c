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
static GLuint	fbo_sec = 0;		/* el buffer de acumulacion secundario */
static int		acum_sec = 0;		/* se esta dibujando en el secundario? */
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
	if (fbo_sec)	{ glDeleteTextures(1, &fbo_sec); fbo_sec = 0; }
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
		El BUFFER DE ACUMULACION SECUNDARIO del TSP (bits 25 y 24), como segundo
		adjunto de color. Existe en el chip para tratar el resultado de
		superponer varios poligonos como si fuera uno solo: se acumula el grupo
		ahi y despues se compone de una vez sobre el primario.
	*/
	glGenTextures(1, &fbo_sec);
	glBindTexture(GL_TEXTURE_2D, fbo_sec);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ancho, alto, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1,
		GL_TEXTURE_2D, fbo_sec, 0);
	p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
		GL_RENDERBUFFER, fbo_prof);

	/* Se dibuja en el primario salvo que una tira pida lo contrario. Sin este
	   glDrawBuffer el adjunto 1 quedaria recibiendo copias de todo. */
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

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

/* ------------------------------------------------------------------------ */
/* El camino programable                                                    */
/* ------------------------------------------------------------------------ */

#define GL_FRAGMENT_SHADER		0x8B30
#define GL_VERTEX_SHADER		0x8B31
#define GL_COMPILE_STATUS		0x8B81
#define GL_LINK_STATUS			0x8B82
#define GL_INFO_LOG_LENGTH		0x8B84

typedef GLuint (APIENTRY * PFN_CREATE_SHADER)(GLenum);
typedef void (APIENTRY * PFN_SHADER_SOURCE)(GLuint, GLsizei, const char * const *, const GLint *);
typedef void (APIENTRY * PFN_COMPILE_SHADER)(GLuint);
typedef void (APIENTRY * PFN_GET_SHADER_IV)(GLuint, GLenum, GLint *);
typedef void (APIENTRY * PFN_GET_SHADER_LOG)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY * PFN_CREATE_PROGRAM)(void);
typedef void (APIENTRY * PFN_ATTACH_SHADER)(GLuint, GLuint);
typedef void (APIENTRY * PFN_LINK_PROGRAM)(GLuint);
typedef void (APIENTRY * PFN_GET_PROGRAM_IV)(GLuint, GLenum, GLint *);
typedef void (APIENTRY * PFN_GET_PROGRAM_LOG)(GLuint, GLsizei, GLsizei *, char *);
typedef void (APIENTRY * PFN_USE_PROGRAM)(GLuint);
typedef void (APIENTRY * PFN_DELETE_SHADER)(GLuint);
typedef GLint (APIENTRY * PFN_GET_UNIFORM_LOC)(GLuint, const char *);
typedef void (APIENTRY * PFN_UNIFORM_1I)(GLint, GLint);
typedef void (APIENTRY * PFN_UNIFORM_1F)(GLint, GLfloat);
typedef void (APIENTRY * PFN_UNIFORM_3F)(GLint, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY * PFN_UNIFORM_4F)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY * PFN_UNIFORM_2FV)(GLint, GLsizei, const GLfloat *);

static PFN_CREATE_SHADER	p_glCreateShader;
static PFN_SHADER_SOURCE	p_glShaderSource;
static PFN_COMPILE_SHADER	p_glCompileShader;
static PFN_GET_SHADER_IV	p_glGetShaderiv;
static PFN_GET_SHADER_LOG	p_glGetShaderInfoLog;
static PFN_CREATE_PROGRAM	p_glCreateProgram;
static PFN_ATTACH_SHADER	p_glAttachShader;
static PFN_LINK_PROGRAM		p_glLinkProgram;
static PFN_GET_PROGRAM_IV	p_glGetProgramiv;
static PFN_GET_PROGRAM_LOG	p_glGetProgramInfoLog;
static PFN_USE_PROGRAM		p_glUseProgram;
static PFN_DELETE_SHADER	p_glDeleteShader;
static PFN_GET_UNIFORM_LOC	p_glGetUniformLocation;
static PFN_UNIFORM_1I		p_glUniform1i;
static PFN_UNIFORM_1F		p_glUniform1f;
static PFN_UNIFORM_3F		p_glUniform3f;
static PFN_UNIFORM_4F		p_glUniform4f;
static PFN_UNIFORM_2FV		p_glUniform2fv;

/* Los mismos, pero sobre un programa que no esta ligado (GL 4.1). Es lo que
   permite mantener dos programas al dia sin un glUseProgram por uniforme. */
typedef void (APIENTRY * PFN_PU_1I)(GLuint, GLint, GLint);
typedef void (APIENTRY * PFN_PU_1F)(GLuint, GLint, GLfloat);
typedef void (APIENTRY * PFN_PU_3F)(GLuint, GLint, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY * PFN_PU_4F)(GLuint, GLint, GLfloat, GLfloat, GLfloat,
									GLfloat);
typedef void (APIENTRY * PFN_PU_2FV)(GLuint, GLint, GLsizei, const GLfloat *);

static PFN_PU_1I	p_glProgramUniform1i;
static PFN_PU_1F	p_glProgramUniform1f;
static PFN_PU_3F	p_glProgramUniform3f;
static PFN_PU_4F	p_glProgramUniform4f;
static PFN_PU_2FV	p_glProgramUniform2fv;

static GLuint	programa = 0;
static int		hay_shader = 0;
static int		shader_puesto = 0;

/*
	Los dos programas de escena, y por que son dos.

	`programa` es el de siempre. `programa_ez` es el MISMO fragment shader mas
	`layout(early_fragment_tests)`, y se liga solamente durante la tanda
	translucida con la lista encendida -- ver el comentario de fs_temprano.

	Que sean dos reabre justo el problema que un solo programa evitaba: los
	uniformes los pone la sombra de estado de graficos.c, y si se escribe uno
	solo el otro miente. Se cierra escribiendo **los dos en cada setter**, con
	glProgramUniform*, que escribe sin ligar; si el driver no la da, no hay
	transparencia ordenada y queda el programa unico de antes. Cuesta una
	llamada de mas por cambio de estado y ninguna decision en el camino de
	dibujo.
*/
static GLuint	programa_ez = 0;
static int		oit_puesto = 0;

typedef struct {
	GLint	muestra, textura, env, offset, alpha, umbral;
	GLint	niebla, nie_color, nie_dens, nie_tabla;
	GLint	bump, bump_param;
	GLint	oit, oit_max, oit_mezcla;
	GLint	volumen, vol_mascara;
	GLint	acum_src, acum_muestra;
} locs_t;

static locs_t	u_n;	/* las del programa normal */
static locs_t	u_z;	/* las del de prueba adelantada */

/* ---- Transparencia ordenada por pixel ---- */

#define GL_R32UI					0x8236
#define GL_RED_INTEGER				0x8D94
#define GL_WRITE_ONLY				0x88B9
#define GL_READ_WRITE				0x88BA
#define GL_SHADER_STORAGE_BUFFER	0x90D2
#define GL_ATOMIC_COUNTER_BUFFER	0x92C0
#define GL_DYNAMIC_DRAW				0x88E8
#define GL_SHADER_STORAGE_BARRIER_BIT		0x00002000
#define GL_TEXTURE_FETCH_BARRIER_BIT		0x00000008
#define GL_ATOMIC_COUNTER_BARRIER_BIT		0x00001000
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT	0x00000020

typedef void (APIENTRY * PFN_GEN_BUF)(GLsizei, GLuint *);
typedef void (APIENTRY * PFN_DEL_BUF)(GLsizei, const GLuint *);
typedef void (APIENTRY * PFN_BIND_BUF)(GLenum, GLuint);
typedef void (APIENTRY * PFN_BUF_DATA)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (APIENTRY * PFN_BUF_SUBDATA)(GLenum, GLintptr, GLsizeiptr, const void *);
typedef void (APIENTRY * PFN_BIND_BUF_BASE)(GLenum, GLuint, GLuint);
typedef void (APIENTRY * PFN_BIND_IMG_TEX)(GLuint, GLuint, GLint, GLboolean,
										   GLint, GLenum, GLenum);
typedef void (APIENTRY * PFN_MEM_BARRIER)(GLbitfield);
typedef void (APIENTRY * PFN_CLEAR_TEX_IMG)(GLuint, GLint, GLenum, GLenum,
											const void *);
typedef void (APIENTRY * PFN_ACTIVE_TEX)(GLenum);

static PFN_GEN_BUF			p_glGenBuffers;
static PFN_DEL_BUF			p_glDeleteBuffers;
static PFN_BIND_BUF			p_glBindBuffer;
static PFN_BUF_DATA			p_glBufferData;
static PFN_BUF_SUBDATA		p_glBufferSubData;
static PFN_BIND_BUF_BASE	p_glBindBufferBase;
static PFN_BIND_IMG_TEX		p_glBindImageTexture;
static PFN_MEM_BARRIER		p_glMemoryBarrier;
static PFN_CLEAR_TEX_IMG	p_glClearTexImage;
static PFN_ACTIVE_TEX		p_glActiveTexture;

static int		hay_oit = 0;
static GLuint	oit_prog = 0;
static GLuint	oit_cabezas = 0;	/* r32ui, una cabeza de lista por pixel */
static GLuint	oit_nodos = 0;		/* SSBO con los fragmentos apilados */
static GLuint	oit_contador = 0;	/* contador atomico de reserva */
static GLuint	oit_fondo = 0;		/* lo que dejo la tanda opaca */
static int		oit_w = 0, oit_h = 0;
static unsigned	oit_max = 0;
static GLint	u_res_fondo = -1;
static GLint	u_res_presort = -1;

/* Capas por pixel que se reservan, y el techo absoluto de la reserva. Con
   escala 1 son 640x480x8 = 2,4 M nodos, o sea 39 MB; el techo existe porque a
   escala 4 la cuenta se iria a 630 MB. Lo que pase del techo se descarta y se
   avisa una vez. */
#define OIT_CAPAS		8
#define OIT_NODOS_TOPE	8000000u

static int oit_armar(void);

/* ---- Volumenes modificadores por pixel ---- */

#define GL_R32I				0x8235
#define GL_RED_INTEGER_		GL_RED_INTEGER

static int		hay_vol = 0;
static GLuint	vol_mascara = 0;	/* lo que lee el shader de escena: !=0 dentro */
static GLuint	vol_cuenta = 0;		/* contador de un grupo, solo si hay exclusion */
static GLuint	vol_prog = 0;		/* acumula caras en la mascara */
static GLuint	vol_prog_plegar = 0;/* dobla un grupo en la mascara */
static int		vol_w = 0, vol_h = 0;
static GLint	u_vol_excluir = -1;

static int vol_armar(void);

/*
	El vertex shader. No hace nada que la funcion fija no hiciera: transforma
	por la matriz y pasa color, color secundario y coordenadas de textura.

	Las coordenadas van de cuatro componentes (u*q, v*q, 0, q) y el fragmento
	las divide con texture2DProj, que es lo mismo que hace glTexCoordPointer(4)
	-- ver el comentario de `vertex` en render.h: para eso el TA entrega 1/w.
*/
/*
	El juego 1 --el de dentro del volumen modificador-- viaja en las unidades de
	textura 1, 2 y 3, y no es un abuso: son cuatro flotantes por unidad y lo que
	hace falta pasar es una UV, un color y un color de offset. La alternativa
	--atributos genericos-- obligaria a VBO y VAO, que es justo lo que el camino
	de arreglos de cliente evita. graficos.c los enciende solo para las tiras que
	un volumen afecta; para el resto las tres quedan apagadas.
*/
static const char * fuente_vs =
	"#version 120\n"
	"void main()\n"
	"{\n"
	"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"	gl_FrontColor = gl_Color;\n"
	"	gl_FrontSecondaryColor = gl_SecondaryColor;\n"
	"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
	"	gl_TexCoord[1] = gl_MultiTexCoord1;\n"	/* UV del juego 1 */
	"	gl_TexCoord[2] = gl_MultiTexCoord2;\n"	/* color del juego 1 */
	"	gl_TexCoord[3] = gl_MultiTexCoord3;\n"	/* offset del juego 1 */
	"}\n";

/*
	El fragment shader: los cuatro modos de la instruccion de textura/sombreado
	del PVR, el color de offset y el descarte por alfa.

	Los cuatro modos son la tabla del DevBox (pagina 210) escrita tal cual, que
	es la ventaja de tenerlos aca en vez de en GL_COMBINE -- **el alfa de salida
	es una regla distinta en cada uno**, y expresarlo con el entorno de textura
	costaba hasta nueve glTexEnvi y dos de los cuatro modos no se podian decir
	sin COMBINE:

	  0 decal          PIXRGB = TEX                      PIXA = TEXA
	  1 modulate       PIXRGB = COL*TEX                  PIXA = TEXA
	  2 decal alpha    PIXRGB = TEX*TEXA + COL*(1-TEXA)  PIXA = COLA
	  3 modulate alpha PIXRGB = COL*TEX                  PIXA = COLA*TEXA

	El descarte reproduce la regla exacta del chip, que son DOS condiciones y
	no una: "alfa >= umbral **y** distinto de cero". En funcion fija habia que
	fingirla con GEQUAL y un piso de medio paso, porque glAlphaFunc no sabe
	decir dos cosas; aca se escribe la que es. Las dos medidas que la fijaron
	--las pastillas del menu de Crazy Taxi y su mundo blanco-- estan en el
	comentario de tira_estado().
*/
/*
	La niebla, y por que aca es mejor que donde estaba.

	El chip aplica niebla **por pixel y antes de la mezcla**: es parte del
	camino del pixel, delante de la unidad de blend. La funcion fija no puede
	hacer ninguna de las dos cosas, asi que dibuja la tira una segunda vez
	entera, con el color de niebla y el alfa evaluado por vertice, mezclada
	encima. Eso difiere del chip en dos sentidos --dentro de triangulos grandes,
	porque interpola el alfa en vez de evaluarlo, y en la geometria translucida,
	porque llega despues de la mezcla en vez de antes-- y ademas cuesta una
	pasada de geometria por tira con niebla.

	El indice de la tabla es densidad x (1/w) acotado a [1, 256): el exponente
	elige la ranura de 16 y la mantisa la entrada dentro de ella, con el alfa
	lejano en el byte alto y el cercano en el bajo, interpolados por la fraccion.
	Es lo mismo que evalua dibujar_niebla_tira(), solo que aca `q` sale
	interpolado por el rasterizador: viaja en gl_TexCoord[0].w, que es el 1/w
	que el TA entrega para la correccion de perspectiva. O sea que la niebla por
	pixel no cuesta ni un dato mas.
*/
/*
	El cuerpo del fragment shader, sin cabecera ni main.

	Se parte en tres piezas --cabecera, cuerpo, main-- porque el programa se
	compila en dos sabores: uno de GLSL 1.20 que escribe gl_FragColor, y uno de
	4.30 que ademas puede APILAR el fragmento en una lista por pixel en vez de
	mezclarlo (la transparencia ordenada). El cuerpo es el mismo en los dos, y
	tiene que serlo: si divergiera, la comparacion entre caminos dejaria de
	significar algo. glShaderSource toma un arreglo de cadenas, asi que no hace
	falta pegarlas.
*/
static const char * fuente_fs_cuerpo =
	"uniform sampler2D muestra;\n"
	"uniform int usa_textura;\n"
	"uniform int modo_env;\n"
	"uniform int usa_offset;\n"
	"uniform int usa_alpha;\n"
	"uniform float umbral;\n"
	"uniform int usa_niebla;\n"
	"uniform vec3 niebla_color;\n"
	"uniform float niebla_densidad;\n"
	"uniform vec2 niebla_tabla[128];\n"
	"uniform int usa_bump;\n"
	"uniform vec4 bump_param;\n"		/* K1, K2, K3, Q ya en radianes */
	/*
		El buffer de acumulacion secundario como ORIGEN de la mezcla (bit 25 del
		TSP). Con el puesto, la tira no aporta el color que calculo: aporta lo
		que el secundario tiene en ese pixel. Es la mitad que compone de una vez
		el grupo acumulado, y por eso el secundario tiene que ser una textura y
		no un renderbuffer.
	*/
	"uniform int acum_src;\n"
	"uniform sampler2D acum_muestra;\n"
	"\n"
	"float niebla_alfa(float q)\n"
	"{\n"
	"	float v = clamp(niebla_densidad * q, 1.0, 255.9999);\n"
	"	float e = floor(log2(v));\n"
	"	float m16 = (v / exp2(e) - 1.0) * 16.0;\n"
	"	float m = floor(m16);\n"
	"	int idx = int(e) * 16 + int(m);\n"
	"\n"
	"	if (idx < 0) idx = 0;\n"
	"	if (idx > 127) idx = 127;\n"
	"\n"
	"	/* x es el alfa lejano y y el cercano, como en la palabra del chip. */\n"
	"	return mix(niebla_tabla[idx].x, niebla_tabla[idx].y, m16 - m);\n"
	"}\n"
	"\n"
	/*
		Los tres datos que un volumen modificador cambia --color, UV y color de
		offset-- entran por parametro y no se leen de las incorporadas.

		Es lo que permite que el juego de parametros se elija POR PIXEL: el chip
		decide dentro/fuera pixel a pixel y con eso escoge uno de los dos juegos
		que trae el vertice. El cuerpo sigue siendo el mismo para los dos sabores
		del shader, que es la condicion para poder compararlos.
	*/
	"vec4 dc_pixel(vec4 col, vec4 uv, vec3 off)\n"
	"{\n"
	"	vec4 pix;\n"
	"\n"
	/* El origen secundario reemplaza todo el camino del pixel: ni textura, ni
	   offset, ni entorno. Lo que se mezcla es el acumulado tal cual. */
	"	if (acum_src != 0)\n"
	"		return texelFetch(acum_muestra, ivec2(gl_FragCoord.xy), 0);\n"
	"\n"
	"	if (usa_textura != 0)\n"
	"	{\n"
	"		vec4 tex = texture2DProj(muestra, uv);\n"
	"\n"
	/*
		El mapa de relieve: los dos angulos vienen crudos en R y G, y la
		intensidad es la formula del chip evaluada por pixel.
		S recorre 0..pi/2, R y Q recorren 0..2pi, K1..K3 son 0..1.
	*/
	"		if (usa_bump != 0)\n"
	"		{\n"
	"			float S = tex.r * 1.5707963;\n"
	"			float R = tex.g * 6.2831853;\n"
	"			float I = bump_param.x + bump_param.y * sin(S)\n"
	"				+ bump_param.z * cos(S) * cos(R - bump_param.w);\n"
	"\n"
	"			tex = vec4(clamp(I, 0.0, 1.0), clamp(I, 0.0, 1.0),\n"
	"				clamp(I, 0.0, 1.0), 1.0);\n"
	"		}\n"
	"\n"
	"		if (modo_env == 0)\n"
	"			pix = tex;\n"
	"		else if (modo_env == 1)\n"
	"			pix = vec4(col.rgb * tex.rgb, tex.a);\n"
	"		else if (modo_env == 2)\n"
	"			pix = vec4(mix(col.rgb, tex.rgb, tex.a), col.a);\n"
	"		else\n"
	"			pix = vec4(col.rgb * tex.rgb, col.a * tex.a);\n"
	"	}\n"
	"	else\n"
	"		pix = col;\n"
	"\n"
	"	if (usa_offset != 0)\n"
	"		pix.rgb += off;\n"
	"\n"
	"	if (usa_alpha != 0 && (pix.a < umbral || pix.a == 0.0))\n"
	"		discard;\n"
	"\n"
	/* Antes de salir, o sea antes de la mezcla: es donde la aplica el chip. */
	"	if (usa_niebla != 0)\n"
	"		pix.rgb = mix(pix.rgb, niebla_color, niebla_alfa(uv.w));\n"
	"\n"
	"	return pix;\n"
	"}\n";

/* ---- Las dos cabeceras y los dos main ---- */

static const char * fs_cabeza_120 = "#version 120\n";

/*
	El sabor con transparencia ordenada por pixel.

	`compatibility` y no `core`: el cuerpo usa gl_Color, gl_SecondaryColor y
	gl_TexCoord, que es lo que permite que los arreglos de cliente del arbol
	sigan alimentando al shader sin VBO ni VAO. Un contexto de compatibilidad
	de 4.6 admite las dos cosas a la vez, que es justamente el hallazgo que hizo
	viable toda esta via.
*/
static const char * fs_cabeza_oit =
	"#version 430 compatibility\n"
	"layout(r32ui) uniform coherent uimage2D oit_cabezas;\n"
	"layout(binding = 0, offset = 0) uniform atomic_uint oit_contador;\n"
	"struct NodoOIT { uint siguiente; uint color; float prof; uint mezcla; };\n"
	"layout(std430, binding = 0) buffer NodosOIT { NodoOIT oit_nodos[]; };\n"
	"uniform int usa_oit;\n"
	/*
		**int y no uint**, aunque conceptualmente sean enteros sin signo.

		glUniform1i sobre un uniforme declarado `uint` es GL_INVALID_OPERATION:
		la llamada no hace nada y el uniforme se queda en cero, en silencio. Con
		oit_max en cero la condicion `idx < oit_max` es siempre falsa y **no se
		apila un solo fragmento**, o sea que la lista sale vacia y la pantalla
		negra sin un solo error a la vista. Costo la primera corrida de esto.
	*/
	"uniform int oit_max;\n"
	"uniform int oit_mezcla;\n"
	/*
		La mascara del volumen modificador: distinto de cero es "este pixel esta
		dentro". La escribe la pasada de volumenes contando caras contra la
		profundidad ya resuelta, igual que la plantilla, solo que aca el shader
		la puede LEER -- que es lo que permite elegir el juego de parametros por
		pixel en vez de dibujar la geometria dos veces con la plantilla de reja.
	*/
	/*
		`binding` explicito y no un uniforme con el numero de unidad. Los
		uniformes de imagen son de los que un driver puede no aceptar por
		glProgramUniform*, y el sintoma es el de siempre: la unidad se queda en
		0 --donde vive otra imagen-- imageLoad devuelve cero, la mascara se lee
		vacia y no hay ni un error. Con el binding en el fuente no hay nada que
		poner ni nada que se pueda quedar sin poner.
	*/
	"layout(binding = 1, r32i) uniform coherent iimage2D vol_mascara;\n"
	"uniform int usa_volumen;\n"
	/* DCEMU_VOL_SONDA=1: la tira que consulta la mascara sale roja donde dio
	   dentro y verde donde dio fuera, en vez de dibujarse. Separa "la mascara
	   esta vacia" de "el juego 1 es igual al 0", que dan el mismo sintoma. */
	"uniform int vol_sonda;\n";

/*
	**La prueba de profundidad tiene que correr ANTES del shader**, y esto es lo
	unico que lo consigue.

	Un shader que contiene `discard` obliga al driver a hacer la prueba de
	profundidad tarde -- despues de ejecutarlo, porque hasta no ejecutarlo no se
	sabe si el fragmento existe. Con la lista encendida el shader apila el
	fragmento y *despues* descarta, asi que **el apilado ocurre igual para los
	fragmentos que la profundidad iba a rechazar**: geometria translucida tapada
	por geometria opaca entra en la lista y la resolucion la mezcla encima de lo
	que la tapaba.

	No es un detalle de una demo: es toda la tanda translucida de cualquier
	escena con paredes. Lo destapo pvr-fb_tex, donde el cubo opaco quedaba
	borrado por los dos cuadrilateros de pantalla completa que estan detras de
	el -- y como esa demo se realimenta del framebuffer, el cubo borrado se
	llevaba puesto el rastro del cuadro siguiente y la pantalla entera terminaba
	en negro.

	Y no puede ir en el programa compartido: con la prueba adelantada la
	profundidad se ESCRIBE antes del shader, asi que un fragmento de
	punch-through que se descarta por alfa dejaria su z escrita igual. Por eso
	el apilado va en un programa propio, identico salvo esta linea, que solo se
	liga durante la tanda translucida -- donde la escritura de z esta apagada y
	la contradiccion no existe.
*/
static const char * fs_temprano = "layout(early_fragment_tests) in;\n";

static const char * fs_main_120 =
	"void main()\n"
	"{\n"
	"	gl_FragColor = dc_pixel(gl_Color, gl_TexCoord[0],\n"
	"		gl_SecondaryColor.rgb);\n"
	"}\n";

/*
	Con la lista encendida el fragmento no se mezcla: se apila y se descarta.

	El nodo guarda el color ya resuelto, la profundidad de ventana y los dos
	codigos de mezcla de la tira -- **los codigos y no los enum de GL**, porque
	la mezcla la va a hacer el shader de resolucion y no glBlendFunc. La cabeza
	de la lista se cambia con imageAtomicExchange, que es lo que hace que el
	orden de llegada no importe.

	Si la reserva se agota el fragmento se pierde. Es lo que hacen todas las
	implementaciones de esto y no hay alternativa barata; lo que si hay es un
	aviso, porque perder capas en silencio seria la falla de siempre.
*/
static const char * fs_main_oit =
	"void main()\n"
	"{\n"
	/*
		El volumen modificador, por pixel y como en el chip: si este pixel esta
		dentro, el poligono se dibuja con su juego 1 --el otro color, la otra UV
		y el otro offset, que el vertice ya trae-- y si no, con el 0. Una sola
		pasada de geometria; la version de plantilla necesitaba dos.
	*/
	"	vec4 col = gl_Color;\n"
	"	vec4 uv  = gl_TexCoord[0];\n"
	"	vec3 off = gl_SecondaryColor.rgb;\n"
	"\n"
	"	bool dentro = usa_volumen != 0\n"
	"		&& imageLoad(vol_mascara, ivec2(gl_FragCoord.xy)).r != 0;\n"
	"\n"
	"	if (dentro)\n"
	"	{\n"
	"		col = gl_TexCoord[2];\n"
	"		uv  = gl_TexCoord[1];\n"
	"		off = gl_TexCoord[3].rgb;\n"
	"	}\n"
	"\n"
	"	if (vol_sonda != 0 && usa_volumen != 0)\n"
	"	{\n"
	"		gl_FragColor = dentro ? vec4(1.0, 0.0, 0.0, 1.0)\n"
	"							  : vec4(0.0, 1.0, 0.0, 1.0);\n"
	"		return;\n"
	"	}\n"
	"\n"
	"	vec4 c = dc_pixel(col, uv, off);\n"
	"\n"
	"	if (usa_oit == 0)\n"
	"	{\n"
	"		gl_FragColor = c;\n"
	"		return;\n"
	"	}\n"
	"\n"
	"	uint idx = atomicCounterIncrement(oit_contador);\n"
	"\n"
	"	if (idx < uint(oit_max))\n"
	"	{\n"
	"		uint prev = imageAtomicExchange(oit_cabezas,\n"
	"			ivec2(gl_FragCoord.xy), idx);\n"
	"\n"
	"		oit_nodos[idx].siguiente = prev;\n"
	"		oit_nodos[idx].color = packUnorm4x8(c);\n"
	"		oit_nodos[idx].prof = gl_FragCoord.z;\n"
	"		oit_nodos[idx].mezcla = uint(oit_mezcla);\n"
	"	}\n"
	"\n"
	"	discard;\n"
	"}\n";

/* ---- Los dos programas de la mascara de volumen ---- */

/*
	Acumular: una cara que mira hacia aca suma y una que mira hacia alla resta,
	contra la profundidad ya resuelta. Dentro del volumen la cuenta no cierra.

	`gl_FrontFacing` en vez de dos pasadas con culling opuesto, que es lo que
	hace la version de plantilla: el sentido se decide en coordenadas de ventana
	igual que el culling, asi que el resultado es el mismo con la mitad de
	geometria enviada.

	`early_fragment_tests` por lo mismo que la transparencia ordenada: el
	`discard` atrasaria la prueba de profundidad y se contarian caras que estan
	DETRAS de la superficie, que es justamente lo que la cuenta no debe ver. Sin
	esto el volumen marca todo lo que sus caras cubran -- el error que la version
	anterior a la plantilla por profundidad ya habia cometido.
*/
static const char * fuente_fs_vol =
	"#version 430 compatibility\n"
	"layout(early_fragment_tests) in;\n"
	"layout(binding = 2, r32i) uniform coherent iimage2D cuenta;\n"
	"void main()\n"
	"{\n"
	"	imageAtomicAdd(cuenta, ivec2(gl_FragCoord.xy),\n"
	"		gl_FrontFacing ? 1 : -1);\n"
	"	discard;\n"
	"}\n";

/*
	Doblar un grupo en la mascara: `dentro` es cuenta != 0, o su complemento si
	el grupo cerro con la instruccion 2 ("cerrar excluyendo"), donde la region
	afectada es el COMPLEMENTO del volumen y no lo que sus caras cubren.

	Sin exclusion los grupos se suman en una sola cuenta y basta un plegado al
	final, que es lo mismo que hacia la plantilla probando != 0.
*/
static const char * fuente_fs_vol_plegar =
	"#version 430 compatibility\n"
	"layout(binding = 1, r32i) uniform coherent iimage2D destino;\n"
	"layout(binding = 2, r32i) uniform coherent iimage2D cuenta;\n"
	"uniform int excluir;\n"
	"void main()\n"
	"{\n"
	"	ivec2 p = ivec2(gl_FragCoord.xy);\n"
	"	bool dentro = imageLoad(cuenta, p).r != 0;\n"
	"\n"
	"	if (excluir != 0) dentro = !dentro;\n"
	"	if (dentro) imageStore(destino, p, ivec4(1));\n"
	"\n"
	"	discard;\n"
	"}\n";

/* ---- El shader de resolucion ---- */

static const char * fuente_vs_resolver =
	"#version 430 compatibility\n"
	"void main()\n"
	"{\n"
	"	gl_Position = gl_Vertex;\n"
	"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
	"}\n";

/*
	Recorre la lista de cada pixel, la ordena de lejos a cerca y la mezcla
	sobre lo que dejo la tanda opaca.

	**El orden**: la z del TA es 1/w --mas grande es mas cerca-- y el glOrtho de
	screeninit() lleva near y far invertidos justamente para que la profundidad
	de ventana crezca con ella. O sea que de lejos a cerca es prof ASCENDENTE.

	**La mezcla** son los ocho factores del TSP resueltos aca uno por uno, con
	el destino acumulado en vez del framebuffer. Eso reproduce exactamente lo
	que glBlendFunc hacia, solo que en el orden correcto por pixel en vez de por
	tira. Los codigos 2 y 3 son "el otro color", que del lado del origen es el
	destino y del lado del destino es el origen -- las dos tablas que ya tiene
	graficos.c, y confundirlas es lo que dejaba las sombras de Virtua Tenis 2
	como trapecios opacos.

	El tope de capas por pixel es fijo: lo que pase de ahi se descarta. Una
	escena con mas capas translucidas superpuestas que eso en el mismo pixel es
	rarisima, y la alternativa --ordenar una lista de largo arbitrario en el
	fragment shader-- no cabe en registros.
*/
static const char * fuente_fs_resolver =
	"#version 430 compatibility\n"
	"layout(r32ui) uniform coherent uimage2D oit_cabezas;\n"
	"struct NodoOIT { uint siguiente; uint color; float prof; uint mezcla; };\n"
	"layout(std430, binding = 0) buffer NodosOIT { NodoOIT oit_nodos[]; };\n"
	"uniform sampler2D fondo;\n"
	/*
		El autosort de la lista translucida. Con ISP_FEED_CFG en pre-sort el
		chip respeta el orden de envio y no ordena por profundidad; ordenar
		igual seria pasarle por encima a una decision del guest, que es lo que
		compare() ya evita en el camino por tira.
	*/
	"uniform int presort;\n"
	"\n"
	"#define CAPAS 32\n"
	"\n"
	"vec3 factor(uint cod, vec3 propio, float propio_a, vec3 otro, float otro_a)\n"
	"{\n"
	"	if (cod == 0u) return vec3(0.0);\n"
	"	if (cod == 1u) return vec3(1.0);\n"
	"	if (cod == 2u) return otro;\n"
	"	if (cod == 3u) return vec3(1.0) - otro;\n"
	"	if (cod == 4u) return vec3(propio_a);\n"
	"	if (cod == 5u) return vec3(1.0 - propio_a);\n"
	"	if (cod == 6u) return vec3(otro_a);\n"
	"	return vec3(1.0 - otro_a);\n"
	"}\n"
	"\n"
	/*
		El factor del ALFA no es el rojo del factor del color.

		Para los codigos escalares --Zero, One, y los cuatro de alfa-- da lo
		mismo, pero los codigos 2 y 3 son "el otro color": su factor de color es
		un vector RGB y su factor de alfa es el ALFA del otro, no su
		componente roja. Es la misma regla que aplica GL con GL_DST_COLOR, y
		tomar el rojo daba un alfa arbitrario en las escenas que mezclan por
		color -- que son justo las que usan el alfa del destino despues.
	*/
	"float factor_a(uint cod, float propio_a, float otro_a)\n"
	"{\n"
	"	if (cod == 0u) return 0.0;\n"
	"	if (cod == 1u) return 1.0;\n"
	"	if (cod == 2u) return otro_a;\n"
	"	if (cod == 3u) return 1.0 - otro_a;\n"
	"	if (cod == 4u) return propio_a;\n"
	"	if (cod == 5u) return 1.0 - propio_a;\n"
	"	if (cod == 6u) return otro_a;\n"
	"	return 1.0 - otro_a;\n"
	"}\n"
	"\n"
	"uniform int solo_fondo;\n"		/* DCEMU_OIT_SOLO_FONDO: ver glmoderno.c */
	"\n"
	"void main()\n"
	"{\n"
	/*
		Las sondas que miran el fondo salen antes de recorrer la lista; las que
		miran la lista tienen que salir DESPUES de armarla, mas abajo.

		Tenerlas todas aca arriba bajo un `!= 0` fue una falla propia: la sonda
		de capas devolvia el fondo y se leyo como "no se apila nada", que era
		justo la conclusion que venia a comprobar.
	*/
	"	if (solo_fondo == 1)\n"
	"	{\n"
	"		gl_FragColor = vec4(texelFetch(fondo, ivec2(gl_FragCoord.xy), 0).rgb,\n"
	"						    1.0);\n"
	"		return;\n"
	"	}\n"
	"\n"
	/* El ALFA del fondo, que es lo que la mezcla por DST_ALPHA consume y lo
	   unico que una captura RGB no puede mostrar. */
	"	if (solo_fondo == 3)\n"
	"	{\n"
	"		gl_FragColor = vec4(vec3(texelFetch(fondo,\n"
	"						    ivec2(gl_FragCoord.xy), 0).a), 1.0);\n"
	"		return;\n"
	"	}\n"
	"\n"
	"	uint idx = imageLoad(oit_cabezas, ivec2(gl_FragCoord.xy)).r;\n"
	"	uint lista[CAPAS];\n"
	"	int n = 0;\n"
	"	int i, j;\n"
	"\n"
	"	while (idx != 0xFFFFFFFFu && n < CAPAS)\n"
	"	{\n"
	"		lista[n] = idx;\n"
	"		n++;\n"
	"		idx = oit_nodos[idx].siguiente;\n"
	"	}\n"
	"\n"
	/*
		**La lista se recorre del mas nuevo al mas viejo**, porque cada
		fragmento se apila en la cabeza. Hay que darla vuelta antes de
		ordenar, y no es cosmetica: el ordenamiento es estable, asi que dos
		fragmentos con la MISMA profundidad se mezclan en el orden en que
		queden. Sin dar vuelta, ese orden es el inverso del de envio -- y el
		desempate por orden de envio es exactamente lo que hace compare() en
		graficos.c, por la misma razon (el PVR dibuja dentro de una lista en
		el orden en que el guest la entrego).

		pvr-fb_tex es lo que lo destapo: sus cuatro cuadriculas de pantalla
		completa estan a la misma profundidad y una de ellas mezcla con
		destino ZERO, o sea que borra lo anterior. Al reves, borraba lo que
		tenia que quedar y la pantalla salia negra.
	*/
	"	for (i = 0; i < n / 2; i++)\n"
	"	{\n"
	"		uint t = lista[i];\n"
	"		lista[i] = lista[n - 1 - i];\n"
	"		lista[n - 1 - i] = t;\n"
	"	}\n"
	"\n"
	"	vec4 dst = texelFetch(fondo, ivec2(gl_FragCoord.xy), 0);\n"
	"\n"
	/* solo_fondo=2: cuantas capas encontro, en gris. Separa "la lista esta
	   vacia" de "la mezcla da negro", que dan el mismo sintoma. */
	"	if (solo_fondo == 2)\n"
	"	{\n"
	"		gl_FragColor = vec4(vec3(float(n) / 8.0), 1.0);\n"
	"		return;\n"
	"	}\n"
	"\n"
	/* solo_fondo=4: el color del fragmento MAS CERCANO de la lista, sin
	   mezclar. Contesta "el color apilado es el que corresponde" aparte de
	   "la mezcla lo usa bien". */
	"	if (solo_fondo == 4)\n"
	"	{\n"
	"		if (n == 0) { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
	"		gl_FragColor = vec4(unpackUnorm4x8(\n"
	"					oit_nodos[lista[n - 1]].color).rgb, 1.0);\n"
	"		return;\n"
	"	}\n"
	"\n"
	"	if (n == 0)\n"
	"	{\n"
	"		gl_FragColor = dst;\n"
	"		return;\n"
	"	}\n"
	"\n"
	/* Ordenamiento por insercion: n es chico y esto no ramifica de mas. */
	"	if (presort == 0)\n"
	"	for (i = 1; i < n; i++)\n"
	"	{\n"
	"		uint v = lista[i];\n"
	"		float p = oit_nodos[v].prof;\n"
	"\n"
	"		for (j = i - 1; j >= 0 && oit_nodos[lista[j]].prof > p; j--)\n"
	"			lista[j + 1] = lista[j];\n"
	"\n"
	"		lista[j + 1] = v;\n"
	"	}\n"
	"\n"
	"	for (i = 0; i < n; i++)\n"
	"	{\n"
	"		vec4 src = unpackUnorm4x8(oit_nodos[lista[i]].color);\n"
	"		uint m = oit_nodos[lista[i]].mezcla;\n"
	"		vec3 fs = factor((m >> 4) & 7u, src.rgb, src.a, dst.rgb, dst.a);\n"
	"		vec3 fd = factor(m & 7u, dst.rgb, dst.a, src.rgb, src.a);\n"
	"\n"
	"		float as = factor_a((m >> 4) & 7u, src.a, dst.a);\n"
	"		float ad = factor_a(m & 7u, dst.a, src.a);\n"
	"\n"
	"		dst = vec4(src.rgb * fs + dst.rgb * fd, src.a * as + dst.a * ad);\n"
	"	}\n"
	"\n"
	"	gl_FragColor = dst;\n"
	"}\n";

/* Compila y reporta. El log se imprime siempre que exista: un shader que
   compila con avisos es lo que despues no dibuja igual. */
static GLuint compilar_partes(GLenum tipo, const char ** partes, int n,
							  const char * nombre)
{
	GLuint	s = p_glCreateShader(tipo);
	GLint	ok = 0, largo = 0;

	p_glShaderSource(s, (GLsizei) n, partes, NULL);
	p_glCompileShader(s);
	p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	p_glGetShaderiv(s, GL_INFO_LOG_LENGTH, &largo);

	if (largo > 1)
	{
		char log[2048];

		p_glGetShaderInfoLog(s, (GLsizei) sizeof(log), NULL, log);
		fprintf(stderr, "gl: %s: %s\n", nombre, log);
	}

	if (!ok)
	{
		p_glDeleteShader(s);
		return 0;
	}

	return s;
}

static GLuint compilar(GLenum tipo, const char * fuente, const char * nombre)
{
	return compilar_partes(tipo, &fuente, 1, nombre);
}

/* Enlaza un par ya compilado y reporta el log. Devuelve 0 si fallo. */
static GLuint enlazar(GLuint vs, GLuint fs, const char * nombre)
{
	GLuint	p;
	GLint	ok = 0, largo = 0;

	if (vs == 0 || fs == 0)
		return 0;

	p = p_glCreateProgram();
	p_glAttachShader(p, vs);
	p_glAttachShader(p, fs);
	p_glLinkProgram(p);

	p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
	p_glGetProgramiv(p, GL_INFO_LOG_LENGTH, &largo);

	if (largo > 1)
	{
		char log[2048];

		p_glGetProgramInfoLog(p, (GLsizei) sizeof(log), NULL, log);
		fprintf(stderr, "gl: enlace de %s: %s\n", nombre, log);
	}

	return ok ? p : 0;
}

static void ubicar(GLuint p, locs_t * l)
{
	if (p == 0)
	{
		memset(l, 0xFF, sizeof(*l));	/* todas en -1: no existen */
		return;
	}

	l->muestra	= p_glGetUniformLocation(p, "muestra");
	l->textura	= p_glGetUniformLocation(p, "usa_textura");
	l->env		= p_glGetUniformLocation(p, "modo_env");
	l->offset	= p_glGetUniformLocation(p, "usa_offset");
	l->alpha	= p_glGetUniformLocation(p, "usa_alpha");
	l->umbral	= p_glGetUniformLocation(p, "umbral");
	l->niebla	= p_glGetUniformLocation(p, "usa_niebla");
	l->nie_color = p_glGetUniformLocation(p, "niebla_color");
	l->nie_dens	= p_glGetUniformLocation(p, "niebla_densidad");
	l->nie_tabla = p_glGetUniformLocation(p, "niebla_tabla");
	l->bump		= p_glGetUniformLocation(p, "usa_bump");
	l->bump_param = p_glGetUniformLocation(p, "bump_param");
	l->oit		= p_glGetUniformLocation(p, "usa_oit");
	l->oit_max	= p_glGetUniformLocation(p, "oit_max");
	l->oit_mezcla = p_glGetUniformLocation(p, "oit_mezcla");
	l->volumen	= p_glGetUniformLocation(p, "usa_volumen");
	l->vol_mascara = p_glGetUniformLocation(p, "vol_mascara");
	l->acum_src	= p_glGetUniformLocation(p, "acum_src");
	l->acum_muestra = p_glGetUniformLocation(p, "acum_muestra");
}

/*
	Escribir un uniforme en los dos programas de escena.

	Si el driver da glProgramUniform* --GL 4.1, y la transparencia ordenada ya
	pide 4.3-- se escribe sin ligar y los dos quedan al dia siempre. Si no la
	da no hay segundo programa, y queda la forma de antes: sobre el que este
	puesto, que es lo que garantiza el camino de dibujo.
*/
static void pu_1i(GLint la, GLint lb, GLint v)
{
	if (p_glProgramUniform1i != NULL)
	{
		if (la >= 0) p_glProgramUniform1i(programa, la, v);
		if (lb >= 0 && programa_ez) p_glProgramUniform1i(programa_ez, lb, v);
	}
	else if (la >= 0)
		p_glUniform1i(la, v);
}

static void pu_1f(GLint la, GLint lb, GLfloat v)
{
	if (p_glProgramUniform1f != NULL)
	{
		if (la >= 0) p_glProgramUniform1f(programa, la, v);
		if (lb >= 0 && programa_ez) p_glProgramUniform1f(programa_ez, lb, v);
	}
	else if (la >= 0)
		p_glUniform1f(la, v);
}

static void pu_3f(GLint la, GLint lb, GLfloat x, GLfloat y, GLfloat z)
{
	if (p_glProgramUniform3f != NULL)
	{
		if (la >= 0) p_glProgramUniform3f(programa, la, x, y, z);
		if (lb >= 0 && programa_ez) p_glProgramUniform3f(programa_ez, lb, x, y, z);
	}
	else if (la >= 0)
		p_glUniform3f(la, x, y, z);
}

static void pu_4f(GLint la, GLint lb, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	if (p_glProgramUniform4f != NULL)
	{
		if (la >= 0) p_glProgramUniform4f(programa, la, x, y, z, w);
		if (lb >= 0 && programa_ez) p_glProgramUniform4f(programa_ez, lb, x, y, z, w);
	}
	else if (la >= 0)
		p_glUniform4f(la, x, y, z, w);
}

static void pu_2fv(GLint la, GLint lb, GLsizei n, const GLfloat * v)
{
	if (p_glProgramUniform2fv != NULL)
	{
		if (la >= 0) p_glProgramUniform2fv(programa, la, n, v);
		if (lb >= 0 && programa_ez) p_glProgramUniform2fv(programa_ez, lb, n, v);
	}
	else if (la >= 0)
		p_glUniform2fv(la, n, v);
}

int glmoderno_shader_iniciar(void)
{
	GLuint	vs, fs;
	GLint	ok = 0, largo = 0;

	p_glCreateShader		= (PFN_CREATE_SHADER)	resolver("glCreateShader");
	p_glShaderSource		= (PFN_SHADER_SOURCE)	resolver("glShaderSource");
	p_glCompileShader		= (PFN_COMPILE_SHADER)	resolver("glCompileShader");
	p_glGetShaderiv			= (PFN_GET_SHADER_IV)	resolver("glGetShaderiv");
	p_glGetShaderInfoLog	= (PFN_GET_SHADER_LOG)	resolver("glGetShaderInfoLog");
	p_glCreateProgram		= (PFN_CREATE_PROGRAM)	resolver("glCreateProgram");
	p_glAttachShader		= (PFN_ATTACH_SHADER)	resolver("glAttachShader");
	p_glLinkProgram			= (PFN_LINK_PROGRAM)	resolver("glLinkProgram");
	p_glGetProgramiv		= (PFN_GET_PROGRAM_IV)	resolver("glGetProgramiv");
	p_glGetProgramInfoLog	= (PFN_GET_PROGRAM_LOG)	resolver("glGetProgramInfoLog");
	p_glUseProgram			= (PFN_USE_PROGRAM)		resolver("glUseProgram");
	p_glDeleteShader		= (PFN_DELETE_SHADER)	resolver("glDeleteShader");
	p_glGetUniformLocation	= (PFN_GET_UNIFORM_LOC)	resolver("glGetUniformLocation");
	p_glUniform1i			= (PFN_UNIFORM_1I)		resolver("glUniform1i");
	p_glUniform1f			= (PFN_UNIFORM_1F)		resolver("glUniform1f");
	p_glUniform3f			= (PFN_UNIFORM_3F)		resolver("glUniform3f");
	p_glUniform4f			= (PFN_UNIFORM_4F)		resolver("glUniform4f");
	p_glUniform2fv			= (PFN_UNIFORM_2FV)		resolver("glUniform2fv");

	p_glProgramUniform1i	= (PFN_PU_1I)	resolver("glProgramUniform1i");
	p_glProgramUniform1f	= (PFN_PU_1F)	resolver("glProgramUniform1f");
	p_glProgramUniform3f	= (PFN_PU_3F)	resolver("glProgramUniform3f");
	p_glProgramUniform4f	= (PFN_PU_4F)	resolver("glProgramUniform4f");
	p_glProgramUniform2fv	= (PFN_PU_2FV)	resolver("glProgramUniform2fv");

	/* Todas o ninguna: media familia resuelta dejaria un uniforme escrito en un
	   programa y no en el otro, que es exactamente la forma de fallar que este
	   arreglo viene a cerrar. */
	if (!p_glProgramUniform1i || !p_glProgramUniform1f || !p_glProgramUniform3f
	||  !p_glProgramUniform4f || !p_glProgramUniform2fv)
	{
		p_glProgramUniform1i = NULL;
		p_glProgramUniform1f = NULL;
		p_glProgramUniform3f = NULL;
		p_glProgramUniform4f = NULL;
		p_glProgramUniform2fv = NULL;
	}

	if (!p_glCreateShader || !p_glShaderSource || !p_glCompileShader
	||  !p_glGetShaderiv || !p_glGetShaderInfoLog || !p_glCreateProgram
	||  !p_glAttachShader || !p_glLinkProgram || !p_glGetProgramiv
	||  !p_glGetProgramInfoLog || !p_glUseProgram || !p_glDeleteShader
	||  !p_glGetUniformLocation || !p_glUniform1i || !p_glUniform1f
	||  !p_glUniform3f || !p_glUniform4f || !p_glUniform2fv)
	{
		fprintf(stderr, "gl: el driver no da GLSL; no hay camino programable\n");
		return 0;
	}

	vs = compilar(GL_VERTEX_SHADER, fuente_vs, "vertex shader");

	if (vs == 0)
		return 0;

	/*
		Se intenta primero el sabor de 4.30, que es el que sabe apilar el
		fragmento en la lista por pixel. Si no compila --driver viejo, o un
		contexto que no da 4.3-- se cae al de 1.20 y la transparencia ordenada
		simplemente no esta.

		Del de 4.30 se compilan **dos programas con el mismo fuente**: el normal
		y el de prueba adelantada. La razon esta entera en fs_temprano, y el
		precio --mantener los uniformes de los dos al dia-- lo paga
		glProgramUniform*. Sin esa entrada se sigue con uno solo y la lista
		queda mal ordenada donde haya opacos delante, asi que se prefiere
		apagar la transparencia ordenada antes que dibujar de mas.
	*/
	{
		const char * partes[4];

		partes[0] = fs_cabeza_oit;
		partes[1] = fuente_fs_cuerpo;
		partes[2] = fs_main_oit;

		fs = compilar_partes(GL_FRAGMENT_SHADER, partes, 3,
			"fragment shader (4.30)");

		if (fs != 0)
		{
			programa = enlazar(vs, fs, "el programa (4.30)");
			p_glDeleteShader(fs);
		}

		if (programa != 0 && p_glProgramUniform1i != NULL)
		{
			partes[0] = fs_cabeza_oit;
			partes[1] = fs_temprano;
			partes[2] = fuente_fs_cuerpo;
			partes[3] = fs_main_oit;

			fs = compilar_partes(GL_FRAGMENT_SHADER, partes, 4,
				"fragment shader (4.30, prueba adelantada)");

			if (fs != 0)
			{
				programa_ez = enlazar(vs, fs,
					"el programa (4.30, prueba adelantada)");
				p_glDeleteShader(fs);
			}
		}

		if (programa != 0 && programa_ez != 0)
			hay_oit = 1;
		else if (programa != 0 && programa_ez == 0)
			fprintf(stderr, "gl: sin prueba de profundidad adelantada no hay"
				" transparencia ordenada\n");
	}

	if (programa == 0)
	{
		const char * partes[3];

		partes[0] = fs_cabeza_120;
		partes[1] = fuente_fs_cuerpo;
		partes[2] = fs_main_120;

		fs = compilar_partes(GL_FRAGMENT_SHADER, partes, 3,
			"fragment shader (1.20)");

		if (fs != 0)
		{
			programa = enlazar(vs, fs, "el programa (1.20)");
			p_glDeleteShader(fs);
		}
	}

	p_glDeleteShader(vs);

	if (programa == 0)
		return 0;

	ubicar(programa, &u_n);
	ubicar(programa_ez, &u_z);

	/* La unidad 0 para las texturas del guest y la 1 para el acumulador
	   secundario, una vez: el arbol no usa multitextura para dibujar. */
	p_glUseProgram(programa);
	pu_1i(u_n.muestra, u_z.muestra, 0);
	pu_1i(u_n.acum_muestra, u_z.acum_muestra, 1);
	pu_1i(u_n.oit, u_z.oit, 0);
	p_glUseProgram(0);

	hay_shader = 1;

	fprintf(stderr, "gl: camino programable listo (GLSL %s)\n",
		hay_oit ? "4.30, con transparencia ordenada" : "1.20");

	if (hay_oit && !oit_armar())
	{
		fprintf(stderr, "gl: no se pudieron crear los recursos de la"
			" transparencia ordenada; queda apagada\n");
		hay_oit = 0;
	}

	/* Los volumenes por pixel piden lo mismo que la OIT --imagenes atomicas y
	   prueba de profundidad adelantada--, asi que van juntos: si el programa de
	   4.30 esta, los dos estan. */
	hay_vol = (programa_ez != 0);

	if (hay_vol && !vol_armar())
	{
		fprintf(stderr, "gl: no se pudieron crear los recursos de los"
			" volumenes por pixel; se dibujan con plantilla\n");
		hay_vol = 0;
	}

	fprintf(stderr, "gl: volumenes modificadores %s\n",
		hay_vol ? "por pixel" : "por plantilla");

	return 1;
}

int glmoderno_hay_shader(void) { return hay_shader; }

/* Cual de los dos va ligado ahora mismo: el de prueba adelantada solo mientras
   la tanda translucida apila. */
static GLuint prog_actual(void)
{
	return (oit_puesto && programa_ez) ? programa_ez : programa;
}

void glmoderno_shader_usar(int puesto)
{
	if (!hay_shader)
		return;

	if (shader_puesto == (puesto ? 1 : 0))
		return;

	shader_puesto = puesto ? 1 : 0;
	p_glUseProgram(shader_puesto ? prog_actual() : 0);
}

/* Los uniformes no llevan sombra propia: los llama la de graficos.c, que ya
   filtra lo que no cambio. Duplicar el filtro solo daria dos verdades. */
void glmoderno_u_textura(int on)
{
	if (hay_shader)
		pu_1i(u_n.textura, u_z.textura, on ? 1 : 0);
}

void glmoderno_u_env(int modo)
{
	if (hay_shader)
		pu_1i(u_n.env, u_z.env, modo);
}

void glmoderno_u_offset(int on)
{
	if (hay_shader)
		pu_1i(u_n.offset, u_z.offset, on ? 1 : 0);
}

void glmoderno_u_alpha(int on, float umbral)
{
	if (!hay_shader)
		return;

	pu_1i(u_n.alpha, u_z.alpha, on ? 1 : 0);
	pu_1f(u_n.umbral, u_z.umbral, umbral);
}

/*
	Lo de la niebla que vale para el cuadro entero. Se sube una vez por escena
	y no por tira: el color, la densidad y la tabla salen de registros del PVR,
	que el guest no toca en medio de un render.

	Va con el programa puesto y lo repone como estaba: se llama desde el
	arranque de la escena, antes de que nadie haya decidido si dibuja con
	shader o sin el.
*/
void glmoderno_niebla_escena(float r, float g, float b, float densidad,
							 const float * tabla)
{
	int antes = shader_puesto;

	if (!hay_shader)
		return;

	if (!antes)
		p_glUseProgram(prog_actual());

	pu_3f(u_n.nie_color, u_z.nie_color, r, g, b);
	pu_1f(u_n.nie_dens, u_z.nie_dens, densidad);

	if (tabla != NULL)
		pu_2fv(u_n.nie_tabla, u_z.nie_tabla, 128, tabla);

	if (!antes)
		p_glUseProgram(0);
}

void glmoderno_u_niebla(int on)
{
	if (hay_shader)
		pu_1i(u_n.niebla, u_z.niebla, on ? 1 : 0);
}

/* ------------------------------------------------------------------------ */
/* Transparencia ordenada por pixel                                         */
/* ------------------------------------------------------------------------ */

/* Resuelve las entradas y compila el shader de resolucion. El buffer de nodos
   y la imagen de cabezas se dimensionan despues, cuando se sabe el tamano. */
static int oit_armar(void)
{
	GLuint vs, fs;

	p_glGenBuffers			= (PFN_GEN_BUF)			resolver("glGenBuffers");
	p_glDeleteBuffers		= (PFN_DEL_BUF)			resolver("glDeleteBuffers");
	p_glBindBuffer			= (PFN_BIND_BUF)		resolver("glBindBuffer");
	p_glBufferData			= (PFN_BUF_DATA)		resolver("glBufferData");
	p_glBufferSubData		= (PFN_BUF_SUBDATA)		resolver("glBufferSubData");
	p_glBindBufferBase		= (PFN_BIND_BUF_BASE)	resolver("glBindBufferBase");
	p_glBindImageTexture	= (PFN_BIND_IMG_TEX)	resolver("glBindImageTexture");
	p_glMemoryBarrier		= (PFN_MEM_BARRIER)		resolver("glMemoryBarrier");
	p_glClearTexImage		= (PFN_CLEAR_TEX_IMG)	resolver("glClearTexImage");
	p_glActiveTexture		= (PFN_ACTIVE_TEX)		resolver("glActiveTexture");

	if (!p_glGenBuffers || !p_glDeleteBuffers || !p_glBindBuffer
	||  !p_glBufferData || !p_glBufferSubData || !p_glBindBufferBase
	||  !p_glBindImageTexture || !p_glMemoryBarrier || !p_glClearTexImage
	||  !p_glActiveTexture)
		return 0;

	vs = compilar(GL_VERTEX_SHADER, fuente_vs_resolver, "vs de resolucion");
	fs = compilar(GL_FRAGMENT_SHADER, fuente_fs_resolver, "fs de resolucion");

	oit_prog = enlazar(vs, fs, "el programa de resolucion");

	if (vs) p_glDeleteShader(vs);
	if (fs) p_glDeleteShader(fs);

	if (oit_prog == 0)
		return 0;

	u_res_fondo = p_glGetUniformLocation(oit_prog, "fondo");
	u_res_presort = p_glGetUniformLocation(oit_prog, "presort");

	/*
		DCEMU_OIT_SOLO_FONDO=1: la resolucion emite el fondo y nada mas.

		Es la sonda que separa "la lista esta vacia" de "el fondo no se
		copio", que dan el mismo sintoma --pantalla negra-- y son dos fallas
		distintas. Cuesta una comparacion contra un uniforme y vive en el
		binario normal, como el resto de las sondas del arbol.
	*/
	{
		GLint u = p_glGetUniformLocation(oit_prog, "solo_fondo");
		const char * e = getenv("DCEMU_OIT_SOLO_FONDO");

		p_glUseProgram(oit_prog);

		if (u >= 0)
			p_glUniform1i(u, (e != NULL) ? atoi(e) : 0);

		p_glUseProgram(0);
	}

	p_glGenBuffers(1, &oit_nodos);
	p_glGenBuffers(1, &oit_contador);

	return 1;
}

int glmoderno_hay_oit(void) { return hay_oit; }

int glmoderno_oit_dimensionar(int ancho, int alto)
{
	unsigned long long pedidos;

	if (!hay_oit || ancho <= 0 || alto <= 0)
		return 0;

	if (oit_cabezas != 0 && ancho == oit_w && alto == oit_h)
		return 1;

	if (oit_cabezas != 0)
		glDeleteTextures(1, &oit_cabezas);
	if (oit_fondo != 0)
		glDeleteTextures(1, &oit_fondo);

	/* Una cabeza de lista por pixel. Entero sin signo de 32 bits porque la
	   cabeza es un indice y el centinela de "lista vacia" es 0xFFFFFFFF. */
	glGenTextures(1, &oit_cabezas);
	glBindTexture(GL_TEXTURE_2D, oit_cabezas);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, ancho, alto, 0,
		GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	/* Y una copia de lo que dejo la tanda opaca, que es el destino sobre el
	   que la resolucion mezcla. Va aparte porque leer y escribir la misma
	   textura en la misma pasada no esta definido. */
	glGenTextures(1, &oit_fondo);
	glBindTexture(GL_TEXTURE_2D, oit_fondo);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ancho, alto, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glBindTexture(GL_TEXTURE_2D, 0);

	pedidos = (unsigned long long) ancho * alto * OIT_CAPAS;

	if (pedidos > OIT_NODOS_TOPE)
		pedidos = OIT_NODOS_TOPE;

	oit_max = (unsigned) pedidos;

	p_glBindBuffer(GL_SHADER_STORAGE_BUFFER, oit_nodos);
	p_glBufferData(GL_SHADER_STORAGE_BUFFER,
		(GLsizeiptr) oit_max * 16, NULL, GL_DYNAMIC_DRAW);
	p_glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	{
		GLuint cero = 0;

		p_glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, oit_contador);
		p_glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), &cero,
			GL_DYNAMIC_DRAW);
		p_glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
	}

	oit_w = ancho;
	oit_h = alto;

	fprintf(stderr, "gl: transparencia ordenada, %dx%d, %u nodos (%.0f MB)\n",
		ancho, alto, oit_max, oit_max * 16.0 / (1024.0 * 1024.0));

	if ((unsigned long long) ancho * alto * OIT_CAPAS > OIT_NODOS_TOPE)
		fprintf(stderr, "gl: la reserva quedo en el tope; con muchas capas"
			" translucidas se van a perder fragmentos\n");

	pu_1i(u_n.oit_max, u_z.oit_max, (GLint) oit_max);

	return 1;
}

/*
	Deja todo listo para la tanda translucida: la lista vacia, el contador en
	cero y una copia de lo que dejo la tanda opaca.

	La copia sale del framebuffer que esta ligado con glCopyTexSubImage2D, o
	sea del FBO -- por eso la transparencia ordenada exige el destino propio y
	no funciona dibujando en la ventana.
*/
void glmoderno_oit_empezar(int ancho, int alto, int presort)
{
	GLuint	vacio = 0xFFFFFFFFu;
	GLuint	cero = 0;

	if (!hay_oit || oit_cabezas == 0)
		return;

	if (u_res_presort >= 0)
	{
		p_glUseProgram(oit_prog);
		p_glUniform1i(u_res_presort, presort ? 1 : 0);
		p_glUseProgram(shader_puesto ? prog_actual() : 0);
	}

	p_glClearTexImage(oit_cabezas, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &vacio);

	p_glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, oit_contador);
	p_glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &cero);
	p_glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);

	/* La unidad 0 explicita: es la que usa el programa de la escena y la que
	   va a leer la resolucion, y dejarlo al azar es pedir un fondo negro sin
	   ningun sintoma que lo explique. */
	p_glActiveTexture(0x84C0 /* GL_TEXTURE0 */);
	glBindTexture(GL_TEXTURE_2D, oit_fondo);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ancho, alto);
	glBindTexture(GL_TEXTURE_2D, 0);

	p_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, oit_nodos);
	p_glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, oit_contador);
	p_glBindImageTexture(0, oit_cabezas, 0, GL_FALSE, 0, GL_READ_WRITE,
		GL_R32UI);
}

/*
	Ordena y mezcla: un quad de pantalla completa con el shader de resolucion.

	La barrera es obligatoria y no una precaucion: sin ella el driver puede
	empezar a leer los nodos antes de que las escrituras de la tanda anterior
	sean visibles, y el resultado depende de la carga de la GPU -- o sea que
	falla distinto en cada corrida, que es la peor forma de fallar.
*/
void glmoderno_oit_resolver(void)
{
	if (!hay_oit || oit_prog == 0)
		return;

	p_glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT
					| GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
					| GL_ATOMIC_COUNTER_BARRIER_BIT
					| GL_TEXTURE_FETCH_BARRIER_BIT);

	p_glUseProgram(oit_prog);

	if (u_res_fondo >= 0)
		p_glUniform1i(u_res_fondo, 0);

	p_glActiveTexture(0x84C0 /* GL_TEXTURE0 */);
	glBindTexture(GL_TEXTURE_2D, oit_fondo);
	glEnable(GL_TEXTURE_2D);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	/* El quad va en coordenadas de recorte directamente: el vertex shader de
	   resolucion no aplica matriz, asi que no depende del glOrtho de la escena
	   ni hay que salvarlo y reponerlo. */
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex4f(-1.0f, -1.0f, 0.0f, 1.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex4f( 1.0f, -1.0f, 0.0f, 1.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex4f( 1.0f,  1.0f, 0.0f, 1.0f);
	glTexCoord2f(0.0f, 1.0f); glVertex4f(-1.0f,  1.0f, 0.0f, 1.0f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_DEPTH_TEST);

	p_glUseProgram(shader_puesto ? prog_actual() : 0);
}

/*
	Encender el apilado es tambien **cambiar de programa**: el de prueba
	adelantada es el unico que deja fuera de la lista lo que la profundidad
	rechaza. Si el driver no dio glProgramUniform* no hay segundo programa y
	esto se reduce al uniforme de antes.
*/
/* ------------------------------------------------------------------------ */
/* Volumenes modificadores por pixel                                        */
/* ------------------------------------------------------------------------ */

static int vol_armar(void)
{
	GLuint vs, fs;

	vs = compilar(GL_VERTEX_SHADER,
		"#version 430 compatibility\n"
		"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n",
		"vertex shader (volumen)");

	if (vs == 0)
		return 0;

	fs = compilar(GL_FRAGMENT_SHADER, fuente_fs_vol, "fragment shader (volumen)");

	if (fs != 0)
	{
		vol_prog = enlazar(vs, fs, "el programa de volumen");
		p_glDeleteShader(fs);
	}

	p_glDeleteShader(vs);

	if (vol_prog == 0)
		return 0;

	/* El de plegar dibuja un quad en coordenadas de recorte, como el de
	   resolucion de la OIT: no depende del glOrtho de la escena. */
	vs = compilar(GL_VERTEX_SHADER, fuente_vs_resolver, "vertex shader (plegar)");
	fs = compilar(GL_FRAGMENT_SHADER, fuente_fs_vol_plegar,
		"fragment shader (plegar)");

	if (vs != 0 && fs != 0)
		vol_prog_plegar = enlazar(vs, fs, "el programa de plegado");

	if (vs != 0) p_glDeleteShader(vs);
	if (fs != 0) p_glDeleteShader(fs);

	if (vol_prog_plegar == 0)
		return 0;

	u_vol_excluir = p_glGetUniformLocation(vol_prog_plegar, "excluir");

	/* Las unidades de imagen van en el fuente (`layout(binding=)`): 1 la
	   mascara y 2 el contador del grupo. No hay uniformes que poner. */

	{
		const char * e = getenv("DCEMU_VOL_SONDA");
		GLint v = (e != NULL) ? atoi(e) : 0;

		pu_1i(p_glGetUniformLocation(programa, "vol_sonda"),
			p_glGetUniformLocation(programa_ez, "vol_sonda"), v);
	}

	return 1;
}

/* ------------------------------------------------------------------------ */
/* El buffer de acumulacion secundario del TSP (bits 25 y 24)               */
/* ------------------------------------------------------------------------ */

int glmoderno_hay_acumulador(void)
{
	return hay_fbo && hay_shader && fbo_sec != 0 && ligado;
}

void glmoderno_acum_limpiar(void)
{
	GLfloat antes[4];

	if (!glmoderno_hay_acumulador())
		return;

	/*
		El secundario arranca en cero en cada escena, como el primario. Si no,
		un grupo que se acumule ahi empieza sobre lo que dejo el cuadro
		anterior -- y como se compone de una vez, el fantasma sale entero.
	*/
	glGetFloatv(GL_COLOR_CLEAR_VALUE, antes);

	glDrawBuffer(GL_COLOR_ATTACHMENT0 + 1);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	glClearColor(antes[0], antes[1], antes[2], antes[3]);
	acum_sec = 0;
}

void glmoderno_acum_destino(int secundario)
{
	if (!glmoderno_hay_acumulador() || acum_sec == (secundario ? 1 : 0))
		return;

	acum_sec = secundario ? 1 : 0;
	glDrawBuffer(GL_COLOR_ATTACHMENT0 + acum_sec);
}

/*
	La fuente de la mezcla: el fragmento (0) o el buffer secundario (1).

	Con 1 el shader NO usa el color que calculo la tira: toma el pixel que el
	secundario tiene en esa posicion. Es la mitad que compone el grupo acumulado
	sobre el primario, y la que obliga a que el secundario sea una textura.
*/
void glmoderno_acum_fuente(int secundario)
{
	if (!hay_shader)
		return;

	pu_1i(u_n.acum_src, u_z.acum_src, secundario ? 1 : 0);

	if (secundario && fbo_sec != 0)
	{
		p_glActiveTexture(0x84C1 /* GL_TEXTURE1 */);
		glBindTexture(GL_TEXTURE_2D, fbo_sec);
		p_glActiveTexture(0x84C0);
	}
}

int glmoderno_hay_volumen_px(void) { return hay_vol; }

int glmoderno_vol_dimensionar(int ancho, int alto)
{
	if (!hay_vol || ancho <= 0 || alto <= 0)
		return 0;

	if (vol_mascara != 0 && ancho == vol_w && alto == vol_h)
		return 1;

	if (vol_mascara != 0) glDeleteTextures(1, &vol_mascara);
	if (vol_cuenta != 0)  glDeleteTextures(1, &vol_cuenta);

	glGenTextures(1, &vol_mascara);
	glBindTexture(GL_TEXTURE_2D, vol_mascara);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, ancho, alto, 0,
		GL_RED_INTEGER, GL_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &vol_cuenta);
	glBindTexture(GL_TEXTURE_2D, vol_cuenta);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, ancho, alto, 0,
		GL_RED_INTEGER, GL_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glBindTexture(GL_TEXTURE_2D, 0);

	vol_w = ancho;
	vol_h = alto;

	return 1;
}

/*
	Empezar la marca de una lista de volumenes. `por_grupo` es 1 si la escena
	trae alguna instruccion 2: entonces cada grupo se cuenta aparte y se dobla,
	y si no, todos suman en la mascara y el shader prueba != 0 -- que es
	exactamente lo que hacia la plantilla.
*/
void glmoderno_vol_empezar(int ancho, int alto, int por_grupo)
{
	GLint cero = 0;

	if (!hay_vol)
		return;

	/*
		El dimensionado va ANTES de mirar la textura, no despues.

		Con `vol_mascara == 0` en la guarda de arriba --que es su valor hasta que
		esta llamada la crea-- la funcion salia sin crear nada, para siempre: la
		imagen nunca se ligaba, imageAtomicAdd escribia en el vacio e imageLoad
		devolvia cero. O sea la mascara vacia en todas las escenas, sin un solo
		error a la vista, que es la forma de fallar de siempre en este arbol.
	*/
	if (!glmoderno_vol_dimensionar(ancho, alto) || vol_mascara == 0)
		return;

	(void) por_grupo;

	p_glClearTexImage(vol_mascara, 0, GL_RED_INTEGER, GL_INT, &cero);
	p_glClearTexImage(vol_cuenta, 0, GL_RED_INTEGER, GL_INT, &cero);

	p_glBindImageTexture(1, vol_mascara, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32I);
	p_glBindImageTexture(2, vol_cuenta, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32I);
}

void glmoderno_vol_acumular(int on, int por_grupo)
{
	(void) por_grupo;

	if (!hay_vol || vol_prog == 0)
		return;

	p_glUseProgram(on ? vol_prog : (shader_puesto ? prog_actual() : 0));
}

void glmoderno_vol_plegar(int excluir)
{
	GLint cero = 0;

	if (!hay_vol || vol_prog_plegar == 0)
		return;

	p_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	p_glUseProgram(vol_prog_plegar);

	if (u_vol_excluir >= 0)
		p_glUniform1i(u_vol_excluir, excluir ? 1 : 0);

	/* El quad tiene que tocar TODOS los pixeles: la prueba de profundidad la
	   rechazaria contra la escena que ya esta dibujada, y el complemento de un
	   volumen vive justamente donde el volumen no llego. */
	glDisable(GL_DEPTH_TEST);

	glBegin(GL_QUADS);
	glVertex4f(-1.0f, -1.0f, 0.0f, 1.0f);
	glVertex4f( 1.0f, -1.0f, 0.0f, 1.0f);
	glVertex4f( 1.0f,  1.0f, 0.0f, 1.0f);
	glVertex4f(-1.0f,  1.0f, 0.0f, 1.0f);
	glEnd();

	/* El grupo siguiente arranca de cero. */
	p_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	p_glClearTexImage(vol_cuenta, 0, GL_RED_INTEGER, GL_INT, &cero);

	glEnable(GL_DEPTH_TEST);
	p_glUseProgram(shader_puesto ? prog_actual() : 0);
}

void glmoderno_vol_listo(void)
{
	if (!hay_vol)
		return;

	p_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	/*
		DCEMU_VOL_SONDA=2: lee la mascara de vuelta y dice cuantos texeles
		quedaron distintos de cero, mas el error de GL pendiente. Es la sonda
		que separa "la pasada de acumulacion no escribio" de "el shader de
		escena no la lee", que dan el mismo sintoma -- la escena sin volumen.
	*/
	{
		static int	sonda = -1;
		static int	quedan = 8;
		int *		p;
		int			i, n = 0, total;

		if (sonda < 0)
		{
			const char * e = getenv("DCEMU_VOL_SONDA");
			sonda = (e != NULL) ? atoi(e) : 0;
		}

		if (sonda != 2 || quedan <= 0 || vol_mascara == 0)
			return;

		quedan--;
		total = vol_w * vol_h;
		p = (int *) malloc((size_t) total * sizeof(int));

		if (p == NULL)
			return;

		glBindTexture(GL_TEXTURE_2D, vol_mascara);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_INT, p);
		glBindTexture(GL_TEXTURE_2D, 0);

		for (i = 0; i < total; i++)
			if (p[i] != 0)
				n++;

		fprintf(stderr, "gl: mascara de volumen %dx%d: %d texeles marcados,"
			" glGetError %04x\n", vol_w, vol_h, n, (unsigned) glGetError());

		free(p);
	}
}

void glmoderno_u_volumen(int on)
{
	if (hay_shader)
		pu_1i(u_n.volumen, u_z.volumen, on ? 1 : 0);
}

void glmoderno_u_oit(int on)
{
	if (!hay_shader)
		return;

	pu_1i(u_n.oit, u_z.oit, on ? 1 : 0);

	if (oit_puesto == (on ? 1 : 0))
		return;

	oit_puesto = on ? 1 : 0;

	if (shader_puesto)
		p_glUseProgram(prog_actual());
}

void glmoderno_u_mezcla(int src, int dst)
{
	if (hay_shader)
		pu_1i(u_n.oit_mezcla, u_z.oit_mezcla,
			(GLint) (((src & 7) << 4) | (dst & 7)));
}

void glmoderno_u_bump(int on, unsigned long param)
{
	if (!hay_shader)
		return;

	pu_1i(u_n.bump, u_z.bump, on ? 1 : 0);

	if (on)
		pu_4f(u_n.bump_param, u_z.bump_param,
			(GLfloat) ((param >> 24) & 0xFF) / 255.0f,		/* K1 */
			(GLfloat) ((param >> 16) & 0xFF) / 255.0f,		/* K2 */
			(GLfloat) ((param >> 8)  & 0xFF) / 255.0f,		/* K3 */
			/* Q ya en radianes: el shader compara contra R, que tambien lo
			   esta. Convertirlo aca y no alla ahorra la constante en el
			   camino caliente y deja una sola definicion del rango. */
			(GLfloat) (((param) & 0xFF) / 255.0f * 6.2831853f));
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
