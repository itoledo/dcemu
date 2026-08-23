# El resto de la ronda 2 de SR2 de la tanda B.2b (la cadena nocturna murio a
# las 23:11 con el cierre de la consola), guardando stderr de completo y
# sin-ranura para el diagnostico de bytes emitidos (hipotesis B: el
# crecimiento del emitido dispersa lo caliente).
$ErrorActionPreference = "Stop"
Set-Location D:\dev\dcemu
$exe = 'build-jit\Release\dcemu.exe'
$err = 'build-jit\Release\stderr.txt'
$img = 'roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi'

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

$env:DCEMU_FUSION = '0'
foreach ($brazo in 'sin-ranura','sin-term','completo','interprete') {
	Remove-Item env:DCEMU_JIT_SIN_TERMINALES,env:DCEMU_JIT_SIN_RANURA_FPU -EA SilentlyContinue
	switch ($brazo) {
		'interprete' { $env:DCEMU_JIT = '0' }
		'completo'   { $env:DCEMU_JIT = '2' }
		'sin-term'   { $env:DCEMU_JIT = '2'; $env:DCEMU_JIT_SIN_TERMINALES = '1' }
		'sin-ranura' { $env:DCEMU_JIT = '2'; $env:DCEMU_JIT_SIN_RANURA_FPU = '1' }
	}
	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe --salir-tras=60 --sin-vmu $img | Out-Null
	$reloj.Stop()
	if ($brazo -in 'completo','sin-ranura') { Copy-Item $err "logs\b2b-sr2-$brazo.txt" -Force }
	$cob = (Select-String -Path $err -Pattern '^jit: \d+ instrucciones' -EA SilentlyContinue |
		ForEach-Object Line) -join ''
	'{0,-11} {1,8} ms   {2}' -f $brazo, $reloj.ElapsedMilliseconds, $cob
}
Remove-Item env:DCEMU_JIT,env:DCEMU_JIT_SIN_TERMINALES,env:DCEMU_JIT_SIN_RANURA_FPU,env:DCEMU_FUSION -EA SilentlyContinue

'--- resumen de emision:'
foreach ($b in 'completo','sin-ranura') {
	"== $b"
	Select-String -Path "logs\b2b-sr2-$b.txt" -Pattern 'bloques traducidos' | ForEach-Object Line
	Select-String -Path "logs\b2b-sr2-$b.txt" -Pattern 'la frontera, por peso' -Context 0,9 |
		ForEach-Object { $_.Line; $_.Context.PostContext }
}
