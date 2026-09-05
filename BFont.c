
/***********************************************************/
/*                                                         */
/*   BFONT.c v. 1.1.0 - Billi Font Library by Diego Billi  */
/*                                                         */
/*   mail: dbilli@cs.unibo.it                              */
/*   home: http://www.cs.unibo.it/~dbilli (ITALIAN)        */
/*                                                         */
/***********************************************************/

#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "stdarg.h"

#include "BFont.h"

/* Antes se usaba SDL_image solo para este IMG_Load. Se reemplaza por stb_image,
   que es un unico header y evita arrastrar SDL_image 1.2. */
#define STBI_NO_STDIO_WRITE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* vsnprintf es estandar desde C99; el workaround de _vsnprintf ya no hace falta. */
#if defined(WIN32) && !defined(_MSC_VER)
	#define vsnprintf _vsnprintf
#endif


/* The first character in BFont fonts is '!' */
#define BFONT_FIRST_FONT_CHAR  33

/* ASCII value for "space" */
#define SPACE  32


/* BFont_Info structure */
struct _BFont_Info {
	int          h;                      /* font height */
	SDL_Surface *Surface;                /* font surface */
    SDL_Rect     Chars[BFONT_NUM_CHARS]; /* characters width */
};


/* Current font */
static BFont_Info *CurrentFont;


/* buffer size  for buffered prints*/
#define BFONT_BUFFER_LEN 1024

/* Single global var for buffered prints */
static char bfont_buffer[BFONT_BUFFER_LEN];


/* utility functions */
static Uint32 GetPixel(SDL_Surface *Surface, Sint32 X, Sint32 Y);


/* Carga una imagen (png o bmp) a un SDL_Surface RGBA de 32 bits, que es lo
   mismo que entregaba IMG_Load para el font.png original (color type 6).
   RGBA32 es el formato por orden de bytes --R en el byte 0, como lo deja
   stb_image--, o sea las mascaras 0xff/0xff00/0xff0000/0xff000000 de antes. */
static SDL_Surface * cargar_imagen(const char *filename)
{
    int w = 0, h = 0, canales = 0;
    unsigned char *pixels;
    SDL_Surface *surface;
    int y;

    pixels = stbi_load(filename, &w, &h, &canales, 4);
    if (pixels == NULL)
        return NULL;

    surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (surface == NULL) {
        stbi_image_free(pixels);
        return NULL;
    }

    if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
    for (y = 0; y < h; y++)
        memcpy((Uint8 *) surface->pixels + y * surface->pitch, pixels + y * w * 4, w * 4);
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);

    stbi_image_free(pixels);

    return surface;
}


/***************************** BFont Functions ********************************/

void InitFont(BFont_Info *Font)
{
    int x = 0, i = 0;
    Uint32 separator_color;

    i= BFONT_FIRST_FONT_CHAR;

    if (SDL_MUSTLOCK(Font->Surface)) SDL_LockSurface(Font->Surface);

    separator_color = GetPixel(Font->Surface,0,0);

    x=0;
    while ( (x < (Font->Surface->w-1)) && (i < BFONT_NUM_CHARS) ) 
	{
	    if(GetPixel(Font->Surface,x,0) != separator_color) {
            Font->Chars[i].x = x;
            Font->Chars[i].y = 1;
            Font->Chars[i].h = Font->Surface->h;
            for ( ; (GetPixel(Font->Surface, x, 0) != separator_color) && (x < Font->Surface->w); ++x) 
				;
            Font->Chars[i].w = (x - Font->Chars[i].x);
            i++;
	    }
        else {
	        x++;
        }
    }

    Font->Chars[SPACE].x = 0;
    Font->Chars[SPACE].y = 0;
    Font->Chars[SPACE].h = Font->Surface->h;
    Font->Chars[SPACE].w = Font->Chars[BFONT_FIRST_FONT_CHAR].w;

    Font->h = Font->Surface->h;

    SDL_SetSurfaceColorKey(Font->Surface, true, GetPixel(Font->Surface, 0, Font->Surface->h-1));

    if (SDL_MUSTLOCK(Font->Surface))
		SDL_UnlockSurface(Font->Surface);
}



BFont_Info * BFont_LoadFont (const char *filename)
{
    SDL_Surface *surface = NULL;
    int x;
	BFont_Info *Font = NULL;

	Font = (BFont_Info *) malloc(sizeof(BFont_Info));
	if (Font == NULL) 
		return NULL;

	surface = cargar_imagen(filename);
	if (surface == NULL) {
		free(Font);
		return NULL;
	}

	Font->Surface = surface;
	for (x=0; x < BFONT_NUM_CHARS ; x++) {
		Font->Chars[x].x = 0;
		Font->Chars[x].y = 0;
		Font->Chars[x].h = 0;
		Font->Chars[x].w = 0;
	}

	InitFont(Font);
	BFont_SetCurrentFont(Font);

	return Font;
}



