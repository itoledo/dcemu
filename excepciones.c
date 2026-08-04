/****************************************************************************

	EXCEPCIONES - la entrada a una excepcion sincrona del SH-4

	Ver excepciones.h.

*****************************************************************************/

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "excepciones.h"
#include "log.h"
#include "mem.h"		/* memread_fisico: el directorio de proceso del censo */
#include "mmu.h"
#include "perf.h"		/* los contadores de la instantanea */
#include "sh4emu.h"
#include "tmu.h"		/* reloj_ms(): el histograma DCEMU_TRAZA_EXC */
#include "traza.h"

jmp_buf	excepcion_salto;
int		excepcion_salto_armado = 0;

DWORD	excepcion_codigo;
DWORD	excepcion_vector;
DWORD	excepcion_fpu_cause;

int		excepcion_vigilar = 0;
int		en_ranura_retardo = 0;

/*
	intc() hace lo mismo para el camino de interrupcion, con INTEVT y el vector
	0x600; esto es para todo lo demas.

	Ojo: a diferencia de intc(), no mira SR.BL. Las excepciones de reejecucion
	con BL puesto son causa de reset en el chip, y aca se registra y se entra
	igual, que es mas util para depurar que reiniciar en silencio.
*/
/*
	Lo que el guest manda a la salida de depuracion, impreso por dcemu.

	Windows CE la tira: su OAL de esta consola no escribe ni al SCI ni al SCIF
	(medido con watchpoint sobre los dos TDR), asi que OutputDebugStringW y
	NKvDbgPrintfW no dejan rastro por ningun lado -- y son cientos de lineas
	en las que el guest cuenta su propio arranque. DCDoom imprime ahi todo el
	log de DOOM, y su driver de video el "Timeout for Tile Accelerator" que
	nombro el bloqueo del TA.

	Va ANTES de tocar SR porque el argumento esta en el R4 del guest, y la
	entrada a la excepcion cambia de banco. La direccion del salto -- que en
	este punto todavia es PC -- decodifica como
	`0xFFFFFE01 - dest = (apiset << 9) | (metodo << 1)`; el apiset 0 es el
	kernel, y sus metodos 14 y 23 son las dos salidas de depuracion. La
	numeracion es de CE, no de esta imagen.

	La lectura es "para mirar" (mmu_traducir_mirar): sin fallar, sin
	excepciones y sin mover URC. Un longjmp desde adentro de excepcion_entrar()
	dejaria al emulador a medio entrar en una excepcion.
*/
static void traza_depuracion_guest(void)
{
	DWORD	v = (0xFFFFFE01ul - PC) & 0xFFFFFFFFul;
	DWORD	apiset = v >> 9;
	DWORD	metodo = (v & 0x1FF) >> 1;
	DWORD	dir = R(4);
	int		ancho;			/* 2 = UTF-16 (OutputDebugStringW), 1 = ASCII */
	int		i;

	if (apiset != 0)
		return;

	if (metodo == 14)
		ancho = 2;			/* OutputDebugStringW */
	else if (metodo == 23)
		ancho = 2;			/* NKvDbgPrintfW: el formato, sin los argumentos */
	else
		return;

	if (dir == 0)
		return;

	fprintf(stderr, "guest: ");

	for (i = 0; i < 512; i++)
	{
		DWORD	fisica = mmu_traducir_mirar(dir + (DWORD) (i * ancho));
		WORD	c = 0;

		if (fisica == 0)
			break;

		memread_fisico(fisica, &c, (size_t) ancho);

		if (c == 0)
			break;

		/* Los saltos de linea del guest ya cierran la linea; el resto se
		   imprime tal cual si es legible. */
		if (c == '\n')
			break;

		fputc((c >= 0x20 && c < 0x7F) ? (int) c : '.', stderr);
	}

	fputc('\n', stderr);
}

