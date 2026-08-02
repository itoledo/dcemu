# Perfil de muestreo del interprete (docs/interprete-plan.md, fase 0.1).
#
# **Hay que correrlo en una consola elevada.** El logger del kernel de ETW es lo
# que toma las muestras de CPU y Windows no lo habilita sin privilegios: sin
# elevacion, wpr contesta "Failed to enable the policy to profile system
# performance" y no graba nada.
#
# No hace falta instalar nada: wpr.exe viene con Windows y xperf.exe con el
# Windows Performance Toolkit del SDK, que ya esta en esta maquina.
#
#   herramientas\perfil.ps1                    # 60 s emulados de Crazy Taxi
#   herramientas\perfil.ps1 -Segundos 30
#
# Deja perfil.csv con una linea por funcion y su cuenta de muestras. Lo que se
# busca es el reparto que --perf no puede dar, porque vive **adentro** del
# 71,6 % que hoy se llama "resto (interprete)":
#
#   - el despacho (main_loop y la lectura de la tabla),
#   - los handlers, por familia (mov.c, arith.c, floatsimple.c, branch.c),
#   - lo que quede de memread/memwrite tras el camino directo,
#   - pref142 y el TA.

param(
	[int]    $Segundos = 60,
	[string] $Imagen   = "roms\Crazy Taxi (USA).cdi",
	[string] $Exe      = "build-x64\Debug\dcemu.exe",
	[string] $Salida   = "perfil.csv"
)

$elevado = ([Security.Principal.WindowsPrincipal] `
	[Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
		[Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $elevado) {
	throw "hace falta una consola elevada: el logger del kernel de ETW no arranca sin privilegios"
}

if (-not (Test-Path $Exe)) { throw "falta $Exe" }

$etl = Join-Path $env:TEMP "dcemu-perfil.etl"

# Los simbolos del build de Debug estan al lado del ejecutable. Sin esto el
# perfil sale con direcciones crudas, que no dicen nada.
$env:_NT_SYMBOL_PATH = (Resolve-Path (Split-Path $Exe)).Path

$env:DCEMU_PULSAR_START = "300,1100"
$env:DCEMU_PULSAR_A     = "1"
$env:DCEMU_SOLO_A       = "1"

Write-Host "grabando ..."
wpr -start CPU -filemode
try {
	& $Exe --bios --perf "--salir-tras=$Segundos" $Imagen | Out-Null
} finally {
	wpr -stop $etl
}

Write-Host "resolviendo simbolos (tarda) ..."

# -a stack agrega las muestras del perfil por pila; -symbols las resuelve contra
# el PDB. Se filtra por proceso para no traer el resto de la maquina.
xperf -i $etl -o $Salida -symbols -a stack -butterfly 2>&1 | Out-Null

if (Test-Path $Salida) {
	Write-Host "listo: $Salida"
} else {
	# xperf cambia de opciones entre versiones del SDK; si la agregacion no
	# salio, el volcado crudo siempre funciona y se agrega con lo que sea.
	Write-Host "la agregacion fallo; volcando los eventos crudos"
	xperf -i $etl -o $Salida -a dumper 2>&1 | Out-Null
}
