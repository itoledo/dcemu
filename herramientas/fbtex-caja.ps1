# La comprobacion propia de pvr-fb_tex, corrida en los dos brazos.
#
# El demo lee su propio buffer delantero como textura con paso, a dos texeles
# por pixel de pantalla: es lo unico del parque que puede medir la convencion
# de muestreo. Su criterio no necesita imagen de referencia -- con la
# convencion bien, **dos cuadros consecutivos difieren solo dentro de la caja
# del cubo nuevo**, de 64x64 texeles del lado emulado.
#
# Esto existe porque el arbol afirma que `pvr-fb_tex` sale byte a byte igual
# con y sin el medio texel, y el barrido dice que no: es determinista (dos
# corridas dan el mismo hash) y cambia entre los dos brazos.
#
# **AVISO: esta sonda no reproduce el criterio de la caja de 64x64.** Medida
# sobre los cuadros 3..6 da cajas de 483x85 en LOS DOS brazos, asi que no
# separa nada. La explicacion es que en esos cuadros la estela todavia crece y
# el cuadro entero se corre; el criterio del arbol describe un estado ya
# asentado. La pregunta la contesto otra medida --los dos brazos a 1:1
# (`--render=fbo --escala=1`) salen byte a byte identicos, y la diferencia del
# barrido es del camino de ventana, donde el corrimiento es 0,4 px--, y este
# archivo queda para no volver a derivar el callejon.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"
Add-Type -AssemblyName System.Drawing

function CajaDeDiferencias($a, $b)
{
	$ba = [System.Drawing.Bitmap]::FromFile((Resolve-Path $a))
	$bb = [System.Drawing.Bitmap]::FromFile((Resolve-Path $b))

	$x0 = $ba.Width; $y0 = $ba.Height; $x1 = -1; $y1 = -1; $n = 0

	$ra = $ba.LockBits([System.Drawing.Rectangle]::new(0,0,$ba.Width,$ba.Height),
			[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
			[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
	$rb = $bb.LockBits([System.Drawing.Rectangle]::new(0,0,$bb.Width,$bb.Height),
			[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
			[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)

	$tam = [Math]::Abs($ra.Stride) * $ba.Height
	$pa = [byte[]]::new($tam); $pb = [byte[]]::new($tam)
	[System.Runtime.InteropServices.Marshal]::Copy($ra.Scan0, $pa, 0, $tam)
	[System.Runtime.InteropServices.Marshal]::Copy($rb.Scan0, $pb, 0, $tam)

	$ba.UnlockBits($ra); $bb.UnlockBits($rb)

	for ($y = 0; $y -lt $ba.Height; $y++) {
		$fila = $y * $ra.Stride
		for ($x = 0; $x -lt $ba.Width; $x++) {
			$i = $fila + $x * 3
			if ($pa[$i] -ne $pb[$i] -or $pa[$i+1] -ne $pb[$i+1] -or $pa[$i+2] -ne $pb[$i+2]) {
				$n++
				if ($x -lt $x0) { $x0 = $x }
				if ($x -gt $x1) { $x1 = $x }
				if ($y -lt $y0) { $y0 = $y }
				if ($y -gt $y1) { $y1 = $y }
			}
		}
	}

	$ba.Dispose(); $bb.Dispose()

	if ($x1 -lt 0) { return "sin diferencias" }

	return "caja {0}x{1} en ({2},{3}), {4} pixeles" -f ($x1-$x0+1), ($y1-$y0+1), $x0, $y0, $n
}

foreach ($brazo in @("sin", "con")) {
	if ($brazo -eq "con") { $env:DCEMU_MEDIO_TEXEL = "1" }
	else { Remove-Item env:DCEMU_MEDIO_TEXEL -EA SilentlyContinue }

	# El numero va DELANTE del nombre del archivo: "f0007-fbtex-c.bmp".
	Remove-Item "f*-fbtex-c.bmp" -Force -EA SilentlyContinue
	Remove-Item vmu-fbtex.bin -Force -EA SilentlyContinue

	$env:DCEMU_CAPTURA_TODAS = "1"
	& .\dcemu.exe pvr-fb_tex.bin --salir-tras=3 --captura-gl="fbtex-c.bmp" `
		--vmu=vmu-fbtex.bin | Out-Null
	Remove-Item env:DCEMU_CAPTURA_TODAS -EA SilentlyContinue

	$cuadros = Get-ChildItem "f*-fbtex-c.bmp" | Sort-Object Name
	"=== brazo ${brazo}: $($cuadros.Count) cuadros"

	if ($cuadros.Count -ge 6) {
		foreach ($i in @(3, 4, 5)) {
			"  {0} -> {1}: {2}" -f $cuadros[$i].Name, $cuadros[$i+1].Name,
				(CajaDeDiferencias $cuadros[$i].FullName $cuadros[$i+1].FullName)
		}
	}

	Remove-Item "f*-fbtex-c.bmp" -Force -EA SilentlyContinue
}

Remove-Item env:DCEMU_MEDIO_TEXEL -EA SilentlyContinue
"=== fin"
