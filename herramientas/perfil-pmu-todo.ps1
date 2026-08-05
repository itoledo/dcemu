# Los tres bancos, uno detras de otro, en una sola sesion elevada.
# Ver perfil-pmu.ps1 por que mide cada modo y por que.
#
#   1. Abrir PowerShell **como administrador**
#   2. cd D:\dev\dcemu
#   3. herramientas\perfil-pmu-todo.ps1
#
# Deja nueve CSV en la raiz (perfil-<banco>-<modo>.csv) y una bitacora en
# perfil-pmu.log. Del orden de media hora: DCDoom va primero porque es el mas
# corto y el que mas puede ensenar.
#
# Mientras corre **no toques el gamepad**: XInput se lee sin foco de ventana y
# las pulsaciones entran en la corrida (ver CLAUDE.md, "Measurement discipline").

param(
	[string[]] $Bancos = @("dcdoom", "crazytaxi", "vtennis"),
	[string[]] $Modos  = @("tiempo", "cuentas", "fallos"),
	[string]   $Log    = "perfil-pmu.log"
)

"arranque: $(Get-Date -Format o)" | Out-File $Log -Encoding utf8

foreach ($b in $Bancos) {
	Write-Host ""
	Write-Host "############ $b ############"
	"=== $b : $(Get-Date -Format o) ===" | Out-File $Log -Append -Encoding utf8

	try {
		& "$PSScriptRoot\perfil-pmu.ps1" -Banco $b -Modos $Modos 2>&1 |
			Tee-Object -FilePath $Log -Append
	} catch {
		# Un banco que falla no tiene por que llevarse los otros dos: media hora
		# de corrida es cara y los CSV que ya salieron sirven igual.
		Write-Host "ERROR en ${b}: $_"
		"ERROR en ${b}: $_" | Out-File $Log -Append -Encoding utf8
	}
}

"fin: $(Get-Date -Format o)" | Out-File $Log -Append -Encoding utf8

Write-Host ""
Write-Host "listo. Los CSV:"
Get-ChildItem "perfil-*.csv" -EA SilentlyContinue |
	Select-Object Name, @{n="KB";e={[int]($_.Length/1KB)}} | Format-Table
