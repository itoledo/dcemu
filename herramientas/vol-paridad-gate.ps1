# La compuerta de la paridad de volumenes modificadores (graficos.c).
#
# Dos brazos sobre UN binario:
#   con    por omision -- paridad por grupo, la regla del chip (DevBox 3.4.5.1)
#   sin    DCEMU_SIN_VOL_PARIDAD=1 -- la cuenta con signo anterior, byte a byte
#
# Lo que se espera y por que (medido 2026-08-25, binario AEE3F2F3B1441687):
#  - Cuatro demos salen IDENTICAS entre brazos en los dos caminos
#    (modifier_volume, _tex, cheap_shadow, volumen-incluir): devanado
#    consistente, y ahi paridad y cuenta con signo coinciden (+1 y -1 son lo
#    mismo modulo 2).
#  - pvr-modifier_volume_zclip DIFIERE para bien: el cubo de KOS tambien viene
#    devanado mixto y la cuenta vieja le dejaba una muesca triangular.
#  - volumen-excluir DIFIERE solo en ventana: la paridad por grupo trae la
#    exclusion real al camino de plantilla (complemento exacto) donde antes
#    estaba la aproximacion GL_ZERO.
#  - El cuadro 8400 del attract de Crazy Taxi (el salto) CAMBIA: sus volumenes
#    traen devanado mixto (medido: 30 de 30 grupos) y la cuenta con signo
#    marcaba las paredes -- la sombra-cortina. Es la senal del arreglo, no una
#    regresion; el arbitro es el attract de consola real (storyboard del video
#    l78Y3gAblwY, ~91 s: la sombra es una mancha plana desplazada en el suelo).
param([string] $Exe = "build-clang\dcemu.exe")

$ErrorActionPreference = "Stop"
Set-Location "$PSScriptRoot\.."
if (-not (Test-Path $Exe)) { throw "falta $Exe" }

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"binario: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

$demos = @(
	"C:\dcsdk\tmp\bins\pvr-modifier_volume.bin",
	"C:\dcsdk\tmp\bins\pvr-modifier_volume_tex.bin",
	"C:\dcsdk\tmp\bins\pvr-modifier_volume_zclip.bin",
	"C:\dcsdk\tmp\bins\pvr-cheap_shadow.bin",
	"demos\volumen-excluir\volumen-excluir.bin",
	"demos\volumen-excluir\volumen-incluir.bin"
)

$env:DCEMU_RTC_FIJO = "1000000000"

foreach ($d in $demos) {
	$n = [IO.Path]::GetFileNameWithoutExtension($d)

	foreach ($r in @("ventana", "shader")) {
		$h = @{}

		foreach ($brazo in @("con", "sin")) {
			Remove-Item env:DCEMU_SIN_VOL_PARIDAD -EA SilentlyContinue
			if ($brazo -eq "sin") { $env:DCEMU_SIN_VOL_PARIDAD = "1" }

			$bmp = "logs\vpg-$n-$r-$brazo.bmp"
			& $Exe --salir-tras=8 --sin-vmu "--render=$r" "--captura-gl=$bmp" $d | Out-Null
			$h[$brazo] = if (Test-Path $bmp) { (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16) } else { "sin captura" }
		}

		"{0,-28} {1,-8} {2}" -f $n, $r, $(if ($h["con"] -eq $h["sin"]) { "identicas ($($h['con']))" } else { "DISTINTAS $($h['con'])/$($h['sin'])" })
	}
}

# El salto del attract de CT, por los dos caminos: aca los brazos DEBEN diferir.
$env:DCEMU_PULSAR_START = "300"
$env:DCEMU_CAPTURA_TODAS = "8400"

foreach ($r in @("ventana", "shader")) {
	$h = @{}

	foreach ($brazo in @("con", "sin")) {
		Remove-Item env:DCEMU_SIN_VOL_PARIDAD -EA SilentlyContinue
		if ($brazo -eq "sin") { $env:DCEMU_SIN_VOL_PARIDAD = "1" }

		$dir = "logs\vpg-ct-$r-$brazo"
		New-Item -ItemType Directory -Force $dir | Out-Null
		& $Exe --salir-tras=153 --sin-vmu "--render=$r" "--captura-gl=$dir\ct.bmp" "roms\Crazy Taxi (USA).cdi" | Out-Null
		$f = "$dir\f8400-ct.bmp"
		$h[$brazo] = if (Test-Path $f) { (Get-FileHash $f -Algorithm SHA256).Hash.Substring(0,16) } else { "sin captura" }
	}

	"{0,-28} {1,-8} {2}" -f "ct-salto-f8400", $r, $(if ($h["con"] -eq $h["sin"]) { "IGUALES (el arreglo no actuo)" } else { "difieren, como debe: $($h['con'])/$($h['sin'])" })
}

Remove-Item env:DCEMU_SIN_VOL_PARIDAD, env:DCEMU_RTC_FIJO, env:DCEMU_PULSAR_START, env:DCEMU_CAPTURA_TODAS -EA SilentlyContinue
"hecho"
