/*
	SIMDx86.h -- Main include for SIMDx86
	Written by Patrick Baggett, 2005 (baggett.patrick@gmail.com)
	Under LGPL License
	Part of SIMDx86 Project
*/

#ifndef _SIMDX86_SIMDX86_H
#define _SIMDX86_SIMDX86_H

#include <SIMDx86/consts.h> /* Constants */
#include <SIMDx86/align.h> /* ALIGNED keyword */

#include <SIMDx86/math.h> /* Math routines */
#include <SIMDx86/matrix.h> /* Matrix routines */
#include <SIMDx86/vector.h> /* Vector routines */
#include <SIMDx86/quaternion.h> /* Quaternion routines */

#define SIMDx86_emms()	asm("emms\n")
#define SIMDx86_femms() asm("femms\n")

#endif
