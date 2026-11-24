# 6. Mise en service et essais

## 6.1 Sécurité électrique

- Le raccordement 230 V du bloc PSC-100A doit être réalisé par une personne qualifiée : coffret relié à la terre, disjoncteur dédié 2 A courbe C, presse-étoupes.
- La carte centrale ne doit recevoir **que de la très basse tension** (≤ 30 V). Aucune charge 230 V sur les sorties relais : utiliser un contacteur externe piloté en mode contact sec.
- Batterie : fusible au plus près de la borne +, câble 1,5 mm² minimum.

## 6.2 Première mise sous tension (carte nue)

1. Sans MCU programmé, sans batterie : alimenter J1 par une alimentation de laboratoire 13,8 V limitée à 300 mA.
2. Mesurer : +5V (5,0 V), +3V3 (3,30 V), +3V3A, VZONE (3,30 V), +3V8_GSM = 0 V (régulateur désactivé par défaut).
3. Vérifier qu'aucun relais ne colle (TPIC6B595 bloqués par /G).
4. Programmer le firmware par SWD (ST-LINK V3), console de débogage sur USART1 115200 bauds.
5. Contrôler la SDRAM (test mémoire au démarrage), l'écran (mire), le tactile.
6. Mettre en place la microSD avec le dossier `/VOIX/`, la carte SIM, l'antenne, le câble Ethernet.

## 6.3 Câblage des détecteurs

Câble 4 conducteurs (2 pour l'alimentation 12 V AUX, 2 pour la boucle) ou 6 conducteurs (+ autoprotection en série dans la boucle).

```
 Détecteur IR (contact d'alarme NF + autoprotection NF)        Contact d'ouverture (porte)

  +12 AUX ──────────────► +12 V détecteur                        Zx ──┬── contact NF ──┬──[4k7]── 0V
  0V ───────────────────► 0 V détecteur                              └─────[4k7]─────┘
  Zx ──┬── alarme NF ──┬── autoprot. NF ──[4k7]── 0V                 (résistances DANS le contact)
       └────[4k7]──────┘
       (résistances DANS le détecteur, pas dans la centrale)
```

Les résistances de fin de ligne doivent être placées **dans le détecteur** : un fraudeur qui shunte le câble près de la centrale crée alors un court-circuit, détecté comme sabotage.

Détecteurs de fumée 4 fils : alimenter par AUX 12 V ; la coupure momentanée de AUX (sortie PG6) permet de les réarmer après une alarme.

## 6.4 Programmation (écran ou futur outil réseau)

1. Code installateur `0000` → changer immédiatement les codes installateur et maître.
2. Zones : nom, type, câblage (NF / NO / SEOL / DEOL), inclusion en partiel, carillon, temps de réponse.
3. Sorties : fonction, mode électrique (sec / 12 V / 5 V), inversion, impulsion.
4. Temporisations : sortie (30 s), entrée (20 s), sirène (180 s).
5. Contacts : numéros au format international (`+336…`), SMS / appel / télécommande, événements.
6. Réseau : DHCP ou IP fixe, serveur NTP, fuseau horaire.

## 6.5 Procès-verbal d'essais

| Essai | Attendu | OK |
|---|---|---|
| Chaque zone : repos / ouverture / coupure / court-circuit | Repos, alarme, sabotage, sabotage ; mesure ‰ visible dans l'onglet Zones | ☐ |
| MES totale avec zone immédiate ouverte | Refus, zone indiquée | ☐ |
| MES, sortie par la porte, retour | Tempo de sortie, armé, tempo d'entrée, MHS par code | ☐ |
| Intrusion armé | Sirènes 12 V, flash, annonce vocale, SMS, appel en cascade | ☐ |
| Acquittement par touche 1 au téléphone | Arrêt de la cascade d'appels | ☐ |
| SMS `code ETAT` / `MES` / `MHS` | Réponse et action correctes ; numéro inconnu ignoré | ☐ |
| 5 codes faux | Verrouillage 3 min, événement journalisé | ☐ |
| Changement de mode d'une sortie (sec → 12 V → 5 V) | Tension correcte au bornier, aucun court-circuit | ☐ |
| Court-circuit sur une sortie 12 V | PTC 0,5 A déclenche, défaut « fusible sorties » si bus affecté | ☐ |
| Coupure secteur | Défaut après 60 s, annonce, SMS ; fonctionnement sur batterie | ☐ |
| Batterie débranchée | Défaut « batterie absente » | ☐ |
| Ouverture du coffret | Sabotage coffret | ☐ |
| Débranchement Ethernet / antenne | Défauts réseau / GSM (GSM après 2 min) | ☐ |
| Blocage logiciel simulé (débogueur) | Redémarrage par chien de garde, relais au repos pendant le reset | ☐ |
| Autonomie | Mesure du courant en veille écran éteint, calcul de l'autonomie | ☐ |

## 6.6 Normes et limites

Cette centrale est un projet personnel : elle **n'est pas certifiée** NF&A2P / EN 50131 et ne peut pas être présentée comme telle à un assureur. Le logiciel reprend néanmoins plusieurs principes de la norme EN 50131 (supervision des boucles, autoprotection, journal horodaté, défauts d'alimentation, limitation de durée des sirènes). La durée de la sirène extérieure est limitée à 3 minutes par défaut, conformément à l'usage en France.
