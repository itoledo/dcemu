# roto — rotozoomer de 256 bytes

Intro de 256 bytes para Dreamcast, de *quarn / Outbreak*, 20 de julio de 2002. Ver
`otb-roto.nfo`. El fuente en ensamblador SH-4 está en `roto.s`; `roto.bin` es el binario ya
linkeado en `0x8C010000`.

Es el homebrew con el que se valida el port a MSVC: ejercita la FPU (`FSCA`, `FDIV`, `FTRC`,
`FLOAT`), `MUL.L`, y escribe directo al framebuffer de la RAM de video.

## Cómo correrlo

`main.c` carga siempre `ip.bin` en `0x8C008000` y empieza a ejecutar en `0x8C00B800`
(`mem_base + ip_bs1_offset`), que en un IP.BIN real es el bootstrap. Este demo no trae uno, así
que `ip-stub.bin` es un IP.BIN mínimo: 32 KB de ceros con cuatro instrucciones en el offset
`0x3800` que saltan directo a `0x8C010000`.

```
mov.l  @(0x3808), r0     ; 01 d0
jmp    @r0               ; 2b 40
nop                      ; 09 00   (delay slot)
nop                      ; 09 00
.long  0x8C010000        ; 00 00 01 8c
```

Copiar junto al ejecutable, renombrando el stub:

```sh
cp demos/roto/roto.bin      build/Debug/
cp demos/roto/ip-stub.bin   build/Debug/ip.bin
cd build/Debug && ./dcemu.exe roto.bin
```

Hace falta además `bios/bios.bin` (el boot ROM de 2 MB, **no** el flash de 128 KB) y `font.png`,
que CMake ya copia.
