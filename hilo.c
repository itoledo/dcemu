/****************************************************************************

	HILO - implementacion sobre SDL. Ver hilo.h para el porque.

	Es el unico archivo del arbol con #ifdef de plataforma por hilos, y el
	unico que habla con SDL_Thread / SDL_Mutex / SDL_Condition. Cambiar de
	backend --pthreads, Win32, C11 <threads.h>-- es reescribir este archivo y
	nada mas.

	El conteo de nucleos se queda en el sistema (GetSystemInfo / sysconf) y no
	en SDL_GetNumLogicalCPUCores: cuenta lo mismo y asi esta funcion no cambia
	de respuesta con la version de SDL.

*****************************************************************************/

#include <stdlib.h>

#include <SDL3/SDL.h>

#include "hilo.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

/*
	Los tipos son punteros opacos hacia afuera. Se envuelven en lugar de hacer
	`typedef SDL_Thread hilo` para que hilo.h no tenga que incluir SDL: si lo
	incluyera, cualquier archivo que use un mutex arrastraria SDL de vuelta y
	esta capa no serviria de nada.
*/
struct hilo		{ SDL_Thread * t; };
struct hilo_mtx	{ SDL_Mutex * m; };
struct hilo_cond{ SDL_Condition * c; };

hilo * hilo_crear(int (* cuerpo)(void *), void * dato, const char * nombre)
{
	hilo * h = (hilo *) malloc(sizeof(hilo));

	if (h == NULL)
		return NULL;

	/* SDL3 nombra los hilos: es lo que el depurador muestra. */
	h->t = SDL_CreateThread(cuerpo, nombre, dato);

	if (h->t == NULL)
	{
		free(h);
		return NULL;
	}

	return h;
}

void hilo_esperar(hilo * h)
{
	if (h == NULL)
		return;

	SDL_WaitThread(h->t, NULL);
	free(h);
}

hilo_mtx * hilo_mtx_crear(void)
{
	hilo_mtx * m = (hilo_mtx *) malloc(sizeof(hilo_mtx));

	if (m == NULL)
		return NULL;

	m->m = SDL_CreateMutex();

	if (m->m == NULL)
	{
		free(m);
		return NULL;
	}

	return m;
}

void hilo_mtx_destruir(hilo_mtx * m)
{
	if (m == NULL)
		return;

	SDL_DestroyMutex(m->m);
	free(m);
}

void hilo_mtx_tomar(hilo_mtx * m)
{
	SDL_LockMutex(m->m);
}

void hilo_mtx_soltar(hilo_mtx * m)
{
	SDL_UnlockMutex(m->m);
}

hilo_cond * hilo_cond_crear(void)
{
	hilo_cond * c = (hilo_cond *) malloc(sizeof(hilo_cond));

	if (c == NULL)
		return NULL;

	c->c = SDL_CreateCondition();

	if (c->c == NULL)
	{
		free(c);
		return NULL;
	}

	return c;
}

void hilo_cond_destruir(hilo_cond * c)
{
	if (c == NULL)
		return;

	SDL_DestroyCondition(c->c);
	free(c);
}

void hilo_cond_esperar(hilo_cond * c, hilo_mtx * m)
{
	SDL_WaitCondition(c->c, m->m);
}

void hilo_cond_avisar(hilo_cond * c)
{
	SDL_SignalCondition(c->c);
}

void hilo_cond_avisar_a_todos(hilo_cond * c)
{
	SDL_BroadcastCondition(c->c);
}

int hilo_nucleos(void)
{
#ifdef _WIN32
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	return (si.dwNumberOfProcessors > 0) ? (int) si.dwNumberOfProcessors : 1;
#else
	long n = sysconf(_SC_NPROCESSORS_ONLN);

	return (n > 0) ? (int) n : 1;
#endif
}

void hilo_prioridad_alta_propia(void)
{
#ifdef _WIN32
	/* HIGHEST y no TIME_CRITICAL: lo segundo pasa por encima de los hilos del
	   sistema y puede colgar la interfaz; lo primero basta para que el
	   planificador lo elija antes al despertarlo. */
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#else
	/* pthread_setschedparam pide politicas y permisos que cambian por sistema;
	   hasta que alguien lo mida en uno, aqui no se hace nada. */
#endif
}
