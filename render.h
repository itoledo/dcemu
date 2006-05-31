#ifndef _render_h_
#define _render_h_

// some typedefs
typedef struct registers_ta  taReg;
typedef struct registers_tex texReg;
typedef struct registers_r  rdReg; 
typedef struct blit blit;

struct registers_ta
{
	// Obj Control
	unsigned pcw_16bit_UV:1;
	unsigned pcw_gouraud:1;
	unsigned pcw_offset:1;
	unsigned pcw_texture:1;
	unsigned pcw_col_type:2;
	unsigned pcw_volume:1;
	unsigned pcw_shadow:1;
	unsigned unk2:8;
	
	// Group Control	
	unsigned pcw_user_clip:2;
	unsigned pcw_strip_len:2;	
	unsigned pcw_unk1:3;
	unsigned pcw_group_en:1;
	
	// Para Control
	unsigned pcw_list_type:3;
	unsigned pcw_unk:1;
	unsigned pcw_end_of_strip:1;
	unsigned pcw_para_type:3;
};

struct registers_tex
{
		unsigned texture_surface:21; // we have to shift this value by 3
		unsigned unk:4;	
		unsigned stride:1;
		unsigned twiddled:1;
		unsigned pixelformat:3;
		unsigned vq:1;
		unsigned mipmap:1;
};

struct registers_r
{
		unsigned unk:20;
		unsigned dcalcctrl:1;
		unsigned cachebypass:1;
		unsigned uv16bit:1;
		unsigned gourad:1;
		unsigned offset:1;
		unsigned texture:1;
		unsigned zwrite:1;
		unsigned cullingmode:2;
		unsigned depthmode:3;
};

typedef union
{
	taReg registers;
	DWORD control;
}tile_accell;

typedef union
{
 	texReg registers;
	DWORD texture;
}texture_info;

typedef union
{
	rdReg registers;
	DWORD control;
}renderer_control;

// vertex parameters 
//  int vertex_param [ ] = {0,0,0,1,0,2,1,2,0,2,3,9,4,10,3,10,0,3,0,4};

/*  Blit info.Contains the framebuffer format and other info */
struct blit
{
	GLenum blit_format;
	GLenum blit_type;
};


float coords[5];
float colors[4];
float data[7];
DWORD base_colour;

#endif
