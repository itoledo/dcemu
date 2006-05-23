// PVR
extern DWORD PVR_SCANINTPOS;	// SCANINTPOS, 0xa05f80cc

// ASIC
#define ASIC_IRQ9	1
#define ASIC_IRQB	2
#define ASIC_IRQD	3

extern	DWORD	SB_PDTNRM;	//	(0x005F 6940)	PVR-DMA trigger select from normal interrupt
extern	DWORD	SB_PDTEXT;	//	(0x005F 6944)		PVR-DMA trigger select from external interrupt

extern	DWORD ASIC_IRQD_A;	// 0xa05f6910	SB_IML2NRM	Level-2 normal interrupt mask control
extern	DWORD ASIC_IRQD_B;	// 0xa05f6914	SB_IML2EXT	Level-2 external interrupt mask control
extern	DWORD ASIC_IRQD_C;	// 0xa05f6918
extern	DWORD ASIC_IRQB_A;	// 0xa05f6920	SB_IML4NRM	Level-4 normal interrupt mask control
extern	DWORD ASIC_IRQB_B;	// 0xa05f6924	SB_IML4EXT	Level-4 external interrupt mask control
extern	DWORD ASIC_IRQB_C;	// 0xa05f6928
extern	DWORD ASIC_IRQ9_A;	// 0xa05f6930	SB_IML6NRM	Level-6 normal interrupt mask control
extern	DWORD ASIC_IRQ9_B;	// 0xa05f6934	SB_IML6EXT	Level-6 external interrupt mask control
extern	DWORD ASIC_IRQ9_C;	// 0xa05f6938

extern	DWORD ASIC_ACK_A;	// 0xa05f6900	SB_ISTNRM	normal interrupt status
extern	DWORD ASIC_ACK_B;	// 0xa05f6904	SB_ISTEXT	external interrupt status
extern	DWORD ASIC_ACK_C;	// 0xa05f6908	SB_ISTERR	error interrupt status

extern	DWORD	pvr_spg_control;
extern	DWORD	pvr_spg_hblank;
extern	DWORD	pvr_spg_load;
extern	DWORD	pvr_spg_vblank;
extern	DWORD	pvr_spg_width;
