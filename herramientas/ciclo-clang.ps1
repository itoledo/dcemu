# El ciclo de PGO del binario de clang, en un paso: GEN -> entrenar -> USE.
# Obligatorio tras cada cambio de emision: un brazo sin perfil contra otro con
# perfil mide el perfil, no el cambio.
param([switch] $SoloUse)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

. "$PSScriptRoot\llvm-entorno.ps1" | Out-Null

if (-not $SoloUse) {
    Write-Output "== GEN =="
    cmake -S . -B build-clang -DDCEMU_PGO=GEN 2>&1 | Select-Object -Last 1
    cmake --build build-clang --target dcemu 2>&1 | Select-String -Pattern "error|Linking" | Select-Object -First 3

    Write-Output "== entrenando =="
    & "$PSScriptRoot\pgo.ps1" -Clang
}

Write-Output "== USE =="
cmake -S . -B build-clang -DDCEMU_PGO=USE 2>&1 | Select-Object -Last 1
cmake --build build-clang --target dcemu 2>&1 | Select-String -Pattern "error|Linking" | Select-Object -First 3
Write-Output "canonico: $((Get-FileHash build-clang\dcemu.exe -Algorithm SHA256).Hash.Substring(0,16))"
