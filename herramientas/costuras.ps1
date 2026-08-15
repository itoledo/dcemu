# Cuenta costuras en una captura: pixeles cuyo vecino de la derecha (o de
# abajo) salta mas que un umbral, sin que sea un borde real de la escena.
#
# Una costura de textura es una linea de UN pixel: salta hacia arriba y vuelve
# enseguida. Un borde de la escena salta y se queda. Por eso lo que se cuenta
# no es el gradiente sino el **pico aislado**: |a-b| > u y |b-c| > u con los
# saltos de signo contrario, que es lo que distingue una costura de un
# contorno.
#
# Es la misma medida con la que se conto la costura del logo de Crazy Taxi.
param(
	[Parameter(Mandatory=$true)][string] $Imagen,
	[int] $Umbral = 24
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$bm = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Imagen))
$r = $bm.LockBits([System.Drawing.Rectangle]::new(0,0,$bm.Width,$bm.Height),
		[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
		[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$tam = [Math]::Abs($r.Stride) * $bm.Height
$p = [byte[]]::new($tam)
[System.Runtime.InteropServices.Marshal]::Copy($r.Scan0, $p, 0, $tam)
$bm.UnlockBits($r)

$hor = 0; $ver = 0

for ($y = 0; $y -lt $bm.Height; $y++) {
	$base = $y * $r.Stride
	for ($x = 1; $x -lt $bm.Width - 1; $x++) {
		$i = $base + $x * 3
		$a = $p[$i-3] + $p[$i-2] + $p[$i-1]
		$b = $p[$i]   + $p[$i+1] + $p[$i+2]
		$c = $p[$i+3] + $p[$i+4] + $p[$i+5]
		$d1 = $b - $a; $d2 = $c - $b
		if ([Math]::Abs($d1) -gt $Umbral -and [Math]::Abs($d2) -gt $Umbral `
			-and (($d1 -gt 0) -ne ($d2 -gt 0))) { $hor++ }
	}
}

for ($y = 1; $y -lt $bm.Height - 1; $y++) {
	for ($x = 0; $x -lt $bm.Width; $x++) {
		$i0 = ($y-1) * $r.Stride + $x * 3
		$i1 = $y     * $r.Stride + $x * 3
		$i2 = ($y+1) * $r.Stride + $x * 3
		$a = $p[$i0] + $p[$i0+1] + $p[$i0+2]
		$b = $p[$i1] + $p[$i1+1] + $p[$i1+2]
		$c = $p[$i2] + $p[$i2+1] + $p[$i2+2]
		$d1 = $b - $a; $d2 = $c - $b
		if ([Math]::Abs($d1) -gt $Umbral -and [Math]::Abs($d2) -gt $Umbral `
			-and (($d1 -gt 0) -ne ($d2 -gt 0))) { $ver++ }
	}
}

$bm.Dispose()

"{0,-40} picos verticales(col) {1,7}  horizontales(fila) {2,7}" -f `
	(Split-Path $Imagen -Leaf), $hor, $ver
