/****************************************************************************

	Pruebas de arith.c -- instrucciones aritmeticas

	Casos borde que cubre esta suite:

	  - ADD/SUB envuelven en 32 bits sin tocar T; ADDC/SUBC/NEGC leen T como
		acarreo de entrada y lo dejan como acarreo de salida. Se prueban los
		dos valores de T de entrada y el cruce por 0xFFFFFFFF.
	  - ADD #imm extiende el signo del inmediato de 8 bits: 0xFF es -1.
	  - CMP/HS y CMP/HI comparan sin signo; CMP/GE y CMP/GT con signo. Los
		casos usan 0xFFFFFFFF contra 1, que es donde las dos familias dan
		resultados opuestos.
	  - CMP/PL con Rn == 0 da T = 0 (estrictamente mayor), CMP/PZ da T = 1.
	  - CMP/STR compara byte a byte: el caso positivo hace coincidir un solo
		byte interior, no el ultimo.
	  - DT sobre 0 no se detiene en 0: envuelve a 0xFFFFFFFF con T = 0.
	  - DMULS.L con 0x80000000 x 0x80000000 es el caso donde negar el operando
		no cambia nada (0 - 0x80000000 = 0x80000000) y el resultado igual debe
		salir positivo (2^62).
	  - MULS.W y MULU.W solo miran los 16 bits bajos, y MUL.L se queda con los
		32 bits bajos del producto.
	  - DIV1 se prueba con la secuencia completa del manual (DIV0U + 32 pasos),
		que es como se usa de verdad; un paso suelto no dice nada.
	  - MAC.L acumula sobre MACH:MACL y avanza los dos punteros.

*****************************************************************************/

#include "arnes.h"
#include "suites.h"

/* --------------------------------------------------------------- ADD/ADDC */

static void add_rm_rn(void)
{
	arnes_reset();

	R(1) = 0x00000010;
	R(2) = 0x00000020;
	ejecutar(instr_nm(0x300C, 1, 2));	/* ADD R2, R1 */

	ESPERAR_U32(R(1), 0x00000030);
	ESPERAR_U32(R(2), 0x00000020);
	ESPERAR_T(0);
	ESPERAR_PC_SIGUIENTE();
}

static void add_envuelve_sin_tocar_t(void)
{
	arnes_reset();

	SR_T = 1;					/* ADD no toca T, ni siquiera al envolver */
	R(1) = 0xFFFFFFFF;
	R(2) = 0x00000002;
	ejecutar(instr_nm(0x300C, 1, 2));

	ESPERAR_U32(R(1), 0x00000001);
	ESPERAR_T(1);
}

static void add_mismo_registro_duplica(void)
{
	arnes_reset();

	R(3) = 0x11111111;
	ejecutar(instr_nm(0x300C, 3, 3));	/* ADD R3, R3 */

	ESPERAR_U32(R(3), 0x22222222);
}

static void add_inmediato_con_signo(void)
{
	arnes_reset();

	R(4) = 0x00000010;
	ejecutar(instr_ni(0x7000, 4, 0xFF));	/* ADD #-1, R4 */

	ESPERAR_U32(R(4), 0x0000000F);
	ESPERAR_PC_SIGUIENTE();
}

static void add_inmediato_positivo(void)
{
	arnes_reset();

	R(4) = 0;
	ejecutar(instr_ni(0x7000, 4, 0x7F));	/* ADD #127, R4 */

	ESPERAR_U32(R(4), 0x0000007F);
}

static void addc_genera_acarreo(void)
{
	arnes_reset();

	SR_T = 0;
	R(1) = 0xFFFFFFFF;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300E, 1, 2));	/* ADDC R2, R1 */

	ESPERAR_U32(R(1), 0x00000000);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void addc_suma_el_acarreo_de_entrada(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0x00000001;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300E, 1, 2));

	ESPERAR_U32(R(1), 0x00000003);
	ESPERAR_T(0);
}

static void addc_acarreo_solo_por_el_t_de_entrada(void)
{
	arnes_reset();

	/* 0xFFFFFFFF + 0 no desborda, pero + T si: el handler tiene que mirar las
	   dos sumas, no solo la primera. */
	SR_T = 1;
	R(1) = 0xFFFFFFFF;
	R(2) = 0x00000000;
	ejecutar(instr_nm(0x300E, 1, 2));

	ESPERAR_U32(R(1), 0x00000000);
	ESPERAR_T(1);
}

/* --------------------------------------------------------------- SUB/SUBC */

