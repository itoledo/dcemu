#ifndef _GLOPS_H_
#define _GLOPS_H_

// enum e_glOp { GLOP_BEGIN, GLOP_BINDTEXTURE, GLOP_CLEAR, GLOP_BLENDFUNC, GLOP_COLOR4F, GLOP_VERTEX3F, GLOP_TEXCOORD2F, GLOP_END, GLOP_TEXIMAGE2D, GLOP_ENABLE, GLOP_DISABLE, GLOP_DEPTHFUNC, GLOP_DEPTHMASK, GLOP_TEXPARAMETERI };
enum e_glOp { GLOP_BEGIN, GLOP_BINDTEXTURE, GLOP_CLEAR, GLOP_BLENDFUNC, GLOP_COLOR4F, GLOP_VERTEX3F, GLOP_TEXCOORD2F, GLOP_END, GLOP_TEXIMAGE2D, GLOP_CULL_FACE, GLOP_ENABLE, GLOP_DISABLE, GLOP_DEPTHFUNC, GLOP_DEPTHMASK, GLOP_TEXPARAMETERI };

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


// dynamic data type
typedef struct node node;

struct node 
{
	s_glOp bar [400];
	int fill;
	struct node * next;
};

typedef struct
{
	node * first;
	node * current;
	node * last;
}list;

// dynamic data type handling function
void init_sOglP();
void incr_sOglP();
void reset_sOglP();

// the list
extern list gop_list;

#define GLOP_BEGIN(par1) { gop_list.current->bar[gop_list.current->fill].op = GLOP_BEGIN;  gop_list.current->bar[gop_list.current->fill ].enum1 = par1; incr_sOglP(gop_list); }

#define GLOP_BINDTEXTURE(par1,par2) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_BINDTEXTURE;  gop_list.current->bar[gop_list.current->fill ].enum1 = par1; gop_list.current->bar[gop_list.current->fill  ].uint1    = par2; incr_sOglP(gop_list); }

#define GLOP_CLEAR(par1) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_CLEAR;  gop_list.current->bar[gop_list.current->fill ].bitfield1 = par1; incr_sOglP(gop_list); }

#define GLOP_BLENDFUNC(par1, par2) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_BLENDFUNC;  gop_list.current->bar[gop_list.current->fill ].enum1 = par1; gop_list.current->bar[gop_list.current->fill ].enum2  = par2; incr_sOglP(gop_list); }

#define GLOP_COLOR4F(par1, par2, par3, par4) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_COLOR4F;  gop_list.current->bar[gop_list.current->fill ].float1 = par1; gop_list.current->bar[gop_list.current->fill  ].float2 = par2; gop_list.current->bar[gop_list.current->fill ].float3 = par3;  gop_list.current->bar[gop_list.current->fill ].float4 = par4; incr_sOglP(gop_list); }

#define GLOP_VERTEX3F(par1, par2, par3) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_VERTEX3F;  gop_list.current->bar[gop_list.current->fill].float1 = par1; gop_list.current->bar[gop_list.current->fill  ].float2 = par2; gop_list.current->bar[gop_list.current->fill ].float3 = par3; incr_sOglP(gop_list); }

#define GLOP_TEXCOORD2F(par1, par2) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_TEXCOORD2F;  gop_list.current->bar[gop_list.current->fill ].float1 = par1; gop_list.current->bar[gop_list.current->fill  ].float2 = par2; incr_sOglP(gop_list); }

#define GLOP_END() { gop_list.current->bar[gop_list.current->fill].op = GLOP_END;incr_sOglP(gop_list);}

#define GLOP_TEXIMAGE2D(par1, par2, par3, par4, par5, par6, par7, par8, par9) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_TEXIMAGE2D; \
gop_list.current->bar[gop_list.current->fill ].enum1 = par1; \
gop_list.current->bar[gop_list.current->fill ].int1 = par2; \
gop_list.current->bar[gop_list.current->fill ].int2 = par3; \
gop_list.current->bar[gop_list.current->fill ].sizei1 = par4; \
 gop_list.current->bar[gop_list.current->fill ].sizei2 = par5; \
 gop_list.current->bar[gop_list.current->fill ].int3 = par6; \
gop_list.current->bar[gop_list.current->fill ].enum2 = par7;\
 gop_list.current->bar[gop_list.current->fill ].enum3 = par8; \
 gop_list.current->bar[gop_list.current->fill ].ptr1 = par9; incr_sOglP(gop_list); }

#define GLOP_ENABLE(par1) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_ENABLE; \
gop_list.current->bar[gop_list.current->fill ].enum1 = par1; \
incr_sOglP(gop_list); }

#define GLOP_CULL_FACE(par1) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_CULL_FACE; \
gop_list.current->bar[gop_list.current->fill ].enum1 = par1; \
incr_sOglP(gop_list); }

#define GLOP_DISABLE(par1) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_DISABLE; \
gop_list.current->bar[gop_list.current->fill ].enum1 = par1; \
incr_sOglP(gop_list); }

#define GLOP_DEPTHFUNC(par1) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_DEPTHFUNC; \
gop_list.current->bar[gop_list.current->fill ].enum1 = par1; \
incr_sOglP(gop_list); }

#define GLOP_DEPTHMASK(par1) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_DEPTHMASK; \
gop_list.current->bar[gop_list.current->fill ].int1 = par1; \
incr_sOglP(gop_list); }

#define GLOP_TEXPARAMETERI(par1, par2, par3) { gop_list.current->bar[gop_list.current->fill ].op = GLOP_TEXPARAMETERI; \
gop_list.current->bar[gop_list.current->fill ].enum1 = par1; \
gop_list.current->bar[gop_list.current->fill ].enum2 = par2; \
gop_list.current->bar[gop_list.current->fill ].int1 = par3; \
incr_sOglP(gop_list); }

#endif // _GLOPS_H_
