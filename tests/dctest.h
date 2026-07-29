/****************************************************************************

	DCTEST - micro framework de pruebas unitarias para dcemu

	Sin dependencias externas: ni SDL, ni OpenGL, ni bibliotecas de terceros.
	Cada archivo de pruebas exporta una dc_suite; tests/principal.c las enumera
	y el runner las recorre.

	Las aserciones son "blandas": la que falla registra la diferencia y el caso
	sigue corriendo, para que un caso reporte todas sus diferencias y no solo
	la primera. Un caso pasa si no registro ninguna.

	Un caso marcado con CASO_XFAIL documenta una desviacion conocida del
	emulador respecto del manual del SH-4: se espera que falle. Si empieza a
	pasar, el runner lo reporta como XPASS y termina con error, para que la
	nota no quede obsoleta cuando alguien arregle el opcode.

*****************************************************************************/

#ifndef _DCTEST_H_
#define _DCTEST_H_

#include <stddef.h>

#define DC_NORMAL	0	/* caso que debe pasar */
#define DC_XFAIL	1	/* desviacion conocida: debe fallar */

typedef void dc_caso_f(void);

typedef struct
{
	const char *	nombre;
	dc_caso_f *		funcion;
	int				tipo;
	const char *	nota;	/* por que se espera la falla (solo XFAIL) */
} dc_caso;

typedef struct
{
	const char *		nombre;
	const dc_caso *		casos;
	int					cantidad;
} dc_suite;

#define CASO(f)					{ #f, f, DC_NORMAL, NULL }
#define CASO_XFAIL(f, nota)		{ #f, f, DC_XFAIL, nota }
#define DEFINIR_SUITE(nom, arr)	{ nom, arr, (int) (sizeof(arr) / sizeof((arr)[0])) }

/* ------------------------------------------------------------------------ */
/* Aserciones                                                               */
/* ------------------------------------------------------------------------ */

void dc_cmp_u32(const char * arch, int linea, const char * expr,
				unsigned int obtenido, unsigned int esperado);
void dc_cmp_i32(const char * arch, int linea, const char * expr,
				int obtenido, int esperado);
void dc_cmp_f32(const char * arch, int linea, const char * expr,
				float obtenido, float esperado, float tolerancia);
void dc_cmp_f64(const char * arch, int linea, const char * expr,
				double obtenido, double esperado, double tolerancia);
void dc_cmp_bytes(const char * arch, int linea, const char * expr,
				  const void * obtenido, const void * esperado, size_t n);
void dc_verificar(const char * arch, int linea, const char * expr, int cond);
void dc_anotar(const char * arch, int linea, const char * fmt, ...);

#define ESPERAR_U32(expr, esperado) \
	dc_cmp_u32(__FILE__, __LINE__, #expr, (unsigned int) (expr), (unsigned int) (esperado))

#define ESPERAR_I32(expr, esperado) \
	dc_cmp_i32(__FILE__, __LINE__, #expr, (int) (expr), (int) (esperado))

#define ESPERAR_F32(expr, esperado) \
	dc_cmp_f32(__FILE__, __LINE__, #expr, (float) (expr), (float) (esperado), 0.0f)

#define ESPERAR_F32_APROX(expr, esperado, tol) \
	dc_cmp_f32(__FILE__, __LINE__, #expr, (float) (expr), (float) (esperado), (float) (tol))

#define ESPERAR_F64(expr, esperado) \
	dc_cmp_f64(__FILE__, __LINE__, #expr, (double) (expr), (double) (esperado), 0.0)

#define ESPERAR_F64_APROX(expr, esperado, tol) \
	dc_cmp_f64(__FILE__, __LINE__, #expr, (double) (expr), (double) (esperado), (double) (tol))

#define ESPERAR_BYTES(obt, esp, n) \
	dc_cmp_bytes(__FILE__, __LINE__, #obt, (obt), (esp), (n))

#define ESPERAR(cond) \
	dc_verificar(__FILE__, __LINE__, #cond, (cond) ? 1 : 0)

/* ------------------------------------------------------------------------ */
/* Runner                                                                   */
/* ------------------------------------------------------------------------ */

/* Lo llama el arnes antes de cada caso; reinicia el contador de diferencias. */
void dc_empezar_caso(void);
int  dc_fallas_del_caso(void);

/* 1 si la corrida no lleva filtros, es decir si se van a ejecutar todas las
   suites. La suite de cobertura solo tiene sentido en ese caso. */
int dc_corrida_completa(void);

int dc_correr(const dc_suite * const * suites, int cantidad,
			  int argc, char ** argv);

#endif /* _DCTEST_H_ */
