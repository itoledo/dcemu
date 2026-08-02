/****************************************************************************

	Pruebas de aica.c: el bloque de registros del AICA.

	El guion no se invento. Es el que programa aica_init() del firmware ARM de
	KOS (kernel/arch/dreamcast/sound/arm/aica.c) y el que su crt0.s espera
	encontrar: enmascarar SCIEB/MCIEB, reconocer todo con SCIRE/MCIRE, fijar
	los tres SCILV, limpiar los 64 canales, recargar el temporizador A y
	desenmascararlo como unica fuente de FIQ.

	El caso que mas dice es el del nivel: con los SCILV que KOS escribe, el
	temporizador A tiene que dar **2** y la entrada MIDI **5**, que son
	exactamente los dos numeros contra los que compara el crt0.s. Si la lectura
	de los tres registros fuera otra, el firmware entraria por la rama
	equivocada de su FIQ.

	Ver docs/aica-plan.md, fase 1.

*****************************************************************************/

#include <string.h>

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "aica.h"
#include "tmu.h"
#include "intc.h"

extern int   dobles_int_ext;
extern DWORD dobles_ultima_int_ext;

/* Las dos ventanas, tal como las ve cada procesador. */
#define G2(off)		(AICA_REG_BASE + (off))

static DWORD leer_g2(unsigned long off)
{
	DWORD v = 0xDEADBEEF;

	aica_leer(G2(off), &v, sizeof(v));

	return v;
}

static void escribir_g2(unsigned long off, DWORD v)
{
	aica_escribir(G2(off), &v, sizeof(v));
}

/*
	Avanza el reloj monotono hasta el ciclo exacto en que el chip alcanza la
	muestra numero N. Sumar n*3324992/735 por llamada trunca -- una muestra son
	4523,8 ciclos -- y pierde una muestra cada tanto, que en un contador de 8
	bits se nota en el desborde.
*/
static unsigned long long muestras_pedidas;

static void reiniciar(void)
{
	aica_reset();
	muestras_pedidas = aica_muestras;
}

static void avanzar_muestras(unsigned n)
{
	muestras_pedidas += n;
	reloj_total = (muestras_pedidas * 3324992ull + 734ull) / 735ull;
	aica_tick();
}

/* ------------------------------------------------------------------------ */

static void el_respaldo_guarda_16_bits(void)
{
	/* "Register accesses by the SH4 are 4-byte accesses only, and only the
	   lower 16 bits are valid." Lo que pase de 16 bits no se guarda. */
	reiniciar();

	escribir_g2(AICA_SCIEB, 0x12345678);
	ESPERAR_U32(leer_g2(AICA_SCIEB), 0x5678);

	escribir_g2(AICA_RBP, 0xFFFF);
	ESPERAR_U32(leer_g2(AICA_RBP), 0xFFFF);
}

static void el_arm_arranca_en_reset(void)
{
	/* Es lo que permite a KOS cargar el firmware antes de soltarlo:
	   spu_disable() pone el bit, spu_enable() lo quita. */
	reiniciar();
	ESPERAR_I32(aica_arm_en_reset(), 1);

	escribir_g2(AICA_ARMRST, 0);
	ESPERAR_I32(aica_arm_en_reset(), 0);

	escribir_g2(AICA_ARMRST, 1);
	ESPERAR_I32(aica_arm_en_reset(), 1);
}

static void armrst_es_solo_del_sh4(void)
{
	/* "This register can only be controlled by the system (SH4)." */
	reiniciar();
	escribir_g2(AICA_ARMRST, 0);

	aica_arm_escribir(AICA_ARMRST, 4, 1);
	ESPERAR_I32(aica_arm_en_reset(), 0);
}

static void l_y_m_son_solo_del_arm(void)
{
	/* Y al reves: "This register can only be controlled by the ARM." */
	reiniciar();
	escribir_g2(AICA_SCIEB, AICA_INT_TIMER_A);
	escribir_g2(AICA_SCIPD, 0);
	aica_arm_escribir(AICA_SCIPD, 4, AICA_INT_CPU);	/* deja una pendiente */

	ESPERAR_U32(leer_g2(AICA_INT_L), 0);
}

