#!/usr/bin/env python3
"""
Génère le schéma KiCad (format KiCad 7, ouvrable par KiCad 7 à 10) de la
centrale d'alarme, à partir de la description du circuit de
hardware/netlist/gen_netlist.py (même source que la netlist OrCAD).

  - un projet alarme32.kicad_pro + une feuille racine alarme32.kicad_sch ;
  - 9 feuillets hiérarchiques (alimentation, MCU, mémoires, écran, zones,
    sorties, audio, Ethernet, 4G) ;
  - symboles intégrés au fichier (bibliothèques KiCad, voir sources/) ;
  - chaque broche reliée par une étiquette globale portant le nom de
    l'équipotentielle ; broches libres marquées « non connectée » (X).

Usage : python3 gen_schema.py   (puis verif_schema.sh pour la validation KiCad)
"""
import json
import math
import os
import re
import sys
import uuid
from collections import defaultdict

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ICI, "..", "netlist"))
import gen_netlist  # noqa: E402  (construit la carte B à l'import)

B = gen_netlist.B
if B.erreurs:
    sys.exit("La description du circuit contient des erreurs : lancer gen_netlist.py")
SYM = json.load(open(os.path.join(ICI, "sources", "symboles.json"), encoding="utf-8"))
PROJET = "alarme32"
LIB = "alarme32"
SORTIE = os.path.join(ICI, "alarme32")
G = 2.54

FEUILLETS = [
    ("alimentation", "1 - Alimentation secourue et rails",
     ["Bloc PSC-100A 13,8 V, diode idéale, mesure batterie INA226, régulateurs 5 V / 3,8 V / 3,3 V / LDO.",
      "Buck-boost LM5176 (+12 V rétroéclairage) : valeurs à recalculer avec TI WEBENCH.",
      "Fusible batterie 7,5 A à placer dans le câble, au plus près de la batterie."]),
    ("mcu", "2 - Microcontrôleur STM32H743BIT6",
     ["Un condensateur 100 nF par broche VDD, au plus près des broches.",
      "TPS3823 : la diode Schottky laisse la sortie push-pull tirer NRST à 0 uniquement."]),
    ("memoires", "3 - SDRAM, Flash QSPI, microSD, EEPROM MAC",
     ["Pistes FMC appariées en longueur (± 5 mm), résistance série 22 Ω sur l'horloge près du MCU."]),
    ("ecran", "4 - Écran LVDS 1024x768 et tactile",
     ["Correspondance LTDC -> SN75LVDS83B au format VESA 24 bits.",
      "Brochage du connecteur de dalle GÉNÉRIQUE : à adapter à la dalle retenue.",
      "Paires LVDS 100 Ω différentiel, longueurs appariées."]),
    ("zones", "5 - 32 entrées de zones",
     ["Par zone : tirage 4,7 kΩ vers VZONE, série 470 Ω 0,5 W, écrêtage BAV99, TVS SMAJ15A, filtre 10 kΩ / 100 nF.",
      "Résistances de fin de ligne 4,7 kΩ à placer DANS les détecteurs."]),
    ("sorties", "6 - Sorties relais, LED, autoprotection",
     ["Tampon 74AHCT125 obligatoire : le TPIC6B595 en 5 V exige VIH >= 4,25 V.",
      "Par sortie : K_MAIN (puissance), K_SRC (sec / alimenté), K_VSEL (12 V / 5 V).",
      "Contacts limités à la TBTS (<= 30 V)."]),
    ("audio", "7 - Audio (MAX98357A)", ["Fichiers WAV 16 kHz 16 bits mono sur microSD."]),
    ("ethernet", "8 - Ethernet (LAN8742A)",
     ["Straps : MODE = 111, PHYAD0 = 0, nINTSEL = 0 (sortie REFCLKO 50 MHz vers PA1)."]),
    ("gsm", "9 - Module 4G SIMCom A7670E",
     ["Condensateur polymère 470 µF à moins de 10 mm des broches VBAT.",
      "Piste d'antenne 50 Ω coplanaire, réseau en π (condensateurs non montés par défaut)."]),
]

