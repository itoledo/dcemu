/****************************************************************************

	HILO - implementacion sobre SDL. Ver hilo.h para el porque.

	Es el unico archivo del arbol con #ifdef de plataforma por hilos, y el
	unico que habla con SDL_Thread / SDL_mutex / SDL_cond. Cambiar de backend
	--pthreads, Win32, C11 <threads.h>-- es reescribir este archivo y nada mas.

	El conteo de nucleos no sale de SDL: SDL_GetCPUCount es de SDL 2 y aqui la
	API es la 1.2.

*****************************************************************************/

#include <stdlib.h>

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_mutex.h>

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
struct hilo_mtx	{ SDL_mutex * m; };
struct hilo_cond{ SDL_cond * c; };

hilo * hilo_crear(int (* cuerpo)(void *), void * dato, const char * nombre)
{
	hilo * h = (hilo *) malloc(sizeof(hilo));

	(void) nombre;		/* SDL 1.2 no nombra los hilos; SDL 2 si */

	if (h == NULL)
		return NULL;

	h->t = SDL_CreateThread(cuerpo, dato);

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
	SDL_mutexP(m->m);
}

void hilo_mtx_soltar(hilo_mtx * m)
{
	SDL_mutexV(m->m);
}

hilo_cond * hilo_cond_crear(void)
{
	hilo_cond * c = (hilo_cond *) malloc(sizeof(hilo_cond));

	if (c == NULL)
		return NULL;

	c->c = SDL_CreateCond();

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

	SDL_DestroyCond(c->c);
	free(c);
}

void hilo_cond_esperar(hilo_cond * c, hilo_mtx * m)
{
	SDL_CondWait(c->c, m->m);
}

void hilo_cond_avisar(hilo_cond * c)
{
	SDL_CondSignal(c->c);
}

void hilo_cond_avisar_a_todos(hilo_cond * c)
{
	SDL_CondBroadcast(c->c);
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