static void la_version_se_lee_encima_de_lo_escrito(void)
{
	/* VER[3:0] esta en los bits 11-8 de 0x2800 y es de solo lectura. El resto
	   del registro --MVOL, MONO, MEM8MB-- es del guest. */
	reiniciar();

	escribir_g2(AICA_MVOL, 0x800F);		/* MONO + MVOL maximo */

	ESPERAR_U32(leer_g2(AICA_MVOL) & 0x0F00, (AICA_VERSION & 0xF) << 8);
	ESPERAR_U32(leer_g2(AICA_MVOL) & 0x800F, 0x800F);
}

static void el_arm_escribe_bytes(void)
{
	/* aica.c de KOS escribe el paneo en +0x24, el nivel de envio en +0x25 y
	   el volumen en +0x29, byte a byte. Con un archivo de palabras esos tres
	   se pisarian entre si. */
	unsigned long canal3 = 3 * AICA_CANAL_PASO;

	reiniciar();

	aica_arm_escribir(canal3 + 0x24, 1, 0x11);		/* DIPAN */
	aica_arm_escribir(canal3 + 0x25, 1, 0x0F);		/* DISDL */

	ESPERAR_U32(leer_g2(canal3 + 0x24), 0x0F11);
	ESPERAR_U32(aica_arm_leer(canal3 + 0x24, 1), 0x11);
	ESPERAR_U32(aica_arm_leer(canal3 + 0x25, 1), 0x0F);

	aica_arm_escribir(canal3 + 0x28, 1, 0x24);		/* Q: filtro apagado */
	aica_arm_escribir(canal3 + 0x29, 1, 0x3F);		/* TL */

	ESPERAR_U32(leer_g2(canal3 + 0x28), 0x3F24);
}

static void los_canales_no_se_pisan(void)
{
	/* 64 ranuras de 0x80 bytes desde el offset 0. Un error de paso aqui haria
	   que programar un canal moviera otro. */
	int c;

	reiniciar();

	for (c = 0; c < AICA_CANALES; c++)
		escribir_g2(c * AICA_CANAL_PASO + 0x18, 0x1000 + c);

	for (c = 0; c < AICA_CANALES; c++)
		ESPERAR_U32(leer_g2(c * AICA_CANAL_PASO + 0x18), (DWORD) (0x1000 + c));
}

/* ------------------------------------------------------------------------ */

static void el_nivel_sale_de_los_tres_scilv(void)
{
	/* Los valores son los de aica_init() de KOS, literales. El nivel de la
	   fuente i es SCILV2:SCILV1:SCILV0 leidos en el bit i. */
	reiniciar();

	escribir_g2(AICA_SCILV0, 0x18);
	escribir_g2(AICA_SCILV1, 0x50);
	escribir_g2(AICA_SCILV2, 0x08);

	/* Temporizador A -- fuente 6. crt0.s: "cmp r9,#2 / beq fiq_timer". */
	escribir_g2(AICA_SCIEB, AICA_INT_TIMER_A);
	aica_arm_escribir(AICA_SCIPD, 4, 0);
	escribir_g2(AICA_SCIRE, AICA_INT_TODAS);

	/* Se deja pendiente el temporizador A haciendolo desbordar. */
	escribir_g2(AICA_TIMER_A, 0xFF);
	avanzar_muestras(1);

	ESPERAR_I32(aica_fiq_pendiente(), 1);
	aica_fiq_tomada();
	ESPERAR_U32(aica_arm_leer(AICA_INT_L, 4), 2);
}

static void el_nivel_de_la_entrada_midi_es_cinco(void)
{
	/* Fuente 3. crt0.s: "cmp r9,#5 / beq fiq_busreq". Es la otra rama, y con
	   ella queda fijada la lectura de los tres registros. */
	reiniciar();

	escribir_g2(AICA_SCILV0, 0x18);
	escribir_g2(AICA_SCILV1, 0x50);
	escribir_g2(AICA_SCILV2, 0x08);

	escribir_g2(AICA_SCIEB, AICA_INT_MIDI_IN);
	aica_arm_escribir(AICA_SCIPD, 4, 0);

	/* La entrada MIDI no llega desde ningun lado en dcemu, asi que se pone
	   la pendiente a mano: lo que se prueba es el nivel, no la fuente. */
	aica_arm_escribir(AICA_SCIPD, 4, AICA_INT_CPU);
	escribir_g2(AICA_SCIEB, AICA_INT_CPU | AICA_INT_MIDI_IN);
	escribir_g2(AICA_SCIRE, AICA_INT_CPU);

	/* Con las dos pendientes gana la de numero menor, que es la MIDI. */
	aica_arm_escribir(AICA_SCIPD, 4, AICA_INT_CPU);
	ESPERAR_I32(aica_fiq_pendiente(), 1);
	aica_fiq_tomada();

	/* La fuente 5 --CPU-> ARM-- tiene nivel 0 con estos SCILV. */
	ESPERAR_U32(aica_arm_leer(AICA_INT_L, 4), 0);
}

