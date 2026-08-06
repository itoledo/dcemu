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
static PFN_UNIFORM_2FV		p_glUniform2fv;

static GLuint	programa = 0;
static int		hay_shader = 0;
static int		shader_puesto = 0;

static GLint	u_muestra, u_textura, u_env, u_offset, u_alpha, u_umbral;
static GLint	u_niebla, u_nie_color, u_nie_dens, u_nie_tabla;

/*
	El vertex shader. No hace nada que la funcion fija no hiciera: transforma
	por la matriz y pasa color, color secundario y coordenadas de textura.

	Las coordenadas van de cuatro componentes (u*q, v*q, 0, q) y el fragmento
	las divide con texture2DProj, que es lo mismo que hace glTexCoordPointer(4)
	-- ver el comentario de `vertex` en render.h: para eso el TA entrega 1/w.
*/
static const char * fuente_vs =
	"#version 120\n"
	"void main()\n"
	"{\n"
	"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
	"	gl_FrontColor = gl_Color;\n"
	"	gl_FrontSecondaryColor = gl_SecondaryColor;\n"
	"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
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
static const char * fuente_fs =
	"#version 120\n"
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
	"void main()\n"
	"{\n"
	"	vec4 col = gl_Color;\n"
	"	vec4 pix;\n"
	"\n"
	"	if (usa_textura != 0)\n"
	"	{\n"
	"		vec4 tex = texture2DProj(muestra, gl_TexCoord[0]);\n"
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
	"		pix.rgb += gl_SecondaryColor.rgb;\n"
	"\n"
	"	if (usa_alpha != 0 && (pix.a < umbral || pix.a == 0.0))\n"
	"		discard;\n"
	"\n"
	/* Antes de salir, o sea antes de la mezcla: es donde la aplica el chip. */
	"	if (usa_niebla != 0)\n"
	"		pix.rgb = mix(pix.rgb, niebla_color,\n"
	"			niebla_alfa(gl_TexCoord[0].w));\n"
	"\n"
	"	gl_FragColor = pix;\n"
	"}\n";

/* Compila y reporta. El log se imprime siempre que exista: un shader que
   compila con avisos es lo que despues no dibuja igual. */
static GLuint compilar(GLenum tipo, const char * fuente, const char * nombre)
{
	GLuint	s = p_glCreateShader(tipo);
	GLint	ok = 0, largo = 0;

	p_glShaderSource(s, 1, &fuente, NULL);
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
	p_glUniform2fv			= (PFN_UNIFORM_2FV)		resolver("glUniform2fv");

	if (!p_glCreateShader || !p_glShaderSource || !p_glCompileShader
	||  !p_glGetShaderiv || !p_glGetShaderInfoLog || !p_glCreateProgram
	||  !p_glAttachShader || !p_glLinkProgram || !p_glGetProgramiv
	||  !p_glGetProgramInfoLog || !p_glUseProgram || !p_glDeleteShader
	||  !p_glGetUniformLocation || !p_glUniform1i || !p_glUniform1f
	||  !p_glUniform3f || !p_glUniform2fv)
	{
		fprintf(stderr, "gl: el driver no da GLSL; no hay camino programable\n");
		return 0;
	}

	vs = compilar(GL_VERTEX_SHADER, fuente_vs, "vertex shader");
	fs = compilar(GL_FRAGMENT_SHADER, fuente_fs, "fragment shader");

	if (vs == 0 || fs == 0)
		return 0;

	programa = p_glCreateProgram();
	p_glAttachShader(programa, vs);
	p_glAttachShader(programa, fs);
	p_glLinkProgram(programa);

	p_glGetProgramiv(programa, GL_LINK_STATUS, &ok);
	p_glGetProgramiv(programa, GL_INFO_LOG_LENGTH, &largo);

	if (largo > 1)
	{
		char log[2048];

		p_glGetProgramInfoLog(programa, (GLsizei) sizeof(log), NULL, log);
		fprintf(stderr, "gl: enlace del programa: %s\n", log);
	}

	/* Los objetos de shader ya no hacen falta con el programa enlazado. */
	p_glDeleteShader(vs);
	p_glDeleteShader(fs);

	if (!ok)
	{
		programa = 0;
		return 0;
	}

	u_muestra	= p_glGetUniformLocation(programa, "muestra");
	u_textura	= p_glGetUniformLocation(programa, "usa_textura");
	u_env		= p_glGetUniformLocation(programa, "modo_env");
	u_offset	= p_glGetUniformLocation(programa, "usa_offset");
	u_alpha		= p_glGetUniformLocation(programa, "usa_alpha");
	u_umbral	= p_glGetUniformLocation(programa, "umbral");
	u_niebla	= p_glGetUniformLocation(programa, "usa_niebla");
	u_nie_color	= p_glGetUniformLocation(programa, "niebla_color");
	u_nie_dens	= p_glGetUniformLocation(programa, "niebla_densidad");
	u_nie_tabla	= p_glGetUniformLocation(programa, "niebla_tabla");

	/* La unidad de textura 0, una vez: el arbol no usa multitextura. */
	p_glUseProgram(programa);
	if (u_muestra >= 0)
		p_glUniform1i(u_muestra, 0);
	p_glUseProgram(0);

	hay_shader = 1;

	fprintf(stderr, "gl: camino programable listo (GLSL 1.20)\n");

	return 1;
}

int glmoderno_hay_shader(void) { return hay_shader; }

void glmoderno_shader_usar(int puesto)
{
	if (!hay_shader)
		return;

	if (shader_puesto == (puesto ? 1 : 0))
		return;

	shader_puesto = puesto ? 1 : 0;
	p_glUseProgram(shader_puesto ? programa : 0);
}

/* Los uniformes no llevan sombra propia: los llama la de graficos.c, que ya
   filtra lo que no cambio. Duplicar el filtro solo daria dos verdades. */
void glmoderno_u_textura(int on)
{
	if (hay_shader && u_textura >= 0)
		p_glUniform1i(u_textura, on ? 1 : 0);
}

void glmoderno_u_env(int modo)
{
	if (hay_shader && u_env >= 0)
		p_glUniform1i(u_env, modo);
}

void glmoderno_u_offset(int on)
{
	if (hay_shader && u_offset >= 0)
		p_glUniform1i(u_offset, on ? 1 : 0);
}

void glmoderno_u_alpha(int on, float umbral)
{
	if (!hay_shader)
		return;

	if (u_alpha >= 0)
		p_glUniform1i(u_alpha, on ? 1 : 0);
	if (u_umbral >= 0)
		p_glUniform1f(u_umbral, umbral);
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
		p_glUseProgram(programa);

	if (u_nie_color >= 0)
		p_glUniform3f(u_nie_color, r, g, b);
	if (u_nie_dens >= 0)
		p_glUniform1f(u_nie_dens, densidad);
	if (u_nie_tabla >= 0 && tabla != NULL)
		p_glUniform2fv(u_nie_tabla, 128, tabla);

	if (!antes)
		p_glUseProgram(0);
}

void glmoderno_u_niebla(int on)
{
	if (hay_shader && u_niebla >= 0)
		p_glUniform1i(u_niebla, on ? 1 : 0);
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
