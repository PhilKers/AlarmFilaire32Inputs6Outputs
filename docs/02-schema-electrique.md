# 2. Schéma électrique détaillé

Le schéma est découpé en feuillets (comme dans un projet KiCad hiérarchique).
Les désignations (U, Q, K, R, C…) correspondent à la nomenclature `04-nomenclature.md`.
Les valeurs marquées « *datasheet* » sont à reprendre du schéma d'application
du fabricant (régulateurs à découpage notamment) ; elles ne sont pas critiques
pour l'architecture.

> 📌 **La netlist fait foi.** Le circuit complet, broche par broche, est décrit
> dans `hardware/netlist/gen_netlist.py` et exporté pour OrCAD dans
> `hardware/netlist/sortie/` (voir `hardware/netlist/LISEZMOI.md`). Les repères
> (U101, R402…) de la netlist et de `sortie/nomenclature.csv` remplacent ceux,
> indicatifs, utilisés ci-dessous.

> ⚠️ Ce document est un **schéma de référence** à saisir dans un outil de CAO
> (KiCad conseillé). Il n'a pas encore été prototypé : prévoir une première
> série de cartes de validation avant toute installation réelle.

---

## Feuillet 1 — Entrée d'alimentation et batterie

```
          BLOC PSC-100A (13,8 V)                               CARTE CENTRALE
   ┌──────────────────────────┐
   │ +V  ─────────────────────┼──► J1.1 ──F1 (5 A, rapide)──┬────────┬──► U1 LM74700-Q1 + Q1 ──┬──► VIN (10…14,4 V)
   │ -V  ─────────────────────┼──► J1.2 ─────────── GND     │        │    (diode idéale,       │
   │                          │                       D1 SMBJ16A      │     anti-inversion)    ├─ C1 470 µF/25 V
   │ BAT+ ────────────────────┼──► J2.1 ──┐              (TVS)        │                        ├─ C2 10 µF/25 V X7R
   │ BAT- ────────────────────┼──► J2.2 ─ GND                         │                        └─ C3 100 nF
   │                          │           │ R1 10 mΩ 1 % 2 W (shunt, Kelvin)
   │ AC OK (contact)  ────────┼──► J3.1   │
   │ BAT LOW (contact) ───────┼──► J3.2   ├──► J2.3 ──► câble ── F2 (7,5 A, porte-fusible auto AU PLUS PRÈS de la batterie) ──► Batterie +
   │ COM contacts ────────────┼──► J3.3 = GND        J2.4 ───────────────────────────────► Batterie −
   └──────────────────────────┘
                                    U2 INA226 (I2C4, adresse 0x40)
                                    IN+ ── côté PSC (J2.1)       VBUS ── côté batterie (J2.3)
                                    IN− ── côté batterie (J2.3)  A0 = A1 = GND
                                    VS  ── 3V3, 100 nF           SCL/SDA ── PD12/PD13 (2,2 kΩ vers 3V3)
                                    Courant > 0 = charge, < 0 = décharge.

   Contacts secs du PSC (J3) :            3V3
                                           │
                                          R 10 kΩ
                                           │
       J3.1 (AC OK) ──── R 1 kΩ ───────────┼──── PA15 (AC_OK, 0 = secteur présent)
                                           │
                                          C 100 nF ─ GND      + D ESD PESD3V3 vers GND
       J3.2 (BAT LOW) : même montage ──► PB15
```

**Rails secondaires**

