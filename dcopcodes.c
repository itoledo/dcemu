#include "main.h"
#include "sh4emu.h"
#include "opcodes.h"
#include <math.h>
#include "intc.h"		/* el fin de DMA que levanta REQ_DMA_TRANS */
#include "iso.h"
#include "gdrom.h"
#include "sistema.h"
#include "traza.h"

#define PI 3.14159265358979323846

DWORD com=0;

/* La ultima lectura servida por el hook: su peticion y cuanto movio, para que
   GDROM_CHECK_COMMAND pueda informar lo transferido. */
static DWORD com_lectura = 0;
static DWORD com_lectura_bytes = 0;

/* Lo que movio la ULTIMA peticion, y sobrevive a que se la consuma: es lo que
   CHECK_COMMAND informa en su tercera word, incluida la consulta final que el
   driver hace ya sin peticion viva. Ver el caso 1. */
static DWORD com_transferido = 0;

/* La peticion aceptada y todavia no consultada. El driver de la BIOS acepta
   UNA por vez: un SEND_COMMAND con otra viva se rechaza con 0, y el guest
   reintenta. Aceptarlo todo al instante parecia inofensivo hasta Capcom vs.
   SNK: manda su lectura larga, y sin consultarla todavia manda el sondeo de
   subcodigo -- en la consola ese segundo SEND rebota, aca se aceptaba, el
   "comando actual" de Katana pasaba al sondeo y la lectura quedaba huerfana:
   su CHECK_COMMAND no llegaba nunca y la capa de CRI esperaba el fin de una
   lectura ya hecha para siempre. */
static DWORD com_viva = 0;

/* La lectura en flujo del MULTI_DMAREAD (comando 38): no lleva destino -- la
   peticion queda en CONTINUE (3, el "STREAMING" del driver de la BIOS) y el
   guest tira de a pedazos con REQ_DMA_TRANS (r7=6), cada pedazo con su
   {destino, tamano}. Es el camino por el que Windows CE carga TODO binario
   grande -- el .text de DCDOOM.EXE y sus DLL del CD --, y contestarle
   COMPLETED sin datos dejaba esas paginas en cero: el loader les aplicaba
   las reubicaciones encima y el primer DllMain ejecutaba basura. La
   numeracion de funciones y la semantica estan verificadas contra el HLE de
   flycast (core/reios/gdrom_hle.*), que corre esta misma plataforma. */
static DWORD multi_id = 0;			/* peticion multi viva; 0 si ninguna */
static DWORD multi_sector = 0;		/* proximo sector del flujo */
static DWORD multi_desplaz = 0;		/* offset dentro de ese sector */
static DWORD multi_restante = 0;	/* bytes que faltan entregar */
static DWORD multi_total = 0;
static DWORD multi_callback = 0;	/* lo registra G1_DMA_END (r7=5) o
									   SET_PIO_CALLBACK (r7=11) */
static DWORD multi_callback_arg = 0;

/* El pedazo PIO pedido (r7=12) se LATCHEA y nada mas: la copia y el aviso al
   callback ocurren en el proximo MAINLOOP (r7=2), que la bomba de wsegacd
   llama justo despues desde su propio hilo. Hacerlo en el momento y avisar en
   el MAINLOOP que tocara despues parecia equivalente y no lo es: el aviso
   caia en el MAINLOOP de otro proceso, el argumento del callback es una VA
   relativa al proceso, y el kernel moria en panico. Es el modelo exacto del
   gdGdcExecServer real (y el del HLE de flycast). */
static DWORD pio_destino = 0;
static DWORD pio_tam = 0;
static int   pio_pedido = 0;

/*
	Escritura traducida por pedazos que no cruzan pagina. memwrite() traduce
	UNA vez por llamada y escribe fisico contiguo desde ahi, lo que para las
	instrucciones del guest (1 a 8 bytes, nunca cruzan pagina) es correcto --
	pero un bloque grande que cruza paginas virtuales NO contiguas en fisico
	rocia las paginas fisicas vecinas de la primera. La pieza de 36 KB del
	flujo PIO de DCDoom, escrita a una pila de usuario de Windows CE, piso
	el directorio de paginas de su propio proceso con datos del WAD (la
	entrada leida despues por el kernel decia "SW17", un nombre de lump de
	DOOM), y el kernel moria por doble excepcion con "Halting system". 1 KB
	es la pagina minima del SH-4, y todo tamano de pagina es multiplo suyo.
*/
static void memwrite_paginado(DWORD destino, char * origen, DWORD tam)
{
	DWORD hecho = 0;

	while (hecho < tam)
	{
		DWORD trozo = 0x400 - ((destino + hecho) & 0x3FF);

		if (trozo > tam - hecho)
			trozo = tam - hecho;

		memwrite(destino + hecho, origen + hecho, trozo);
		hecho += trozo;
	}
}

