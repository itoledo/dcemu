# El ciclo completo del binario del JIT: GEN, entrenar (pgo.ps1 -Jit), USE.
# Para despues de cada cambio de emision: el perfil tiene que corresponder al
# codigo que se mide, o la tanda mide la disposicion (ver el plan).
#
# Corre desde la raiz del repo. Al final imprime el hash del binario, que es
# lo que la tanda vuelve a imprimir: un A/B no se lee sin haberlos comparado.
$ErrorActionPreference = "Stop"

$cmake = Get-Command cmake -EA SilentlyContinue
if (-not $cmake) {
	$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:PATH"
}

cmake -S . -B build-jit -DDCEMU_PGO=GEN | Out-Null
if ($LASTEXITCODE -ne 0) { throw "configure GEN fallo" }
cmake --build build-jit --config Release --target dcemu | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build GEN fallo" }

herramientas\pgo.ps1 -Jit

cmake -S . -B build-jit -DDCEMU_PGO=USE | Out-Null
if ($LASTEXITCODE -ne 0) { throw "configure USE fallo" }
cmake --build build-jit --config Release --target dcemu | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build USE fallo" }

Write-Output "ciclo completo: $((Get-FileHash build-jit\Release\dcemu.exe -Algorithm SHA256).Hash.Substring(0,16))"
