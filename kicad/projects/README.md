# Projets KiCad : mode d'emploi pas à pas

Ce dossier contient quatre projets KiCad prêts à simuler. Le principal,
**`simAVR_atmega328p`**, utilise le **symbole officiel de la bibliothèque
KiCad `MCU_Microchip_ATmega:ATmega328P-P`** avec son empreinte officielle
`Package_DIP:DIP-28_W7.62mm` : le même schéma sert à la simulation **et** à
la fabrication du circuit imprimé.

Disposition du symbole officiel : toutes les E/S à droite (PB, PC, PD),
VCC et AVCC en haut, GND en bas, AREF à gauche. Les broches inutilisées de
la démo portent un drapeau « non connecté » (ERC propre).

| Projet | Firmware | Ce qu'il montre |
|---|---|---|
| `simAVR_atmega328p` | `firmware/full_demo.c` ou `full_demo/full_demo.ino` | bouton avec pull-up interne, LED, potentiomètre (ADC), PWM, UART |
| `simAVR_demo` | `firmware/blink.c` | Timer0 + interruption (LED à 1 kHz), entrée filtrée RC |
| `simAVR_adc` | `firmware/adc_pwm.c` | convertisseur SAR visible, PWM filtrée |
| `simAVR_uart` | `firmware/uart_adc.c` | console série, trames sur TXD |

*Outils → Mettre à jour les symboles depuis la bibliothèque* remplace la
copie du symbole intégrée au schéma par celle de votre bibliothèque KiCad,
sans changer le câblage ni le modèle de simulation.

Seul `simAVR_atmega328p` a des empreintes : les trois autres utilisent des
symboles simplifiés à 2–3 broches, faits uniquement pour la simulation.

---

## Étape 0 — Prérequis (une seule fois)

Le projet doit être installé et compilé (voir le README principal, § 1) :

```bash
cd ~/dev
git clone https://github.com/PFrogerais/kisimavr.git
cd kisimavr
cmake -S . -B build && cmake --build build     # -> build/mcu_cosim.so
make -C firmware                               # -> firmware/*.hex
cmake --build build --target demos             # -> kicad/projects/*/*.lib
```

> Les modèles `.lib` des démos contiennent des chemins **absolus** propres à
> votre machine : ils ne sont pas dans git, la cible `demos` (ou
> `python3 tools/setup_demos.py`) les écrit. Seuls les `.lib` sont
> réécrits, vos schémas ne sont pas touchés. À relancer après un
> déplacement du dossier. Si le chemin du `.so` contient des majuscules,
> `python3 tools/setup_demos.py --so /opt/kisimavr/mcu_cosim.so`
> (KiCad met la netlist en minuscules).

## Étape 1 — Ouvrir la démo et simuler

1. Lancer KiCad depuis un terminal, pour voir les messages du plug-in :
   ```bash
   flatpak run org.kicad.KiCad 2>&1 | grep --line-buffered mcu_cosim   # KiCad Flatpak
   # ou : kicad 2>&1 | grep --line-buffered mcu_cosim                  # KiCad natif
   ```
2. *Fichier → Ouvrir un projet* → `kicad/projects/simAVR_atmega328p/simAVR_atmega328p.kicad_pro`.
3. Ouvrir le schéma, puis *Inspecter → Simulateur*.
4. Créer l'analyse : *Simulation → Paramètres de simulation* →
   **Transitoire**, pas `0.5u`, durée `20m`.
5. Dans ces mêmes paramètres, **décocher « Sauvegarder tous les courants »**
   (sinon : erreur `.probe alli` incompatible avec les nœuds numériques).
6. Lancer (▶). Le terminal affiche :
   ```
   [mcu_cosim] atmega328p @ 16000000 Hz running …/full_demo.hex (0 in, 0 out, 20 io, 6 ADC)
   [mcu_cosim] t=… s UART0> ATmega328P full model
   [mcu_cosim] t=… s UART0> A0 = …  BTN = 1
   ```
