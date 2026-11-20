# kisimavr — émuler un ATmega (.hex) dans KiCad / ngspice

Équivalent libre du « charger un .hex » de Proteus : un plug-in `d_cosim`
pour ngspice où **simavr** exécute le firmware cycle par cycle, synchronisé
avec la simulation analogique transitoire. KiCad utilisant ngspice, le
microcontrôleur devient un composant simulable du schéma.

```
.hex ─▶ simavr ─▶ mcu_cosim.so ─▶ d_cosim (XSPICE) ─▶ ngspice ─▶ KiCad
```

Validé avec **KiCad 10.0.8 (Flatpak)**, **ngspice 47**, simavr (master),
Fedora 45, firmwares avr-gcc et **Arduino IDE** (carte Arduino Uno).

## Fonctionnalités

| | |
|---|---|
| Composant `ATMEGA328P` | 20 E/S bidirectionnelles (brochage Arduino Uno), symbole officiel KiCad compatible |
| GPIO | sens choisi par le firmware (DDRx), sortie 25 Ω, pull-up interne 35 kΩ, lecture de la tension réelle |
| Timers, interruptions, PWM | émulés par simavr (`millis()`, `analogWrite()`…) |
| ADC A0–A5 | convertisseur SAR reconstruit dans ngspice : la dichotomie est visible |
| UART0 | texte dans le terminal et un fichier ; trames réelles sur TXD (PD1) |
| Chronogrammes | export VCD → GTKWave, PulseView (décodeurs UART, I²C, SPI…) |

---

## 1. Installation (Fedora)

simavr n'est pas packagé dans Fedora : on le compile depuis ses sources.

```bash
# Outils
sudo dnf install git cmake gcc-c++ make elfutils-libelf-devel avr-gcc avr-libc ngspice

# simavr, installé dans ~/.local (sans sudo)
git clone --depth 1 https://github.com/buserror/simavr.git
make -C simavr/simavr RELEASE=1
make -C simavr/simavr install RELEASE=1 DESTDIR=$HOME/.local

# Ce projet
cd kisimavr
cmake -S . -B build && cmake --build build     # -> build/mcu_cosim.so
make -C firmware                               # -> firmware/*.hex
cmake --build build --target demos             # -> modèles .lib des projets KiCad
```