# Symbole de chaque composant générique (sans base de brochage)
GENERIQUE = {"RES": "R", "CAP": "C", "CAPPOL": "C_Polarized", "IND": "L", "FERRITE": "FerriteBead",
             "TVS": "D_Zener", "ESD": "D_Zener", "DIODE": "D", "SCHOTTKY": "D_Schottky", "LED": "LED",
             "XTAL4": "Crystal_GND24", "XTAL2": "Crystal", "BATT_HOLDER": "Battery_Cell",
             "SW_PUSH": "SW_Push", "JUMPER2": "Conn_01x02", "TESTPOINT": "TestPoint"}


def uid(*parts):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "alarme32/" + "/".join(map(str, parts))))


def f(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


# ---------------------------------------------------------------------------
# Symboles
# ---------------------------------------------------------------------------
def symbole_boite(nom, broches):
    """Symbole rectangulaire généré (composants absents des bibliothèques KiCad)."""
    nums = sorted(broches, key=lambda n: int(n) if n.isdigit() else 999)
    moitie = (len(nums) + 1) // 2
    gauche, droite = nums[:moitie], nums[moitie:]
    h = max(len(gauche), len(droite)) * G + G
    w = 20.32
    pins = []
    for i, n in enumerate(gauche):
        pins.append({"type": "passive", "style": "line", "x": -w / 2 - G * 2, "y": h / 2 - G * (i + 1),
                     "angle": 0, "length": G * 2, "hide": False, "name": broches[n], "number": n})
    for i, n in enumerate(droite):
        pins.append({"type": "passive", "style": "line", "x": w / 2 + G * 2, "y": h / 2 - G * (i + 1),
                     "angle": 180, "length": G * 2, "hide": False, "name": broches[n], "number": n})
    rect = f"(rectangle (start {f(-w / 2)} {f(h / 2)}) (end {f(w / 2)} {f(-h / 2)}) " \
           f"(stroke (width 0.254) (type default)) (fill (type background)))"
    return {"pin_names_offset": 0.508, "pin_names_hide": False, "pin_numbers_hide": False, "ref": "U",
            "units": {"0": {"graphics": [rect], "pins": []}, "1": {"graphics": [], "pins": pins}}}


def symbole_de(c):
    if c.base:
        if c.base in SYM:
            return c.base, SYM[c.base]
        nom = re.sub(r"[^A-Za-z0-9_-]", "_", c.base)
        return nom, symbole_boite(nom, c.broches)
    if c.device == "FUSE":
        n = "Fuse" if c.valeur == "5A" else "Polyfuse"
    elif c.device.startswith("CONN"):
        n = f"Conn_01x{len(c.broches):02d}"
    else:
        n = GENERIQUE[c.device]
    return n, SYM[n]


def lib_symbol(nom, s):
    """Définition de symbole au format KiCad 7 (bloc lib_symbols)."""
    pn = f"(pin_names (offset {f(s['pin_names_offset'])}){' hide' if s['pin_names_hide'] else ''})"
    out = [f'  (symbol "{LIB}:{nom}"' + (" (pin_numbers hide)" if s["pin_numbers_hide"] else "") +
           f" {pn} (in_bom yes) (on_board yes)"]
    for k, v, cache in (("Reference", s["ref"], False), ("Value", nom, False),
                        ("Footprint", "", True), ("Datasheet", "", True)):
        out.append(f"    (property {q(k)} {q(v)} (at 0 0 0) (effects (font (size 1.27 1.27)){' hide' if cache else ''}))")
    for u, d in sorted(s["units"].items()):
        out.append(f'    (symbol "{nom}_{u}_1"')
        for g in d["graphics"]:
            out.append("      " + g)
        for p in d["pins"]:
            typ = p["type"]
            if p["hide"] and typ == "power_in":
                typ = "passive"          # évite les connexions implicites par nom (broches cachées)
            out.append(f"      (pin {typ} {p['style']} (at {f(p['x'])} {f(p['y'])} {p['angle']}) "
                       f"(length {f(p['length'])}){' hide' if p['hide'] else ''} "
                       f"(name {q(p['name'])} (effects (font (size 1.27 1.27)))) "
                       f"(number {q(p['number'])} (effects (font (size 1.27 1.27)))))")
        out.append("    )")
    out.append("  )")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Placement
# ---------------------------------------------------------------------------
DIRS = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}   # sens de l'étiquette (écran, y vers le bas)