7. Avec la sonde, cliquer sur les labels **BTN**, **D13**, **A0**, **VAVG**,
   **TXD**. Attendu :
   - BTN tombe à 0 entre 4 et 12 ms (bouton appuyé), sinon le pull-up
     interne le tient à 5 V — il n'y a aucune résistance de tirage externe ;
   - D13 (LED) est à 5 V pendant l'appui ;
   - VAVG (PWM filtrée) suit A0 ;
   - TXD porte les trames série (zoomer sur ~200 µs pour voir les bits).

Le texte complet de l'UART est aussi écrit dans `uart_log.txt`, dans le
dossier du projet :
```bash
tail -f kicad/projects/simAVR_atmega328p/uart_log.txt
```

## Étape 2 — Mettre votre propre programme

### 2.a Compiler le programme

- **Arduino IDE** : carte **Arduino Uno**, puis
  *Croquis → Exporter les binaires compilés*. Prendre
  `MonCroquis.ino.hex` (**pas** `…with_bootloader.hex`) dans
  `build/arduino.avr.uno/` du dossier du croquis.
- **avr-gcc** : `avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -Os -o prog.elf prog.c`
  puis `avr-objcopy -O ihex -R .eeprom prog.elf prog.hex`.

### 2.b Générer le modèle du composant

```bash
python3 ~/dev/kisimavr/tools/make_atmega328p_lib.py \
    --hex ~/Arduino/MonCroquis/build/arduino.avr.uno/MonCroquis.ino.hex \
    -o ~/dev/kisimavr/kicad/projects/simAVR_atmega328p/atmega328p_full.lib
```

Le script écrit tous les chemins en absolu et prévient si un fichier est
introuvable, si le chemin du `.so` contient des majuscules (KiCad les met
en minuscules : ngspice ne le trouverait pas) ou si vous avez pris le
`.hex` avec bootloader.

### 2.c Relancer la simulation

Un simple ▶ suffit : le `.hex` est relu à chaque simulation.
**Ne redémarrez KiCad que si vous avez recompilé `mcu_cosim.so`.**

## Étape 3 — Créer votre propre projet

### Méthode rapide : dupliquer la démo

```bash
cp -r ~/dev/kisimavr/kicad/projects/simAVR_atmega328p ~/kicad/mon_projet
cd ~/kicad/mon_projet
for f in simAVR_atmega328p.*; do mv "$f" "mon_projet.${f#*.}"; done
python3 ~/dev/kisimavr/tools/make_atmega328p_lib.py --hex /chemin/mon.hex -o atmega328p_full.lib
```
Ouvrir `mon_projet.kicad_pro`, supprimer ce qui ne sert pas, câbler votre
montage.

### Méthode complète : partir d'un projet vide

1. **Générer le modèle** dans le dossier du projet (étape 2.b, avec
   `-o ~/kicad/mon_projet/atmega328p_full.lib`).
2. **Placer le symbole officiel** : touche **A** →
   `MCU_Microchip_ATmega:ATmega328P-P`. Son empreinte
   `Package_DIP:DIP-28_W7.62mm` est déjà renseignée.
3. **Associer le modèle de simulation** : double-clic sur le symbole →
   *Modèle de simulation…* :
   - onglet *Modèle* : **Modèle SPICE depuis un fichier**,
     fichier `atmega328p_full.lib`, modèle **`ATMEGA328P`** ;
   - onglet *Affectation des broches* : si les listes sont incomplètes,
     fermer avec OK et renseigner directement le champ **`Sim.Pins`** des
     propriétés du symbole :
     ```
     2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5
     ```
   Champs obtenus : `Sim.Device = SUBCKT`, `Sim.Library = atmega328p_full.lib`,
   `Sim.Name = ATMEGA328P`, `Sim.Pins = …`.
4. **Alimentation** : relier VCC (7) et AVCC (20) à `+5V`, les deux GND
   (8, 22) à la masse, avec un `PWR_FLAG` sur chaque réseau (ERC). Ces
   broches ne sont pas simulées : le modèle fonctionne en 5 V interne.
