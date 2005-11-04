#ifndef _iso_h
#define _iso_h

int iso_init();
int iso_get_info();
int iso_getSectorMode();
int iso_get_status();
int iso_read_sector(char * target, int secstart, int secnum);
int closeIso();

#endif
