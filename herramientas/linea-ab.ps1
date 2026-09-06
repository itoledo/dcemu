# linea-ab.ps1 -- las tandas A/B de la entrega determinista de la linea del
# AICA (docs/hilos-plan.md). Un binario, palancas de ambiente, rondas con el
# orden rotado dentro del par, calentamiento por guest descartado, RTC clavado,
# sin captura. Dos preguntas, dos modos:
#
#   -Modo latencia : sin hilos, DCEMU_AICA_DEMORA_LINEA=D contra =0. El costo
#                    de la latencia sola en el camino por omision. Tiene que
#                    ser neutro.
#   -Modo hilos    : --hilos contra sin hilos, con la demora D en los dos. La
#                    ganancia del hilo con la entrega ya determinista, y la
#                    sonda de esperas al lado, que es la que decide D.
#
# -Demoras acepta varias (p. ej. 1,2,4) y corre una tanda por cada una.
param(
	[ValidateSet("latencia","hilos")] [string] $Modo = "hilos",
	[string] $Exe = "build-clang\dcemu.exe",
	[int[]]  $Demoras = @(1),
	[string] $Guest = "ct",
	[int]    $Segundos = 0,
	[int]    $Rondas = 4
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
. "$PSScriptRoot\banco.ps1"

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))  modo $Modo  guest $Guest"

$bancos = @{
	ct   = @{ s = 180; teclas = $true  }
	doom = @{ s = 35;  teclas = $false }
	sr2  = @{ s = 60;  teclas = $false }
}
$j = $bancos[$Guest]
if (-not $j) { throw "guest desconocido: $Guest" }
if ($Segundos -gt 0) { $j.s = $Segundos }
$img = ImagenBanco $Guest
if (-not $img) { throw "sin imagen para $Guest" }

function Correr($demora, $hilos) {
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MANDO","DCEMU_CP_MS")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($j.teclas) { $env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1" }
	$env:DCEMU_RTC_FIJO = "1000000000"
	$env:DCEMU_AICA_DEMORA_LINEA = "$demora"

	$args = @("--salir-tras=$($j.s)", "--sin-vmu", "--sin-audio")
	# Explicito en los dos brazos: desde la adopcion (2026-09-05) la omision es
	# con hilos, y un brazo "sin" que no lo dijera mediria hilos contra hilos.
	$args += $(if ($hilos) { "--hilos" } else { "--sin-hilos" })
	$args += $img

	$t = [Diagnostics.Stopwatch]::StartNew()
	& $Exe @args | Out-Null
	$t.Stop()

	$esp = (Select-String -Path $err -Pattern "hilo del AICA, linea" -EA SilentlyContinue | ForEach-Object { $_.Line }) -join ""
	return @{ ms = $t.ElapsedMilliseconds; esp = $esp }
}

foreach ($d in $Demoras) {
	Write-Output "=== $Guest, $($j.s) s emulados, demora $d ($Modo)"

	if ($Modo -eq "latencia") {
		$brazos = @(@{ n = "D=$d";  demora = $d; hilos = $false },
					@{ n = "D=0";   demora = 0;  hilos = $false })
	} else {
		$brazos = @(@{ n = "con hilos"; demora = $d; hilos = $true  },
					@{ n = "sin hilos"; demora = $d; hilos = $false })
	}

	# calentamiento, descartado
	Correr $brazos[1].demora $brazos[1].hilos | Out-Null

	for ($r = 1; $r -le $Rondas; $r++) {
		$orden = if ($r % 2 -eq 1) { @(0, 1) } else { @(1, 0) }
		foreach ($i in $orden) {
			$b = $brazos[$i]
			$res = Correr $b.demora $b.hilos
			"{0} r{1} {2,-10} {3,7} ms   {4}" -f $Guest, $r, $b.n, $res.ms, $res.esp
		}
	}
}

foreach ($v in @("DCEMU_RTC_FIJO","DCEMU_AICA_DEMORA_LINEA","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
