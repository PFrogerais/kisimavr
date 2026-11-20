#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the wired KiCad demo projects of kisimavr.

Usage: python3 tools/gen_kicad_demo.py OUT_DIR SOURCE_LIB VARIANT [--kisimavr DIR]
  VARIANT: full or i2c (official ATmega328P-P, DIP-28), gpio, adc, uart,
           uno (Arduino UNO R3 board around the ATmega328P, official schematic)
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
           "full": "simAVR_atmega328p", "i2c": "simAVR_i2c",
           "uno": "Arduino_UNO"}[VARIANT]
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
    "Device:Resonator": "Crystal:Resonator_SMD_Murata_CSTxExxV-3Pin_3.0x1.1mm",
    "Switch:SW_Push": "Button_Switch_THT:SW_PUSH_6mm",
    "simAVR:LMV358": "Package_SO:VSSOP-8_3x3mm_P0.65mm",
    "Connector_Generic:Conn_01x06": "Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical",
    "Connector_Generic:Conn_01x08": "Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical",
    "Connector_Generic:Conn_01x10": "Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical",
    "Connector_Generic:Conn_02x03_Odd_Even": "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical",
}
DATASHEETS = {
    "MCU_Microchip_ATmega:ATmega328P-P": "http://ww1.microchip.com/downloads/en/DeviceDoc/ATmega328_P%20AVR%20MCU%20with%20picoPower%20Technology%20Data%20Sheet%2040001984A.pdf",
}
# Simulation-only parts: no footprint, not on the board, not in the BOM.
SIM_ONLY = {"Simulation_SPICE:VPULSE", "Simulation_SPICE:VDC", "simAVR:USB_SERIE"}

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


def _conn_1xn(n):
    """Connector_Generic:Conn_01xNN: pins on the left, pin 1 on top."""
    top = 2.54 * ((n - 1) // 2)
    bottom = top - 2.54 * (n - 1)
    return dict(
        ref="J", value=f"Conn_01x{n:02d}",
        body=f'(rectangle (start -2.54 {top + 1.27}) (end 1.27 {bottom - 1.27}) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[(str(k + 1), f"Pin_{k + 1}", -5.08, round(top - 2.54 * k, 2), 0, "passive")
              for k in range(n)],
        pin_names=False)


for _n in (6, 8, 10):
    LIB[f"Connector_Generic:Conn_01x{_n:02d}"] = _conn_1xn(_n)

