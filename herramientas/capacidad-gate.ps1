# La compuerta de la capacidad de bloques y de la etiqueta sin modo: tres
# brazos sobre UN binario (omision / DCEMU_JIT_BLOQUES=32768 /
# DCEMU_MMU_ETIQUETA_MODO=1), capturas al salir y DCEMU_CP_MS de la corrida
# entera comparados contra el brazo de omision. SR2 va a 180 s porque el tope
# de bloques recien muerde ahi; CT no entra: sin MMU y con menos bloques que
# el tope viejo, los dos brazos son el mismo codigo (y su cp es sensible al
# pad -- ver el expediente del replay en recompilador-plan.md).
param(
    [string] $Exe = "build-clang\dcemu.exe"
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

if (Get-Process dcemu -EA SilentlyContinue) { throw "dcemu corriendo" }
$err = Join-Path (Split-Path -Parent $Exe) "stderr.txt"
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

New-Item -ItemType Directory -Force logs\capacidad-gate | Out-Null

function HashLineas($l) {
    $ms = [System.Security.Cryptography.SHA256]::Create()
    [BitConverter]::ToString($ms.ComputeHash([Text.Encoding]::UTF8.GetBytes(
        ($l -join "`n")))).Replace("-","").Substring(0,16)
}

foreach ($j in @(
    @{n="sr2";  img="roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s=180},
    @{n="doom"; img="roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s=35}))
{
    $res = @{}

    foreach ($brazo in @("omision","bloques","etiqueta")) {
        foreach ($v in @("DCEMU_JIT_BLOQUES","DCEMU_MMU_ETIQUETA_MODO")) {
            if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
        }
        if ($brazo -eq "bloques")  { $env:DCEMU_JIT_BLOQUES = "32768" }
        if ($brazo -eq "etiqueta") { $env:DCEMU_MMU_ETIQUETA_MODO = "1" }
        $env:DCEMU_CP_MS = [string]($j.s * 1000)

        $bmp = "logs\capacidad-gate\$($j.n)-$brazo.bmp"
        & $Exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
        Remove-Item Env:\DCEMU_CP_MS

        $cp = Get-Content $err | Where-Object { $_ -match '^cp ' }
        $res[$brazo] = @{
            bmp = (Get-FileHash $bmp).Hash.Substring(0,16)
            cph = (HashLineas $cp); cpn = $cp.Count
        }
    }

    foreach ($brazo in @("bloques","etiqueta")) {
        $ok  = ($res[$brazo].bmp -eq $res.omision.bmp)
        $okc = ($res[$brazo].cph -eq $res.omision.cph -and $res[$brazo].cpn -eq $res.omision.cpn)
        Write-Output ("$($j.n) $brazo vs omision: captura " +
            $(if ($ok) {"IGUAL"} else {"DISTINTA $($res[$brazo].bmp) vs $($res.omision.bmp)"}) +
            ", cp " + $(if ($okc) {"IGUAL ($($res.omision.cpn) puntos)"}
                        else {"DISTINTO $($res[$brazo].cpn)/$($res[$brazo].cph) vs $($res.omision.cpn)/$($res.omision.cph)"}))
    }
}

foreach ($v in @("DCEMU_JIT_BLOQUES","DCEMU_MMU_ETIQUETA_MODO","DCEMU_CP_MS")) {
    if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
}
Write-Output "listo: logs\capacidad-gate"
