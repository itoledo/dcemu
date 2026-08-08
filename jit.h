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

/* ------------------------------------------------------------------------ */
/* La epoca del codigo traducido                                            */
/* ------------------------------------------------------------------------ */

/*
	**Lo que hace seguro saltar de un bloque a otro sin volver a C.**

	Un bloque traducido vale mientras su codigo sea el mismo y su pagina siga
	mapeada donde estaba. Verificarlo palabra por palabra en cada entrada es lo
	que hacia el traductor, y es lo que un salto directo se saltearia. En vez de
	eso hay una **epoca global**: se mueve cuando cambia el mapeo --las dos
	invalidaciones de la MMU-- y cuando alguien escribe sobre una pagina que
	tiene codigo traducido. Un bloque que guarda la epoca con la que se verifico
	entero sigue siendo valido mientras la epoca no se mueva, y eso es **una
	comparacion**.

	El interruptor `jit_vigila_codigo` esta en cero salvo con el traductor
	encendido, asi que en el binario del arbol el gancho de escritura es una
	comparacion contra cero sobre una global caliente. Con el traductor puesto,
	la prueba adicional es un bit de un mapa de 8 KB indexado por la pagina
	**del anfitrion**: asi sirve igual para las escrituras del guest y para las
	internas (DMA del GD-ROM cargando un overlay), que llegan con la direccion
	fisica y no con la virtual.

	Los alias del mapa --dos paginas distintas que caen en el mismo bit-- solo
	provocan un movimiento de epoca de mas, o sea una verificacion completa de
	mas. Nunca lo contrario.
*/
#ifdef DCEMU_JIT

extern int				jit_vigila_codigo;
extern unsigned			jit_epoca;
extern unsigned char	jit_pag_codigo[0x10000];

/*
	**La clave de validez**, que es contra lo que compara el salto encadenado:
	la epoca y el modo en una sola palabra, `(epoca << 1) | MD`.

	El modo no puede mover la epoca. Se probo y se midio: Windows CE entra y
	sale de modo privilegiado **8 197 860 veces en 35 segundos emulados**, y
	moviendo la epoca cada vez se desataban TODOS los enlaces ocho millones de
	veces -- una cada 43 entradas al despacho. Metiendo el modo en la clave, un
	cambio de modo solo invalida los bloques verificados en el otro modo, que es
	lo que corresponde: los de este siguen valiendo.
*/
extern unsigned			jit_validez;
extern unsigned			jit_md_visto;

/* De donde salio cada movimiento de epoca. Solo para el resumen: sin saber cual
   de las tres fuentes manda, «la época se mueve mucho» no dice qué arreglar. */
extern unsigned			jit_ep_escritura;
extern unsigned			jit_ep_mapeo;
extern unsigned			jit_ep_modo;

#define JIT_PAG_BIT(ptr)												\
	((unsigned) (((size_t) (ptr)) >> 12) & 0xFFFFu)

/* Un byte por pagina y no un bit: el codigo emitido tiene que mirarlo en una
   comparacion sola, porque su camino rapido de escritura no pasa por
   memwrite() y si no lo mirara escribiria sin mover la epoca. */
#define JIT_ESCRITURA_HOST(ptr)											\
	do																	\
	{																	\
		if (jit_pag_codigo[JIT_PAG_BIT(ptr)])							\
		{																\
			jit_epoca++;												\
			jit_validez = (jit_epoca << 1) | jit_md_visto;				\
			jit_ep_escritura++;											\
		}																\
	} while (0)

/* Desde memwrite()/memwrite_fisico(), con la direccion **fisica**: la pagina
   del anfitrion sale de la base de zona, que es la misma que usa la escritura.
   Sin traductor no se toca nada. */
#define JIT_ESCRITURA(fisica)											\
	do																	\
	{																	\
		if (jit_vigila_codigo)											\
		{																\
			unsigned char * _jb = mem_base_escritura[(fisica) >> 24];	\
																		\
			if (_jb)													\
				JIT_ESCRITURA_HOST(_jb + ((fisica) & 0xFFFFFF));			\
		}																\
	} while (0)

/* Un cambio de mapeo invalida todo: lo llaman las dos invalidaciones de la
   MMU. Los bloques no dejan de valer, pero hay que volver a comprobarlos. */
#define JIT_EPOCA_MAPEO()												\
	do																	\
	{																	\
		if (jit_vigila_codigo)											\
		{																\
			jit_epoca++;												\
			jit_validez = (jit_epoca << 1) | jit_md_visto;				\
			jit_ep_mapeo++;												\
		}																\
	} while (0)

/*
	SR.MD tambien cambia el mapeo -- la misma virtual traduce distinto en modo
	usuario y en privilegiado --, pero **no mueve la epoca**: entra en la clave.
	Asi un cambio de modo solo invalida los bloques verificados en el otro modo,
	y los de este siguen valiendo. Ver `jit_validez` arriba.
*/
#define JIT_EPOCA_MODO(md)												\
	do																	\
	{																	\
		if (jit_vigila_codigo && jit_md_visto != (unsigned) (md))		\
		{																\
			jit_md_visto = (unsigned) (md);								\
			jit_validez  = (jit_epoca << 1) | jit_md_visto;				\
			jit_ep_modo++;												\
		}																\
	} while (0)

#else	/* sin traductor compilado no hay nada que vigilar */

/*
	El arbol se compila sin -DDCEMU_JIT, y entonces esto tiene que desaparecer
	entero: mem.h y mmu.c llaman a los ganchos en sus caminos mas calientes.
*/
#define jit_vigila_codigo		0
#define JIT_ESCRITURA(fisica)	do { } while (0)
#define JIT_ESCRITURA_HOST(ptr)	do { } while (0)
#define JIT_EPOCA_MAPEO()		do { } while (0)
#define JIT_EPOCA_MODO(md)		do { } while (0)

#endif /* DCEMU_JIT */

void jit_iniciar(void);

/*
	El muestreo de candidatos. Se llama desde el bloque periodico de
	main_loop() --que corre cada RELOJ_GRANO ciclos, o sea unas 130
	instrucciones--, asi que no cuesta nada en el camino caliente. Un PC visto
	varias veces se marca en el mapa, y la proxima vez que el despacho lo vea
	se traduce. Sin traductor (DCEMU_JIT=1) no hace nada.
*/
void jit_muestrear(DWORD pc);

/*
	Corre el bloque traducido cuya entrada es `pc`. Devuelve 1 si corrio (PC,
	ciclos y registros ya avanzados: main_loop() sigue derecho al bloque
	periodico) y 0 si no hay bloque, o si la verificacion de sus palabras
	fallo y no se toco nada.
*/
int jit_despachar(DWORD pc);

#endif /* _JIT_H_ */
