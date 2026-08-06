# La demo del buffer de acumulacion secundario del TSP.
#
#   make               -> acum-directo.elf
#   make ACUMULAR=1    -> acum-acumulado.elf
#
# Se compila desde el shell mingw64 de MSYS2 con el entorno de KOS puesto
# (source /opt/toolchains/dc/kos/environ.sh). Ver acumulador.c.

ifdef ACUMULAR
TARGET = acum-acumulado.elf
KOS_CFLAGS += -DACUMULAR=1
OBJS = acum-acumulado.o
else
TARGET = acum-directo.elf
OBJS = acum-directo.o
endif

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f acum-directo.o acum-acumulado.o

rm-elf:
	-rm -f $(TARGET)

acum-directo.o: acumulador.c
	kos-cc $(KOS_CFLAGS) -c acumulador.c -o $@

acum-acumulado.o: acumulador.c
	kos-cc $(KOS_CFLAGS) -c acumulador.c -o $@

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
