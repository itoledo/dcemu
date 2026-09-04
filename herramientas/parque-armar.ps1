# parque-armar.ps1 -- arma el parque de demos KOS que consume barrido.ps1.
#
# El parque siempre fue un directorio suelto (`C:\dcsdk\tmp\bins`) que existia
# en UNA maquina y cuya procedencia no estaba escrita en ningun lado: el dia
# que hubo que correr el barrido en la segunda maquina no habia forma de
# reconstruirlo. Esto es esa forma.
#
# Que hace: por cada `.elf` construido bajo `kos/examples/dreamcast`, emite el
# binario crudo con `sh-elf-objcopy -O binary` (la regla `%.bin: %.elf` de
# `Makefile.rules` de KOS, o sea el mismo archivo que produce un `make bin`).
#
# El nombre es **la ruta del directorio bajo examples/dreamcast con las barras
# cambiadas por guiones**, que es la convencion que ya usan docs/demos-kos.md y
# los resumenes del barrido: `pvr/bumpmap/bump.elf` -> `pvr-bumpmap.bin`,
# `basic/dma/speedtest/speedtest.elf` -> `basic-dma-speedtest.bin`,
# `sound/cdda/basic_cdda/*.elf` -> `sound-cdda-basic_cdda.bin`. El nombre del
# .elf no entra: mas de la mitad no coincide con el de su directorio.
#
# El unico directorio con dos .elf es `basic/exec` (exec.elf y sub.elf, que el
# primero carga): con varios gana el que se llama como el directorio, y si
# ninguno coincide se emiten todos con el nombre del .elf pegado detras -- y se
# dice cuales, porque un parque con una demo de mas no es comparable contra uno
# que no la tiene.
#
# No compila nada: los ejemplos se construyen con el toolchain de KOS
# (`make -C $Kos/examples/dreamcast`), y aqui se recoge lo que haya. Los que no
# se construyeron no aparecen, y el guion dice cuantos vio.
param(
	[string] $Kos     = "C:\dcsdk\opt\toolchains\dc\kos",
	[string] $Salida  = "C:\dcsdk\tmp\bins",
	[string] $Objcopy = "C:\dcsdk\opt\toolchains\dc\sh-elf\bin\sh-elf-objcopy.exe",
	# El binutils de KOS esta enlazado contra el runtime de MSYS2 y NO arranca
	# fuera de su shell: falla con 0xC0000135 (-1073741515), "DLL no encontrada",
	# que PowerShell muestra solo como un codigo de salida raro. De los tres
	# entornos que trae el SDK sirve mingw64; usr\bin y clang64\bin no.
	[string] $Dlls    = "",
	# demos/roto es del arbol de dcemu, no de KOS, y entra al parque igual
	# (su .bin va versionado justamente para no necesitar el SDK).
	[string] $Roto    = "",
	[switch] $Limpiar
)

$ErrorActionPreference = "Stop"

$ejemplos = Join-Path $Kos "examples\dreamcast"
if (-not (Test-Path -LiteralPath $ejemplos)) { throw "no encuentro $ejemplos" }
if (-not (Test-Path -LiteralPath $Objcopy))  { throw "no encuentro $Objcopy" }

if ($Dlls -eq "") {
	# .../dc/kos -> .../dc -> toolchains -> opt -> la raiz del SDK
	$raizSdk = Split-Path (Split-Path (Split-Path (Split-Path $Kos -Parent) -Parent) -Parent) -Parent
	$Dlls = Join-Path $raizSdk "mingw64\bin"
}
if (Test-Path -LiteralPath $Dlls) { $env:PATH = "$Dlls;$env:PATH" }

& $Objcopy --version > $null 2>&1
if ($LASTEXITCODE -ne 0) {
	throw "objcopy no arranca (salida $LASTEXITCODE). Si es -1073741515 le faltan las DLL de MSYS2: pasa -Dlls con el bin que las tenga (mingw64\bin en el SDK de KOS)."
}

if ($Limpiar -and (Test-Path -LiteralPath $Salida)) {
	Get-ChildItem -LiteralPath $Salida -Filter *.bin | Remove-Item -Force
}
New-Item -ItemType Directory -Force $Salida | Out-Null

$elfs = Get-ChildItem -LiteralPath $ejemplos -Filter *.elf -Recurse -File
Write-Output "$($elfs.Count) .elf construidos bajo $ejemplos"

# Agrupar por directorio: el nombre del parque es del directorio, no del .elf.
$porDir = $elfs | Group-Object { $_.DirectoryName }
$hechos = 0
$avisos = @()

foreach ($g in $porDir) {
	$rel = $g.Name.Substring($ejemplos.Length).Trim('\')
	$base = $rel -replace '[\\/]', '-'

	$elegidos = @()
	if ($g.Count -eq 1) {
		$elegidos += @{ elf = $g.Group[0]; nombre = $base }
	} else {
		$hoja = Split-Path $g.Name -Leaf
		$principal = $g.Group | Where-Object { $_.BaseName -eq $hoja }
		if ($principal) {
			$elegidos += @{ elf = $principal[0]; nombre = $base }
			$otros = ($g.Group | Where-Object { $_.BaseName -ne $hoja } | ForEach-Object { $_.Name }) -join ", "
			$avisos += "$base tiene $($g.Count) .elf; se toma $($principal[0].Name) y se dejan fuera: $otros"
		} else {
			foreach ($e in $g.Group) { $elegidos += @{ elf = $e; nombre = "$base-$($e.BaseName)" } }
			$avisos += "$base tiene $($g.Count) .elf y ninguno se llama como el directorio: van todos con el nombre del .elf"
		}
	}

	foreach ($x in $elegidos) {
		$destino = Join-Path $Salida "$($x.nombre).bin"
		& $Objcopy -O binary $x.elf.FullName $destino
		if ($LASTEXITCODE -ne 0) { throw "objcopy fallo en $($x.elf.FullName)" }
		$hechos++
	}
}

if ($Roto -eq "") {
	$candidato = Join-Path (Split-Path -Parent $PSScriptRoot) "demos\roto\roto.bin"
	if (Test-Path -LiteralPath $candidato) { $Roto = $candidato }
}
if ($Roto -ne "" -and (Test-Path -LiteralPath $Roto)) {
	Copy-Item -LiteralPath $Roto (Join-Path $Salida "roto.bin") -Force
	$hechos++
	Write-Output "roto.bin copiado desde $Roto"
}

foreach ($a in $avisos) { Write-Output "aviso: $a" }
Write-Output "parque: $hechos binarios en $Salida"
