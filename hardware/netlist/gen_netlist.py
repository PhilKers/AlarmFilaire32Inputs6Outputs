#!/usr/bin/env python3
"""
Générateur de netlist — Centrale d'alarme 32 zones / 6 sorties (STM32H743BIT6).

Le circuit est décrit composant par composant, broche par broche, en
utilisant les NOMS de broches ; ils sont convertis en NUMÉROS grâce à la base
sources/brochages.json (extraite des bibliothèques KiCad officielles et de
bibliothèques publiques recoupées, voir RAPPORT.md).

Contrôles effectués :
  - chaque nom de broche utilisé existe dans le composant ;
  - chaque broche de chaque circuit intégré est soit connectée, soit
    déclarée « non connectée » explicitement (aucun oubli possible) ;
  - aucune broche n'est connectée deux fois ;
  - les équipotentielles à une seule broche sont signalées.

Fichiers produits (dossier sortie/) :
  alarme32_allegro.net        OrCAD PCB Editor / Allegro (format « Telesis »)
  devices/*.txt               fichiers « device » Allegro (un par type)
  alarme32_pcb2.net           format « OrCAD PCB II » (OrCAD Layout, et
                              importable par la plupart des outils de CAO)
  alarme32_connexions.csv     liste complète composant / broche / équipotentielle
  nomenclature.csv            nomenclature regroupée
  empreintes.csv              empreintes utilisées (à associer à votre bibliothèque)
  RAPPORT.md                  statistiques et avertissements

Usage : python3 gen_netlist.py
"""
import csv
import json
import os
import re
import sys
from collections import OrderedDict, defaultdict

ICI = os.path.dirname(os.path.abspath(__file__))
SORTIE = os.path.join(ICI, "sortie")
BASE = json.load(open(os.path.join(ICI, "sources", "brochages.json"), encoding="utf-8"))
BROCHAGES = BASE["brochages"]

NC = None  # marqueur de broche non connectée


def norm(nom):
    """Normalise un nom de broche : retire ~ { } ●, et les suffixes _2 / @3."""
    n = re.sub(r"_\{([^}]*)\}", r"\1", nom)          # indices : V_{DD} -> VDD, IO_{1} -> IO1
    n = re.sub(r"[~{}●]", "", n)
    n = re.sub(r"(_\d+|@\d+)$", "", n)
    return n.upper()


class Composant:
    def __init__(self, ref, device, valeur, empreinte, desc, broches):
        self.ref, self.device, self.valeur, self.empreinte, self.desc = ref, device, valeur, empreinte, desc
        self.broches = broches                  # numéro -> nom
        self.net = OrderedDict((n, "") for n in broches)   # numéro -> net ('' = non affecté)


class Carte:
    def __init__(self):
        self.composants = OrderedDict()
        self.compteurs = defaultdict(int)
        self.erreurs = []

    # -- attribution des repères par feuillet ------------------------------
    def feuillet(self, n):
        for p in list(self.compteurs) + ["R", "C", "D", "L", "Q", "U", "J", "F", "K", "X", "FB", "TP", "SW", "JP", "BT"]:
            self.compteurs[p] = max(self.compteurs[p], n * 100)

    def ref(self, prefixe):
        self.compteurs[prefixe] += 1
        return f"{prefixe}{self.compteurs[prefixe]}"

    # -- ajout d'un composant ----------------------------------------------
    def ajoute(self, prefixe, device, valeur, empreinte, desc, connexions, base=None, nc=(), ref=None):
        """connexions : dict {nom_ou_#numéro: net} (base fournie) ou liste de nets (broches 1..n)."""
        ref = ref or self.ref(prefixe)
        if base:
            broches = BROCHAGES[base]
        else:
            broches = OrderedDict((str(i + 1), str(i + 1)) for i in range(len(connexions)))
            connexions = OrderedDict((f"#{i + 1}", n) for i, n in enumerate(connexions))
        c = Composant(ref, device, valeur, empreinte, desc, broches)
        index = defaultdict(list)
        for num, nom in broches.items():
            index[norm(nom)].append(num)
        for cle, net in list(connexions.items()) + [(n, NC) for n in nc]:
            nums = [cle[1:]] if cle.startswith("#") else index.get(norm(cle), [])
            if not nums or any(n not in broches for n in nums):
                self.erreurs.append(f"{ref} ({device}) : broche inconnue « {cle} »")
                continue
            for n in nums:
                if c.net[n]:
                    self.erreurs.append(f"{ref} : broche {n} ({broches[n]}) connectée deux fois")
                c.net[n] = net if net is not None else "?"
        # Les broches nommées NC sont automatiquement non connectées
        for n, nom in broches.items():
            if not c.net[n] and norm(nom) in ("NC", "DNC"):
                c.net[n] = "?"
        manquantes = [f"{n}:{broches[n]}" for n in broches if not c.net[n]]
        if manquantes:
            self.erreurs.append(f"{ref} ({device}) : broches non affectées {manquantes}")
        if ref in self.composants:
            self.erreurs.append(f"Repère en double : {ref}")
        self.composants[ref] = c
        return ref

    # -- raccourcis pour composants à 2 broches ----------------------------
    def R(self, val, a, b, fp="R0603", desc="Résistance 1 % 0603"):
        return self.ajoute("R", "RES", val, fp, desc, [a, b])

    def C(self, val, a, b, fp="C0603", desc="Condensateur céramique X7R"):
        return self.ajoute("C", "CAP", val, fp, desc, [a, b])

    def CP(self, val, plus, moins, fp, desc):
        return self.ajoute("C", "CAPPOL", val, fp, desc, [plus, moins])

    def L(self, val, a, b, fp, desc):
        return self.ajoute("L", "IND", val, fp, desc, [a, b])

    def FB(self, a, b):
        return self.ajoute("FB", "FERRITE", "BLM18PG221SN1", "R0603", "Ferrite 220 Ω @100 MHz 1,4 A", [a, b])

    def D(self, device, val, k, a, fp, desc):
        """Diode : broche 1 = cathode, 2 = anode (convention KiCad / IPC)."""
        return self.ajoute("D", device, val, fp, desc, [k, a])

    def F(self, val, a, b, fp, desc):
        return self.ajoute("F", "FUSE", val, fp, desc, [a, b])

    def decouple(self, net, n=1, val="100nF", fp="C0603"):
        for _ in range(n):
            self.C(val, net, "GND", fp)

    def connecteur(self, prefixe, nom_fp, desc, nets, valeur=None):
        n = len(nets)
        return self.ajoute(prefixe, f"CONN{n}", valeur or f"{n}P", nom_fp, desc, nets)

    # -- sorties ------------------------------------------------------------
    def equipotentielles(self):
        nets = OrderedDict()
        for c in self.composants.values():
            for num, net in c.net.items():
                if net and net != "?":
                    nets.setdefault(net, []).append((c.ref, num))
        return OrderedDict(sorted(nets.items(), key=lambda kv: (kv[0] != "GND", kv[0])))


def q(s):
    return "'" + s.replace("'", "") + "'"


# ===========================================================================
# DESCRIPTION DU CIRCUIT
# ===========================================================================
B = Carte()

# ---------------------------------------------------------------------------
# Feuillet 1 — Entrée d'alimentation, batterie, rails
# ---------------------------------------------------------------------------
B.feuillet(0)
B.connecteur("J", "TB_2P_5MM08", "Bornier entrée 13,8 V (bloc PSC)", ["VPSC_IN", "GND"], "ENTREE_13V8")
B.F("5A", "VPSC_IN", "VIN_F", "FUSE_LF154", "Fusible 5 A rapide Littelfuse 154 (OMNI-BLOK)")
B.D("TVS", "SMBJ16A", "VIN_F", "GND", "SMB", "TVS 16 V unidirectionnelle")
B.ajoute("U", "LM74700", "LM74700-Q1", "SOT23_6", "Contrôleur diode idéale",
         {"ANODE": "VIN_F", "CATHODE": "VIN", "GATE": "IDEAL_GATE", "VCAP": "IDEAL_VCAP",
          "EN": "VIN_F", "GND": "GND"}, base="LM74700")