void BFont_FreeFont(BFont_Info *Font)
{
    SDL_DestroySurface(Font->Surface);
	free(Font);
	Font = NULL;
}

void BFont_SetCurrentFont(BFont_Info *Font)
{
    CurrentFont = Font;
}

BFont_Info * BFont_GetCurrentFont(void)
{
    return CurrentFont;
}

int BFont_FontHeight (BFont_Info *Font)
{
    return (Font->h);
}

void BFont_SetFontHeight(BFont_Info *Font, int height)
{
	if (height >= 0)
		Font->h = height;
}


int BFont_CharWidth(BFont_Info *Font,int c)
{
    return Font->Chars[c].w;
}

int BFont_PutChar(SDL_Surface *Surface, int x, int y, int c)
{
    return BFont_PutCharFont(Surface, CurrentFont, x, y, c);
}

int BFont_PutCharFont(SDL_Surface *Surface, BFont_Info *Font,int x, int y, int c)
{
    SDL_Rect dest;
	SDL_Rect src;

	src = Font->Chars[c];
	if (src.h > Font->h)
		src.h = Font->h;

	dest = src;
	dest.x = x;
	dest.y = y;

    if (c != SPACE) {
        SDL_BlitSurface( Font->Surface, &src, Surface, &dest);
    }

	/* Next line modified by  Antti Mannisto  */
    return (Font->Chars[c].w);
}

void BFont_PutString(SDL_Surface *Surface, int x, int y, const char *text)
{
    BFont_PutStringFont(Surface, CurrentFont, x, y, text);
}

void BFont_PutStringFont(SDL_Surface *Surface, BFont_Info *Font, int x, int y, const char *text)
{
    while ( *text ) {
        x  += BFont_PutCharFont(Surface,Font,x,y, *text);
        text++;;
    }
}


int BFont_TextWidth(const char *text)
{
    return BFont_TextWidthFont( CurrentFont, text);
}

int BFont_TextWidthFont(BFont_Info *Font, const char *text)
{
    int x=0;

	while (*text) {
		x += BFont_CharWidth(Font,*text);
		text++;
	}
    return x;
}


/* counts the spaces of the strings */
static int count (const char *text)
{
    char *p = NULL;
    int pos = -1;
    int i   = 0;

    /* Calculate the space occupied by the text without spaces */
    while ((p=strchr(&text[pos+1],SPACE)) != NULL) {
            i++;
            pos = p - text;
    }
    return i;
}

void BFont_JustifiedPutString(SDL_Surface *Surface, int y, const char *text)
{
    BFont_JustifiedPutStringFont( Surface, CurrentFont, y,text);
}

void BFont_JustifiedPutStringFont(SDL_Surface *Surface, BFont_Info *Font,  int y, const char *text)
{
    int spaces = 0;
    int gap;
    int single_gap;
    int dif;

    char *strtmp;
    char *p;
    int pos = -1;
    int xpos = 0;


    if (strchr(text,SPACE) == NULL) {
        BFont_PutStringFont(Surface, Font, 0, y, text);
    }
    else {
        gap = (Surface->w-1) - BFont_TextWidthFont(Font,text);

        if (gap <= 0) {
            BFont_PutStringFont(Surface, Font,0,y,text);
        } else {
            spaces = count(text);
            dif = gap % spaces;
            single_gap = (gap - dif) / spaces;
            xpos=0;
            pos = -1;
            while ( spaces > 0 ) {
                p = strstr(&text[pos+1]," ");
                strtmp = NULL;
                strtmp = (char *) calloc ((p - &text[pos+1]) + 1,sizeof(char));
                if (strtmp != NULL) 
				{
                    strncpy (strtmp, &text[pos+1], (p - &text[pos+1]));
                    BFont_PutStringFont(Surface, Font, xpos, y, strtmp);
                    xpos = xpos + BFont_TextWidthFont(Font, strtmp) + single_gap + BFont_CharWidth(Font,SPACE);
                    if (dif >= 0) {
                        xpos ++;
                        dif--;
                    }
                    pos = p - text;
                    spaces--;
                    free(strtmp);
                }
            }
            strtmp = NULL;
            strtmp = (char *) calloc (strlen(&text[pos+1]) + 1,sizeof(char));

            if (strtmp != NULL) {
                strncpy (strtmp, &text[pos+1], strlen( &text[pos+1]));
                BFont_PutStringFont(Surface, Font,xpos, y, strtmp);
                free(strtmp);
            }
        }
    }
}

