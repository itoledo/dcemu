#include "main.h"
#include "intc.h"

#define SR_GET_IMASK() ((SR >> 4) & 0xF)

bool inside_int = false;
bool maple_dma = false;
static	long	intdelay = 0;
static	int 	intcnt = 0;
extern	int		pvr_registered;
extern	int		pvr_registering;
extern	int		pvr_listdone;
static	DWORD	pending_ints = 0;
DWORD	intc_queuemask = 0;

#undef DEBUG_INTC

bool intc(DWORD irq)
{
	BYTE v;

	if (IS_SET(SR, SR_BL) || VBR == 0)
	{
#ifdef DEBUG_INTC
 		logxmsg(LOG_INTC, "intc: BL seteado, retornando.\n");
#endif
		return false;
	}

/*	logmsg("antes:");
	dump_registers(); */

	// a revisar por int's pendientes
//	logmsg("imask: %x, VBR: %x\r\n", SR_GET_IMASK(), VBR);

	switch(irq)
	{
/* 2 */	case EXC_IRQD:			v = 0x1;	break; // más de 0001, no corre
/* 4 */	case EXC_IRQB:			v = 0x3;	break; // más de 0011, no corre
/* 6 */	case EXC_IRQ9:			v = 0x5;	break; // más de 0101, no corre
		case EXC_TMU0_TUNI0:	v = ((*IPRA) >> 12) & 0xf;	break;
		case EXC_TMU1_TUNI1:	v = ((*IPRA) >>  8) & 0xf;	break;
		case EXC_TMU2_TUNI2:	v = ((*IPRA) >>  4) & 0xf;	break;
		default:			logxmsg(LOG_INTC, "intc: irq %d, v=1\n", irq);	v = 0x1;	break;
	}
	
	if (v == 0)
	{
#ifdef DEBUG_INTC
	    logxmsg(LOG_INTC, "v = 0 para irq %x, retornando.\n", irq);
#endif
 		return false;	
	}
 
	if (SR_GET_IMASK() == 0xf)
	{
#ifdef DEBUG_INTC
 		logxmsg(LOG_INTC, "imask = 0xf, retornando.\n");
#endif
 		return false;
	}

	if (SR_GET_IMASK() > v)
	{
#ifdef DEBUG_INTC
		logxmsg(LOG_INTC, "imask > irq, retornando.\n");
#endif
		return false;
	}

	if (inside_int == true)
	{
#ifdef DEBUG_INTC
		logxmsg(LOG_INTC, "llamando intc mientras se procesa otra int.\n");
//		return;
#endif
	}

/*	switch(irq)
	{
		case ASIC_IRQ9:		*INTEVT = 0x320;	break;
		case ASIC_IRQB:		*INTEVT = 0x360;	break;
		case ASIC_IRQD:		*INTEVT = 0x3a0;	break;
	} */
	
	*INTEVT = irq;

	SSR = SR;
	SPC = PC;
	SGR = R(15);
	UpdateSR(SR | SR_BL | SR_MD | SR_RB);
/*	SET_BIT(SR, SR_BL);
	SET_BIT(SR, SR_MD);
	SET_BIT(SR, SR_RB); */

	PC = VBR + 0x600;
//	str_PC = &memoria[(PC % 0x20000000)]; // - mem_n_base];

//	str_PC = get_memory_pointer(PC);

#ifdef DEBUG_INTC
	logxmsg(LOG_INTC, "intc: saltando a %x\n", PC);
#endif
/*	logmsg("intc: saltando a %x, con registros:\n", PC);
	dump_registers(); */
	inside_int = true;
//	filelogging = FILELOG_CALLS;
	return true;
}

typedef struct pending_ints_str PENDING_INT;

struct pending_ints_str
{
    PENDING_INT * next;
    DWORD pending_int;
    int cnt;
};

PENDING_INT * last_int;
PENDING_INT * int_list;

void intc_add(DWORD inttoadd, int cnt)
{
	PENDING_INT * tmp;

	if (intc_queuemask & inttoadd)
	{
     	logxmsg(LOG_INTC, "descartando int %x\n", inttoadd);
		return; // descartamos si ya existe una
	}

	tmp = malloc(sizeof(PENDING_INT));
	tmp->pending_int = inttoadd;
	tmp->cnt = cnt;
	tmp->next = NULL;
	if (last_int != NULL)
		last_int->next = tmp;
	last_int = tmp;
	if (int_list == NULL)
		int_list = tmp;
/*	pending_ints |= inttoadd; */
	SET_BIT(ASIC_ACK_A, inttoadd);
	intc_queuemask |= inttoadd;
	logxmsg(LOG_INTC, "añadiendo int %x, total %x\n", inttoadd, intc_queuemask);
}

void intc_delete(PENDING_INT * int2del)
{
    PENDING_INT * tmp, * tmp_next;
	logxmsg(LOG_INTC, "borrando int %x\n", int2del->pending_int);
    intc_queuemask &= ~int2del->pending_int;
	if (int2del == int_list)
	{
		int_list = int_list->next;
		last_int = int_list;
		free(int2del);
	}
    for (tmp = int_list; tmp; tmp = tmp_next)
    {
        tmp_next = tmp->next;
        if (tmp->next == int2del)
        {
            tmp->next = int2del->next;
            if (last_int == int2del)
            	last_int = tmp;
            free(int2del);
            return;
		}
	}
}

