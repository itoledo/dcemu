#include "main.h"
#include "sh4emu.h"
#include "opcodes.h"
#include <math.h>
#include "iso.h"
#include "gdrom.h"
#include "sistema.h"
#include "traza.h"

#define PI 3.1415926535

DWORD com=0;

OPCODE(fsca) // FSCA FPUL, DRn
{
//	long n = (arg >> 8) & 0x0F;
	long n = (arg >> 9) & 0x07;
	float x = (float) (2 * PI * (float) FPUL / 65536.0);
//	float x = (float) (PI * FPUL / 65536.0);

	FR(n*2) = sin(x);
	FR(n*2+1) = cos(x);
/*	float_R(n) = sin(FPUL);
	float_registers[n+1] = cos(FPUL); */

/*	float_R(n) = 0;
	float_registers[n+1] = 0; */

	PC += 2;

#ifdef ASM_DEBUG
	fprintf(logfp, "fsca: n:%d, FPUL=%f, fl_reg[n]=%f, fl_reg[n+1]=%f\r\n",
		n, (float) FPUL, FR(n), float_registers[n+1]);
#endif
}

OPCODE(NOIMP)
{
	logmsg("opcode no implementado: %s (%d)\r\n", opcodes[find_opcode(PC)].opdesc, find_opcode(PC));
	PC += 2;
}

/*
	El syscall de la fuente del BIOS, vector 0x8C0000B4.

	A diferencia del resto, este lleva el numero de funcion en R1 -- ver
	syscall_font.s de KOS -- y son solo tres: 0 devuelve la direccion de la
	fuente, 1 toma el mutex y 2 lo suelta. El lock responde 0 si lo concedio.

	El stub antes era RTS + MOV.L @(0,PC),R0 con la direccion como literal, o
	sea que respondia la direccion a las tres funciones. Eso dejaba colgado a
	lock_bfont() de KOS, que hace thd_poll(bfont_lock) hasta que el syscall
	devuelva 0: con la direccion de vuelta -- distinta de cero -- giraba para
	siempre y ningun bfont_draw_str() llegaba a dibujar.
*/
static void hack_romfont(void)
{
	logmsg("HACK_ROMFONT: func=%d\r\n", R(1));

	switch (R(1))
	{
		case 0:		/* direccion de la fuente */
#ifdef USE_BIOS_FONT
		/* En P0, que es lo que devuelve el boot ROM real: bios.bin trae dos
		   veces la constante 0x00100020 y ninguna vez 0xA0100020. Ojo con eso
		   si el guest enciende la MMU y mapea la zona baja -- ver
		   docs/demos-kos.md, `basic-mmu-pvrmap`. */
		R(0) = 0x00100020;	/* la fuente real, dentro de bios.bin */
#else
		R(0) = FONT_BASE;	/* la que rasteriza inicializar_fonts() */
#endif
		break;

		case 1:		/* tomar el mutex: nadie mas lo pide, siempre se concede */
		R(0) = 0;
		break;

		case 2:		/* soltarlo */
		R(0) = 0;
		break;

		default:
		logmsg("HACK_ROMFONT: funcion %d desconocida\r\n", R(1));
		R(0) = -1;
		break;
	}
}

