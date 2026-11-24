# 4. Nomenclature (BOM)

> 📌 La nomenclature **complète et à jour** (587 composants, repères définitifs) est générée
> avec la netlist : `hardware/netlist/sortie/nomenclature.csv`. Ce document en donne une
> vue d'ensemble commentée.

Références indicatives, disponibles chez les distributeurs usuels (Mouser, Farnell, DigiKey, LCSC).
Les composants passifs génériques (résistances 0603 1 %, condensateurs X7R/X5R) ne sont listés que par quantités globales.

## Semi-conducteurs principaux

| Repère | Référence | Description | Qté |
|---|---|---|---|
| U1 + Q1 | LM74700-Q1 + IRF7403 (N-MOS 30 V, SO-8) | Diode idéale / protection inversion | 1 |
| U2 | INA226AIDGSR | Mesure tension/courant batterie, I2C | 1 |
| U3, U4 | LMR33630ADDA | Buck 36 V / 3 A (5 V et 3,8 V) | 2 |
| U5 | TPS62130RGTR | Buck 3 A (3,3 V) | 1 |
| U6, U7 | TLV75533PDBVR | LDO 3,3 V 500 mA (VDDA, VZONE) | 2 |
| U8 | LM5176PWPR + 4 × IRF7403 | Buck-boost 12 V rétroéclairage (valeurs à valider par WEBENCH) | 1 |
| U9 | TPS22918DBVR | Interrupteur de charge 3V3_LCD | 1 |
| MCU | **STM32H743BIT6** | Cortex-M7 480 MHz, LQFP208 | 1 |
| U10 | TPS3823-33DBVR + BAT54J | Superviseur + chien de garde (Schottky vers NRST) | 1 |
| U11 | USBLC6-2SC6 | ESD USB | 1 |
| U12 | IS42S32800J-6TLI | SDRAM 32 Mo, 32 bits | 1 |
| U13 | W25Q128JVSIQ | Flash QSPI 16 Mo | 1 |
| U14 | 24AA025E48T-I/OT | EEPROM + adresse MAC EUI-48 | 1 |
| U15 | SN75LVDS83BDGGR | Émetteur LVDS FlatLink | 1 |
| U16 | SN74AHCT125PWR | Tampon 3,3 V → 5 V vers la chaîne TPIC6B595 | 1 |
| U17, U18 | CD74HC4067M96 | Multiplexeur analogique 16 voies | 2 |
| U19…U21 | TPIC6B595DWR | Registre à décalage, sorties puissance | 3 |
| U22 | MAX98357AETE+T | Ampli I2S classe D 3,2 W | 1 |
| U23 | LAN8742A-CZ-TR | PHY Ethernet RMII | 1 |
| U24 | TPD4E05U06DQAR | ESD Ethernet | 1 |
| U25 | **SIMCom A7670E-LASE** | Module LTE Cat-1 / GSM (Europe) | 1 |
| U26 | TXS0108EPWR | Traducteur de niveaux 8 voies | 1 |
| U27 | ESDA6V1W5 | ESD carte SIM | 1 |
| Q2 | FDS9435A | P-MOS 30 V coupure AUX 12 V | 1 |
| Q3…Q5 | MMBT3904 | NPN commande | 3 |
| D1, D2 | SMBJ16A | TVS entrée et AUX | 2 |
| D3 | BAT54C | OU de diodes VBAT | 1 |
| D4 | SMF5.0A | TVS VBAT module 4G | 1 |
| D_Z1…32 | SMAJ15A | TVS bornes de zones | 32 |
| D_CL1…32 | BAV99 | Écrêtage nœuds de zones | 32 |
| D_K1…18 | 1N4148W | Roue libre bobines relais | 18 |
| — | PESD3V3S1UL | ESD entrées contacts | 4 |

## Électromécanique

