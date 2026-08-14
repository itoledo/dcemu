# El A/B de tres brazos del camino de escritura emitido, dentro de UN binario.
#
# El de dos brazos (guardas-ab.ps1) mide los dos cambios juntos, que es como
# entraron; este los separa, porque tiran para lados distintos y el combinado
# no lo dice:
#
#   nuevo    las dos guardas muertas plegadas + la rejilla de 64 bytes en linea
#   rejilla  solo el pliegue de guardas (DCEMU_JIT_SIN_REJILLA=1)
#   viejo    la emision anterior entera (las dos palancas)
#
# Asi "nuevo contra rejilla" aisla la rejilla y "rejilla contra viejo" aisla el
# pliegue. Sin los tres, un combinado neutro puede ser dos cosas que se cancelan
# y nadie se entera.
#
# Solo los dos guests inmunes al jitter del mando: Crazy Taxi necesita teclas y
# su primera corrida de cada tanda sale fuera de rango (el cache frio de la
# imagen), que fue justo lo que dejo su par solapado en la tanda anterior.
$ErrorActionPreference = "Stop"

$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

if (-not (Test-Path $exe)) { throw "falta $exe" }

Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35 },
	@{ n = "sr2";    img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60 }
)

function Correr($img, $segundos, $forma)
{
	$env:DCEMU_JIT = "2"; $env:DCEMU_FUSION = "0"
	Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	Remove-Item env:DCEMU_JIT_GUARDAS_VIEJAS -EA SilentlyContinue
	Remove-Item env:DCEMU_JIT_SIN_REJILLA -EA SilentlyContinue

	if ($forma -eq "rejilla") { $env:DCEMU_JIT_SIN_REJILLA = "1" }
	if ($forma -eq "viejo") { $env:DCEMU_JIT_SIN_REJILLA = "1"; $env:DCEMU_JIT_GUARDAS_VIEJAS = "1" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$cob = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones en" |
			ForEach-Object { $_.Line }) -join " "

	return @{ ms = $reloj.ElapsedMilliseconds; cob = $cob }
}

# Un calentamiento por guest, no uno para toda la tanda: el cache frio de la
# imagen se paga por guest y no por binario.
foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (primera corrida descartada)"
	Correr $b.img $b.s "nuevo" | Out-Null

	for ($r = 1; $r -le 3; $r++) {
		# El orden rota entre rondas para que ninguna forma sea siempre la
		# primera del grupo.
		$orden = switch ($r % 3) {
			1 { @("nuevo", "rejilla", "viejo") }
			2 { @("rejilla", "viejo", "nuevo") }
			0 { @("viejo", "nuevo", "rejilla") }
		}

		foreach ($f in $orden) {
			$x = Correr $b.img $b.s $f
			"{0,-9} {1,8} ms   {2}" -f $f, $x.ms, $x.cob
		}

		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT_GUARDAS_VIEJAS,env:DCEMU_JIT_SIN_REJILLA -EA SilentlyContinue
Remove-Item env:DCEMU_JIT,env:DCEMU_FUSION -EA SilentlyContinue
Write-Output "=== fin"
