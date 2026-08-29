# Compuerta de regresion para el cambio del Maple/VMU: cada juego sensible al
# bus arranca con la tarjeta puesta y deja una captura final para leer a mano.
# Cada corrida parte de una VMU fresca y del RTC clavado.
param(
    [string] $Exe    = "build-clang\dcemu.exe",
    [string] $Salida = "logs\sr2at\verif2\compuerta",
    [string] $VmuUs  = ""            # vacio = la omision del binario
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

$juegos = @(
    @{ n = "dcdoom";  img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; seg = 40 },
    @{ n = "vtennis"; img = "roms\Virtua Tennis (2000)(Sega)(US)[cr DCRES][f PAL 60Hz][repack].cdi"; seg = 35 },
    @{ n = "ctaxi";   img = "roms\Crazy Taxi (USA).cdi"; seg = 35 },
    @{ n = "ctaxi2";  img = "E:\Juegos\roms\dreamcast\Crazy Taxi 2 (USA).chd"; seg = 35 },
    @{ n = "mkg";     img = "E:\Juegos\roms\dreamcast\Mortal Kombat Gold (USA).chd"; seg = 45 }
)

$vivos = Get-Process dcemu -ErrorAction SilentlyContinue
if ($vivos) { throw "hay dcemu.exe corriendo" }

New-Item -ItemType Directory -Force $Salida | Out-Null
Write-Host ("binario: " + (Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16) + "  demora VMU: '" + $VmuUs + "'")

foreach ($j in $juegos) {
    if (-not (Test-Path -LiteralPath $j.img)) { Write-Host "$($j.n): FALTA $($j.img)"; continue }

    Copy-Item "bios\vmu-a1.bin" "$Salida\vmu-$($j.n).bin" -Force
    $env:DCEMU_RTC_FIJO = "2200000000"
    if ($VmuUs -ne "") { $env:DCEMU_MAPLE_DEMORA_VMU_US = $VmuUs }
    else { Remove-Item Env:\DCEMU_MAPLE_DEMORA_VMU_US -ErrorAction SilentlyContinue }
    Remove-Item Env:\DCEMU_MANDO -ErrorAction SilentlyContinue

    $t0 = Get-Date
    & $Exe --salir-tras=$($j.seg) --vmu="$Salida\vmu-$($j.n).bin" `
           --captura-gl="$Salida\fin-$($j.n).bmp" --traza-mem `
           $j.img 2>&1 | Out-Null
    $ok = Test-Path "$Salida\fin-$($j.n).bmp"
    Copy-Item (Join-Path (Split-Path -Parent $Exe) "stderr.txt") "$Salida\stderr-$($j.n).txt" -Force
    Write-Host ("{0}: {1:n0} s reales, captura {2}" -f $j.n, ((Get-Date)-$t0).TotalSeconds, $(if ($ok) {"si"} else {"NO"}))
}
Remove-Item Env:\DCEMU_MAPLE_DEMORA_VMU_US -ErrorAction SilentlyContinue
Write-Host "listo: $Salida"