void excepcion_entrar(DWORD codigo, DWORD vector)
{
	if (traza_activa && codigo == 0x0e0)
	{
		static int habilitado = -1;

		if (habilitado == -1)
		{
			const char * v = getenv("DCEMU_TRAZA_DEPURACION");

			habilitado = (v != NULL && v[0] == '1');
		}

		if (habilitado)
			traza_depuracion_guest();
	}

	SSR = SR;
	SPC = PC;
	SGR = R(15);

	*EXPEVT = codigo;

	SET_SH4_BIT(SR_BL);
	SET_SH4_BIT(SR_MD);
	SET_SH4_BIT(SR_RB);
	UpdateSR_ya_escrito();

	PC = VBR + vector;

	logxmsg(LOG_INTC, "excepcion: EXPEVT %03x, SPC %08x, salta a %08x\n",
		codigo, SPC, PC);

	/*
		Una excepcion general que el guest no esperaba termina casi siempre en
		un BRA a si mismo, y desde ahi no se puede saber cual fue: el manejador
		ya piso los registros. Con --traza-mem se reporta cada codigo la primera
		vez, que es lo que convierte "se colgo en 0xAC00E0B2" en "instruccion
		ilegal en tal PC".
	*/
	if (traza_activa)
	{
		static DWORD vistos[16];
		static int   n = 0;
		int          i;

		/*
			DCEMU_TRAZA_EXC=1: histograma de excepciones por segundo emulado.
			Es la vista macro de un guest con MMU o syscalls por trampa
			--Windows CE--: recargas de TLB (040/060) y syscalls (0e0) por
			segundo dicen si el sistema vive, quedo ocioso o tormenta, y en
			que segundo cambio. Dos veces contesto en minutos lo que las
			herramientas por direccion no podian: donde muere el arranque de
			DCDoom.
		*/
		{
			static int habilitado = -1;

			if (habilitado == -1)
			{
				const char * v = getenv("DCEMU_TRAZA_EXC");

				/* 1: el histograma. 2: ademas, cada syscall (0x0e0) pasado el
				   segundo 0, con su destino y su llamador -- quien sigue vivo
				   y que pide, con tope para no inundar. 3: ademas, el flujo
				   completo de syscalls linea por linea, incluido el segundo 0
				   -- que es donde DCDOOM.EXE decidio salir y el censo no ve. */
				habilitado = (v != NULL) ? atoi(v) : 0;
			}

			if (habilitado)
			{
				static unsigned long long	seg_marca = 0;
				static unsigned long		cuenta[8];
				unsigned long long			seg = reloj_ms() / 1000;

				if (seg != seg_marca)
				{
					fprintf(stderr, "traza: excepciones del segundo %llu: "
						"040=%lu 060=%lu 080=%lu 0a0=%lu 0e0=%lu 1e0=%lu"
						" 820=%lu otras=%lu\n",
						seg_marca, cuenta[0], cuenta[1], cuenta[2], cuenta[3],
						cuenta[4], cuenta[5], cuenta[6], cuenta[7]);
					memset(cuenta, 0, sizeof(cuenta));
					seg_marca = seg;
				}

				switch (codigo)
				{
					case 0x040: cuenta[0]++; break;
					case 0x060: cuenta[1]++; break;
					case 0x080: cuenta[2]++; break;
					case 0x0a0: cuenta[3]++; break;
					case 0x0e0: cuenta[4]++; break;
					case 0x1e0: cuenta[5]++; break;
					case 0x820: cuenta[6]++; break;
					default:
					cuenta[7]++;

					/* Las "otras" son raras y suelen ser la fatal: nombrarlas. */
					if (habilitado >= 2 && cuenta[7] <= 4)
						fprintf(stderr, "traza: excepcion rara %03lx"
							" (SPC=%08lx, PR=%08lx)\n",
							(unsigned long) codigo,
							(unsigned long) SPC, (unsigned long) PR);
					break;
				}

				if (habilitado >= 2 && codigo == 0x0e0 && seg >= 1)
				{
					/* Una linea por (destino, llamador) distinto, pasado el
					   arranque: el censo de sitios de syscall del regimen
					   estacionario. El numero es el segundo de la primera
					   aparicion de ese sitio. */
					static DWORD	sitios[192][2];
					static int		n = 0;
					int				i;

					for (i = 0; i < n; i++)
						if (sitios[i][0] == SPC && sitios[i][1] == PR)
							break;

					if (i == n && n < 192)
					{
						/* El directorio de proceso vigente (la entrada 0 de la
						   tabla que apunta TTB) desambigua los PR bajos: el
						   espejo de la ranura 0 hace que el kernel y cada
						   proceso compartan esas direcciones. */
						DWORD directorio = 0;

						if (*TTB)
							memread_fisico(*TTB, &directorio, sizeof(DWORD));

						sitios[n][0] = SPC;
						sitios[n][1] = PR;
						n++;

						fprintf(stderr, "traza: syscall s%llu hacia %08lx"
							" (PR=%08lx, proceso %08lx)\n", seg,
							(unsigned long) SPC, (unsigned long) PR,
							(unsigned long) directorio);
					}

				}

				if (habilitado >= 3 && codigo == 0x0e0)
				{
					/* El flujo que el censo resume: cada syscall en orden con
					   su instante. El tope avisa al cortar -- un corte callado
					   ya costo una conclusion falsa con el watchpoint -- y
					   DCEMU_TRAZA_SC_MAX lo mueve: el tope fijo dejaba el
					   flujo en el primer medio segundo, y la pregunta suele
					   estar mas adelante (la ventana de DCDoom muere a los
					   1800 ms). */
					static long	quedan = -1;

					if (quedan < 0)
					{
						const char * m = getenv("DCEMU_TRAZA_SC_MAX");

						quedan = (m != NULL) ? atol(m) : 50000;
					}

					if (quedan > 0)
					{
						unsigned long long	us = reloj_us();
						DWORD				directorio = 0;

						if (*TTB)
							memread_fisico(*TTB, &directorio, sizeof(DWORD));

						fprintf(stderr, "traza: sc %llu.%llums %08lx"
							" PR=%08lx proc=%08lx\n",
							us / 1000, (us / 100) % 10,
							(unsigned long) SPC, (unsigned long) PR,
							(unsigned long) directorio);

						if (--quedan == 0)
							fprintf(stderr, "traza: sc: tope de lineas"
								" alcanzado, flujo cortado aca.\n");
					}
				}
			}

			/*
				DCEMU_TRAZA_SYSCALL=dest[:pr[:N[:K]]] (hex:hex:dec:dec) traza N
				instrucciones (3000 por omision) desde la K-esima aparicion del
				syscall a dest con llamador pr. Es DCEMU_TRAZA_ATA para las
				trampas de Windows CE: la unica manera de leer que hizo el guest
				con lo que un syscall le contesto -- --traza-desde no distingue
				el sitio cuando el PC de retorno es codigo compartido. El flujo
				de DCEMU_TRAZA_EXC=3 da el dest, el pr y la K.

				Casa contra el SPC de CUALQUIER excepcion, no solo el 0x0e0:
				un dest ffffxxxx solo aparece como syscall igual, y un dest en
				codigo comun arma la traza sobre la excepcion que ese sitio
				levanta -- una recarga de TLB o el primer uso de FPU de un
				hilo, que es lo que --traza-desde no puede aislar por proceso.
				El PR del llamador desambigua el mismo PC en dos procesos.
			*/
			{
				static int		ts_estado = -1;		/* -1 sin leer, 0 apagado */
				static DWORD	ts_dest = 0, ts_pr = 0;
				static long		ts_n = 3000, ts_saltar = 0;
				static int		ts_con_pr = 0;

				if (ts_estado == -1)
				{
					const char * v = getenv("DCEMU_TRAZA_SYSCALL");

					ts_estado = 0;

					if (v != NULL)
					{
						char * resto = NULL;

						ts_dest   = strtoul(v, &resto, 16);
						ts_estado = 1;

						if (resto != NULL && *resto == ':')
						{
							ts_pr     = strtoul(resto + 1, &resto, 16);
							ts_con_pr = 1;
						}

						if (resto != NULL && *resto == ':')
							ts_n = strtol(resto + 1, &resto, 0);

						if (resto != NULL && *resto == ':')
							ts_saltar = strtol(resto + 1, NULL, 0);
					}
				}

				if (ts_estado == 1 && SPC == ts_dest
					&& (!ts_con_pr || PR == ts_pr))
				{
					if (ts_saltar > 0)
						ts_saltar--;
					else
						traza_arrancar(ts_n);
				}
			}
		}

		for (i = 0; i < n; i++)
			if (vistos[i] == codigo)
				return;

		if (n < 16)
			vistos[n++] = codigo;

		fprintf(stderr, "traza: excepcion EXPEVT %03lx desde PC %08lx,"
			" salta a %08lx\n",
			(unsigned long) codigo, (unsigned long) SPC, (unsigned long) PC);
	}
}

