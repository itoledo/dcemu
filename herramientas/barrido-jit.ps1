# barrido-jit.ps1 -- la fase F.1 de jit-sota-plan.md: el parque de demos KOS
# entero bajo DCEMU_JIT=2 contra corrida de control, con el piso de ruido
# medido EL MISMO DIA (la regla del 40-de-139: un barrido es ilegible hasta
# medir su piso, y el piso se mide corriendo el mismo brazo dos veces).
#
# Tres brazos: int-a, int-b (el piso) y jit-a. Los tres con DCEMU_RTC_FIJO
# --que clava a las demos que vuelven al menu del BIOS con el reloj del host
# adentro; se puede porque los tres brazos son de hoy y no se comparan contra
# una linea base vieja-- y con la VMU fresca por demo que ya trae barrido.ps1.
# El residuo esperado del piso: las demos de hilos (carrera) y las de
# volumenes modificadores (rand()).
#
# La senal es (int-a != jit-a) menos el piso, y sobre ella el veredicto
# serial manda: SUCCEEDED/FAIL/panic no pueden regresar aunque la imagen
# cambie.
param(
	[string] $Exe   = "build-jit\Release\dcemu.exe",
	[string] $Demos = "C:\dcsdk\tmp\bins",
	[switch] $SoloLeer
)

$ErrorActionPreference = "Stop"

$brazos = @(
	@{ n = "int-a"; dir = "logs\barrido-fj-int-a"; jit = $false },
	@{ n = "int-b"; dir = "logs\barrido-fj-int-b"; jit = $false },
	@{ n = "jit-a"; dir = "logs\barrido-fj-jit-a"; jit = $true }
)

if (-not $SoloLeer) {
	if (Get-Process dcemu -EA SilentlyContinue) { throw "hay un dcemu corriendo" }
	Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

	$env:DCEMU_RTC_FIJO = "1000000000"

	foreach ($b in $brazos) {
		# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
		$env:DCEMU_JIT = if ($b.jit) { "2" } else { "0" }
		Write-Output "=== brazo $($b.n)"
		& "$PSScriptRoot\barrido.ps1" -Salida $b.dir -Demos $Demos -Exe $Exe -Vmu "logs\barrido-fj-vmu.bin"
	}

	Remove-Item env:DCEMU_JIT, env:DCEMU_RTC_FIJO -EA SilentlyContinue
}

function Hashes($dir)
{
	$h = @{}
	Import-Csv (Join-Path $dir "resumen.csv") | ForEach-Object { $h[$_.demo] = $_.sha256 }
	return $h
}

function Serial($dir, $demo)
{
	$f = Join-Path $dir "$demo.serial.txt"
	if (-not (Test-Path $f)) { return "(sin serial)" }
	(Select-String $f -Pattern "SUCCEEDED|FAIL|panic" | ForEach-Object { $_.Line.Trim() }) -join " / "
}

$ia = Hashes $brazos[0].dir
$ib = Hashes $brazos[1].dir
$ja = Hashes $brazos[2].dir

# EL CONTROL, y esta escrito porque su ausencia costo un barrido entero: si
# ninguna demo dejo captura, los tres brazos coinciden en NADA y el barrido
# sale "piso 0, senal 0" sin haber medido una sola imagen. Paso el 2026-09-04
# en la segunda maquina -- faltaba `ip.bin` en el directorio de trabajo (dcemu
# lo carga junto a todo `.bin` suelto y no esta versionado), las 151 demos
# salieron con rv=1 y cero bytes, y el resumen se leyo como verde. Es la forma
# de falla recurrente del arbol: algo que se acepta sin hacer nada y sin
# decirlo.
$conImagen = @($ia.Keys | Where-Object { $ia[$_] -ne "-" }).Count
Write-Output "demos con captura: $conImagen de $($ia.Count)"
if ($conImagen -eq 0) {
	throw ("ninguna demo dejo captura: el barrido no midio nada. Mira un" +
		" $($brazos[0].dir)\*.stderr.txt -- si dice 'No se pudo abrir ip.bin'," +
		" falta ip.bin en el directorio de trabajo.")
}

$ruido = @($ia.Keys | Where-Object { $ia[$_] -ne $ib[$_] } | Sort-Object)
$camb  = @($ia.Keys | Where-Object { $ia[$_] -ne $ja[$_] } | Sort-Object)
$real  = @($camb | Where-Object { $ruido -notcontains $_ })

Write-Output ""
Write-Output "piso de ruido (int-a vs int-b): $($ruido.Count) de $($ia.Count)"
Write-Output "  $($ruido -join ', ')"
Write-Output "cambian con el jit (int-a vs jit-a): $($camb.Count)"
Write-Output "LA SENAL (cambian y no son ruido): $($real.Count)"

foreach ($d in $real) {
	Write-Output "  $d"
	$si = Serial $brazos[0].dir $d
	$sj = Serial $brazos[2].dir $d
	if ($si -cne $sj) {
		Write-Output "    serial int: $si"
		Write-Output "    serial jit: $sj"
	} else {
		Write-Output "    serial igual: $si"
	}
}

# Y el veredicto serial sobre TODO el parque, no solo la senal de imagen: una
# demo puede salir con el mismo hash (pantalla estatica) y aun asi haber
# cambiado de veredicto.
$regresiones = @()
foreach ($d in ($ia.Keys | Sort-Object)) {
	$si = Serial $brazos[0].dir $d
	$sj = Serial $brazos[2].dir $d
	if ($si -cne $sj) { $regresiones += "$d`n    int: $si`n    jit: $sj" }
}
Write-Output ""
Write-Output "veredictos serial distintos en el parque: $($regresiones.Count)"
$regresiones | ForEach-Object { Write-Output "  $_" }
