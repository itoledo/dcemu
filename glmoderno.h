/*
	glmoderno.h -- el contexto de GL que el driver ya daba, y un destino de
	render propio.

	**El hallazgo que hace esto posible**: `SDL_GL_SetAttribute` de SDL 1.2 no
	tiene atributos de version ni de perfil, asi que no se puede pedir un
	contexto *core*. No hace falta -- en Windows y en Mesa el contexto por
	omision es de **compatibilidad**, que en cualquier driver actual llega a
	GL 4.6, y las entradas se resuelven con `SDL_GL_GetProcAddress`. O sea que
	funcion fija y GL moderno conviven en el mismo contexto y la migracion es
	incremental, sin cambiar de SDL y sin un salto todo o nada.

	El patron ya existia en el arbol: `offset_iniciar()` de graficos.c resuelve
	`glSecondaryColorPointer` exactamente asi, con su respaldo EXT y su bandera
	de disponibilidad. Esto es lo mismo a mayor escala.

	Lo que hay aca es la etapa 2.a de docs/rendimiento-plan.md: un objeto de
	framebuffer donde dibujar. Hoy la escena va al buffer trasero de la ventana
	--800x600-- y todo lo que el guest tiene que leer de vuelta vuelve por un
	glReadPixels de la ventana. Eso arrastra tres cosas que un FBO arregla:

	  - **el volcado del framebuffer se remuestrea**: se leen 800x600 y se
	    guardan 640x480 por vecino mas cercano, porque la ventana no mide lo
	    que la pantalla emulada;
	  - **el render a textura no puede pasar del tamano de la ventana**, porque
	    dibuja en el buffer trasero;
	  - **no hay escalado de resolucion interna**, que es lo que cualquier
	    emulador actual ofrece y aca sale casi gratis.

	No hay shaders todavia: el modelo de dibujo es el mismo, y por eso el
	contenido se puede comparar contra el camino de siempre.
*/

#ifndef _GLMODERNO_H_
#define _GLMODERNO_H_

/* Resuelve las entradas y averigua la version. Se llama una vez, con el
   contexto ya creado. Devuelve 1 si el FBO se puede usar. */
int glmoderno_iniciar(void);

/* 1 si el driver dio lo que hace falta (FBO y blit). */
int glmoderno_hay_fbo(void);

/* La version del contexto por 10: 46 es 4.6. 0 si no se pudo leer. */
int glmoderno_version(void);

/*
	Se asegura de que exista un FBO de al menos ancho x alto, con color de 8
	bits por canal mas alfa, profundidad de 24 y plantilla de 8 -- lo mismo que
	el contexto de la ventana, porque el arbol depende de las tres cosas: la
	plantilla lleva los volumenes modificadores, la profundidad de 24 hace
	falta porque profundidad_ta() comprime las z en una parte chica del rango,
	y sin planos de alfa el blend por DST_ALPHA no funciona.

	Devuelve 1 si a partir de aca se puede dibujar en el. Crece si se le pide
	mas: el guest cambia de modo de video en caliente.
*/
int glmoderno_fbo_asegurar(int ancho, int alto);

/* Dibujar en el FBO (1) o en la ventana (0). Sin FBO no hace nada. */
void glmoderno_fbo_ligar(int puesto);

/* 1 si en este momento se esta dibujando en el FBO. */
int glmoderno_fbo_ligado(void);

/*
	Copia el rectangulo (0,0)-(ancho,alto) del FBO a la ventana, escalando a
	`ven_ancho` x `ven_alto` y **conservando la relacion de aspecto**: lo que
	sobra queda en negro. Deja la ventana ligada.
*/
void glmoderno_presentar(int ancho, int alto, int ven_ancho, int ven_alto);

/* El tamano del FBO que hay hoy, para quien tenga que leerlo de vuelta. */
int glmoderno_fbo_ancho(void);
int glmoderno_fbo_alto(void);

/* ------------------------------------------------------------------------ */
/* El camino programable (etapa 2.b)                                        */
/* ------------------------------------------------------------------------ */

