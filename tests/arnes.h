/****************************************************************************

	ARNES - puesta a punto del nucleo SH-4 para las pruebas unitarias

	Las pruebas ejercitan los handlers reales de mov.c, arith.c, logic.c,
	shift.c, branch.c, syscontrol.c, floatsimple.c, floatcontrol.c,
	floatgraph.c y dcopcodes.c, con la tabla real de opcodes.c. Lo unico que
	se reemplaza es lo que arrastraria SDL y OpenGL al enlace:

	  - memoria_prueba.c en lugar de mem.c (mem.c llama a los callbacks del
		PVR en graficos.c),
	  - dobles.c para los simbolos de graficos.c, iso.c e intc.c que los
		handlers referencian.

	El resto es el emulador tal cual.

*****************************************************************************/

#ifndef _ARNES_H_
#define _ARNES_H_

#include "sh4emu.h"

#include "dctest.h"

/* Direcciones que usan las pruebas. Ambas caen en la RAM del sistema
   (zona 0x8C), lejos una de otra para que un @-Rn no pise las instrucciones. */
#define PRUEBA_PC		0x8C010000u	/* aqui se arma y ejecuta la instruccion */
#define PRUEBA_DATOS	0x8C020000u	/* zona de datos */
#define PRUEBA_VIDEO	0xA5000000u	/* RAM de video, para probar el despacho por zona */

/* Se limpia en cada reset: [PRUEBA_PC - 0x1000, PRUEBA_DATOS + 0x10000). */
#define PRUEBA_VENTANA_INICIO	(PRUEBA_PC - 0x1000u)
#define PRUEBA_VENTANA_FIN		(PRUEBA_DATOS + 0x10000u)

void arnes_iniciar(void);	/* una sola vez, antes de todo */
void arnes_reset(void);		/* antes de cada caso */

/* Escribe la instruccion en PC y la ejecuta, igual que main_loop(). */
void ejecutar(WORD instr);

/*
	Igual, pero por el camino vigilado de main_loop(): instantanea del contexto,
	salto armado, y si la instruccion aborta se restaura y se entra a la
	excepcion. Devuelve 1 si hubo excepcion y 0 si no.

	Es lo que hace falta para probar cualquier excepcion de reejecucion -- las
	de la MMU y las tres de la FPU --, porque el efecto observable no es solo
	EXPEVT y el salto a VBR: es tambien que el registro destino quedo sin tocar.
*/
int ejecutar_vigilado(WORD instr);

/* Deja una instruccion en memoria sin ejecutarla (delay slots). */
void poner_instr(DWORD dir, WORD instr);

/* Acceso a la memoria de prueba sin pasar por los handlers, para preparar
   el estado y para verificar el resultado. */
void  escribir_b(DWORD dir, BYTE valor);
void  escribir_w(DWORD dir, WORD valor);
void  escribir_l(DWORD dir, DWORD valor);
void  escribir_f(DWORD dir, float valor);
BYTE  leer_b(DWORD dir);
WORD  leer_w(DWORD dir);
DWORD leer_l(DWORD dir);
float leer_f(DWORD dir);

/* Constructores de instrucciones. El "base" trae los bits fijos del formato;
   los campos van en su lugar segun el manual del SH-4.

	instr_nm(0x300C, n, m)	0011nnnn mmmm1100
	instr_n(0x4011, n)		0100nnnn 00010001
	instr_ni(0xE000, n, i)	1110nnnn iiiiiiii
	instr_i(0x8800, i)		10001000 iiiiiiii
	instr_nd(0x8000, n, d)	10000000 nnnndddd
	instr_nmd(0x1000, n,m,d)0001nnnn mmmmdddd
*/
WORD instr_nm(WORD base, int n, int m);
WORD instr_n(WORD base, int n);
WORD instr_ni(WORD base, int n, int imm);
WORD instr_i(WORD base, int imm);
WORD instr_nd(WORD base, int n, int d);
WORD instr_nmd(WORD base, int n, int m, int d);

/* Estado de los registros del SH-4 que viven dentro de regmem y que los
   handlers usan (QACR0/1 en PREF, TRA y EXPEVT en TRAPA). */
void arnes_registros_en_cero(void);

/* Cuenta los accesos a zonas de memoria sin mapear. */
extern int prueba_accesos_invalidos;

/* Registro de que handlers llego a ejecutar la corrida. ejecutar() anota el
   puntero que salio de la tabla de despacho; la suite de cobertura recorre
   opcodes[] y avisa si alguna fila implementada nunca se ejercito. */
int arnes_handler_ejecutado(opcode_f * funcion);

/* Atajos para las dos comprobaciones que aparecen en casi todos los casos. */
#define ESPERAR_PC_SIGUIENTE()	ESPERAR_U32(PC, PRUEBA_PC + 2)
#define ESPERAR_T(v)			ESPERAR_U32(SR_T, (v))

/* Contadores de los dobles de graficos.c: PREF los dispara segun el tipo de
   parametro que encuentre en la FIFO del TA. */
extern int dobles_ta_list_end;
extern int dobles_user_clip;
extern int dobles_object_list_set;
extern int dobles_poly_modifier;
extern int dobles_vertex_handler;

/* Bandera de intc.c: RTE tiene que apagarla al volver de la excepcion.
   dobles_reset() la deja prendida para que la prueba vea el cambio. */
extern bool inside_int;

/* Dobles de intc.c: la lectora interrumpe al terminar cada comando. */
extern int		dobles_int_normal;
extern int		dobles_int_ext;
extern DWORD	dobles_ultima_int_normal;
extern DWORD	dobles_ultima_int_ext;

/* Doble de iso.c: 0 simula la bandeja vacia. */
extern int		dobles_hay_disco;

void dobles_reset(void);

#endif /* _ARNES_H_ */
