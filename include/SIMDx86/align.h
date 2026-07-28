/*
	align.h -- Defines ALIGNED for SIMDx86 structures to be aligned.

*/

#ifndef _SIMDX86_ALIGNED_H
#define _SIMDX86_ALGINED_H

/* MSVC no tiene un equivalente en posicion trailing (__declspec(align) va
   antes del tipo). Como el SIMD se reemplaza por C plano, queda vacio. */
#ifdef _MSC_VER
#define ALIGNED
#else
#define ALIGNED __attribute__((  aligned(16)  ))
#endif

#endif
