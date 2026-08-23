/****************************************************************************

	AICADSP - ver aicadsp.h.

	La referencia es el DevBox §8.1.1.8, contrastada con las implementaciones
	que descienden de las notas de Neill Corlett (MAME, nullDC, reicast): el
	formato del paso, el orden de las operaciones y el flotante de 16 bits son
	los mismos en todas, y ese acuerdo es lo mas parecido a una segunda fuente
	que tiene este chip.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "main.h"			/* solo por los tipos; no se enlaza nada de SDL */
#include "aica.h"
#include "aicadsp.h"
#include "traza.h"

/* ------------------------------------------------------------------------ */
/* Donde vive cada cosa en aica_reg[]                                       */
/* ------------------------------------------------------------------------ */

#define DSP_RBP_RBL		0x2804		/* RBP en 11:0 (x2 KB), RBL en 14:13 */
#define DSP_COEF		0x3000		/* 128 x 4 bytes, el valor en 15:3 */
#define DSP_MADRS		0x3200		/* 64 x 4 bytes */
#define DSP_MPRO		0x3400		/* 128 pasos x 4 palabras x 4 bytes */
#define DSP_MPRO_FIN	0x3C00

/* Una palabra de 16 bits del archivo de registros, sin pasar por el
   despachador: esto corre 128 veces por muestra y no necesita los casos
   especiales de leer_registro(). */
static DWORD palabra(unsigned long off)
{
	return (DWORD) (aica_reg[off] | (aica_reg[off + 1] << 8));
}

/* ------------------------------------------------------------------------ */
/* El estado de trabajo                                                     */
/* ------------------------------------------------------------------------ */

/*
	Todo el estado vive en un solo bloque (aicadsp.h) para que el programa
	emitido lo direccione desde un registro base; los nombres de siempre
	siguen valiendo por estos alias, asi el cuerpo C no cambio de forma.
	dsp_memval es el retardo de MRD: el dato de un MRD en el paso n lo
	entrega el IWT del paso n+2, que es como el ensamblador de Sega genera
	los programas -- cuatro posiciones alcanzan porque el retardo es fijo.
*/
struct aicadsp_est	aicadsp_est;

#define dsp_temp	(aicadsp_est.temp)
#define dsp_mems	(aicadsp_est.mems)
#define dsp_mixs	(aicadsp_est.mixs)
#define dsp_exts	(aicadsp_est.exts)
#define dsp_efreg	(aicadsp_est.efreg)
#define dsp_memval	(aicadsp_est.memval)
#define dsp_frc		(aicadsp_est.frc)
#define dsp_y		(aicadsp_est.y)
#define dsp_adrs	(aicadsp_est.adrs)
#define dsp_dec		(aicadsp_est.dec)

/* El emisor instalado (aicadspjit.c) y el programa emitido vigente. */
static aicadsp_fn	(* dsp_emisor)(const aicadsp_paso_dec *, int) = NULL;
static aicadsp_fn	dsp_fn = NULL;

/* El microprograma, reescaneado solo cuando alguien lo toco. */
static int		dsp_sucio = 1;
static int		dsp_pasos = 0;		/* pasos con alguna palabra != 0 */

/* El corte del programa: el lazo corre hasta el ultimo paso con algun efecto
   observable, no hasta 128 -- ver el calculo en aicadsp_activo(). Crazy Taxi
   programa 78 pasos: los 50 de cola eran 39 % del costo del DSP corriendo
   para nadie. DCEMU_SIN_DSP_CORTE=1 vuelve a los 128 (el brazo del A/B). */
static int		dsp_ultimo = 127;
static int		dsp_corte  = -1;	/* -1: el ambiente no se leyo todavia */
static int		dsp_rapidos = 0;	/* censo: pasos "MAC simple" hasta el corte */

