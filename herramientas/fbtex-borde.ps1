# El residuo documentado de pvr-fb_tex: "la copia pierde la columna 0 y la fila
# 0", que es el desplazamiento de -1/1024 en U del propio demo leyendo fuera de
# la textura en el borde izquierdo.
#
# Si el medio texel cambia donde cae u=0, ese residuo tiene que moverse. Se
# mide como se midio el de DCDoom: la tinta de las primeras columnas y filas,
# donde una progresion suave dice que la muestra cae adentro y un salto dice
# que la primera cae afuera.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"
Add-Type -AssemblyName System.Drawing

function Tinta($ruta)
{
	$bm = [System.Drawing.Bitmap]::FromFile((Resolve-Path $ruta))
	$r = $bm.LockBits([System.Drawing.Rectangle]::new(0,0,$bm.Width,$bm.Height),
			[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
			[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
	$tam = [Math]::Abs($r.Stride) * $bm.Height
	$p = [byte[]]::new($tam)
	[System.Runtime.InteropServices.Marshal]::Copy($r.Scan0, $p, 0, $tam)
	$bm.UnlockBits($r)

	$col = [long[]]::new(6)
	$fil = [long[]]::new(6)

	for ($y = 0; $y -lt $bm.Height; $y++) {
		$base = $y * $r.Stride
		for ($x = 0; $x -lt 6; $x++) {
			$i = $base + $x * 3
			$col[$x] += $p[$i] + $p[$i+1] + $p[$i+2]
		}
	}

	# Las filas del BMP van de abajo hacia arriba; la fila 0 de la pantalla es
	# la ULTIMA del archivo.
	for ($y = 0; $y -lt 6; $y++) {
		$base = ($bm.Height - 1 - $y) * $r.Stride
		for ($x = 0; $x -lt $bm.Width; $x++) {
			$i = $base + $x * 3
			$fil[$y] += $p[$i] + $p[$i+1] + $p[$i+2]
		}
	}

	$bm.Dispose()

	return @{ col = $col; fil = $fil }
}

foreach ($b in @("sin", "con")) {
	$t = Tinta "fbtex-nv-$b.bmp"
	"=== brazo ${b}"
	"  columnas 0..5: {0}" -f (($t.col | ForEach-Object { "{0,8}" -f $_ }) -join "")
	"  filas    0..5: {0}" -f (($t.fil | ForEach-Object { "{0,8}" -f $_ }) -join "")
}