B.C("100nF", "IDEAL_VCAP", "VIN_F", desc="Condensateur pompe de charge LM74700 (VCAP-ANODE)")
B.ajoute("Q", "IRF7403", "IRF7403", "SOIC8", "N-MOS 30 V 8,5 A (diode idéale)",
         {"S": "VIN_F", "G": "IDEAL_GATE", "D": "VIN"}, base="IRF7403")
B.CP("470uF/25V", "VIN", "GND", "CPOL_D10_SMD", "Électrolytique 470 µF 25 V faible ESR")
B.C("10uF/25V", "VIN", "GND", "C1210")
B.decouple("VIN")

# Batterie et mesure INA226
B.connecteur("J", "TB_4P_5MM08", "Bornier batterie : 1 BAT+ PSC, 2 BAT- PSC, 3 BAT+ batterie (fusible 7,5 A dans le câble), 4 BAT- batterie",
             ["VBAT_PSC", "GND", "VBAT_P", "GND"], "BATTERIE")
B.R("10mR", "VBAT_PSC", "VBAT_P", fp="R2512", desc="Shunt 10 mΩ 1 % 2 W, connexion Kelvin")
B.ajoute("U", "INA226", "INA226AIDGSR", "VSSOP10", "Mesure tension/courant batterie I2C (0x40)",
         {"VIN+": "VBAT_PSC", "VIN-": "VBAT_P", "VBUS": "VBAT_P", "VS": "P3V3", "GND": "GND",
          "A0": "GND", "A1": "GND", "SDA": "I2C4_SDA", "SCL": "I2C4_SCL"}, base="INA226", nc=["ALERT"])
B.decouple("P3V3")

# Contacts secs du bloc PSC
B.connecteur("J", "TB_3P_5MM08", "Contacts PSC : 1 AC OK, 2 BAT LOW, 3 commun", ["AC_OK_IN", "BAT_LOW_IN", "GND"], "CONTACTS_PSC")
for sig in ("AC_OK", "BAT_LOW"):
    B.R("1k", f"{sig}_IN", sig)
    B.R("10k", sig, "P3V3")
    B.C("100nF", sig, "GND")
    B.D("ESD", "PESD3V3S1UL", f"{sig}_IN", "GND", "SOD323", "Protection ESD 3,3 V")

# +5 V (LMR33630, 400 kHz)
B.ajoute("U", "LMR33630ADDA", "LMR33630ADDA", "HSOIC8_PAD", "Buck 36 V 3 A — rail +5 V",
         {"VIN": "VIN", "EN": "VIN", "GND": "GND", "FB": "FB_5V", "VCC": "VCC_5V", "BOOT": "BOOT_5V", "SW": "SW_5V"},
         base="LMR33630ADDA", nc=["PG"])
B.C("10uF/50V", "VIN", "GND", "C1210"); B.C("100nF", "VIN", "GND")
B.C("1uF", "VCC_5V", "GND"); B.C("100nF", "BOOT_5V", "SW_5V")
B.L("10uH", "SW_5V", "P5V", "L_XAL6060", "Inductance 10 µH 6 A blindée (XAL6060-103)")
B.R("100k", "P5V", "FB_5V"); B.R("24.9k", "FB_5V", "GND")
B.C("22uF/16V", "P5V", "GND", "C1210"); B.C("22uF/16V", "P5V", "GND", "C1210")

# +3,8 V module 4G (commandable)
B.ajoute("U", "LMR33630ADDA", "LMR33630ADDA", "HSOIC8_PAD", "Buck 36 V 3 A — rail +3,8 V module 4G",
         {"VIN": "VIN", "EN": "MODEM_PWR_EN", "GND": "GND", "FB": "FB_3V8", "VCC": "VCC_3V8", "BOOT": "BOOT_3V8", "SW": "SW_3V8"},
         base="LMR33630ADDA", nc=["PG"])
B.R("100k", "MODEM_PWR_EN", "GND", desc="Module 4G éteint par défaut")
B.C("10uF/50V", "VIN", "GND", "C1210"); B.C("100nF", "VIN", "GND")
B.C("1uF", "VCC_3V8", "GND"); B.C("100nF", "BOOT_3V8", "SW_3V8")
B.L("10uH", "SW_3V8", "P3V8_GSM", "L_XAL6060", "Inductance 10 µH 6 A blindée (XAL6060-103)")
B.R("100k", "P3V8_GSM", "FB_3V8"); B.R("35.7k", "FB_3V8", "GND")
B.C("22uF/16V", "P3V8_GSM", "GND", "C1210"); B.C("22uF/16V", "P3V8_GSM", "GND", "C1210")

# +3,3 V (TPS62130 depuis +5 V)
B.ajoute("U", "TPS62130", "TPS62130RGTR", "VQFN16_3X3", "Buck 3 A — rail +3,3 V",
         {"VIN": "P5V", "EN": "P5V", "SW": "SW_3V3", "VOS": "P3V3", "FB": "FB_3V3", "FSW": "GND",
          "DEF": "GND", "SS/TR": "SS_3V3", "GND": "GND"}, base="TPS62130", nc=["PG"])
B.C("10uF/16V", "P5V", "GND", "C0805"); B.C("100nF", "P5V", "GND")
B.C("3.3nF", "SS_3V3", "GND")
B.L("2.2uH", "SW_3V3", "P3V3", "L_XFL4020", "Inductance 2,2 µH 3,5 A (XFL4020-222)")
B.R("316k", "P3V3", "FB_3V3"); B.R("100k", "FB_3V3", "GND")
B.C("22uF/10V", "P3V3", "GND", "C0805"); B.C("22uF/10V", "P3V3", "GND", "C0805")

# LDO VDDA/VREF+ et VZONE
for net in ("P3V3A", "VZONE"):
    B.ajoute("U", "TLV75533PDBV", "TLV75533PDBV", "SOT23_5", f"LDO 3,3 V 500 mA — {net}",
             {"IN": "P5V", "EN": "P5V", "OUT": net, "GND": "GND"}, base="TLV75533PDBV")
    B.C("1uF", "P5V", "GND")
    B.C("10uF/10V" if net == "VZONE" else "1uF", net, "GND", "C0805" if net == "VZONE" else "C0603")
    B.C("100nF", net, "GND")

# +3,3 V dalle (interrupteur de charge)
B.ajoute("U", "TPS22918DBV", "TPS22918DBVR", "SOT23_6", "Interrupteur de charge 3V3_LCD",
         {"VIN": "P3V3", "GND": "GND", "ON": "LCD_PWR_EN", "CT": "CT_LCD", "QOD": "P3V3_LCD", "VOUT": "P3V3_LCD"},
         base="TPS22918DBV")
B.C("1nF", "CT_LCD", "GND"); B.C("1uF", "P3V3", "GND"); B.C("10uF/10V", "P3V3_LCD", "GND", "C0805")
B.R("100k", "LCD_PWR_EN", "GND")

# +12 V rétroéclairage : buck-boost LM5176 (valeurs à confirmer par WEBENCH)
B.ajoute("U", "LM5176PWP", "LM5176PWPR", "HTSSOP28_PAD", "Buck-boost 4 interrupteurs — +12 V rétroéclairage",
         {"EN/UVLO": "BB_EN", "VIN": "VIN", "VISNS": "BB_VISNS", "MODE": "BB_MODE", "DITH": "GND",
          "RT/SYNC": "BB_RT", "SLOPE": "BB_SLOPE", "SS": "BB_SS", "COMP": "BB_COMP", "AGND": "GND",
          "FB": "BB_FB", "VOSNS": "P12V_LCD", "ISNS(-)": "P12V_LCD", "ISNS(+)": "BB_OUT", "CSG": "GND",
          "CS": "BB_CS", "SW1": "BB_SW1", "HDRV1": "BB_HD1", "BOOT1": "BB_BT1", "LDRV1": "BB_LD1",
          "SW2": "BB_SW2", "HDRV2": "BB_HD2", "BOOT2": "BB_BT2", "LDRV2": "BB_LD2", "PGND": "GND",
          "VCC": "BB_VCC", "BIAS": "P12V_LCD", "POWERPAD": "GND"}, base="LM5176PWP", nc=["PGOOD"])
