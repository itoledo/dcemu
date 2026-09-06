# La compuerta de exactitud de la fase 1 de hilos, sobre el binario de hoy:
# el AICA/ARM7 en su hilo no puede mover NI un byte de salida. Por guest:
# captura final identica, y en CT ademas el .wav byte a byte -- la prueba de
# aceptacion original de la fase (docs/hilos-plan.md).
param(
    [string] $Exe = "build-clang\dcemu.exe"
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }

$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
$doom = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"
$ct   = "roms\Crazy Taxi (USA).cdi"
$sr2  = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"

$d = "logs\hilos-gate"
New-Item -ItemType Directory -Force $d | Out-Null
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

function Limpiar-Teclas {
    foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
        if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
    }
}

foreach ($j in @(
    @{n="ct";   img=$ct;   s=60; k=$true;  wav=$true},
    @{n="doom"; img=$doom; s=25; k=$false; wav=$false},
    @{n="sr2";  img=$sr2;  s=40; k=$false; wav=$true}))
{
    foreach ($brazo in @("sin","con")) {
        Limpiar-Teclas
        if ($j.k) {
            $env:DCEMU_PULSAR_START = "300,1100"
            $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
        }
        $args = @($j.img, "--salir-tras=$($j.s)", "--sin-vmu", "--sin-audio",
                  "--captura-gl=$d\$($j.n)-$brazo.bmp")
        if ($j.wav) { $args += "--captura-audio=$d\$($j.n)-$brazo.wav" }
        # Desde la adopcion (2026-09-05) la omision es con hilos: el brazo de
        # control lo dice explicito, o la compuerta compara hilos contra hilos.
        $args += $(if ($brazo -eq "con") { "--hilos" } else { "--sin-hilos" })

        & $Exe @args | Out-Null
        Copy-Item $err "$d\stderr-$($j.n)-$brazo.txt" -Force
    }

    $hb = (Get-FileHash "$d\$($j.n)-sin.bmp").Hash
    $hc = (Get-FileHash "$d\$($j.n)-con.bmp").Hash
    $vb = if ($j.wav) { (Get-FileHash "$d\$($j.n)-sin.wav").Hash } else { "-" }
    $vc = if ($j.wav) { (Get-FileHash "$d\$($j.n)-con.wav").Hash } else { "-" }

    $bmpOk = if ($hb -eq $hc) { "IGUAL" } else { "DISTINTA" }
    $wavOk = if ($vb -eq $vc) { "IGUAL" } else { "DISTINTO" }
    Write-Output ("{0}: captura {1} ({2}) wav {3} ({4})" -f $j.n, $bmpOk,
        $hb.Substring(0,8), $wavOk, $(if ($vb -ne "-") { $vb.Substring(0,8) } else { "-" }))
}
Limpiar-Teclas
Write-Output "listo"
