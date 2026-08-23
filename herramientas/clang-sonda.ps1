# clang-sonda.ps1 -- sonda de exactitud rapida del binario clang (fase G).
#
# DCDoom 20 s emulados (arbitro inmune al pad), clang contra el MSVC de
# control: captura, puntos de control (DCEMU_CP_MS) y totales del jit. No es
# la compuerta completa -- esa corre sobre el binario final con PGO -- sino el
# filtro barato antes de pagar el entrenamiento.
param(
	[string] $Clang = "build-clang\dcemu.exe",
	[string] $Msvc  = "build-jit\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"

if (Get-Process dcemu -ErrorAction SilentlyContinue) { throw "hay un dcemu corriendo" }

foreach ($e in @($Clang, $Msvc)) {
	$h = (Get-FileHash $e -Algorithm SHA256).Hash.Substring(0, 16)
	Write-Host "$e  $h"
}

$img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"
$env:DCEMU_JIT = "2"
$env:DCEMU_CP_MS = "1000"

& $Clang --salir-tras=20 --sin-vmu --captura-gl=logs\clang-doom.bmp $img | Out-Null
Copy-Item (Join-Path (Split-Path $Clang) "stderr.txt") logs\clang-doom-err.txt

& $Msvc --salir-tras=20 --sin-vmu --captura-gl=logs\msvc-doom.bmp $img | Out-Null
Copy-Item (Join-Path (Split-Path $Msvc) "stderr.txt") logs\msvc-doom-err.txt

Remove-Item env:DCEMU_JIT, env:DCEMU_CP_MS

$hc = (Get-FileHash logs\clang-doom.bmp).Hash.Substring(0, 16)
$hm = (Get-FileHash logs\msvc-doom.bmp).Hash.Substring(0, 16)
Write-Host "bmp   clang=$hc msvc=$hm  $(if ($hc -eq $hm) { 'IGUALES' } else { 'DISTINTOS' })"

$cc = Select-String logs\clang-doom-err.txt -Pattern "^cp " | ForEach-Object Line
$cm = Select-String logs\msvc-doom-err.txt -Pattern "^cp " | ForEach-Object Line
Write-Host "cp    clang=$($cc.Count) msvc=$($cm.Count)  $(if (($cc -join '|') -eq ($cm -join '|')) { 'IDENTICOS' } else { 'DIVERGEN' })"

$jc = (Select-String logs\clang-doom-err.txt -Pattern "^jit: \d+ instrucciones" | ForEach-Object Line)
$jm = (Select-String logs\msvc-doom-err.txt -Pattern "^jit: \d+ instrucciones" | ForEach-Object Line)
Write-Host "clang: $jc"
Write-Host "msvc:  $jm"
