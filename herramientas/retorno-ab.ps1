# La tanda de la cola de retorno del ARM7 (fase E de jit-sota-plan.md): TRES
# brazos por guest sobre el mismo binario -- todo (por omision), sinret
# (DCEMU_SIN_RETORNO_ARM=1: sin la cola generalizada ni la terminal sola) y
# viejo (las tres palancas: la conducta anterior a las formas anchas) --,
# bajo DCEMU_JIT=2 que es la forma que la adopcion mide. todo-sinret es el
# incremento del retorno; todo-viejo, la pila entera de la manana.
# Calentamiento POR GUEST descartado, orden rotado entre rondas.
#
# ciclo-jit.ps1 primero: arm7.c cambio, el perfil tiene que corresponder al
# codigo o la tanda mide la disposicion del binario.
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

	$env:DCEMU_JIT = "2"
	Remove-Item env:DCEMU_SIN_FORMAS_ARM,env:DCEMU_SIN_CABE_ARM,env:DCEMU_SIN_RETORNO_ARM -EA SilentlyContinue
	if ($brazo -in "sinret", "viejo") { $env:DCEMU_SIN_RETORNO_ARM = "1" }
	if ($brazo -eq "viejo") { $env:DCEMU_SIN_CABE_ARM = "1"; $env:DCEMU_SIN_FORMAS_ARM = "1" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$resumen = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-9} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("todo", "sinret", "viejo"),
	@("viejo", "sinret", "todo"),
	@("sinret", "viejo", "todo")
)

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "todo" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT,env:DCEMU_SIN_FORMAS_ARM,env:DCEMU_SIN_CABE_ARM,env:DCEMU_SIN_RETORNO_ARM,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