static void sub_rm_rn(void)
{
	arnes_reset();

	R(1) = 0x00000030;
	R(2) = 0x00000010;
	ejecutar(instr_nm(0x3008, 1, 2));	/* SUB R2, R1 */

	ESPERAR_U32(R(1), 0x00000020);
	ESPERAR_PC_SIGUIENTE();
}

static void sub_envuelve_sin_tocar_t(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0x00000000;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x3008, 1, 2));

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_T(1);
}

static void subc_genera_prestamo(void)
{
	arnes_reset();

	SR_T = 0;
	R(1) = 0x00000000;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300A, 1, 2));	/* SUBC R2, R1 */

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void subc_resta_el_prestamo_de_entrada(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0x00000003;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300A, 1, 2));

	ESPERAR_U32(R(1), 0x00000001);
	ESPERAR_T(0);
}

static void subc_prestamo_solo_por_el_t_de_entrada(void)
{
	arnes_reset();

	SR_T = 1;
	R(1) = 0x00000000;
	R(2) = 0x00000000;
	ejecutar(instr_nm(0x300A, 1, 2));

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_T(1);
}

/* ----------------------------------------------------------------- NEG(C) */

static void neg_rm_rn(void)
{
	arnes_reset();

	R(2) = 0x00000001;
	ejecutar(instr_nm(0x600B, 1, 2));	/* NEG R2, R1 */

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_PC_SIGUIENTE();
}

static void neg_de_0x80000000_se_queda_igual(void)
{
	arnes_reset();

	R(2) = 0x80000000;					/* el unico valor sin opuesto en 32 bits */
	ejecutar(instr_nm(0x600B, 1, 2));

	ESPERAR_U32(R(1), 0x80000000);
}

static void negc_sin_prestamo(void)
{
	arnes_reset();

	SR_T = 0;
	R(2) = 0x00000000;
	ejecutar(instr_nm(0x600A, 1, 2));	/* NEGC R2, R1 */

	ESPERAR_U32(R(1), 0x00000000);
	ESPERAR_T(0);
	ESPERAR_PC_SIGUIENTE();
}

static void negc_con_prestamo(void)
{
	arnes_reset();

	SR_T = 0;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x600A, 1, 2));

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_T(1);
}

static void negc_prestamo_solo_por_el_t_de_entrada(void)
{
	arnes_reset();

	SR_T = 1;
	R(2) = 0x00000000;
	ejecutar(instr_nm(0x600A, 1, 2));

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_T(1);
}

/* -------------------------------------------------------------------- CMP */

static void cmpeq_inmediato_extiende_signo(void)
{
	arnes_reset();

	R(0) = 0xFFFFFFFF;
	ejecutar(instr_i(0x8800, 0xFF));	/* CMP/EQ #-1, R0 */

	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void cmpeq_inmediato_distinto(void)
{
	arnes_reset();

	R(0) = 0x000000FF;
	ejecutar(instr_i(0x8800, 0xFF));	/* 0xFF con signo es -1, no 255 */

	ESPERAR_T(0);
}

static void cmpeq_rm_rn(void)
{
	arnes_reset();

	R(1) = 0x12345678;
	R(2) = 0x12345678;
	ejecutar(instr_nm(0x3000, 1, 2));

	ESPERAR_T(1);
}

static void cmphs_es_sin_signo(void)
{
	arnes_reset();

	R(1) = 0xFFFFFFFF;					/* sin signo es el mayor de todos */
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x3002, 1, 2));	/* CMP/HS R2, R1 */

	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void cmphs_iguales(void)
{
	arnes_reset();

	R(1) = 0x00000005;
	R(2) = 0x00000005;
	ejecutar(instr_nm(0x3002, 1, 2));

	ESPERAR_T(1);
}

static void cmpge_es_con_signo(void)
{
	arnes_reset();

	R(1) = 0xFFFFFFFF;					/* con signo es -1 */
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x3003, 1, 2));	/* CMP/GE R2, R1 */

	ESPERAR_T(0);
}

static void cmpge_iguales(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = 0x80000000;
	ejecutar(instr_nm(0x3003, 1, 2));

	ESPERAR_T(1);
}

static void cmphi_estricto(void)
{
	arnes_reset();

	R(1) = 0x00000005;
	R(2) = 0x00000005;
	ejecutar(instr_nm(0x3006, 1, 2));	/* CMP/HI R2, R1 */

	ESPERAR_T(0);
}

static void cmphi_es_sin_signo(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = 0x7FFFFFFF;
	ejecutar(instr_nm(0x3006, 1, 2));

	ESPERAR_T(1);
}

