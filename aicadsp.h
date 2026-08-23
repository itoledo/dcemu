/****************************************************************************

	AICADSP - el DSP de efectos del AICA (fase 6 de docs/aica-plan.md)

	El chip lleva un DSP de 128 pasos que corre una vez por muestra, entre el
	sintetizador y el DAC. El DevBox lo trae completo en §8.1.1.8: un
	microprograma de 4 palabras de 16 bits por paso (`MPRO`, 0x3400-0x3BFF),
	128 coeficientes de 13 bits (`COEF`, 0x3000), 64 direcciones (`MADRS`,
	0x3200), un anillo en la RAM de onda (`RBP`/`RBL`, en 0x2804), 128 palabras
	de trabajo de 24 bits (`TEMP`), 32 entradas desde memoria (`MEMS`), 16
	acumuladores de mezcla que llenan los canales (`MIXS`), 2 entradas externas
	donde entra el CD-DA (`EXTS`) y 16 salidas (`EFREG`) que el mezclador final
	compone con sus propios niveles (EFSDL/EFPAN, 0x2000-0x2044).

	**El microprograma, los coeficientes y las direcciones se leen de
	`aica_reg[]` directamente**, sin copia propia: los registros SON el
	almacenamiento, igual que el resto del chip, asi que la subida por DMA
	interno y la relectura del guest funcionan solas. El estado de trabajo
	--TEMP, MEMS, MIXS, EFREG y los registros internos-- es del modulo: ningun
	driver del arbol lo relee, y si un guest escribe algo distinto de cero en
	esa zona se avisa una vez en vez de aceptarlo en silencio.

	El costo cuando nadie lo programa es cero de verdad: un microprograma en
	cero deja el paso en un retorno temprano, y eso es lo que corre el parque
	entero de KOS -- su driver no lo usa. Lo que se gana emulándolo es la
	reverberacion de los juegos comerciales que si lo programan, y que el nivel
	del CD-DA deje de ser fijo: en el chip el CD entra por EXTS y sale por los
	niveles EFSDL de las ranuras 16 y 17.

	Sin SDL a proposito, como aica.c y arm7.c: las pruebas lo enlazan de verdad.

*****************************************************************************/

#ifndef _AICADSP_H_
#define _AICADSP_H_

/* Todo a cero: al arrancar y al resetear el chip. */
void aicadsp_reiniciar(void);

/*
	Aviso de que un registro en [0x2800, 0x3C00) cambio -- RBP/RBL o el
	microprograma. Solo marca; el reescaneo (contar los pasos con programa)
	ocurre una vez, en el proximo paso. Lo llaman escribir_registro() y el DMA
	interno, que son los dos caminos por los que entra un microprograma.
*/
void aicadsp_tocar(void);

/* El envio de un canal: acumula la muestra (16 bits con signo, ya atenuada
   por IMXL) en MIXS[sel]. Lo llama canal_muestrear() cuando IMXL != 0. */
void aicadsp_mixs(int sel, int muestra);

/* Las dos entradas externas (el CD-DA), 16 bits con signo. Se fijan, no se
   acumulan: son una linea, no una suma. */
void aicadsp_exts(int i, int muestra);

/* Corre los 128 pasos de una muestra. Con el microprograma en cero no hace
   nada y MIXS igual queda limpio para la muestra siguiente. */
void aicadsp_paso(void);

/* Una salida de efecto, 16 bits con signo. */
int aicadsp_efreg(int i);

/* 1 si hay microprograma cargado (algun paso distinto de cero). */
int aicadsp_activo(void);

/*
	El formato de 16 bits con el que el DSP guarda en el anillo cuando NOFL
	esta apagado: signo, exponente de 4 bits y mantisa de 11, desempacado a 24.
	Expuestos porque la suite los prueba de ida y vuelta; nadie mas los llama.
*/
unsigned short aicadsp_empacar(long v);
long aicadsp_desempacar(unsigned short v);

/* Aviso unico si el guest escribe distinto de cero en el estado de trabajo
   (0x4000-0x45BF), que el DSP emulado no relee. */
void aicadsp_estado_escrito(unsigned long off, unsigned int valor);

/* ------------------------------------------------------------------------ */
/* Lo que comparte con el emisor (aicadspjit.c)                             */
/* ------------------------------------------------------------------------ */

/*
	Los campos de las 4 palabras de un paso, extraidos una vez al reconstruir
	(la regla de dsp_sucio). `coef` ya viene corrido y con signo; `madrs` es
	la palabra de MADRS[masa] resuelta -- vive en el rango que ensucia, asi
	que hornearla al reconstruir es exacto por la misma regla que RBP/RBL.
*/
typedef struct
{
	unsigned char	tra, twt, twa;
	unsigned char	xsel, ysel, ira, iwt, iwa;
	unsigned char	table, mwt, mrd, ewt, ewa;
	unsigned char	adrl, frcl, shift, yrl, negb, zero, bsel;
	unsigned char	nofl, masa, adreb, nxadr;
	long			coef;
	long			madrs;
} aicadsp_paso_dec;

/*
	El estado de trabajo, en un solo bloque para que el codigo emitido lo
	direccione entero desde un registro base. Los ultimos cuatro campos son
	del mecanismo: dos derramaderos para las llamadas a los ayudantes, y
	RBP/mascara horneados al reconstruir (leerlos por muestra y por paso era
	releer un registro que solo cambia cuando dsp_sucio ya se levanto).
*/
struct aicadsp_est
{
	long			temp[128];		/* 24 bits: la memoria de retardo */
	long			mems[32];		/* 24 bits: lo que trajo la memoria */
	long			mixs[16];		/* 20 bits: lo que acumulan los canales */
	long			exts[2];		/* 16 bits: el CD-DA */
	long			efreg[16];		/* las salidas, 16 bits */
	long			memval[4];		/* el anillo del retardo de MRD */
	long			frc;
	long			y;
	unsigned long	adrs;
	unsigned long	dec;
	long			entrada;		/* derramadero: entrada viva tras una llamada */
	unsigned long	dir;			/* derramadero: la direccion del anillo */
	unsigned long	rbp;			/* horneado: origen del anillo, en palabras */
	unsigned long	mascara;		/* horneado: rbl_palabras - 1 */
};

extern struct aicadsp_est aicadsp_est;

/*
	La instalacion del emisor, como arm7_blq_instalar_emisor(): aicadsp.c no
	sabe de x64. El emisor recibe la tabla y el ultimo paso con efecto, y
	devuelve el programa emitido (NULL si declino); se lo llama en cada
	reconstruccion. NULL desinstala.
*/
typedef void (* aicadsp_fn)(void);
void aicadsp_instalar_emisor(aicadsp_fn (* emisor)(const aicadsp_paso_dec * tabla,
	int ultimo));

/* El censo, para traza_resumen(): pasos con programa, muestras corridas con
   programa, envios a MIXS, y los EFSDL vistos. */
void aicadsp_resumen(void);

#endif /* _AICADSP_H_ */
