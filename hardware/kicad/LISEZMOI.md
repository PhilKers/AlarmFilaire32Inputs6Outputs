# Projet KiCad de la centrale d'alarme

Le dossier `alarme32/` est un **projet KiCad complet** : schéma + circuit imprimé + bibliothèque
d'empreintes du projet.

| Fichier | Contenu |
|---|---|
| `alarme32/alarme32.kicad_pro` | Projet (à ouvrir dans KiCad), classes d'équipotentielles et règles de conception |
| `alarme32/alarme32.kicad_sch` + 9 feuillets | Schéma hiérarchique |
| `alarme32/alarme32.kicad_pcb` | Circuit imprimé 4 couches 300 × 210 mm, composants placés, **non routé** |
| `alarme32/alarme32.pretty/` + `fp-lib-table` | Les 64 empreintes utilisées (bibliothèque du projet, autonome) |
| `alarme32/alarme32.dsn` | Export Specctra pour un essai d'autoroutage avec Freerouting |
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

**À faire par vous (ou avec un routeur) : le routage des 499 liaisons.** Un routage automatique
de qualité n'est pas réaliste sur cette carte : SDRAM 32 bits (longueurs appariées), paires LVDS
100 Ω, piste d'antenne 50 Ω, courants forts des relais. Ordre conseillé :
1. alimentations (régulateurs, inductances, condensateurs) et pistes larges des relais ;
2. SDRAM ↔ MCU (FMC), puis LVDS et RMII Ethernet ;
3. le reste des signaux ; un plan d'alimentation 3,3 V peut être ajouté sur In2.Cu.

Pour un premier jet automatique : ouvrez `alarme32.dsn` dans **Freerouting**
(https://github.com/freerouting/freerouting), puis réimportez la session (`.ses`) dans KiCad
(*Fichier → Importer → Session Specctra*). À reprendre à la main pour la SDRAM, le LVDS et l'antenne.

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
État actuel : pastilles **OK**, lien schéma ↔ PCB **OK**, DRC : **aucune erreur** hors liaisons à
router ; 40 avertissements de sérigraphie (textes qui se chevauchent, à retoucher).

## Régénérer

```bash
python3 ../netlist/gen_netlist.py   # description du circuit -> netlist OrCAD
python3 gen_schema.py               # -> schéma KiCad
/usr/bin/python3 gen_pcb.py         # -> circuit imprimé (API Python de KiCad, « pcbnew »)
```
⚠️ Relancer `gen_schema.py` ou `gen_pcb.py` **écrase** le schéma ou le PCB. Dès que vous commencez à
modifier ou router dans KiCad, travaillez directement dans KiCad et ne relancez plus les générateurs.

`extraire_symboles.py` et l'option `gen_pcb.py --bibliotheque …` ne servent qu'à reconstruire les
symboles et la bibliothèque d'empreintes à partir des bibliothèques officielles KiCad.
