#ifndef _config_h_
#define _config_h_

typedef struct
{
 char Bios[256];
 char Flash[256];
 char Logo [256];
}Config;

Config config;

int LoadConfig();
void SaveConfig();
void updateConfig(int field,char *);

#endif