static void scire_limpia_y_m_libera(void)
{
	/*
		El ciclo completo tal como lo hace el crt0.s: atender, escribir SCIRE
		con el bit de la fuente, y despues M. Sin M la linea no baja, y sin
		SCIRE la fuente vuelve a pedir en cuanto M la libera.
	*/
	reiniciar();

	escribir_g2(AICA_SCIEB, AICA_INT_TIMER_A);
	escribir_g2(AICA_TIMER_A, 0xFF);
	avanzar_muestras(1);

	ESPERAR_I32(aica_fiq_pendiente(), 1);
	aica_fiq_tomada();

	/* En curso: no vuelve a pedir aunque siga pendiente. */
	ESPERAR_I32(aica_fiq_pendiente(), 0);

	/* Solo M no alcanza: la fuente sigue pendiente. */
	aica_arm_escribir(AICA_INT_M, 4, 1);
	ESPERAR_I32(aica_fiq_pendiente(), 1);

	/* Con SCIRE se limpia de verdad. */
	aica_fiq_tomada();
	aica_arm_escribir(AICA_SCIRE, 4, AICA_INT_TIMER_A);
	aica_arm_escribir(AICA_INT_M, 4, 1);
	ESPERAR_I32(aica_fiq_pendiente(), 0);
}

static void una_fuente_sin_mascara_sigue_pendiente(void)
{
	/*
		Es el mismo error que tenia check_ints() con los eventos del ASIC y que
		costo el arranque por BIOS: si en el momento del evento nadie lo tiene
		habilitado, se descartaba. En el chip la bandera queda puesta y la
		interrupcion llega en cuanto el guest levanta la mascara.
	*/
	reiniciar();

	escribir_g2(AICA_SCIEB, 0);
	escribir_g2(AICA_TIMER_A, 0xFF);
	avanzar_muestras(1);

	ESPERAR_I32(aica_fiq_pendiente(), 0);
	ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_TIMER_A, AICA_INT_TIMER_A);

	escribir_g2(AICA_SCIEB, AICA_INT_TIMER_A);
	ESPERAR_I32(aica_fiq_pendiente(), 1);
}

static void la_interrupcion_al_sh4_sale_por_el_asic(void)
{
	/*
		G2AICINT es el bit 1 del registro externo, junto al fin de comando de la
		lectora (ASIC_EVT_SPU_IRQ = 0x0101 en asic.h de KOS).

		**El chip levanta una linea de nivel; no entrega la interrupcion.** Antes
		llamaba a intc_add_ext() en el acto y esta prueba contaba esas llamadas.
		Con el AICA en su propio hilo (hilo_aica.c) esa llamada ocurriria fuera
		del hilo que atiende el ASIC, y toca tanto intc_queuemask_ext como el
		registro ASIC_ACK_B; ahora el chip deja aica_linea_asic y la cobra
		main_loop(), que es donde vive el controlador. Lo que se prueba aqui es
		la linea, que es lo que el chip hace.
	*/
	reiniciar();

	escribir_g2(AICA_MCIEB, AICA_INT_TIMER_A);
	escribir_g2(AICA_TIMER_A, 0xFF);

	ESPERAR_I32(aica_linea_asic, 0);

	avanzar_muestras(1);

	ESPERAR_I32(aica_linea_asic, 1);

	/* Y reconocer con MCIRE la baja. */
	escribir_g2(AICA_MCIRE, AICA_INT_TODAS);
	ESPERAR_I32(aica_linea_asic, 0);
}

/* ------------------------------------------------------------------------ */

static void el_temporizador_cuenta_hacia_arriba(void)
{
	/* Ocho bits que suben y piden al pasar de 0xFF a 0x00. El guest escribe la
	   recarga, no el periodo. */
	reiniciar();

	escribir_g2(AICA_TIMER_A, 0xF0);		/* faltan 16 muestras */
	avanzar_muestras(15);

	ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_TIMER_A, 0);
	ESPERAR_U32(leer_g2(AICA_TIMER_A) & 0xFF, 0xFF);

	avanzar_muestras(1);
	ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_TIMER_A, AICA_INT_TIMER_A);
	ESPERAR_U32(leer_g2(AICA_TIMER_A) & 0xFF, 0);
}

