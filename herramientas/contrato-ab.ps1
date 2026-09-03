# La tanda del contrato por instruccion: CUATRO brazos sobre el mismo binario,
# para aislar las dos palancas por separado (la regla del arbol: un A/B de dos
# mecanismos necesita mas de dos brazos, o un combinado neutro puede ser dos
# efectos que se cancelan).
#
#   base   = las dos viejas   (DCEMU_JIT_SYNC_PREVIA=1 + DCEMU_JIT_CORTE_VIEJO=1)
#   sync   = solo la sincronizacion en el talon
#   corte  = solo el corte en una comparacion
#   ambas  = la omision del arbol
#
# Calentamiento POR GUEST descartado, orden rotado entre rondas. Ciclo de PGO
# primero (GEN -> pgo.ps1 -Clang -> USE): la emision cambio, y un brazo sin
# perfil contra otro con perfil mide el perfil.
# TRAMPA: para borrar una variable hay que usar Remove-Item "Env:NOMBRE". En
# PowerShell 7, [Environment]::SetEnvironmentVariable($v, $null) NO la borra --
# la deja VACIA-- y getenv() devuelve "" en vez de NULL. Para DCEMU_JIT eso
# significa atoi("")==0, o sea APAGAR el traductor: la compuerta compara
# interprete contra interprete, sale verde y no prueba nada. Es la misma trampa
# que la adopcion ya cobro una vez, con otra cara.
param([string] $Exe = "build-clang\dcemu.exe", [string] $Guest = "")

$ErrorActionPreference = "Stop"
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($brazo, $img, $segundos, $teclas)
{
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A",
	                 "DCEMU_JIT_SYNC_PREVIA","DCEMU_JIT_CORTE_VIEJO")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	}

	$env:DCEMU_JIT = "2"
	if ($brazo -eq "base" -or $brazo -eq "corte") { $env:DCEMU_JIT_SYNC_PREVIA = "1" }
	if ($brazo -eq "base" -or $brazo -eq "sync")  { $env:DCEMU_JIT_CORTE_VIEJO = "1" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$resumen = (Select-String -Path $err -Pattern "instrucciones en .* entradas" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-6} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("base","sync","corte","ambas"),
	@("ambas","corte","sync","base"),
	@("sync","base","ambas","corte"),
	@("corte","ambas","base","sync")
)

if ($Guest) { $bancos = $bancos | Where-Object { $_.n -like "*$Guest*" } }
if (-not $bancos) { throw "guest desconocido: $Guest" }

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "ambas" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

foreach ($v in @("DCEMU_JIT","DCEMU_JIT_SYNC_PREVIA","DCEMU_JIT_CORTE_VIEJO",
                 "DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
