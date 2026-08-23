# sonido-cota.ps1 -- la cota del subsistema de sonido HOY: --sin-aica (ni
# ARM, ni canales, ni DSP, ni temporizadores) contra la omision, sobre el
# binario canonico. NO es un A/B de mecanismo: sin AICA el guest puede tomar
# otro camino (sondea su musica y nadie contesta), asi que cada fila lleva
# las instrucciones del traductor -- si el trabajo del guest diverge fuerte,
# la cota no vale para ese guest y la tabla lo muestra en vez de esconderlo.
# Lo que compra: saber cuanto queda EN TOTAL en la via ARM7+mezclador, que
# decide si el proximo escalon (emision de la cola, mezclador) puede pagar.
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

	$args = @("--salir-tras=$segundos", "--sin-vmu")
	if ($brazo -eq "sinaica") { $args += "--sin-aica" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe @args $img | Out-Null
	$reloj.Stop()

	$resumen = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-8} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("normal", "sinaica"),
	@("sinaica", "normal"),
	@("normal", "sinaica")
)

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "normal" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
