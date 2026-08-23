# El piso de ruido de un expediente de divergencia (fase A de
# jit-sota-plan.md, el metodo del expediente de THPS2): cada brazo corrido
# DOS veces -- int-a, int-b, jit-a, jit-b -- con la receta exacta del barrido
# (RTC clavado, CP_MS punto por punto, sin VMU). La lectura: si int-a≡int-b y
# jit-a≡jit-b pero int≠jit en el mismo punto las dos veces, la divergencia es
# de forma y pasa a biseccion; si un brazo no se reproduce a si mismo, es el
# ambiente (el pad ocioso entra por XInput global) y el expediente se queda
# en observacion.
param(
	[string]   $Exe = "build-jit\Release\dcemu.exe",
	[int]      $Segundos = 40,
	[int]      $TopeMin = 8,
	[string[]] $Juegos = @('Marvel vs. Capcom 2 (USA)', 'Shenmue (USA) (Disc 1)')
)

$ErrorActionPreference = "Continue"
Set-Location D:\dev\dcemu
if (-not (Test-Path $Exe)) { throw "falta $Exe" }
$err = Join-Path (Split-Path $Exe) "stderr.txt"

Get-Process dcemu -EA SilentlyContinue | Stop-Process -Force -EA SilentlyContinue
"hash: $((Get-FileHash $Exe -Algorithm SHA256).Hash.Substring(0,16))"

function Correr($nombre, $brazo, $vuelta)
{
	$img = "E:\Juegos\roms\dreamcast\$nombre.chd"
	$tag = ($nombre -split ' \(')[0] -replace '[^A-Za-z0-9]', ''
	$bmp = "logs\piso-$tag-$brazo-$vuelta.bmp"

	$env:DCEMU_RTC_FIJO = '1000000000'
	$env:DCEMU_CP_MS    = "$($Segundos * 1000)"
	# El 0 explicito: desde la adopcion (F.2) la omision es el traductor.
	$env:DCEMU_JIT = if ($brazo -eq 'jit') { '2' } else { '0' }

	Remove-Item $bmp -EA SilentlyContinue
	$p = Start-Process -FilePath (Resolve-Path $Exe) `
		-ArgumentList "--salir-tras=$Segundos --sin-vmu --captura-gl=$bmp `"$img`"" `
		-WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
	if (-not $p.WaitForExit($TopeMin * 60000)) {
		$p | Stop-Process -Force; Start-Sleep 2
		return $null
	}
	Start-Sleep 1
	Copy-Item $err "logs\piso-$tag-$brazo-$vuelta.txt" -Force -EA SilentlyContinue
	return (Get-FileHash $bmp -Algorithm SHA256).Hash.Substring(0,16)
}

function Comparar($tag, $a, $b)
{
	$ca = Select-String -Path "logs\piso-$tag-$a.txt" -Pattern '^cp ' | ForEach-Object Line
	$cb = Select-String -Path "logs\piso-$tag-$b.txt" -Pattern '^cp ' | ForEach-Object Line
	$n = [Math]::Min($ca.Count, $cb.Count); $prim = -1
	for ($i = 0; $i -lt $n; $i++) { if ($ca[$i] -cne $cb[$i]) { $prim = $i; break } }
	$v = if ($prim -ge 0) { "PUNTO $prim" } elseif ($ca.Count -ne $cb.Count) { "largo $($ca.Count)/$($cb.Count)" } else { 'exacto' }
	"  {0,-11} contra {1,-11}: {2}" -f $a, $b, $v
	if ($prim -ge 0) { "    ${a}: $($ca[$prim])"; "    ${b}: $($cb[$prim])" }
}

foreach ($g in $Juegos) {
	$tag = ($g -split ' \(')[0] -replace '[^A-Za-z0-9]', ''
	"=== $g"

	$h = @{}
	foreach ($c in @(@('int','a'), @('int','b'), @('jit','a'), @('jit','b'))) {
		$h["$($c[0])-$($c[1])"] = Correr $g $c[0] $c[1]
		if ($null -eq $h["$($c[0])-$($c[1])"]) { "  $($c[0])-$($c[1]): COLGADO"; }
	}

	"  bmp int-a=$($h['int-a']) int-b=$($h['int-b']) jit-a=$($h['jit-a']) jit-b=$($h['jit-b'])"
	Comparar $tag 'int-a' 'int-b'
	Comparar $tag 'jit-a' 'jit-b'
	Comparar $tag 'int-a' 'jit-a'
}

Remove-Item env:DCEMU_JIT,env:DCEMU_CP_MS,env:DCEMU_RTC_FIJO -EA SilentlyContinue
"hecho"
