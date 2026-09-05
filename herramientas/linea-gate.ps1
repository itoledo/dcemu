# linea-gate.ps1 -- la compuerta de la entrega determinista de la linea del
# AICA al ASIC (docs/hilos-plan.md). Corre los brazos con linea-brazo.ps1 y los
# compara por manifiesto (captura, .wav, puntos de DCEMU_CP_MS y la LISTA DE
# ENTREGAS, que es el arbitro de un cambio de temporizacion de interrupciones).
#
# Los brazos, y que responde cada comparacion:
#
#   ref      binario anterior (-Ref), demora 0        \  "D=0 es el binario
#   d0       binario nuevo, demora 0                  /   viejo": todo igual
#   d1-sin   nuevo, demora 1, sin hilos               \  la razon del cambio:
#   d1-con   nuevo, demora 1, con hilos               /   todo igual, SR2 incluido
#   d1-conB  nuevo, demora 1, con hilos, otra vez     -   reproducible
#   d0-con   nuevo, demora 0, con hilos               -   el CONTROL: SR2 tiene
#                                                        que seguir divergiendo
#   d1-sin contra d0: lo que la latencia mueve (nueva linea base, no compuerta)
#
# -SoloComparar lee los manifiestos que ya esten en logs\linea.
param(
	[string] $Exe = "build-clang\dcemu.exe",
	[string] $Ref = "build-ref\dcemu.exe",
	[int]    $Demora = 1,
	[switch] $SoloComparar
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (-not $SoloComparar) {
	if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
	Remove-Item logs\linea -Recurse -Force -EA SilentlyContinue
	& "$PSScriptRoot\linea-brazo.ps1" -Nombre ref     -Exe $Ref -Demora 0
	& "$PSScriptRoot\linea-brazo.ps1" -Nombre d0      -Exe $Exe -Demora 0
	& "$PSScriptRoot\linea-brazo.ps1" -Nombre d1-sin  -Exe $Exe -Demora $Demora
	& "$PSScriptRoot\linea-brazo.ps1" -Nombre d1-con  -Exe $Exe -Demora $Demora -Hilos
	& "$PSScriptRoot\linea-brazo.ps1" -Nombre d1-conB -Exe $Exe -Demora $Demora -Hilos
	& "$PSScriptRoot\linea-brazo.ps1" -Nombre d0-con  -Exe $Exe -Demora 0 -Hilos
}

function Manifiesto($brazo) {
	$m = @{}
	$f = "logs\linea\$brazo\manifiesto.txt"
	if (-not (Test-Path $f)) { return $m }
	foreach ($l in Get-Content $f) {
		if ($l -match '^(\w+): bmp (\S+) wav (\S+) cp (\S+) entregas (\S+)\s+\[(.*)\]$') {
			$m[$Matches[1]] = @{ bmp=$Matches[2]; wav=$Matches[3]; cp=$Matches[4]; ent=$Matches[5]; ctl=$Matches[6] }
		}
	}
	return $m
}

$brazos = @{}
foreach ($b in @("ref","d0","d1-sin","d1-con","d1-conB","d0-con")) { $brazos[$b] = Manifiesto $b }

function Comparar($a, $b, $guest) {
	$x = $brazos[$a][$guest]; $y = $brazos[$b][$guest]
	if (-not $x -or -not $y) { return "(falta)" }
	$dif = @()
	foreach ($k in @("bmp","wav","cp","ent")) { if ($x[$k] -ne $y[$k]) { $dif += $k } }
	if ($dif.Count -eq 0) { return "IGUAL" } else { return "DISTINTO: " + ($dif -join ",") }
}

$fallas = 0
Write-Output ""
Write-Output ("{0,-6} {1,-22} {2,-22} {3,-22} {4,-22} {5}" -f "guest", "d0 vs ref", "d1-con vs d1-sin", "d1-conB vs d1-con", "d0-con vs d0 (ctrl)", "d1-sin vs d0 (base)")
foreach ($g in @("sr2","doom","ct")) {
	$c1 = Comparar "d0" "ref" $g
	$c2 = Comparar "d1-con" "d1-sin" $g
	$c3 = Comparar "d1-conB" "d1-con" $g
	$c4 = Comparar "d0-con" "d0" $g
	$c5 = Comparar "d1-sin" "d0" $g
	Write-Output ("{0,-6} {1,-22} {2,-22} {3,-22} {4,-22} {5}" -f $g, $c1, $c2, $c3, $c4, $c5)
	if ($c1 -ne "IGUAL") { $fallas++ }
	if ($c2 -ne "IGUAL") { $fallas++ }
	if ($c3 -ne "IGUAL") { $fallas++ }
	# el control: solo SR2 consume la linea en la ventana del banco; en el es
	# obligatorio que el camino viejo con hilos siga divergiendo.
	if ($g -eq "sr2" -and $c4 -eq "IGUAL") { $fallas++; Write-Output "  CONTROL ROTO: d0-con no diverge en SR2 -- la compuerta no ve lo que dice ver" }
}

Write-Output ""
Write-Output "controles (el binario nuevo, brazo d1-con):"
foreach ($g in @("sr2","doom","ct")) {
	if ($brazos["d1-con"][$g]) { Write-Output ("  {0}: {1}" -f $g, $brazos["d1-con"][$g].ctl) }
}

Write-Output ""
if ($fallas) { Write-Output "COMPUERTA ROJA: $fallas" } else { Write-Output "COMPUERTA VERDE" }
