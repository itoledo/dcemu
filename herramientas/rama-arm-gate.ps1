# La compuerta de la cola de salto y del sondeo en bloque del ARM7 (fase E de
# docs/jit-sota-plan.md): TRES brazos dentro del mismo binario -- `con` (todo),
# `medio` (DCEMU_SIN_SONDEO_ARM=1: solo la cola) y `viejo` (ademas
# DCEMU_SIN_RAMA_ARM=1: la conducta anterior entera) --, exactitud al byte en
# los tres guests del banco: captura, .wav en CT (el guest AICA del banco),
# puntos de DCEMU_CP_MS punto por punto, y el histograma del ARM (pasos y
# filas identicos entre brazos; las lineas de bloques/vueltas y las palabras
# decodificadas son el efecto del mecanismo, no la comparacion). El histograma
# es la comparacion que cazo el agujero del presupuesto de la reposicion: los
# mecanismos de aca no eliden, asi que los pasos ejecutados TIENEN que salir
# identicos.
param([string] $Exe = "build-jit\Release\dcemu.exe")

$brazos = @('con', 'medio', 'viejo')

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

function Correr($b, $brazo, $perfil)
{
	if ($b.teclas) { $env:DCEMU_PULSAR_START = '300,1100'; $env:DCEMU_PULSAR_A = '1'; $env:DCEMU_SOLO_A = '1' }
	else { Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue }

	Remove-Item env:DCEMU_SIN_RAMA_ARM,env:DCEMU_SIN_SONDEO_ARM -EA SilentlyContinue
	if ($brazo -in 'medio', 'viejo') { $env:DCEMU_SIN_SONDEO_ARM = '1' }
	if ($brazo -eq 'viejo') { $env:DCEMU_SIN_RAMA_ARM = '1' }

	# La memo clavada ENCENDIDA en los brazos comparados: desde el A/B del
	# 2026-08-20 la omision la apaga bajo bloques, y sin el clavo el brazo
	# viejo (rama apagada -> memo encendida) compararia elisiones distintas
	# -- el histograma fallaria por diseno, no por bug. El interjuego
	# borde/grabacion de la cola queda asi ejercitado igual que siempre.
	# La omision nueva se compara aparte, a nivel de salida (abajo).
	if ($brazo -eq 'omision') { Remove-Item env:DCEMU_MEMO_ARM -EA SilentlyContinue }
	else { $env:DCEMU_MEMO_ARM = '1' }
	if ($perfil) { $env:DCEMU_PERFIL_ARM = '1' } else { Remove-Item env:DCEMU_PERFIL_ARM -EA SilentlyContinue }

	$env:DCEMU_JIT = '2'; $env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS = "$($b.s * 1000)"

	$suf = if ($perfil) { "$brazo-perfil" } else { $brazo }
	$bmp = "logs\rama-$($b.n)-$suf.bmp"
	$args = @("--salir-tras=$($b.s)", '--sin-vmu', "--captura-gl=$bmp")
	if ($b.wav -and -not $perfil) { $args += @('--sin-audio', "--captura-audio=logs\rama-$($b.n)-$suf.wav") }

	& $Exe @args $b.img | Out-Null
	Copy-Item $err "logs\rama-$($b.n)-$suf.txt" -Force
	(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

foreach ($b in $bancos) {
	$h = @{}
	foreach ($m in $brazos) { $h[$m] = Correr $b $m $false }

	$base = Select-String -Path "logs\rama-$($b.n)-con.txt" -Pattern '^cp ' | ForEach-Object Line
	foreach ($m in @('medio', 'viejo')) {
		$c = Select-String -Path "logs\rama-$($b.n)-$m.txt" -Pattern '^cp ' | ForEach-Object Line
		$n = [Math]::Min($base.Count, $c.Count); $p = -1
		for ($i = 0; $i -lt $n; $i++) { if ($base[$i] -cne $c[$i]) { $p = $i; break } }
		"{0,-7} con/{1,-6} bmp {2,-9}  puntos {3}/{4} primer distinto={5}" -f $b.n, $m,
			($(if ($h['con'] -eq $h[$m]) { 'identicas' } else { "DISTINTAS $($h['con'])/$($h[$m])" })),
			$base.Count, $c.Count,
			$(if ($p -ge 0) { $p } elseif ($base.Count -ne $c.Count) { 'largo' } else { 'ninguno' })
		if ($p -ge 0) { "  con:   $($base[$p])"; "  ${m}: $($c[$p])" }
	}

	if ($b.wav) {
		$wc = (Get-FileHash "logs\rama-$($b.n)-con.wav" -Algorithm SHA256).Hash
		foreach ($m in @('medio', 'viejo')) {
			$wm = (Get-FileHash "logs\rama-$($b.n)-$m.wav" -Algorithm SHA256).Hash
			"{0,-7} wav con/{1,-6} identicos={2}" -f $b.n, $m, ($wc -eq $wm)
		}
	}
}

# El histograma del ARM: CT con DCEMU_PERFIL_ARM, los tres brazos. Se comparan
# los pasos y las filas; bloques/vueltas y decodificadas quedan como efecto.
$bp = $bancos[1].Clone(); $bp.s = 30; $bp.wav = $false
foreach ($m in $brazos) { Correr $bp $m $true | Out-Null }

$fa = Select-String -Path logs\rama-ct-con-perfil.txt -Pattern '^arm7:' | ForEach-Object Line |
	Where-Object { $_ -notmatch 'bloques corridos|palabras decodificadas' }
foreach ($m in @('medio', 'viejo')) {
	$fb = Select-String -Path "logs\rama-ct-$m-perfil.txt" -Pattern '^arm7:' | ForEach-Object Line |
		Where-Object { $_ -notmatch 'bloques corridos|palabras decodificadas|rechazo por|descubrimientos' }
	$dif = @(Compare-Object $fa $fb)
	"histograma con/${m} identico: $($dif.Count -eq 0)"
	if ($dif.Count) { $dif | Select-Object -First 6 | ForEach-Object { "  $($_.SideIndicator) $($_.InputObject)" } }
}

foreach ($m in $brazos) {
	Select-String -Path "logs\rama-ct-$m-perfil.txt" -Pattern 'bloques corridos' | ForEach-Object { "efecto ${m}: $($_.Line)" }
}

# La omision nueva (memo apagada bajo bloques): exactitud a nivel de salida
# contra el brazo `con` clavado -- la elision es exacta por construccion, y
# bmp/wav/cp lo atestiguan a traves del cambio de estado del memo.
$bo = $bancos[1]
$ho = Correr $bo 'omision' $false
$co = Select-String -Path "logs\rama-ct-omision.txt" -Pattern '^cp ' | ForEach-Object Line
$cc = Select-String -Path "logs\rama-ct-con.txt" -Pattern '^cp ' | ForEach-Object Line
$n = [Math]::Min($co.Count, $cc.Count); $p = -1
for ($i = 0; $i -lt $n; $i++) { if ($co[$i] -cne $cc[$i]) { $p = $i; break } }
$hc = (Get-FileHash "logs\rama-ct-con.bmp" -Algorithm SHA256).Hash.Substring(0,16)
"ct      con/omision bmp {0,-9}  puntos {1}/{2} primer distinto={3}" -f
	($(if ($hc -eq $ho) { 'identicas' } else { "DISTINTAS $hc/$ho" })),
	$cc.Count, $co.Count,
	$(if ($p -ge 0) { $p } elseif ($cc.Count -ne $co.Count) { 'largo' } else { 'ninguno' })
$wo = (Get-FileHash "logs\rama-ct-omision.wav" -Algorithm SHA256).Hash
$wc2 = (Get-FileHash "logs\rama-ct-con.wav" -Algorithm SHA256).Hash
"ct      wav con/omision identicos={0}" -f ($wo -eq $wc2)

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_SIN_RAMA_ARM,env:DCEMU_SIN_SONDEO_ARM,env:DCEMU_MEMO_ARM,env:DCEMU_PERFIL_ARM,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
"hecho"
