/****************************************************************************

	Pruebas de jit_x64.c: el emisor de codigo x86-64 del recompilador.

	Existe por lo que dice docs/recompilador-plan.md en la tabla de riesgos:
	"el emisor mismo (codificacion x64 mal)". Un emisor que solo se prueba
	corriendo el guest cuesta una corrida por pregunta y contesta con un
	cuelgue, sin decir cual de los cientos de bytes emitidos estaba mal.

	Cada caso compara byte a byte contra la codificacion del manual de Intel.
	Se cubren en particular las tres trampas que un emisor propio paga:

	  - **RSP y R12 como base** obligan un SIB, porque rm = 100 significa "hay
		SIB" y no "esta base".
	  - **RBP y R13 como base** obligan un disp8 aunque el desplazamiento sea
		cero, porque mod = 00 con rm = 101 significa RIP-relativo.
	  - **Los operandos de 8 bits** necesitan un REX aunque sea pelado cuando
		el registro pasa de 3: sin el, 4..7 nombran AH/CH/DH/BH y no
		SPL/BPL/SIL/DIL.

	Y las formas exactas que los bloques de la fase 0 emiten, incluida la
	longitud del prologo, de la que depende la informacion de desenrollado.

*****************************************************************************/

#include <string.h>

#include "dctest.h"
#include "suites.h"

#include "jit_x64.h"

/* ------------------------------------------------------------------------ */

static unsigned char buf[256];
static x64_emisor e;

static void arrancar(void)
{
	memset(buf, 0xAA, sizeof(buf));
	jit_x64_iniciar(&e, buf, sizeof(buf));
}

#define ESPERAR_EMITIDO(...)											\
	do																	\
	{																	\
		static const unsigned char esperado[] = { __VA_ARGS__ };			\
																		\
		ESPERAR_U32(jit_x64_largo(&e), (unsigned) sizeof(esperado));		\
		ESPERAR_BYTES(buf, esperado, sizeof(esperado));					\
		ESPERAR_U32((unsigned) e.desborde, 0);							\
	} while (0)

/* ------------------------------------------------------------------------ */

static void movimientos_entre_registros(void)
{
	arrancar();
	jit_x64_mov_rr(&e, X64_RCX, X64_RDX);			/* mov ecx, edx */
	ESPERAR_EMITIDO(0x89, 0xD1);

	arrancar();
	jit_x64_mov_rr(&e, X64_R12, X64_RAX);			/* mov r12d, eax */
	ESPERAR_EMITIDO(0x41, 0x89, 0xC4);

	arrancar();
	jit_x64_mov_rr(&e, X64_RCX, X64_R15);			/* mov ecx, r15d */
	ESPERAR_EMITIDO(0x44, 0x89, 0xF9);

	arrancar();
	jit_x64_xor_rr(&e, X64_RSI, X64_RSI);			/* xor esi, esi */
	ESPERAR_EMITIDO(0x31, 0xF6);
}

static void movimientos_con_memoria(void)
{
	arrancar();
	jit_x64_mov_rm(&e, X64_RDI, X64_RBX, 0x10);		/* mov edi, [rbx+10h] */
	ESPERAR_EMITIDO(0x8B, 0x7B, 0x10);

	arrancar();
	jit_x64_mov_mr(&e, X64_RBX, 0x64, X64_R15);		/* mov [rbx+64h], r15d */
	ESPERAR_EMITIDO(0x44, 0x89, 0x7B, 0x64);

	arrancar();
	jit_x64_mov_ri(&e, X64_RCX, 0x0C158480u);		/* mov ecx, 0C158480h */
	ESPERAR_EMITIDO(0xB9, 0x80, 0x84, 0x15, 0x0C);

	arrancar();									/* mov [rbx+4], 0C1583FAh */
	jit_x64_mov_mi(&e, X64_RBX, 4, 0x0C1583FAu);
	ESPERAR_EMITIDO(0xC7, 0x43, 0x04, 0xFA, 0x83, 0x15, 0x0C);

	arrancar();									/* mov rbx, imm64 */
	jit_x64_mov64_ri(&e, X64_RBX, 0x0123456789ABCDEFull);
	ESPERAR_EMITIDO(0x48, 0xBB, 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01);

	arrancar();									/* mov rax, [rbx+11223344h] */
	jit_x64_mov64_rm(&e, X64_RAX, X64_RBX, 0x11223344);
	ESPERAR_EMITIDO(0x48, 0x8B, 0x83, 0x44, 0x33, 0x22, 0x11);
}

