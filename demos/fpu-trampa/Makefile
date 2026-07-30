#
# Prueba de la trampa de excepcion de la FPU
#
# Se compila con el toolchain de KallistiOS, igual que los ejemplos del arbol
# de KOS. Ver README.md.
#

TARGET = fpu_trampa.elf
OBJS = fpu_trampa.o

include $(KOS_BASE)/Makefile.rules

all: rm-elf $(TARGET)

clean: rm-elf
	-rm -f $(OBJS) fpu_trampa.bin

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

# El binario plano que carga dcemu.
fpu_trampa.bin: $(TARGET)
	$(KOS_OBJCOPY) -O binary $(TARGET) fpu_trampa.bin

bin: fpu_trampa.bin

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
