# El A/B del atajo de P1/P2 en la traduccion emitida, dentro de UN binario.
#
# La palanca es DCEMU_JIT_SIN_ATAJO_P1P2=1, que reproduce la emision anterior
# byte por byte (verificado por el censo: el camino rapido vuelve a 64,1 % en
# DCDoom). Los dos guests con MMU son los unicos que pueden cambiar -- Crazy
# Taxi no emite traduccion alguna --, y son ademas los arbitros inmunes al
# jitter del mando, asi que la tanda no necesita teclas.
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
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; rondas = 3 },
	@{ n = "sr2";    img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; rondas = 3 }
)

function Correr($img, $segundos, $conAtajo)
{
	$env:DCEMU_JIT = "2"; $env:DCEMU_FUSION = "0"
	Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue

	if ($conAtajo) {
		Remove-Item env:DCEMU_JIT_SIN_ATAJO_P1P2 -EA SilentlyContinue
	} else {
		$env:DCEMU_JIT_SIN_ATAJO_P1P2 = "1"
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
Correr $bancos[0].img $bancos[0].s $true | Out-Null

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados"

	for ($r = 1; $r -le $b.rondas; $r++) {
		# El orden se alterna dentro del par: si el primero de cada par paga
		# algo por ser primero, "con atajo siempre primero" convertiria ese
		# costo en una diferencia entre las dos formas.
		$orden = if ($r % 2 -eq 1) { @($true, $false) } else { @($false, $true) }

		foreach ($a in $orden) {
			$x = Correr $b.img $b.s $a
			"{0,-11} {1,8} ms   {2}" -f $(if ($a) { "con atajo" } else { "sin atajo" }), $x.ms, $x.cob
		}

		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT_SIN_ATAJO_P1P2,env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Write-Output "=== fin"
