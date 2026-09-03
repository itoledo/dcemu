# La compuerta del contrato por instruccion: tres brazos sobre UN binario,
# comparados contra el INTERPRETE, que es el arbitro correcto aqui -- lo que
# estas fases mueven es donde y como se vuelca el estado antes de una falta y
# donde se corta el bloque periodico, o sea justo lo que separa al traductor
# del intervalo por instruccion del interprete.
#
#   interprete = DCEMU_JIT=0
#   base       = las dos palancas viejas (la emision anterior byte por byte)
#   ambas      = la omision del arbol (sync en el talon + corte en una)
#
# Se corre de a un guest (-Guest doom|sr2|ct) para no chocar con los topes de
# tiempo del entorno. CT va bajo replay de mando: su cp es sensible al jitter
# de XInput y el descarte de verdad es el replay, no la reproducibilidad.
# TRAMPA: para borrar una variable hay que usar Remove-Item "Env:NOMBRE". En
# PowerShell 7, [Environment]::SetEnvironmentVariable($v, $null) NO la borra --
# la deja VACIA-- y getenv() devuelve "" en vez de NULL. Para DCEMU_JIT eso
# significa atoi("")==0, o sea APAGAR el traductor: la compuerta compara
# interprete contra interprete, sale verde y no prueba nada. Es la misma trampa
# que la adopcion ya cobro una vez, con otra cara.
param(
    [string] $Exe = "build-clang\dcemu.exe",
    [string] $Guest = "doom",
    # El brazo "base": que palancas lo definen. Por omision, las dos del
    # contrato por instruccion; con -Base "DCEMU_JIT_SIN_DIV1=1" la misma
    # compuerta sirve para cualquier escalon nuevo.
    [string] $Base = "DCEMU_JIT_SYNC_PREVIA=1,DCEMU_JIT_CORTE_VIEJO=1"
)

$pares = @($Base -split "," | Where-Object { $_ } | ForEach-Object {
    $kv = $_ -split "=", 2
    @{ n = $kv[0].Trim(); v = $kv[1].Trim() }
})

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

New-Item -ItemType Directory -Force logs\contrato-gate | Out-Null

function HashLineas($l) {
    $ms = [System.Security.Cryptography.SHA256]::Create()
    [BitConverter]::ToString($ms.ComputeHash([Text.Encoding]::UTF8.GetBytes(
        ($l -join "`n")))).Replace("-","").Substring(0,16)
}

$bancos = @{
    doom = @{ img="roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s=35; replay=$false }
    sr2  = @{ img="roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s=60; replay=$false }
    ct   = @{ img="roms\Crazy Taxi (USA).cdi"; s=60; replay=$true }
}

$j = $bancos[$Guest]
if (-not $j) { throw "guest desconocido: $Guest" }

$res = @{}
$fallas = 0

foreach ($brazo in @("interprete","base","ambas")) {
    # Borrar de verdad, no poner cadena vacia: getenv() la devuelve NO nula y
    # atoi("") vale 0, asi que $env:DCEMU_JIT = "" APAGA el traductor y deja la
    # compuerta comparando interprete contra interprete -- verde y muda.
    foreach ($v in @("DCEMU_JIT","DCEMU_MANDO") + ($pares | ForEach-Object { $_.n })) {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
    if ($brazo -eq "interprete") { $env:DCEMU_JIT = "0" }
    if ($brazo -eq "base") {
        foreach ($p in $pares) { Set-Item "Env:$($p.n)" $p.v }
    }
    if ($j.replay) { $env:DCEMU_MANDO = "logs\tabla-gate\ct-mando.txt" }
    $env:DCEMU_CP_MS = [string]($j.s * 1000)

    $bmp = "logs\contrato-gate\$Guest-$brazo.bmp"
    & $Exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
    Remove-Item "Env:DCEMU_CP_MS" -ErrorAction SilentlyContinue

    $sal = Get-Content $err
    $cp  = $sal | Where-Object { $_ -match '^cp ' }
    $res[$brazo] = @{
        bmp = (Get-FileHash $bmp).Hash.Substring(0,16)
        cph = (HashLineas $cp); cpn = $cp.Count
        ctl = (($sal | Where-Object { $_ -match 'filas con acceso sin sitio' }) -join "; ")
        ent = (($sal | Where-Object { $_ -match 'instrucciones en .* entradas' }) -join "; ")
    }
    Copy-Item $err "logs\contrato-gate\$Guest-$brazo.txt"
}

foreach ($brazo in @("base","ambas")) {
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
# brazo: un brazo mudo aqui quiere decir que se comparo interprete contra
# interprete, que es verde por construccion y no prueba nada.
Write-Output ("$Guest control base:  " + ($res.base.ctl  -replace '^jit: ',''))
Write-Output ("$Guest control ambas: " + ($res.ambas.ctl -replace '^jit: ',''))
if (-not $res.base.ent -or -not $res.ambas.ent) {
    $fallas++; Write-Output "$Guest CONTROL ROTO: algun brazo no corrio el traductor"
}
if ($res.interprete.ent) {
    $fallas++; Write-Output "$Guest CONTROL ROTO: el brazo del interprete corrio el traductor"
}

foreach ($v in @("DCEMU_JIT","DCEMU_MANDO","DCEMU_CP_MS") + ($pares | ForEach-Object { $_.n })) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

if ($fallas) { Write-Output "COMPUERTA ROJA ($Guest): $fallas" }
else         { Write-Output "COMPUERTA VERDE ($Guest)" }
