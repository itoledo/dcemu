/****************************************************************************

	Pruebas de aicadsp.c: el DSP de efectos del AICA.

	El microprograma no se inventa: cada caso ensambla a mano los campos que
	enumera el DevBox §8.1.1.8 y los escribe por la ventana del SH-4, que es el
	camino por el que un driver de verdad sube el programa. Lo que se cuida:

	  - que un programa en cero no haga nada (es lo que corre el parque entero
	    de KOS, y por eso el DSP puede existir sin costarle nada);
	  - la aritmetica del paso: entrada, multiplica-acumula, desplazador y la
	    salida a EFREG, con las escalas del papel;
	  - la memoria de retardo TEMP con su decremento por muestra, que es lo que
	    convierte 128 palabras en una linea de retardo;
	  - el anillo en la RAM de onda, en crudo (NOFL) y en el flotante de 16
	    bits, ida y vuelta;
	  - que MIXS se consuma por muestra.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "aica.h"
#include "aicadsp.h"

#define G2(off)		(AICA_REG_BASE + (off))

static void escribir_g2(unsigned long off, DWORD v)
{
	aica_escribir(G2(off), &v, sizeof(v));
}

/* ------------------------------------------------------------------------ */
/* El ensamblador de un paso                                                */
/* ------------------------------------------------------------------------ */

/* Los campos de las cuatro palabras, con los nombres del papel. Cero es un
   paso que no hace nada, asi que solo se nombran los que cada caso usa. */
typedef struct
{
	int tra, twt, twa;
	int xsel, ysel, ira, iwt, iwa;
	int table, mwt, mrd, ewt, ewa, adrl, frcl, shift, yrl, negb, zero, bsel;
	int nofl, masa, adreb, nxadr;
} paso_dsp;

static void ensamblar(int paso, const paso_dsp * p)
{
	unsigned long base = 0x3400ul + (unsigned long) paso * 16;

	escribir_g2(base,      (DWORD) ((p->tra << 9) | (p->twt << 8)
	                              | (p->twa << 1)));
	escribir_g2(base + 4,  (DWORD) ((p->xsel << 15) | (p->ysel << 13)
	                              | (p->ira << 7) | (p->iwt << 6)
	                              | (p->iwa << 1)));
	escribir_g2(base + 8,  (DWORD) ((p->table << 15) | (p->mwt << 14)
	                              | (p->mrd << 13) | (p->ewt << 12)
	                              | (p->ewa << 8) | (p->adrl << 7)
	                              | (p->frcl << 6) | (p->shift << 4)
	                              | (p->yrl << 3) | (p->negb << 2)
	                              | (p->zero << 1) | p->bsel));
	escribir_g2(base + 12, (DWORD) ((p->nofl << 15) | (p->masa << 9)
	                              | (p->adreb << 8) | (p->nxadr << 7)));
}

/* El coeficiente del paso: 13 bits con signo en 15:3. +4095 es lo mas cerca
   de +1.0 que tiene el formato (1.12 fijo). */
static void coeficiente(int paso, int valor13)
{
	escribir_g2(0x3000ul + (unsigned long) paso * 4,
		(DWORD) ((valor13 & 0x1FFF) << 3));
}

static void reiniciar(void)
{
	aica_reset();
}

/* ------------------------------------------------------------------------ */

static void sin_programa_no_hace_nada(void)
{
	reiniciar();

	ESPERAR(aicadsp_activo() == 0);

	aicadsp_mixs(0, 0x1000);
	aicadsp_paso();

	ESPERAR_I32(aicadsp_efreg(0), 0);

	/* Y MIXS quedo limpio igual: la muestra siguiente arranca de cero. */
	aicadsp_paso();
	ESPERAR_I32(aicadsp_efreg(0), 0);
}

