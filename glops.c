#include "main.h"
#include "glops.h"
#include "log.h"

s_glOp gop_list[MAX_GLOPS];
int gop_cnt = 0;

// #define DEBUG_GLOP

void glop_process()
{
	int i;
	
	logxmsg(LOG_GLOP, "glop_process: procesando %d eventos\n", gop_cnt);
	
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	for (i = 0; i < gop_cnt; i++)
	{
		switch(gop_list[i].op)
		{
			case GLOP_BEGIN:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "begin\n");
#endif
				glBegin(gop_list[i].enum1);
				break;

			case GLOP_BINDTEXTURE:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "bindtexture\n");
#endif
				glEnable(GL_TEXTURE_2D);
				glBindTexture(gop_list[i].enum1, gop_list[i].uint1);
				break;

			case GLOP_CLEAR:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "clear\n");
#endif
				glClear(gop_list[i].bitfield1);
				break;

			case GLOP_BLENDFUNC:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "blendfunc\n");
#endif
				glBlendFunc(gop_list[i].enum1, gop_list[i].enum2);
				break;

			case GLOP_COLOR4F:	
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "color4f %f, %f, %f, %f\n",
					gop_list[i].float1, gop_list[i].float2, gop_list[i].float3, gop_list[i].float4);
#endif
				glColor4f(gop_list[i].float1, gop_list[i].float2, gop_list[i].float3, gop_list[i].float4);
				break;

			case GLOP_VERTEX3F:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "vertex3f %f, %f, %f\n",
					gop_list[i].float1, gop_list[i].float2, gop_list[i].float3);
#endif
				glVertex3f(gop_list[i].float1, gop_list[i].float2, gop_list[i].float3);
				break;
				
			case GLOP_TEXCOORD2F:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "texcoord2f\n");
#endif
				glTexCoord2f(gop_list[i].float1, gop_list[i].float2);
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
				glTexImage2D(gop_list[i].enum1, gop_list[i].int1, gop_list[i].int2, gop_list[i].sizei1, gop_list[i].sizei2,
					gop_list[i].int3, gop_list[i].enum2, gop_list[i].enum3, gop_list[i].ptr1); break;
				break;
		}
	}

	glDisable(GL_DEPTH_TEST);

	gop_cnt = 0;
}
