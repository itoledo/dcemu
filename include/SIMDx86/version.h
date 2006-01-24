/*
	version.h -- SIMDx86 Version Information
	Written by Patrick Baggett, 2005 (baggett.patrick@gmail.com)
	Under LGPL License
	Part of SIMDx86 Project
*/

#ifndef _SIMDX86_VERSION_H
#define _SIMDX86_VERSION_H

/* Compile Time Constants for v0.2.1 */
#define SIMDX86_VERSION_MAJOR		0
#define SIMDX86_VERSION_MINOR		2
#define SIMDX86_VERSION_REVISION	1


#ifdef __cplusplus
extern "C" {
#endif

/* Runtime (Linkage) Constants for v0.2 */
extern int SIMDx86_GetMajorVersion();
extern int SIMDx86_GetMinorVersion();
extern int SIMDx86_GetRevisionVersion();
extern const char* SIMDx86_GetBuildString();

#ifdef __cplusplus
}
#endif

#endif
