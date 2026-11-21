# kisimavr — émuler un ATmega ou une Raspberry Pi Pico dans KiCad / ngspice

Équivalent libre du « charger un .hex » de Proteus : un plug-in `d_cosim`
pour ngspice où le firmware s'exécute, synchronisé avec la simulation
analogique transitoire. KiCad utilisant ngspice, le microcontrôleur devient
un composant simulable du schéma :

- **ATmega** (Arduino UNO) : **simavr** exécute le `.hex` cycle par cycle ;
- **Raspberry Pi Pico** (RP2040) et **Pico 2** (RP2350, cœurs RISC-V ou
  Arm) : **rp2040js** / **rp2350js** exécutés par QuickJS dans le plug-in,
  firmwares `.uf2` du Pico SDK ou d'Arduino-pico ;
- **modules** du schéma co-simulés eux aussi : afficheur LCD, modules I²C
  (LM75, EEPROM, LCD I²C), capteur à ultrasons HC-SR04, servomoteur.

```
.hex / .uf2 ─▶ simavr / rp2040js / rp2350js ─▶ mcu_cosim.so ─▶ d_cosim (XSPICE) ─▶ ngspice ─▶ KiCad
```

Validé avec **KiCad 10.0.8 (Flatpak)**, **ngspice 47**, simavr (master),
Fedora 45, firmwares avr-gcc et **Arduino IDE** (carte Arduino Uno), clang
pour les démos Pico bare-metal.

## Fonctionnalités

| | |
|---|---|
| Composant `ATMEGA328P` | 20 E/S bidirectionnelles (brochage Arduino Uno), symbole officiel KiCad compatible |
| GPIO | sens choisi par le firmware (DDRx), sortie 25 Ω, pull-up interne 35 kΩ, lecture de la tension réelle |
| Timers, interruptions, PWM | émulés par simavr (`millis()`, `analogWrite()`…) |
| ADC A0–A5 | convertisseur SAR reconstruit dans ngspice : la dichotomie est visible |
| UART0 | émission : texte dans le terminal et un fichier, trames réelles sur TXD (PD1) ; réception : fichier de commandes horodaté ou trames venant du circuit sur RXD (PD0) ; port série virtuel pour picocom, CuteCom, PuTTY… (§ 6.7) |
| I²C (TWI) | esclaves virtuels LM75 et EEPROM 24C02, journal des transactions, signaux SDA/SCL réels en drain ouvert |
| SPI | maître sur les vraies broches (SCK, MOSI, MISO), 4 modes, ordre des bits, débit de SPCR ; esclaves = composants du schéma (modèle 74HC595 fourni) ; journal des octets |
| Afficheur LCD | module 16x2 / 20x4 (HD44780) comme composant du schéma : écran affiché dans le terminal et un fichier, vérification des temps du datasheet |
| Raspberry Pi Pico (RP2040) | composant `PICO` 3,3 V : rp2040js (Wokwi) exécuté par QuickJS dans le plug-in ; GPIO, PWM, UART (texte + trames), I²C et SPI maîtres (signaux réels), USB série (texte), ADC GP26–GP28 ; firmwares `.uf2` (Pico SDK, Arduino-pico), `.bin`, `.hex` |
| Modules I²C | composant `I2C_…` câblé sur SDA / SCL : capteur LM75, EEPROM 24C02, afficheur LCD 16x2 I²C (PCF8574), décodés bit à bit depuis les tensions, ACK et données en drain ouvert ; indépendants du maître (validés avec la Pico) |
| Raspberry Pi Pico 2 (RP2350) | composant `PICO2`, même brochage : rp2350js dans QuickJS, cœurs RISC-V (Hazard3) ou Arm (Cortex-M33) selon le firmware, démarrage par la bootrom A2 ; mêmes GPIO, UART, ADC |
| Capteurs et actionneurs | HC-SR04 et servomoteur co-simulés (temps exacts, messages dans le terminal), CTN 10 kΩ, moteur à courant continu et pont en H L298N en SPICE ; démos ATmega328P et Pico |
| Chronogrammes | export VCD → GTKWave, PulseView (décodeurs UART, I²C, SPI…) |

---

## 1. Installation (Fedora)

simavr n'est pas packagé dans Fedora : on le compile depuis ses sources.

```bash
# Outils
sudo dnf install git cmake gcc-c++ make elfutils-libelf-devel avr-gcc avr-libc ngspice \
                 clang lld llvm

# simavr, installé dans ~/.local (sans sudo)
git clone --depth 1 https://github.com/buserror/simavr.git
make -C simavr/simavr RELEASE=1
make -C simavr/simavr install RELEASE=1 DESTDIR=$HOME/.local

# Ce projet
cd kisimavr
cmake -S . -B build && cmake --build build     # -> build/mcu_cosim.so
make -C firmware                               # -> firmware/*.hex
make -C firmware/pico                          # -> firmware/pico/*.bin (clang + lld)
make -C firmware/pico2                         # -> firmware/pico2/*.bin (RISC-V et Arm)
cmake --build build --target demos             # -> modèles .lib des projets KiCad
```

