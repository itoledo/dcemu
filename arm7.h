/****************************************************************************

	ARM7DI - el procesador de sonido que lleva el AICA adentro

	Es un ARMv3, y conviene tener presente lo que **no** tiene, porque es la
	mitad de lo que hace que este nucleo sea chico:

	  - no hay Thumb (llego con el ARM7TDMI);
	  - no hay LDRH/STRH/LDRSB/LDRSH: las transferencias de media palabra son
	    de ARMv4, y aqui esos patrones son instrucciones indefinidas;
	  - no hay BX -- por eso KOS compila el firmware con --fix-v4bx;
	  - no hay MMU, ni cache, ni coprocesadores, ni multiplicacion larga.

	Queda: proceso de datos, MRS/MSR, MUL/MLA, SWP, LDR/STR, LDM/STM, B/BL y
	SWI. Veinte filas en la tabla.

	El espacio de direcciones que ve es el de la tabla 4-8 del documento de
	arquitectura, y nada mas:

	  0x00000000-0x001FFFFF   los 2 MB de RAM de onda
	  0x00800000-0x00807FFF   sus propios registros

	Corre a 22,5792 MHz, que son **512 ciclos por muestra** exactos
	(22579200 = 44100 x 512). Por eso no necesita reloj propio: aica_tick()
	cuenta muestras y le pasa 512 ciclos por cada una.

	**Lo ejecutan las 135 demos, no las siete de sonido.** spu_init() de KOS
	escribe 0xEAFFFFF8 -- un salto a si mismo -- en la direccion 0 y suelta el
	reset en todos los programas, y el boot ROM hace lo suyo tres veces antes
	de llegar al menu. De ahi que aqui importe tanto que no cueste nada cuando
	no hace nada.

	Ver docs/aica-plan.md, fase 3.

*****************************************************************************/

#ifndef _ARM7_H_
#define _ARM7_H_

/* Los seis modos que existen en ARMv3, en el valor que llevan en CPSR[4:0]. */
#define ARM7_MODO_USR	0x10
#define ARM7_MODO_FIQ	0x11
#define ARM7_MODO_IRQ	0x12
#define ARM7_MODO_SVC	0x13
#define ARM7_MODO_ABT	0x17
#define ARM7_MODO_UND	0x1B
#define ARM7_MODO_SYS	0x1F

/* Banderas de CPSR. */
#define ARM7_N		0x80000000u
#define ARM7_Z		0x40000000u
#define ARM7_C		0x20000000u
#define ARM7_V		0x10000000u
#define ARM7_I		0x00000080u		/* IRQ enmascarada */
#define ARM7_F		0x00000040u		/* FIQ enmascarada */
#define ARM7_MODO	0x0000001Fu

/* Los vectores, en el orden del manual. */
#define ARM7_VEC_RESET		0x00
#define ARM7_VEC_UNDEF		0x04
#define ARM7_VEC_SWI		0x08
#define ARM7_VEC_PABORT		0x0C
#define ARM7_VEC_DABORT		0x10
#define ARM7_VEC_IRQ		0x18
#define ARM7_VEC_FIQ		0x1C

/* Los bancos que se guardan cuando el modo cambia. */
enum { ARM7_B_USR, ARM7_B_FIQ, ARM7_B_IRQ, ARM7_B_SVC, ARM7_B_ABT, ARM7_B_UND,
       ARM7_BANCOS };

/*
	Memoizacion de los barridos de sondeo. El porque y las reglas estan en
	arm7.c, junto al codigo; aca solo lo que cruza de archivo.

	arm7_memo_fin es el centinela del camino caliente: vale ~0 cuando no se esta
	grabando ningun barrido, y el PC del salto de atras cuando si.
*/
extern DWORD	arm7_memo_fin;
extern int		arm7_memo_apagada;			/* DCEMU_SIN_MEMO_ARM */

extern unsigned long long arm7_memo_aciertos;
extern unsigned long long arm7_memo_pasos;		/* instrucciones no ejecutadas */
extern unsigned long long arm7_memo_grabados;
extern unsigned long long arm7_memo_abortados;
extern unsigned long long arm7_memo_sucios;

/* Por que se abandono una grabacion. Es diagnostico, no control: la primera
   version de este mecanismo repuso **cero** barridos y sin este desglose no
   habia forma de saber cual de los seis motivos lo estaba matando. */
enum { ARM7_MEMO_ESCRITURA, ARM7_MEMO_REGISTRO, ARM7_MEMO_PC,
       ARM7_MEMO_EXCEPCION, ARM7_MEMO_ANIDADO, ARM7_MEMO_LARGO,
       ARM7_MEMO_PAGS, ARM7_MEMO_MOTIVOS };

