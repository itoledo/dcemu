# linea-brazo.ps1 -- un brazo de la compuerta de la entrega de la linea del
# AICA (docs/hilos-plan.md, la entrega determinista): corre los tres guests del
# banco con una demora y con o sin hilos, y deja por guest la captura, el .wav,
# los puntos de DCEMU_CP_MS y la LISTA DE ENTREGAS (DCEMU_SONDA_ENTREGAS), mas
# un manifiesto con sus hashes. Dos brazos se comparan por manifiesto.
#
# La lista de entregas es el arbitro de un cambio de temporizacion de
# interrupciones (notas-tiempo.md): los puntos por ms muestrean en las entradas
# del bloque periodico y no ven una entrega corrida dentro del mismo ms.
#
# RTC clavado y --sin-vmu, la regla de toda compuerta. Crazy Taxi va con las
# teclas del banco (no con replay: aqui no se compara contra la receta del
# replay sino entre brazos de la misma corrida, y las teclas son deterministas).
param(
	[Parameter(Mandatory=$true)][string] $Nombre,
	[string] $Exe = "build-clang\dcemu.exe",
	[int]    $Demora = 0,
	[switch] $Hilos,
	[string] $Solo = ""
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
. "$PSScriptRoot\banco.ps1"

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
$dir = "logs\linea\$Nombre"
New-Item -ItemType Directory -Force $dir | Out-Null

$hash = (Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16)
"binario $hash  demora $Demora  hilos $($Hilos.IsPresent)" | Set-Content "$dir\manifiesto.txt"

function HashLineas($l) {
	$ms = [System.Security.Cryptography.SHA256]::Create()
	[BitConverter]::ToString($ms.ComputeHash([Text.Encoding]::UTF8.GetBytes(
		($l -join "`n")))).Replace("-","").Substring(0,16)
}

$guests = @(
	@{ n = "sr2";  s = 20; teclas = $false },
	@{ n = "doom"; s = 20; teclas = $false },
	@{ n = "ct";   s = 40; teclas = $true }
)
if ($Solo) { $guests = @($guests | Where-Object { $_.n -eq $Solo }) }

foreach ($g in $guests) {
	$img = ImagenBanco $g.n
	if (-not $img) { "$($g.n): SALTEADO, sin imagen" | Add-Content "$dir\manifiesto.txt"; continue }

	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MANDO")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($g.teclas) { $env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1" }

	$env:DCEMU_RTC_FIJO = "1000000000"
	$env:DCEMU_CP_MS = "$($g.s * 1000)"
	$env:DCEMU_SONDA_ENTREGAS = "1"
	$env:DCEMU_AICA_DEMORA_LINEA = "$Demora"

	$bmp = "$dir\$($g.n).bmp"; $wav = "$dir\$($g.n).wav"
	# Sin --traza-mem: la traza apaga el traductor y la compuerta tiene que
	# correr lo que se entrega. Los contadores de control imprimen sin ella.
	$args = @("--salir-tras=$($g.s)", "--sin-vmu", "--sin-audio",
		"--captura-gl=$bmp", "--captura-audio=$wav")
	# Explicito en los dos sentidos: desde la adopcion (2026-09-05) la omision
	# es con hilos, y un brazo "sin" que callara compararia hilos contra hilos.
	$args += $(if ($Hilos) { "--hilos" } else { "--sin-hilos" })
	$args += $img

	& $Exe @args | Out-Null

	$sal = Get-Content $err
	$cp  = @($sal | Where-Object { $_ -match '^cp ' })
	$ent = @($sal | Where-Object { $_ -match '^entrega ' })
	$cp  | Set-Content "$dir\$($g.n).cp"
	$ent | Set-Content "$dir\$($g.n).entregas"
	Copy-Item $err "$dir\$($g.n).stderr.txt" -Force

	$ctl = ($sal | Where-Object { $_ -match 'aica: linea al ASIC|hilo del AICA, linea|instrucciones en .* entradas' }) -join " | "

	$linea = "{0}: bmp {1} wav {2} cp {3}/{4} entregas {5}/{6}  [{7}]" -f $g.n,
		(Get-FileHash $bmp).Hash.Substring(0,16), (Get-FileHash $wav).Hash.Substring(0,16),
		$cp.Count, (HashLineas $cp), $ent.Count, (HashLineas $ent), $ctl
	$linea | Add-Content "$dir\manifiesto.txt"
	Write-Output $linea
}

foreach ($v in @("DCEMU_RTC_FIJO","DCEMU_CP_MS","DCEMU_SONDA_ENTREGAS","DCEMU_AICA_DEMORA_LINEA",
				 "DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