void excepcion_abortar(DWORD codigo, DWORD vector)
{
	excepcion_codigo = codigo;
	excepcion_vector = vector;

	if (excepcion_salto_armado)
	{
		excepcion_salto_armado = 0;
		longjmp(excepcion_salto, 1);
	}

	logxmsg(LOG_MEM, "excepcion: %03x fuera de una instruccion\n", codigo);
}

void excepcion_actualizar_vigilancia(void)
{
	/* SR.FD apaga la FPU entera; cualquier bit de Enable arma la trampa de
	   operacion de FPU. Ninguno de los dos esta puesto en lo que corre hoy.

	   fpu_deshabilitada se deriva aca y en ningun otro lado: es una copia de
	   SR.FD para que el despacho no extraiga un campo de bits por instruccion,
	   y si se pudiera escribir por separado las dos se irian de sincronia. */
	fpu_deshabilitada = SR_FD;

	excepcion_vigilar = mmu_activa
					 || fpu_deshabilitada
					 || (FPSCR & FPU_ENABLE_TODAS) != 0;
}

/*
	La instantanea del estado que una instruccion puede llegar a mutar antes de
	fallar.

	No alcanza con copiar core.context: los dos bancos de registros de punto
	flotante viven fuera, y el contexto solo guarda los punteros. Sin esto,
	FMOV.S @Rm+,FRn que falla por MMU dejaria FRn escrito, y sobre todo la
	trampa de FPU no podria cumplir lo que pide el manual -- que el registro
	destino no se actualice.
*/
static context_t	instantanea_contexto;
static FPR_BANK		instantanea_fr;
static FPR_BANK		instantanea_xf;