B.R("100k", "VIN", "BB_EN"); B.R("15k", "BB_EN", "GND")               # UVLO ≈ 9,4 V
B.R("10R", "VIN", "BB_VISNS"); B.C("100nF", "BB_VISNS", "GND")
B.R("93.1k", "BB_MODE", "GND", desc="Sélection de mode LM5176 (voir datasheet)")
B.R("20k", "BB_RT", "GND", desc="Fréquence de découpage ~ 400 kHz (à confirmer)")
B.C("100pF", "BB_SLOPE", "GND"); B.C("47nF", "BB_SS", "GND")
B.R("10k", "BB_COMP", "BB_COMP_RC"); B.C("10nF", "BB_COMP_RC", "GND"); B.C("100pF", "BB_COMP", "GND")
B.R("100k", "P12V_LCD", "BB_FB"); B.R("7.15k", "BB_FB", "GND")       # 0,8 V × (1 + 100/7,15) = 11,99 V
B.C("4.7uF", "BB_VCC", "GND")
B.C("100nF", "BB_BT1", "BB_SW1"); B.C("100nF", "BB_BT2", "BB_SW2")
for g, d, s in (("BB_HD1", "VIN", "BB_SW1"), ("BB_LD1", "BB_SW1", "BB_CS"),
                ("BB_LD2", "BB_SW2", "BB_CS"), ("BB_HD2", "BB_OUT", "BB_SW2")):
    B.ajoute("Q", "IRF7403", "IRF7403", "SOIC8", "N-MOS 30 V (étage buck-boost, à optimiser)",
             {"S": s, "G": g, "D": d}, base="IRF7403")
B.L("6.8uH", "BB_SW1", "BB_SW2", "L_XAL6060", "Inductance 6,8 µH 6 A (à confirmer WEBENCH)")
B.R("8mR", "BB_CS", "GND", fp="R1206", desc="Résistance de mesure de courant 8 mΩ 1 W")
B.R("10mR", "BB_OUT", "P12V_LCD", fp="R1206", desc="Mesure courant de sortie 10 mΩ")
B.C("10uF/50V", "VIN", "GND", "C1210")
B.C("22uF/25V", "BB_OUT", "GND", "C1210"); B.C("22uF/25V", "P12V_LCD", "GND", "C1210")
B.CP("100uF/25V", "P12V_LCD", "GND", "CPOL_D8_SMD", "Électrolytique 100 µF 25 V")

# +12 V AUX détecteurs (coupure commandée + PTC)
B.ajoute("Q", "FDS9435A", "FDS9435A", "SOIC8", "P-MOS 30 V coupure AUX 12 V",
         {"S": "VIN", "G": "AUX_GATE", "D": "AUX_SW"}, base="FDS9435A")
B.R("10k", "VIN", "AUX_GATE"); B.R("4.7k", "AUX_GATE", "AUX_DRV")
B.ajoute("Q", "MMBT3904", "MMBT3904", "SOT23", "NPN commande AUX", {"B": "AUX_B", "E": "GND", "C": "AUX_DRV"}, base="MMBT3904")
B.R("10k", "AUX12_EN", "AUX_B"); B.R("100k", "AUX_B", "GND")
B.F("1.1A", "AUX_SW", "P12V_AUX", "PTC_1812", "PTC réarmable 1,1 A (MF-MSMF110)")
B.D("TVS", "SMBJ16A", "P12V_AUX", "GND", "SMB", "TVS 16 V")
B.C("10uF/25V", "P12V_AUX", "GND", "C1210")

# Bus d'alimentation des sorties
B.F("2A", "VIN", "P12V_OUT", "PTC_1812", "PTC réarmable 2 A (MF-MSMF200)")
B.F("1.1A", "P5V", "P5V_OUT", "PTC_1812", "PTC réarmable 1,1 A (MF-MSMF110)")

# Ponts de mesure
for net, adc, rh, rb in (("VIN", "ADC_VIN", "51k", "10k"), ("P5V", "ADC_5V", "10k", "10k"),
                         ("VZONE", "ADC_VZONE", "10k", "10k"), ("P12V_AUX", "ADC_AUX12", "51k", "10k"),
                         ("P12V_OUT", "ADC_OUT12", "51k", "10k")):
    B.R(rh, net, adc); B.R(rb, adc, "GND"); B.C("100nF", adc, "GND")

# ---------------------------------------------------------------------------
# Feuillet 2 — Microcontrôleur
# ---------------------------------------------------------------------------
B.feuillet(1)
mcu = {
    # Alimentations
    "VDD": "P3V3", "VSS": "GND", "VDDA": "P3V3A", "VREF+": "P3V3A", "VSSA": "GND",
    "VBAT": "VBAT_RTC", "VCAP": "VCAP", "VDD33_USB": "P3V3", "PDR_ON": "P3V3",
    "BOOT0": "BOOT0", "NRST": "NRST",
    "PH0": "OSC_IN", "PH1": "OSC_OUT", "PC14": "OSC32_IN", "PC15": "OSC32_OUT",
    # Débogage / USB / console
    "PA13": "SWDIO", "PA14": "SWCLK", "PB3": "SWO", "PA9": "DBG_TX", "PA10": "DBG_RX",
    "PA11": "USB_DM", "PA12": "USB_DP",
    # Tactile (I2C1)
    "PB8": "TP_SCL", "PB9": "TP_SDA", "PB7": "TP_INT", "PB4": "TP_RST",
    # Ethernet RMII
    "PA1": "ETH_REF_CLK", "PA2": "ETH_MDIO", "PC1": "ETH_MDC", "PA7": "ETH_CRS_DV",
    "PC4": "ETH_RXD0", "PC5": "ETH_RXD1", "PG11": "ETH_TX_EN", "PG13": "ETH_TXD0",
    "PG12": "ETH_TXD1", "PG10": "PHY_RST",
    # QSPI / SDMMC / I2C4
    "PB2": "QSPI_CLK", "PB6": "QSPI_NCS", "PF8": "QSPI_IO0", "PF9": "QSPI_IO1",
    "PF7": "QSPI_IO2", "PF6": "QSPI_IO3",
    "PC8": "SD_D0", "PC9": "SD_D1", "PC10": "SD_D2", "PC11": "SD_D3", "PC12": "SD_CK",
    "PD2": "SD_CMD", "PD11": "SD_DETECT", "PD12": "I2C4_SCL", "PD13": "I2C4_SDA",
    # Audio SAI1
    "PE4": "SAI1_FS", "PE5": "SAI1_SCK", "PE6": "SAI1_SD", "PE2": "AMP_SD",
    # Module 4G
    "PB10": "USART3_TX", "PB11": "USART3_RX", "PB13": "USART3_CTS", "PB14": "USART3_RTS",
    "PH6": "MDM_PWRKEY_CTL", "PH7": "MDM_RESET_CTL", "PI8": "MDM_STATUS", "PH4": "MDM_RI",
    "PI11": "MDM_DTR", "PE3": "MODEM_PWR_EN",
    # Zones
    "PA3": "MUXA_OUT", "PB0": "MUXB_OUT", "PB1": "ADC_VZONE",
    "PD4": "MUX_S0", "PD5": "MUX_S1", "PD6": "MUX_S2", "PD7": "MUX_S3",
    # Surveillance
    "PA4": "ADC_VIN", "PA0": "ADC_5V", "PC0": "ADC_AUX12", "PF10": "ADC_OUT12",
    "PA15": "AC_OK", "PB15": "BAT_LOW", "PG9": "BOX_TAMPER", "PG6": "AUX12_EN", "PG7": "WDI",
    # Relais (SPI1)
    "PA5": "RLY_SCK_3V", "PB5": "RLY_MOSI_3V", "PA6": "RLY_MISO", "PG14": "RLY_RCK_3V", "PG3": "RLY_OE_3V",
    # Écran
    "PA8": "BL_PWM", "PC13": "BL_EN", "PC7": "LCD_PWR_EN", "PC6": "LVDS_SHDN",
    "PI12": "LCD_HSYNC", "PI13": "LCD_VSYNC", "PI14": "LCD_CLK", "PK7": "LCD_DE",
    "PI15": "LCD_R0", "PJ0": "LCD_R1", "PJ1": "LCD_R2", "PJ2": "LCD_R3", "PJ3": "LCD_R4",
    "PJ4": "LCD_R5", "PJ5": "LCD_R6", "PJ6": "LCD_R7",
    "PJ7": "LCD_G0", "PJ8": "LCD_G1", "PJ9": "LCD_G2", "PJ10": "LCD_G3", "PJ11": "LCD_G4",
    "PK0": "LCD_G5", "PK1": "LCD_G6", "PK2": "LCD_G7",
    "PJ12": "LCD_B0", "PJ13": "LCD_B1", "PJ14": "LCD_B2", "PJ15": "LCD_B3",
    "PK3": "LCD_B4", "PK4": "LCD_B5", "PK5": "LCD_B6", "PK6": "LCD_B7",
    # SDRAM (FMC)
    "PG8": "SDCLK_MCU", "PH2": "SDR_CKE", "PH3": "SDR_CS", "PF11": "SDR_RAS",
    "PG15": "SDR_CAS", "PH5": "SDR_WE",
    "PE0": "SDR_DQM0", "PE1": "SDR_DQM1", "PI4": "SDR_DQM2", "PI5": "SDR_DQM3",
    "PG4": "SDR_BA0", "PG5": "SDR_BA1",
}
for i, p in enumerate(["PF0", "PF1", "PF2", "PF3", "PF4", "PF5", "PF12", "PF13", "PF14", "PF15", "PG0", "PG1"]):
    mcu[p] = f"SDR_A{i}"
