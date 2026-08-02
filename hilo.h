/****************************************************************************

	HILO - la capa de hilos, mutex y variables de condicion

	El emulador no habla con la API de hilos del sistema ni con la de SDL: habla
	con esta. Ver docs/hilos-plan.md, "La capa de portabilidad".

	**Por que no Boost.** Es una dependencia nueva que hay que compilar por
	plataforma, en un arbol que dedico un plan entero (docs/msvc-build-plan.md) a
	sacarse de encima las dependencias de 2004-2005. Es C++, y arrastraria a C++
	justamente a los modulos que tests/ enlaza. Y no hace falta: lo unico que se
	necesita son hilos, exclusion y espera con condicion, que es la interseccion
	de todas las APIs de hilos que existen.

	**Por que SDL.** Ya es dependencia dura en Windows, Linux y macOS, compila
	con MSVC y con GCC, y provee las tres cosas. En la ruta de Windows es
	sdl12-compat sobre SDL2 sobre SDL3, asi que por debajo son hilos modernos del
	sistema. main.c:1385 tiene un SDL_CreateThread comentado de 2005: la idea ya
	estaba.

	**Por que igual detras de una capa.** Tres razones, y la primera manda:

	  1. aica.c, arm7.c, g2dma.c, ta.c, vram.c y sistema.c estan libres de SDL a
	     proposito, para que tests/ los enlace de verdad (tests/CMakeLists.txt no
	     enlaza ni una funcion de SDL). Un SDL_mutexP dentro de aica.c se lleva
	     puestas las suites aica, arm7 y g2dma. Con esta capa, el protocolo vive
	     en hilo_aica.c y aica.c no se entera de que hay hilos.
	  2. La cadena SDL -> sdl12-compat -> SDL2 -> sdl2-compat -> SDL3 es fragil.
	     Cambiar a pthreads/Win32 o a C11 <threads.h> es **un archivo**.
	  3. hilo.c es el unico sitio donde puede haber #ifdef de plataforma.

	**Sin atomicos, a proposito.** SDL 1.2 no tiene, y C11 <stdatomic.h> es
	solido en GCC/Clang pero reciente y con banderas en MSVC. El diseno de la
	fase 1 esta hecho para no necesitarlos: los puntos de sincronizacion son
	poco frecuentes y un mutex alcanza. El unico dato que cruza sin mutex es el
	anillo aica_salida[], que ya funciona asi hoy con indices volatiles y un
	productor y un consumidor.

*****************************************************************************/

#ifndef _HILO_H_
#define _HILO_H_

typedef struct hilo			hilo;
typedef struct hilo_mtx		hilo_mtx;
typedef struct hilo_cond	hilo_cond;

/*
	Un hilo. El cuerpo devuelve un entero, como el de SDL y como main(); el
	nombre es solo para depurar y puede ignorarse.

	Devuelve NULL si no se pudo crear, y el llamador tiene que poder seguir sin
	el hilo: que no haya hilos disponibles no es motivo para no arrancar el
	emulador.
*/
hilo *	hilo_crear(int (* cuerpo)(void *), void * dato, const char * nombre);

/* Espera a que termine y libera. Con NULL no hace nada. */
void	hilo_esperar(hilo * h);

hilo_mtx *	hilo_mtx_crear(void);
void		hilo_mtx_destruir(hilo_mtx * m);
void		hilo_mtx_tomar(hilo_mtx * m);
void		hilo_mtx_soltar(hilo_mtx * m);

hilo_cond *	hilo_cond_crear(void);
void		hilo_cond_destruir(hilo_cond * c);

/* Suelta el mutex, espera, y lo vuelve a tomar antes de volver. El mutex tiene
   que estar tomado por quien llama. */
void		hilo_cond_esperar(hilo_cond * c, hilo_mtx * m);

void		hilo_cond_avisar(hilo_cond * c);
void		hilo_cond_avisar_a_todos(hilo_cond * c);

/* Cuantos hilos de ejecucion tiene la maquina. Al menos 1. */
int		hilo_nucleos(void);

#endif /* _HILO_H_ */