static void el_prescaler_divide_por_potencias_de_dos(void)
{
	/* TxCTL[2:0]: sube cada 1, 2, 4, ... 128 muestras. */
	int ctl;

	for (ctl = 0; ctl < 8; ctl++)
	{
		unsigned paso = 1u << ctl;

		reiniciar();
		escribir_g2(AICA_TIMER_B, (DWORD) ((ctl << 8) | 0xFF));

		avanzar_muestras(paso - 1);
		ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_TIMER_B, 0);

		avanzar_muestras(1);
		ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_TIMER_B, AICA_INT_TIMER_B);
	}
}

static void el_temporizador_a_da_100_hz_con_lo_que_pone_kos(void)
{
	/*
		crt0.s recarga con 256 - (44100/4410) = 246, o sea 10 muestras por
		desborde, y el firmware cuenta esos desbordes como milisegundos. Con
		TACTL en 0 --una muestra por paso-- eso son 4410 interrupciones por
		segundo, que es lo que hace correr el reloj del planificador de flujos
		de KOS. Aqui se cuenta cuantas caen en un segundo de tiempo emulado.
	*/
	int cuenta = 0;
	int i;

	reiniciar();
	escribir_g2(AICA_TIMER_A, 246);

	/* Un segundo, mirando de a 100 muestras. */
	for (i = 0; i < 441; i++)
	{
		avanzar_muestras(100);

		if (leer_g2(AICA_SCIPD) & AICA_INT_TIMER_A)
		{
			/* Se reconoce y se recarga, igual que la FIQ del firmware. */
			escribir_g2(AICA_SCIRE, AICA_INT_TIMER_A);
			cuenta += 10;			/* diez desbordes por cada 100 muestras */
			escribir_g2(AICA_TIMER_A, 246);
		}
	}

	/* 4410 por segundo, con el redondeo de mirar cada 100 muestras. */
	ESPERAR_I32(cuenta, 4410);
}

static void el_reloj_de_muestreo_es_de_44100_hz(void)
{
	/* 735 muestras cada 3324992 ciclos, exacto. Un segundo de tiempo emulado
	   tiene que dar 44100 muestras, sin deriva acumulada. */
	unsigned long long antes;

	reiniciar();
	antes = aica_muestras;

	reloj_total += 199499520ull;		/* DC_CPU_HZ: un segundo */
	aica_tick();

	ESPERAR_U32((DWORD) (aica_muestras - antes), 44100);
}

/* ------------------------------------------------------------------------ */

static void el_dma_interno_mueve_de_la_onda_a_los_registros(void)
{
	/* DMEA es la RAM de onda, DRGA el archivo de registros, DLG el largo, las
	   tres en palabras. DDIR = 0: de la onda a los registros. */
	int i;

	reiniciar();

	for (i = 0; i < 16; i++)
		sound_mem[0x1000 + i] = (unsigned char) (i + 1);

	escribir_g2(AICA_DMA_DMEA_ALTO, 0);
	escribir_g2(AICA_DMA_DMEA_BAJO, 0x1000);
	escribir_g2(AICA_DMA_DRGA, 0x0100);
	escribir_g2(AICA_DMA_DLG, 16 | 1);		/* DEXE en el bit 0 */

	ESPERAR_BYTES(&aica_reg[0x0100], &sound_mem[0x1000], 16);

	/* "The value goes to 0 at DMA end." */
	ESPERAR_U32(leer_g2(AICA_DMA_DLG) & 1, 0);

	/* Y deja pendiente el fin de DMA. */
	ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_DMA_FIN, AICA_INT_DMA_FIN);
}

static void el_dma_interno_con_dgate_pone_ceros(void)
{
	int i;

	reiniciar();

	for (i = 0; i < 16; i++)
		aica_reg[0x0100 + i] = 0xAB;

	escribir_g2(AICA_DMA_DMEA_ALTO, 0);
	escribir_g2(AICA_DMA_DMEA_BAJO, 0x1000);
	escribir_g2(AICA_DMA_DRGA, 0x8000 | 0x0100);	/* DGATE */
	escribir_g2(AICA_DMA_DLG, 16 | 1);

	for (i = 0; i < 16; i++)
		ESPERAR_U32(aica_reg[0x0100 + i], 0);
}