dq = ["PD14", "PD15", "PD0", "PD1", "PE7", "PE8", "PE9", "PE10", "PE11", "PE12", "PE13", "PE14", "PE15",
      "PD8", "PD9", "PD10", "PH8", "PH9", "PH10", "PH11", "PH12", "PH13", "PH14", "PH15",
      "PI0", "PI1", "PI2", "PI3", "PI6", "PI7", "PI9", "PI10"]
for i, p in enumerate(dq):
    mcu[p] = f"SDR_DQ{i}"
U_MCU = B.ajoute("U", "STM32H743BIT6", "STM32H743BIT6", "LQFP208_28X28", "MCU Cortex-M7 480 MHz 2 Mo Flash",
                 mcu, base="STM32H743BITx", nc=["PB12", "PD3", "PG2", "PC2_C", "PC3_C"])
B.decouple("P3V3", 16)                                   # un 100 nF par broche VDD
B.C("4.7uF", "P3V3", "GND", "C0805"); B.C("4.7uF", "P3V3", "GND", "C0805")
B.C("2.2uF", "VCAP", "GND", "C0603"); B.C("2.2uF", "VCAP", "GND", "C0603")   # 2 broches VCAP
B.C("1uF", "P3V3A", "GND"); B.C("100nF", "P3V3A", "GND"); B.C("100nF", "P3V3A", "GND")
B.decouple("VBAT_RTC")
# Quartz
B.ajoute("X", "XTAL4", "25MHz", "XTAL_3225_4P", "Quartz 25 MHz CL 10 pF (ABM8-25.000MHZ-B2)",
         ["OSC_IN", "GND", "OSC_OUT", "GND"])
B.C("12pF", "OSC_IN", "GND"); B.C("12pF", "OSC_OUT", "GND")
B.ajoute("X", "XTAL2", "32.768kHz", "XTAL_ABS07", "Quartz 32,768 kHz CL 4 pF (ABS07)", ["OSC32_IN", "OSC32_OUT"])
B.C("6.8pF", "OSC32_IN", "GND"); B.C("6.8pF", "OSC32_OUT", "GND")
# Sauvegarde RTC
B.ajoute("BT", "BATT_HOLDER", "CR2032", "BAT_CR2032_SMD", "Support pile CR2032 (1 = +, 2 = -)", ["VBAT_CELL", "GND"])
B.ajoute("D", "BAT54C", "BAT54C", "SOT23", "Double Schottky cathode commune (OU VBAT)",
         {"A1": "VBAT_CELL", "A2": "P3V3", "K": "VBAT_RTC"}, base="BAT54C")
# Reset, BOOT0, chien de garde externe
B.C("100nF", "NRST", "GND")
B.ajoute("SW", "SW_PUSH", "RESET", "SW_SMD_6X3_5", "Bouton RESET", ["NRST", "GND"])
B.R("10k", "BOOT0", "GND")
B.ajoute("JP", "JUMPER2", "BOOT0", "PINHEAD_1X2_2MM54", "Cavalier BOOT0 (DFU)", ["BOOT0", "P3V3"])
B.ajoute("U", "TPS3823-33DBV", "TPS3823-33DBVR", "SOT23_5", "Superviseur + chien de garde (1,6 s)",
         {"RESET": "WDG_RST", "GND": "GND", "WDI": "WDI", "VDD": "P3V3"}, base="TPS3823-33DBV", nc=["MR"])
B.decouple("P3V3")
B.D("SCHOTTKY", "BAT54J", "WDG_RST", "NRST", "SOD323", "Schottky : la sortie push-pull du TPS3823 ne peut que tirer NRST à 0")
# Connecteur SWD
B.ajoute("J", "SWD10", "SWD", "HDR_2X5_1MM27", "Connecteur ARM Cortex Debug 10 points",
         {"VTREF": "P3V3", "SWDIO/TMS": "SWDIO", "#3": "GND", "#5": "GND", "SWCLK/TCK": "SWCLK",
          "SWO/TDO": "SWO", "GNDDETECT": "GND", "RESET": "NRST"}, base="SWD_10", nc=["KEY", "NC/TDI"])
# USB-C de maintenance (périphérique, non alimentant)
B.ajoute("J", "USB_C", "USB-C", "USB_C_16P_SMD", "Embase USB-C 2.0 16 broches",
         {"VBUS": "USB_VBUS", "GND": "GND", "CC1": "USB_CC1", "CC2": "USB_CC2", "D+": "USB_DP_C",
          "D-": "USB_DM_C", "SHIELD": "GND"}, base="USB_C_16P", nc=["SBU1", "SBU2"])
B.R("5.1k", "USB_CC1", "GND"); B.R("5.1k", "USB_CC2", "GND")
B.ajoute("U", "USBLC6-2SC6", "USBLC6-2SC6", "SOT23_6", "Protection ESD USB",
         {"#1": "USB_DP_C", "#6": "USB_DP", "#3": "USB_DM_C", "#4": "USB_DM", "GND": "GND", "VBUS": "USB_VBUS"},
         base="USBLC6-2SC6")
# NB : USBLC6 broches 1/6 et 3/4 traversantes : on intercale le composant (C = côté connecteur)
B.C("1uF", "USB_VBUS", "GND")
# Console et extension I2C
B.connecteur("J", "HDR_1X3_2MM54", "Console USART1 : 1 TX, 2 RX, 3 GND", ["DBG_TX", "DBG_RX", "GND"], "CONSOLE")
B.connecteur("J", "HDR_1X4_2MM54", "Extension I2C4 : 1 3V3, 2 SCL, 3 SDA, 4 GND", ["P3V3", "I2C4_SCL", "I2C4_SDA", "GND"], "EXT_I2C")
B.R("2.2k", "I2C4_SCL", "P3V3"); B.R("2.2k", "I2C4_SDA", "P3V3")

# ---------------------------------------------------------------------------
# Feuillet 3 — Mémoires
# ---------------------------------------------------------------------------
B.feuillet(2)
sdr = {"VDD": "P3V3", "VDDQ": "P3V3", "VSS": "GND", "VSSQ": "GND", "CLK": "SDR_CLK", "CKE": "SDR_CKE",
       "CS": "SDR_CS", "RAS": "SDR_RAS", "CAS": "SDR_CAS", "WE": "SDR_WE", "BA0": "SDR_BA0", "BA1": "SDR_BA1"}