/*
	Las tres formas que un ModRM escrito a mano equivoca. Cada una es un
	cuelgue silencioso si sale mal: la direccion apunta a otro lado y el bloque
	lee o escribe donde no debe.
*/
static void las_bases_que_obligan_una_forma_especial(void)
{
	/* R12 como base: rm = 100 significa "hay SIB", asi que hace falta un SIB
	   que diga "sin indice". */
	arrancar();
	jit_x64_mov_rm(&e, X64_RAX, X64_R12, 8);		/* mov eax, [r12+8] */
	ESPERAR_EMITIDO(0x41, 0x8B, 0x44, 0x24, 0x08);

	/* RSP como base, lo mismo y sin REX. */
	arrancar();
	jit_x64_mov_rm(&e, X64_RAX, X64_RSP, 0);		/* mov eax, [rsp] */
	ESPERAR_EMITIDO(0x8B, 0x04, 0x24);

	/* RBP como base con desplazamiento cero: mod = 00 seria RIP-relativo, asi
	   que va un disp8 de cero. */
	arrancar();
	jit_x64_mov_rm(&e, X64_RAX, X64_RBP, 0);		/* mov eax, [rbp+0] */
	ESPERAR_EMITIDO(0x8B, 0x45, 0x00);

	/* R13 igual, y con su REX.B. */
	arrancar();
	jit_x64_mov_rm(&e, X64_RAX, X64_R13, 0);		/* mov eax, [r13+0] */
	ESPERAR_EMITIDO(0x41, 0x8B, 0x45, 0x00);

	/* Y el desplazamiento largo, que es el que usan los estaticos vistos
	   desde rbx. */
	arrancar();
	jit_x64_mov_rm(&e, X64_RAX, X64_RBX, 0x1000);	/* mov eax, [rbx+1000h] */
	ESPERAR_EMITIDO(0x8B, 0x83, 0x00, 0x10, 0x00, 0x00);
}

static void extensiones_de_signo_y_de_cero(void)
{
	arrancar();
	jit_x64_movsx_w(&e, X64_RDI, X64_R12);			/* movsx edi, r12w */
	ESPERAR_EMITIDO(0x41, 0x0F, 0xBF, 0xFC);

	/* MOVZX de r14b: el REX ya viene por R14, pero el caso que importa es el
	   siguiente. */
	arrancar();
	jit_x64_movzx_b(&e, X64_RDI, X64_R14);			/* movzx edi, r14b */
	ESPERAR_EMITIDO(0x41, 0x0F, 0xB6, 0xFE);

	/* Fuente RSI: sin un REX pelado esto seria DH y no SIL. */
	arrancar();
	jit_x64_movzx_b(&e, X64_RAX, X64_RSI);			/* movzx eax, sil */
	ESPERAR_EMITIDO(0x40, 0x0F, 0xB6, 0xC6);

	/* Fuente RAX: aqui no hay ambiguedad y no tiene que aparecer REX. */
	arrancar();
	jit_x64_movzx_b(&e, X64_RAX, X64_RAX);			/* movzx eax, al */
	ESPERAR_EMITIDO(0x0F, 0xB6, 0xC0);
}

