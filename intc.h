#ifndef _INTC_H_
#define _INTC_H_

bool intc(DWORD irq);

/* La entrada generica a una excepcion --  excepcion_entrar() -- se mudo a
   excepciones.h, que es donde la comparten la MMU y la FPU. intc() es el caso
   particular de las interrupciones y la sigue usando. */

/*
	Mira las banderas de los perifericos del propio SH-4 -- los tres TMU y el
	WDT -- y entrega la interrupcion que corresponda si SR lo permite.

	Antes cada periferico llamaba a intc() en el momento del subdesborde, y si
	SR.BL estaba puesto o IMASK era demasiado alto, intc() devolvia false y
	**el evento se perdia**. En el chip la peticion queda asertada hasta que el
	software limpia la bandera del periferico, asi que no se pierde nunca.

	Modelarlo por bandera en vez de por evento no necesita estado nuevo: UNF de
	cada TCR e IOVF de WTCSR ya son la fuente de verdad, y los limpia el
	manejador del guest. Ver docs/clock-plan.md.
*/
void intc_revisar_sh4(void);

void check_ints();
bool intc_check();
void intc_add(DWORD inttoadd, int cnt);

/* Si main_loop tiene que llamar a check_ints(): demoras corriendo, cola
   externa, o una linea del ASIC afirmada (SB_ISTNRM contra las mascaras --
   la peticion es por nivel, asi que es estado y no cola). */
bool intc_asic_pendiente(void);

/* La ponen las dos entradas de escritura de SR; la consume main_loop().
   Ver el comentario en intc.c. */
extern int intc_sh4_reintentar;

/* El ASIC tiene dos registros de estado: el normal (SB_ISTNRM, ASIC_ACK_A) y
   el externo (SB_ISTEXT, ASIC_ACK_B). El fin de comando de la lectora llega
   por el externo, asi que hace falta una cola aparte. */
void intc_add_ext(DWORD inttoadd);
void intc_remove_ext(DWORD int2remove);

extern bool maple_dma;
extern DWORD	intc_queuemask;
extern DWORD	intc_queuemask_ext;

#define EXC_IRQ9                0x0320
#define EXC_IRQA                0x0340
#define EXC_IRQB                0x0360
#define EXC_IRQC                0x0380
#define EXC_IRQD                0x03a0
#define EXC_IRQE                0x03c0
#define EXC_WDT_ITI             0x0560  /* WDT, modo temporizador de intervalo */
#define EXC_TMU0_TUNI0          0x0400  /* TMU0 underflow */
#define EXC_TMU1_TUNI1          0x0420  /* TMU1 underflow */
#define EXC_TMU2_TUNI2          0x0440  /* TMU2 underflow */
#define EXC_DMTE0               0x0640  /* DMAC, fin de transferencia canal 0 */
#define EXC_DMTE1               0x0660  /* DMAC, fin de transferencia canal 1 */
#define EXC_DMTE2               0x0680  /* DMAC, fin de transferencia canal 2 */
#define EXC_DMTE3               0x06a0  /* DMAC, fin de transferencia canal 3 */

#define ASIC_EVT_PVR_RENDERDONE         (1 << 0x0002)
#define ASIC_EVT_PVR_SCANINT1           (1 << 0x0003)
#define ASIC_EVT_PVR_SCANINT2           (1 << 0x0004)
#define ASIC_EVT_PVR_VBLINT             (1 << 0x0005)
#define ASIC_EVT_PVR_OPAQUEDONE         (1 << 0x0007)
#define ASIC_EVT_PVR_OPAQUEMODDONE      (1 << 0x0008)
#define ASIC_EVT_PVR_TRANSDONE          (1 << 0x0009)
#define ASIC_EVT_PVR_TRANSMODDONE       (1 << 0x000a)
#define ASIC_EVT_PVR_DMA                (1 << 0x0013)
#define ASIC_EVT_PVR_PTDONE             (1 << 0x0015)
/* Fin del Sort-DMA (DTDESINT): la via de subida al TA por cadenas enlazadas
   en RAM. Ver sort_dma_ejecutar() en mem.c. */
#define ASIC_EVT_PVR_SORTDMA            (1 << 0x0014)
#define ASIC_EVT_MAPLE_DMA              (1 << 0x000c)
#define ASIC_EVT_MAPLE_ERROR            (1 << 0x000d)
#define ASIC_EVT_GDROM_DMA              (1 << 0x000e)

/* El fin de transferencia de los cuatro canales del G2-DMA. El del canal 0 es
   el del AICA, y es el que espera g2_dma_transfer() de KOS. asic.h de KOS les
   da 0x000F a 0x0012: registro 0 --SB_ISTNRM-- y bits 15 a 18. */
#define ASIC_EVT_G2_DMA0                (1 << 0x000f)
#define ASIC_EVT_G2_DMA1                (1 << 0x0010)
#define ASIC_EVT_G2_DMA2                (1 << 0x0011)
#define ASIC_EVT_G2_DMA3                (1 << 0x0012)

/* Bits del registro externo (SB_ISTEXT). */
#define ASIC_EVT_EXT_GDROM              (1 << 0x0000)   /* fin de comando */
#define ASIC_EVT_EXT_AICA               (1 << 0x0001)   /* G2AICINT, del AICA */
// #define ASIC_EVT_PVR_PRIMOUTOFMEM       0x0202
// #define ASIC_EVT_PVR_MATOUTOFMEM        0x0203

#endif // _INTC_H
