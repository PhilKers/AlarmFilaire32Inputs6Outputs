#!/usr/bin/env python3
"""
Extrait la géométrie des symboles utilisés (bibliothèques KiCad 6 à 10) et la
normalise au format KiCad 7 dans sources/symboles.json. À relancer seulement
si l'on change de composant.

Usage : extraire_symboles.py <clone kicad-symbols> <dossier symboles externes>
"""
import json, os, sys
from sexpr import parse, dump, find, findall, Q

KS, EXT = sys.argv[1], sys.argv[2]
SOURCES = {   # nom dans le schéma : (fichier, symbole)
    "STM32H743BITx": ("MCU_ST_STM32H7/STM32H743BITx", None),
    "LAN8742A": ("Interface_Ethernet/LAN8742A", None),
    "LMR33630ADDA": ("Regulator_Switching/LMR33640ADDA", None),
    "TPS62130": ("Regulator_Switching/TPS62130", None),
    "LM5176PWP": ("Regulator_Switching/LM5175PWP", None),
    "TLV75533PDBV": ("Regulator_Linear/TLV70012_SOT23-5", None),
    "W25Q128JVS": ("Memory_Flash/W25Q32JVSS", None),
    "24AA025E-OT": ("Memory_EEPROM/24AA025E-OT", None),
    "CD74HC4067M": ("74xx/CD74HC4067M", None),
    "TXS0108EPW": ("Logic_LevelTranslator/TXS0108EPW", None),
    "INA226": ("Sensor_Energy/INA226", None),
    "MAX98357A": ("Audio/MAX98357A", None),
    "TPS3823-33DBV": ("Power_Supervisor/TPS3823-xxDBV", None),
    "USBLC6-2SC6": ("Power_Protection/USBLC6-2P6", None),
    "TPD4E05U06DQA": ("Power_Protection/TPD4EUSB30", None),
    "ESDA6V1-5SC6": ("Power_Protection/ESDA6V1-5SC6", None),
    "LM74700": ("Power_Management/LM74700", None),
    "FDS9435A": ("Transistor_FET/IRF7404", None),
    "IRF7403": ("Transistor_FET/IRF8721PBF-1", None),
    "MMBT3904": ("Transistor_BJT/Q_NPN_BEC", None),
    "74AHCT125": ("74xx/74LS125", None),
    "USB_C_16P": ("Connector/USB_C_Receptacle_USB2.0_16P", None),
    "HR911105A": ("Connector/RJ45_Hanrun_HR911105A_Horizontal", None),
    "MICROSD_DET": ("Connector/Micro_SD_Card_Det1", None),
    "SIM_CARD": ("Connector/SIM_Card", None),
    "SWD_10": ("Connector/Conn_ARM_JTAG_SWD_10", None),
    "G5LE-1": ("Relay/G5LE-1", None),
    "G5V-1": ("Relay/G5V-1", None),
    "BAV99": ("Diode/BAV99", None),
    "BAT54C": ("Diode/BAT54C", None),
    "R": ("Device/R", None), "C": ("Device/C", None), "C_Polarized": ("Device/C_Polarized", None),
    "L": ("Device/L", None), "FerriteBead": ("Device/FerriteBead", None), "Fuse": ("Device/Fuse", None),
    "Polyfuse": ("Device/Polyfuse", None), "D": ("Device/D", None), "D_Schottky": ("Device/D_Schottky", None),
    "D_Zener": ("Device/D_Zener", None), "LED": ("Device/LED", None), "Crystal": ("Device/Crystal", None),
    "Crystal_GND24": ("Device/Crystal_GND24", None), "Battery_Cell": ("Device/Battery_Cell", None),
    "SW_Push": ("Switch/SW_Push", None), "TestPoint": ("Connector/TestPoint", None),
    "Conn_01x02": ("Connector_Generic/Conn_01x02", None), "Conn_01x03": ("Connector_Generic/Conn_01x03", None),
    "Conn_01x04": ("Connector_Generic/Conn_01x04", None), "Conn_01x06": ("Connector_Generic/Conn_01x06", None),
    "Conn_01x20": ("Connector_Generic/Conn_01x20", None),
    # Bibliothèques externes (GitHub, voir hardware/netlist/sortie/RAPPORT.md)
    "SN75LVDS83B": ("@LVDS83B_a", "SN75LVDS83B"),
    "IS42S32800J-6TL": ("@IS42_a", None),
    "TPS22918DBV": ("@TPS22918_b", None),
    "A7670E": ("@A7670E_a", "A7670E"),
}

