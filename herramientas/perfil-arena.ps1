# El reparto modulo/arena bajo el JIT (fase A de docs/jit-sota-plan.md): cuanto
# del tiempo corre en el codigo EMITIDO (el arena RWX de VirtualAlloc, fuera de
# todo modulo) y cuanto en el C compilado (dcemu.exe). Es el techo de la fase G:
# el compilador solo puede mover la mitad compilada.
#
# **Hay que correrlo en una consola elevada** (el logger del kernel de ETW; la
# misma regla y el mismo porque de perfil.ps1). Corre los tres guests del banco
# con DCEMU_JIT=2 y deja logs/perfil-arena-<guest>.csv mas un resumen en
# pantalla: muestras en dcemu.exe / muestras sin modulo (= arena) / resto.
#
# La clasificacion: en el volcado de xperf, una muestra cuyo PC no cae en ningun
# modulo cargado sale sin imagen ("?" o "unknown"). En el proceso dcemu eso es el
# arena -- no hay otro codigo fuera de modulos -- asi que el reparto no necesita
# simbolos para la cifra gruesa; los CSV quedan para mirar el detalle por
# funcion del lado compilado.
param(
	[string] $Exe = "build-jit\Release\dcemu.exe"
)

$elevado = ([Security.Principal.WindowsPrincipal] `
	[Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
		[Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $elevado) {
	throw "hace falta una consola elevada: el logger del kernel de ETW no arranca sin privilegios"
}
if (-not (Test-Path $Exe)) { throw "falta $Exe" }

$env:_NT_SYMBOL_PATH = (Resolve-Path (Split-Path $Exe)).Path
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
	@{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi";  s = 35;  teclas = $false },
	@{ n = "ct";     img = "roms\Crazy Taxi (USA).cdi";               s = 180; teclas = $true  },
	@{ n = "sr2";    img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

foreach ($b in $bancos) {
	if ($b.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}
	$env:DCEMU_JIT = "2"

	$etl = Join-Path $env:TEMP "dcemu-arena-$($b.n).etl"
	Write-Output "grabando $($b.n) ($($b.s) s emulados) ..."
	wpr -start CPU -filemode
	try {
		& $Exe "--salir-tras=$($b.s)" --sin-vmu $b.img | Out-Null
	} finally {
		wpr -stop $etl
	}

	# El volcado crudo de muestras: una linea por muestra con proceso, hilo y
	# la columna Image!Function. La cifra gruesa se saca de ahi mismo.
	$csv = "logs\perfil-arena-$($b.n).csv"
	xperf -i $etl -o $csv -symbols -a dumper 2>&1 | Out-Null

	if (-not (Test-Path $csv)) {
		"{0,-8} xperf no dejo CSV; el .etl queda en {1} para reintentar" -f $b.n, $etl
		continue
	}

	# Solo el HILO PRINCIPAL (arranca en dcemu.exe!WinMainCRTStartup): el
	# proceso carga ademas los workers del driver de video y el audio de SDL,
	# que no son ni C compilado ni arena. El arena sale como `"Unknown"` (con
	# comillas) en la columna de imagen: es lo unico del hilo que no cae en
	# ningun modulo.
	$total = 0; $modulo = 0; $arena = 0; $resto = @{}
	Select-String -Path $csv -Pattern '^\s*SampledProfile,[^,]+,\s*dcemu\.exe \([0-9]+\),[^,]+,[^,]+,[^,]+,\s*dcemu\.exe!WinMainCRTStartup,\s*([^,]+),' | ForEach-Object {
		$img = $_.Matches[0].Groups[1].Value.Trim(); $total++
		if     ($img -like 'dcemu.exe!*')  { $modulo++ }
		elseif ($img -match '^"?Unknown|^0x') { $arena++ }
		else   { $lib = ($img -split '!')[0]; $resto[$lib] = 1 + ($resto[$lib] ?? 0) }
	}

	"{0,-8} hilo principal: {1} muestras  |  dcemu.exe {2} ({3:P1})  arena {4} ({5:P1})" -f `
		$b.n, $total, $modulo, ($modulo / [Math]::Max(1,$total)), $arena, ($arena / [Math]::Max(1,$total))
	$resto.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 5 | ForEach-Object {
		"           {0,-20} {1,6}  ({2:P1})" -f $_.Key, $_.Value, ($_.Value / [Math]::Max(1,$total)) }
	Remove-Item $etl -EA SilentlyContinue
}

Remove-Item env:DCEMU_JIT,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "hecho: logs\perfil-arena-*.csv"
