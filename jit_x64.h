/****************************************************************************

	JIT_X64 - el emisor de codigo x86-64

	Fase 0 de docs/recompilador-plan.md. Es el subconjunto exacto que las
	plantillas del JIT usan y nada mas: movimientos de 32 y 64 bits entre
	registros, memoria e inmediatos, la aritmetica y la logica enteras, los
	corrimientos, las comparaciones, setcc, los saltos y la llamada indirecta
	por memoria. Cientos de lineas, cero dependencias -- lo que el plan pedia
	frente a una biblioteca de emision.

	**No sabe nada del SH-4 ni de dcemu.** Es C portable que produce bytes; por
	eso tests/ lo enlaza y lo verifica contra codificaciones conocidas
	(tests/test_jit_x64.c), que es el desarme del riesgo "el emisor mismo".

	Convenciones:

	  - Todo lo que no lleva sufijo opera en 32 bits. Escribir un registro de
		32 bits **pone en cero los 32 altos**, y de eso depende el resto del
		JIT: un registro del SH-4 cargado con jit_x64_mov_rm() queda extendido
		con ceros y se puede usar como base de un LEA de 64 sin enmascarar.
	  - Los operandos van en orden destino, origen.
	  - `disp` es un desplazamiento de 32 bits con signo desde el registro
		base. El emisor elige la codificacion mas corta (sin disp, disp8 o
		disp32) y arma el SIB cuando la base lo obliga (RSP y R12).
	  - Cualquier desborde del buffer, o un salto corto fuera de alcance, deja
		`desborde` en 1 y sigue emitiendo sobre el ultimo byte. Se consulta una
		vez al cerrar el bloque: el JIT descarta el bloque entero y el guest
		sigue interpretado.

*****************************************************************************/

#ifndef _JIT_X64_H_
#define _JIT_X64_H_

/* Los 16 registros generales, en su numeracion de codificacion. */
typedef enum
{
	X64_RAX = 0,  X64_RCX = 1,  X64_RDX = 2,  X64_RBX = 3,
	X64_RSP = 4,  X64_RBP = 5,  X64_RSI = 6,  X64_RDI = 7,
	X64_R8  = 8,  X64_R9  = 9,  X64_R10 = 10, X64_R11 = 11,
	X64_R12 = 12, X64_R13 = 13, X64_R14 = 14, X64_R15 = 15
} x64_reg;

/* Condiciones, en su numeracion de codificacion (el bajo nibble de Jcc/SETcc).
   Estan las que el JIT usa; el resto se agrega cuando haga falta. */
typedef enum
{
	X64_E   = 0x4,	/* igual / cero */
	X64_NE  = 0x5,
	X64_B   = 0x2,	/* menor sin signo */
	X64_AE  = 0x3,	/* mayor o igual sin signo */
	X64_BE  = 0x6,
	X64_A   = 0x7,	/* mayor sin signo */
	X64_S   = 0x8,	/* negativo */
	X64_NS  = 0x9,
	X64_L   = 0xC,	/* menor con signo */
	X64_GE  = 0xD,
	X64_LE  = 0xE,
	X64_G   = 0xF
} x64_cond;

typedef struct
{
	unsigned char *	inicio;
	unsigned char *	p;
	unsigned char *	fin;
	int				desborde;
} x64_emisor;

/* Un salto cuyo destino todavia no se emitio. `ancho` es 1 o 4 bytes. */
typedef struct
{
	unsigned char *	sitio;
	int				ancho;
} x64_parche;

void			jit_x64_iniciar(x64_emisor * e, void * buffer, unsigned tam);
unsigned char *	jit_x64_aqui(const x64_emisor * e);
unsigned		jit_x64_largo(const x64_emisor * e);

/* --- movimientos ------------------------------------------------------- */

void jit_x64_mov_rr  (x64_emisor * e, x64_reg dst, x64_reg src);
void jit_x64_mov_rm  (x64_emisor * e, x64_reg dst, x64_reg base, int disp);
void jit_x64_mov_mr  (x64_emisor * e, x64_reg base, int disp, x64_reg src);
void jit_x64_mov_ri  (x64_emisor * e, x64_reg dst, unsigned imm);
void jit_x64_mov_mi  (x64_emisor * e, x64_reg base, int disp, unsigned imm);