static void aritmetica_y_logica(void)
{
	arrancar();
	jit_x64_add_rr(&e, X64_R13, X64_R14);			/* add r13d, r14d */
	ESPERAR_EMITIDO(0x45, 0x01, 0xF5);

	arrancar();
	jit_x64_add_ri(&e, X64_RBP, 2);					/* add ebp, 2 */
	ESPERAR_EMITIDO(0x83, 0xC5, 0x02);

	arrancar();
	jit_x64_add_ri(&e, X64_RBP, 400);				/* add ebp, 190h */
	ESPERAR_EMITIDO(0x81, 0xC5, 0x90, 0x01, 0x00, 0x00);

	arrancar();
	jit_x64_add_mr(&e, X64_RBX, 0x3C, X64_RAX);		/* add [rbx+3Ch], eax */
	ESPERAR_EMITIDO(0x01, 0x43, 0x3C);

	arrancar();
	jit_x64_add_mi(&e, X64_RBX, 0x2C, -1);			/* add dword [rbx+2Ch], -1 */
	ESPERAR_EMITIDO(0x83, 0x43, 0x2C, 0xFF);

	arrancar();
	jit_x64_and_ri(&e, X64_RDI, 0x7F);				/* and edi, 7Fh */
	ESPERAR_EMITIDO(0x83, 0xE7, 0x7F);

	arrancar();
	jit_x64_shr_ri(&e, X64_R12, 16);				/* shr r12d, 16 */
	ESPERAR_EMITIDO(0x41, 0xC1, 0xEC, 0x10);

	arrancar();
	jit_x64_inc_r(&e, X64_RSI);						/* inc esi */
	ESPERAR_EMITIDO(0xFF, 0xC6);

	arrancar();										/* add [rbx+1000h], rsi */
	jit_x64_add64_mr(&e, X64_RBX, 0x1000, X64_RSI);
	ESPERAR_EMITIDO(0x48, 0x01, 0xB3, 0x00, 0x10, 0x00, 0x00);

	arrancar();
	jit_x64_add64_mr(&e, X64_RAX, 0, X64_RSI);		/* add [rax], rsi */
	ESPERAR_EMITIDO(0x48, 0x01, 0x30);

	arrancar();
	jit_x64_sub64_ri(&e, X64_RSP, 40);				/* sub rsp, 28h */
	ESPERAR_EMITIDO(0x48, 0x83, 0xEC, 0x28);

	arrancar();
	jit_x64_add64_ri(&e, X64_RSP, 40);				/* add rsp, 28h */
	ESPERAR_EMITIDO(0x48, 0x83, 0xC4, 0x28);
}

/* SR.T vive en el bit 0 de un campo de bits: se toca el byte, no el registro,
   porque escribir SR entero pisaria S y el IMASK. */
static void el_bit_t_se_toca_por_bytes(void)
{
	arrancar();
	jit_x64_and_mi8(&e, X64_RBX, 8, 0xFE);			/* and byte [rbx+8], 0FEh */
	ESPERAR_EMITIDO(0x80, 0x63, 0x08, 0xFE);

	arrancar();
	jit_x64_or_mr8(&e, X64_RBX, 8, X64_RAX);		/* or byte [rbx+8], al */
	ESPERAR_EMITIDO(0x08, 0x43, 0x08);

	/* Con RSI de origen hace falta el REX pelado, o seria DH. */
	arrancar();
	jit_x64_or_mr8(&e, X64_RBX, 8, X64_RSI);		/* or byte [rbx+8], sil */
	ESPERAR_EMITIDO(0x40, 0x08, 0x73, 0x08);

	arrancar();
	jit_x64_test_mi8(&e, X64_RBX, 8, 1);			/* test byte [rbx+8], 1 */
	ESPERAR_EMITIDO(0xF6, 0x43, 0x08, 0x01);
}

static void comparaciones(void)
{
	arrancar();
	jit_x64_cmp_rr(&e, X64_R14, X64_R13);			/* cmp r14d, r13d */
	ESPERAR_EMITIDO(0x45, 0x39, 0xEE);

	arrancar();
	jit_x64_cmp_ri(&e, X64_RBP, 400);				/* cmp ebp, 190h */
	ESPERAR_EMITIDO(0x81, 0xFD, 0x90, 0x01, 0x00, 0x00);

	arrancar();										/* cmp dword [rbx+1000h], 0 */
	jit_x64_cmp_mi(&e, X64_RBX, 0x1000, 0);
	ESPERAR_EMITIDO(0x83, 0xBB, 0x00, 0x10, 0x00, 0x00, 0x00);

	arrancar();
	jit_x64_test_ri(&e, X64_RDI, 3);				/* test edi, 3 */
	ESPERAR_EMITIDO(0xF7, 0xC7, 0x03, 0x00, 0x00, 0x00);

	arrancar();
	jit_x64_setcc(&e, X64_A, X64_RAX);				/* seta al */
	ESPERAR_EMITIDO(0x0F, 0x97, 0xC0);

	arrancar();
	jit_x64_setcc(&e, X64_E, X64_RAX);				/* sete al */
	ESPERAR_EMITIDO(0x0F, 0x94, 0xC0);
}

