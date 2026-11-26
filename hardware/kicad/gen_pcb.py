#!/usr/bin/env python3
"""
Génère le circuit imprimé KiCad (alarme32.kicad_pcb) de la centrale d'alarme
avec l'API Python de KiCad (pcbnew, KiCad 7 ou plus récent).

  - carte 4 couches 300 × 210 mm, 4 trous de fixation M3 ;
  - 587 empreintes placées par zones fonctionnelles, connecteurs sur les bords,
    chaque entrée de zone alignée sur sa borne ;
  - équipotentielles affectées à chaque pastille (même description que le
    schéma et la netlist OrCAD) ;
  - plans de masse GND sur In1.Cu et B.Cu ; classes d'équipotentielles ;
  - lien avec le schéma (chemins des symboles) pour « Mettre à jour le PCB ».

Le ROUTAGE n'est pas fait : voir LISEZMOI.md.

Usage :
  python3 gen_pcb.py [--bibliotheque <clone kicad-footprints 7.0.11>]
  (l'option reconstruit alarme32.pretty à partir des bibliothèques KiCad)
"""
import json
import os
import re
import shutil
import sys

import numpy as np
import pcbnew

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
sys.path.insert(0, os.path.join(ICI, "..", "netlist"))
import gen_netlist                    # noqa: E402
from empreintes import EMPREINTES     # noqa: E402
import gen_schema                     # noqa: E402  (UUID des symboles, pour le lien schéma ↔ PCB)

PROJET = os.path.join(ICI, "alarme32")
BIBLI = os.path.join(PROJET, "alarme32.pretty")
W, H = 300.0, 210.0          # dimensions de la carte (mm)
OX, OY = 20.0, 20.0          # origine de la carte dans la feuille
MARGE = 0.5                  # écart minimal entre cours (courtyards), mm
PAS = 0.25                   # résolution de la grille d'occupation, mm


def mm(v):
    return pcbnew.FromMM(v)


# ===========================================================================
# 1. Bibliothèque d'empreintes du projet
# ===========================================================================
def pad_txt(num, x, y, w, h):
    return (f'  (pad "{num}" smd rect (at {x:g} {y:g}) (size {w:g} {h:g}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask"))')


def entete(nom, descr, ref_y, val_y):
    return [f'(footprint "{nom}" (version 20221018) (generator gen_pcb)', '  (layer "F.Cu")',
            f'  (descr "{descr}")', "  (attr smd)",
            f'  (fp_text reference "REF**" (at 0 {ref_y:g}) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))',
            f'  (fp_text value "{nom}" (at 0 {val_y:g}) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))',
            '  (fp_text user "${REFERENCE}" (at 0 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))']


def rect(x0, y0, x1, y1, couche, ep):
    return (f"  (fp_rect (start {x0:g} {y0:g}) (end {x1:g} {y1:g}) (stroke (width {ep:g}) (type solid)) "
            f'(fill none) (layer "{couche}"))')


def empreinte_tsop86():
    """TSOP-II 86 broches 400 mil, pas 0,5 mm (SDRAM IS42S32800J-6TL), d'après le TSOP-II-54 KiCad."""
    nom = EMPREINTES["TSOP2_86"][1]
    L = entete(nom, "TSOP-II 86 broches, corps 22,22 x 10,16 mm, pas 0,5 mm (IS42S32800J-6TL)", -12.6, 12.6)
    L += [rect(-5.08, -11.11, 5.08, 11.11, "F.Fab", 0.1), rect(-6.76, -11.5, 6.76, 11.5, "F.CrtYd", 0.05),
          '  (fp_line (start -5.2 -11.23) (end 5.2 -11.23) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))',
          '  (fp_line (start -5.2 11.23) (end 5.2 11.23) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))',
          '  (fp_circle (center -6.3 -11.2) (end -6.1 -11.2) (stroke (width 0.12) (type solid)) (fill solid) (layer "F.SilkS"))']
    for i in range(43):
        y = -10.5 + 0.5 * i
        L.append(pad_txt(i + 1, -5.75, round(y, 3), 1.51, 0.3))     # 1 → 43 côté gauche, de haut en bas
        L.append(pad_txt(86 - i, 5.75, round(y, 3), 1.51, 0.3))     # 44 → 86 côté droit, de bas en haut
    return nom, "\n".join(L) + "\n)\n"


