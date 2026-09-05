/*
	glmoderno.h -- el contexto de GL que el driver ya daba, y un destino de
	render propio.

	**El hallazgo que hace esto posible**: `SDL_GL_SetAttribute` de SDL 1.2 no
	tenia atributos de version ni de perfil, asi que no se podia pedir un
	contexto *core*. No hace falta -- en Windows y en Mesa el contexto por
	omision es de **compatibilidad**, que en cualquier driver actual llega a
	GL 4.6, y las entradas se resuelven con `SDL_GL_GetProcAddress`. O sea que
	funcion fija y GL moderno conviven en el mismo contexto y la migracion es
	incremental, sin un salto todo o nada. Con SDL3 (2026-09-05) se podria
	pedir perfil y no se pide: glinit() toma el contexto por omision, que es
	el mismo de antes (profundidad 24, plantilla 8, alfa 8).

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

/* Bit 19 del TSP: el alfa del texel se ignora y vale 1.0. Lo pide mas de la
   mitad de las tiras con textura en cinco de los catorce juegos. */
void glmoderno_u_sin_alfa_tex(int on);

/*
	Bit 21 del TSP: el color del pixel se acota entre FOG_CLAMP_MIN y
	FOG_CLAMP_MAX, despues de la niebla. Los limites valen para la escena y el
	bit es por tira. **La funcion fija no lo puede expresar**, asi que esto solo
	existe en el camino programable: ver docs/notas-graficos.md.
*/
void glmoderno_u_clamp(int on);
void glmoderno_clamp_escena(const float * minimo /* RGBA */,
							const float * maximo);
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

/* ------------------------------------------------------------------------ */
/* Volumenes modificadores por pixel                                        */
/* ------------------------------------------------------------------------ */

/*
	El chip decide pixel a pixel si esta dentro del volumen y con eso elige uno
	de los DOS juegos de parametros que trae el vertice. En funcion fija eso son
	el buffer de plantilla y **dos pasadas de la misma geometria**, una recortada
	a fuera y otra a dentro.

	Aca la cuenta de caras va a una imagen que el fragment shader puede leer, y
	entonces la eleccion se hace donde corresponde: dentro del shader, en una
	sola pasada. El juego 1 viaja en las unidades de textura 1, 2 y 3.

	Y con la mascara aparte de la plantilla se puede implementar la instruccion 2
	("cerrar excluyendo"), que con la plantilla era una aproximacion: la region
	afectada es el complemento del volumen del grupo, no lo que sus caras cubren.
*/
int glmoderno_hay_volumen_px(void);

/* Reserva la mascara y el contador de grupo para ese tamano. */
int glmoderno_vol_dimensionar(int ancho, int alto);

/*
	Empieza la marca de una lista. `por_grupo` en 1 --solo si la escena trae
	alguna instruccion 2-- cuenta cada grupo aparte para poder doblarlo con su
	polaridad; en 0 todos suman en la mascara y el shader prueba != 0, que es
	exactamente lo que hacia la plantilla.
*/
void glmoderno_vol_empezar(int ancho, int alto, int por_grupo);

/* Liga (1) o suelta (0) el programa que acumula caras. */
void glmoderno_vol_acumular(int on, int por_grupo);

/* Dobla el grupo contado en la mascara y deja el contador en cero.
   `excluir` es la instruccion 2. */
void glmoderno_vol_plegar(int excluir);

/* Con 1, el plegado prueba cuenta IMPAR --la regla del chip, ciega al sentido
   de giro-- en vez de cuenta != 0. Lo fija graficos.c segun la palanca. */
void glmoderno_vol_paridad(int on);

/* Barrera: la mascara ya se puede leer desde el shader de escena. */
void glmoderno_vol_listo(void);

/* Si la tira que viene consulta la mascara. */
void glmoderno_u_volumen(int on);

/* ------------------------------------------------------------------------ */
/* El buffer de acumulacion secundario del TSP (bits 25 y 24)               */
/* ------------------------------------------------------------------------ */

/*
	El chip lleva DOS buffers de acumulacion por pixel, y cada tira dice con dos
	bits del TSP cual usa como origen y cual como destino de la mezcla. Existe
	--DevBox 3.4.6.1-- para tratar el resultado de superponer varios poligonos
	como si fuera uno solo: se acumula el grupo en el secundario y despues se
	compone de una vez sobre el primario, en vez de mezclar cada poligono contra
	la escena.

	dcemu los registraba desde siempre y **no los consultaba nunca**. El censo
	dice por que se podia: sobre 1,16 millones de tiras de Crazy Taxi en juego,
	las doce demos de control y los nueve juegos, no hay UNA sola tira que
	seleccione el secundario. O sea que no habia con que verificarlo, y por eso
	`demos/acumulador/` existe.

	El secundario es el segundo adjunto de color del FBO. El destino se elige con
	glDrawBuffer y el origen leyendolo como textura desde el shader, que es lo
	unico que la funcion fija no puede hacer.
*/
int  glmoderno_hay_acumulador(void);

/* El secundario arranca en cero en cada escena, como el primario. */
void glmoderno_acum_limpiar(void);

/* Adonde va lo que se dibuja: primario (0) o secundario (1). */
void glmoderno_acum_destino(int secundario);

/* De donde sale el termino "origen" de la mezcla: el fragmento (0) o el
   secundario (1). */
void glmoderno_acum_fuente(int secundario);

#endif /* _GLMODERNO_H_ */
