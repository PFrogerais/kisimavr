#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the wired KiCad demo projects of kisimavr.

Usage: python3 tools/gen_kicad_demo.py OUT_DIR SOURCE_LIB VARIANT [--kisimavr DIR]
  VARIANT: full or i2c (official ATmega328P-P, DIP-28), gpio, adc, uart,
           uno (Arduino UNO R3 board around the ATmega328P, official schematic),
           spi (SPI master driving a 74HC595 and 8 LEDs, official symbols),
           lcd (character LCD 16x2 HD44780 + voltage on A0, official symbols)
  --kisimavr: absolute path of the kisimavr folder written into the .lib
              (default: the folder containing this script's parent)

Every part is placed unrotated so pin positions are simple:
schematic pin = (X + px, Y - py) for a library pin at (px, py).
"""

import json
import os
import re
import sys
import uuid

VARIANT = sys.argv[3] if len(sys.argv) > 3 else "gpio"
PROJECT = {"gpio": "simAVR_demo", "adc": "simAVR_adc", "uart": "simAVR_uart",
           "full": "simAVR_atmega328p", "i2c": "simAVR_i2c",
           "uno": "Arduino_UNO", "spi": "simAVR_spi",
           "lcd": "simAVR_lcd"}[VARIANT]
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
    "74xx:74HC595": "Package_DIP:DIP-16_W7.62mm",
    "Display_Character:WC1602A": "Display:WC1602A",
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
SHEET_UUID = str(uuid.uuid4())         # Sheet symbol of a sub-sheet (UNO project).
SUB_UUID = str(uuid.uuid4())           # File uuid of that sub-sheet.
INSTANCE_PATH = f"/{ROOT_UUID}"        # Sheet path of the symbols being written.
HLABELS = []                           # Hierarchical labels: (name, x, y, angle)
POWER_TIES = []                        # (hierarchical name, power net) to tie.


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
    # Official KiCad symbol Display_Character:WC1602A (16x2 module, 16 pins).
    "Display_Character:WC1602A": dict(
        ref="DS", value="WC1602A",
        body='(rectangle (start -7.62 17.78) (end 7.62 -17.78) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "VSS", 0.0, -20.32, 90, "power_in"),
              ("2", "VDD", 0.0, 20.32, 270, "power_in"),
              ("3", "VO", 10.16, 15.24, 180, "input"),
              ("4", "RS", -10.16, 10.16, 0, "input"),
              ("5", "R/W", -10.16, 12.7, 0, "input"),
              ("6", "E", -10.16, 15.24, 0, "input"),
              ("7", "D0", -10.16, 2.54, 0, "input"),
              ("8", "D1", -10.16, 0.0, 0, "input"),
              ("9", "D2", -10.16, -2.54, 0, "input"),
              ("10", "D3", -10.16, -5.08, 0, "input"),
              ("11", "D4", -10.16, -7.62, 0, "input"),
              ("12", "D5", -10.16, -10.16, 0, "input"),
              ("13", "D6", -10.16, -12.7, 0, "input"),
              ("14", "D7", -10.16, -15.24, 0, "input"),
              ("15", "LED(+)", 10.16, 7.62, 180, "power_in"),
              ("16", "LED(-)", 10.16, 5.08, 180, "power_in")],
        pin_names=True,
        description="LCD 16x2 Alphanumeric , 8 bit parallel bus, 5V VDD",
        keywords="display LCD dot-matrix",
        fp_filters="*WC*1602A*"),
    # Official KiCad symbol 74xx:74HC595 (pins and outline converted from
    # the KiCad library).
    "74xx:74HC595": dict(
        ref="U", value="74HC595",
        body='(rectangle (start -7.62 12.7) (end 7.62 -15.24) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("1", "QB", 10.16, 7.62, 180, "tri_state"),
              ("2", "QC", 10.16, 5.08, 180, "tri_state"),
              ("3", "QD", 10.16, 2.54, 180, "tri_state"),
              ("4", "QE", 10.16, 0.0, 180, "tri_state"),
              ("5", "QF", 10.16, -2.54, 180, "tri_state"),
              ("6", "QG", 10.16, -5.08, 180, "tri_state"),
              ("7", "QH", 10.16, -7.62, 180, "tri_state"),
              ("8", "GND", 0.0, -17.78, 90, "power_in"),
              ("9", "QH'", 10.16, -12.7, 180, "output"),
              ("10", "~{SRCLR}", -10.16, 2.54, 0, "input"),
              ("11", "SRCLK", -10.16, 5.08, 0, "input"),
              ("12", "RCLK", -10.16, -2.54, 0, "input"),
              ("13", "~{OE}", -10.16, -5.08, 0, "input"),
              ("14", "SER", -10.16, 10.16, 0, "input"),
              ("15", "QA", 10.16, 10.16, 180, "tri_state"),
              ("16", "VCC", 0.0, 15.24, 270, "power_in")],
        pin_names=True,
        description="8-bit serial in, parallel out Shift Register 3-State Outputs",
        keywords="HCMOS SR 3State",
        fp_filters="DIP*W7.62mm* SOIC*3.9x9.9mm*P1.27mm* TSSOP*4.4x5mm*P0.65mm*"),
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


def lib_fields(d):
    """Reference / Value of the library symbol (y up), bottom left for ICs."""
    if is_ic(d):
        left, bottom = body_bottom_left(d)
        j = font(justify="left bottom")
        return (f'(property "Reference" "{d["ref"]}" (at {left} {round(bottom - 2.54, 2)} 0) {j}) '
                f'(property "Value" "{d["value"]}" (at {left} {round(bottom - 5.08, 2)} 0) {j}) ')
    return (f'(property "Reference" "{d["ref"]}" (at 0 7.62 0) {font()}) '
            f'(property "Value" "{d["value"]}" (at 0 -7.62 0) {font()}) ')


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
        + lib_fields(d)
        + f'(property "Footprint" "{FOOTPRINTS.get(lib_id, "")}" (at 0 0 0) {font(hide=True)}) '
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

LIBFILE = ("atmega328p_full.lib" if VARIANT in ("full", "i2c", "uno", "spi", "lcd")
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


def build_stubbed(spec, hier=None):
    """Parts from (lib_id, ref, value, X, Y, extra, {pin: net}).

    Every pin gets a 2.54 mm stub ending on a net label, or on a +5V / GND
    power symbol ("+5V!" / "GND!" also adds a PWR_FLAG); "NC" = no-connect
    flag; a label net ending with "!" also gets a PWR_FLAG (power input fed
    through a resistor); the key "*" gives the net of the pins not listed.
    hier: {(ref, pin): name}: those pins also reach a hierarchical label
    "name" at the end of a longer stub (power pins: only the hierarchical
    label; POWER_TIES then gets (name, power net) to join them elsewhere).
    Labels go to HLABELS.
    Returns PARTS, WIRES, LABELS, NO_CONNECTS.
    """
    hier = hier or {}
    parts, wires, labels, ncs = [], [], [], []
    stub = 2.54
    count = {"pwr": 0, "flg": 0}

    for lib_id, ref, value, X, Y, extra, conn in spec:
        parts.append((lib_id, ref, value, X, Y, extra))
        seen = set()
        for pin in LIB[lib_id]["pins"]:
            num = pin[0]
            px, py, pang = pin_xy(X, Y, pin, extra.get("_rot", 0))
            if (px, py) in seen:
                continue                 # Stacked pins (GND 8 and 22).
            seen.add((px, py))
            net = conn.get(num, conn.get("*"))
            if net is None:
                raise SystemExit(f"{ref} pin {num} not connected")
            if net == "NC":
                ncs.append((px, py))
                continue
            # Wire away from the body: pin angle 0 -> left, 180 -> right,
            # 90 -> down (screen), 270 -> up.
            dx, dy = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[pang]
            hname = hier.get((ref, num))
            if hname is not None:
                # Long stub to a hierarchical label; the net name (local label
                # or power symbol) stays next to the pin.
                hx, hy = round(px + dx * 4 * stub, 2), round(py + dy * 4 * stub, 2)
                HLABELS.append((hname, hx, hy, 180 if dx < 0 else 0))
                if net in ("+5V", "GND"):
                    wires.append(((px, py), (hx, hy)))
                    POWER_TIES.append((hname, net))
                else:
                    # Two segments: the local label sits on their common end.
                    lx, ly = round(px + dx * stub, 2), round(py + dy * stub, 2)
                    wires.append(((px, py), (lx, ly)))
                    wires.append(((lx, ly), (hx, hy)))
                    labels.append((net, lx, ly, 180 if dx < 0 else 0))
                continue
            ex, ey = round(px + dx * stub, 2), round(py + dy * stub, 2)
            wires.append(((px, py), (ex, ey)))
            flag = net.endswith("!")
            net = net.rstrip("!")
            if net in ("+5V", "GND"):
                # Symbol pointing away from the stub (rotation counter-clockwise).
                up = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[(dx, dy)]
                count["pwr"] += 1
                parts.append(("power:+5V" if net == "+5V" else "power:GND",
                              f"#PWR{count['pwr']:02d}", net, ex, ey,
                              {"_rot": up if net == "+5V" else (up + 180) % 360}))
                if flag:
                    count["flg"] += 1
                    parts.append(("power:PWR_FLAG", f"#FLG{count['flg']:02d}", "PWR_FLAG", ex, ey,
                                  {"_rot": (up + 90) % 360 if net == "+5V" else (up + 270) % 360}))
            else:
                labels.append((net, ex, ey, 180 if dx < 0 else 0))
                if flag:
                    # Power input fed through a part (e.g. a backlight resistor).
                    count["flg"] += 1
                    parts.append(("power:PWR_FLAG", f"#FLG{count['flg']:02d}", "PWR_FLAG", ex, ey,
                                  {"_rot": 0 if dy == 0 else 90}))
    return parts, wires, labels, ncs


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
    DIRECTIVES = []    # On the root sheet (Arduino_UNO.kicad_sch).

    # The board is a sub-sheet: every header pin also reaches a
    # hierarchical label, i.e. a pin of the "Arduino UNO R3" sheet symbol.
    UNO_SUBFILE = "arduino_uno_r3.kicad_sch"
    UNO_HEADERS = {
        "J1": ["NC", "IOREF", "RESET", "3V3", "5V", "GND1", "GND2", "VIN"],
        "J2": ["A0", "A1", "A2", "A3", "A4", "A5"],
        "J3": ["D0_RX", "D1_TX", "D2", "D3", "D4", "D5", "D6", "D7"],
        "J4": ["D8", "D9", "D10", "D11", "D12", "D13", "GND3", "AREF", "SDA", "SCL"],
    }
    UNO_HIER = {(ref, str(k + 1)): name for ref, names in UNO_HEADERS.items()
                for k, name in enumerate(names) if name != "NC"}
    PARTS, WIRES, LABELS, NO_CONNECTS = build_stubbed(UNO_PARTS, UNO_HIER)
    # Power pins of the headers: hierarchical label tied to the power net.
    for k, (hname, net) in enumerate(POWER_TIES):
        tx, ty = 381.0, round(190.5 + 5.08 * k, 2)
        HLABELS.append((hname, tx, ty, 180))
        WIRES.append(((tx, ty), (round(tx + 5.08, 2), ty)))
        PARTS.append(("power:+5V" if net == "+5V" else "power:GND", f"#PWR{90 + k}", net,
                      round(tx + 5.08, 2), ty, {"_rot": 270 if net == "+5V" else 90}))
    TEXTS.append(("Broches d'alimentation des connecteurs", 375.92, 184.15))
    JUNCTIONS = []
    NO_CONNECT_REF = None


def uno_root_sheet():
    """Root sheet of Arduino_UNO: the board as a sheet symbol shaped like an
    UNO R3 (top view, USB on the left), header pins at their real positions
    (scale 2.5: 0.1 inch = 6.35 mm)."""
    x0, y0 = 63.5, 38.1                    # Upper left corner of the board.
    w, h = 171.45, 133.35                  # 2.7 x 2.1 inches.

    def bx(inch):
        return round(x0 + inch * 63.5, 2)

    def by(inch_from_bottom):
        return round(y0 + h - inch_from_bottom * 63.5, 2)

    top = (["SCL", "SDA", "AREF", "GND3", "D13", "D12", "D11", "D10", "D9", "D8"],
           0.84)                           # IOH from 0.84 in, then IOL from 1.90 in.
    pins = []
    for k, name in enumerate(top[0]):
        pins.append((name, bx(top[1] + 0.1 * k), y0, 90))
    for k, name in enumerate(["D7", "D6", "D5", "D4", "D3", "D2", "D1_TX", "D0_RX"]):
        pins.append((name, bx(1.90 + 0.1 * k), y0, 90))
    for k, name in enumerate(["IOREF", "RESET", "3V3", "5V", "GND1", "GND2", "VIN"]):
        pins.append((name, bx(1.14 + 0.1 * k), y0 + h, 270))    # NC at 1.04 in.
    for k, name in enumerate(["A0", "A1", "A2", "A3", "A4", "A5"]):
        pins.append((name, bx(1.94 + 0.1 * k), y0 + h, 270))

    out = [f'(kicad_sch (version 20231120) (generator "eeschema") '
           f'(generator_version "8.0") (uuid "{ROOT_UUID}") (paper "A4")']
    out.append(f'(title_block (title "Arduino UNO R3 (simulation kisimavr)") '
               f'(comment 1 "Carte complete : feuille {UNO_SUBFILE}") '
               f'(comment 2 "Cablez vos composants sur les broches du bloc"))')
    out.append("(lib_symbols)")

    def rect(xa, ya, xb, yb, dash=False):
        kind = "dash" if dash else "default"
        out.append(f'(rectangle (start {xa} {ya}) (end {xb} {yb}) '
                   f'(stroke (width 0.254) (type {kind})) (fill (type none)) (uuid "{uid()}"))')

    def text(t, x, y, size=1.27, just="left bottom"):
        out.append(f'(text "{t}" (exclude_from_sim yes) (at {x} {y} 0) '
                   f'(effects (font (size {size} {size})) (justify {just})) (uuid "{uid()}"))')

    # Board details, inside the sheet: USB B, power jack, headers, MCU, reset.
    rect(round(x0 - 6.35, 2), by(1.65), bx(0.45), by(1.17))          # USB B
    text("USB", bx(0.0), by(1.39))
    rect(round(x0 - 1.27, 2), by(0.55), bx(0.55), by(0.10))          # DC jack
    text("7-12 V", bx(0.0), by(0.30))
    for (first, n, y) in ((0.84, 10, by(2.05)), (1.90, 8, by(2.05)),
                          (1.04, 8, by(0.05)), (1.94, 6, by(0.05))):
        rect(round(bx(first) - 1.905, 2), round(y - 1.905, 2),
             round(bx(first + 0.1 * (n - 1)) + 1.905, 2), round(y + 1.905, 2), dash=True)
    rect(bx(1.30), by(1.05), bx(2.45), by(0.70))                     # ATmega328P DIP-28
    text("ATmega328P", bx(1.62), by(0.83))
    out.append(f'(circle (center {bx(0.55)} {by(1.90)}) (radius 2.54) '
               f'(stroke (width 0.254) (type default)) (fill (type none)) (uuid "{uid()}"))')
    text("RESET", round(bx(0.55) + 3.81, 2), round(by(1.90) + 1.27, 2))
    text("ARDUINO UNO R3", bx(1.05), by(1.35), size=3.0)
    text("DIGITAL (PWM ~ : D3 D5 D6 D9 D10 D11)", bx(1.30), round(y0 + 13.97, 2))
    text("POWER", bx(1.04), round(y0 + h - 13.97, 2))
    text("ANALOG IN", bx(1.94), round(y0 + h - 13.97, 2))

    # Each sheet pin: short wire and a label of the same name, to wire the
    # user's parts by name (an unconnected sheet pin is an ERC error).
    for (n, x, y, a) in pins:
        ey = round(y - 5.08, 2) if a == 90 else round(y + 5.08, 2)
        out.append(f'(wire (pts (xy {x} {y}) (xy {x} {ey})) '
                   f'(stroke (width 0) (type default)) (uuid "{uid()}"))')
        out.append(f'(label "{n}" (at {x} {ey} {a}) (fields_autoplaced yes) '
                   f'(effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid()}"))')

    pin_txt = " ".join(
        f'(pin "{n}" bidirectional (at {x} {y} {a}) '
        f'(effects (font (size 1.27 1.27)) (justify {"right" if a == 90 else "left"})) '
        f'(uuid "{uid()}"))' for (n, x, y, a) in pins)
    out.append(
        f'(sheet (at {x0} {y0}) (size {w} {h}) (fields_autoplaced yes) '
        f'(stroke (width 0.254) (type solid)) (fill (color 0 0 0 0.0000)) (uuid "{SHEET_UUID}") '
        f'(property "Sheetname" "Arduino UNO R3" (at {x0} {round(y0 - 0.7, 2)} 0) '
        f'(effects (font (size 1.27 1.27)) (justify left bottom))) '
        f'(property "Sheetfile" "{UNO_SUBFILE}" (at {x0} {round(y0 + h + 0.6, 2)} 0) '
        f'(effects (font (size 1.27 1.27)) (justify left top))) '
        f'{pin_txt} '
        f'(instances (project "{PROJECT}" (path "/{ROOT_UUID}" (page "2")))))')
    text("Simulation : transitoire 10 ms. Terminal simule : terminal.txt -> "
         "tools/make_terminal_lib.py", 25.4, 190.5)
    text("Chaque broche porte un label : placer le meme label sur un composant pour le relier",
         25.4, 186.69)
    text("Sonder : /Arduino UNO R3/D13, L13, D0_RX, D1_TX (ou un net de cette feuille)",
         25.4, 194.31)
    out.append(f'(text ".tran 0.5u 10m" (exclude_from_sim no) (at 25.4 200.66 0) '
               f'(effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid()}"))')
    out.append('(sheet_instances (path "/" (page "1")))')
    out.append(")")
    return "\n".join(out) + "\n"


if VARIANT == "lcd":
    SIM_PINS = ("2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 "
                "16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5")
    LCDFILE = "lcd1602.lib"
    LCD_PARTS = [
        ("MCU_Microchip_ATmega:ATmega328P-P", "U1", "ATmega328P-P", 76.2, 99.06,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P", "Sim.Pins": SIM_PINS},
         {"7": "+5V", "20": "+5V", "8": "GND", "*": "NC",
          "18": "LCD_RS", "17": "LCD_E", "11": "LCD_D4", "6": "LCD_D5", "5": "LCD_D6",
          "4": "LCD_D7", "23": "A0"}),
        ("Display_Character:WC1602A", "DS1", "WC1602A", 165.1, 93.98,
         {"Sim.Device": "SUBCKT", "Sim.Library": LCDFILE, "Sim.Name": "LCD1602",
          "Sim.Pins": "1=VSS 2=VDD 3=VO 4=RS 5=RW 6=E 7=D0 8=D1 9=D2 10=D3 11=D4 "
                      "12=D5 13=D6 14=D7 15=LEDP 16=LEDN"},
         {"1": "GND", "2": "+5V", "3": "VO", "4": "LCD_RS", "5": "GND", "6": "LCD_E",
          "7": "NC", "8": "NC", "9": "NC", "10": "NC",
          "11": "LCD_D4", "12": "LCD_D5", "13": "LCD_D6", "14": "LCD_D7",
          "15": "BL_A!", "16": "GND"}),
        # Contrast: divider in place of the usual 10k potentiometer.
        ("Device:R", "R1", "4.7k", 195.58, 63.5, {}, {"1": "+5V", "2": "VO"}),
        ("Device:R", "R2", "4.7k", 195.58, 81.28, {}, {"1": "VO", "2": "GND"}),
        # Backlight
        ("Device:R", "R3", "220", 208.28, 76.2, {}, {"1": "+5V", "2": "BL_A"}),
        # "Potentiometer" on A0: 0.5 V -> 4.5 V ramp through 10k
        ("Device:R", "R4", "10k", 119.38, 132.08, {}, {"1": "A0", "2": "POT"}),
        ("Simulation_SPICE:VPULSE", "V2", "POT", 119.38, 149.86,
         {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "y1=0.5 y2=4.5 td=0 tr=30m tf=1u tw=1 per=2"},
         {"1": "POT", "2": "GND"}),
        ("Simulation_SPICE:VDC", "V1", "5V", 30.48, 160.02,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=5"},
         {"1": "+5V!", "2": "GND!"}),
    ]
    PARTS, WIRES, LABELS, NO_CONNECTS = build_stubbed(LCD_PARTS)
    JUNCTIONS = []
    NO_CONNECT_REF = None
    TEXTS = [
        ("Afficheur LCD 16x2 (HD44780), interface 4 bits, cablage LiquidCrystal(12, 11, 5, 4, 3, 2)",
         25.4, 30.48),
        ("RS = D12, E = D11, D4..D7 = D5, D4, D3, D2 ; R/W a la masse", 25.4, 34.29),
        ("Ecran affiche dans la console (LCD> [...]) et dans lcd_log.txt", 25.4, 38.1),
        ("Contraste", 190.5, 50.8),
        ("Retroeclairage", 203.2, 66.04),
        ("Alimentation 5 V de la simulation", 22.86, 147.32),
    ]
    DIRECTIVES = [(".tran 0.5u 30m", 25.4, 185.42)]


if VARIANT == "spi":
    SIM_PINS = ("2=PD0 3=PD1 4=PD2 5=PD3 6=PD4 11=PD5 12=PD6 13=PD7 14=PB0 15=PB1 "
                "16=PB2 17=PB3 18=PB4 19=PB5 23=PC0 24=PC1 25=PC2 26=PC3 27=PC4 28=PC5")
    HC595FILE = "74hc595.lib"
    SPI_PARTS = [
        ("MCU_Microchip_ATmega:ATmega328P-P", "U1", "ATmega328P-P", 76.2, 99.06,
         {"Sim.Device": "SUBCKT", "Sim.Library": LIBFILE,
          "Sim.Name": "ATMEGA328P", "Sim.Pins": SIM_PINS},
         {"7": "+5V", "20": "+5V", "8": "GND", "*": "NC",
          "19": "SCK", "17": "MOSI", "18": "MISO", "16": "RCLK", "3": "TXD"}),
        ("74xx:74HC595", "U2", "74HC595", 139.7, 91.44,
         {"Sim.Device": "SUBCKT", "Sim.Library": HC595FILE, "Sim.Name": "74HC595",
          "Sim.Pins": "1=QB 2=QC 3=QD 4=QE 5=QF 6=QG 7=QH 8=VSS 9=QHS 10=SRCLR_N "
                      "11=SRCLK 12=RCLK 13=OE_N 14=SER 15=QA 16=VCC"},
         {"14": "MOSI", "11": "SCK", "10": "+5V", "12": "RCLK", "13": "GND",
          "9": "MISO", "16": "+5V", "8": "GND",
          "15": "QA", "1": "QB", "2": "QC", "3": "QD", "4": "QE", "5": "QF", "6": "QG",
          "7": "QH"}),
        ("Device:R", "R9", "100k", 106.68, 129.54, {}, {"1": "TXD", "2": "GND"}),
        ("Simulation_SPICE:VDC", "V1", "5V", 30.48, 160.02,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=5"},
         {"1": "+5V!", "2": "GND!"}),
    ]
    for k, q in enumerate("ABCDEFGH"):
        x = 180.34 + 12.7 * k
        SPI_PARTS.append(("Device:R", f"R{k + 1}", "330", x, 81.28, {},
                          {"1": f"Q{q}", "2": f"L{q}"}))
        SPI_PARTS.append(("Device:LED", f"D{k + 1}", "LEDRED", x, 99.06,
                          {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE,
                           "Sim.Name": "LEDRED", "Sim.Pins": "1=K 2=A"},
                          {"2": f"L{q}", "1": "GND"}))
    PARTS, WIRES, LABELS, NO_CONNECTS = build_stubbed(SPI_PARTS)
    JUNCTIONS = []
    NO_CONNECT_REF = None
    TEXTS = [
        ("SPI maitre : D13 SCK -> SRCLK, D11 MOSI -> SER, D10 -> RCLK (verrou)", 25.4, 30.48),
        ("D12 MISO <- QH' : relit l'octet precedent (firmware spi_595.hex)", 25.4, 34.29),
        ("Chenillard sur 8 LED ; terminal : uart_log.txt, journal SPI> dans la console", 25.4, 38.1),
        ("Sonder : SCK, MOSI, RCLK, MISO (zoom sur 20 us), QA..QH", 25.4, 41.91),
        ("Alimentation 5 V de la simulation", 22.86, 147.32),
    ]
    DIRECTIVES = [(".tran 0.5u 20m", 25.4, 185.42)]


def body_bottom_left(d):
    """Lower left corner of a symbol body (library coordinates, y up)."""
    nums = re.findall(r"\((?:xy|start|end) (-?[0-9.]+) (-?[0-9.]+)\)", d["body"])
    xs = [float(a) for a, _ in nums]
    ys = [float(b) for _, b in nums]
    return min(xs), min(ys)


def is_ic(d):
    """Integrated circuits (MCU, logic, display...): reference "U" or "DS"."""
    return not d.get("power") and d["ref"] in ("U", "DS")


def field_positions(d, x, y):
    """Schematic positions and justification of Reference and Value.

    ICs: both under the body, aligned on its left edge (bottom left);
    other parts: on the right of the symbol, as before.
    """
    if is_ic(d):
        left, bottom = body_bottom_left(d)
        bx, by = round(x + left, 2), round(y - bottom, 2)
        return (bx, round(by + 2.54, 2)), (bx, round(by + 5.08, 2)), "left bottom"
    return (x + 3.81, y - 1.27), (x + 3.81, y + 1.27), "left"


def placed_symbol(lib_id, ref, value, x, y, extra):
    d = LIB[lib_id]
    extra = dict(extra)
    rot = extra.pop("_rot", 0)
    nosim = extra.pop("_nosim", False)
    hide_ref = d.get("power", False)
    (rx, ry), (vx, vy), just = field_positions(d, x, y)
    props = [
        f'(property "Reference" "{ref}" (at {rx} {ry} 0) '
        f'{font(hide=hide_ref, justify=just)})',
        f'(property "Value" "{value}" (at {vx} {vy} 0) {font(justify=just)})',
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
        f'(instances (project "{PROJECT}" (path "{INSTANCE_PATH}" '
        f'(reference "{ref}") (unit 1)))))')


def schematic(file_uuid=None, root=True):
    parts = [f'(kicad_sch (version 20231120) (generator "eeschema") '
             f'(generator_version "8.0") (uuid "{file_uuid or ROOT_UUID}") (paper "{PAPER}")']
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
    for (name, x, y, angle) in HLABELS:
        just = "right" if angle == 180 else "left"
        parts.append(f'(hierarchical_label "{name}" (shape bidirectional) (at {x} {y} {angle}) '
                     f'(fields_autoplaced yes) {font(justify=just)} (uuid "{uid()}"))')
    for (text, x, y) in TEXTS:
        parts.append(f'(text "{text}" (exclude_from_sim yes) (at {x} {y} 0) '
                     f'{font(justify="left bottom")} (uuid "{uid()}"))')
    for (text, x, y) in DIRECTIVES:
        parts.append(f'(text "{text}" (exclude_from_sim no) (at {x} {y} 0) '
                     f'{font(justify="left bottom")} (uuid "{uid()}"))')
    for p in PARTS:
        parts.append(placed_symbol(*p))
    if root:
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
    label_pts = {(round(l[1], 2), round(l[2], 2)) for l in LABELS + HLABELS}
    for p, n in ends.items():
        if (p not in pts and n < 2 and p not in label_pts
                and p not in [(round(j[0], 2), round(j[1], 2)) for j in JUNCTIONS]):
            raise SystemExit(f"dangling wire end at {p}")
    for p in pts:
        find(p)
    # Same label name or same power symbol value: same net.
    for label in LABELS:
        union((round(label[1], 2), round(label[2], 2)), ("label", label[0]))
    for label in HLABELS:
        # Same hierarchical name on one sheet: same sheet pin, same net.
        union((round(label[1], 2), round(label[2], 2)), ("hier", label[0]))
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
    sheets = [[ROOT_UUID, "Root"]]
    if VARIANT == "uno":
        global INSTANCE_PATH
        INSTANCE_PATH = f"/{ROOT_UUID}/{SHEET_UUID}"
        sub = schematic(SUB_UUID, root=False)
        assert balanced(sub), "unbalanced s-expression"
        with open(os.path.join(OUT, UNO_SUBFILE), "w") as f:
            f.write(sub)
        sch = uno_root_sheet()
        sheets.append([SHEET_UUID, "Arduino UNO R3"])
    else:
        sch = schematic()
    assert balanced(sch), "unbalanced s-expression"
    with open(os.path.join(OUT, PROJECT + ".kicad_sch"), "w") as f:
        f.write(sch)

    pro = {"meta": {"filename": PROJECT + ".kicad_pro", "version": 1},
           "sheets": sheets}
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
    if VARIANT == "spi":
        here = os.path.dirname(os.path.abspath(__file__))
        with open(os.path.join(here, "..", "kicad", HC595FILE)) as f:
            model = f.read()
        with open(os.path.join(OUT, HC595FILE), "w") as f:
            f.write(model)

    with open(sys.argv[2]) as f:
        lib = f.read()
    lib = lib.replace('"../', f'"{KISIMAVR}/')
    with open(os.path.join(OUT, LIBFILE), "w") as f:
        f.write(lib)
    print("written to", OUT)


if __name__ == "__main__":
    main()
