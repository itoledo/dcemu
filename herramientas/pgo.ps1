# pgo.ps1 -- entrena el perfil de PGO con el banco fijo.
#
# El ciclo entero:
#
#   cmake -S . -B build -DDCEMU_PGO=GEN
#   cmake --build build --config Release --target dcemu
#   herramientas\pgo.ps1
#   cmake -S . -B build -DDCEMU_PGO=USE
#   cmake --build build --config Release --target dcemu
#
# Por que el banco fijo y no cualquier cosa: PGO ordena el codigo por lo que la
# corrida de entrenamiento **hizo**, asi que entrenar con el menu del boot ROM
# deja el menu rapido y el juego donde estaba. Los tres guests van juntos a
# proposito -- dos sin MMU y uno con ella -- porque el reparto que se quiere
# ordenar es el de los tres, no el de uno.
#
# Los segundos emulados son mas cortos que los del banco de medicion: el binario
# instrumentado corre del orden de tres veces mas lento y lo que hace falta es
# pasar por los caminos, no repetirlos. Aun asi Crazy Taxi necesita llegar al
# juego (los 300/1100 de DCEMU_PULSAR_START), porque en los menus el reparto es
# otro -- 53 tiras por escena contra 1183 --.
param(
	[string] $Exe = "build\Release\dcemu.exe",
	[int]    $SegundosKatana = 90,
	[int]    $SegundosCE     = 20
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Exe)) { throw "falta ${Exe}: compila primero con -DDCEMU_PGO=GEN" }

$pgd = "build-pgo\dcemu.pgd"
if (-not (Test-Path $pgd)) { throw "falta ${pgd}: el binario no se enlazo con /GENPROFILE" }

# Las herramientas de PGO son las del **toolset x64**, no las que estan en el
# PATH: en esta maquina `link.exe` resuelve al de HostX86\x86 y su pgomgr no
# entiende un .pgd de 64 bits.
$dirExe = Split-Path (Resolve-Path $Exe)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msvc = $null

if (Test-Path $vswhere) {
	$vs = & $vswhere -latest -property installationPath
	if ($vs) {
		$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory -EA SilentlyContinue |
			Sort-Object Name -Descending |
			ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64" } |
			Where-Object { Test-Path (Join-Path $_ "pgomgr.exe") } |
			Select-Object -First 1
	}
}

if (-not $msvc) { throw "no encuentro las herramientas x64 de MSVC (pgomgr)" }

# El binario instrumentado carga pgort140.dll en tiempo de ejecucion. Sin ella
# no arranca --0xC000007B, que no dice nada util-- y la corrida se ve como una
# salida limpia si nadie mira el codigo de retorno. Por eso se copia y por eso
# el script mira el codigo de retorno de cada corrida.
if (-not (Test-Path (Join-Path $dirExe "pgort140.dll"))) {
	Copy-Item (Join-Path $msvc "pgort140.dll") $dirExe -Force
	Write-Host "copiado pgort140.dll junto al ejecutable"
}

# Los .pgc de una tanda anterior contaminarian el perfil con codigo que ya no
# existe. Se borran antes, no despues: si algo falla a mitad, lo que queda es
# un perfil incompleto y no uno mezclado. Caen junto al EJECUTABLE, no junto
# al .pgd.
Get-ChildItem $dirExe -Filter "dcemu!*.pgc" -EA SilentlyContinue | Remove-Item

# El peso con el que entra cada banco al perfil, y por que no es 1 para todos.
#
# PGO ordena por cuentas, y las cuentas son proporcionales a **instrucciones
# ejecutadas**: 90 s emulados de Crazy Taxi y de Virtua Tennis son unos 22 mil
# millones entre los dos, contra 3,1 de los 20 s de DCDoom. Con pesos iguales el
# guest con MMU queda en el 12 % del perfil y el enlazador manda su codigo al
# fondo -- medido: PGO asi entrenado daba **+15,9 % en Crazy Taxi y -6,3 % en
# DCDoom**, o sea que reproducia el mismo canje que se queria eliminar.
#
# El 7 equilibra las dos mitades sin alargar el entrenamiento: multiplicar los
# segundos emulados de DCDoom por siete son doce minutos de corrida
# instrumentada, y pgomgr sabe ponderar al fundir.
$bancos = @(
	@{ n = "crazytaxi"; img = "roms\Crazy Taxi (USA).cdi";                                                  s = $SegundosKatana; teclas = $true;  peso = 1 },
	@{ n = "vtennis";   img = "roms\Virtua Tennis (2000)(Sega)(US)[cr DCRES][f PAL 60Hz][repack].cdi";      s = $SegundosKatana; teclas = $true;  peso = 1 },
	@{ n = "dcdoom";    img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi";                                     s = $SegundosCE;     teclas = $false; peso = 7 }
)

foreach ($b in $bancos) {
	if ($b.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	Write-Host "entrenando con $($b.n) ($($b.s) s emulados, peso $($b.peso))..." -NoNewline

	$antes = @(Get-ChildItem $dirExe -Filter "dcemu!*.pgc" -EA SilentlyContinue |
				Select-Object -ExpandProperty Name)
	& $Exe "--salir-tras=$($b.s)" $b.img | Out-Null
	$despues = @(Get-ChildItem $dirExe -Filter "dcemu!*.pgc" -EA SilentlyContinue |
				Select-Object -ExpandProperty Name)

	# Que la corrida haya dejado su .pgc es la unica prueba de que corrio: un
	# binario instrumentado al que le falta pgort140.dll sale enseguida y en
	# silencio, y el entrenamiento entero se completa "bien" sin datos.
	$nuevo = @($despues | Where-Object { $antes -notcontains $_ })
	if ($nuevo.Count -eq 0) { throw "$($b.n) no dejo perfil" }

	$b.pgc = $nuevo[0]
	Write-Host " ok ($($b.pgc))"
}

Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue

# Cada corrida deja su propio .pgc; pgomgr los funde en el .pgd. Viene con
# MSVC, pero solo esta en el PATH de un shell de Visual Studio: si no aparece,
# se lo busca al lado del enlazador.
$pgomgr = Join-Path $msvc "pgomgr.exe"

# Uno por uno y con su peso: pgomgr /merge:N multiplica las cuentas del .pgc
# que funde. Fundir el directorio entero de una vez los pondera a todos igual.
foreach ($b in $bancos) {
	Write-Host "fundiendo $($b.pgc) con peso $($b.peso)..."

	& $pgomgr "/merge:$($b.peso)" (Join-Path $dirExe $b.pgc) $pgd | Out-Null
	if ($LASTEXITCODE -ne 0) { throw "pgomgr fallo con $LASTEXITCODE en $($b.n)" }
}

Write-Host "perfil listo en $pgd. Ahora:"
Write-Host "  cmake -S . -B build -DDCEMU_PGO=USE"
Write-Host "  cmake --build build --config Release --target dcemu"
