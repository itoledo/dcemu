# La tanda del lote B.2b (jit-sota-plan.md): CUATRO brazos por guest --
# interprete, traductor completo, sin filas terminales, sin FPU en ranura --
# porque dos palancas medidas juntas se leen al reves (la leccion de la
# rejilla: el combinado dio solapado en DOOM y disjunto en SR2, y era al
# reves). Calentamiento POR GUEST descartado (el cache frio de la imagen se
# paga por guest, no por tanda), orden rotado entre rondas.
#
# ciclo-jit.ps1 primero: el perfil tiene que corresponder al codigo.
$ErrorActionPreference = "Stop"
$exe = "build-jit\Release\dcemu.exe"
$err = "build-jit\Release\stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Write-Output "hash jit: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($brazo, $img, $segundos, $teclas)
{
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$env:DCEMU_FUSION = "0"
	Remove-Item env:DCEMU_JIT_SIN_TERMINALES,env:DCEMU_JIT_SIN_RANURA_FPU -EA SilentlyContinue
	switch ($brazo) {
		"interprete"  { $env:DCEMU_JIT = "0" }
		"completo"    { $env:DCEMU_JIT = "2" }
		"sin-term"    { $env:DCEMU_JIT = "2"; $env:DCEMU_JIT_SIN_TERMINALES = "1" }
		"sin-ranura"  { $env:DCEMU_JIT = "2"; $env:DCEMU_JIT_SIN_RANURA_FPU = "1" }
	}

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$cobertura = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-11} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $cobertura
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("interprete", "completo", "sin-term", "sin-ranura"),
	@("sin-ranura", "sin-term", "completo", "interprete")
)

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "completo" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT,env:DCEMU_JIT_SIN_TERMINALES,env:DCEMU_JIT_SIN_RANURA_FPU,env:DCEMU_FUSION,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
