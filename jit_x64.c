/****************************************************************************

	JIT_X64 - el emisor de codigo x86-64. Ver jit_x64.h.

	Sin dependencias del emulador a proposito: tests/test_jit_x64.c lo enlaza
	solo y compara byte a byte contra codificaciones sacadas del manual de
	Intel. Un emisor que se prueba unicamente corriendo el guest cuesta una
	corrida por pregunta y contesta con un cuelgue.

*****************************************************************************/

#include "jit_x64.h"

/* ------------------------------------------------------------------------ */
/* Primitivas                                                               */
/* ------------------------------------------------------------------------ */

static void b1(x64_emisor * e, unsigned v)
{
	if (e->p >= e->fin)
	{
		e->desborde = 1;
		return;
	}

	*e->p++ = (unsigned char) (v & 0xFF);
}

static void b4(x64_emisor * e, unsigned v)
{
	b1(e, v);
	b1(e, v >> 8);
	b1(e, v >> 16);
	b1(e, v >> 24);
}

static void b8(x64_emisor * e, unsigned long long v)
{
	b4(e, (unsigned) (v & 0xFFFFFFFFull));
	b4(e, (unsigned) (v >> 32));
}

/*
	El prefijo REX. Se emite cuando hace falta ancho de 64 bits (w) o cuando
	algun registro pasa de 7. `forzar` existe por la trampa de los operandos de
	8 bits: sin REX, los codigos 4..7 nombran AH/CH/DH/BH, y con REX (aunque
	sea 0x40 pelado) nombran SPL/BPL/SIL/DIL. Cualquier forma de 8 bits que
	toque un registro >= 4 tiene que forzarlo.
*/
static void rex_x(x64_emisor * e, int w, int reg, int indice, int base, int forzar)
{
	unsigned pref = 0x40u
		| (unsigned) ((w != 0) << 3)
		| (unsigned) (((reg >> 3) & 1) << 2)
		| (unsigned) ((indice > 0 ? (indice >> 3) & 1 : 0) << 1)
		| (unsigned) ((base >> 3) & 1);

	if (pref != 0x40u || forzar)
		b1(e, pref);
}

static void rex(x64_emisor * e, int w, int reg, int base, int forzar)
{
	rex_x(e, w, reg, -1, base, forzar);
}

/* ModRM de registro a registro (mod = 11). */
static void modrm_rr(x64_emisor * e, int reg, int rm)
{
	b1(e, 0xC0u | (unsigned) ((reg & 7) << 3) | (unsigned) (rm & 7));
}

/*
	ModRM + SIB + desplazamiento para [base + disp].

	Dos casos obligados por la codificacion: rm = 100 significa "hay SIB", asi
	que RSP y R12 necesitan un SIB que diga "sin indice"; y rm = 101 con
	mod = 00 significa RIP-relativo, asi que RBP y R13 necesitan disp8 aunque
	el desplazamiento sea cero.
*/
static void modrm_m(x64_emisor * e, int reg, int base, int disp)
{
	int b3 = base & 7;
	int mod;

	if (disp == 0 && b3 != 5)
		mod = 0;
	else if (disp >= -128 && disp <= 127)
		mod = 1;
	else
		mod = 2;

	b1(e, (unsigned) (mod << 6) | (unsigned) ((reg & 7) << 3)
		| (unsigned) (b3 == 4 ? 4 : b3));

	if (b3 == 4)
		b1(e, 0x24);			/* escala 1, sin indice, base = rm */

	if (mod == 1)
		b1(e, (unsigned) disp);
	else if (mod == 2)
		b4(e, (unsigned) disp);
}

