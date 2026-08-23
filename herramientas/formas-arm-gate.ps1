# La compuerta de las formas anchas y la cola de retorno del ARM7 (fase E de
# docs/jit-sota-plan.md): CUATRO brazos que componen hacia abajo dentro del
# mismo binario -- `con` (todo), `sinret` (DCEMU_SIN_RETORNO_ARM=1: sin la
# cola generalizada ni la terminal sola), `medio` (ademas
# DCEMU_SIN_CABE_ARM=1: las formas corren en el interprete pero los bloques
# no las admiten) y `viejo` (ademas DCEMU_SIN_FORMAS_ARM=1: la conducta
# anterior entera) --, exactitud al byte en los tres guests del banco:
# captura, .wav en CT, puntos de DCEMU_CP_MS punto por punto, y el histograma
# del ARM (pasos y filas identicos entre brazos: nada de esto elide, asi que
# los pasos ejecutados TIENEN que salir identicos; los contadores del
# mecanismo quedan fuera de la comparacion, la leccion de rama-arm-gate.ps1).
# La sonda de marcas negativas imprime con prefijo `arm7 neg:` y no entra.
param([string] $Exe = "build-jit\Release\dcemu.exe")

$brazos = @('con', 'sinret', 'medio', 'viejo')

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

	Remove-Item env:DCEMU_SIN_FORMAS_ARM,env:DCEMU_SIN_CABE_ARM,env:DCEMU_SIN_RETORNO_ARM -EA SilentlyContinue
	if ($brazo -in 'sinret', 'medio', 'viejo') { $env:DCEMU_SIN_RETORNO_ARM = '1' }
	if ($brazo -in 'medio', 'viejo') { $env:DCEMU_SIN_CABE_ARM = '1' }
	if ($brazo -eq 'viejo') { $env:DCEMU_SIN_FORMAS_ARM = '1' }
	if ($perfil) { $env:DCEMU_PERFIL_ARM = '1' } else { Remove-Item env:DCEMU_PERFIL_ARM -EA SilentlyContinue }

	$env:DCEMU_JIT = '2'; $env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS = "$($b.s * 1000)"

	$suf = if ($perfil) { "$brazo-perfil" } else { $brazo }
	$bmp = "logs\formas-$($b.n)-$suf.bmp"
	$args = @("--salir-tras=$($b.s)", '--sin-vmu', "--captura-gl=$bmp")
	if ($b.wav -and -not $perfil) { $args += @('--sin-audio', "--captura-audio=logs\formas-$($b.n)-$suf.wav") }

	& $Exe @args $b.img | Out-Null
	Copy-Item $err "logs\formas-$($b.n)-$suf.txt" -Force
	(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

foreach ($b in $bancos) {
	$h = @{}
	foreach ($m in $brazos) { $h[$m] = Correr $b $m $false }

	$base = Select-String -Path "logs\formas-$($b.n)-con.txt" -Pattern '^cp ' | ForEach-Object Line
	foreach ($m in @('sinret', 'medio', 'viejo')) {
		$c = Select-String -Path "logs\formas-$($b.n)-$m.txt" -Pattern '^cp ' | ForEach-Object Line
		$n = [Math]::Min($base.Count, $c.Count); $p = -1
		for ($i = 0; $i -lt $n; $i++) { if ($base[$i] -cne $c[$i]) { $p = $i; break } }
		"{0,-7} con/{1,-6} bmp {2,-9}  puntos {3}/{4} primer distinto={5}" -f $b.n, $m,
			($(if ($h['con'] -eq $h[$m]) { 'identicas' } else { "DISTINTAS $($h['con'])/$($h[$m])" })),
			$base.Count, $c.Count,
			$(if ($p -ge 0) { $p } elseif ($base.Count -ne $c.Count) { 'largo' } else { 'ninguno' })
		if ($p -ge 0) { "  con:   $($base[$p])"; "  ${m}: $($c[$p])" }
	}

	if ($b.wav) {
		$wc = (Get-FileHash "logs\formas-$($b.n)-con.wav" -Algorithm SHA256).Hash
		foreach ($m in @('sinret', 'medio', 'viejo')) {
			$wm = (Get-FileHash "logs\formas-$($b.n)-$m.wav" -Algorithm SHA256).Hash
			"{0,-7} wav con/{1,-6} identicos={2}" -f $b.n, $m, ($wc -eq $wm)
		}
	}
}

# El histograma del ARM: CT con DCEMU_PERFIL_ARM, los tres brazos. Pasos y
# filas se comparan; los contadores del mecanismo quedan como efecto.
$bp = $bancos[1].Clone(); $bp.s = 30; $bp.wav = $false
foreach ($m in $brazos) { Correr $bp $m $true | Out-Null }

$fa = Select-String -Path logs\formas-ct-con-perfil.txt -Pattern '^arm7:' | ForEach-Object Line |
	Where-Object { $_ -notmatch 'bloques corridos|palabras decodificadas|rechazo por|descubrimientos' }
foreach ($m in @('sinret', 'medio', 'viejo')) {
	$fb = Select-String -Path "logs\formas-ct-$m-perfil.txt" -Pattern '^arm7:' | ForEach-Object Line |
		Where-Object { $_ -notmatch 'bloques corridos|palabras decodificadas|rechazo por|descubrimientos' }
	$dif = @(Compare-Object $fa $fb)
	"histograma con/${m} identico: $($dif.Count -eq 0)"
	if ($dif.Count) { $dif | Select-Object -First 6 | ForEach-Object { "  $($_.SideIndicator) $($_.InputObject)" } }
}

foreach ($m in $brazos) {
	Select-String -Path "logs\formas-ct-$m-perfil.txt" -Pattern 'bloques corridos|rechazo por marca' | ForEach-Object { "efecto ${m}: $($_.Line)" }
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_SIN_FORMAS_ARM,env:DCEMU_SIN_CABE_ARM,env:DCEMU_SIN_RETORNO_ARM,env:DCEMU_PERFIL_ARM,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
"hecho"