bool intc_check(DWORD intcheck)
{
/*    if (PC_func != PC_f_normal)
    	return false; */

    SET_BIT(ASIC_ACK_A, intcheck);

	if (ASIC_IRQ9_A & intcheck
	&&	intc(EXC_IRQ9))
	    return true;
 
	if (ASIC_IRQB_A & intcheck
	&&  intc(EXC_IRQB))
	    return true;
 
	if (ASIC_IRQD_A & intcheck
	&&  intc(EXC_IRQD))
	    return true;
	    
	return false;
}

void check_ints()
{
//    char * s;
	PENDING_INT * pint, * pint_next;
    
//	if (PC_func != PC_f_normal)
/* 	||  IS_SET(SR, SR_BL)
	||  VBR == 0) */
//		return;

/*	if (pending_ints == 0)
		return; */
		
	if (int_list == NULL)
 		return;

	for (pint = int_list; pint; pint = pint_next)
	{
     	pint_next = pint->next;
/*		if (--pint->cnt > 0)
			continue; */
     	if (pint->pending_int & ASIC_IRQ9_A
     	&&  intc(EXC_IRQ9))
     	{
          	intc_delete(pint);
          	return;
		}
     	if (pint->pending_int & ASIC_IRQB_A
     	&&  intc(EXC_IRQB))
     	{
          	intc_delete(pint);
          	return;
		}
     	if (pint->pending_int & ASIC_IRQD_A
     	&&  intc(EXC_IRQD))
     	{
          	intc_delete(pint);
          	return;
		}
		intc_delete(pint);
	}
/*	if (pending_ints & ASIC_IRQ9_A
	&&  intc(EXC_IRQ9))
	{
		pending_ints = 0;
		return;
	}
		
	if (pending_ints & ASIC_IRQB_A
	&&  intc(EXC_IRQB))
	{
		pending_ints = 0;
		return;
	}

	if (pending_ints & ASIC_IRQD_A
	&&  intc(EXC_IRQD))
	{
		pending_ints = 0;
		return;
	} */

/*	intdelay++;

	if (intdelay == 50000)
	{
		DWORD dw = 0;
		s = NULL;

		if (intcnt > 6)
			intcnt = 0;

//		logxmsg(LOG_INTC, "intc: procesando eventos PVR: cnt = %d\n", intcnt);

		switch(intcnt)
		{
			case 0:
			if ((pvr_registered & (1 << 0)))
			{
				dw = 1 << 7;	// ASIC_EVT_PVR_OPAQUEDONE
				s = "ASIC_EVT_PVR_OPAQUEDONE\n";
			}				
			break;
			
			case 1:
			if ((pvr_registered & (1 << 1)))
			{			
				dw = 1 << 8;	// ASIC_EVT_PVR_OPAQUEMODDONE
				s = "ASIC_EVT_PVR_OPAQUEMODDONE\n";
			}				
			break;
			
			case 2:
			if ((pvr_registered & (1 << 2)))
			{			
				dw = 1 << 9;	// ASIC_EVT_PVR_TRANSDONE
				s = "ASIC_EVT_PVR_TRANSDONE\n";
			}				
			break;
			
			case 3:
			if ((pvr_registered & (1 << 3)))
			{			
				dw = 1 << 10;	// ASIC_EVT_PVR_TRANSMODDONE
				s = "ASIC_EVT_PVR_TRANSMODDONE\n";
			}				
			break;

			case 4:
			if ((pvr_registered & (1 << 4)))
			{
				dw = 1 << 21;	// ASIC_EVT_PVR_PTDONE
				s = "ASIC_EVT_PVR_PTDONE\n";
			}				
			break;

			case 5:
//			if (pvr_registering == -1)
			if (pvr_registered == pvr_listdone) // todas las listas hechas
			{
				dw = 1 << 2;	// ASIC_EVT_PVR_RENDERDONE
				s = "ASIC_EVT_PVR_RENDERDONE\n";
			}			
			break;

			case 6:
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
//			if (intc(ASIC_IRQ9))
			if (intc(EXC_IRQ9))
			{
			    if (s)
			    	logxmsg(LOG_PVR, "intc %s ejecutada\n", s);
				return;
			}
		}
		if (ASIC_IRQD_A & dw)
		{
			logxmsg(LOG_INTC, "intc: asic_irqd, %d\n", intcnt);
//			if (intc(ASIC_IRQD))
			if (intc(EXC_IRQD))
			{
			    if (s)
			    	logxmsg(LOG_PVR, "intc %s ejecutada\n", s);
				return;
			}
		}
		if (ASIC_IRQB_A & dw)
		{
			logxmsg(LOG_INTC, "intc: asic_irqb, %d\n", intcnt);
//			if (intc(ASIC_IRQB))
			if (intc(EXC_IRQB))
			{
			    if (s)
			    	logxmsg(LOG_PVR, "intc %s ejecutada\n", s);
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

