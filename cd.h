#ifndef _cd_h
#define _cd_h

// this function inits all the cdio stuff and retreives the bootstrap and exec from the cd
int cd_init(char * input);
// this function inits all the cdio stuff and retreives the bootstrap and exec from the the current directory
int dummy_init(char * input);
int cd_get_mode(DWORD write_address);
int cd_getStatus(DWORD write_address);
int cd_get_lba();
int cd_read_sector(char * target, int secstart, int secnum);
int cd_close();

#endif
