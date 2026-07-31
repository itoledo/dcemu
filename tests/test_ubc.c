/****************************************************************************

	Pruebas del UBC (ubc.c), manejado como lo maneja el driver de KOS
	(kernel/arch/dreamcast/hardware/ubc.c): los registros se programan con
	las mismas secuencias que enable_breakpoint() y se verifica lo que el
	manejador del guest veria -- EXPEVT, SPC, el salto a VBR + 0x100 y los
	flags CMFA/CMFB en BRCR.

	La frontera de instruccion de main_loop() se reproduce llamando a
	ubc_revisar_instruccion() antes de cada ejecutar(), que es exactamente lo
	que hace el bucle real. El gancho de operandos no hay que simularlo: los
	handlers de mov.c pasan por los macros memread/memwrite de mem.h, que ya
	llaman a ubc_operando() cuando hay un canal armado sobre operandos.

*****************************************************************************/

#include "arnes.h"
#include "dctest.h"
#include "suites.h"

#include "excepciones.h"
#include "ubc.h"

#define NOP				0x0009
#define PRUEBA_VBR		0x8C030000u

/* BBR: ID en bits 5-4 (1 instruccion, 2 operando, 3 ambos), RW en 3-2
   (1 lectura, 2 escritura, 3 ambos), tamano en el bit 6 y los bits 1-0. */
#define BBR_INSTR			0x10
#define BBR_OPERANDO		0x20
#define BBR_LEE				0x04
#define BBR_ESCRIBE			0x08
#define BBR_TAM_BYTE		0x01
#define BBR_TAM_WORD		0x02
#define BBR_TAM_LONG		0x03

/* BAMR: BASM (no comparar ASID) y el codigo de mascara en bits 3,1,0. */
#define BAMR_SIN_ASID		0x04
#define BAMR_MASCARA_10		0x01

static void preparar(void)
{
	arnes_reset();
	VBR = PRUEBA_VBR;
}

/* La frontera + la ejecucion, como main_loop(): si la frontera entro a la
   excepcion no se ejecuta nada. Devuelve 1 si hubo excepcion. */
static int frontera_y_ejecutar(WORD instr)
{
	if (ubc_activa && ubc_revisar_instruccion())
		return 1;

	ejecutar(instr);
	return 0;
}

/* ------------------------------------------------------------------------ */

static void instruccion_despues_de_ejecutar(void)
{
	preparar();

	/* Como el demo basic-breaking: solo la direccion; KOS arma ID=ambos,
	   RW=ambos y break despues de ejecutar (PCBA=1). */
	*UBC_BARA  = PRUEBA_PC;
	*UBC_BAMRA = BAMR_SIN_ASID;
	*UBC_BBRA  = (BBR_INSTR | BBR_OPERANDO) | (BBR_LEE | BBR_ESCRIBE);
	*UBC_BRCR  = UBC_BRCR_PCBA;
	ubc_registros_escritos();

	ESPERAR_U32(ubc_activa, 1);

	/* La instruccion coincide: se ejecuta entera y el break queda pendiente. */
	ESPERAR_I32(frontera_y_ejecutar(NOP), 0);
	ESPERAR_U32(PC, PRUEBA_PC + 2);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFA) != 0, 1);

	/* La frontera siguiente entrega: SPC = la siguiente, como imprime KOS. */
	ESPERAR_I32(ubc_revisar_instruccion(), 1);
	ESPERAR_U32(*EXPEVT, EXC_UBC_BREAK);
	ESPERAR_U32(SPC, PRUEBA_PC + 2);
	ESPERAR_U32(PC, PRUEBA_VBR + 0x100);
	ESPERAR_U32(SR_BL, 1);
}

static void instruccion_antes_de_ejecutar(void)
{
	preparar();

	*UBC_BARA  = PRUEBA_PC;
	*UBC_BAMRA = BAMR_SIN_ASID;
	*UBC_BBRA  = BBR_INSTR | (BBR_LEE | BBR_ESCRIBE);
	*UBC_BRCR  = 0;			/* PCBA=0: antes de ejecutar */
	ubc_registros_escritos();

	/* Entra sin ejecutar: SPC = la instruccion que coincidio. */
	ESPERAR_I32(frontera_y_ejecutar(NOP), 1);
	ESPERAR_U32(*EXPEVT, EXC_UBC_BREAK);
	ESPERAR_U32(SPC, PRUEBA_PC);
	ESPERAR_U32(PC, PRUEBA_VBR + 0x100);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFA) != 0, 1);
}

