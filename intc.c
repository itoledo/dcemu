#include "main.h"
#include "excepciones.h"
#include "intc.h"
#include "traza.h"
#include "wdt.h"
#include "tmu.h"			/* reloj_total: el vencimiento de las demoras */

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
	Eventos del ASIC con tiempo de transferencia pendiente. En el chip el bit
	de SB_ISTNRM se enciende cuando el evento OCURRE -- el TA termino de
	consumir la lista --, no cuando el guest manda el marcador: dcemu lo
	encendia al instante, y el fin de lista se le adelantaba al juego que
	arma su espera justo despues de mandar la lista (Virtua Tennis marca su
	"pendiente" y la interrupcion ya habia pasado: el bit que su ISR debia
	limpiar quedaba puesto para siempre). El `cnt` de intc_add() siempre
	quiso ser esta demora; vale cnt*50 ciclos, y hasta vencer no existen ni
	el bit de estado ni la entrega.
*/
/*
	**El vencimiento es absoluto contra reloj_total, no una cuenta regresiva.**

	Estaba como cuenta regresiva que restaba 50 por vuelta, dando por sentado
	que check_ints() corre cada 50 ciclos. En cuanto el bloque periodico de
	main_loop() cambio de compas -- RELOJ_GRANO, ver tmu.h -- eso dejo de ser
	cierto y las demoras pasaron a durar ocho veces mas en tiempo emulado: con
	grano 400 Crazy Taxi no pasaba de la advertencia de VMU, y la rama sola si
	pasaba. Un tic acoplado a la frecuencia con que alguien lo mira es
	exactamente lo que docs/clock-plan.md saco de todos los demas relojes de
	este arbol; faltaba este.

	Con una marca absoluta da igual cada cuanto se mire: el evento ocurre en el
	ciclo emulado que le toca.
*/
#define INTC_DEMORAS 16
static struct { DWORD evt; unsigned long long vence; } intc_demora[INTC_DEMORAS];
static DWORD intc_demorados = 0;

/*
	Las peticiones de los perifericos del SH-4 se derivan de sus banderas, no de
	un evento puntual: asi una interrupcion que no se puede entregar ahora sigue
	pendiente hasta que se pueda, que es lo que hace el chip. Ver intc.h.
*/
/*
	**Armado cuando el guest escribe SR.** La entrega de una interrupcion al
	SH-4 no depende solo de que la fuente pida: depende de que SR lo permita, y
	un guest que pasa la mayor parte del tiempo con BL puesto -- Windows CE, con
	miles de excepciones por segundo -- solo la acepta en ventanas cortas. Mirar
	cada RELOJ_GRANO ciclos hace que esas ventanas se pierdan de forma
	sistematica, y eso es lo que dejaba a DCDoom en su pantalla de titulo: no es
	latencia --400 ciclos son 2 us-- sino **cuantas veces se intenta**.

	Asi que ademas del compas periodico se intenta justo cuando la ventana se
	abre: UpdateSR() y UpdateSR_ya_escrito() ponen esta bandera, y main_loop()
	la mira con una comparacion por instruccion. Escribir SR es raro -- RTE,
	LDC ...,SR y la entrada a una excepcion --, asi que no cuesta nada.
*/
int intc_sh4_reintentar = 0;

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

	/* El DMAC: fin de transferencia. dma_canal() deja TE puesto y, con IE, la
	   peticion queda asertada hasta que el guest limpia TE en el CHCR. Es lo
	   que espera el driver de KOS: arma el canal, duerme, y el callback llega
	   por DMTE (basic-dma-speedtest se colgaba justamente aqui). */
	if (*DMAOR & DME)
	{
		if ((*CHCR0 & CHCR_TE) && (*CHCR0 & CHCR_IE) && intc(EXC_DMTE0))
			return;

		if ((*CHCR1 & CHCR_TE) && (*CHCR1 & CHCR_IE) && intc(EXC_DMTE1))
			return;

		if ((*CHCR2 & CHCR_TE) && (*CHCR2 & CHCR_IE) && intc(EXC_DMTE2))
			return;

		if ((*CHCR3 & CHCR_TE) && (*CHCR3 & CHCR_IE) && intc(EXC_DMTE3))
			return;
	}
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
		/* Los cuatro canales del DMAC comparten prioridad: IPRC, bits 11-8. */
		case EXC_DMTE0:
		case EXC_DMTE1:
		case EXC_DMTE2:
		case EXC_DMTE3:			v = ((*IPRC) >>  8) & 0xf;	break;
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

	/*
		Censo de interrupciones entregadas por segundo emulado, por INTEVT. La
		pregunta que contesta es a que ritmo late la logica del guest: un juego
		que mueve su menu sobre un temporizador repite tan rapido como ese
		temporizador le pida, y el histograma de DCEMU_TRAZA_EXC no lo ve
		porque las interrupciones no pasan por excepcion_entrar().
	*/
	if (traza_activa)
	{
		static unsigned long      codigo[16];
		static unsigned long      cuenta[16];
		static int                usados = 0;
		static unsigned long long marca  = 0;

		unsigned long long ahora = reloj_ms();
		int i;

		for (i = 0; i < usados; i++)
			if (codigo[i] == (unsigned long) irq)
				break;

		if (i == usados && usados < 16)
		{
			codigo[usados] = (unsigned long) irq;
			cuenta[usados] = 0;
			usados++;
		}

		if (i < 16)
			cuenta[i]++;

		if (ahora - marca >= 1000)
		{
			fprintf(stderr, "traza: interrupciones en %lu ms:",
				(unsigned long) (ahora - marca));

			for (i = 0; i < usados; i++)
			{
				fprintf(stderr, " %03lx=%lu", codigo[i], cuenta[i]);
				cuenta[i] = 0;
			}

			fprintf(stderr, "\n");
			marca = ahora;
		}
	}

	SSR = SR;
	SPC = PC;
	SGR = R(15);
	SET_SH4_BIT(SR_BL);
	SET_SH4_BIT(SR_MD);
	SET_SH4_BIT(SR_RB);
	UpdateSR_ya_escrito();


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