static void cmpgt_es_con_signo(void)
{
	arnes_reset();

	R(1) = 0x80000000;					/* el mas negativo */
	R(2) = 0x7FFFFFFF;					/* el mas positivo */
	ejecutar(instr_nm(0x3007, 1, 2));	/* CMP/GT R2, R1 */

	ESPERAR_T(0);
}

static void cmppz_con_cero(void)
{
	arnes_reset();

	R(1) = 0;
	ejecutar(instr_n(0x4011, 1));		/* CMP/PZ R1 */

	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void cmppz_negativo(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	ejecutar(instr_n(0x4011, 1));

	ESPERAR_T(0);
}

static void cmppl_con_cero(void)
{
	arnes_reset();

	R(1) = 0;
	ejecutar(instr_n(0x4015, 1));		/* CMP/PL R1: estrictamente mayor */

	ESPERAR_T(0);
	ESPERAR_PC_SIGUIENTE();
}

static void cmppl_positivo(void)
{
	arnes_reset();

	R(1) = 1;
	ejecutar(instr_n(0x4015, 1));

	ESPERAR_T(1);
}

static void cmpstr_byte_interior_coincide(void)
{
	arnes_reset();

	R(1) = 0x12345678;
	R(2) = 0xAABB56DD;					/* coincide el byte 1 (0x56) */
	ejecutar(instr_nm(0x200C, 1, 2));	/* CMP/STR R2, R1 */

	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void cmpstr_ningun_byte_coincide(void)
{
	arnes_reset();

	R(1) = 0x11223344;
	R(2) = 0x55667788;
	ejecutar(instr_nm(0x200C, 1, 2));

	ESPERAR_T(0);
}

static void cmpstr_byte_alto_coincide(void)
{
	arnes_reset();

	R(1) = 0x12000000;
	R(2) = 0x12FFFFFF;
	ejecutar(instr_nm(0x200C, 1, 2));

	ESPERAR_T(1);
}

/* --------------------------------------------------------------------- DT */

static void dt_llega_a_cero(void)
{
	arnes_reset();

	R(5) = 1;
	ejecutar(instr_n(0x4010, 5));		/* DT R5 */

	ESPERAR_U32(R(5), 0);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void dt_sigue_contando(void)
{
	arnes_reset();

	R(5) = 2;
	ejecutar(instr_n(0x4010, 5));

	ESPERAR_U32(R(5), 1);
	ESPERAR_T(0);
}

static void dt_desde_cero_envuelve(void)
{
	arnes_reset();

	R(5) = 0;
	ejecutar(instr_n(0x4010, 5));

	ESPERAR_U32(R(5), 0xFFFFFFFF);
	ESPERAR_T(0);
}

/* ------------------------------------------------------------- EXTS/EXTU */

static void extsb_extiende_el_signo(void)
{
	arnes_reset();

	R(2) = 0x123456FF;
	ejecutar(instr_nm(0x600E, 1, 2));	/* EXTS.B R2, R1 */

	ESPERAR_U32(R(1), 0xFFFFFFFF);
	ESPERAR_PC_SIGUIENTE();
}

static void extsb_positivo(void)
{
	arnes_reset();

	R(2) = 0x1234567F;
	ejecutar(instr_nm(0x600E, 1, 2));

	ESPERAR_U32(R(1), 0x0000007F);
}

static void extsw_extiende_el_signo(void)
{
	arnes_reset();

	R(2) = 0x12348000;
	ejecutar(instr_nm(0x600F, 1, 2));	/* EXTS.W R2, R1 */

	ESPERAR_U32(R(1), 0xFFFF8000);
}

static void extub_rellena_con_ceros(void)
{
	arnes_reset();

	R(2) = 0x123456FF;
	ejecutar(instr_nm(0x600C, 1, 2));	/* EXTU.B R2, R1 */

	ESPERAR_U32(R(1), 0x000000FF);
}

static void extuw_rellena_con_ceros(void)
{
	arnes_reset();

	R(2) = 0x12348000;
	ejecutar(instr_nm(0x600D, 1, 2));	/* EXTU.W R2, R1 */

	ESPERAR_U32(R(1), 0x00008000);
}

/* --------------------------------------------------------- multiplicacion */

static void mull_guarda_los_32_bits_bajos(void)
{
	arnes_reset();

	MACH = 0xDEADBEEF;					/* MUL.L no toca MACH */
	R(1) = 0x00010001;
	R(2) = 0x00010001;					/* producto = 0x0001_00020001 */
	ejecutar(instr_nm(0x0007, 1, 2));	/* MUL.L R2, R1 */

	ESPERAR_U32(MACL, 0x00020001);
	ESPERAR_U32(MACH, 0xDEADBEEF);
	ESPERAR_PC_SIGUIENTE();
}

static void mulsw_multiplica_con_signo(void)
{
	arnes_reset();

	R(1) = 0xFFFFFFFE;					/* los 16 bits bajos: -2 */
	R(2) = 0x00000003;
	ejecutar(instr_nm(0x200F, 1, 2));	/* MULS.W R2, R1 */

	ESPERAR_U32(MACL, 0xFFFFFFFA);		/* -6 */
	ESPERAR_PC_SIGUIENTE();
}

static void mulsw_ignora_la_parte_alta(void)
{
	arnes_reset();

	R(1) = 0x7FFF0002;					/* solo cuenta 0x0002 */
	R(2) = 0x12340003;					/* solo cuenta 0x0003 */
	ejecutar(instr_nm(0x200F, 1, 2));

	ESPERAR_U32(MACL, 0x00000006);
}

static void muluw_multiplica_sin_signo(void)
{
	arnes_reset();

	R(1) = 0x0000FFFF;
	R(2) = 0x0000FFFF;
	ejecutar(instr_nm(0x200E, 1, 2));	/* MULU.W R2, R1 */

	ESPERAR_U32(MACL, 0xFFFE0001);
	ESPERAR_PC_SIGUIENTE();
}

static void dmulsl_negativo(void)
{
	arnes_reset();

	R(1) = 0xFFFFFFFE;					/* -2 */
	R(2) = 0x00000003;
	ejecutar(instr_nm(0x300D, 1, 2));	/* DMULS.L R2, R1 */

	ESPERAR_U32(MACH, 0xFFFFFFFF);
	ESPERAR_U32(MACL, 0xFFFFFFFA);		/* -6 en 64 bits */
	ESPERAR_PC_SIGUIENTE();
}

static void dmulsl_maximo_positivo(void)
{
	arnes_reset();

	R(1) = 0x7FFFFFFF;
	R(2) = 0x7FFFFFFF;
	ejecutar(instr_nm(0x300D, 1, 2));

	ESPERAR_U32(MACH, 0x3FFFFFFF);
	ESPERAR_U32(MACL, 0x00000001);
}

static void dmulsl_minimo_por_minimo(void)
{
	arnes_reset();

	/* 0x80000000 es su propio opuesto: negarlo para tomar el valor absoluto
	   no cambia nada, y el resultado igual tiene que salir positivo (2^62). */
	R(1) = 0x80000000;
	R(2) = 0x80000000;
	ejecutar(instr_nm(0x300D, 1, 2));

	ESPERAR_U32(MACH, 0x40000000);
	ESPERAR_U32(MACL, 0x00000000);
}

static void dmulul_maximo(void)
{
	arnes_reset();

	R(1) = 0xFFFFFFFF;
	R(2) = 0xFFFFFFFF;
	ejecutar(instr_nm(0x3005, 1, 2));	/* DMULU.L R2, R1 */

	ESPERAR_U32(MACH, 0xFFFFFFFE);
	ESPERAR_U32(MACL, 0x00000001);
	ESPERAR_PC_SIGUIENTE();
}

/* ------------------------------------------------------------------ DIV0 */

static void div0s_copia_los_signos(void)
{
	arnes_reset();

	R(1) = 0x80000000;					/* Rn negativo -> Q = 1 */
	R(2) = 0x00000001;					/* Rm positivo  -> M = 0 */
	ejecutar(instr_nm(0x2007, 1, 2));	/* DIV0S R2, R1 */

	ESPERAR_U32(SR_Q, 1);
	ESPERAR_U32(SR_M, 0);
	ESPERAR_T(1);						/* T = Q ^ M */
	ESPERAR_PC_SIGUIENTE();
}

static void div0s_mismos_signos(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = 0x80000000;
	ejecutar(instr_nm(0x2007, 1, 2));

	ESPERAR_U32(SR_Q, 1);
	ESPERAR_U32(SR_M, 1);
	ESPERAR_T(0);
}

static void div0u_apaga_q_m_y_t(void)
{
	arnes_reset();

	SR_Q = 1;
	SR_M = 1;
	SR_T = 1;
	ejecutar(0x0019);					/* DIV0U */

	ESPERAR_U32(SR_Q, 0);
	ESPERAR_U32(SR_M, 0);
	ESPERAR_T(0);
	ESPERAR_PC_SIGUIENTE();
}

/* --------------------------------------------------------------- DIV1 ---

   DIV1 hace un paso de division sin restauracion. El manual del SH-4 termina
   el paso con  T = (Q == M);  div1s52() en arith.c pone  T = Q && M,  con la
   linea original comentada justo abajo. Las dos formulas coinciden mientras
   M = 1, y difieren en todo el tramo M = 0, que es el que usa la division sin
   signo: ahi T queda siempre en 0 y el cociente sale 0.

   Los casos de un paso aislan la formula; los de la secuencia completa
   muestran la consecuencia. La secuencia corre dos veces, una tal cual y otra
   corrigiendo T desde C despues de cada DIV1: si la version corregida da el
   cociente correcto, el resto del paso (el sumar/restar y el calculo de Q)
   esta bien y lo unico roto es la formula de T. */

static void paso_div1(int corregir_t)
{
	ejecutar(instr_nm(0x3004, 1, 0));	/* DIV1 R0, R1 */

	if (corregir_t)
		SR_T = (SR_Q == SR_M) ? 1 : 0;
}

/* Secuencia del manual para dividendo de 64 bits sin signo (R1:R2) entre un
   divisor de 32 bits (R0): DIV0U y 32 veces ROTCL R2 + DIV1 R0,R1, con un
   ROTCL R2 final. Al terminar, R2 tiene el cociente y R1 el resto. */
static void dividir_sin_signo(DWORD dividendo, DWORD divisor, int corregir_t)
{
	int i;

	R(0) = divisor;
	R(1) = 0;			/* parte alta del dividendo */
	R(2) = dividendo;	/* parte baja */

	ejecutar(0x0019);						/* DIV0U */

	for (i = 0; i < 32; i++)
	{
		ejecutar(instr_n(0x4024, 2));		/* ROTCL R2 */
		paso_div1(corregir_t);
	}

	ejecutar(instr_n(0x4024, 2));			/* ROTCL R2 */

	/* DIV1 hace division sin restauracion: el ultimo paso puede haber restado
	   de mas y dejar el resto negativo. Devolverlo al rango [0, divisor) es
	   responsabilidad del codigo que llama, no de la instruccion. Se hace aqui
	   desde C porque es andamiaje de la prueba, no lo que se esta midiendo. */
	if ((signed long) R(1) < 0)
		R(1) += R(0);
}

static void div1_un_paso_con_divisor_negativo(void)
{
	arnes_reset();

	R(0) = 0x80000000;					/* divisor negativo -> M = 1 */
	R(1) = 0x00000000;					/* resto parcial positivo -> Q = 0 */
	ejecutar(instr_nm(0x2007, 1, 0));	/* DIV0S R0, R1 */

	ESPERAR_U32(SR_M, 1);
	ESPERAR_T(1);						/* T = Q ^ M */

	ejecutar(instr_nm(0x3004, 1, 0));	/* DIV1 R0, R1 */

	ESPERAR_U32(R(1), 0x80000001);
	ESPERAR_U32(SR_Q, 1);
	ESPERAR_U32(SR_M, 1);
	ESPERAR_T(1);						/* Q == M */
}

static void div1_un_paso_con_divisor_positivo(void)
{
	arnes_reset();

	ejecutar(0x0019);					/* DIV0U: Q = M = T = 0 */

	R(0) = 0x00000001;					/* divisor */
	R(1) = 0x40000000;					/* al desplazar queda 0x80000000 */

	ejecutar(instr_nm(0x3004, 1, 0));	/* DIV1 R0, R1 */

	ESPERAR_U32(R(1), 0x7FFFFFFF);		/* 0x80000000 - 1 */
	ESPERAR_U32(SR_Q, 0);
	ESPERAR_U32(SR_M, 0);
	ESPERAR_T(1);						/* Q == M == 0, el manual pide T = 1 */
}

static void div1_secuencia_con_t_corregido(void)
{
	arnes_reset();
	dividir_sin_signo(100, 7, 1);
	ESPERAR_U32(R(2), 14);
	ESPERAR_U32(R(1), 2);

	arnes_reset();
	dividir_sin_signo(100, 5, 1);
	ESPERAR_U32(R(2), 20);
	ESPERAR_U32(R(1), 0);

	arnes_reset();
	dividir_sin_signo(0xFFFFFFFF, 0x10000, 1);
	ESPERAR_U32(R(2), 0x0000FFFF);
	ESPERAR_U32(R(1), 0x0000FFFF);

	arnes_reset();
	dividir_sin_signo(5, 100, 1);
	ESPERAR_U32(R(2), 0);
	ESPERAR_U32(R(1), 5);
}

static void div1_division_exacta(void)
{
	arnes_reset();

	dividir_sin_signo(100, 5, 0);

	ESPERAR_U32(R(2), 20);	/* cociente */
	ESPERAR_U32(R(1), 0);	/* resto */
}

static void div1_division_con_resto(void)
{
	arnes_reset();

	dividir_sin_signo(100, 7, 0);

	ESPERAR_U32(R(2), 14);
	ESPERAR_U32(R(1), 2);
}

static void div1_divisor_grande(void)
{
	arnes_reset();

	dividir_sin_signo(0xFFFFFFFF, 0x10000, 0);

	ESPERAR_U32(R(2), 0xFFFF);
	ESPERAR_U32(R(1), 0xFFFF);
}

/* Este es el unico caso de division sin signo que pasa con DIV1 como esta: el
   cociente correcto ya es 0, que es lo que devuelve siempre la version rota.
   Se deja como recordatorio de que "pasa" no siempre quiere decir "anda". */
static void div1_divisor_mayor_que_el_dividendo(void)
{
	arnes_reset();

	dividir_sin_signo(5, 100, 0);

	ESPERAR_U32(R(2), 0);
	ESPERAR_U32(R(1), 5);
}

/* ------------------------------------------------------------------ MAC.L */

static void macl_acumula_y_avanza_los_punteros(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 0, 3);
	escribir_l(PRUEBA_DATOS + 8, 5);

	R(1) = PRUEBA_DATOS + 8;	/* @Rn+ */
	R(2) = PRUEBA_DATOS + 0;	/* @Rm+ */
	MACH = 0;
	MACL = 10;

	ejecutar(instr_nm(0x000F, 1, 2));	/* MAC.L @R2+, @R1+ */

	ESPERAR_U32(MACL, 25);				/* 10 + 3*5 */
	ESPERAR_U32(MACH, 0);
	ESPERAR_U32(R(1), PRUEBA_DATOS + 12);
	ESPERAR_U32(R(2), PRUEBA_DATOS + 4);
	ESPERAR_PC_SIGUIENTE();
}

static void macl_con_operandos_negativos(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 0, (DWORD) -3);
	escribir_l(PRUEBA_DATOS + 8, 5);

	R(1) = PRUEBA_DATOS + 8;
	R(2) = PRUEBA_DATOS + 0;
	MACH = 0;
	MACL = 0;

	ejecutar(instr_nm(0x000F, 1, 2));

	ESPERAR_U32(MACL, (DWORD) -15);
	ESPERAR_U32(MACH, 0xFFFFFFFF);
}

static void macl_arrastra_hacia_mach(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 0, 0x00010000);
	escribir_l(PRUEBA_DATOS + 8, 0x00010000);	/* producto = 2^32 */

	R(1) = PRUEBA_DATOS + 8;
	R(2) = PRUEBA_DATOS + 0;
	MACH = 0;
	MACL = 0;

	ejecutar(instr_nm(0x000F, 1, 2));

	ESPERAR_U32(MACL, 0x00000000);
	ESPERAR_U32(MACH, 0x00000001);
}

