#include "main.h"
#include "log.h"
#include "graficos.h"
#include "glops.h"

#ifdef VERTEX_PROCESS
struct pntr
{
	int vertexpos;
	int texcoord;
};

typedef struct pntr pntr;
#endif

list gop_list;

// #define DEBUG_GLOP

char * enable(GLenum bit)
{
	switch(bit)
	{
		case GL_CULL_FACE:				return "GL_CULL_FACE";				break;
		case GL_BLEND:					return "GL_BLEND";					break;
	}
	
	return "ERROR";
}

char * blendfunc(GLenum func)
{
	switch(func)
	{
		case GL_ZERO:					return "GL_ZERO";
		case GL_ONE:					return "GL_ONE";
		case GL_DST_COLOR:				return "GL_DST_COLOR";
		case GL_ONE_MINUS_DST_COLOR:	return "GL_ONE_MINUS_DST_COLOR";	break;
		case GL_SRC_ALPHA:				return "GL_SRC_ALPHA";				break;
		case GL_ONE_MINUS_SRC_ALPHA:	return "GL_ONE_MINUS_SRC_ALPHA";	break;
		case GL_DST_ALPHA:				return "GL_DST_ALPHA";				break;
		case GL_ONE_MINUS_DST_ALPHA:	return "GL_ONE_MINUS_DST_ALPHA";	break;
	}
	
	return "ERROR";
}

char * depthfunc(GLenum func)
{
	switch(func)
	{
		case GL_NEVER:					return "GL_NEVER";					break;
		case GL_LESS:					return "GL_LESS";					break;
		case GL_EQUAL:					return "GL_EQUAL";					break;
		case GL_LEQUAL:					return "GL_LEQUAL";					break;
		case GL_GREATER:				return "GL_GREATER";				break;
		case GL_NOTEQUAL:				return "GL_NOTEQUAL";				break;
		case GL_GEQUAL:					return "GL_GEQUAL";					break;
		case GL_ALWAYS:					return "GL_ALWAYS";					break;
	}
	
	return "ERROR";
}

char * depthmask(GLenum mask)
{
	switch(mask)
	{
		case GL_TRUE:					return "GL_TRUE";					break;
		case GL_FALSE:					return "GL_FALSE";					break;
	}
	
	return "ERROR";
}

void init_sOglP()
{
	gop_list.current = (node *) malloc(sizeof(node));
	gop_list.current->fill = 0;
	gop_list.first = gop_list.last = gop_list.current;
}

