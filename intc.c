#include "main.h"
#include "excepciones.h"
#include "intc.h"
#include "wdt.h"

//  #define INT_QUEUE

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
DWORD	intc_queuemask_ext = 0;

/*
	Las peticiones de los perifericos del SH-4 se derivan de sus banderas, no de
	un evento puntual: asi una interrupcion que no se puede entregar ahora sigue
	pendiente hasta que se pueda, que es lo que hace el chip. Ver intc.h.
*/
void intc_revisar_sh4(void)
{
	/* Si no se puede entregar ninguna, ni vale mirar las banderas. */
	if (IS_SH4_REG_SET(SR_BL) || VBR == 0)
		return;

	/* Una por vez: entrar a una deja BL puesto, asi que el resto espera. El
	   orden entre ellas lo decide intc() por IPR; aca solo se ofrecen. */
	if ((*TCR0 & TMU_TCR_UNF) && (*TCR0 & TMU_TCR_UNIE) && intc(EXC_TMU0_TUNI0))
		return;

	if ((*TCR1 & TMU_TCR_UNF) && (*TCR1 & TMU_TCR_UNIE) && intc(EXC_TMU1_TUNI1))
		return;

	if ((*TCR2 & TMU_TCR_UNF) && (*TCR2 & TMU_TCR_UNIE) && intc(EXC_TMU2_TUNI2))
		return;

	/* El WDT solo interrumpe en modo temporizador de intervalo; en modo
	   watchdog el desborde reinicia la maquina y marca WOVF, no IOVF. */
	if ((wdt_control() & WTCSR_IOVF) && !(wdt_control() & WTCSR_WTIT)
		&& intc(EXC_WDT_ITI))
		return;
}

