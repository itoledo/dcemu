# El clamp de borde contra los 15 juegos comerciales.
#
# Es la pasada que decide si el arreglo de la costura del logo de Crazy Taxi
# --clamp en vez de REPEAT cuando la tira no sale de [0,1]-- se puede dejar
# encendido. Existe porque el intento ANTERIOR sobre el mismo sintoma (sumar
# medio texel a las UV) paso el parque de KOS entero y rompio el fondo de
# Street Fighter III: para un cambio de muestreo de textura, la linea base son
# los juegos.
#
#   con    por omision -- el clamp de borde
#   sin    DCEMU_SIN_CLAMP_BORDE=1 -- la conducta anterior
#
# Se comprueba lo mismo que aquella vez: que cada juego sigue arrancando y
# dibujando (escenas y tiras del resumen, porque una captura negra y una lista
# vacia dan el mismo sintoma), que la captura es determinista, y cuantas
# costuras tiene cada brazo (herramientas/costuras.ps1, aparte).

$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"

$juegos = @(
	@{ n = "crazytaxi";  img = "..\..\roms\Crazy Taxi (USA).cdi"; s = 30; teclas = $true },
	@{ n = "crazytaxi2"; img = "..\..\roms\Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!]\Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!].gdi"; s = 30; teclas = $true },
	@{ n = "sr2";        img = "..\..\roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "dcdoom";     img = "..\..\roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 20; teclas = $false },
	@{ n = "vtennis";    img = "..\..\roms\Virtua Tennis v1.001 (2000)(Sega)(US)[!]\Virtua Tennis v1.001 (2000)(Sega)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "vtennis2";   img = "..\..\roms\Virtua Tenis 2 (USA).cdi"; s = 30; teclas = $false },
	@{ n = "dave";       img = "..\..\roms\Dave Mirra Freestyle BMX v1.001 (2000)(Acclaim)(US)[!][0K055A, B, D, E]\Dave Mirra Freestyle BMX v1.001 (2000)(Acclaim)(US)[!][0K055A, B, D, E].gdi"; s = 30; teclas = $false },
	@{ n = "chuchu";     img = "..\..\roms\ChuChu Rocket! v1.007 (2000)(Sega)(US)(en-ja)[!]\ChuChu Rocket! v1.007 (2000)(Sega)(US)(en-ja)[!].gdi"; s = 30; teclas = $false },
	@{ n = "doa2";       img = "..\..\roms\Dead or Alive 2 v1.100 (2000)(Tecmo)(US)[!]\Dead or Alive 2 v1.100 (2000)(Tecmo)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "sf3";        img = "..\..\roms\Street Fighter III - 3rd Strike v1.001 (2000)(Capcom)(US)[!]\Street Fighter III - 3rd Strike v1.001 (2000)(Capcom)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "cvs";        img = "..\..\roms\Capcom vs. SNK (USA).cdi"; s = 30; teclas = $false },
	@{ n = "4x4evo";     img = "..\..\roms\4X4 EVO v1.001 (2000)(GOD)(US)[!]\4X4 EVO v1.001 (2000)(GOD)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "mathoffman"; img = "..\..\roms\Mat Hoffman's Pro BMX v1.000 (2001)(Activision)(US)[!]\Mat Hoffman's Pro BMX v1.000 (2001)(Activision)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "quake3";     img = "..\..\roms\Quake III Arena v0.800 (2000)(Sega)(US)[!]\Quake III Arena v0.800 (2000)(Sega)(US)[!].gdi"; s = 30; teclas = $false },
	@{ n = "tennis2k2";  img = "..\..\roms\Tennis 2K2 v1.009 (2001)(Sega)(US)(M5)[!]\Tennis 2K2 v1.009 (2001)(Sega)(US)(M5)[!].gdi"; s = 30; teclas = $false }
)

function Correr($j, $brazo, $sufijo)
{
	if ($brazo -eq "sin") { $env:DCEMU_SIN_CLAMP_BORDE = "1" }
	else { Remove-Item env:DCEMU_SIN_CLAMP_BORDE -EA SilentlyContinue }

	if ($j.teclas) {
		$env:DCEMU_PULSAR_START = "150,300,450"
		$env:DCEMU_PULSAR_A = "1"
		$env:DCEMU_SOLO_A = "600"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$bmp = "clamp-$($j.n)-$brazo$sufijo.bmp"
	Remove-Item $bmp -Force -EA SilentlyContinue

	# --traza-mem para que el resumen informe escenas y tiras: es lo que separa
	# "dejo de dibujar" de "la captura salio mal".
	& .\dcemu.exe $j.img "--salir-tras=$($j.s)" --sin-vmu --traza-mem `
		"--captura-gl=$bmp" | Out-Null

	$h = if (Test-Path $bmp) { (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16) } else { "sin captura" }

	$tiras = (Select-String -Path stderr.txt -Pattern "escenas rendidas" |
			Select-Object -Last 1).Line
	if ($null -eq $tiras) { $tiras = "" }

	return @{ h = $h; tiras = $tiras.Trim() }
}

foreach ($j in $juegos) {
	$a1 = Correr $j "con" ""
	$a2 = Correr $j "con" "-2"
	$b1 = Correr $j "sin" ""

	$det = if ($a1.h -eq $a2.h) { "determinista" } else { "NO DETERMINISTA" }
	$mov = if ($a1.h -eq $b1.h) { "sin cambio" } else { "movida" }

	"{0,-12} con={1} sin={2}  {3,-16} {4}" -f $j.n, $a1.h, $b1.h, $det, $mov
	if ($a1.tiras -ne "") { "             {0}" -f $a1.tiras }
}

Remove-Item env:DCEMU_SIN_CLAMP_BORDE -EA SilentlyContinue
Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
"=== fin"
