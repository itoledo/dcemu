# llvm-entorno.ps1 -- deja clang/LLVM y el toolchain de MSVC en la sesion.
#
#   . herramientas\llvm-entorno.ps1            # hay que puntearlo: modifica $env:
#   . herramientas\llvm-entorno.ps1 -Verificar # ademas corre las tres pruebas
#
# Por que hace falta un script y no basta agregar el bin al PATH: clang-cl no
# trae cabeceras ni bibliotecas del sistema, las toma del MSVC instalado, y para
# encontrarlo pregunta a vswhere por la instalacion mas nueva. **En esta maquina
# esa pregunta contesta SQL Server Management Studio 22** -- que es la cascara de
# Visual Studio y tiene version 18.9.0, mas alta que la de VS 2022 -- que no
# trae compilador. El sintoma es "'stdio.h' file not found" compilando un hola
# mundo, que se lee como una instalacion rota de LLVM y no lo es.
#
# El arreglo son las dos mitades de abajo: pedirle a vswhere la instalacion que
# **tenga el componente de C++** (-requires), y entrar al entorno de VS con
# vcvars64, que fija INCLUDE/LIB y con eso clang-cl ya no pregunta nada.
#
# LLVM vive fuera de Program Files y fuera del arbol de VS a proposito: el
# toolset que trae Visual Studio ya perdio llvm-objdump y llvm-objcopy en un
# update, que es lo que dejo rota la receta de DCEMU_JIT_VOLCADO. Este es un
# arbol extraido del instalador oficial, sin registro ni desinstalador, y se
# borra con un rm -rf.
param(
	[string] $Raiz = "E:\llvm\22.1.8",
	[switch] $Verificar
)

if (-not (Test-Path "$Raiz\bin\clang-cl.exe")) {
	throw "No hay clang-cl en $Raiz\bin. Ver el paso 0 de docs/jit-sota-plan.md."
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * `
	-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
	-property installationPath
if (-not $vs) { throw "vswhere no encontro una instalacion de VS con el toolset de C++." }

# vcvars64 es un .bat: se corre en cmd y se cosecha el entorno que dejo.
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
	if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

$env:PATH = "$Raiz\bin;$env:PATH"
Write-Host "LLVM: $Raiz  ($((& "$Raiz\bin\clang-cl.exe" --version | Select-Object -First 1)))"
Write-Host "MSVC: $vs"

if ($Verificar) {
	$t = Join-Path $env:TEMP "llvm-verificar"
	New-Item -ItemType Directory -Force $t | Out-Null
	Push-Location $t
	try {
		# 1. clang-cl compila y enlaza contra el MSVC recien importado.
		'#include <stdio.h>
int main(void){ printf("ok\n"); return 0; }' | Set-Content h.c
		& clang-cl /nologo /O2 h.c /Fe:h.exe | Out-Null
		Write-Host "1. clang-cl: $(if ((& .\h.exe) -eq 'ok') { 'OK' } else { 'FALLA' })"

		# 2. la receta de DCEMU_JIT_VOLCADO: envolver el crudo y desensamblarlo.
		[IO.File]::WriteAllBytes("$t\v.bin", [byte[]] @(0x48,0x89,0xd8,0xc3))
		& llvm-objcopy -I binary -O elf64-x86-64 v.bin v.o
		$d = & llvm-objdump -D --triple=x86_64 --section=.data v.o
		Write-Host "2. objcopy+objdump: $(if ($d -match 'movq\s+%rbx, %rax') { 'OK' } else { 'FALLA' })"

		# 3. el segundo oraculo de tests/test_jit_x64.c. El instalador oficial no
		#    trae llvm-mc (nunca se instala, es herramienta de test), pero el
		#    ensamblador integrado de clang es la misma capa MC.
		"mov %rbx, %rax`nret" | Set-Content a.s
		& clang -c -x assembler a.s -o a.o
		$e = (& llvm-objdump -d a.o) -join "`n"
		Write-Host "3. ensamblador:      $(if ($e -match '48 89 d8') { 'OK' } else { 'FALLA' })"
	} finally { Pop-Location }
}