def empreinte_a7670(source):
    """Module SIMCom A7670E LGA-124 24 × 24 mm : pastilles reprises de l'empreinte publiée (2 sources identiques)."""
    nom = EMPREINTES["SIMCOM_A7670_LCC_LGA"][1]
    t = open(source, encoding="utf-8").read()
    pads = []
    for m in re.finditer(r'\(pad\s+"?([^"\s]+)"?\s+smd\s+(\w+)\s*\(at\s+([-\d.]+)\s+([-\d.]+)[^)]*\)\s*\(size\s+([-\d.]+)\s+([-\d.]+)', t):
        pads.append((m.group(1), float(m.group(3)), float(m.group(4)), float(m.group(5)), float(m.group(6))))
    assert len(pads) == 124, len(pads)
    xs = [p[1] - p[3] / 2 for p in pads] + [p[1] + p[3] / 2 for p in pads]
    ys = [p[2] - p[4] / 2 for p in pads] + [p[2] + p[4] / 2 for p in pads]
    c = max(max(map(abs, xs)), max(map(abs, ys))) + 0.3
    L = entete(nom, "SIMCom A7670E LCC+LGA 124 pastilles 24 x 24 mm", -c - 1.3, c + 1.3)
    L += [rect(-12, -12, 12, 12, "F.Fab", 0.1), rect(-c, -c, c, c, "F.CrtYd", 0.05), rect(-c + 0.15, -c + 0.15, c - 0.15, c - 0.15, "F.SilkS", 0.12),
          f'  (fp_circle (center {-c - 0.6:g} {-8:g}) (end {-c - 0.4:g} {-8:g}) (stroke (width 0.12) (type solid)) (fill solid) (layer "F.SilkS"))']
    for n, x, y, w, h in pads:
        L.append(pad_txt(n, x, y, w, h))
    return nom, "\n".join(L) + "\n)\n"


def construire_bibliotheque(kf, a7670_source):
    os.makedirs(BIBLI, exist_ok=True)
    for lib, nom in set(EMPREINTES.values()):
        if lib != "PERSO":
            shutil.copy(os.path.join(kf, lib + ".pretty", nom + ".kicad_mod"), BIBLI)
    for nom, txt in (empreinte_tsop86(), empreinte_a7670(a7670_source)):
        open(os.path.join(BIBLI, nom + ".kicad_mod"), "w", encoding="utf-8").write(txt)
    print(f"Bibliothèque {BIBLI} : {len(os.listdir(BIBLI))} empreintes")


# ===========================================================================
# 2. Plan de placement
# ===========================================================================
# Connecteurs et éléments imposés : repère -> (x, y, rotation°) en coordonnées carte
FIXES = {
    # Bord haut, de gauche à droite
    "J802": (14, 6, 0), "J801": (32, 9, 180), "J701": (62, 11, 180), "J102": (88, 6, 180),
    "J201": (106, 10, 180), "J301": (140, 5, 0), "J303": (168, 5, 0), "J302": (184, 6, 0),
    "J101": (200, 7, 0), "J103": (209, 6, 90), "J104": (220, 6, 90),
    "J1": (239, 7, 180), "J2": (263, 7, 180), "J3": (287, 7, 180),
    # MCU et SDRAM côte à côte (liaisons FMC courtes, broches face à face)
    "U101": (140, 74, 0), "U201": (170, 74, 0),
    # Bord gauche
    "J601": (7, 105, 270), "J507": (6, 125, 0),
    "D519": (6, 140, 90), "D520": (6, 146, 90), "D521": (6, 152, 90),
    "D522": (6, 158, 90), "D523": (6, 164, 90), "D524": (6, 170, 90),
}
# Bornier de zones : 8 blocs de 6 points le long du bord bas, puis AUX 12 V
PAS_BLOC = 32.0
for b in range(8):
    FIXES[f"J{401 + b}"] = (12 + PAS_BLOC * b, H - 7, 0)
