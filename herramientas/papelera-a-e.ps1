# Restaura a E:\Juegos\roms\dreamcast todo lo de la Papelera cuyo origen era
# D:\dev\dcemu\roms, con su nombre original. Move-Item directo desde el
# almacen fisico ($R...) de la Papelera: determinista y con el nombre bueno
# de una (el MoveHere del shell llega con el nombre interno). Las entradas
# huerfanas ($I...) que queden en la Papelera desaparecen al vaciarla.
$ErrorActionPreference = "Continue"
$destino = 'E:\Juegos\roms\dreamcast'

$shell = New-Object -ComObject Shell.Application
$rb = $shell.Namespace(10)
$items = @($rb.Items() | Where-Object { $rb.GetDetailsOf($_, 1) -eq 'D:\dev\dcemu\roms' })

"{0} elementos" -f $items.Count

foreach ($it in $items) {
	$dest = Join-Path $destino $it.Name
	if (Test-Path -LiteralPath $dest) {
		"YA EXISTE, no se pisa: $($it.Name)"
		continue
	}
	try {
		Move-Item -LiteralPath $it.Path -Destination $dest -ErrorAction Stop
		"movido: $($it.Name)"
	} catch {
		"FALLO: $($it.Name) -- $($_.Exception.Message)"
	}
}

'--- destino:'
Get-ChildItem -LiteralPath $destino | Sort-Object Name | ForEach-Object {
	'{0,-72} {1,8}' -f $_.Name, $(if ($_.PSIsContainer) { 'carpeta' } else { '{0:N0} MB' -f ($_.Length/1MB) }) }
'--- libre en E: y D:'
Get-Volume E, D | Select-Object DriveLetter, @{n='LibreGB';e={[math]::Round($_.SizeRemaining/1GB,1)}}