LIB.update({
    "Connector_Generic:Conn_02x03_Odd_Even": dict(
        ref="J", value="Conn_02x03_Odd_Even",
        body='(rectangle (start -2.54 3.81) (end 5.08 -3.81) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "Pin_1", -5.08, 2.54, 0, "passive"), ("2", "Pin_2", 7.62, 2.54, 180, "passive"),
              ("3", "Pin_3", -5.08, 0, 0, "passive"), ("4", "Pin_4", 7.62, 0, 180, "passive"),
              ("5", "Pin_5", -5.08, -2.54, 0, "passive"), ("6", "Pin_6", 7.62, -2.54, 180, "passive")],
        pin_names=False),
    "Device:Resonator": dict(
        ref="Y", value="Resonator",
        body='(rectangle (start -0.762 1.778) (end 0.762 -1.778) '
             '(stroke (width 0.254) (type default)) (fill (type none))) '
             '(polyline (pts (xy -1.524 2.032) (xy -1.524 -2.032)) '
             '(stroke (width 0.254) (type default)) (fill (type none))) '
             '(polyline (pts (xy 1.524 2.032) (xy 1.524 -2.032)) '
             '(stroke (width 0.254) (type default)) (fill (type none))) '
             '(polyline (pts (xy -2.54 0) (xy -1.524 0)) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy 2.54 0) (xy 1.524 0)) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy -2.032 -2.54) (xy 2.032 -2.54)) '
             '(stroke (width 0) (type dash)) (fill (type none)))',
        pins=[("1", "1", -5.08, 0, 0, "passive"), ("2", "2", 0, -5.08, 90, "passive"),
              ("3", "3", 5.08, 0, 180, "passive")],
        pin_names=False,
        description="Three pin ceramic resonator"),
    "Switch:SW_Push": dict(
        ref="SW", value="SW_Push",
        body='(circle (center -2.032 0) (radius 0.508) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(circle (center 2.032 0) (radius 0.508) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy -2.54 1.27) (xy 2.54 1.27)) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy 0 1.27) (xy 0 3.048)) '
             '(stroke (width 0) (type default)) (fill (type none)))',
        pins=[("1", "1", -5.08, 0, 0, "passive"), ("2", "2", 5.08, 0, 180, "passive")],
        pin_names=False,
        description="Push button switch, generic, two pins"),
    # One op-amp of the dual LMV358 (unit A), power pins included.
    "simAVR:LMV358": dict(
        ref="U", value="LMV358",
        body='(polyline (pts (xy -5.08 5.08) (xy 5.08 0) (xy -5.08 -5.08) (xy -5.08 5.08)) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("3", "+", -7.62, 2.54, 0, "input"), ("2", "-", -7.62, -2.54, 0, "input"),
              ("1", "~", 7.62, 0, 180, "output"),
              ("8", "V+", -2.54, 6.35, 270, "power_in"), ("4", "V-", -2.54, -6.35, 90, "power_in")],
        pin_names=True,
        description="Low-voltage rail-to-rail output op-amp (unit A of the LMV358)"),
    # Simulation-only: the ATmega16U2 USB-serial converter and the PC
    # terminal behind it (terminal.lib, tools/make_terminal_lib.py).
    "simAVR:USB_SERIE": dict(
        ref="U", value="USB_SERIE",
        body='(rectangle (start -7.62 5.08) (end 7.62 -10.16) '
            '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "TXD", 10.16, 2.54, 180, "output"), ("2", "RXD", 10.16, -2.54, 180, "input"),
              ("3", "DTR", 10.16, -7.62, 180, "output"), ("4", "GND", 0, -12.7, 90, "power_in")],
        pin_names=True,
        description="Serial terminal: ATmega16U2 USB-serial converter + PC (simulation only)"),
})


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


