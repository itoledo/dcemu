#include "main.h"

#define SR_GET_IMASK() ((SR >> 4) & 0xF)

bool inside_int = false;
bool maple_dma = false;
static	long	intdelay = 0;
static	int 	intcnt = 0;
extern	int		pvr_registered;
extern	int		pvr_registering;
extern	int		pvr_listdone;

bool intc(DWORD irq)
{
	BYTE v;

	if (IS_SET(SR, SR_BL) || VBR == 0)
		return false;

	if (inside_int == true)
	{
		logmsg("llamando intc mientras se procesa otra int.\n");
//		return;
	}

/*	logmsg("antes:");
	dump_registers(); */

	// a revisar por int's pendientes
//	logmsg("imask: %x, VBR: %x\r\n", SR_GET_IMASK(), VBR);

	switch(irq)
	{
		case ASIC_IRQ9:		v = 0x9;	break;
		case ASIC_IRQB:		v = 0xb;	break;
		case ASIC_IRQD:		v = 0xd;	break;
		default:			logmsg("ERROR: IRQ NO RECONOCIDA!");		abort();	break;
	}
	
	if (SR_GET_IMASK() > v)
	{
		logmsg("imask > irq, retornando.\n");
		return false;
	}

	switch(irq)
	{
		case ASIC_IRQ9:		*INTEVT = 0x320;	break;
		case ASIC_IRQB:		*INTEVT = 0x360;	break;
		case ASIC_IRQD:		*INTEVT = 0x3a0;	break;
	}

	SSR = SR;
	SPC = PC;
	SGR = R(15);
	SET_BIT(SR, SR_BL);
	SET_BIT(SR, SR_MD);
	SET_BIT(SR, SR_RB);

	PC = VBR + 0x600;
//	str_PC = &memoria[(PC % 0x20000000)]; // - mem_n_base];
	str_PC = get_memory_pointer(PC);

/*	logmsg("intc: saltando a %x, con registros:\n", PC);
	dump_registers(); */
	inside_int = true;
//	filelogging = FILELOG_CALLS;
	return true;
}

void check_ints()
{
	if (PC_func != PC_f_normal)
/* 	||  IS_SET(SR, SR_BL)
	||  VBR == 0) */
		return;

	intdelay++;

	if (intdelay == 50000)
	{
		DWORD dw = 0;

		if (intcnt > 9)
			intcnt = 0;

//		logxmsg(LOG_INTC, "intc: procesando eventos PVR: cnt = %d\n", intcnt);

		switch(intcnt)
		{
			case 0:
			dw = 1 << 3;	// ASIC_EVT_PVR_SCANINT1
			break;

			case 1:
			dw = 1 << 4;	// ASIC_EVT_PVR_SCANINT2
			break;

			case 2:
			if ((pvr_registered & (1 << 0)))
				dw = 1 << 7;	// ASIC_EVT_PVR_OPAQUEDONE
			break;
			
			case 3:
			if ((pvr_registered & (1 << 1)))
				dw = 1 << 8;	// ASIC_EVT_PVR_OPAQUEMODDONE
			break;
			
			case 4:
			if ((pvr_registered & (1 << 2)))
				dw = 1 << 9;	// ASIC_EVT_PVR_TRANSDONE
			break;
			
			case 5:
			if ((pvr_registered & (1 << 3)))
				dw = 1 << 10;	// ASIC_EVT_PVR_TRANSMODDONE
			break;

			case 6:
			if ((pvr_registered & (1 << 4)))
				dw = 1 << 21;	// ASIC_EVT_PVR_PTDONE
			break;

			case 7:
//			if (pvr_registering == -1)
			if (pvr_registered == pvr_listdone) // todas las listas hechas
				dw = 1 << 2;	// ASIC_EVT_PVR_RENDERDONE
			break;

			case 8:
			dw = 1 << 5;	// ASIC_EVT_PVR_VBLINT
			break;

			case 9:
			if (maple_dma)
			{
				dw = 1 << 12; // maple dma complete
				maple_dma = false;
			}
			break;
		}
		
		intcnt++;
		intdelay = 0;

		if (dw == 0)
			return;

		logxmsg(LOG_INTC, "seteando bit %x\n", dw);
		SET_BIT(ASIC_ACK_A, dw);
		if (ASIC_IRQ9_A & dw)
		{
			logxmsg(LOG_INTC, "intc: asic_irq9, %d\n", intcnt);
			if (intc(ASIC_IRQ9))
			{
				return;
			}
		}
		if (ASIC_IRQD_A & dw)
		{
			logxmsg(LOG_INTC, "intc: asic_irqd, %d\n", intcnt);
			if (intc(ASIC_IRQD))
			{
				return;
			}
		}				
		if (ASIC_IRQB_A & dw)
		{
			logxmsg(LOG_INTC, "intc: asic_irqb, %d\n", intcnt);
			if (intc(ASIC_IRQB))
			{
				return;
			}
		}
		return;
	}
/*	if (maple_dma && intdelay == 400000)
	{
		DWORD dw = 1 << 15; // maple dma complete
		SET_BIT(ASIC_ACK_A, dw);
		logmsg("intc: chequeando si corresponde llamar a maple dma...\n");
		intdelay = 0;
		if (ASIC_IRQ9_A & dw)
		{
			logmsg("intc: asic_irq9, maple dma\n");
			intc(ASIC_IRQ9);
			maple_dma = false;
			return;
		}
		if (ASIC_IRQD_A & dw)
		{
			logmsg("intc: asic_irqd, maple dma\n");
			intc(ASIC_IRQD);
			maple_dma = false;
			return;
		}				
		if (ASIC_IRQB_A & dw)
		{
			logmsg("intc: asic_irqb, maple dma\n");
			intc(ASIC_IRQB);
			maple_dma = false;
			return;
		}
		return;
	} */
}

