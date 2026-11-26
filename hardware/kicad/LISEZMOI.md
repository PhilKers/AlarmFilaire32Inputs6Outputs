# Projet KiCad de la centrale d'alarme

Le dossier `alarme32/` est un **projet KiCad complet** : schéma + circuit imprimé + bibliothèque
d'empreintes du projet.

| Fichier | Contenu |
|---|---|
| `alarme32/alarme32.kicad_pro` | Projet (à ouvrir dans KiCad), classes d'équipotentielles et règles de conception |
| `alarme32/alarme32.kicad_sch` + 9 feuillets | Schéma hiérarchique |
| `alarme32/alarme32.kicad_pcb` | Circuit imprimé 4 couches 300 × 210 mm, composants placés, **routage partiel** (voir ci-dessous) |
| `alarme32/alarme32.pretty/` + `fp-lib-table` | Les 64 empreintes utilisées (bibliothèque du projet, autonome) |
| `alarme32/alarme32.dsn` | Export Specctra de la carte (pour Freerouting) |
| `alarme32_pcb_routage.pdf` | Vue des pistes (F.Cu, In2.Cu, B.Cu) |
| `alarme32_schema.pdf` | Schéma imprimable (10 pages) |
| `alarme32_pcb_placement.pdf` | Plan de placement imprimable (face composants) |

## Ouvrir

1. Installez **KiCad** (gratuit) : https://www.kicad.org — version 7, 8, 9 ou 10.
2. Ouvrez `alarme32/alarme32.kicad_pro`. Avec KiCad 8 ou plus, acceptez la conversion au nouveau format.
3. Depuis la fenêtre projet : **Éditeur de schéma** ou **Éditeur de PCB**. La vue 3D (*Affichage → Visionneuse 3D*)
   utilise les modèles 3D installés avec les bibliothèques KiCad.

## Le schéma

Neuf feuillets hiérarchiques (alimentation, MCU, mémoires, écran, zones, sorties, audio, Ethernet, 4G).
Chaque broche porte une **étiquette globale** au nom de son équipotentielle ; les étiquettes de même
nom sont reliées sur tous les feuillets. Une croix bleue marque une broche volontairement libre.

## Le circuit imprimé

**Fait :**
- carte 4 couches (F.Cu / In1.Cu / In2.Cu / B.Cu), 300 × 210 mm, 4 trous de fixation M3 reliés à la masse ;
- 587 composants placés par fonction :
  - bord haut : antenne, micro-SIM, RJ45, USB-C, microSD, connecteurs de dalle, SWD, entrées d'alimentation ;
  - bord bas : 32 colonnes d'entrée de zone, chacune **alignée sur sa borne** ;
  - bord droit : 6 borniers de sortie ; bord gauche : haut-parleur, autoprotection, LED ;
  - centre : MCU et SDRAM côte à côte (6 mm), alimentation en haut à droite, 4G en haut à gauche, relais au milieu ;
- plans de masse GND sur In1.Cu et B.Cu ;
- classes d'équipotentielles : `Puissance` (pistes 0,6 mm), `Relais` (1 mm, isolement 0,5 mm),
  `SDRAM` (0,15 mm), `LVDS_100R`.

**Routage partiel automatique** (`route_auto.py`, Freerouting 1.9 + contrôles KiCad) :
- **251 équipotentielles routées** (19,5 m de pistes) : toutes les entrées de zones, multiplexeurs,
  commandes des relais, LED, I2C, UART, commandes du modem, surveillance, audio, et les contacts des
  relais en pistes de 1 mm ; couches F.Cu, In2.Cu et B.Cu ;
- **In1.Cu reste un plan de masse plein** (interdit aux pistes) ;
- **250 vias de masse** posées automatiquement (pastille CMS → plan), chacune contrôlée avant pose ;
- DRC KiCad : **aucune violation** (hors chevelu et avertissements de sérigraphie).

**Reste à router à la main (826 liaisons en chevelu)** — volontairement, car un autorouteur les
traiterait mal :
1. **alimentations** (VIN, P5V, P3V3, P3V3A, VZONE, P3V8_GSM, P12V_*, VBAT…) en pistes larges ou
   zones de cuivre (un plan 3,3 V peut être ajouté sur In2.Cu sous le MCU) ;
2. **régulateurs à découpage** (nœuds SW, BOOT, FB, buck-boost LM5176) : boucles les plus courtes possible ;
3. **SDRAM ↔ MCU** (FMC, ~ 60 liaisons, longueurs appariées ± 5 mm), **LVDS** (paires 100 Ω),
   **RMII Ethernet**, quartz, USB, QSPI, microSD ;
4. **antenne 4G** (piste 50 Ω coplanaire) et carte SIM ;
5. **150 pastilles de masse** des zones denses (broches VSS du MCU, condensateurs serrés) : une via
   chacune (touche V dans KiCad).

Le style de Freerouting (diagonales, bus parallèles) est fonctionnel mais perfectible : n'hésitez
pas à reprendre des pistes. Pour recommencer : `/usr/bin/python3 route_auto.py <freerouting.jar>`
(repart de la carte sans pistes).

**À vérifier avant fabrication :**
- les points déjà listés pour la netlist (SN75LVDS83B, relais, connecteur de dalle, valeurs du LM5176) ;
- l'emplacement des connecteurs par rapport au coffret et à la dalle (passages de câbles) ;
- l'empreinte de l'A7670E (LGA-124 24 × 24 mm), reprise de deux bibliothèques publiques identiques : à
  comparer au plan mécanique SIMCom ;
- l'empreinte TSOP-II-86 de la SDRAM, dessinée sur le modèle du TSOP-II-54 de KiCad : à comparer à la
  datasheet ISSI.

## Vérifications automatiques

```bash
python3 verif_schema.py           # schéma relu par KiCad == netlist de référence (broche par broche)
/usr/bin/python3 verif_pcb.py     # PCB : net de chaque pastille, lien schéma <-> PCB, DRC KiCad
```
État actuel : pastilles **OK**, lien schéma ↔ PCB **OK**, DRC : **aucune erreur** hors 826 liaisons à
router ; 38 avertissements de sérigraphie (textes qui se chevauchent, à retoucher).

## Régénérer

```bash
python3 ../netlist/gen_netlist.py   # description du circuit -> netlist OrCAD
python3 gen_schema.py               # -> schéma KiCad
/usr/bin/python3 gen_pcb.py         # -> circuit imprimé placé (API Python de KiCad, « pcbnew »)
/usr/bin/python3 route_auto.py <freerouting.jar>   # -> routage partiel + vias de masse
```
⚠️ Relancer `gen_schema.py` ou `gen_pcb.py` **écrase** le schéma ou le PCB. Dès que vous commencez à
modifier ou router dans KiCad, travaillez directement dans KiCad et ne relancez plus les générateurs.

`extraire_symboles.py` et l'option `gen_pcb.py --bibliotheque …` ne servent qu'à reconstruire les
symboles et la bibliothèque d'empreintes à partir des bibliothèques officielles KiCad.
