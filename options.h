#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#define APPTITLE 	"DCEMU - DreamCast Emulator for Windows 21-APR-2004"
#define OPENGL

// Usar funcciones o macros para accesar la memoria
#define MEMORY_MACROS

#define USE_BIOS_FONT
//#define PRINT_ASM
#define LOGGING
//#define JOYSTICK
//#define ASM_DEBUG

// #define DEBUG_MOV
// #define DEBUG_MOV_MOVA
// #define DEBUG_BRANCH
// #define DEBUG_BRANCH_JSR
// #define DEBUG_ARITH
// #define DEBUG_ARITH_CMP
// #define DEBUG_ARITH_DMUL
// #define DEBUG_SHIFT
// #define DEBUG_FLOAT_SIMPLE
// #define DEBUG_FLOAT_GRAPH
// #define DEBUG_SYSCONTROL
// #define DEBUG_SYSCONTROL_STS
// #define DEBUG_FLOAT_CONTROL

// #define DEBUG_MEM
// #define DEBUG_MEM_READ
// #define DEBUG_MEM_WRITE
#define DEBUG_MEMORY_POINTER
// #define DEBUG_LOGIC
// #define DEBUG_MEM_REGISTERS
// #define DEBUG_MEM_VIDEO
// #define EXTRA_REG_DEBUG
#define DEBUG_VERTEX
// #define FULL_DEBUG_FROM		(0x8c018980)
// #define FULL_DEBUG_TO		(0x8c018996)
// #define CHECK_VALUE			(0x8c021410)

#endif // _OPTIONS_H_
