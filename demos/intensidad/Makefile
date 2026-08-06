# La demo del color de cara y los vertices en modo intensidad.
#
#   make                -> int-empaquetado.elf
#   make INTENSIDAD=1      -> int-intensidad.elf
#
# Se compila desde el shell mingw64 de MSYS2 con el entorno de KOS puesto
# (source /opt/toolchains/dc/kos/environ.sh). Ver intensidad.c.

ifdef INTENSIDAD
TARGET = int-intensidad.elf
KOS_CFLAGS += -DINTENSIDAD=1
OBJS = int-intensidad.o
else
TARGET = int-empaquetado.elf
OBJS = int-empaquetado.o
endif

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f int-empaquetado.o int-intensidad.o

rm-elf:
	-rm -f $(TARGET)

int-empaquetado.o: intensidad.c
	kos-cc $(KOS_CFLAGS) -c intensidad.c -o $@

int-intensidad.o: intensidad.c
	kos-cc $(KOS_CFLAGS) -c intensidad.c -o $@

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