static void otra_direccion_no_dispara(void)
{
	preparar();

	*UBC_BARA  = PRUEBA_PC + 0x100;
	*UBC_BAMRA = BAMR_SIN_ASID;
	*UBC_BBRA  = BBR_INSTR | (BBR_LEE | BBR_ESCRIBE);
	*UBC_BRCR  = 0;
	ubc_registros_escritos();

	ESPERAR_I32(frontera_y_ejecutar(NOP), 0);
	ESPERAR_I32(ubc_revisar_instruccion(), 0);
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFA, 0);
	ESPERAR_U32(PC, PRUEBA_PC + 2);
}

static void operando_lectura_con_mascara_de_10_bits(void)
{
	preparar();

	/* Un rango de 1 KB en PRUEBA_DATOS, solo lecturas, cualquier tamano:
	   la segunda prueba del demo. */
	*UBC_BARA  = PRUEBA_DATOS;
	*UBC_BAMRA = BAMR_SIN_ASID | BAMR_MASCARA_10;
	*UBC_BBRA  = BBR_OPERANDO | BBR_LEE;
	*UBC_BRCR  = UBC_BRCR_PCBA;
	ubc_registros_escritos();

	ESPERAR_U32(ubc_operando_activa, 1);

	/* Leer por encima del rango: nada. */
	R(4) = PRUEBA_DATOS + 0x400;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x6002, 5, 4)), 0);	/* MOV.L @R4,R5 */
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFA, 0);

	/* Escribir dentro del rango: es de solo lectura, nada. */
	R(4) = PRUEBA_DATOS + 0x200;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2002, 4, 5)), 0);	/* MOV.L R5,@R4 */
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFA, 0);

	/* Leer dentro: dispara, y la entrega es en la frontera siguiente. */
	R(4) = PRUEBA_DATOS + 0x200;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x6002, 5, 4)), 0);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFA) != 0, 1);

	ESPERAR_I32(ubc_revisar_instruccion(), 1);
	ESPERAR_U32(*EXPEVT, EXC_UBC_BREAK);
	ESPERAR_U32(PC, PRUEBA_VBR + 0x100);
}

static void operando_escritura_de_un_dato_con_tamano(void)
{
	preparar();

	/* La tercera prueba del demo: escribir el valor 3, como word, en una
	   direccion exacta. El dato solo lo compara el canal B, con DBEB. */
	*UBC_BARB  = PRUEBA_DATOS;
	*UBC_BAMRB = BAMR_SIN_ASID;
	*UBC_BBRB  = BBR_OPERANDO | BBR_ESCRIBE | BBR_TAM_WORD;
	*UBC_BDRB  = 3;
	*UBC_BDMRB = 0;
	*UBC_BRCR  = UBC_BRCR_DBEB | UBC_BRCR_PCBB;
	ubc_registros_escritos();

	/* Leerla: nada (es de escritura). */
	R(4) = PRUEBA_DATOS;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x6001, 5, 4)), 0);	/* MOV.W @R4,R5 */
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFB, 0);

	/* El valor equivocado: nada. */
	R(5) = 43;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2001, 4, 5)), 0);	/* MOV.W R5,@R4 */
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFB, 0);

	/* El valor correcto con el tamano equivocado: nada. */
	R(5) = 3;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2000, 4, 5)), 0);	/* MOV.B R5,@R4 */
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFB, 0);

	/* El valor correcto como word: dispara. */
	R(5) = 3;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2001, 4, 5)), 0);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFB) != 0, 1);

	ESPERAR_I32(ubc_revisar_instruccion(), 1);
	ESPERAR_U32(*EXPEVT, EXC_UBC_BREAK);
}

static void dato_con_mascara_acepta_el_rango(void)
{
	preparar();

	/* Valor 0x7FF con mascara 0x3: acepta 0x7FC..0x7FF, como la cuarta
	   prueba del demo. */
	*UBC_BARB  = PRUEBA_DATOS;
	*UBC_BAMRB = BAMR_SIN_ASID;
	*UBC_BBRB  = BBR_OPERANDO | BBR_ESCRIBE | BBR_TAM_LONG;
	*UBC_BDRB  = 0x7FF;
	*UBC_BDMRB = 0x3;
	*UBC_BRCR  = UBC_BRCR_DBEB | UBC_BRCR_PCBB;
	ubc_registros_escritos();

	R(4) = PRUEBA_DATOS;

	/* Fuera del rango de valores: nada. */
	R(5) = 0x8FD;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2002, 4, 5)), 0);	/* MOV.L R5,@R4 */
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFB, 0);

	/* Dentro del rango: dispara. */
	R(5) = 0x7FD;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2002, 4, 5)), 0);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFB) != 0, 1);
}

