## La ventana de muestreo de pvr-fb_tex.
#
# El demo NO es una prueba de nitidez ni un caso raro de textura con paso: es
# un medidor directo de **donde adentro del pixel muestrea el chip**, y su
# criterio es de dos texeles de ancho.
#
# Como funciona (fb_tex.c de KOS, de Paul Cercueil): el buffer delantero vive
# en la memoria de 32 bits y las texturas se leen por la de 64, que intercala
# los bancos cada 4 bytes -- o sea dos pixeles buenos y dos de basura. El demo
# declara la textura 1024x1024 con paso 640 y NEAREST, y dibuja la pantalla en
# dos mitades de 320 con u de 0 a 640/1024: **dos texeles por pixel de
# pantalla**. Una mascara de columnas alternadas (alfa 1 en las pares, 0 en las
# impares) y dos pasadas con corrimiento de U de 0 y de -1 texel arman la fila
# entera: la pasada A pide el texel 2p en el pixel par p, la B el 2p-1 en el
# impar.
#
# De ahi sale la prediccion, que es de dos lados y no de uno: escribiendo `s`
# para el punto de muestreo adentro del pixel (0,5 = el centro, o sea sin
# corrimiento), la coordenada de textura en el pixel p es 2(p+s) y el texel
# elegido es floor(2p+2s). Sale 2p **si y solo si 0 <= s < 0,5**. Con
# DCEMU_MEDIO_PIXEL_MIL=N es s = 0,5 - N/1000, asi que:
#
#   N = 0        s = 0,5     falla   (elige 2p+1)
#   N = 1..500   s en (0, 0,5]  pasa
#   N > 500      s < 0       falla   (elige 2p-1)
#
# El criterio automatico es el modo de falla que se ve: la pantalla sale como
# **dos copias de media pantalla**, o sea las dos mitades casi iguales. Se mide
# la diferencia media entre la mitad izquierda y la derecha; con la convencion
# bien la estela diagonal las hace muy distintas.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"
Add-Type -AssemblyName System.Drawing

function DifMitades($ruta)
{
	$bm = [System.Drawing.Bitmap]::FromFile((Resolve-Path $ruta))
	$r = $bm.LockBits([System.Drawing.Rectangle]::new(0,0,$bm.Width,$bm.Height),
			[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
			[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
	$tam = [Math]::Abs($r.Stride) * $bm.Height
	$p = [byte[]]::new($tam)
	[System.Runtime.InteropServices.Marshal]::Copy($r.Scan0, $p, 0, $tam)
	$bm.UnlockBits($r)

	$mitad = [int] ($bm.Width / 2)
	[long] $suma = 0
	[long] $n = 0

	for ($y = 0; $y -lt $bm.Height; $y++) {
		$base = $y * $r.Stride
		for ($x = 0; $x -lt $mitad; $x++) {
			$i = $base + $x * 3
			$j = $base + ($x + $mitad) * 3
			$suma += [Math]::Abs([int] $p[$i]   - [int] $p[$j])
			$suma += [Math]::Abs([int] $p[$i+1] - [int] $p[$j+1])
			$suma += [Math]::Abs([int] $p[$i+2] - [int] $p[$j+2])
			$n += 3
		}
	}

	$bm.Dispose()
	return [double] $suma / $n
}

foreach ($mil in @(0, 125, 250, 375, 484, 499, 500, 501, 600, 750, 999)) {
	$env:DCEMU_MEDIO_PIXEL_MIL = "$mil"

	& .\dcemu.exe pvr-fb_tex.bin --salir-tras=8 --sin-vmu --render=fbo --escala=1 `
		"--captura-gl=fbv-$mil.bmp" | Out-Null

	Remove-Item env:DCEMU_MEDIO_PIXEL_MIL -EA SilentlyContinue

	$d = DifMitades "fbv-$mil.bmp"
	$v = if ($d -gt 10.0) { "pasa " } else { "FALLA" }

	"N = {0,4}   s = {1,6:N3}   dif mitades {2,7:N2}   {3}" -f `
		$mil, (0.5 - $mil / 1000.0), $d, $v
}

"=== fin"
