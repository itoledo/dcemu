# La cadena de verificacion del lote B.2 (jit-sota-plan.md): reentrenar el
# perfil (ciclo-jit), la tanda de tiempo (ab-jit) y el trio CHD con la emision
# nueva. Un solo proceso, desacoplado, con centinela de fin para el vigia.
# **Una cadena por maquina**: no lanzar nada de dcemu mientras corre.
$ErrorActionPreference = "Continue"
Set-Location D:\dev\dcemu

Remove-Item logs\lote-cadena-fin.txt -EA SilentlyContinue

herramientas\ciclo-jit.ps1 2>&1 | Out-File logs\lote-ciclo.log
$ciclo = $LASTEXITCODE

if ($ciclo -eq 0 -or $null -eq $ciclo) {
	herramientas\ab-jit.ps1 2>&1 | Out-File logs\lote-tanda.log
	herramientas\chd-exactitud.ps1 2>&1 | Out-File logs\lote-chd.log
}

"fin ciclo=$ciclo" | Out-File logs\lote-cadena-fin.txt
