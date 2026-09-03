# La tanda de la elision de lazos ociosos: tres brazos sobre el mismo binario.
#
#   entera  = la omision (bumps + sondas)
#   bumps   = DCEMU_JIT_OCIOSOS=1 (solo la generacion de impureza: su costo)
#   apagada = DCEMU_JIT_OCIOSOS=0 (la emision anterior byte por byte)
#
# Tres brazos y no dos porque la palanca son dos mecanismos: un combinado
# neutro puede ser el ahorro de las sondas tapando el costo de los bumps, y
# sin el brazo del medio no hay forma de saberlo (la leccion del pliegue de
# guardas y la rejilla). El cliente es Crazy Taxi (su lazo de espera es el
# 47 % de sus instrucciones); DCDoom y Sega Rally 2 van de control y por
# construccion deberian salir inertes (bajo MMU no se emiten bumps ni se
# instalan sondas). Los guests cuya imagen no este en roms\ se saltean.
#
# TRAMPA: para borrar una variable hay que usar Remove-Item "Env:NOMBRE". En
# PowerShell 7, [Environment]::SetEnvironmentVariable($v, $null) NO la borra --
# la deja VACIA-- y getenv() devuelve "" en vez de NULL. Para DCEMU_JIT eso
# significa atoi("")==0, o sea APAGAR el traductor.
param([string] $Exe = "build-jit\dcemu.exe", [string] $Guest = "", [int] $Rondas = 4)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
$err = Join-Path (Split-Path $Exe) "stderr.txt"

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($brazo, $img, $segundos, $teclas)
{
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_JIT_OCIOSOS","DCEMU_MANDO")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	}
	$env:DCEMU_JIT = "2"
	if ($brazo -eq "bumps")   { $env:DCEMU_JIT_OCIOSOS = "1" }
	if ($brazo -eq "apagada") { $env:DCEMU_JIT_OCIOSOS = "0" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$cuenta = (Select-String -Path $err -Pattern "instrucciones en .* entradas" -EA SilentlyContinue |
		ForEach-Object { $_.Line -replace '^jit: ','' }) -join " | "
	$elision = (Select-String -Path $err -Pattern "elision de ociosos" -EA SilentlyContinue |
		ForEach-Object { ($_.Line -replace '^jit: elision de ociosos ','') -replace ': generacion.*?, ','; ' }) -join ""
	$elision = $elision -replace '\(.*?sin lugar\), ',''

	"{0,-8} {1,8} ms   {2}   [{3}]" -f $brazo, $reloj.ElapsedMilliseconds, $cuenta, $elision
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)
if ($Guest) { $bancos = $bancos | Where-Object { $_.n -like "*$Guest*" } }
if (-not $bancos) { throw "guest desconocido: $Guest" }

# El orden rota entre rondas para que ningun brazo vaya siempre primero.
$ordenes = @(@("entera","bumps","apagada"), @("apagada","entera","bumps"),
             @("bumps","apagada","entera"), @("entera","apagada","bumps"),
             @("bumps","entera","apagada"), @("apagada","bumps","entera"))

foreach ($b in $bancos) {
	if (-not (Test-Path $b.img)) { Write-Output "=== $($b.n) SALTEADO: no esta la imagen"; continue }
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "entera" $b.img $b.s $b.teclas | Out-Null
	for ($r = 0; $r -lt $Rondas; $r++) {
		foreach ($m in $ordenes[$r % $ordenes.Count]) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

foreach ($v in @("DCEMU_JIT","DCEMU_JIT_OCIOSOS","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MANDO")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