sdr.update({f"A{i}": f"SDR_A{i}" for i in range(12)})
sdr.update({f"DQ{i}": f"SDR_DQ{i}" for i in range(32)})
sdr.update({f"DQM{i}": f"SDR_DQM{i}" for i in range(4)})
B.ajoute("U", "IS42S32800J", "IS42S32800J-6TL", "TSOP2_86", "SDRAM 32 Mo 8M×32 166 MHz", sdr, base="IS42S32800J-6TL")
B.R("22R", "SDCLK_MCU", "SDR_CLK", desc="Adaptation série horloge SDRAM (près du MCU)")
B.decouple("P3V3", 8); B.C("10uF/10V", "P3V3", "GND", "C0805"); B.C("10uF/10V", "P3V3", "GND", "C0805")

B.ajoute("U", "W25Q128JVS", "W25Q128JVSIQ", "SOIC8_208MIL", "Flash QSPI 16 Mo",
         {"CS": "QSPI_NCS", "DO/IO1": "QSPI_IO1", "WP/IO2": "QSPI_IO2", "GND": "GND", "DI/IO0": "QSPI_IO0",
          "CLK": "QSPI_CLK", "HOLD/RESET/IO3": "QSPI_IO3", "VCC": "P3V3"}, base="W25Q128JVS")
B.R("10k", "QSPI_NCS", "P3V3"); B.decouple("P3V3")

B.ajoute("J", "MICROSD", "MICROSD", "MICROSD_MOLEX_503182", "Support microSD à poussoir avec détection",
         {"DAT2": "SD_D2", "DAT3/CD": "SD_D3", "CMD": "SD_CMD", "VDD": "P3V3", "CLK": "SD_CK", "VSS": "GND",
          "DAT0": "SD_D0", "DAT1": "SD_D1", "DET": "SD_DETECT", "SHIELD": "GND"}, base="MICROSD_DET")
for s in ("SD_D0", "SD_D1", "SD_D2", "SD_D3", "SD_CMD"):
    B.R("47k", s, "P3V3")
B.R("10k", "SD_DETECT", "P3V3"); B.C("10uF/10V", "P3V3", "GND", "C0805"); B.decouple("P3V3")

B.ajoute("U", "24AA025E48", "24AA025E48T-I/OT", "SOT23_6", "EEPROM I2C 2 kbit + adresse MAC EUI-48 (0x50)",
         {"SCL": "I2C4_SCL", "VSS": "GND", "SDA": "I2C4_SDA", "A1": "GND", "A0": "GND", "VCC": "P3V3"},
         base="24AA025E-OT")
B.decouple("P3V3")

# ---------------------------------------------------------------------------
# Feuillet 4 — Écran LVDS et tactile
# ---------------------------------------------------------------------------
B.feuillet(3)
# Correspondance VESA 24 bits (si la dalle est au format JEIDA : voir RAPPORT.md)
vesa = {0: "LCD_R0", 1: "LCD_R1", 2: "LCD_R2", 3: "LCD_R3", 4: "LCD_R4", 6: "LCD_R5", 7: "LCD_G0",
        8: "LCD_G1", 9: "LCD_G2", 12: "LCD_G3", 13: "LCD_G4", 14: "LCD_G5", 15: "LCD_B0", 18: "LCD_B1",
        19: "LCD_B2", 20: "LCD_B3", 21: "LCD_B4", 22: "LCD_B5", 24: "LCD_HSYNC", 25: "LCD_VSYNC",
        26: "LCD_DE", 27: "LCD_R6", 5: "LCD_R7", 10: "LCD_G6", 11: "LCD_G7", 16: "LCD_B6", 17: "LCD_B7",
        23: "GND"}
lv = {f"D{i}": n for i, n in vesa.items()}
lv.update({"IOVCC": "P3V3", "VCC": "P3V3", "PLLVCC": "P3V3", "LVDSVCC": "P3V3", "GND": "GND",
           "CLKIN": "LCD_CLK", "SHTDN": "LVDS_SHDN", "CLKSEL": "P3V3",
           "Y0P": "LVDS_Y0P", "Y0M": "LVDS_Y0N", "Y1P": "LVDS_Y1P", "Y1M": "LVDS_Y1N",
           "Y2P": "LVDS_Y2P", "Y2M": "LVDS_Y2N", "Y3P": "LVDS_Y3P", "Y3M": "LVDS_Y3N",
           "CLCKOUTP": "LVDS_CLKP", "CLCKOUTM": "LVDS_CLKN"})
B.ajoute("U", "SN75LVDS83B", "SN75LVDS83BDGGR", "TSSOP56", "Émetteur LVDS FlatLink 24 bits", lv, base="SN75LVDS83B")
B.R("10k", "LVDS_SHDN", "GND")
B.decouple("P3V3", 4); B.C("10uF/10V", "P3V3", "GND", "C0805")
B.connecteur("J", "FFC_20P_1MM", "Connecteur LVDS dalle (brochage GÉNÉRIQUE à adapter à la dalle)",
             ["P3V3_LCD", "P3V3_LCD", "GND", "GND", "LVDS_Y0N", "LVDS_Y0P", "GND", "LVDS_Y1N", "LVDS_Y1P", "GND",
              "LVDS_Y2N", "LVDS_Y2P", "GND", "LVDS_CLKN", "LVDS_CLKP", "GND", "LVDS_Y3N", "LVDS_Y3P", "GND",
              "LCD_SEL68"], "DALLE_LVDS")
B.R("10k", "LCD_SEL68", "P3V3_LCD", desc="Sélection 8 bits (selon dalle)")
B.connecteur("J", "HDR_1X6_2MM", "Rétroéclairage : 1-2 +12 V, 3-4 GND, 5 EN, 6 PWM",
             ["P12V_LCD", "P12V_LCD", "GND", "GND", "BL_EN", "BL_PWM"], "RETROECLAIRAGE")
B.R("100k", "BL_EN", "GND"); B.R("100k", "BL_PWM", "GND")
B.connecteur("J", "FPC_6P_0MM5", "Dalle tactile GT911 : 1 VDD, 2 GND, 3 SCL, 4 SDA, 5 INT, 6 RST",
             ["P3V3", "GND", "TP_SCL", "TP_SDA", "TP_INT", "TP_RST"], "TACTILE")
B.R("2.2k", "TP_SCL", "P3V3"); B.R("2.2k", "TP_SDA", "P3V3"); B.R("10k", "TP_INT", "P3V3"); B.R("10k", "TP_RST", "P3V3")

# ---------------------------------------------------------------------------
# Feuillet 5 — 32 entrées de zones
# ---------------------------------------------------------------------------
B.feuillet(4)
for z in range(1, 33):
    zn, zt, za = f"ZN{z}", f"ZONE{z}", f"ZA{z}"
    B.R("4.7k", "VZONE", zn, desc="Tirage de boucle 4,7 kΩ 1 %")
    B.R("470R", zn, zt, fp="R1206", desc="Résistance série 470 Ω 0,5 W anti-surtension")
    B.R("10k", zn, za)
    B.C("100nF", za, "GND")
    B.ajoute("D", "BAV99", "BAV99", "SOT23", "Double diode d'écrêtage", {"A": "GND", "KA": zn, "K": "P3V3"}, base="BAV99")
    B.D("TVS", "SMAJ15A", zt, "GND", "SMA", "TVS 15 V borne de zone")
for m, base_z, sortie in (("A", 1, "MUXA_OUT"), ("B", 17, "MUXB_OUT")):
    cx = {f"I{i}": f"ZA{base_z + i}" for i in range(16)}
    cx.update({"COM": sortie, "S0": "MUX_S0", "S1": "MUX_S1", "S2": "MUX_S2", "S3": "MUX_S3",
               "E": "GND", "VCC": "P3V3", "GND": "GND"})
    B.ajoute("U", "CD74HC4067", "CD74HC4067M96", "SOIC24", f"Multiplexeur analogique 16 voies ({m})", cx, base="CD74HC4067M")
    B.decouple("P3V3"); B.C("1nF", sortie, "GND")
# Borniers : 8 × 6 points (Za, Zb, 0V, Zc, Zd, 0V)
for b in range(8):
    z = 4 * b + 1
    B.connecteur("J", "TB_6P_5MM08", f"Bornier zones {z} à {z + 3}",
                 [f"ZONE{z}", f"ZONE{z + 1}", "GND", f"ZONE{z + 2}", f"ZONE{z + 3}", "GND"], f"ZONES_{z}_{z + 3}")
