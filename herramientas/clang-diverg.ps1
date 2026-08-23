# clang-diverg.ps1 -- acota la divergencia de contadores del traductor entre
# el binario clang y el MSVC (fase G): por guest y por tiempo.
param(
	[string] $Clang = "build-clang\dcemu.exe",
	[string] $Msvc  = "build-jit\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"
if (Get-Process dcemu -ErrorAction SilentlyContinue) { throw "hay un dcemu corriendo" }

$casos = @(
	@{ n = "doom-2s";  img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 2 },
	@{ n = "doom-5s";  img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 5 },
	@{ n = "doom-10s"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 10 },
	@{ n = "ct-10s";   img = "roms\Crazy Taxi (USA).cdi";              s = 10 }
)

$env:DCEMU_JIT = "2"

foreach ($c in $casos) {
	$lineas = @{}
	foreach ($lado in @(@{ n = "clang"; exe = $Clang }, @{ n = "msvc"; exe = $Msvc })) {
		& $lado.exe "--salir-tras=$($c.s)" --sin-vmu $c.img | Out-Null
		$err = Join-Path (Split-Path $lado.exe) "stderr.txt"
		$lineas[$lado.n] = @(
			(Select-String $err -Pattern "^jit: [0-9]+ instrucciones").Line,
			(Select-String $err -Pattern "bloques traducidos").Line
		)
	}
	Write-Host "=== $($c.n)"
	Write-Host "  clang: $($lineas['clang'][0])"
	Write-Host "         $($lineas['clang'][1])"
	Write-Host "  msvc:  $($lineas['msvc'][0])"
	Write-Host "         $($lineas['msvc'][1])"
	$v = if (($lineas['clang'] -join '|') -eq ($lineas['msvc'] -join '|')) { "IGUALES" } else { "DIVERGEN" }
	Write-Host "  -> $v"
}

Remove-Item env:DCEMU_JIT