FIXES["J409"] = (12 + PAS_BLOC * 8, H - 7, 0)
# Bornier des sorties le long du bord droit
for i in range(6):
    FIXES[f"J{501 + i}"] = (W - 7, 48 + 25 * i, 90)        # entrée des fils vers le bord droit
TROUS = [(5, 5), (W - 5, 5), (5, H - 5), (W - 5, H - 5)]

# Régions (x0, y0, x1, y1) par feuillet, en coordonnées carte
REGIONS = {
    8: (14, 35, 64, 96),      # 4G (sous la zone d'insertion de la carte SIM)
    7: (66, 24, 100, 56),     # Ethernet, près du RJ45
    3: (116, 14, 200, 42),    # écran, près des connecteurs de dalle
    1: (102, 44, 182, 100),   # MCU
    2: (184, 44, 222, 100),   # mémoires, à droite du MCU (SDRAM au plus près)
    0: (224, 15, 286, 100),   # alimentation, près des borniers d'entrée
    6: (14, 98, 40, 130),     # audio, près du haut-parleur
    4: (42, 98, 118, 164),    # zones : multiplexeurs (les voies sont en colonnes)
    5: (120, 102, 286, 164),  # sorties : tampon, registres, relais
}


def feuillet(ref):
    return int(re.search(r"\d+", ref).group(0)) // 100


# ===========================================================================
# 3. Grille d'occupation
# ===========================================================================
class Grille:
    def __init__(self):
        self.nx, self.ny = int(W / PAS), int(H / PAS)
        self.occ = np.zeros((self.ny, self.nx), dtype=np.uint8)
        self._maj()

    def _maj(self):
        self.integ = np.pad(self.occ.astype(np.int32).cumsum(0).cumsum(1), ((1, 0), (1, 0)))

    def indices(self, x0, y0, x1, y1):
        return (max(0, int(np.floor(x0 / PAS))), max(0, int(np.floor(y0 / PAS))),
                min(self.nx, int(np.ceil(x1 / PAS))), min(self.ny, int(np.ceil(y1 / PAS))))

    def libre(self, x0, y0, x1, y1):
        if x0 < 1 or y0 < 1 or x1 > W - 1 or y1 > H - 1:
            return False
        i0, j0, i1, j1 = self.indices(x0, y0, x1, y1)
        s = self.integ[j1, i1] - self.integ[j0, i1] - self.integ[j1, i0] + self.integ[j0, i0]
        return s == 0

    def occuper(self, x0, y0, x1, y1):
        i0, j0, i1, j1 = self.indices(x0 - MARGE, y0 - MARGE, x1 + MARGE, y1 + MARGE)
        self.occ[j0:j1, i0:i1] = 1
        self._maj()

    def chercher(self, w, h, region, pas=1.0):
        """Première position libre (balayage ligne par ligne) dans la région, sinon sur toute la carte."""
        zones = []
        if region:
            zones = [region, (region[0] - 25, region[1] - 25, region[2] + 25, region[3] + 25)]
        for (x0, y0, x1, y1) in zones + [(1, 1, W - 1, H - 1)]:
            x0, y0, x1, y1 = max(x0, 1), max(y0, 1), min(x1, W - 1), min(y1, H - 1)
            y = y0
            while y + h <= y1:
                x = x0
                while x + w <= x1:
                    if self.libre(x, y, x + w, y + h):
                        return x, y
                    x += pas
                y += pas
        raise RuntimeError(f"Plus de place pour un bloc {w:.1f} × {h:.1f} mm")


# ===========================================================================
# 4. Construction de la carte
# ===========================================================================
def charger(c):
    lib, nom = EMPREINTES[c.empreinte]
    fp = pcbnew.FootprintLoad(BIBLI, nom)
    if fp is None:
        raise SystemExit(f"Empreinte introuvable : {nom}")
    fp.SetFPID(pcbnew.LIB_ID("alarme32", nom))
    return fp


