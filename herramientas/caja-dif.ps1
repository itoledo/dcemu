# La caja que encierra las diferencias entre dos capturas, y cuantos pixeles
# son. Contesta "donde cambio", que es lo que separa un arreglo de borde de un
# cambio en toda la imagen.
param(
	[Parameter(Mandatory=$true)][string] $A,
	[Parameter(Mandatory=$true)][string] $B
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Pix($ruta)
{
	$bm = [System.Drawing.Bitmap]::FromFile((Resolve-Path $ruta))
	$r = $bm.LockBits([System.Drawing.Rectangle]::new(0,0,$bm.Width,$bm.Height),
			[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
			[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
	$tam = [Math]::Abs($r.Stride) * $bm.Height
	$p = [byte[]]::new($tam)
	[System.Runtime.InteropServices.Marshal]::Copy($r.Scan0, $p, 0, $tam)
	$bm.UnlockBits($r)
	$o = @{ p = $p; w = $bm.Width; h = $bm.Height; s = $r.Stride }
	$bm.Dispose()
	return $o
}

$x = Pix $A
$y = Pix $B

$x0 = $x.w; $y0 = $x.h; $x1 = -1; $y1 = -1; $n = 0

for ($j = 0; $j -lt $x.h; $j++) {
	$base = $j * $x.s
	for ($i = 0; $i -lt $x.w; $i++) {
		$k = $base + $i * 3
		if ($x.p[$k] -ne $y.p[$k] -or $x.p[$k+1] -ne $y.p[$k+1] -or $x.p[$k+2] -ne $y.p[$k+2]) {
			$n++
			if ($i -lt $x0) { $x0 = $i }
			if ($i -gt $x1) { $x1 = $i }
			if ($j -lt $y0) { $y0 = $j }
			if ($j -gt $y1) { $y1 = $j }
		}
	}
}

if ($x1 -lt 0) { "sin diferencias"; exit }

# La fila 0 del BMP es la de ABAJO en pantalla.
"caja {0}x{1} en ({2},{3}) de {4}x{5}, {6} pixeles ({7:N2} %)" -f `
	($x1-$x0+1), ($y1-$y0+1), $x0, ($x.h - 1 - $y1), $x.w, $x.h, $n,
	(100.0 * $n / ($x.w * $x.h))