/*
	El syscall de la flash ROM, vector 0x8C0000B8.

	La convencion es syscall(r4, r5, r6, func): el numero de funcion viaja en R7
	y el resultado sale por R0. Las funciones son 0 info, 1 lectura, 2 escritura
	y 3 borrado.

	Hacia falta porque flashrom_get_region() de KOS pregunta por la particion 0
	a traves de este syscall en vez de parsear la flash, y sin respuesta reporta
	"flashrom_get_region: can't find partition 0" -- que es lo que decia el
	ejemplo video/palmenu.
*/
static void hack_flashrom(void)
{
	/* Offset y tamano de cada particion, en el orden en que las numera KOS
	   (dc/flashrom.h): 0 ajustes de fabrica, 1 reservada, 2 y 4 bloques, 3
	   ajustes de juegos. Los offsets ya estaban en sistema.h. */
	static const DWORD offset[5] =
	{
		FLASH_PART0_OFF, FLASH_PART1_OFF, FLASH_PART2_OFF,
		FLASH_PART3_OFF, FLASH_PART4_OFF
	};
	static const DWORD tamano[5] =
	{
		 8 * 1024,		/* 0: ajustes de fabrica  */
		 8 * 1024,		/* 1: reservada           */
		16 * 1024,		/* 2: bloques             */
		32 * 1024,		/* 3: ajustes de juegos   */
		64 * 1024		/* 4: bloques             */
	};

	logmsg("HACK_FLASHROM: func=%d, r4=%x, r5=%x, r6=%x\r\n", R(7), R(4), R(5), R(6));

	switch (R(7))
	{
		case 0:		/* info: R4 = particion, R5 -> {offset, tamano} */
		{
			DWORD parte = R(4);

			if (parte >= 5)
			{
				logmsg("HACK_FLASHROM: particion %d inexistente\r\n", parte);
				R(0) = (DWORD) -1;
				break;
			}

			memwrite(R(5),     (void *) &offset[parte], sizeof(DWORD));
			memwrite(R(5) + 4, (void *) &tamano[parte], sizeof(DWORD));

			R(0) = 0;
		}
		break;

		case 1:		/* lectura: R4 = offset, R5 = destino, R6 = cuantos */
		{
			DWORD desde = R(4);
			DWORD cuantos = R(6);

			if (desde + cuantos > FLASH_SIZE)
			{
				logmsg("HACK_FLASHROM: lectura fuera de rango (%x + %x)\r\n", desde, cuantos);
				R(0) = (DWORD) -1;
				break;
			}

			memwrite(R(5), &flash_mem[desde], cuantos);

			R(0) = cuantos;
		}
		break;

		case 2:		/* escritura */
		{
			DWORD hacia = R(4);
			DWORD cuantos = R(6);
			DWORD i;

			if (hacia + cuantos > FLASH_SIZE)
			{
				logmsg("HACK_FLASHROM: escritura fuera de rango (%x + %x)\r\n", hacia, cuantos);
				R(0) = (DWORD) -1;
				break;
			}

			/* La flash real solo puede pasar bits de 1 a 0 sin borrar antes, y
			   el guest cuenta con eso para los bloques de ajustes. */
			for (i = 0; i < cuantos; i++)
			{
				BYTE b;

				memread(R(5) + i, &b, sizeof(BYTE));
				flash_mem[hacia + i] &= b;
			}

			R(0) = cuantos;
		}
		break;

		case 3:		/* borrado de un bloque: todo a 0xFF */
		{
			DWORD parte = R(4);

			if (parte >= 5)
			{
				R(0) = (DWORD) -1;
				break;
			}

			memset(&flash_mem[offset[parte]], 0xFF, tamano[parte]);
			R(0) = 0;
		}
		break;

		default:
		logmsg("HACK_FLASHROM: funcion %d sin implementar\r\n", R(7));
		R(0) = (DWORD) -1;
		break;
	}
}

