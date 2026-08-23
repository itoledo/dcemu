# La tanda de la corte del programa DSP (aicadsp.c): DOS brazos sobre el mismo
# binario -- con (por omision) y sin (DCEMU_SIN_DSP_CORTE=1, los 128 pasos de siempre) -- bajo el traductor, que es la forma que la adopcion entrega.
# Calentamiento POR GUEST descartado, orden alternado entre rondas.
#
# ciclo de PGO primero (GEN -> pgo.ps1 -Clang -> USE): arm7.c cambio, el
# perfil tiene que corresponder al codigo o la tanda mide la disposicion.
param([string] $Exe = "build-clang\dcemu.exe")

$ErrorActionPreference = "Stop"
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($brazo, $img, $segundos, $teclas)
{
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$env:DCEMU_JIT = "2"
	Remove-Item env:DCEMU_SIN_DSP_CORTE -EA SilentlyContinue
	if ($brazo -eq "sin") { $env:DCEMU_SIN_DSP_CORTE = "1" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$resumen = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-5} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("con", "sin"),
	@("sin", "con"),
	@("con", "sin"),
	@("sin", "con")
)

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "con" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT,env:DCEMU_SIN_DSP_CORTE,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"


