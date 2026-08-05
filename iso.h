int iso_init(char * sDevice);
int iso_get_lba();
int iso_get_mode();

/* 1 si hay una imagen montada. Es lo que decide, con --bandeja=auto, si la
   lectora arranca con disco o sin disco. */
int iso_hay_disco();

/* 1 si el disco montado es un GD-ROM: sus datos estan en el area de alta
   densidad. Lo mira la lectora para decir de que tipo es el disco. */
int iso_es_gdrom();

/*
	1 si el ejecutable de arranque de esta imagen esta cifrado.

	**Depende del formato, y esta medido en las dos direcciones.** Un selfboot en
	.cdi lo trae cifrado -- es lo que espera su bootstrap -- y por eso dcemu
	descifraba siempre. Un rip en .gdi no: en los dos que hay a mano, el archivo
	en el disco **ya es codigo SH-4 valido** (DCDoom empieza con un cargador
	auto-relocalizante, Dave Mirra con seis NOP y un JMP) y descifrarlo lo
	convierte en basura, con lo que el bootstrap salta a cualquier parte.

	No es una heuristica sobre el contenido: es de que formato salio la imagen.
	Si aparece un .gdi con el ejecutable cifrado, esto se entera por el mismo
	sintoma -- el guest saltando a memoria baja -- y habra que mirarlo de nuevo.
*/
int iso_ejecutable_cifrado(void);

/* EXPERIMENTO: 1 si el selfboot se esta presentando como GD-ROM. */
int iso_gd_presentando(void);

/*
	Las pistas del disco, para armar la TOC y contestar REQ_SES. Un .iso plano
	es una sola pista de datos; un .cdi de juego son dos o tres, y la ultima es
	el area de alta densidad. Todo en FAD.
*/
int iso_num_pistas(void);
int iso_pista_fad(int i);
int iso_pista_sectores(int i);
int iso_pista_es_datos(int i);
int iso_num_sesiones(void);
int iso_sesion_fad(int n);
int iso_sesion_primera_pista(int n);

/* Sectores que ocupa el disco: la TOC lo necesita para el lead-out. */
int iso_num_sectores();

int iso_read_sector(char * target, int secstart, int secnum);
int cargar_archivo( char * fname, void * target);
int cargar_archivo_iso(char * fname, bool scrambled, unsigned char * mempos);
int cargar_ip_bin(unsigned char * mempos);
