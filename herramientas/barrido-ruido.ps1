# El piso de ruido del barrido: la MISMA rama, dos veces.
#
# Existe porque la primera lectura del barrido del medio texel salio ilegible:
# 40 demos distintas contra la linea base, y la primera que se miro --`hello`,
# que es determinista-- resulto no estar mostrando la demo sino **el menu del
# BIOS**, con la fecha del reloj del anfitrion. Los demos de consola imprimen
# al serial, terminan, y KOS devuelve el control al boot ROM: a los 8 segundos
# emulados lo que quedo en pantalla es el menu, y el reloj persiste en bios/.
#
# A eso se suman los demos con hilos (carreras) y los de volumen modificador
# (colocan geometria con rand(), documentado en CLAUDE.md).
#
# Asi que antes de leer una diferencia hay que saber cuales se mueven solas.
# Este barrido corre la rama `sin` otra vez, con los mismos ajustes, y lo que
# difiera entre las dos corridas es ruido por construccion.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"

$env:DCEMU_SIN_MEDIO_TEXEL = "1"

& "$PSScriptRoot\barrido.ps1" -Salida "barrido-mt-sin2" -Demos "." `
	-Vmu "vmu-barrido.bin" -Exe ".\dcemu.exe"

Remove-Item env:DCEMU_SIN_MEDIO_TEXEL -EA SilentlyContinue

Write-Output "=== el piso de ruido: la misma rama dos veces"
& "$PSScriptRoot\comparar.ps1" "barrido-mt-sin" "barrido-mt-sin2"

Write-Output "=== fin"
