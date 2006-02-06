#include "sh4emu.h"
#include "fast_interpreter.h"

loop jumptable [65536];
loop * current_loop;
int dt_value = -1;
int sub_routine=0;
register int in_loop=0;
int uncoditional_jump=0; // 1 if we are in an unconditional jump ie doesn't depend on the state of the T flag
int control=0;

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
		//1st we cache everything
		
		// we can start executing stuff in the loop
		while()
		{
			while(dt_value > 1)
			{
				dt_value--;
				current_loop->array[current_loop.current].opcode(current_loop->array[current_loop.current].arg);
				if(current_loop.current == current_loop.size)	
				{
					current_loop.current =0; //we don't execute DT nor BFS
				}
				else current_loop.current++;
				// we've got to check the timers
				if(core.context.cycles >= 50)
				{
					timer_check();
					core.context.cycles_v_int += core.context.cycles;
					core.context.cycles = 50 - core.context.cycles;
				}
				core.context.cycles_v_int_total += core.context.cycles_v_int;
				if(core.context.cycles_v_int > 978)
				{
					core.context.cycles_v_int = 978 - core.context.cycles_v_int;
					pvr_scanline++;
					control=1;
				}
				if(core.context.cycles_v_int_total >= pvr_spg_load_vcount)
				{
					pvr_scanline = 0;
					control =2;
				}
			}
			if(!dt_value)
			{
				PC = PC +4; // jumping over BFS
				core.context.cycles +=2; // but we need to add the number of cycles it would take
				in_loop =0;
				break; // loop is done
			}
			// if we reach here wheter we need to refresh the screen or treat and added interrupt
			else
			{
				if(control==1)
				{
					pvr_scanline++;
   				
					if (pvr_scanline == pvr_spg_vblank_int_out)
					{
        					logxmsg(LOG_PVR, "llamando SCANINT1\n");
    						intc_add(ASIC_EVT_PVR_SCANINT1, 0);
					}
					else
					if (pvr_scanline == pvr_spg_vblank_int_in)
					{
        					logxmsg(LOG_PVR, "llamando SCANINT2\n");
    						intc_add(ASIC_EVT_PVR_SCANINT2, 0);
					}				
				}				
			}
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