- Tests de non-régression : `ctest --test-dir build --output-on-failure`
  (firmwares compilés automatiquement ; le test de bout en bout ngspice
  n'est actif que si le dossier de build est `build/`).
- ngspice **≥ 42** est requis (modèle `d_cosim`) : `ngspice -v`.
- Le `.so` mémorise l'emplacement de `libsimavr.so` (RPATH) : pas besoin
  de `LD_LIBRARY_PATH`, y compris depuis KiCad.
- Debian/Ubuntu : `sudo apt install cmake g++ libelf-dev gcc-avr avr-libc ngspice`,
  puis simavr comme ci-dessus (ou `libsimavr-dev` s'il est disponible).
- simavr ailleurs : `cmake -S . -B build -DSIMAVR_INCLUDE_DIRS=<dossier de sim_avr.h> -DSIMAVR_LIBRARIES=<libsimavr.so>`.
- Après un déplacement du dossier : `rm -rf build`, reconfigurer, puis
  relancer la cible `demos` (chemins absolus des projets de démo).

## 2. Premier essai (ngspice seul)

```bash
cd ngspice
ngspice full_test.cir
```

Le terminal doit afficher :
```
[mcu_cosim] atmega328p @ 16000000 Hz running ../firmware/full_demo.hex (0 in, 0 out, 20 io, 6 ADC)
[mcu_cosim] t=… s UART0> ATmega328P full model
[mcu_cosim] t=… s UART0> A0 = …  BTN = 0
```
et les graphiques : LED allumée pendant l'appui sur le bouton (4–12 ms),
PWM filtrée qui suit A0, trames série sur TXD.

## 3. Nouveau projet KiCad

> **Guide pas à pas détaillé (démo, nouveau projet, passage au PCB) :
> [`kicad/projects/README.md`](kicad/projects/README.md).**

### 3.1 Générer le modèle avec votre firmware

```bash
python3 ~/dev/kisimavr/tools/make_atmega328p_lib.py \
    --hex ~/Arduino/mon_projet/build/arduino.avr.uno/mon_projet.ino.hex \
    -o ~/kicad/mon_projet/atmega328p_full.lib
```

Le script écrit tous les chemins **en absolu** (`.hex`, `mcu_cosim.so` trouvé
à côté du script, `uart_log.txt` et `trace.vcd` dans le dossier du projet) et
prévient en cas de fichier introuvable, de majuscules dans le chemin du `.so`
ou de `.hex` avec bootloader.

| Option | Rôle | Défaut |
|---|---|---|
| `--hex` | firmware Intel HEX | obligatoire |
| `-o` | `.lib` à écrire | sortie standard |
| `--so` | plug-in `mcu_cosim.so` | `../build/mcu_cosim.so` (relatif au script) |
| `--uartlog` | fichier texte de l'UART | à côté du `.lib` |
| `--vcd` | trace VCD (`""` = aucune) | à côté du `.lib` |
| `--freq` | horloge CPU en Hz | `16000000` |

### 3.2 Le symbole

Symbole officiel **`MCU_Microchip_ATmega:ATmega328P-P`**, puis
*Propriétés → Modèle de simulation… → Modèle SPICE depuis un fichier* :

| Champ | Valeur |
|---|---|
| `Sim.Device` | `SUBCKT` |
| `Sim.Library` | `atmega328p_full.lib` |
| `Sim.Name` | `ATMEGA328P` |
| `Sim.Pins` | `2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5` |

VCC, GND, AVCC, AREF, RESET et XTAL ne sont pas simulés : on peut les câbler
normalement (ERC) ou les laisser libres.

Plus simple : **dupliquer le projet de démo `kicad/projects/simAVR_atmega328p`**
(déjà câblé, empreintes réelles DIP-28 et THT, avec `led_models.lib`) et
relancer le script avec votre `.hex`.

### 3.3 Simuler

- Analyse **transitoire**, par ex. pas `0.5u`, durée `20m`.
- **Décocher « Sauvegarder tous les courants »** (`.probe alli` est
  incompatible avec les nœuds numériques).
- Sonder les fils ; pour voir le SAR : signal `V(xu1.vdac)`.
- Texte de l'UART : `tail -f ~/kicad/mon_projet/uart_log.txt`.

### 3.4 Changer de firmware

Relancer le script avec le nouveau `--hex` (ou modifier le chemin qui suit
`"hex"` dans le `.lib`). Un nouveau `.hex` ne demande pas de redémarrer KiCad.
En revanche, après **toute recompilation de `mcu_cosim.so`**, il faut fermer
et relancer KiCad, qui garde l'ancienne version en mémoire.

## 4. Firmwares Arduino IDE

1. Carte **Arduino Uno** (ATmega328P, 16 MHz).
2. *Croquis → Exporter les binaires compilés*.
3. Utiliser **`MonCroquis.ino.hex`**, pas `…with_bootloader.hex`.

Correspondance des broches :

| Arduino | AVR | Broche DIP-28 | | Arduino | AVR | Broche DIP-28 |
|---|---|---|---|---|---|---|
| D0 (RX) | PD0 | 2 | | D8 | PB0 | 14 |
| D1 (TX) | PD1 | 3 | | D9 | PB1 | 15 |
| D2 | PD2 | 4 | | D10 | PB2 | 16 |
| D3 | PD3 | 5 | | D11 | PB3 | 17 |
| D4 | PD4 | 6 | | D12 | PB4 | 18 |
| D5 | PD5 | 11 | | D13 (LED) | PB5 | 19 |
| D6 | PD6 | 12 | | A0–A5 | PC0–PC5 | 23–28 |
| D7 | PD7 | 13 | | | | |

Fonctionnent : `pinMode` (y compris `INPUT_PULLUP`), `digitalRead/Write`,
`analogRead` (référence `DEFAULT`), `analogWrite`, `millis`, `delay`,
`Serial.begin/print`.

⚠ `delay()` dure en **temps simulé** : `delay(1000)` = 1 s de simulation,
soit plusieurs minutes de calcul. Préférer des délais de quelques ms pour
les essais en simulation.

## 5. Démos fournies

| Firmware | Modèle (`.lib`) | Test ngspice | Projet KiCad | Montre |
|---|---|---|---|---|
| `full_demo.c` / `full_demo/full_demo.ino` | `atmega328p_full.lib` → `ATMEGA328P` | `full_test.cir` | `simAVR_atmega328p` (DIP-28, empreintes réelles) | bouton + pull-up interne, LED, ADC, PWM, UART |
| `blink.c` | `atmega328p_cosim.lib` → `ATMEGA328P_COSIM` | `blink_test.cir` | `simAVR_demo` | Timer0 + interruption (1 kHz), entrée filtrée RC |
| `adc_pwm.c` | `atmega328p_cosim.lib` → `ATMEGA328P_ADC` | `adc_test.cir` | `simAVR_adc` | SAR visible, PWM filtrée, seuil |
| `uart_adc.c` | `atmega328p_cosim.lib` → `ATMEGA328P_UART` | `uart_test.cir` | `simAVR_uart` | console série, trames TXD, VCD |

Les tests ngspice se lancent depuis `ngspice/` (chemins relatifs).

## 6. Fonctionnement

### 6.1 Synchronisation

`d_cosim` appelle le plug-in avec un temps cible ; simavr exécute les
instructions jusqu'à ce temps. Dès qu'une sortie change, l'exécution
s'arrête et le temps est ramené à l'instant du changement (résolution :
un cycle CPU, 62,5 ns à 16 MHz). Les entrées arrivent de ngspice à leur
instant. Les modèles ADC/UART/complet utilisent `delay=100n` (sorties
retardées de 100 ns) pour laisser à ngspice la marge de placer ses pas.

### 6.2 Broche bidirectionnelle (`io`)

```
          ┌─ adc_bridge (seuils 1,5 V / 3 V) ─────▶ lecture → PINx
broche ───┤
          ├─ interrupteur 25 Ω / 1 GΩ ◀── oe = DDRx     source 0/5 V ◀── level = PORTx
          └─ interrupteur 35 kΩ vers 5 V ◀── pu = (DDRx = 0 et PORTx = 1)
```

Le niveau lu par le firmware vient **toujours du circuit** : l'émulation
interne des pull-ups de simavr est remplacée par le modèle électrique.
Un conflit (sortie à 1 contre un court-circuit à la masse) se voit donc
électriquement.

### 6.3 ADC : convertisseur SAR reconstruit

```
            ┌──────────────── ngspice ─────────────────┐
An ─────────┤ Ecmp = An − vdac → adc_bridge (comp.)    ├──▶ 1 entrée par canal
            │ Bdac : vdac = 5 V · code / 1024          ◀── 10 sorties b0..b9
            └──────────────────────────────────────────┘
                   plug-in : 1 bit par `adcbit` (1 µs), canal après canal
                   → résultat (mV) remis à l'ADC de simavr
```

- 10 µs par conversion et par canal ; le firmware lit la dernière valeur
  (au plus 60 µs d'âge avec 6 canaux).
- Ne tourne que si l'ADC est activé (bit ADEN).
- Référence AVCC = `vref` (5 V). Avec la référence interne 1,1 V la tension
  reste juste, avec la résolution du CNA 5 V (≈ 4,9 mV).

### 6.4 UART0 et chronogrammes

- **Texte** : chaque ligne (`'\n'`) s'affiche, horodatée, sur stderr
  (`[mcu_cosim] t=0.002345 s UART0> …`) et dans le fichier `uartlog`.
- **TXD** : simavr ne pilote pas PD1 ; le plug-in reconstruit les trames
  (start, 5 à 9 bits de poids faible en tête, 1 ou 2 stops) au débit
  programmé dans UBRR0/U2X0. Exemple : `Serial.begin(115200)` donne 117 647
  bauds réels (+2,1 %), comme sur la vraie carte.
- **VCD** (`vcd`) : toutes les broches, résolution 1 ns. PulseView :
  *Fichier → Importer → Value Change Dump* (sous-échantillonnage 10 à 100),
  décodeur **UART**, RX = PD1.
- Si `main()` se termine (`cli; rjmp .`), le CPU s'arrête mais les
  périphériques continuent : la dernière trame part jusqu'au bout.

## 7. Référence `sim_args`

Chaque clé et chaque valeur est **une chaîne séparée** :

```spice
.model mcu d_cosim simulation="/home/moi/kisimavr/build/mcu_cosim.so"
+ sim_args=["hex" "/home/moi/blink.hex" "mcu" "atmega328p" "freq" "16000000"
+           "io" "PD2" "PB5" "adc" "0" "uartlog" "/home/moi/uart_log.txt"]
+ delay=100n
```

| Clé | Valeur(s) | Défaut |
|---|---|---|
| `hex` | firmware Intel HEX | obligatoire |
| `mcu` | nom de cœur simavr (`atmega328p`, `atmega2560`…) | `atmega328p` |
| `freq` | horloge CPU en Hz | `16000000` |
| `io` | broches bidirectionnelles | — |
| `in` | broches en entrée fixe (pilotées par le circuit) | — |
| `out` | broches en sortie fixe | — |
| `adc` | canaux analogiques (`"0" "1"`) | — |
| `vref` | pleine échelle du CNA en volts (= AVCC) | `5` |
| `adcbit` | durée d'un pas SAR en secondes | `1e-6` |
| `txpin` | broche qui porte les trames UART0 | `PD1` |
| `uartlog` | fichier recevant le texte de l'UART0 (`.txt` ou `.log`) | — |
| `vcd` | trace VCD des broches (`.vcd`) | — |
| `log` | `1` : activité des broches et conversions sur stderr | `0` |

Les fichiers `uartlog` et `vcd` ne sont jamais écrits à travers un lien
symbolique, et un fichier existant n'est remplacé que s'il a été écrit par
le plug-in (texte ASCII de l'UART, ou VCD commençant par `$timescale`) :
une faute de frappe dans la netlist ne peut pas écraser un autre fichier.

Ordre des ports de la ligne `aMcu [entrées] [sorties] null modèle` :
- entrées = `in`, puis lecture de chaque `io`, puis un comparateur par `adc` ;
- sorties = `out`, puis (level, oe, pu) par `io`, puis b0..b9 si `adc`.

## 8. Tester sans ngspice : `cosim_runner`

Charge le plug-in comme `d_cosim`, pilote l'entrée 0 par un signal carré
(`-p`), simule le côté analogique d'un canal ADC (`-a vdébut,vfin[,vref]`),
écrit un VCD (`-o`) et mesure les périodes.

```bash
./build/cosim_runner -t 0.02 -p 0.004 -o blink.vcd ./build/mcu_cosim.so -- \
    hex firmware/blink.hex in PD2 out PB5 PB0
./build/cosim_runner -t 0.02 -a 0,5,5 ./build/mcu_cosim.so -- \
    hex firmware/adc_pwm.hex out PD6 PB5 adc 0
./build/cosim_runner -t 0.005 -a 1,4,5 ./build/mcu_cosim.so -- \
    hex firmware/uart_adc.hex out PD1 adc 0 | grep UART0
```

## 9. Dépannage

| Symptôme | Cause | Remède |
|---|---|---|
| `Undefined parameter [...]` | numparam évalue `"clé=valeur"` ou un `.model` placé dans le `.subckt` | une chaîne par clé et par valeur ; `.model d_cosim` hors du `.subckt` |
| `cannot open firmware '...'` | chemin faux, ou relatif au mauvais dossier | chemins absolus (le script les génère) |
| `mismatched ... counts` | les ports de `aMcu` ne correspondent pas aux `sim_args` | voir l'ordre des ports (§ 7) |
| `'.probe alli' and digital nodes are not compatible` | option « Sauvegarder tous les courants » | la décocher |
| Sorties figées à 0 dans KiCad seulement | KiCad utilise encore un ancien `mcu_cosim.so` | fermer **tout** KiCad (`pgrep -a kicad` vide), relancer |
| Le `.so` n'est pas trouvé depuis KiCad | KiCad met la netlist en minuscules | chemin du `.so` tout en minuscules (lien `/opt/kisimavr/mcu_cosim.so` par ex.) ; les autres chemins sont corrigés par le plug-in |
| `kicad: commande introuvable` | KiCad installé en Flatpak | `flatpak run org.kicad.KiCad 2>&1 \| grep --line-buffered mcu_cosim` |
| Quelques `XSPICE time is behind vtime` | ngspice a rejeté un pas après l'avoir proposé | sans effet notable (< 1 µs) ; sinon `delay=200n` ou `"adcbit" "2e-6"` |
| `not writing UART log '…'` / `not writing VCD '…'` | extension inattendue, lien symbolique, ou fichier existant qui n'a pas été écrit par le plug-in | corriger le chemin, ou supprimer l'ancien fichier |
| Simulation très lente | `delay()` longs, ou durée simulée trop grande | réduire les délais du firmware et la durée de la simulation |

Voir les messages du plug-in : lancer ngspice ou KiCad depuis un terminal ;
`"log" "1"` détaille l'activité des broches.

## 10. Limites actuelles

- Pas encore de réception UART (RXD), ni d'I²C/SPI matériels testés.
- ADC : pas d'échantillonneur-bloqueur (Vin comparée pendant les 10 pas),
  pas d'entrée différentielle ni de capteur de température.
- Seule la flash du `.hex` est chargée (pas l'EEPROM) ; un seul bloc
  contigu (sortie `avr-objcopy -O ihex -R .eeprom`, ou `.ino.hex` Arduino).
- RESET, XTAL, alimentation et AREF ne sont pas modélisés.

## Arborescence

| Fichier | Rôle |
|---|---|
| `src/mcu_cosim.cpp` | le plug-in (pont simavr ↔ d_cosim) |
| `include/cosim_abi.h` | copie binaire-compatible de `ngspice/cosim.h` |
| `tools/main.cpp` | `cosim_runner` : test sans ngspice |
| `tests/` | tests `ctest` (plug-in via `cosim_runner`, erreurs de configuration, modèle généré, ngspice) |
| `tools/make_atmega328p_lib.py` | génère le modèle `ATMEGA328P` pour un projet |
| `kicad/atmega328p_full.lib` | modèle complet (chemins relatifs, pour `ngspice/`) |
| `kicad/atmega328p_cosim.lib` | sous-circuits des démos GPIO, ADC, UART |
| `kicad/projects/` | 4 projets KiCad de démo + guide pas à pas (`README.md`) |
| `tools/setup_demos.py` | écrit les `.lib` des projets de démo (chemins de la machine) |
| `tools/gen_kicad_demo.py` | a généré les schémas de ces projets |
| `firmware/` | firmwares de démo (C) et `full_demo/full_demo.ino` (Arduino) |
| `ngspice/` | netlists de test autonomes |

## Sécurité

Simuler un projet revient à exécuter du code : la ligne
`.model … d_cosim simulation="….so"` charge n'importe quelle bibliothèque
partagée, et le firmware choisi tourne dans l'émulateur. C'est le
fonctionnement de `d_cosim` dans ngspice, pas propre à ce plug-in :
**ne simulez pas un projet KiCad ou une netlist reçus d'une source non
fiable** sans avoir lu leurs `.lib`.

## Licence

GPL-3.0-or-later (voir `LICENSE`), comme simavr auquel le plug-in est lié.
`include/cosim_abi.h` reprend l'interface de ngspice (licence BSD modifiée).

## Crédits

- [simavr](https://github.com/buserror/simavr) (GPL-3.0) — émulateur AVR.
- [ngspice](https://ngspice.sourceforge.io/) — modèle XSPICE `d_cosim`
  de Giles Atkinson ; `include/cosim_abi.h` reprend son interface
  (licence BSD modifiée).
