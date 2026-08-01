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

/* Cuantas filas de la tabla existen, y cuantas se ejercitaron. Es lo que mira
   la suite de cobertura. */
int arm7_filas(void);
int arm7_fila_usada(int i);
const char * arm7_fila_nombre(int i);

#endif /* _ARM7_H_ */
