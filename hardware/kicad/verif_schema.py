#!/usr/bin/env python3
"""Fait relire le schéma par KiCad (kicad-cli), exporte sa netlist et la compare
broche par broche à la description de référence (gen_netlist.py)."""
import os, subprocess, sys, tempfile
ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ICI, "..", "netlist"))
import gen_netlist
from sexpr import parse, find, findall

out = os.path.join(tempfile.mkdtemp(), "kicad.net")
r = subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out,
                    os.path.join(ICI, "alarme32", "alarme32.kicad_sch")], capture_output=True, text=True)
if r.returncode or not os.path.exists(out):
    sys.exit("kicad-cli a échoué :\n" + r.stdout + r.stderr)
net = parse(open(out, encoding="utf-8").read())[0]
kic = {}
refs = {str(find(c, "ref")[1]) for c in findall(find(net, "components"), "comp")}
for n in findall(find(net, "nets"), "net"):
    nom = str(find(n, "name")[1]).lstrip("/")
    for nd in findall(n, "node"):
        kic[(str(find(nd, "ref")[1]), str(find(nd, "pin")[1]))] = nom

attendu = {}
for c in gen_netlist.B.composants.values():
    for p, n in c.net.items():
        attendu[(c.ref, p)] = n
erreurs = []
if refs != set(gen_netlist.B.composants):
    erreurs.append(f"composants : manquants {sorted(set(gen_netlist.B.composants) - refs)[:10]}, "
                   f"en trop {sorted(refs - set(gen_netlist.B.composants))[:10]}")
for k, n in attendu.items():
    got = kic.get(k)
    if n == "?":
        if got and not got.startswith("unconnected"):
            erreurs.append(f"{k[0]}.{k[1]} devrait être non connectée, KiCad : {got}")
    elif got != n:
        erreurs.append(f"{k[0]}.{k[1]} : attendu {n}, KiCad : {got}")
print(f"KiCad {r.stderr.strip() or ''}: {len(refs)} composants, "
      f"{len(set(v for v in kic.values() if not v.startswith('unconnected')))} équipotentielles relues.")
if erreurs:
    print(f"{len(erreurs)} ÉCARTS :"); [print("  -", e) for e in erreurs[:30]]; sys.exit(1)
print("Schéma KiCad identique à la netlist de référence, broche par broche.")
