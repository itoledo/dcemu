#
# Prueba del mapeo y la reejecucion de la MMU
#
# Se compila con el toolchain de KallistiOS, igual que los ejemplos del arbol
# de KOS. Ver README.md.
#

TARGET = mmu_mapeo.elf
OBJS = mmu_mapeo.o

include $(KOS_BASE)/Makefile.rules

all: rm-elf $(TARGET)

clean: rm-elf
	-rm -f $(OBJS) mmu_mapeo.bin

rm-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

# El binario plano que carga dcemu.
mmu_mapeo.bin: $(TARGET)
	$(KOS_OBJCOPY) -O binary $(TARGET) mmu_mapeo.bin

bin: mmu_mapeo.bin

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)
