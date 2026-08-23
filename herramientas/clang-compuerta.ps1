# La compuerta de exactitud de la fase G (clang-cl contra MSVC): los tres
# guests del banco, cada uno por tres brazos -- clang-int, clang-jit y
# msvc-jit -- comparando captura byte a byte y DCEMU_CP_MS punto por punto.
# clang-int contra clang-jit es la compuerta int/jit del binario nuevo;
# clang-jit contra msvc-jit, la igualdad entre compiladores. El trio CHD va
# aparte con chd-exactitud.ps1 -Exe build-clang\dcemu.exe.
#
# Ademas un par de .wav de Crazy Taxi (clang contra msvc, --sin-audio +
# --captura-audio): el mezclador del AICA es coma flotante y es exactamente lo
# que un compilador nuevo puede mover.
#
# Secuencial (una cadena por maquina), --sin-vmu, RTC clavado en todos los
# brazos, stderr copiado tras cada corrida.
param(
	[string] $Clang = "build-clang\dcemu.exe",
	[string] $Msvc  = "build-jit\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"
if (Get-Process dcemu -EA SilentlyContinue) { throw "hay un dcemu corriendo" }

foreach ($e in @($Clang, $Msvc)) {
	Write-Output "$e  $((Get-FileHash $e -Algorithm SHA256).Hash.Substring(0,16))"
}

$guests = @(
	@{ n = "doom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 20; teclas = $false },
	@{ n = "ct";   img = "roms\Crazy Taxi (USA).cdi";              s = 40; teclas = $true },
	@{ n = "sr2";  img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

function Correr($g, $exe, $brazo, $jit)
{
	if ($g.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
	$env:DCEMU_JIT = if ($jit) { "2" } else { "0" }
	$env:DCEMU_CP_MS = "$($g.s * 1000)"
	$env:DCEMU_RTC_FIJO = "1000000000"

	$bmp = "logs\fg-$($g.n)-$brazo.bmp"
	& $exe "--salir-tras=$($g.s)" --sin-vmu "--captura-gl=$bmp" $g.img | Out-Null
	Copy-Item (Join-Path (Split-Path $exe) "stderr.txt") "logs\fg-$($g.n)-$brazo.txt" -Force
	(Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0, 16)
}

function CompararCp($g, $a, $b, $rotulo)
{
	$ca = Select-String "logs\fg-$($g.n)-$a.txt" -Pattern "^cp " | ForEach-Object Line
	$cb = Select-String "logs\fg-$($g.n)-$b.txt" -Pattern "^cp " | ForEach-Object Line
	$n = [Math]::Min($ca.Count, $cb.Count)
	$primero = -1
	for ($i = 0; $i -lt $n; $i++) { if ($ca[$i] -cne $cb[$i]) { $primero = $i; break } }
	$v = if ($primero -ge 0) { "primer distinto=$primero" }
		elseif ($ca.Count -ne $cb.Count) { "largos distintos ($($ca.Count)/$($cb.Count))" }
		else { "IDENTICOS ($($ca.Count) puntos)" }
	Write-Output "  cp $rotulo : $v"
	if ($primero -ge 0) {
		Write-Output "    ${a}: $($ca[$primero])"
		Write-Output "    ${b}: $($cb[$primero])"
	}
}

foreach ($g in $guests) {
	Write-Output "=== $($g.n), $($g.s) s emulados"
	$h = @{}
	$h["clang-int"] = Correr $g $Clang "clang-int" $false
	$h["clang-jit"] = Correr $g $Clang "clang-jit" $true
	$h["msvc-jit"]  = Correr $g $Msvc  "msvc-jit"  $true

	Write-Output "  bmp clang-int=$($h['clang-int']) clang-jit=$($h['clang-jit']) msvc-jit=$($h['msvc-jit'])"
	CompararCp $g "clang-int" "clang-jit" "int/jit (clang)  "
	CompararCp $g "clang-jit" "msvc-jit"  "clang/msvc (jit) "
}

# El par de .wav: la medida del mezclador, no de la tarjeta. Con las teclas
# del banco -- sin ellas CT no suena y el archivo cuida nada.
Write-Output "=== ct .wav (40 s, --sin-audio)"
$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
$env:DCEMU_JIT = "2"; $env:DCEMU_RTC_FIJO = "1000000000"
Remove-Item env:DCEMU_CP_MS -EA SilentlyContinue
foreach ($lado in @(@{ n = "clang"; exe = $Clang }, @{ n = "msvc"; exe = $Msvc })) {
	& $lado.exe --salir-tras=40 --sin-vmu --sin-audio "--captura-audio=logs\fg-ct-$($lado.n).wav" "roms\Crazy Taxi (USA).cdi" | Out-Null
	Write-Output "  $($lado.n): $((Get-FileHash "logs\fg-ct-$($lado.n).wav" -Algorithm SHA256).Hash.Substring(0,16))"
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "hecho"
