#!/usr/bin/env python3
"""
Routage automatique PARTIEL de la carte avec Freerouting.

Seules les liaisons non critiques sont confiées à l'autorouteur ; les autres
restent en chevelu pour un routage manuel :
  - critiques (exclues) : SDRAM, LTDC/LVDS, Ethernet RMII, quartz, USB, QSPI,
    SDMMC, antenne, carte SIM, nœuds des régulateurs à découpage ;
  - alimentations (exclues) : rails à router en pistes larges / plans ;
  - routées : signaux lents, contacts des relais (pistes 1 mm), masse (vias
    vers le plan In1.Cu).

Usage : /usr/bin/python3 route_auto.py <freerouting.jar> [passes]
        /usr/bin/python3 route_auto.py --importer <session.ses>
Le résultat est écrit dans alarme32/alarme32.kicad_pcb (zones re-remplies).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

import pcbnew

ICI = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(ICI, "alarme32", "alarme32.kicad_pcb")

CRITIQUES = re.compile(r"^(SDR_|SDCLK|LCD_|LVDS_|ETH_|PHY_|ANT_|OSC|USB_|QSPI_|SD_|SIM_|MDM_USB|BB_|SW_|BOOT_|"
                       r"FB_|VCC_|IDEAL_|SS_|CT_)")
ALIMENTATIONS = {"VIN", "VIN_F", "VPSC_IN", "VBAT_PSC", "VBAT_P", "VBAT_RTC", "VBAT_CELL", "P5V", "P5V_OUT",
                 "P3V3", "P3V3A", "P3V3_LCD", "P3V3_PHY", "P3V8_GSM", "P12V_LCD", "P12V_AUX", "P12V_OUT",
                 "VZONE", "MDM_1V8", "AUX_SW", "VCAP"}


def classe(net):
    if CRITIQUES.match(net) or net in ALIMENTATIONS:
        return None
    if re.match(r"^OUT\d", net):
        return "relais"
    if net == "GND":
        return "masse"
    if net.startswith("SPK_"):
        return "masse"           # même largeur (0,4 mm) pour le haut-parleur
    return "signal"


def main():
    if sys.argv[1] == "--drc":
        etape_drc(sys.argv[2])
        return
    if sys.argv[1] == "--retirer":
        etape_retirer(sys.argv[2])
        return
    if sys.argv[1] == "--importer":          # réimporte une session déjà calculée
        importer(sys.argv[2])
        return
    jar = sys.argv[1]
    passes = sys.argv[2] if len(sys.argv) > 2 else "40"
    tmp = tempfile.mkdtemp()
    dsn, ses = os.path.join(tmp, "carte.dsn"), os.path.join(tmp, "carte.ses")

    board = pcbnew.LoadBoard(PCB)
    # Repart d'une carte sans pistes ni vias
    for t in list(board.GetTracks()):
        board.Remove(t)
    pcbnew.ExportSpecctraDSN(board, dsn)
    txt = open(dsn, encoding="utf-8").read()

    # In1.Cu reste un plan de masse plein : couche « power », interdite aux pistes
    txt = re.sub(r"(\(layer In1\.Cu\s+\(type )signal\)", r"\1power)", txt)
    assert "(type power)" in txt
    # B.Cu sert aux pistes : on retire son plan (le plan GND B.Cu est re-rempli après coup)
    txt = re.sub(r"\(plane GND \(polygon B\.Cu[^)]*\)\)\s*", "", txt)

    # Réseau : on ne garde que les liaisons sélectionnées
    a = txt.index("  (network")
    b = txt.index("  (wiring")
    reseau = txt[a:b]
    nets = re.findall(r"\(net (\S+)\s+\(pins ([^)]*)\)\s*\)", reseau)
    garde = {n: p for n, p in nets if classe(n.strip('"'))}
    par_classe = {}
    for n in garde:
        par_classe.setdefault(classe(n.strip('"')), []).append(n)
    regles = {"signal": (200, 200), "masse": (400, 200), "relais": (1000, 500)}
    lignes = ["  (network"]
    for n, p in garde.items():
        lignes.append(f"    (net {n}\n      (pins {' '.join(p.split())})\n    )")
    for c, liste in par_classe.items():
        w, cl = regles[c]
        lignes.append(f'    (class {c} "" {" ".join(liste)}\n      (circuit (use_via Via[0-3]_600:300_um))\n'
                      f"      (rule (width {w}) (clearance {cl}))\n    )")
    lignes.append("  )\n")
    txt = txt[:a] + "\n".join(lignes) + txt[b:]
    open(dsn, "w", encoding="utf-8").write(txt)
    print(f"{len(garde)} équipotentielles confiées à Freerouting sur {len(nets)} "
          f"({', '.join(f'{c} {len(l)}' for c, l in par_classe.items())})")

    # -da : statistiques anonymes désactivées ; proxy invalide : aucun accès réseau pendant le routage.
    # Freerouting 1.9 exige un affichage : xvfb-run fournit un écran virtuel (Linux sans écran).
    env = dict(os.environ, JAVA_TOOL_OPTIONS="-Dhttp.proxyHost=127.0.0.1 -Dhttp.proxyPort=9 "
               "-Dhttps.proxyHost=127.0.0.1 -Dhttps.proxyPort=9")
    cmd = ["java", "-jar", jar, "-de", dsn, "-do", ses, "-mp", passes, "-mt", "4", "-da"]
    if not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a"] + cmd
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=7200, env=env)
    if not os.path.exists(ses):
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit("Freerouting n'a pas produit de session")
    print("Freerouting :", " ".join(r.stdout.split()[-30:]))

    importer(ses)


def importer(ses):
    """Lit la session Specctra (.ses) et crée pistes et vias avec l'API pcbnew
    (ImportSpecctraSES de KiCad 7 n'agit que dans l'éditeur graphique)."""
    sys.path.insert(0, ICI)
    from sexpr import parse, find, findall
    racine = parse(open(ses, encoding="utf-8").read())[0]
    routes = find(racine, "routes")
    res = find(routes, "resolution")
    echelle = {"um": 1e-3, "mm": 1.0, "mil": 0.0254}[str(res[1])] / float(res[2])   # unité -> mm
    board = pcbnew.LoadBoard(PCB)
    for t in list(board.GetTracks()):
        board.Remove(t)
    couches = {board.GetLayerName(l): l for l in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)}
    nb_seg = nb_via = 0

    def pt(x, y):
        return pcbnew.VECTOR2I(pcbnew.FromMM(float(x) * echelle), pcbnew.FromMM(-float(y) * echelle))

    for net in findall(find(routes, "network_out"), "net"):
        ni = board.FindNet(str(net[1]))
        for w in findall(net, "wire"):
            chemin = find(w, "path")
            couche, larg, xy = couches[str(chemin[1])], float(chemin[2]) * echelle, chemin[3:]
            pts = [pt(xy[i], xy[i + 1]) for i in range(0, len(xy) - 1, 2)]
            for p0, p1 in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(p0); t.SetEnd(p1); t.SetWidth(pcbnew.FromMM(larg))
                t.SetLayer(couche); t.SetNet(ni)
                board.Add(t); nb_seg += 1
        for v in findall(net, "via"):
            m = re.search(r"_(\d+):(\d+)_um", str(v[1]))
            d, perc = (int(m.group(1)) / 1000, int(m.group(2)) / 1000) if m else (0.6, 0.3)
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pt(v[2], v[3])); via.SetWidth(pcbnew.FromMM(d)); via.SetDrill(pcbnew.FromMM(perc))
            via.SetViaType(pcbnew.VIATYPE_THROUGH); via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); via.SetNet(ni)
            board.Add(via); nb_via += 1
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    print(f"Session importée : {nb_seg} segments de piste, {nb_via} vias")
    nettoyer()


def nettoyer(max_iter=5):
    """Supprime le routage fautif signalé par le DRC (isolement, trous) : l'équipotentielle
    concernée est rendue au chevelu ; pour la masse, seules les vias fautives et leurs amorces
    sont retirées. Répète jusqu'à disparition de ces violations.
    Chaque étape tourne dans un processus séparé (l'API Python de KiCad 7 supporte mal
    plusieurs chargements de carte dans un même processus)."""
    import tempfile
    rendues = set()
    for _ in range(max_iter):
        rap = os.path.join(tempfile.mkdtemp(), "drc.rpt")
        subprocess.run([sys.executable, __file__, "--drc", rap], check=True, capture_output=True)
        r = subprocess.run([sys.executable, __file__, "--retirer", rap], check=True, capture_output=True, text=True)
        nets = [l for l in r.stdout.splitlines() if l.startswith("RETIRE ")]
        if not nets and "AUCUNE" in r.stdout:
            break
        rendues |= {l[7:] for l in nets}
    if rendues - {"GND"}:
        print("Routage retiré (à faire à la main) :", ", ".join(sorted(rendues - {"GND"})))


def etape_drc(rap):
    pcbnew.WriteDRCReport(pcbnew.LoadBoard(PCB), rap, pcbnew.EDA_UNITS_MILLIMETRES, True)


def etape_retirer(rap):
    blocs = [x for x in re.split(r"\n(?=\[)", open(rap, encoding="utf-8").read())
             if x.startswith(("[clearance", "[hole_clearance", "[tracks_crossing", "[shorting_items", "[track_dangling"))]
    if not blocs:
        print("AUCUNE")
        return
    nets_a_retirer, vias_gnd, pendantes = set(), [], []
    for x in blocs:
        if x.startswith("[track_dangling"):
            m = re.search(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): Track \[([^\]]+)\]", x)
            if m:
                pendantes.append((m.group(3), (float(m.group(1)), float(m.group(2)))))
            continue
        for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): (Track|Via) \[([^\]]+)\]", x):
            px, py, genre, net = float(m.group(1)), float(m.group(2)), m.group(3), m.group(4)
            if net == "GND":
                if genre == "Via":
                    vias_gnd.append((px, py))
            else:
                nets_a_retirer.add(net)
    board = pcbnew.LoadBoard(PCB)
    proche = lambda p, q: abs(pcbnew.ToMM(p.x) - q[0]) < 0.02 and abs(pcbnew.ToMM(p.y) - q[1]) < 0.02
    for t in list(board.GetTracks()):
        n = t.GetNetname()
        if t.GetClass() == "PCB_TRACK" and any(n == pn and (proche(t.GetStart(), q) or proche(t.GetEnd(), q))
                                               for pn, q in pendantes):
            board.Remove(t)
            continue
        if n in nets_a_retirer:
            board.Remove(t)
        elif n == "GND":
            if t.GetClass() == "PCB_VIA" and any(proche(t.GetPosition(), v) for v in vias_gnd):
                board.Remove(t)
            elif t.GetClass() == "PCB_TRACK" and any(proche(t.GetStart(), v) or proche(t.GetEnd(), v) for v in vias_gnd):
                board.Remove(t)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    for n in sorted(nets_a_retirer):
        print("RETIRE " + n)
    if vias_gnd or pendantes:
        print("RETIRE GND")


if __name__ == "__main__":
    main()
