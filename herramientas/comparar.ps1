# comparar.ps1 -- dos barridos, byte a byte.
#
# La comparacion es de hashes de los BMP, no de cuentas de colores. Contar
# colores fue el metodo viejo y tiene dos trampas documentadas en
# docs/demos-kos.md: se cuenta mal en PowerShell (-shl sobre un [byte] no
# promueve) y dos capturas distintas pueden dar el mismo numero. Un hash no
# tiene ninguna de las dos.
#
#   .\herramientas\comparar.ps1 barrido-antes barrido-despues

param(
	[Parameter(Mandatory=$true)][string] $Antes,
	[Parameter(Mandatory=$true)][string] $Despues
)

$a = @{}; $b = @{}
Import-Csv (Join-Path $Antes   "resumen.csv") | ForEach-Object { $a[$_.demo] = $_ }
Import-Csv (Join-Path $Despues "resumen.csv") | ForEach-Object { $b[$_.demo] = $_ }

$iguales = 0; $distintas = @(); $solo = @()

foreach ($k in ($a.Keys | Sort-Object)) {
	if (-not $b.ContainsKey($k)) { $solo += "$k (solo en $Antes)"; continue }

	if ($a[$k].sha256 -eq $b[$k].sha256) {
		$iguales++
	} else {
		$distintas += [pscustomobject]@{
			demo   = $k
			antes  = if ($a[$k].sha256 -eq '-') { 'sin captura' } else { $a[$k].sha256.Substring(0,8) }
			despues= if ($b[$k].sha256 -eq '-') { 'sin captura' } else { $b[$k].sha256.Substring(0,8) }
			rv     = "$($a[$k].rv) -> $($b[$k].rv)"
		}
	}
}

foreach ($k in ($b.Keys | Sort-Object)) {
	if (-not $a.ContainsKey($k)) { $solo += "$k (solo en $Despues)" }
}

Write-Host "identicas: $iguales de $($a.Count)"

if ($distintas.Count) {
	Write-Host ""
	Write-Host "DISTINTAS ($($distintas.Count)):"
	$distintas | Format-Table -AutoSize
}

if ($solo.Count) {
	Write-Host ""
	Write-Host "solo en una:"
	$solo | ForEach-Object { Write-Host "  $_" }
}

if ($distintas.Count -eq 0 -and $solo.Count -eq 0) { Write-Host "sin cambios." }
