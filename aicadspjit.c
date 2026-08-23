/****************************************************************************

	AICADSPJIT - el microprograma del DSP de efectos, emitido a x86-64

	El tercer emisor del arbol, con el emisor jit_x64.c de los otros dos. El
	programa del DSP es fijo entre escrituras (la regla de dsp_sucio: todo
	escritor de 0x2800-0x3BFF pasa por aicadsp_tocar()), asi que se emite UNA
	vez por reconstruccion y corre una vez por muestra. Lo que este archivo
	quita es lo que el cuerpo C no puede: las ~24 cargas de campos de la
	tabla por paso (aca son inmediatos), la seleccion de ysel/shift/fuente
	por paso (aca se resuelve al emitir), el coeficiente y MADRS[masa] como
	constantes, y los registros que sobreviven de un paso al otro (acc,
	shifted, frc, y, dec) viviendo en registros del anfitrion. La leccion del
	cuerpo rapido (2026-08-21, revertido) dice que las ramas no eran el costo
	-- el predictor se aprende la secuencia --; el costo nombrado fue la
	cadena MAC **con sus cargas**, y esto elimina las cargas.

	Convenciones del codigo emitido (ABI de Windows x64):

	  - RSI: &aicadsp_est (todo el estado, aicadsp.h); RDI: sound_mem.
	  - EBX: acc; R12D: shifted; R13D: dec; R14D: frc; R15D: y. Todos
	    callee-saved: sobreviven a las llamadas a empacar/desempacar.
	  - EAX: la entrada del paso (volatil: se derrama a aicadsp_est.entrada
	    cuando una llamada la pisa y un adrl posterior la necesita).
	  - la funcion es `void fn(void)`: el envoltorio C (aicadsp_paso) pone
	    MIXS/EFREG y el decremento de dec, igual que alrededor del lazo C.

	La aritmetica es la del cuerpo C transcrita operacion por operacion --
	signo24 es shl 8 / sar 8, el MAC es movsxd + imul de 64 + sar 12, el
	recorte son dos ramas cortas -- y la baranda es la suite: cada programa
	corre por el lazo C y por el emitido y el estado entero tiene que salir
	identico (tests/test_dsp.c), mas el .wav de Crazy Taxi byte a byte.

	DCEMU_SIN_JIT_DSP=1 no instala el emisor: el DSP queda en el cuerpo C,
	que es el A/B.

*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* Como en arm7jit.c: DWORD lo trae <windows.h> y aca no hay nada de SDL. */
#include <windows.h>

#include "aica.h"
#include "aicadsp.h"
#include "aicadspjit.h"
#include "jit_x64.h"

/* ------------------------------------------------------------------------ */

#define DJ_ARENA_TAM	(64u * 1024)

#define DJ_OFF(campo)	((int) offsetof(struct aicadsp_est, campo))

static unsigned char *	dj_arena = NULL;

static unsigned long long	dj_emitidos   = 0;
static unsigned long long	dj_declinados = 0;

/* ------------------------------------------------------------------------ */
/* Piezas                                                                   */
/* ------------------------------------------------------------------------ */

/* Extension de signo desde 24 bits, como signo24(): identica para todo
   valor de 32 bits (descarta los 8 altos y extiende el bit 23). */
static void dj_signo24(x64_emisor * e, x64_reg r)
{
	jit_x64_shl_ri(e, r, 8);
	jit_x64_shift_ri(e, X64_SAR, r, 8);
}

/* recortar24(): el recorte a [-0x800000, 0x7FFFFF] con dos ramas cortas.
   El audio casi nunca satura, asi que las dos van casi siempre tomadas al
   camino recto. */
static void dj_recortar24(x64_emisor * e, x64_reg r)
{
	x64_parche le, ge, salto;

	jit_x64_cmp_ri(e, r, 0x007FFFFF);
	le = jit_x64_jcc_corto(e, X64_LE);
	jit_x64_mov_ri(e, r, 0x007FFFFF);
	salto = jit_x64_jmp(e);
	jit_x64_fijar(e, le);
	jit_x64_cmp_ri(e, r, (int) 0xFF800000);
	ge = jit_x64_jcc_corto(e, X64_GE);
	jit_x64_mov_ri(e, r, 0xFF800000u);
	jit_x64_fijar(e, ge);
	jit_x64_fijar(e, salto);
}