static void llamadas_pilas_y_retorno(void)
{
	arrancar();										/* call qword [rbx+1000h] */
	jit_x64_call_m(&e, X64_RBX, 0x1000);
	ESPERAR_EMITIDO(0xFF, 0x93, 0x00, 0x10, 0x00, 0x00);

	arrancar();
	jit_x64_push(&e, X64_RBX);
	jit_x64_push(&e, X64_R12);
	jit_x64_pop(&e, X64_R15);
	jit_x64_pop(&e, X64_RBX);
	jit_x64_ret(&e);
	ESPERAR_EMITIDO(0x53, 0x41, 0x54, 0x41, 0x5F, 0x5B, 0xC3);
}

/*
	El CALL directo es cinco bytes y sin carga, pero solo si el destino cae a
	menos de 2 GB. **En este arbol no cae**: VirtualAlloc deja el arena a
	terabytes de la imagen, asi que lo que corre de verdad es la forma por
	memoria. Se prueban las dos ramas porque la que decide cual se usa es esta
	funcion, y de que conteste bien depende que el bloque no salte a la nada.
*/
static void la_llamada_directa_solo_si_alcanza(void)
{
	unsigned char * lejos;

	arrancar();
	ESPERAR_U32((unsigned) jit_x64_call_directo(&e, buf + 21), 1);
	ESPERAR_EMITIDO(0xE8, 0x10, 0x00, 0x00, 0x00);	/* rel32 = 21 - 5 */

	/* Un destino a 16 TB: no alcanza, no emite nada y lo dice. */
	arrancar();
	lejos = (unsigned char *) (((size_t) buf) ^ (size_t) 0x0000100000000000ull);
	ESPERAR_U32((unsigned) jit_x64_call_directo(&e, lejos), 0);
	ESPERAR_U32(jit_x64_largo(&e), 0);
	ESPERAR_U32((unsigned) e.desborde, 0);
}

/* El camino rapido de memoria en linea (gen_rapido_inicio en jit.c): la tabla
   indexada por el byte alto de la direccion y el acceso a base+desplazamiento.
   Es lo que la fase 0 midio que valia la mitad de la ganancia del bloque. */
static void el_acceso_por_tabla_indexada(void)
{
	arrancar();									/* mov rax,[rbx+rax*8+5ED3A0h] */
	jit_x64_mov64_rm_idx(&e, X64_RAX, X64_RBX, X64_RAX, 8, 0x5ED3A0);
	ESPERAR_EMITIDO(0x48, 0x8B, 0x84, 0xC3, 0xA0, 0xD3, 0x5E, 0x00);

	arrancar();									/* mov eax,[rax+r8] */
	jit_x64_mov_rm_idx(&e, X64_RAX, X64_RAX, X64_R8, 1, 0);
	ESPERAR_EMITIDO(0x42, 0x8B, 0x04, 0x00);

	arrancar();									/* movsx eax,byte [rax+r8] */
	jit_x64_movsx_b_rm_idx(&e, X64_RAX, X64_RAX, X64_R8, 1, 0);
	ESPERAR_EMITIDO(0x42, 0x0F, 0xBE, 0x04, 0x00);

	arrancar();									/* mov [rax+r8], r13b */
	jit_x64_mov8_mr_idx(&e, X64_RAX, X64_R8, 1, 0, X64_R13);
	ESPERAR_EMITIDO(0x46, 0x88, 0x2C, 0x00);

	arrancar();									/* mov [rax+r8], edx */
	jit_x64_mov_mr_idx(&e, X64_RAX, X64_R8, 1, 0, X64_RDX);
	ESPERAR_EMITIDO(0x42, 0x89, 0x14, 0x00);

	arrancar();
	jit_x64_test_ri8(&e, X64_RCX, 3);			/* test cl, 3 */
	ESPERAR_EMITIDO(0xF6, 0xC1, 0x03);

	arrancar();
	jit_x64_test64_rr(&e, X64_RAX, X64_RAX);	/* test rax, rax */
	ESPERAR_EMITIDO(0x48, 0x85, 0xC0);
}

