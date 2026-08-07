/****************************************************************************

	FUSION - el prototipo desechable de bloques fusionados

	Fase 4 de docs/rendimiento-plan-2.md: medir el techo del recompilador
	traduciendo A MANO el lazo mas caliente del banco de Crazy Taxi -- el lazo
	de espera de 0c1583f8, que se lleva el 47 % de todas las instrucciones --
	como C fusionado: registros del SH-4 en locales, PC y ciclos por bloque,
	sin despacho por instruccion. MSVC asignando registros sobre ese cuerpo es
	el proxy de lo que emitiria un JIT.

	Es una sonda, no una optimizacion: vive detras de -DDCEMU_FUSION (apagada
	por omision, como DCEMU_BLOQUES y DCEMU_INLINE) y dentro del binario se
	elige con DCEMU_FUSION=1, para que el A/B corra sobre una sola imagen.

	La regla que la hace medible: **la ejecucion es identica al digito**. Los
	cortes del bloque periodico caen en las mismas fronteras de instruccion
	que en el interprete, los ciclos de cada instruccion son los de su
	manejador (incluida la ranura del RTS, un NOP que no suma), y todo lo que
	no es el camino caliente exacto -- el callback que no es el RTS conocido,
	una direccion desalineada, las salidas del lazo -- repone el estado y
	vuelve al interprete en esa misma instruccion.

*****************************************************************************/

#ifndef _FUSION_H_
#define _FUSION_H_

/* La entrada del lazo fusionado. Fisica por la ventana 0x0C, que es donde
   Crazy Taxi ejecuta (el anillo de PC lo muestra asi). */
#define FUSION_CT_ENTRADA	0x0C1583F8ul

/* Y la del bloque con MMU: el blit de columnas de DOOM, en el espacio de
   usuario de DCDOOM.EXE. Virtual: cada acceso traduce. */
#define FUSION_CE_ENTRADA	0x0002EF3Eul

extern int fusion_activa;

void fusion_iniciar(void);

/* Corre el lazo fusionado desde FUSION_CT_ENTRADA. Devuelve 1 si ejecuto al
   menos una instruccion (PC, ciclos y registros ya avanzados: main_loop()
   sigue derecho al bloque periodico) y 0 si la verificacion del codigo fallo
   y no toco nada (main_loop() despacha normal). */
int fusion_lazo_ct(void);

/* Idem para el bloque con MMU, desde FUSION_CE_ENTRADA. Corre con el salto de
   excepcion armado: si un acceso falta, el longjmp sale por adentro y el
   contexto ya lleva el estado pre-instruccion exacto (ver fusion.c). */
int fusion_bloque_ce(void);

#endif /* _FUSION_H_ */
