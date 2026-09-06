# hilos-juegos.ps1 -- la red de juegos de la adopcion de --hilos (2026-09-05):
# todo el material comercial disponible, --sin-hilos contra --hilos sobre el
# mismo binario. La receta de juegos-jit.ps1 (captura byte a byte y
# DCEMU_CP_MS punto por punto) mas el arbitro propio de los hilos: la lista de
# entregas de DCEMU_SONDA_ENTREGAS=1, porque el hilo mueve el instante en que
# la linea del AICA llega al ASIC y los puntos por ms no lo ven (CLAUDE.md,
# DCEMU_SONDA_ENTREGAS).
#
# La lista es la de juegos-jit.ps1: las imagenes del banco en roms/ mas los
# CHD de primer disco del directorio que resuelva banco.ps1.
param(
	[string] $Exe = "build-clang\dcemu.exe",
	[int]    $Segundos = 20,
	[int]    $TopeMin = 8
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\banco.ps1"
if (Get-Process dcemu -EA SilentlyContinue) { throw "hay un dcemu corriendo" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))  --sin-hilos contra --hilos, $Segundos s"

$deRoms = @()
foreach ($n in @("doom", "sr2", "ct", "vt")) {
	$p = ImagenBanco $n
	if ($p) { $deRoms += (Resolve-Path -LiteralPath $p).Path }
	else    { Write-Output "sin imagen para $n" }
}
$chds = @()
$chdDir = ChdRaiz
if ($chdDir) {
	$chds = Get-ChildItem -LiteralPath $chdDir -Filter *.chd |
		Where-Object { $_.Name -notmatch "\(Disc [234]\)" } |
		Select-Object -ExpandProperty FullName
}
$imagenes = @($deRoms) + @($chds | Where-Object { $deRoms -notcontains $_ })

function Correr($img, $brazo)
{
	$tag = (([IO.Path]::GetFileNameWithoutExtension($img)) -split " \(")[0] -replace "[^A-Za-z0-9]", ""
	$bmp = "logs\fh-$tag-$brazo.bmp"

	$env:DCEMU_RTC_FIJO = "1000000000"
	$env:DCEMU_CP_MS    = "$($Segundos * 1000)"
	$env:DCEMU_SONDA_ENTREGAS = "1"
	Remove-Item Env:DCEMU_JIT -EA SilentlyContinue
	$bandera = if ($brazo -eq "con") { "--hilos" } else { "--sin-hilos" }

	Remove-Item $bmp -EA SilentlyContinue
	$p = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$Segundos --sin-vmu --sin-audio $bandera --captura-gl=$bmp `"$img`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $p.WaitForExit($TopeMin * 60000)) {
		$p | Stop-Process -Force
		Start-Sleep 2
		Copy-Item $err "logs\fh-$tag-$brazo.txt" -Force -EA SilentlyContinue
		return @{ ok = $false; razon = "COLGADO"; bmp = ""; tag = $tag }
	}
	Start-Sleep 1
	Copy-Item $err "logs\fh-$tag-$brazo.txt" -Force -EA SilentlyContinue
	if (-not (Test-Path $bmp)) { return @{ ok = $false; razon = "sin captura (salida $($p.ExitCode))"; bmp = ""; tag = $tag } }
	return @{ ok = $true; razon = ""; bmp = (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16); tag = $tag }
}

function Lineas($archivo, $patron) {
	return @(Select-String -Path $archivo -Pattern $patron | ForEach-Object Line)
}

$distintas = 0
foreach ($img in $imagenes) {
	$nombre = [IO.Path]::GetFileNameWithoutExtension($img)
	$a = Correr $img "sin"
	$b = Correr $img "con"
	$tag = $a.tag

	if (-not $a.ok -or -not $b.ok) {
		"{0,-52} sin={1} con={2}" -f $nombre, ($a.ok ? $a.bmp : $a.razon), ($b.ok ? $b.bmp : $b.razon)
		$distintas++
		continue
	}

	$ca = Lineas "logs\fh-$tag-sin.txt" "^cp "
	$cb = Lineas "logs\fh-$tag-con.txt" "^cp "
	$n = [Math]::Min($ca.Count, $cb.Count); $prim = -1
	for ($i = 0; $i -lt $n; $i++) { if ($ca[$i] -cne $cb[$i]) { $prim = $i; break } }
	$puntos = if ($prim -ge 0) { "PUNTO $prim" } elseif ($ca.Count -ne $cb.Count) { "largo $($ca.Count)/$($cb.Count)" } else { "exacto ($($ca.Count))" }

	$ea = Lineas "logs\fh-$tag-sin.txt" "^entrega "
	$eb = Lineas "logs\fh-$tag-con.txt" "^entrega "
	$m = [Math]::Min($ea.Count, $eb.Count); $pe = -1
	for ($i = 0; $i -lt $m; $i++) { if ($ea[$i] -cne $eb[$i]) { $pe = $i; break } }
	$entregas = if ($pe -ge 0) { "ENTREGA $pe" } elseif ($ea.Count -ne $eb.Count) { "largo $($ea.Count)/$($eb.Count)" } else { "exactas ($($ea.Count))" }

	# El control de que el brazo "con" corrio con hilo y el "sin" no.
	$hc = [bool] (Select-String -Path "logs\fh-$tag-con.txt" -Pattern "hilo del AICA" -Quiet)
	$hs = [bool] (Select-String -Path "logs\fh-$tag-sin.txt" -Pattern "hilo del AICA" -Quiet)
	$control = if ($hc -and -not $hs) { "" } else { " CONTROL: hilo con=$hc sin=$hs" }

	$igual = ($a.bmp -eq $b.bmp) -and ($prim -lt 0) -and ($ca.Count -eq $cb.Count) -and ($pe -lt 0) -and ($ea.Count -eq $eb.Count) -and -not $control
	if (-not $igual) { $distintas++ }
	"{0,-52} bmp {1} puntos {2,-14} entregas {3}{4}" -f $nombre,
		($(if ($a.bmp -eq $b.bmp) { "identicas" } else { "DISTINTAS $($a.bmp)/$($b.bmp)" })),
		$puntos, $entregas, $control
	if ($prim -ge 0) { "   sin: $($ca[$prim])"; "   con: $($cb[$prim])" }
	if ($pe -ge 0)   { "   sin: $($ea[$pe])";   "   con: $($eb[$pe])" }
}

Remove-Item env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO,env:DCEMU_SONDA_ENTREGAS -EA SilentlyContinue
"hecho: $($imagenes.Count) imagenes, $distintas con diferencia"
