#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scramble.h"

#define MAXCHUNK (2048*1024)

static unsigned int seed;

void my_srand(unsigned int n)
{
  seed = n & 0xffff;
}

unsigned int my_rand()
{
  seed = (seed * 2109 + 9273) & 0x7fff;
  return (seed + 0xc000) & 0xffff;
}

void load(FILE *fh, unsigned char *ptr, unsigned long sz)
{
  if(fread(ptr, 1, sz, fh) != sz)
    {
      fprintf(stderr, "Read error!\n");
      exit(1);
    }
}

void load_chunk(FILE *fh, unsigned char *ptr, unsigned long sz)
{
  static int idx[MAXCHUNK/32];
  int i;

  /* Convert chunk size to number of slices */
  sz /= 32;

  /* Initialize index table with unity,
     so that each slice gets loaded exactly once */
  for(i = 0; i < sz; i++)
    idx[i] = i;

  for(i = sz-1; i >= 0; --i)
    {
      /* Select a replacement index */
      int x = (my_rand() * i) >> 16;

      /* Swap */
      int tmp = idx[i];
      idx[i] = idx[x];
      idx[x] = tmp;

      /* Load resulting slice */
      load(fh, ptr+32*idx[i], 32);
    }
}

void load_file(FILE *fh, unsigned char *ptr, unsigned long filesz)
{
  unsigned long chunksz;

  my_srand(filesz);

  /* Descramble 2 meg blocks for as long as possible, then
     gradually reduce the window down to 32 bytes (1 slice) */
  for(chunksz = MAXCHUNK; chunksz >= 32; chunksz >>= 1)
    while(filesz >= chunksz)
      {
	load_chunk(fh, ptr, chunksz);
	filesz -= chunksz;
	ptr += chunksz;
      }

  /* Load final incomplete slice */
  if(filesz)
    load(fh, ptr, filesz);
}

void read_file(char *filename, unsigned char **ptr, unsigned long *sz)
{
  FILE *fh = fopen(filename, "rb");
  if(fh == NULL)
    {
      fprintf(stderr, "Can't open \"%s\".\n", filename);
      exit(1);
    }
  if(fseek(fh, 0, SEEK_END)<0)
    {
      fprintf(stderr, "Seek error.\n");
      exit(1);
    }
  *sz = ftell(fh);
  *ptr = malloc(*sz);
  if( *ptr == NULL )
    {
      fprintf(stderr, "Out of memory.\n");
      exit(1);
    }
  if(fseek(fh, 0, SEEK_SET)<0)
    {
      fprintf(stderr, "Seek error.\n");
      exit(1);
    }
  load_file(fh, *ptr, *sz);
  fclose(fh);
}

void save(FILE *fh, unsigned char *ptr, unsigned long sz)
{
  if(fwrite(ptr, 1, sz, fh) != sz)
    {
      fprintf(stderr, "Write error!\n");
      exit(1);
    }
}

void save_chunk(FILE *fh, unsigned char *ptr, unsigned long sz)
{
  static int idx[MAXCHUNK/32];
  int i;

  /* Convert chunk size to number of slices */
  sz /= 32;

  /* Initialize index table with unity,
     so that each slice gets saved exactly once */
  for(i = 0; i < sz; i++)
    idx[i] = i;

  for(i = sz-1; i >= 0; --i)
    {
      /* Select a replacement index */
      int x = (my_rand() * i) >> 16;

      /* Swap */
      int tmp = idx[i];
      idx[i] = idx[x];
      idx[x] = tmp;

      /* Save resulting slice */
      save(fh, ptr+32*idx[i], 32);
    }
}

void save_file(FILE *fh, unsigned char *ptr, unsigned long filesz)
{
  unsigned long chunksz;

  my_srand(filesz);

  /* Descramble 2 meg blocks for as long as possible, then
     gradually reduce the window down to 32 bytes (1 slice) */
  for(chunksz = MAXCHUNK; chunksz >= 32; chunksz >>= 1)
    while(filesz >= chunksz)
      {
	save_chunk(fh, ptr, chunksz);
	filesz -= chunksz;
	ptr += chunksz;
      }

  /* Save final incomplete slice */
  if(filesz)
    save(fh, ptr, filesz);
}

void write_file(char *filename, unsigned char *ptr, unsigned long sz)
{
  FILE *fh = fopen(filename, "wb");
  if(fh == NULL)
    {
      fprintf(stderr, "Can't open \"%s\".\n", filename);
      exit(1);
    }
  save_file(fh, ptr, sz);
  fclose(fh);
}

/* ------------------------------------------------------------------------ */
/* Lo mismo, en memoria                                                     */
/* ------------------------------------------------------------------------ */

/*
	El descrambling de arriba va de archivo a archivo, que es lo que necesita
	la carga desde una imagen -- y de paso deja scrambled.bin y descrambled.bin
	a mano, que sirven para buscar cadenas sin correr nada. Para un ejecutable
	que ya esta en la RAM del guest hace falta hacerlo ahi mismo.
*/

static void trozo_desde_memoria(const unsigned char ** src, unsigned char * dst,
	unsigned long sz)
{
	static int	idx[MAXCHUNK / 32];
	int			i, n = (int) (sz / 32);

	for (i = 0; i < n; i++)
		idx[i] = i;

	for (i = n - 1; i >= 0; --i)
	{
		int x = (int) ((my_rand() * (unsigned int) i) >> 16);
		int tmp = idx[i];

		idx[i] = idx[x];
		idx[x] = tmp;

		memcpy(dst + 32 * idx[i], *src, 32);
		*src += 32;
	}
}

