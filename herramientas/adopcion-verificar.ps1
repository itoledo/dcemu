# adopcion-verificar.ps1 -- las tres caras de la palanca tras la F.2: sin
# variable corre el traductor, DCEMU_JIT=0 deja al interprete solo, y la
# captura por omision es identica a la del brazo jit de la compuerta.
param([string] $Exe = "build-clang\dcemu.exe")

$ErrorActionPreference = "Stop"
if (Get-Process dcemu -EA SilentlyContinue) { throw "hay un dcemu corriendo" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Remove-Item env:DCEMU_JIT -EA SilentlyContinue
$env:DCEMU_RTC_FIJO = "1000000000"; $env:DCEMU_CP_MS = "20000"

& $Exe --salir-tras=20 --sin-vmu --captura-gl=logs\adop-doom.bmp "roms\DCDoom GDI and CDI\DCDoom CDI.cdi" | Out-Null
Copy-Item $err logs\adop-doom.txt -Force
$j = (Select-String logs\adop-doom.txt -Pattern "^jit: traductor" | Measure-Object).Count
Write-Output "sin variable -> traductor: $(if ($j) { 'SI' } else { 'NO' })"

$env:DCEMU_JIT = "0"
& $Exe --salir-tras=5 --sin-vmu "roms\DCDoom GDI and CDI\DCDoom CDI.cdi" | Out-Null
$j0 = (Select-String $err -Pattern "^jit: " | Measure-Object).Count
Write-Output "con DCEMU_JIT=0 -> lineas jit: $j0 (0 = interprete solo)"
Remove-Item env:DCEMU_JIT

$h = (Get-FileHash logs\adop-doom.bmp).Hash.Substring(0, 16)
$href = (Get-FileHash logs\fg-doom-clang-jit.bmp).Hash.Substring(0, 16)
Write-Output "bmp por omision=$h referencia jit=$href $(if ($h -eq $href) { 'IGUALES' } else { 'DISTINTAS' })"

$ca = Select-String logs\adop-doom.txt -Pattern "^cp " | ForEach-Object Line
$cb = Select-String logs\fg-doom-clang-jit.txt -Pattern "^cp " | ForEach-Object Line
Write-Output "cp: $($ca.Count)/$($cb.Count) $(if (($ca -join '|') -eq ($cb -join '|')) { 'IDENTICOS' } else { 'DIVERGEN' })"

Remove-Item env:DCEMU_RTC_FIJO, env:DCEMU_CP_MS -EA SilentlyContinue
