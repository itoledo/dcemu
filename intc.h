bool intc(DWORD irq);
void check_ints();
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

