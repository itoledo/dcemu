/*  Pcsx - Pc Psx Emulator
 *  Copyright (C) 1999-2002  Pcsx Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * The base code was taken from the pcsx project so i left the reference to their project
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"

char * cfgfile = "config.cfg";

#define GetValue(name, var) \
	tmp = strstr(data, name); \
	if (tmp != NULL) { \
		tmp+=strlen(name)+1; \
		while ((*tmp == ' ') || (*tmp == '=')) tmp++; \
		if (*tmp != '\n') sscanf(tmp, "%s", var); \
	}

#define GetValuel(name, var) \
	tmp = strstr(data, name); \
	if (tmp != NULL) { \
		tmp+=strlen(name)+1; \
		while ((*tmp == ' ') || (*tmp == '=')) tmp++; \
		if (*tmp != '\n') sscanf(tmp, "%lx", &var); \
	}

#define SetValue(name, var) \
	fprintf (f,"%s = %s\n", name, var);

#define SetValuel(name, var) \
	fprintf (f,"%s = %lx\n", name, var);


int LoadConfig() {

	struct stat buf;
	FILE *f;
	int size;
	char *data,*tmp;

	if (stat(cfgfile, &buf) == -1) 
	{
		fprintf(stderr,"Config file not found");		
		return -1;
	}

	size = buf.st_size;

	f = fopen(cfgfile,"r");
	if (f == NULL) return -1;

	data = (char*)malloc(size);
	if (data == NULL) return -1;

	fread(data, 1, size, f);
	fclose(f);

	GetValue("Bios", config.Bios);
	GetValue("Flash", config.Flash);
	GetValue("Logo", config.Logo);


 	free(data);

	return 0;
}

/////////////////////////////////////////////////////////

void SaveConfig() {
	FILE *f;

	f = fopen(cfgfile,"w");
	if (f == NULL) return;
	
	SetValue("Bios", config.Bios);
	SetValue("Flash",config.Flash);
	SetValue("Logo", config.Logo);

	fclose(f);
}