- **RP2040** : CMake télécharge QuickJS-ng (version figée) à la
  configuration. Hors ligne : `-DKISIMAVR_RP2040=OFF`. Les démos Pico
  bare-metal se compilent avec `sudo dnf install clang lld llvm`.

- Tests de non-régression : `ctest --test-dir build --output-on-failure`
  (firmwares compilés automatiquement ; le test de bout en bout ngspice
  n'est actif que si le dossier de build est `build/`).
- ngspice **≥ 44** est requis (`d_cosim` avec `sim_args`) : `ngspice -v`.
  ngspice 42 et 43 ne transmettent pas `sim_args` au plug-in (erreur
  `missing firmware`) : c'est le cas d'Ubuntu 24.04, compiler alors ngspice
  depuis ses sources ou utiliser une distribution plus récente.
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
| `--uartin` | fichier de commandes envoyées au récepteur de l'UART | — |
| `--name` | suffixe des noms (`B` → sous-circuit `ATMEGA328P_B`) : plusieurs MCU dans un schéma | — |
| `--vcd` | trace VCD (`""` = aucune) | à côté du `.lib` |
| `--freq` | horloge CPU en Hz | `16000000` |
| `--i2c` | esclaves I²C virtuels (`""` = aucun) | `"lm75 eeprom"` |
| `--lm75temp`, `--lm75rate` | température du LM75 et sa pente (°C, °C/s) | `25`, `0` |

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
| `i2c_lm75.c` / `i2c_lm75/i2c_lm75.ino` | `atmega328p_full.lib` → `ATMEGA328P` | `i2c_test.cir` | `simAVR_i2c` | bus I²C : capteur LM75, EEPROM, pull-ups |
| `uart_cmd.c` / `uart_cmd/uart_cmd.ino` | `--uartin` ou RXD câblé | `uart_rx_test.cir` (2 MCU) | — | réception série : commandes `LED ON` / `LED OFF` |
| `spi_595.c` / `spi_595/spi_595.ino` | `atmega328p_full.lib` → `ATMEGA328P` | `spi_test.cir` | `simAVR_spi` (symboles officiels) | SPI maître : 74HC595, chenillard 8 LED, relecture par MISO |
| `lcd_demo.c` / `lcd_demo/lcd_demo.ino` | `atmega328p_full.lib` + `lcd1602.lib` → `LCD1602` | `lcd_test.cir` | `simAVR_lcd` (symboles officiels) | afficheur 16x2 en 4 bits, tension de A0 |
| `pico/pico_demo.c` (RP2040, clang) | `rp2040.lib` → `RP2040` (`--chip`) | — | `RaspberryPi_Pico` (carte complète, schéma officiel, feuille hiérarchique) | VSYS / 3 sur ADC3, détection VBUS, LED de la carte |
| `pico/pico_demo.c` (RP2040, clang) | `pico.lib` → `PICO` | `pico_test.cir` | `simPICO` (symbole officiel `RaspberryPi_Pico`) | LED, bouton, ADC, UART avec terminal simulé |
| `pico2/pico2_demo.c` (RP2350, clang, RISC-V et Arm) | `pico2.lib` → `PICO2` | `pico2_test.cir` | `simPICO2` | le montage de `simPICO` avec une Pico 2 |
| `pico2/pico2_demo.c` | `rp2350.lib` → `RP2350` (`--mcu rp2350 --chip`) | — | `RaspberryPi_Pico2` (carte reconstruite, feuille hiérarchique) | VSYS / 3, détection VBUS, LED de la carte |
| `uno_demo.c` / `uno_demo/uno_demo.ino` | `atmega328p_full.lib` → `ATMEGA328P` | netlist exportée par `kicad-cli` | `Arduino_UNO` (schéma officiel UNO R3, feuille hiérarchique) | blink + UART RX/TX avec un terminal simulé (`terminal.txt`) |
| `pico/pico_i2c.c` (RP2040) | `pico_i2c.lib` → `PICO_I2C` + `i2c_slaves.lib` → `I2C_SLAVES` | `pico_i2c_test.cir` | `simPICO_i2c` | bus I²C électrique : capteur LM75 et afficheur LCD 16x2 I²C (PCF8574) |
| `pico/pico_spi.c` (RP2040) | `pico_spi.lib` → `PICO_SPI` + `74hc595.lib` | `pico_spi_test.cir` | `simPICO_spi` | bus SPI électrique : 74HC595 en 3,3 V, chenillard, relecture par MISO |
| `radar.c` / `pico/pico_radar.c` | `atmega328p_radar.lib` / `pico_radar.lib` + `hcsr04.lib`, `servo.lib` | `radar_test.cir`, `pico_radar_test.cir` | `simAVR_radar`, `simPICO_radar` | HC-SR04 et servomoteur qui suit la distance |
| `ventilo.c` / `pico/pico_ventilo.c` | `atmega328p_ventilo.lib` / `pico_ventilo.lib` + `ntc.lib`, `motor.lib` | `ventilo_test.cir`, `pico_ventilo_test.cir` | `simAVR_ventilo`, `simPICO_ventilo` | ventilateur régulé : CTN, pont en H L298N, moteur à courant continu |

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

### 6.4 I²C (TWI)

simavr émule le TWI de l'ATmega par **messages** (START + adresse, octet,
ACK, STOP), sans piloter les broches. Le plug-in :

