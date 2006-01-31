#include "sh4emu.h"
#include "fast_interpreter.h"

loop jumptable [65536];
loop * current_loop;
int dt_value = -1;
int sub_routine=0;
register int in_loop=0;
int uncoditional_jump=0; // 1 if we are in an unconditional jump ie doesn't depend on the state of the T flag

void alloc_block(DWORD address,int size)
{
	jumptable[address] = (loop*) malloc(sizeof(loop));
	jumptable[address].array = (instruction *) malloc(sizeof(instruction)*size);
	current_loop = jumptable[address];
}

void execute_loop()
{
	switch(type)
	{
	// deals with the cases where we have DT followed by BFS
	case DT:
		if(dt_value > 1)
		{
			dt_value--;
			current_loop->array[current_loop.current].opcode(current_loop->array[current_loop.current].arg);
			if(current_loop.current == current_loop.size)	
			{
					current =0; //we don't execute DT nor BFS
			}
			else current_loop.current++;
		}
		else
		{
			PC = PC +4; // jumping over DT and BFS
			in_loop =0; 
		}
	break;
	// in loop until an instruction within the loop changes the T Bit
	case CMP_LOOP:
			current_loop->array[current_loop.current].opcode(current_loop->array[current_loop.current].arg);
			if(current_loop.current == current_loop.size)	
			{
				current_loop.current =0; //we don't run BFS
			}
			else current_loop.current++;
	break;
	}
}