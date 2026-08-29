# barrido-vmu-demora.ps1 -- el barrido KOS pendiente del expediente de la caja
# automatica de Sega Rally 2 (docs/notas-arranque.md): DCEMU_MAPLE_DEMORA_VMU_US
# cambia la temporizacion de todo guest que toque la VMU, y el A/B de la nota
# lo midio solo con cinco juegos comerciales. Esto es el parque de 131 demos.
#
# Tres brazos, un solo binario (la regla del arbol: un lever, un binario):
#   viejo-a, viejo-b: DCEMU_MAPLE_DEMORA_VMU_US=0, la conducta anterior al
#                      commit -- corrido DOS veces para medir el piso de ruido
#                      de este barrido especifico (la regla del 40-de-139).
#   nuevo-a:          sin la variable, o sea la omision nueva (13000 us).
#
# RTC clavado y VMU fresca por demo en los tres brazos (via barrido.ps1 -Vmu),
# asi que la unica variable entre brazos es la palanca.
param(
	[string] $Exe   = "build-clang\dcemu.exe",
	[string] $Demos = "C:\dcsdk\tmp\bins",
	[switch] $SoloLeer
)

$ErrorActionPreference = "Stop"

$brazos = @(
	@{ n = "viejo-a"; dir = "logs\barrido-vp-viejo-a"; us = "0" },
	@{ n = "viejo-b"; dir = "logs\barrido-vp-viejo-b"; us = "0" },
	@{ n = "nuevo-a"; dir = "logs\barrido-vp-nuevo-a"; us = $null }
)

if (-not $SoloLeer) {
	if (Get-Process dcemu -EA SilentlyContinue) { throw "hay un dcemu corriendo" }
	Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

	$env:DCEMU_RTC_FIJO = "1000000000"

	foreach ($b in $brazos) {
		if ($null -ne $b.us) { $env:DCEMU_MAPLE_DEMORA_VMU_US = $b.us }
		else { Remove-Item Env:\DCEMU_MAPLE_DEMORA_VMU_US -EA SilentlyContinue }

		Write-Output "=== brazo $($b.n) (DCEMU_MAPLE_DEMORA_VMU_US=$($b.us))"
		& "$PSScriptRoot\barrido.ps1" -Salida $b.dir -Demos $Demos -Exe $Exe `
			-Vmu "logs\barrido-vp-vmu.bin"
	}

	Remove-Item Env:\DCEMU_MAPLE_DEMORA_VMU_US, Env:\DCEMU_RTC_FIJO -EA SilentlyContinue
}

function Hashes($dir)
{
	$h = @{}
	Import-Csv (Join-Path $dir "resumen.csv") | ForEach-Object { $h[$_.demo] = $_.sha256 }
	return $h
}

function Serial($dir, $demo)
{
	$f = Join-Path $dir "$demo.serial.txt"
	if (-not (Test-Path $f)) { return "(sin serial)" }
	(Select-String $f -Pattern "SUCCEEDED|FAIL|panic" | ForEach-Object { $_.Line.Trim() }) -join " / "
}

$va = Hashes $brazos[0].dir
$vb = Hashes $brazos[1].dir
$na = Hashes $brazos[2].dir

$piso = @($va.Keys | Where-Object { $va[$_] -ne $vb[$_] })
Write-Output ""
Write-Output "piso de ruido (viejo-a vs viejo-b, mismo brazo dos veces): $($piso.Count) de $($va.Count)"
foreach ($k in $piso) { Write-Output "  $k" }

$senal = @($va.Keys | Where-Object { $va[$_] -ne $na[$_] -and $piso -notcontains $_ })
Write-Output ""
Write-Output "senal (viejo-a vs nuevo-a, fuera del piso): $($senal.Count) de $($va.Count)"
foreach ($k in $senal) {
	Write-Output ("  {0}  viejo={1}  nuevo={2}  serial viejo=[{3}]  serial nuevo=[{4}]" -f `
		$k, $va[$k].Substring(0,8), $na[$k].Substring(0,8), `
		(Serial $brazos[0].dir $k), (Serial $brazos[2].dir $k))
}
