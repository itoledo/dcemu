# El A/B del camino de escritura emitido, dentro de UN binario. Son dos
# cambios que tocan la misma secuencia y por eso se miden juntos:
#
#   - DCEMU_JIT_GUARDAS_VIEJAS=1 vuelve a emitir las dos guardas que el censo
#     mostro muertas: el cambio de modo del lado MMU y el break de operando del
#     UBC (que ahora se pliega en las tablas base, como el watchpoint);
#   - DCEMU_JIT_SIN_REJILLA=1 vuelve a desviar al ayudante toda escritura sobre
#     una **pagina** con codigo traducido, en vez de preguntarle en linea a la
#     rejilla de 64 bytes. Son 90 616 485 desvios cada 20 s de DCDoom, y el
#     censo dice que ni uno solo hacia falta.
#
# Con las dos puestas la emision es la anterior; el gancho de la epoca queda
# conectado en los dos brazos, porque eso es correccion y no una opcion. Se
# verifico que los dos brazos ejecutan igual: totales al digito y 9000 puntos
# de control de DCEMU_CP_MS identicos.
#
# Los tres guests entran, y no solo los de MMU: la guarda del UBC se emitia
# tambien del lado plano, asi que Crazy Taxi tambien deja de pagarla -- una de
# dos en vez de dos de dos. Va con las teclas del banco, que son las que lo
# sacan de la pantalla de titulo; DOOM y SR2 no llevan y son los arbitros
# inmunes al jitter del mando.
#
# Reglas de lectura, las de siempre (CLAUDE.md, "Measurement discipline"):
# reentrenar antes (herramientas\ciclo-jit.ps1), descartar la primera corrida
# del binario recien enlazado, alternar el orden dentro de cada ronda, y
# comparar absolutos solo dentro de este binario. El hash se imprime para que
# la tabla no se lea sin el.
$ErrorActionPreference = "Stop"

$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

if (-not (Test-Path $exe)) { throw "falta $exe (ciclo-jit.ps1 primero)" }

Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false; rondas = 3 },
	@{ n = "crazytaxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true; rondas = 2 },
	@{ n = "sr2";    img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false; rondas = 3 }
)

function Correr($img, $segundos, $teclas, $plegadas)
{
	$env:DCEMU_JIT = "2"; $env:DCEMU_FUSION = "0"

	if ($teclas) {
		$env:DCEMU_PULSAR_START = "150,300,450"
		$env:DCEMU_PULSAR_A = "1"
		$env:DCEMU_SOLO_A = "600"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	if ($plegadas) {
		Remove-Item env:DCEMU_JIT_GUARDAS_VIEJAS -EA SilentlyContinue
		Remove-Item env:DCEMU_JIT_SIN_REJILLA -EA SilentlyContinue
	} else {
		$env:DCEMU_JIT_GUARDAS_VIEJAS = "1"
		$env:DCEMU_JIT_SIN_REJILLA = "1"
	}

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	# El control de trabajo por corrida: si la cobertura cambia entre corridas
	# del mismo modo, la tanda no compara.
	$cob = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones en" |
			ForEach-Object { $_.Line }) -join " "

	return @{ ms = $reloj.ElapsedMilliseconds; cob = $cob }
}

Write-Output "=== calentamiento (descartado)"
Correr $bancos[0].img $bancos[0].s $bancos[0].teclas $true | Out-Null

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados"

	for ($r = 1; $r -le $b.rondas; $r++) {
		# El orden se alterna dentro del par: si el primero de cada par paga
		# algo por ser primero, "plegadas siempre primero" convertiria ese
		# costo en una diferencia entre las dos formas.
		$orden = if ($r % 2 -eq 1) { @($true, $false) } else { @($false, $true) }

		foreach ($p in $orden) {
			$x = Correr $b.img $b.s $b.teclas $p
			"{0,-11} {1,8} ms   {2}" -f $(if ($p) { "plegadas" } else { "viejas" }), $x.ms, $x.cob
		}

		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT_GUARDAS_VIEJAS,env:DCEMU_JIT_SIN_REJILLA -EA SilentlyContinue
Remove-Item env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