/* Con S = 1 el resultado se satura a 48 bits: MACH conserva sus bits 31-16 y
   solo los 16 bajos son parte del acumulador. */
static void macl_saturado_conserva_la_parte_alta_de_mach(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 0, 0x40000000);
	escribir_l(PRUEBA_DATOS + 8, 0x40000000);	/* producto = 2^60, se pasa */

	R(1) = PRUEBA_DATOS + 8;
	R(2) = PRUEBA_DATOS + 0;
	MACH = 0x12340000;
	MACL = 0;
	SR_S = 1;

	ejecutar(instr_nm(0x000F, 1, 2));

	ESPERAR_U32(MACL, 0xFFFFFFFF);
	ESPERAR_U32(MACH, 0x12347FFF);
}

/* Con n == m el manual lee @Rn+, incrementa, y recien despues lee @Rm+: son
   dos posiciones consecutivas y el registro avanza 8. */
static void macl_con_el_mismo_registro_lee_dos_posiciones(void)
{
	arnes_reset();

	escribir_l(PRUEBA_DATOS + 0, 3);
	escribir_l(PRUEBA_DATOS + 4, 5);

	R(1) = PRUEBA_DATOS;
	MACH = 0;
	MACL = 0;

	ejecutar(instr_nm(0x000F, 1, 1));	/* MAC.L @R1+, @R1+ */

	ESPERAR_U32(MACL, 15);
	ESPERAR_U32(R(1), PRUEBA_DATOS + 8);
}