void descramble_memoria(unsigned char * datos, unsigned long tam)
{
	unsigned char *			copia;
	const unsigned char *	src;
	unsigned char *			dst = datos;
	unsigned long			resto = tam, trozo;

	if (datos == NULL || tam == 0)
		return;

	/* El cifrado dispersa, asi que no se puede hacer sobre el mismo bloque. */
	copia = (unsigned char *) malloc(tam);

	if (copia == NULL)
	{
		fprintf(stderr, "descramble_memoria: sin memoria para %lu bytes\n", tam);
		return;
	}

	memcpy(copia, datos, tam);
	src = copia;

	my_srand(tam);

	for (trozo = MAXCHUNK; trozo >= 32; trozo >>= 1)
		while (resto >= trozo)
		{
			trozo_desde_memoria(&src, dst, trozo);
			resto -= trozo;
			dst   += trozo;
		}

	/* La ultima rebanada incompleta va tal cual. */
	if (resto)
		memcpy(dst, src, resto);

	free(copia);
}

/*
	Si un bloque parece un ejecutable cifrado.

	**Por estadistica no se puede**: el cifrado solo permuta rebanadas de 32
	bytes, asi que cualquier cuenta de bytes o de palabras da identica en las
	dos versiones -- medido, y por eso no se intenta. Lo que cambia es *donde*
	queda cada cosa, y hay una posicion conocida: la primera instruccion de un
	ejecutable de Dreamcast es casi siempre un MOV.L @(disp,PC),Rn, el prologo
	que carga la direccion a la que va a saltar, y ese literal tiene que ser una
	direccion de RAM o de RAM de video. Si no lo es, el principio del archivo no
	es el principio del programa: viene cifrado.

	Si el primer opcode no es ese MOV.L no se opina, y se responde "no cifrado",
	que es lo que dcemu hizo siempre.
*/

static int direccion_plausible(unsigned long v)
{
	unsigned long fisica = v & 0x1FFFFFFFu;

	if (v & 1)
		return 0;					/* ninguna direccion util es impar */

	if (fisica >= 0x0C000000 && fisica < 0x10000000)
		return 1;					/* RAM del sistema */

	if (fisica >= 0x04000000 && fisica < 0x06000000)
		return 1;					/* RAM de video */

	return 0;
}

int parece_cifrado(const unsigned char * datos, unsigned long tam)
{
	unsigned int	op;
	unsigned long	donde, valor;

	if (datos == NULL || tam < 8)
		return 0;

	/* El SH-4 de la Dreamcast es little endian, tambien para las instrucciones. */
	op = (unsigned int) datos[0] | ((unsigned int) datos[1] << 8);

	if ((op >> 12) != 0xD)
		return 0;

	donde = 4 + (unsigned long) (op & 0xFF) * 4;

	if (donde + 4 > tam)
		return 1;					/* el literal no cabe: no es el prologo */

	valor = (unsigned long) datos[donde]
		| ((unsigned long) datos[donde + 1] <<  8)
		| ((unsigned long) datos[donde + 2] << 16)
		| ((unsigned long) datos[donde + 3] << 24);

	return !direccion_plausible(valor);
}

void descramble(char *src, char *dst)
{
  unsigned char *ptr = NULL;
  unsigned long sz = 0;
  FILE *fh;

  read_file(src, &ptr, &sz);

  fh = fopen(dst, "wb");
  if(fh == NULL)
    {
      fprintf(stderr, "Can't open \"%s\".\n", dst);
      exit(1);
    }
  if( fwrite(ptr, 1, sz, fh) != sz )
    {
      fprintf(stderr, "Write error.\n");
      exit(1);
    }
  fclose(fh);
  free(ptr);
}

void scramble(char *src, char *dst)
{
  unsigned char *ptr = NULL;
  unsigned long sz = 0;
  FILE *fh;

  fh = fopen(src, "rb");
  if(fh == NULL)
    {
      fprintf(stderr, "Can't open \"%s\".\n", src);
      exit(1);
    }
  if(fseek(fh, 0, SEEK_END)<0)
    {
      fprintf(stderr, "Seek error.\n");
      exit(1);
    }
  sz = ftell(fh);
  ptr = malloc(sz);
  if( ptr == NULL )
    {
      fprintf(stderr, "Out of memory.\n");
      exit(1);
    }
  if(fseek(fh, 0, SEEK_SET)<0)
    {
      fprintf(stderr, "Seek error.\n");
      exit(1);
    }
  if( fread(ptr, 1, sz, fh) != sz )
    {
      fprintf(stderr, "Read error.\n");
      exit(1);
    }
  fclose(fh);

  write_file(dst, ptr, sz);

  free(ptr);
}

/* int main(int argc, char *argv[])
{
  int opt = 0;

  if(argc > 1 && !strcmp(argv[1], "-d"))
    opt ++;

  if(argc != 3+opt)
    {
      fprintf(stderr, "Usage: %s [-d] from to\n", argv[0]);
      exit(1);
    }
  
  if(opt)
    descramble(argv[2], argv[3]);
  else
    scramble(argv[1], argv[2]);

  return 0;
} */

