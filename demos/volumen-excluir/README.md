# La instrucción de cierre de un volumen modificador

Un volumen modificador se cierra con «incluyendo» —afecta a los píxeles de **dentro**— o con
«excluyendo», que afecta a los de **fuera** (bits 30-29 de la palabra ISP/TSP). dcemu trataba la
segunda como una aproximación, y se podía porque **nada del árbol la usa**: censadas 1,16 millones
de tiras de Crazy Taxi en juego, las tres demos de volúmenes de KOS y los otros ocho juegos, la
instrucción 2 no aparece ni una vez.

Esta demo existe para poder verificarla. Ver la cabecera de `volumen.c`.

## La prueba, que no necesita imagen de referencia

Incluir y excluir son **complementos exactos**, así que las dos capturas tienen que ser una el
negativo de la otra, píxel a píxel. Eso lo decide una regla del chip y no la opinión de quien mira.

```sh
dcemu --sin-vmu --sin-audio --render=shader --captura-gl=inc.bmp \
      --salir-tras=4 demos/volumen-excluir/volumen-incluir.bin
dcemu --sin-vmu --sin-audio --render=shader --captura-gl=exc.bmp \
      --salir-tras=4 demos/volumen-excluir/volumen-excluir.bin
```

`inc.bmp` tiene que salir con un cuadrado rojo de 320×240 en x 160..479, y 120..359 sobre fondo azul,
y `exc.bmp` exactamente al revés: **307 200 de 307 200 píxeles complementarios, cero fallas**.

Con `--render=fbo` —el camino de plantilla, que no implementa la instrucción— el sabor «excluir» da
38 640 píxeles rojos en vez de 230 400. Esa es la aproximación, medida.

## Compilar

Desde el shell **mingw64** de MSYS2, con el entorno de KOS puesto:

```sh
cd demos/volumen-excluir && ./build.sh
```

Los dos `.bin` están en el repo, así que no hace falta el toolchain para correr la prueba.
