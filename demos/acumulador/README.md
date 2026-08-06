# El buffer de acumulación secundario del TSP

El chip lleva **dos buffers de acumulación por píxel**; los bits 24 y 25 de la palabra TSP eligen
cuál usa cada tira como destino y como origen de la mezcla. Existe —DevBox 3.4.6.1— para tratar el
resultado de superponer varios polígonos como si fuera uno solo.

dcemu los registraba y no los consultaba. Se podía porque **nada del árbol los selecciona**: sobre
1,16 millones de tiras de Crazy Taxi en juego, las doce demos de control y los nueve juegos, no hay
una sola tira con esos bits puestos — ni siquiera Virtua Tennis 2, lo que lo saca de la lista de
sospechosos de su sombra.

Esta demo existe para poder verificarlo. Ver la cabecera de `acumulador.c`.

## La prueba, que no necesita imagen de referencia

Los dos sabores dibujan los mismos dos cuadrados superpuestos con mezcla **aditiva sobre negro**: uno
los suma al primario y el otro los acumula en el secundario y después lo compone entero. Sumar sobre
negro es asociativo, así que **las dos capturas tienen que salir byte a byte iguales**.

```sh
dcemu --sin-vmu --sin-audio --render=shader --captura-gl=dir.bmp \
      --salir-tras=4 demos/acumulador/acum-directo.bin
dcemu --sin-vmu --sin-audio --render=shader --captura-gl=sec.bmp \
      --salir-tras=4 demos/acumulador/acum-acumulado.bin
```

Las dos tienen que dar el mismo SHA-256 (`334919C1…`), con cuatro colores: el fondo negro, los dos
cuadrados en `4060A0` y `A06040`, y el solapamiento en `E0C0E0`, que es la suma exacta.

Lo que hace que la prueba sirva es que **las tres formas de equivocarse dan imágenes distintas**: si
se ignora el bit 24 los cuadrados van al primario y además se compone encima; si se ignora el 25, el
cuadrilátero de composición aporta su propio color —verde oscuro a propósito— y tiñe la pantalla; si
el secundario no se limpia, sale doble. La segunda no es hipotética: `--render=fbo` da exactamente
eso, fondo `008000` y los cuadrados teñidos.

**Con `--render=oit` los bits no se aplican** —el fragmento se apila en vez de dibujarse— y la demo
igual sale bien, por casualidad. dcemu avisa una vez cuando se da esa combinación.

## Compilar

Desde el shell **mingw64** de MSYS2, con el entorno de KOS puesto:

```sh
cd demos/acumulador && ./build.sh
```

Los dos `.bin` están en el repo, así que no hace falta el toolchain para correr la prueba.
