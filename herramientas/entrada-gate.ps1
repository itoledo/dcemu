# La compuerta de la entrada de 32 bytes, en sus DOS mitades, porque son dos
# preguntas distintas y una sola de ellas cabe dentro de un binario:
#
#   1. la emision (indice por corrimiento, mascara ya negada) contra la vieja
#      (imul y not), sobre el binario nuevo -- palanca de ambiente;
#   2. el binario nuevo contra el canonico anterior (entrada de 28 bytes), que
#      es un cambio de tamano de struct y no tiene palanca posible.
#
# En las dos: captura + DCEMU_CP_MS + lista de entregas, tres guests, RTC
# clavado. Los puntos imprimen MMUCR, asi que comparan tambien el URC.
Set-Location D:\dev\dcemu
$log = "logs\entrada-gate.txt"
$exe = "build-clang\dcemu.exe"
$ref = "build-ref-urc\dcemu.exe"
foreach ($e in @($exe, $ref)) {
	if (-not (Test-Path -LiteralPath $e)) { throw "falta $e" }
}
$he = (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16)
$hr = (Get-FileHash $ref -Algorithm SHA256).Hash.Substring(0,16)
if ($he -eq $hr) { throw "los dos binarios son el mismo ($he): no hay A/B" }
"=== $(Get-Date -Format HH:mm:ss) compuerta de la entrada de 32 bytes; nuevo $he, referencia $hr" | Tee-Object -FilePath $log -Append

$bancos = @(
	@{ n = "sr2";  img = "roms\chd\Sega Rally 2 (USA).chd";        s = 40; k = $false },
	@{ n = "doom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 25; k = $false },
	@{ n = "ct";   img = "roms\Crazy Taxi (USA).cdi";              s = 40; k = $true  })

# $brazo: "nueva" | "vieja" (palanca, binario nuevo) | "ref" (binario anterior)
function Correr($j, $brazo) {
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MMU_EMISION_VIEJA")) {
		Remove-Item "Env:$v" -EA SilentlyContinue
	}
	if ($j.k) { $env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1" }
	$env:DCEMU_RTC_FIJO = "1000000000"
	$env:DCEMU_CP_MS = "$($j.s * 1000)"
	$env:DCEMU_SONDA_ENTREGAS = "1"
	if ($brazo -eq "vieja") { $env:DCEMU_MMU_EMISION_VIEJA = "1" }

	$bin = if ($brazo -eq "ref") { $ref } else { $exe }
	$bmp = "logs\entrada-$($j.n)-$brazo.bmp"
	Remove-Item $bmp -EA SilentlyContinue
	& $bin "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
	Copy-Item "$(Split-Path $bin)\stderr.txt" "logs\entrada-$($j.n)-$brazo.txt" -Force
	return $bmp
}

function Comparar($n, $a, $b) {
	$ha = (Get-FileHash "logs\entrada-$n-$a.bmp" -Algorithm SHA256).Hash.Substring(0,16)
	$hb = (Get-FileHash "logs\entrada-$n-$b.bmp" -Algorithm SHA256).Hash.Substring(0,16)
	$ca = @(Select-String -Path "logs\entrada-$n-$a.txt" -Pattern "^cp " | ForEach-Object Line)
	$cb = @(Select-String -Path "logs\entrada-$n-$b.txt" -Pattern "^cp " | ForEach-Object Line)
	$ea = @(Select-String -Path "logs\entrada-$n-$a.txt" -Pattern "^entrega " | ForEach-Object Line)
	$eb = @(Select-String -Path "logs\entrada-$n-$b.txt" -Pattern "^entrega " | ForEach-Object Line)

	$cpOk  = ($ca.Count -eq $cb.Count) -and ($ca.Count -gt 0) -and (0 -eq (Compare-Object $ca $cb -SyncWindow 0).Count)
	$entOk = ($ea.Count -eq $eb.Count) -and (0 -eq (Compare-Object $ea $eb -SyncWindow 0).Count)
	$ok = ($ha -eq $hb) -and $cpOk -and $entOk
	"{0,-5} {1,-12} bmp {2} ({3}/{4})  cp {5} ({6})  entregas {7} ({8})" -f $n, "$a/$b",
		$(if ($ha -eq $hb) { "IGUAL" } else { "DISTINTA" }), $ha, $hb,
		$(if ($cpOk) { "exactos" } else { "DISTINTOS" }), $ca.Count,
		$(if ($entOk) { "exactas" } else { "DISTINTAS" }), $ea.Count | Tee-Object -FilePath $log -Append
	return $ok
}

$malos = 0
foreach ($j in $bancos) {
	Correr $j "nueva" | Out-Null
	Correr $j "vieja" | Out-Null
	Correr $j "ref"   | Out-Null
	if (-not (Comparar $j.n "nueva" "vieja")) { $malos++ }
	if (-not (Comparar $j.n "nueva" "ref"))   { $malos++ }
	$ctrl = @(Select-String -Path "logs\entrada-$($j.n)-nueva.txt" -Pattern "^jit: etiqueta de traduccion" | ForEach-Object Line)
	$ctrv = @(Select-String -Path "logs\entrada-$($j.n)-vieja.txt" -Pattern "^jit: etiqueta de traduccion" | ForEach-Object Line)
	$ctrr = @(Select-String -Path "logs\entrada-$($j.n)-ref.txt"   -Pattern "^jit: etiqueta de traduccion" | ForEach-Object Line)
	"      nueva: $($ctrl -join ' ')" | Tee-Object -FilePath $log -Append
	"      vieja: $($ctrv -join ' ')" | Tee-Object -FilePath $log -Append
	"      ref:   $($ctrr -join ' ')" | Tee-Object -FilePath $log -Append
	# El control barato, el que caza que la compuerta compare lo mismo contra
	# lo mismo: el brazo nuevo dice "32 bytes" e "indice por corrimiento", el
	# viejo dice "imul", y la referencia -- que es de ANTES de que la linea
	# llevara el tamano -- no dice ninguna de las dos cosas.
	if (($ctrl -join '') -notmatch "entrada de 32 bytes, indice por corrimiento") {
		"      SONDA MUERTA: el brazo nuevo no emite el corrimiento" | Tee-Object -FilePath $log -Append; $malos++ }
	if (($ctrv -join '') -notmatch "indice por imul") {
		"      SONDA MUERTA: la palanca no volvio al imul" | Tee-Object -FilePath $log -Append; $malos++ }
	if (($ctrr -join '') -match "entrada de") {
		"      SONDA MUERTA: la referencia no es el binario anterior" | Tee-Object -FilePath $log -Append; $malos++ }
}
foreach ($v in @("DCEMU_RTC_FIJO","DCEMU_CP_MS","DCEMU_SONDA_ENTREGAS","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MMU_EMISION_VIEJA")) {
	Remove-Item "Env:$v" -EA SilentlyContinue
}
$(if ($malos -eq 0) { "COMPUERTA VERDE" } else { "COMPUERTA ROJA: $malos comparaciones" }) | Tee-Object -FilePath $log -Append
"FIN" | Out-File "logs\entrada-gate.FIN"
