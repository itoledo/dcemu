# El A/B de la memoizacion del ARM7 BAJO los bloques con cola/encadenado
# (2026-08-20): el censo de rechazos mostro que el 44,7 % de los pasos de CT
# se rechaza por "grabando" -- el memo graba el barrido de canales que la
# muestra siguiente invalida, y mientras graba los bloques estan apagados.
# Dos brazos sobre el mismo binario: memo encendida (por omision) y
# DCEMU_SIN_MEMO_ARM=1. CT y SR2 (DOOM no elide nada, documentado).
$ErrorActionPreference = "Stop"
$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($brazo, $img, $segundos, $teclas)
{
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$env:DCEMU_JIT = "2"
	if ($brazo -eq "sin-memo") { $env:DCEMU_SIN_MEMO_ARM = "1" }
	else { Remove-Item env:DCEMU_SIN_MEMO_ARM -EA SilentlyContinue }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$resumen = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-9} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("con-memo", "sin-memo"),
	@("sin-memo", "con-memo"),
	@("con-memo", "sin-memo")
)

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "con-memo" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT,env:DCEMU_SIN_MEMO_ARM,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
