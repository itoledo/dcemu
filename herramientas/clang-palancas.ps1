# clang-palancas.ps1 -- que palanca del traductor iguala los contadores de
# clang y MSVC (fase G). CT 10 s, sin MMU y sin rechazos: el contador limpio.
param(
	[string] $Clang = "build-clang\dcemu.exe",
	[string] $Msvc  = "build-jit\Release\dcemu.exe"
)

$ErrorActionPreference = "Stop"
if (Get-Process dcemu -ErrorAction SilentlyContinue) { throw "hay un dcemu corriendo" }

$img = "roms\Crazy Taxi (USA).cdi"
$palancas = @("", "DCEMU_JIT_SIN_RANURA_FPU", "DCEMU_JIT_SIN_TERMINALES",
	"DCEMU_JIT_SIN_PARES", "DCEMU_JIT_SIN_PARES_LLAMADA", "DCEMU_JIT_SIN_PUENTES",
	"DCEMU_JIT_SIN_ATAJO_P1P2")

$env:DCEMU_JIT = "2"

foreach ($p in $palancas) {
	if ($p -ne "") { Set-Item "env:$p" "1" }
	$r = @{}
	foreach ($lado in @(@{ n = "clang"; exe = $Clang }, @{ n = "msvc"; exe = $Msvc })) {
		& $lado.exe --salir-tras=10 --sin-vmu $img | Out-Null
		$err = Join-Path (Split-Path $lado.exe) "stderr.txt"
		$r[$lado.n] = (Select-String $err -Pattern "bloques traducidos").Line
	}
	if ($p -ne "") { Remove-Item "env:$p" }

	$tag = if ($p -eq "") { "(base)" } else { $p }
	$v = if ($r['clang'] -eq $r['msvc']) { "IGUALES" } else { "DIVERGEN" }
	Write-Host "$tag  ->  $v"
	if ($v -eq "DIVERGEN") {
		Write-Host "  clang: $(($r['clang'] -split ',')[0..1] -join ',')"
		Write-Host "  msvc:  $(($r['msvc'] -split ',')[0..1] -join ',')"
	}
}

Remove-Item env:DCEMU_JIT
