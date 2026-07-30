#include "main.h"

// ASIC
DWORD	ASIC_IRQD_A		= 0x0;	// 0xa05f6910
DWORD	ASIC_IRQD_B		= 0x0;	// 0xa05f6914
DWORD	ASIC_IRQD_C		= 0x0;	// 0xa05f6918
DWORD	ASIC_IRQB_A		= 0x0;	// 0xa05f6920
DWORD	ASIC_IRQB_B		= 0x0;	// 0xa05f6924
DWORD	ASIC_IRQB_C		= 0x0;	// 0xa05f6928
DWORD	ASIC_IRQ9_A		= 0x0;	// 0xa05f6930
DWORD	ASIC_IRQ9_B		= 0x0;	// 0xa05f6934
DWORD	ASIC_IRQ9_C		= 0x0;	// 0xa05f6938

DWORD	ASIC_ACK_A		= 0x0;	// 0xa05f6900
DWORD	ASIC_ACK_B		= 0x0;	// 0xa05f6904
DWORD	ASIC_ACK_C		= 0x0;	// 0xa05f6908

DWORD	SB_PDTNRM		= 0x0;	//	(0x005F 6940)	PVR-DMA trigger select from normal interrupt
DWORD	SB_PDTEXT		= 0x0;	//	(0x005F 6944)	PVR-DMA trigger select from external interrupt

// sync pulse generator
DWORD	pvr_spg_vblank_int = 0x00280208;	// VGA 640x480
DWORD	pvr_spg_load_vcount = 0x20C;		// VGA 640x480

/* Ciclos de CPU por linea de barrido. Se recalcula al escribir SPG_LOAD o
   SPG_CONTROL; el valor inicial corresponde al modo VGA de arriba. Antes
   main_loop() usaba 978 fijo, 6,5 veces mas rapido de lo que toca.
   Ver docs/clock-plan.md, fase 3. */
DWORD	pvr_ciclos_linea = 6345;			// DC_CPU_HZ / (0x20C * 60)

DWORD	pvr_spg_control = 0x100;
DWORD	pvr_spg_hblank = 0x007E0345;
DWORD	pvr_spg_load = 0x020C0359;
DWORD	pvr_spg_vblank = 0x00280208;
DWORD	pvr_spg_width = 0x03F1933F;
