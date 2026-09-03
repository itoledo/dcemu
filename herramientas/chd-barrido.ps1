# El barrido de validacion del parque CHD (fase A ampliada de jit-sota-plan.md):
# cada imagen corre 40 s emulados por los dos brazos -- interprete y DCEMU_JIT=2
# sobre el mismo binario -- con captura, DCEMU_CP_MS punto por punto y el RTC
# clavado (la regla del expediente de THPS2). El veredicto por juego: si
# arranca, si el traductor es exacto, y sus instrucciones por entrada.
#
# Secuencial (una cadena por maquina), con vigia anti-cuelgue por corrida: un
# guest que deje al emulador sin avanzar tiempo emulado no puede colgar el
# barrido entero. El banco de PGO no se toca: esto es material de exactitud y
# compatibilidad, no de cronometro.
param(
	[string] $Exe = "",
	[int]    $Segundos = 40,
	[int]    $TopeMin = 8,
	[string] $Tanda = 'viejos'		# viejos | nuevos | todos
)

$ErrorActionPreference = "Continue"
. "$PSScriptRoot\banco.ps1"
if (-not $Exe) { $Exe = ExeBanco "build-jit" }
Set-Location D:\dev\dcemu
if (-not (Test-Path -LiteralPath $Exe)) { throw "falta $Exe" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

# Los que faltan validar bajo el traductor. 18W/THPS2/CvS2 ya pasaron su
# compuerta (fase A); CT/CT2/VT tienen ademas la barandilla de doble contenedor.
$guests = @(
	'Sega Rally 2 (USA)',
	'Capcom vs. SNK (USA)',
	'ChuChu Rocket! (USA) (En,Ja,Fr,De,Es)',
	'4x4 Evo (USA)',
	'Dead or Alive 2 (USA)',
	'Mat Hoffman''s Pro BMX (USA)',
	'Quake III - Arena (USA)',
	'Street Fighter III - Double Impact (USA)',
	'Street Fighter III - 3rd Strike (USA)',
	'Street Fighter Alpha 3 (USA)',
	'Soulcalibur (USA)',
	'Sonic Adventure (USA)',
	'Sonic Adventure 2 (USA) (En,Ja,Fr,De,Es)',
	'Sonic Shuffle (USA)',
	'Jet Set Radio (Europe) (En,Fr,De,Es)',
	'MSR - Metropolis Street Racer (USA) (Rev A)',
	'Daytona USA (USA)',
	'Power Stone (USA)',
	'Power Stone 2 (USA)',
	'Re-Volt (USA)',
	'Mortal Kombat Gold (USA)',
	'San Francisco Rush 2049 (USA) (En,Fr,De,Es,It,Nl)',
	'Space Channel 5 (USA)',
	'Ooga Booga (USA)',
	'Tech Romancer (USA)'
)

# La carga del 2026-08-19: la lista pedida para ensanchar cobertura -- mas
# Windows CE (RE2, Tomb Raider, Rayman 2), streaming ADX (MvC2), RTT y mezcla
# (Ikaruga, Rez), multipass del TA (F355), RTC de verdad (Shenmue), FMV
# Sofdec (Berserk). Multi-disco: solo el disco 1.
$nuevos = @(
	'Ikaruga (Japan)',
	'Rez (Europe) (En,Ja,Fr,De,Es,It)',
	'Marvel vs. Capcom 2 (USA)',
	'F355 Challenge - Passione Rossa (USA)',
	'Test Drive Le Mans (USA) (En,Fr,Es)',
	'Sword of the Berserk - Guts'' Rage (USA)',
	'Grandia II (USA)',
	'Skies of Arcadia (USA) (Disc 1)',
	'Phantasy Star Online Ver. 2 (USA) (En,Ja,Fr,De,Es)',
	'Shenmue (USA) (Disc 1)',
	'Shenmue II (Europe) (En,Fr,De,Es) (Disc 1)',
	'Resident Evil 2 (USA) (Disc 1)',
	'Tomb Raider - The Last Revelation (USA)',
	'Rayman 2 - The Great Escape (USA) (En,Fr,De,Es,It)'
)

switch ($Tanda) {
	'nuevos' { $guests = $nuevos }
	'todos'  { $guests = $guests + $nuevos }
}

function Correr($nombre, $brazo)
{
	$img = ImagenChd $nombre
	if (-not $img) { throw "falta el .chd de $nombre (ver herramientas/banco.ps1)" }
	$tag = ($nombre -split ' \(')[0] -replace '[^A-Za-z0-9]', ''
	$bmp = "logs\chdb-$tag-$brazo.bmp"

	$env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS    = "$($Segundos * 1000)"
	# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
	$env:DCEMU_JIT = if ($brazo -eq 'jit') { '2' } else { '0' }

	Remove-Item $bmp -EA SilentlyContinue
	# Un solo string con la imagen entre comillas: Start-Process con arreglo
	# parte las rutas con espacios ("sobra el argumento: Rally").
	$p = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$Segundos --sin-vmu --captura-gl=$bmp `"$img`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $p.WaitForExit($TopeMin * 60000)) {
		$p | Stop-Process -Force
		Start-Sleep 2
		Copy-Item $err "logs\chdb-$tag-$brazo.txt" -Force -EA SilentlyContinue
		return @{ ok = $false; razon = 'COLGADO'; bmp = ''; tag = $tag }
	}
	Start-Sleep 1
	Copy-Item $err "logs\chdb-$tag-$brazo.txt" -Force -EA SilentlyContinue
	if (-not (Test-Path $bmp)) { return @{ ok = $false; razon = "sin captura (salida $($p.ExitCode))"; bmp = ''; tag = $tag } }
	return @{ ok = $true; razon = ''; bmp = (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16); tag = $tag }
}

foreach ($g in $guests) {
	$a = Correr $g 'int'
	$b = Correr $g 'jit'
	$tag = $a.tag

	if (-not $a.ok -or -not $b.ok) {
		"{0,-46} int={1} jit={2}" -f $g, ($a.ok ? $a.bmp : $a.razon), ($b.ok ? $b.bmp : $b.razon)
		continue
	}

	$ca = Select-String -Path "logs\chdb-$tag-int.txt" -Pattern '^cp ' | ForEach-Object Line
	$cb = Select-String -Path "logs\chdb-$tag-jit.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($ca.Count, $cb.Count); $prim = -1
	for ($i = 0; $i -lt $n; $i++) { if ($ca[$i] -cne $cb[$i]) { $prim = $i; break } }
	$puntos = if ($prim -ge 0) { "PUNTO $prim" } elseif ($ca.Count -ne $cb.Count) { "largo $($ca.Count)/$($cb.Count)" } else { 'exacto' }

	$jl = (Select-String -Path "logs\chdb-$tag-jit.txt" -Pattern '^jit: \d+ instrucciones' | Select-Object -First 1)
	$porEntrada = if ($jl -and $jl.Line -match '\(([\d.]+) por entrada\)') { $matches[1] } else { '?' }

	"{0,-46} bmp {1} puntos {2,-12} {3} por entrada" -f $g,
		($(if ($a.bmp -eq $b.bmp) { 'identicas' } else { "DISTINTAS $($a.bmp)/$($b.bmp)" })),
		$puntos, $porEntrada
	if ($prim -ge 0) { "   int: $($ca[$prim])"; "   jit: $($cb[$prim])" }
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO -EA SilentlyContinue
"hecho"
