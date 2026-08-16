# Cuanto esta en juego con el medio pixel: los 15 juegos a 1:1, con el
# corrimiento y sin el.
#
# El oraculo es el numero de colores distintos, y **solo vale para contenido
# 1:1** (un texel por pixel): ahi un juego de texeles es discreto y cualquier
# muestreo entre texeles inventa intermedios. Para contenido magnificado no
# dice nada -- DCDoom dibuja 320x240 texeles sobre 640x480 pixeles y su cuenta
# mide fase de magnificacion, no convencion de muestreo. Por eso lo que se
# busca aqui es una caida GRANDE y sistematica, no el signo de cada fila.
$ErrorActionPreference = "Stop"

Set-Location "$PSScriptRoot\..\build\Release"

$juegos = @(
	@{ n = "crazytaxi";  img = "..\..\roms\Crazy Taxi (USA).cdi"; s = 10 },
	@{ n = "crazytaxi2"; img = "..\..\roms\Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!]\Crazy Taxi 2 v1.004 (2001)(Sega)(US)[!].gdi"; s = 30 },
	@{ n = "sr2";        img = "..\..\roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 30 },
	@{ n = "vtennis";    img = "..\..\roms\Virtua Tennis v1.001 (2000)(Sega)(US)[!]\Virtua Tennis v1.001 (2000)(Sega)(US)[!].gdi"; s = 30 },
	@{ n = "vtennis2";   img = "..\..\roms\Virtua Tenis 2 (USA).cdi"; s = 30 },
	@{ n = "dave";       img = "..\..\roms\Dave Mirra Freestyle BMX v1.001 (2000)(Acclaim)(US)[!][0K055A, B, D, E]\Dave Mirra Freestyle BMX v1.001 (2000)(Acclaim)(US)[!][0K055A, B, D, E].gdi"; s = 30 },
	@{ n = "chuchu";     img = "..\..\roms\ChuChu Rocket! v1.007 (2000)(Sega)(US)(en-ja)[!]\ChuChu Rocket! v1.007 (2000)(Sega)(US)(en-ja)[!].gdi"; s = 30 },
	@{ n = "doa2";       img = "..\..\roms\Dead or Alive 2 v1.100 (2000)(Tecmo)(US)[!]\Dead or Alive 2 v1.100 (2000)(Tecmo)(US)[!].gdi"; s = 30 },
	@{ n = "sf3";        img = "..\..\roms\Street Fighter III - 3rd Strike v1.001 (2000)(Capcom)(US)[!]\Street Fighter III - 3rd Strike v1.001 (2000)(Capcom)(US)[!].gdi"; s = 30 },
	@{ n = "cvs";        img = "..\..\roms\Capcom vs. SNK (USA).cdi"; s = 30 },
	@{ n = "4x4evo";     img = "..\..\roms\4X4 EVO v1.001 (2000)(GOD)(US)[!]\4X4 EVO v1.001 (2000)(GOD)(US)[!].gdi"; s = 30 },
	@{ n = "mathoffman"; img = "..\..\roms\Mat Hoffman's Pro BMX v1.000 (2001)(Activision)(US)[!]\Mat Hoffman's Pro BMX v1.000 (2001)(Activision)(US)[!].gdi"; s = 30 },
	@{ n = "quake3";     img = "..\..\roms\Quake III Arena v0.800 (2000)(Sega)(US)[!]\Quake III Arena v0.800 (2000)(Sega)(US)[!].gdi"; s = 30 },
	@{ n = "tennis2k2";  img = "..\..\roms\Tennis 2K2 v1.009 (2001)(Sega)(US)(M5)[!]\Tennis 2K2 v1.009 (2001)(Sega)(US)(M5)[!].gdi"; s = 30 },
	@{ n = "dcdoom";     img = "..\..\roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 20 }
)

function Colores($ruta)
{
	$s = & "$PSScriptRoot\colores.ps1" -Imagen $ruta
	if ($s -match '(\d+) colores') { return [int] $Matches[1] }
	return -1
}

foreach ($j in $juegos) {
	foreach ($mp in @("hoy", "centro")) {
		if ($mp -eq "centro") { $env:DCEMU_MEDIO_PIXEL_MIL = "0" }
		else { Remove-Item env:DCEMU_MEDIO_PIXEL_MIL -EA SilentlyContinue }

		& .\dcemu.exe $j.img "--salir-tras=$($j.s)" --sin-vmu --render=fbo --escala=1 `
			"--captura-gl=mpj-$($j.n)-$mp.bmp" | Out-Null
	}

	Remove-Item env:DCEMU_MEDIO_PIXEL_MIL -EA SilentlyContinue

	$a = Colores "mpj-$($j.n)-hoy.bmp"
	$b = Colores "mpj-$($j.n)-centro.bmp"
	$d = if ($a -gt 0) { 100.0 * ($b - $a) / $a } else { 0 }

	"{0,-12} hoy {1,7}   centro del pixel {2,7}   {3,7:N1} %" -f $j.n, $a, $b, $d
}

"=== fin"