/*
	MIXS[0] x ~1.0 -> EFREG[0]. Con la muestra 0x1000: MIXS guarda 0x10000
	(20 bits), la entrada del paso son 0x100000 (24), el producto por 4095
	baja 0x100 (un 1/4096), y EFREG recibe SHIFTED >> 8.
*/
static void un_programa_copia_mixs_a_efreg(void)
{
	paso_dsp p;

	reiniciar();

	memset(&p, 0, sizeof(p));
	p.ira = 0x20;	p.xsel = 1;		p.ysel = 1;		p.zero = 1;
	ensamblar(0, &p);
	coeficiente(0, 4095);

	memset(&p, 0, sizeof(p));
	p.ewt = 1;		p.ewa = 0;		p.zero = 1;
	ensamblar(1, &p);

	ESPERAR(aicadsp_activo() == 1);

	aicadsp_mixs(0, 0x1000);
	aicadsp_paso();

	ESPERAR_I32(aicadsp_efreg(0), (0x100000 - 0x100) >> 8);
}

static void mixs_se_consume_por_muestra(void)
{
	paso_dsp p;

	reiniciar();

	memset(&p, 0, sizeof(p));
	p.ira = 0x20;	p.xsel = 1;		p.ysel = 1;		p.zero = 1;
	ensamblar(0, &p);
	coeficiente(0, 4095);

	memset(&p, 0, sizeof(p));
	p.ewt = 1;		p.zero = 1;
	ensamblar(1, &p);

	aicadsp_mixs(0, 0x1000);
	aicadsp_paso();
	ESPERAR(aicadsp_efreg(0) != 0);

	/* Sin envio nuevo, la muestra siguiente sale en cero. */
	aicadsp_paso();
	ESPERAR_I32(aicadsp_efreg(0), 0);
}

/*
	La linea de retardo: el paso 1 guarda en TEMP[0] y el paso 2 lee TEMP[1],
	que por el decremento es lo guardado UNA muestra antes. La primera muestra
	lee cero; la segunda, lo que entro en la primera.
*/
static void temp_retarda_una_muestra(void)
{
	paso_dsp p;

	reiniciar();

	memset(&p, 0, sizeof(p));
	p.ira = 0x20;	p.xsel = 1;		p.ysel = 1;		p.zero = 1;
	ensamblar(0, &p);
	coeficiente(0, 4095);

	memset(&p, 0, sizeof(p));
	p.twt = 1;		p.twa = 0;		p.zero = 1;
	ensamblar(1, &p);

	memset(&p, 0, sizeof(p));
	p.tra = 1;		p.xsel = 0;		p.ysel = 1;		p.zero = 1;
	ensamblar(2, &p);
	coeficiente(2, 4095);

	memset(&p, 0, sizeof(p));
	p.ewt = 1;		p.ewa = 1;		p.zero = 1;
	ensamblar(3, &p);

	aicadsp_mixs(0, 0x1000);
	aicadsp_paso();
	ESPERAR_I32(aicadsp_efreg(1), 0);			/* todavia no hay historia */

	aicadsp_mixs(0, 0x1000);
	aicadsp_paso();
	ESPERAR(aicadsp_efreg(1) > 0xF00);			/* ~0xFFE: dos pasadas por 4095/4096 */
	ESPERAR(aicadsp_efreg(1) <= 0x1000);
}

/*
	El anillo, en crudo: MWT con NOFL escribe los 16 bits altos de SHIFTED en
	la RAM de onda, en (MADRS[0] + RBP) * 2 con TABLE puesto. La muestra
	0x1000 entra como 0x100000 y baja a 0x0FFF.
*/
static void mwt_escribe_la_ram_de_onda(void)
{
	paso_dsp p;

	reiniciar();

	escribir_g2(0x3200, 0x0100);				/* MADRS[0] */
	escribir_g2(0x2804, 0x0001);				/* RBP = 1: +1024 palabras */

	memset(&p, 0, sizeof(p));
	p.ira = 0x20;	p.xsel = 1;		p.ysel = 1;		p.zero = 1;
	ensamblar(0, &p);
	coeficiente(0, 4095);

	memset(&p, 0, sizeof(p));
	p.mwt = 1;		p.nofl = 1;		p.table = 1;	p.masa = 0;	p.zero = 1;
	ensamblar(1, &p);

	sound_mem[(0x100 + 1024) * 2]     = 0xAA;
	sound_mem[(0x100 + 1024) * 2 + 1] = 0xAA;

	aicadsp_mixs(0, 0x1000);
	aicadsp_paso();

	ESPERAR_U32(sound_mem[(0x100 + 1024) * 2]
	          | (sound_mem[(0x100 + 1024) * 2 + 1] << 8), 0x0FFF);
}

