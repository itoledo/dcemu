# La tanda del superbloque por flujo, en TRES brazos y dentro de un binario.
#
#   interprete   DCEMU_JIT=0, la referencia de la marca del arbol
#   traductor    por omision (sin seguir el flujo)
#   flujo        DCEMU_JIT_FLUJO=1, seguir BRA/BSR/RTS al descubrir
#
# Por que se rehace un veredicto ya archivado como neutro: cuando se midio, el
# enlazado era CUADRATICO en la cantidad de bloques traducidos, y el flujo
# cambia esa cantidad -- o sea que el brazo con flujo pagaba o cobraba un costo
# que no era suyo. Y desde entonces la verificacion por entrada dejo de
# comparar palabra por palabra en el caso normal, que es justo lo que mas caro
# le sale a una traza larga y no contigua. Dos motivos independientes para que
# el numero de antes ya no aplique.
#
# El brazo del interprete va en la misma tanda porque este binario todavia
# arrastra las marcas del escalon anterior (la verificacion se midio sobre el
# binario entrenado para el indice de enlaces): la tanda las refresca sin pagar
# dos reentrenamientos.
#
# Reglas de siempre (CLAUDE.md, "Measurement discipline"): reentrenar antes
# (herramientas\ciclo-jit.ps1), descartar la primera corrida de cada guest,
# rotar el orden dentro de cada ronda, y comparar absolutos solo dentro de este
# binario.
$ErrorActionPreference = "Stop"

$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

if (-not (Test-Path $exe)) { throw "falta $exe (ciclo-jit.ps1 primero)" }

Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false; rondas = 3 },
	@{ n = "crazytaxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true; rondas = 2 },
	@{ n = "sr2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false; rondas = 3 }
)

function Correr($img, $segundos, $teclas, $forma)
{
	$env:DCEMU_FUSION = "0"
	$env:DCEMU_JIT = if ($forma -eq "interprete") { "0" } else { "2" }

	if ($teclas) {
		$env:DCEMU_PULSAR_START = "150,300,450"
		$env:DCEMU_PULSAR_A = "1"
		$env:DCEMU_SOLO_A = "600"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	if ($forma -eq "flujo") {
		$env:DCEMU_JIT_FLUJO = "1"
	} else {
		Remove-Item env:DCEMU_JIT_FLUJO -EA SilentlyContinue
	}

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	# Control de trabajo por corrida. La cobertura CAMBIA entre formas a
	# proposito (el flujo traduce otra cosa), asi que lo que se compara es
	# cada forma consigo misma entre rondas.
	$cob = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones en" |
			ForEach-Object { $_.Line }) -join " "
	$tr = (Select-String -Path $err -Pattern "^jit: (traducir|\d+ bloques traducidos)" |
			ForEach-Object { $_.Line }) -join " | "
	$fl = (Select-String -Path $err -Pattern "^jit: \d+ aristas seguidas" |
			ForEach-Object { $_.Line }) -join " "

	return @{ ms = $reloj.ElapsedMilliseconds; cob = $cob; tr = $tr; fl = $fl }
}

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (primera corrida descartada)"
	Correr $b.img $b.s $b.teclas "traductor" | Out-Null

	for ($r = 1; $r -le $b.rondas; $r++) {
		$orden = switch ($r % 3) {
			1 { @("traductor", "flujo", "interprete") }
			2 { @("flujo", "interprete", "traductor") }
			0 { @("interprete", "traductor", "flujo") }
		}

		foreach ($f in $orden) {
			$x = Correr $b.img $b.s $b.teclas $f
			"{0,-11} {1,8} ms   {2}" -f $f, $x.ms, $x.cob
			if ($x.tr) { "                          {0}" -f $x.tr }
			if ($x.fl) { "                          {0}" -f $x.fl }
		}

		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT_FLUJO,env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
