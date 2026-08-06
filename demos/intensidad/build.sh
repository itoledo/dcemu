#!/bin/sh
set -e
. /opt/toolchains/dc/kos/environ.sh
make clean >/dev/null 2>&1 || true
make
make INTENSIDAD=1
for f in int-empaquetado int-intensidad; do
  sh-elf-objcopy -O binary "$f.elf" "$f.bin"
  ls -l "$f.bin"
done
