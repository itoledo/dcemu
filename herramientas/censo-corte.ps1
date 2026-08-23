# El censo de la frontera ponderado por veces (fase B.1 de docs/jit-sota-plan.md):
# recompila build-jit y corre los tres guests del banco con DCEMU_JIT=2 para
# leer "lo que mas corto, ponderado por veces" del resumen. Los totales de
# instrucciones/entradas validan el instrumento: deben clavar al digito con la
# corrida anterior (DOOM y SR2, los arbitros inmunes al pad).
$ErrorActionPreference = "Stop"

$cmake = Get-Command cmake -EA SilentlyContinue
if (-not $cmake) {
	$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:PATH"
}

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue

cmake --build build-jit --config Release --target dcemu | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build fallo" }
Write-Output "hash: $((Get-FileHash build-jit\Release\dcemu.exe -Algorithm SHA256).Hash.Substring(0,16))"

$exe = 'build-jit\Release\dcemu.exe'
$err = 'build-jit\Release\stderr.txt'
$bancos = @(
	@{ n='dcdoom'; img='roms\DCDoom GDI and CDI\DCDoom CDI.cdi'; s=35; teclas=$false },
	@{ n='ct'; img='roms\Crazy Taxi (USA).cdi'; s=180; teclas=$true },
	@{ n='sr2'; img='roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi'; s=60; teclas=$false }
)

$env:DCEMU_JIT = '2'
foreach ($b in $bancos) {
	if ($b.teclas) {
		$env:DCEMU_PULSAR_START = '300,1100'; $env:DCEMU_PULSAR_A = '1'; $env:DCEMU_SOLO_A = '1'
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}
	& $exe "--salir-tras=$($b.s)" --sin-vmu $b.img | Out-Null
	Copy-Item $err "logs\censo-corte-$($b.n).txt" -Force
}
Remove-Item env:DCEMU_JIT,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue

foreach ($b in $bancos) {
	"== $($b.n)"
	Select-String -Path "logs\censo-corte-$($b.n).txt" -Pattern 'instrucciones en ' | ForEach-Object Line
	Select-String -Path "logs\censo-corte-$($b.n).txt" -Pattern 'ponderado por veces' -Context 0,17 |
		ForEach-Object { $_.Line; $_.Context.PostContext }
}