/*
	El prologo exacto de los bloques de la fase 0. Su longitud --16 bytes-- y
	los desplazamientos de cada empuje son lo que la informacion de
	desenrollado declara; si el emisor cambiara de codificacion y esto no se
	notara, la primera falta adentro de un bloque se llevaria el proceso.
*/
static void el_prologo_de_los_bloques_mide_dieciseis_bytes(void)
{
	static const x64_reg empujados[8] =
	{
		X64_RBX, X64_RBP, X64_RSI, X64_RDI, X64_R12, X64_R13, X64_R14, X64_R15
	};
	static const unsigned esperados[8] = { 1, 2, 3, 4, 6, 8, 10, 12 };
	int i;

	arrancar();

	for (i = 0; i < 8; i++)
	{
		jit_x64_push(&e, empujados[i]);
		ESPERAR_U32(jit_x64_largo(&e), esperados[i]);
	}

	jit_x64_sub64_ri(&e, X64_RSP, 40);
	ESPERAR_U32(jit_x64_largo(&e), 16);

	ESPERAR_EMITIDO(
		0x53,				/* push rbx */
		0x55,				/* push rbp */
		0x56,				/* push rsi */
		0x57,				/* push rdi */
		0x41, 0x54,			/* push r12 */
		0x41, 0x55,			/* push r13 */
		0x41, 0x56,			/* push r14 */
		0x41, 0x57,			/* push r15 */
		0x48, 0x83, 0xEC, 0x28);	/* sub rsp, 28h */
}

static void saltos_hacia_adelante_y_hacia_atras(void)
{
	x64_parche p;
	unsigned char * atras;

	/* jcc corto sobre dos bytes de relleno. */
	arrancar();
	p = jit_x64_jcc_corto(&e, X64_E);
	jit_x64_ret(&e);
	jit_x64_ret(&e);
	jit_x64_fijar(&e, p);
	ESPERAR_EMITIDO(0x74, 0x02, 0xC3, 0xC3);

	/* jmp largo: el rel32 se mide desde el final de la instruccion. */
	arrancar();
	p = jit_x64_jmp(&e);
	jit_x64_ret(&e);
	jit_x64_fijar(&e, p);
	ESPERAR_EMITIDO(0xE9, 0x01, 0x00, 0x00, 0x00, 0xC3);

	/* jcc largo, con su prefijo de dos bytes. */
	arrancar();
	p = jit_x64_jcc(&e, X64_NE);
	jit_x64_fijar(&e, p);
	ESPERAR_EMITIDO(0x0F, 0x85, 0x00, 0x00, 0x00, 0x00);

	/* Hacia atras y cerca: forma corta, desplazamiento negativo. */
	arrancar();
	atras = jit_x64_aqui(&e);
	jit_x64_ret(&e);
	jit_x64_jmp_a(&e, atras);
	ESPERAR_EMITIDO(0xC3, 0xEB, 0xFD);
}

/* El desborde del buffer y un salto corto fuera de alcance tienen que dejar
   rastro: es lo unico que separa "el bloque no se emitio" de "el bloque quedo
   a medias y corre". */
static void el_desborde_se_reporta(void)
{
	unsigned char chico[3];
	x64_parche p;
	int i;

	jit_x64_iniciar(&e, chico, sizeof(chico));
	ESPERAR_U32((unsigned) e.desborde, 0);

	jit_x64_mov_ri(&e, X64_RCX, 0x11223344u);		/* 5 bytes en 3 */
	ESPERAR_U32((unsigned) e.desborde, 1);

	/* Un rel8 que no alcanza. */
	arrancar();
	p = jit_x64_jcc_corto(&e, X64_E);

	for (i = 0; i < 200; i++)
		jit_x64_ret(&e);

	ESPERAR_U32((unsigned) e.desborde, 0);
	jit_x64_fijar(&e, p);
	ESPERAR_U32((unsigned) e.desborde, 1);
}

