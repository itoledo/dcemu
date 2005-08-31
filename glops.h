#ifndef _GLOPS_H_
#define _GLOPS_H_

#define MAX_GLOPS 1000000

enum e_glOp { GLOP_BEGIN, GLOP_BINDTEXTURE, GLOP_CLEAR, GLOP_BLENDFUNC, GLOP_COLOR4F, GLOP_VERTEX3F, GLOP_TEXCOORD2F, GLOP_END, GLOP_TEXIMAGE2D };

typedef enum e_glOp e_glOp;
typedef struct s_glOp s_glOp;

// this is bloated.
struct s_glOp
{
	e_glOp		op;
	GLfloat		float1;
	GLfloat		float2;
	GLfloat		float3;
	GLfloat 	float4;
	GLenum		enum1;
	GLenum		enum2;
	GLenum		enum3;
	GLint		int1;
	GLint		int2;
	GLint		int3;
	GLbitfield	bitfield1;
	GLuint		uint1;
	GLvoid *	ptr1;
	GLsizei		sizei1;
	GLsizei		sizei2;
};

void glop_process();

extern s_glOp gop_list[MAX_GLOPS];
extern int gop_cnt;

#define GLOP_BEGIN(par1) { gop_list[gop_cnt].op = GLOP_BEGIN; gop_list[gop_cnt].enum1 = par1; gop_cnt++; }
#define GLOP_BINDTEXTURE(par1,par2) { gop_list[gop_cnt].op = GLOP_BINDTEXTURE; gop_list[gop_cnt].enum1 = par1; gop_list[gop_cnt].uint1 = par2; gop_cnt++; }
#define GLOP_CLEAR(par1) { gop_list[gop_cnt].op = GLOP_CLEAR; gop_list[gop_cnt].bitfield1 = par1; gop_cnt++; }
#define GLOP_BLENDFUNC(par1, par2) { gop_list[gop_cnt].op = GLOP_BLENDFUNC; gop_list[gop_cnt].enum1 = par1; gop_list[gop_cnt].enum2 = par2; gop_cnt++; }
#define GLOP_COLOR4F(par1, par2, par3, par4) { gop_list[gop_cnt].op = GLOP_COLOR4F; gop_list[gop_cnt].float1 = par1; gop_list[gop_cnt].float2 = par2; gop_list[gop_cnt].float3 = par3; gop_list[gop_cnt].float4 = par4; gop_cnt++; }
#define GLOP_VERTEX3F(par1, par2, par3) { gop_list[gop_cnt].op = GLOP_VERTEX3F; gop_list[gop_cnt].float1 = par1; gop_list[gop_cnt].float2 = par2; gop_list[gop_cnt].float3 = par3; gop_cnt++; }
#define GLOP_TEXCOORD2F(par1, par2) { gop_list[gop_cnt].op = GLOP_TEXCOORD2F; gop_list[gop_cnt].float1 = par1; gop_list[gop_cnt].float2 = par2; gop_cnt++; }
#define GLOP_END() { gop_list[gop_cnt++].op = GLOP_END; }
#define GLOP_TEXIMAGE2D(par1, par2, par3, par4, par5, par6, par7, par8, par9) { gop_list[gop_cnt].op = GLOP_TEXIMAGE2D; \
gop_list[gop_cnt].enum1 = par1; gop_list[gop_cnt].int1 = par2; gop_list[gop_cnt].int2 = par3; \
gop_list[gop_cnt].sizei1 = par4; gop_list[gop_cnt].sizei2 = par5; gop_list[gop_cnt].int3 = par6; \
gop_list[gop_cnt].enum2 = par7; gop_list[gop_cnt].enum3 = par8; gop_list[gop_cnt].ptr1 = par9; gop_cnt++; }

#endif // _GLOPS_H_
