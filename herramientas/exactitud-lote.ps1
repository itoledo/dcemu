# La compuerta de exactitud de un lote de emision (fase B de jit-sota-plan.md):
# los tres guests del banco, interprete contra DCEMU_JIT=2 sobre el mismo
# binario, captura byte a byte + DCEMU_CP_MS punto por punto. DCEMU_RTC_FIJO en
# los dos brazos: la regla que dejo el expediente de THPS2 (un juego tambien
# puede leer el reloj en medio de la corrida). DOOM y SR2 son los arbitros
# inmunes al pad; CT viaja igual porque es el guest FPU que el lote ataca.
param(
	[string] $Exe = "build-jit\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Exe)) { throw "falta $Exe" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n='dcdoom'; img='roms\DCDoom GDI and CDI\DCDoom CDI.cdi'; s=35; teclas=$false },
	@{ n='ct'; img='roms\Crazy Taxi (USA).cdi'; s=180; teclas=$true },
	@{ n='sr2'; img='roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi'; s=60; teclas=$false }
)

function Correr($b, $brazo)
{
	if ($b.teclas) {
		$env:DCEMU_PULSAR_START = '300,1100'; $env:DCEMU_PULSAR_A = '1'; $env:DCEMU_SOLO_A = '1'
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}
	# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
	$env:DCEMU_JIT = if ($brazo -eq 'jit') { '2' } else { '0' }
	$env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS = "$($b.s * 1000)"

	$bmp = "logs\lote-$($b.n)-$brazo.bmp"
	& $Exe "--salir-tras=$($b.s)" --sin-vmu "--captura-gl=$bmp" $b.img | Out-Null
	Copy-Item $err "logs\lote-$($b.n)-$brazo.txt" -Force
	(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

foreach ($b in $bancos) {
	$ha = Correr $b 'int'
	$hb = Correr $b 'jit'

	$a = Select-String -Path "logs\lote-$($b.n)-int.txt" -Pattern '^cp ' | ForEach-Object Line
	$c = Select-String -Path "logs\lote-$($b.n)-jit.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($a.Count, $c.Count); $p = -1
	for ($i = 0; $i -lt $n; $i++) { if ($a[$i] -cne $c[$i]) { $p = $i; break } }
	"{0,-7} bmp int={1} jit={2} identicas={3}  puntos {4}/{5} primer distinto={6}" -f $b.n, $ha, $hb,
		($ha -eq $hb), $a.Count, $c.Count,
		$(if ($p -ge 0) { $p } elseif ($a.Count -ne $c.Count) { 'largo' } else { 'ninguno' })
	if ($p -ge 0) { "  int: $($a[$p])"; "  jit: $($c[$p])" }
	Select-String -Path "logs\lote-$($b.n)-jit.txt" -Pattern 'instrucciones en ' | ForEach-Object { "  $($_.Line)" }
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "hecho"