- joue les **esclaves virtuels** déclarés par `"i2c"` :
  - **LM75** (défaut 0x48) : registre 0 = température sur 9 bits (0,5 °C),
    1 = configuration, 2 = Thyst (75 °C), 3 = Tos (80 °C) ; la température
    vaut `lm75temp` + `lm75rate` × t, bornée à −55…+125 °C ;
  - **EEPROM 24C02** (défaut 0x50) : 256 octets, effacée à 0xFF, premier
    octet écrit = adresse, puis écriture ou lecture séquentielle ;
  - toute autre adresse répond **NACK**, comme sur un vrai bus ;
- affiche chaque transaction :
  `[mcu_cosim] t=0.000001 s I2C> 0x48 W 00 | 0x48 R 19 80`
  (`|` = START répété, `(NACK)` = pas de réponse) ;
- reconstruit **SDA (PC4) et SCL (PC5)** au débit programmé dans TWBR
  (100 kHz pour `Wire`), en **drain ouvert** : la broche tire à 0 ou se
  relâche. Il faut donc des **résistances de tirage** dans le schéma
  (4,7 kΩ vers 5 V), ou les pull-ups internes que `Wire.begin()` active.
  SDA ne change que lorsque SCL est bas : les signaux se décodent dans
  PulseView (décodeur I²C) ou à l'oscilloscope du simulateur.

Le modèle `ATMEGA328P` déclare par défaut un LM75 et une EEPROM (options
`--i2c`, `--lm75temp`, `--lm75rate` du script) ; ils n'ont aucun effet sur
un firmware qui n'utilise pas l'I²C.

### 6.4 bis SPI

simavr ne donne l'octet envoyé qu'à la fin du transfert. Le plug-in suit
l'écriture dans SPDR et **reconstruit la trame sur les broches** :

- **SCK = PB5 (D13), MOSI = PB3 (D11)** suivent le SPI dès que SPE et MSTR
  sont à 1 et que la broche est en sortie (DDRB), comme sur la puce. Les
  4 modes (CPOL, CPHA), l'ordre des bits (DORD) et le débit de SPCR/SPSR
  (fosc/2 à fosc/128) sont respectés ;
- **MISO = PB4 (D12)** est forcée en entrée ; sa tension est lue dans le
  circuit à chaque front d'échantillonnage, et l'octet reçu est rendu à
  simavr à la fin du transfert (`SPI.transfer()` le renvoie) ;
- la sélection d'esclave (SS) est une GPIO ordinaire du firmware (D10) ;
- chaque rafale d'octets s'affiche :
  `[mcu_cosim] t=0.002 s SPI> MOSI 04 | MISO 02`
  (`"spilog" "0"` pour le masquer).

Les esclaves sont de vrais composants du schéma. `kicad/74hc595.lib` décrit
un **74HC595** en XSPICE numérique (registre à décalage, verrou, ~OE,
~SRCLR, QH'), utilisable avec le symbole officiel `74xx:74HC595`. Exemple :
`ngspice/spi_test.cir` et le projet `simAVR_spi`.

⚠ Débit : l'esclave doit répondre en moins d'une demi-période de SCK
(125 ns à fosc/4, la valeur par défaut de `SPI.begin()`). Pour les esclaves
lents, choisir fosc/16 (1 MHz) ou moins. Le mode esclave de l'AVR (MSTR = 0)
n'est pas émulé.

### 6.4 ter Afficheur LCD (HD44780)

L'afficheur est un **deuxième composant co-simulé** : le même plug-in,
chargé avec `"device" "hd44780"`, lit RS, R/W, E et D0..D7 dans le circuit
et exécute les instructions du contrôleur à chaque front descendant de E
(interfaces 8 et 4 bits, séquence d'initialisation de `LiquidCrystal`,
effacement, curseur, décalage, CGRAM). Il n'a pas de sortie.

- **Écran** : quand il ne change plus pendant 1 ms, il s'affiche :
  `[mcu_cosim] t=0.0175 s LCD> [Bonjour STI2D   ] [A0 = 2.50 V     ]`,
  et s'ajoute, encadré, dans `lcd_log.txt`. Caractères de la ROM A00
  (`°` = code 223, `¥`, flèches) ; caractères CGRAM affichés `#`.
- **Datasheet** (avertissement `warning`, une fois par type) : impulsion E
  < 230 ns, données changées < 80 ns avant la chute de E, instruction
  envoyée pendant que l'afficheur est occupé (37 µs ; 1,52 ms pour clear et
  home), première instruction < 15 ms après la mise sous tension, lecture
  (R/W = 1, non modélisée : relier R/W à la masse).
