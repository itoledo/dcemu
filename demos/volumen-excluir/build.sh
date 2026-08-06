#!/bin/sh
set -e
. /opt/toolchains/dc/kos/environ.sh
make clean >/dev/null 2>&1 || true
make
make EXCLUIR=1
for f in volumen-incluir volumen-excluir; do
  sh-elf-objcopy -O binary "$f.elf" "$f.bin"
  ls -l "$f.bin"
done