/*
	Y la vuelta: MRD trae de la RAM de onda, el IWT de DOS pasos despues lo
	deja en MEMS, y de ahi sale por EFREG. El retardo del dato es la regla que
	el ensamblador de Sega da por sentada.
*/
static void mrd_lee_con_dos_pasos_de_retardo(void)
{
	paso_dsp p;

	reiniciar();

	escribir_g2(0x3200, 0x0200);				/* MADRS[0], RBP = 0 */

	sound_mem[0x400] = 0x34;					/* 0x1234 en (0x200)*2 */
	sound_mem[0x401] = 0x12;

	memset(&p, 0, sizeof(p));
	p.mrd = 1;		p.nofl = 1;		p.table = 1;	p.masa = 0;	p.zero = 1;
	ensamblar(0, &p);

	memset(&p, 0, sizeof(p));
	p.zero = 1;
	ensamblar(1, &p);							/* el hueco del retardo */

	memset(&p, 0, sizeof(p));
	p.iwt = 1;		p.iwa = 5;
	p.ira = 5;		p.xsel = 1;		p.ysel = 1;		p.zero = 1;
	ensamblar(2, &p);
	coeficiente(2, 4095);

	memset(&p, 0, sizeof(p));
	p.ewt = 1;		p.ewa = 2;		p.zero = 1;
	ensamblar(3, &p);

	aicadsp_paso();

	/* 0x1234 << 8, por 4095/4096, >> 8: 0x1232.. o sea 0x1233 con el redondeo
	   hacia abajo de la cadena entera. */
	ESPERAR(aicadsp_efreg(2) >= 0x1230);
	ESPERAR(aicadsp_efreg(2) <= 0x1234);
}

/* El flotante del anillo: ida y vuelta exacta para lo representable. */
static void el_flotante_de_16_va_y_vuelve(void)
{
	static const long representables[] = {
		0, 1, -1, 0x7FF, -0x800, 0x3FF800, -0x400000, 0x7FF800, -0x800000,
		0x001234 & ~0xF,	/* mantisa que cabe entera */
	};
	int i;

	for (i = 0; i < (int) (sizeof(representables) / sizeof(long)); i++)
	{
		long v = aicadsp_desempacar(aicadsp_empacar(representables[i]));

		/* Empacar pierde cola de mantisa; desempacar lo empacado de un valor
		   ya desempacado tiene que ser exacto. */
		ESPERAR_I32(aicadsp_desempacar(aicadsp_empacar(v)), v);
	}

	/* Y el barrido de los 65536 patrones: desempacar nunca se sale de los 24
	   bits con signo, y reempacar reproduce el mismo desempacado. */
	{
		unsigned long u;

		for (u = 0; u <= 0xFFFF; u++)
		{
			long v = aicadsp_desempacar((unsigned short) u);

			ESPERAR(v <= 0x7FFFFF && v >= -0x800000);

			if (aicadsp_desempacar(aicadsp_empacar(v)) != v)
			{
				ESPERAR_I32(aicadsp_desempacar(aicadsp_empacar(v)), v);
				break;					/* con un reporte alcanza */
			}
		}
	}
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] = {
	CASO(sin_programa_no_hace_nada),
	CASO(un_programa_copia_mixs_a_efreg),
	CASO(mixs_se_consume_por_muestra),
	CASO(temp_retarda_una_muestra),
	CASO(mwt_escribe_la_ram_de_onda),
	CASO(mrd_lee_con_dos_pasos_de_retardo),
	CASO(el_flotante_de_16_va_y_vuelve),
};

const dc_suite suite_dsp = DEFINIR_SUITE("dsp", casos);