| Rail | Régulateur | Courant | Remarques |
|---|---|---|---|
| +5V | U3 **LMR33630ADDA** (buck 36 V / 3 A, 400 kHz) | 3 A | R_haut 100 kΩ, R_bas 24,9 kΩ (V_FB = 1,0 V → 5,02 V) ; L = 10 µH (XAL6060) ; C_out 2 × 22 µF |
| +3V8_GSM | U4 **LMR33630ADDA** | 3 A crête | R_haut 100 kΩ, R_bas 35,7 kΩ → 3,80 V ; EN ← PE3 (MODEM_PWR_EN) + 100 kΩ vers GND (module éteint par défaut) ; C_out 2 × 22 µF + **470 µF polymère** + 100 µF près du module |
| +3V3 | U5 **TPS62130** (buck 3 A, entrée +5V) | 1,5 A | R_haut 316 kΩ, R_bas 100 kΩ (V_FB 0,8 V → 3,33 V) ; L = 2,2 µH ; C_out 2 × 22 µF |
| +3V3A (VDDA, VREF+) | U6 **TLV75533PDBV** (LDO 500 mA, entrée +5V) | 20 mA | Ferrite BLM18PG221 + 1 µF + 100 nF côté MCU |
| VZONE (tirage des boucles) | U7 **TLV75533PDBV** | 25 mA | Dédié : un court-circuit sur les boucles ne perturbe pas l'ADC ; 10 µF + 100 nF |
| +12V_LCD (rétroéclairage) | U8 **LM5176** (buck-boost 4 interrupteurs) | 1,5 A | 12,0 V régulés quelle que soit VIN (10…14,4 V) ; composants *datasheet / WEBENCH* |
| +3V3_LCD | U9 **TPS22918** (interrupteur de charge) | 0,6 A | ON ← PC7 (LCD_PWR_EN) ; démarrage progressif CT = 1 nF |
| +12V_AUX (détecteurs) | Q2 **FDS9435A** (P-MOS 30 V, SO-8) + Q3 MMBT3904 | 1 A | Grille : 10 kΩ vers VIN, collecteur Q3 → grille via 4,7 kΩ ; base Q3 ← PG6 via 10 kΩ ; **PTC F3 1,1 A** (MF-MSMF110) ; TVS D2 SMBJ16A sur la borne |
| +12V_OUT (bus des sorties) | — (VIN) | 2 A | **PTC F4 2 A** (MF-MSMF200) puis une PTC 0,5 A par sortie |
| +5V_OUT (bus des sorties) | — (+5V) | 1 A | **PTC F5 1,1 A** puis une PTC 0,5 A par sortie |