/* dst = signo24(temp[(base_tra + dec) & 0x7F]) -- la lectura de la linea de
   retardo, con el indice armado en el propio dst. */
static void dj_leer_temp(x64_emisor * e, x64_reg dst, int base_tra)
{
	jit_x64_mov_rr(e, dst, X64_R13);
	jit_x64_alu_ri(e, X64_ADD, dst, base_tra);
	jit_x64_and_ri(e, dst, 0x7F);
	jit_x64_mov_rm_idx(e, dst, X64_RSI, dst, 4, DJ_OFF(temp));
	dj_signo24(e, dst);
}

/* CALL a un destino absoluto, como aj_llamar(): rel32 si alcanza, y si no
   por R10, que no esta vivo en ningun sitio de llamada de este emisor. */
static void dj_llamar(x64_emisor * e, const void * fn)
{
	if (!jit_x64_call_directo(e, fn))
	{
		jit_x64_mov64_ri(e, X64_R10, (unsigned long long) (size_t) fn);
		jit_x64_call_r(e, X64_R10);
	}
}

/* ------------------------------------------------------------------------ */
/* El paso                                                                  */
/* ------------------------------------------------------------------------ */

static void dj_paso(x64_emisor * e, const aicadsp_paso_dec * d, int paso)
{
	/* Que consume cada cosa, resuelto al emitir. `entrada` es local del paso
	   en el cuerpo C (siempre asignada antes de usarse), asi que elidirla
	   cuando nadie la mira es exacto; lo mismo con `shifted`. */
	int usa_entrada = d->xsel || d->yrl || (d->adrl && d->shift != 3);
	int usa_shifted = d->twt || d->frcl || d->mwt || d->ewt
					|| (d->adrl && d->shift == 3);
	int usa_temp    = (!d->zero && !d->bsel) || !d->xsel;
	int llama       = (d->mrd && !d->nofl) || (d->mwt && !d->nofl);
	int derrama     = llama && d->adrl && d->shift != 3;

	/* La entrada del paso, en EAX. */
	if (usa_entrada)
	{
		if (d->ira <= 0x1F)
			jit_x64_mov_rm(e, X64_RAX, X64_RSI, DJ_OFF(mems) + d->ira * 4);
		else if (d->ira <= 0x2F)
		{
			jit_x64_mov_rm(e, X64_RAX, X64_RSI,
				DJ_OFF(mixs) + (d->ira - 0x20) * 4);
			jit_x64_shl_ri(e, X64_RAX, 4);
		}
		else if (d->ira <= 0x31)
		{
			jit_x64_mov_rm(e, X64_RAX, X64_RSI,
				DJ_OFF(exts) + (d->ira - 0x30) * 4);
			jit_x64_shl_ri(e, X64_RAX, 8);
		}
		else
			jit_x64_xor_rr(e, X64_RAX, X64_RAX);

		dj_signo24(e, X64_RAX);
	}

	/* El IWT entrega lo que trajo el MRD de hace dos pasos. */
	if (d->iwt)
	{
		jit_x64_mov_rm(e, X64_RCX, X64_RSI, DJ_OFF(memval) + (paso & 3) * 4);
		jit_x64_mov_mr(e, X64_RSI, DJ_OFF(mems) + d->iwa * 4, X64_RCX);

		/* Sin signo24, como el cuerpo C: memval ya viene extendido. */
		if (d->ira == d->iwa && usa_entrada)
			jit_x64_mov_rr(e, X64_RAX, X64_RCX);
	}

	/* La lectura compartida de TEMP (b y x piden el mismo valor). */
	if (usa_temp)
		dj_leer_temp(e, X64_RDX, d->tra);

	/* b, en R8D. */
	if (d->zero)
		jit_x64_xor_rr(e, X64_R8, X64_R8);
	else
	{
		if (d->bsel)
			jit_x64_mov_rr(e, X64_R8, X64_RBX);
		else
			jit_x64_mov_rr(e, X64_R8, X64_RDX);

		if (d->negb)
			jit_x64_neg_r(e, X64_R8);
	}

	/* x, en EDX: o la entrada, o el TEMP que ya esta ahi. */
	if (d->xsel)
		jit_x64_mov_rr(e, X64_RDX, X64_RAX);

	/* y, en ECX, con su recorte a 13 bits con signo resuelto al emitir:
	   el coeficiente ya viene extendido de la tabla (es identidad), y el
	   caso 3 produce 12 bits, donde tambien lo es. */
	switch (d->ysel)
	{
		case 0:
			jit_x64_mov_rr(e, X64_RCX, X64_R14);
			jit_x64_shl_ri(e, X64_RCX, 19);
			jit_x64_shift_ri(e, X64_SAR, X64_RCX, 19);
			break;
		case 1:
			jit_x64_mov_ri(e, X64_RCX, (unsigned) d->coef);
			break;
		case 2:
			jit_x64_mov_rr(e, X64_RCX, X64_R15);
			jit_x64_shift_ri(e, X64_SAR, X64_RCX, 11);
			jit_x64_and_ri(e, X64_RCX, 0x1FFF);
			jit_x64_shl_ri(e, X64_RCX, 19);
			jit_x64_shift_ri(e, X64_SAR, X64_RCX, 19);
			break;
		default:
			jit_x64_mov_rr(e, X64_RCX, X64_R15);
			jit_x64_shift_ri(e, X64_SAR, X64_RCX, 4);
			jit_x64_and_ri(e, X64_RCX, 0x0FFF);
			break;
	}

	if (d->yrl)
		jit_x64_mov_rr(e, X64_R15, X64_RAX);

	/* El desplazador mira el acumulador del paso ANTERIOR (EBX todavia). */
	if (usa_shifted)
	{
		jit_x64_mov_rr(e, X64_R12, X64_RBX);

		switch (d->shift)
		{
			case 0:
				dj_recortar24(e, X64_R12);
				break;
			case 1:
				jit_x64_add_rr(e, X64_R12, X64_R12);
				dj_recortar24(e, X64_R12);
				break;
			case 2:
				jit_x64_add_rr(e, X64_R12, X64_R12);
				dj_signo24(e, X64_R12);
				break;
			default:
				dj_signo24(e, X64_R12);
				break;
		}
	}

	/* Multiplica-acumula: X de 24, Y de 13, producto de 37 -> ancho de 64,
	   corrido 12 y truncado a 32 como el (long) del cuerpo C. */
	jit_x64_movsxd_rr(e, X64_RDX, X64_RDX);
	jit_x64_movsxd_rr(e, X64_RCX, X64_RCX);
	jit_x64_imul64_rr(e, X64_RDX, X64_RCX);
	jit_x64_shift64_ri(e, X64_SAR, X64_RDX, 12);
	jit_x64_mov_rr(e, X64_RBX, X64_RDX);
	jit_x64_add_rr(e, X64_RBX, X64_R8);

	if (d->twt)
	{
		jit_x64_mov_rr(e, X64_RCX, X64_R13);
		jit_x64_alu_ri(e, X64_ADD, X64_RCX, d->twa);
		jit_x64_and_ri(e, X64_RCX, 0x7F);
		jit_x64_mov_mr_idx(e, X64_RSI, X64_RCX, 4, DJ_OFF(temp), X64_R12);
	}

	if (d->frcl)
	{
		jit_x64_mov_rr(e, X64_R14, X64_R12);

		if (d->shift == 3)
			jit_x64_and_ri(e, X64_R14, 0xFFF);
		else
		{
			jit_x64_shift_ri(e, X64_SAR, X64_R14, 11);
			jit_x64_and_ri(e, X64_R14, 0x1FFF);
		}
	}

	if (d->mrd || d->mwt)
	{
		/* La direccion del anillo, con MADRS[masa], la mascara de RBL y RBP
		   como inmediatos (viven en el rango que ensucia). Se derrama a
		   aicadsp_est.dir y cada consumidor la carga fresca: las llamadas a
		   empacar/desempacar pisan los volatiles. */
		if (derrama)
			jit_x64_mov_mr(e, X64_RSI, DJ_OFF(entrada), X64_RAX);

		jit_x64_mov_ri(e, X64_RCX, (unsigned) d->madrs);

		if (!d->table)
			jit_x64_add_rr(e, X64_RCX, X64_R13);
		if (d->adreb)
		{
			jit_x64_mov_rm(e, X64_RDX, X64_RSI, DJ_OFF(adrs));
			jit_x64_and_ri(e, X64_RDX, 0xFFF);
			jit_x64_add_rr(e, X64_RCX, X64_RDX);
		}
		if (d->nxadr)
			jit_x64_alu_ri(e, X64_ADD, X64_RCX, 1);

		jit_x64_and_ri(e, X64_RCX, d->table
			? 0xFFFF : (int) aicadsp_est.mascara);
		jit_x64_alu_ri(e, X64_ADD, X64_RCX, (int) aicadsp_est.rbp);
		jit_x64_add_rr(e, X64_RCX, X64_RCX);
		jit_x64_and_ri(e, X64_RCX, AICA_ONDA_SIZE - 1);
		jit_x64_mov_mr(e, X64_RSI, DJ_OFF(dir), X64_RCX);

		if (d->mrd)
		{
			jit_x64_movsx_w_rm_idx(e, X64_RDX, X64_RDI, X64_RCX, 1, 0);

			if (d->nofl)
			{
				/* signo24(v << 8) de una palabra sin signo es exactamente
				   ((short) v) << 8: la carga ya extendio el bit 15. */
				jit_x64_shl_ri(e, X64_RDX, 8);
				jit_x64_mov_mr(e, X64_RSI,
					DJ_OFF(memval) + ((paso + 2) & 3) * 4, X64_RDX);
			}
			else
			{
				jit_x64_movzx_w(e, X64_RCX, X64_RDX);
				dj_llamar(e, (const void *) aicadsp_desempacar);
				jit_x64_mov_mr(e, X64_RSI,
					DJ_OFF(memval) + ((paso + 2) & 3) * 4, X64_RAX);
			}
		}

		if (d->mwt)
		{
			if (d->nofl)
			{
				jit_x64_mov_rr(e, X64_RDX, X64_R12);
				jit_x64_shift_ri(e, X64_SAR, X64_RDX, 8);
				jit_x64_and_ri(e, X64_RDX, 0xFFFF);
			}
			else
			{
				jit_x64_mov_rr(e, X64_RCX, X64_R12);
				dj_llamar(e, (const void *) aicadsp_empacar);
				jit_x64_movzx_w(e, X64_RDX, X64_RAX);
			}

			jit_x64_mov_rm(e, X64_RCX, X64_RSI, DJ_OFF(dir));
			jit_x64_mov16_mr_idx(e, X64_RDI, X64_RCX, 1, 0, X64_RDX);

			/* onda_marcar_escritura(dir, 2) en linea: dir es par, asi que
			   dos bytes nunca cruzan la pagina de 1 KB y el camino largo del
			   macro no puede darse. */
			jit_x64_shr_ri(e, X64_RCX, ONDA_PAG_BITS);
			jit_x64_mov64_ri(e, X64_R10,
				(unsigned long long) (size_t) onda_gen);
			jit_x64_lea64_idx(e, X64_R10, X64_R10, X64_RCX, 4, 0);
			jit_x64_add_mi(e, X64_R10, 0, 1);
		}

		if (derrama)
			jit_x64_mov_rm(e, X64_RAX, X64_RSI, DJ_OFF(entrada));
	}

	if (d->adrl)
	{
		if (d->shift == 3)
		{
			jit_x64_mov_rr(e, X64_RCX, X64_R12);
			jit_x64_shift_ri(e, X64_SAR, X64_RCX, 12);
		}
		else
		{
			jit_x64_mov_rr(e, X64_RCX, X64_RAX);
			jit_x64_shift_ri(e, X64_SAR, X64_RCX, 16);
		}

		jit_x64_and_ri(e, X64_RCX, 0xFFF);
		jit_x64_mov_mr(e, X64_RSI, DJ_OFF(adrs), X64_RCX);
	}

	if (d->ewt)
	{
		jit_x64_mov_rr(e, X64_RCX, X64_R12);
		jit_x64_shift_ri(e, X64_SAR, X64_RCX, 8);
		jit_x64_add_mr(e, X64_RSI, DJ_OFF(efreg) + d->ewa * 4, X64_RCX);
	}
}

