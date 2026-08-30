# La compuerta de la tabla de bloques del traductor: dos brazos sobre UN
# binario (la emision no cambia -- el sondeo, el reuso de lapidas y el
# desmarcar son puro lado anfitrion), capturas al salir y puntos de control
# DCEMU_CP_MS comparados byte a byte. DCEMU_JIT_TABLA_VIEJA=1 es el brazo de
# control.
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

New-Item -ItemType Directory -Force logs\tabla-gate | Out-Null

function Limpiar {
    foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A",
                     "DCEMU_JIT_TABLA_VIEJA","DCEMU_CP_MS")) {
        if (Test-Path "Env:\$v") { Remove-Item "Env:\$v" }
    }
}

foreach ($j in @(
    @{n="sr2";  img=$sr2;  s=60; k=$false},
    @{n="doom"; img=$doom; s=35; k=$false},
    @{n="ct";   img=$ct;   s=180; k=$true}))
{
    $res = @{}

    foreach ($brazo in @("nueva","vieja")) {
        Limpiar
        if ($j.k) {
            $env:DCEMU_PULSAR_START = "300,1100"
            $env:DCEMU_PULSAR_A = "1"
            $env:DCEMU_SOLO_A = "1"
        }
        # Un punto por ms durante la corrida ENTERA: DCEMU_CP_MS=N cubre los
        # primeros N ms (con 1 la compuerta compararia un solo punto trivial).
        $env:DCEMU_CP_MS = [string]($j.s * 1000)
        if ($brazo -eq "vieja") { $env:DCEMU_JIT_TABLA_VIEJA = "1" }

        $bmp = "logs\tabla-gate\$($j.n)-$brazo.bmp"
        & $Exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
        Copy-Item $err "logs\tabla-gate\$($j.n)-$brazo.txt" -Force

        $cp = Select-String -Path $err -Pattern "^cp " | ForEach-Object { $_.Line }
        $ms = [System.Security.Cryptography.SHA256]::Create()
        $cphash = [BitConverter]::ToString($ms.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes(($cp -join "`n")))).Replace("-","").Substring(0,16)

        $res[$brazo] = @{
            bmp = (Get-FileHash $bmp).Hash.Substring(0,16)
            cpn = $cp.Count
            cph = $cphash
        }
    }

    $ok  = ($res.nueva.bmp -eq $res.vieja.bmp)
    $okc = ($res.nueva.cph -eq $res.vieja.cph -and $res.nueva.cpn -eq $res.vieja.cpn)
    Write-Output ("$($j.n): captura " + $(if ($ok) {"IGUAL ($($res.nueva.bmp))"} else {"DISTINTA $($res.nueva.bmp) vs $($res.vieja.bmp)"}) +
        ", cp " + $(if ($okc) {"IGUAL ($($res.nueva.cpn) puntos)"} else {"DISTINTO $($res.nueva.cpn)/$($res.nueva.cph) vs $($res.vieja.cpn)/$($res.vieja.cph)"}))
}

Limpiar
Write-Output "listo: logs\tabla-gate"
