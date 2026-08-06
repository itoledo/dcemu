# La demo de la instruccion de cierre de un volumen modificador.
#
#   make                -> volumen-incluir.elf
#   make EXCLUIR=1      -> volumen-excluir.elf
#
# Se compila desde el shell mingw64 de MSYS2 con el entorno de KOS puesto
# (source /opt/toolchains/dc/kos/environ.sh). Ver volumen.c.

ifdef EXCLUIR
TARGET = volumen-excluir.elf
KOS_CFLAGS += -DEXCLUIR=1
OBJS = volumen-excluir.o
else
TARGET = volumen-incluir.elf
OBJS = volumen-incluir.o
endif

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f volumen-incluir.o volumen-excluir.o

rm-elf:
	-rm -f $(TARGET)

volumen-incluir.o: volumen.c
	kos-cc $(KOS_CFLAGS) -c volumen.c -o $@

volumen-excluir.o: volumen.c
	kos-cc $(KOS_CFLAGS) -c volumen.c -o $@

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
