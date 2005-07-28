#ifndef _INTC_H_
#define _INTC_H_

bool intc(DWORD irq);
void check_ints();
bool intc_check();
void intc_add(DWORD inttoadd, int cnt);
extern bool maple_dma;

#define EXC_IRQ9                0x0320
#define EXC_IRQA                0x0340
#define EXC_IRQB                0x0360
#define EXC_IRQC                0x0380
#define EXC_IRQD                0x03a0
#define EXC_IRQE                0x03c0
#define EXC_TMU0_TUNI0          0x0400  /* TMU0 underflow */
#define EXC_TMU1_TUNI1          0x0420  /* TMU1 underflow */
#define EXC_TMU2_TUNI2          0x0440  /* TMU2 underflow */

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
#define ASIC_EVT_MAPLE_DMA              (1 << 0x000c)
#define ASIC_EVT_MAPLE_ERROR            (1 << 0x000d)
// #define ASIC_EVT_PVR_PRIMOUTOFMEM       0x0202
// #define ASIC_EVT_PVR_MATOUTOFMEM        0x0203

#endif // _INTC_H
