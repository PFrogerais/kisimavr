#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the wired KiCad demo projects of kisimavr.

Usage: python3 tools/gen_kicad_demo.py OUT_DIR SOURCE_LIB VARIANT [--kisimavr DIR]
  VARIANT: full or i2c (official ATmega328P-P, DIP-28), gpio, adc, uart
  --kisimavr: absolute path of the kisimavr folder written into the .lib
              (default: the folder containing this script's parent)

Every part is placed unrotated so pin positions are simple:
schematic pin = (X + px, Y - py) for a library pin at (px, py).
"""

import json
import os
import sys
import uuid

VARIANT = sys.argv[3] if len(sys.argv) > 3 else "gpio"
PROJECT = {"gpio": "simAVR_demo", "adc": "simAVR_adc", "uart": "simAVR_uart",
           "full": "simAVR_atmega328p", "i2c": "simAVR_i2c"}[VARIANT]
OUT = sys.argv[1]
KISIMAVR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if "--kisimavr" in sys.argv:
    KISIMAVR = os.path.abspath(os.path.expanduser(sys.argv[sys.argv.index("--kisimavr") + 1]))

# Real footprints (KiCad standard libraries), by symbol.
FOOTPRINTS = {
    "MCU_Microchip_ATmega:ATmega328P-P": "Package_DIP:DIP-28_W7.62mm",
    "Device:R": "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal",
    "Device:C": "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm",
    "Device:LED": "LED_THT:LED_D5.0mm",
    "Device:D": "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal",
}
DATASHEETS = {
    "MCU_Microchip_ATmega:ATmega328P-P": "http://ww1.microchip.com/downloads/en/DeviceDoc/ATmega328_P%20AVR%20MCU%20with%20picoPower%20Technology%20Data%20Sheet%2040001984A.pdf",
}
# Simulation-only parts: no footprint, not on the board, not in the BOM.
SIM_ONLY = {"Simulation_SPICE:VPULSE", "Simulation_SPICE:VDC"}

ROOT_UUID = str(uuid.uuid4())


def uid():
    return str(uuid.uuid4())


def font(hide=False, justify=None):
    j = f" (justify {justify})" if justify else ""
    h = " hide" if hide else ""
    return f"(effects (font (size 1.27 1.27)){j}{h})"


# --------------------------------------------------------------------------
# Library symbols: name -> (body graphics, pins[(num, name, x, y, angle, type)])
# --------------------------------------------------------------------------


LIB = {
    "simAVR:ATMEGA328P_COSIM": dict(
        ref="U", value="ATMEGA328P_COSIM",
        body='(rectangle (start -7.62 5.08) (end 7.62 -5.08) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "PD2", -10.16, 2.54, 0, "bidirectional"),
              ("2", "PB5", -10.16, -2.54, 0, "bidirectional"),
              ("3", "PB0", 10.16, 2.54, 180, "bidirectional")],
        pin_names=True),
    "simAVR:ATMEGA328P_ADC": dict(
        ref="U", value="ATMEGA328P_ADC",
        body='(rectangle (start -7.62 5.08) (end 7.62 -5.08) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "ADC0", -10.16, 2.54, 0, "input"),
              ("2", "PD6", 10.16, 2.54, 180, "output"),
              ("3", "PB5", 10.16, -2.54, 180, "output")],
        pin_names=True),
    "simAVR:ATMEGA328P_UART": dict(
        ref="U", value="ATMEGA328P_UART",
        body='(rectangle (start -7.62 5.08) (end 7.62 -5.08) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "ADC0", -10.16, 2.54, 0, "input"),
              ("2", "TXD", 10.16, 2.54, 180, "output")],
        pin_names=True),
    # Official KiCad symbol MCU_Microchip_ATmega:ATmega328P-P (pins and
    # outline converted from the KiCad library, ATmega48PV-10P family).
    "MCU_Microchip_ATmega:ATmega328P-P": dict(
        ref="U", value="ATmega328P-P",
        body='(rectangle (start -12.7 35.56) (end 12.7 -35.56) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[
              ('1', '~{RESET}/PC6', 15.24, -7.62, 180, 'bidirectional', False),
              ('2', 'PD0', 15.24, -12.7, 180, 'bidirectional', False),
              ('3', 'PD1', 15.24, -15.24, 180, 'bidirectional', False),
              ('4', 'PD2', 15.24, -17.78, 180, 'bidirectional', False),
              ('5', 'PD3', 15.24, -20.32, 180, 'bidirectional', False),
              ('6', 'PD4', 15.24, -22.86, 180, 'bidirectional', False),
              ('7', 'VCC', 0.0, 38.1, 270, 'power_in', False),
              ('8', 'GND', 0.0, -38.1, 90, 'power_in', False),
              ('9', 'XTAL1/PB6', 15.24, 15.24, 180, 'bidirectional', False),
              ('10', 'XTAL2/PB7', 15.24, 12.7, 180, 'bidirectional', False),
              ('11', 'PD5', 15.24, -25.4, 180, 'bidirectional', False),
              ('12', 'PD6', 15.24, -27.94, 180, 'bidirectional', False),
              ('13', 'PD7', 15.24, -30.48, 180, 'bidirectional', False),
              ('14', 'PB0', 15.24, 30.48, 180, 'bidirectional', False),
              ('15', 'PB1', 15.24, 27.94, 180, 'bidirectional', False),
              ('16', 'PB2', 15.24, 25.4, 180, 'bidirectional', False),
              ('17', 'PB3', 15.24, 22.86, 180, 'bidirectional', False),
              ('18', 'PB4', 15.24, 20.32, 180, 'bidirectional', False),
              ('19', 'PB5', 15.24, 17.78, 180, 'bidirectional', False),
              ('20', 'AVCC', 2.54, 38.1, 270, 'power_in', False),
              ('21', 'AREF', -15.24, 30.48, 0, 'passive', False),
              ('22', 'GND', 0.0, -38.1, 90, 'passive', True),
              ('23', 'PC0', 15.24, 7.62, 180, 'bidirectional', False),
              ('24', 'PC1', 15.24, 5.08, 180, 'bidirectional', False),
              ('25', 'PC2', 15.24, 2.54, 180, 'bidirectional', False),
              ('26', 'PC3', 15.24, 0.0, 180, 'bidirectional', False),
              ('27', 'PC4', 15.24, -2.54, 180, 'bidirectional', False),
              ('28', 'PC5', 15.24, -5.08, 180, 'bidirectional', False)],
        pin_names=True,
        description="20MHz, 32kB Flash, 2kB SRAM, 1kB EEPROM, DIP-28",
        keywords="AVR 8bit Microcontroller MegaAVR PicoPower",
        fp_filters="DIP*W7.62mm*"),
    "Device:D": dict(
        ref="D", value="D",
        body='(polyline (pts (xy -1.27 1.27) (xy 1.27 1.27) (xy 0 -1.27) (xy -1.27 1.27)) '
             '(stroke (width 0.254) (type default)) (fill (type none))) '
             '(polyline (pts (xy -1.27 -1.27) (xy 1.27 -1.27)) '
             '(stroke (width 0.254) (type default)) (fill (type none)))',
        pins=[("1", "K", 0, -3.81, 90, "passive"),
              ("2", "A", 0, 3.81, 270, "passive")],
        pin_names=False),
    "Device:R": dict(
        ref="R", value="R",
        body='(rectangle (start -1.016 2.54) (end 1.016 -2.54) '
             '(stroke (width 0.254) (type default)) (fill (type none)))',
        pins=[("1", "~", 0, 3.81, 270, "passive"),
              ("2", "~", 0, -3.81, 90, "passive")],
        pin_names=False),
    "Device:C": dict(
        ref="C", value="C",
        body='(polyline (pts (xy -2.032 0.762) (xy 2.032 0.762)) '
             '(stroke (width 0.508) (type default)) (fill (type none))) '
             '(polyline (pts (xy -2.032 -0.762) (xy 2.032 -0.762)) '
             '(stroke (width 0.508) (type default)) (fill (type none)))',
        pins=[("1", "~", 0, 3.81, 270, "passive"),
              ("2", "~", 0, -3.81, 90, "passive")],
        pin_names=False),
    "Device:LED": dict(
        ref="D", value="LED",
        # Vertical LED: anode (pin 2) on top, cathode (pin 1) at bottom.
        body='(polyline (pts (xy -1.27 1.27) (xy 1.27 1.27) (xy 0 -1.27) (xy -1.27 1.27)) '
             '(stroke (width 0.254) (type default)) (fill (type none))) '
             '(polyline (pts (xy -1.27 -1.27) (xy 1.27 -1.27)) '
             '(stroke (width 0.254) (type default)) (fill (type none))) '
             '(polyline (pts (xy 1.778 0.508) (xy 3.048 -0.762)) '
             '(stroke (width 0.2) (type default)) (fill (type none))) '
             '(polyline (pts (xy 1.778 1.524) (xy 3.048 0.254)) '
             '(stroke (width 0.2) (type default)) (fill (type none)))',
        pins=[("1", "K", 0, -3.81, 90, "passive"),
              ("2", "A", 0, 3.81, 270, "passive")],
        pin_names=False),
    "Simulation_SPICE:VPULSE": dict(
        ref="V", value="VPULSE",
        body='(circle (center 0 0) (radius 2.54) '
             '(stroke (width 0.254) (type default)) (fill (type background))) '
             '(polyline (pts (xy -1.27 -0.762) (xy -0.508 -0.762) (xy -0.508 0.762) '
             '(xy 0.508 0.762) (xy 0.508 -0.762) (xy 1.27 -0.762)) '
             '(stroke (width 0.2) (type default)) (fill (type none)))',
        pins=[("1", "+", 0, 5.08, 270, "passive"),
              ("2", "-", 0, -5.08, 90, "passive")],
        pin_names=False),
    "Simulation_SPICE:VDC": dict(
        ref="V", value="VDC",
        body='(circle (center 0 0) (radius 2.54) '
             '(stroke (width 0.254) (type default)) (fill (type background))) '
             '(polyline (pts (xy -1.27 0.508) (xy 1.27 0.508)) '
             '(stroke (width 0.2) (type default)) (fill (type none))) '
             '(polyline (pts (xy -0.762 -0.508) (xy 0.762 -0.508)) '
             '(stroke (width 0.2) (type default)) (fill (type none)))',
        pins=[("1", "+", 0, 5.08, 270, "passive"),
              ("2", "-", 0, -5.08, 90, "passive")],
        pin_names=False),
    "power:+5V": dict(
        ref="#PWR", value="+5V", power=True,
        body='(polyline (pts (xy -0.762 1.27) (xy 0 2.54) (xy 0.762 1.27)) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy 0 0) (xy 0 2.54)) '
             '(stroke (width 0) (type default)) (fill (type none)))',
        pins=[("1", "+5V", 0, 0, 90, "power_in")],
        pin_names=False),
    "power:PWR_FLAG": dict(
        ref="#FLG", value="PWR_FLAG", power=True,
        body='(polyline (pts (xy 0 0) (xy 0 1.27) (xy -1.016 1.905) (xy 0 2.54) '
             '(xy 1.016 1.905) (xy 0 1.27)) '
             '(stroke (width 0) (type default)) (fill (type none)))',
        pins=[("1", "pwr", 0, 0, 90, "power_out")],
        pin_names=False),
    "power:GND": dict(
        ref="#PWR", value="GND", power=True,
        body='(polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27) (xy 0 -2.54) '
             '(xy -1.27 -1.27) (xy 0 -1.27)) '
             '(stroke (width 0) (type default)) (fill (type none)))',
        pins=[("1", "GND", 0, 0, 270, "power_in")],
        pin_names=False),
}


def lib_symbol(lib_id, d):
    short = lib_id.split(":")[1]
    power = "(power) " if d.get("power") else ""
    pn = "" if d["pin_names"] else "(pin_names (offset 0) hide) "
    if d["pin_names"]:
        pn = "(pin_names (offset 1.016)) "
    pins = []
    for pin in d["pins"]:
        num, name, x, y, ang, typ = pin[:6]
        length = 0 if d.get("power") else 2.54
        hide = " hide" if (d.get("power") or (len(pin) > 6 and pin[6])) else ""
        pins.append(
            f'(pin {typ} line (at {x} {y} {ang}) (length {length}){hide} '
            f'(name "{name}" {font()}) (number "{num}" {font()}))')
    return (
        f'(symbol "{lib_id}" {power}{pn}(exclude_from_sim no) (in_bom yes) (on_board yes) '
        f'(property "Reference" "{d["ref"]}" (at 0 7.62 0) {font()}) '
        f'(property "Value" "{d["value"]}" (at 0 -7.62 0) {font()}) '
        f'(property "Footprint" "{FOOTPRINTS.get(lib_id, "")}" (at 0 0 0) {font(hide=True)}) '
        f'(property "Datasheet" "{DATASHEETS.get(lib_id, "")}" (at 0 0 0) {font(hide=True)}) '
        f'(property "Description" "{d.get("description", "")}" (at 0 0 0) {font(hide=True)}) '
        + (f'(property "ki_keywords" "{d["keywords"]}" (at 0 0 0) {font(hide=True)}) '
           if d.get("keywords") else "")
        + (f'(property "ki_fp_filters" "{d["fp_filters"]}" (at 0 0 0) {font(hide=True)}) '
           if d.get("fp_filters") else "")
        + f'(symbol "{short}_0_1" {d["body"]}) '
        f'(symbol "{short}_1_1" {" ".join(pins)}))')


# --------------------------------------------------------------------------
# Placed parts: (lib_id, ref, value, X, Y, extra properties)
# --------------------------------------------------------------------------

LIBFILE = "atmega328p_full.lib" if VARIANT in ("full", "i2c") else "atmega328p_cosim.lib"
LEDFILE = "led_models.lib"

PARTS = [
    ("simAVR:ATMEGA328P_COSIM", "U1", "ATMEGA328P_COSIM", 127.0, 76.2,
     {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
      "Sim.Name": "ATMEGA328P_COSIM", "Sim.Pins": "1=PD2 2=PB5 3=PB0"}),
    ("Simulation_SPICE:VPULSE", "V1", "VPULSE", 88.9, 99.06,
     {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
      "Sim.Params": "y1=0 y2=5 td=2m tr=1u tf=1u tw=3m per=6m"}),
    ("Device:R", "R1", "1k", 88.9, 85.09, {}),
    ("Device:C", "C1", "100n", 101.6, 80.01, {}),
    ("Device:R", "R2", "330", 109.22, 88.9, {}),
    ("Device:LED", "D1", "LEDRED", 109.22, 99.06,
     {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE,
      "Sim.Name": "LEDRED", "Sim.Pins": "1=K 2=A"}),
    ("Device:R", "R3", "10k", 144.78, 80.01, {}),
    ("power:GND", "#PWR01", "GND", 88.9, 106.68, {}),
    ("power:GND", "#PWR02", "GND", 101.6, 86.36, {}),
    ("power:GND", "#PWR03", "GND", 109.22, 105.41, {}),
    ("power:GND", "#PWR04", "GND", 144.78, 86.36, {}),
]

WIRES = [
    # Source -> R1 -> PD2 (with C1 to ground)
    ((88.9, 93.98), (88.9, 88.9)),
    ((88.9, 81.28), (88.9, 73.66)),
    ((88.9, 73.66), (101.6, 73.66)),
    ((101.6, 73.66), (116.84, 73.66)),
    ((101.6, 73.66), (101.6, 76.2)),
    ((101.6, 83.82), (101.6, 86.36)),
    ((88.9, 104.14), (88.9, 106.68)),
    # PB5 -> R2 -> LED -> GND
    ((116.84, 78.74), (109.22, 78.74)),
    ((109.22, 78.74), (109.22, 85.09)),
    ((109.22, 92.71), (109.22, 95.25)),
    ((109.22, 102.87), (109.22, 105.41)),
    # PB0 -> R3 -> GND
    ((137.16, 73.66), (144.78, 73.66)),
    ((144.78, 73.66), (144.78, 76.2)),
    ((144.78, 83.82), (144.78, 86.36)),
]

JUNCTIONS = [(101.6, 73.66)]
LABELS = [("PD2", 106.68, 73.66), ("PB5", 114.3, 78.74), ("PB0", 140.97, 73.66)]
DIRECTIVES = [(".tran 1u 10m", 88.9, 116.84)]

if VARIANT == "adc":
    PARTS = [
        ("simAVR:ATMEGA328P_ADC", "U1", "ATMEGA328P_ADC", 127.0, 76.2,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P_ADC", "Sim.Pins": "1=ADC0 2=PD6 3=PB5"}),
        ("Simulation_SPICE:VPULSE", "V1", "RAMPE 0-5V", 88.9, 99.06,
         {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "y1=0 y2=5 td=0 tr=20m tf=1u tw=1 per=2"}),
        ("Device:R", "R1", "10k", 88.9, 85.09, {}),
        ("Device:C", "C1", "10n", 101.6, 80.01, {}),
        ("Device:R", "R4", "10k", 149.86, 80.01, {}),
        ("Device:C", "C2", "100n", 149.86, 91.44, {}),
        ("Device:R", "R2", "330", 139.7, 88.9, {}),
        ("Device:LED", "D1", "LEDRED", 139.7, 99.06,
         {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE,
          "Sim.Name": "LEDRED", "Sim.Pins": "1=K 2=A"}),
        ("power:GND", "#PWR01", "GND", 88.9, 106.68, {}),
        ("power:GND", "#PWR02", "GND", 101.6, 86.36, {}),
        ("power:GND", "#PWR03", "GND", 149.86, 97.79, {}),
        ("power:GND", "#PWR04", "GND", 139.7, 105.41, {}),
    ]
    WIRES = [
        ((88.9, 93.98), (88.9, 88.9)),
        ((88.9, 81.28), (88.9, 73.66)),
        ((88.9, 73.66), (101.6, 73.66)),
        ((101.6, 73.66), (116.84, 73.66)),
        ((101.6, 73.66), (101.6, 76.2)),
        ((101.6, 83.82), (101.6, 86.36)),
        ((88.9, 104.14), (88.9, 106.68)),
        # PD6 -> RC low-pass
        ((137.16, 73.66), (149.86, 73.66)),
        ((149.86, 73.66), (149.86, 76.2)),
        ((149.86, 83.82), (149.86, 87.63)),
        ((149.86, 95.25), (149.86, 97.79)),
        # PB5 -> R2 -> LED
        ((137.16, 78.74), (139.7, 78.74)),
        ((139.7, 78.74), (139.7, 85.09)),
        ((139.7, 92.71), (139.7, 95.25)),
        ((139.7, 102.87), (139.7, 105.41)),
    ]
    JUNCTIONS = [(101.6, 73.66)]
    LABELS = [("A0", 106.68, 73.66), ("PD6", 142.24, 73.66),
              ("PB5", 138.43, 78.74), ("VAVG", 149.86, 85.09)]
    DIRECTIVES = [(".tran 0.5u 20m", 88.9, 116.84)]

if VARIANT == "uart":
    PARTS = [
        ("simAVR:ATMEGA328P_UART", "U1", "ATMEGA328P_UART", 127.0, 76.2,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P_UART", "Sim.Pins": "1=ADC0 2=TXD"}),
        ("Simulation_SPICE:VPULSE", "V1", "RAMPE 1-4V", 88.9, 99.06,
         {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "y1=1 y2=4 td=0 tr=20m tf=1u tw=1 per=2"}),
        ("Device:R", "R1", "10k", 88.9, 85.09, {}),
        ("Device:C", "C1", "10n", 101.6, 80.01, {}),
        ("Device:R", "R2", "100k", 149.86, 80.01, {}),
        ("power:GND", "#PWR01", "GND", 88.9, 106.68, {}),
        ("power:GND", "#PWR02", "GND", 101.6, 86.36, {}),
        ("power:GND", "#PWR03", "GND", 149.86, 86.36, {}),
    ]
    WIRES = [
        ((88.9, 93.98), (88.9, 88.9)),
        ((88.9, 81.28), (88.9, 73.66)),
        ((88.9, 73.66), (101.6, 73.66)),
        ((101.6, 73.66), (116.84, 73.66)),
        ((101.6, 73.66), (101.6, 76.2)),
        ((101.6, 83.82), (101.6, 86.36)),
        ((88.9, 104.14), (88.9, 106.68)),
        ((137.16, 73.66), (149.86, 73.66)),
        ((149.86, 73.66), (149.86, 76.2)),
        ((149.86, 83.82), (149.86, 86.36)),
    ]
    JUNCTIONS = [(101.6, 73.66)]
    LABELS = [("A0", 106.68, 73.66), ("TXD", 142.24, 73.66)]
    DIRECTIVES = [(".tran 0.5u 20m", 88.9, 116.84)]

if VARIANT == "full":
    SIM_PINS = ("2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 "
                "16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5")
    # U1 at (127, 88.9): all I/O on the right (x = 142.24), VCC/AVCC on top
    # (y = 50.8), GND at the bottom (y = 127).
    PARTS = [
        ("MCU_Microchip_ATmega:ATmega328P-P", "U1", "ATmega328P-P", 127.0, 88.9,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P", "Sim.Pins": SIM_PINS}),
        # Power
        ("power:+5V", "#PWR01", "+5V", 127.0, 48.26, {}),
        ("power:PWR_FLAG", "#FLG01", "PWR_FLAG", 129.54, 48.26, {}),
        ("power:GND", "#PWR02", "GND", 127.0, 129.54, {}),
        ("power:PWR_FLAG", "#FLG02", "PWR_FLAG", 121.92, 129.54, {}),
        # D13 (PB5, pin 19): LED through 330 ohms
        ("Device:R", "R3", "330", 190.5, 74.93, {}),
        ("Device:LED", "D2", "LEDRED", 190.5, 85.09,
         {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE,
          "Sim.Name": "LEDRED", "Sim.Pins": "1=K 2=A"}),
        ("power:GND", "#PWR03", "GND", 190.5, 91.44, {}),
        # A0 (PC0, pin 23): potentiometer = 0.5 -> 4.5 V ramp through 10k
        ("Device:R", "R4", "10k", 177.8, 85.09, {}),
        ("Simulation_SPICE:VPULSE", "V2", "POT", 177.8, 96.52,
         {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "y1=0.5 y2=4.5 td=0 tr=20m tf=1u tw=1 per=2"}),
        ("power:GND", "#PWR04", "GND", 177.8, 104.14, {}),
        # D1 (PD1/TXD, pin 3): receiver load
        ("Device:R", "R1", "100k", 165.1, 107.95, {}),
        ("power:GND", "#PWR05", "GND", 165.1, 114.3, {}),
        # D2 (PD2, pin 4): push-button = diode to a 5 V / 0 V source
        ("Device:D", "D1", "DSIG", 154.94, 110.49,
         {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE,
          "Sim.Name": "DSIG", "Sim.Pins": "1=K 2=A"}),
        ("Simulation_SPICE:VPULSE", "V1", "BOUTON", 154.94, 121.92,
         {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "y1=5 y2=0 td=4m tr=1u tf=1u tw=8m per=40m"}),
        ("power:GND", "#PWR06", "GND", 154.94, 129.54, {}),
        # D6 (PD6, pin 12): PWM averaged by an RC low-pass
        ("Device:R", "R2", "10k", 147.32, 120.65, {}),
        ("Device:C", "C1", "100n", 147.32, 130.81, {}),
        ("power:GND", "#PWR07", "GND", 147.32, 137.16, {}),
    ]
    WIRES = [
        # Power
        ((127.0, 50.8), (127.0, 48.26)),
        ((129.54, 50.8), (129.54, 48.26)),
        ((127.0, 48.26), (129.54, 48.26)),
        ((127.0, 127.0), (127.0, 129.54)),
        ((127.0, 129.54), (121.92, 129.54)),
        # D13
        ((142.24, 71.12), (190.5, 71.12)),
        ((190.5, 78.74), (190.5, 81.28)),
        ((190.5, 88.9), (190.5, 91.44)),
        # A0
        ((142.24, 81.28), (177.8, 81.28)),
        ((177.8, 88.9), (177.8, 91.44)),
        ((177.8, 101.6), (177.8, 104.14)),
        # TXD
        ((142.24, 104.14), (165.1, 104.14)),
        ((165.1, 111.76), (165.1, 114.3)),
        # BTN
        ((142.24, 106.68), (154.94, 106.68)),
        ((154.94, 114.3), (154.94, 116.84)),
        ((154.94, 127.0), (154.94, 129.54)),
        # PWM
        ((142.24, 116.84), (147.32, 116.84)),
        ((147.32, 124.46), (147.32, 127.0)),
        ((147.32, 134.62), (147.32, 137.16)),
    ]
    JUNCTIONS = []
    LABELS = [("D13", 146.05, 71.12), ("A0", 146.05, 81.28), ("TXD", 148.59, 104.14),
              ("BTN", 148.59, 106.68), ("PWM", 143.51, 116.84), ("VAVG", 147.32, 125.73)]
    NO_CONNECT_REF = "U1"      # Unused U1 pins get a no-connect flag.
    DIRECTIVES = [(".tran 0.5u 20m", 101.6, 144.78)]

if VARIANT == "i2c":
    SIM_PINS = ("2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 "
                "16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5")
    # Official symbol at (127, 88.9): PC4/SDA pin 27 at (142.24, 91.44),
    # PC5/SCL pin 28 at (142.24, 93.98), PD1/TXD pin 3 at (142.24, 104.14).
    PARTS = [
        ("MCU_Microchip_ATmega:ATmega328P-P", "U1", "ATmega328P-P", 127.0, 88.9,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P", "Sim.Pins": SIM_PINS}),
        ("power:+5V", "#PWR01", "+5V", 127.0, 48.26, {}),
        ("power:PWR_FLAG", "#FLG01", "PWR_FLAG", 129.54, 48.26, {}),
        ("power:GND", "#PWR02", "GND", 127.0, 129.54, {}),
        ("power:PWR_FLAG", "#FLG02", "PWR_FLAG", 121.92, 129.54, {}),
        # I2C pull-ups 4.7k to +5V
        ("Device:R", "R1", "4.7k", 160.02, 87.63, {}),
        ("power:+5V", "#PWR03", "+5V", 160.02, 83.82, {}),
        ("Device:R", "R2", "4.7k", 170.18, 90.17, {}),
        ("power:+5V", "#PWR04", "+5V", 170.18, 86.36, {}),
        # TXD load (USB-serial adapter input)
        ("Device:R", "R3", "100k", 154.94, 107.95, {}),
        ("power:GND", "#PWR05", "GND", 154.94, 114.3, {}),
        # 5 V supply for the simulation (not on the board)
        ("Simulation_SPICE:VDC", "V1", "5V", 190.5, 99.06,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "dc=5"}),
        ("power:+5V", "#PWR06", "+5V", 190.5, 91.44, {}),
        ("power:GND", "#PWR07", "GND", 190.5, 106.68, {}),
    ]
    WIRES = [
        ((127.0, 50.8), (127.0, 48.26)),
        ((129.54, 50.8), (129.54, 48.26)),
        ((127.0, 48.26), (129.54, 48.26)),
        ((127.0, 127.0), (127.0, 129.54)),
        ((127.0, 129.54), (121.92, 129.54)),
        ((142.24, 91.44), (160.02, 91.44)),     # SDA
        ((142.24, 93.98), (170.18, 93.98)),     # SCL
        ((142.24, 104.14), (154.94, 104.14)),   # TXD
        ((154.94, 111.76), (154.94, 114.3)),
        ((190.5, 93.98), (190.5, 91.44)),
        ((190.5, 104.14), (190.5, 106.68)),
    ]
    JUNCTIONS = []
    LABELS = [("SDA", 146.05, 91.44), ("SCL", 148.59, 93.98), ("TXD", 146.05, 104.14)]
    NO_CONNECT_REF = "U1"
    DIRECTIVES = [(".tran 0.2u 5m", 101.6, 144.78)]


def placed_symbol(lib_id, ref, value, x, y, extra):
    d = LIB[lib_id]
    hide_ref = d.get("power", False)
    props = [
        f'(property "Reference" "{ref}" (at {x + 3.81} {y - 1.27} 0) '
        f'{font(hide=hide_ref, justify="left")})',
        f'(property "Value" "{value}" (at {x + 3.81} {y + 1.27} 0) {font(justify="left")})',
        f'(property "Footprint" "{FOOTPRINTS.get(lib_id, "")}" (at {x} {y} 0) {font(hide=True)})',
        f'(property "Datasheet" "{DATASHEETS.get(lib_id, "")}" (at {x} {y} 0) {font(hide=True)})',
        f'(property "Description" "" (at {x} {y} 0) {font(hide=True)})',
    ]
    for k, v in extra.items():
        props.append(f'(property "{k}" "{v}" (at {x} {y} 0) {font(hide=True)})')
    pins = " ".join(f'(pin "{p[0]}" (uuid "{uid()}"))' for p in d["pins"])
    board = "no" if (lib_id in SIM_ONLY or lib_id == "power:PWR_FLAG") else "yes"
    if d.get("power"):
        board = "yes" if lib_id != "power:PWR_FLAG" else "no"
    return (
        f'(symbol (lib_id "{lib_id}") (at {x} {y} 0) (unit 1) (exclude_from_sim no) '
        f'(in_bom {board}) (on_board {board}) (dnp no) (uuid "{uid()}") {" ".join(props)} {pins} '
        f'(instances (project "{PROJECT}" (path "/{ROOT_UUID}" '
        f'(reference "{ref}") (unit 1)))))')


def schematic():
    parts = [f'(kicad_sch (version 20231120) (generator "eeschema") '
             f'(generator_version "8.0") (uuid "{ROOT_UUID}") (paper "A4")']
    used_ids = {p[0] for p in PARTS}
    parts.append("(lib_symbols " + " ".join(lib_symbol(k, v) for k, v in LIB.items()
                                           if k in used_ids) + ")")
    for (x, y) in JUNCTIONS:
        parts.append(f'(junction (at {x} {y}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))')
    for (a, b) in WIRES:
        parts.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) '
                     f'(stroke (width 0) (type default)) (uuid "{uid()}"))')
    if globals().get("NO_CONNECT_REF"):
        used = {(round(x, 2), round(y, 2)) for a, b in WIRES for (x, y) in (a, b)}
        for lib_id, ref, _v, X, Y, _e in PARTS:
            if ref != NO_CONNECT_REF:
                continue
            for pin in LIB[lib_id]["pins"]:
                pt = (round(X + pin[2], 2), round(Y - pin[3], 2))
                if pt not in used:
                    parts.append(f'(no_connect (at {pt[0]} {pt[1]}) (uuid "{uid()}"))')
    for (name, x, y) in LABELS:
        parts.append(f'(label "{name}" (at {x} {y} 0) (fields_autoplaced yes) '
                     f'{font(justify="left bottom")} (uuid "{uid()}"))')
    for (text, x, y) in DIRECTIVES:
        parts.append(f'(text "{text}" (exclude_from_sim no) (at {x} {y} 0) '
                     f'{font(justify="left bottom")} (uuid "{uid()}"))')
    for p in PARTS:
        parts.append(placed_symbol(*p))
    parts.append('(sheet_instances (path "/" (page "1")))')
    parts.append(")")
    return "\n".join(parts) + "\n"


# --------------------------------------------------------------------------
# Connectivity check: every wire end must touch a pin, a label or a wire.
# --------------------------------------------------------------------------

def check():
    pts = {}
    for lib_id, ref, _v, x, y, _e in PARTS:
        for pin in LIB[lib_id]["pins"]:
            num, name, px, py = pin[:4]
            pts.setdefault((round(x + px, 2), round(y - py, 2)), []).append(f"{ref}.{name}")

    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            a = parent[a]
        return a

    def union(a, b):
        parent[find(a)] = find(b)

    ends = {}
    for a, b in WIRES:
        a, b = (round(a[0], 2), round(a[1], 2)), (round(b[0], 2), round(b[1], 2))
        union(a, b)
        for p in (a, b):
            ends[p] = ends.get(p, 0) + 1
    for p, n in ends.items():
        if p not in pts and n < 2 and p not in [(round(j[0], 2), round(j[1], 2)) for j in JUNCTIONS]:
            raise SystemExit(f"dangling wire end at {p}")
    for p in pts:
        find(p)
    nets = {}
    for p, names in pts.items():
        nets.setdefault(find(p), []).extend(names)
    for net in nets.values():
        print("net:", sorted(net))
    for _n, x, y in LABELS:
        on = any(min(a[0], b[0]) <= x <= max(a[0], b[0]) and min(a[1], b[1]) <= y <= max(a[1], b[1])
                 for a, b in WIRES)
        if not on:
            raise SystemExit(f"label off wire at {x},{y}")


def balanced(text):
    depth = 0
    in_str = False
    for c in text:
        if c == '"':
            in_str = not in_str
        elif not in_str:
            depth += (c == "(") - (c == ")")
            if depth < 0:
                return False
    return depth == 0 and not in_str


def main():
    check()
    os.makedirs(OUT, exist_ok=True)
    sch = schematic()
    assert balanced(sch), "unbalanced s-expression"
    with open(os.path.join(OUT, PROJECT + ".kicad_sch"), "w") as f:
        f.write(sch)

    pro = {"meta": {"filename": PROJECT + ".kicad_pro", "version": 1},
           "sheets": [[ROOT_UUID, "Root"]]}
    with open(os.path.join(OUT, PROJECT + ".kicad_pro"), "w") as f:
        json.dump(pro, f, indent=2)

    with open(os.path.join(OUT, LEDFILE), "w") as f:
        f.write("* Red LED model (pins: anode cathode)\n"
                ".subckt LEDRED A K\n"
                "D1 A K DLEDRED\n"
                ".model DLEDRED D(IS=93.2p RS=42m N=3.73 BV=4 IBV=10u CJO=2.97p "
                "VJ=0.75 M=0.333 TT=4.32u)\n"
                ".ends LEDRED\n\n"
                "* Small-signal diode (pins: anode cathode)\n"
                ".subckt DSIG A K\n"
                "D1 A K D1N4148M\n"
                ".model D1N4148M D(IS=2.52n RS=0.568 N=1.752 BV=100 IBV=100u "
                "CJO=4p M=0.4 TT=20n)\n"
                ".ends DSIG\n")

    with open(sys.argv[2]) as f:
        lib = f.read()
    lib = lib.replace('"../', f'"{KISIMAVR}/')
    with open(os.path.join(OUT, LIBFILE), "w") as f:
        f.write(lib)
    print("written to", OUT)


if __name__ == "__main__":
    main()