def largeur_etiquette(net):
    return 1.05 * len(net) + 3.5


class Bloc:
    """Une unité d'un composant, avec ses étiquettes, à placer sur la feuille."""

    def __init__(self, c, nom_sym, s, unite):
        self.c, self.nom_sym, self.s, self.unite = c, nom_sym, s, unite
        pins = list(s["units"].get("0", {}).get("pins", [])) + \
            (list(s["units"][str(unite)]["pins"]) if str(unite) in s["units"] else [])
        graph = list(s["units"].get("0", {}).get("graphics", [])) + \
            list(s["units"].get(str(unite), {}).get("graphics", []))
        coords = [(float(a), float(b)) for g in graph
                  for a, b in re.findall(r"\((?:start|end|xy|center|mid) (-?[\d.]+) (-?[\d.]+)\)", g)]
        # Composant à 2 broches verticales : rotation de 90° (étiquettes horizontales, plus compact)
        self.rot = 90 if len(pins) == 2 and all(p["angle"] in (90, 270) for p in pins) else 0
        if self.rot:
            pins = [dict(p, x=-p["y"], y=p["x"], angle=(p["angle"] + 90) % 360) for p in pins]
            coords = [(-y, x) for x, y in coords]
        self.pins = pins
        xs = [x for x, _ in coords]; ys = [-y for _, y in coords]
        self.corps = (min(xs, default=0), min(ys, default=0), max(xs, default=0), max(ys, default=0))
        # Regroupe les broches par position de connexion
        self.points = defaultdict(list)
        for p in pins:
            self.points[(round(p["x"], 3), round(-p["y"], 3))].append(p)
        x0, y0, x1, y1 = self.corps
        for p in pins:
            x, y = p["x"], -p["y"]
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
        self.horizontal = any(p["angle"] in (0, 180) for p in pins)
        # Étendue des étiquettes
        for (x, y), grp in self.points.items():
            net = self.net_de(grp)
            if net is None:
                continue
            dx, dy = DIRS[grp[0]["angle"]]
            L = largeur_etiquette(net) if net != "?" else 1.5
            x0, x1 = min(x0, x + dx * L), max(x1, x + dx * L)
            y0, y1 = min(y0, y + dy * L - 1), max(y1, y + dy * L + 1)
        txt = 1.1 * max(len(c.ref), len(c.valeur))
        bx0, by0, bx1, by1 = self.corps
        if len(pins) <= 2 and self.horizontal:     # 2 broches : textes centrés au-dessus / au-dessous
            y0, y1 = min(y0, by0 - 4), max(y1, by1 + 4)
            x0, x1 = min(x0, (bx0 + bx1) / 2 - txt / 2), max(x1, (bx0 + bx1) / 2 + txt / 2)
        else:                                       # textes à droite du corps, au-dessus / au-dessous
            y0, y1 = min(y0, by0 - 3.5), max(y1, by1 + 3.5)
            x1 = max(x1, bx1 + 2 + txt)
        self.bbox = (x0 - 2, y0 - 2, x1 + 2, y1 + 2)

    def net_de(self, grp):
        nets = {self.c.net[p["number"]] for p in grp}
        if len(nets) > 1:
            raise SystemExit(f"{self.c.ref} : broches superposées {[p['number'] for p in grp]} sur des nets différents {nets}")
        n = nets.pop()
        return n if n else None


MAJEURS = ("U", "Q", "K", "J", "X", "BT")


