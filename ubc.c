/* Ver ubc.h. La referencia de uso es el driver de KOS
   (kernel/arch/dreamcast/hardware/ubc.c) y la norma el manual del SH7750,
   capitulo del UBC. */

#include <string.h>

#include <stdio.h>

#include "main.h"			/* solo por los tipos y sh4emu.h */
#include "sh4emu.h"
#include "excepciones.h"
#include "traza.h"
#include "ubc.h"

DWORD *	UBC_BARA;
BYTE *	UBC_BAMRA;
WORD *	UBC_BBRA;
DWORD *	UBC_BARB;
BYTE *	UBC_BAMRB;
WORD *	UBC_BBRB;
DWORD *	UBC_BDRB;
DWORD *	UBC_BDMRB;
WORD *	UBC_BRCR;
BYTE *	UBC_BASRA;
BYTE *	UBC_BASRB;

int ubc_activa = 0;
int ubc_operando_activa = 0;

/* Break detectado y todavia no entregado: se entrega en la frontera
   siguiente, que es lo que hace "despues de ejecutar" en el chip. */
static int ubc_pendiente = 0;

/* En modo secuencial, 1 desde que A cumplio hasta que B dispara. */
static int ubc_secuencia = 0;

/* Campos de BBR. */
#define BBR_ID(bbr)		(((bbr) >> 4) & 3)	/* 1 instruccion, 2 operando */
#define BBR_RW(bbr)		(((bbr) >> 2) & 3)	/* 1 lectura, 2 escritura */
#define BBR_SZ(bbr)		((((bbr) >> 4) & 4) | ((bbr) & 3))

#define BAMR_BASM		0x04				/* 1 = no comparar ASID */

void ubc_reiniciar(void)
{
	ubc_pendiente = 0;
	ubc_secuencia = 0;
	ubc_activa = 0;
	ubc_operando_activa = 0;
}

void ubc_registros_escritos(void)
{
	int a = BBR_ID(*UBC_BBRA) != 0;
	int b = BBR_ID(*UBC_BBRB) != 0;

	ubc_activa = a || b;
	ubc_operando_activa = (a && (BBR_ID(*UBC_BBRA) & 2))
					   || (b && (BBR_ID(*UBC_BBRB) & 2));

	/* Reconfigurar los canales reinicia la secuencia. El break pendiente y
	   CMFA/CMFB no se tocan: los primeros dos son del chip y los flags del
	   guest. */
	ubc_secuencia = 0;

	if (traza_activa)
		fprintf(stderr, "traza: UBC regs: A bar=%08lx bamr=%02x bbr=%04x | "
			"B bar=%08lx bamr=%02x bbr=%04x bdr=%08lx bdmr=%08lx | brcr=%04x\n",
			(unsigned long) *UBC_BARA, *UBC_BAMRA, *UBC_BBRA,
			(unsigned long) *UBC_BARB, *UBC_BAMRB, *UBC_BBRB,
			(unsigned long) *UBC_BDRB, (unsigned long) *UBC_BDMRB,
			*UBC_BRCR);
}

/* Los bits de BAM: cuantos bits bajos de la direccion se ignoran. El codigo
   011 no compara la direccion en absoluto. */
static DWORD ubc_mascara(BYTE bamr)
{
	switch ((((bamr) >> 1) & 4) | ((bamr) & 3))
	{
		case 0:		return 0x00000000;
		case 1:		return 0x000003FF;
		case 2:		return 0x00000FFF;
		case 3:		return 0xFFFFFFFF;
		case 4:		return 0x0000FFFF;
		case 5:		return 0x000FFFFF;
		default:	return 0x00000000;
	}
}

static int ubc_dir_coincide(DWORD dir, DWORD bar, BYTE bamr, BYTE basr)
{
	DWORD ignorar = ubc_mascara(bamr);

	if ((dir & ~ignorar) != (bar & ~ignorar))
		return 0;

	/* El ASID solo se compara con BASM en 0 (y solo importa con la MMU). */
	if (!(bamr & BAMR_BASM) && basr != (BYTE) (*PTEH & 0xFF))
		return 0;

	return 1;
}

static int ubc_tam_coincide(WORD bbr, size_t tam)
{
	switch (BBR_SZ(bbr))
	{
		case 0:		return 1;			/* sin restriccion de tamano */
		case 1:		return tam == 1;
		case 2:		return tam == 2;
		case 3:		return tam == 4;
		case 4:		return tam == 8;
		default:	return 0;
	}
}

/* El dato del canal B: BDMRB con un bit en 1 lo excluye de la comparacion, y
   el ancho lo fija el tamano del acceso. */
