# La compuerta de la verificacion por generacion de onda del ARM7 (ver arm7_verif_onda en arm7.c): DOS brazos en el mismo binario -- `con` (por
# omision) y `sin` (DCEMU_SIN_VERIF_ONDA=1: el memcmp en cada salto, la
# conducta anterior) -- con exactitud al byte en los tres guests del banco:
# captura, .wav en CT, DCEMU_CP_MS punto por punto, y el histograma del ARM
# bajo perfil. El mecanismo NO elide pasos -- solo memcmps --, asi que los
# histogramas TIENEN que salir identicos, filas y pasos por igual.
#
# Del perfil sale ademas el censo del mecanismo ("arm7 verif:"): cuantos
# memcmp corrieron y cuantos se elidieron, que es el techo de la ganancia.
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

function Correr($b, $brazo, $perfil)
{
	if ($b.teclas) { $env:DCEMU_PULSAR_START = '300,1100'; $env:DCEMU_PULSAR_A = '1'; $env:DCEMU_SOLO_A = '1' }
	else { Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue }

	Remove-Item env:DCEMU_SIN_VERIF_ONDA -EA SilentlyContinue
	if ($brazo -eq 'sin') { $env:DCEMU_SIN_VERIF_ONDA = '1' }
	if ($perfil) { $env:DCEMU_PERFIL_ARM = '1' } else { Remove-Item env:DCEMU_PERFIL_ARM -EA SilentlyContinue }

	$env:DCEMU_JIT = '2'; $env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS = "$($b.s * 1000)"

	$suf = if ($perfil) { "$brazo-perfil" } else { $brazo }
	$bmp = "logs\vl-$($b.n)-$suf.bmp"
	$args = @("--salir-tras=$($b.s)", '--sin-vmu', "--captura-gl=$bmp")
	if ($b.wav -and -not $perfil) { $args += @('--sin-audio', "--captura-audio=logs\vl-$($b.n)-$suf.wav") }

	& $Exe @args $b.img | Out-Null
	Copy-Item $err "logs\vl-$($b.n)-$suf.txt" -Force
	(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

foreach ($b in $bancos) {
	$h = @{}
	foreach ($m in $brazos) { $h[$m] = Correr $b $m $false }

	$base = Select-String -Path "logs\vl-$($b.n)-con.txt" -Pattern '^cp ' | ForEach-Object Line
	$c = Select-String -Path "logs\vl-$($b.n)-sin.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($base.Count, $c.Count); $p = -1
	for ($i = 0; $i -lt $n; $i++) { if ($base[$i] -cne $c[$i]) { $p = $i; break } }
	"{0,-7} bmp {1,-9}  puntos {2}/{3} primer distinto={4}" -f $b.n,
		($(if ($h['con'] -eq $h['sin']) { 'identicas' } else { "DISTINTAS $($h['con'])/$($h['sin'])" })),
		$base.Count, $c.Count,
		$(if ($p -ge 0) { $p } elseif ($base.Count -ne $c.Count) { 'largo' } else { 'ninguno' })
	if ($p -ge 0) { "  con: $($base[$p])"; "  sin: $($c[$p])" }

	if ($b.wav) {
		$wc = (Get-FileHash "logs\vl-$($b.n)-con.wav" -Algorithm SHA256).Hash
		$ws = (Get-FileHash "logs\vl-$($b.n)-sin.wav" -Algorithm SHA256).Hash
		"{0,-7} wav identicos={1}" -f $b.n, ($wc -eq $ws)
	}
}

# El histograma del ARM: CT bajo perfil, los dos brazos. Los contadores del
# mecanismo (arm7 verif:) llevan prefijo propio y quedan fuera de ^arm7:.
$bp = $bancos[1].Clone(); $bp.s = 30; $bp.wav = $false
foreach ($m in $brazos) { Correr $bp $m $true | Out-Null }

$fa = Select-String -Path logs\vl-ct-con-perfil.txt -Pattern '^arm7:' | ForEach-Object Line |
	Where-Object { $_ -notmatch 'bloques corridos|palabras decodificadas|rechazo por|descubrimientos' }
$fb = Select-String -Path logs\vl-ct-sin-perfil.txt -Pattern '^arm7:' | ForEach-Object Line |
	Where-Object { $_ -notmatch 'bloques corridos|palabras decodificadas|rechazo por|descubrimientos' }
$dif = @(Compare-Object $fa $fb)
"histograma con/sin identico: $($dif.Count -eq 0)"
if ($dif.Count) { $dif | Select-Object -First 6 | ForEach-Object { "  $($_.SideIndicator) $($_.InputObject)" } }

foreach ($m in $brazos) {
	Select-String -Path "logs\vl-ct-$m-perfil.txt" -Pattern 'bloques corridos|^arm7 verif:' |
		ForEach-Object { "censo ${m}: $($_.Line)" }
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_SIN_VERIF_ONDA,env:DCEMU_PERFIL_ARM,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
"hecho"

