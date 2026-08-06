# El color de cara y los vértices en modo intensidad

Un vértice del PVR trae su color de cuatro formas (bits 5-4 de la palabra de control):
empaquetado en ARGB de 32 bits, en cuatro flotantes, o en **modo intensidad**, donde el vértice
trae un solo número y el color sale del **color de cara** que puso el encabezado. El modo 3 ni
siquiera lo trae: usa el que dejó el último encabezado en modo 2.

Es lo que usan los juegos de verdad para geometría iluminada. Censado: **Virtua Tennis 2 manda
826 414 encabezados en modo intensidad contra 56 576 empaquetados**, o sea el 64 % de todo lo que
dibuja — y era el último sospechoso abierto de su sombra, precisamente porque **ninguna demo de KOS
lo ejercita** y no había con qué comprobarlo.

## La prueba, que no necesita imagen de referencia

Los dos sabores dibujan la misma figura con los mismos colores: uno los manda empaquetados y el
otro como color de cara más una intensidad por vértice, elegidos para dar exactamente lo mismo. Las
dos capturas tienen que salir **byte a byte iguales**.

```sh
dcemu --sin-vmu --sin-audio --render=shader --captura-gl=emp.bmp \
      --salir-tras=4 demos/intensidad/int-empaquetado.bin
dcemu --sin-vmu --sin-audio --render=shader --captura-gl=int.bmp \
      --salir-tras=4 demos/intensidad/int-intensidad.bin
```

Las dos tienen que dar `6DBF5EE5…`, con la fila opaca en `201030`, `402060`, `603090` y `8040C0`
—que son (0x80,0x40,0xC0) por 1/4, 2/4, 3/4 y 4/4— y la translúcida en exactamente la mitad de cada
una sobre negro.

Cada fila prueba una mitad de la regla. La de arriba, el **RGB**. La de abajo, de dónde sale el
**alfa**: en el chip sale del color de cara y es constante en el polígono, así que si saliera de la
intensidad los cuatro cuadros de abajo tendrían transparencias distintas y los sabores no
coincidirían.

Ojo con el formato del vértice, que es donde es fácil equivocarse: **la intensidad va en la palabra
4**, donde el vértice empaquetado tiene una reservada, y el color empaquetado va en la 6. Los dos
miden 32 bytes.

## Compilar

Desde el shell **mingw64** de MSYS2, con el entorno de KOS puesto:

```sh
cd demos/intensidad && ./build.sh
```

Los dos `.bin` están en el repo, así que no hace falta el toolchain para correr la prueba.
