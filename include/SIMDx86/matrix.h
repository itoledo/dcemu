/*
	matrix.h -- SIMDx86 Matrix Library
	Written by Patrick Baggett, 2005 (baggett.patrick@gmail.com)
	Under LGPL License
	Part of SIMDx86 Project
*/

#ifndef _SIMDX86_MATRIX_H
#define _SIMDX86_MATRIX_H

#include <SIMDx86/align.h>
#include <SIMDx86/vector.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SIMDx86Matrix
{
	float m[16];
} SIMDx86Matrix;

#define TOSIMDX86MATRIX( ptr ) 	((SIMDx86Matrix*)(ptr))

void SIMDx86Matrix_Sum(SIMDx86Matrix* pMat, const SIMDx86Matrix* pIn);
void SIMDx86Matrix_SumOf(SIMDx86Matrix* pMat, const SIMDx86Matrix* pIn1, const SIMDx86Matrix* pIn2);
void SIMDx86Matrix_Diff(SIMDx86Matrix* pMat, const SIMDx86Matrix* pIn);
void SIMDx86Matrix_DiffOf(SIMDx86Matrix* pMat, const SIMDx86Matrix* pLeft, const SIMDx86Matrix* pRight);
void SIMDx86Matrix_Scale(SIMDx86Matrix* pMat, float scalar);
void SIMDx86Matrix_ScaleOf(SIMDx86Matrix* pOut, const SIMDx86Matrix* pIn, float scalar);
void SIMDx86Matrix_Multiply(SIMDx86Matrix* pLeft, const SIMDx86Matrix* pRight);
void SIMDx86Matrix_MultiplyOf(SIMDx86Matrix* pOut, const SIMDx86Matrix* pLeft, const SIMDx86Matrix* pRight);
void SIMDx86Matrix_Transpose(SIMDx86Matrix* pIn);
void SIMDx86Matrix_TransposeOf(SIMDx86Matrix* pOut, const SIMDx86Matrix* pIn);
void SIMDx86Matrix_VectorMultiply(SIMDx86Vector* pOut, const SIMDx86Matrix* pIn);
void SIMDx86Matrix_VectorMultiplyOf(SIMDx86Vector* pOut, const SIMDx86Vector* pIn, const SIMDx86Matrix* pMat);
void SIMDx86Matrix_Vector4Multiply(float* pOut4D, const SIMDx86Matrix* pIn);
void SIMDx86Matrix_Vector4MultiplyOf(float* pOut4D, const float* pIn4D, const SIMDx86Matrix* pMat);

#ifdef __cplusplus
}
#endif


#endif