**Mesures de tension** (ponts diviseurs 1 %, 100 nF sur l'entrée ADC) :

| Signal | Pont | Broche ADC |
|---|---|---|
| VIN | 51 kΩ / 10 kΩ (÷ 6,1) | PA4 — ADC1_INP18 |
| +5V | 10 kΩ / 10 kΩ (÷ 2) | PA0 — ADC1_INP16 |
| VZONE | 10 kΩ / 10 kΩ (÷ 2) | PB1 — ADC1_INP5 |
| +12V_AUX | 51 kΩ / 10 kΩ | PC0 — ADC1_INP10 |
| +12V_OUT | 51 kΩ / 10 kΩ | PF10 — ADC3_INP6 |

---

## Feuillet 2 — Microcontrôleur STM32H743BIT6

```
                          ┌──────────────────────────────────────┐
     +3V3 ── 14 × 100 nF ─┤ VDD (×14)                             │
             + 2 × 4,7 µF │                                       │
     +3V3A ───────────────┤ VDDA, VREF+   (1 µF + 100 nF chacun)  │
     GND ─────────────────┤ VSS, VSSA                             │
                          │                                       │
     2 × 2,2 µF ──────────┤ VCAP1, VCAP2  (régulateur LDO interne)│
                          │                                       │
     X1 25 MHz ───────────┤ PH0-OSC_IN / PH1-OSC_OUT (2 × 12 pF*) │
     X2 32,768 kHz ───────┤ PC14 / PC15             (2 × 6,8 pF*) │
                          │                                       │
     BT1 CR2032 ─┐        │                                       │
     +3V3 ───────┴─ D3 BAT54C ── VBAT (100 nF)                    │
                          │                                       │
     U10 TPS3823-33 ─BAT54J┤ NRST  (100 nF, bouton RESET SW1)      │
        WDI ← PG7         │                                       │
        MR  ← SW1         │                                       │
                          │                                       │
     R 10 kΩ ─ GND ───────┤ BOOT0 (cavalier JP1 vers 3V3 = DFU)   │
                          │                                       │
     J4 SWD 2×5 1,27 mm ──┤ PA13 SWDIO, PA14 SWCLK, PB3 SWO, NRST │
                          │                                       │
     J5 USB-C (maint.) ───┤ PA11 D−, PA12 D+  (U11 USBLC6-2SC6,   │
        CC1/CC2 5,1 kΩ    │  pas d'alimentation par l'USB)        │
                          └──────────────────────────────────────┘
   * valeurs à ajuster selon la capacité de charge (CL) du quartz choisi.
```

- Le TPS3823-33 a une sortie push-pull : une diode Schottky BAT54J (cathode côté TPS3823, anode côté NRST) lui permet seulement de tirer NRST à 0, sans entrer en conflit avec le reset interne du MCU. WDI doit être rafraîchi toutes les < 1,6 s.
- Horloges : HSE 25 MHz → PLL1 480 MHz (CPU) ; PLL2R 200 MHz → FMC (SDCLK 100 MHz) ; PLL3R → LTDC 65 MHz ; PLL1Q → SDMMC 100 MHz.

---

## Feuillet 3 — Mémoires

**SDRAM U12 IS42S32800J-6TL** (8 M × 32 bits = 32 Mo, TSOP-II 86), banque FMC SDRAM 1 :

| SDRAM | STM32 | SDRAM | STM32 |
|---|---|---|---|
| A0…A5 | PF0…PF5 | DQ0, DQ1 | PD14, PD15 |
| A6…A9 | PF12…PF15 | DQ2, DQ3 | PD0, PD1 |
| A10, A11 | PG0, PG1 | DQ4…DQ12 | PE7…PE15 |
| BA0, BA1 | PG4, PG5 | DQ13…DQ15 | PD8…PD10 |
| CLK | PG8 | DQ16…DQ23 | PH8…PH15 |
| CKE | PH2 (SDCKE0) | DQ24…DQ27 | PI0…PI3 |
| /CS | PH3 (SDNE0) | DQ28, DQ29 | PI6, PI7 |
| /RAS | PF11 | DQ30, DQ31 | PI9, PI10 |
| /CAS | PG15 | DQM0, DQM1 | PE0, PE1 |
| /WE | PH5 | DQM2, DQM3 | PI4, PI5 |

Découplage : 8 × 100 nF + 2 × 10 µF. Horloge : résistance série 22 Ω près du MCU.
Plan mémoire : 0xC000 0000 tampon d'image 0 (1,5 Mo), 0xC018 0000 tampon 1, reste pour images/polices LVGL.

**Flash QSPI U13 W25Q128JVSIQ** (16 Mo) : CLK PB2, /CS PB6 (10 kΩ vers 3V3), IO0 PF8, IO1 PF9, IO2 PF7, IO3 PF6 ; 100 nF.
Contient : configuration (2 copies, fin de mémoire), polices et images de l'IHM.

**microSD J6** (Molex 503182-1852) : SDMMC1 — D0 PC8, D1 PC9, D2 PC10, D3 PC11, CK PC12, CMD PD2 ; tirages 47 kΩ sur CMD et D0…D3 ; détection carte → PD11 (10 kΩ vers 3V3) ; 10 µF + 100 nF. Contient les fichiers vocaux `/VOIX/` et le journal `JOURNAL.CSV`.

**EEPROM U14 24AA025E48** (I2C4, 0x50) : adresse MAC unique EUI-48 pour l'Ethernet.

---

## Feuillet 4 — Écran 1024×768 et tactile

```
 STM32 LTDC (RGB888)                U15 SN75LVDS83B (TSSOP-56)           J7 connecteur LVDS dalle
 ───────────────────                ──────────────────────────           (brochage selon la dalle)
 R0…R7  PI15,PJ0…PJ6   ───────────► D0…D7  (voir table JEIDA/VESA)  ┌──► Y0± ─► RXIN0±
 G0…G7  PJ7…PJ11,PK0…PK2 ─────────► D8…D15                          ├──► Y1± ─► RXIN1±
 B0…B7  PJ12…PJ15,PK3…PK6 ────────► D16…D23                         ├──► Y2± ─► RXIN2±
 HSYNC PI12, VSYNC PI13, DE PK7 ──► D24…D26                         ├──► Y3± ─► RXIN3±
 CLK   PI14 (65 MHz) ─────────────► CLKIN                           └──► CLKOUT± ─► RXCLKIN±
 PC6 (LVDS_SHDN) ─────────────────► /SHTDN (10 kΩ vers GND)
                                    R/F = 3V3 (front montant), VCC/PLLVCC/LVDSVCC = 3V3 + 3 × 100 nF + 10 µF
                                    Paires LVDS : 100 Ω différentiel, longueurs appariées ± 0,5 mm

 J7 (alimentation dalle) : VDD ← +3V3_LCD (U9), VLED ← +12V_LCD (U8),
                           BL_EN ← PC13, BL_PWM ← PA8 (TIM1_CH1, 20 kHz)
                           (si la dalle attend 5 V sur BL_EN/PWM : ajouter un 74LVC2T45)

 J8 FPC 6 broches dalle tactile (contrôleur GT911/GT928 sur la nappe) :
   1 VDD ← 3V3   2 GND   3 SCL ── PB8   4 SDA ── PB9   (2,2 kΩ vers 3V3)
   5 INT ── PB7 (10 kΩ vers 3V3)       6 RST ── PB4
```

Correspondance LTDC → SN75LVDS83B (format **VESA 24 bits**) : D0-D4 = R0-R4, D6 = R5, D7 = G0, D8-D9 = G1-G2, D12-D14 = G3-G5, D15 = B0, D18-D22 = B1-B5, D24 = HSYNC, D25 = VSYNC, D26 = DE, D27 = R6, D5 = R7, D10-D11 = G6-G7, D16-D17 = B6-B7, D23 = GND. Pour une dalle **JEIDA**, échanger les bits de poids fort et faible (voir la datasheet de la dalle).

Dalle conseillée : 10,4" XGA LVDS 1 canal 6/8 bits avec LED intégrées (ex. AUO G104XVN01.x ou équivalent Innolux/Kyocera), dalle tactile PCAP collée (optical bonding conseillé en cas de lumière forte). **Vérifier le brochage et le mapping JEIDA/VESA de la dalle retenue** avant de router J7.

Timings LTDC (XGA 60 Hz) : horloge 65 MHz, HSYNC 136, HBP 160, HFP 24, VSYNC 6, VBP 29, VFP 3.

---

## Feuillet 5 — Entrées de zones (× 32)

Schéma d'**une** zone (identique pour les 32) :

```
 VZONE (3,3 V)
    │
 [R_PU 4,7 kΩ 1 %]
    │
  N ●────────────[R_F 10 kΩ]────────●──────────► entrée Yn du multiplexeur
    │                               │
    ├── D_CL BAV99 (clamp 3V3/GND) [C_F 100 nF]
    │                               │
 [R_S 470 Ω 0,5 W anti-surtension] GND
    │
    ●──────────────────────────────────────────► Borne Zx
    │
 [D_Z TVS SMAJ15A]
    │
   GND ────────────────────────────────────────► Borne 0V
```

**Câblage des détecteurs** (R_EOL = 4,7 kΩ 1 %) :

```
 NF simple (sans résistance)      NF + 1 résistance (SEOL)       NF + 2 résistances (DEOL, recommandé)
 Zx ──o/ o── 0V                   Zx ──o/ o──[4k7]── 0V          Zx ──┬──o/ o──┬──[4k7]── 0V
     contact                           contact                         └─[4k7]──┘
                                                                      (R_ALM)   (R_EOL)
                                                       autoprotection du détecteur en série avec la boucle
```

**Valeurs mesurées** (rapport V_nœud / VZONE, R_S incluse) :

| État de la boucle | R boucle | Rapport | Seuil logiciel (‰) | Interprétation DEOL |
|---|---|---|---|---|
| Court-circuit | 0 Ω | 0,091 | < 300 | **Sabotage** |
| Repos (contact fermé) | 4,7 kΩ | 0,524 | 300…600 | Repos |
| Alarme (contact ouvert) | 9,4 kΩ | 0,677 | 600…850 | **Alarme** |
| Coupure | ∞ | 1,000 | > 850 | **Sabotage** |

**Multiplexage**

```
 Zones 1-16  ──► U17 CD74HC4067 (Y0…Y15) ── Z ──► PA3 (ADC1_INP15) + 1 nF
 Zones 17-32 ──► U18 CD74HC4067 (Y0…Y15) ── Z ──► PB0 (ADC2_INP9)  + 1 nF
 S0…S3 (communs aux deux) ◄── PD4…PD7      /E = GND      VCC = 3V3 (100 nF)
```

Cycle d'acquisition : 16 adresses × (15 µs d'établissement + 2 conversions) ≈ 0,4 ms, répété toutes les 10 ms ; temps de réponse logiciel configurable par zone (300 ms par défaut, 100 ms pour le sabotage).