def charger(chemin, nom):
    if chemin.startswith("@"):
        f = os.path.join(EXT, chemin[1:] + ".kicad_sym")
    else:
        d, n = chemin.split("/")
        f = os.path.join(KS, d + ".kicad_symdir", n + ".kicad_sym")
    lib = parse(open(f, encoding="utf-8").read())[0]
    syms = findall(lib, "symbol")
    if nom:
        syms = [s for s in syms if s[1] == nom]
    return syms[0]

def num(x): return float(x)

def stroke_fill(e):
    st = find(e, "stroke"); fi = find(e, "fill")
    w = num(find(st, "width")[1]) if st and find(st, "width") else 0
    t = find(fi, "type")[1] if fi and find(fi, "type") else "none"
    return f"(stroke (width {w:g}) (type default)) (fill (type {t}))"

def xy(p): return f"{num(p[1]):g} {num(p[2]):g}"

def graphique(g):
    k = g[0]
    if k == "rectangle":
        return f"(rectangle (start {xy(find(g,'start'))}) (end {xy(find(g,'end'))}) {stroke_fill(g)})"
    if k == "polyline":
        pts = " ".join(f"(xy {xy(p)})" for p in findall(find(g, "pts"), "xy"))
        return f"(polyline (pts {pts}) {stroke_fill(g)})"
    if k == "circle":
        return f"(circle (center {xy(find(g,'center'))}) (radius {num(find(g,'radius')[1]):g}) {stroke_fill(g)})"
    if k == "arc":
        if find(g, "mid") is None:
            return None
        return f"(arc (start {xy(find(g,'start'))}) (mid {xy(find(g,'mid'))}) (end {xy(find(g,'end'))}) {stroke_fill(g)})"
    if k == "text":
        at = find(g, "at"); a = num(at[3]) if len(at) > 3 else 0
        return dump(["text", Q(g[1])]) [:-1] + f" (at {xy(at)} {a:g}) (effects (font (size 1.27 1.27))))"
    return None

def cache(e):
    """Vrai si l'élément porte « hide » (ancien ou nouveau format)."""
    for x in e[1:]:
        if x == "hide": return True
        if isinstance(x, list) and x and x[0] == "hide" and (len(x) == 1 or x[1] == "yes"): return True
    return False

def extraire(sym):
    out = {"pin_names_offset": 0.508, "pin_names_hide": False, "pin_numbers_hide": False, "units": {}}
    pn = find(sym, "pin_names")
    if pn:
        o = find(pn, "offset"); out["pin_names_offset"] = num(o[1]) if o else 0.508
        out["pin_names_hide"] = cache(pn)
    pnum = find(sym, "pin_numbers")
    if pnum: out["pin_numbers_hide"] = cache(pnum)
    ref = [p for p in findall(sym, "property") if p[1] == "Reference"]
    out["ref"] = ref[0][2] if ref else "U"
    for u in findall(sym, "symbol"):
        unite, style = u[1].rsplit("_", 2)[-2:]
        if style not in ("0", "1"):
            continue
        d = out["units"].setdefault(unite, {"graphics": [], "pins": []})
        for g in u[2:]:
            if not isinstance(g, list): continue
            if g[0] == "pin":
                at = find(g, "at")
                d["pins"].append({"type": g[1], "style": g[2], "x": num(at[1]), "y": num(at[2]),
                                  "angle": int(num(at[3])) if len(at) > 3 else 0,
                                  "length": num(find(g, "length")[1]), "hide": cache(g),
                                  "name": str(find(g, "name")[1]), "number": str(find(g, "number")[1])})
            else:
                s = graphique(g)
                if s: d["graphics"].append(s)
    return out

res = {n: extraire(charger(c, s)) for n, (c, s) in SOURCES.items()}
json.dump(res, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "sources", "symboles.json"), "w"),
          ensure_ascii=False, indent=0)
for n, s in res.items():
    print(f"{n:16s} unités {sorted(s['units'])} broches {sum(len(u['pins']) for u in s['units'].values())}")
