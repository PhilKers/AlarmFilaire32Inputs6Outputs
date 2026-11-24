#!/usr/bin/env python3
"""Relit les netlists générées (Allegro « Telesis » et OrCAD PCB II), vérifie
leur syntaxe et qu'elles décrivent exactement la même connectivité, puis
applique quelques règles de conception simples."""
import os, re, sys
from collections import defaultdict

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sortie")

def lire_telesis(path):
    txt = open(path, encoding="ascii").read()
    assert txt.startswith("$PACKAGES") and txt.rstrip().endswith("$END"), "structure $PACKAGES/$NETS/$END"
    pk, nets = txt.split("$NETS")
    pk = pk.replace("$PACKAGES", "")
    pk = re.sub(r",\s*\n\s*", " ", pk)          # lignes de continuation
    refs = {}
    for l in pk.strip().splitlines():
        m = re.match(r"^'([^']+)' ! '([^']+)' ! '([^']*)' ; (.+)$", l.strip())
        assert m, f"ligne $PACKAGES invalide : {l}"
        for r in m.group(4).split():
            assert r not in refs, f"repère en double {r}"
            refs[r] = m.group(1)
    conn = {}
    nets = re.sub(r",\s*\n\s*", " ", nets.replace("$END", ""))
    for l in nets.strip().splitlines():
        m = re.match(r"^'([^']+)' ; (.+)$", l.strip())
        assert m, f"ligne $NETS invalide : {l}"
        for node in m.group(2).split():
            r, p = node.split(".", 1)
            assert r in refs, f"{node} : composant absent de $PACKAGES"
            assert (r, p) not in conn, f"{node} dans deux équipotentielles"
            conn[(r, p)] = m.group(1)
    return refs, conn

def lire_pcb2(path):
    refs, conn, cur = {}, {}, None
    for l in open(path, encoding="ascii"):
        s = l.strip()
        m = re.match(r"^\( ([0-9A-F]{8}) (\S+) (\S+) (\S+)$", s)
        if m:
            cur = m.group(3); refs[cur] = m.group(2); continue
        m = re.match(r"^\( (\S+) (\S+) \)$", s)
        if m and cur:
            if m.group(2) != "?":
                conn[(cur, m.group(1))] = m.group(2)
    return refs, conn

r1, c1 = lire_telesis(os.path.join(D, "alarme32_allegro.net"))
r2, c2 = lire_pcb2(os.path.join(D, "alarme32_pcb2.net"))
assert r1 == r2, "listes de composants/empreintes différentes"
assert c1 == c2, "connectivités différentes entre les deux formats"

par_net = defaultdict(list)
for (r, p), n in c1.items():
    par_net[n].append(r + "." + p)

erreurs = []
# Règles : alimentations jamais reliées entre elles, nets de puissance assez peuplés
for n in ("GND", "P3V3", "P5V", "VIN", "P3V8_GSM", "P3V3A", "VZONE"):
    if len(par_net[n]) < 3:
        erreurs.append(f"{n} : seulement {len(par_net[n])} connexions")
for n, nd in par_net.items():
    if len(nd) < 2:
        erreurs.append(f"{n} : une seule connexion ({nd[0]})")
# Chaque zone : borne, R série, TVS ; chaque nœud de zone : 4 éléments
for z in range(1, 33):
    if len(par_net[f"ZONE{z}"]) != 3 or len(par_net[f"ZN{z}"]) != 4 or len(par_net[f"ZA{z}"]) != 3:
        erreurs.append(f"zone {z} : topologie inattendue")
tp = [n for n, nd in par_net.items() if len(nd) == 1]
print(f"Allegro et PCB II identiques : {len(r1)} composants, {len(par_net)} équipotentielles, "
      f"{len(c1)} broches connectées.")
if erreurs:
    print("PROBLÈMES :"); [print("  -", e) for e in erreurs]; sys.exit(1)
print("Règles de conception : OK")
