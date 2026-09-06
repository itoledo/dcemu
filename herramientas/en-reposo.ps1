# en-reposo.ps1 -- la maquina en reposo, como condicion y como testigo.
#
# La regla del arbol: una tanda cuyos brazos corren mas lento que su propia
# repeticion no decide nada, y eso pasa cuando el usuario trabaja encima
# (docs/hilos-plan.md, la tanda contaminada del 2026-09-05). Desde el mismo
# dia el proceso se exime del estrangulamiento de Windows 11 (CLAUDE.md,
# DCEMU_ESTRANGULAR), asi que lo que queda es la carga de los demas procesos y
# el techo termico de dos hilos calientes. Este guion da dos cosas:
#
#   Esperar-Reposo [-MinutosSinEntrada 10] [-CargaMax 15]
#       vuelve cuando no hubo teclado ni raton en N minutos, no hay dcemu y la
#       carga de CPU esta bajo el tope. Espera lo que haga falta.
#   Ultima-Entrada
#       el instante de la ultima entrada del usuario (GetLastInputInfo), para
#       comparar con el inicio de una tanda y decir si el usuario estuvo
#       presente durante ella -- que se anota al lado del veredicto, no se
#       descarta: la lectura sigue siendo de quien lee.
#
# Se punta (. .\herramientas\en-reposo.ps1) desde el guion de la cadena.

Add-Type @"
using System; using System.Runtime.InteropServices;
public class EntradaUsuario {
	[StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
	[DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
	public static DateTime Ultima() {
		LASTINPUTINFO l = new LASTINPUTINFO(); l.cbSize = (uint) Marshal.SizeOf(l);
		if (!GetLastInputInfo(ref l)) return DateTime.MinValue;
		long ahora = Environment.TickCount64;
		long hace = (uint) ahora - l.dwTime;   // los dos en ms desde el arranque, 32 bits
		if (hace < 0) hace += 0x100000000L;
		return DateTime.Now.AddMilliseconds(-hace);
	}
}
"@

function Ultima-Entrada { return [EntradaUsuario]::Ultima() }

function Carga-Cpu {
	$c = Get-CimInstance Win32_Processor | Select-Object -First 1
	return [int] $c.LoadPercentage
}

function Esperar-Reposo([int] $MinutosSinEntrada = 10, [int] $CargaMax = 15) {
	$avisado = $false
	while ($true) {
		$hace = (Get-Date) - (Ultima-Entrada)
		$carga = Carga-Cpu
		$dcemu = [bool] (Get-Process dcemu -EA SilentlyContinue)
		if ($hace.TotalMinutes -ge $MinutosSinEntrada -and $carga -le $CargaMax -and -not $dcemu) {
			return "reposo: {0:N0} min sin entrada, carga {1} %" -f $hace.TotalMinutes, $carga
		}
		if (-not $avisado) {
			Write-Output ("esperando reposo: {0:N0} min sin entrada (pido {1}), carga {2} % (tope {3}){4}" -f `
				$hace.TotalMinutes, $MinutosSinEntrada, $carga, $CargaMax, $(if ($dcemu) { ", dcemu corriendo" } else { "" }))
			$avisado = $true
		}
		Start-Sleep -Seconds 60
	}
}
