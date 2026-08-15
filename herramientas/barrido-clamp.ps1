# El parque de KOS contra el clamp de borde.
#
# Basta UNA corrida: `barrido-mt-sin` ya es el brazo sin el clamp (se tomo con
# el medio texel apagado, que es la conducta de hoy salvo por esta regla), y
# `barrido-mt-sin2` es su repeticion, o sea el piso de ruido -- las 40 demos
# que se mueven solas. Lo que hay que mirar es lo que cambie FUERA de esas 40.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"

Remove-Item env:DCEMU_SIN_CLAMP_BORDE -EA SilentlyContinue

& "$PSScriptRoot\barrido.ps1" -Salida "barrido-clamp" -Demos "." `
	-Vmu "vmu-barrido.bin" -Exe ".\dcemu.exe"

function Hashes($dir)
{
	$h = @{}
	Import-Csv (Join-Path $dir "resumen.csv") | ForEach-Object { $h[$_.demo] = $_.sha256 }
	return $h
}

$sin   = Hashes "barrido-mt-sin"
$sin2  = Hashes "barrido-mt-sin2"
$clamp = Hashes "barrido-clamp"

$ruido = @($sin.Keys | Where-Object { $sin[$_] -ne $sin2[$_] })
$camb  = @($sin.Keys | Where-Object { $clamp.ContainsKey($_) -and $sin[$_] -ne $clamp[$_] })
$real  = @($camb | Where-Object { $ruido -notcontains $_ } | Sort-Object)

""
"demos: $($sin.Count); piso de ruido: $($ruido.Count)"
"cambian con el clamp: $($camb.Count), de las cuales fuera del ruido: $($real.Count)"
$real | ForEach-Object { "    $_" }

""
"=== veredictos del serial"
$patron = "SUCCEEDED|SUCCESS|PASSED|FAIL|panic|assertion"
$dif = 0
foreach ($k in ($sin.Keys | Sort-Object)) {
	$fa = "barrido-mt-sin\$k.serial.txt"
	$fb = "barrido-clamp\$k.serial.txt"
	if (-not (Test-Path $fa) -or -not (Test-Path $fb)) { continue }

	$va = (Select-String -Path $fa -Pattern $patron | ForEach-Object { $_.Line.Trim() }) -join " | "
	$vb = (Select-String -Path $fb -Pattern $patron | ForEach-Object { $_.Line.Trim() }) -join " | "

	if ($va -ne $vb) { $dif++; "  $k"; "    antes:   $va"; "    despues: $vb" }
}
if ($dif -eq 0) { "  sin cambios de veredicto en ninguna demo" }

"=== fin"
