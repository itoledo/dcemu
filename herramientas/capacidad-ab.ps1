# Las dos tandas de la noche, cada una con su palanca y sobre UN binario
# reentrenado:
#
#   capacidad   SR2 a 180 s: omision (65 536) contra DCEMU_JIT_BLOQUES=32768.
#               A 60 s el tope no muerde, asi que el banco es el largo.
#   etiqueta    SR2 a 60 s y DOOM a 35 s: omision (MMU_CACHE_AMBOS) contra
#               DCEMU_MMU_ETIQUETA_MODO=1 (el modo siempre en la etiqueta).
#
# CT no corre: sin MMU y por debajo del tope viejo, los brazos son el mismo
# codigo. Reglas de siempre: huerfanos, primera corrida descartada por guest,
# orden alternado dentro de cada ronda.
param(
    [string] $Exe = "build-clang\dcemu.exe",
    [int] $Rondas = 4
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$sr2  = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"
$doom = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"

function Correr($img, $segundos, $var, $valor)
{
    foreach ($v in @("DCEMU_JIT_BLOQUES","DCEMU_MMU_ETIQUETA_MODO")) {
        if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
    }
    if ($valor -ne "") { Set-Item "Env:\$var" $valor }

    $reloj = [System.Diagnostics.Stopwatch]::StartNew()
    & $Exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
    $reloj.Stop()
    return $reloj.ElapsedMilliseconds
}

$tandas = @(
    @{ n = "capacidad sr2-180"; img = $sr2;  s = 180; var = "DCEMU_JIT_BLOQUES";       valor = "32768" },
    @{ n = "etiqueta sr2-60";   img = $sr2;  s = 60;  var = "DCEMU_MMU_ETIQUETA_MODO"; valor = "1" },
    @{ n = "etiqueta doom-35";  img = $doom; s = 35;  var = "DCEMU_MMU_ETIQUETA_MODO"; valor = "1" }
)

foreach ($t in $tandas) {
    Write-Output "=== $($t.n) (primera corrida descartada)"
    Correr $t.img $t.s $t.var "" | Out-Null

    for ($r = 1; $r -le $Rondas; $r++) {
        $orden = if ($r % 2 -eq 1) { @("nueva","vieja") } else { @("vieja","nueva") }

        foreach ($f in $orden) {
            $ms = Correr $t.img $t.s $t.var $(if ($f -eq "vieja") { $t.valor } else { "" })
            "{0,-6} {1,8} ms" -f $f, $ms
        }
        Write-Output "---"
    }
}

foreach ($v in @("DCEMU_JIT_BLOQUES","DCEMU_MMU_ETIQUETA_MODO")) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}
Write-Output "=== fin"