/* La compuerta de main_loop: hay que llamar a check_ints() si hay demoras
   corriendo, algo en la cola externa, o una linea del ASIC afirmada -- que
   con peticion por nivel es estado, no cola: SB_ISTNRM contra las mascaras. */
bool intc_asic_pendiente(void)
{
	return intc_demorados != 0
	    || intc_queuemask_ext != 0
	    || (ASIC_ACK_A & (ASIC_IRQ9_A | ASIC_IRQB_A | ASIC_IRQD_A)) != 0;
}

static void censo_evento(DWORD inttoadd);

void intc_add(DWORD inttoadd, int cnt)
{
	censo_evento(inttoadd);

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
	if (cnt > 0)
	{
		int di;

		/* El mismo evento ya demorado no se duplica: el chip tampoco genera
		   dos fines para la misma transferencia. Uno ya ocurrido (bit de
		   SB_ISTNRM puesto sin acusar) no necesita descarte: volver a poner
		   un bit puesto es lo que hace el hardware. */
		if (intc_demorados & inttoadd)
		{
			logxmsg(LOG_INTC, "descartando int %x, ya demorada\n", inttoadd);
			return;
		}

		for (di = 0; di < INTC_DEMORAS; di++)
			if (intc_demora[di].evt == 0)
			{
				intc_demora[di].evt = inttoadd;
				intc_demora[di].vence = reloj_total + (unsigned long long) cnt * 50;
				intc_demorados |= inttoadd;
				return;
			}

		/* Sin lugar en la tabla: sale al instante, como antes. */
	}

	SET_BIT(ASIC_ACK_A, inttoadd);
	logxmsg(LOG_INTC, "a�adiendo int %x, estado %x\n", inttoadd, ASIC_ACK_A);
#endif
}