B.connecteur("J", "TB_4P_5MM08", "Alimentation détecteurs : 1-2 +12 V AUX, 3-4 0 V",
             ["P12V_AUX", "P12V_AUX", "GND", "GND"], "AUX_12V")

# ---------------------------------------------------------------------------
# Feuillet 6 — Sorties relais, LED, autoprotection
# ---------------------------------------------------------------------------
B.feuillet(5)
# Tampon 3,3 V → 5 V (TTL) : le TPIC6B595 exige VIH ≥ 0,85 × VCC
B.ajoute("U", "74AHCT125", "SN74AHCT125PWR", "TSSOP14", "Tampon 3,3 V → 5 V pour la chaîne relais",
         {"1OE": "GND", "1A": "RLY_SCK_3V", "1Y": "RLY_SCK", "2OE": "GND", "2A": "RLY_MOSI_3V", "2Y": "RLY_SER0",
          "3OE": "GND", "3A": "RLY_RCK_3V", "3Y": "RLY_RCK", "4OE": "GND", "4A": "RLY_OE_3V", "4Y": "RLY_OE",
          "GND": "GND", "VCC": "P5V"}, base="74AHCT125")
B.decouple("P5V")
B.R("10k", "RLY_OE_3V", "P3V3", desc="Relais bloqués tant que le MCU ne pilote pas /G")
B.R("10k", "RLY_OE", "P5V", desc="Relais bloqués si le tampon n'est pas alimenté")
B.R("10k", "RLY_RCK_3V", "GND")
# Chaîne de 3 TPIC6B595 ; bit n -> registre n//8, sortie DRAIN(n%8)
bits = {}
for i in range(6):
    bits[i] = f"K_MAIN{i + 1}_DRV"; bits[6 + i] = f"K_SRC{i + 1}_DRV"; bits[12 + i] = f"K_VSEL{i + 1}_DRV"
for i, nom in enumerate(["SECTEUR", "MARCHE", "ALARME", "DEFAUT", "RESEAU", "GSM"]):
    bits[18 + i] = f"LED_{nom}_K"
for r in range(3):
    cx = {"VCC": "P5V", "SERIN": "RLY_SER0" if r == 0 else f"RLY_SER{r}",
          "SEROUT": f"RLY_SER{r + 1}" if r < 2 else "RLY_SEROUT_5V",
          "SRCLR": "P5V", "G": "RLY_OE", "RCK": "RLY_RCK", "SRCK": "RLY_SCK", "GND": "GND"}
    cx.update({f"DRAIN{k}": bits[8 * r + k] for k in range(8)})
    B.ajoute("U", "TPIC6B595", "TPIC6B595DWR", "SOIC20W", f"Registre à décalage de puissance n°{r + 1}", cx, base="TPIC6B595")
    B.decouple("P5V")
B.R("10k", "RLY_SEROUT_5V", "RLY_MISO", desc="Pont 5 V → 3,3 V pour la relecture")
B.R("20k", "RLY_MISO", "GND")
B.C("100uF/16V", "P5V", "GND", "C1210")
# 6 sorties × 3 relais
for n in range(1, 7):
    com, no, nf = f"OUT{n}_COM", f"OUT{n}_NO", f"OUT{n}_NF"
    src, cn, v12, v5 = f"OUT{n}_SRC", f"OUT{n}_C", f"OUT{n}_12V", f"OUT{n}_5V"
    B.F("0.5A", "P12V_OUT", v12, "PTC_1206", "PTC réarmable 0,5 A (MF-MSMF050)")
    B.F("0.5A", "P5V_OUT", v5, "PTC_1206", "PTC réarmable 0,5 A (MF-MSMF050)")
    for nom, dev, cxs in ((f"K_VSEL{n}", "G5V-1", {"COM": src, "NC": v12, "NO": v5}),
                          (f"K_SRC{n}", "G5V-1", {"COM": cn, "NC": com, "NO": src}),
                          (f"K_MAIN{n}", "G5LE-1", {"COM": cn, "NC": nf, "NO": no})):
        cxs.update({"COIL_A": "VIN", "COIL_B": f"{nom}_DRV"})
        fp = "RELAY_G5LE" if dev == "G5LE-1" else "RELAY_G5V1"
        desc = "Relais 12 V inverseur 10 A (puissance)" if dev == "G5LE-1" else "Relais 12 V inverseur 1 A (sélection)"
        B.ajoute("K", dev, f"{dev} DC12", fp, f"{desc} — {nom}", cxs, base=dev)
        B.D("DIODE", "1N4148W", "VIN", f"{nom}_DRV", "SOD123", "Diode de roue libre")
    B.connecteur("J", "TB_4P_5MM08", f"Sortie {n} : 1 COM, 2 NO, 3 NF, 4 0 V", [com, no, nf, "GND"], f"SORTIE_{n}")
# LED de façade
for nom in ["SECTEUR", "MARCHE", "ALARME", "DEFAUT", "RESEAU", "GSM"]:
    B.R("470R", "P3V3", f"LED_{nom}_A")
    B.D("LED", f"LED_{nom}", f"LED_{nom}_K", f"LED_{nom}_A", "LED_3MM_THT", f"LED de façade « {nom.lower()} »")
# Autoprotection du coffret
B.connecteur("J", "HDR_1X2_2MM54", "Autoprotection coffret (contact NF vers 0 V)", ["BOX_TAMPER_IN", "GND"], "AUTOPROTECTION")
B.R("1k", "BOX_TAMPER_IN", "BOX_TAMPER"); B.R("10k", "BOX_TAMPER", "P3V3"); B.C("100nF", "BOX_TAMPER", "GND")
B.D("ESD", "PESD3V3S1UL", "BOX_TAMPER_IN", "GND", "SOD323", "Protection ESD 3,3 V")

# ---------------------------------------------------------------------------
# Feuillet 7 — Audio
# ---------------------------------------------------------------------------
B.feuillet(6)
B.ajoute("U", "MAX98357A", "MAX98357AETE+T", "TQFN16_3X3_PAD", "Amplificateur I2S classe D 3,2 W",
         {"DIN": "SAI1_SD", "GND": "GND", "SD_MODE": "AMP_SD_MODE", "VDD": "P5V", "OUTP": "SPK_P_AMP",
          "OUTN": "SPK_N_AMP", "LRCLK": "SAI1_FS", "BCLK": "SAI1_SCK", "PAD": "GND"},
         base="MAX98357A", nc=["GAIN_SLOT"])
B.R("220k", "AMP_SD", "AMP_SD_MODE", desc="≈ 1 V avec la résistance interne : mode (G+D)/2")
B.C("10uF/10V", "P5V", "GND", "C0805"); B.decouple("P5V"); B.CP("220uF/10V", "P5V", "GND", "CPOL_D6_SMD", "Électrolytique 220 µF 10 V")
B.FB("SPK_P_AMP", "SPK_P"); B.FB("SPK_N_AMP", "SPK_N")
B.C("1nF", "SPK_P", "GND"); B.C("1nF", "SPK_N", "GND")
B.connecteur("J", "TB_2P_3MM81", "Haut-parleur 4-8 Ω 3 W", ["SPK_P", "SPK_N"], "HAUT_PARLEUR")