/*
	La predecodificacion del microprograma: los campos de las 4 palabras de
	cada paso, extraidos UNA vez y validos mientras dsp_sucio no se levante --
	la misma regla y el mismo gancho que el rescaneo de dsp_pasos: todo
	escritor de 0x2800-0x3BFF pasa por aicadsp_tocar() (el registro comun y el
	DMA interno de aica.c) y el reset arranca sucio. Antes cada muestra releia
	y redecodificaba las 4 palabras de los 128 pasos: en el banco de Crazy
	Taxi, 1016 millones de extracciones por corrida. `coef` guarda el
	coeficiente del paso ya corrido (el caso ysel==1); el recorte final
	(y << 19) >> 19 del cuerpo queda donde estaba, para que la aritmetica sea
	identica bit a bit.
*/
static aicadsp_paso_dec	dsp_tabla[128];

/* El censo para el resumen. */
static unsigned long long	censo_muestras_con_programa = 0;
static unsigned long long	censo_envios_mixs = 0;
static unsigned long long	censo_reconstrucciones = 0;
static int					censo_aviso_estado = 0;

void aicadsp_reiniciar(void)
{
	memset(dsp_temp, 0, sizeof(dsp_temp));
	memset(dsp_mems, 0, sizeof(dsp_mems));
	memset(dsp_mixs, 0, sizeof(dsp_mixs));
	memset(dsp_exts, 0, sizeof(dsp_exts));
	memset(dsp_efreg, 0, sizeof(dsp_efreg));
	memset(dsp_memval, 0, sizeof(dsp_memval));

	dsp_frc = dsp_y = 0;
	dsp_adrs = 0;
	dsp_dec = 0;
	dsp_fn = NULL;			/* la reconstruccion que dispara dsp_sucio lo rehace */
	dsp_sucio = 1;
}

void aicadsp_tocar(void)
{
	dsp_sucio = 1;
}

/* El emisor, como arm7_blq_instalar_emisor(): este archivo no sabe de x64.
   Instalar (o desinstalar, con NULL) ensucia, asi el proximo paso rehace el
   programa emitido junto con la tabla. */
void aicadsp_instalar_emisor(aicadsp_fn (* emisor)(const aicadsp_paso_dec * tabla,
	int ultimo))
{
	dsp_emisor = emisor;
	dsp_fn = NULL;
	dsp_sucio = 1;
}

void aicadsp_mixs(int sel, int muestra)
{
	/* MIXS es de 20 bits: la muestra de 16 entra corrida 4, y el paso la
	   corre otros 4 hasta los 24 de la ALU. Asi una voz a escala plena llena
	   la escala del DSP, que es la convencion del chip. */
	dsp_mixs[sel & 15] += (long) muestra << 4;
	censo_envios_mixs++;
}

void aicadsp_exts(int i, int muestra)
{
	dsp_exts[i & 1] = muestra;
}

int aicadsp_efreg(int i)
{
	return (int) dsp_efreg[i & 15];
}