void incr_sOglP()
{
gop_list.current->fill++;
// gop_cnt++;
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
	int gop_cnt = 0;
	bool fin = false;

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glDepthMask(GL_TRUE);
	
#ifdef VERTEX_PROCESS
	int vertex_cnt = 0, begin = 0;
	pntr p[4];
	s_glOp vert1, vert3;
	s_glOp tex1, tex3;
	bool texture = false;

	// hagamos un pequeño preproceso de los vertexes, para probar...
	for (i = 0; i < gop_cnt; i++)
	{
		switch(gop_list[i].op)
		{
			case GLOP_BEGIN: begin = i; vertex_cnt = 0; texture = false; break;
			case GLOP_END:
				if (vertex_cnt == 4)
				{
					gop_list[begin].enum1 = GL_QUADS;

/*					// 3, 0, 2, 1
					memcpy(&vert1, &gop_list[p[1].vertexpos], sizeof(s_glOp));
					memcpy(&vert3, &gop_list[p[3].vertexpos], sizeof(s_glOp));
					
					memcpy(&gop_list[p[3].vertexpos], &gop_list[p[0].vertexpos], sizeof(s_glOp));
					// hasta acá estamos 3 0 2 3

					memcpy(&gop_list[p[0].vertexpos], &vert1, sizeof(s_glOp));
					// hasta acá estamos 0 0 2 3
					
					memcpy(&gop_list[p[1].vertexpos], &vert3, sizeof(s_glOp));
					// al final tenemos 0 1 2 3

					if (texture)
					{
						memcpy(&tex1, &gop_list[p[1].texcoord], sizeof(s_glOp));
						memcpy(&tex3, &gop_list[p[3].texcoord], sizeof(s_glOp));
						memcpy(&gop_list[p[3].texcoord], &gop_list[p[0].texcoord], sizeof(s_glOp));
						memcpy(&gop_list[p[0].texcoord], &tex1, sizeof(s_glOp));
						memcpy(&gop_list[p[1].texcoord], &tex3, sizeof(s_glOp));
					} */

					// la DC los manda en este orden: 0 3 1 2
					memcpy(&vert1, &gop_list[p[1].vertexpos], sizeof(s_glOp));
					memcpy(&vert3, &gop_list[p[3].vertexpos], sizeof(s_glOp));
					
					memcpy(&gop_list[p[1].vertexpos], &gop_list[p[2].vertexpos], sizeof(s_glOp));
					// hasta acá estamos 0 1 1 2

					memcpy(&gop_list[p[2].vertexpos], &vert3, sizeof(s_glOp));
					// hasta acá estamos 0 1 2 2
					
					memcpy(&gop_list[p[3].vertexpos], &vert1, sizeof(s_glOp));
					// al final tenemos 0 1 2 3

					if (texture)
					{
						memcpy(&tex1, &gop_list[p[1].texcoord], sizeof(s_glOp));
						memcpy(&tex3, &gop_list[p[3].texcoord], sizeof(s_glOp));
						memcpy(&gop_list[p[3].texcoord], &gop_list[p[0].texcoord], sizeof(s_glOp));
						memcpy(&gop_list[p[0].texcoord], &tex1, sizeof(s_glOp));
						memcpy(&gop_list[p[1].texcoord], &tex3, sizeof(s_glOp));
					}
				}
				break;
				
			case GLOP_TEXCOORD2F:
				texture = true;
				if (vertex_cnt < 4)
				{
					p[vertex_cnt].texcoord = i;
				}
				break;
				
			case GLOP_VERTEX3F:
				if (vertex_cnt < 4)
				{
					p[vertex_cnt].vertexpos = i;
				}
				vertex_cnt++;
				break;
		}
	}
#endif

	ptr = gop_list.first;

	while (fin == false)
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
				logxmsg(LOG_GLOP, "blendfunc: %s, %s\n", blendfunc(ptr->bar[i].enum1), blendfunc(ptr->bar[i].enum2));
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
				glVertex3f(ptr->bar[i].float1, ptr->bar[i].float2, 1.0 / ptr->bar[i].float3);
				break;
					
				case GLOP_TEXCOORD2F:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "texcoord2f\n");
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
			
				case GLOP_ENABLE:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "enable: %s\n", enable(ptr->bar[i].enum1));
#endif
				glEnable(ptr->bar[i].enum1);
				break;

				case GLOP_CULL_FACE:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "enable: %s\n", enable(ptr->bar[i].enum1));
#endif
				glCullFace(ptr->bar[i].enum1);
				break;

				case GLOP_DISABLE:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "disable: %s\n", enable(ptr->bar[i].enum1));
#endif
				glDisable(ptr->bar[i].enum1);
				break;

				case GLOP_DEPTHFUNC:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "depthfunc: %s\n", depthfunc(ptr->bar[i].enum1));
#endif
				glDepthFunc(ptr->bar[i].enum1);
				break;

				case GLOP_DEPTHMASK:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "depthmask: %s\n", depthmask(ptr->bar[i].int1));
#endif
				glDepthMask(ptr->bar[i].int1);
				break;
				
				case GLOP_TEXPARAMETERI:
#ifdef DEBUG_GLOP
				logxmsg(LOG_GLOP, "texparameteri %d, %d, %d\n",
					ptr->bar[i].enum1, ptr->bar[i].enum2, ptr->bar[i].int1);
#endif
				glTexParameteri(ptr->bar[i].enum1, ptr->bar[i].enum2, ptr->bar[i].int1);
				break;
			}
			gop_cnt++;
		}
		if (ptr == gop_list.last)
			fin = true;
		else
			ptr = ptr->next;
	}

	glDisable(GL_DEPTH_TEST);
	reset_sOglP();

	logxmsg(LOG_GLOP, "glops: %d eventos procesados.\n", gop_cnt);
}