/*
	ModRM + SIB + desplazamiento para [base + indice*escala + disp]. La forma
	con indice obliga rm = 100 siempre (es lo que significa "hay SIB"), y la
	base 101 sigue necesitando su disp8 de cero por el mismo motivo que arriba.
*/
static void modrm_m_idx(x64_emisor * e, int reg, int base, int indice,
	int escala, int disp)
{
	int b3 = base & 7;
	int mod;
	int ss;

	switch (escala)
	{
		case 1:  ss = 0; break;
		case 2:  ss = 1; break;
		case 4:  ss = 2; break;
		default: ss = 3; break;			/* 8 */
	}

	if (disp == 0 && b3 != 5)
		mod = 0;
	else if (disp >= -128 && disp <= 127)
		mod = 1;
	else
		mod = 2;

	b1(e, (unsigned) (mod << 6) | (unsigned) ((reg & 7) << 3) | 4u);
	b1(e, (unsigned) (ss << 6) | (unsigned) ((indice & 7) << 3)
		| (unsigned) b3);

	if (mod == 1)
		b1(e, (unsigned) disp);
	else if (mod == 2)
		b4(e, (unsigned) disp);
}

static int cabe_en_8(int v)
{
	return v >= -128 && v <= 127;
}

/* ------------------------------------------------------------------------ */

void jit_x64_iniciar(x64_emisor * e, void * buffer, unsigned tam)
{
	e->inicio   = (unsigned char *) buffer;
	e->p        = e->inicio;
	e->fin      = e->inicio + tam;
	e->desborde = 0;
}

unsigned char * jit_x64_aqui(const x64_emisor * e)
{
	return e->p;
}

unsigned jit_x64_largo(const x64_emisor * e)
{
	return (unsigned) (e->p - e->inicio);
}

/* ------------------------------------------------------------------------ */
/* Movimientos                                                              */
/* ------------------------------------------------------------------------ */

void jit_x64_mov_rr(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, src, dst, 0);
	b1(e, 0x89);				/* MOV r/m32, r32 */
	modrm_rr(e, src, dst);
}

void jit_x64_mov_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 0, dst, base, 0);
	b1(e, 0x8B);				/* MOV r32, r/m32 */
	modrm_m(e, dst, base, disp);
}

void jit_x64_mov_mr(x64_emisor * e, x64_reg base, int disp, x64_reg src)
{
	rex(e, 0, src, base, 0);
	b1(e, 0x89);
	modrm_m(e, src, base, disp);
}

void jit_x64_mov_ri(x64_emisor * e, x64_reg dst, unsigned imm)
{
	rex(e, 0, 0, dst, 0);
	b1(e, 0xB8u + (unsigned) (dst & 7));	/* MOV r32, imm32 */
	b4(e, imm);
}

void jit_x64_mov_mi(x64_emisor * e, x64_reg base, int disp, unsigned imm)
{
	rex(e, 0, 0, base, 0);
	b1(e, 0xC7);				/* MOV r/m32, imm32 */
	modrm_m(e, 0, base, disp);
	b4(e, imm);
}

void jit_x64_mov64_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 1, dst, base, 0);
	b1(e, 0x8B);
	modrm_m(e, dst, base, disp);
}

void jit_x64_mov64_ri(x64_emisor * e, x64_reg dst, unsigned long long imm)
{
	rex(e, 1, 0, dst, 0);
	b1(e, 0xB8u + (unsigned) (dst & 7));	/* MOV r64, imm64 */
	b8(e, imm);
}

void jit_x64_mov_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 0, dst, indice, base, 0);
	b1(e, 0x8B);
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_mov64_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 1, dst, indice, base, 0);
	b1(e, 0x8B);
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_mov_mr_idx(x64_emisor * e, x64_reg base, x64_reg indice,
	int escala, int disp, x64_reg src)
{
	rex_x(e, 0, src, indice, base, 0);
	b1(e, 0x89);				/* MOV r/m32, r32 */
	modrm_m_idx(e, src, base, indice, escala, disp);
}

void jit_x64_lea64_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 1, dst, indice, base, 0);
	b1(e, 0x8D);				/* LEA r64, m */
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_shift64_ri(x64_emisor * e, x64_shift op, x64_reg dst, int cuenta)
{
	rex(e, 1, 0, dst, 0);
	b1(e, 0xC1);
	modrm_rr(e, (int) op, dst);
	b1(e, (unsigned) cuenta);
}