void jit_x64_mov64_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp);

/* Con indice escalado: [base + indice*escala + disp]. Es lo que pide una tabla
   indexada por el byte alto de la direccion (mem_base_lectura) y la suma
   base+desplazamiento de un acceso ya resuelto. */
void jit_x64_mov_rm_idx  (x64_emisor * e, x64_reg dst, x64_reg base,
                          x64_reg indice, int escala, int disp);
void jit_x64_mov64_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
                          x64_reg indice, int escala, int disp);
void jit_x64_mov8_mr_idx (x64_emisor * e, x64_reg base, x64_reg indice,
                          int escala, int disp, x64_reg src);
void jit_x64_movsx_b_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
                          x64_reg indice, int escala, int disp);
void jit_x64_mov64_ri(x64_emisor * e, x64_reg dst, unsigned long long imm);

void jit_x64_movsx_w (x64_emisor * e, x64_reg dst, x64_reg src);	/* de 16 bits */
void jit_x64_movzx_b (x64_emisor * e, x64_reg dst, x64_reg src);	/* de 8 bits */

/* --- aritmetica y logica ----------------------------------------------- */

/*
	La familia entera de x86, en su numeracion de extension de opcode: el
	opcode base de cada una es ese numero por ocho, asi que las cinco formas
	--registro/registro, registro/memoria, memoria/registro y las dos con
	inmediato-- salen de una sola tabla. Es lo que hace que agregar una
	plantilla del traductor sea una linea y no cinco funciones.
*/
typedef enum
{
	X64_ADD = 0, X64_OR = 1, X64_ADC = 2, X64_SBB = 3,
	X64_AND = 4, X64_SUB = 5, X64_XOR = 6, X64_CMP = 7
} x64_alu;

void jit_x64_alu_rr(x64_emisor * e, x64_alu op, x64_reg dst, x64_reg src);
void jit_x64_alu_rm(x64_emisor * e, x64_alu op, x64_reg dst, x64_reg base, int disp);
void jit_x64_alu_mr(x64_emisor * e, x64_alu op, x64_reg base, int disp, x64_reg src);
void jit_x64_alu_ri(x64_emisor * e, x64_alu op, x64_reg dst, int imm);
void jit_x64_alu_mi(x64_emisor * e, x64_alu op, x64_reg base, int disp, int imm);

/* Corrimientos por cuenta inmediata, en la misma numeracion. */
typedef enum
{
	X64_ROL = 0, X64_ROR = 1, X64_RCL = 2, X64_RCR = 3,
	X64_SHL = 4, X64_SHR = 5, X64_SAR = 7
} x64_shift;

void jit_x64_shift_ri(x64_emisor * e, x64_shift op, x64_reg dst, int cuenta);
void jit_x64_neg_r   (x64_emisor * e, x64_reg dst);
void jit_x64_movsx_b (x64_emisor * e, x64_reg dst, x64_reg src);
void jit_x64_movzx_w (x64_emisor * e, x64_reg dst, x64_reg src);

void jit_x64_add_rr  (x64_emisor * e, x64_reg dst, x64_reg src);
void jit_x64_add_ri  (x64_emisor * e, x64_reg dst, int imm);
void jit_x64_add_rm  (x64_emisor * e, x64_reg dst, x64_reg base, int disp);
void jit_x64_add_mr  (x64_emisor * e, x64_reg base, int disp, x64_reg src);
void jit_x64_add_mi  (x64_emisor * e, x64_reg base, int disp, int imm);
void jit_x64_and_ri  (x64_emisor * e, x64_reg dst, int imm);
void jit_x64_or_ri   (x64_emisor * e, x64_reg dst, int imm);
void jit_x64_xor_ri  (x64_emisor * e, x64_reg dst, int imm);
void jit_x64_and_rr  (x64_emisor * e, x64_reg dst, x64_reg src);
void jit_x64_and_rm  (x64_emisor * e, x64_reg dst, x64_reg base, int disp);
void jit_x64_or_rm   (x64_emisor * e, x64_reg dst, x64_reg base, int disp);
void jit_x64_not_r   (x64_emisor * e, x64_reg dst);
void jit_x64_shl_ri  (x64_emisor * e, x64_reg dst, int cuenta);
void jit_x64_imul_rri(x64_emisor * e, x64_reg dst, x64_reg src, int imm);
void jit_x64_xor_rr  (x64_emisor * e, x64_reg dst, x64_reg src);

