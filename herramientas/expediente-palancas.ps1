# La biseccion por palancas de un expediente de divergencia (fase A de
# jit-sota-plan.md): una corrida jit por palanca de emision, comparada contra
# el log int-a que piso-expediente.ps1 ya dejo (el interprete no cambia con
# ellas). Si una palanca hace desaparecer la divergencia, ese mecanismo es el
# que cobra el ciclo de mas. La receta es la del barrido: RTC clavado, CP_MS
# punto por punto, sin VMU.
param(
	[string] $Exe = "build-jit\Release\dcemu.exe",
	[int]    $Segundos = 40,
	[int]    $TopeMin = 8,
	[string] $Juego = 'Shenmue (USA) (Disc 1)'
)

$ErrorActionPreference = "Continue"
Set-Location D:\dev\dcemu
if (-not (Test-Path $Exe)) { throw "falta $Exe" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"
$tag = ($Juego -split ' \(')[0] -replace '[^A-Za-z0-9]', ''

if (-not (Test-Path "logs\piso-$tag-int-a.txt")) { throw "falta logs\piso-$tag-int-a.txt (correr piso-expediente.ps1 primero)" }
$ca = Select-String -Path "logs\piso-$tag-int-a.txt" -Pattern '^cp ' | ForEach-Object Line

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))  juego: $Juego"

$palancas = @(
	'DCEMU_JIT_SIN_PARES',
	'DCEMU_JIT_SIN_PARES_LLAMADA',
	'DCEMU_JIT_SIN_TERMINALES',
	'DCEMU_JIT_SIN_RANURA_FPU',
	'DCEMU_JIT_SIN_PUENTES',
	'DCEMU_JIT_SIN_ATAJO_P1P2'
)

foreach ($p in $palancas) {
	$img = "E:\Juegos\roms\dreamcast\$Juego.chd"
	$bmp = "logs\palanca-$tag.bmp"

	foreach ($q in $palancas) { Remove-Item "env:$q" -EA SilentlyContinue }
	Set-Item "env:$p" '1'
	$env:DCEMU_JIT = '2'; $env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS = "$($Segundos * 1000)"

	Remove-Item $bmp -EA SilentlyContinue
	$proc = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$Segundos --sin-vmu --captura-gl=$bmp `"$img`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $proc.WaitForExit($TopeMin * 60000)) {
		$proc | Stop-Process -Force; Start-Sleep 2
		"{0,-30} COLGADO" -f $p
		continue
	}
	Start-Sleep 1
	Copy-Item $err "logs\palanca-$tag-$p.txt" -Force -EA SilentlyContinue

	$cb = Select-String -Path "logs\palanca-$tag-$p.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($ca.Count, $cb.Count); $dif = 0; $prim = -1
	for ($i = 0; $i -lt $n; $i++) { if ($ca[$i] -cne $cb[$i]) { $dif++; if ($prim -lt 0) { $prim = $i } } }
	"{0,-30} distintos={1,-4} primero={2}" -f $p, $dif, $(if ($prim -ge 0) { $prim } else { 'ninguno' })
}

foreach ($q in $palancas) { Remove-Item "env:$q" -EA SilentlyContinue }
Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO -EA SilentlyContinue
"hecho"