void jit_x64_cmp8_mi_idx(x64_emisor * e, x64_reg base, x64_reg indice,
	int escala, int disp, int imm8)
{
	rex_x(e, 0, 0, indice, base, 0);
	b1(e, 0x80);				/* CMP r/m8, imm8 */
	modrm_m_idx(e, 7, base, indice, escala, disp);
	b1(e, (unsigned) imm8);
}

void jit_x64_mov8_mr_idx(x64_emisor * e, x64_reg base, x64_reg indice,
	int escala, int disp, x64_reg src)
{
	rex_x(e, 0, src, indice, base, src >= 4);
	b1(e, 0x88);				/* MOV r/m8, r8 */
	modrm_m_idx(e, src, base, indice, escala, disp);
}

void jit_x64_movsx_b_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 0, dst, indice, base, 0);
	b1(e, 0x0F);
	b1(e, 0xBE);				/* MOVSX r32, r/m8 */
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_movsx_w_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 0, dst, indice, base, 0);
	b1(e, 0x0F);
	b1(e, 0xBF);				/* MOVSX r32, r/m16 */
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_movsx_w(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, dst, src, 0);
	b1(e, 0x0F);
	b1(e, 0xBF);				/* MOVSX r32, r/m16 */
	modrm_rr(e, dst, src);
}

void jit_x64_movzx_b(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, dst, src, src >= 4);
	b1(e, 0x0F);
	b1(e, 0xB6);				/* MOVZX r32, r/m8 */
	modrm_rr(e, dst, src);
}

/* ------------------------------------------------------------------------ */
/* Aritmetica y logica                                                      */
/* ------------------------------------------------------------------------ */

void jit_x64_alu_rr(x64_emisor * e, x64_alu op, x64_reg dst, x64_reg src)
{
	rex(e, 0, src, dst, 0);
	b1(e, (unsigned) (op * 8 + 1));		/* op r/m32, r32 */
	modrm_rr(e, src, dst);
}

void jit_x64_alu_rm(x64_emisor * e, x64_alu op, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 0, dst, base, 0);
	b1(e, (unsigned) (op * 8 + 3));		/* op r32, r/m32 */
	modrm_m(e, dst, base, disp);
}

void jit_x64_alu_mr(x64_emisor * e, x64_alu op, x64_reg base, int disp, x64_reg src)
{
	rex(e, 0, src, base, 0);
	b1(e, (unsigned) (op * 8 + 1));
	modrm_m(e, src, base, disp);
}

void jit_x64_shift_ri(x64_emisor * e, x64_shift op, x64_reg dst, int cuenta)
{
	rex(e, 0, 0, dst, 0);

	if (cuenta == 1)
	{
		b1(e, 0xD1);					/* op r/m32, 1 */
		modrm_rr(e, (int) op, dst);
		return;
	}

	b1(e, 0xC1);						/* op r/m32, imm8 */
	modrm_rr(e, (int) op, dst);
	b1(e, (unsigned) cuenta);
}

void jit_x64_neg_r(x64_emisor * e, x64_reg dst)
{
	rex(e, 0, 0, dst, 0);
	b1(e, 0xF7);						/* NEG r/m32 */
	modrm_rr(e, 3, dst);
}

void jit_x64_movsx_b(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, dst, src, src >= 4);
	b1(e, 0x0F);
	b1(e, 0xBE);						/* MOVSX r32, r/m8 */
	modrm_rr(e, dst, src);
}

void jit_x64_movzx_w(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, dst, src, 0);
	b1(e, 0x0F);
	b1(e, 0xB7);						/* MOVZX r32, r/m16 */
	modrm_rr(e, dst, src);
}

void jit_x64_add_rr(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, src, dst, 0);
	b1(e, 0x01);				/* ADD r/m32, r32 */
	modrm_rr(e, src, dst);
}

