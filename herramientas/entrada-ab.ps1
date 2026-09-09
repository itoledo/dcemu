# entrada-ab.ps1 -- la tanda A/B de la EMISION que habilita la entrada de 32
# bytes de la cache de traducciones (mmu.h): un binario, palancas de ambiente.
# Brazo nuevo: indice por corrimiento y mascara ya negada. Brazo viejo
# (DCEMU_MMU_EMISION_VIEJA=1): imul por el tamano y not al vuelo.
#
# Lo que ESTE guion no puede medir es la forma de la entrada: 28 contra 32
# bytes es tamano de struct, o sea compilacion, y va por binarios-ab.ps1
# contra el canonico anterior. Aqui se aisla la mitad emitida.
#
# Receta del arbol: rondas con el orden rotado dentro del par, calentamiento
# por guest descartado, RTC clavado, sin captura, sin VMU, sin audio.
#
# Solo distingue algo en los guests con MMU: en modo plano el codigo emitido
# no traduce nada. Crazy Taxi va igual, de testigo.
param(
	[string] $Exe = "build-clang\dcemu.exe",
	[string] $Guest = "sr2",
	[int]    $Segundos = 0,
	[int]    $Rondas = 4
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
. "$PSScriptRoot\banco.ps1"

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))  emision nueva contra vieja, guest $Guest"

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

function Correr($vieja) {
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MANDO","DCEMU_CP_MS","DCEMU_JIT","DCEMU_MMU_EMISION_VIEJA")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($j.teclas) { $env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1" }
	$env:DCEMU_RTC_FIJO = "1000000000"
	if ($vieja) { $env:DCEMU_MMU_EMISION_VIEJA = "1" }

	$t = [Diagnostics.Stopwatch]::StartNew()
	& $Exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio $img | Out-Null
	$t.Stop()

	# Los dos controles: el total de instrucciones (que entre brazos exactos
	# sale al digito) y la linea que dice que brazo corrio de verdad.
	$jit = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue | Select-Object -First 1 | ForEach-Object Line)
	$et  = (Select-String -Path $err -Pattern "^jit: etiqueta de traduccion" -EA SilentlyContinue | Select-Object -First 1 | ForEach-Object Line)
	if (-not $et) { throw "el binario no imprime la linea de la etiqueta: no hay A/B" }
	return @{ ms = $t.ElapsedMilliseconds; jit = "$jit | $et" }
}

Write-Output "=== $Guest, $($j.s) s emulados, $Rondas rondas (calentamiento descartado)"
$brazos = @(@{ n = "nueva"; calc = $false }, @{ n = "vieja"; calc = $true })
Correr $brazos[1].calc | Out-Null

$res = @{ "nueva" = @(); "vieja" = @() }
for ($r = 1; $r -le $Rondas; $r++) {
	$orden = if ($r % 2 -eq 1) { @(0, 1) } else { @(1, 0) }
	foreach ($i in $orden) {
		$b = $brazos[$i]
		$x = Correr $b.calc
		$res[$b.n] += $x.ms
		"{0} r{1} {2,-10} {3,7} ms   {4}" -f $Guest, $r, $b.n, $x.ms, $x.jit
	}
}

$a = $res["nueva"] | Measure-Object -Minimum -Maximum -Average
$c = $res["vieja"] | Measure-Object -Minimum -Maximum -Average
$pares = 0
for ($r = 0; $r -lt $Rondas; $r++) { if ($res["nueva"][$r] -lt $res["vieja"][$r]) { $pares++ } }
$disjuntos = ($a.Maximum -lt $c.Minimum) -or ($c.Maximum -lt $a.Minimum)
$pct = ($a.Average - $c.Average) / $c.Average * 100
"{0}: nueva {1}-{2} ms, vieja {3}-{4} ms, {5:+0.0;-0.0} % en la media, {6}/{7} pares a favor de la nueva, rangos {8}" -f `
	$Guest, $a.Minimum, $a.Maximum, $c.Minimum, $c.Maximum, $pct, $pares, $Rondas,
	$(if ($disjuntos) { "DISJUNTOS" } else { "solapados" })

foreach ($v in @("DCEMU_RTC_FIJO","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MMU_EMISION_VIEJA")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