/* ------------------------------------------------------------------------ */

static void la_secuencia_de_aica_init_de_kos(void)
{
	/*
		El guion completo del firmware, en orden, tal como esta escrito en
		aica_init(). Lo que se comprueba es que al final el chip queda con el
		temporizador A como unica fuente de FIQ y con el mezclador restaurado:
		si algo de esto se perdiera, el firmware arrancaria sordo o sin reloj.
	*/
	int i, j;

	reiniciar();

	aica_arm_escribir(AICA_SCIEB, 4, 0);
	aica_arm_escribir(AICA_MCIEB, 4, 0);
	aica_arm_escribir(AICA_SCIRE, 4, 0x7FF);
	aica_arm_escribir(AICA_MCIRE, 4, 0x7FF);
	aica_arm_escribir(AICA_SCILV0, 4, 0x18);
	aica_arm_escribir(AICA_SCILV1, 4, 0x50);
	aica_arm_escribir(AICA_SCILV2, 4, 0x08);

	aica_arm_escribir(AICA_MVOL, 4, 0x0000);

	for (i = 0; i < 64; i++)
	{
		aica_arm_escribir(i * AICA_CANAL_PASO, 4, 0x8000);

		for (j = 4; j < 0x80; j += 4)
			aica_arm_escribir(i * AICA_CANAL_PASO + j, 4, 0);

		aica_arm_escribir(i * AICA_CANAL_PASO + 20, 4, 0x1f);
	}

	aica_arm_escribir(AICA_MVOL, 4, 0x000f);
	aica_arm_escribir(AICA_TIMER_A, 4, 256 - (44100 / 4410));
	aica_arm_escribir(AICA_SCIEB, 4, 0x40);

	/* Estado final: volumen maestro al maximo, temporizador A armado y
	   habilitado, y nada mas pendiente. */
	ESPERAR_U32(leer_g2(AICA_MVOL) & 0xF, 0xF);
	ESPERAR_U32(leer_g2(AICA_SCIEB), AICA_INT_TIMER_A);
	ESPERAR_U32(leer_g2(AICA_SCIPD) & AICA_INT_TODAS, 0);
	ESPERAR_U32(leer_g2(AICA_TIMER_A) & 0xFF, 246);

	/* Y a los 10 muestreos la primera FIQ, con nivel 2. */
	avanzar_muestras(10);
	ESPERAR_I32(aica_fiq_pendiente(), 1);
	aica_fiq_tomada();
	ESPERAR_U32(aica_arm_leer(AICA_INT_L, 4), 2);
}

/* ------------------------------------------------------------------------ */
/* El sintetizador                                                          */
/* ------------------------------------------------------------------------ */

/* Deja un canal listo con un formato, un bucle y volumen al maximo. */
static void armar_canal(int canal, DWORD sa, int formato, DWORD lsa, DWORD lea,
                        int bucle)
{
	unsigned long b = (unsigned long) canal * AICA_CANAL_PASO;

	escribir_g2(b + 0x04, sa & 0xFFFF);
	escribir_g2(b + 0x08, lsa);
	escribir_g2(b + 0x0C, lea);
	escribir_g2(b + 0x10, 0x1F);			/* AR maximo: ataque instantaneo */
	escribir_g2(b + 0x14, 0x1F);			/* RR maximo */
	escribir_g2(b + 0x18, 0);				/* OCT 0, FNS 0: a la frecuencia base */
	escribir_g2(b + 0x24, 0x0F00);			/* DISDL 0xF, DIPAN centrado */
	escribir_g2(b + 0x28, 0x0000);			/* TL 0: sin atenuacion */

	escribir_g2(AICA_MVOL, 0x000F);			/* volumen maestro al maximo */

	/* Y el registro 0 al final, que es el que dispara. */
	escribir_g2(b + 0x00,
		0xC000									/* KYONEX + KYONB */
		| (DWORD) (formato << 7)
		| (bucle ? 0x200 : 0)
		| ((sa >> 16) & 0x7F));
}

