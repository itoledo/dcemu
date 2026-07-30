/****************************************************************************

	MMU - la ventana de control P4 del SH-4

	Ver mmu.h y docs/mmu-plan.md, fase 1.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "excepciones.h"
#include "mmu.h"
#include "log.h"
#include "sh4emu.h"

int mmu_activa = 0;

DWORD	mmu_exc_direccion;

DWORD mmu_itlb_dir[MMU_ITLB_ENTRADAS];
DWORD mmu_itlb_dat1[MMU_ITLB_ENTRADAS];
DWORD mmu_itlb_dat2[MMU_ITLB_ENTRADAS];

DWORD mmu_utlb_dir[MMU_UTLB_ENTRADAS];
DWORD mmu_utlb_dat1[MMU_UTLB_ENTRADAS];
DWORD mmu_utlb_dat2[MMU_UTLB_ENTRADAS];

/* Nombres cortos para uso interno; el formato lo define mmu.h. */
#define BIT_V			MMU_BIT_V

/* Campos de una entrada de direcciones que hacen falta para la busqueda
   asociativa. */
#define VPN_DE(e)		((e) & 0xFFFFFC00)
#define ASID_DE(e)		((e) & 0x000000FF)
#define BIT_SH			MMU_BIT_SH
#define BIT_D_DIR		MMU_BIT_D_DIR
#define BIT_D_DAT		MMU_BIT_D_DAT

void mmu_reset(void)
{
	mmu_activa = 0;
	excepcion_salto_armado = 0;
	excepcion_actualizar_vigilancia();

	memset(mmu_itlb_dir,  0, sizeof(mmu_itlb_dir));
	memset(mmu_itlb_dat1, 0, sizeof(mmu_itlb_dat1));
	memset(mmu_itlb_dat2, 0, sizeof(mmu_itlb_dat2));

	memset(mmu_utlb_dir,  0, sizeof(mmu_utlb_dir));
	memset(mmu_utlb_dat1, 0, sizeof(mmu_utlb_dat1));
	memset(mmu_utlb_dat2, 0, sizeof(mmu_utlb_dat2));
}

/* ------------------------------------------------------------------------ */
/* Entrada y salida de un valor de 32 bits                                  */
/* ------------------------------------------------------------------------ */

/*
	El manual solo admite acceso de palabra larga a estas ventanas. En vez de
	rechazar los demas tamanos -- que dejaria el destino sin tocar y haria la
	corrida irreproducible, el mismo problema que arreglaba mem_read_error() --
	se copian los bytes bajos que entren.
*/
static void devolver(DWORD valor, void * p, size_t size)
{
	if (!p)
		return;

	if (size >= sizeof(DWORD))
	{
		memcpy(p, &valor, sizeof(DWORD));

		if (size > sizeof(DWORD))
			memset((unsigned char *) p + sizeof(DWORD), 0, size - sizeof(DWORD));
	}
	else
		memcpy(p, &valor, size);
}

static DWORD recoger(const void * p, size_t size)
{
	DWORD valor = 0;

	if (!p)
		return 0;

	memcpy(&valor, p, (size > sizeof(DWORD)) ? sizeof(DWORD) : size);

	return valor;
}

/* ------------------------------------------------------------------------ */
/* Busqueda asociativa en la UTLB                                           */
/* ------------------------------------------------------------------------ */

/*
	Escribir en el arreglo de direcciones con el bit A puesto no indexa: compara
	el VPN (y el ASID, salvo en paginas compartidas) contra todas las entradas y
	actualiza la que coincide. Es como el software invalida una pagina puntual.

	Se implementa ahora, aunque nadie traduzca todavia, porque la alternativa
	seria indexar con los bits 13-8 -- o sea, pisar una entrada al azar que no
	es la que pidieron. Un respaldo plano que miente no sirve ni para probar.
*/
static void utlb_escritura_asociativa(DWORD valor)
{
	int i;
	int encontrada = 0;

	for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
	{
		if (!(mmu_utlb_dir[i] & BIT_V))
			continue;

		if (VPN_DE(mmu_utlb_dir[i]) != VPN_DE(valor))
			continue;

		/* Una pagina compartida coincide con cualquier ASID. */
		if (!(mmu_utlb_dat1[i] & BIT_SH) && ASID_DE(mmu_utlb_dir[i]) != ASID_DE(valor))
			continue;

		/* Solo se actualizan V y D; el resto de la entrada queda como estaba. */
		mmu_utlb_dir[i] = (mmu_utlb_dir[i] & ~(BIT_V | 0x00000200))
						| (valor & (BIT_V | 0x00000200));

		encontrada = 1;
	}

	/* Sin coincidencia no pasa nada. En el chip de verdad dos coincidencias son
	   causa de reset; aca se registran y se sigue, como dice el plan. */
	if (!encontrada)
		logxmsg(LOG_MEM, "mmu: escritura asociativa en UTLB sin coincidencia, VPN %08x\n", VPN_DE(valor));
}