/* ------------------------------------------------------------ ADDV / SUBV */

static void addv_desborda_por_arriba(void)
{
	arnes_reset();

	R(1) = 0x7FFFFFFF;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300F, 1, 2));	/* ADDV R2, R1 */

	ESPERAR_U32(R(1), 0x80000000);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void addv_desborda_por_abajo(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = 0xFFFFFFFF;					/* -1 */
	ejecutar(instr_nm(0x300F, 1, 2));

	ESPERAR_U32(R(1), 0x7FFFFFFF);
	ESPERAR_T(1);
}

static void addv_sin_desborde(void)
{
	arnes_reset();

	R(1) = 0x00000001;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300F, 1, 2));

	ESPERAR_U32(R(1), 0x00000002);
	ESPERAR_T(0);
}

/* Sumar dos numeros de signo distinto nunca puede desbordar, aunque el
   resultado envuelva el rango sin signo. */
static void addv_con_signos_distintos_no_desborda(void)
{
	arnes_reset();

	R(1) = 0x7FFFFFFF;
	R(2) = 0xFFFFFFFF;
	ejecutar(instr_nm(0x300F, 1, 2));

	ESPERAR_U32(R(1), 0x7FFFFFFE);
	ESPERAR_T(0);
}

static void subv_desborda_por_abajo(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = 0x00000001;
	ejecutar(instr_nm(0x300B, 1, 2));	/* SUBV R2, R1 */

	ESPERAR_U32(R(1), 0x7FFFFFFF);
	ESPERAR_T(1);
	ESPERAR_PC_SIGUIENTE();
}

