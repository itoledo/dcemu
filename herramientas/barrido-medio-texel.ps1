# El barrido del parque tras la correccion del medio texel, en DOS brazos.
#
# La correccion (commit 770d052) mueve TODAS las capturas del arbol, asi que un
# barrido contra la linea base anterior sale distinto en casi todo y no dice
# nada por si solo. Por eso van dos brazos dentro del mismo binario:
#
#   sin    por omision -- la convencion de siempre, y la vigente otra vez
#          desde que el medio texel se apago (ver CLAUDE.md). Es el brazo que
#          se compara contra `barrido-reloj-on` (10 de agosto): si sale
#          identico, entonces desde entonces NADA mas movio el render, y la
#          diferencia del otro brazo es exactamente la correccion.
#   con    DCEMU_MEDIO_TEXEL=1 -- la correccion.
#
# OJO: cuando este barrido se corrio, el medio texel venia ENCENDIDO por
# omision y los dos brazos estaban al reves (la palanca se llamaba
# DCEMU_SIN_MEDIO_TEXEL). Las capturas guardadas no cambian de contenido; lo
# que cambio es cual de los dos es hoy el comportamiento por omision.
#
# El barrido se corre desde build\Release, que es donde viven los .bin y donde
# dcemu resuelve bios/, font.png y logs/ por ruta relativa. `vmu-barrido.bin`
# es la tarjeta del barrido (se borra antes de cada demo) y aparece como una
# "demo" mas sin captura, igual que en las corridas anteriores.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"

foreach ($brazo in @("sin", "con")) {
	if ($brazo -eq "con") { $env:DCEMU_MEDIO_TEXEL = "1" }
	else { Remove-Item env:DCEMU_MEDIO_TEXEL -EA SilentlyContinue }

	Write-Output "=== brazo $brazo"

	& "$PSScriptRoot\barrido.ps1" -Salida "barrido-mt-$brazo" -Demos "." `
		-Vmu "vmu-barrido.bin" -Exe ".\dcemu.exe"
}

Remove-Item env:DCEMU_MEDIO_TEXEL -EA SilentlyContinue

Write-Output "=== sin medio texel contra la linea base del 10 de agosto"
& "$PSScriptRoot\comparar.ps1" "barrido-reloj-on" "barrido-mt-sin"

Write-Output "=== y lo que movio la correccion"
& "$PSScriptRoot\comparar.ps1" "barrido-mt-sin" "barrido-mt-con"

Write-Output "=== fin"
