# La tanda del traductor: interprete contra traductor dentro del binario del
# JIT con su perfil (herramientas\ciclo-jit.ps1 primero). DCDoom 35 s por tres
# rondas, Crazy Taxi 180 s por dos y Sega Rally 2 60 s por dos, alternando el
# orden dentro de cada ronda, con una corrida de calentamiento descartada -- el
# primer arranque de un binario recien enlazado midio 13 % de mas en este arbol.
#
# La linea "jit:" de stderr es el control de trabajo por corrida: si la
# cobertura cambia entre corridas del mismo modo, la tanda no compara. Los
# numeros de referencia y las dispersiones esperables (0,14-0,42 % entre
# rondas) estan en docs/recompilador-plan.md.
$ErrorActionPreference = "Stop"
$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($modo, $img, $segundos, $teclas)
{
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$env:DCEMU_FUSION = "0"
	$env:DCEMU_JIT = if ($modo -eq "traductor") { "2" } else { "0" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$cobertura = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-11} {1,8} ms   {2}" -f $modo, $reloj.ElapsedMilliseconds, $cobertura
}

$doom = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"
$ct   = "roms\Crazy Taxi (USA).cdi"
$sr2  = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"

Write-Output "=== calentamiento (descartado)"
Correr "traductor" $doom 35 $false | Out-Null

Write-Output "=== DCDoom, 35 s emulados"
foreach ($r in @(@("interprete","traductor"), @("traductor","interprete"), @("interprete","traductor"))) {
	foreach ($m in $r) { Correr $m $doom 35 $false }
	Write-Output "---"
}

Write-Output "=== Crazy Taxi, 180 s emulados, con teclas"
foreach ($r in @(@("interprete","traductor"), @("traductor","interprete"))) {
	foreach ($m in $r) { Correr $m $ct 180 $true }
	Write-Output "---"
}

Write-Output "=== Sega Rally 2, 60 s emulados"
foreach ($r in @(@("interprete","traductor"), @("traductor","interprete"))) {
	foreach ($m in $r) { Correr $m $sr2 60 $false }
	Write-Output "---"
}

Write-Output "=== fin"