# ---------------------------------------------------------------------------
# Feuillet 8 — Ethernet
# ---------------------------------------------------------------------------
B.feuillet(7)
B.FB("P3V3", "P3V3_PHY")
B.ajoute("U", "LAN8742A", "LAN8742A-CZ-TR", "QFN24_4X4_PAD", "PHY Ethernet 10/100 RMII",
         {"VDD2A": "P3V3_PHY", "VDD1A": "P3V3_PHY", "VDDIO": "P3V3", "LED2/INTSEL": "PHY_LED2",
          "LED1/REGOFF": "PHY_LED1", "XTAL2": "PHY_XO", "XTAL1/CLKIN": "PHY_XI", "VDDCR": "PHY_VDDCR",
          "RXD1/MODE1": "ETH_RXD1", "RXD0/MODE0": "ETH_RXD0", "RXER/PHYAD0": "PHY_RXER",
          "CRS_DV/MODE2": "ETH_CRS_DV", "MDIO": "ETH_MDIO", "MDC": "ETH_MDC", "INT/REFCLKO": "ETH_REF_CLK",
          "RST": "PHY_RST", "TXEN": "ETH_TX_EN", "TXD0": "ETH_TXD0", "TXD1": "ETH_TXD1",
          "TXN": "ETH_TXN", "TXP": "ETH_TXP", "RXN": "ETH_RXN", "RXP": "ETH_RXP", "RBIAS": "PHY_RBIAS",
          "VSS": "GND"}, base="LAN8742A")
B.decouple("P3V3_PHY", 2); B.C("10uF/10V", "P3V3_PHY", "GND", "C0805"); B.decouple("P3V3")
B.C("1uF", "PHY_VDDCR", "GND"); B.C("470pF", "PHY_VDDCR", "GND")
B.R("12.1k", "PHY_RBIAS", "GND")
B.ajoute("X", "XTAL4", "25MHz", "XTAL_3225_4P", "Quartz 25 MHz CL 10 pF (PHY)", ["PHY_XI", "GND", "PHY_XO", "GND"])
B.C("15pF", "PHY_XI", "GND"); B.C("15pF", "PHY_XO", "GND")
# Straps : MODE[2:0] = 111, PHYAD0 = 0 ; nINTSEL = 0 et REGOFF = 0 via LED + R vers GND
for s in ("ETH_RXD0", "ETH_RXD1", "ETH_CRS_DV"):
    B.R("10k", s, "P3V3")
B.R("10k", "PHY_RXER", "GND")
B.R("1.5k", "ETH_MDIO", "P3V3"); B.R("10k", "PHY_RST", "P3V3")
for s in ("ETH_TXP", "ETH_TXN", "ETH_RXP", "ETH_RXN"):
    B.R("49.9R", s, "P3V3_PHY")
B.ajoute("U", "TPD4E05U06DQA", "TPD4E05U06DQAR", "USON10", "Protection ESD paires Ethernet",
         {"D1+": "ETH_TXP", "D1-": "ETH_TXN", "D2+": "ETH_RXP", "D2-": "ETH_RXN", "GND": "GND"},
         base="TPD4E05U06DQA")
B.R("330R", "PHY_LED1", "ETH_LED_Y_A"); B.R("330R", "PHY_LED2", "ETH_LED_G_A")
B.ajoute("J", "HR911105A", "HR911105A", "RJ45_HR911105A", "RJ45 avec transformateur et LED",
         {"TD+": "ETH_TXP", "TD-": "ETH_TXN", "RD+": "ETH_RXP", "RD-": "ETH_RXN", "TCT": "P3V3_PHY",
          "RCT": "P3V3_PHY", "GND": "GND", "LED_G_A": "ETH_LED_G_A", "LED_G_K": "GND",
          "LED_Y_A": "ETH_LED_Y_A", "LED_Y_K": "GND", "#SH": "GND"}, base="HR911105A")
B.C("100nF", "P3V3_PHY", "GND")

# ---------------------------------------------------------------------------
# Feuillet 9 — Module 4G
# ---------------------------------------------------------------------------
B.feuillet(8)
gsm = {"VBAT": "P3V8_GSM", "GND": "GND", "PWRKEY": "MDM_PWRKEY", "RESET": "MDM_RESET",
       "DTR": "MDM_DTR_18", "RI": "MDM_RI_18", "CTS": "MDM_CTS_18", "RTS": "MDM_RTS_18",
       "TXD": "MDM_TXD_18", "RXD": "MDM_RXD_18", "VDD_1V8": "MDM_1V8", "STATUS": "MDM_STATUS_18",
       "NETLIGHT": "MDM_NETLIGHT", "USIM1_VDD": "SIM_VDD", "USIM1_DATA": "SIM_DATA_M",
       "USIM1_CLK": "SIM_CLK_M", "USIM1_RST": "SIM_RST_M", "RF_ANT": "ANT_MAIN",
       "USB_BOOT": "MDM_USB_BOOT", "VBUS": "MDM_USB_VBUS", "USB_DP": "MDM_USB_DP", "USB_DM": "MDM_USB_DM",
       "UART_LOG_TX": "MDM_LOG_TX", "UART_LOG_RX": "MDM_LOG_RX"}
inutilises = [n for n in sorted(set(norm(v) for v in BROCHAGES["A7670E"].values()))
              if n not in {norm(k) for k in gsm} and n != "NC"]
B.ajoute("U", "A7670E", "A7670E-LASE", "SIMCOM_A7670_LCC_LGA", "Module LTE Cat-1 / GSM SIMCom",
         gsm, base="A7670E", nc=inutilises)
B.CP("470uF/6.3V", "P3V8_GSM", "GND", "CPOL_D8_SMD", "Polymère 470 µF 6,3 V faible ESR (près de VBAT)")
B.C("100uF/6.3V", "P3V8_GSM", "GND", "C1210"); B.C("10uF/10V", "P3V8_GSM", "GND", "C0805")
B.C("100nF", "P3V8_GSM", "GND"); B.C("33pF", "P3V8_GSM", "GND")
B.D("TVS", "SMF5.0A", "P3V8_GSM", "GND", "SOD123F", "TVS 5 V sur VBAT module")
# Traducteur de niveaux 1,8 V (A) ↔ 3,3 V (B)
B.ajoute("U", "TXS0108E", "TXS0108EPWR", "TSSOP20", "Traducteur de niveaux 8 voies 1,8 V ↔ 3,3 V",
         {"VCCA": "MDM_1V8", "VCCB": "P3V3", "OE": "MDM_OE", "GND": "GND",
          "A1": "MDM_RXD_18", "B1": "USART3_TX", "A2": "MDM_TXD_18", "B2": "USART3_RX",
          "A3": "MDM_RTS_18", "B3": "USART3_RTS", "A4": "MDM_CTS_18", "B4": "USART3_CTS",
          "A5": "MDM_DTR_18", "B5": "MDM_DTR", "A6": "MDM_RI_18", "B6": "MDM_RI",
          "A7": "MDM_STATUS_18", "B7": "MDM_STATUS"}, base="TXS0108EPW", nc=["A8", "B8"])
B.R("10k", "MDM_1V8", "MDM_OE", desc="Traducteur actif uniquement module allumé")
B.C("100nF", "MDM_1V8", "GND"); B.C("100nF", "P3V3", "GND")
# PWRKEY et RESET par transistors
for ctl, sig in (("MDM_PWRKEY_CTL", "MDM_PWRKEY"), ("MDM_RESET_CTL", "MDM_RESET")):
    b = f"{sig}_B"
    B.R("4.7k", ctl, b); B.R("47k", b, "GND")
    B.ajoute("Q", "MMBT3904", "MMBT3904", "SOT23", f"NPN commande {sig}", {"B": b, "E": "GND", "C": sig}, base="MMBT3904")
B.R("100k", "MDM_PWRKEY_CTL", "GND"); B.R("100k", "MDM_RESET_CTL", "GND")
# Carte SIM
B.ajoute("J", "SIM", "NANO_SIM", "SIM_NANO_HINGED", "Support nano-SIM à charnière",
         {"VCC": "SIM_VDD", "RST": "SIM_RST", "CLK": "SIM_CLK", "GND": "GND", "I/O": "SIM_DATA"},
         base="SIM_CARD", nc=["VPP"])
for s in ("SIM_RST", "SIM_CLK", "SIM_DATA"):
    B.R("22R", f"{s}_M", s); B.C("33pF", s, "GND")
B.C("100nF", "SIM_VDD", "GND")
B.ajoute("U", "ESDA6V1-5SC6", "ESDA6V1-5SC6", "SOT23_6", "Protection ESD carte SIM",
         {"IO1": "SIM_VDD", "IO2": "SIM_RST", "IO3": "SIM_CLK", "IO4": "SIM_DATA", "GND": "GND"},
         base="ESDA6V1-5SC6", nc=["IO5"])
