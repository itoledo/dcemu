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

/*
	Cuantas escrituras cayeron en una **pagina** con codigo traducido, o sea
	cuantas veces la rejilla fina tuvo que decidir. Es el control de la sonda,
	y no es opcional: sin el, "0 movimientos por escritura" no distingue "el
	guest no escribe sobre su codigo" de "el gancho no esta conectado" -- que
	es exactamente lo que este arbol tuvo durante toda la vida del traductor,
	con el mismo 0 en el resumen de cada corrida.
*/
extern unsigned long long	jit_ep_pag_vista;
extern unsigned			jit_ep_mapeo;
extern unsigned			jit_ep_modo;

/*
	La sonda de la frontera FPU: cuantas veces por corrida cambian PR, SZ o
	«algun Enable» de FPSCR. Es lo que decide si esos bits pueden entrar a la
	clave de validez como entro SR.MD --barato si cambian poco, churn de
	enlaces si cambian a ritmo de matrices-- y hay que medirlo antes de
	disenarlo: la leccion del modo costo 8,2 millones de movimientos.
	El bit FR no se cuenta: el acceso emitido ira por el puntero de banco
	vivo del contexto, asi que conmutarlo no invalida nada.

	**SR.FD es el bit 3 de la clave**, y es la leccion de la compuerta: con
	FD puesto, run() alza 0x800 en el despacho, ANTES de tocar nada -- el
	cambio perezoso de contexto FPU de Windows CE --, y un bloque con filas
	FPU traducido con FD=0 y entrado con FD=1 las ejecutaria directo:
	traduce la direccion del almacenamiento (avance de URC de mas), escribe
	el banco viejo, y el guest pierde su conmutacion. Con FD en la clave la
	entrada se rechaza y el interprete alza el 0x800 al digito. Se recalcula
	donde fpu_deshabilitada se deriva (excepcion_actualizar_vigilancia).
*/
extern int				fpu_deshabilitada;		/* de sh4emu.h, para la clave */
extern unsigned			jit_fpu_visto;
extern unsigned			jit_ep_fpu;

#define JIT_FPSCR_SONDA(fpscr)											\
	do																	\
	{																	\
		if (jit_vigila_codigo)											\
		{																\
			unsigned _jf = ((((fpscr) >> 19) & 3u) << 1)				\
						 | ((((fpscr) & 0x00000F80u) != 0) ? 1u : 0u)	\
						 | (fpu_deshabilitada ? 8u : 0u);				\
																		\
			if (_jf != jit_fpu_visto)									\
			{															\
				jit_fpu_visto = _jf;									\
				jit_ep_fpu++;											\
			}															\
		}																\
	} while (0)

#define JIT_PAG_BIT(ptr)												\
	((unsigned) (((size_t) (ptr)) >> 12) & 0xFFFFu)

/*
	**La segunda rejilla, de 64 bytes**, y existe porque la de paginas no
	alcanza para decidir: dice si la pagina tiene codigo, no si lo escrito ES
	codigo. En Windows CE los datos viven en las mismas paginas de 4 KB que el
	codigo, y el censo de accesos lo midio -- **el 8,4 % de los accesos de
	DCDoom caen en una pagina con codigo traducido**, 90 millones cada 20
	segundos. Mover la epoca en cada uno desataria todos los enlaces noventa
	millones de veces, o sea que la pagina sirve para desviar barato pero no
	para invalidar.

	Toda zona plana vive dentro del mismo bloque de 16 MB (los tres espejos de
	la RAM apuntan ahi), asi que 18 bits de indice no tienen alias. Se mira
	**detras** de la de paginas: en el caso comun ni se toca, y la de paginas
	--4096 entradas utiles-- se queda en L1.
*/
#define JIT_LIN_BIT(ptr)												\
	((unsigned) (((size_t) (ptr)) >> 6) & 0x3FFFFu)

extern unsigned char	jit_lin_codigo[0x40000];