def cour(fp):
    """Rectangle de la cour (courtyard) de l'empreinte, en mm, coordonnées absolues
    (calculé à partir des tracés F/B.CrtYd ; à défaut, boîte englobante)."""
    bb = None
    for it in fp.GraphicalItems():
        if it.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            b = it.GetBoundingBox()
            if bb is None:
                bb = b
            else:
                bb.Merge(b)
    if bb is None:
        bb = fp.GetBoundingBox(False, False)
    return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def exclusions(fp):
    """Rectangles des zones d'exclusion (keepout) intégrées à l'empreinte, en coordonnées carte."""
    out = []
    for z in fp.Zones():
        b = z.GetBoundingBox()
        out.append((pcbnew.ToMM(b.GetLeft()) - OX, pcbnew.ToMM(b.GetTop()) - OY,
                    pcbnew.ToMM(b.GetRight()) - OX, pcbnew.ToMM(b.GetBottom()) - OY))
    return out


def poser(fp, x, y, rot=0):
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)))


def main():
    args = sys.argv[1:]
    if "--bibliotheque" in args:
        kf = args[args.index("--bibliotheque") + 1]
        a76 = args[args.index("--a7670") + 1]
        construire_bibliotheque(kf, a76)

    B = gen_netlist.B
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    tb = board.GetTitleBlock()
    tb.SetTitle("Centrale d'alarme 32 zones / 6 sorties - circuit imprime")
    tb.SetRevision("A")
    tb.SetComment(0, "Placement genere par gen_pcb.py - routage a realiser")
    ds = board.GetDesignSettings()
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.5)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_MinClearance = mm(0.15)
    ds.m_CopperEdgeClearance = mm(0.3)

    # Équipotentielles
    nets = {}
    for c in B.composants.values():
        for n in c.net.values():
            if n and n != "?" and n not in nets:
                ni = pcbnew.NETINFO_ITEM(board, n)
                board.Add(ni)
                nets[n] = ni

    # Contour et trous de fixation
    for (x0, y0, x1, y1) in ((0, 0, W, 0), (W, 0, W, H), (W, H, 0, H), (0, H, 0, 0)):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(mm(OX + x0), mm(OY + y0)))
        s.SetEnd(pcbnew.VECTOR2I(mm(OX + x1), mm(OY + y1)))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)
    grille = Grille()
    for i, (x, y) in enumerate(TROUS):
        fp = pcbnew.FootprintLoad(BIBLI, EMPREINTES["TROU_M3"][1])
        fp.SetFPID(pcbnew.LIB_ID("alarme32", EMPREINTES["TROU_M3"][1]))
        fp.SetReference(f"H{i + 1}")
        fp.SetValue("M3")
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_BOARD_ONLY | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        poser(fp, x, y)
        for p in fp.Pads():
            p.SetNet(nets["GND"])
        board.Add(fp)
        grille.occuper(*[v - o for v, o in zip(cour(fp), (OX, OY, OX, OY))])

    # Chemins des symboles (lien schéma ↔ PCB), lus dans la netlist exportée par KiCad depuis le schéma
    uid_feuille = {i: gen_schema.uid("feuillet", f) for i, (f, _, _) in enumerate(gen_schema.FEUILLETS)}
    chemins = {}
    try:
        import subprocess, tempfile
        from sexpr import parse, find, findall
        net = os.path.join(tempfile.mkdtemp(), "sch.net")
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", net,
                        os.path.join(PROJET, "alarme32.kicad_sch")], capture_output=True, check=True)
        for comp in findall(find(parse(open(net, encoding="utf-8").read())[0], "components"), "comp"):
            ts = find(comp, "tstamps") or find(comp, "tstamp")
            chemins[str(find(comp, "ref")[1])] = str(find(find(comp, "sheetpath"), "tstamps")[1]) + str(ts[1])
    except Exception as e:  # noqa: BLE001
        print("Avertissement : chemins du schéma recalculés (kicad-cli indisponible) :", e)

    def preparer(c, rot=0):
        fp = charger(c)
        fp.SetReference(c.ref)
        fp.SetValue(c.valeur)
        i = feuillet(c.ref)
        fichier = gen_schema.FEUILLETS[i][0] + ".kicad_sch"
        sym = gen_schema.uid(fichier, c.ref, 1)
        fp.SetPath(pcbnew.KIID_PATH(chemins.get(c.ref, f"/{uid_feuille[i]}/{sym}")))
        if hasattr(fp, "SetSheetfile"):
            fp.SetSheetfile(fichier)
            fp.SetSheetname(gen_schema.FEUILLETS[i][1])
        if "ThermalVias" in str(fp.GetFPID().GetLibItemName()):
            fp.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)     # pastille thermique reliée pleinement au plan
        if c.valeur == "NC":
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        # Nets des pastilles ; les vias thermiques sans numéro prennent le net de la pastille qui les contient
        numerotees = []
        for p in fp.Pads():
            n = p.GetNumber()
            if n in c.net:
                if c.net[n] and c.net[n] != "?":
                    p.SetNet(nets[c.net[n]])
                numerotees.append(p)
        for p in fp.Pads():
            if p.GetNumber() == "":
                for q in numerotees:
                    if q.HitTest(p.GetPosition()):
                        p.SetNet(q.GetNet())
        manquantes = set(c.net) - {p.GetNumber() for p in fp.Pads()}
        if manquantes:
            raise SystemExit(f"{c.ref} : broches {sorted(manquantes)} absentes de l'empreinte {fp.GetFPID().GetLibItemName()}")
        # Références sérigraphiées réduites sur les petits boîtiers (carte dense)
        if c.empreinte in ("R0603", "C0603", "C0805", "SOD323", "SOD123", "SOT23", "SOT23_5", "SOT23_6", "TP_1MM"):
            t = fp.Reference()
            t.SetTextSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6)))
            t.SetTextThickness(mm(0.1))
        poser(fp, 0, 0, rot)
        return fp

    places = set()
    tous = list(B.composants.values())

    def fixer(c, x, y, rot):
        fp = preparer(c, rot)
        poser(fp, x, y, rot)
        x0, y0, x1, y1 = [v - o for v, o in zip(cour(fp), (OX, OY, OX, OY))]
        # recale à l'intérieur de la carte si la cour dépasse
        dx = max(0, 1 - x0) - max(0, x1 - (W - 1))
        dy = max(0, 1 - y0) - max(0, y1 - (H - 1))
        if dx or dy:
            poser(fp, x + dx, y + dy, rot)
            x0, y0, x1, y1 = x0 + dx, y0 + dy, x1 + dx, y1 + dy
        board.Add(fp)
        grille.occuper(x0, y0, x1, y1)
        for r in exclusions(fp):
            grille.occuper(*r)
        places.add(c.ref)

    # 4a. Connecteurs et éléments fixes
    for c in tous:
        if c.ref in FIXES:
            fixer(c, *FIXES[c.ref])

    # 4b. Voies d'entrée de zone : une colonne alignée sur chaque borne
    par_zone = {}
    for c in tous:
        if feuillet(c.ref) == 4 and not c.ref.startswith(("J", "U")):
            nets_c = {n for n in c.net.values()}
            z = next((int(m.group(1)) for n in nets_c for m in [re.match(r"Z[NAO]N?E?(\d+)$", n)] if m), None)
            if z:
                par_zone.setdefault(z, []).append(c)
    for z, comps in par_zone.items():
        b, k = (z - 1) // 4, (z - 1) % 4
        xb = 12 + PAS_BLOC * b + 5.08 * (k if k < 2 else k + 1)      # broches 1, 2, 4, 5 du bornier
        y = H - 17.0
        ordre = sorted(comps, key=lambda c: ["D_TVS", "R470", "BAV", "R4.7", "R10", "C"].index(
            "D_TVS" if c.valeur == "SMAJ15A" else "R470" if c.valeur == "470R" else "BAV" if c.valeur == "BAV99"
            else "R4.7" if c.valeur == "4.7k" else "R10" if c.valeur == "10k" else "C"))
        for c in ordre:
            fp = preparer(c, 90)
            x0, y0, x1, y1 = cour(fp)
            h = y1 - y0
            poser(fp, xb, y - h / 2, 90)
            x0, y0, x1, y1 = [v - o for v, o in zip(cour(fp), (OX, OY, OX, OY))]
            board.Add(fp)
            grille.occuper(x0, y0, x1, y1)
            places.add(c.ref)
            y -= h + 0.5

    # 4c. Groupes fonctionnels (composant principal + ses passifs) dans leur région
    restants = [c for c in tous if c.ref not in places]
    groupes, cur = [], []
    for c in restants:
        majeur = re.match(r"[A-Z]+", c.ref).group(0) in ("U", "Q", "K", "X", "BT", "J")
        if cur and (majeur or feuillet(c.ref) != feuillet(cur[0].ref) or len(cur) >= 8):
            groupes.append(cur)
            cur = []
        cur.append(c)
    if cur:
        groupes.append(cur)
    # les plus gros groupes d'abord dans chaque région
    groupes.sort(key=lambda g: (feuillet(g[0].ref), -len(g)))
    hors_region = []
    for g in groupes:
        fps = [preparer(c) for c in g]
        tailles = []
        for fp in fps:
            x0, y0, x1, y1 = cour(fp)
            tailles.append((x1 - x0, y1 - y0, x0, y0))
        # rangement en étagères à l'intérieur du groupe
        aire = sum(w * h for w, h, _, _ in tailles)
        larg = max(max(w for w, _, _, _ in tailles), (aire ** 0.5) * 1.3)
        pos, x, y, hl, wmax = [], 0.0, 0.0, 0.0, 0.0
        for (w, h, _, _) in tailles:
            if x > 0 and x + w > larg:
                x, y, hl = 0.0, y + hl + MARGE * 2, 0.0
            pos.append((x, y))
            x += w + MARGE * 2
            hl = max(hl, h)
            wmax = max(wmax, x)
        gw, gh = wmax, y + hl
        reg = REGIONS.get(feuillet(g[0].ref))
        gx, gy = grille.chercher(gw + 2 * MARGE, gh + 2 * MARGE, reg)
        if reg and not (reg[0] <= gx and gx + gw <= reg[2] + 0.01 and reg[1] <= gy and gy + gh <= reg[3] + 0.01):
            hors_region.append(g[0].ref)
        for c, fp, (px, py), (w, h, x0, y0) in zip(g, fps, pos, tailles):
            # x0, y0 : coin de la cour relatif à l'origine de l'empreinte posée en (0, 0) + origine carte
            ax = gx + MARGE + px - (x0 - OX)
            ay = gy + MARGE + py - (y0 - OY)
            poser(fp, ax, ay, fp.GetOrientationDegrees())
            board.Add(fp)
            places.add(c.ref)
        grille.occuper(gx, gy, gx + gw + 2 * MARGE, gy + gh + 2 * MARGE)

    assert places == set(B.composants), set(B.composants) - places
    if hors_region:
        print("Groupes placés hors de leur région :", hors_region)

    # 5. Plans de masse
    for couche in (pcbnew.In1_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(couche)
        z.SetNet(nets["GND"])
        z.SetIsRuleArea(False)
        z.SetLocalClearance(mm(0.5))       # >= isolement de la classe « Relais »
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in ((0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)):
            ol.Append(mm(OX + x), mm(OY + y))
        z.SetZoneName(f"GND_{board.GetLayerName(couche)}")
        board.Add(z)

    # 6. Textes
    t = pcbnew.PCB_TEXT(board)
    t.SetText("Centrale alarme 32 zones / 6 sorties - rev A - placement automatique, routage a faire")
    t.SetPosition(pcbnew.VECTOR2I(mm(OX + 150), mm(OY + H - 1.5)))
    t.SetLayer(pcbnew.B_SilkS)
    t.SetMirrored(True)
    t.SetTextSize(pcbnew.VECTOR2I(mm(1.5), mm(1.5)))
    board.Add(t)

    sortie = os.path.join(PROJET, "alarme32.kicad_pcb")
    pcbnew.SaveBoard(sortie, board)
    # Table des bibliothèques d'empreintes du projet
    open(os.path.join(PROJET, "fp-lib-table"), "w").write(
        '(fp_lib_table\n  (version 7)\n  (lib (name "alarme32")(type "KiCad")(uri "${KIPRJMOD}/alarme32.pretty")'
        '(options "")(descr "Empreintes de la centrale d\'alarme"))\n)\n')
    # Classes d'équipotentielles dans le fichier projet
    pro = os.path.join(PROJET, "alarme32.kicad_pro")
    d = json.load(open(pro, encoding="utf-8"))
    d["meta"] = {"filename": "alarme32.kicad_pro", "version": 1}
    classe = lambda nom, piste, isol, via, perce: {
        "name": nom, "track_width": piste, "clearance": isol, "via_diameter": via, "via_drill": perce,
        "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2, "diff_pair_gap": 0.25,
        "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
        "pcb_color": "rgba(0, 0, 0, 0.000)"}
    d["net_settings"] = {
        "classes": [classe("Default", 0.2, 0.2, 0.6, 0.3), classe("Puissance", 0.6, 0.2, 0.8, 0.4),
                    classe("Relais", 1.0, 0.5, 1.0, 0.5), classe("SDRAM", 0.15, 0.15, 0.5, 0.25),
                    classe("LVDS_100R", 0.2, 0.2, 0.6, 0.3)],
        "meta": {"version": 3}, "net_colors": None, "netclass_assignments": None,
        "netclass_patterns": [{"netclass": "Puissance", "pattern": p} for p in
                              ("VIN*", "VPSC_IN", "VBAT_*", "P5V", "P5V_OUT", "P3V8_GSM", "P12V_*", "SW_*", "BB_SW*", "BB_OUT", "AUX_SW")]
                             + [{"netclass": "Relais", "pattern": "OUT*"}]
                             + [{"netclass": "SDRAM", "pattern": "SDR_*"}]
                             + [{"netclass": "LVDS_100R", "pattern": "LVDS_*"}]}
    d["board"] = {"design_settings": {"rules": {
        "min_clearance": 0.15, "min_track_width": 0.15, "min_via_diameter": 0.45, "min_via_annular_width": 0.1,
        "min_through_hole_diameter": 0.2, "min_hole_to_hole": 0.25, "min_hole_clearance": 0.2,
        "min_copper_edge_clearance": 0.3, "min_silk_clearance": 0.0, "min_resolved_spokes": 1,
        "min_text_height": 0.5, "min_text_thickness": 0.08, "solder_mask_to_copper_clearance": 0.0,
        "min_microvia_diameter": 0.2, "min_microvia_drill": 0.1, "use_height_for_length_calcs": True}}}
    json.dump(d, open(pro, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    # Format de page A3 (non exposé par l'API Python de KiCad 7)
    txt = open(sortie, encoding="utf-8").read()
    open(sortie, "w", encoding="utf-8").write(re.sub(r'\(paper "[^"]*"\)', '(paper "A3")', txt, count=1))
    # Remplissage des plans de masse sur la carte rechargée (plus robuste)
    b2 = pcbnew.LoadBoard(sortie)
    b2.SynchronizeNetsAndNetClasses(False)
    b2.BuildConnectivity()
    pcbnew.ZONE_FILLER(b2).Fill(b2.Zones())
    pcbnew.SaveBoard(sortie, b2)
    # Export Specctra pour un essai d'autoroutage (Freerouting)
    pcbnew.ExportSpecctraDSN(b2, os.path.join(PROJET, "alarme32.dsn"))
    print(f"Carte écrite : {sortie} ({len(list(b2.GetFootprints()))} empreintes, {len(nets)} équipotentielles)")


if __name__ == "__main__":
    main()
