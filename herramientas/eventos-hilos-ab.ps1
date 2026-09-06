# eventos-hilos-ab.ps1 -- la tanda A/B del reloj por eventos bajo --hilos
# (2026-09-05): un binario, los dos brazos con --hilos explicito, el reloj por
# eventos encendido (la omision) contra DCEMU_SIN_RELOJ_EVENTOS=1 (la conducta
# anterior bajo hilos: un servicio del bloque periodico por grano). Misma
# receta que linea-ab.ps1: rondas con el orden rotado dentro del par,
# calentamiento descartado, RTC clavado, sin captura, sin VMU, sin audio.
param(
	[string] $Exe = "build-clang\dcemu.exe",
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
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))  reloj por eventos bajo --hilos: encendido contra apagado, guest $Guest"

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

function Correr($sinEventos) {
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MANDO","DCEMU_CP_MS","DCEMU_JIT","DCEMU_SIN_RELOJ_EVENTOS")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($j.teclas) { $env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1" }
	$env:DCEMU_RTC_FIJO = "1000000000"
	if ($sinEventos) { $env:DCEMU_SIN_RELOJ_EVENTOS = "1" }

	$args = @("--salir-tras=$($j.s)", "--sin-vmu", "--sin-audio", "--hilos", $img)

	$t = [Diagnostics.Stopwatch]::StartNew()
	& $Exe @args | Out-Null
	$t.Stop()

	$jit = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue | Select-Object -First 1 | ForEach-Object Line)
	if (-not $jit) { $jit = "(sin resumen jit:)" }
	return @{ ms = $t.ElapsedMilliseconds; jit = $jit }
}

Write-Output "=== $Guest, $($j.s) s emulados, $Rondas rondas (calentamiento descartado)"
$brazos = @(@{ n = "eventos"; sin = $false }, @{ n = "cada grano"; sin = $true })
Correr $brazos[1].sin | Out-Null

$res = @{ "eventos" = @(); "cada grano" = @() }
for ($r = 1; $r -le $Rondas; $r++) {
	$orden = if ($r % 2 -eq 1) { @(0, 1) } else { @(1, 0) }
	foreach ($i in $orden) {
		$b = $brazos[$i]
		$x = Correr $b.sin
		$res[$b.n] += $x.ms
		"{0} r{1} {2,-11} {3,7} ms   {4}" -f $Guest, $r, $b.n, $x.ms, $x.jit
	}
}

$a = $res["eventos"] | Measure-Object -Minimum -Maximum -Average
$c = $res["cada grano"] | Measure-Object -Minimum -Maximum -Average
$pares = 0
for ($r = 0; $r -lt $Rondas; $r++) { if ($res["eventos"][$r] -lt $res["cada grano"][$r]) { $pares++ } }
$disjuntos = ($a.Maximum -lt $c.Minimum) -or ($c.Maximum -lt $a.Minimum)
$pct = ($a.Average - $c.Average) / $c.Average * 100
"{0}: eventos {1}-{2} ms, cada grano {3}-{4} ms, {5:+0.0;-0.0} % en la media, {6}/{7} pares a favor de eventos, rangos {8}" -f `
	$Guest, $a.Minimum, $a.Maximum, $c.Minimum, $c.Maximum, $pct, $pares, $Rondas,
	$(if ($disjuntos) { "DISJUNTOS" } else { "solapados" })

foreach ($v in @("DCEMU_RTC_FIJO","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_SIN_RELOJ_EVENTOS")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
