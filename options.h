#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#ifdef WIN32
#define PLATFORM "Windows"
#else
#define PLATFORM "Unknown"
#endif

#define APPTITLE 	"DCEMU - DreamCast Emulator for " PLATFORM " - " __DATE__
#define OPENGL

// Usar funcciones o macros para accesar la memoria
#define MEMORY_MACROS

#define BIOS_HACKS		// para tratar de correr la bios
#define USE_BIOS_FONT
#define _fast_interpreter_
// #define PRINT_ASM
//#define LOGGING
// #define LOG_FFLUSH
// #define MACRO_REPLACEMENTS // ???
// #define NI
// #define JOYSTICK
// #define ASM_DEBUG

// #define DEBUG_MOV
// #define DEBUG_MOV_MOVA
// #define DEBUG_BRANCH
// #define DEBUG_BRANCH_JSR
// #define DEBUG_ARITH
// #define DEBUG_ARITH_CMP
// #define DEBUG_ARITH_DMUL
// #define DEBUG_SHIFT
// #define DEBUG_SHIFT_SHLD
//#define DEBUG_FLOAT_SIMPLE
//#define DEBUG_FLOAT_GRAPH
// #define DEBUG_SYSCONTROL
// #define DEBUG_SYSCONTROL_STS
// #define DEBUG_FLOAT_CONTROL

// #define DEBUG_MEM
// #define DEBUG_MEM_READ
// #define DEBUG_MEM_WRITE
// #define DEBUG_MEM_REGISTERS
// #define DEBUG_MEMORY_POINTER
// #define DEBUG_MEM_HASH
// #define DEBUG_LOGIC
// #define DEBUG_MEM_VIDEO
// #define EXTRA_REG_DEBUG
// #define DEBUG_INTC

#define TEXTURE_CACHING
// #define FULL_DEBUG_FROM		(0x8c018980)
// #define FULL_DEBUG_TO		(0x8c018996)
// #define CHECK_VALUE			(0x8c021410)

/*
	Watchpoint de escritura. Informa por stderr cada escritura que toca la
	direccion vigilada, con el PC y el PR que la hicieron y el valor que queda.

	Es para responder "quien escribe esta variable", que es la pregunta que
	aparece al depurar el arranque por BIOS. Se estreno con 0x8C22FF94: el boot
	ROM se detenia esperando que esa palabra valiera 6 y se quedaba en 5, y el
	watchpoint llevo derecho a que faltaba el registro REVISION del PVR. La
	historia esta en docs/bios-boot-plan.md.

	**Se configura por linea de comandos**, con --watchpoint=DIR[:TAM]; era un
	#define y cada pregunta costaba una recompilacion del emulador entero.
	Apagado cuesta una comparacion contra cero por escritura, al lado de la
	llamada indirecta que ya hay ahi.

	La direccion se compara por su parte fisica, asi que vigilar 0x8C22FF94
	atrapa tambien las escrituras por 0x0C22FF94 y 0xAC22FF94. Pensado para
	RAM: si se apunta a un registro, la lectura del valor actual puede tener
	efectos secundarios.
*/
#define WATCHPOINT_MAX		200			/* cortar despues de tantos informes */
// #define WATCHPOINT_SOLO_CAMBIOS		/* callar las escrituras que no cambian nada */
// #define WATCHPOINT_ANILLO			/* volcar ademas el anillo de PC de --traza-mem */

#endif // _OPTIONS_H_
