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

static long		dsp_temp[128];		/* 24 bits: la memoria de retardo */
static long		dsp_mems[32];		/* 24 bits: lo que trajo la memoria */
static long		dsp_mixs[16];		/* 20 bits: lo que acumulan los canales */
static long		dsp_exts[2];		/* 16 bits: el CD-DA */
static long		dsp_efreg[16];		/* las salidas, 16 bits */

/* Los registros internos que sobreviven de un paso al otro. */
static long		dsp_frc;			/* 13 bits */
static long		dsp_y;				/* 24 bits */
static unsigned long dsp_adrs;		/* 12 bits */
static unsigned long dsp_dec;		/* el decremento del anillo, 1 por muestra */

/*
	Lo que trajo una lectura de memoria, con su retardo: el dato de un MRD en
	el paso n lo entrega el IWT del paso n+2, que es como el ensamblador de
	Sega genera los programas. Cuatro posiciones alcanzan porque el retardo es
	fijo.
*/
static long		dsp_memval[4];

/* El microprograma, reescaneado solo cuando alguien lo toco. */
static int		dsp_sucio = 1;
static int		dsp_pasos = 0;		/* pasos con alguna palabra != 0 */

/* El censo para el resumen. */
static unsigned long long	censo_muestras_con_programa = 0;
static unsigned long long	censo_envios_mixs = 0;
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
	dsp_sucio = 1;
}

void aicadsp_tocar(void)
{
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

		dsp_pasos = 0;

		for (off = DSP_MPRO; off < DSP_MPRO_FIN; off += 4)
			if (palabra(off))
			{
				dsp_pasos++;
				off |= 0xC;			/* con una alcanza: al proximo paso */
			}

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
	unsigned long	rbp, rbl_palabras;
	int				paso;

	memset(dsp_efreg, 0, sizeof(dsp_efreg));

	if (!aicadsp_activo())
	{
		/* MIXS es de esta muestra aunque nadie lo consuma. */
		memset(dsp_mixs, 0, sizeof(dsp_mixs));
		return;
	}

	censo_muestras_con_programa++;

	{
		DWORD r = palabra(DSP_RBP_RBL);

		rbp = (unsigned long) (r & 0xFFF) << 10;		/* en palabras: x2 KB */
		rbl_palabras = 8192ul << ((r >> 13) & 3);
	}

	for (paso = 0; paso < 128; paso++)
	{
		unsigned long	base = DSP_MPRO + (unsigned long) paso * 16;
		DWORD			w0 = palabra(base);
		DWORD			w1 = palabra(base + 4);
		DWORD			w2 = palabra(base + 8);
		DWORD			w3 = palabra(base + 12);

		int tra   = (int) ((w0 >> 9) & 0x7F);
		int twt   = (int) ((w0 >> 8) & 1);
		int twa   = (int) ((w0 >> 1) & 0x7F);

		int xsel  = (int) ((w1 >> 15) & 1);
		int ysel  = (int) ((w1 >> 13) & 3);
		int ira   = (int) ((w1 >> 7) & 0x3F);
		int iwt   = (int) ((w1 >> 6) & 1);
		int iwa   = (int) ((w1 >> 1) & 0x1F);

		int table = (int) ((w2 >> 15) & 1);
		int mwt   = (int) ((w2 >> 14) & 1);
		int mrd   = (int) ((w2 >> 13) & 1);
		int ewt   = (int) ((w2 >> 12) & 1);
		int ewa   = (int) ((w2 >> 8) & 0xF);
		int adrl  = (int) ((w2 >> 7) & 1);
		int frcl  = (int) ((w2 >> 6) & 1);
		int shift = (int) ((w2 >> 4) & 3);
		int yrl   = (int) ((w2 >> 3) & 1);
		int negb  = (int) ((w2 >> 2) & 1);
		int zero  = (int) ((w2 >> 1) & 1);
		int bsel  = (int) (w2 & 1);

		int nofl  = (int) ((w3 >> 15) & 1);
		int masa  = (int) ((w3 >> 9) & 0x3F);
		int adreb = (int) ((w3 >> 8) & 1);
		int nxadr = (int) ((w3 >> 7) & 1);

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
			   campo de seleccion como el SCSP. 13 bits con signo en 15:3. */
			case 1:  y = (long) ((short) palabra(DSP_COEF
						+ (unsigned long) paso * 4)) >> 3; break;
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
			unsigned long dir = palabra(DSP_MADRS
				+ (unsigned long) masa * 4);

			if (!table)
				dir += dsp_dec;
			if (adreb)
				dir += dsp_adrs & 0xFFF;
			if (nxadr)
				dir++;

			/* Dentro del anillo la direccion envuelve por RBL; con TABLE la
			   tabla es plana de 64 K palabras. RBP corre el origen. */
			dir &= table ? 0xFFFFul : (rbl_palabras - 1);
			dir = ((dir + rbp) * 2) & (AICA_ONDA_SIZE - 1);

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

	fprintf(stderr, "traza: AICA DSP: %d pasos con programa, %llu muestras"
		" corridas, %llu envios de canal a MIXS, %d ranuras EFSDL != 0\n",
		aicadsp_activo() ? dsp_pasos : 0,
		censo_muestras_con_programa, censo_envios_mixs, envios);
}