/* Produce n muestras y devuelve el pico absoluto del canal izquierdo. */
static int producir(unsigned n, short * destino)
{
	short buffer[4096];
	unsigned dadas, i;
	int pico = 0;

	if (!destino)
		destino = buffer;

	/* Se vacia lo que hubiera quedado de un caso anterior. */
	while (aica_salida_leer(buffer, 4096) > 0)
		;

	avanzar_muestras(n);

	dadas = aica_salida_leer(destino, n);

	for (i = 0; i < dadas; i++)
	{
		int v = destino[i * 2];

		if (v < 0)		v = -v;
		if (v > pico)	pico = v;
	}

	return pico;
}

static void el_adpcm_sigue_la_formula_del_papel(void)
{
	/*
		Seccion 8.1.1.2, decodificacion:

		  X(n)   = (1 - 2*L4) * (L3 + L2/2 + L1/4 + 1/8) * D(n) + X(n-1)
		  D(n+1) = f(L3,L2,L1) * D(n)

		con X inicial 0, D inicial 127 y los ocho factores de la tabla 8-4. Los
		tres primeros valores estan calculados a mano desde esas dos lineas:

		  0x7: delta = 127*15/8 = 238        -> X = 238, D = 127*614/256 = 304
		  0x0: delta = 304*1/8  =  38        -> X = 276, D = 304*230/256 = 273
		  0x8: delta = 273*1/8  =  34, resta -> X = 242, D = 273*230/256 = 245

		Que salgan exactos importa mas que en otros sitios: el decodificador es
		de estado, asi que un redondeo distinto no se nota en una muestra sino
		que deriva sobre todo el flujo.
	*/
	short salida[8];

	reiniciar();

	/* Dos nibbles por byte, y **el bajo primero**: 0x7, 0x0, luego 0x8. */
	sound_mem[0x1000] = 0x07;
	sound_mem[0x1001] = 0x08;

	armar_canal(0, 0x1000, AICA_ADPCM, 0, 100, 0);

	producir(3, salida);

	ESPERAR_I32(salida[0], 238);
	ESPERAR_I32(salida[2], 276);
	ESPERAR_I32(salida[4], 242);
}

static void el_bucle_vuelve_a_lsa(void)
{
	/*
		Seccion 8.1.1.1. El papel dibuja LSA y LEA sobre una onda de once
		muestras --D[0] a D[A]-- y da la secuencia con el bucle puesto:

		  D[0] -> D[1] -> ... -> D[A] -> D[5] -> D[6] -> ... -> D[A] -> D[5] ...

		El texto dice ademas "the setting for LSA is 0x3", que no cuadra con esa
		secuencia. La descripcion del registro --"loop starting address"-- si es
		inequivoca, y es la que se sigue: se vuelve a LSA.
	*/
	short salida[64];
	int i;

	reiniciar();

	/* Once muestras de PCM16 con valores reconocibles: D[n] = (n+1) * 256. */
	for (i = 0; i <= 0xA; i++)
	{
		sound_mem[0x2000 + i * 2]     = 0;
		sound_mem[0x2000 + i * 2 + 1] = (unsigned char) (i + 1);
	}

	armar_canal(0, 0x2000, AICA_PCM16, 5, 0xA, 1);

	producir(17, salida);

	for (i = 0; i <= 0xA; i++)
		ESPERAR_I32(salida[i * 2], (i + 1) * 256);

	/* Y detras D[5], D[6], ... */
	for (i = 0; i < 5; i++)
		ESPERAR_I32(salida[(0xB + i) * 2], (5 + i + 1) * 256);
}

static void sin_bucle_el_canal_se_apaga_en_lea(void)
{
	short salida[32];
	int i;

	reiniciar();

	for (i = 0; i <= 0xA; i++)
	{
		sound_mem[0x2000 + i * 2]     = 0;
		sound_mem[0x2000 + i * 2 + 1] = (unsigned char) (i + 1);
	}

	armar_canal(0, 0x2000, AICA_PCM16, 5, 0xA, 0);

	producir(16, salida);

	ESPERAR_I32(salida[0xA * 2], 0xB * 256);
	ESPERAR_I32(salida[0xB * 2], 0);			/* pasado LEA, nada */
	ESPERAR_I32(aica_canales[0].activo, 0);
}