/*
	La familia ALU completa, que es sobre lo que el traductor automatico monta
	sus plantillas: una sola tabla da las cinco formas de las ocho operaciones,
	y el opcode base de cada una es su numero de extension por ocho. Si esa
	relacion se rompiera, cada plantilla emitiria la operacion equivocada -- con
	la codificacion bien formada, que es lo peor: el guest divergiria sin que
	nada se queje.
*/
static void la_familia_alu_completa(void)
{
	arrancar();
	jit_x64_alu_rr(&e, X64_SUB, X64_RAX, X64_RCX);		/* sub eax, ecx */
	ESPERAR_EMITIDO(0x29, 0xC8);

	arrancar();
	jit_x64_alu_rm(&e, X64_OR, X64_RAX, X64_RBX, 0x10);	/* or eax, [rbx+10h] */
	ESPERAR_EMITIDO(0x0B, 0x43, 0x10);

	arrancar();											/* xor [rbx+20h], r12d */
	jit_x64_alu_mr(&e, X64_XOR, X64_RBX, 0x20, X64_R12);
	ESPERAR_EMITIDO(0x44, 0x31, 0x63, 0x20);

	arrancar();
	jit_x64_alu_ri(&e, X64_AND, X64_RDI, 0x7F);			/* and edi, 7Fh */
	ESPERAR_EMITIDO(0x83, 0xE7, 0x7F);

	arrancar();										/* cmp dword [rbx+10h], 0 */
	jit_x64_alu_mi(&e, X64_CMP, X64_RBX, 0x10, 0);
	ESPERAR_EMITIDO(0x83, 0x7B, 0x10, 0x00);

	arrancar();
	jit_x64_cmp_rm(&e, X64_RAX, X64_RBX, 8);			/* cmp eax, [rbx+8] */
	ESPERAR_EMITIDO(0x3B, 0x43, 0x08);

	arrancar();
	jit_x64_test_rr(&e, X64_RAX, X64_RCX);				/* test eax, ecx */
	ESPERAR_EMITIDO(0x85, 0xC8);

	arrancar();
	jit_x64_not_r(&e, X64_RAX);							/* not eax */
	ESPERAR_EMITIDO(0xF7, 0xD0);

	arrancar();
	jit_x64_neg_r(&e, X64_RCX);							/* neg ecx */
	ESPERAR_EMITIDO(0xF7, 0xD9);

	arrancar();
	jit_x64_imul_rri(&e, X64_RAX, X64_RAX, 28);			/* imul eax, eax, 28 */
	ESPERAR_EMITIDO(0x6B, 0xC0, 0x1C);

	arrancar();
	jit_x64_imul_rr(&e, X64_RAX, X64_RBX);				/* imul eax, ebx */
	ESPERAR_EMITIDO(0x0F, 0xAF, 0xC3);

	arrancar();
	jit_x64_imul_rr(&e, X64_R10, X64_RCX);				/* imul r10d, ecx */
	ESPERAR_EMITIDO(0x44, 0x0F, 0xAF, 0xD1);

	arrancar();
	jit_x64_imul_rm(&e, X64_RAX, X64_RBX, 0x40);		/* imul eax, [rbx+40h] */
	ESPERAR_EMITIDO(0x0F, 0xAF, 0x43, 0x40);

	arrancar();									/* add qword [rbx+1000h], 1 */
	jit_x64_add64_mi(&e, X64_RBX, 0x1000, 1);
	ESPERAR_EMITIDO(0x48, 0x83, 0x83, 0x00, 0x10, 0x00, 0x00, 0x01);
}

static void corrimientos_y_extensiones(void)
{
	arrancar();
	jit_x64_shift_ri(&e, X64_SHL, X64_R12, 2);			/* shl r12d, 2 */
	ESPERAR_EMITIDO(0x41, 0xC1, 0xE4, 0x02);

	/* Por uno hay forma propia, de dos bytes. */
	arrancar();
	jit_x64_shift_ri(&e, X64_SAR, X64_RAX, 1);			/* sar eax, 1 */
	ESPERAR_EMITIDO(0xD1, 0xF8);

	arrancar();
	jit_x64_movsx_b(&e, X64_RAX, X64_RCX);				/* movsx eax, cl */
	ESPERAR_EMITIDO(0x0F, 0xBE, 0xC1);

	/* Con RDI de fuente hace falta el REX pelado, o seria BH. */
	arrancar();
	jit_x64_movsx_b(&e, X64_RAX, X64_RDI);				/* movsx eax, dil */
	ESPERAR_EMITIDO(0x40, 0x0F, 0xBE, 0xC7);

	arrancar();
	jit_x64_movzx_w(&e, X64_RAX, X64_RCX);				/* movzx eax, cx */
	ESPERAR_EMITIDO(0x0F, 0xB7, 0xC1);
}