Bornier : 32 bornes de zone + 16 bornes 0 V (une pour deux zones) + 4 bornes AUX 12 V / 0 V. Borniers débrochables pas de 5 mm.

---

## Feuillet 6 — Sorties relais (× 6) et LED

**Chaîne de commande**

```
  STM32 (3,3 V)          74AHCT125 (VCC 5 V, seuils TTL)        3 × TPIC6B595 (VCC 5 V)
  SCK  PA5  ───────────► 1A → 1Y ──► SRCK (commun aux 3)
  MOSI PB5  ───────────► 2A → 2Y ──► SER IN n°1 ─SER OUT─► SER IN n°2 ─SER OUT─► SER IN n°3
  RCK  PG14 ─(10k↓)────► 3A → 3Y ──► RCK (commun aux 3)                               │
  /G   PG3  ─(10k↑3V3)─► 4A → 4Y ──► /G  (commun aux 3, 10 kΩ vers 5 V)              SER OUT n°3
  MISO PA6 ◄── 10 kΩ ──┬─────────────────────────────────────────────────────────────────┘
                       20 kΩ → GND   (pont 5 V → 3,3 V pour la relecture)
  /SRCLR = 5 V ; /OE du 74AHCT125 = GND ; 100 nF par boîtier

  ⚠️ Le TPIC6B595 alimenté en 5 V exige VIH ≥ 0,85 × VCC = 4,25 V : les sorties
  3,3 V du STM32 ne suffisent pas, d'où le tampon 74AHCT125 (VIH = 2 V).

 Bits : 0-5 K_MAIN1-6 | 6-11 K_SRC1-6 | 12-17 K_VSEL1-6 | 18-23 LED (Secteur, Marche, Alarme, Défaut, Réseau, GSM)
```