static int ubc_dato_coincide(const void * valor, size_t tam)
{
	DWORD v = 0;
	DWORD ignorar = *UBC_BDMRB;

	memcpy(&v, valor, tam < 4 ? tam : 4);

	switch (tam)
	{
		case 1:		ignorar |= 0xFFFFFF00;	break;
		case 2:		ignorar |= 0xFFFF0000;	break;
		default:	break;
	}

	return (v & ~ignorar) == (*UBC_BDRB & ~ignorar);
}

/* La condicion del canal A cumplida: flag, y excepcion o latch segun SEQ.
   `despues` en 1 pospone la entrega a la frontera siguiente. */
static int ubc_cumplio_a(int despues)
{
	*UBC_BRCR |= UBC_BRCR_CMFA;

	if (traza_activa)
		fprintf(stderr, "traza: UBC canal A cumplio en PC=%08lx%s\n",
			(unsigned long) PC,
			(*UBC_BRCR & UBC_BRCR_SEQ) ? " (SEQ: arma a B)" : "");

	if (*UBC_BRCR & UBC_BRCR_SEQ)
	{
		ubc_secuencia = 1;		/* arma al canal B, sin excepcion */
		return 0;
	}

	ubc_pendiente = 1;

	return !despues;
}

static int ubc_cumplio_b(int despues)
{
	if ((*UBC_BRCR & UBC_BRCR_SEQ) && !ubc_secuencia)
		return 0;				/* B solo vale despues de A */

	*UBC_BRCR |= UBC_BRCR_CMFB;
	ubc_secuencia = 0;
	ubc_pendiente = 1;

	if (traza_activa)
		fprintf(stderr, "traza: UBC canal B cumplio en PC=%08lx\n",
			(unsigned long) PC);

	return !despues;
}

/*
	La frontera. El pendiente se entrega primero: viene de la instruccion (o
	del operando) anterior, asi que SPC = PC actual es exactamente "la
	siguiente". Con SR.BL puesto espera -- la peticion no se pierde, igual
	que las del INTC.
*/
int ubc_revisar_instruccion(void)
{
	int entrar = 0;

	if (ubc_pendiente)
	{
		if (IS_SH4_REG_SET(SR_BL))
			return 0;

		ubc_pendiente = 0;
		excepcion_entrar(EXC_UBC_BREAK, EXC_VEC_GENERAL);
		return 1;
	}

	if ((BBR_ID(*UBC_BBRA) & 1)
	&&  ubc_dir_coincide(PC, *UBC_BARA, *UBC_BAMRA, *UBC_BASRA))
		entrar |= ubc_cumplio_a((*UBC_BRCR & UBC_BRCR_PCBA) != 0);

	if ((BBR_ID(*UBC_BBRB) & 1)
	&&  ubc_dir_coincide(PC, *UBC_BARB, *UBC_BAMRB, *UBC_BASRB))
		entrar |= ubc_cumplio_b((*UBC_BRCR & UBC_BRCR_PCBB) != 0);

	if (entrar)
	{
		/* Break antes de ejecutar: SPC = la instruccion que coincidio, que se
		   reejecuta al volver -- y vuelve a coincidir salvo que el manejador
		   cambie la configuracion, igual que en el chip. */
		ubc_pendiente = 0;
		excepcion_entrar(EXC_UBC_BREAK, EXC_VEC_GENERAL);
		return 1;
	}

	return 0;
}

/* RW del BBR contra el sentido del acceso: bit 0 lectura, bit 1 escritura. */
#define RW_COINCIDE(bbr, escritura)	\
	(BBR_RW(bbr) & ((escritura) ? 2 : 1))

void ubc_operando(unsigned long direccion, const void * valor, size_t tam,
				  int escritura)
{
	if ((BBR_ID(*UBC_BBRA) & 2)
	&&  RW_COINCIDE(*UBC_BBRA, escritura)
	&&  ubc_tam_coincide(*UBC_BBRA, tam)
	&&  ubc_dir_coincide((DWORD) direccion, *UBC_BARA, *UBC_BAMRA, *UBC_BASRA))
		ubc_cumplio_a(1);		/* operando: siempre despues */

	if ((BBR_ID(*UBC_BBRB) & 2)
	&&  RW_COINCIDE(*UBC_BBRB, escritura)
	&&  ubc_tam_coincide(*UBC_BBRB, tam)
	&&  ubc_dir_coincide((DWORD) direccion, *UBC_BARB, *UBC_BAMRB, *UBC_BASRB)
	&&  (!(*UBC_BRCR & UBC_BRCR_DBEB) || ubc_dato_coincide(valor, tam)))
		ubc_cumplio_b(1);
}