/*
	El contador de escrituras sobre codigo, **aparte de la epoca global**.

	La epoca se mueve por tres motivos --escritura, cambio de mapeo y (por la
	clave) cambio de modo-- y el salto encadenado los necesita los tres,
	porque se saltea el despachador. La verificacion por entrada no: calcula
	el puntero de busqueda y lo compara, y ese puntero ya identifica pagina,
	ASID y modo. Lo unico que le falta es saber si alguien escribio encima, y
	eso es este contador. Ver `epoca_escr` en jit_bloque.
*/
extern unsigned			jit_epoca_escr;

/* DCEMU_JIT_VERIF_COMPLETA=1: el contador de escrituras se mueve tambien con
   el mapeo y el modo, o sea que la verificacion por entrada vuelve a ser tan
   estricta como la clave. Va aqui y no en jit_verificar() porque aquello
   corre por entrada -- mil quinientos millones de veces en un banco -- y
   esto miles. */
extern int				jit_verif_completa;

#define JIT_EPOCA_ESCRITURA()											\
	do																	\
	{																	\
		jit_epoca++;													\
		jit_validez = (jit_epoca << 1) | jit_md_visto;					\
		jit_ep_escritura++;												\
																		\
		/* Con la palanca puesta el contador ES la clave, asi que la		\
		   comparacion del camino caliente reproduce la de antes EXACTA:	\
		   un bloque verificado en un modo y reencontrado en ese mismo	\
		   modo tras un viaje de ida y vuelta vuelve a casar, igual que	\
		   con la clave empaquetada. Subir un contador no haria eso: 	\
		   invalidaria de mas y sobreestimaria la ganancia. */			\
		if (jit_verif_completa)											\
			jit_epoca_escr = jit_validez;								\
		else															\
			jit_epoca_escr++;											\
	} while (0)

/*
	El bloque: una escritura de mas de 8 bytes puede cubrir muchas lineas y
	hasta cruzar de pagina, asi que no alcanza con mirar donde empieza.
	`memwrite_paginado` copia trozos de hasta 1 KB --dieciseis lineas-- y los
	DMA copian mas; mirar cabeza y cola dejaria el medio sin ver, que es el
	mismo agujero que marcar solo la cabeza de un bloque al traducirlo.
	Va fuera de linea porque es el camino de los bloques, no el de un acceso.
*/
int jit_escritura_bloque(const unsigned char * p, size_t tam);

/* Un byte por pagina y no un bit: el codigo emitido tiene que mirarlo en una
   comparacion sola, porque su camino rapido de escritura no pasa por
   memwrite() y si no lo mirara escribiria sin mover la epoca.

   El tamano entra para separar los dos casos, y en todo llamador caliente es
   una constante de compilacion (`sizeof`), asi que la rama se pliega y no
   queda nada. Hasta 8 bytes el acceso del guest va alineado --el error de
   direccion lo filtra antes-- y no cruza limite de 64 ni de pagina; se miran
   igual las dos lineas, porque las escrituras internas entran por aqui sin
   pasar por esa comprobacion. */
#define JIT_ESCRITURA_HOST(ptr, tam)									\
	do																	\
	{																	\
		const unsigned char * _je = (const unsigned char *) (ptr);		\
																		\
		if ((tam) > 8)													\
		{																\
			if (jit_escritura_bloque(_je, (tam)))						\
				JIT_EPOCA_ESCRITURA();									\
		}																\
		else if (jit_pag_codigo[JIT_PAG_BIT(_je)])						\
		{																\
			jit_ep_pag_vista++;											\
																		\
			if (jit_lin_codigo[JIT_LIN_BIT(_je)]						\
			 || jit_lin_codigo[JIT_LIN_BIT(_je + (tam) - 1)])			\
				JIT_EPOCA_ESCRITURA();									\
		}																\
	} while (0)

