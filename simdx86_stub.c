/*
	simdx86_stub.c -- reemplazo en C plano de la parte de SIMDx86 que usa dcemu.

	Las bibliotecas de lib/win32 y lib/linux son archivos .a de MinGW/GCC, que el
	linker de MSVC no puede leer, y las cabeceras originales traen ensamblador
	inline con sintaxis de GCC. La superficie realmente usada son seis simbolos,
	asi que se implementan aqui.

	SIMDx86Vector_Dot4 y SIMDx86Matrix_Vector4Multiply ya no las llama nadie:
	fipr y ftrv hacen la cuenta en floatgraph.c para poder aplicar FPSCR.DN a
	cada operando. Se dejan con la misma semantica que aquellas -- acumulador en
	doble y una sola redondeada, como pide el manual del SH-4 en 6.4 -- para que
	el stub no mienta sobre lo que reemplaza.
*/

#include <math.h>

#include <SIMDx86/math.h>
#include <SIMDx86/vector.h>
#include <SIMDx86/matrix.h>
#include <SIMDx86/version.h>

float SIMDx86_sqrtf(float value)
{
	return (float) sqrt(value);
}

float SIMDx86_rsqrtf(float value)
{
	return (float) (1.0 / sqrt(value));
}

double SIMDx86_sqrt(double value)
{
	return sqrt(value);
}

double SIMDx86_rsqrtd(double value)
{
	return 1.0 / sqrt(value);
}

/*
	El acumulador va en doble y se redondea una sola vez al final. Es lo que
	pide el manual del SH-4 (6.4: "rounding is performed when generating the
	final operation result from the intermediate result"), y ademas evita un
	falso NaN: con productos parciales que desbordan a infinito en simple,
	sumar +inf con -inf da NaN donde el chip entrega un infinito. Diez de las
	500 pruebas de FIPR de SingleStepTests y cincuenta de las de FTRV caian
	justo ahi.
*/
float SIMDx86Vector_Dot4(const float* pSrc4D1, const float* pSrc4D2)
{
	return (float) ((double) pSrc4D1[0] * (double) pSrc4D2[0] +
					(double) pSrc4D1[1] * (double) pSrc4D2[1] +
					(double) pSrc4D1[2] * (double) pSrc4D2[2] +
					(double) pSrc4D1[3] * (double) pSrc4D2[3]);
}

/* Multiplica el vector por la matriz, en sitio. La matriz esta en el mismo orden
   que XMTRX del SH-4:

	m[0]	m[4]	m[8]	m[12]
	m[1]	m[5]	m[9]	m[13]
	m[2]	m[6]	m[10]	m[14]
	m[3]	m[7]	m[11]	m[15]
*/
void SIMDx86Matrix_Vector4Multiply(float* pOut4D, const SIMDx86Matrix* pIn)
{
	const float * m = pIn->m;
	double v0 = pOut4D[0], v1 = pOut4D[1], v2 = pOut4D[2], v3 = pOut4D[3];

	/* En doble y una sola redondeada, por lo mismo que SIMDx86Vector_Dot4. */
	pOut4D[0] = (float) (m[0] * v0 + m[4] * v1 + m[8]  * v2 + m[12] * v3);
	pOut4D[1] = (float) (m[1] * v0 + m[5] * v1 + m[9]  * v2 + m[13] * v3);
	pOut4D[2] = (float) (m[2] * v0 + m[6] * v1 + m[10] * v2 + m[14] * v3);
	pOut4D[3] = (float) (m[3] * v0 + m[7] * v1 + m[11] * v2 + m[15] * v3);
}

int SIMDx86_GetMajorVersion()
{
	return SIMDX86_VERSION_MAJOR;
}

int SIMDx86_GetMinorVersion()
{
	return SIMDX86_VERSION_MINOR;
}

int SIMDx86_GetRevisionVersion()
{
	return SIMDX86_VERSION_REVISION;
}

const char* SIMDx86_GetBuildString()
{
	return "C plano (stub)";
}