def grouper(blocs):
    """Regroupe chaque composant « majeur » avec les petits composants qui le suivent."""
    groupes, cur, mineurs = [], [], 0
    for b in blocs:
        prefixe = re.match(r"[A-Z]+", b.c.ref).group(0)
        majeur = prefixe in MAJEURS
        if cur and (majeur and (b.c.ref != cur[-1].c.ref) or (not majeur and mineurs >= 6 and
                                                                 not any(re.match(r"[A-Z]+", x.c.ref).group(0) in MAJEURS for x in cur))):
            groupes.append(cur); cur, mineurs = [], 0
        cur.append(b)
        mineurs += 0 if majeur else 1
    if cur:
        groupes.append(cur)
    return groupes


def etageres(blocs, largeur_max):
    """Rangement en étagères ; positions relatives au coin haut-gauche du groupe."""
    x, y, h_ligne, pos, w_max = 0.0, 0.0, 0.0, [], 0.0
    for b in blocs:
        x0, y0, x1, y1 = b.bbox
        w = x1 - x0
        if x > 0 and x + w > largeur_max:
            x, y, h_ligne = 0.0, y + h_ligne + 2.54, 0.0
        ox = math.ceil((x - x0) / G) * G
        oy = math.ceil((y - y0) / G) * G
        pos.append((ox, oy))
        x = ox + x1 + 2.54
        h_ligne = max(h_ligne, oy + y1 - y)
        w_max = max(w_max, x)
    return pos, w_max, y + h_ligne


