#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "medium.h"
#include "controller.h"

int dcemu_init(int argc,char ** argv)
{
	if (argc <= 2)
 	{
		fprintf(stderr,"No Input we can trace");
		printf("dcemu [filters] [media] [-js] \n\n Filters may be on of the following: \n -i -> Loads Iso\n-cd -> Loads a cd\n -b -> Loads a binary file\n-bcd -> Loads a binary file that needs to acess the cd \n\n[media] -> Is the path to the file\device\n [-js] if this option is chosen we will try and use a connected joystick\n\n");
		return 1;
 	}
 	if (argc >= 3)
 	{
   		if(strcmp(argv[1],"-i") == 0)
			medium_init(ISO,argv[2]);
		else  if(strcmp(argv[1],"-b") == 0)
			 	medium_init(BINARY,argv[2]);
		else if(strcmp(argv[1],"-bcd") == 0)
			medium_init(BINARY_CD,argv[2]);
		else if(strcmp(argv[1],"-cd") == 0)
			medium_init(CDROM,argv[2]);
		if ((argv[3] != NULL) && (strcmp(argv[3],"-js") == 0))
			setController(JOYSTICK);
		else setController (KEYBOARD);
	}
 	return 0;
}
