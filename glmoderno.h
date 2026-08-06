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

#endif /* _GLMODERNO_H_ */
