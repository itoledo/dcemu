#include "main.h"
#include "log.h"
#include "graficos.h"
#include "glops.h"

list gop_list;

int gop_cnt = 0;

// #define DEBUG_GLOP

void init_sOglP()
{
	gop_list.current = (node *) malloc(sizeof(node));
	gop_list.current->fill = 0;
	gop_list.first = gop_list.last = gop_list.current;
}

void incr_sOglP()
{
gop_list.current->fill++;
gop_cnt++;
if(gop_list.current->fill == 400)
  {
	if (gop_list.current->next == NULL)
		gop_list.current->next = (node *) malloc(sizeof(node));
	gop_list.last =gop_list.current->next;
	gop_list.current =gop_list.current->next;
	gop_list.current->fill = 0;
  }
}

void reset_sOglP()
{
	gop_list.last = gop_list.current = gop_list.first;
	gop_list.first->fill =0;
}

void glop_process()
{
	int i;
	node * ptr;
	logxmsg(LOG_GLOP, "gop_process: procesando %d eventos\n", gop_cnt);
	
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	ptr = gop_list.first;


	for(;ptr;ptr = ptr->next)
	{
		for (i = 0; i < ptr->fill; i++)
		{
			switch(ptr->bar[i].op)
			{
				case GLOP_BEGIN:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "begin\n");;
	#endif
					glBegin(ptr->bar[i].enum1);
					break;
	
				case GLOP_BINDTEXTURE:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "bindtexture\n");
	#endif
					glEnable(GL_TEXTURE_2D);
					glBindTexture(ptr->bar[i].enum1, ptr->bar[i].uint1);
					break;
	
				case GLOP_CLEAR:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "clear\n");
	#endif
					glClear(ptr->bar[i].bitfield1);
					break;
	
				case GLOP_BLENDFUNC:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "blendfunc\n");
	#endif
					glBlendFunc(ptr->bar[i].enum1, ptr->bar[i].enum2);
					break;
	
				case GLOP_COLOR4F:	
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "color4f %f, %f, %f, %f\n",
						ptr->bar[i].float1, ptr->bar[i].float2, ptr->bar[i].float3, ptr->bar[i].float4);
	#endif
					glColor4f(ptr->bar[i].float1, ptr->bar[i].float2, ptr->bar[i].float3, ptr->bar[i].float4);
					break;
	
				case GLOP_VERTEX3F:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "vertex3f %f, %f, %f\n",
						ptr->bar[i].float1, ptr->bar[i].float2, ptr->bar[i].float3);
	#endif
					glVertex3f(ptr->bar[i].float1, ptr->bar[i].float2, ptr->bar[i].float3);
					break;
					
				case GLOP_TEXCOORD2F:
	#ifdef DEBUG_GLOP
			+		logxmsg(LOG_GLOP, "texcoord2f\n");
	#endif
					glTexCoord2f(ptr->bar[i].float1, ptr->bar[i].float2);
					break;
	
				case GLOP_END:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "end\n");
	#endif
					glEnd();
					glDisable(GL_TEXTURE_2D);
					break;
	
				case GLOP_TEXIMAGE2D:
	#ifdef DEBUG_GLOP
					logxmsg(LOG_GLOP, "teximage2d\n");
	#endif
					glTexImage2D(ptr->bar[i].enum1, ptr->bar[i].int1, ptr->bar[i].int2, ptr->bar[i].sizei1, ptr->bar[i].sizei2,
						ptr->bar[i].int3, ptr->bar[i].enum2, ptr->bar[i].enum3, ptr->bar[i].ptr1);
				break;
			break;
				}
			}
		}
		glDisable(GL_DEPTH_TEST);
		gop_cnt = 0;
		reset_sOglP();
}