**Une sortie** (n = 1…6) :

```
                 +12V_OUT ──PTC 0,5 A──┐                 +5V_OUT ──PTC 0,5 A──┐
                                       │ NF                                   │ NO
                                  ┌────┴──────────────────────────────────────┴───┐
                                  │  K_VSEL n  (G5V-1 12 V, inverseur 1 A)        │
                                  │            COM ──► nœud SRC_n                 │
                                  └───────────────────────┬──────────────────────┘
                                                          │
          Borne COM_n ───────────── NF ┌──────────────────┴───── NO ┐
                                       │  K_SRC n  (G5V-1 12 V)     │
                                       │      COM ──► nœud C_n      │
                                       └──────────────┬─────────────┘
                                                      │
                                       ┌──────────────┴─────────────┐
                                       │  K_MAIN n (G5LE-1 12 V,    │
                                       │  inverseur 10 A)  COM = C_n│
                                       └──────┬──────────────┬──────┘
                                           NO │              │ NF
                                       Borne NO_n       Borne NF_n          Borne 0V_n = GND

 Bobines : +12V (VIN) ──┬── bobine ──┬── DRAIN TPIC6B595
                        └── 1N4148W ─┘  (diode de roue libre, cathode côté +12 V)
```

| Mode logiciel | K_SRC | K_VSEL | Comportement du bornier |
|---|---|---|---|
| Contact sec | repos | repos | COM_n / NO_n / NF_n isolés (contact libre de potentiel) |
| +12 V | travail | repos | NO_n reçoit +12 V (PTC 0,5 A) quand la sortie est active ; charge entre NO_n et 0V_n |
| +5 V | travail | travail | NO_n reçoit +5 V (PTC 0,5 A) quand la sortie est active |

