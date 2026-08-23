# La tanda de la fase G: clang-cl contra MSVC, DOS BINARIOS por construccion
# (el caso DCEMU_SIN_ALINEAR: la contaminacion de layout no se puede evitar,
# se carga con ella y se mira la dispersion). Cada uno con su PGO entrenado en
# su esquema -- un clang sin perfil contra un MSVC con perfil no mide el
# compilador, mide el perfil. DCEMU_JIT=2, que es la forma que la adopcion
# mide; la mitad del arena es identica por construccion (los contadores del
# traductor salen al digito entre los dos, verificado tras /OPT:NOICF).
#
# Protocolo entero: hash de ambos binarios, calentamiento POR GUEST y POR
# BINARIO descartado, orden alternado entre rondas, resumen jit por corrida
# (que ademas verifica la igualdad de conteos en cada fila).
$ErrorActionPreference = "Stop"

$brazos = @(
	@{ n = "clang"; exe = "build-clang\dcemu.exe" },
	@{ n = "msvc";  exe = "build-jit\Release\dcemu.exe" }
)

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
foreach ($b in $brazos) {
	Write-Output "hash $($b.n): $((Get-FileHash $b.exe -Algorithm SHA256).Hash.Substring(0,16))"
}

function Correr($brazo, $img, $segundos, $teclas)
{
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	} else {
		Remove-Item env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
	}

	$env:DCEMU_JIT = "2"

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $brazo.exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$err = Join-Path (Split-Path $brazo.exe) "stderr.txt"
	$resumen = (Select-String -Path $err -Pattern "^jit: \d+ instrucciones" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-6} {1,8} ms   {2}" -f $brazo.n, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)

$ordenes = @(
	@("clang", "msvc"),
	@("msvc", "clang"),
	@("clang", "msvc"),
	@("msvc", "clang")
)

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento por binario descartado)"
	foreach ($brazo in $brazos) { Correr $brazo $b.img $b.s $b.teclas | Out-Null }
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) {
			$brazo = $brazos | Where-Object { $_.n -eq $m }
			Correr $brazo $b.img $b.s $b.teclas
		}
		Write-Output "---"
	}
}

Remove-Item env:DCEMU_JIT,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
Write-Output "=== fin"
