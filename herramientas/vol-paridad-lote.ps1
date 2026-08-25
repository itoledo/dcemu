# El lote de amplitud de la paridad de volumenes: el cinturon de juegos y el
# parque de KOS.
#
# Dos brazos sobre UN binario (con = paridad por omision; sin =
# DCEMU_SIN_VOL_PARIDAD=1, la cuenta con signo anterior), mas la repeticion
# del brazo `con` -- el determinismo en los juegos, el piso de ruido en el
# parque (la regla del 40-de-139: un barrido es ilegible sin su piso del dia).
#
# El cinturon es el de juegos-jit.ps1: lo que queda en roms/ mas TODOS los
# CHD de primer disco de E:\Juegos\roms\dreamcast (los discos 2+ arrancan
# pidiendo el disco 1; tope explicito, no silencioso).
#
# La lectura esperada: un juego solo puede moverse si su resumen registra
# cierres de volumen (la linea `censo de volumenes` de --traza-mem), y de
# esos, solo donde el devanado mixto quede a la vista en el cuadro final;
# una demo del parque no deberia moverse salvo pvr-modifier_volume_zclip,
# cuyo cubo tambien viene devanado mixto (la muesca, ver A.16).
param(
	[string] $Exe      = "build-clang\dcemu.exe",
	[string] $Demos    = "C:\dcsdk\tmp\bins",
	[int]    $Segundos = 20,
	[int]    $TopeMin  = 8,
	[switch] $SinParque,
	[switch] $SinJuegos
)

$ErrorActionPreference = "Stop"
Set-Location "$PSScriptRoot\.."
if (-not (Test-Path $Exe)) { throw "falta $Exe" }

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"binario: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"
$err = Join-Path (Split-Path $Exe) "stderr.txt"

$deRoms = @(
	"roms\DCDoom GDI and CDI\DCDoom CDI.cdi",
	"roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi",
	"roms\Crazy Taxi (USA).cdi",
	"roms\Virtua Tennis (2000)(Sega)(US)[cr DCRES][f PAL 60Hz][repack].cdi"
)

$chds = Get-ChildItem "E:\Juegos\roms\dreamcast" -Filter *.chd |
	Where-Object { $_.Name -notmatch "\(Disc [234]\)" } |
	Select-Object -ExpandProperty FullName

$imagenes = $deRoms + $chds

function Correr($img, $brazo, $tag)
{
	$bmp = "logs\vpj-$tag-$brazo.bmp"

	$env:DCEMU_RTC_FIJO = "1000000000"
	Remove-Item env:DCEMU_SIN_VOL_PARIDAD -EA SilentlyContinue
	if ($brazo -like "sin*") { $env:DCEMU_SIN_VOL_PARIDAD = "1" }

	Remove-Item $bmp -EA SilentlyContinue
	$p = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$Segundos --sin-vmu --traza-mem --captura-gl=$bmp `"$img`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $p.WaitForExit($TopeMin * 60000)) {
		$p | Stop-Process -Force
		Start-Sleep 2
		return @{ ok = $false; h = "COLGADO"; censo = "" }
	}
	Start-Sleep 1

	$h = if (Test-Path $bmp) { (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16) } else { "sin captura (salida $($p.ExitCode))" }
	$censo = (Select-String -Path $err -Pattern "censo de volumenes" -EA SilentlyContinue | Select-Object -Last 1).Line
	if ($null -eq $censo) { $censo = "" }

	return @{ ok = (Test-Path $bmp); h = $h; censo = $censo.Trim() }
}

if (-not $SinJuegos) {
	"=== cinturon (con/con-2/sin; un juego solo puede moverse si cierra volumenes)"
	foreach ($img in $imagenes) {
		$nombre = [IO.Path]::GetFileNameWithoutExtension($img)
		$tag = (($nombre) -split " \(")[0] -replace "[^A-Za-z0-9]", ""

		$a1 = Correr $img "con" $tag
		$a2 = Correr $img "con2" $tag
		$b1 = Correr $img "sin" $tag

		$det = if ($a1.h -eq $a2.h) { "determinista" } else { "NO DETERMINISTA" }
		$mov = if ($a1.h -eq $b1.h) { "sin cambio" } else { "MOVIDA" }

		"{0,-52} con={1} sin={2}  {3,-16} {4}" -f $nombre, $a1.h, $b1.h, $det, $mov
		if ($a1.censo -ne "") { "    $($a1.censo)" }
	}
	Remove-Item env:DCEMU_SIN_VOL_PARIDAD -EA SilentlyContinue
}

if ($SinParque) { "=== fin (sin parque)"; exit 0 }

"=== parque: tres brazos (con-a, con-b = piso, sin-a)"
$env:DCEMU_RTC_FIJO = "1000000000"

$brazos = @(
	@{ n = "con-a"; dir = "logs\barrido-vp-con-a"; palanca = $false },
	@{ n = "con-b"; dir = "logs\barrido-vp-con-b"; palanca = $false },
	@{ n = "sin-a"; dir = "logs\barrido-vp-sin-a"; palanca = $true }
)

foreach ($b in $brazos) {
	Remove-Item env:DCEMU_SIN_VOL_PARIDAD -EA SilentlyContinue
	if ($b.palanca) { $env:DCEMU_SIN_VOL_PARIDAD = "1" }
	"=== brazo $($b.n)"
	& "$PSScriptRoot\barrido.ps1" -Salida $b.dir -Demos $Demos -Exe $Exe -Vmu "logs\barrido-vp-vmu.bin"
}

Remove-Item env:DCEMU_SIN_VOL_PARIDAD, env:DCEMU_RTC_FIJO -EA SilentlyContinue

function Hashes($dir)
{
	$h = @{}
	Import-Csv (Join-Path $dir "resumen.csv") | ForEach-Object { $h[$_.demo] = $_.sha256 }
	return $h
}

$ca = Hashes $brazos[0].dir
$cb = Hashes $brazos[1].dir
$sa = Hashes $brazos[2].dir

$ruido = @($ca.Keys | Where-Object { $ca[$_] -ne $cb[$_] } | Sort-Object)
$camb  = @($ca.Keys | Where-Object { $ca[$_] -ne $sa[$_] } | Sort-Object)
$real  = @($camb | Where-Object { $ruido -notcontains $_ })

""
"piso de ruido (con-a vs con-b): $($ruido.Count) de $($ca.Count)"
if ($ruido.Count) { "  $($ruido -join ', ')" }
"cambian con la paridad (con-a vs sin-a): $($camb.Count)"
"LA SENAL (cambian y no son ruido): $($real.Count)"
$real | ForEach-Object { "  $_" }
"=== fin"