bool intc(DWORD irq)
{
	BYTE v;

#ifdef DEBUG_INTC
	logxmsg(LOG_INTC, "intc: irq %04x\n", irq);
#endif

	if (IS_SH4_REG_SET(SR_BL) || VBR == 0)
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
/* 2 */	case EXC_IRQD:			v = 0x1;	break; // m�s de 0001, no corre
/* 4 */	case EXC_IRQB:			v = 0x3;	break; // m�s de 0011, no corre
/* 6 */	case EXC_IRQ9:			v = 0x5;	break; // m�s de 0101, no corre
		/* La prioridad del WDT vive en IPRB, bits 15-12. */
		case EXC_WDT_ITI:		v = ((*IPRB) >> 12) & 0xf;	break;
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
	SET_SH4_BIT(SR_BL);
	SET_SH4_BIT(SR_MD);
	SET_SH4_BIT(SR_RB);
	UpdateSR(SH4_SYSTEM_REGISTER_INTC_REWRITTEN);


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

#ifdef INT_QUEUE
typedef struct pending_ints_str PENDING_INT;

struct pending_ints_str
{
    PENDING_INT * next;
    DWORD pending_int;
    int cnt;
};

PENDING_INT * last_int;
PENDING_INT * int_list;
int interrupt_queue = 0;
#endif

void intc_add(DWORD inttoadd, int cnt)
{
#ifdef INT_QUEUE
	PENDING_INT * tmp;

	if (intc_queuemask & inttoadd)
	{
     	logxmsg(LOG_INTC, "descartando int %x\n", inttoadd);
		return; // descartamos si ya existe una
	}

	interrupt_queue++;

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
	logxmsg(LOG_INTC, "a�adiendo int %x, total %x, count %d\n", inttoadd, intc_queuemask, interrupt_queue);
#else
	if (intc_queuemask & inttoadd)
	{
     	logxmsg(LOG_INTC, "descartando int %x\n", inttoadd);
		return; // descartamos si ya existe una
	}
	SET_BIT(ASIC_ACK_A, inttoadd);
	intc_queuemask |= inttoadd;
	logxmsg(LOG_INTC, "a�adiendo int %x, total %x\n", inttoadd, intc_queuemask);
#endif
}

/* Cola del registro externo (SB_ISTEXT). Es la misma mecanica que intc_add()
   pero contra ASIC_ACK_B y las mascaras _B. */
void intc_add_ext(DWORD inttoadd)
{
	if (intc_queuemask_ext & inttoadd)
	{
		logxmsg(LOG_INTC, "descartando int externa %x\n", inttoadd);
		return;
	}

	SET_BIT(ASIC_ACK_B, inttoadd);
	intc_queuemask_ext |= inttoadd;

	logxmsg(LOG_INTC, "anadiendo int externa %x, total %x\n", inttoadd, intc_queuemask_ext);
}

void intc_remove_ext(DWORD int2remove)
{
	REMOVE_BIT(ASIC_ACK_B, int2remove);
	REMOVE_BIT(intc_queuemask_ext, int2remove);
}

#ifdef INT_QUEUE
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
		interrupt_queue--;
	}
	else
    for (tmp = int_list; tmp; tmp = tmp_next)
    {
        tmp_next = tmp->next;
        if (tmp->next == int2del)
        {
			interrupt_queue--;
            tmp->next = int2del->next;
            if (last_int == int2del)
            	last_int = tmp;
            free(int2del);
            return;
		}
	}
}
#endif

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
#ifdef INT_QUEUE
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
#else
// 	if (intc_queuemask == 0)
//  		return;

 	if (intc_queuemask & ASIC_IRQ9_A
 	&&  intc(EXC_IRQ9))
 	{
		REMOVE_BIT(intc_queuemask, ASIC_IRQ9_A);
      	return;
	}

 	if (intc_queuemask & ASIC_IRQB_A
 	&&  intc(EXC_IRQB))
 	{
		REMOVE_BIT(intc_queuemask, ASIC_IRQB_A);
      	return;
	}

 	if (intc_queuemask & ASIC_IRQD_A
 	&&  intc(EXC_IRQD))
 	{
		REMOVE_BIT(intc_queuemask, ASIC_IRQD_A);
      	return;
	}

	/*
		Aca estaba REMOVE_BIT(intc_queuemask, ASIC_ACK_A), o sea: si en este
		instante ninguna de las tres mascaras cubria el evento, se tiraba.

		En el chip no funciona asi. El bit de SB_ISTNRM queda puesto hasta que
		el guest lo acusa escribiendo el registro, y si habilita la mascara
		despues la interrupcion llega igual. Es el mismo error que tenian los
		temporizadores antes de intc_revisar_sh4() -- ver docs/clock-plan.md --,
		solo que del lado del ASIC, y lo que se pierde asi no deja rastro.

		Lo que si lo delata: el boot ROM habilita el DMA del Maple, lo arranca y
		espera el fin por interrupcion. La habilita despues de encolarla, asi
		que la perdia siempre y no volvia a sondear el bus nunca mas -- por eso
		llegaba a su pantalla pero no veia el mando.

		Ahora el bit se queda pendiente y lo limpia el guest, en el caso
		SB_ISTNRM de pvr_write().
	*/

	/* Registro externo. La lectora avisa el fin de comando por aca. */
	if (intc_queuemask_ext == 0)
		return;

 	if (intc_queuemask_ext & ASIC_IRQ9_B
 	&&  intc(EXC_IRQ9))
 	{
		REMOVE_BIT(intc_queuemask_ext, ASIC_IRQ9_B);
      	return;
	}

 	if (intc_queuemask_ext & ASIC_IRQB_B
 	&&  intc(EXC_IRQB))
 	{
		REMOVE_BIT(intc_queuemask_ext, ASIC_IRQB_B);
      	return;
	}

 	if (intc_queuemask_ext & ASIC_IRQD_B
 	&&  intc(EXC_IRQD))
 	{
		REMOVE_BIT(intc_queuemask_ext, ASIC_IRQD_B);
      	return;
	}

	REMOVE_BIT(intc_queuemask_ext, ASIC_ACK_B);
#endif
}