# Antenne principale (réseau d'adaptation en π, condensateurs non montés par défaut)
B.R("0R", "ANT_MAIN", "ANT_RF", desc="Réseau en π : élément série")
B.C("NC", "ANT_MAIN", "GND", desc="Réseau en π : non monté (DNP)")
B.C("NC", "ANT_RF", "GND", desc="Réseau en π : non monté (DNP)")
B.connecteur("J", "UFL", "Connecteur U.FL antenne LTE (1 signal, 2 masse)", ["ANT_RF", "GND"], "ANTENNE_LTE")
# Points de test (mise à jour firmware du module, journal)
for s in ("MDM_USB_VBUS", "MDM_USB_DP", "MDM_USB_DM", "MDM_USB_BOOT", "MDM_LOG_TX", "MDM_LOG_RX", "MDM_NETLIGHT"):
    B.ajoute("TP", "TESTPOINT", s, "TP_1MM", f"Point de test {s}", [s])
for s in ("P3V3", "P5V", "VIN", "P3V8_GSM", "VZONE", "P12V_LCD", "GND"):
    B.ajoute("TP", "TESTPOINT", s, "TP_1MM", f"Point de test {s}", [s])


# ===========================================================================
# GÉNÉRATION DES FICHIERS
# ===========================================================================
def ecrire_tout():
    if B.erreurs:
        print("ERREURS :")
        for e in B.erreurs:
            print("  -", e)
        sys.exit(1)
    os.makedirs(os.path.join(SORTIE, "devices"), exist_ok=True)
    nets = B.equipotentielles()
    comps = list(B.composants.values())
    # Allegro : un fichier « device » = une empreinte. Si un type de composant
    # existe sur plusieurs empreintes, son nom de device est suffixé.
    fps = defaultdict(set)
    for c in comps:
        fps[c.device].add(c.empreinte)
    for c in comps:
        c.dev_allegro = c.device if len(fps[c.device]) == 1 else f"{c.device}_{c.empreinte}"

    # 1. Allegro / OrCAD PCB Editor — format tiers « Telesis »
    with open(os.path.join(SORTIE, "alarme32_allegro.net"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("$PACKAGES\n")
        groupes = OrderedDict()
        for c in comps:
            groupes.setdefault((c.empreinte, c.dev_allegro, c.valeur), []).append(c.ref)
        for (fp, dev, val), refs in groupes.items():
            lignes = [refs[i:i + 10] for i in range(0, len(refs), 10)]
            f.write(f"{q(fp)} ! {q(dev)} ! {q(ascii_(val))} ; " + " ".join(lignes[0]))
            for l in lignes[1:]:
                f.write(" ,\n    " + " ".join(l))
            f.write("\n")
        f.write("$NETS\n")
        for net, noeuds in nets.items():
            items = [f"{r}.{p}" for r, p in noeuds]
            lignes = [items[i:i + 8] for i in range(0, len(items), 8)]
            f.write(f"{q(net)} ; " + " ".join(lignes[0]))
            for l in lignes[1:]:
                f.write(" ,\n    " + " ".join(l))
            f.write("\n")
        f.write("$END\n")

    # 2. Fichiers « device » Allegro
    classes = {"U": "IC", "Q": "DISCRETE", "D": "DISCRETE", "K": "IO", "J": "IO", "TP": "IO", "SW": "IO",
               "JP": "IO", "BT": "IO"}
    vus = {}
    for c in comps:
        if c.dev_allegro in vus:
            continue
        vus[c.dev_allegro] = c
        prefixe = re.match(r"[A-Z]+", c.ref).group(0)
        nom = re.sub(r"[^a-z0-9_]", "_", c.dev_allegro.lower())
        with open(os.path.join(SORTIE, "devices", nom + ".txt"), "w", encoding="ascii", newline="\r\n") as f:
            f.write(f"(Device file for {c.dev_allegro} - genere par gen_netlist.py)\n")
            f.write(f"PACKAGE {c.empreinte}\n")
            f.write(f"CLASS {classes.get(prefixe, 'DISCRETE')}\n")
            f.write(f"PINCOUNT {len(c.broches)}\n")
            f.write("END\n")

    # 3. OrCAD PCB II
    with open(os.path.join(SORTIE, "alarme32_pcb2.net"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("( { OrCAD PCB II Netlist Format - Centrale alarme 32 zones 6 sorties }\n")
        for i, c in enumerate(comps, 1):
            f.write(f" ( {i:08X} {c.empreinte} {c.ref} {ascii_(c.valeur).replace(' ', '_')}\n")
            for num, net in c.net.items():
                f.write(f"  ( {num} {net if net else '?'} )\n")
            f.write(" )\n")
        f.write(")\n")

    # 4. CSV des connexions
    with open(os.path.join(SORTIE, "alarme32_connexions.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["Repere", "Valeur", "Device", "Empreinte", "Broche", "Nom broche", "Equipotentielle"])
        for c in comps:
            for num, net in c.net.items():
                w.writerow([c.ref, c.valeur, c.device, c.empreinte, num, c.broches[num],
                            "(non connectee)" if net == "?" else net])

    # 5. Nomenclature regroupée
    with open(os.path.join(SORTIE, "nomenclature.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["Quantite", "Valeur / reference", "Empreinte", "Description", "Reperes"])
        grp = OrderedDict()
        for c in comps:
            grp.setdefault((c.valeur, c.empreinte, c.desc), []).append(c.ref)
        for (val, fp, desc), refs in sorted(grp.items(), key=lambda kv: (kv[0][2], kv[0][0])):
            w.writerow([len(refs), val, fp, desc, " ".join(refs)])

    # 6. Empreintes
    with open(os.path.join(SORTIE, "empreintes.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["Empreinte", "Nombre de pastilles", "Numeros de pastilles", "Exemple de composant", "Quantite"])
        emp = OrderedDict()
        for c in comps:
            e = emp.setdefault(c.empreinte, [c, 0])
            e[1] += 1
        for fp, (c, n) in sorted(emp.items()):
            nums = list(c.broches)
            w.writerow([fp, len(nums), " ".join(nums) if len(nums) <= 24 else f"{nums[0]} ... {nums[-1]}",
                        f"{c.ref} {c.valeur}", n])

    # 7. Rapport
    simples = [(n, nd) for n, nd in nets.items() if len(nd) == 1]
    nb_broches = sum(len(c.broches) for c in comps)
    nb_nc = sum(1 for c in comps for n in c.net.values() if n == "?")
    par_prefixe = defaultdict(int)
    for c in comps:
        par_prefixe[re.match(r"[A-Z]+", c.ref).group(0)] += 1
    with open(os.path.join(SORTIE, "RAPPORT.md"), "w", encoding="utf-8") as f:
        f.write("# Rapport de génération de la netlist\n\n")
        f.write(f"- Composants : **{len(comps)}** ({', '.join(f'{k} {v}' for k, v in sorted(par_prefixe.items()))})\n")
        f.write(f"- Broches : **{nb_broches}** dont {nb_broches - nb_nc} connectées et {nb_nc} déclarées non connectées\n")
        f.write(f"- Équipotentielles : **{len(nets)}**\n")
        f.write("- Contrôles : noms de broches vérifiés, aucune broche oubliée, aucune double affectation\n\n")
        f.write("## Équipotentielles à une seule broche\n\n")
        f.write("Normal pour les points de test ; à examiner sinon.\n\n")
        for n, nd in simples:
            f.write(f"- `{n}` : {nd[0][0]}.{nd[0][1]}\n")
        f.write("\n## Sources des brochages\n\n")
        for k, s in sorted(BASE["sources"].items()):
            f.write(f"- **{k}** : {s}\n")
    print(f"{len(comps)} composants, {len(nets)} équipotentielles, {nb_broches} broches ({nb_nc} NC) ; "
          f"{len(simples)} équipotentielles à 1 broche. Fichiers dans {SORTIE}")


def ascii_(s):
    tr = str.maketrans({"µ": "u", "Ω": "R", "é": "e", "è": "e", "à": "a", "ç": "c"})
    return s.translate(tr)


if __name__ == "__main__":
    ecrire_tout()