/*
	Censo de eventos del ASIC generados por segundo emulado, por bit. Es el
	desglose del censo de entregas de intc(): dice cuales son los eventos de
	cada cuadro y si alguno se genera de mas.

	**Va al principio de intc_add(), no al final.** Un evento con demora se
	guarda en la tabla y la funcion retorna ahi mismo, asi que un censo puesto
	al final no ve ninguno de los demorados --RENDERDONE, los fines de lista,
	los fines de DMA-- y da la lista de eventos incompleta sin decirlo: la
	primera version contaba cuatro por cuadro contra nueve entradas al ISR, y
	esa diferencia era el censo, no el emulador.
*/
static void censo_evento(DWORD inttoadd)
{
	if (traza_activa)
	{
		static unsigned long      bit[32];
		static unsigned long long marca = 0;

		unsigned long long ahora = reloj_ms();
		int b;

		for (b = 0; b < 32; b++)
			if (inttoadd & (1u << b))
				bit[b]++;

		if (ahora - marca >= 1000)
		{
			fprintf(stderr, "traza: eventos ASIC en %lu ms:",
				(unsigned long) (ahora - marca));

			for (b = 0; b < 32; b++)
				if (bit[b])
				{
					fprintf(stderr, " b%d=%lu", b, bit[b]);
					bit[b] = 0;
				}

			fprintf(stderr, "\n");
			marca = ahora;
		}
	}
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

/* Una linea por (evento, destino) distinto: que evento del ASIC se entrego por
   que nivel, o se descarto sin mascara ('-'). Es la pregunta de siempre cuando
   un guest espera una interrupcion que no llega. */
static void traza_asic_entrega(DWORD evento, char nivel)
{
	static DWORD vistos_ok[4], vistos_no;
	static int	entregas_crudas = -1;

	if (!traza_activa)
		return;

	/* DCEMU_TRAZA_ENTREGAS=1: cada entrega con su ciclo, sin dedup, con tope.
	   Para medir la cadencia real cuando el dedup por valor tapa la pregunta
	   "¿se esta re-entregando?". El centinela va con == -1: el estado
	   "resuelto y apagado" (-2) tambien es negativo, y un getenv por llamada
	   en este camino costo 5x de velocidad una vez. */
	if (entregas_crudas == -1)
	{
		const char * v = getenv("DCEMU_TRAZA_ENTREGAS");

		entregas_crudas = (v != NULL && v[0] == '1') ? 0 : -2;
	}

	if (entregas_crudas >= 0 && nivel != '-')
	{
		static unsigned long long segundo_marca = 0;
		static unsigned long cuenta_seg = 0;

		/* El total por segundo emulado ademas de las primeras 600: la
		   cadencia sigue visible cuando el tope ya se comio el detalle. */
		cuenta_seg++;

		if (reloj_total - segundo_marca >= 199499520ULL)
		{
			fprintf(stderr, "traza: entregas del ultimo segundo: %lu\n",
				cuenta_seg);
			segundo_marca = reloj_total;
			cuenta_seg = 0;
		}

		if (entregas_crudas < 600)
		{
			entregas_crudas++;
			fprintf(stderr, "traza: entrega cruda %c %08lx a %llu ciclos\n",
				nivel, (unsigned long) evento, reloj_total);
		}
	}

	if (nivel == '-')
	{
		if (vistos_no & evento)
			return;

		vistos_no |= evento;
	}
	else
	{
		int i = (nivel == '9') ? 0 : (nivel == 'B') ? 1 : 2;

		if (vistos_ok[i] & evento)
			return;

		vistos_ok[i] |= evento;
	}

	fprintf(stderr, "traza: asic evento %08lx %s (IML6=%08lx IML4=%08lx IML2=%08lx)\n",
		(unsigned long) evento,
		(nivel == '-') ? "encolado sin entregar" :
		(nivel == '9') ? "entregado por IRQ9" :
		(nivel == 'B') ? "entregado por IRQB" : "entregado por IRQD",
		(unsigned long) ASIC_IRQ9_A, (unsigned long) ASIC_IRQB_A,
		(unsigned long) ASIC_IRQD_A);
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
			traza_asic_entrega(pint->pending_int, '9');
          	intc_delete(pint);
          	return;
		}
     	if (pint->pending_int & ASIC_IRQB_A
     	&&  intc(EXC_IRQB))
     	{
			traza_asic_entrega(pint->pending_int, 'B');
          	intc_delete(pint);
          	return;
		}
     	if (pint->pending_int & ASIC_IRQD_A
     	&&  intc(EXC_IRQD))
     	{
			traza_asic_entrega(pint->pending_int, 'D');
          	intc_delete(pint);
          	return;
		}
		/* Sin mascara que lo cubra (o SR no deja entrar): se saca de la cola.
		   El bit de SB_ISTNRM queda puesto igual, asi que un guest que
		   sondee lo ve; uno que habilite la mascara despues, no. */
		traza_asic_entrega(pint->pending_int, '-');
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

	/* Las demoras vencidas. Al vencer, el evento OCURRE: recien ahi se enciende
	   su bit de SB_ISTNRM y queda entregable. Se compara contra reloj_total, asi
	   que no importa cada cuanto se llame a esta funcion. */
	if (intc_demorados != 0)
	{
		int di;

		for (di = 0; di < INTC_DEMORAS; di++)
			if (intc_demora[di].evt != 0)
			{
				if (reloj_total >= intc_demora[di].vence)
				{
					SET_BIT(ASIC_ACK_A, intc_demora[di].evt);
					intc_demorados &= ~intc_demora[di].evt;
					intc_demora[di].evt = 0;
				}
			}
	}

	/*
		Nivel, no flanco: la peticion de cada linea se deriva del estado --
		SB_ISTNRM contra su mascara -- y la entrega no consume nada. Lo unico
		que baja la linea es el acuse del guest en SB_ISTNRM (pvr_write), o
		enmascarar.

		Antes habia una cola: la entrega sacaba de ella TODOS los bits que la
		mascara cubria, de una vez, y un bit sin mascara que lo cubriera se
		tiraba (eso ultimo costo el mando en el boot ROM: habilitaba el DMA del
		Maple, lo arrancaba y ponia la mascara despues, asi que perdia el fin
		por interrupcion y no volvia a sondear el bus). Con la cola, un ISR que
		atiende UN bit por entrada -- el mas bajo --, lo acusa y confia en que
		la linea siga afirmada para re-entrar por los demas (Katana hace
		exactamente eso) no recibia mas que el primero: Virtua Tennis atendia
		el render-done de video y los de ISP y TSP quedaban puestos sin volver
		a interrumpir jamas, y su pipeline de cuadro no giraba. Es el mismo
		error que tenian los temporizadores antes de intc_revisar_sh4() -- ver
		docs/clock-plan.md --, en sus dos formas sucesivas: primero se tiraba
		lo no cubierto, despues se consumia con la entrega.
	*/
	{
		DWORD listos = ASIC_ACK_A & ~intc_demorados;
		DWORD sin_mascara;

		if (listos & ASIC_IRQ9_A
		&&  intc(EXC_IRQ9))
		{
			traza_asic_entrega(listos & ASIC_IRQ9_A, '9');
			return;
		}

		if (listos & ASIC_IRQB_A
		&&  intc(EXC_IRQB))
		{
			traza_asic_entrega(listos & ASIC_IRQB_A, 'B');
			return;
		}

		if (listos & ASIC_IRQD_A
		&&  intc(EXC_IRQD))
		{
			traza_asic_entrega(listos & ASIC_IRQD_A, 'D');
			return;
		}

		/* Lo ocurrido que ninguna mascara cubre: si el guest no la habilita
		   despues, no va a salir nunca -- eso es lo que vale reportar. Lo
		   bloqueado por SR sale solo en una vuelta proxima. */
		sin_mascara = listos & ~(ASIC_IRQ9_A | ASIC_IRQB_A | ASIC_IRQD_A);

		if (sin_mascara != 0)
			traza_asic_entrega(sin_mascara, '-');
	}

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

	/* Sin entrega -- SR.BL, IMASK alto, o la mascara todavia apagada -- el
	   evento QUEDA en la cola y se reintenta: en el hardware la peticion
	   sigue afirmada hasta que el guest la atiende. Barrer aca la cola era
	   el mismo descarte silencioso que ya se curo en los timers y en el
	   registro normal, y costo el fin de comando que Windows CE espera para
	   consultar su lectura en flujo. */
#endif
}

