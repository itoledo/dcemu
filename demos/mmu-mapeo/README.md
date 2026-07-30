# mmu-mapeo — mapeo de una página y reejecución

Programa de KallistiOS escrito para dcemu. Cubre la mitad de la MMU que `basic/mmu/nullptr`
del árbol de KOS **no** prueba.

`nullptr` verifica que un acceso a una página sin mapear levanta el fallo de TLB con el PC
correcto, pero su callback devuelve `NULL` a propósito: nunca llega a mapear ni a
reejecutar, y termina en el `kernel panic` que la demo espera. Esto hace lo otro:

1. mapea una página virtual (`0x10000000`) a una física de la RAM del sistema,
2. escribe por la dirección virtual, lo que falla en la TLB,
3. el manejador de KOS carga la entrada y hace `RTE`,
4. la instrucción **se reejecuta** y la escritura llega,
5. lee por la dirección **física** (P1, que nunca pasa por la TLB) y compara.

El paso 5 es el que importa: si la traducción resolviera a otra página, o si la reejecución
no rehiciera la escritura, el valor no estaría ahí. Después cierra el círculo al revés —
escribe por la física y lee por la virtual— para descartar que sean dos copias.

Esto es el hito que `docs/mmu-plan.md` dejaba anotado como *«la reejecución solo está probada
por construcción, no por observación»*.

## Compilar

Necesita el toolchain de KallistiOS. En este equipo está en `C:\dcsdk` y **solo compila desde
`mingw64.exe`**, no desde `msys2.exe`:

```sh
source /opt/toolchains/dc/kos/environ.sh
cd demos/mmu-mapeo
make bin          # deja mmu_mapeo.elf y mmu_mapeo.bin
```

## Correr

```sh
cp demos/mmu-mapeo/mmu_mapeo.bin build/Release/mmu-mapeo.bin
cd build/Release && ./dcemu.exe mmu-mapeo.bin
```

El veredicto sale por el serial, en `logs/serial.txt`:

```
Prueba de mapeo y reejecucion de la MMU
	virtual  10000000
	fisica   0c044000 (destino en 0x8c044000)
Escribiendo por la direccion virtual...
	virt[0] = deadbeef, fisico[0] = deadbeef
	virt[1] = 12345678, fisico[1] = 12345678
	la escritura por la fisica se ve por la virtual

TEST SUCCEEDED!
```

La dirección física depende de dónde caiga `destino` en el enlace, así que ese `0c044000`
cambia entre compilaciones. Lo que no cambia es que las dos columnas coincidan.
