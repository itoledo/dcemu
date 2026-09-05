# binarios-ab.ps1 -- tanda A/B entre DOS binarios: el caso que la regla "un solo
# binario, palancas de ambiente" no cubre, porque no hay palanca posible --un
# cambio de dependencia (SDL 1.2 -> SDL3, 2026-09-05), de compilador o de
# enlazador--. Misma receta que linea-ab.ps1: rondas con el orden rotado dentro
# del par, calentamiento por guest y por binario descartado, RTC clavado, sin
# captura, sin VMU, hash de los dos binarios impreso y comparado (dos iguales es
# la trampa del Copy-Item que fallo).
#
# La disposicion del codigo es una variable que aqui no se puede quitar: los
# dos binarios tienen que venir con su PGO **reentrenado** (canonicos), y aun
# asi el veredicto se lee por rangos y pares, no por medias. Al lado de cada
# corrida va el total de instrucciones del resumen jit:, que entre dos binarios
# exactos tiene que salir al digito (el control de que se midio lo mismo).
#
# Sin --sin-audio por omision: es el regimen que ve el usuario y, en el caso de
# SDL3, justo el camino que cambio (la callback del dispositivo). -SinAudio
# mide el otro regimen, el del banco de siempre.
param(
	[string] $Nuevo = "build-clang\dcemu.exe",
	[string] $Viejo = "..\dcemu-presdl\build-presdl\dcemu.exe",
	[string] $Guest = "ct",
	[int]    $Segundos = 0,
	[int]    $Rondas = 4,
	[switch] $SinAudio
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
. "$PSScriptRoot\banco.ps1"

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
foreach ($e in @($Nuevo, $Viejo)) {
	if (-not (Test-Path -LiteralPath $e)) { throw "falta $e" }
}
$hn = (Get-FileHash $Nuevo -Algorithm SHA256).Hash.Substring(0, 16)
$hv = (Get-FileHash $Viejo -Algorithm SHA256).Hash.Substring(0, 16)
if ($hn -eq $hv) { throw "los dos binarios son el mismo ($hn): no hay A/B" }
$audio = if ($SinAudio) { "--sin-audio" } else { "audio abierto" }
Write-Output "nuevo $hn ($Nuevo)  viejo $hv ($Viejo)  guest $Guest  $audio"

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

function Correr($exe) {
	foreach ($v in @("DCEMU_PULSAR_START", "DCEMU_PULSAR_A", "DCEMU_SOLO_A",
	                 "DCEMU_MANDO", "DCEMU_CP_MS", "DCEMU_JIT")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($j.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	}
	$env:DCEMU_RTC_FIJO = "1000000000"

	$args = @("--salir-tras=$($j.s)", "--sin-vmu")
	if ($SinAudio) { $args += "--sin-audio" }
	$args += $img

	$t = [Diagnostics.Stopwatch]::StartNew()
	& $exe @args | Out-Null
	$t.Stop()

	# El control: el resumen del traductor, con su total de instrucciones.
	$err = Join-Path (Split-Path -Parent $exe) "stderr.txt"
	$jit = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		Select-Object -First 1 | ForEach-Object { $_.Line })
	if (-not $jit) { $jit = "(sin resumen jit: en $err)" }
	return @{ ms = $t.ElapsedMilliseconds; jit = $jit }
}

$brazos = @(@{ n = "nuevo"; exe = $Nuevo }, @{ n = "viejo"; exe = $Viejo })
Write-Output "=== $Guest, $($j.s) s emulados, $Rondas rondas; calentamiento de los dos binarios (descartado)"
foreach ($b in $brazos) { Correr $b.exe | Out-Null }

$res = @{ nuevo = @(); viejo = @() }
for ($r = 1; $r -le $Rondas; $r++) {
	$orden = if ($r % 2 -eq 1) { @(0, 1) } else { @(1, 0) }
	foreach ($i in $orden) {
		$b = $brazos[$i]
		$x = Correr $b.exe
		$res[$b.n] += $x.ms
		"{0} r{1} {2,-6} {3,7} ms   {4}" -f $Guest, $r, $b.n, $x.ms, $x.jit
	}
}

$n = $res.nuevo | Measure-Object -Minimum -Maximum -Average
$v = $res.viejo | Measure-Object -Minimum -Maximum -Average
$pares = 0
for ($r = 0; $r -lt $Rondas; $r++) { if ($res.nuevo[$r] -lt $res.viejo[$r]) { $pares++ } }
$disjuntos = ($n.Maximum -lt $v.Minimum) -or ($v.Maximum -lt $n.Minimum)
$pct = ($n.Average - $v.Average) / $v.Average * 100
"{0}: nuevo {1}-{2} ms, viejo {3}-{4} ms, {5:+0.0;-0.0} % en la media, {6}/{7} pares a favor del nuevo, rangos {8}" -f `
	$Guest, $n.Minimum, $n.Maximum, $v.Minimum, $v.Maximum, $pct, $pares, $Rondas,
	$(if ($disjuntos) { "DISJUNTOS" } else { "solapados" })

foreach ($v in @("DCEMU_RTC_FIJO", "DCEMU_PULSAR_START", "DCEMU_PULSAR_A", "DCEMU_SOLO_A")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