int aicadsp_activo(void)
{
	if (dsp_sucio)
	{
		unsigned long off;
		int paso;

		censo_reconstrucciones++;
		dsp_pasos = 0;

		for (off = DSP_MPRO; off < DSP_MPRO_FIN; off += 4)
			if (palabra(off))
			{
				dsp_pasos++;
				off |= 0xC;			/* con una alcanza: al proximo paso */
			}

		/* La tabla de predecodificacion, con los mismos campos y los mismos
		   corrimientos que tenia el cuerpo del paso. */
		for (paso = 0; paso < 128; paso++)
		{
			unsigned long	base = DSP_MPRO + (unsigned long) paso * 16;
			DWORD			w0 = palabra(base);
			DWORD			w1 = palabra(base + 4);
			DWORD			w2 = palabra(base + 8);
			DWORD			w3 = palabra(base + 12);
			aicadsp_paso_dec *	d  = &dsp_tabla[paso];

			d->tra   = (unsigned char) ((w0 >> 9) & 0x7F);
			d->twt   = (unsigned char) ((w0 >> 8) & 1);
			d->twa   = (unsigned char) ((w0 >> 1) & 0x7F);

			d->xsel  = (unsigned char) ((w1 >> 15) & 1);
			d->ysel  = (unsigned char) ((w1 >> 13) & 3);
			d->ira   = (unsigned char) ((w1 >> 7) & 0x3F);
			d->iwt   = (unsigned char) ((w1 >> 6) & 1);
			d->iwa   = (unsigned char) ((w1 >> 1) & 0x1F);

			d->table = (unsigned char) ((w2 >> 15) & 1);
			d->mwt   = (unsigned char) ((w2 >> 14) & 1);
			d->mrd   = (unsigned char) ((w2 >> 13) & 1);
			d->ewt   = (unsigned char) ((w2 >> 12) & 1);
			d->ewa   = (unsigned char) ((w2 >> 8) & 0xF);
			d->adrl  = (unsigned char) ((w2 >> 7) & 1);
			d->frcl  = (unsigned char) ((w2 >> 6) & 1);
			d->shift = (unsigned char) ((w2 >> 4) & 3);
			d->yrl   = (unsigned char) ((w2 >> 3) & 1);
			d->negb  = (unsigned char) ((w2 >> 2) & 1);
			d->zero  = (unsigned char) ((w2 >> 1) & 1);
			d->bsel  = (unsigned char) (w2 & 1);

			d->nofl  = (unsigned char) ((w3 >> 15) & 1);
			d->masa  = (unsigned char) ((w3 >> 9) & 0x3F);
			d->adreb = (unsigned char) ((w3 >> 8) & 1);
			d->nxadr = (unsigned char) ((w3 >> 7) & 1);

			d->coef  = (long) ((short) palabra(DSP_COEF
						+ (unsigned long) paso * 4)) >> 3;

			/* MADRS[masa] resuelta: vive en el rango que ensucia, asi que
			   se hornea aca por la misma regla que el resto de la tabla. */
			d->madrs = (long) palabra(DSP_MADRS + (unsigned long) d->masa * 4);
		}

		/*
			El corte: el ultimo paso con algun efecto que sobreviva al lazo.
			twt/iwt escriben TEMP y MEMS; mwt escribe memoria; mrd cuenta
			porque dsp_memval es un anillo de 4 que CRUZA muestras (el iwt del
			paso 1 de la muestra siguiente consume lo que el mrd del paso 127
			trajo); ewt alimenta EFREG; frcl/yrl/adrl escriben registros
			persistentes. Lo que sigue al ultimo de esos solo mueve acc y
			shifted, que mueren con la muestra: correrlo es trabajo que nadie
			observa. La suite del dsp y el .wav son las barandas.
		*/
		if (dsp_corte < 0)
		{
			const char * e = getenv("DCEMU_SIN_DSP_CORTE");

			dsp_corte = !(e != NULL && atoi(e) != 0);
		}

		dsp_ultimo = 127;

		if (dsp_corte)
		{
			for (dsp_ultimo = 127; dsp_ultimo >= 0; dsp_ultimo--)
			{
				const aicadsp_paso_dec * d = &dsp_tabla[dsp_ultimo];

				if (d->twt || d->iwt || d->mwt || d->mrd || d->ewt
					|| d->frcl || d->yrl || d->adrl)
					break;
			}
		}

		/* El censo del "MAC simple", para el resumen: fue la sonda del cuerpo
		   rapido por clase de paso, que se midio NEUTRO y se revirtio (ver el
		   comentario en el lazo de paso). Queda porque describe la forma del
		   programa y ya contesto una pregunta que alguien puede rehacer. */
		dsp_rapidos = 0;

		for (paso = 0; paso <= dsp_ultimo; paso++)
		{
			const aicadsp_paso_dec * d = &dsp_tabla[paso];

			if (!d->iwt && !d->mrd && !d->mwt && !d->ewt
				&& !d->adrl && !d->frcl && !d->yrl)
				dsp_rapidos++;
		}

		/* RBP/RBL, horneados: viven en 0x2804, dentro del rango que ensucia,
		   asi que releerlos por muestra era releer un registro que solo cambia
		   cuando dsp_sucio ya se levanto. */
		{
			DWORD r = palabra(DSP_RBP_RBL);

			aicadsp_est.rbp     = (unsigned long) (r & 0xFFF) << 10;
			aicadsp_est.mascara = (8192ul << ((r >> 13) & 3)) - 1;
		}

		/* El programa emitido, si hay emisor: se rehace en cada
		   reconstruccion, sobre la tabla que acaba de salir. NULL (declino o
		   sin emisor) deja el cuerpo C de siempre. */
		dsp_fn = NULL;

		if (dsp_emisor != NULL && dsp_pasos != 0)
			dsp_fn = dsp_emisor(dsp_tabla, dsp_ultimo);

		dsp_sucio = 0;
	}

	return dsp_pasos != 0;
}