| Repère | Référence | Description | Qté |
|---|---|---|---|
| K_MAIN1…6 | Omron G5LE-1 DC12 | Relais inverseur 10 A | 6 |
| K_SRC1…6, K_VSEL1…6 | Omron G5V-1 DC12 | Relais signal inverseur 1 A (150 mW) | 12 |
| F1 | Littelfuse 0154005.DR | Fusible 5 A rapide + support | 1 |
| F2 | Fusible auto 7,5 A + porte-fusible de câble | Batterie (dans le câble, près de la batterie) | 1 |
| F3, F5 | Bourns MF-MSMF110-2 | PTC 1,1 A | 2 |
| F4 | Bourns MF-MSMF200-2 | PTC 2 A | 1 |
| F_O1…12 | Bourns MF-MSMF050-2 | PTC 0,5 A par sortie et par source | 12 |
| X1 | ABM8-25.000MHZ-B2-T | Quartz 25 MHz (MCU) | 1 |
| X2 | ABS07-32.768KHZ-T | Quartz 32,768 kHz (RTC) | 1 |
| X3 | ABM8-25.000MHZ-B2-T | Quartz 25 MHz (PHY) | 1 |
| BT1 | Support CR2032 + pile | Sauvegarde RTC | 1 |
| J10 | HanRun HR911105A | RJ45 avec transformateur et LED | 1 |
| J6 | Molex 503182-1852 | Support microSD | 1 |
| J11 | Support nano-SIM à charnière (ex. Amphenol C707 10M006 512 2) | | 1 |
| J12 | U.FL (Hirose U.FL-R-SMT-1) + câble vers SMA | Antenne LTE | 1 |
| J5 | USB-C 16 broches | Maintenance | 1 |
| J4 | Embase 2 × 5 1,27 mm | SWD | 1 |
| J14…J28 | Borniers débrochables pas 5 mm (Phoenix MSTB / équivalent) | Zones, sorties, alim | ~ 110 points |
| SW1 | Bouton tactile | Reset | 1 |

## Sous-ensembles externes

| Élément | Référence conseillée | Remarque |
|---|---|---|
| Bloc alimentation secourue | **Mean Well PSC-100A-C** (13,8 V / 7 A, chargeur, contacts AC OK & BAT LOW) | Raccordement 230 V par électricien, coffret avec terre |
| Batterie | Plomb AGM 12 V 7 Ah à 18 Ah | Selon autonomie, voir 01-architecture.md |
| Dalle LCD | 10,4" XGA 1024×768 LVDS (ex. AUO G104XVN01.x) | Vérifier VLED, brochage LVDS |
| Dalle tactile | PCAP 10,4" avec contrôleur GT911/GT928, sortie I2C FPC 6 broches | |
| Haut-parleur | 4 Ω ou 8 Ω, 3 W, Ø 50 à 66 mm | |
| Antenne | LTE 698-2700 MHz, câble SMA, passe-cloison | Hors du coffret métallique |
| Coffret | Métallique avec autoprotection (microrupteur à l'ouverture et à l'arrachement) | |
| Carte microSD | 4 à 32 Go, classe industrielle | Fichiers vocaux + journal |
| Carte SIM | Forfait avec SMS + voix, **code PIN désactivé** ou renseigné dans la config | |

## Passifs (ordre de grandeur)

| Type | Qté |
|---|---|
| Résistances 0603 1 % (dont 32 × 4,7 kΩ, 32 × 10 kΩ) | ~ 160 |
| Résistances 1206 0,5 W 470 Ω (série zones) | 32 |
| Résistances de fin de ligne 4,7 kΩ 1 % traversantes (fournies pour les détecteurs) | 64 |
| Condensateurs 0603 100 nF | ~ 110 |
| Condensateurs 0805/1206 1 à 22 µF | ~ 30 |
| Électrolytiques / polymères (470 µF 25 V, 470 µF 6,3 V polymère, 220 µF, 100 µF) | 5 |
| Inductances de puissance (10 µH ×2, 2,2 µH, buck-boost) | 4 |
| Ferrites 0603 (BLM18PG221SN1) | 8 |