void hack_gdrom()
{
	DWORD valor;

	logmsg("HACK_GDROM: r6=%x, r7=%x\r\n", R(6), R(7));

	/* Con los hooks puestos el juego no le habla a la lectora emulada, asi que
	   los paquetes SPI que reporta gdrom.c no dicen nada: lo que hay que ver es
	   esto. Sin esta linea, "no lee del disco" y "lee por syscall" se parecen
	   demasiado -- y ya costo confundirlos una vez. */
	if (traza_activa)
		fprintf(stderr, "hack: syscall GD-ROM r4=%lx r5=%lx r6=%lx r7=%lx "
			"(PC=%08lx PR=%08lx)\n",
			(unsigned long) R(4), (unsigned long) R(5),
			(unsigned long) R(6), (unsigned long) R(7),
			(unsigned long) PC, (unsigned long) PR);
	if (R(6) == 0)
	{
		switch(R(7))
		{
			case 0: // GDROM_SEND_COMMAND
			logmsg("GDROM_SEND_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
			switch(R(4))
			{
				/* 16 es la lectura por PIO y 17 la misma por DMA: mismos
				   parametros --sector, cuantos, destino-- y aqui la
				   transferencia se hace igual, en el momento. Sin el caso 17 el
				   guest pedia sus datos, se llevaba un identificador valido y no
				   recibia ni un sector. Crazy Taxi pide la 17 y solo esa. */
				case 17: // lo mismo por DMA
				case 16: // read sector, R(5) es el lugar donde se guardan los datos
				{
					int secstart, secnum;
					DWORD targetaddr;
					char * targetmem;

					memread(R(5), &secstart, sizeof(int));
					memread(R(5) + 4, &secnum, sizeof(int));
					memread(R(5) + 8, &targetaddr, sizeof(DWORD));

					logmsg("read sector: start=%x, num=%x, addr=%x\n", secstart, secnum, targetaddr);
					targetmem = malloc(sizeof(char) * 2048 * secnum);
					iso_read_sector(&targetmem[0], secstart, secnum);
					memwrite(targetaddr, &targetmem[0], 2048 * secnum);
					free(targetmem);
				}
				break;

				case 19: // read toc, R(5) es el lugar donde se guardan los datos
				{
					int session;
					DWORD targetaddr;
					struct TOC toc;

		        	// hay que leer el # de sesi�n, es el 1er numero que apunta esa dir
		        	memread(R(5), &session, sizeof(int));
		        	logmsg("read toc: sesion n %d\n", session);

					// La misma TOC que arma la lectora de verdad. Antes se
					// llenaban a mano tres campos y los otros 98 quedaban con
					// lo que hubiera en la pila.
					gdrom_construir_toc(&toc);

		        	memread(R(5) + 4, &targetaddr, sizeof(DWORD));
		        	logmsg("escribiendo TOC a %x\n", targetaddr);
		        	memwrite(targetaddr, &toc, sizeof(struct TOC));
   				}
   				break;

				default:
				/* El guest se lleva un identificador valido igual, asi que da
				   su peticion por hecha. Nombrar el comando que falta es lo
				   unico que impide que eso pase inadvertido. */
				logmsg("GDROM_SEND_COMMAND: comando %d sin implementar\r\n", R(4));

				if (traza_activa)
					fprintf(stderr, "hack: comando %lu del GD-ROM sin implementar\n",
						(unsigned long) R(4));
   				break;
			}
			/* El identificador de la peticion, que el guest usara para
			   preguntar por ella. Aqui es fijo porque nunca hay mas de una
			   viva: el trabajo ya se hizo mas arriba, en el momento. */
			com = 0x6969;
			R(0) = com;
			break;

		    case 1: // GDROM_CHECK_COMMAND
		    logmsg("GDROM_CHECK_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
			/*
				gdGdcGetCmdStat(peticion, estado). Como SEND_COMMAND hace el
				trabajo en el momento, cuando el guest pregunta el comando ya
				termino: COMPLETED. Devolver NO_ACTIVE la primera vez que se
				preguntaba por una peticion --que es lo que hacia-- se lee como
				"esa peticion no existe", y Crazy Taxi respondia reintentando la
				inicializacion entera, para siempre.

				Los codigos son los del driver de la BIOS: 0 NO_ACTIVE,
				1 PROCESSING, 2 COMPLETED, 3 STREAMING, 4 BUSY.
			*/
		    if (R(4) == com)
			{
				/* Cuatro words de estado. Sin error que informar van en cero;
				   dejarlos sin tocar le entregaba al guest lo que hubiera. */
				if (R(5))
				{
					DWORD	cero = 0;
					int		i;

					for (i = 0; i < 4; i++)
						WriteMemoryL(R(5) + i * 4, &cero);
				}

				R(0) = 2;			// COMPLETED
			}
		    else
				R(0) = 0;			// NO_ACTIVE: no hay tal peticion
		    break;

		    case 2: // GDROM_MAIN_LOOP
		    logmsg("GDROM_MAIN_LOOP\r\n");
		    break;

			case 3: // GDROM_INIT
			logmsg("GDROM_INIT\r\n");
			break;

			case 4: // GDROM_CHECK_DRIVE
			logmsg("GDROM_CHECK_DRIVE\r\n");
			// Los mismos codigos que usa el protocolo: 2 = standby, 6 =
			// bandeja abierta, 7 = sin disco. Asi --bandeja tambien vale por
			// este camino.
			valor = gdrom.unidad;
			WriteMemoryL(R(4), &valor);
			valor = (DWORD) gdrom.formato << 4;	// 0x10 = CD-ROM, 0x80 = GD-ROM
			WriteMemoryL(R(4) + 4, &valor);
			R(0) = 0;
			break;

			case 10: // GDROM_SECTOR_MODE
			logmsg("GDROM_SECTOR_MODE\r\n");
			ReadMemoryL(R(4), &valor);
			logmsg("valor: %x\r\n", valor);
			if (valor == 0)
			{
				valor = 8192;
				WriteMemoryL(R(4) + 4, &valor);
//				valor = 2048; // mode 2
				valor = 1024 * iso_get_mode();
				WriteMemoryL(R(4) + 8, &valor);
				valor = 2048; // sector size in bytes
				WriteMemoryL(R(4) + 12, &valor);
			}
			else
				logmsg("GDROM_SECTOR_MODE: valor != 0 no implementado\n");
			R(0) = 0;
			break;

			default:
			logmsg("GDROM: error!\n");
			break;
		}
	}
}

OPCODE(BIOS_HACK)
{
    logmsg("bios_hack\n");

    switch(PC)
    {
        case HACK_BASE + HACK_ROMFONT + 2:  hack_romfont(); break;
        case HACK_BASE + HACK_GDROM + 2:    hack_gdrom();   break;
        case HACK_BASE + HACK_FLASHROM + 2: hack_flashrom(); break;
        default:	logmsg("bios_hack: error\n"); break;
    }
    
    PC += 2;
}

