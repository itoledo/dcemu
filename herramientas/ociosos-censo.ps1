# ociosos-censo.ps1 -- el censo de por que NO se elide, que es lo que decide si
# hay una v2 de la elision de lazos ociosos y de que forma.
#
# La v1 elide el 37 % de las instrucciones de Crazy Taxi y CERO en todo lo
# demas, y con los contadores de la v1 esa diferencia no se puede leer: una
# arista sin sonda no cuenta nada, y una sonda que falla no dice por que. El
# binario del censo agrega las dos mitades que faltan --el reparto de los
# enlaces SIN sonda por motivo, y el de las vueltas impuras por clase de
# suciedad (escritura emitida, acceso por ayudante, manejador, entrada al
# despachador)-- y esto las junta por guest.
#
# No cronometra: las cuentas son del guest y salen iguales en cualquier
# compilacion (la excepcion de DCEMU_FORMA). Corre el traductor entero, que es
# la omision, y solo lee stderr.
param(
	[string] $Exe = "",
	[int]    $TopeMin = 12,
	[switch] $SinDemos
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
. "$PSScriptRoot\banco.ps1"
if (-not $Exe) { $Exe = "build-clang\dcemu.exe" }
if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"
New-Item -ItemType Directory -Force logs\ociosos-censo | Out-Null

# Los guests planos son los unicos que pueden elidir en la v1 (bajo MMU no se
# emiten bumps ni se instalan sondas), asi que el censo son ellos: el que
# elide, dos Katana que no, y una muestra del parque KOS -- que es donde vive
# la forma `while (!flag);` que motivo la pregunta.
$guests = @(
	@{ n = "ct";       img = (ImagenBanco "ct"); s = 60; teclas = $true },
	@{ n = "vtennis";  img = (ImagenBanco "vt"); s = 60; teclas = $true },
	@{ n = "cvs";      img = "roms\Capcom vs. SNK (USA).cdi"; s = 40; teclas = $true }
)

if (-not $SinDemos) {
	$parque = "C:\dcsdk\tmp\bins"
	foreach ($d in @("pvr-modifier_volume", "2ndmix", "conio-basic",
					 "basic-memtest32", "gldc-nehe-nehe06", "roto")) {
		$p = Join-Path $parque "$d.bin"
		if (Test-Path -LiteralPath $p) {
			$guests += @{ n = "kos-$d"; img = $p; s = 8; teclas = $false }
		}
	}
}

foreach ($g in $guests) {
	if (-not $g.img -or -not (Test-Path -LiteralPath $g.img)) {
		Write-Output "=== $($g.n) SALTEADO: no esta la imagen"
		continue
	}

	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_JIT_OCIOSOS")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($g.teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	}
	$env:DCEMU_JIT = "2"
	$env:DCEMU_RTC_FIJO = "1000000000"

	$p = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$($g.s) --sin-vmu --sin-audio `"$($g.img)`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $p.WaitForExit($TopeMin * 60000)) { $p | Stop-Process -Force; Start-Sleep 2 }
	Start-Sleep 1
	Copy-Item $err "logs\ociosos-censo\$($g.n).txt" -Force -EA SilentlyContinue

	Write-Output "=== $($g.n) ($($g.s) s)"
	# Solo las lineas de la elision: el `jit:   ` con sangria lo usan tambien
	# los otros censos del resumen (plantillas, bloques calientes), y sin
	# anclar al `pc -> pc` de la arista se cuelan cien lineas ajenas.
	Select-String -Path $err -Pattern "instrucciones en .* entradas|elision de ociosos|ociosos, enlaces|ociosos, vueltas|^jit:   [0-9a-f]{8} -> " |
		ForEach-Object { "  " + ($_.Line -replace '^jit: *','') }
}

foreach ($v in @("DCEMU_JIT","DCEMU_RTC_FIJO","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
Write-Output "CENSO-LISTO"
