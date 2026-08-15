# La tanda del indice de enlaces, en TRES brazos y dentro de un binario.
#
# Tres y no dos porque el cambio mueve las dos cosas que el arbol reporta y se
# leen distinto:
#
#   interprete   DCEMU_JIT=0, la referencia de la marca
#   traductor    el indice por PC destino (por omision)
#   lineal       DCEMU_JIT_ENLACE_LINEAL=1, el barrido cuadratico de antes
#
# "traductor contra lineal" es el veredicto del escalon; "traductor contra
# interprete" es la marca del arbol, que hay que rehacer porque el escalon la
# mueve. En una sola tanda para no pagar dos veces el reentrenamiento.
#
# Ojo con como se lee el brazo lineal: su costo CRECE con la corrida, porque el
# barrido es cuadratico en la cantidad de bloques traducidos. O sea que la
# diferencia depende de cuanto dura el banco, y no es una constante del guest.
#
# Reglas de siempre (CLAUDE.md, "Measurement discipline"): reentrenar antes
# (herramientas\ciclo-jit.ps1), descartar la primera corrida de cada guest --el
# cache frio de la imagen se paga por guest, no por tanda--, rotar el orden
# dentro de cada ronda, y comparar absolutos solo dentro de este binario.
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

	if ($forma -eq "lineal") {
		$env:DCEMU_JIT_ENLACE_LINEAL = "1"
	} else {
		Remove-Item env:DCEMU_JIT_ENLACE_LINEAL -EA SilentlyContinue
	}

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	# Control de trabajo por corrida: si la cobertura cambia entre corridas de
	# la misma forma, la tanda no compara. Y el tiempo de traducir al lado, que
	# es lo que este escalon mueve.
	$cob = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones en" |
			ForEach-Object { $_.Line }) -join " "
	$tr = (Select-String -Path $err -Pattern "^jit: traducir" |
			ForEach-Object { $_.Line }) -join " "

	return @{ ms = $reloj.ElapsedMilliseconds; cob = $cob; tr = $tr }
}

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (primera corrida descartada)"
	Correr $b.img $b.s $b.teclas "traductor" | Out-Null

	for ($r = 1; $r -le $b.rondas; $r++) {
		$orden = switch ($r % 3) {
			1 { @("traductor", "lineal", "interprete") }
			2 { @("lineal", "interprete", "traductor") }
			0 { @("interprete", "traductor", "lineal") }
		}

		foreach ($f in $orden) {
			$x = Correr $b.img $b.s $b.teclas $f
			"{0,-11} {1,8} ms   {2}" -f $f, $x.ms, $x.cob
			if ($x.tr) { "                          {0}" -f $x.tr }
		}

		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT_ENLACE_LINEAL,env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