/* Las formas con indice que pide la cache de traducciones de la MMU, cuyo
   elemento no mide una potencia de dos: el indice viaja ya multiplicado. */
static void las_formas_con_indice_de_la_mmu(void)
{
	arrancar();								/* cmp edx, [rbx+r9+100h] */
	jit_x64_cmp_rm_idx(&e, X64_RDX, X64_RBX, X64_R9, 1, 0x100);
	ESPERAR_EMITIDO(0x42, 0x3B, 0x94, 0x0B, 0x00, 0x01, 0x00, 0x00);

	arrancar();								/* test dword [rbx+r9+10h], 1 */
	jit_x64_test_mi_idx(&e, X64_RBX, X64_R9, 1, 0x10, 1);
	ESPERAR_EMITIDO(0x42, 0xF7, 0x44, 0x0B, 0x10, 0x01, 0x00, 0x00, 0x00);

	arrancar();								/* or r11d, [rbx+r9+100h] */
	jit_x64_or_rm_idx(&e, X64_R11, X64_RBX, X64_R9, 1, 0x100);
	ESPERAR_EMITIDO(0x46, 0x0B, 0x9C, 0x0B, 0x00, 0x01, 0x00, 0x00);

	arrancar();								/* and eax, [rbx+r9+4] */
	jit_x64_and_rm_idx(&e, X64_RAX, X64_RBX, X64_R9, 1, 4);
	ESPERAR_EMITIDO(0x42, 0x23, 0x44, 0x0B, 0x04);

	arrancar();								/* movsx edx, word [rbx+r9+10h] */
	jit_x64_movsx_w_rm_idx(&e, X64_RDX, X64_RBX, X64_R9, 1, 0x10);
	ESPERAR_EMITIDO(0x42, 0x0F, 0xBF, 0x54, 0x0B, 0x10);

	arrancar();
	jit_x64_call_r(&e, X64_RAX);						/* call rax */
	ESPERAR_EMITIDO(0xFF, 0xD0);

	arrancar();
	jit_x64_call_r(&e, X64_R10);						/* call r10 */
	ESPERAR_EMITIDO(0x41, 0xFF, 0xD2);

	arrancar();								/* mov word [rax+r8], dx */
	jit_x64_mov16_mr_idx(&e, X64_RAX, X64_R8, 1, 0, X64_RDX);
	ESPERAR_EMITIDO(0x66, 0x42, 0x89, 0x14, 0x00);

	arrancar();
	jit_x64_mov64_mr(&e, X64_RCX, 0x10, X64_RAX);		/* mov [rcx+10h], rax */
	ESPERAR_EMITIDO(0x48, 0x89, 0x41, 0x10);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(movimientos_entre_registros),
	CASO(movimientos_con_memoria),
	CASO(las_bases_que_obligan_una_forma_especial),
	CASO(extensiones_de_signo_y_de_cero),
	CASO(aritmetica_y_logica),
	CASO(el_bit_t_se_toca_por_bytes),
	CASO(comparaciones),
	CASO(llamadas_pilas_y_retorno),
	CASO(la_llamada_directa_solo_si_alcanza),
	CASO(el_acceso_por_tabla_indexada),
	CASO(la_familia_alu_completa),
	CASO(corrimientos_y_extensiones),
	CASO(las_formas_con_indice_de_la_mmu),
	CASO(el_prologo_de_los_bloques_mide_dieciseis_bytes),
	CASO(saltos_hacia_adelante_y_hacia_atras),
	CASO(el_desborde_se_reporta),
};

const dc_suite suite_jit_x64 = DEFINIR_SUITE("jit_x64", casos);
