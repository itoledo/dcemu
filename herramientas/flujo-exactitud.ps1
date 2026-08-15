# La compuerta de exactitud del superbloque por flujo, antes de cronometrarlo.
#
# DCEMU_JIT_FLUJO=1 cambia QUE se traduce (la traza sigue BRA/BSR/RTS), asi que
# cambia la cobertura y la cantidad de bloques a proposito. Lo que NO puede
# cambiar es la ejecucion del guest: los puntos de control por ms tienen que
# salir identicos con la palanca y sin ella.
#
# Se corre antes de la tanda porque una diferencia aqui invalida cualquier
# numero de alla.
$ErrorActionPreference = "Stop"

$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

if (-not (Test-Path $exe)) { throw "falta $exe" }

Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; teclas = $false },
	@{ n = "crazytaxi"; img = "roms\Crazy Taxi (USA).cdi"; teclas = $true },
	@{ n = "sr2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; teclas = $false }
)

function Correr($img, $teclas, $flujo, $salida)
{
	$env:DCEMU_FUSION = "0"
	$env:DCEMU_JIT = "2"
	$env:DCEMU_CP_MS = "9000"

	if ($teclas) {
		$env:DCEMU_PULSAR_START = "150,300,450"
		$env:DCEMU_PULSAR_A = "1"
		$env:DCEMU_SOLO_A = "600"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	if ($flujo) { $env:DCEMU_JIT_FLUJO = "1" }
	else { Remove-Item env:DCEMU_JIT_FLUJO -EA SilentlyContinue }

	& $exe "--salir-tras=10" --sin-vmu $img | Out-Null

	Copy-Item $err $salida -Force
}

foreach ($b in $bancos) {
	Correr $b.img $b.teclas $false "logs\flujo-$($b.n)-off.txt"
	Correr $b.img $b.teclas $true  "logs\flujo-$($b.n)-on.txt"

	$off = Select-String -Path "logs\flujo-$($b.n)-off.txt" -Pattern "^cp " |
			ForEach-Object { $_.Line }
	$on  = Select-String -Path "logs\flujo-$($b.n)-on.txt" -Pattern "^cp " |
			ForEach-Object { $_.Line }

	$dif = 0
	$primero = ""
	for ($i = 0; $i -lt [Math]::Min($off.Count, $on.Count); $i++) {
		if ($off[$i] -ne $on[$i]) {
			$dif++
			if ($primero -eq "") { $primero = $off[$i].Substring(0, 40) }
		}
	}

	"{0,-11} {1} puntos sin flujo, {2} con flujo, {3} distintos {4}" -f `
		$b.n, $off.Count, $on.Count, $dif, $primero

	foreach ($f in @("off", "on")) {
		$c = (Select-String -Path "logs\flujo-$($b.n)-$f.txt" -Pattern "^jit: \d+ instrucciones en" |
				ForEach-Object { $_.Line }) -join " "
		"            {0,-4} {1}" -f $f, $c
	}
}

Remove-Item env:DCEMU_JIT_FLUJO,env:DCEMU_CP_MS,env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
