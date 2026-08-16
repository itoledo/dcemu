# El juicio del medio pixel, a 1:1 y con las dos evidencias enfrentadas.
#
# La afirmacion del arbol es que el PVR muestrea el pixel en su coordenada
# ENTERA y GL en el centro, y por eso screeninit() corre el glOrtho medio pixel
# (medio_pixel()). Su consecuencia, que nadie habia medido, es que a 1:1 el
# punto de muestreo cae sobre el BORDE del texel en vez de su centro: todo blit
# 1:1 sale mezclado a medias con su vecino.
#
# Aqui se enfrentan las dos evidencias:
#
#   a favor de apagarlo   la nitidez del contenido 2D a 1:1 (colores distintos:
#                         un juego de texeles es discreto, y la mezcla inventa
#                         intermedios)
#   a favor de dejarlo    pvr-fb_tex, que segun el arbol solo reconstruye su
#                         pantalla con el corrimiento, y la columna 0 de DCDoom
#
# Todo a --render=fbo --escala=1, que es donde la pregunta tiene sentido: el
# camino de ventana estira y ahi no hay muestreo 1:1 que juzgar.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"
Add-Type -AssemblyName System.Drawing

$casos = @(
	@{ n = "ctlogo"; a = @("..\..\roms\Crazy Taxi (USA).cdi", "--salir-tras=10") },
	@{ n = "sf3";    a = @("..\..\roms\Street Fighter III - 3rd Strike v1.001 (2000)(Capcom)(US)[!]\Street Fighter III - 3rd Strike v1.001 (2000)(Capcom)(US)[!].gdi", "--salir-tras=30") },
	@{ n = "dcdoom"; a = @("..\..\roms\DCDoom GDI and CDI\DCDoom CDI.cdi", "--salir-tras=20") },
	@{ n = "fbtex";  a = @("pvr-fb_tex.bin", "--salir-tras=8") },
	@{ n = "png";    a = @("png.bin", "--salir-tras=8") }
)

function Tinta($ruta, $n)
{
	$bm = [System.Drawing.Bitmap]::FromFile((Resolve-Path $ruta))
	$r = $bm.LockBits([System.Drawing.Rectangle]::new(0,0,$bm.Width,$bm.Height),
			[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
			[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
	$tam = [Math]::Abs($r.Stride) * $bm.Height
	$p = [byte[]]::new($tam)
	[System.Runtime.InteropServices.Marshal]::Copy($r.Scan0, $p, 0, $tam)
	$bm.UnlockBits($r)

	$col = [long[]]::new($n)
	for ($y = 0; $y -lt $bm.Height; $y++) {
		$base = $y * $r.Stride
		for ($x = 0; $x -lt $n; $x++) {
			$i = $base + $x * 3
			$col[$x] += $p[$i] + $p[$i+1] + $p[$i+2]
		}
	}
	$bm.Dispose()
	return $col
}

foreach ($c in $casos) {
	foreach ($mp in @("con", "sin")) {
		if ($mp -eq "sin") { $env:DCEMU_SIN_MEDIO_PIXEL = "1" }
		else { Remove-Item env:DCEMU_SIN_MEDIO_PIXEL -EA SilentlyContinue }

		$bmp = "mp-$($c.n)-$mp.bmp"
		$args = $c.a + @("--sin-vmu", "--render=fbo", "--escala=1", "--captura-gl=$bmp")
		& .\dcemu.exe $args | Out-Null
	}

	Remove-Item env:DCEMU_SIN_MEDIO_PIXEL -EA SilentlyContinue

	$r1 = & "$PSScriptRoot\colores.ps1" -Imagen "mp-$($c.n)-con.bmp"
	$r2 = & "$PSScriptRoot\colores.ps1" -Imagen "mp-$($c.n)-sin.bmp"
	"$r1"
	"$r2"

	$t1 = Tinta "mp-$($c.n)-con.bmp" 4
	$t2 = Tinta "mp-$($c.n)-sin.bmp" 4
	"     columnas 0..3 con medio pixel: {0}" -f (($t1 | ForEach-Object { "{0,8}" -f $_ }) -join "")
	"     columnas 0..3 sin medio pixel: {0}" -f (($t2 | ForEach-Object { "{0,8}" -f $_ }) -join "")
}

"=== fin"
