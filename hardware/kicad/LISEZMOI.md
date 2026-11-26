# Schéma KiCad de la centrale d'alarme

## Ouvrir le schéma

1. Installez **KiCad** (gratuit, Windows / macOS / Linux) : https://www.kicad.org — version 7, 8, 9 ou 10.
2. Ouvrez `alarme32/alarme32.kicad_pro` (ou directement `alarme32/alarme32.kicad_sch`).
   Avec KiCad 8 ou plus, un message propose de convertir le fichier au nouveau format : acceptez.
3. La feuille racine affiche 9 feuillets : **double-cliquez** sur un feuillet pour l'ouvrir.

Version imprimable : `alarme32_schema.pdf` (10 pages, une par feuillet).

## Organisation

| Feuillet | Contenu | Symboles |
|---|---|---|
| 1 - Alimentation | Entrée 13,8 V, diode idéale, INA226, régulateurs, buck-boost 12 V, AUX 12 V | 119 |
| 2 - MCU | STM32H743BIT6, quartz, RTC, reset/chien de garde, SWD, USB-C | 50 |
| 3 - Mémoires | SDRAM 32 Mo, Flash QSPI, microSD, EEPROM MAC | 26 |
| 4 - Écran | SN75LVDS83B, connecteurs dalle, rétroéclairage, tactile | 17 |
| 5 - Zones | 32 entrées (6 composants chacune), 2 multiplexeurs, borniers | 207 |
| 6 - Sorties | 74AHCT125, 3 × TPIC6B595, 18 relais, LED, autoprotection | 89 |
| 7 - Audio | MAX98357A, haut-parleur | 10 |
| 8 - Ethernet | LAN8742A, RJ45 | 27 |
| 9 - 4G | A7670E, traducteur de niveaux, carte SIM, antenne | 46 |

**Style « étiquettes »** : au lieu de fils dessinés, chaque broche porte une **étiquette globale** au
nom de son équipotentielle (`P3V3`, `USART3_TX`, `ZONE12`…). Toutes les étiquettes de même nom
sont reliées, sur tous les feuillets. Une croix bleue marque une broche volontairement libre.
Dans KiCad, cliquez sur une étiquette pour surligner toute l'équipotentielle.

## Fiabilité

- Le schéma est généré par `gen_schema.py` à partir de la **même description** que la netlist OrCAD
  (`../netlist/gen_netlist.py`).
- `verif_schema.py` fait relire le schéma par **KiCad lui-même** (`kicad-cli`), exporte sa netlist et
  la compare broche par broche à la référence : **587 composants et 466 équipotentielles identiques**.
- Les symboles viennent des bibliothèques KiCad officielles (ou recoupées, voir
  `../netlist/LISEZMOI.md`) ; le TPIC6B595, absent de KiCad, est dessiné en rectangle.
- Les points à vérifier avant fabrication sont les mêmes que pour la netlist
  (SN75LVDS83B, relais, connecteur de dalle, valeurs du LM5176).

## Modifier

Deux façons de travailler :
- **Dans KiCad** directement : le schéma devient alors votre référence (pensez à ne plus relancer
  `gen_schema.py`, qui écraserait vos modifications).
- **Dans la description** `../netlist/gen_netlist.py`, puis :
  ```bash
  python3 ../netlist/gen_netlist.py   # netlist OrCAD
  python3 gen_schema.py               # schéma KiCad
  python3 verif_schema.py             # contrôle par KiCad (kicad-cli requis)
  ```

`extraire_symboles.py` ne sert qu'à mettre à jour `sources/symboles.json` depuis les bibliothèques KiCad.
