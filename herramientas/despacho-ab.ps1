# A/B de las dos formas de despachar del interprete (docs/interprete-plan.md, A).
#
# Alterna los dos binarios en la MISMA tanda, que es la unica comparacion valida:
# la misma escena ya dio 0,35x y 0,57x segun la cache de archivos y la temperatura
# de la maquina.
#
#   cmake -S . -B build-x64      -A x64                                 # expandida
#   cmake -S . -B build-compacto -A x64 -DDCEMU_DESPACHO_COMPACTO=ON     # compacta
#   cmake --build build-x64      --config Debug --target dcemu
#   cmake --build build-compacto --config Debug --target dcemu
#   herramientas\despacho-ab.ps1
#
# Se corre desde la raiz del repo: bios/, font.png y roms/ resuelven contra el
# directorio de trabajo.

# **Sin --bios**, que es el camino de los hooks de syscall. Con --bios el ROM
# arranca el disco pero Crazy Taxi se cuelga en LOADING (31K): el hito D se
# alcanzo por los hooks, y es ahi donde GDROM_CHECK_DRIVE contesta el tipo de
# disco que Katana espera (ver CLAUDE.md). Con --bios la corrida da 53 tiras por
# escena y son las del menu del ROM -- identicas para cualquier disco, que es
# como se descubrio: Crazy Taxi y Virtua Tennis daban el mismo numero al digito.
#
# Y 120 segundos no alcanzan: a los 120 la corrida esta en MODE SELECTION.
param(
	[int]    $Vueltas  = 3,
	[int]    $Segundos = 120,
	[string] $Imagen   = "roms\Crazy Taxi (USA).cdi"
)

$binarios = @{
	"expandida" = "build-x64\Debug\dcemu.exe"
	"compacta"  = "build-compacto\Debug\dcemu.exe"
}

foreach ($b in $binarios.Values) {
	if (-not (Test-Path $b)) { throw "falta $b" }
}

# La secuencia que llega al juego, no a los menus: con mil tiras por escena el
# interprete pesa lo que pesa de verdad. Ver docs/hilos-plan.md.
$env:DCEMU_PULSAR_START = "300,1100"
$env:DCEMU_PULSAR_A     = "1"
$env:DCEMU_SOLO_A       = "1"

$resultados = @()

for ($v = 1; $v -le $Vueltas; $v++) {
	foreach ($nombre in @("expandida", "compacta")) {
		$exe = $binarios[$nombre]
		$err = Join-Path (Split-Path $exe) "stderr.txt"

		if (Test-Path $err) { Remove-Item $err }

		Write-Host "vuelta $v : $nombre ..." -NoNewline

		& $exe --perf "--salir-tras=$Segundos" $Imagen | Out-Null

		if (-not (Test-Path $err)) {
			Write-Host " sin stderr.txt"
			continue
		}

		$texto = Get-Content $err -Raw

		# Las cifras del resumen de --perf. Las tres ultimas existen para poder
		# afirmar que las dos corridas hicieron el MISMO trabajo: si difieren,
		# los tiempos no se comparan.
		$ms       = [regex]::Match($texto, 'perf: (\d+) ms reales')
		$vel      = [regex]::Match($texto, '\(([\d.]+)x\)')
		$instr    = [regex]::Match($texto, 'perf: (\d+) instrucciones, ([\d.]+) ns')
		$cuadros  = [regex]::Match($texto, 'perf: (\d+) cuadros presentados')
		$escenas  = [regex]::Match($texto, 'perf: (\d+) escenas, ([\d.]+) tiras')

		$r = [pscustomobject]@{
			vuelta   = $v
			variante = $nombre
			ms       = if ($ms.Success)      { [long]  $ms.Groups[1].Value }      else { 0 }
			veloc    = if ($vel.Success)     { [double]$vel.Groups[1].Value }     else { 0 }
			instr    = if ($instr.Success)   { [long]  $instr.Groups[1].Value }   else { 0 }
			ns_instr = if ($instr.Success)   { [double]$instr.Groups[2].Value }   else { 0 }
			cuadros  = if ($cuadros.Success) { [long]  $cuadros.Groups[1].Value } else { 0 }
			escenas  = if ($escenas.Success) { [long]  $escenas.Groups[1].Value } else { 0 }
			tiras    = if ($escenas.Success) { [double]$escenas.Groups[2].Value } else { 0 }
		}

		$resultados += $r

		# Las tiras por escena van en la linea de cada corrida a proposito: es
		# lo que dice si se midio el juego o un menu, y una tanda entera con el
		# workload equivocado se parece a una tanda buena.
		Write-Host (" {0} ms, {1}x, {2} ns/instr, {3} cuadros, {4} tiras/escena" -f `
			$r.ms, $r.veloc, $r.ns_instr, $r.cuadros, $r.tiras)
	}
}

Write-Host ""
$resultados | Format-Table -AutoSize

# Dos corridas solo se comparan si hicieron el mismo trabajo. Si las tiras por
# escena se separan, el guest diverguio y los tiempos no significan nada -- ya
# paso una vez, con --sin-aica: la corrida "salia" 40 % mas rapida porque el
# juego no avanzaba.
$tiras = @($resultados | Select-Object -ExpandProperty tiras)
if ($tiras.Count -gt 1) {
	$disp = ($tiras | Measure-Object -Maximum -Minimum)
	if ($disp.Minimum -gt 0 -and ($disp.Maximum - $disp.Minimum) / $disp.Minimum -gt 0.05) {
		Write-Host "`nOJO: las tiras por escena varian mas de 5 % entre corridas" -ForegroundColor Yellow
		Write-Host "     ($($disp.Minimum) a $($disp.Maximum)): no hicieron el mismo trabajo." -ForegroundColor Yellow
	}
}

Write-Host "`nmediana por variante:"
foreach ($nombre in @("expandida", "compacta")) {
	$g = @($resultados | Where-Object variante -eq $nombre | Sort-Object ms)
	if ($g.Count -eq 0) { continue }

	$mediana = $g[[int]($g.Count / 2)]

	"{0,-10} {1,8} ms  {2,6} ns/instr  cuadros {3}  escenas {4}  tiras {5}" -f `
		$nombre, $mediana.ms, $mediana.ns_instr, $mediana.cuadros, $mediana.escenas, $mediana.tiras
}