def placer(blocs, largeur_max):
    """Groupes compacts, puis rangement « skyline » (bas-gauche) des groupes sur la feuille."""
    groupes = grouper(blocs)
    rects = []
    for grp in groupes:
        aire = sum((b.bbox[2] - b.bbox[0]) * (b.bbox[3] - b.bbox[1]) for b in grp)
        larg = max(max(b.bbox[2] - b.bbox[0] for b in grp), math.sqrt(aire) * 1.4)
        pos, w, h = etageres(grp, min(larg, largeur_max))
        rects.append((grp, pos, math.ceil((w + 6) / G) * G, math.ceil((h + 6) / G) * G))
    ncol = int(largeur_max // G)
    ciel = [0.0] * ncol
    resultat = {}
    w_tot = h_tot = 0.0
    for grp, pos, w, h in rects:
        k = max(1, int(round(w / G)))
        meilleur = None
        for i in range(0, max(1, ncol - k + 1)):
            y = max(ciel[i:i + k])
            if meilleur is None or y < meilleur[0] - 1e-6:
                meilleur = (y, i)
        y, i = meilleur
        for j in range(i, min(ncol, i + k)):
            ciel[j] = y + h
        gx, gy = i * G, y
        for b, (bx, by) in zip(grp, pos):
            resultat[id(b)] = (gx + bx, gy + by)
        w_tot, h_tot = max(w_tot, gx + w), max(h_tot, y + h)
    return [resultat[id(b)] for b in blocs], w_tot, h_tot


FORMATS = [("A3", 420, 297), ("A2", 594, 420), ("A1", 841, 594), ("A0", 1189, 841)]


def feuille(idx, nom_fichier, titre, notes, comps, uuid_racine, uuid_feuille):
    libs, blocs = {}, []
    for c in comps:
        nom_sym, s = symbole_de(c)
        libs[nom_sym] = s
        unites = sorted(int(u) for u, d in s["units"].items() if u != "0" and d["pins"]) or [1]
        for u in unites:
            blocs.append(Bloc(c, nom_sym, s, u))
    # Choix du format : le plus petit où tout tient
    oy0 = 15 + 6 * (len(notes) + 1) + 6
    choix = None
    for fmt, W, H in FORMATS:
        pos, wu, hu = placer(blocs, W - 50)
        if wu <= W - 40 and hu <= H - oy0 - 50:
            choix = (fmt, W, H, pos)
            break
        if hu <= 1.35 * (H - oy0 - 50):
            # Dépasse un peu : format personnalisé, largeur du format, hauteur ajustée
            choix = ("User", W, math.ceil(hu + oy0 + 55), pos)
            break
    if choix is None:
        pos, wu, hu = placer(blocs, 1500)
        choix = ("User", math.ceil(wu + 50), math.ceil(hu + oy0 + 55), pos)
    fmt, W, H, pos = choix
    ox0 = 25.4
    L = [f'(kicad_sch (version 20230121) (generator eeschema)',
         f"  (uuid {uuid_feuille if idx else uuid_racine})",
         f'  (paper "{fmt}"' + (f" {f(W)} {f(H)})" if fmt == "User" else ")"),
         f'  (title_block (title {q(titre)}) (date "2026-11-26") (rev "A")',
         f'    (company "Centrale d\'alarme 32 zones / 6 sorties")',
         f'    (comment 1 "Généré par hardware/kicad/gen_schema.py depuis la description vérifiée de la netlist")'
         f'    (comment 2 "Les étiquettes globales de même nom sont reliées entre tous les feuillets"))',
         "  (lib_symbols"]
    L += [lib_symbol(n, s) for n, s in sorted(libs.items())]
    L.append("  )")
    # Notes
    for i, n in enumerate([titre] + notes):
        taille = 3.0 if i == 0 else 1.8
        L.append(f"  (text {q(n)} (at 25.4 {f(15 + i * 6)} 0) (effects (font (size {taille} {taille})"
                 f"{' bold' if i == 0 else ''}) (justify left bottom)) (uuid {uid(nom_fichier, 'note', i)}))")
    for b, (ox, oy) in zip(blocs, pos):
        X, Y = ox + ox0, oy + oy0
        c = b.c
        ident = uid(nom_fichier, c.ref, b.unite)
        x0, y0, x1, y1 = b.corps
        if len(b.pins) <= 2 and b.horizontal:
            xm = X + (x0 + x1) / 2
            pr, pv, just = (xm, Y + y0 - 1.5), (xm, Y + y1 + 2.8), None
        elif len(b.pins) <= 2:
            pr, pv, just = (X + x1 + 1.5, Y - 1.3), (X + x1 + 1.5, Y + 1.3), "left"
        else:
            pr, pv, just = (X + x1 + 1.5, Y + y0 - 1.2), (X + x1 + 1.5, Y + y1 + 2.5), "left"
        ja = f" (justify {just})" if just else ""
        L.append(f'  (symbol (lib_id "{LIB}:{b.nom_sym}") (at {f(X)} {f(Y)} {b.rot}) (unit {b.unite})')
        L.append(f"    (in_bom yes) (on_board yes) (dnp {'yes' if c.valeur == 'NC' else 'no'})")
        L.append(f"    (uuid {ident})")
        L.append(f'    (property "Reference" {q(c.ref)} (at {f(pr[0])} {f(pr[1])} {b.rot}) (effects (font (size 1.27 1.27)){ja}))')
        L.append(f'    (property "Value" {q(c.valeur)} (at {f(pv[0])} {f(pv[1])} {b.rot}) (effects (font (size 1.27 1.27)){ja}))')
        L.append(f'    (property "Footprint" "" (at {f(X)} {f(Y)} 0) (effects (font (size 1.27 1.27)) hide))')
        L.append(f'    (property "Datasheet" "" (at {f(X)} {f(Y)} 0) (effects (font (size 1.27 1.27)) hide))')
        L.append(f'    (property "Empreinte" {q(c.empreinte)} (at {f(X)} {f(Y)} 0) (effects (font (size 1.27 1.27)) hide))')
        L.append(f'    (property "Description" {q(c.desc)} (at {f(X)} {f(Y)} 0) (effects (font (size 1.27 1.27)) hide))')
        for p in b.pins:
            L.append(f'    (pin {q(p["number"])} (uuid {uid(ident, p["number"])}))')
        L.append(f'    (instances (project "{PROJET}" (path "/{uuid_racine}/{uuid_feuille}" '
                 f'(reference {q(c.ref)}) (unit {b.unite}))))')
        L.append("  )")
        # Étiquettes et marques de non-connexion
        for (px, py), grp in b.points.items():
            net = b.net_de(grp)
            if net is None:
                continue
            ax, ay = X + px, Y + py
            if net == "?":
                L.append(f"  (no_connect (at {f(ax)} {f(ay)}) (uuid {uid(ident, 'nc', px, py)}))")
                continue
            ang = {0: 180, 180: 0, 90: 270, 270: 90}[grp[0]["angle"]]
            just = "left" if ang in (0, 90) else "right"
            L.append(f"  (global_label {q(net)} (shape passive) (at {f(ax)} {f(ay)} {ang}) (fields_autoplaced)"
                     f" (effects (font (size 1.27 1.27)) (justify {just})) (uuid {uid(ident, 'lbl', px, py)})"
                     f' (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {f(ax)} {f(ay)} 0)'
                     f" (effects (font (size 1.27 1.27)) hide)))")
    L.append(")")
    open(os.path.join(SORTIE, nom_fichier), "w", encoding="utf-8").write("\n".join(L) + "\n")
    return (fmt if fmt != "User" else f"{W}x{H} mm"), len(blocs)


def main():
    os.makedirs(SORTIE, exist_ok=True)
    par_feuillet = defaultdict(list)
    for c in B.composants.values():
        n = int(re.search(r"\d+", c.ref).group(0)) // 100
        par_feuillet[n].append(c)
    u_racine = uid("racine")
    racine = [f'(kicad_sch (version 20230121) (generator eeschema)', f"  (uuid {u_racine})", '  (paper "A3")',
              '  (title_block (title "Centrale d\'alarme filaire 32 zones / 6 sorties - STM32H743") (date "2026-11-26") (rev "A")',
              '    (comment 1 "Feuille racine : un feuillet hiérarchique par fonction")',
              '    (comment 2 "Généré par hardware/kicad/gen_schema.py - la netlist OrCAD est issue de la même description"))',
              "  (lib_symbols)",
              '  (text "Centrale d\'alarme filaire 32 zones / 6 sorties" (at 25.4 25.4 0) (effects (font (size 5 5) bold) (justify left bottom)) '
              f"(uuid {uid('racine', 'titre')}))",
              '  (text "Double-cliquez sur un feuillet pour l\'ouvrir. Les étiquettes globales de même nom sont reliées." '
              f"(at 25.4 35 0) (effects (font (size 2 2)) (justify left bottom)) (uuid {uid('racine', 'aide')}))"]
    for i, (fichier, titre, notes) in enumerate(FEUILLETS):
        u = uid("feuillet", fichier)
        fmt, n = feuille(i + 1, fichier + ".kicad_sch", titre, notes, par_feuillet[i], u_racine, u)
        x, y = 25.4 + (i % 3) * 127, 55.88 + (i // 3) * 63.5
        racine.append(f"  (sheet (at {f(x)} {f(y)}) (size 101.6 38.1) (fields_autoplaced)"
                      f" (stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0000)) (uuid {u})"
                      f' (property "Sheetname" {q(titre)} (at {f(x)} {f(y - 0.7)} 0) (effects (font (size 1.8 1.8)) (justify left bottom)))'
                      f' (property "Sheetfile" "{fichier}.kicad_sch" (at {f(x)} {f(y + 38.7)} 0) (effects (font (size 1.27 1.27)) (justify left top)))'
                      f' (instances (project "{PROJET}" (path "/{u_racine}" (page "{i + 2}")))))')
        racine.append(f"  (text {q(f'{n} symboles - format {fmt}')} (at {f(x + 3)} {f(y + 20)} 0) "
                      f"(effects (font (size 2 2)) (justify left bottom)) (uuid {uid('racine', fichier)}))")
        print(f"  {fichier:14s} {n:4d} symboles, format {fmt}")
    racine.append('  (sheet_instances (path "/" (page "1")))')
    racine.append(")")
    open(os.path.join(SORTIE, f"{PROJET}.kicad_sch"), "w", encoding="utf-8").write("\n".join(racine) + "\n")
    json.dump({"meta": {"filename": f"{PROJET}.kicad_pro", "version": 1}, "schematic": {}, "boards": [],
               "sheets": [[u_racine, "Racine"]] + [[uid("feuillet", fi), t] for fi, t, _ in FEUILLETS]},
              open(os.path.join(SORTIE, f"{PROJET}.kicad_pro"), "w"), indent=2, ensure_ascii=False)
    print(f"Schéma écrit dans {SORTIE}")


if __name__ == "__main__":
    main()