- **Modèle** : `tools/make_lcd_lib.py -o lcd1602.lib` (options `--cols`,
  `--rows` pour un 20x4, `--log`) ; sous-circuit `LCD1602`, broches dans
  l'ordre du module, compatible avec le symbole officiel
  `Display_Character:WC1602A`. Côté électrique : seuils CMOS, pull-ups
  internes, consommation, LED de rétroéclairage.
- **Rang `d_cosim`** : l'afficheur prend le rang 10 (`--irreversible`) ;
  chaque composant co-simulé d'une netlist doit avoir le sien.

Exemple : `ngspice/lcd_test.cir` et le projet `simAVR_lcd`.

### 6.4 quater Raspberry Pi Pico (RP2040)

Avec `"mcu" "rp2040"`, l'instance est un **Raspberry Pi Pico**. Le RP2040
est émulé par [rp2040js](https://github.com/wokwi/rp2040js) (Wokwi, MIT),
écrit en JavaScript. Le plug-in l'exécute lui-même avec un moteur
**QuickJS** intégré : pas de Node.js, ce qui fonctionne aussi avec KiCad en
Flatpak. Le temps de rp2040js n'avance qu'au rythme de ngspice.

- **Firmware** : `.uf2` (Pico SDK, ou Arduino-pico : *Croquis → Exporter
  les binaires compilés*), `.bin` (image de la flash) ou `.hex`. Le
  démarrage passe par boot2 comme sur la carte ; la bootrom B1 est fournie.
- **GPIO** (3,3 V) : sortie 50 Ω, pull-up et pull-down 50 kΩ, lecture de la
  tension réelle (seuils 0,8 / 2,0 V). PWM (`analogWrite`) incluse.
- **UART0 / UART1** : texte dans le terminal (`UART0> …`) et `uart_log.txt`,
  trames reconstruites sur la broche TX (GP0 par défaut), trames du
  circuit décodées sur RX (GP1). `Serial` d'Arduino-pico passe par l'USB :
  son texte s'affiche (`USB> …`), sans trames ; `Serial1` = UART0.
- **ADC** : GP26–GP29 (ADC0–3), convertisseur SAR 12 bits reconstruit dans
  ngspice comme pour l'AVR, Vref = 3,3 V. Une étape par microseconde pour
  laisser ngspice calculer le comparateur : une conversion dure 12 µs (2 µs
  sur la puce), et le pas de simulation maximal doit rester ≤ 0,5 µs
  (`.tran 0.5u …`).
- **LED de la carte** : GP25, dans le modèle (470 Ω + LED verte).
- **Puce seule** : `make_pico_lib.py --chip` donne le sous-circuit `RP2040`
  (GPIO0–GPIO29, symbole officiel `MCU_RaspberryPi:RP2040`), utilisé par le
  projet `RaspberryPi_Pico` qui redessine la carte entière.
- **Modèle** : `tools/make_pico_lib.py --fw prog.uf2 -o pico.lib` ;
  sous-circuit `PICO`, compatible avec le symbole officiel
  `MCU_Module:RaspberryPi_Pico` (broches 1–40 de la carte ; masses
  non simulées, comme pour l'ATmega).
- **Vitesse** : QuickJS interprète le JavaScript, soit environ 1 s de calcul
  pour 10 ms simulées quand le programme tourne en boucle active, beaucoup
  moins quand il attend (`sleep_ms`, `delay` en WFI).

Exemple : `firmware/pico/pico_demo.c` (bare-metal, sans SDK),
`ngspice/pico_test.cir` et le projet `simPICO`.

- **I²C** (maître) : sur les broches de fonction I²C de la liste `io`
  (SDA = GP pair, SCL = GP impair, ex. GP4 / GP5), en drain ouvert, au
  débit réglé par le firmware (`i2c_init(i2c0, 100000)`, `Wire`). START,
  adresse, ACK, données et STOP sont de vrais fronts ; les ACK et les bits
  lus sont les tensions du circuit, et l'étirement d'horloge est respecté.
  Il faut les pull-ups dans le schéma (4,7 kΩ).
- **SPI** (maître) : SCK, TX (MOSI) et CSn pilotés au débit, au mode
  (CPOL / CPHA) et à la taille de trame du firmware ; RX (MISO) lu dans le
  circuit. Esclaves = composants du schéma (74HC595 fourni).
- **Modules I²C** : `tools/make_i2c_lib.py --lm75 0x48 --lcd 0x27 -o i2c.lib`
  écrit un sous-circuit à 4 broches (GND VCC SDA SCL) ; chaque module décode
  le bus et répond comme le vrai (`--eeprom` pour une 24C02, `--temp` /
  `--rate` pour le LM75, `--log` pour le journal `I2C> 0x48 R 19 80`).
  L'écran du LCD s'affiche dans le terminal (`LCD> [...]`) et `lcd_log.txt`.

Exemples : `firmware/pico/pico_i2c.c` et `pico_spi.c`,
`ngspice/pico_i2c_test.cir`, `ngspice/pico_spi_test.cir`, projets
`simPICO_i2c` et `simPICO_spi`.

### 6.4 quinquies Raspberry Pi Pico 2 (RP2350)

Avec `"mcu" "rp2350"`, l'instance est une **Raspberry Pi Pico 2**. Le RP2350
est émulé par [rp2350js](https://github.com/c1570/rp2350js) (c1570, MIT,
dérivé de rp2040js), lui aussi exécuté par QuickJS. Tout ce qui précède
(GPIO, UART, USB série, ADC, LED sur GP25) vaut aussi pour la Pico 2.

- **Démarrage** : la bootrom A2 du RP2350 cherche le bloc `IMAGE_DEF` du
  firmware, comme sur la puce. Un firmware RP2040 n'en a pas : le plug-in le
  signale (`RP2350: no IMAGE_DEF block…`).
- **Cœurs** : RISC-V (Hazard3) ou Arm (Cortex-M33), d'après la famille du
  `.uf2`, sinon le bloc `IMAGE_DEF`, sinon la clé `"arch" "riscv"|"arm"`
  (`make_pico_lib.py --arch`). L'émulation RISC-V est la plus aboutie ;
  l'émulation Arm de rp2350js est « basique » (elle fait tourner MicroPython
  et la démo, mais un firmware peut buter sur une instruction non émulée).
- **Modèle** : `tools/make_pico_lib.py --mcu rp2350 --fw prog.uf2 -o pico2.lib`
  (sous-circuit `PICO2`, broches de la Pico) ; `--chip` : puce seule `RP2350`
  (GPIO0–GPIO29 du boîtier QFN-60, symbole officiel `MCU_RaspberryPi:RP2350A`).
- **Vitesse** : environ 1 s de calcul par milliseconde simulée en boucle
  active (150 MHz, deux cœurs) ; les cœurs endormis (`wfi`, `sleep_ms`)
  sautent directement au prochain événement.

Exemple : `firmware/pico2/pico2_demo.c` (bare-metal, compilé pour les deux
architectures), `ngspice/pico2_test.cir` et les projets `simPICO2` et
`RaspberryPi_Pico2`.

### 6.4 sexies Capteurs et actionneurs

- **HC-SR04** et **servomoteur** : `tools/make_module_lib.py hcsr04 --dist 40
  --rate -100` (sous-circuit `HCSR04`, broches VCC TRIG ECHO GND) et
  `make_module_lib.py servo` (`SERVO`, broches SIG VCC GND ANGLE, ANGLE en
  volts = degrés). Le plug-in mesure TRIG et la largeur des impulsions
  exactement ; le terminal affiche `HCSR04> 40.0 cm (echo 2332 us)` et
  `SERVO> 126 deg (pulse 1843 us)`.
- **CTN** : `kicad/ntc.lib`, sous-circuit `NTC10K` (10 kΩ à 25 °C, B = 3950),
  température `T0` qui varie de `RATE` °C/s (`Sim.Params = T0=20 RATE=100`).
- **Moteur et pont en H** : `kicad/motor.lib`, `MOTOR_DC` (R, L, f.c.é.m.,
  inertie ; sortie SPEED, 1 V = 1000 tr/min) et `L298N` (brochage du symbole
  officiel `Driver_Motor:L298N`, entrées 3,3 / 5 V, chutes de tension,
  diodes de roue libre).

Exemples : `firmware/radar.c`, `firmware/ventilo.c` (ATmega328P),
`firmware/pico/pico_radar.c`, `pico_ventilo.c` ; `ngspice/radar_test.cir`,
`ventilo_test.cir`, `pico_radar_test.cir`, `pico_ventilo_test.cir` ; projets
`simAVR_radar`, `simAVR_ventilo`, `simPICO_radar`, `simPICO_ventilo`.

### 6.5 UART0 et chronogrammes

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

### 6.6 UART0 : réception (`Serial.read`)

Deux sources, utilisables ensemble :

- **Fichier de commandes** (`"uartin"`, option `--uartin`) : une ligne par
  envoi, un instant puis le texte :
  ```
  # Commandes pour firmware/uart_cmd.hex
  1m    LED ON\n
  2.5m  Bonjour\n
  4m    LED OFF\n
  ```
  Instants en secondes avec les suffixes SPICE (`1m`, `250u`, `2.5e-3`).
  Échappements : `\n`, `\r`, `\t`, `\\`, `\s` (espace), `\xHH`. Les octets
  partent au débit programmé (UBRR0), un temps de trame chacun. Une erreur
  est signalée avec son numéro de ligne.
- **Trames venant du circuit** sur **RXD (PD0)** quand c'est une broche `in`
  ou `io` (le modèle `ATMEGA328P` l'a) : le plug-in les décode au débit
  programmé, échantillon au milieu de chaque bit. On peut ainsi relier le
  TXD d'un ATmega au RXD d'un autre (voir `ngspice/uart_rx_test.cir` et
  l'option `--name` pour mettre deux modèles dans le même schéma), ou
  envoyer des trames depuis une source PWL.

Le texte reçu s'affiche comme `[mcu_cosim] t=0.001000 s UART0< LED ON`.
Tant que le firmware n'a pas activé le récepteur (RXEN = 0, avant
`Serial.begin`), simavr ignore les octets : ils sont signalés
(`(lost: receiver off, RXEN = 0)`) plutôt que perdus en silence. Quand RXEN
est à 1, RXD est une entrée (le pull-up suit PORTD0, comme sur la puce).

⚠ simavr met chaque octet reçu à disposition un temps de trame après sa
réception : une réponse arrive donc environ 85 µs (115200 bauds) plus tard
que sur la vraie puce.

Comme sur une vraie puce, un récepteur activé **au milieu d'une trame**
(ici : un MCU dont le programme démarre plus lentement que l'émetteur) se
cale sur un mauvais front ; si les octets se suivent sans silence, il reste
désynchronisé (`framing error`, texte brouillé) jusqu'à la première pause,
puis reçoit correctement. C'est ce que montre la première ligne de
`ngspice/uart_rx_test.cir`.

**Plusieurs MCU dans une netlist** : chaque modèle doit avoir son propre
nom (`--name`) et son propre rang `d_cosim` (`--irreversible 1`, `2`…),
sinon ngspice plante. ngspice fait avancer les MCU l'un après l'autre :
il signale alors souvent `XSPICE time is behind vtime` pour le second,
sans conséquence sur les résultats.

### 6.7 Terminal série externe (picocom, CuteCom, PuTTY…)

L'UART0 de l'ATmega, l'UART0 de la Pico / Pico 2 et l'USB série de la
Pico (`Serial` d'Arduino-pico) peuvent sortir sur un **vrai port série
virtuel** : on l'ouvre avec un logiciel de terminal comme un adaptateur
USB-série. Ce que le firmware envoie s'y affiche au fil de la simulation,
ce qu'on y tape arrive sur son RX (`Serial.read()`), au débit programmé.

```bash
# Modèle avec le port série virtuel (lien ~/kisimavr_uart0 par défaut)
python3 tools/make_atmega328p_lib.py --hex prog.hex --pty -o atmega328p_full.lib
python3 tools/make_pico_lib.py --fw prog.uf2 --pty -o pico.lib     # UART0
python3 tools/make_pico_lib.py --fw prog.uf2 --ptyusb -o pico.lib  # Serial (USB)
```

Pendant la simulation (le plug-in affiche
`UART0 on ~/kisimavr_uart0 -> /dev/pts/5`) :

```bash
picocom -b 115200 ~/kisimavr_uart0      # ou CuteCom, GTKTerm, screen, pyserial
```

- **KiCad en Flatpak** : ses pseudo-terminaux ne sont pas visibles depuis
  le système. Le plug-in le détecte et ouvre à la place un **serveur TCP
  local** : `127.0.0.1:5555` (UART0) ou `5556` (USB), message
  `KiCad runs in Flatpak, pseudo-terminal replaced by tcp:5555`. On s'y
  connecte avec `nc localhost 5555`, PuTTY (type *Raw*), ou, pour CuteCom
  et picocom, par un pont :
  `socat pty,link=$HOME/kisimavr_uart0,raw,echo=0 tcp:localhost:5555`.
  On peut aussi le demander directement : `--pty tcp:5555`. Les 4 derniers
  ko envoyés avant la connexion sont transmis à la connexion.
- La simulation ne tourne pas en temps réel (souvent plus lentement) : un
  caractère tapé arrive au temps simulé du moment. Prévoir une durée de
  simulation assez longue pour avoir le temps de taper.
- Le moniteur série de l'IDE Arduino ne liste que les vrais ports série.
- Les projets `Arduino_UNO`, `simPICO` et `simPICO2` ont le port activé.
- L'USB série (`--ptyusb`) suppose la pile USB du firmware (TinyUSB
  d'Arduino-pico ou du Pico SDK) ; il n'est pas couvert par les tests
  automatiques (les démos sont bare-metal, sans USB).

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
| `uartin` | fichier de texte horodaté envoyé au récepteur (§ 6.6) | — |
| `rxpin` | broche dont les trames sont décodées par le récepteur | `PD0` |
| `pty` | UART0 sur un port série virtuel (§ 6.7) : `"1"`, chemin d'un lien (`"~/kisimavr_uart0"`) ou `"tcp:5555"` | — |
| `vcd` | trace VCD des broches (`.vcd`) | — |
| `i2c` | esclaves virtuels : `"lm75" ["0x48"] "eeprom" ["0x50"]` | — |
| `lm75temp` | température du LM75 à t = 0 (°C) | `25` |
| `lm75rate` | pente de la température (°C/s) | `0` |
| `i2clog` | `0` : ne pas afficher les transactions I²C | `1` |
| `sda` / `scl` | broches du bus | `PC4` / `PC5` |
| `spilog` | `0` : ne pas afficher les octets SPI | `1` |
| `log` | `1` : activité des broches et conversions sur stderr | `0` |

Les fichiers `uartlog` et `vcd` ne sont jamais écrits à travers un lien
symbolique, et un fichier existant n'est remplacé que s'il a été écrit par
le plug-in (texte ASCII de l'UART, ou VCD commençant par `$timescale`) :
une faute de frappe dans la netlist ne peut pas écraser un autre fichier.

Autres composants co-simulés (`"device"` à la place de `"hex"`), décrits
dans l'en-tête de leur source et générés par les scripts de `tools/` :

| `sim_args` | Composant | Ports (entrées / sorties) | Script |
|---|---|---|---|
| `"mcu" "rp2040"` / `"mcu" "rp2350"` + `"fw"` (+ `"pty"`, `"ptyusb"`) | Pico / Pico 2 (`src/rp2040_config.h`) | lectures `io` + comparateurs ADC / 4 sorties par `io` + 12 bits CNA | `make_pico_lib.py` |
| `"device" "hd44780"` | afficheur LCD (`src/lcd_device.h`) | RS RW E D0..D7 / — | `make_lcd_lib.py` |
| `"device" "i2c"` | modules I²C (`src/i2c_slave_device.h`) | SDA SCL / SDA tiré à 0 | `make_i2c_lib.py` |
| `"device" "hcsr04"` | HC-SR04 (`src/sensor_devices.h`) | TRIG / ECHO | `make_module_lib.py hcsr04` |
| `"device" "servo"` | servomoteur (`src/sensor_devices.h`) | SIG / angle sur 8 bits | `make_module_lib.py servo` |

Chaque composant co-simulé d'un même circuit a son propre rang
`irreversible` (les scripts en proposent un par défaut, option
`--irreversible`).

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
| `missing firmware` alors que `"hex"` est bien dans `sim_args` | ngspice 42 ou 43 | ngspice ≥ 44 (`ngspice -v`) |
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

- RP2040 : I²C et SPI en maître seulement (pas de mode esclave) ; PIO à la
  cadence approximative de rp2040js ;
  octets UART envoyés sans attente de FIFO (les trames suivent à la
  vitesse de la ligne) ; un seul cœur suivi en temps (le second tourne).
- RP2350 : mêmes limites que le RP2040 ; émulation Arm (Cortex-M33) encore
  partielle dans rp2350js (le RISC-V est plus complet) ; horloges fixes
  (clk_sys = clk_peri = 150 MHz), sans PLL ni sécurité (TrustZone, OTP).
- SPI : AVR maître uniquement, sur PB5/PB3/PB4 (famille ATmega48/88/168/328).
- I²C de l'ATmega : maître uniquement, avec les esclaves virtuels LM75 et
  24C02 (clé `i2c`) ; les modules I²C du schéma (`"device" "i2c"`) sont
  validés avec la Pico ; étirement d'horloge non modélisé côté ATmega.
- Modules : HC-SR04 sans écho multiple ni bruit ; servomoteur sans couple
  ni surconsommation au démarrage ; CTN sans auto-échauffement ; moteur et
  L298N comportementaux (pas de modèle thermique).
- ADC : pas d'échantillonneur-bloqueur (Vin comparée pendant les 10 pas),
  pas d'entrée différentielle ni de capteur de température.
- Seule la flash du `.hex` est chargée (pas l'EEPROM) ; un seul bloc
  contigu (sortie `avr-objcopy -O ihex -R .eeprom`, ou `.ino.hex` Arduino).
- RESET, XTAL, alimentation et AREF ne sont pas modélisés.
- d_cosim (ngspice 44 à 47 au moins) a deux défauts que le plug-in
  contourne : sa copie des sorties n'est pas initialisée (le plug-in
  annonce toutes les sorties à 0 au premier pas) et deux envois dans le
  même appel perdent le premier lot (un changement qui y exposerait est
  différé à l'appel suivant, soit quelques centaines de ns au plus).

## Arborescence

| Fichier | Rôle |
|---|---|
| `src/mcu_cosim.cpp` | point d'entrée du plug-in (`Cosim_setup`) et référence des clés |
| `src/mcu_bridge.*` | pont simavr ↔ d_cosim : temps, GPIO, ports |
| `src/sar_adc.*`, `src/uart0.*` | convertisseur SAR, UART0 (console, log, trames TXD, réception) |
| `src/uart_script.*` | lecture du fichier `uartin` (sans simavr, testée unitairement) |
| `src/spi_bus.*`, `src/spi_frame.*` | SPI maître : trames sur SCK/MOSI, lecture de MISO, journal ; chronologie des fronts (testée unitairement) |
| `src/lcd_device.*`, `src/hd44780.*` | afficheur LCD co-simulé : entrées RS/E/D, écran, journal ; contrôleur HD44780 (testé unitairement) |
| `tools/make_lcd_lib.py` | génère le modèle `LCD1602` (ou 20x4) pour un projet |
| `src/rp2040_device.*`, `src/rp2040_config.*`, `rp2040/bridge.js` | RP2040 : QuickJS, pont JavaScript vers rp2040js (broches, UART, ADC) ; `sim_args` (testé unitairement) |
| `third_party/rp2040js/` | rp2040js compilé en JavaScript (MIT) + bootrom B1 ; `tools/vendor_rp2040js.sh` le reconstruit |
| `rp2040/rp2350_bridge.js`, `rp2040/pico_common.js` | RP2350 : pont vers rp2350js ; code commun aux deux ponts (broches, UART, USB, ADC) |
| `third_party/rp2350js/` | rp2350js compilé en JavaScript (MIT) + bootroms ; `tools/vendor_rp2350js.sh` le reconstruit |
| `tools/make_pico_lib.py` | génère le modèle `PICO` / `PICO2` (ou la puce `RP2040` / `RP2350`) pour un projet |
| `src/sensor_devices.*`, `tools/make_module_lib.py` | HC-SR04 et servomoteur co-simulés (testés unitairement) ; leurs modèles SPICE |
| `kicad/ntc.lib`, `kicad/motor.lib` | CTN, moteur à courant continu, L298N (SPICE comportemental) |
| `src/i2c_slave_device.*`, `tools/make_i2c_lib.py` | modules I²C du circuit (LM75, 24C02, LCD PCF8574) décodés bit à bit ; leur modèle SPICE |
| `kicad/74hc595.lib` | modèle XSPICE du 74HC595 (symbole officiel `74xx:74HC595`) |
| `src/i2c_bus.*`, `src/i2c_devices.*` | TWI : esclaves virtuels, journal, signaux SDA/SCL ; logique LM75 / 24C02 (testée unitairement) |
| `src/pty_port.*`, `tests/pty_test.py` | port série virtuel (pseudo-terminal ou TCP) ; test qui joue le rôle d'un logiciel de terminal |
| `src/config.*`, `src/paths.*`, `src/vcd.*` | `sim_args`, chemins et fichiers de sortie, trace VCD (sans simavr, testés unitairement) |
| `include/cosim_abi.h` | copie binaire-compatible de `ngspice/cosim.h` |
| `tools/main.cpp` | `cosim_runner` : test sans ngspice |
| `tests/` | tests `ctest` (plug-in via `cosim_runner`, erreurs de configuration, modèle généré, ngspice) |
| `tools/make_atmega328p_lib.py` | génère le modèle `ATMEGA328P` pour un projet |
| `kicad/atmega328p_full.lib` | modèle complet (chemins relatifs, pour `ngspice/`) |
| `kicad/atmega328p_cosim.lib` | sous-circuits des démos GPIO, ADC, UART |
| `tools/kicad_symbols/`, `tools/dump_kicad_symbol.py` | symboles officiels KiCad 10 utilisés par la carte Pico, et l'outil qui les extrait |
| `kicad/projects/` | 19 projets KiCad de démo, dont `Arduino_UNO`, + guide pas à pas (`README.md`) |
| `tools/make_terminal_lib.py` | modèle SPICE d'un terminal série (trames sur TXD) depuis un fichier texte |
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
- [rp2040js](https://github.com/wokwi/rp2040js) (MIT, Uri Shaked / Wokwi)
  et [rp2350js](https://github.com/c1570/rp2350js) (MIT, c1570) —
  émulateurs RP2040 et RP2350, avec les bootroms de Raspberry Pi
  (BSD-3-Clause).
- [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (MIT) — moteur
  JavaScript intégré au plug-in.
- Symboles KiCad 10 (CC-BY-SA 4.0 avec l'exception des bibliothèques KiCad).
- [ngspice](https://ngspice.sourceforge.io/) — modèle XSPICE `d_cosim`
  de Giles Atkinson ; `include/cosim_abi.h` reprend son interface
  (licence BSD modifiée).