/* Desde memwrite()/memwrite_fisico(), con la direccion **fisica**: la pagina
   del anfitrion sale de la base de zona. **Va por mem_base_plana y no por la
   de escritura**: esta ultima se pone en NULL para desviar el acceso (el
   watchpoint, el UBC de operandos) y con ella la epoca no se movia justo
   cuando algo estaba desviando -- la escritura ocurre igual, y si la pagina
   tiene codigo traducido hay que invalidarlo igual.
   Sin traductor no se toca nada. */
#define JIT_ESCRITURA(fisica, tam)										\
	do																	\
	{																	\
		if (jit_vigila_codigo)											\
		{																\
			unsigned char * _jb = mem_base_plana[(fisica) >> 24];		\
																		\
			if (_jb)													\
				JIT_ESCRITURA_HOST(_jb + ((fisica) & 0xFFFFFF), (tam));	\
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
																		\
			if (jit_verif_completa)										\
				jit_epoca_escr = jit_validez;										\
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
																		\
			if (jit_verif_completa)										\
				jit_epoca_escr = jit_validez;										\
		}																\
	} while (0)

/*
	El censo del contrato (perf.c, con -DDCEMU_FORMA y --perf).

	Clasifica cada codificacion por **la plantilla que el traductor le
	aplicaria**, que es lo que decide su costo emitido: una fila directa es
	elegible para correr dentro de un tramo recto, una de acceso paga hoy la
	sincronizacion previa, una FPU por envoltorio paga una llamada a C. Sin esto
	la mezcla dinamica del guest solo se podia mirar por mnemonico, que no dice
	nada del costo.

	La clasificacion sale de la MISMA tabla que usa el traductor y del mismo
	oplist, asi que no hay un segundo decodificador que pueda derivar. Se toma el
	modo PR=0/SZ=0: las filas que dependen del modo FPU caen en la clase FPU en
	cualquiera de los cuatro.
*/
#define JIT_CL_SIN			0	/* sin plantilla: corta el bloque */
#define JIT_CL_ALU			1	/* directa: elegible para un tramo recto */
#define JIT_CL_ACCESO		2	/* toca memoria: hoy paga tr_sync antes */
#define JIT_CL_RAMA			3
#define JIT_CL_FPU			4	/* envoltorio con llamada a C */
#define JIT_CL_TERMINAL		5	/* el bloque termina en ella */
#define JIT_CL_MANEJADOR	6	/* se traduce llamando al manejador real */
#define JIT_CL_N				7

/* Tabla de 65536 clases, construida al primer uso. NULL si no hay sitio. */
const unsigned char * jit_clases(void);

/* El informe, con el histograma de ejecuciones por codificacion. */
void jit_censo_contrato(const unsigned long long * histo,
	unsigned long long total);

/* El resumen del traductor, llamado desde la secuencia de salida de main.c. */
void jit_resumen(void);

#else	/* sin traductor compilado no hay nada que vigilar */

/*
	El arbol se compila sin -DDCEMU_JIT, y entonces esto tiene que desaparecer
	entero: mem.h y mmu.c llaman a los ganchos en sus caminos mas calientes.
*/
#define jit_vigila_codigo			0
#define JIT_ESCRITURA(fisica, tam)		do { } while (0)
#define JIT_ESCRITURA_HOST(ptr, tam)	do { } while (0)
#define JIT_EPOCA_MAPEO()		do { } while (0)
#define JIT_EPOCA_MODO(md)		do { } while (0)
#define JIT_FPSCR_SONDA(fpscr)	do { } while (0)

#endif /* DCEMU_JIT */

void jit_iniciar(void);

/* Cuantas traducciones lleva la corrida: la sonda de tirones (perf.h) la mira
   por cuadro para separar «el JIT tradujo de golpe» de los demas culpables. */
unsigned long long jit_cuenta_traducidos(void);

/* Y cuanto tiempo se fue traduciendo, sin muestrear. */
extern unsigned long long jit_ns_traducir;

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
