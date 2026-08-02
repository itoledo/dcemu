# A/B entre la rama antigua y la nueva: cuanto cambio la velocidad de un juego.
#
# La rama antigua es pendientes-c-y-banco, que trae TODO el contenido nuevo --
# render, arranque, los cuatro juegos -- y nada del trabajo de rendimiento. Asi
# el numero aisla lo que aporto rendimiento-hilos y no se le cuelan las mejoras
# de render, que cambian lo que se dibuja pero no por que se dibuja mas rapido.
#
#   git worktree add ..\dcemu-antiguo origin/pendientes-c-y-banco --detach
#   cmake -S ..\dcemu-antiguo -B ..\dcemu-antiguo\build -A x64 -DDCEMU_BUILD_TESTS=OFF
#   cmake --build ..\dcemu-antiguo\build --config Debug --target dcemu
#   herramientas\rama-ab.ps1
#
# El binario antiguo no tiene --perf, que es de la rama nueva, asi que el tiempo
# se toma desde afuera. No importa: --salir-tras fija el tiempo **emulado**, o
# sea que la velocidad es emulado/real y las dos corridas emulan lo mismo.
#
# Sin --bios: con --bios Crazy Taxi se cuelga en LOADING (31K) y lo que se mide
# es el menu del boot ROM. Ver CLAUDE.md, "BIOS syscall emulation".

param(
	[int]    $Vueltas  = 2,
	[int]    $Segundos = 180,
	[string] $Imagen   = "roms\Crazy Taxi (USA).cdi",
	[string] $Antiguo  = "..\dcemu-antiguo\build\Debug\dcemu.exe",
	[string] $Nuevo    = "build-x64\Debug\dcemu.exe"
)

foreach ($b in @($Antiguo, $Nuevo)) {
	if (-not (Test-Path $b)) { throw "falta $b" }
}

# La secuencia que llega al 3D en movimiento: a los 120 s la corrida esta en
# MODE SELECTION (479 tiras por escena) y a los 180 en el juego (1182, con
# picos de 1889). Medir en los menus mide otra cosa.
$env:DCEMU_PULSAR_START = "300,1100"
$env:DCEMU_PULSAR_A     = "1"
$env:DCEMU_SOLO_A       = "1"

$resultados = @()

for ($v = 1; $v -le $Vueltas; $v++) {
	foreach ($par in @(@("antigua", $Antiguo), @("nueva", $Nuevo))) {
		$nombre = $par[0]
		$exe    = $par[1]

		Write-Host "vuelta $v : $nombre ..." -NoNewline

		$t = Measure-Command { & $exe "--salir-tras=$Segundos" $Imagen | Out-Null }

		$ms = [long]$t.TotalMilliseconds

		$resultados += [pscustomobject]@{
			vuelta = $v
			rama   = $nombre
			ms     = $ms
			veloc  = [math]::Round($Segundos * 1000.0 / $ms, 3)
		}

		Write-Host (" {0} ms, {1}x" -f $ms, [math]::Round($Segundos * 1000.0 / $ms, 3))
	}
}

Write-Host ""
$resultados | Format-Table -AutoSize

$medianas = @{}
foreach ($nombre in @("antigua", "nueva")) {
	$g = @($resultados | Where-Object rama -eq $nombre | Sort-Object ms)
	if ($g.Count -eq 0) { continue }
	$medianas[$nombre] = $g[[int]($g.Count / 2)].ms
}

if ($medianas.ContainsKey("antigua") -and $medianas.ContainsKey("nueva")) {
	$a = $medianas["antigua"]
	$n = $medianas["nueva"]

	Write-Host ("`nmediana antigua {0} ms, nueva {1} ms" -f $a, $n)
	Write-Host ("gana {0:N1} %  ({1:N2}x)" -f (100.0 * ($a - $n) / $a), ($a / [double]$n))
	Write-Host ("velocidad {0:N2}x -> {1:N2}x" -f `
		($Segundos * 1000.0 / $a), ($Segundos * 1000.0 / $n))
}
