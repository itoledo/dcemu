# juegos-jit.ps1 -- la mitad de juegos de la fase F.1 (jit-sota-plan.md): todo
# el material comercial disponible, interprete contra DCEMU_JIT=2 sobre el
# mismo binario -- captura byte a byte + DCEMU_CP_MS punto por punto, la
# receta de chd-barrido.ps1.
#
# La lista es: las imagenes del banco en roms/ mas TODOS los CHD de primer
# disco del directorio que resuelva banco.ps1 (E:\Juegos\roms\dreamcast en una
# maquina, roms\chd en la otra). Los discos 2+ de los juegos multi-disco quedan
# fuera a proposito y se dice aca: arrancan pidiendo el disco 1 y ese camino ya
# lo cubre el disco 1 (tope explicito, no silencioso).
#
# El brazo int corre primero y paga el primer barrido en frio del CHD (la
# leccion de Rez); no importa: aqui no se cronometra nada.
param(
	[string] $Exe = "",
	[int]    $Segundos = 20,
	[int]    $TopeMin = 8
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\banco.ps1"
if (-not $Exe) { $Exe = ExeBanco "build-jit" }
if (Get-Process dcemu -EA SilentlyContinue) { throw "hay un dcemu corriendo" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$deRoms = @()
foreach ($n in @("doom", "sr2", "ct", "vt")) {
	$p = ImagenBanco $n
	if ($p) { $deRoms += (Resolve-Path -LiteralPath $p).Path }
	else    { Write-Output "sin imagen para $n" }
}

# Los CHD: los que haya, y ninguno si no hay directorio. Un .chd que ya entro
# por el banco --SR2 lo es en la segunda maquina-- no se corre dos veces.
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
	$bmp = "logs\fj-$tag-$brazo.bmp"

	$env:DCEMU_RTC_FIJO = "1000000000"
	$env:DCEMU_CP_MS    = "$($Segundos * 1000)"
	# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
	$env:DCEMU_JIT = if ($brazo -eq "jit") { "2" } else { "0" }

	Remove-Item $bmp -EA SilentlyContinue
	$p = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$Segundos --sin-vmu --captura-gl=$bmp `"$img`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $p.WaitForExit($TopeMin * 60000)) {
		$p | Stop-Process -Force
		Start-Sleep 2
		Copy-Item $err "logs\fj-$tag-$brazo.txt" -Force -EA SilentlyContinue
		return @{ ok = $false; razon = "COLGADO"; bmp = ""; tag = $tag }
	}
	Start-Sleep 1
	Copy-Item $err "logs\fj-$tag-$brazo.txt" -Force -EA SilentlyContinue
	if (-not (Test-Path $bmp)) { return @{ ok = $false; razon = "sin captura (salida $($p.ExitCode))"; bmp = ""; tag = $tag } }
	return @{ ok = $true; razon = ""; bmp = (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16); tag = $tag }
}

foreach ($img in $imagenes) {
	$nombre = [IO.Path]::GetFileNameWithoutExtension($img)
	$a = Correr $img "int"
	$b = Correr $img "jit"
	$tag = $a.tag

	if (-not $a.ok -or -not $b.ok) {
		"{0,-52} int={1} jit={2}" -f $nombre, ($a.ok ? $a.bmp : $a.razon), ($b.ok ? $b.bmp : $b.razon)
		continue
	}

	$ca = Select-String -Path "logs\fj-$tag-int.txt" -Pattern "^cp " | ForEach-Object Line
	$cb = Select-String -Path "logs\fj-$tag-jit.txt" -Pattern "^cp " | ForEach-Object Line
	$n = [Math]::Min($ca.Count, $cb.Count); $prim = -1
	for ($i = 0; $i -lt $n; $i++) { if ($ca[$i] -cne $cb[$i]) { $prim = $i; break } }
	$puntos = if ($prim -ge 0) { "PUNTO $prim" } elseif ($ca.Count -ne $cb.Count) { "largo $($ca.Count)/$($cb.Count)" } else { "exacto" }

	$jl = (Select-String -Path "logs\fj-$tag-jit.txt" -Pattern "^jit: \d+ instrucciones" | Select-Object -First 1)
	$porEntrada = if ($jl -and $jl.Line -match "\(([\d.]+) por entrada\)") { $matches[1] } else { "?" }

	"{0,-52} bmp {1} puntos {2,-12} {3} por entrada" -f $nombre,
		($(if ($a.bmp -eq $b.bmp) { "identicas" } else { "DISTINTAS $($a.bmp)/$($b.bmp)" })),
		$puntos, $porEntrada
	if ($prim -ge 0) { "   int: $($ca[$prim])"; "   jit: $($cb[$prim])" }
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO -EA SilentlyContinue
"hecho"
