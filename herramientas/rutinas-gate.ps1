# La compuerta de las variantes FPU del traductor (jit.c): DOS brazos en
# el mismo binario -- con (por omision) y sin (DCEMU_JIT_TRAD_EN_LINEA=1: la busqueda ciega al modo
# de siempre). La baranda que manda es el .wav de Crazy Taxi (78 pasos de
# reverberacion programados: exactamente lo que el emisor traduce), mas
# captura y DCEMU_CP_MS en los tres guests del banco.
param([string] $Exe = "build-clang\dcemu.exe")

$brazos = @('con', 'sin')

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Exe)) { throw "falta $Exe" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"binario: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n='dcdoom'; img='roms\DCDoom GDI and CDI\DCDoom CDI.cdi'; s=20; teclas=$false; wav=$false },
	@{ n='ct';     img='roms\Crazy Taxi (USA).cdi';              s=60; teclas=$true;  wav=$true },
	@{ n='sr2';    img='roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi'; s=30; teclas=$false; wav=$false }
)

function Correr($b, $brazo)
{
	if ($b.teclas) { $env:DCEMU_PULSAR_START = '300,1100'; $env:DCEMU_PULSAR_A = '1'; $env:DCEMU_SOLO_A = '1' }
	else { Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue }

	Remove-Item env:DCEMU_JIT_TRAD_EN_LINEA -EA SilentlyContinue
	if ($brazo -eq 'sin') { $env:DCEMU_JIT_TRAD_EN_LINEA = '1' }

	$env:DCEMU_JIT = '2'; $env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS = "$($b.s * 1000)"

	$bmp = "logs\tr-$($b.n)-$brazo.bmp"
	$args = @("--salir-tras=$($b.s)", '--sin-vmu', "--captura-gl=$bmp")
	if ($b.wav) { $args += @('--sin-audio', "--captura-audio=logs\tr-$($b.n)-$brazo.wav") }

	& $Exe @args $b.img | Out-Null
	Copy-Item $err "logs\tr-$($b.n)-$brazo.txt" -Force
	(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

foreach ($b in $bancos) {
	$h = @{}
	foreach ($m in $brazos) { $h[$m] = Correr $b $m }

	$base = Select-String -Path "logs\tr-$($b.n)-con.txt" -Pattern '^cp ' | ForEach-Object Line
	$c = Select-String -Path "logs\tr-$($b.n)-sin.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($base.Count, $c.Count); $p = -1
	for ($i = 0; $i -lt $n; $i++) { if ($base[$i] -cne $c[$i]) { $p = $i; break } }
	"{0,-7} bmp {1,-9}  puntos {2}/{3} primer distinto={4}" -f $b.n,
		($(if ($h['con'] -eq $h['sin']) { 'identicas' } else { "DISTINTAS $($h['con'])/$($h['sin'])" })),
		$base.Count, $c.Count,
		$(if ($p -ge 0) { $p } elseif ($base.Count -ne $c.Count) { 'largo' } else { 'ninguno' })
	if ($p -ge 0) { "  con: $($base[$p])"; "  sin: $($c[$p])" }

	if ($b.wav) {
		$wc = (Get-FileHash "logs\tr-$($b.n)-con.wav" -Algorithm SHA256).Hash
		$ws = (Get-FileHash "logs\tr-$($b.n)-sin.wav" -Algorithm SHA256).Hash
		"{0,-7} wav identicos={1}" -f $b.n, ($wc -eq $ws)
	}
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_JIT_TRAD_EN_LINEA,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
"hecho"
