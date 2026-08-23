# La compuerta de la predecodificacion del DSP: el .wav de Crazy Taxi (el
# guest con microprograma real, 78 pasos) byte a byte contra el binario
# anterior, mas la suite `dsp` de ctest. La referencia se genera ANTES de
# recompilar, con el binario vigente en disco.
$ErrorActionPreference = "Stop"
Set-Location D:\dev\dcemu

$cmake = Get-Command cmake -EA SilentlyContinue
if (-not $cmake) {
	$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:PATH"
}

$exe = 'build-jit\Release\dcemu.exe'
Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"referencia con: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

function CorrerWav($wav)
{
	$env:DCEMU_JIT = '2'; $env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_PULSAR_START = '300,1100'; $env:DCEMU_PULSAR_A = '1'; $env:DCEMU_SOLO_A = '1'
	& $exe --salir-tras=60 --sin-vmu --sin-audio "--captura-audio=$wav" 'roms\Crazy Taxi (USA).cdi' | Out-Null
	Remove-Item env:DCEMU_JIT,env:DCEMU_RTC_FIJO,env:DCEMU_PULSAR_START,env:DCEMU_PULSAR_A,env:DCEMU_SOLO_A -EA SilentlyContinue
}

CorrerWav 'logs\dsp-ref.wav'

cmake --build build-jit --config Release --target dcemu | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build fallo" }
"nuevo: $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))"

CorrerWav 'logs\dsp-nuevo.wav'

$a = (Get-FileHash logs\dsp-ref.wav -Algorithm SHA256).Hash
$b = (Get-FileHash logs\dsp-nuevo.wav -Algorithm SHA256).Hash
"wav ref=$($a.Substring(0,16)) nuevo=$($b.Substring(0,16)) identicos=$($a -eq $b)"

# La suite dsp, con el arbol de tests.
cmake --build build --config Debug --target dcemu_tests 2>&1 | Select-String -Pattern ' error |dcemu_tests.vcxproj ->' | ForEach-Object Line
ctest --test-dir build -C Debug -R 'dsp' --output-on-failure 2>&1 | Select-String -Pattern 'Passed|Failed|tests passed' | ForEach-Object Line
