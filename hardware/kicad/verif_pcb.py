#!/usr/bin/env python3
"""Recharge le circuit imprimé, vérifie l'équipotentielle de chaque pastille par
rapport à la description de référence, puis lance le contrôle DRC de KiCad et
résume les violations par type."""
import os, re, sys, tempfile
from collections import Counter
import pcbnew
ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ICI, "..", "netlist"))
import gen_netlist

board = pcbnew.LoadBoard(os.path.join(ICI, "alarme32", "alarme32.kicad_pcb"))
fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
err = []
for c in gen_netlist.B.composants.values():
    fp = fps.get(c.ref)
    if fp is None:
        err.append(f"{c.ref} absent"); continue
    pads = {}
    for p in fp.Pads():
        pads.setdefault(p.GetNumber(), set()).add(p.GetNetname())
    for num, net in c.net.items():
        attendu = "" if net == "?" else net
        if pads.get(num) != {attendu}:
            err.append(f"{c.ref}.{num} : attendu « {attendu} », trouvé {pads.get(num)}")
print(f"{len(fps)} empreintes ; contrôle des pastilles : {'OK' if not err else str(len(err)) + ' écarts'}")
for e in err[:20]: print("  -", e)

# Lien schéma <-> PCB : identifiants de symboles et empreintes exportés par KiCad depuis le schéma
import subprocess
sys.path.insert(0, ICI)
from sexpr import parse, find, findall
net = os.path.join(tempfile.mkdtemp(), "sch.net")
subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", net,
                os.path.join(ICI, "alarme32", "alarme32.kicad_sch")], capture_output=True)
lien = []
for comp in findall(find(parse(open(net, encoding="utf-8").read())[0], "components"), "comp"):
    ref = str(find(comp, "ref")[1])
    fpn = str(find(comp, "footprint")[1])
    sp = str(find(find(comp, "sheetpath"), "tstamps")[1])
    ts = find(comp, "tstamps") or find(comp, "tstamp")
    chemin = sp + str(ts[1])
    fp = fps.get(ref)
    if fp is None:
        lien.append(f"{ref} absent du PCB"); continue
    if fp.GetPath().AsString() != chemin:
        lien.append(f"{ref} : chemin PCB {fp.GetPath().AsString()} ≠ schéma {chemin}")
    if "alarme32:" + str(fp.GetFPID().GetLibItemName()) != fpn:
        lien.append(f"{ref} : empreinte PCB {fp.GetFPID().GetLibItemName()} ≠ schéma {fpn}")
print(f"Lien schéma ↔ PCB : {'OK' if not lien else str(len(lien)) + ' écarts'}")
for e in lien[:10]: print("  -", e)
err += lien

rap = os.path.join(tempfile.mkdtemp(), "drc.rpt")
pcbnew.WriteDRCReport(board, rap, pcbnew.EDA_UNITS_MILLIMETRES, True)
txt = open(rap, encoding="utf-8").read()
types = Counter(re.findall(r"^\[(\w+)\]", txt, re.M))
print("DRC :", dict(types) if types else "aucune violation")
nb = re.search(r"\*\* Found (\d+) unconnected pads", txt)
print("Liaisons à router :", nb.group(1) if nb else "?")
if "--details" in sys.argv:
    print(txt[:6000])
sys.exit(1 if err else 0)
