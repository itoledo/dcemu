"""Lee el bloque del auto de Sega Rally 2 en un volcado de la RAM del guest.

El volcado son los 16 MB planos que deja DCEMU_VOLCAR_RAM, o sea la ventana
0x0C000000. El bloque se localiza por la tabla de la caja automatica --el factor
0,78 seguido de [0, 0,5, 0,65, 0,85, 1,0]-- que vive en base+0x264; es la misma
tabla que aparece una vez por auto, asi que se informan todas las apariciones.
"""

import struct
import sys

BASE_GUEST = 0x0C000000
DESP_FACTOR = 0x264          # base del auto -> factor de la caja
TABLA = (0.0, 0.5, 0.65, 0.85, 1.0)

CAMPOS = [
    (0x14, "velocidad"),
    (0x28, "marcha"),
    (0x2c, "rpm"),
    (0x40, "vel_norm"),
    (0x4c, "rpm_norm"),
    (0x7c, "vel_tope"),
]


def casi(a, b, tol=1e-6):
    return abs(a - b) < tol


def buscar(ram):
    """Devuelve las direcciones guest donde arranca el factor de la caja."""
    hits = []
    # El factor 0,78 en simple precision.
    aguja = struct.pack("<f", 0.78)
    desde = 0
    while True:
        i = ram.find(aguja, desde)
        if i < 0:
            return hits
        desde = i + 4
        if i + 4 + 4 * len(TABLA) > len(ram):
            continue
        vals = struct.unpack_from("<%df" % len(TABLA), ram, i + 4)
        if all(casi(v, t) for v, t in zip(vals, TABLA)):
            hits.append(i)


def main():
    if len(sys.argv) < 2:
        print("uso: sr2-leer-auto.py volcado.bin [...]", file=sys.stderr)
        return 2

    for camino in sys.argv[1:]:
        with open(camino, "rb") as f:
            ram = f.read()
        hits = buscar(ram)
        print("\n== %s  (%d autos)" % (camino, len(hits)))
        for i in hits:
            base = i - DESP_FACTOR
            if base < 0:
                continue
            dir_guest = BASE_GUEST + base
            partes = []
            for desp, nombre in CAMPOS:
                if nombre == "marcha":
                    v = struct.unpack_from("<i", ram, base + desp)[0]
                    partes.append("%s=%d" % (nombre, v))
                else:
                    v = struct.unpack_from("<f", ram, base + desp)[0]
                    partes.append("%s=%.4f" % (nombre, v))
            vel = struct.unpack_from("<f", ram, base + 0x14)[0]
            tope = struct.unpack_from("<f", ram, base + 0x7c)[0]
            frac = vel / tope if tope else 0.0
            print("  %08x  %s  vel/tope=%.4f" % (dir_guest, "  ".join(partes), frac))
    return 0


if __name__ == "__main__":
    sys.exit(main())