static void el_tono_sale_de_oct_y_fns(void)
{
	/*
		Tabla 8-7: con FNS = 0 y OCT = 0 el intervalo coincide con el de la
		fuente, o sea una muestra leida por muestra producida. OCT +1 va al
		doble y OCT -1 a la mitad, con OCT en complemento a dos.
	*/
	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 0xFFFF, 1);
	producir(100, NULL);
	ESPERAR_U32(aica_canales[0].pos, 100);

	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 0xFFFF, 1);
	escribir_g2(0x18, 1u << 11);				/* OCT = +1 */
	producir(100, NULL);
	ESPERAR_U32(aica_canales[0].pos, 200);

	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 0xFFFF, 1);
	escribir_g2(0x18, 0xFu << 11);				/* OCT = -1 */
	producir(100, NULL);
	ESPERAR_U32(aica_canales[0].pos, 50);
}

static void una_tasa_cero_es_infinito_y_no_instantaneo(void)
{
	/*
		Las tasas 0 y 1 de la tabla 8-5 valen "infinito": la envolvente **no se
		mueve**. Tomarlas por instantaneas apagaba el canal en la primera
		muestra del decaimiento, y el sintoma era un .wav de ocho segundos con
		pico 14 sobre 32767 -- el equivalente auditivo de un BMP negro.
	*/
	short salida[16];
	int i;

	reiniciar();

	for (i = 0; i < 200; i++)
	{
		sound_mem[0x2000 + i * 2]     = 0x00;
		sound_mem[0x2000 + i * 2 + 1] = 0x40;	/* 0x4000 constante */
	}

	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(0x10, 0x1F);					/* AR = 0x1F, D1R = 0, D2R = 0 */

	producir(8, salida);

	ESPERAR_I32(salida[7 * 2] > 0x3000, 1);
	ESPERAR_I32(aica_canales[0].activo, 1);
}

static void las_tres_tablas_de_volumen(void)
{
	/*
		Tablas 8-10, 8-11 y 8-12, medidas por su efecto sobre la salida, que es
		lo unico observable. TL lleva peso por bit --el bit 3 vale -3 dB-- y
		DISDL y MVOL van de 3 dB por escalon con el 0 en silencio.
	*/
	short salida[8];
	int pleno, con_tl, con_disdl;

	reiniciar();
	sound_mem[0x2000] = 0x00;
	sound_mem[0x2001] = 0x40;

	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	pleno = producir(4, salida);

	ESPERAR_I32(pleno > 0x3000, 1);

	/* -3 dB es 0,707 de amplitud; se admite un 5% por el redondeo de la tabla
	   de ganancia. */
	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(0x28, 0x0800);					/* TL = 8 */
	con_tl = producir(4, salida);

	ESPERAR_I32(con_tl * 100 / pleno >= 66 && con_tl * 100 / pleno <= 76, 1);

	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(0x24, 0x0E00);					/* DISDL = 0xE: -3 dB */
	con_disdl = producir(4, salida);

	ESPERAR_I32(con_disdl * 100 / pleno >= 66 && con_disdl * 100 / pleno <= 76, 1);

	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(0x24, 0x0000);					/* DISDL = 0: -MAX dB */
	ESPERAR_I32(producir(4, salida), 0);

	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(AICA_MVOL, 0);					/* MVOL = 0: el otro extremo */
	ESPERAR_I32(producir(4, salida), 0);
}

static void el_paneo_manda_a_un_lado(void)
{
	/*
		Tabla 8-12: 0x00-0x0F atenuan la izquierda y 0x10-0x1F la derecha, y
		0x0F / 0x1F son -MAX dB, o sea silencio de ese lado. Es lo que produce
		calc_aica_pan() del firmware de KOS: paneo 0 da 0x1F y paneo 255 da
		0x0F.
	*/
	short salida[8];

	reiniciar();
	sound_mem[0x2000] = 0x00;
	sound_mem[0x2001] = 0x40;

	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(0x24, 0x0F1F);					/* DIPAN 0x1F */
	producir(4, salida);

	ESPERAR_I32(salida[0] != 0, 1);				/* izquierda suena */
	ESPERAR_I32(salida[1], 0);					/* derecha, callada */

	reiniciar();
	armar_canal(0, 0x2000, AICA_PCM16, 0, 100, 1);
	escribir_g2(0x24, 0x0F0F);					/* DIPAN 0x0F */
	producir(4, salida);

	ESPERAR_I32(salida[0], 0);
	ESPERAR_I32(salida[1] != 0, 1);
}

