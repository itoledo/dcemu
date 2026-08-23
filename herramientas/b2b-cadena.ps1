# La cadena de verificacion del lote B.2b: reentrenar (ciclo-jit), la tanda de
# cuatro brazos (b2b-ab) y la regresion del trio CHD. Un proceso desacoplado
# con centinela. Una cadena por maquina.
$ErrorActionPreference = "Continue"
Set-Location D:\dev\dcemu

Remove-Item logs\b2b-cadena-fin.txt -EA SilentlyContinue

herramientas\ciclo-jit.ps1 2>&1 | Out-File logs\b2b-ciclo.log
$ciclo = $LASTEXITCODE

if ($ciclo -eq 0 -or $null -eq $ciclo) {
	herramientas\b2b-ab.ps1 2>&1 | Out-File logs\b2b-tanda.log
	herramientas\chd-exactitud.ps1 2>&1 | Out-File logs\b2b-chd.log
}

"fin ciclo=$ciclo" | Out-File logs\b2b-cadena-fin.txt