/* La familia 81 /n (imm32) con su forma corta 83 /n (imm8 con signo). */
static void alu_ri(x64_emisor * e, int w, int ext, x64_reg dst, int imm)
{
	rex(e, w, 0, dst, 0);

	if (cabe_en_8(imm))
	{
		b1(e, 0x83);
		modrm_rr(e, ext, dst);
		b1(e, (unsigned) imm);
	}
	else
	{
		b1(e, 0x81);
		modrm_rr(e, ext, dst);
		b4(e, (unsigned) imm);
	}
}

static void alu_mi(x64_emisor * e, int w, int ext, x64_reg base, int disp, int imm)
{
	rex(e, w, 0, base, 0);

	if (cabe_en_8(imm))
	{
		b1(e, 0x83);
		modrm_m(e, ext, base, disp);
		b1(e, (unsigned) imm);
	}
	else
	{
		b1(e, 0x81);
		modrm_m(e, ext, base, disp);
		b4(e, (unsigned) imm);
	}
}

void jit_x64_alu_ri(x64_emisor * e, x64_alu op, x64_reg dst, int imm)
{
	alu_ri(e, 0, (int) op, dst, imm);
}

void jit_x64_alu_mi(x64_emisor * e, x64_alu op, x64_reg base, int disp, int imm)
{
	alu_mi(e, 0, (int) op, base, disp, imm);
}

void jit_x64_add_ri(x64_emisor * e, x64_reg dst, int imm)
{
	alu_ri(e, 0, 0, dst, imm);
}

void jit_x64_add_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 0, dst, base, 0);
	b1(e, 0x03);				/* ADD r32, r/m32 */
	modrm_m(e, dst, base, disp);
}

void jit_x64_add_mr(x64_emisor * e, x64_reg base, int disp, x64_reg src)
{
	rex(e, 0, src, base, 0);
	b1(e, 0x01);
	modrm_m(e, src, base, disp);
}

void jit_x64_add_mi(x64_emisor * e, x64_reg base, int disp, int imm)
{
	alu_mi(e, 0, 0, base, disp, imm);
}

void jit_x64_and_ri(x64_emisor * e, x64_reg dst, int imm)
{
	alu_ri(e, 0, 4, dst, imm);
}

void jit_x64_or_ri(x64_emisor * e, x64_reg dst, int imm)
{
	alu_ri(e, 0, 1, dst, imm);
}

void jit_x64_xor_ri(x64_emisor * e, x64_reg dst, int imm)
{
	alu_ri(e, 0, 6, dst, imm);
}

void jit_x64_and_rr(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, src, dst, 0);
	b1(e, 0x21);				/* AND r/m32, r32 */
	modrm_rr(e, src, dst);
}

void jit_x64_and_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 0, dst, base, 0);
	b1(e, 0x23);				/* AND r32, r/m32 */
	modrm_m(e, dst, base, disp);
}

void jit_x64_or_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 0, dst, base, 0);
	b1(e, 0x0B);				/* OR r32, r/m32 */
	modrm_m(e, dst, base, disp);
}

void jit_x64_not_r(x64_emisor * e, x64_reg dst)
{
	rex(e, 0, 0, dst, 0);
	b1(e, 0xF7);				/* NOT r/m32 */
	modrm_rr(e, 2, dst);
}

void jit_x64_shl_ri(x64_emisor * e, x64_reg dst, int cuenta)
{
	rex(e, 0, 0, dst, 0);
	b1(e, 0xC1);				/* SHL r/m32, imm8 */
	modrm_rr(e, 4, dst);
	b1(e, (unsigned) cuenta);
}

void jit_x64_imul_rri(x64_emisor * e, x64_reg dst, x64_reg src, int imm)
{
	rex(e, 0, dst, src, 0);

	if (cabe_en_8(imm))
	{
		b1(e, 0x6B);			/* IMUL r32, r/m32, imm8 */
		modrm_rr(e, dst, src);
		b1(e, (unsigned) imm);
	}
	else
	{
		b1(e, 0x69);			/* IMUL r32, r/m32, imm32 */
		modrm_rr(e, dst, src);
		b4(e, (unsigned) imm);
	}
}

