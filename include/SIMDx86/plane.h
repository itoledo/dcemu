/*
	plane.h -- SIMDx86 Plane Library
	Written by Patrick Baggett, 2005 (baggett.patrick@gmail.com)
	Under LGPL License
	Part of SIMDx86 Project
*/

#ifndef _SIMDX86_PLANE_H
#define _SIMDX86_PLANE_H

#include <SIMDx86/align.h>
#include <SIMDx86/vector.h>
#include <SIMDx86/polygon.h>
#include <SIMDx86/sphere.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
	Plane equation: ax + by + cz = d
	Or: ax + by + cz - d = 0

	Since d is the distance, does the sign of d matter? eg: ax+by+cz+d vs ax+by+cz-d ?
*/
typedef struct SIMDx86Plane
{
	SIMDx86Vector abc;
	float d;			/* distance from the axis, d */
	float __SIMD_pad___[3];
} SIMDx86Plane ALIGNED;

/* Results of the ClassifyXXXXX() functions are either -1, 0, or 1, meaning... */
#define PLANE_BEHIND		(-1)
#define PLANE_INTERSECT		(0)
#define PLANE_INFRONT		(1)

void SIMDx86Plane_FromPolygon(SIMDx86Plane* pOut, const SIMDx86Polygon* pPoly);
void SIMDx86Plane_FromPoints(SIMDx86Plane* pOut, const SIMDx86Vector* pA, const SIMDx86Vector* pB,const SIMDx86Vector* pC);
int SIMDx86Plane_ClassifyPoint(const SIMDx86Plane* pPlane, const SIMDx86Vector* pPoint);
int SIMDx86Plane_ClassifyPolygon(const SIMDx86Plane* pPlane, const SIMDx86Polygon* pPoly);
int SIMDx86Plane_ClassifySphere(const SIMDx86Plane* pPlane, const SIMDx86Sphere* pSphere);
float SIMDx86Plane_DistToPoint(const SIMDx86Plane* pPlane, const SIMDx86Vector* pPoint);
float SIMDx86Plane_DistToPolygon(const SIMDx86Plane* pPlane, const SIMDx86Polygon* pPoly);
float SIMDx86Plane_DistToSphere(const SIMDx86Plane* pPlane, const SIMDx86Sphere* pSphere);
void SIMDx86Plane_Normalize(SIMDx86Plane* pOut);
void SIMDx86Plane_NormalizeOf(SIMDx86Plane* pOut, SIMDx86Plane* pIn);


#ifdef __cplusplus
}
#endif

#endif