extern unsigned long long arm7_memo_motivo[ARM7_MEMO_MOTIVOS];
extern const char * const arm7_memo_motivo_nombre[ARM7_MEMO_MOTIVOS];

/*
	El aborto se llama desde caminos calientes -- arm7_escribir() corre en cada
	escritura del ARM, y son decenas de millones -- asi que el centinela se mira
	**antes de llamar**. La primera version llamaba siempre y dejaba que la
	funcion volviera enseguida: una llamada por escritura, pagada tambien con el
	mecanismo apagado, o sea que ni siquiera aparecia en el A/B.
*/
void arm7_memo_abortar_real(int motivo);

#define arm7_memo_abortar_por(motivo)								\
	do {															\
		if (arm7_memo_fin != ~0u)									\
			arm7_memo_abortar_real(motivo);							\
	} while (0)
void arm7_memo_pagina(DWORD direccion);
void arm7_memo_terminar(void);
void arm7_memo_reset(void);

struct arm7_estado
{
	DWORD	r[16];					/* el banco activo; r[15] es el PC */
	DWORD	cpsr;
	DWORD	spsr;					/* el del modo activo */

	DWORD	r8_12_usr[5];			/* R8-R12 fuera de FIQ */
	DWORD	r8_12_fiq[5];
	DWORD	r13_14[ARM7_BANCOS][2];	/* R13 y R14 por banco */
	DWORD	spsr_banco[ARM7_BANCOS];

	int		banco;					/* cual esta cargado en r[] */

	/* Deuda de ciclos: aica_tick() la acredita y arm7_ejecutar() la gasta. */
	long	ciclos;

	/* Cuentas para la traza. */
	unsigned long long instrucciones;
	unsigned long		indefinidas;
};

extern struct arm7_estado arm7;

/*
	El perfil del propio ARM, con DCEMU_PERFIL_ARM=1 en el ambiente.

	Existe porque la unica cifra que habia --perf_arm_ocioso-- contesta una
	pregunta demasiado estrecha: detecta el salto a si mismo, que es como espera
	el firmware de KOS, y da cero contra un juego. Cero ahi no significa "esta
	trabajando": significa "no espera de ESA forma". Un lazo de tres
	instrucciones sondeando un registro tampoco mueve esa cifra y es igual de
	saltable.

	Lo que hace falta para decidir entre acelerar el interprete y no ejecutarlo
	es saber **donde** esta el PC y **que** ejecuta. Dos histogramas: uno por
	direccion (que dice si unas pocas concentran todo, o sea un lazo) y otro por
	fila de la tabla de despacho (que dice si el peso esta en el acceso a
	memoria, en la ALU o en los saltos).

	Apagado no cuesta nada: una comparacion contra cero por paso, la misma clase
	que el resto de los instrumentos del arbol.
*/
extern int arm7_perfil;

void arm7_perfil_inicio(void);
void arm7_perfil_resumen(void);

/* Construye la tabla de despacho. Se llama una vez, como initopcodes(). */
void arm7_init(void);

/* Estado de encendido: PC en 0, modo supervisor, las dos mascaras puestas. */
void arm7_reset(void);

/* Ejecuta hasta gastar la deuda de ciclos. No hace nada si el ARM esta en
   reset (ARMRST) o si no hay deuda. */
void arm7_ejecutar(long ciclos);

/* Una sola instruccion, para las pruebas. Devuelve los ciclos que costo. */
int arm7_paso(void);

/* Los accesos a memoria tal como los ve el ARM, expuestos para las pruebas. */
DWORD arm7_leer(DWORD direccion, int tam);
void  arm7_escribir(DWORD direccion, int tam, DWORD valor);

/*
	La entrada de la predecodificacion, publica porque el traductor a x64
	(arm7jit.c) emite a partir de ella: la palabra cruda, los campos ya
	extraidos y el manejador especializado. Que significa cada campo depende
	de la forma -- ver arm7_decodificar() en arm7.c, que es el unico que los
	llena.
*/
struct arm7_deco_s
{
	DWORD			palabra;			/* de que palabra se decodifico */
	unsigned char	cond;
	unsigned char	b0, b1, b2;			/* campos chicos, por forma */
	DWORD			imm;				/* inmediato / desplazador / lista */
	DWORD			imm2;				/* segundo inmediato (mascara de MSR) */
	void		 (* fn)(const struct arm7_deco_s * e);
};

typedef struct arm7_deco_s arm7_deco;

