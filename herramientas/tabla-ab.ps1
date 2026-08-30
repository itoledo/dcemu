# La tanda de la tabla de bloques: dos brazos sobre UN binario.
#
#   nueva   sondeo 32 + reuso de lapidas + desmarcar al fallar (la omision)
#   vieja   DCEMU_JIT_TABLA_VIEJA=1, la conducta anterior con su fuga
#
# La emision no cambia con nada de esto, asi que los absolutos comparan.
# El efecto esperado esta concentrado en SR2 (el unico guest que chocaba el
# tope: 12 904 ranuras quemadas y el traductor muerto a mitad de corrida);
# DOOM y CT tenian 0 sin-lugar y van de testigos de neutralidad.
#
# Reglas de siempre: matar huerfanos, descartar la primera corrida de cada
# guest, rotar el orden dentro de cada ronda, sin audio capturado ni captura.
param(
    [string] $Exe = "build-clang\dcemu.exe",
    [int] $Rondas = 4
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$bancos = @(
    @{ n = "sr2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false },
    @{ n = "dcdoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
    @{ n = "crazytaxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true }
)

function Correr($img, $segundos, $teclas, $forma)
{
    if ($teclas) {
        $env:DCEMU_PULSAR_START = "300,1100"
        $env:DCEMU_PULSAR_A = "1"
        $env:DCEMU_SOLO_A = "1"
    } else {
        Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
    }

    if ($forma -eq "vieja") {
        $env:DCEMU_JIT_TABLA_VIEJA = "1"
    } else {
        Remove-Item env:DCEMU_JIT_TABLA_VIEJA -EA SilentlyContinue
    }

    $reloj = [System.Diagnostics.Stopwatch]::StartNew()
    & $Exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
    $reloj.Stop()

    # Control de trabajo: la cobertura y los sin-lugar por corrida.
    $cob = (Select-String -Path $err -Pattern "^jit: \d+ bloques traducidos" |
            ForEach-Object { ($_.Line -split ",")[0..1] -join "," }) -join " "
    $sl = (Select-String -Path $err -Pattern "^jit: sin lugar:" |
            ForEach-Object { ($_.Line -split ";")[0..1] -join ";" }) -join " "

    return @{ ms = $reloj.ElapsedMilliseconds; cob = $cob; sl = $sl }
}

foreach ($b in $bancos) {
    Write-Output "=== $($b.n), $($b.s) s emulados (primera corrida descartada)"
    Correr $b.img $b.s $b.teclas "nueva" | Out-Null

    for ($r = 1; $r -le $Rondas; $r++) {
        $orden = if ($r % 2 -eq 1) { @("nueva", "vieja") } else { @("vieja", "nueva") }

        foreach ($f in $orden) {
            $x = Correr $b.img $b.s $b.teclas $f
            "{0,-6} {1,8} ms   {2}   {3}" -f $f, $x.ms, $x.cob, $x.sl
        }
        Write-Output "---"
    }
}

Remove-Item env:DCEMU_JIT_TABLA_VIEJA -EA SilentlyContinue
Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
