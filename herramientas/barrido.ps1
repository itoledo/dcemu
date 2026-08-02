# barrido.ps1 -- corre las demos de KOS y guarda lo que cada una dibujo.
#
# Es la regresion de todo lo que esta por encima del nucleo SH-4. Ver
# docs/demos-kos.md: el metodo es --captura-gl (no capturar la ventana, que
# depende del compositor del anfitrion y falla en silencio) y --salir-tras, que
# corta por tiempo **emulado** y por lo tanto deja dos corridas comparables.
#
# La comparacion no se hace aqui: cada corrida deja un directorio con un BMP por
# demo y un resumen.txt con el hash de cada uno. Dos corridas se comparan con
# comparar.ps1, byte a byte.
#
#   .\herramientas\barrido.ps1 -Salida barrido-antes
#   ... el cambio ...
#   .\herramientas\barrido.ps1 -Salida barrido-despues
#   .\herramientas\comparar.ps1 barrido-antes barrido-despues
#
# Se corre desde el directorio del ejecutable (build-x64/Debug), porque dcemu
# abre bios/, font.png y logs/ por ruta relativa al directorio de trabajo.

param(
	[Parameter(Mandatory=$true)][string] $Salida,
	[string] $Demos    = "kosdemos",
	[int]    $Segundos = 8,
	[string] $Exe      = ".\dcemu.exe",
	[string[]] $Extra  = @()
)

if (-not (Test-Path $Exe))   { throw "no encuentro $Exe; se corre desde el directorio del ejecutable" }
if (-not (Test-Path $Demos)) { throw "no encuentro el directorio de demos $Demos" }

New-Item -ItemType Directory -Force -Path $Salida | Out-Null

$bins = Get-ChildItem -Path $Demos -Filter *.bin | Sort-Object Name
$n = 0
$filas = @()

foreach ($b in $bins) {
	$n++
	$nombre = [System.IO.Path]::GetFileNameWithoutExtension($b.Name)
	$bmp    = Join-Path $Salida "$nombre.bmp"

	# Cada demo en su propio proceso: una que se cuelgue no se lleva el barrido.
	# El timeout es de tiempo real y esta muy por encima de los $Segundos
	# emulados; --salir-tras es el corte que importa.
	$args = @($b.FullName, "--salir-tras=$Segundos", "--captura-gl=$bmp") + $Extra

	$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru -NoNewWindow `
			-RedirectStandardOutput "$Salida\$nombre.out"

	if (-not $p.WaitForExit(60000)) {
		$p.Kill()
		$rv = "timeout"
	} else {
		$rv = $p.ExitCode
	}

	if (Test-Path $bmp) {
		$h = (Get-FileHash $bmp -Algorithm SHA256).Hash
		$t = (Get-Item $bmp).Length
	} else {
		$h = "-"
		$t = 0
	}

	$filas += [pscustomobject]@{ demo = $nombre; rv = $rv; bytes = $t; sha256 = $h }

	Write-Host ("[{0}/{1}] {2} rv={3} gl={4}" -f $n, $bins.Count, $nombre, $rv,
		$(if ($h -eq "-") { "no" } else { $h.Substring(0,8) }))
}

$filas | Export-Csv -Path (Join-Path $Salida "resumen.csv") -NoTypeInformation -Encoding UTF8

Write-Host ""
Write-Host ("$($bins.Count) demos, $(($filas | Where-Object { $_.sha256 -ne '-' }).Count) con captura -> $Salida")
