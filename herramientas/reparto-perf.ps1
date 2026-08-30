# El reparto de hoy: --perf sobre los tres guests del banco, un binario, una
# corrida por guest con calentamiento descartado. Es el paso 0 de cualquier
# fase de hilos: dice cuanto vale mover el AICA/ARM7 y cuanto el render.
param(
    [string] $Exe = "build-clang\dcemu.exe"
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$doom = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"
$ct   = "roms\Crazy Taxi (USA).cdi"
$sr2  = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"

New-Item -ItemType Directory -Force logs\reparto | Out-Null

function Limpiar-Teclas {
    foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
        if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
    }
}

# calentamiento, descartado
& $Exe --salir-tras=20 --sin-vmu --perf $doom | Out-Null

foreach ($j in @(
    @{n="doom"; img=$doom; s=35;  k=$false},
    @{n="ct";   img=$ct;   s=180; k=$true},
    @{n="sr2";  img=$sr2;  s=60;  k=$false}))
{
    Limpiar-Teclas
    if ($j.k) {
        $env:DCEMU_PULSAR_START = "300,1100"
        $env:DCEMU_PULSAR_A = "1"
        $env:DCEMU_SOLO_A = "1"
    }

    $reloj = [System.Diagnostics.Stopwatch]::StartNew()
    & $Exe "--salir-tras=$($j.s)" --sin-vmu --perf $j.img | Out-Null
    $reloj.Stop()
    Copy-Item $err "logs\reparto\perf-$($j.n).txt" -Force
    Write-Output "$($j.n): $($reloj.ElapsedMilliseconds) ms reales / $($j.s) s emulados"
}

Limpiar-Teclas
Write-Output "listo: logs\reparto"