Limites : contact sec **≤ 30 V DC / 2 A** (TBTS uniquement, aucune tension secteur sur la carte) ; modes alimentés 0,5 A par sortie, 2 A cumulés sur le bus 12 V. Pour piloter une charge plus forte ou du 230 V, utiliser un contacteur externe en mode contact sec.

**LED de façade** : anode → +3V3 via 470 Ω, cathode → DRAIN du TPIC6B595 (bits 18 à 23).

**Autoprotection du coffret** : microrupteur NF (fermé couvercle en place) entre PG9 et GND, tirage 10 kΩ vers 3V3, filtre 1 kΩ / 100 nF.

---

## Feuillet 7 — Audio

```
 SAI1_FS_A  PE4 ──► LRCLK ┐
 SAI1_SCK_A PE5 ──► BCLK  │  U22 MAX98357AETE+ (I2S, classe D, 3,2 W / 4 Ω)
 SAI1_SD_A  PE6 ──► DIN   │  VDD = +5V : 10 µF + 100 nF + 220 µF
 PE2 ──R 220 kΩ──► SD_MODE│  (≈ 1 V avec la résistance interne 100 kΩ → mode (G+D)/2 ; 0 V = arrêt)
                   GAIN   ┘  non connectée = 9 dB
                   OUTP/OUTN ──► ferrites + 1 nF ──► J9 haut-parleur 4 à 8 Ω, 3 W
```

Format des fichiers : WAV PCM 16 bits mono 16 kHz. Le logiciel duplique l'échantillon sur les deux voies I2S.

---

## Feuillet 8 — Ethernet

```
 STM32 (RMII)                  U23 LAN8742A-CZ (QFN-24)                 J10 HR911105A (RJ45 + transfo + LED)
 PA1  ETH_REF_CLK ◄──────────── nINT/REFCLKO (50 MHz)                    TD+ / TD− ◄── TXP/TXN
 PA2  ETH_MDIO ◄──────────────► MDIO (1,5 kΩ vers 3V3)                   RD+ / RD− ──► RXP/RXN
 PC1  ETH_MDC ────────────────► MDC                                       4 × 49,9 Ω 1 % vers 3V3 (ferrite)
 PA7  ETH_CRS_DV ◄──────────── CRS_DV/MODE2                               + 100 nF sur les points milieux
 PC4  ETH_RXD0 ◄─────────────── RXD0/MODE0                                LED verte/jaune ◄── LED1/LED2
 PC5  ETH_RXD1 ◄─────────────── RXD1/MODE1                                ESD : U24 TPD4E05U06 côté PHY
 PG11 ETH_TX_EN ──────────────► TXEN
 PG13 ETH_TXD0 ───────────────► TXD0                    XTAL1/XTAL2 : X3 25 MHz + 2 × 15 pF*
 PG12 ETH_TXD1 ───────────────► TXD1                    RBIAS : 12,1 kΩ 1 %
 PG10 PHY_RST ────────────────► nRST (10 kΩ vers 3V3)   VDDCR : 1 µF + 470 pF
                                                        VDD1A/VDD2A/VDDIO : 3V3 via ferrite, 100 nF + 10 µF
 Straps : LED2/nINTSEL = 0 (10 kΩ vers GND) → sortie REFCLKO 50 MHz ; RXER/PHYAD0 = 0 → adresse PHY 0 ;
          MODE[2:0] = 111 (tout auto-négocié, tirages 10 kΩ vers 3V3)
```