void BFont_CenteredPutString(SDL_Surface *Surface, int y, const char *text)
{
    BFont_CenteredPutStringFont(Surface, CurrentFont, y, text);
}

void BFont_CenteredPutStringFont(SDL_Surface *Surface, BFont_Info *Font, int y, const char *text)
{
    BFont_PutStringFont(Surface, Font, (Surface->w/2) - (BFont_TextWidthFont(Font,text)/2), y, text);
}

void BFont_RightPutString(SDL_Surface *Surface, int y, const char *text)
{
    BFont_RightPutStringFont(Surface, CurrentFont, y, text);
}

void BFont_RightPutStringFont(SDL_Surface *Surface, BFont_Info *Font, int y, const char *text)
{
    BFont_PutStringFont(Surface, Font, Surface->w - BFont_TextWidthFont(Font,text) - 1, y, text);
}

void BFont_LeftPutString(SDL_Surface *Surface, int y, const char *text)
{
    BFont_LeftPutStringFont(Surface, CurrentFont, y, text);
}

void BFont_LeftPutStringFont(SDL_Surface *Surface, BFont_Info *Font, int y, const char *text)
{
    BFont_PutStringFont(Surface, Font, 0, y, text);
}

/******/

void BFont_PrintString (SDL_Surface *Surface, int x, int y, const char *fmt, ...)
{
    va_list args;

    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_PutStringFont(Surface, CurrentFont, x, y, bfont_buffer);
}

void BFont_PrintStringFont(SDL_Surface *Surface, BFont_Info *Font, int x, int y, const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_PutStringFont(Surface, Font, x, y, bfont_buffer);
}

void BFont_CenteredPrintString(SDL_Surface *Surface, int y,  const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_CenteredPutString(Surface, y, bfont_buffer);
}

void BFont_CenteredPrintStringFont(SDL_Surface *Surface, BFont_Info *Font, int y,  const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_CenteredPutStringFont(Surface, Font, y, bfont_buffer);
}

void BFont_RightPrintString(SDL_Surface *Surface, int y, const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_RightPutString(Surface, y, bfont_buffer);
}

void BFont_RightPrintStringFont(SDL_Surface *Surface, BFont_Info *Font, int y,  const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_RightPutStringFont(Surface, Font, y, bfont_buffer);
}

void BFont_LeftPrintString(SDL_Surface *Surface, int y, const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_LeftPutString(Surface, y, bfont_buffer);
}

void BFont_LeftPrintStringFont(SDL_Surface *Surface, BFont_Info *Font, int y, const  char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_LeftPutStringFont(Surface, Font, y, bfont_buffer);
}

void BFont_JustifiedPrintString(SDL_Surface *Surface, int y, const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_JustifiedPutString( Surface,  y,bfont_buffer);
}

void BFont_JustifiedPrintStringFont(SDL_Surface *Surface, BFont_Info *Font,  int y, const char *fmt, ...)
{
    va_list args;
    
    va_start (args,fmt);
    vsnprintf(bfont_buffer,BFONT_BUFFER_LEN,fmt,args);
    va_end(args);

    bfont_buffer[BFONT_BUFFER_LEN-1] = '\0';
    BFont_JustifiedPutStringFont( Surface, Font, y,bfont_buffer);
}

/*********************************************************************************************************/
/*********************************************************************************************************/
/*********************************************************************************************************/

static Uint32 GetPixel(SDL_Surface *Surface, Sint32 X, Sint32 Y)
{

   Uint8  *bits;
   Uint32 Bpp;

   if (X<0) puts("x too small in GetPixel!");
   if (X>=Surface->w) puts("x too big in GetPixel!");

   /* En SDL3 `format` es el enumerado del formato, no una estructura; los
      bytes por pixel y los corrimientos salen de las macros y de
      SDL_GetPixelFormatDetails. */
   Bpp = SDL_BYTESPERPIXEL(Surface->format);

   bits = ((Uint8 *)Surface->pixels)+Y*Surface->pitch+X*Bpp;

   // Get the pixel
   switch(Bpp) {
      case 1:
         return *((Uint8 *)Surface->pixels + Y * Surface->pitch + X);
         break;
      case 2:
         return *((Uint16 *)Surface->pixels + Y * Surface->pitch/2 + X);
         break;
      case 3: { // Format/endian independent
         const SDL_PixelFormatDetails *d = SDL_GetPixelFormatDetails(Surface->format);
         Uint8 r, g, b;
         r = *((bits)+d->Rshift/8);
         g = *((bits)+d->Gshift/8);
         b = *((bits)+d->Bshift/8);
         return SDL_MapRGB(d, NULL, r, g, b);
         }
         break;
      case 4:
         return *((Uint32 *)Surface->pixels + Y * Surface->pitch/4 + X);
         break;
   }

    return -1;
}