/*
	Las dos sondas de techo de la fase 5 de docs/rendimiento-plan.md.

	Se leen una vez al arrancar --nunca con getenv() en el camino caliente, que
	ya costo 44 millones de barridos del bloque de entorno en otra ocasion-- y
	viven en el mismo binario a proposito: comparar dos compilaciones distintas
	introduce el layout como variable, y este arbol ya perdio una sesion por
	eso. Con una sola imagen, la A y la B difieren solo en un getenv del
	arranque.

	DCEMU_SONDA_SIN_BANCOS_FPU=1 saltea las dos copias de los bancos de coma
	flotante, en tomar Y en restaurar. Es exactamente la salida 1 del plan
	llevada al extremo (ninguna instruccion los copia, ni siquiera las de FPU),
	asi que mide su techo. **Puede alterar el resultado** si una instruccion de
	FPU escribe su destino y despues falla; que no lo haya alterado se verifica
	con la cuenta de instrucciones y de escenas, como cualquier A/B de aca.

	DCEMU_SONDA_SIN_INSTANTANEA=1 saltea las tres copias. Mide el techo total
	del mecanismo y **rompe el guest** en cuanto una falta tenga algo que
	deshacer; el numero vale solo si el trabajo sale igual, y si no sale igual
	lo que vale es saber cuanto se apoya el guest en esto.
*/
int excepcion_sonda_sin_bancos     = 0;
int excepcion_sonda_sin_instantanea = 0;
int excepcion_sonda_setjmp_instr   = 0;

void excepcion_sondas_iniciar(void)
{
	const char * v;

	v = getenv("DCEMU_SONDA_SIN_BANCOS_FPU");
	excepcion_sonda_sin_bancos = (v != NULL && atoi(v) != 0);

	v = getenv("DCEMU_SONDA_SIN_INSTANTANEA");
	excepcion_sonda_sin_instantanea = (v != NULL && atoi(v) != 0);

	/* La forma vieja de armar el salto: un setjmp por instruccion. Se conserva
	   para poder medir la nueva contra ella sin cambiar de binario. */
	v = getenv("DCEMU_SONDA_SETJMP_POR_INSTRUCCION");
	excepcion_sonda_setjmp_instr = (v != NULL && atoi(v) != 0);

	if (excepcion_sonda_sin_bancos || excepcion_sonda_sin_instantanea)
		fprintf(stderr, "sonda: instantanea %s\n",
			excepcion_sonda_sin_instantanea ? "APAGADA (el guest puede divergir)"
											: "sin los bancos de FPU");
}

