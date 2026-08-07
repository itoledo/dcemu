/****************************************************************************

	JIT - el recompilador dinamico, fase 0

	docs/recompilador-plan.md, fase 0: "el emisor minimo y el cache; los dos
	bloques de fusion.c emitidos a mano por el JIT (traduce esos PC y nada
	mas)". La prueba de aceptacion es reproducir los A/B de las sondas -- los
	mismos numeros y la ejecucion identica al digito --, y lo que decide es que
	el emisor, el cache, la convencion de llamada, los ayudantes de memoria y
	la semantica de reejecucion existen y no mienten.

	**No hay traductor todavia.** Los dos bloques se emiten a mano contra la
	API de jit_x64.c, instruccion por instruccion, copiando de fusion.c -- que
	a su vez copio de los manejadores reales. El traductor automatico por
	identidad de manejador es la fase 1.

	Todo lo que las sondas establecieron sigue valiendo aca y es lo que este
	codigo tiene que respetar:

	  - Los cortes del bloque periodico caen en cada frontera de instruccion,
		y la salida deja el PC en la siguiente.
	  - Los ciclos son los del manejador de cada instruccion, rarezas
		incluidas (mov3 no suma, el NOP de una ranura tampoco).
	  - Antes de cada acceso a memoria se vuelca al contexto lo que el bloque
		mutó, los ciclos y el PC de esa instruccion, con la instantanea ya
		invalidada: una falta sale por longjmp con el contexto en el estado
		pre-instruccion exacto. El intento se cuenta **antes** del acceso.
	  - La memoria es la de siempre: el codigo emitido llama a jit_leer32() y
		companiia, que envuelven los macros reales de mem.h.
	  - Las palabras del bloque se verifican enteras en cada entrada.
	  - La traza, el UBC y el modo de depuracion apagan el JIT (lo decide
		main.c en el sitio de despacho).

	Como fusion.c y por el mismo motivo: vive detras de -DDCEMU_JIT y dentro
	del binario lo enciende DCEMU_JIT=1, para que el A/B corra sobre una sola
	imagen y la disposicion del binario no entre como variable.

*****************************************************************************/

#ifndef _JIT_H_
#define _JIT_H_

#include "lnxdefs.h"

/* Las dos entradas de la fase 0, las mismas que fusion.h. */
#define JIT_CT_ENTRADA	0x0C1583F8ul	/* el lazo de espera de Crazy Taxi */
#define JIT_CE_ENTRADA	0x0002EF3Eul	/* el blit de columnas de DOOM, con MMU */

extern int jit_activo;

/*
	El primer filtro del despacho: un mapa de bits de 8 KB indexado por
	(PC >> 1) & 0xFFFF. Una carga y una prueba de bit; solo con el bit puesto
	se paga la busqueda real. La vara esta puesta por el antecedente del cache
	de bloques (-3,3 % en los menus), asi que el despacho se mide tambien ahi.
*/
extern unsigned char jit_mapa[8192];

#define JIT_MARCADO(pc)													\
	(jit_mapa[(((pc) >> 1) & 0xFFFFu) >> 3] & (1u << ((((pc) >> 1)) & 7u)))

void jit_iniciar(void);

/*
	Corre el bloque traducido cuya entrada es `pc`. Devuelve 1 si corrio (PC,
	ciclos y registros ya avanzados: main_loop() sigue derecho al bloque
	periodico) y 0 si no hay bloque, o si la verificacion de sus palabras
	fallo y no se toco nada.
*/
int jit_despachar(DWORD pc);

#endif /* _JIT_H_ */
