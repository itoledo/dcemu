# Analiza el volcado crudo de xperf de un ETL del modo `cuentas` de
# perfil-pmu.ps1: filas Pmc (los 4 contadores por interrupcion de perfil) y
# filas SampledProfile (el PC y la funcion en esa interrupcion). Las une por
# (hilo, instante) y atribuye los deltas a la funcion muestreada.
#
# Existe porque la agregacion propia de xperf no funciona en esta maquina
# (2026-08-07): `-a pmc` deja el CSV con solo el encabezado aunque los eventos
# esten en el .etl, y el modo `fallos` (-PmcProfile) directamente no graba
# nada en este procesador hibrido. El volcado crudo si trae todo:
#
#   xperf -i traza.etl -o crudo.txt -symbols -tle -tti -a dumper
#   python herramientas\pmu-analizar.py crudo.txt
#
# (con _NT_SYMBOL_PATH apuntando a build\Release para que resuelva dcemu.exe)
import sys, re
from collections import defaultdict

ruta = sys.argv[1]
proceso_de = {}                       # tid -> nombre de proceso
pend = {}                             # tid -> (ts, lecturas crudas)
ultimo = {}                           # cpu -> lecturas crudas anteriores
tot_hilo = defaultdict(lambda: [0, 0, 0, 0, 0])   # tid -> [n, i, c, f, l]
tot_fn = defaultdict(lambda: [0, 0, 0, 0, 0])     # fn -> idem
sin_par = 0

# **Las filas Pmc traen lecturas acumulativas del contador de su nucleo, no
# deltas.** Sumarlas tal cual da exa-instrucciones y el mismo IPC en todas las
# funciones (el cociente de dos acumulados grandes) -- asi salio la primera
# pasada y asi se detecto. El delta real es lectura menos lectura anterior DEL
# MISMO NUCLEO, y el nucleo esta en la fila SampledProfile del par.

with open(ruta, "r", encoding="utf-8", errors="replace") as f:
    for linea in f:
        tipo = linea[:24].strip().rstrip(",")

        if tipo == "Pmc":
            c = linea.split(",")
            if len(c) < 7 or not c[1].strip().isdigit():
                continue
            ts = int(c[1]); tid = int(c[2])
            pend[tid] = (ts, int(c[3]), int(c[4]), int(c[5]), int(c[6]))

        elif tipo == "SampledProfile":
            c = [x.strip() for x in linea.split(",")]
            if len(c) < 8 or not c[1].isdigit() or not c[5].isdigit():
                continue
            ts = int(c[1]); tid = int(c[3]); cpu = int(c[5]); fn = c[7]
            m = re.match(r"(.+) \(\s*(\d+)\)", c[2])
            nombre = m.group(1).strip() if m else c[2]
            proceso_de[tid] = nombre

            p = pend.pop(tid, None)
            if p is None or abs(ts - p[0]) > 3:
                sin_par += 1
                continue

            crudo = p[1:]
            antes = ultimo.get(cpu)
            ultimo[cpu] = crudo

            if antes is None:
                continue

            d = [a - b for a, b in zip(crudo, antes)]

            # Un delta negativo es un reinicio del contador o un par mal
            # unido: se descarta la muestra, no se inventa.
            if any(x < 0 for x in d) or d[1] == 0:
                continue

            i, cc, fa, ll = d
            t = tot_hilo[tid]
            t[0] += 1; t[1] += i; t[2] += cc; t[3] += fa; t[4] += ll
            if nombre == "dcemu.exe":
                g = tot_fn[fn]
                g[0] += 1; g[1] += i; g[2] += cc; g[3] += fa; g[4] += ll

def fila(nombre, n, i, c, f, l):
    ipc = i / c if c else 0
    return (f"{nombre:55s} {n:8d} {i/1e9:9.2f} {c/1e9:9.2f} {ipc:5.2f}"
            f" {f/(i/1000) if i else 0:8.2f} {l/(i/1000) if i else 0:8.2f}")

print(f"== {ruta}  (muestras sin par: {sin_par})")
print(f"{'':55s} {'muestras':>8s} {'instr G':>9s} {'ciclos G':>9s} {'IPC':>5s}"
      f" {'fallo/ki':>8s} {'LLC/ki':>8s}")

# Por hilo, agrupado por proceso, solo los gordos.
por_proc = defaultdict(lambda: [0, 0, 0, 0, 0])
for tid, t in tot_hilo.items():
    p = por_proc[proceso_de.get(tid, "?")]
    for k in range(5):
        p[k] += t[k]

for nombre, t in sorted(por_proc.items(), key=lambda kv: -kv[1][2])[:8]:
    print(fila(nombre, *t))

print()
print("dcemu.exe por hilo:")
for tid, t in sorted(tot_hilo.items(), key=lambda kv: -kv[1][2]):
    if proceso_de.get(tid) == "dcemu.exe" and t[2] > 1e7:
        print(fila(f"  tid {tid}", *t))

print()
print("dcemu.exe por funcion (top 25 por ciclos):")
for fn, t in sorted(tot_fn.items(), key=lambda kv: -kv[1][2])[:25]:
    print(fila(f"  {fn}", *t))
