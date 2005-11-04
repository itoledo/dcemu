#ifndef _file_h
#define _file_h

typedef enum {HW_ID,MK_ID,DEV_I,AREA,PHER,PROD_NUM,PROD_VER,REL_DATE,BOOT,CPNY,NAME}IP_BIN;
extern char ** IP_STRUCT;

int loadSys();
int load_bootstrap( char * fname, void * target);
int load_exec(char * fname, void * target);

#endif