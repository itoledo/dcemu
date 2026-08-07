# Perfil con contadores de hardware (docs/rendimiento-plan.md, paso 1.1).
#
# Contesta la pregunta que quedo abierta cuando el cache de bloques dio cero:
# **en que se van los ~25 ciclos de anfitrion por instruccion emulada**, si el
# despacho no es. Tres cosas, y cada una pide una accion distinta:
#
#   - **IPC** del proceso. Bajo con pocos fallos de cache significa dependencias
#     y llamadas indirectas; alto significa que simplemente hay mucho trabajo.
#   - **Predicciones falladas de salto.** Es el predictor directo de lo que un
#     recompilador podria ganar en el despacho. Con el cache de bloques midiendo
#     cero, aca deberia verse por que.
#   - **Fallos de ultimo nivel de cache.** La otra hipotesis: el conjunto de
#     trabajo del guest, que es lo que explicaria 8,4 ns en los menus contra
#     14,4 en juego con la misma tabla de despacho.
#
# **Hay que correrlo en una consola elevada**: el logger del kernel de ETW no
# arranca sin privilegios. No hace falta instalar nada -- xperf.exe viene con el
# Windows Performance Toolkit del SDK, que ya esta en esta maquina, y los
# contadores los expone el propio procesador (xperf -pmcsources los lista).
#
#   herramientas\perfil-pmu.ps1                 # Crazy Taxi, los tres modos
#   herramientas\perfil-pmu.ps1 -Banco dcdoom   # el guest con MMU
#   herramientas\perfil-pmu.ps1 -Modos tiempo   # solo el perfil de tiempo
#
# Deja perfil-<banco>-<modo>.csv por cada modo.
#
# **Dos de los tres modos salen vacios en esta maquina y el CSV chico no lo
# grita** (medido el 2026-08-07, la primera vez que hubo consola elevada):
#
#   - `cuentas`: los eventos Pmc SI quedan en el .etl, pero la accion `-a pmc`
#     de esta version de xperf agrega cero filas. La agregacion la hace
#     herramientas/pmu-analizar.py sobre el volcado crudo -- ver su encabezado.
#   - `fallos`: `-PmcProfile` no graba ningun evento en este hibrido P+E; el
#     reporte sale con las tablas vacias. La atribucion por funcion sale igual
#     del .etl de `cuentas`, porque cada par Pmc/SampledProfile trae el PC.
#
# Los resultados de la primera corrida completa estan en
# docs/interprete-plan.md, "0.1, por fin".