/* Las dos formas de dos operandos (0F AF): MUL.L las quiere para R(n)*R(m)
   sin pasar por el inmediato. */
void jit_x64_imul_rr(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, dst, src, 0);
	b1(e, 0x0F);
	b1(e, 0xAF);				/* IMUL r32, r/m32 */
	modrm_rr(e, dst, src);
}

void jit_x64_imul_rm(x64_emisor * e, x64_reg dst, x64_reg base, int disp)
{
	rex(e, 0, dst, base, 0);
	b1(e, 0x0F);
	b1(e, 0xAF);
	modrm_m(e, dst, base, disp);
}

void jit_x64_cmp_rm_idx(x64_emisor * e, x64_reg a, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 0, a, indice, base, 0);
	b1(e, 0x3B);				/* CMP r32, r/m32 */
	modrm_m_idx(e, a, base, indice, escala, disp);
}

void jit_x64_and_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 0, dst, indice, base, 0);
	b1(e, 0x23);				/* AND r32, r/m32 */
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_or_rm_idx(x64_emisor * e, x64_reg dst, x64_reg base,
	x64_reg indice, int escala, int disp)
{
	rex_x(e, 0, dst, indice, base, 0);
	b1(e, 0x0B);				/* OR r32, r/m32 */
	modrm_m_idx(e, dst, base, indice, escala, disp);
}

void jit_x64_test_mi_idx(x64_emisor * e, x64_reg base, x64_reg indice,
	int escala, int disp, int imm)
{
	rex_x(e, 0, 0, indice, base, 0);
	b1(e, 0xF7);				/* TEST r/m32, imm32 */
	modrm_m_idx(e, 0, base, indice, escala, disp);
	b4(e, (unsigned) imm);
}

void jit_x64_xor_rr(x64_emisor * e, x64_reg dst, x64_reg src)
{
	rex(e, 0, src, dst, 0);
	b1(e, 0x31);				/* XOR r/m32, r32 */
	modrm_rr(e, src, dst);
}

void jit_x64_shr_ri(x64_emisor * e, x64_reg dst, int cuenta)
{
	rex(e, 0, 0, dst, 0);
	b1(e, 0xC1);				/* SHR r/m32, imm8 */
	modrm_rr(e, 5, dst);
	b1(e, (unsigned) cuenta);
}

void jit_x64_inc_r(x64_emisor * e, x64_reg dst)
{
	rex(e, 0, 0, dst, 0);
	b1(e, 0xFF);				/* INC r/m32 */
	modrm_rr(e, 0, dst);
}

void jit_x64_add64_mr(x64_emisor * e, x64_reg base, int disp, x64_reg src)
{
	rex(e, 1, src, base, 0);
	b1(e, 0x01);
	modrm_m(e, src, base, disp);
}

void jit_x64_add64_mi(x64_emisor * e, x64_reg base, int disp, int imm)
{
	alu_mi(e, 1, 0, base, disp, imm);
}

void jit_x64_add64_ri(x64_emisor * e, x64_reg dst, int imm)
{
	alu_ri(e, 1, 0, dst, imm);
}

void jit_x64_sub64_ri(x64_emisor * e, x64_reg dst, int imm)
{
	alu_ri(e, 1, 5, dst, imm);
}

void jit_x64_and_mi8(x64_emisor * e, x64_reg base, int disp, int imm8)
{
	rex(e, 0, 0, base, 0);
	b1(e, 0x80);				/* AND r/m8, imm8 */
	modrm_m(e, 4, base, disp);
	b1(e, (unsigned) imm8);
}

void jit_x64_or_mr8(x64_emisor * e, x64_reg base, int disp, x64_reg src)
{
	rex(e, 0, src, base, src >= 4);
	b1(e, 0x08);				/* OR r/m8, r8 */
	modrm_m(e, src, base, disp);
}

/* ------------------------------------------------------------------------ */
/* Comparaciones                                                            */
/* ------------------------------------------------------------------------ */

