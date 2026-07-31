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
