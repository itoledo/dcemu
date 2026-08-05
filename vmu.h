/*
	vmu.h -- la Visual Memory en la ranura 1 del mando del puerto A.

	Es la tarjeta de memoria: 128 KB de flash en 256 bloques de 512 bytes,
	con el sistema de archivos que el boot ROM y los juegos esperan (bloque
	raiz en el 255, FAT en el 254, directorio del 253 al 241, 200 bloques de
	usuario). Sin ella, medio catalogo se para en la pantalla de "no memory
	card" antes de llegar al juego.

	El modulo habla el protocolo Maple de la funcion de almacenamiento tal
	como lo usa el driver de KOS (kernel/arch/dreamcast/hardware/maple/vmu.c,
	que es la referencia de los formatos de cable):

	  - DEVINFO (1)   -> 5, el descriptor de una Visual Memory de serie
	  - GETCOND (9)   -> 8, los botones de la VMU (funcion reloj)
	  - GETMINFO (10) -> 8, la geometria de la tarjeta
	  - BREAD (11)    -> 8, un bloque entero de 512 bytes en una fase
	  - BWRITE (12)   -> 7, un cuarto de bloque por fase: 4 fases de 128
	  - BSYNC (13)    -> 7, y es el momento en que se persiste al archivo

	El blkid de BREAD/BWRITE/BSYNC lleva el bloque partido en bytes y la fase:
	((bloque & 0xFF) << 24) | ((bloque >> 8) << 16) | (fase << 8) | particion.

	Las funciones LCD y reloj se declaran en el DEVINFO --los juegos las
	esperan de una VMU de verdad-- y se aceptan sin hacer nada: el dibujo del
	LCD contesta OK y se descarta, el reloj contesta una fecha fija. Lo unico
	con estado es el almacenamiento.

	La imagen persiste como la flash (sistema.c): se carga al arrancar, se
	marca sucia al escribir, y vuelve al archivo en BSYNC y al salir. Si el
	archivo no existe se formatea una tarjeta vacia. Con ruta NULL vive solo
	en memoria, que es lo que usan las pruebas.
*/

#ifndef _VMU_H_
#define _VMU_H_

#include <stddef.h>

#define VMU_BLOQUES			256
#define VMU_BLOQUE_TAM		512
#define VMU_TAM				(VMU_BLOQUES * VMU_BLOQUE_TAM)

/* La geometria que declara el bloque raiz y GETMINFO, identica en los dos
   sitios a proposito: un guest puede leer cualquiera de ellos. */
#define VMU_BLOQUE_RAIZ		255
#define VMU_BLOQUE_FAT		254
#define VMU_BLOQUE_DIR		253		/* crece hacia abajo, 13 bloques */
#define VMU_DIR_BLOQUES		13
#define VMU_BLOQUES_USUARIO	200

/* Marcas de la FAT (16 bits, little endian). */
#define VMU_FAT_LIBRE		0xFFFC
#define VMU_FAT_ULTIMO		0xFFFA

/* 0 si cargo o formateo bien. Con ruta NULL no toca el disco. */
int vmu_iniciar(const char * ruta);

/* Vuelca la imagen al archivo si hay cambios. La llaman BSYNC y la salida. */
void vmu_guardar(void);

/* 1 si hay tarjeta: decide el bit de subdispositivo del AP del mando y el
   despacho de los marcos dirigidos a la ranura. */
int vmu_presente(void);

/* Atiende un marco Maple ya extraido de la lista de comandos: paquete[0] es
   el encabezado (comando, destinatario, remitente, largo) y el resto las
   palabras de datos. Escribe el marco de respuesta completo --encabezado
   incluido-- en respuesta y devuelve su largo en palabras. */
int vmu_maple(const void * paquete, int tam_palabras,
			  void * respuesta, int resp_max_palabras);

/* La imagen cruda, para las pruebas. NULL si no hay tarjeta. */
unsigned char * vmu_imagen(void);

#endif /* _VMU_H_ */