def pin_xy(X, Y, pin, rot=0):
    """Schematic position and angle of a library pin, symbol rotated by rot (0/90/180/270)."""
    px, py, ang = pin[2], pin[3], pin[4]
    r = rot % 360
    # Rotate the library point counter-clockwise (y up), then flip y.
    for _ in range(r // 90):
        px, py = -py, px
    return round(X + px, 2), round(Y - py, 2), (ang + r) % 360


# --------------------------------------------------------------------------
# Placed parts: (lib_id, ref, value, X, Y, extra properties)
# --------------------------------------------------------------------------

LIBFILE = ("atmega328p_full.lib" if VARIANT in ("full", "i2c", "uno")
           else "atmega328p_cosim.lib")
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
    ("power:PWR_FLAG", "#FLG01", "PWR_FLAG", 88.9, 106.68, {"_rot": 90}),
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
        ("power:PWR_FLAG", "#FLG01", "PWR_FLAG", 88.9, 106.68, {"_rot": 90}),
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
        ("power:PWR_FLAG", "#FLG01", "PWR_FLAG", 88.9, 106.68, {"_rot": 90}),
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


PAPER = "A4"
TITLE = None
NO_CONNECTS = []
TEXTS = []          # Comments: (text, x, y)

if VARIANT == "uno":
    # Arduino UNO R3 around the ATmega328P, after the official schematic
    # (A000066). Every pin gets a 2.54 mm stub ending on a net label or a
    # power symbol, so the blocks can be laid out freely.
    PAPER = "A3"
    TITLE = ("Arduino UNO R3 - partie ATmega328P (simulation kisimavr)",
             "D'apres le schema officiel Arduino A000066 (UNO R3)",
             "USB, ATmega16U2 et regulateurs : remplaces par U3 et V1 (simulation)")
    UNOFILE = "uno_models.lib"
    TERMFILE = "terminal.lib"
    SIM_PINS = ("2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 "
                "16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5")
    NOSIM = {"_nosim": True}
    UNO_PARTS = [
        # --- ATmega328P (ZU4 on the board) ---
        ("MCU_Microchip_ATmega:ATmega328P-P", "ZU4", "ATmega328P-PU", 165.1, 111.76,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P", "Sim.Pins": SIM_PINS},
         {"7": "+5V", "20": "+5V", "8": "GND", "21": "AREF", "1": "RESET",
          "9": "XTAL1", "10": "XTAL2",
          "14": "D8", "15": "D9", "16": "D10", "17": "D11", "18": "D12", "19": "D13",
          "23": "A0", "24": "A1", "25": "A2", "26": "A3", "27": "A4", "28": "A5",
          "2": "D0_RX", "3": "D1_TX", "4": "D2", "5": "D3", "6": "D4", "11": "D5",
          "12": "D6", "13": "D7"}),
        ("Device:C", "C1", "100n", 134.62, 53.34, {}, {"1": "+5V", "2": "GND"}),
        ("Device:C", "C2", "100n", 144.78, 53.34, {}, {"1": "+5V", "2": "GND"}),
        ("Device:C", "C3", "100n", 134.62, 91.44, NOSIM, {"1": "AREF", "2": "GND"}),
        # --- 16 MHz ceramic resonator ---
        ("Device:Resonator", "Y2", "CSTCE16M0V53-R0", 223.52, 63.5, NOSIM,
         {"1": "XTAL1", "2": "GND", "3": "XTAL2"}),
        ("Device:R", "R1", "1M", 223.52, 48.26, NOSIM, {"1": "XTAL1", "2": "XTAL2"}),
        # --- Reset: pull-up, auto-reset capacitor from DTR, diode, button ---
        ("Device:R", "R2", "10k", 50.8, 55.88, {}, {"1": "+5V", "2": "RESET"}),
        ("Device:D", "D1", "CD1206-S01575", 63.5, 55.88,
         {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE, "Sim.Name": "DSIG",
          "Sim.Pins": "1=K 2=A", "_rot": 180},
         {"1": "+5V", "2": "RESET"}),
        ("Device:C", "C4", "100n", 81.28, 55.88, {}, {"1": "DTR", "2": "RESET"}),
        ("Switch:SW_Push", "SW1", "RESET", 101.6, 66.04, NOSIM, {"1": "RESET", "2": "GND"}),
        # --- LED "L" on D13, buffered by U5A (LMV358 follower) ---
        ("simAVR:LMV358", "U5", "LMV358", 254.0, 111.76,
         {"Sim.Device": "SUBCKT", "Sim.Library": UNOFILE, "Sim.Name": "LMV358_A",
          "Sim.Pins": "3=INP 2=INN 1=OUT 8=VCC 4=VEE"},
         {"3": "D13", "2": "L13", "1": "L13", "8": "+5V", "4": "GND"}),
        ("Device:R", "R3", "1k", 274.32, 109.22, {}, {"1": "L13", "2": "L13_A"}),
        ("Device:LED", "D2", "L (jaune)", 274.32, 124.46,
         {"Sim.Device": "SUBCKT", "Sim.Library": UNOFILE, "Sim.Name": "LEDJAUNE",
          "Sim.Pins": "1=K 2=A"},
         {"2": "L13_A", "1": "GND"}),
        # --- Power LED "ON" ---
        ("Device:R", "R4", "1k", 292.1, 109.22, {}, {"1": "+5V", "2": "ON_A"}),
        ("Device:LED", "D3", "ON (vert)", 292.1, 124.46,
         {"Sim.Device": "SUBCKT", "Sim.Library": UNOFILE, "Sim.Name": "LEDVERTE",
          "Sim.Pins": "1=K 2=A"},
         {"2": "ON_A", "1": "GND"}),
        # --- Serial link to the USB-serial converter (1k series resistors) ---
        ("simAVR:USB_SERIE", "U3", "ATmega16U2 + PC", 50.8, 137.16,
         {"Sim.Device": "SUBCKT", "Sim.Library": TERMFILE, "Sim.Name": "USB_SERIE",
          "Sim.Pins": "1=TXD 2=RXD 3=DTR 4=GND"},
         {"1": "USB_TX", "2": "USB_RX", "3": "DTR", "4": "GND"}),
        ("Device:R", "R5", "1k", 83.82, 137.16, {}, {"1": "USB_TX", "2": "D0_RX"}),
        ("Device:R", "R6", "1k", 99.06, 137.16, {}, {"1": "D1_TX", "2": "USB_RX"}),
        # --- 5 V supply of the simulation (regulator NCP1117 / USB on the board) ---
        ("Simulation_SPICE:VDC", "V1", "5V", 50.8, 190.5,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=5"},
         {"1": "+5V!", "2": "GND!"}),
        # --- Headers of the board ---
        ("Connector_Generic:Conn_01x08", "J1", "POWER", 355.6, 55.88, NOSIM,
         {"1": "NC", "2": "+5V", "3": "RESET", "4": "3V3", "5": "+5V", "6": "GND",
          "7": "GND", "8": "VIN"}),
        ("Connector_Generic:Conn_01x06", "J2", "ANALOG IN", 355.6, 86.36, NOSIM,
         {str(k + 1): f"A{k}" for k in range(6)}),
        ("Connector_Generic:Conn_01x08", "J3", "IOL", 355.6, 119.38, NOSIM,
         {"1": "D0_RX", "2": "D1_TX", "3": "D2", "4": "D3", "5": "D4", "6": "D5",
          "7": "D6", "8": "D7"}),
        ("Connector_Generic:Conn_01x10", "J4", "IOH", 355.6, 157.48, NOSIM,
         {"1": "D8", "2": "D9", "3": "D10", "4": "D11", "5": "D12", "6": "D13",
          "7": "GND", "8": "AREF", "9": "A4", "10": "A5"}),
        ("Connector_Generic:Conn_02x03_Odd_Even", "J5", "ICSP", 353.06, 198.12, NOSIM,
         {"1": "D12", "2": "+5V", "3": "D13", "4": "D11", "5": "RESET", "6": "GND"}),
    ]
    TEXTS = [
        ("ATmega328P (modele kisimavr, firmware uno_demo.hex)", 139.7, 66.04),
        ("Decouplage", 134.62, 43.18),
        ("Resonateur 16 MHz (non simule : horloge interne du modele)", 205.74, 40.64),
        ("Reset : tirage 10k, reset auto par DTR, bouton (non simule)", 43.18, 43.18),
        ("LED L sur D13, tamponnee par U5A (suiveur)", 241.3, 96.52),
        ("LED ON", 287.02, 96.52),
        ("Liaison serie : resistances 1k vers l'ATmega16U2", 43.18, 124.46),
        ("U3 : convertisseur USB-serie + terminal du PC (simulation),", 43.18, 160.02),
        ("texte envoye : terminal.txt -> tools/make_terminal_lib.py", 43.18, 163.83),
        ("Alimentation 5 V de la simulation", 43.18, 177.8),
        ("Connecteurs de la carte (non simules)", 337.82, 40.64),
        ("Composants exclus de la simulation : Y2, R1, C3, SW1, J1-J5", 43.18, 223.52),
        ("Sonder : D13, L13, D0_RX (RX), D1_TX (TX). Terminal : uart_log.txt", 43.18, 227.33),
    ]
    DIRECTIVES = [(".tran 0.5u 10m", 43.18, 233.68)]

    # Build stubs, labels, power symbols and no-connects.
    PARTS, WIRES, LABELS, JUNCTIONS = [], [], [], []
    STUB = 2.54
    _pwr = [0]
    _flg = [0]

    def _power(value, x, y, angle):
        lib = "power:+5V" if value == "+5V" else "power:GND"
        _pwr[0] += 1
        PARTS.append((lib, f"#PWR{_pwr[0]:02d}", value, x, y, {"_rot": angle}))

    for lib_id, ref, value, X, Y, extra, conn in UNO_PARTS:
        PARTS.append((lib_id, ref, value, X, Y, extra))
        seen = set()
        for pin in LIB[lib_id]["pins"]:
            num = pin[0]
            px, py, pang = pin_xy(X, Y, pin, extra.get("_rot", 0))
            if (px, py) in seen:
                continue                 # Stacked pins (GND 8 and 22).
            seen.add((px, py))
            net = conn.get(num)
            if net is None:
                raise SystemExit(f"{ref} pin {num} not connected")
            if net == "NC":
                NO_CONNECTS.append((px, py))
                continue
            # Wire away from the body: pin angle 0 -> left, 180 -> right,
            # 90 -> down (screen), 270 -> up.
            dx, dy = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[pang]
            ex, ey = round(px + dx * STUB, 2), round(py + dy * STUB, 2)
            WIRES.append(((px, py), (ex, ey)))
            flag = net.endswith("!")
            net = net.rstrip("!")
            if net in ("+5V", "GND"):
                # Symbol pointing away from the stub (rotation counter-clockwise).
                up = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[(dx, dy)]
                _power(net, ex, ey, up if net == "+5V" else (up + 180) % 360)
                if flag:
                    _flg[0] += 1
                    PARTS.append(("power:PWR_FLAG", f"#FLG{_flg[0]:02d}", "PWR_FLAG", ex, ey,
                                  {"_rot": (up + 90) % 360 if net == "+5V" else (up + 270) % 360}))
            else:
                LABELS.append((net, ex, ey, 180 if dx < 0 else 0))
    NO_CONNECT_REF = None


def placed_symbol(lib_id, ref, value, x, y, extra):
    d = LIB[lib_id]
    extra = dict(extra)
    rot = extra.pop("_rot", 0)
    nosim = extra.pop("_nosim", False)
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
        f'(symbol (lib_id "{lib_id}") (at {x} {y} {rot}) (unit 1) '
        f'(exclude_from_sim {"yes" if nosim else "no"}) '
        f'(in_bom {board}) (on_board {board}) (dnp no) (uuid "{uid()}") {" ".join(props)} {pins} '
        f'(instances (project "{PROJECT}" (path "/{ROOT_UUID}" '
        f'(reference "{ref}") (unit 1)))))')