/* ------------------------------------------------------------------------ */
/* El flotante de 16 bits del anillo                                        */
/* ------------------------------------------------------------------------ */

/*
	Sin NOFL, el DSP guarda en memoria un formato propio: bit 15 el signo,
	14:11 un exponente y 10:0 la mantisa, con el bit implicito INVERTIDO
	respecto de IEEE -- la mantisa guarda los bits que siguen al primer bit
	DISTINTO del signo. Desempacar un valor empacado es exacto para lo que el
	formato representa; lo que se pierde al empacar es precision de la cola,
	como en cualquier flotante.
*/
unsigned short aicadsp_empacar(long v)
{
	int				signo = (int) ((v >> 23) & 1);
	unsigned long	temp = (unsigned long) (v ^ (v << 1)) & 0xFFFFFF;
	int				exp = 0;
	int				k;

	for (k = 0; k < 12; k++)
	{
		if (temp & 0x800000)
			break;

		temp <<= 1;
		exp++;
	}

	if (exp < 12)
		v = (v << exp) & 0x3FFFFF;
	else
		v <<= 11;

	v >>= 11;
	v &= 0x7FF;
	v |= (long) signo << 15;
	v |= (long) exp << 11;

	return (unsigned short) v;
}

long aicadsp_desempacar(unsigned short v)
{
	int		signo = (v >> 15) & 1;
	int		exp = (v >> 11) & 0xF;
	long	m = v & 0x7FF;
	long	r;

	r = m << 11;

	if (exp > 11)
	{
		exp = 11;
		r |= (long) signo << 22;
	}
	else
		r |= (long) (signo ^ 1) << 22;

	r |= (long) signo << 23;

	/* A 24 bits con signo, y el exponente corre a la derecha. */
	r = (r << 8) >> 8;
	r >>= exp;

	return r;
}

/* ------------------------------------------------------------------------ */
/* El paso                                                                  */
/* ------------------------------------------------------------------------ */

/* Recorte a 24 bits con signo. */
static long recortar24(long v)
{
	if (v >  0x007FFFFF)	return  0x007FFFFF;
	if (v < -0x00800000)	return -0x00800000;
	return v;
}

/* Extension de signo desde 24 bits. */
static long signo24(long v)
{
	return (v & 0x00800000) ? (long) (v | ~0xFFFFFFL) : (v & 0xFFFFFF);
}

