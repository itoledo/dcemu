int iso_init(char * sDevice);
int iso_get_lba();
int iso_get_mode();
int iso_read_sector(char * target, int secstart, int secnum);
int cargar_archivo( char * fname, void * target);
int cargar_archivo_iso(char * fname, bool scrambled, unsigned char * mempos);

