/****************************************************************************

	MANDO - ver mando.h.

*****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "mando.h"

void mando_reposo(struct mando_estado_t * dest)
{
	dest->botones   = 0xFFFF;		/* activos en bajo: nada pulsado */
	dest->ltrig     = TRIGGER_OFF;
	dest->rtrig     = TRIGGER_OFF;
	dest->joyx      = JOYSTICK_NEUTRAL;
	dest->joyy      = JOYSTICK_NEUTRAL;
	dest->conectado = 0;
}

#ifdef WIN32

/* ------------------------------------------------------------------------ */
/* XInput                                                                   */
/* ------------------------------------------------------------------------ */

/*
	Las estructuras y constantes de XInput, copiadas de <xinput.h> para no
	depender de que el SDK este instalado. La ABI de XINPUT_STATE no ha
	cambiado desde la primera version.
*/

#define XI_DPAD_UP			0x0001
#define XI_DPAD_DOWN		0x0002
#define XI_DPAD_LEFT		0x0004
#define XI_DPAD_RIGHT		0x0008
#define XI_START			0x0010
#define XI_BACK				0x0020
#define XI_A				0x1000
#define XI_B				0x2000
#define XI_X				0x4000
#define XI_Y				0x8000

/* La zona muerta del stick izquierdo que recomienda la propia documentacion de
   XInput. Sin ella un mando gastado manda al personaje a caminar solo. */
#define XI_ZONA_MUERTA		7849

typedef struct
{
	WORD	wButtons;
	BYTE	bLeftTrigger;
	BYTE	bRightTrigger;
	SHORT	sThumbLX;
	SHORT	sThumbLY;
	SHORT	sThumbRX;
	SHORT	sThumbRY;
} XI_GAMEPAD;

typedef struct
{
	DWORD		dwPacketNumber;
	XI_GAMEPAD	Gamepad;
} XI_STATE;

typedef DWORD (WINAPI * xi_get_state_t)(DWORD, XI_STATE *);

static HMODULE			xi_dll = NULL;
static xi_get_state_t	xi_get_state = NULL;

/* Cual de los cuatro puertos respondio la ultima vez. Empezar por ese evita
   preguntar por los cuatro en cada cuadro. */
static DWORD			xi_puerto = 0;

int mando_iniciar(void)
{
	/*
		Las tres DLL que puede haber, de la mas nueva a la mas vieja.
		xinput1_4 viene con Windows 8 en adelante; xinput9_1_0 esta en todos
		desde Vista y es la que asegura que esto funcione en cualquier maquina.
	*/
	static const char * nombres[] =
		{ "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };

	int i;

	if (xi_get_state != NULL)
		return 1;

	for (i = 0; i < 3 && xi_dll == NULL; i++)
		xi_dll = LoadLibraryA(nombres[i]);

	if (xi_dll == NULL)
	{
		fprintf(stderr, "mando: XInput no esta disponible, solo teclado.\n");
		return 0;
	}

	xi_get_state = (xi_get_state_t) GetProcAddress(xi_dll, "XInputGetState");

	if (xi_get_state == NULL)
	{
		FreeLibrary(xi_dll);
		xi_dll = NULL;
		fprintf(stderr, "mando: XInput sin XInputGetState, solo teclado.\n");
		return 0;
	}

	return 1;
}

void mando_terminar(void)
{
	if (xi_dll != NULL)
		FreeLibrary(xi_dll);

	xi_dll = NULL;
	xi_get_state = NULL;
}

/* Un eje de XInput (-32768..32767) al del Dreamcast (0..255, centro 128). */
static BYTE eje_a_dc(SHORT v, int invertir)
{
	int escalado;

	if (v > -XI_ZONA_MUERTA && v < XI_ZONA_MUERTA)
		return JOYSTICK_NEUTRAL;

	if (invertir)
		v = (SHORT) ((v == -32768) ? 32767 : -v);

	/* De -32768..32767 a 0..255. El +32768 antes de dividir mantiene el centro
	   en 128 en vez de en 127. */
	escalado = ((int) v + 32768) >> 8;

	if (escalado < 0)		escalado = 0;
	if (escalado > 255)		escalado = 255;

	return (BYTE) escalado;
}

int mando_leer(struct mando_estado_t * dest)
{
	XI_STATE	estado;
	DWORD		i, puerto;
	WORD		b;

	mando_reposo(dest);

	if (xi_get_state == NULL)
		return 0;

	/* Se empieza por el puerto que contesto la vez pasada. */
	for (i = 0; i < 4; i++)
	{
		puerto = (xi_puerto + i) & 3;

		memset(&estado, 0, sizeof(estado));

		if (xi_get_state(puerto, &estado) == 0)	/* ERROR_SUCCESS */
		{
			xi_puerto = puerto;
			break;
		}
	}

	if (i == 4)
		return 0;

	b = estado.Gamepad.wButtons;

	/*
		Los bits del Dreamcast son activos en bajo, asi que se parte de todo en
		uno y se baja lo que este pulsado.

		A y B quedan donde el usuario los espera: el boton de abajo del mando es
		el A del Dreamcast, que es el de confirmar en la BIOS y en casi todos
		los juegos. X e Y se cruzan por la misma razon -- la disposicion fisica
		de los cuatro botones es la misma, pero las etiquetas de Xbox y de
		Dreamcast no coinciden.
	*/
	dest->botones = 0xFFFF;

	if (b & XI_A)			dest->botones &= (WORD) ~CONT_A;
	if (b & XI_B)			dest->botones &= (WORD) ~CONT_B;
	if (b & XI_X)			dest->botones &= (WORD) ~CONT_X;
	if (b & XI_Y)			dest->botones &= (WORD) ~CONT_Y;
	if (b & XI_START)		dest->botones &= (WORD) ~CONT_START;

	if (b & XI_DPAD_UP)		dest->botones &= (WORD) ~CONT_DPAD_UP;
	if (b & XI_DPAD_DOWN)	dest->botones &= (WORD) ~CONT_DPAD_DOWN;
	if (b & XI_DPAD_LEFT)	dest->botones &= (WORD) ~CONT_DPAD_LEFT;
	if (b & XI_DPAD_RIGHT)	dest->botones &= (WORD) ~CONT_DPAD_RIGHT;

	/* Los gatillos son analogicos en las dos consolas y tienen el mismo rango,
	   asi que pasan tal cual. */
	dest->ltrig = estado.Gamepad.bLeftTrigger;
	dest->rtrig = estado.Gamepad.bRightTrigger;

	/* El eje Y va al reves: en XInput arriba es positivo y en el Dreamcast
	   arriba es 0. */
	dest->joyx = eje_a_dc(estado.Gamepad.sThumbLX, 0);
	dest->joyy = eje_a_dc(estado.Gamepad.sThumbLY, 1);

	dest->conectado = 1;

	return 1;
}

#else /* !WIN32 */

int mando_iniciar(void)
{
	return 0;
}

void mando_terminar(void)
{
}

int mando_leer(struct mando_estado_t * dest)
{
	mando_reposo(dest);

	return 0;
}

#endif