/* ------------------------------------------------------------------------ */
/* El programa                                                              */
/* ------------------------------------------------------------------------ */

static aicadsp_fn dj_emitir(const aicadsp_paso_dec * tabla, int ultimo)
{
	x64_emisor		e;
	unsigned char *	inicio;
	int				paso;
	int				hay_onda = 0;

	if (dj_arena == NULL)
	{
		dj_declinados++;
		return NULL;
	}

	for (paso = 0; paso <= ultimo; paso++)
		if (tabla[paso].mrd || tabla[paso].mwt)
			hay_onda = 1;

	jit_x64_iniciar(&e, dj_arena, DJ_ARENA_TAM);
	inicio = jit_x64_aqui(&e);

	/* 7 pushes dejan la pila alineada a 16; los 32 de abajo son la sombra
	   de las llamadas a empacar/desempacar. */
	jit_x64_push(&e, X64_RBX);
	jit_x64_push(&e, X64_RSI);
	jit_x64_push(&e, X64_RDI);
	jit_x64_push(&e, X64_R12);
	jit_x64_push(&e, X64_R13);
	jit_x64_push(&e, X64_R14);
	jit_x64_push(&e, X64_R15);
	jit_x64_sub64_ri(&e, X64_RSP, 32);

	jit_x64_mov64_ri(&e, X64_RSI,
		(unsigned long long) (size_t) &aicadsp_est);

	/* sound_mem es un puntero, no un arreglo: se carga su valor vigente en
	   vez de hornearlo, por si alguien lo reasigna. */
	if (hay_onda)
	{
		jit_x64_mov64_ri(&e, X64_RDI,
			(unsigned long long) (size_t) &sound_mem);
		jit_x64_mov64_rm(&e, X64_RDI, X64_RDI, 0);
	}

	jit_x64_xor_rr(&e, X64_RBX, X64_RBX);		/* acc = 0 */
	jit_x64_xor_rr(&e, X64_R12, X64_R12);		/* shifted = 0 */
	jit_x64_mov_rm(&e, X64_R13, X64_RSI, DJ_OFF(dec));
	jit_x64_mov_rm(&e, X64_R14, X64_RSI, DJ_OFF(frc));
	jit_x64_mov_rm(&e, X64_R15, X64_RSI, DJ_OFF(y));

	for (paso = 0; paso <= ultimo; paso++)
		dj_paso(&e, &tabla[paso], paso);

	jit_x64_mov_mr(&e, X64_RSI, DJ_OFF(frc), X64_R14);
	jit_x64_mov_mr(&e, X64_RSI, DJ_OFF(y), X64_R15);

	jit_x64_add64_ri(&e, X64_RSP, 32);
	jit_x64_pop(&e, X64_R15);
	jit_x64_pop(&e, X64_R14);
	jit_x64_pop(&e, X64_R13);
	jit_x64_pop(&e, X64_R12);
	jit_x64_pop(&e, X64_RDI);
	jit_x64_pop(&e, X64_RSI);
	jit_x64_pop(&e, X64_RBX);
	jit_x64_ret(&e);

	if (e.desborde)
	{
		dj_declinados++;
		return NULL;
	}

	dj_emitidos++;
	return (aicadsp_fn) (void *) inicio;
}

/* ------------------------------------------------------------------------ */

void aicadspjit_iniciar(void)
{
	const char * v = getenv("DCEMU_SIN_JIT_DSP");

	if (v != NULL && atoi(v) != 0)
		return;

	if (dj_arena == NULL)
	{
		dj_arena = (unsigned char *) VirtualAlloc(NULL, DJ_ARENA_TAM,
			MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

		if (dj_arena == NULL)
			return;
	}

	aicadsp_instalar_emisor(dj_emitir);
}
