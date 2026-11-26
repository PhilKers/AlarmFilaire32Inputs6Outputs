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

    board = pcbnew.LoadBoard(PCB)
    for t in list(board.GetTracks()):
        board.Remove(t)
    if not pcbnew.ImportSpecctraSES(board, ses):
        sys.exit("Import de la session refusé")
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    n_pistes = sum(1 for t in board.GetTracks() if t.GetClass() == "PCB_TRACK")
    n_vias = sum(1 for t in board.GetTracks() if t.GetClass() == "PCB_VIA")
    print(f"Carte enregistrée : {n_pistes} segments de piste, {n_vias} vias")


if __name__ == "__main__":
    main()