/* ------------------------------------------------------------------------ */
/* Lectura                                                                  */
/* ------------------------------------------------------------------------ */

void mmu_p4_read(unsigned long direccion, void * p, size_t size)
{
	switch ((direccion >> 24) & 0xFF)
	{
		/* Arreglos de la cache. dcemu no emula cache, asi que toda linea esta
		   invalida: etiqueta 0, U=0, V=0. Es la respuesta honesta, y ademas es
		   la que evita que el software crea que hay algo sucio que volcar. */
		case MMU_P4_IC_DIR:
		case MMU_P4_IC_DAT:
		case MMU_P4_OC_DIR:
		case MMU_P4_OC_DAT:
		devolver(0, p, size);
		break;

		case MMU_P4_ITLB_DIR:
		devolver(mmu_itlb_dir[MMU_ITLB_INDICE(direccion)], p, size);
		break;

		case MMU_P4_ITLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			devolver(mmu_itlb_dat2[MMU_ITLB_INDICE(direccion)], p, size);
		else
			devolver(mmu_itlb_dat1[MMU_ITLB_INDICE(direccion)], p, size);
		break;

		case MMU_P4_UTLB_DIR:
		devolver(mmu_utlb_dir[MMU_UTLB_INDICE(direccion)], p, size);
		break;

		case MMU_P4_UTLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			devolver(mmu_utlb_dat2[MMU_UTLB_INDICE(direccion)], p, size);
		else
			devolver(mmu_utlb_dat1[MMU_UTLB_INDICE(direccion)], p, size);
		break;

		default:
		devolver(0, p, size);
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Escritura                                                                */
/* ------------------------------------------------------------------------ */

void mmu_p4_write(unsigned long direccion, void * p, size_t size)
{
	DWORD valor = recoger(p, size);

	switch ((direccion >> 24) & 0xFF)
	{
		/*
			Arreglos de la cache: se acepta y se descarta.

			Con el bit A puesto esto es una purga asociativa, que en el chip
			vuelca la linea a memoria si estaba sucia. dcemu escribe siempre
			directo a memoria, asi que nunca hay nada sucio y no volcar es
			correcto, no una simplificacion.

			Esta rama es la que saca de --traza-mem las 512 escrituras por
			corrida que hace KOS al barrer la cache de operandos.
		*/
		case MMU_P4_IC_DIR:
		case MMU_P4_IC_DAT:
		case MMU_P4_OC_DIR:
		case MMU_P4_OC_DAT:
		break;

		case MMU_P4_ITLB_DIR:
		mmu_itlb_dir[MMU_ITLB_INDICE(direccion)] = valor;
		break;

		case MMU_P4_ITLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			mmu_itlb_dat2[MMU_ITLB_INDICE(direccion)] = valor;
		else
			mmu_itlb_dat1[MMU_ITLB_INDICE(direccion)] = valor;
		break;

		case MMU_P4_UTLB_DIR:
		if (direccion & MMU_BIT_A_TLB)
			utlb_escritura_asociativa(valor);
		else
		{
			int e = MMU_UTLB_INDICE(direccion);

			mmu_utlb_dir[e] = valor;

			/* V y D son un solo bit del chip, visible desde los dos arreglos:
			   en el de direcciones estan en 8 y 9, en el de datos 1 en 8 y 2.
			   Escribir uno tiene que verse en el otro. */
			mmu_utlb_dat1[e] = (mmu_utlb_dat1[e] & ~(BIT_V | BIT_D_DAT))
							 | (valor & BIT_V)
							 | ((valor & BIT_D_DIR) >> 7);
		}
		break;

		case MMU_P4_UTLB_DAT:
		if (direccion & MMU_BIT_DATOS2)
			mmu_utlb_dat2[MMU_UTLB_INDICE(direccion)] = valor;
		else
		{
			int e = MMU_UTLB_INDICE(direccion);

			mmu_utlb_dat1[e] = valor;

			mmu_utlb_dir[e] = (mmu_utlb_dir[e] & ~(BIT_V | BIT_D_DIR))
							| (valor & BIT_V)
							| ((valor & BIT_D_DAT) << 7);
		}
		break;

		default:
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* LDTLB                                                                    */
/* ------------------------------------------------------------------------ */

void mmu_ldtlb(DWORD pteh, DWORD ptel, DWORD ptea, int urc)
{
	int e = urc & (MMU_UTLB_ENTRADAS - 1);

	/* PTEH: VPN en 31-10, ASID en 7-0. Los bits 9-8 son reservados. */
	mmu_utlb_dir[e] = (pteh & 0xFFFFFC00) | (pteh & 0x000000FF)
					| (ptel & BIT_V)					/* V viene de PTEL */
					| ((ptel & BIT_D_DAT) << 7);		/* D tambien, bit 2 -> 9 */

	/* PTEL tiene exactamente el formato del arreglo de datos 1: PPN en 28-10,
	   V, SZ1, PR, SZ0, C, D, SH y WT en los bits bajos. */
	mmu_utlb_dat1[e] = ptel & 0x1FFFFDFF;

	/* PTEA: TC en el bit 3, SA en 2-0. */
	mmu_utlb_dat2[e] = ptea & 0x0000000F;
}

/* ------------------------------------------------------------------------ */
/* Traduccion                                                               */
/* ------------------------------------------------------------------------ */

/*
	SZ1 esta en el bit 7 y SZ0 en el 4. Juntos dan el tamano de pagina, y de
	ahi sale la mascara del desplazamiento dentro de la pagina.
*/
static DWORD mascara_de_pagina(DWORD dat1)
{
	switch ((((dat1) >> 6) & 2) | (((dat1) >> 4) & 1))
	{
		case 0:		return 0x000003FFul;	/*  1 KB */
		case 1:		return 0x00000FFFul;	/*  4 KB */
		case 2:		return 0x0000FFFFul;	/* 64 KB */
		default:	return 0x000FFFFFul;	/*  1 MB */
	}
}

/*
	Deja la excepcion preparada y sale por el salto. No devuelve, salvo que no
	haya salto armado -- que es el caso de un acceso disparado por el propio
	emulador y no por una instruccion, donde no hay nada que reejecutar.
*/
static DWORD fallar(DWORD codigo, DWORD vector, DWORD direccion)
{
	mmu_exc_direccion = direccion;

	/* TEA lleva la direccion que fallo y PTEH.VPN su VPN; el ASID no se toca. */
	*TEA  = direccion;
	*PTEH = (direccion & 0xFFFFFC00) | (*PTEH & 0x000000FF);

	/* No devuelve si hay salto armado. Si no lo hay -- un acceso del propio
	   emulador, no de una instruccion -- deja la excepcion anotada y sigue. */
	excepcion_abortar(codigo, vector);

	return direccion;
}

DWORD mmu_traducir(DWORD direccion, int escritura)
{
	int usuario = (SR_MD == 0);
	int sv      = (*MMUCR & MMUCR_SV) != 0;
	int i;

	/*
		P1 y P2 no pasan por la TLB nunca, y se devuelven SIN tocar. Convertirlas
		a fisica romperia el despacho: mem_hash_read[0x80] es bios_read mientras
		que mem_hash_read[0xA0] es pvr_read, o sea que la misma direccion fisica
		se atiende distinto segun la ventana. Dejarlas como estan mantiene el
		comportamiento actual exacto.
	*/
	if (direccion >= 0x80000000ul && direccion < 0xC0000000ul)
	{
		if (usuario)
			return fallar(escritura ? MMU_EXC_DIR_W : MMU_EXC_DIR_R,
						  MMU_VEC_GENERAL, direccion);

		return direccion;
	}

	/* P4: registros de control y store queues. Tampoco se traduce. Que las
	   store queues respeten MMUCR.SQMD es la fase 6. */
	if (direccion >= 0xE0000000ul)
	{
		if (usuario && direccion >= 0xE4000000ul)
			return fallar(escritura ? MMU_EXC_DIR_W : MMU_EXC_DIR_R,
						  MMU_VEC_GENERAL, direccion);

		return direccion;
	}

	/* P3 es solo privilegiada. P0/U0 la ve cualquiera. */
	if (direccion >= 0xC0000000ul && usuario)
		return fallar(escritura ? MMU_EXC_DIR_W : MMU_EXC_DIR_R,
					  MMU_VEC_GENERAL, direccion);

	/*
		Recorrido lineal de las 64 entradas, desempaquetando al vuelo. Es lo
		mas lento que se puede hacer y esta bien asi por ahora: con AT en cero
		no se ejecuta nunca, y cuando se ejecuta lo que importa es que este
		bien. Si llega a molestar, el paso siguiente es un arreglo decodificado
		en paralelo, actualizado en los cuatro sitios que mutan la TLB.
	*/
	for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
	{
		DWORD dir = mmu_utlb_dir[i];
		DWORD d1  = mmu_utlb_dat1[i];
		DWORD mascara, fisica;
		int pr;

		if (!(dir & BIT_V))
			continue;

		mascara = mascara_de_pagina(d1);

		if ((dir & ~mascara) != (direccion & ~mascara))
			continue;

		/* El ASID se ignora en paginas compartidas, y tambien en modo espacio
		   unico (MMUCR.SV) cuando corremos en modo privilegiado. */
		if (!(d1 & BIT_SH) && !(sv && !usuario)
			&& ASID_DE(dir) != ASID_DE(*PTEH))
			continue;

		/* PR: 00 privilegiado solo lectura, 01 privilegiado lectura/escritura,
		   10 todos solo lectura, 11 todos lectura/escritura. */
		pr = (d1 >> 5) & 3;

		if (usuario && pr < 2)
			return fallar(escritura ? MMU_EXC_PROT_W : MMU_EXC_PROT_R,
						  MMU_VEC_GENERAL, direccion);

		if (escritura && !(pr & 1))
			return fallar(MMU_EXC_PROT_W, MMU_VEC_GENERAL, direccion);

		/* Escribir en una pagina limpia no es violacion de proteccion sino
		   "primera escritura": el manejador marca la pagina y reejecuta. */
		if (escritura && !(dir & BIT_D_DIR))
			return fallar(MMU_EXC_PRIMERA_W, MMU_VEC_GENERAL, direccion);

		/* PPN en los bits 28-10 del arreglo de datos 1. */
		fisica = ((d1 & 0x1FFFFC00ul) & ~mascara) | (direccion & mascara);

		return fisica | 0xA0000000ul;
	}

	return fallar(escritura ? MMU_EXC_FALLO_W : MMU_EXC_FALLO_R,
				  MMU_VEC_FALLO, direccion);
}

/* ------------------------------------------------------------------------ */
/* MMUCR                                                                    */
/* ------------------------------------------------------------------------ */

int mmu_mmucr_escrito(DWORD valor)
{
	static int ya_aviso = 0;
	int i;

	/* TI invalida la TLB entera: baja el bit V de todas las entradas. Es un
	   bit de un solo disparo, no queda puesto. */
	if (valor & MMUCR_TI)
	{
		for (i = 0; i < MMU_ITLB_ENTRADAS; i++)
		{
			mmu_itlb_dir[i]  &= ~BIT_V;
			mmu_itlb_dat1[i] &= ~BIT_V;
		}

		for (i = 0; i < MMU_UTLB_ENTRADAS; i++)
		{
			mmu_utlb_dir[i]  &= ~BIT_V;
			mmu_utlb_dat1[i] &= ~BIT_V;
		}

		logxmsg(LOG_MEM, "mmu: TI, TLB invalidada\n");
	}

	mmu_activa = (valor & MMUCR_AT) != 0;
	excepcion_actualizar_vigilancia();

	if (!mmu_activa)
		return 0;

	/*
		Sale por stderr y no por logxmsg porque tiene que verse sin LOGGING.
		La traduccion ya funciona para los accesos a datos, pero quedan dos
		agujeros conocidos y conviene que quien depure los tenga presentes.
	*/
	if (!ya_aviso)
	{
		fprintf(stderr,
			"mmu: MMUCR.AT=1 -- traduccion encendida.\n"
			"     Sin traducir todavia: las store queues (fase 6) y la busqueda\n"
			"     de instrucciones (fase 7). Ver docs/mmu-plan.md.\n");

		ya_aviso = 1;
	}

	return 1;
}
