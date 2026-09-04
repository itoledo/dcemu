# La compuerta de la elision de lazos ociosos: cuatro brazos sobre UN binario,
# comparados contra el INTERPRETE, que es el arbitro correcto -- lo que la
# elision mueve es cuantas vueltas de un lazo corren de verdad, y la promesa
# es que el corte del bloque periodico caiga en la misma instruccion y el
# total de instrucciones salga al digito.
#
#   interprete = DCEMU_JIT=0
#   apagada    = DCEMU_JIT_OCIOSOS=0 (la emision anterior byte por byte)
#   bumps      = DCEMU_JIT_OCIOSOS=1 (solo la generacion de impureza)
#   entera     = la omision del arbol (bumps + sondas)
#
# Crazy Taxi va bajo replay de mando: su cp es sensible al jitter de XInput y
# el descarte de verdad es el replay, no la reproducibilidad. Se graba una vez
# con DCEMU_GRABAR_MANDO (ver -Mando; la del arbol es herramientas\mando-ct-ociosos.txt,
# 60 s con START en 300 y 1100 y A) y se reemite en los cuatro brazos.
# Los guests cuya imagen no este en roms\ se saltean y se dice.
#
# TRAMPA: para borrar una variable hay que usar Remove-Item "Env:NOMBRE". En
# PowerShell 7, [Environment]::SetEnvironmentVariable($v, $null) NO la borra --
# la deja VACIA-- y getenv() devuelve "" en vez de NULL: para DCEMU_JIT eso es
# atoi("")==0, o sea APAGAR el traductor y comparar interprete contra
# interprete, verde y mudo.
param(
    [string] $Exe = "build-jit\dcemu.exe",
    [string] $Guest = "ct",
    [string] $Mando = "herramientas\mando-ct-ociosos.txt",
    [int] $Segundos = 0
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
. "$PSScriptRoot\banco.ps1"

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

New-Item -ItemType Directory -Force logs\ociosos-gate | Out-Null

function HashLineas($l) {
    $ms = [System.Security.Cryptography.SHA256]::Create()
    [BitConverter]::ToString($ms.ComputeHash([Text.Encoding]::UTF8.GetBytes(
        ($l -join "`n")))).Replace("-","").Substring(0,16)
}

$bancos = @{
    doom = @{ img=(ImagenBanco "doom"); s=35; replay=$false }
    sr2  = @{ img=(ImagenBanco "sr2");  s=60; replay=$false }
    ct   = @{ img=(ImagenBanco "ct");   s=60; replay=$true }
}

$j = $bancos[$Guest]
if (-not $j) { throw "guest desconocido: $Guest" }
if (-not $j.img) { Write-Output "$Guest SALTEADO: no esta la imagen (ver herramientas/banco.ps1)"; exit 0 }
Write-Output "imagen: $($j.img)"
if ($j.replay -and -not (Test-Path $Mando)) { throw "falta la receta de mando $Mando (grabar con DCEMU_GRABAR_MANDO)" }
if ($Segundos -gt 0) { $j.s = $Segundos }

$res = @{}
$fallas = 0

foreach ($brazo in @("interprete","apagada","bumps","entera")) {
    foreach ($v in @("DCEMU_JIT","DCEMU_MANDO","DCEMU_JIT_OCIOSOS","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
    if ($brazo -eq "interprete") { $env:DCEMU_JIT = "0" }
    if ($brazo -eq "apagada")    { $env:DCEMU_JIT_OCIOSOS = "0" }
    if ($brazo -eq "bumps")      { $env:DCEMU_JIT_OCIOSOS = "1" }
    if ($j.replay) { $env:DCEMU_MANDO = $Mando }
    $env:DCEMU_CP_MS = [string]($j.s * 1000)

    # El RTC clavado en TODOS los brazos, que es la regla del expediente de
    # THPS2 y de la que esta compuerta carecia: un juego tambien lee el reloj
    # a mitad de corrida, y el RTC sigue al anfitrion. Los brazos corren con
    # minutos de diferencia, asi que sin esto la compuerta compara dos relojes
    # distintos -- y salio verde tres veces por suerte antes de que Crazy Taxi
    # divergiera 1852 instrucciones en un brazo con la captura intacta.
    $env:DCEMU_RTC_FIJO = "1000000000"

    $bmp = "logs\ociosos-gate\$Guest-$brazo.bmp"
    & $Exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
    Remove-Item "Env:DCEMU_CP_MS" -ErrorAction SilentlyContinue

    $sal = Get-Content $err
    $cp  = $sal | Where-Object { $_ -match '^cp ' }
    $res[$brazo] = @{
        bmp = (Get-FileHash $bmp).Hash.Substring(0,16)
        cph = (HashLineas $cp); cpn = $cp.Count
        ctl = (($sal | Where-Object { $_ -match 'elision de ociosos' }) -join "; ")
        ent = (($sal | Where-Object { $_ -match 'instrucciones en .* entradas' }) -join "; ")
    }
    Copy-Item $err "logs\ociosos-gate\$Guest-$brazo.txt"
}

foreach ($brazo in @("apagada","bumps","entera")) {
    $ok  = ($res[$brazo].bmp -eq $res.interprete.bmp)
    $okc = ($res[$brazo].cph -eq $res.interprete.cph -and
            $res[$brazo].cpn -eq $res.interprete.cpn)
    if (-not ($ok -and $okc)) { $fallas++ }
    Write-Output ("$Guest $brazo vs interprete: captura " +
        $(if ($ok) {"IGUAL"} else {"DISTINTA $($res[$brazo].bmp) vs $($res.interprete.bmp)"}) +
        ", cp " + $(if ($okc) {"IGUAL ($($res.interprete.cpn) puntos)"}
                    else {"DISTINTO $($res[$brazo].cpn)/$($res[$brazo].cph) vs $($res.interprete.cpn)/$($res.interprete.cph)"}))
}

# El control. La linea del resumen solo existe si el traductor CORRIO en ese
# brazo, y la de la elision dice en que posicion corrio la palanca: un brazo
# "entera" con cero sondas es un talon que no corrio, no un guest sin lazos.
foreach ($brazo in @("apagada","bumps","entera")) {
    Write-Output ("$Guest control ${brazo}: " + ($res[$brazo].ctl -replace '^jit: ',''))
    Write-Output ("$Guest cuenta ${brazo}:   " + ($res[$brazo].ent -replace '^jit: ',''))
    if (-not $res[$brazo].ent) {
        $fallas++; Write-Output "$Guest CONTROL ROTO: el brazo $brazo no corrio el traductor"
    }
}
if ($res.interprete.ent) {
    $fallas++; Write-Output "$Guest CONTROL ROTO: el brazo del interprete corrio el traductor"
}
if ($res.entera.ctl -notmatch 'entera') {
    $fallas++; Write-Output "$Guest CONTROL ROTO: el brazo entera no corrio con la palanca entera"
}

foreach ($v in @("DCEMU_JIT","DCEMU_MANDO","DCEMU_CP_MS","DCEMU_JIT_OCIOSOS","DCEMU_RTC_FIJO")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

if ($fallas) { Write-Output "COMPUERTA ROJA ($Guest): $fallas" }
else         { Write-Output "COMPUERTA VERDE ($Guest)" }
