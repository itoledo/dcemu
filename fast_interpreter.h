#ifndef _loop_
#define _loop_

#include "log.h"

typedef struct
{
	WORD arg;
	opcode_f * opcode;
}instruction;

typedef struct
{
  instruction * array; // the array of instructions in the loop
  int breakpoint; // the instruction which can get us out of the loop
  int size; // the size of the array
  int current; // the current instruction withing the loop
}loop;

extern int dt_value; // holds the R(n) value to decrement
extern int loop_type; // the type of loop we've encountered
extern register int in_loop;
extern int uncoditional_jump; // 1 if we are in an unconditional jump ie doesn't depend on the state of the T flag
extern int sub_routine; // we cache entire sub routines ie from calls to JSR or BSR to RTS
extern loop jumptable [65536];

// a bunch of defines for us to know what kind of loop or subroutine we're in
#define DT_LOOP  0
#define CMP_LOOP 1
#define SUB_ROUTINE 2

void execute_loop();

#endif