void jit_x64_cmp_rr(x64_emisor * e, x64_reg a, x64_reg b)
{
	rex(e, 0, b, a, 0);
	b1(e, 0x39);				/* CMP r/m32, r32 -- compara a contra b */
	modrm_rr(e, b, a);
}

void jit_x64_cmp_rm(x64_emisor * e, x64_reg a, x64_reg base, int disp)
{
	rex(e, 0, a, base, 0);
	b1(e, 0x3B);				/* CMP r32, r/m32 */
	modrm_m(e, a, base, disp);
}

void jit_x64_test_rr(x64_emisor * e, x64_reg a, x64_reg b)
{
	rex(e, 0, b, a, 0);
	b1(e, 0x85);				/* TEST r/m32, r32 */
	modrm_rr(e, b, a);
}

/*
	Las dos formas **de ancho fijo**, para los sitios que se parchean en tiempo
	de ejecucion. El emisor elige normalmente la codificacion mas corta, y eso
	es exactamente lo que un parche no puede tolerar: `cmp eax, 1` sale con
	inmediato de 8 bits, asi que el sitio que el JIT creia el imm32 caia en
	medio de la instruccion siguiente. Se cayo el proceso con instruccion
	privilegiada, que es lo que pasa cuando se escribe encima del codigo.
*/
void jit_x64_cmp_ri32(x64_emisor * e, x64_reg a, int imm)
{
	rex(e, 0, 0, a, 0);
	b1(e, 0x81);
	modrm_rr(e, 7, a);
	b4(e, (unsigned) imm);
}

void jit_x64_cmp_rm32(x64_emisor * e, x64_reg a, x64_reg base, int disp)
{
	rex(e, 0, a, base, 0);
	b1(e, 0x3B);				/* CMP r32, r/m32 */
	b1(e, 0x80u | (unsigned) ((a & 7) << 3) | (unsigned) (base & 7));
	b4(e, (unsigned) disp);
}

void jit_x64_cmp_ri(x64_emisor * e, x64_reg a, int imm)
{
	alu_ri(e, 0, 7, a, imm);
}

void jit_x64_cmp_mi(x64_emisor * e, x64_reg base, int disp, int imm)
{
	alu_mi(e, 0, 7, base, disp, imm);
}

void jit_x64_test_ri(x64_emisor * e, x64_reg a, int imm)
{
	rex(e, 0, 0, a, 0);
	b1(e, 0xF7);				/* TEST r/m32, imm32 */
	modrm_rr(e, 0, a);
	b4(e, (unsigned) imm);
}

void jit_x64_test_ri8(x64_emisor * e, x64_reg a, int imm8)
{
	rex(e, 0, 0, a, a >= 4);
	b1(e, 0xF6);				/* TEST r/m8, imm8 */
	modrm_rr(e, 0, a);
	b1(e, (unsigned) imm8);
}

void jit_x64_test64_rr(x64_emisor * e, x64_reg a, x64_reg b)
{
	rex(e, 1, b, a, 0);
	b1(e, 0x85);				/* TEST r/m64, r64 */
	modrm_rr(e, b, a);
}

void jit_x64_test_mi8(x64_emisor * e, x64_reg base, int disp, int imm8)
{
	rex(e, 0, 0, base, 0);
	b1(e, 0xF6);				/* TEST r/m8, imm8 */
	modrm_m(e, 0, base, disp);
	b1(e, (unsigned) imm8);
}

void jit_x64_setcc(x64_emisor * e, x64_cond cc, x64_reg dst)
{
	rex(e, 0, 0, dst, dst >= 4);
	b1(e, 0x0F);
	b1(e, 0x90u + (unsigned) cc);
	modrm_rr(e, 0, dst);
}

/* ------------------------------------------------------------------------ */
/* Flujo                                                                    */
/* ------------------------------------------------------------------------ */

x64_parche jit_x64_jcc(x64_emisor * e, x64_cond cc)
{
	x64_parche p;

	b1(e, 0x0F);
	b1(e, 0x80u + (unsigned) cc);
	p.sitio = e->p;
	p.ancho = 4;
	b4(e, 0);

	return p;
}

