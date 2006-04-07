#ifdef _decoder_h_
#define _decoder_h_


#define GET_RM_RN_NORMAL(x,y){\
	x =  (arg >> 8) & 0x0F;\
	y = (arg >> 4) & 0x0F;\ 
}

#define GET_SINGLE_REG_ARG(x){\
	x =  (arg >> 8) & 0x0F;\
}

#define GET_DWORD_DISP_SIGN_8(x){\
	x = SignExtend8(arg & 0x00FF)*2 + 4 + PC; \
}


#define GET_DWORD_DISP_SIGN_12(x){\
	x = SignExtend12(arg & 0x0FFF) * 2 + PC + 4;\
}

#define GET_IMM(x){\
 x =  arg & 0xFF;\
}

#define GET_FMOV_DP(x,y){\
   	x = (arg >> 9) & 0x07; \
	y = (arg >> 5) & 0x07;\
}

#define GET_FMOV_RM_DP(x,y){\
   	x = (arg >> 9) & 0x07; \
	y = (arg >> 4) & 0x0F;\
}

#define GET_FMOV_AT_REG_DP(x,y){\
   	x= (arg >> 8) & 0x0F;\
	y = (arg >> 5) & 0x07;\
}

#define GET_FIPR(x,y){\
	x = (arg >> 10) & 0x3 ; \
	y = (arg >> 8) & 0x3; \
}

#define GET_FRTV(x){ \
	x = (arg >> 10) & 0x3; \
}

#define GET_AT_FMOV_DP(x,y){	\
	   	x = (arg >> 9) & 0x07; \
		y  = (arg >> 4) & 0x0F; \
}

#define GET_FLOAT(x){\
	x = (arg >> 9) & 0x07;\
}

#define GET_MOV_DISP(x,y){\
	x = (arg >> 8) & 0x0F; \
	y  = SignExtend8(arg & 0xFF); \
}

#define GET_MOV_AT_DISP(x,y){\
	x = (arg >> 8) & 0x0F; \
	y = (arg & 0x00FF);\
}

#define GET_MOVL_WORD_DISP(x,y,z){\
	GET_MOV_AT_DISP(x,y) \
	z = y*4 + (PC & 0xFFFFFFFC) + 4;\
}

#define GET_MOV_BYTE_DISP(x,y){\
	x = (arg >> 4) & 0x0F;\
	y = arg & 0x0F;\
}

#define GET_MOV_DWORD_DISP(x,y,z){\
	x = (arg >> 8) & 0x0F;\
	y = (arg >> 4) & 0x0F;\
	z = arg & 0x0F;\
	z <<= 2;\
}

#define GET_MOVL_DISP_N(x,y,z){ \
	x = (arg >> 8) & 0x0F; \
	y = (arg >> 4) & 0x0F; \
	z = (arg & 0x0F); \
}


#endif