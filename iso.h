int iso_init(char * sDevice);
int iso_get_lba();
int iso_get_mode();

/* 1 si hay una imagen montada. Es lo que decide, con --bandeja=auto, si la
   lectora arranca con disco o sin disco. */
int iso_hay_disco();

/* Sectores que ocupa el disco: la TOC lo necesita para el lead-out. */
int iso_num_sectores();

int iso_read_sector(char * target, int secstart, int secnum);
int cargar_archivo( char * fname, void * target);
int cargar_archivo_iso(char * fname, bool scrambled, unsigned char * mempos);
int cargar_ip_bin(unsigned char * mempos);