/* Las mismas, con indice escalado: el arreglo de la cache de traducciones de
   la MMU tiene elementos que no miden una potencia de dos, asi que el indice
   viaja ya multiplicado y la escala es 1. */
void jit_x64_cmp_rm_idx (x64_emisor * e, x64_reg a, x64_reg base,
                         x64_reg indice, int escala, int disp);
void jit_x64_and_rm_idx (x64_emisor * e, x64_reg dst, x64_reg base,
                         x64_reg indice, int escala, int disp);
void jit_x64_or_rm_idx  (x64_emisor * e, x64_reg dst, x64_reg base,
                         x64_reg indice, int escala, int disp);
void jit_x64_test_mi_idx(x64_emisor * e, x64_reg base, x64_reg indice,
                         int escala, int disp, int imm);
void jit_x64_shr_ri  (x64_emisor * e, x64_reg dst, int cuenta);
void jit_x64_inc_r   (x64_emisor * e, x64_reg dst);

/* De 64 bits: los acumuladores del JIT son unsigned long long. */
void jit_x64_add64_mr(x64_emisor * e, x64_reg base, int disp, x64_reg src);
void jit_x64_add64_mi(x64_emisor * e, x64_reg base, int disp, int imm);
void jit_x64_add64_ri(x64_emisor * e, x64_reg dst, int imm);
void jit_x64_sub64_ri(x64_emisor * e, x64_reg dst, int imm);

/* Sobre bytes: SR.T vive en el bit 0 de un campo de bits, asi que se toca
   leyendo y escribiendo el byte, no el registro entero. */
void jit_x64_and_mi8 (x64_emisor * e, x64_reg base, int disp, int imm8);
void jit_x64_or_mr8  (x64_emisor * e, x64_reg base, int disp, x64_reg src);

/* --- comparaciones ----------------------------------------------------- */

void jit_x64_cmp_rr  (x64_emisor * e, x64_reg a, x64_reg b);
void jit_x64_cmp_ri  (x64_emisor * e, x64_reg a, int imm);
void jit_x64_cmp_rm  (x64_emisor * e, x64_reg a, x64_reg base, int disp);
void jit_x64_test_rr (x64_emisor * e, x64_reg a, x64_reg b);
void jit_x64_cmp_mi  (x64_emisor * e, x64_reg base, int disp, int imm);
void jit_x64_test_ri (x64_emisor * e, x64_reg a, int imm);
void jit_x64_test_ri8(x64_emisor * e, x64_reg a, int imm8);
void jit_x64_test64_rr(x64_emisor * e, x64_reg a, x64_reg b);
void jit_x64_test_mi8(x64_emisor * e, x64_reg base, int disp, int imm8);
void jit_x64_setcc   (x64_emisor * e, x64_cond cc, x64_reg dst);

/* --- flujo ------------------------------------------------------------- */

/* Saltos hacia adelante: devuelven el sitio a parchear con jit_x64_fijar(). */
x64_parche jit_x64_jcc      (x64_emisor * e, x64_cond cc);	/* rel32 */
x64_parche jit_x64_jcc_corto(x64_emisor * e, x64_cond cc);	/* rel8 */
x64_parche jit_x64_jmp      (x64_emisor * e);				/* rel32 */
void       jit_x64_fijar    (x64_emisor * e, x64_parche p);

/* Salto hacia atras, a una posicion ya emitida. */
void jit_x64_jmp_a(x64_emisor * e, const unsigned char * destino);

/* CALL [base+disp]: la tabla de ayudantes vive en memoria, asi que la llamada
   son seis bytes y no hace falta un inmediato de 64 bits ni que el arena
   quede a menos de 2 GB del codigo del emulador. */
int  jit_x64_call_directo(x64_emisor * e, const void * destino);
void jit_x64_call_m(x64_emisor * e, x64_reg base, int disp);

void jit_x64_push (x64_emisor * e, x64_reg r);
void jit_x64_pop  (x64_emisor * e, x64_reg r);
void jit_x64_ret  (x64_emisor * e);
void jit_x64_int3 (x64_emisor * e);

#endif /* _JIT_X64_H_ */
