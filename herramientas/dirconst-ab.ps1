# TRAMPA: para borrar una variable hay que usar Remove-Item "Env:NOMBRE". En
# PowerShell 7, [Environment]::SetEnvironmentVariable($v, $null) NO la borra --
# la deja VACIA-- y getenv() devuelve "" en vez de NULL. Para DCEMU_JIT eso
# significa atoi("")==0, o sea APAGAR el traductor.
#
# La tanda del pliegue de direcciones constantes: dos brazos sobre el mismo binario, con
# (por omision) y sin (DCEMU_JIT_SIN_DIR_CONSTANTE=1, por manejador). El cliente es
# DCDoom -- 4,91 % de sus instrucciones -- y los otros dos guests van como
# control: el censo no encuentra DIV1 en sus plantillas pesadas, asi que si se
# movieran seria senal de que la tanda mide otra cosa.
param([string] $Exe = "build-clang\dcemu.exe", [string] $Guest = "")

$ErrorActionPreference = "Stop"
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
Write-Output "hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($brazo, $img, $segundos, $teclas)
{
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_JIT_SIN_DIR_CONSTANTE")) {
		Remove-Item "Env:$v" -ErrorAction SilentlyContinue
	}
	if ($teclas) {
		$env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1"
	}
	$env:DCEMU_JIT = "2"
	if ($brazo -eq "sin") { $env:DCEMU_JIT_SIN_DIR_CONSTANTE = "1" }

	$reloj = [System.Diagnostics.Stopwatch]::StartNew()
	& $Exe "--salir-tras=$segundos" --sin-vmu $img | Out-Null
	$reloj.Stop()

	$resumen = (Select-String -Path $err -Pattern "instrucciones en .* entradas" -EA SilentlyContinue |
		ForEach-Object { $_.Line }) -join " | "

	"{0,-4} {1,8} ms   {2}" -f $brazo, $reloj.ElapsedMilliseconds, $resumen
}

$bancos = @(
	@{ n = "DCDoom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 35; teclas = $false },
	@{ n = "Crazy Taxi"; img = "roms\Crazy Taxi (USA).cdi"; s = 180; teclas = $true },
	@{ n = "Sega Rally 2"; img = "roms\Sega Rally 2 v1.003 (1999)(Sega)(US)[!]\Sega Rally 2 v1.003 (1999)(Sega)(US)[!].gdi"; s = 60; teclas = $false }
)
if ($Guest) { $bancos = $bancos | Where-Object { $_.n -like "*$Guest*" } }
if (-not $bancos) { throw "guest desconocido: $Guest" }

$ordenes = @(@("con","sin"), @("sin","con"), @("con","sin"), @("sin","con"))

foreach ($b in $bancos) {
	Write-Output "=== $($b.n), $($b.s) s emulados (calentamiento descartado)"
	Correr "con" $b.img $b.s $b.teclas | Out-Null
	foreach ($orden in $ordenes) {
		foreach ($m in $orden) { Correr $m $b.img $b.s $b.teclas }
		Write-Output "---"
	}
}

foreach ($v in @("DCEMU_JIT","DCEMU_JIT_SIN_DIR_CONSTANTE","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A")) {
	Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}