def schematic():
    parts = [f'(kicad_sch (version 20231120) (generator "eeschema") '
             f'(generator_version "8.0") (uuid "{ROOT_UUID}") (paper "{PAPER}")']
    if TITLE:
        comments = " ".join(f'(comment {i + 1} "{c}")' for i, c in enumerate(TITLE[1:]))
        parts.append(f'(title_block (title "{TITLE[0]}") {comments})')
    used_ids = {p[0] for p in PARTS}
    parts.append("(lib_symbols " + " ".join(lib_symbol(k, v) for k, v in LIB.items()
                                           if k in used_ids) + ")")
    for (x, y) in JUNCTIONS:
        parts.append(f'(junction (at {x} {y}) (diameter 0) (color 0 0 0 0) (uuid "{uid()}"))')
    for (a, b) in WIRES:
        parts.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) '
                     f'(stroke (width 0) (type default)) (uuid "{uid()}"))')
    for (x, y) in NO_CONNECTS:
        parts.append(f'(no_connect (at {x} {y}) (uuid "{uid()}"))')
    if globals().get("NO_CONNECT_REF"):
        used = {(round(x, 2), round(y, 2)) for a, b in WIRES for (x, y) in (a, b)}
        for lib_id, ref, _v, X, Y, _e in PARTS:
            if ref != NO_CONNECT_REF:
                continue
            for pin in LIB[lib_id]["pins"]:
                pt = (round(X + pin[2], 2), round(Y - pin[3], 2))
                if pt not in used:
                    parts.append(f'(no_connect (at {pt[0]} {pt[1]}) (uuid "{uid()}"))')
    for label in LABELS:
        name, x, y = label[:3]
        angle = label[3] if len(label) > 3 else 0
        just = "right bottom" if angle == 180 else "left bottom"
        parts.append(f'(label "{name}" (at {x} {y} {angle}) (fields_autoplaced yes) '
                     f'{font(justify=just)} (uuid "{uid()}"))')
    for (text, x, y) in TEXTS:
        parts.append(f'(text "{text}" (exclude_from_sim yes) (at {x} {y} 0) '
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
    for lib_id, ref, _v, x, y, extra in PARTS:
        for pin in LIB[lib_id]["pins"]:
            px, py, _a = pin_xy(x, y, pin, extra.get("_rot", 0))
            name = f"{ref}.{pin[1]}" if not LIB[lib_id].get("power") else f"<{_v}>"
            pts.setdefault((px, py), []).append(name)

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
    label_pts = {(round(l[1], 2), round(l[2], 2)) for l in LABELS}
    for p, n in ends.items():
        if (p not in pts and n < 2 and p not in label_pts
                and p not in [(round(j[0], 2), round(j[1], 2)) for j in JUNCTIONS]):
            raise SystemExit(f"dangling wire end at {p}")
    for p in pts:
        find(p)
    # Same label name or same power symbol value: same net.
    for label in LABELS:
        union((round(label[1], 2), round(label[2], 2)), ("label", label[0]))
    for lib_id, _r, value, x, y, _e in PARTS:
        if LIB[lib_id].get("power") and lib_id != "power:PWR_FLAG":
            union((round(x, 2), round(y, 2)), ("power", value))
    for lib_id, _r, value, x, y, _e in PARTS:
        if lib_id == "power:PWR_FLAG":
            if find((round(x, 2), round(y, 2))) == (round(x, 2), round(y, 2)) and \
                    len(pts[(round(x, 2), round(y, 2))]) < 2:
                raise SystemExit(f"PWR_FLAG at {x},{y} not connected")
    nets = {}
    for p, names in pts.items():
        nets.setdefault(find(p), []).extend(names)
    names_of = {}
    for label in LABELS:
        names_of.setdefault(find((round(label[1], 2), round(label[2], 2))), set()).add(label[0])
    for root, members in nets.items():
        title = ",".join(sorted(names_of.get(root, []))) or "-"
        print(f"net {title}:", sorted(n for n in members if not n.startswith("<")))
    for root, names in names_of.items():
        if len(names) > 1:
            raise SystemExit(f"labels {sorted(names)} short-circuited")
    for label in LABELS:
        _n, x, y = label[:3]
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


UNO_MODELS = """\
* Models of the Arduino UNO demo (kisimavr).

* One op-amp of the LMV358 (unit A), simple macro-model:
* open-loop gain 1e5, gain-bandwidth 1 MHz (pole at 10 Hz), output clamped
* to the rails (rail-to-rail output), ideal inputs.
* Pins: non-inverting, inverting, output, V+, V-
.subckt LMV358_A INP INN OUT VCC VEE
B1 N1 0 V = max(min(1e5*(V(INP)-V(INN)), V(VCC)+1), V(VEE)-1)
R1 N1 N2 1k
C1 N2 0 15.9u
B2 OUT 0 V = max(min(V(N2), V(VCC)), V(VEE))
.ends LMV358_A

* Yellow LED (LED "L"), about 1.8 V at 3 mA (pins: anode cathode)
.subckt LEDJAUNE A K
D1 A K DLEDJAUNE
.model DLEDJAUNE D(IS=1e-18 RS=10 N=2 BV=5 IBV=10u CJO=10p)
.ends LEDJAUNE

* Green LED (LED "ON"), about 2.0 V at 3 mA (pins: anode cathode)
.subckt LEDVERTE A K
D1 A K DLEDVERTE
.model DLEDVERTE D(IS=1e-19 RS=10 N=2 BV=5 IBV=10u CJO=10p)
.ends LEDVERTE
"""

UNO_TERMINAL = r"""# Text sent by the PC terminal to the Arduino UNO (115200 bauds).
# One line per send: <time> <text>   (escapes: \n \r \s \xHH)
# After a change: python3 tools/make_terminal_lib.py terminal.txt -o terminal.lib
2m Bonjour\n
4.5m BLINK OFF\n
7m BLINK ON\n
"""


def write_uno_models():
    """uno_models.lib (op-amp, LEDs), terminal.txt (kept if present), terminal.lib."""
    with open(os.path.join(OUT, UNOFILE), "w") as f:
        f.write(UNO_MODELS)
    script = os.path.join(OUT, "terminal.txt")
    if not os.path.exists(script):
        with open(script, "w") as f:
            f.write(UNO_TERMINAL)
    import subprocess
    subprocess.run([sys.executable,
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_terminal_lib.py"),
                    script, "-o", os.path.join(OUT, TERMFILE)], check=True)


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

    if VARIANT == "uno":
        write_uno_models()

    with open(sys.argv[2]) as f:
        lib = f.read()
    lib = lib.replace('"../', f'"{KISIMAVR}/')
    with open(os.path.join(OUT, LIBFILE), "w") as f:
        f.write(lib)
    print("written to", OUT)


if __name__ == "__main__":
    main()