x64_parche jit_x64_jcc_corto(x64_emisor * e, x64_cond cc)
{
	x64_parche p;

	b1(e, 0x70u + (unsigned) cc);
	p.sitio = e->p;
	p.ancho = 1;
	b1(e, 0);

	return p;
}

x64_parche jit_x64_jmp(x64_emisor * e)
{
	x64_parche p;

	b1(e, 0xE9);
	p.sitio = e->p;
	p.ancho = 4;
	b4(e, 0);

	return p;
}

void jit_x64_fijar(x64_emisor * e, x64_parche p)
{
	long long rel;

	if (p.sitio == 0)
		return;

	rel = (long long) (e->p - (p.sitio + p.ancho));

	if (p.ancho == 1)
	{
		if (rel < -128 || rel > 127)
		{
			e->desborde = 1;	/* el salto corto no alcanza: bloque invalido */
			return;
		}

		p.sitio[0] = (unsigned char) rel;
	}
	else
	{
		if (rel < -2147483647LL || rel > 2147483647LL)
		{
			e->desborde = 1;
			return;
		}

		p.sitio[0] = (unsigned char) ((unsigned long long) rel & 0xFF);
		p.sitio[1] = (unsigned char) (((unsigned long long) rel >> 8) & 0xFF);
		p.sitio[2] = (unsigned char) (((unsigned long long) rel >> 16) & 0xFF);
		p.sitio[3] = (unsigned char) (((unsigned long long) rel >> 24) & 0xFF);
	}
}

void jit_x64_jmp_a(x64_emisor * e, const unsigned char * destino)
{
	/* Los dos desplazamientos se miden desde el final de la instruccion, que
	   no es el mismo en las dos formas: 2 bytes la corta, 5 la larga. */
	long long rel_corto = (long long) (destino - (e->p + 2));
	long long rel_largo = (long long) (destino - (e->p + 5));

	if (rel_corto >= -128 && rel_corto <= 127)
	{
		b1(e, 0xEB);
		b1(e, (unsigned) (int) rel_corto);
		return;
	}

	if (rel_largo < -2147483647LL || rel_largo > 2147483647LL)
	{
		e->desborde = 1;
		return;
	}

	b1(e, 0xE9);
	b4(e, (unsigned) (int) rel_largo);
}

/*
	CALL directo, cinco bytes y sin carga. El arena casi siempre cae a menos de
	2 GB del codigo del emulador, pero VirtualAlloc no lo promete, asi que esto
	dice si pudo: con 0, el llamador emite la forma por memoria.
*/
int jit_x64_call_directo(x64_emisor * e, const void * destino)
{
	long long rel = (long long) ((const unsigned char *) destino - (e->p + 5));

	if (rel < -2147483647LL || rel > 2147483647LL)
		return 0;

	b1(e, 0xE8);
	b4(e, (unsigned) (int) rel);

	return 1;
}

void jit_x64_jmp_r(x64_emisor * e, x64_reg r)
{
	rex(e, 0, 0, r, 0);
	b1(e, 0xFF);				/* JMP r/m64 */
	modrm_rr(e, 4, r);
}

void jit_x64_call_m(x64_emisor * e, x64_reg base, int disp)
{
	rex(e, 0, 0, base, 0);
	b1(e, 0xFF);				/* CALL r/m64 */
	modrm_m(e, 2, base, disp);
}

void jit_x64_push(x64_emisor * e, x64_reg r)
{
	rex(e, 0, 0, r, 0);
	b1(e, 0x50u + (unsigned) (r & 7));
}

void jit_x64_pop(x64_emisor * e, x64_reg r)
{
	rex(e, 0, 0, r, 0);
	b1(e, 0x58u + (unsigned) (r & 7));
}

void jit_x64_ret(x64_emisor * e)
{
	b1(e, 0xC3);
}

void jit_x64_int3(x64_emisor * e)
{
	b1(e, 0xCC);
}