---

## Feuillet 9 — Module 4G SIMCom A7670E

```
 +3V8_GSM ──┬── VBAT (×2)        U25 A7670E-LASE                                    
            ├─ C 470 µF polymère + 100 µF + 10 µF + 100 nF + 33 pF                    
            └─ D4 SMF5.0A (TVS)                                                      
                                                                                   
 3V3 ──► VCCB  U26 TXS0108E  VCCA ◄── VDD_EXT (1,8 V, sortie du module)              
         OE ◄── VDD_EXT (10 kΩ) : traducteur actif seulement module allumé           
   PB10 USART3_TX  ─► B1 ── A1 ─► RXD                                                
   PB11 USART3_RX  ◄─ B2 ── A2 ◄─ TXD                                                
   PB14 USART3_RTS ─► B3 ── A3 ─► RTS                                                
   PB13 USART3_CTS ◄─ B4 ── A4 ◄─ CTS                                                
   PI11 MODEM_DTR  ─► B5 ── A5 ─► DTR                                                
   PH4  MODEM_RI   ◄─ B6 ── A6 ◄─ RI                                                 
   PI8  MODEM_STAT ◄─ B7 ── A7 ◄─ STATUS                                             
                                                                                   
 PH6 ──4,7 kΩ──► base Q4 MMBT3904 (47 kΩ B-E) ; collecteur ──► PWRKEY (niveau haut MCU = PWRKEY à 0)
 PH7 ──4,7 kΩ──► base Q5 MMBT3904             ; collecteur ──► RESET                 
                                                                                   
 J11 nano-SIM : SIM_VDD, SIM_DATA, SIM_CLK, SIM_RST — 22 Ω série + 33 pF vers GND,    
              U27 ESDA6V1W5 au plus près du support                                   
 J12 U.FL MAIN_ANT ── réseau en π (0 Ω + 2 × NC) ── piste 50 Ω coplanaire             
 TP USB_VBUS / USB_DP / USB_DM du module sur pastilles de test (mise à jour firmware)
 NETLIGHT ──► LED via 1 kΩ (indication réseau)                                     
```

Antenne LTE externe au coffret métallique (passe-cloison SMA). Le régulateur 3,8 V (U4) doit tenir 2 A de crête : pistes larges, condensateur polymère à moins de 10 mm des broches VBAT.

---

## Feuillet 10 — Connecteurs et borniers

| Repère | Fonction | Broches |
|---|---|---|
| J1 | Entrée 13,8 V depuis le PSC | +V, −V |
| J2 | Batterie (via shunt) | BAT_PSC+, GND, BAT+, BAT− |
| J3 | Contacts PSC | AC OK, BAT LOW, COM |
| J14…J21 | Zones (8 borniers de 6 : 4 zones + 2 × 0V) | Z1…Z32, 0V |
| J22 | Alimentation détecteurs | +12V AUX ×2, 0V ×2 |
| J23…J28 | Sorties 1 à 6 | COM, NO, NF, 0V |
| J9 | Haut-parleur | +, − |
| J29 | Autoprotection coffret | 2 broches |
| J7, J8 | Dalle LVDS, tactile | selon dalle |
| J10 | Ethernet RJ45 | — |
| J4, J5 | SWD, USB-C maintenance | — |
| J6, J11 | microSD, nano-SIM | — |
| J12 | Antenne 4G principale (l'A7670E n'a pas d'entrée de diversité) | U.FL |
