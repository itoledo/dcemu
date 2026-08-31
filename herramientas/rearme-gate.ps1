# La compuerta del rearme condicional de la entrega: dos brazos sobre UN
# binario (omision / DCEMU_SR_REARME_SIEMPRE=1) comparando la LISTA DE
# ENTREGAS (DCEMU_SONDA_ENTREGAS=1: reloj_total + INTEVT + PC por entrega,
# el unico calendario que el rearme puede mover) mas la captura al salir.
#
# Los cp por ms NO sirven de arbitro aqui: muestrean en las entradas del
# bloque periodico y el rearme cambia cuales entradas existen -- moveria el
# instante del muestreo con el estado del guest intacto.
#
# CT corre bajo el replay de mando (la leccion del expediente del pad): su
# lista de entregas depende del camino del guest, que el pad mueve.
param(
    [string] $Exe = "build-clang\dcemu.exe"
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

New-Item -ItemType Directory -Force logs\rearme-gate | Out-Null

function HashLineas($l) {
    $ms = [System.Security.Cryptography.SHA256]::Create()
    [BitConverter]::ToString($ms.ComputeHash([Text.Encoding]::UTF8.GetBytes(
        ($l -join "`n")))).Replace("-","").Substring(0,16)
}

foreach ($j in @(
    @{n="doom"; img="roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s=35; replay=$false},
    @{n="sr2";  img="roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s=60; replay=$false},
    @{n="ct";   img="roms\Crazy Taxi (USA).cdi"; s=180; replay=$true}))
{
    $res = @{}

    foreach ($brazo in @("omision","siempre")) {
        foreach ($v in @("DCEMU_SR_REARME_SIEMPRE","DCEMU_MANDO")) {
            if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
        }
        if ($brazo -eq "siempre") { $env:DCEMU_SR_REARME_SIEMPRE = "1" }
        if ($j.replay) { $env:DCEMU_MANDO = "logs\tabla-gate\ct-mando.txt" }
        $env:DCEMU_SONDA_ENTREGAS = "1"

        $bmp = "logs\rearme-gate\$($j.n)-$brazo.bmp"
        & $Exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
        Remove-Item Env:\DCEMU_SONDA_ENTREGAS

        $ent = Get-Content $err | Where-Object { $_ -match '^entrega ' }
        $res[$brazo] = @{
            bmp = (Get-FileHash $bmp).Hash.Substring(0,16)
            enh = (HashLineas $ent); enn = $ent.Count
        }
    }

    $ok  = ($res.omision.bmp -eq $res.siempre.bmp)
    $oke = ($res.omision.enh -eq $res.siempre.enh -and $res.omision.enn -eq $res.siempre.enn)
    Write-Output ("$($j.n): captura " +
        $(if ($ok) {"IGUAL"} else {"DISTINTA $($res.omision.bmp) vs $($res.siempre.bmp)"}) +
        ", entregas " + $(if ($oke) {"IGUALES ($($res.omision.enn))"}
                          else {"DISTINTAS $($res.omision.enn)/$($res.omision.enh) vs $($res.siempre.enn)/$($res.siempre.enh)"}))
}

foreach ($v in @("DCEMU_SR_REARME_SIEMPRE","DCEMU_MANDO","DCEMU_SONDA_ENTREGAS")) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}
Write-Output "listo: logs\rearme-gate"