static void subv_desborda_por_arriba(void)
{
	arnes_reset();

	R(1) = 0x7FFFFFFF;
	R(2) = 0xFFFFFFFF;					/* max - (-1) */
	ejecutar(instr_nm(0x300B, 1, 2));

	ESPERAR_U32(R(1), 0x80000000);
	ESPERAR_T(1);
}

/* Restar dos numeros del mismo signo nunca desborda. */
static void subv_con_signos_iguales_no_desborda(void)
{
	arnes_reset();

	R(1) = 0x80000000;
	R(2) = 0x80000000;
	ejecutar(instr_nm(0x300B, 1, 2));

	ESPERAR_U32(R(1), 0x00000000);
	ESPERAR_T(0);
}

/* ------------------------------------------------------------------ MAC.W */

static void macw_acumula_y_avanza_los_punteros(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS + 0, 3);
	escribir_w(PRUEBA_DATOS + 8, 5);

	R(1) = PRUEBA_DATOS + 8;			/* @Rn+ */
	R(2) = PRUEBA_DATOS + 0;			/* @Rm+ */
	MACH = 0;
	MACL = 10;

	ejecutar(instr_nm(0x400F, 1, 2));	/* MAC.W @R2+, @R1+ */

	ESPERAR_U32(MACL, 25);				/* 10 + 3*5 */
	ESPERAR_U32(MACH, 0);
	ESPERAR_U32(R(1), PRUEBA_DATOS + 10);	/* avanza 2, no 4 */
	ESPERAR_U32(R(2), PRUEBA_DATOS + 2);
	ESPERAR_PC_SIGUIENTE();
}

