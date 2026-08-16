# Cuenta colores distintos en una captura.
#
# Sirve como oraculo de nitidez para contenido 2D a 1:1: una pantalla de
# interfaz hecha de texeles tiene un juego chico y discreto de colores, y
# cualquier mezcla del filtro inventa colores intermedios. Mas colores = se
# esta muestreando entre texeles.
#
# Ojo con la trampa documentada en docs/demos-kos.md: en PowerShell `-shl`
# sobre un [byte] no promueve, y el metodo viejo de contar colores contaba mal.
# Aqui se arma la clave con [int] explicito.
param(
	[Parameter(Mandatory=$true)][string] $Imagen
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

$set = [System.Collections.Generic.HashSet[int]]::new()

for ($y = 0; $y -lt $bm.Height; $y++) {
	$base = $y * $r.Stride
	for ($x = 0; $x -lt $bm.Width; $x++) {
		$i = $base + $x * 3
		$c = ([int] $p[$i]) -bor (([int] $p[$i+1]) -shl 8) -bor (([int] $p[$i+2]) -shl 16)
		[void] $set.Add($c)
	}
}

$w = $bm.Width; $h = $bm.Height
$bm.Dispose()

"{0,-28} {1,7} colores distintos en {2}x{3}" -f (Split-Path $Imagen -Leaf), $set.Count, $w, $h