static void secuencial_a_arma_a_b(void)
{
	preparar();

	/* La quinta prueba del demo: instruccion en A, luego escritura con dato
	   en B. B sin A previo no vale; A solo deja el latch, sin excepcion. */
	*UBC_BARA  = PRUEBA_PC;
	*UBC_BAMRA = BAMR_SIN_ASID;
	*UBC_BBRA  = (BBR_INSTR | BBR_OPERANDO) | (BBR_LEE | BBR_ESCRIBE);
	*UBC_BARB  = PRUEBA_DATOS;
	*UBC_BAMRB = BAMR_SIN_ASID;
	*UBC_BBRB  = BBR_OPERANDO | BBR_ESCRIBE | BBR_TAM_LONG;
	*UBC_BDRB  = 0x7FC;
	*UBC_BDMRB = 0x3;
	*UBC_BRCR  = UBC_BRCR_SEQ | UBC_BRCR_DBEB | UBC_BRCR_PCBA | UBC_BRCR_PCBB;
	ubc_registros_escritos();

	/* B con el valor correcto pero sin A previo: nada. Ojo que la frontera
	   corre en un PC que no es BARA (poner_instr + core.execute directo). */
	R(4) = PRUEBA_DATOS;
	R(5) = 0x7FC;
	PC = PRUEBA_PC + 0x40;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2002, 4, 5)), 0);
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFB, 0);

	/* La instruccion en BARA: A cumple, deja CMFA y el latch, sin entrar. */
	PC = PRUEBA_PC;
	ESPERAR_I32(frontera_y_ejecutar(NOP), 0);
	ESPERAR_I32(ubc_revisar_instruccion(), 0);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFA) != 0, 1);
	ESPERAR_U32(*EXPEVT, 0);

	/* Un valor que no coincide no rompe la secuencia... */
	R(5) = 0xFC;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2002, 4, 5)), 0);
	ESPERAR_U32(*UBC_BRCR & UBC_BRCR_CMFB, 0);

	/* ...y el que coincide, con A ya cumplida, dispara. */
	R(5) = 0x7FD;
	ESPERAR_I32(frontera_y_ejecutar(instr_nm(0x2002, 4, 5)), 0);
	ESPERAR_U32((*UBC_BRCR & UBC_BRCR_CMFB) != 0, 1);

	ESPERAR_I32(ubc_revisar_instruccion(), 1);
	ESPERAR_U32(*EXPEVT, EXC_UBC_BREAK);
}

static void bl_pospone_la_entrega(void)
{
	preparar();

	*UBC_BARA  = PRUEBA_PC;
	*UBC_BAMRA = BAMR_SIN_ASID;
	*UBC_BBRA  = BBR_INSTR | (BBR_LEE | BBR_ESCRIBE);
	*UBC_BRCR  = UBC_BRCR_PCBA;
	ubc_registros_escritos();

	ESPERAR_I32(frontera_y_ejecutar(NOP), 0);

	/* Con BL puesto la entrega espera; la peticion no se pierde. */
	SET_SH4_BIT(SR_BL);
	ESPERAR_I32(ubc_revisar_instruccion(), 0);
	ESPERAR_U32(*EXPEVT, 0);

	REMOVE_SH4_BIT(SR_BL);
	ESPERAR_I32(ubc_revisar_instruccion(), 1);
	ESPERAR_U32(*EXPEVT, EXC_UBC_BREAK);
}

static void desarmado_no_cuesta_nada(void)
{
	preparar();

	/* Sin ningun canal armado las banderas quedan en cero: es la condicion
	   que mantiene el camino rapido intacto. */
	ubc_registros_escritos();

	ESPERAR_U32(ubc_activa, 0);
	ESPERAR_U32(ubc_operando_activa, 0);

	R(4) = PRUEBA_DATOS;
	R(5) = 3;
	ejecutar(instr_nm(0x2002, 4, 5));
	ESPERAR_U32(*UBC_BRCR, 0);
}

/* ------------------------------------------------------------------------ */

static const dc_caso casos[] =
{
	CASO(instruccion_despues_de_ejecutar),
	CASO(instruccion_antes_de_ejecutar),
	CASO(otra_direccion_no_dispara),
	CASO(operando_lectura_con_mascara_de_10_bits),
	CASO(operando_escritura_de_un_dato_con_tamano),
	CASO(dato_con_mascara_acepta_el_rango),
	CASO(secuencial_a_arma_a_b),
	CASO(bl_pospone_la_entrega),
	CASO(desarmado_no_cuesta_nada),
};

const dc_suite suite_ubc = DEFINIR_SUITE("ubc", casos);
