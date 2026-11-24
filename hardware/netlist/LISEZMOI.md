# Netlist de la centrale d'alarme — import dans OrCAD

## Contenu

| Fichier (`sortie/`) | Usage |
|---|---|
| `alarme32_allegro.net` | **OrCAD PCB Editor / Allegro** : netlist au format tiers « Telesis » (`$PACKAGES` / `$NETS` / `$END`) |
| `devices/*.txt` | Fichiers « device » Allegro (un par type de composant et par empreinte) |
| `alarme32_pcb2.net` | Format **OrCAD PCB II** : OrCAD Layout (ancien), et importable par KiCad Pcbnew, DipTrace, Altium… |
| `alarme32_connexions.csv` | Tableur : composant / broche / nom de broche / équipotentielle (pour relecture) |
| `nomenclature.csv` | Nomenclature regroupée avec repères définitifs |
| `empreintes.csv` | Liste des 75 empreintes utilisées, avec le nombre et les numéros de pastilles |
| `RAPPORT.md` | Statistiques, contrôles et sources des brochages |

Chiffres : **587 composants, 466 équipotentielles, 2 165 broches** (103 déclarées non connectées).

## Import dans OrCAD PCB Editor (Allegro)

1. **Empreintes** : les noms utilisés (`LQFP208_28X28`, `R0603`, `TSOP2_86`, `RELAY_G5LE`…) sont génériques. Pour chacun, créez ou renommez une empreinte (`.dra/.psm`) de votre bibliothèque, en vous aidant de `empreintes.csv`. **Les numéros de pastilles doivent correspondre exactement**, y compris les pastilles alphanumériques : USB-C (`A1`…`B12`, `SH`), microSD (`SH`), RJ45 (`SH`).
   Astuce : vous pouvez aussi modifier le dictionnaire des empreintes directement dans `gen_netlist.py` pour utiliser les noms de votre bibliothèque, puis relancer le script.
2. **Chemins** : *Setup → User Preferences → Paths → Library* : ajoutez `sortie/devices` à `devpath` et votre bibliothèque d'empreintes à `psmpath` et `padpath`.
3. **Import** : *File → Import → Logic/Netlist…*, onglet **Other**, fichier `alarme32_allegro.net`, cocher *Create user-defined properties* puis **Import**.
4. **Placement** : *Place → Quickplace* (ou *Place → Manually*) ; les liaisons apparaissent en chevelu (ratsnest).

## Import dans OrCAD Layout (ancien) ou un autre outil

Utilisez `alarme32_pcb2.net` (format OrCAD PCB II). Dans KiCad : *Pcbnew → Fichier → Importer → Netlist* (les empreintes doivent exister sous les mêmes noms, ou être réassociées).

## Et le schéma dans OrCAD Capture ?

OrCAD Capture **ne sait pas créer un schéma à partir d'une netlist** : la netlist permet d'aller directement au routage PCB. Si vous dessinez le schéma dans Capture, vous pourrez exporter sa propre netlist et la comparer à celle-ci (même noms d'équipotentielles) pour vérifier votre saisie.

## Régénérer / modifier

```bash
python3 gen_netlist.py     # régénère tout le dossier sortie/
python3 verif_netlist.py   # relit les deux netlists et vérifie leur cohérence
```

Le circuit est décrit dans `gen_netlist.py`, feuillet par feuillet, avec les **noms** de broches (« PA3 », « VBAT », « SER IN »…). Le script les convertit en numéros via `sources/brochages.json` et refuse de générer si une broche est inconnue, oubliée ou affectée deux fois.

## Fiabilité des brochages

| Composants | Source |
|---|---|
| STM32H743BIT6 (LQFP208), LAN8742A, LMR33630, TPS62130, LM5176, TLV755, W25Q128, 24AA025E48, CD74HC4067, TXS0108E, INA226, MAX98357A, TPS3823, USBLC6, TPD4E05U06, ESDA6V1, LM74700, MOSFET, MMBT3904, BAV99, BAT54C, relais G5LE/G5V, 74AHCT125, connecteurs | Bibliothèque officielle **KiCad** (`kicad-symbols`) |
| TPIC6B595 | Bibliothèque KiCad de **Digi-Key** |
| IS42S32800J (SDRAM), TPS22918, A7670E | Bibliothèques publiques GitHub, **recoupées par deux sources indépendantes** (identiques) |
| SN75LVDS83B | Une seule bibliothèque publique (GitHub) : **à vérifier sur la datasheet TI** |

## Points à vérifier avant fabrication

1. **SN75LVDS83B** : contrôler le brochage TSSOP-56 sur la datasheet TI (une seule source).
2. **Relais G5LE-1 / G5V-1** : les fonctions COM / repos / travail / bobine sont déduites du dessin du symbole KiCad ; les confirmer sur la datasheet Omron.
3. **Connecteur de dalle LVDS** (20 broches) : brochage **générique**, à adapter à la dalle retenue ; vérifier aussi VESA/JEIDA.
4. **Détection microSD** : la numérotation de la broche de détection (`9`) dépend du support choisi.
5. **Régulateur buck-boost LM5176** : la topologie est complète mais les valeurs (fréquence, compensation, résistances de mesure, MOSFET, inductance) sont à recalculer avec **TI WEBENCH**.
6. **Module A7670E** : 124 broches du symbole ; l'empreinte LCC+LGA du fabricant peut comporter des pastilles de masse supplémentaires (toutes à relier à GND).

## Licence des données de brochage

Seuls des faits (numéros et noms de broches) ont été extraits. Les bibliothèques KiCad et
Digi-Key sont sous licence CC-BY-SA 4.0 avec exception pour les conceptions électroniques ;
les autres dépôts sont cités dans `sortie/RAPPORT.md` (voir leurs licences respectives).
