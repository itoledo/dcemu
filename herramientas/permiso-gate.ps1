# La compuerta de la etiqueta fundida con el permiso: los dos brazos de la palanca sobre un binario
# (fundida contra en cada acceso), captura + DCEMU_CP_MS + lista de entregas,
# en los tres guests. Los puntos de control imprimen MMUCR, asi que comparan
# tambien el URC materializado -- que es exactamente lo que hay que probar.
Set-Location D:\dev\dcemu
$log = "logs\permiso-gate.txt"
$exe = "build-clang\dcemu.exe"
"=== $(Get-Date -Format HH:mm:ss) compuerta de la etiqueta fundida con el permiso, $((Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16))" | Tee-Object -FilePath $log -Append

$bancos = @(
	@{ n = "sr2";  img = "roms\chd\Sega Rally 2 (USA).chd";        s = 40; k = $false },
	@{ n = "doom"; img = "roms\DCDoom GDI and CDI\DCDoom CDI.cdi"; s = 25; k = $false },
	@{ n = "ct";   img = "roms\Crazy Taxi (USA).cdi";              s = 40; k = $true  })

function Correr($j, $brazo) {
	foreach ($v in @("DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MMU_PERMISO_APARTE")) {
		Remove-Item "Env:$v" -EA SilentlyContinue
	}
	if ($j.k) { $env:DCEMU_PULSAR_START = "300,1100"; $env:DCEMU_PULSAR_A = "1"; $env:DCEMU_SOLO_A = "1" }
	$env:DCEMU_RTC_FIJO = "1000000000"
	$env:DCEMU_CP_MS = "$($j.s * 1000)"
	$env:DCEMU_SONDA_ENTREGAS = "1"
	if ($brazo -eq "aparte") { $env:DCEMU_MMU_PERMISO_APARTE = "1" }

	$bmp = "logs\permiso-$($j.n)-$brazo.bmp"
	Remove-Item $bmp -EA SilentlyContinue
	& $exe "--salir-tras=$($j.s)" --sin-vmu --sin-audio "--captura-gl=$bmp" $j.img | Out-Null
	Copy-Item "$(Split-Path $exe)\stderr.txt" "logs\permiso-$($j.n)-$brazo.txt" -Force
	return $bmp
}

$malos = 0
foreach ($j in $bancos) {
	$a = Correr $j "fundida"
	$b = Correr $j "aparte"
	$ha = (Get-FileHash $a -Algorithm SHA256).Hash.Substring(0,16)
	$hb = (Get-FileHash $b -Algorithm SHA256).Hash.Substring(0,16)

	$ca = @(Select-String -Path "logs\permiso-$($j.n)-fundida.txt"  -Pattern "^cp " | ForEach-Object Line)
	$cb = @(Select-String -Path "logs\permiso-$($j.n)-aparte.txt" -Pattern "^cp " | ForEach-Object Line)
	$ea = @(Select-String -Path "logs\permiso-$($j.n)-fundida.txt"  -Pattern "^entrega " | ForEach-Object Line)
	$eb = @(Select-String -Path "logs\permiso-$($j.n)-aparte.txt" -Pattern "^entrega " | ForEach-Object Line)

	$cpOk  = ($ca.Count -eq $cb.Count) -and ($ca.Count -gt 0) -and (0 -eq (Compare-Object $ca $cb -SyncWindow 0).Count)
	$entOk = ($ea.Count -eq $eb.Count) -and (0 -eq (Compare-Object $ea $eb -SyncWindow 0).Count)
	$ctrl = @(Select-String -Path "logs\permiso-$($j.n)-fundida.txt" -Pattern "^jit: etiqueta de traduccion" | ForEach-Object Line)

	if ($ha -ne $hb -or -not $cpOk -or -not $entOk) { $malos++ }
	"{0,-5} bmp {1} ({2}/{3})  cp {4} ({5})  entregas {6} ({7})" -f $j.n,
		$(if ($ha -eq $hb) { "IGUAL" } else { "DISTINTA" }), $ha, $hb,
		$(if ($cpOk) { "exactos" } else { "DISTINTOS" }), $ca.Count,
		$(if ($entOk) { "exactas" } else { "DISTINTAS" }), $ea.Count | Tee-Object -FilePath $log -Append
	"      $($ctrl -join ' | ')" | Tee-Object -FilePath $log -Append

	# El control barato de sonda viva: sin el, una palanca que no llega deja la
	# compuerta verde y muda, comparando lo mismo contra lo mismo.
	$ctra = @(Select-String -Path "logs\permiso-$($j.n)-aparte.txt" -Pattern "^jit: etiqueta de traduccion" | ForEach-Object Line)
	if (($ctrl -join '') -notmatch "etiqueta y permiso fundidos en una") {
		"      SONDA MUERTA: el brazo nuevo no funde la comparacion" | Tee-Object -FilePath $log -Append; $malos++ }
	if (($ctra -join '') -notmatch "etiqueta y permiso en dos comparaciones") {
		"      SONDA MUERTA: la palanca no separo la comparacion" | Tee-Object -FilePath $log -Append; $malos++ }
}
foreach ($v in @("DCEMU_RTC_FIJO","DCEMU_CP_MS","DCEMU_SONDA_ENTREGAS","DCEMU_PULSAR_START","DCEMU_PULSAR_A","DCEMU_SOLO_A","DCEMU_MMU_PERMISO_APARTE")) {
	Remove-Item "Env:$v" -EA SilentlyContinue
}
$(if ($malos -eq 0) { "COMPUERTA VERDE" } else { "COMPUERTA ROJA: $malos guests" }) | Tee-Object -FilePath $log -Append
"FIN" | Out-File "logs\permiso-gate.FIN"
