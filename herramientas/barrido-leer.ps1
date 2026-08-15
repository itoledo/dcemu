# La lectura del barrido del medio texel, con el piso de ruido descontado.
#
# Tres conjuntos:
#   R  las que se mueven solas (barrido-mt-sin contra barrido-mt-sin2)
#   C  las que cambian con la correccion (barrido-mt-sin contra barrido-mt-con)
#   C \ R  lo que la correccion movio DE VERDAD
#
# Y aparte el veredicto de las de consola: las marcas del serial no pueden
# regresar aunque la imagen cambie.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"

function Hashes($dir)
{
	$h = @{}
	Import-Csv (Join-Path $dir "resumen.csv") | ForEach-Object { $h[$_.demo] = $_.sha256 }
	return $h
}

$sin  = Hashes "barrido-mt-sin"
$sin2 = Hashes "barrido-mt-sin2"
$con  = Hashes "barrido-mt-con"
$base = Hashes "barrido-reloj-on"

$ruido = @($sin.Keys | Where-Object { $sin[$_] -ne $sin2[$_] })
$camb  = @($sin.Keys | Where-Object { $sin[$_] -ne $con[$_] })
$vsbase = @($sin.Keys | Where-Object { $base.ContainsKey($_) -and $sin[$_] -ne $base[$_] })

$real = @($camb | Where-Object { $ruido -notcontains $_ } | Sort-Object)
$sucio = @($camb | Where-Object { $ruido -contains $_ } | Sort-Object)
$regres = @($vsbase | Where-Object { $ruido -notcontains $_ } | Sort-Object)

"demos: $($sin.Count)"
"se mueven solas (piso de ruido): $($ruido.Count)"
"distintas contra la linea base del 10-ago: $($vsbase.Count)"
"  de esas, NO explicadas por el ruido: $($regres.Count)"
if ($regres.Count) { $regres | ForEach-Object { "    $_" } }
""
"cambian con el medio texel: $($camb.Count)"
"  contaminadas por ruido (no se pueden leer): $($sucio.Count)"
"  movidas de verdad por la correccion: $($real.Count)"
$real | ForEach-Object { "    $_" }
""

# El veredicto del serial: lo que la demo dijo, que es independiente de si la
# pantalla quedo en el menu del BIOS.
"=== veredictos del serial (linea base -> medio texel)"
$patron = "SUCCEEDED|SUCCESS|PASSED|FAIL|panic|assertion"
$dif = 0
foreach ($k in ($sin.Keys | Sort-Object)) {
	$fa = "barrido-reloj-on\$k.serial.txt"
	$fb = "barrido-mt-con\$k.serial.txt"
	if (-not (Test-Path $fa) -or -not (Test-Path $fb)) { continue }

	$va = (Select-String -Path $fa -Pattern $patron | ForEach-Object { $_.Line.Trim() }) -join " | "
	$vb = (Select-String -Path $fb -Pattern $patron | ForEach-Object { $_.Line.Trim() }) -join " | "

	if ($va -ne $vb) {
		$dif++
		"  $k"
		"    antes:   $va"
		"    despues: $vb"
	}
}
if ($dif -eq 0) { "  sin cambios de veredicto en ninguna demo" }