/*
	Si los bancos de coma flotante entraron en la instantanea de esta
	instruccion. Ver excepcion_instantanea_fpu().
*/
static int instantanea_con_bancos = 0;

/*
	Si hay instantanea que restaurar para la instruccion en curso.

	Vale 0 en dos casos y los dos significan lo mismo --nada que deshacer--:
	entre la invalidacion y la toma, o sea mientras se busca la instruccion, y
	durante toda instruccion que la clasificacion de opcodes.c declara incapaz
	de abortar. Restaurar en cualquiera de los dos desharia la instruccion
	**anterior**, que si se ejecutó entera.
*/
static int instantanea_valida = 0;

void excepcion_instantanea_invalidar(void)
{
	instantanea_valida = 0;
}

void excepcion_instantanea_tomar(void)
{
	PERF_CONTAR(perf_instantaneas);

	instantanea_con_bancos = 0;
	instantanea_valida     = 1;

	if (!excepcion_sonda_sin_instantanea)
		memcpy(&instantanea_contexto, &core.context, sizeof(context_t));
}

/*
	Los dos bancos de coma flotante, que son 128 de los ~318 bytes y **solo
	hacen falta si la instruccion puede escribirlos**.

	Lo llama run() cuando la instruccion es de FPU segun es_instruccion_fpu(),
	que es la misma definicion que decide la trampa de SR.FD y sale del manual:
	todo lo que empieza con 1111 mas las cuatro transferencias de FPUL y FPSCR.
	Ponerlo ahi y no en main_loop() es lo que cierra el agujero de las ranuras
	de retardo: una instruccion de FPU en la ranura de un salto tambien pasa por
	run(), y su instantanea de contexto es la del salto -- tomada antes, cuando
	los bancos todavia no se habian tocado --.

	Con la MMU apagada no se llama nunca: sin instantanea armada no hay nada que
	guardar.
*/
void excepcion_instantanea_fpu(void)
{
	if (excepcion_sonda_sin_instantanea || excepcion_sonda_sin_bancos)
		return;

	memcpy(&instantanea_fr, core.context.FR_BANK, sizeof(FPR_BANK));
	memcpy(&instantanea_xf, core.context.XF_BANK, sizeof(FPR_BANK));

	instantanea_con_bancos = 1;
}

void excepcion_instantanea_restaurar(void)
{
	PERF_CONTAR(perf_instantaneas_usadas);

	/* Sin instantanea no hay nada que deshacer, y deshacer seria peor: ver
	   instantanea_valida. Pasa en la falta de busqueda, que ocurre antes de
	   ejecutar. */
	if (!instantanea_valida || excepcion_sonda_sin_instantanea)
		return;

	/* Los punteros de banco vuelven con el contexto, asi que hay que escribir
	   el contenido despues de restaurarlo: si FRCHG los intercambio, los
	   bloques tienen que volver a su banco original, no al que quedo. */
	memcpy(&core.context, &instantanea_contexto, sizeof(context_t));

	if (!instantanea_con_bancos)
		return;

	memcpy(core.context.FR_BANK, &instantanea_fr, sizeof(FPR_BANK));
	memcpy(core.context.XF_BANK, &instantanea_xf, sizeof(FPR_BANK));
}

void excepcion_reponer(void)
{
	if (excepcion_codigo != EXC_FPU_OPERACION)
		return;

	/* Cause si, Flag no: el manual dice que en una excepcion de FPU el campo
	   Flag no se actualiza. La instantanea ya devolvio los dos a como estaban
	   antes de la instruccion, asi que basta con escribir Cause encima. */
	FPSCR = (FPSCR & ~FPU_CAUSA_TODAS) | excepcion_fpu_cause;
}
