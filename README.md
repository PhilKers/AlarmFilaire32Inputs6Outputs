# Centrale d'alarme filaire 32 zones / 6 sorties — STM32H7

Conception complète (matériel + logiciel) d'une centrale d'alarme :

- **32 entrées filaires** supervisées : NF, NO, simple ou double résistance de fin de ligne (repos / alarme / coupure / court-circuit)
- **6 sorties relais**, mode électrique **configurable par logiciel** : contact sec, +12 V ou +5 V
- **Annonces vocales** en français (haut-parleur local et messages au téléphone)
- **Ethernet** : API HTTP/JSON, mise à l'heure SNTP
- **4G** (SIMCom A7670E + nano-SIM) : SMS, appels en cascade, acquittement et commandes DTMF, pilotage par SMS
- **Alimentation secourue** : bloc 13,8 V avec chargeur, batterie 12 V, surveillance secteur / batterie / fusibles
- **Écran tactile 10,4" 1024×768** (LVGL v9)
- **MCU STM32H743BIT6** (Cortex-M7 480 MHz) + SDRAM 32 Mo + Flash QSPI 16 Mo + microSD

## Documentation

| Document | Contenu |
|---|---|
| [docs/01-architecture.md](docs/01-architecture.md) | Synoptique, choix techniques, bilan de consommation, autonomie |
| [docs/02-schema-electrique.md](docs/02-schema-electrique.md) | Schéma détaillé par feuillets : alimentation, MCU, mémoires, écran, zones, relais, audio, Ethernet, 4G, connecteurs |
| [docs/03-brochage-stm32.md](docs/03-brochage-stm32.md) | Affectation de toutes les broches du STM32 |
| [docs/04-nomenclature.md](docs/04-nomenclature.md) | Nomenclature (BOM) |
| [docs/05-logiciel.md](docs/05-logiciel.md) | Architecture logicielle, machine d'états, tâches, commandes SMS/DTMF/API, construction |
| [docs/06-mise-en-service.md](docs/06-mise-en-service.md) | Câblage des détecteurs, programmation, procès-verbal d'essais |
| [hardware/netlist/LISEZMOI.md](hardware/netlist/LISEZMOI.md) | **Netlist pour OrCAD** (Allegro/PCB Editor et OrCAD PCB II), nomenclature et empreintes générées |

## Arborescence

```
firmware/
├── core/                 Noyau portable en C11 (logique d'alarme), testé sur PC
├── platform/stm32h7/     Pilotes et tâches FreeRTOS pour la carte (HAL, lwIP, LVGL, FatFS)
├── tests/                Tests unitaires du noyau
├── sim/                  Simulateur console
└── CMakeLists.txt        Construction PC (tests + simulateur)
tools/
├── gen_voix.sh           Génération des messages vocaux (WAV + AMR)
└── gen_fonts.sh          Génération des polices LVGL avec accents
```

## Essai rapide sur PC

```bash
cd firmware
cmake -S . -B build && cmake --build build
./build/test_core      # tests unitaires du noyau
./build/alarm_sim      # simulateur : essayez « mes 1234 », « att 31 », « z 3 ouvert », « sms 1234 ETAT »
```

## État du projet

| Partie | État |
|---|---|
| Schéma de référence, brochage, nomenclature | Rédigés — brochage à valider dans STM32CubeMX |
| Netlist (587 composants, 466 équipotentielles) | Générée et vérifiée automatiquement (brochages issus de bibliothèques KiCad) |
| Noyau logiciel (zones, moteur, sorties, codes, journal, notifications, voix, commandes) | Écrit, **compilé et testé** (105 vérifications) |
| Couche STM32 (pilotes, modem 4G, réseau, IHM) | Écrite, **non compilée ni essayée sur carte** (nécessite le projet CubeMX) |
| Prototype matériel | À réaliser |

> Projet personnel non certifié NF&A2P / EN 50131. La partie 230 V est confiée à un bloc d'alimentation du commerce et doit être raccordée par une personne qualifiée.