param(
	[string]   $Banco  = "crazytaxi",
	[string[]] $Modos  = @("tiempo", "cuentas", "fallos"),
	[string]   $Exe    = "build\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"

# **Un codigo de salida de xperf no puede tumbar la corrida.** Desde PowerShell
# 7.3 un nativo que sale distinto de cero se convierte en excepcion terminante
# con ErrorActionPreference en Stop, y xperf sale distinto de cero por avisos
# --"1 Events were lost in this trace"-- que no invalidan nada. Se apaga esa
# conversion y se mira $LASTEXITCODE donde de verdad importa.
$PSNativeCommandUseErrorActionPreference = $false

if (-not (Test-Path -LiteralPath $Exe)) { throw "falta $Exe" }

$xperf = "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe"
if (-not (Test-Path $xperf)) {
	$c = Get-Command xperf -EA SilentlyContinue
	if (-not $c) { throw "falta xperf.exe (Windows Performance Toolkit)" }
	$xperf = $c.Source
}

# Los bancos, con la misma receta que el resto del arbol: sin --bios, 180 s para
# Katana y 35 para DCDoom.
#
# `retraso` son segundos **reales** de espera antes de empezar a grabar, y existe
# por una razon que invalidaria el perfil entero sin ella: a los 120 segundos
# emulados Crazy Taxi todavia esta en MODE SELECTION, asi que un perfil de la
# corrida completa serian dos tercios de menu. La diferencia importa -- el mismo
# despacho cuesta 8,4 ns en los menus y 14,4 en juego -- y es justo lo que se
# viene a mirar. DCDoom no lo necesita: juega E1M1 desde temprano.
$bancos = @{
	crazytaxi = @{
		img = "roms\Crazy Taxi (USA).cdi"
		s = 180; teclas = $true; retraso = 80
	}
	vtennis = @{
		img = "roms\Virtua Tennis (2000)(Sega)(US)[cr DCRES][f PAL 60Hz][repack].cdi"
		s = 180; teclas = $true; retraso = 80
	}
	dcdoom = @{
		img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"
		s = 35;  teclas = $false; retraso = 5
	}
}

if (-not $bancos.ContainsKey($Banco)) {
	throw "banco desconocido: $Banco. Hay: $($bancos.Keys -join ', ')"
}

$b = $bancos[$Banco]
# -LiteralPath y no Test-Path a secas: el .cdi de Virtua Tennis lleva
# corchetes en el nombre y Test-Path los toma por comodines.
if (-not (Test-Path -LiteralPath $b.img)) { throw "falta la imagen $($b.img)" }

# Los tres modos. `pmc` son los contadores a programar y `accion` como se
# procesa el .etl despues.
#
# El tope de la maquina son 9 fuentes simultaneas (xperf -pmcsources), asi que
# las cuatro de `cuentas` entran holgadas.
$catalogo = @{
	tiempo = @{
		arranque = @("-on", "PROC_THREAD+LOADER+PROFILE", "-stackwalk", "profile")
		accion   = @("profile", "-detail")
		que      = "donde se va el tiempo (muestreo por temporizador)"
	}
	cuentas = @{
		arranque = @("-on", "PROC_THREAD+LOADER+PROFILE",
		             "-Pmc", "InstructionsRetiredFixed,UnhaltedCoreCyclesFixed,BranchMispredictsRetired,LLCMisses",
		             "PROFILE")
		accion   = @("pmc")
		que      = "IPC, saltos fallados y fallos de LLC, en absoluto"
	}
	fallos = @{
		arranque = @("-on", "PROC_THREAD+LOADER", "-PmcProfile", "BranchMispredictsRetired",
		             "-stackwalk", "PmcInterrupt")
		accion   = @("stack", "-butterfly")
		que      = "que funcion falla las predicciones de salto"
	}
}

# Recien aca los privilegios: primero se valida todo lo barato, para que un
# banco mal escrito no cueste un cuadro de UAC.
$elevado = ([Security.Principal.WindowsPrincipal] `
	[Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
		[Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $elevado) {
	throw "hace falta una consola elevada: el logger del kernel de ETW no arranca sin privilegios"
}

# Los simbolos salen del PDB que Release deja al lado del ejecutable
# (CMakeLists.txt agrega /Zi a proposito: sin PDB el perfil manda todo a
# ***unknown*** y el primero salio asi).
$env:_NT_SYMBOL_PATH = (Resolve-Path (Split-Path $Exe)).Path

foreach ($m in $Modos) {
	if (-not $catalogo.ContainsKey($m)) { throw "modo desconocido: $m" }

	$cfg    = $catalogo[$m]
	$etl    = Join-Path $env:TEMP "dcemu-pmu-$Banco-$m.etl"
	$salida = "perfil-$Banco-$m.csv"

	Write-Host ""
	Write-Host "== $Banco / $m : $($cfg.que)"

	if ($b.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"
		$env:DCEMU_PULSAR_A     = "1"
		$env:DCEMU_SOLO_A       = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	# El emulador arranca primero y la traza despues, para no grabar el arranque
	# ni los menus. Ver el comentario de `retraso`.
	# **La imagen va entrecomillada a mano.** Start-Process une el arreglo de
	# argumentos con espacios y NO entrecomilla los elementos que los llevan, asi
	# que "roms\DCDoom GDI and CDI\DCDoom CDI.cdi" le llegaba al emulador partido
	# en cinco argumentos y salia con "sobra el argumento: GDI". Les pasa a los
	# tres bancos: los tres .cdi tienen espacios en el nombre.
	#
	# Y -WorkingDirectory explicito, que no cuesta nada: Start-Process no hereda
	# el directorio de PowerShell, y el emulador resuelve bios/, font.png y roms/
	# contra el suyo.
	$p = Start-Process -FilePath $Exe `
		-ArgumentList @("--perf", "--salir-tras=$($b.s)", ('"' + $b.img + '"')) `
		-WorkingDirectory (Get-Location).Path `
		-PassThru -NoNewWindow

	Start-Sleep -Seconds $b.retraso

	if ($p.HasExited) {
		throw ("el emulador termino con codigo $($p.ExitCode) antes de empezar a " +
			"grabar. Con 1, la razon esta en build\Release\stderr.txt --que cae " +
			"junto al EJECUTABLE, no aca--; con 0, bajar el retraso del banco.")
	}

	& $xperf -start @($cfg.arranque) | Out-Null
	if ($LASTEXITCODE -ne 0) { $p.Kill(); throw "xperf -start fallo con $LASTEXITCODE" }

	try {
		$p.WaitForExit()
	} finally {
		# En finally para que una caida del emulador no deje el logger del kernel
		# grabando: si queda prendido, el proximo -start falla y el .etl crece sin
		# techo hasta llenar el disco.
		& $xperf -stop -d $etl | Out-Null
	}

	Write-Host "   resolviendo simbolos (tarda) ..."

	# -tle y -tti: procesar igual con eventos perdidos o inversiones de tiempo.
	# Un evento perdido de un millon no cambia un reparto por muestreo, y sin
	# esto xperf se planta y el .etl --que costo la corrida entera-- queda sin
	# procesar.
	#
	# Y la accion va con sus opciones: `-a stack` **exige** una, y sin ella
	# contesta "error: stack: no option specified" y deja un CSV de cero bytes.
	& $xperf -i $etl -o $salida -symbols -tle -tti -a @($cfg.accion) 2>&1 |
		Out-Null

	if ((Test-Path -LiteralPath $salida) -and
	    ((Get-Item -LiteralPath $salida).Length -gt 0)) {
		Write-Host "   listo: $salida ($([int]((Get-Item -LiteralPath $salida).Length/1KB)) KB)"
	} else {
		Write-Host "   la agregacion fallo; volcando los eventos crudos"
		& $xperf -i $etl -o $salida -tle -tti -a dumper 2>&1 | Out-Null
	}

	# El .etl se conserva: pesa 45 MB pero vale media hora de corrida, y
	# reprocesarlo con otra accion **no necesita privilegios**.
	Write-Host "   traza en $etl"
}

Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Host ""
Write-Host "hecho."