static void kyonex_dispara_todos_los_canales(void)
{
	/*
		"Writing 1 to this register executes KEY_ON, OFF for all slots." El
		disparo es global y lo que decide canal por canal es KYONB: es asi como
		aica_sync_play() del firmware arranca varias voces a la vez.
	*/
	reiniciar();

	escribir_g2(0 * AICA_CANAL_PASO + 0x00, 0x4000);
	escribir_g2(1 * AICA_CANAL_PASO + 0x00, 0x4000);
	escribir_g2(2 * AICA_CANAL_PASO + 0x00, 0x4000);

	ESPERAR_I32(aica_canales[0].activo, 0);
	ESPERAR_I32(aica_canales[1].activo, 0);
	ESPERAR_I32(aica_canales[2].activo, 0);

	/* KYONEX en **uno solo** arranca los tres. */
	escribir_g2(2 * AICA_CANAL_PASO + 0x00, 0xC000);

	ESPERAR_I32(aica_canales[0].activo, 1);
	ESPERAR_I32(aica_canales[1].activo, 1);
	ESPERAR_I32(aica_canales[2].activo, 1);
}

static void el_monitor_informa_el_canal_elegido(void)
{
	/*
		MSLC elige el canal y detras se leen SGC, EG, LP y CA. aica_get_pos()
		del firmware escribe el byte 0x280D y lee 0x2814, y el planificador de
		flujos de KOS decide con eso cuando rellenar el buffer: sin esto la
		reproduccion se traba o se pisa.
	*/
	reiniciar();

	sound_mem[0x2000] = 0x00;
	sound_mem[0x2001] = 0x40;

	armar_canal(3, 0x2000, AICA_PCM16, 0, 0xFFFF, 1);
	producir(50, NULL);

	aica_arm_escribir(AICA_MIDI_OUT + 1, 1, 3);		/* MSLC = 3 */
	ESPERAR_U32(leer_g2(AICA_MONITOR_CA), 50);

	aica_arm_escribir(AICA_MIDI_OUT + 1, 1, 7);		/* uno que no se movio */
	ESPERAR_U32(leer_g2(AICA_MONITOR_CA), 0);
}

static void el_pcm_de_8_bits_se_extiende_con_signo(void)
{
	short salida[8];

	reiniciar();

	sound_mem[0x2000] = 0x40;					/* +64 -> 0x4000 */
	sound_mem[0x2001] = 0xC0;					/* -64 -> 0xC000 */

	armar_canal(0, 0x2000, AICA_PCM8, 0, 100, 1);
	producir(2, salida);

	ESPERAR_I32(salida[0], 0x4000);
	ESPERAR_I32(salida[2], -0x4000);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(el_respaldo_guarda_16_bits),
	CASO(el_arm_arranca_en_reset),
	CASO(armrst_es_solo_del_sh4),
	CASO(l_y_m_son_solo_del_arm),
	CASO(la_version_se_lee_encima_de_lo_escrito),
	CASO(el_arm_escribe_bytes),
	CASO(los_canales_no_se_pisan),
	CASO(el_nivel_sale_de_los_tres_scilv),
	CASO(el_nivel_de_la_entrada_midi_es_cinco),
	CASO(scire_limpia_y_m_libera),
	CASO(una_fuente_sin_mascara_sigue_pendiente),
	CASO(la_interrupcion_al_sh4_sale_por_el_asic),
	CASO(el_temporizador_cuenta_hacia_arriba),
	CASO(el_prescaler_divide_por_potencias_de_dos),
	CASO(el_temporizador_a_da_100_hz_con_lo_que_pone_kos),
	CASO(el_reloj_de_muestreo_es_de_44100_hz),
	CASO(el_dma_interno_mueve_de_la_onda_a_los_registros),
	CASO(el_dma_interno_con_dgate_pone_ceros),
	CASO(la_secuencia_de_aica_init_de_kos),
	CASO(el_adpcm_sigue_la_formula_del_papel),
	CASO(el_bucle_vuelve_a_lsa),
	CASO(sin_bucle_el_canal_se_apaga_en_lea),
	CASO(el_tono_sale_de_oct_y_fns),
	CASO(una_tasa_cero_es_infinito_y_no_instantaneo),
	CASO(las_tres_tablas_de_volumen),
	CASO(el_paneo_manda_a_un_lado),
	CASO(kyonex_dispara_todos_los_canales),
	CASO(el_monitor_informa_el_canal_elegido),
	CASO(el_pcm_de_8_bits_se_extiende_con_signo),
};

const dc_suite suite_aica = DEFINIR_SUITE("aica", casos);
