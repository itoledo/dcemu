/***********************************************************************************
 *  1st_read.bin File Checker - Visual Basic to C conversion
 *  
 *  LyingWake <LyingWake@gmail.com>
 *  http://www.consolevision.com/members/fackue/
 *
 *  This is a port of 1st_read.bin File Checker 1.5's source code
 *  from Visual Basic 6.0 to Microsoft Visual C++.
 *
 *  Thanks to all those that helped; JustBurn, Quzar, GPFerror and
 *  and anyond else I forgot.
 ***********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include "1strdchk.h"

#ifdef _WIN32
	#include <io.h>
#else
	#include <unistd.h>
	#define _O_BINARY 0
	#define _O_RDONLY O_RDONLY
#endif

#ifndef _WIN32
	long _filelength(int fd) {
		long pos;
		lseek(fd, 0, SEEK_SET);
		pos = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		return pos; }
#endif


char *memoryspace;  /* A pointer that will be stored the file */


/*******************************  
 *   0 = Unscrambled 
 *   1 = Scrambled
 *  -1 = File not found 
 *******************************/

/* ///////////////////////////////////////////////////////////////////////////////////////////////// */

int IdentifyBin(char *filename) 
{
 int pFile;
 int rIdentifyBin;
 int bytesreaded;
 int i;

 char abc1[] = "abcdefghijklmnopqrstuvwxyz1234567890";
 char abc2[] = "abcdefghijklmnopqrstuvwxyz0123456789";
 char abc3[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
 char abc4[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
 char abc5[] = "1234567890abcdefghijklmnopqrstuvwxyz";
 char abc6[] = "0123456789abcdefghijklmnopqrstuvwxyz";
 char abc7[] = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";
 char abc8[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
 char temp[] = "#...'...*...-.../...2...4...7...9...;...=...?...A...C...E...G...I...J...L...N...O...Q...R...T...U...W...X...Z...";
 char temp2[] = "0123456789abcdef....(null)..0123456789ABCDEF";
 char bortmnt[] = "0123456789ABCDEF....Inf.NaN.0123456789abcdef....(null)...";
 char dreamsnes[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.0123456789-";
 char tetris[] = "abcdefghijklEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";
 char punch[] = "PORTDEV INFOENBLSTATRADRTOUTDRQCFUNCEND";
 char netbsd[] = "$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

 rIdentifyBin = Scrambled;

 pFile = open(filename, _O_BINARY | _O_RDONLY);
 if (pFile == -1)
 {
	return -1;
 }  
  
 /* Get the number of bytes in the file */
 bytesreaded = _filelength(pFile); 
 
 /* Allocate memory to store the file plus the NULL-termination char */
 memoryspace = (char *)malloc(bytesreaded+1); 
 
 /* Ok, now we read the file */
 bytesreaded = read(pFile, memoryspace, bytesreaded);
 
 /* This is the little cheat ;) */
 for (i = 0; i < bytesreaded; i++) 
 { 
	if (memoryspace[i] == 0x00)
	{
		memoryspace[i] = '.';
	} 
 } 
 
 /* We can't forget the NULL-termination char or program will crash! */
 memoryspace[bytesreaded] = '\0'; 
 close(pFile);

 if (strstr(memoryspace, abc1)      ||
     strstr(memoryspace, abc2)      ||
     strstr(memoryspace, abc3)      ||
     strstr(memoryspace, abc4)      ||
     strstr(memoryspace, abc5)      ||
     strstr(memoryspace, abc6)      ||
     strstr(memoryspace, abc7)      ||
     strstr(memoryspace, abc8)      ||
     strstr(memoryspace, temp)      ||
     strstr(memoryspace, temp2)     ||
     strstr(memoryspace, bortmnt)   ||
     strstr(memoryspace, dreamsnes) ||
     strstr(memoryspace, tetris)    ||
     strstr(memoryspace, punch)     ||
     strstr(memoryspace, netbsd)) 
 {
	rIdentifyBin = Unscrambled;
 }
 
 /* We dont need the memory anymore */
 free(memoryspace);  

 return rIdentifyBin;
} 

/* ///////////////////////////////////////////////////////////////////////////////////////////////// 

int main(int argc, char *argv[])
{
 int i;
 int retval = 0;

 // The program was opened without a filename argument 
 if(argc < 2) 
 {
	fprintf(stderr, "1st_read.bin File Checker\nhttp://www.consolevision.com/members/fackue\n\n");
	fprintf(stderr, "Usage: %s filename\n", argv[0]);
	return 1;
 }

 // The program was opened with a filename argument 
 for(i = 1; i < argc; i++) 
 {
	switch(IdentifyBin(argv[i])) 
	{
		case 0:
			printf("%s is unscrambled\n", argv[i]);
			break;
		case 1:
			printf("%s is scrambled\n", argv[i]);
			break;
		default:
			fprintf(stderr, "%s not found\n", argv[i]);
			retval = 1;
			break;
	}
 }
 return retval;
}
*/