void aicadsp_paso(void)
{
	long			acc = 0, shifted = 0, x, y, b, entrada = 0;
	int				paso;

	memset(dsp_efreg, 0, sizeof(dsp_efreg));

	if (!aicadsp_activo())
	{
		/* MIXS es de esta muestra aunque nadie lo consuma. */
		memset(dsp_mixs, 0, sizeof(dsp_mixs));
		return;
	}

	censo_muestras_con_programa++;

	/* El programa emitido es el lazo entero de esta muestra; el cuerpo C de
	   abajo es el mismo paso a paso, y el brazo del A/B (DCEMU_SIN_JIT_DSP
	   deja el emisor sin instalar). */
	if (dsp_fn != NULL)
	{
		dsp_fn();

		dsp_dec--;
		memset(dsp_mixs, 0, sizeof(dsp_mixs));
		return;
	}

	for (paso = 0; paso <= dsp_ultimo; paso++)
	{
		/* Los campos vienen de la tabla de predecodificacion (ver arriba):
		   mismos nombres, mismos valores, cero relecturas por muestra. */
		const aicadsp_paso_dec * d = &dsp_tabla[paso];

		/* Aca vivio unas horas el cuerpo rapido por clase de paso (2026-08-21)
		   y se revirtio MEDIDO: 63 de los 86 pasos de Crazy Taxi califican
		   como "MAC simple" y un cuerpo de la mitad del tamano salio NEUTRO
		   al milisegundo en la tanda -- el patron de ramas por paso es fijo
		   entre muestras, el predictor se aprende la secuencia entera, y el
		   costo real es la cadena MAC con sus cargas, que el cuerpo corto
		   conserva. El censo queda en el resumen de traza; la leccion, en
		   docs/jit-sota-plan.md. */
		int tra   = d->tra;
		int twt   = d->twt;
		int twa   = d->twa;

		int xsel  = d->xsel;
		int ysel  = d->ysel;
		int ira   = d->ira;
		int iwt   = d->iwt;
		int iwa   = d->iwa;

		int table = d->table;
		int mwt   = d->mwt;
		int mrd   = d->mrd;
		int ewt   = d->ewt;
		int ewa   = d->ewa;
		int adrl  = d->adrl;
		int frcl  = d->frcl;
		int shift = d->shift;
		int yrl   = d->yrl;
		int negb  = d->negb;
		int zero  = d->zero;
		int bsel  = d->bsel;

		int nofl  = d->nofl;
		int adreb = d->adreb;
		int nxadr = d->nxadr;

		/* La entrada del paso. MIXS es de 20 bits y EXTS de 16; los dos suben
		   a los 24 de la ALU. */
		if (ira <= 0x1F)
			entrada = dsp_mems[ira];
		else if (ira <= 0x2F)
			entrada = dsp_mixs[ira - 0x20] << 4;
		else if (ira <= 0x31)
			entrada = dsp_exts[ira - 0x30] << 8;
		else
			entrada = 0;

		entrada = signo24(entrada);

		/* El IWT entrega lo que trajo el MRD de hace dos pasos. */
		if (iwt)
		{
			dsp_mems[iwa] = dsp_memval[paso & 3];

			if (ira == iwa)
				entrada = dsp_mems[iwa];
		}

		/* Los tres operandos. */
		if (!zero)
		{
			b = bsel ? acc : signo24(dsp_temp[(tra + (int) dsp_dec) & 0x7F]);

			if (negb)
				b = -b;
		}
		else
			b = 0;

		x = xsel ? entrada : signo24(dsp_temp[(tra + (int) dsp_dec) & 0x7F]);

		switch (ysel)
		{
			case 0:  y = dsp_frc; break;
			/* El coeficiente del PASO: el AICA lleva uno por paso, no un
			   campo de seleccion como el SCSP. 13 bits con signo en 15:3,
			   ya corrido en la tabla. */
			case 1:  y = d->coef; break;
			case 2:  y = (dsp_y >> 11) & 0x1FFF; break;
			default: y = (dsp_y >> 4) & 0x0FFF; break;
		}

		if (yrl)
			dsp_y = entrada;

		/* El desplazador mira el acumulador del paso ANTERIOR. */
		switch (shift)
		{
			case 0:  shifted = recortar24(acc); break;
			case 1:  shifted = recortar24(acc * 2); break;
			case 2:  shifted = signo24(acc * 2); break;
			default: shifted = signo24(acc); break;
		}

		/* Multiplica-acumula: X de 24, Y de 13 con signo, producto >> 12. */
		y = (y << 19) >> 19;
		acc = (long) (((long long) x * (long long) y) >> 12) + b;

		if (twt)
			dsp_temp[(twa + (int) dsp_dec) & 0x7F] = shifted;

		if (frcl)
			dsp_frc = (shift == 3) ? (shifted & 0xFFF)
								   : ((shifted >> 11) & 0x1FFF);

		if (mrd || mwt)
		{
			unsigned long dir = (unsigned long) d->madrs;

			if (!table)
				dir += dsp_dec;
			if (adreb)
				dir += dsp_adrs & 0xFFF;
			if (nxadr)
				dir++;

			/* Dentro del anillo la direccion envuelve por RBL; con TABLE la
			   tabla es plana de 64 K palabras. RBP corre el origen. */
			dir &= table ? 0xFFFFul : aicadsp_est.mascara;
			dir = ((dir + aicadsp_est.rbp) * 2) & (AICA_ONDA_SIZE - 1);

			if (mrd)
			{
				DWORD v = (DWORD) (sound_mem[dir]
						| (sound_mem[dir + 1] << 8));

				dsp_memval[(paso + 2) & 3] = nofl
					? signo24((long) v << 8)
					: aicadsp_desempacar((unsigned short) v);
			}

			if (mwt)
			{
				DWORD v = nofl ? (DWORD) ((shifted >> 8) & 0xFFFF)
							   : aicadsp_empacar(shifted);

				sound_mem[dir]     = (unsigned char) (v & 0xFF);
				sound_mem[dir + 1] = (unsigned char) (v >> 8);

				/* El DSP es el cuarto escritor de la RAM de onda: sin la
				   marca, el ARM se saltea un barrido que debia rehacer.
				   Ver aica.h. */
				onda_marcar_escritura(dir, 2);
			}
		}

		if (adrl)
			dsp_adrs = (shift == 3) ? (unsigned long) ((shifted >> 12) & 0xFFF)
									: (unsigned long) (entrada >> 16) & 0xFFF;

		if (ewt)
			dsp_efreg[ewa] += shifted >> 8;
	}

	dsp_dec--;

	/* MIXS se consume por muestra: lo que los canales manden para la proxima
	   arranca de cero. */
	memset(dsp_mixs, 0, sizeof(dsp_mixs));
}

