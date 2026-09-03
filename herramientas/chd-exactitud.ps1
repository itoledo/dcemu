# Las compuertas de exactitud del trio CHD (fase A de docs/jit-sota-plan.md):
# 18 Wheeler, Tony Hawk's Pro Skater 2 y Capcom vs. SNK 2 -- los tres juegos que
# solo existen como CHD y que nunca corrieron bajo DCEMU_JIT=2 -- intérprete
# contra traductor, captura byte a byte + DCEMU_CP_MS punto por punto.
#
# Secuencial a proposito (una cadena de verificacion por maquina), --sin-vmu
# (la regla del A/B), y stderr copiado tras cada corrida porque dos instancias
# se truncan una a la otra. 18 Wheeler lleva DCEMU_PULSAR_START en el probe
# 2700 (45 s emulados: su pantalla de PRESS START) en LOS DOS brazos.
param(
	[string] $Exe = "",
	[string] $Solo = ""   # correr un solo guest por nombre (p.ej. -Solo thps1)
)

$ErrorActionPreference = "Stop"
# Los .chd viven en E:\Juegos\roms\dreamcast en una maquina y en roms\chd en
# la otra: banco.ps1 los resuelve (DCEMU_CHD manda), y el ejecutable tambien.
. "$PSScriptRoot\banco.ps1"
if (-not $Exe) { $Exe = ExeBanco "build-jit" }
if (-not (Test-Path -LiteralPath $Exe)) { throw "falta $Exe" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$guests = @(
	@{ n = "18w";   img = (ImagenChd "18 Wheeler - American Pro Trucker (USA)");              s = 60; start = "2700" },
	@{ n = "thps2"; img = (ImagenChd "Tony Hawk's Pro Skater 2 (USA)");                       s = 40; start = $null },
	@{ n = "cvs2";  img = (ImagenChd "Capcom vs. SNK 2 - Millionaire Fighting 2001 (Japan)"); s = 40; start = $null },
	@{ n = "thps1"; img = (ImagenChd "Tony Hawk's Pro Skater (USA)");                         s = 40; start = $null }
)
if ($Solo) { $guests = @($guests | Where-Object { $_.n -eq $Solo }); if (-not $guests) { throw "guest desconocido: $Solo" } }
foreach ($g in $guests) { if (-not $g.img) { throw "falta el .chd de $($g.n) (ver herramientas/banco.ps1)" } }

function Correr($g, $brazo)
{
	if ($g.start) { $env:DCEMU_PULSAR_START = $g.start }
	else { Remove-Item env:DCEMU_PULSAR_START -EA SilentlyContinue }

	# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
	$env:DCEMU_JIT = if ($brazo -eq "jit") { "2" } else { "0" }

	$env:DCEMU_CP_MS = "$($g.s * 1000)"
	# La regla del expediente de THPS2: un juego tambien lee el reloj en medio
	# de la corrida, y el RTC sigue al host. Clavado en los DOS brazos.
	$env:DCEMU_RTC_FIJO = '1000000000'

	$bmp = "logs\chd-$($g.n)-$brazo.bmp"
	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe "--salir-tras=$($g.s)" --sin-vmu "--captura-gl=$bmp" $g.img | Out-Null
	$reloj.Stop()

	Copy-Item $err "logs\chd-$($g.n)-$brazo.txt" -Force
	"{0,-6} {1,-4} {2,8} ms  bmp={3}" -f $g.n, $brazo, $reloj.ElapsedMilliseconds,
		(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

foreach ($g in $guests) {
	Correr $g "int"
	Correr $g "jit"

	$a = Select-String -Path "logs\chd-$($g.n)-int.txt" -Pattern '^cp ' | ForEach-Object Line
	$b = Select-String -Path "logs\chd-$($g.n)-jit.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($a.Count, $b.Count)
	$primero = -1
	for ($i = 0; $i -lt $n; $i++) { if ($a[$i] -cne $b[$i]) { $primero = $i; break } }
	"{0,-6} puntos: int={1} jit={2} primer distinto={3}" -f $g.n, $a.Count, $b.Count,
		$(if ($primero -ge 0) { $primero } elseif ($a.Count -ne $b.Count) { "largo" } else { "ninguno" })
	if ($primero -ge 0) {
		"  int: $($a[$primero])"
		"  jit: $($b[$primero])"
	}
}

Remove-Item env:DCEMU_CP_MS,env:DCEMU_JIT,env:DCEMU_PULSAR_START -EA SilentlyContinue
Write-Output "hecho"
