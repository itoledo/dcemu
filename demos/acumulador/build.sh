#!/bin/sh
set -e
. /opt/toolchains/dc/kos/environ.sh
make clean >/dev/null 2>&1 || true
make
make ACUMULAR=1
for f in acum-directo acum-acumulado; do
  sh-elf-objcopy -O binary "$f.elf" "$f.bin"
  ls -l "$f.bin"
done