/* ------------------------------------------------------------------------ */
/* El censo                                                                 */
/* ------------------------------------------------------------------------ */

/* Aviso unico si un guest escribe distinto de cero en la zona de estado de
   trabajo (0x4000-0x45BF): el DSP no lo mira, y aceptarlo sin decirlo seria
   la falla clasica del arbol. Lo llama escribir_registro(). */
void aicadsp_estado_escrito(unsigned long off, unsigned int valor)
{
	if (valor == 0 || censo_aviso_estado)
		return;

	censo_aviso_estado = 1;

	if (traza_activa)
		fprintf(stderr, "traza: AICA: el guest escribio %04lx en el estado de"
			" trabajo del DSP (0x%04lx); el DSP emulado no lo relee\n",
			(unsigned long) valor, off);
}

void aicadsp_resumen(void)
{
	int i, envios = 0;

	if (!traza_activa)
		return;

	/* Los EFSDL distintos de cero, que son los que convierten salidas del DSP
	   --o el CD-DA, ranuras 16 y 17-- en sonido audible. */
	for (i = 0; i < 18; i++)
		if ((palabra(0x2000 + (unsigned long) i * 4) >> 8) & 0xF)
			envios++;

	/* "emitido"/"cuerpo C" dice que corre de verdad: un A/B con el emisor
	   caido (arena, desborde, la palanca) mediria C contra C en silencio. */
	fprintf(stderr, "traza: AICA DSP: %d pasos con programa, corte en el paso"
		" %d (%d MAC simple), %llu muestras corridas (%s), %llu"
		" reconstrucciones, %llu envios de canal a MIXS, %d ranuras"
		" EFSDL != 0\n",
		aicadsp_activo() ? dsp_pasos : 0, dsp_ultimo, dsp_rapidos,
		censo_muestras_con_programa,
		dsp_fn != NULL ? "programa emitido" : "cuerpo C",
		censo_reconstrucciones, censo_envios_mixs, envios);
}
