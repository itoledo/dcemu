# banco.ps1 -- las rutas del banco que cambian de una maquina a otra, resueltas
# en un solo lugar. Se puntea:
#
#   . "$PSScriptRoot\banco.ps1"
#
# Dos maquinas corren este arbol y no comparten disco. LLVM 22.1.8 vive en
# E:\llvm en la maquina del banco original y en C:\llvm en la segunda; el trio
# CHD en E:\Juegos\roms\dreamcast en una y en roms\chd en la otra; Sega Rally 2
# es el .gdi v1.003 en una y el .chd de redump en la otra. Un guion con la ruta
# escrita adentro corre en una sola maquina y en la otra dice "no esta la
# imagen" -- que se lee como "ese guest no tiene banco aqui" y no lo es.
#
# Cada resolvedor prueba una lista corta de candidatos en orden y devuelve el
# primero que exista. Las variables DCEMU_LLVM y DCEMU_CHD van primero para
# poder apuntar a otro lado sin tocar el arbol.
#
# Regla de lectura que no cambia: un .chd y un .gdi del mismo juego son el
# mismo guest para la compuerta (capturas byte a byte, ver notas-gdrom.md)
# pero NO para el cronometro entre maquinas -- el .chd paga la descompresion
# de sus hunks. Como los absolutos solo se comparan dentro de un binario y de
# una maquina, eso no cambia ninguna regla; solo hay que saberlo al leer una
# marca de SR2 de aqui contra una de alla.
#
# TRAMPA: Test-Path a secas expande comodines, y "[!]" es una clase de
# caracteres que casa con "!" -- asi que el .gdi de Sega Rally 2 y el .cdi de
# Virtua Tennis ([cr DCRES]) NO existen para Test-Path aunque esten. Aqui se
# usa -LiteralPath siempre; un guion que pregunte por su cuenta tiene que
# hacer lo mismo (perfil-pmu.ps1 ya lo pago).

function LlvmRaiz {
	$candidatos = @()
	if ($env:DCEMU_LLVM) { $candidatos += $env:DCEMU_LLVM }
	$candidatos += "C:\llvm\22.1.8", "E:\llvm\22.1.8"
	foreach ($c in $candidatos) {
		if (Test-Path -LiteralPath "$c\bin\clang-cl.exe") { return $c }
	}
	throw "No hay LLVM 22.1.8 en ninguno de: $($candidatos -join ', '). Ver docs/jit-sota-plan.md, fase G paso 0 (DCEMU_LLVM apunta a otra raiz)."
}

function ChdRaiz {
	$candidatos = @()
	if ($env:DCEMU_CHD) { $candidatos += $env:DCEMU_CHD }
	$candidatos += "E:\Juegos\roms\dreamcast", "roms\chd"
	foreach ($c in $candidatos) {
		if (Test-Path -LiteralPath $c) { return $c }
	}
	return $null
}

# Las imagenes del banco por nombre corto (doom, ct, sr2, vt). Devuelve la
# ruta o $null; el que llama decide si saltear el guest o parar -- una
# compuerta saltea y lo dice, el entrenamiento de PGO tiene que parar, porque
# un perfil al que le falta un guest describe otro programa.
function ImagenBanco([string] $nombre) {
	$chd = ChdRaiz
	$lista = switch ($nombre) {
		"doom" { @("roms\DCDoom GDI and CDI\DCDoom CDI.cdi") }
		"ct"   { @("roms\Crazy Taxi (USA).cdi") }
		"sr2"  {
			$l = @("roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi")
			if ($chd) { $l += "$chd\Sega Rally 2 (USA).chd" }
			$l
		}
		"vt"   { @("roms\Virtua Tennis (2000)(Sega)(US)[cr DCRES][f PAL 60Hz][repack].cdi") }
		default { throw "imagen desconocida: $nombre" }
	}
	foreach ($p in $lista) {
		if (Test-Path -LiteralPath $p) { return $p }
	}
	return $null
}

# Un .chd por su nombre redump, sin extension. Devuelve la ruta o $null.
function ImagenChd([string] $nombre) {
	$chd = ChdRaiz
	if (-not $chd) { return $null }
	$p = Join-Path $chd "$nombre.chd"
	if (Test-Path -LiteralPath $p) { return $p }
	return $null
}

# El ejecutable de un directorio de compilacion: los generadores de Visual
# Studio lo dejan en Release\, NMake y Ninja en la raiz del directorio.
function ExeBanco([string] $dir) {
	foreach ($p in @("$dir\Release\dcemu.exe", "$dir\dcemu.exe")) {
		if (Test-Path -LiteralPath $p) { return $p }
	}
	return "$dir\dcemu.exe"
}