/*
	Un par de shaders que reproduce lo que hoy hacen GL_COMBINE, glAlphaFunc y
	GL_COLOR_SUM. **Misma imagen, distinto mecanismo**: no agrega precision por
	si mismo, y por eso se puede verificar contra el camino de funcion fija.

	Se escribe en GLSL 1.20 **de compatibilidad**, con las variables
	incorporadas (gl_Vertex, gl_Color, gl_SecondaryColor, gl_MultiTexCoord0,
	gl_ModelViewProjectionMatrix). Eso no es nostalgia: es lo que hace que los
	arreglos de cliente que ya programa glinit() --y el glColorPointer que el
	barrido de niebla intercambia por su propia tabla-- sigan alimentando al
	shader sin tocar una linea del camino de dibujo. Con atributos genericos
	habria que armar VBO y VAO, que es trabajo de la etapa siguiente y otro
	riesgo.

	Ojo con la matriz: screeninit() pone el glOrtho en la MODELVIEW y deja la
	PROJECTION en identidad, asi que gl_ModelViewProjectionMatrix es justo el
	ortho. Sale bien, pero no por donde uno lo buscaria.
*/
int glmoderno_shader_iniciar(void);

/* 1 si el programa compilo y enlazo. */
int glmoderno_hay_shader(void);

/* Encender o apagar el programa. Apagado se vuelve a funcion fija, que es lo
   que necesitan los caminos 2D y los quads del framebuffer. */
void glmoderno_shader_usar(int puesto);

/*
	Los uniformes, uno por cada pieza de estado que el shader reemplaza. Se
	llaman desde la sombra de estado de graficos.c --gl_textura(),
	gl_alpha_test(), offset_estado() y el switch del entorno de textura-- para
	que el shader y la funcion fija no puedan discrepar: si la sombra dice que
	algo no cambio, tampoco cambio para el shader.
*/
void glmoderno_u_textura(int on);
void glmoderno_u_env(int modo);
void glmoderno_u_offset(int on);
void glmoderno_u_alpha(int on, float umbral);

/*
	La niebla, que en el camino programable es **por pixel como en el chip** y
	no una segunda pasada de geometria por tira.

	`glmoderno_niebla_escena()` sube lo que vale para el cuadro entero: el color,
	la densidad y las 128 entradas de la tabla, cada una con su alfa lejano y su
	alfa cercano. `glmoderno_u_niebla()` dice si la tira que viene la lleva.
*/
void glmoderno_niebla_escena(float r, float g, float b, float densidad,
							 const float * tabla /* 128 x 2 */);
void glmoderno_u_niebla(int on);

/*
	El mapa de relieve. Con el camino programable la textura sube con los dos
	angulos crudos y la intensidad se resuelve por pixel, con los parametros
	del **poligono** -- que es lo que la version horneada no podia hacer, porque
	la cache de texturas se indexa por direccion y no por parametros.

	`param` es la palabra tal cual viene en el color de offset del encabezado:
	K1, K2, K3 y Q, un byte cada uno de arriba hacia abajo.
*/
void glmoderno_u_bump(int on, unsigned long param);

/* ------------------------------------------------------------------------ */
/* Transparencia ordenada por pixel (OIT)                                   */
/* ------------------------------------------------------------------------ */

/*
	El artefacto clasico de la Dreamcast, y el punto grande de la etapa 2.c.

	El chip ordena la lista translucida **por pixel**; dcemu la ordena por tira
	con un qsort sobre la profundidad del centro, y su propio comentario admite
	que geometria translucida que se interpenetra puede salir mal -- dos tiras
	que se cruzan no tienen un orden correcto como tiras.

	El mecanismo es una lista encadenada por pixel: la tanda translucida no
	mezcla, apila cada fragmento con su color, su profundidad y sus dos codigos
	de mezcla, y una pasada de resolucion ordena cada lista y la mezcla sobre lo
	que dejo la tanda opaca. Necesita GL 4.3 (SSBO, imagenes atomicas) y el
	destino propio, porque el fondo se copia del FBO.

	**Y necesita que la prueba de profundidad corra antes del shader**, que es
	lo que obliga a un segundo programa: ver fs_temprano en glmoderno.c. Sin eso
	se apila tambien lo que la geometria opaca tapa, y la resolucion lo mezcla
	encima de lo que lo tapaba.
*/
int glmoderno_hay_oit(void);

/* Reserva las cabezas, los nodos y la copia del fondo para ese tamano. */
int glmoderno_oit_dimensionar(int ancho, int alto);

/* Antes de la tanda translucida: lista vacia, contador en cero y copia del
   fondo. */
void glmoderno_oit_empezar(int ancho, int alto, int presort);

/* Despues: ordena cada lista y la mezcla sobre el fondo. */
void glmoderno_oit_resolver(void);

/* Si los fragmentos se apilan (1) o se mezclan como siempre (0). */
void glmoderno_u_oit(int on);

/* Los dos codigos de mezcla del TSP --0 a 7, sin traducir a GL-- que viajan
   con cada fragmento apilado. */
void glmoderno_u_mezcla(int src, int dst);

#endif /* _GLMODERNO_H_ */