static void macw_extiende_el_signo_de_los_operandos(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS + 0, 0xFFFD);	/* -3 */
	escribir_w(PRUEBA_DATOS + 8, 5);

	R(1) = PRUEBA_DATOS + 8;
	R(2) = PRUEBA_DATOS + 0;
	MACH = 0;
	MACL = 0;

	ejecutar(instr_nm(0x400F, 1, 2));

	ESPERAR_U32(MACL, (DWORD) -15);
	ESPERAR_U32(MACH, 0xFFFFFFFF);		/* la extension de signo llega a MACH */
}

/* Con S=1 el acumulador de MAC.W es de 32 bits y se satura en MACL. */
static void macw_saturado_por_arriba(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS + 0, 3);
	escribir_w(PRUEBA_DATOS + 8, 5);

	R(1) = PRUEBA_DATOS + 8;
	R(2) = PRUEBA_DATOS + 0;
	MACL = 0x7FFFFFFF;
	SR_S = 1;

	ejecutar(instr_nm(0x400F, 1, 2));

	ESPERAR_U32(MACL, 0x7FFFFFFF);
}

static void macw_saturado_por_abajo(void)
{
	arnes_reset();

	escribir_w(PRUEBA_DATOS + 0, 0xFFFD);	/* -3 */
	escribir_w(PRUEBA_DATOS + 8, 5);

	R(1) = PRUEBA_DATOS + 8;
	R(2) = PRUEBA_DATOS + 0;
	MACL = 0x80000000;
	SR_S = 1;

	ejecutar(instr_nm(0x400F, 1, 2));

	ESPERAR_U32(MACL, 0x80000000);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(add_rm_rn),
	CASO(add_envuelve_sin_tocar_t),
	CASO(add_mismo_registro_duplica),
	CASO(add_inmediato_con_signo),
	CASO(add_inmediato_positivo),
	CASO(addc_genera_acarreo),
	CASO(addc_suma_el_acarreo_de_entrada),
	CASO(addc_acarreo_solo_por_el_t_de_entrada),
	CASO(sub_rm_rn),
	CASO(sub_envuelve_sin_tocar_t),
	CASO(subc_genera_prestamo),
	CASO(subc_resta_el_prestamo_de_entrada),
	CASO(subc_prestamo_solo_por_el_t_de_entrada),
	CASO(neg_rm_rn),
	CASO(neg_de_0x80000000_se_queda_igual),
	CASO(negc_sin_prestamo),
	CASO(negc_con_prestamo),
	CASO(negc_prestamo_solo_por_el_t_de_entrada),
	CASO(cmpeq_inmediato_extiende_signo),
	CASO(cmpeq_inmediato_distinto),
	CASO(cmpeq_rm_rn),
	CASO(cmphs_es_sin_signo),
	CASO(cmphs_iguales),
	CASO(cmpge_es_con_signo),
	CASO(cmpge_iguales),
	CASO(cmphi_estricto),
	CASO(cmphi_es_sin_signo),
	CASO(cmpgt_es_con_signo),
	CASO(cmppz_con_cero),
	CASO(cmppz_negativo),
	CASO(cmppl_con_cero),
	CASO(cmppl_positivo),
	CASO(cmpstr_byte_interior_coincide),
	CASO(cmpstr_ningun_byte_coincide),
	CASO(cmpstr_byte_alto_coincide),
	CASO(dt_llega_a_cero),
	CASO(dt_sigue_contando),
	CASO(dt_desde_cero_envuelve),
	CASO(extsb_extiende_el_signo),
	CASO(extsb_positivo),
	CASO(extsw_extiende_el_signo),
	CASO(extub_rellena_con_ceros),
	CASO(extuw_rellena_con_ceros),
	CASO(mull_guarda_los_32_bits_bajos),
	CASO(mulsw_multiplica_con_signo),
	CASO(mulsw_ignora_la_parte_alta),
	CASO(muluw_multiplica_sin_signo),
	CASO(dmulsl_negativo),
	CASO(dmulsl_maximo_positivo),
	CASO(dmulsl_minimo_por_minimo),
	CASO(dmulul_maximo),
	CASO(div0s_copia_los_signos),
	CASO(div0s_mismos_signos),
	CASO(div0u_apaga_q_m_y_t),
	CASO(div1_un_paso_con_divisor_negativo),
	CASO(div1_secuencia_con_t_corregido),
	CASO(div1_un_paso_con_divisor_positivo),
	CASO(div1_division_exacta),
	CASO(div1_division_con_resto),
	CASO(div1_divisor_grande),
	CASO(div1_divisor_mayor_que_el_dividendo),
	CASO(macl_acumula_y_avanza_los_punteros),
	CASO(macl_con_operandos_negativos),
	CASO(macl_arrastra_hacia_mach),
	CASO(macl_saturado_conserva_la_parte_alta_de_mach),
	CASO(macl_con_el_mismo_registro_lee_dos_posiciones),
	CASO(addv_desborda_por_arriba),
	CASO(addv_desborda_por_abajo),
	CASO(addv_sin_desborde),
	CASO(addv_con_signos_distintos_no_desborda),
	CASO(subv_desborda_por_abajo),
	CASO(subv_desborda_por_arriba),
	CASO(subv_con_signos_iguales_no_desborda),
	CASO(macw_acumula_y_avanza_los_punteros),
	CASO(macw_extiende_el_signo_de_los_operandos),
	CASO(macw_saturado_por_arriba),
	CASO(macw_saturado_por_abajo),
};

const dc_suite suite_arith = DEFINIR_SUITE("arith", casos);