/*
	La forma de una entrada, para quien no puede comparar los manejadores --
	que son estaticos de arm7.c a proposito. El traductor elige plantilla por
	esto; ARM7_DF_OTRA es "llama al manejador por e->fn".
*/
enum
{
	ARM7_DF_OTRA,
	ARM7_DF_ALU_IMM_S0, ARM7_DF_ALU_IMM_S1,
	ARM7_DF_ALU_REG_S0, ARM7_DF_ALU_REG_S1,
	ARM7_DF_LDR_IMM, ARM7_DF_STR_IMM,
	ARM7_DF_BLOQUE, ARM7_DF_MRS,

	/* Las dos formas anchas que tocan memoria: el emisor las llama por el
	   manejador, pero necesita saber que pueden caer en el archivo de
	   registros -- el mismo trato que ARM7_DF_BLOQUE. */
	ARM7_DF_LDR_REG, ARM7_DF_STR_REG
};

int arm7_deco_forma(const arm7_deco * e);

/*
	Lo que el codigo emitido necesita tocar por direccion absoluta: el costo
	de la instruccion en curso (los manejadores lo suman sobre 1), la marca de
	"el acceso cayo en el archivo de registros" (la salida lateral de los
	bloques) y las instrucciones que el ultimo bloque ejecuto.
*/
extern int arm7_ciclos_op;
extern int arm7_toco_reg;
extern int arm7_blq_ult_pasos;

/*
	El encadenado emitido: lo que el emisor necesita para emitir la cola B/BL
	de un bloque y el salto directo al sucesor, sin volver al lazo en C por
	cada salto (el corredor cruzaba al C cada 3,4 pasos). El slot destino es
	opaco -- arm7_blq es privado de arm7.c -- asi que viajan su direccion y
	los desplazamientos de los campos que la cadena consulta.
*/
typedef struct
{
	void *	slot;			/* &arm7_blqs[indice del destino] */
	DWORD	base;			/* la base que ese slot debe tener */
} arm7_enlace;

typedef struct
{
	/* Desplazamientos dentro de arm7_blq (comunes a los dos enlaces). */
	int		off_base, off_n, off_ciclos_max;
	int		off_verif0, off_verif1;
	int		off_pgen0, off_pgen1;
	int		off_cadena;

	/* La cola, ya decodificada. */
	DWORD	pc_cola;		/* direccion de bus de la palabra del salto */
	unsigned cond;			/* palabra >> 28 (0xE = siempre, 0xF = nunca) */
	int		bl;				/* 1: escribe r14 = pc_cola + 4 */
	int		atras;			/* 1: B hacia atras sin BL -> pasa por memo_borde */
	DWORD	destino;		/* (pc_cola + imm) & ARM7_BUS, constante */

	arm7_enlace	salto;		/* el slot del destino */
	arm7_enlace	caida;		/* el slot de pc_cola + 4 (condicion no cumplida) */
} arm7_cola_emitir;

/* El borde de la memoizacion y la contabilidad de "el salto arranco una
   grabacion": los llama tambien el codigo emitido (la cola emitida reproduce
   d_salto y el epilogo del lazo en C paso por paso). */
int  arm7_memo_borde(DWORD destino, DWORD pc_salto);
void arm7_memo_cola_contabilizar(int ciclos);

/*
	Instala el traductor de bloques: `emitir` recibe las entradas ya
	decodificadas de un bloque recto (sin PC ni modo, ver arm7_blq_cabe) y
	devuelve un puntero a codigo `int fn(void)` que lo ejecuta entero --
	devuelve los ciclos NO comprometidos y ACUMULA en arm7_blq_ult_pasos los
	pasos (el que llama lo pone en cero; la semantica es += porque el
	encadenado emitido corre varios bloques por llamada) -- o NULL para
	dejarle ese bloque al lazo en C. `dir` es la direccion de bus de la
	primera palabra, con la garantia arm7.r[15] == dir a la entrada.

	`cola` no nulo pide ademas el epilogo del encadenado (compromiso de
	ciclos, la cola B/BL y el salto directo al sucesor); `cadena` devuelve la
	entrada interna post-prologo, que es a la que saltan los encadenados de
	otros bloques.
*/
void arm7_blq_instalar_emisor(void * (* emitir)(const arm7_deco * entradas,
                                                int n, DWORD dir,
                                                const arm7_cola_emitir * cola,
                                                void ** cadena));

/* Cuantas filas de la tabla existen, y cuantas se ejercitaron. Es lo que mira
   la suite de cobertura. */
/* El censo de filas ejercitadas que la suite pide al terminar. Lo enciende
   tests/, no el emulador: escribirlo en cada paso del ARM cuesta dos cargas y
   un almacenamiento por instruccion por un dato que en produccion nadie mira. */
extern int arm7_cobertura;

int arm7_filas(void);
int arm7_fila_usada(int i);
const char * arm7_fila_nombre(int i);

#endif /* _ARM7_H_ */
