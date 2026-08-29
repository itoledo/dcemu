"""Resume la telemetria DCEMU_SONDA_SR2 de una o mas corridas.

Por corrida: maximo vnorm, marcha maxima, y el instante del primer cambio a
segunda si lo hubo -- que es el veredicto que la caja automatica de SR2 pide.
"""

import re
import sys

PAT = re.compile(
    r"sr2 (\d+) ms base=(\w+) vel=([\d.]+) marcha=(\d+) rpm=([\d.]+)"
    r" vnorm=([\d.]+) rnorm=([\d.]+) rampa=([\d.]+)")


def resumir(camino):
    rows = []
    for l in open(camino, encoding="latin-1"):
        m = PAT.match(l)
        if m:
            rows.append((int(m.group(1)), float(m.group(3)), int(m.group(4)),
                         float(m.group(5)), float(m.group(6)),
                         float(m.group(8))))
    if not rows:
        print(f"{camino}: sin muestras")
        return
    vmax = max(r[4] for r in rows)
    gmax = max(r[2] for r in rows)
    velmax = max(r[1] for r in rows)
    seg = next((r for r in rows if r[2] >= 2), None)
    rampa_llena = sum(1 for r in rows if r[5] > 0.98)
    print(f"{camino}:")
    print(f"  muestras {rows[0][0]}..{rows[-1][0]} ms   vnorm max {vmax:.4f}"
          f"   vel max {velmax:.1f}   marcha max {gmax}"
          f"   rampa>0.98 en {rampa_llena}/{len(rows)}")
    if seg:
        print(f"  SEGUNDA a los {seg[0]} ms (vel {seg[1]:.1f}, vnorm {seg[4]:.4f})")


for c in sys.argv[1:]:
    resumir(c)