5. **Masse de simulation** : au moins un symbole `GND` dans le schéma
   (nœud 0 de ngspice).
6. **Stimuli** (composants de `Simulation_SPICE`, sans empreinte) :
   - bouton : une source `VPULSE` 5 V → 0 V en série avec une diode, ou un
     interrupteur commandé ;
   - potentiomètre : une `VPULSE` en rampe (`tr` = durée de la simulation)
     derrière une résistance de 10 kΩ ;
   - pour ces composants, décocher *Exclure de la simulation* et cocher
     *Ne pas placer sur le PCB* / *Exclure de la nomenclature*.
7. **LED** : modèle `LEDRED` de `led_models.lib` (à copier depuis la démo) :
   *Modèle SPICE depuis un fichier* → `led_models.lib` → `LEDRED`,
   broches `1=K 2=A`.
8. **Simuler** comme à l'étape 1.

### Correspondance des broches

| Arduino | AVR | DIP-28 | | Arduino | AVR | DIP-28 |
|---|---|---|---|---|---|---|
| D0 (RX) | PD0 | 2 | | D8 | PB0 | 14 |
| D1 (TX) | PD1 | 3 | | D9 | PB1 | 15 |
| D2 | PD2 | 4 | | D10 | PB2 | 16 |
| D3 | PD3 | 5 | | D11 | PB3 | 17 |
| D4 | PD4 | 6 | | D12 | PB4 | 18 |
| D5 | PD5 | 11 | | D13 | PB5 | 19 |
| D6 | PD6 | 12 | | A0–A5 | PC0–PC5 | 23–28 |
| D7 | PD7 | 13 | | VCC / AVCC | — | 7 / 20 |
| RESET | PC6 | 1 | | GND | — | 8, 22 |
| XTAL1/2 | PB6/PB7 | 9 / 10 | | AREF | — | 21 |

## Étape 4 — Passer au circuit imprimé

Les empreintes de `simAVR_atmega328p` sont réelles :

| Composant | Empreinte |
|---|---|
| U1 ATmega328P-P | `Package_DIP:DIP-28_W7.62mm` |
| Résistances | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal` |
| C1 | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| LED | `LED_THT:LED_D5.0mm` |
| Diode | `Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal` |
| Sources V1, V2 | aucune (simulation seulement, exclues du PCB) |

*Outils → Mettre à jour le PCB depuis le schéma* (F8) les importe dans
l'éditeur de PCB.

⚠ Pour une carte **réelle**, ajoutez ce que la simulation ne modélise pas :
quartz 16 MHz + 2 × 22 pF (broches 9–10), condensateurs de découplage
100 nF sur VCC et AVCC, résistance de 10 kΩ de RESET vers +5V, connecteur
de programmation (ISP ou FTDI), vrai bouton poussoir et vrai potentiomètre
à la place des sources de simulation.

## En cas de problème

| Symptôme | Remède |
|---|---|
| Sorties figées à 0, terminal : `cannot open firmware` | chemin du `.hex` faux : régénérer le `.lib` (étape 2.b) |
| Sorties figées à 0, aucun message | KiCad utilise un ancien `mcu_cosim.so` : fermer **tout** KiCad (`pgrep -a kicad` vide), relancer |
| `'.probe alli' and digital nodes are not compatible` | décocher « Sauvegarder tous les courants » |
| `mismatched XSPICE/co-simulator … counts` | `.lib` d'une ancienne version : le régénérer avec le script |
| Champ `Sim.Pins` refusé / broches manquantes | coller la chaîne `Sim.Pins` ci-dessus dans les propriétés du symbole |
| Simulation interminable | `delay()` longs dans le programme : 1 s de `delay` = 1 s simulée |

Les modèles `.lib` des projets sont écrits par `tools/setup_demos.py`.
Les schémas ont été produits une première fois par `tools/gen_kicad_demo.py`
(`python3 tools/gen_kicad_demo.py <dossier> <lib source> full|gpio|adc|uart --kisimavr <chemin>`).
