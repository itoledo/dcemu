# El reparto del tiempo bajo el JIT (la fase 0 del plan de estado del arte).
#
# Corre los tres guests del banco con DCEMU_JIT=2 dos veces cada uno: una con
# --perf (el desglose interno: bloque periodico, ARM7, mezclador, escena,
# presentar, TA -- lo que no esta ahi es el resto: codigo emitido + despachador
# + ayudantes + interprete residual) y una con DCEMU_JIT_SONDA_CRUCES=1 (los
# cruces de enlace y el censo de presencia de registros, que cambian la emision
# y por eso van en corrida aparte). El resumen "jit:" cae en las dos.
#
# Los tiempos de estas corridas NO son la tanda: --perf mete sus relojes y la
# sonda mete su contador emitido. Para absolutos esta ab-jit.ps1. Esto contesta
# REPARTO y CUENTAS, que es lo que decide el orden de las fases (2, 4, 5).
#
# stderr cae junto al ejecutable (SDL 1.2); cada corrida se copia a
# logs/perfil-jit-<guest>-<modo>.txt antes de la siguiente, porque dos corridas
# se truncan una a la otra.
param(
	[string] $Exe = "build-jit\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Exe)) { throw "falta $Exe (ciclo-jit.ps1 primero)" }

$err = Join-Path (Split-Path $Exe) "stderr.txt"

Write-Output "hash jit: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi";  s = 35;  teclas = $false },
	@{ n = "ct";     img = "roms\Crazy Taxi (USA).cdi";               s = 180; teclas = $true  },
	@{ n = "sr2";    img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

function Correr($b, $modo)
{
	if ($b.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$env:DCEMU_JIT = "2"; $env:DCEMU_FUSION = "0"

	# $args es variable automatica de PowerShell: no se le asigna.
	$argumentos = @("--salir-tras=$($b.s)", "--sin-vmu")
	if ($modo -eq "perf") { $argumentos += "--perf" } else { $env:DCEMU_JIT_SONDA_CRUCES = "1" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe @argumentos $b.img | Out-Null
	$reloj.Stop()

	Remove-Item env:DCEMU_JIT_SONDA_CRUCES -EA SilentlyContinue

	$destino = "logs\perfil-jit-$($b.n)-$modo.txt"
	Copy-Item $err $destino -Force

	"{0,-8} {1,-6} {2,8} ms   -> {3}" -f $b.n, $modo, $reloj.ElapsedMilliseconds, $destino
}

foreach ($b in $bancos) {
	Correr $b "perf"
	Correr $b "cruces"
}

Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A,env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Write-Output "hecho: logs\perfil-jit-*.txt"