OPCODE(fsca) // FSCA FPUL, DRn
{
//	long n = (arg >> 8) & 0x0F;
	long n = (arg >> 9) & 0x07;

	/*
		**El angulo son los 16 bits bajos de FPUL, no FPUL entero.** Es un punto
		fijo donde 65536 es la vuelta completa, asi que la parte alta no es parte
		del angulo: tomarla daba un x de miles de millones de radianes y un seno
		y un coseno que no tienen nada que ver.

		Ademas FPUL es DWORD, o sea que el (float) de antes lo convertia **sin
		signo**: un FPUL negativo salia como un numero enorme positivo. Con la
		mascara de 16 bits eso deja de importar.

		Lo miden las 500 pruebas de 1111nnn011111101, que fallaban todas.
	*/
	/* El angulo se calcula y se usa en **doble**: redondearlo a float mete un
	   error de hasta 2^-24 en radianes, que sale entero en el seno y el coseno
	   -- hasta 4e-7, mas de lo que el chip se aparta del valor exacto. */
	double x = 2 * PI * (double) (FPUL & 0xFFFF) / 65536.0;

	FR(n*2) = (float) sin(x);
	FR(n*2+1) = (float) cos(x);
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
	/*
		Sin find_opcode(PC): eso volvia a **leer la instruccion de memoria** para
		nombrarla, y una lectura que la instruccion no hace es una lectura de
		mas -- la ve un watchpoint de lectura, y con la MMU encendida puede
		fallar y abortar una instruccion que no accede a nada. Ademas logmsg()
		es una funcion, no una macro, asi que el argumento se evaluaba siempre,
		con LOGGING apagado o no.

		El nombre tampoco servia: aqui solo se llega con patrones que no son
		instrucciones, o sea que siempre decia "NOIMP". El opcode crudo si dice
		algo.

		Lo miden las 1000 pruebas de 1111mmm010111101 y 1111nnn010101101, que
		son FCNVDS y FCNVSD con PR=0: el manual las declara reservadas y lo
		unico que se observa es que PC avanza dos.
	*/
	logmsg("opcode no implementado: %04x en PC %08x\r\n", arg, PC);
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
	El syscall que dcemu no atiende: el que el ROM deja en 0x8C0000E0, sin
	nombre conocido.

	Antes era RTS + NOP: volvia sin hacer nada y **sin decirlo**, que es la
	forma que tuvo cada uno de los agujeros de este arbol -- algo que el guest
	pide, que se le contesta sin querer decir nada, y que no deja rastro. Con
	esto sigue sin hacer nada, pero se ve: un juego que se cuelgue despues de
	llamarlo deja la linea en --traza-mem y ya no hay que sospecharlo.

	R0 queda en 0 en vez de en lo que hubiera: un puntero de vuelta con basura
	es peor que uno nulo, porque el guest lo sigue.
*/
static void hack_mudo(const char * nombre)
{
	logmsg("%s: llamado, func=%d\r\n", nombre, R(7));

	if (traza_activa)
		fprintf(stderr, "syscall %s sin emular: R4 %08lx R5 %08lx R6 %08lx"
			" R7 %08lx, PC %08lx, PR %08lx\n", nombre,
			(unsigned long) R(4), (unsigned long) R(5),
			(unsigned long) R(6), (unsigned long) R(7),
			(unsigned long) PC, (unsigned long) PR);

	R(0) = 0;
}

/*
	El syscall SYSINFO, vector 0x8C0000B0. La numeracion esta confirmada contra
	KOS (kernel/arch/dreamcast/hardware/syscalls.c): 0 INIT, 2 ICON, 3 ID, con
	la convencion de siempre -- syscall(r4, r5, r6, func) y el resultado en R0.

	La funcion 3 devuelve **un puntero** al identificador de 8 bytes de la
	consola, no el identificador: syscall_sysinfo_id() lo desreferencia. En la
	consola ese puntero es 0x8C000068, adonde INIT lo copio de la flash; dcemu
	deja lo mismo en SYSID_BASE desde main(), asi que INIT no tiene nada que
	hacer y la 3 contesta esa direccion. Contestar 0 no era neutro: Crazy Taxi
	sigue el puntero y copia su "identificador" escribiendo alrededor de la
	direccion 0x10.

	La funcion 2 (el icono de la VMU, desde la flash) sigue sin implementar,
	pero se ve por --traza-mem, igual que antes.
*/
static void hack_sysinfo(void)
{
	switch (R(7))
	{
		case 0:					/* INIT: en el ROM, copiar el ID de la flash a
								   0x8C000068. main() ya lo dejo hecho. */
		R(0) = 0;
		break;

		case 3:					/* ID: el puntero al identificador */
		R(0) = SYSID_BASE;
		break;

		default:
		hack_mudo("SYSINFO");
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

			/* Una peticion viva por vez, como el driver de la BIOS: si la
			   anterior no se consulto todavia, esta se rechaza sin hacer el
			   trabajo y el guest la reintentara. */
			if (com_viva != 0)
			{
				R(0) = 0;
				break;
			}

			/* El identificador de esta peticion. Crece de a uno desde 0x6969:
			   con un id fijo, dos peticiones consecutivas se confunden en la
			   contabilidad del guest. El trabajo se hace en el momento, aca
			   abajo. */
			com = (com < 0x6969) ? 0x6969 : com + 1;

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

					if (traza_activa)
					{
						int i;

						fprintf(stderr, "hack: leer %d sectores desde %d a %08lx:",
							secnum, secstart, (unsigned long) targetaddr);

						for (i = 0; i < 8; i++)
							fprintf(stderr, " %02x", (BYTE) targetmem[i]);

						fprintf(stderr, "\n");
					}

					/*
						El destino de la 17 es una direccion FISICA -- el G1 DMA
						de la consola no pasa por la MMU -- y el de la 16 es la
						escritura por CPU del driver, o sea virtual. La
						diferencia se ve recien con la MMU encendida: Windows CE
						pide la 17 con 0x0CF77000 fisico, y traducirlo como
						virtual lo mandaba a la ranura 6 de otro proceso, sin
						mapear. Los juegos de Katana pasan P1 (0x8C...), que
						resuelve igual por los dos caminos.
					*/
					if (R(4) == 17)
						memwrite_fisico(targetaddr, &targetmem[0], 2048 * secnum);
					else
						/* Traducida y por paginas: ver memwrite_paginado(). */
						memwrite_paginado(targetaddr, &targetmem[0], 2048 * secnum);

					free(targetmem);

					/* Para que CHECK_COMMAND pueda informar lo transferido. */
					com_lectura = com;
					com_transferido = 2048 * (DWORD) secnum;
					com_lectura_bytes = 2048 * (DWORD) secnum;
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

				case 24: // INIT: arrancar la lectora
				/* En la consola pone en marcha el motor y deja la unidad lista.
				   Aqui gdrom_iniciar() ya la dejo en el estado que pidio
				   --bandeja y los sectores salen de iso.c, no de un motor, asi
				   que no queda nada por hacer: lo que importa es contestar que
				   salio bien. Es el primer comando que manda Crazy Taxi.
				   Corta un flujo pendiente, como el driver real. */
				logmsg("GDROM_INIT_DRIVE\r\n");
				multi_id       = 0;
				multi_restante = 0;
				multi_callback = 0;
				pio_pedido     = 0;
				break;

				case 40: // GET_VERS: la version del driver del GD-ROM
				{
					/* Los 32 bytes que trae el boot ROM en bios.bin+0x3b60,
					   rellenados con espacios y sin NUL, que es lo que contesta
					   una consola. El unico parametro apunta al destino. */
					static const char version[] = "GDC Version 1.01 1998-09-30 MP  ";
					DWORD destino = 0;

					memread(R(5), &destino, sizeof(DWORD));

					if (destino)
						memwrite(destino, (void *) version, 32);	/* sin el NUL */

					logmsg("GDROM_GET_VERS: a %x\r\n", destino);
				}
				break;

				case 34: // GETSCD: el subcodigo Q, como el paquete SPI GET_SCD
				{
					/* Parametros {formato, tamano, destino}, como los pasa el
					   driver de la BIOS (y reicast los lee igual). La respuesta
					   lleva el encabezado del SPI: [0] reservado, [1] estado de
					   audio, [2..3] largo, y detras el subcodigo del formato
					   pedido. Sin CD-DA sonando el estado es 0x15 --"sin
					   informacion de audio"-- y la posicion es el track de
					   datos. Contestar COMPLETED sin escribir el bufer dejaba
					   el estado en 0x00, que no es ningun codigo, y la capa de
					   CRI de Capcom vs. SNK repetia el sondeo para siempre. */
					DWORD	formato = 0, tam = 0, destino = 0;
					BYTE	scd[100];

					memread(R(5), &formato, sizeof(DWORD));
					memread(R(5) + 4, &tam, sizeof(DWORD));
					memread(R(5) + 8, &destino, sizeof(DWORD));

					if (tam > sizeof(scd))
						tam = sizeof(scd);

					memset(scd, 0, sizeof(scd));
					scd[0] = (BYTE) formato;
					scd[1] = 0x15;					/* sin estado de audio */
					scd[2] = (BYTE) (tam >> 8);
					scd[3] = (BYTE) tam;

					if (formato == 1 && tam >= 14)
					{
						/* Solo la Q: control/ADR, track, indice, y las dos
						   posiciones en FAD de 24 bits. Un track de datos
						   parado al principio del area de programa. */
						scd[4] = 0x41;				/* datos, ADR = posicion */
						scd[5] = 1;					/* track */
						scd[6] = 1;					/* indice */
						scd[7] = 0; scd[8] = 0; scd[9] = 0;		/* transcurrido */
						scd[10] = 0;
						scd[11] = 0; scd[12] = 0; scd[13] = 150;	/* FAD absoluto */
					}

					if (destino && tam)
						memwrite(destino, scd, tam);

					if (traza_activa)
					{
						static int visto = 0;

						if (!visto)
						{
							visto = 1;
							fprintf(stderr, "hack: GETSCD formato=%lu tam=%lu "
								"a %08lx, estado 0x15\n",
								(unsigned long) formato, (unsigned long) tam,
								(unsigned long) destino);
						}
					}

					logmsg("GDROM_GETSCD: formato %d, %d bytes a %x\r\n",
						formato, tam, destino);
				}
				break;

				case 38: // GDCC_MULTI_DMAREAD: lectura en flujo, sin destino
				case 39: // GDCC_MULTI_PIOREAD: lo mismo, pero se tira con la CPU
				{
					/* Los dos dejan el mismo estado: la peticion queda en
					   CONTINUE y el guest tira de a pedazos -- por r7=6 el 38,
					   por r7=12 el 39. El "adelanto" es el Next Address del
					   CD_READ2 (31h) del protocolo SPI (docs/cdif131e.pdf,
					   8.2): la posicion de pre-lectura, cuyo error no se
					   informa en este comando; aca no hay nada que adelantar.
					   DCDoom recorre DOOM.WAD entero por el 39. */
					int		secstart = 0, secnum = 0;
					DWORD	adelanto = 0;	/* seekAhead; solo Windows CE lo pasa */

					memread(R(5), &secstart, sizeof(int));
					memread(R(5) + 4, &secnum, sizeof(int));
					memread(R(5) + 8, &adelanto, sizeof(DWORD));

					multi_id       = com;
					multi_sector   = (DWORD) secstart;
					multi_desplaz  = 0;
					multi_restante = 2048 * (DWORD) secnum;
					multi_total    = multi_restante;
					com_transferido = 0;

					if (traza_activa)
						fprintf(stderr, "hack: MULTI_%sREAD %d sectores desde %d"
							" (flujo, adelanto=%lu)\n",
							(R(4) == 38) ? "DMA" : "PIO",
							secnum, secstart, (unsigned long) adelanto);
				}
				break;

				case 36: // GDCC_REQ_STAT: el estado del drive, en cuatro punteros
				{
					/* {estado | repeticiones<<8, track, (adr<<28)|(ctrl<<24)|fad,
					   indice}, cada uno a su puntero. Windows CE lo sondea
					   periodicamente desde wsegacd. Mismos valores ficticios que
					   GETSCD: parado al principio del area de programa. */
					DWORD	destino[4] = { 0, 0, 0, 0 };
					DWORD	valores[4];
					int		i;

					for (i = 0; i < 4; i++)
						memread(R(5) + i * 4, &destino[i], sizeof(DWORD));

					valores[0] = (gdrom.unidad == 2) ? 1 : gdrom.unidad;
					valores[1] = 1;								/* track */
					valores[2] = (1u << 28) | (4u << 24) | 150;	/* adr|ctrl|fad */
					valores[3] = 1;								/* indice */

					for (i = 0; i < 4; i++)
						if (destino[i])
							memwrite(destino[i], &valores[i], sizeof(DWORD));

					if (traza_activa)
					{
						static int visto = 0;

						if (!visto)
						{
							visto = 1;
							fprintf(stderr, "hack: REQ_STAT a %08lx %08lx %08lx"
								" %08lx, estado %lu\n",
								(unsigned long) destino[0], (unsigned long) destino[1],
								(unsigned long) destino[2], (unsigned long) destino[3],
								(unsigned long) valores[0]);
						}
					}
				}
				break;

				default:
				/* El guest se lleva un identificador valido igual, asi que da
				   su peticion por hecha. Nombrar el comando que falta -- con sus
				   parametros: la corrida determinista convierte ese volcado en
				   la semantica del comando -- es lo unico que impide que eso
				   pase inadvertido. Asi se encontro el 38 que dejaba a Windows
				   CE sin el codigo de sus DLL. */
				logmsg("GDROM_SEND_COMMAND: comando %d sin implementar\r\n", R(4));

				if (traza_activa)
				{
					DWORD	parametros[6];
					int		i;

					for (i = 0; i < 6; i++)
					{
						parametros[i] = 0;

						if (R(5))
							memread(R(5) + i * 4, &parametros[i], sizeof(DWORD));
					}

					fprintf(stderr, "hack: comando %lu del GD-ROM sin implementar,"
						" parametros %08lx %08lx %08lx %08lx %08lx %08lx\n",
						(unsigned long) R(4),
						(unsigned long) parametros[0], (unsigned long) parametros[1],
						(unsigned long) parametros[2], (unsigned long) parametros[3],
						(unsigned long) parametros[4], (unsigned long) parametros[5]);
				}
   				break;
			}
			com_viva = com;
			R(0) = com;
			break;

		    case 1: // GDROM_CHECK_COMMAND
		    logmsg("GDROM_CHECK_COMMAND: r4=%x, r5=%x\r\n", R(4), R(5));
			/*
				gdGdcGetCmdStat(peticion, estado). Como SEND_COMMAND hace el
				trabajo en el momento, cuando el guest pregunta el comando ya
				termino: COMPLETED, para cualquier id que este hook haya
				repartido. Devolver NO_ACTIVE la primera vez que se preguntaba
				por una peticion --que es lo que hacia-- se lee como "esa
				peticion no existe", y Crazy Taxi respondia reintentando la
				inicializacion entera, para siempre.

				Los codigos son los del driver de la BIOS: 0 NO_ACTIVE,
				1 PROCESSING, 2 COMPLETED, 3 STREAMING, 4 BUSY.
			*/
			/*
				Las cuatro words de estado se escriben SIEMPRE, antes de mirar
				si la peticion sigue viva -- como el HLE de flycast, cuyo
				`result[]` persiste entre consultas. El driver de la BIOS hace
				una consulta final DESPUES de haber cobrado el COMPLETED, y es
				de esa de donde saca el conteo que le devuelve a su llamador:
				no escribir nada ahi le entregaba un cero, y el guest lo cree.
				DCDoom lo dijo con todas las letras -- "W_ReadLump: only read
				0 of 17544 on lump 1968" -- con los 18432 bytes ya copiados a
				su buffer. Sin error que informar las otras tres van en cero;
				dejarlas sin tocar le entrega al guest lo que hubiera.
			*/
			if (R(5))
			{
				DWORD	cero = 0;
				DWORD	transferido = com_transferido;
				int		i;

				for (i = 0; i < 4; i++)
					WriteMemoryL(R(5) + i * 4, &cero);

				WriteMemoryL(R(5) + 2 * 4, &transferido);
			}

		    if (R(4) != 0 && R(4) == com_viva)
			{

				/* Un pedazo PIO latcheado y todavia no copiado es PROCESSING
				   (1), no CONTINUE: con 3 la bomba de wsegacd entiende "datos
				   ya entregados", se duerme a esperar el callback, y el
				   MAINLOOP que haria la copia y lo llamaria no llega nunca --
				   abrazo mortal medido con DCDoom en el ultimo pedazo del
				   directorio de DOOM.WAD. Con 1 sigue en su bucle
				   MAINLOOP+CHECK, que es lo que hace girar la copia. El HLE
				   de flycast contesta exactamente esto ("Bust-a-move 4 likes
				   this"). */
				if (R(4) == multi_id && pio_pedido)
				{
					R(0) = 1;		// PROCESSING
					break;
				}

				/* Un MULTI_DMAREAD con datos por entregar sigue vivo: CONTINUE
				   (el "STREAMING" del driver), y la peticion no se libera --
				   los REQ_DMA_TRANS que vienen se validan contra su id. */
				if (R(4) == multi_id && multi_restante > 0)
				{
					R(0) = 3;		// CONTINUE
					break;
				}

				/* Consultada: el driver queda libre para la proxima. */
				com_viva = 0;

				if (R(4) == multi_id)
					multi_id = 0;

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
			/* Sin exito explicito, R0 se lleva lo que hubiera: el init de
			   maple.dll de Windows CE es quien mira este retorno. */
			R(0) = 0;
			break;

			case 4: // GDROM_CHECK_DRIVE
			logmsg("GDROM_CHECK_DRIVE\r\n");
			// Los mismos codigos que usa el protocolo: 2 = standby, 6 =
			// bandeja abierta, 7 = sin disco. Asi --bandeja tambien vale por
			// este camino.
			valor = gdrom.unidad;
			WriteMemoryL(R(4), &valor);
			/* El tipo de disco. Con disco puesto se contesta GD-ROM, no lo que
			   diga la imagen: este camino reemplaza consola, BIOS y lectora
			   para correr el juego montado, y el disco original de un juego
			   comercial es un GD-ROM. El SDK de Katana lo exige literalmente
			   --gdFsInit() compara este word contra 0x80 y ante otra cosa
			   devuelve -5 y el juego reintenta la inicializacion para
			   siempre, con la carga clavada en "LOADING (31K)"--, y lo hacen
			   los dos rips de Crazy Taxi, el de layout GD y el MIL-CD: ningun
			   selfboot trae ese chequeo parcheado. El camino por --bios no
			   pasa por aca y sigue viendo el CD que la imagen es, que es lo
			   que su rama de MIL-CD necesita. */
			if (gdrom.unidad == GD_OPEN || gdrom.unidad == GD_NODISC)
				valor = (DWORD) gdrom.formato << 4;
			else
				valor = GD_DISCO_GDROM << 4;	/* 0x80 */
			WriteMemoryL(R(4) + 4, &valor);
			R(0) = 0;
			break;

			case 5: // GDROM_G1_DMA_END: registrar el callback de fin de DMA
			/* En la consola el driver lo llama cuando termina un pedazo del
			   flujo; aca el aviso va por la interrupcion (ASIC_EVT_GDROM_DMA)
			   que REQ_DMA_TRANS levanta, asi que solo se registra. */
			multi_callback     = R(4);
			multi_callback_arg = R(5);
			R(0) = 0;
			break;

			case 6: // GDROM_REQ_DMA_TRANS: un pedazo del flujo del MULTI_DMAREAD
			{
				/* r4 es el id de la peticion multi y r5 apunta a {destino,
				   tamano}. El destino es fisico, como el de la lectura 17: el
				   G1 DMA de la consola no pasa por la MMU. */
				DWORD	destino = 0, tam = 0;

				memread(R(5), &destino, sizeof(DWORD));
				memread(R(5) + 4, &tam, sizeof(DWORD));

				if (R(4) != multi_id || multi_id == 0
					|| tam == 0 || tam > multi_restante)
				{
					R(0) = (DWORD) -1;		// GDC_ERR
					break;
				}

				{
					char	sector[2048];
					DWORD	hecho = 0;

					while (hecho < tam)
					{
						DWORD trozo = 2048 - multi_desplaz;

						if (trozo > tam - hecho)
							trozo = tam - hecho;

						iso_read_sector(sector, multi_sector, 1);
						memwrite_fisico(destino + hecho,
							&sector[multi_desplaz], trozo);

						hecho          += trozo;
						multi_desplaz  += trozo;

						if (multi_desplaz >= 2048)
						{
							multi_desplaz = 0;
							multi_sector++;
						}
					}

					multi_restante -= tam;
					com_transferido = multi_total - multi_restante;
				}

				if (traza_activa)
				{
					static int vistos = 0;

					if (vistos < 8)
					{
						vistos++;
						fprintf(stderr, "hack: REQ_DMA_TRANS %lu bytes a %08lx,"
							" quedan %lu\n", (unsigned long) tam,
							(unsigned long) destino,
							(unsigned long) multi_restante);
					}
				}

				/* El fin de DMA, con el retraso proporcional de siempre: al
				   instante, la interrupcion le gana al driver que vuelve del
				   disparo (la leccion del CH2 DMA con Virtua Tennis). */
				intc_add(ASIC_EVT_GDROM_DMA, tam / 200 + 10);

				/* El pedazo que vacia el flujo termina el COMANDO, y el fin de
				   comando es la interrupcion externa del GD-ROM, como en la
				   lectora real. La bomba de wsegacd espera esa señal para
				   volver a consultar (CHECK) y ver el COMPLETED: sin ella,
				   consulto una vez el CONTINUE y no vuelve nunca. */
				if (multi_restante == 0)
					intc_add_ext(ASIC_EVT_EXT_GDROM);

				R(0) = 0;				// GDC_OK
			}
			break;

			case 7: // GDROM_CHECK_DMA_TRANS: cuanto queda del flujo
			{
				/* Mientras la peticion multi siga viva la respuesta es OK, con
				   el resto informado -- aunque ya sea cero. Lo que cierra el
				   flujo es el COMPLETED del CHECK_COMMAND, no vaciarlo: en el
				   HLE de flycast el estado sigue en CONTINUE hasta esa
				   consulta. Contestar error o BUSY en la ultima vuelta le dice
				   al driver que la transferencia fallo. */
				DWORD	restante = multi_restante;

				if (R(5))
					WriteMemoryL(R(5), &restante);

				R(0) = (multi_id != 0) ? 0 : 1;
			}
			break;

			case 11: // GDROM_SET_PIO_CALLBACK: como el 5, para el flujo por CPU
			multi_callback     = R(4);
			multi_callback_arg = R(5);
			R(0) = 0;
			break;

			case 12: // GDROM_REQ_PIO_TRANS: un pedazo del flujo, copiado por CPU
			{
				/* El gemelo del 6 para el MULTI_PIOREAD (39), con dos
				   diferencias. El destino es una direccion VIRTUAL -- lo
				   escribe la CPU del driver, no el G1 DMA --, el espejo de la
				   regla de las lecturas 16 (PIO, traducida) y 17 (DMA,
				   fisica). Y aca solo se VALIDA y LATCHEA: la copia y el
				   callback van en el MAINLOOP -- ver pio_pedido arriba. */
				DWORD	destino = 0, tam = 0;

				memread(R(5), &destino, sizeof(DWORD));
				memread(R(5) + 4, &tam, sizeof(DWORD));

				if (R(4) != multi_id || multi_id == 0 || tam > multi_restante)
				{
					if (traza_activa)
						fprintf(stderr, "hack: REQ_PIO_TRANS rechazado:"
							" id=%lx (vivo %lx), %lu bytes a %08lx con %lu"
							" restantes\n", (unsigned long) R(4),
							(unsigned long) multi_id, (unsigned long) tam,
							(unsigned long) destino,
							(unsigned long) multi_restante);

					R(0) = (DWORD) -1;		// GDC_ERR
					break;
				}

				pio_destino = destino;
				pio_tam     = tam;
				pio_pedido  = 1;

				if (traza_activa)
				{
					static int vistos = 0;

					if (vistos < 8)
					{
						vistos++;
						fprintf(stderr, "hack: REQ_PIO_TRANS %lu bytes a %08lx"
							" (latcheado, quedan %lu)\n", (unsigned long) tam,
							(unsigned long) destino,
							(unsigned long) multi_restante);
					}
				}

				R(0) = 0;				// GDC_OK
			}
			break;

			case 13: // GDROM_CHECK_PIO_TRANS: cuanto queda del flujo por CPU
			{
				/* Igual que el 7 en cuanto a cuando vale, y con la asimetria
				   del driver: sin peticion viva no informa nada y contesta
				   error. Vaciar el flujo NO la mata -- eso lo hace el
				   COMPLETED del CHECK_COMMAND --, y contestar error en la
				   ultima vuelta le decia al driver que la lectura fallo:
				   DCDoom recibia sus 18432 bytes y su W_ReadLump informaba
				   "only read 0 of 17544". */
				DWORD	restante = multi_restante;

				if (multi_id != 0)
				{
					if (R(5))
						WriteMemoryL(R(5), &restante);

					R(0) = 0;			// GDC_OK
				}
				else
					R(0) = (DWORD) -1;	// GDC_ERR
			}
			break;

			case 8: // GDROM_READ_ABORT
			if (R(4) != 0 && (R(4) == com_viva || R(4) == multi_id))
			{
				if (R(4) == multi_id)
				{
					multi_id       = 0;
					multi_restante = 0;
					pio_pedido     = 0;
				}

				com_viva = 0;
				R(0) = 0;
			}
			else
				R(0) = (DWORD) -1;
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
			/* Una funcion del vector que falta es la misma enfermedad que un
			   comando que falta: nombrarla o no enterarse. */
			logmsg("GDROM: error!\n");

			if (traza_activa)
			{
				static int vistos = 0;

				if (vistos < 8)
				{
					vistos++;
					fprintf(stderr, "hack: funcion r7=%lu del vector GD-ROM"
						" sin implementar (r4=%lx r5=%lx r6=%lx)\n",
						(unsigned long) R(7), (unsigned long) R(4),
						(unsigned long) R(5), (unsigned long) R(6));
				}
			}
			break;
		}
	}

	/*
		El retorno del stub, que no es RTS (ver main.c): de ordinario, volver
		al llamador. La excepcion es el MAINLOOP (r7=2) con un pedazo PIO
		latcheado: como en el gdGdcExecServer real, el servidor hace aqui la
		copia y "llama" al callback -- PC va al callback con R4 = su argumento
		y PR intacto, asi el RTS del callback vuelve al llamador del MAINLOOP,
		que es la propia bomba de wsegacd: el mismo hilo y el mismo proceso
		que pidieron el pedazo, unica manera de que la VA del argumento
		signifique lo que debe. Sin este aviso la bomba consulta dos veces,
		espera para siempre y el juego termina abortando el flujo (asi se
		quedo DCDoom sin leer DOOM.WAD). Es el modelo del HLE de flycast.
	*/
	if (R(6) == 0 && R(7) == 2 && pio_pedido && multi_id != 0)
	{
		/* Transaccional: el memwrite traducido puede fallar por TLB y abortar
		   la instruccion entera -- el guest recarga y reejecuta este MAINLOOP,
		   o sea esto desde cero --, asi que el estado del flujo se toca solo
		   con la copia completa hecha. Reescribir los mismos bytes en la
		   reejecucion es inocuo. */
		char	sector[2048];
		DWORD	hecho    = 0;
		DWORD	sect     = multi_sector;
		DWORD	desplaz  = multi_desplaz;

		/*
			Tocar el destino ANTES de mover nada. El buffer del llamador es
			una direccion virtual de otra ranura de CE y su pagina puede no
			estar en la TLB: la escritura fallaria a mitad de la copia,
			abortaria la instruccion y el guest reejecutaria este stub -- con
			la bomba del driver ya a mitad de camino, que es lo que le hacia
			informar "only read 0 of 17544" con los bytes ya en su buffer.
			Leyendo primero, el fallo ocurre aca, con el estado del flujo
			todavia intacto: CE recarga la TLB, reejecuta el stub y la copia
			entera pasa de una.
		*/
		{
			DWORD p;

			for (p = 0; p < pio_tam; p += 0x400)
			{
				BYTE b;
				memread(pio_destino + p, &b, 1);
			}

			if (pio_tam > 0)
			{
				BYTE b;
				memread(pio_destino + pio_tam - 1, &b, 1);
			}
		}

		while (hecho < pio_tam)
		{
			DWORD trozo = 2048 - desplaz;

			if (trozo > pio_tam - hecho)
				trozo = pio_tam - hecho;

			iso_read_sector(sector, sect, 1);
			memwrite_paginado(pio_destino + hecho, &sector[desplaz], trozo);

			hecho   += trozo;
			desplaz += trozo;

			if (desplaz >= 2048)
			{
				desplaz = 0;
				sect++;
			}
		}

		multi_sector    = sect;
		multi_desplaz   = desplaz;
		multi_restante -= pio_tam;
		com_transferido = multi_total - multi_restante;
		pio_pedido      = 0;

		/* El fin de COMANDO, cuando el flujo se vacia, es la interrupcion
		   externa de la lectora, como en el 6. Por pieza no hay ninguna: el
		   aviso PIO es el callback. */
		if (multi_restante == 0)
			intc_add_ext(ASIC_EVT_EXT_GDROM);

		if (traza_activa)
		{
			static int vistos = 0;

			if (vistos < 8)
			{
				vistos++;
				fprintf(stderr, "hack: MAINLOOP copia %lu bytes a %08lx"
					" (quedan %lu) y llama al callback %08lx(%08lx)\n",
					(unsigned long) pio_tam, (unsigned long) pio_destino,
					(unsigned long) multi_restante,
					(unsigned long) multi_callback,
					(unsigned long) multi_callback_arg);
			}
		}

		if (multi_callback != 0)
		{
			R(4) = multi_callback_arg;
			PC   = multi_callback;
			return;
		}
	}

	PC = PR;
}

OPCODE(BIOS_HACK)
{
    logmsg("bios_hack\n");

    switch(PC)
    {
        case HACK_BASE + HACK_ROMFONT + 2:  hack_romfont(); break;
        /* El stub del GD-ROM lleva el ilegal en el offset 0, sin RTS, y
           hack_gdrom() fija el PC del retorno el mismo -- ver main.c. */
        case HACK_BASE + HACK_GDROM:        hack_gdrom();   return;
        /* La entrada fija que el word de 8C0000C0 apunta en el ROM real: el
           mismo servicio del GD-ROM, llamado por direccion. Ver mem.h. */
        case HACK_GDROM_FIJO:               hack_gdrom();   return;
        case HACK_BASE + HACK_FLASHROM + 2: hack_flashrom(); break;
        case HACK_BASE + HACK_SYSINFO + 2:  hack_sysinfo();  break;
        case HACK_BASE + HACK_UNKNOWN + 2:  hack_mudo("UNKNOWN");   break;
        default:	logmsg("bios_hack: error\n"); break;
    }

    PC += 2;
}

