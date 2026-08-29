# Valida la caja automatica de Sega Rally 2: el A/B de la demora del DMA Maple.
# Un solo binario, una sola palanca (DCEMU_MAPLE_DEMORA_NOMINAL), misma receta y
# misma VMU de partida en los dos brazos.
param(
    [string] $Exe      = "build-clang\dcemu.exe",
    [string] $Receta   = "logs\sr2at\replay-at6.txt",
    [int]    $Segundos = 70,
    [int]    $Cada     = 30,
    [string] $Salida   = "logs\sr2at\verif2"
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz

$juego = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"
if (-not (Test-Path -LiteralPath $juego))  { throw "no esta la imagen: $juego" }
if (-not (Test-Path $Receta)) { throw "no esta la receta: $Receta" }

$vivos = Get-Process dcemu -ErrorAction SilentlyContinue
if ($vivos) { throw "hay $($vivos.Count) dcemu.exe corriendo; matalos antes de medir" }

Write-Host ("binario $Exe  " + (Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))

foreach ($b in @(@{n="nuevo"; nominal=$false}, @{n="viejo"; nominal=$true})) {
    $n = $b.n
    $d = "$Salida\$n"
    if (Test-Path $d) { Get-ChildItem "$d\*" -File | Remove-Item -Force }
    New-Item -ItemType Directory -Force $d | Out-Null
    Write-Host "`n=== brazo $n ==="

    # VMU fresca por brazo: una corrida escribe la tarjeta y la siguiente
    # arrancaria de otro estado, o sea de otro camino del guest.
    Copy-Item "bios\vmu-a1.bin" "$d\vmu.bin" -Force

    $env:DCEMU_RTC_FIJO     = "2200000000"
    $env:DCEMU_MANDO        = $Receta
    $env:DCEMU_CAPTURA_TODAS = "$Cada"
    $env:DCEMU_VOLCAR_RAM   = "$d\ram.bin"
    if ($b.nominal) { $env:DCEMU_MAPLE_DEMORA_NOMINAL = "1" }
    else            { Remove-Item Env:\DCEMU_MAPLE_DEMORA_NOMINAL -ErrorAction SilentlyContinue }

    $t0 = Get-Date
    & $Exe --salir-tras=$Segundos --vmu="$d\vmu.bin" --captura-gl="$d\t.bmp" --traza-mem `
           $juego 2>&1 | Out-Null
    Write-Host ("  {0:n1} s de reloj real, {1} cuadros" -f `
        ((Get-Date) - $t0).TotalSeconds, (Get-ChildItem "$d\f*.bmp").Count)

    Copy-Item (Join-Path (Split-Path -Parent $Exe) "stderr.txt") "$d\stderr.txt" -Force
}

foreach ($v in @("DCEMU_MAPLE_DEMORA_NOMINAL","DCEMU_VOLCAR_RAM","DCEMU_MANDO","DCEMU_CAPTURA_TODAS")) {
    Remove-Item "Env:\$v" -ErrorAction SilentlyContinue
}
Write-Host "`nlisto: $Salida"
