void descramble(char *src, char *dst);

/*
	Descrambling sobre un bloque que ya esta en memoria. Es lo que hace falta
	para un ejecutable cargado a mano; la version de arriba va de archivo a
	archivo, que es lo que necesita la carga desde una imagen.
*/
void descramble_memoria(unsigned char * datos, unsigned long tam);

/*
	Si un bloque parece un ejecutable cifrado. Mira el prologo de entrada, no
	estadisticas: el cifrado permuta rebanadas de 32 bytes y por lo tanto no
	cambia ninguna cuenta de bytes ni de palabras. Ver scramble.c.
*/
int parece_cifrado(const unsigned char * datos, unsigned long tam);
