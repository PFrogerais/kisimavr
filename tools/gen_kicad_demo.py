#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the wired KiCad demo projects of kisimavr.

Usage: python3 tools/gen_kicad_demo.py OUT_DIR SOURCE_LIB VARIANT [--kisimavr DIR]
  VARIANT: full or i2c (official ATmega328P-P, DIP-28), gpio, adc, uart,
           uno (Arduino UNO R3 board around the ATmega328P, official schematic),
           spi (SPI master driving a 74HC595 and 8 LEDs, official symbols),
           lcd (character LCD 16x2 HD44780 + voltage on A0, official symbols),
           pico (Raspberry Pi Pico, RP2040: LED, button, ADC, UART terminal),
           picoboard (the Pico board itself after its official schematic, in a
                      sub-sheet; the root sheet shows it as a Pico-shaped block),
           pico2, pico2board (the same for the Raspberry Pi Pico 2, RP2350)
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
           "lcd": "simAVR_lcd", "pico": "simPICO",
           "picoboard": "RaspberryPi_Pico", "pico2": "simPICO2",
           "pico2board": "RaspberryPi_Pico2"}[VARIANT]
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
    "MCU_Module:RaspberryPi_Pico": "Module:RaspberryPi_Pico_Common_Unspecified",
    "simAVR:RT6150B-33GQW": "Package_DFN_QFN:Richtek_UQFN-10_2.6x2.6mm_P0.5mm",
    "Connector_Generic:Conn_01x06": "Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical",
    "Connector_Generic:Conn_01x08": "Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical",
    "Connector_Generic:Conn_01x10": "Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical",
    "Connector_Generic:Conn_02x03_Odd_Even": "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical",
}
DATASHEETS = {
    "MCU_Module:RaspberryPi_Pico": "https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf",
    "MCU_Microchip_ATmega:ATmega328P-P": "http://ww1.microchip.com/downloads/en/DeviceDoc/ATmega328_P%20AVR%20MCU%20with%20picoPower%20Technology%20Data%20Sheet%2040001984A.pdf",
}
PICO2_DATASHEET = "https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf"
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
    "power:+3V3": dict(
        ref="#PWR", value="+3V3", power=True,
        body='(polyline (pts (xy -0.762 1.27) (xy 0 2.54) (xy 0.762 1.27)) '
             '(stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy 0 0) (xy 0 2.54)) '
             '(stroke (width 0) (type default)) (fill (type none)))',
        pins=[("1", "+3V3", 0, 0, 90, "power_in")],
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
    # Richtek RT6150B-33GQW buck-boost converter of the Pico (not in the KiCad
    # library): pins as on the Pico schematic.
    "simAVR:RT6150B-33GQW": dict(
        ref="U", value="RT6150B-33GQW",
        body='(rectangle (start -7.62 7.62) (end 7.62 -7.62) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[("5", "VIN", -10.16, 5.08, 0, "power_in"),
              ("8", "VINA", -10.16, 2.54, 0, "power_in"),
              ("6", "EN", -10.16, -2.54, 0, "input"),
              ("7", "PS", -10.16, -5.08, 0, "input"),
              ("4", "LX1", -2.54, 10.16, 270, "passive"),
              ("2", "LX2", 2.54, 10.16, 270, "passive"),
              ("1", "VOUT", 10.16, 2.54, 180, "power_out"),
              ("10", "FB", 10.16, -2.54, 180, "input"),
              ("3", "GND", 0.0, -10.16, 90, "power_in"),
              ("9", "GND", 0.0, -10.16, 90, "passive", True),
              ("11", "GND_PAD", 0.0, -10.16, 90, "passive", True)],
        pin_names=True,
        description="Buck-boost converter 3.3 V, 1 A (Richtek), as on the Raspberry Pi Pico"),
    # Official KiCad symbol MCU_Module:RaspberryPi_Pico (pins and outline
    # taken from the KiCad 10 library).
    "MCU_Module:RaspberryPi_Pico": dict(
        ref="A", value="RaspberryPi_Pico", pin_length=3.81,
        body='(rectangle (start -19.05 34.29) (end 19.05 -31.75) '
             '(stroke (width 0.254) (type default)) (fill (type background)))',
        pins=[
              ("1", "GPIO0", -22.86, 15.24, 0, "bidirectional"),
              ("2", "GPIO1", -22.86, 12.7, 0, "bidirectional"),
              ("3", "GND", 0.0, -35.56, 90, "power_out"),
              ("4", "GPIO2", -22.86, 10.16, 0, "bidirectional"),
              ("5", "GPIO3", -22.86, 7.62, 0, "bidirectional"),
              ("6", "GPIO4", -22.86, 5.08, 0, "bidirectional"),
              ("7", "GPIO5", -22.86, 2.54, 0, "bidirectional"),
              ("8", "GND", 0.0, -35.56, 90, "passive", True),
              ("9", "GPIO6", -22.86, 0.0, 0, "bidirectional"),
              ("10", "GPIO7", -22.86, -2.54, 0, "bidirectional"),
              ("11", "GPIO8", -22.86, -5.08, 0, "bidirectional"),
              ("12", "GPIO9", -22.86, -7.62, 0, "bidirectional"),
              ("13", "GND", 0.0, -35.56, 90, "passive", True),
              ("14", "GPIO10", -22.86, -10.16, 0, "bidirectional"),
              ("15", "GPIO11", -22.86, -12.7, 0, "bidirectional"),
              ("16", "GPIO12", -22.86, -15.24, 0, "bidirectional"),
              ("17", "GPIO13", -22.86, -17.78, 0, "bidirectional"),
              ("18", "GND", 0.0, -35.56, 90, "passive", True),
              ("19", "GPIO14", -22.86, -20.32, 0, "bidirectional"),
              ("20", "GPIO15", -22.86, -22.86, 0, "bidirectional"),
              ("21", "GPIO16", 22.86, 12.7, 180, "bidirectional"),
              ("22", "GPIO17", 22.86, 10.16, 180, "bidirectional"),
              ("23", "GND", 0.0, -35.56, 90, "passive", True),
              ("24", "GPIO18", 22.86, 7.62, 180, "bidirectional"),
              ("25", "GPIO19", 22.86, 5.08, 180, "bidirectional"),
              ("26", "GPIO20", 22.86, 2.54, 180, "bidirectional"),
              ("27", "GPIO21", 22.86, 0.0, 180, "bidirectional"),
              ("28", "GND", 0.0, -35.56, 90, "passive", True),
              ("29", "GPIO22", 22.86, -2.54, 180, "bidirectional"),
              ("30", "RUN", -22.86, 22.86, 0, "passive"),
              ("31", "GPIO26_ADC0", 22.86, -12.7, 180, "bidirectional"),
              ("32", "GPIO27_ADC1", 22.86, -15.24, 180, "bidirectional"),
              ("33", "AGND", 22.86, -22.86, 180, "power_out"),
              ("34", "GPIO28_ADC2", 22.86, -17.78, 180, "bidirectional"),
              ("35", "ADC_VREF", 22.86, -7.62, 180, "power_in"),
              ("36", "3V3", 5.08, 38.1, 270, "power_out"),
              ("37", "3V3_EN", -22.86, 20.32, 0, "passive"),
              ("38", "GND", 0.0, -35.56, 90, "passive", True),
              ("39", "VSYS", -5.08, 38.1, 270, "power_in"),
              ("40", "VBUS", 0.0, 38.1, 270, "power_out"),],
        pin_names=True,
        description="Versatile and inexpensive microcontroller module powered by RP2040 "
                    "dual-core Arm Cortex-M0+ processor up to 133 MHz, 264kB SRAM, 2MB QSPI flash",
        keywords="RP2040 M0+ usb",
        fp_filters="RaspberryPi?Pico?Common* RaspberryPi?Pico?SMD*"),
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


def load_official_symbols(filename):
    """Official KiCad symbols stored by tools/dump_kicad_symbol.py.

    Returns {lib_id: entry} with the raw s-expression (renamed "Lib:Name",
    derived symbols flattened) and the pins for the stub builder.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kicad_symbols", filename)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    blocks = {}
    for part in re.split(r"^### ", text, flags=re.M)[1:]:
        name, body = part.split("\n", 1)
        blocks[name.strip()] = body.strip()
    out = {}
    for lib_id, body in blocks.items():
        lib, name = lib_id.split(":", 1)
        parent = re.search(r'\(extends "([^"]+)"\)', body)
        if parent:
            pname = parent.group(1)
            flat = blocks[f"{lib}:{pname}"]
            flat = flat.replace(f'(symbol "{pname}"', f'(symbol "{name}"', 1)
            flat = flat.replace(f'(symbol "{pname}_', f'(symbol "{name}_')
            for key, value in re.findall(r'\(property "([\w ]+)" "([^"]*)"', body):
                flat = re.sub(rf'(\(property "{re.escape(key)}" )"[^"]*"',
                              lambda m, v=value: f'{m.group(1)}"{v}"', flat, count=1)
            body = flat
        raw = body.replace(f'(symbol "{name}"', f'(symbol "{lib_id}"', 1)
        pins = []
        for typ, x, y, ang, hide, pname, num in re.findall(
                r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length [\d.]+\)'
                r'(\s*\(hide yes\))?\s*\(name "([^"]*)".*?\(number "([^"]*)"', raw, re.S):
            pins.append((num, pname, float(x), float(y), int(ang), typ, bool(hide)))
        props = dict(re.findall(r'\(property "([\w ]+)" "([^"]*)"', raw))
        out[lib_id] = dict(raw=raw, body=raw, pins=pins, ref=props.get("Reference", "U"),
                           value=props.get("Value", name), power="(power" in raw,
                           footprint=props.get("Footprint", ""),
                           datasheet=props.get("Datasheet", ""), pin_names=True)
    return out


def lib_symbol(lib_id, d):
    if "raw" in d:
        return d["raw"]
    short = lib_id.split(":")[1]
    power = "(power) " if d.get("power") else ""
    pn = "" if d["pin_names"] else "(pin_names (offset 0) hide) "
    if d["pin_names"]:
        pn = "(pin_names (offset 1.016)) "
    pins = []
    for pin in d["pins"]:
        num, name, x, y, ang, typ = pin[:6]
        length = 0 if d.get("power") else d.get("pin_length", 2.54)
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


def build_stubbed(spec, hier=None, vlabels=False):
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

    def label_angle(dx, dy):
        if vlabels and dy != 0:
            return 90 if dy < 0 else 270     # Vertical text along a vertical stub.
        return 180 if dx < 0 else 0
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
                if net in ("+5V", "+3V3", "GND"):
                    wires.append(((px, py), (hx, hy)))
                    POWER_TIES.append((hname, net))
                else:
                    # Two segments: the local label sits on their common end.
                    lx, ly = round(px + dx * stub, 2), round(py + dy * stub, 2)
                    wires.append(((px, py), (lx, ly)))
                    wires.append(((lx, ly), (hx, hy)))
                    labels.append((net, lx, ly, label_angle(dx, dy)))
                continue
            ex, ey = round(px + dx * stub, 2), round(py + dy * stub, 2)
            wires.append(((px, py), (ex, ey)))
            flag = net.endswith("!")
            net = net.rstrip("!")
            if net in ("+5V", "+3V3", "GND"):
                # Symbol pointing away from the stub (rotation counter-clockwise).
                up = {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}[(dx, dy)]
                positive = net != "GND"
                count["pwr"] += 1
                parts.append((f"power:{net}", f"#PWR{count['pwr']:02d}", net, ex, ey,
                              {"_rot": up if positive else (up + 180) % 360}))
                if flag:
                    count["flg"] += 1
                    parts.append(("power:PWR_FLAG", f"#FLG{count['flg']:02d}", "PWR_FLAG", ex, ey,
                                  {"_rot": (up + 90) % 360 if positive else (up + 270) % 360}))
            else:
                labels.append((net, ex, ey, label_angle(dx, dy)))
                if flag:
                    # Power input fed through a part (e.g. a backlight resistor).
                    count["flg"] += 1
                    parts.append(("power:PWR_FLAG", f"#FLG{count['flg']:02d}", "PWR_FLAG", ex, ey,
                                  {"_rot": 0 if dy == 0 else 90}))
    return parts, wires, labels, ncs


PAPER = "A4"
TITLE = None
HIER_SHEET = None    # (sub-sheet file, sheet name, root sheet writer) or None
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
    HIER_SHEET = (UNO_SUBFILE, "Arduino UNO R3", lambda: uno_root_sheet())


def snap(v):
    """Nearest point of the 1.27 mm (50 mil) schematic grid."""
    return round(round(v / 1.27) * 1.27, 2)


def uno_root_sheet():
    """Root sheet of Arduino_UNO: the board as a sheet symbol shaped like an
    UNO R3 (top view, USB on the left), 2.7 x 2.1 in drawn at 42.3 mm per
    inch; header pins at their place, 3.81 mm apart (on the 1.27 mm grid)."""
    x0, y0 = 63.5, 38.1                    # Upper left corner of the board.
    w, h = 114.3, 88.9                     # 2.7 x 2.1 inches.
    K = w / 2.7                            # mm per inch
    P = 3.81                               # Pin pitch

    def bx(inch):
        return snap(x0 + inch * K)

    def by(inch_from_bottom):
        return snap(y0 + h - inch_from_bottom * K)

    pins = []
    for k, name in enumerate(["SCL", "SDA", "AREF", "GND3", "D13", "D12", "D11", "D10", "D9",
                              "D8"]):
        pins.append((name, round(bx(0.84) + P * k, 2), y0, 90))      # IOH from 0.84 in
    for k, name in enumerate(["D7", "D6", "D5", "D4", "D3", "D2", "D1_TX", "D0_RX"]):
        pins.append((name, round(bx(1.90) + P * k, 2), y0, 90))      # IOL from 1.90 in
    for k, name in enumerate(["IOREF", "RESET", "3V3", "5V", "GND1", "GND2", "VIN"]):
        pins.append((name, round(bx(1.04) + P * (k + 1), 2), y0 + h, 270))  # NC at 1.04 in
    for k, name in enumerate(["A0", "A1", "A2", "A3", "A4", "A5"]):
        pins.append((name, round(bx(1.94) + P * k, 2), y0 + h, 270))

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
    rect(round(x0 - 3.81, 2), by(1.65), bx(0.45), by(1.17))          # USB B
    text("USB", bx(0.0), by(1.39))
    rect(round(x0 - 1.27, 2), by(0.55), bx(0.55), by(0.10))          # DC jack
    text("7-12 V", bx(0.0), by(0.30))
    for (first, n, y) in ((bx(0.84), 10, y0 + 1.27), (bx(1.90), 8, y0 + 1.27),
                          (bx(1.04), 8, y0 + h - 1.27), (bx(1.94), 6, y0 + h - 1.27)):
        rect(round(first - 1.27, 2), round(y - 1.27, 2),
             round(first + P * (n - 1) + 1.27, 2), round(y + 1.27, 2), dash=True)
    rect(bx(1.30), by(1.05), bx(2.45), by(0.70))                     # ATmega328P DIP-28
    text("ATmega328P", bx(1.62), by(0.83))
    out.append(f'(circle (center {bx(0.55)} {by(1.90)}) (radius 1.778) '
               f'(stroke (width 0.254) (type default)) (fill (type none)) (uuid "{uid()}"))')
    text("RESET", round(bx(0.55) + 2.54, 2), round(by(1.90) + 1.27, 2))
    text("ARDUINO UNO R3", bx(1.05), by(1.35), size=2.0)
    text("DIGITAL (PWM ~ : D3 D5 D6 D9 D10 D11)", bx(1.30), round(y0 + 8.89, 2))
    text("POWER", bx(1.04), round(y0 + h - 8.89, 2))
    text("ANALOG IN", bx(1.94), round(y0 + h - 8.89, 2))

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
         "tools/make_terminal_lib.py", 25.4, 156.21)
    text("Chaque broche porte un label : placer le meme label sur un composant pour le relier",
         25.4, 152.4)
    text("Sonder : /Arduino UNO R3/D13, L13, D0_RX, D1_TX (ou un net de cette feuille)",
         25.4, 160.02)
    out.append(f'(text ".tran 0.5u 10m" (exclude_from_sim no) (at 25.4 166.37 0) '
               f'(effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid()}"))')
    out.append('(sheet_instances (path "/" (page "1")))')
    out.append(")")
    return "\n".join(out) + "\n"


if VARIANT == "picoboard":
    # Raspberry Pi Pico R3 after its official schematic (RPI-PICO-R3, 2020,
    # datasheet appendix B): references and values of the schematic, official
    # KiCad symbols. Sub-sheet: the board; root: a Pico-shaped sheet symbol.
    LIB.update(load_official_symbols("pico_board.kicad_sym.txt"))
    PAPER = "A3"
    TITLE = ("Raspberry Pi Pico R3 (simulation kisimavr)",
             "D'apres le schema officiel RPI-PICO-R3 (datasheet Raspberry Pi Pico, annexe B)",
             "Simulation : RP2040 = rp2040js ; U2 (RT6150) remplace par V3 ; USB = V2 (5 V)")
    BOARDFILE = "pico_board_models.lib"
    CHIPFILE = "rp2040.lib"
    LIBFILE = CHIPFILE    # main() copies the model given on the command line here.
    PICO_SUBFILE = "pico_board.kicad_sch"
    NOSIM = {"_nosim": True}
    R0402 = "Resistor_SMD:R_0402_1005Metric"
    R0603 = "Resistor_SMD:R_0603_1608Metric"
    C0201 = "Capacitor_SMD:C_0201_0603Metric"
    C0402 = "Capacitor_SMD:C_0402_1005Metric"
    C0805 = "Capacitor_SMD:C_0805_2012Metric"

    def res(ref, value, x, y, net1, net2, fp=R0402, sim=True):
        extra = {"_fp": fp}
        if not sim:
            extra.update(NOSIM)
        return ("Device:R", ref, value, x, y, extra, {"1": net1, "2": net2})

    def cap(ref, value, x, y, net1, net2, fp=C0201, sim=True):
        extra = {"_fp": fp}
        if not sim:
            extra.update(NOSIM)
        return ("Device:C", ref, value, x, y, extra, {"1": net1, "2": net2})

    gpio_pins = {"2": 0, "3": 1, "4": 2, "5": 3, "6": 4, "7": 5, "8": 6, "9": 7, "11": 8,
                 "12": 9, "13": 10, "14": 11, "15": 12, "16": 13, "17": 14, "18": 15,
                 "27": 16, "28": 17, "29": 18, "30": 19, "31": 20, "32": 21, "34": 22,
                 "35": 23, "36": 24, "37": 25, "38": 26, "39": 27, "40": 28, "41": 29}
    gpio_net = {n: (f"GPIO{n}" if n < 26 else f"GPIO{n}_ADC{n - 26}") for n in range(30)}
    u1 = {num: gpio_net[g] for num, g in gpio_pins.items()}
    u1.update({"1": "+3V3", "44": "+3V3", "48": "+3V3", "23": "1V1", "45": "1V1",
               "43": "ADC_VREF!", "57": "GND", "19": "GND", "20": "XIN", "21": "XOUT",
               "24": "SWCLK", "25": "SWDIO", "26": "RUN", "46": "USB_DM", "47": "USB_DP",
               "51": "QSPI_SD3", "52": "QSPI_SCLK", "53": "QSPI_SD0", "54": "QSPI_SD2",
               "55": "QSPI_SD1", "56": "QSPI_SS"})
    chip_sim_pins = " ".join(f"{num}=GPIO{g}" for num, g in
                             sorted(gpio_pins.items(), key=lambda kv: kv[1]))

    BOARD_PARTS = [
        # --- RP2040 (co-simulated) ---
        ("MCU_RaspberryPi:RP2040", "U1", "RP2040", 200.66, 160.02,
         {"Sim.Device": "SUBCKT", "Sim.Library": CHIPFILE, "Sim.Name": "RP2040",
          "Sim.Pins": chip_sim_pins}, u1),
        # --- USB: micro-B connector, 27R series resistors, test points ---
        ("Connector:USB_B_Micro", "J1", "690-005-298-486", 40.64, 60.96,
         dict(NOSIM, _fp="Connector_USB:USB_Micro-B_Molex-105017-0001"),
         {"1": "VBUS", "2": "USB_DMX", "3": "USB_DPX", "4": "NC", "5": "GND", "SH": "GND"}),
        res("R12", "27R", 73.66, 53.34, "USB_DMX", "USB_DM", R0402, sim=False),
        res("R13", "27R", 83.82, 53.34, "USB_DPX", "USB_DP", R0402, sim=False),
        ("Connector:TestPoint", "TP1", "GND", 63.5, 80.01, NOSIM, {"1": "GND"}),
        ("Connector:TestPoint", "TP2", "USB_DM", 73.66, 80.01, NOSIM, {"1": "USB_DMX"}),
        ("Connector:TestPoint", "TP3", "USB_DP", 83.82, 80.01, NOSIM, {"1": "USB_DPX"}),
        # --- Power: VBUS -> D1 -> VSYS -> U2 buck-boost -> 3V3 ---
        ("Device:D_Schottky", "D1", "MBR120VLSFT1G", 124.46, 43.18,
         {"Sim.Device": "SUBCKT", "Sim.Library": BOARDFILE, "Sim.Name": "MBR120",
          "Sim.Pins": "1=K 2=A", "_rot": 180, "_fp": "Diode_SMD:D_SOD-123F"},
         {"2": "VBUS", "1": "VSYS"}),
        cap("C1", "47u", 147.32, 58.42, "VSYS!", "GND", C0805),
        res("R2", "100K", 157.48, 58.42, "VSYS", "3V3_EN"),
        res("R9", "1R", 167.64, 58.42, "VSYS", "VINA"),
        cap("C18", "2u2", 167.64, 76.2, "VINA!", "GND", C0402),
        ("simAVR:RT6150B-33GQW", "U2", "RT6150B-33GQW", 205.74, 63.5, NOSIM,
         {"5": "VSYS", "8": "VINA", "6": "3V3_EN", "7": "GPIO23", "4": "LX1", "2": "LX2",
          "1": "+3V3", "10": "+3V3", "3": "GND"}),
        ("Device:L", "L1", "2u2", 233.68, 48.26, NOSIM, {"1": "LX1", "2": "LX2"}),
        cap("C2", "47u", 246.38, 63.5, "+3V3", "GND", C0805),
        ("Connector:TestPoint", "TP4", "GPIO23", 182.88, 83.82, NOSIM, {"1": "GPIO23"}),
        # --- VSYS / 3 on ADC3 (Q1 isolates the pin when 3V3 is off) ---
        res("R5", "200K", 279.4, 50.8, "VSYS", "VSYS_DIV"),
        res("R6", "100K", 279.4, 76.2, "VSYS_DIV", "GND"),
        ("Transistor_FET:DMG1012T", "Q1", "DMG1012T", 297.18, 63.5,
         {"Sim.Device": "SUBCKT", "Sim.Library": BOARDFILE, "Sim.Name": "DMG1012T",
          "Sim.Pins": "1=G 2=S 3=D"},
         {"1": "+3V3", "3": "VSYS_DIV", "2": "GPIO29_ADC3"}),
        cap("C3", "1n", 312.42, 76.2, "GPIO29_ADC3", "GND"),
        # --- VBUS sense on GPIO24 ---
        res("R10", "5K6", 335.28, 50.8, "VBUS", "GPIO24"),
        res("R1", "10K", 335.28, 76.2, "GPIO24", "GND"),
        # --- User LED on GPIO25 ---
        res("R3", "470R", 358.14, 50.8, "GPIO25", "LED_A"),
        ("Device:LED", "D2", "Green", 358.14, 73.66,
         {"Sim.Device": "SUBCKT", "Sim.Library": BOARDFILE, "Sim.Name": "LEDVERTE",
          "Sim.Pins": "1=K 2=A", "_rot": 90, "_fp": "LED_SMD:LED_0603_1608Metric"},
         {"2": "LED_A", "1": "GND"}),
        ("Connector:TestPoint", "TP5", "GPIO25", 370.84, 63.5, NOSIM, {"1": "GPIO25"}),
        # --- ADC supply / reference: 3V3 filtered by R7 / C14 ---
        res("R7", "200R", 279.4, 109.22, "+3V3", "ADC_VREF", R0603),
        cap("C14", "2u2", 279.4, 127.0, "ADC_VREF", "GND", C0402),
        # --- QSPI flash, BOOTSEL ---
        ("Memory_Flash:W25Q16JVSS", "U3", "W25Q16JVUXIQ", 101.6, 116.84,
         dict(NOSIM, _fp="Package_SON:Winbond_USON-8-1EP_3x2mm_P0.5mm_EP0.2x1.6mm"),
         {"1": "QSPI_SS", "2": "QSPI_SD1", "3": "QSPI_SD2", "4": "GND", "5": "QSPI_SD0",
          "6": "QSPI_SCLK", "7": "QSPI_SD3", "8": "+3V3"}),
        cap("C4", "100n", 124.46, 116.84, "+3V3", "GND"),
        res("R11", "1K", 63.5, 132.08, "QSPI_SS", "BOOTSEL", sim=False),
        ("Switch:SW_Push", "SW1", "TP-1221U-K9K5325", 76.2, 147.32, NOSIM,
         {"1": "BOOTSEL", "2": "GND"}),
        ("Connector:TestPoint", "TP6", "BOOTSEL", 50.8, 147.32, NOSIM, {"1": "BOOTSEL"}),
        # --- 12 MHz crystal ---
        ("Device:Crystal_GND24", "X1", "12MHz ABM8-272-T3", 101.6, 187.96,
         dict(NOSIM, _fp="Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm"),
         {"1": "XIN", "3": "XR", "2": "GND"}),
        res("R14", "1K", 124.46, 182.88, "XOUT", "XR", sim=False),
        cap("C16", "15p", 86.36, 203.2, "XIN", "GND", sim=False),
        cap("C17", "15p", 116.84, 203.2, "XR", "GND", sim=False),
        # --- Decoupling: 3V3 (100n), VREG (2u2), 1V1 core (not simulated) ---
        cap("C5", "100n", 162.56, 236.22, "+3V3", "GND"),
        cap("C6", "100n", 172.72, 236.22, "+3V3", "GND"),
        cap("C7", "100n", 182.88, 236.22, "+3V3", "GND"),
        cap("C8", "100n", 193.04, 236.22, "+3V3", "GND"),
        cap("C9", "100n", 203.2, 236.22, "+3V3", "GND"),
        cap("C10", "100n", 213.36, 236.22, "+3V3", "GND"),
        cap("C11", "100n", 223.52, 236.22, "1V1", "GND", sim=False),
        cap("C15", "100n", 233.68, 236.22, "1V1", "GND", sim=False),
        cap("C12", "2u2", 246.38, 236.22, "+3V3", "GND", C0402),
        cap("C13", "2u2", 256.54, 236.22, "1V1", "GND", C0402, sim=False),
        # --- Board edge: 40-pin castellated header J0, SWD header J2 ---
        ("Connector_Generic:Conn_02x20_Counter_Clockwise", "J0", "CON_PICO_40W",
         370.84, 160.02, dict(NOSIM, _fp="Module:RaspberryPi_Pico_Common_Unspecified"),
         {"1": "GPIO0", "2": "GPIO1", "3": "GND", "4": "GPIO2", "5": "GPIO3", "6": "GPIO4",
          "7": "GPIO5", "8": "GND", "9": "GPIO6", "10": "GPIO7", "11": "GPIO8", "12": "GPIO9",
          "13": "GND", "14": "GPIO10", "15": "GPIO11", "16": "GPIO12", "17": "GPIO13",
          "18": "GND", "19": "GPIO14", "20": "GPIO15", "21": "GPIO16", "22": "GPIO17",
          "23": "GND", "24": "GPIO18", "25": "GPIO19", "26": "GPIO20", "27": "GPIO21",
          "28": "GND", "29": "GPIO22", "30": "RUN", "31": "GPIO26_ADC0",
          "32": "GPIO27_ADC1", "33": "GND", "34": "GPIO28_ADC2", "35": "ADC_VREF",
          "36": "+3V3", "37": "3V3_EN", "38": "GND", "39": "VSYS", "40": "VBUS"}),
        ("Connector_Generic:Conn_01x03", "J2", "CON_PICO_3W", 370.84, 251.46,
         NOSIM, {"1": "SWCLK", "2": "GND", "3": "SWDIO"}),
        # --- Simulation only: PC on the USB port, U2 output ---
        ("Simulation_SPICE:VDC", "V2", "USB 5 V", 40.64, 236.22,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=5"},
         {"1": "VBUS", "2": "GND"}),
        ("Simulation_SPICE:VDC", "V3", "3,3 V (U2)", 60.96, 236.22,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=3.3"},
         {"1": "+3V3", "2": "GND"}),
    ]
    # Pins of the board edge -> pins of the sheet symbol (unique names).
    PICO_EDGE = {1: "GP0", 2: "GP1", 3: "GND3", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5",
                 8: "GND8", 9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9", 13: "GND13",
                 14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13", 18: "GND18", 19: "GP14",
                 20: "GP15", 21: "GP16", 22: "GP17", 23: "GND23", 24: "GP18", 25: "GP19",
                 26: "GP20", 27: "GP21", 28: "GND28", 29: "GP22", 30: "RUN", 31: "GP26",
                 32: "GP27", 33: "AGND", 34: "GP28", 35: "ADC_VREF", 36: "3V3",
                 37: "3V3_EN", 38: "GND38", 39: "VSYS", 40: "VBUS"}
    PICO_HIER = {("J0", str(k)): name for k, name in PICO_EDGE.items()}
    PICO_HIER.update({("J2", "1"): "SWCLK", ("J2", "2"): "DBG_GND", ("J2", "3"): "SWDIO"})
    PARTS, WIRES, LABELS, NO_CONNECTS = build_stubbed(BOARD_PARTS, PICO_HIER, vlabels=True)
    # GND / 3V3 pins of the edge: hierarchical labels tied to the power nets.
    for k, (hname, net) in enumerate(POWER_TIES):
        tx, ty = 314.96, round(193.04 + 5.08 * k, 2)
        HLABELS.append((hname, tx, ty, 180))
        WIRES.append(((tx, ty), (round(tx + 5.08, 2), ty)))
        PARTS.append((f"power:{net}", f"#PWR{90 + k}", net, round(tx + 5.08, 2), ty,
                      {"_rot": 270 if net != "GND" else 90}))
    JUNCTIONS = []
    NO_CONNECT_REF = None
    TEXTS = [
        ("USB micro-B", 30.48, 35.56),
        ("Alimentation : VBUS -> D1 -> VSYS -> U2 (buck-boost 3,3 V) -> 3V3", 119.38, 30.48),
        ("PS=0 : PFM (defaut), PS=1 : PWM (GPIO23)", 180.34, 88.9),
        ("VSYS / 3 sur ADC3 ; Q1 bloque la fuite par ADC3 quand 3V3 est coupe", 269.24, 35.56),
        ("Detection VBUS", 327.66, 35.56),
        ("LED (GPIO25)", 350.52, 35.56),
        ("Alimentation et reference de l'ADC", 266.7, 99.06),
        ("Flash QSPI 2 Mo", 86.36, 99.06),
        ("BOOTSEL : force le demarrage USB", 45.72, 124.46),
        ("Quartz 12 MHz", 88.9, 172.72),
        ("Decouplage 3V3 et 1V1", 160.02, 223.52),
        ("Broches GND / 3V3 du bord de carte", 304.8, 186.69),
        ("Bord de carte (40 broches)", 353.06, 124.46),
        ("DEBUG (SWD)", 360.68, 241.3),
        ("Simulation seulement : PC sur l'USB (V2), sortie de U2 (V3)", 30.48, 223.52),
        ("Non simules : J0, J1, J2, U2, U3, L1, X1, SW1, TP, R11..R14, C11, C13, C15..C17",
         30.48, 271.78),
        ("U1 : modele RP2040 (rp2040js), seules les GPIO sont simulees", 30.48, 275.59),
    ]
    DIRECTIVES = []
    HIER_SHEET = (PICO_SUBFILE, "Raspberry Pi Pico", lambda: pico_root_sheet())


if VARIANT == "pico2board":
    # Raspberry Pi Pico 2 (RP2350A). Its schematic is only published as a
    # picture (Pico 2 datasheet, appendix B): the board is rebuilt from the
    # Pico (picoboard above, same board outline and power section), the
    # text of the Pico 2 datasheet, the Pico 2 W schematic (same RP2350
    # core: references and values) and "Hardware design with RP2350".
    LIB.update(load_official_symbols("pico_board.kicad_sym.txt"))
    LIB.update(load_official_symbols("rp2350.kicad_sym.txt"))
    PAPER = "A3"
    TITLE = ("Raspberry Pi Pico 2 (simulation kisimavr)",
             "Reconstruit : datasheet Pico 2, schema Pico 2 W, Hardware design with RP2350",
             "Simulation : RP2350 = rp2350js ; U2 remplace par V3 ; USB = V2 (5 V)")
    BOARDFILE = "pico2_board_models.lib"
    CHIPFILE = "rp2350.lib"
    LIBFILE = CHIPFILE    # main() copies the model given on the command line here.
    PICO_SUBFILE = "pico2_board.kicad_sch"
    NOSIM = {"_nosim": True}
    R0402 = "Resistor_SMD:R_0402_1005Metric"
    R0603 = "Resistor_SMD:R_0603_1608Metric"
    C0402 = "Capacitor_SMD:C_0402_1005Metric"
    C0603 = "Capacitor_SMD:C_0603_1608Metric"
    C0805 = "Capacitor_SMD:C_0805_2012Metric"

    def res(ref, value, x, y, net1, net2, fp=R0603, sim=True):
        extra = {"_fp": fp}
        if not sim:
            extra.update(NOSIM)
        return ("Device:R", ref, value, x, y, extra, {"1": net1, "2": net2})

    def cap(ref, value, x, y, net1, net2, fp=C0603, sim=True):
        extra = {"_fp": fp}
        if not sim:
            extra.update(NOSIM)
        return ("Device:C", ref, value, x, y, extra, {"1": net1, "2": net2})

    # RP2350A (QFN-60) GPIO pins, symbol MCU_RaspberryPi:RP2350A.
    gpio_pins = {"2": 0, "3": 1, "4": 2, "5": 3, "7": 4, "8": 5, "9": 6, "10": 7, "12": 8,
                 "13": 9, "14": 10, "15": 11, "16": 12, "17": 13, "18": 14, "19": 15,
                 "27": 16, "28": 17, "29": 18, "31": 19, "32": 20, "33": 21, "34": 22,
                 "35": 23, "36": 24, "37": 25, "40": 26, "41": 27, "42": 28, "43": 29}
    gpio_net = {n: (f"GPIO{n}" if n < 26 else f"GPIO{n}_ADC{n - 26}") for n in range(30)}
    u1 = {num: gpio_net[g] for num, g in gpio_pins.items()}
    u1.update({"1": "+3V3", "11": "+3V3", "20": "+3V3", "30": "+3V3", "38": "+3V3",
               "45": "+3V3", "53": "+3V3", "54": "+3V3", "49": "+3V3",
               "6": "1V1!", "23": "1V1", "39": "1V1", "50": "1V1", "48": "VREG_LX",
               "46": "VREG_AVDD!", "47": "GND", "44": "ADC_VREF!", "61": "GND",
               "21": "XIN", "22": "XOUT", "24": "SWCLK", "25": "SWDIO", "26": "RUN",
               "51": "USB_DM", "52": "USB_DP", "55": "QSPI_SD3", "56": "QSPI_SCLK",
               "57": "QSPI_SD0", "58": "QSPI_SD2", "59": "QSPI_SD1", "60": "QSPI_SS"})
    chip_sim_pins = " ".join(f"{num}=GPIO{g}" for num, g in
                             sorted(gpio_pins.items(), key=lambda kv: kv[1]))

    BOARD_PARTS = [
        # --- RP2350A (co-simulated) ---
        ("MCU_RaspberryPi:RP2350A", "U1", "RP2350A0A2", 200.66, 167.64,
         {"Sim.Device": "SUBCKT", "Sim.Library": CHIPFILE, "Sim.Name": "RP2350",
          "Sim.Pins": chip_sim_pins}, u1),
        # --- USB: micro-B connector, 27R series resistors, test points ---
        ("Connector:USB_B_Micro", "J1", "690-005-298-486", 40.64, 60.96,
         dict(NOSIM, _fp="Connector_USB:USB_Micro-B_Molex-105017-0001"),
         {"1": "VBUS", "2": "USB_DMX", "3": "USB_DPX", "4": "NC", "5": "GND", "SH": "GND"}),
        res("R12", "27R", 73.66, 53.34, "USB_DMX", "USB_DM", sim=False),
        res("R13", "27R", 83.82, 53.34, "USB_DPX", "USB_DP", sim=False),
        ("Connector:TestPoint", "TP1", "GND", 63.5, 80.01, NOSIM, {"1": "GND"}),
        ("Connector:TestPoint", "TP2", "USB_DM", 73.66, 80.01, NOSIM, {"1": "USB_DMX"}),
        ("Connector:TestPoint", "TP3", "USB_DP", 83.82, 80.01, NOSIM, {"1": "USB_DPX"}),
        # --- Power: VBUS -> D1 -> VSYS -> U2 buck-boost -> 3V3 ---
        ("Device:D_Schottky", "D1", "PMEG6010ELR", 124.46, 43.18,
         {"Sim.Device": "SUBCKT", "Sim.Library": BOARDFILE, "Sim.Name": "PMEG6010",
          "Sim.Pins": "1=K 2=A", "_rot": 180, "_fp": "Diode_SMD:Nexperia_CFP3_SOD-123W"},
         {"2": "VBUS", "1": "VSYS"}),
        cap("C1", "47u", 147.32, 58.42, "VSYS!", "GND", C0805),
        res("R2", "100K", 157.48, 58.42, "VSYS", "3V3_EN"),
        res("R9", "1R", 167.64, 58.42, "VSYS", "VINA"),
        cap("C22", "4u7", 167.64, 76.2, "VINA!", "GND", C0402),
        ("simAVR:RT6150B-33GQW", "U2", "RT6150B-33GQW", 205.74, 63.5, NOSIM,
         {"5": "VSYS", "8": "VINA", "6": "3V3_EN", "7": "GPIO23", "4": "LX1", "2": "LX2",
          "1": "+3V3", "10": "+3V3", "3": "GND"}),
        ("Device:L", "L1", "1u", 233.68, 48.26, NOSIM, {"1": "LX1", "2": "LX2"}),
        cap("C2", "47u", 246.38, 63.5, "+3V3", "GND", C0805),
        cap("C21", "47u", 256.54, 63.5, "+3V3", "GND", C0805),
        ("Connector:TestPoint", "TP4", "GPIO23", 182.88, 83.82, NOSIM, {"1": "GPIO23"}),
        # --- VSYS / 3 on ADC3 (Q1 isolates the pin when 3V3 is off) ---
        res("R5", "200K", 279.4, 50.8, "VSYS", "VSYS_DIV"),
        res("R6", "100K", 279.4, 76.2, "VSYS_DIV", "GND"),
        ("Transistor_FET:DMG1012T", "Q1", "DMG1012T", 297.18, 63.5,
         {"Sim.Device": "SUBCKT", "Sim.Library": BOARDFILE, "Sim.Name": "DMG1012T",
          "Sim.Pins": "1=G 2=S 3=D"},
         {"1": "+3V3", "3": "VSYS_DIV", "2": "GPIO29_ADC3"}),
        cap("C3", "1n", 312.42, 76.2, "GPIO29_ADC3", "GND"),
        # --- VBUS sense on GPIO24 ---
        res("R10", "5K6", 335.28, 50.8, "VBUS", "GPIO24"),
        res("R1", "10K", 335.28, 76.2, "GPIO24", "GND"),
        # --- User LED on GPIO25 ---
        res("R3", "470R", 358.14, 50.8, "GPIO25", "LED_A"),
        ("Device:LED", "D2", "Green", 358.14, 73.66,
         {"Sim.Device": "SUBCKT", "Sim.Library": BOARDFILE, "Sim.Name": "LEDVERTE",
          "Sim.Pins": "1=K 2=A", "_rot": 90, "_fp": "LED_SMD:LED_0603_1608Metric"},
         {"2": "LED_A", "1": "GND"}),
        ("Connector:TestPoint", "TP5", "GPIO25", 370.84, 63.5, NOSIM, {"1": "GPIO25"}),
        # --- ADC supply / reference: 3V3 filtered by R7 / C14 ---
        res("R7", "200R", 279.4, 109.22, "+3V3", "ADC_VREF", R0402),
        cap("C14", "2u2", 279.4, 127.0, "ADC_VREF", "GND", C0402),
        # --- Core regulator of the RP2350: 3V3 -> VREG_LX -> L2 -> 1V1 ---
        cap("C9", "4u7", 320.04, 109.22, "+3V3", "GND", C0402),
        ("Device:L", "L2", "3u3", 335.28, 104.14,
         dict(NOSIM, _fp="Inductor_SMD:L_0806_2016Metric"), {"1": "VREG_LX", "2": "1V1"}),
        cap("C13", "4u7", 350.52, 109.22, "1V1", "GND", C0402, sim=False),
        res("R4", "33R", 320.04, 134.62, "+3V3", "VREG_AVDD", R0402, sim=False),
        cap("C18", "4u7", 335.28, 139.7, "VREG_AVDD", "GND", C0402, sim=False),
        # --- QSPI flash, BOOTSEL ---
        ("Memory_Flash:W25Q32JVSS", "U3", "W25Q32RVXHJQ", 101.6, 116.84,
         dict(NOSIM, _fp="Package_SON:Winbond_USON-8-1EP_3x2mm_P0.5mm_EP0.2x1.6mm"),
         {"1": "QSPI_SS", "2": "QSPI_SD1", "3": "QSPI_SD2", "4": "GND", "5": "QSPI_SD0",
          "6": "QSPI_SCLK", "7": "QSPI_SD3", "8": "+3V3"}),
        cap("C4", "100n", 124.46, 116.84, "+3V3", "GND"),
        res("R11", "1K", 63.5, 132.08, "QSPI_SS", "BOOTSEL", sim=False),
        ("Switch:SW_Push", "SW1", "TP-1221U-K9K5325", 76.2, 147.32, NOSIM,
         {"1": "BOOTSEL", "2": "GND"}),
        ("Connector:TestPoint", "TP6", "BOOTSEL", 50.8, 147.32, NOSIM, {"1": "BOOTSEL"}),
        # --- 12 MHz crystal ---
        ("Device:Crystal_GND24", "X1", "12MHz ABM8-272-T3", 101.6, 187.96,
         dict(NOSIM, _fp="Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm"),
         {"1": "XIN", "3": "XR", "2": "GND"}),
        res("R14", "1K", 124.46, 182.88, "XOUT", "XR", sim=False),
        cap("C16", "15p", 86.36, 203.2, "XIN", "GND", sim=False),
        cap("C17", "15p", 116.84, 203.2, "XR", "GND", sim=False),
        # --- Decoupling: 3V3 (100n, 4u7), 1V1 core (not simulated) ---
        cap("C5", "100n", 162.56, 241.3, "+3V3", "GND"),
        cap("C6", "100n", 172.72, 241.3, "+3V3", "GND"),
        cap("C7", "100n", 182.88, 241.3, "+3V3", "GND"),
        cap("C8", "100n", 193.04, 241.3, "+3V3", "GND"),
        cap("C10", "100n", 203.2, 241.3, "+3V3", "GND"),
        cap("C11", "100n", 213.36, 241.3, "+3V3", "GND"),
        cap("C12", "100n", 223.52, 241.3, "+3V3", "GND"),
        cap("C23", "4u7", 233.68, 241.3, "+3V3", "GND", C0402),
        cap("C15", "100n", 246.38, 241.3, "1V1", "GND", sim=False),
        cap("C20", "100n", 256.54, 241.3, "1V1", "GND", sim=False),
        cap("C19", "4u7", 266.7, 241.3, "1V1", "GND", C0402, sim=False),
        # --- Board edge: 40-pin castellated header J0, SWD header J2 ---
        ("Connector_Generic:Conn_02x20_Counter_Clockwise", "J0", "CON_PICO_40W",
         370.84, 167.64, dict(NOSIM, _fp="Module:RaspberryPi_Pico_Common_Unspecified"),
         {"1": "GPIO0", "2": "GPIO1", "3": "GND", "4": "GPIO2", "5": "GPIO3", "6": "GPIO4",
          "7": "GPIO5", "8": "GND", "9": "GPIO6", "10": "GPIO7", "11": "GPIO8", "12": "GPIO9",
          "13": "GND", "14": "GPIO10", "15": "GPIO11", "16": "GPIO12", "17": "GPIO13",
          "18": "GND", "19": "GPIO14", "20": "GPIO15", "21": "GPIO16", "22": "GPIO17",
          "23": "GND", "24": "GPIO18", "25": "GPIO19", "26": "GPIO20", "27": "GPIO21",
          "28": "GND", "29": "GPIO22", "30": "RUN", "31": "GPIO26_ADC0",
          "32": "GPIO27_ADC1", "33": "GND", "34": "GPIO28_ADC2", "35": "ADC_VREF",
          "36": "+3V3", "37": "3V3_EN", "38": "GND", "39": "VSYS", "40": "VBUS"}),
        ("Connector_Generic:Conn_01x03", "J2", "CON_PICO_3W", 370.84, 254.0,
         NOSIM, {"1": "SWCLK", "2": "GND", "3": "SWDIO"}),
        # --- Simulation only: PC on the USB port, U2 output ---
        ("Simulation_SPICE:VDC", "V2", "USB 5 V", 40.64, 236.22,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=5"},
         {"1": "VBUS", "2": "GND"}),
        ("Simulation_SPICE:VDC", "V3", "3,3 V (U2)", 60.96, 236.22,
         {"Sim.Device": "V", "Sim.Type": "DC", "Sim.Pins": "1=+ 2=-", "Sim.Params": "dc=3.3"},
         {"1": "+3V3", "2": "GND"}),
    ]
    PICO_EDGE = {1: "GP0", 2: "GP1", 3: "GND3", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5",
                 8: "GND8", 9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9", 13: "GND13",
                 14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13", 18: "GND18", 19: "GP14",
                 20: "GP15", 21: "GP16", 22: "GP17", 23: "GND23", 24: "GP18", 25: "GP19",
                 26: "GP20", 27: "GP21", 28: "GND28", 29: "GP22", 30: "RUN", 31: "GP26",
                 32: "GP27", 33: "AGND", 34: "GP28", 35: "ADC_VREF", 36: "3V3",
                 37: "3V3_EN", 38: "GND38", 39: "VSYS", 40: "VBUS"}
    PICO_HIER = {("J0", str(k)): name for k, name in PICO_EDGE.items()}
    PICO_HIER.update({("J2", "1"): "SWCLK", ("J2", "2"): "DBG_GND", ("J2", "3"): "SWDIO"})
    PARTS, WIRES, LABELS, NO_CONNECTS = build_stubbed(BOARD_PARTS, PICO_HIER, vlabels=True)
    for k, (hname, net) in enumerate(POWER_TIES):
        tx, ty = 314.96, round(200.66 + 5.08 * k, 2)
        HLABELS.append((hname, tx, ty, 180))
        WIRES.append(((tx, ty), (round(tx + 5.08, 2), ty)))
        PARTS.append((f"power:{net}", f"#PWR{90 + k}", net, round(tx + 5.08, 2), ty,
                      {"_rot": 270 if net != "GND" else 90}))
    JUNCTIONS = []
    NO_CONNECT_REF = None
    TEXTS = [
        ("USB micro-B", 30.48, 35.56),
        ("Alimentation : VBUS -> D1 -> VSYS -> U2 (buck-boost 3,3 V) -> 3V3", 119.38, 30.48),
        ("U2 : RT6150 d'apres la datasheet Pico 2 (RT6154A sur le schema Pico 2 W)",
         119.38, 34.29),
        ("PS=0 : PFM (defaut), PS=1 : PWM (GPIO23)", 180.34, 88.9),
        ("VSYS / 3 sur ADC3 ; Q1 bloque la fuite par ADC3 quand 3V3 est coupe", 269.24, 35.56),
        ("Detection VBUS", 327.66, 35.56),
        ("LED (GPIO25)", 350.52, 35.56),
        ("Alimentation et reference de l'ADC", 266.7, 99.06),
        ("Regulateur du coeur (dans le RP2350) : 3V3 -> 1V1", 314.96, 99.06),
        ("Flash QSPI 4 Mo", 86.36, 99.06),
        ("BOOTSEL : force le demarrage USB", 45.72, 124.46),
        ("Quartz 12 MHz", 88.9, 172.72),
        ("Decouplage 3V3 et 1V1", 160.02, 228.6),
        ("Broches GND / 3V3 du bord de carte", 304.8, 194.31),
        ("Bord de carte (40 broches)", 353.06, 129.54),
        ("DEBUG (SWD)", 360.68, 243.84),
        ("Simulation seulement : PC sur l'USB (V2), sortie de U2 (V3)", 30.48, 223.52),
        ("Non simules : J0, J1, J2, U2, U3, L1, L2, X1, SW1, TP, R4, R11..R14, C13, C15..C20",
         30.48, 271.78),
        ("U1 : modele RP2350 (rp2350js, coeurs RISC-V ou Arm), seules les GPIO sont simulees",
         30.48, 275.59),
    ]
    DIRECTIVES = []
    HIER_SHEET = (PICO_SUBFILE, "Raspberry Pi Pico 2", lambda: pico_root_sheet(pico2=True))


def pico_root_sheet(pico2=False):
    """Root sheet of RaspberryPi_Pico (RaspberryPi_Pico2): the board as a
    sheet symbol shaped like a Pico seen from above (USB at the top), header
    pins at their real positions (scale 2: 2.54 mm pitch = 5.08 mm)."""
    board = "Raspberry Pi Pico 2" if pico2 else "Raspberry Pi Pico"
    chip = "RP2350" if pico2 else "RP2040"
    x0, y0 = 116.84, 25.4                  # Upper left corner of the board.
    w, h = 41.91, 101.6                    # 21 x 51 mm (scale 2, on the grid).
    pins = []
    for k in range(1, 21):                 # Left edge, pin 1 at the top.
        pins.append((PICO_EDGE[k], x0, round(y0 + 2.54 + 5.08 * (k - 1), 2), 180))
    for k in range(21, 41):                # Right edge, pin 40 at the top.
        pins.append((PICO_EDGE[k], round(x0 + w, 2), round(y0 + 2.54 + 5.08 * (40 - k), 2), 0))
    for name, dx in (("SWCLK", 16.51), ("DBG_GND", 21.59), ("SWDIO", 26.67)):
        pins.append((name, round(x0 + dx, 2), round(y0 + h, 2), 270))

    out = [f'(kicad_sch (version 20231120) (generator "eeschema") '
           f'(generator_version "8.0") (uuid "{ROOT_UUID}") (paper "A4")']
    out.append(f'(title_block (title "{board} (simulation kisimavr)") '
               f'(comment 1 "Carte complete : feuille {PICO_SUBFILE}") '
               '(comment 2 "Cablez vos composants sur les broches du bloc"))')
    out.append("(lib_symbols)")

    def rect(xa, ya, xb, yb, dash=False):
        kind = "dash" if dash else "default"
        out.append(f'(rectangle (start {xa} {ya}) (end {xb} {yb}) '
                   f'(stroke (width 0.254) (type {kind})) (fill (type none)) (uuid "{uid()}"))')

    def text(t, x, y, size=1.27, just="left bottom"):
        out.append(f'(text "{t}" (exclude_from_sim yes) (at {x} {y} 0) '
                   f'(effects (font (size {size} {size})) (justify {just})) (uuid "{uid()}"))')

    # Details placed at scale 3: bx / by bring them to scale 2.
    def bx(mm):
        return snap(x0 + mm * 2 / 3)

    def by(mm):
        return snap(y0 + mm * 2 / 3)

    rect(bx(21.59), by(-3.81), bx(41.91), by(13.97))                 # micro-USB
    text("USB", bx(27.94), by(8.89))
    rect(bx(11.43), by(22.86), bx(20.32), by(31.75))                 # BOOTSEL
    text("BOOTSEL", bx(10.16), by(35.56))
    rect(bx(46.99), by(17.78), bx(50.8), by(21.59))                  # LED
    text("LED (GP25)", bx(41.91), by(16.51))
    rect(bx(19.05), by(55.88), bx(44.45), by(81.28))                 # RP2040
    text(chip, bx(26.67), by(69.85), size=1.5)
    rect(bx(21.59), by(91.44), bx(41.91), by(101.6))                 # Flash
    text("Flash 4 Mo" if pico2 else "Flash 2 Mo", bx(24.13), by(97.79))
    rect(bx(24.13), by(110.49), bx(39.37), by(116.84))               # Crystal
    text("12 MHz", bx(27.94), by(114.3))
    text(board, bx(16.51 if pico2 else 19.05), by(132.08), size=1.5)
    text("DEBUG", bx(27.94), by(146.05))

    # Each sheet pin: short wire and a label of the same name.
    for (n, x, y, a) in pins:
        if a == 180:
            ex, ey = round(x - 2.54, 2), y
        elif a == 0:
            ex, ey = round(x + 2.54, 2), y
        else:
            ex, ey = x, round(y + 2.54, 2)
        out.append(f'(wire (pts (xy {x} {y}) (xy {ex} {ey})) '
                   f'(stroke (width 0) (type default)) (uuid "{uid()}"))')
        just = "right bottom" if a in (180, 270) else "left bottom"
        out.append(f'(label "{n}" (at {ex} {ey} {a}) (fields_autoplaced yes) '
                   f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid()}"))')

    side_just = {180: "left", 0: "right", 270: "right"}
    pin_txt = " ".join(
        f'(pin "{n}" bidirectional (at {x} {y} {a}) '
        f'(effects (font (size 1.27 1.27)) (justify {side_just[a]})) '
        f'(uuid "{uid()}"))' for (n, x, y, a) in pins)
    out.append(
        f'(sheet (at {x0} {y0}) (size {w} {h}) (fields_autoplaced yes) '
        f'(stroke (width 0.254) (type solid)) (fill (color 0 0 0 0.0000)) (uuid "{SHEET_UUID}") '
        f'(property "Sheetname" "{board}" (at {x0} {round(y0 - 0.7, 2)} 0) '
        f'(effects (font (size 1.27 1.27)) (justify left bottom))) '
        f'(property "Sheetfile" "{PICO_SUBFILE}" (at {x0} {round(y0 + h + 13.97, 2)} 0) '
        f'(effects (font (size 1.27 1.27)) (justify left top))) '
        f'{pin_txt} '
        f'(instances (project "{PROJECT}" (path "/{ROOT_UUID}" (page "2")))))')
    if pico2:
        text("Firmware : pico2_demo_riscv.bin -> rp2350.lib "
             "(tools/make_pico_lib.py --mcu rp2350 --chip)", 20.32, 152.4)
    else:
        text("Firmware : pico_demo.bin -> rp2040.lib (tools/make_pico_lib.py --chip)",
             20.32, 152.4)
    text(f"Sonder : /{board}/GPIO25, VSYS, GPIO29_ADC3... ; terminal : UART0> ...",
         20.32, 156.21)
    out.append(f'(text ".tran 0.5u 10m" (exclude_from_sim no) (at 20.32 162.56 0) '
               f'(effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{uid()}"))')
    out.append('(sheet_instances (path "/" (page "1")))')
    out.append(")")
    return "\n".join(out) + "\n"


if VARIANT in ("pico", "pico2"):
    PICO2 = VARIANT == "pico2"
    PICOFILE = "pico2.lib" if PICO2 else "pico.lib"
    if PICO2:
        LIBFILE = PICOFILE    # main() copies the model given on the command line here.
    TERMFILE = "terminal.lib"
    PICO_SIM_PINS = ("1=GP0 2=GP1 4=GP2 5=GP3 6=GP4 7=GP5 9=GP6 10=GP7 11=GP8 12=GP9 "
                     "14=GP10 15=GP11 16=GP12 17=GP13 19=GP14 20=GP15 21=GP16 22=GP17 "
                     "24=GP18 25=GP19 26=GP20 27=GP21 29=GP22 30=RUN 31=GP26 32=GP27 "
                     "34=GP28 35=ADC_VREF 36=V3V3 37=V3V3_EN 39=VSYS 40=VBUS")
    PICO_PARTS = [
        ("MCU_Module:RaspberryPi_Pico", "A1", "RaspberryPi_Pico2" if PICO2 else "RaspberryPi_Pico",
         127.0, 99.06,
         dict({"Sim.Device": "SUBCKT", "Sim.Library": PICOFILE,
               "Sim.Name": "PICO2" if PICO2 else "PICO", "Sim.Pins": PICO_SIM_PINS},
              **({"_ds": PICO2_DATASHEET} if PICO2 else {})),
         # AGND (33) is tied to GND on the board; wiring it too joins two
         # "power output" pins of the official symbol (ERC error).
         {"*": "NC", "3": "GND", "36": "+3V3",
          "1": "TX", "2": "RX", "19": "BTN", "20": "SQ", "21": "LED", "31": "POT"}),
        # GP16: LED through 330 ohm
        ("Device:R", "R1", "330", 170.18, 83.82, {}, {"1": "LED", "2": "LED_A"}),
        ("Device:LED", "D1", "LEDRED", 170.18, 99.06,
         {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE, "Sim.Name": "LEDRED",
          "Sim.Pins": "1=K 2=A"},
         {"2": "LED_A", "1": "GND"}),
        # GP26 (ADC0): divider on 3V3, as a potentiometer at one third of its course
        ("Device:R", "R2", "10k", 185.42, 83.82, {}, {"1": "+3V3", "2": "POT"}),
        ("Device:R", "R3", "4.7k", 185.42, 99.06, {}, {"1": "POT", "2": "GND"}),
        ("Device:C", "C1", "10n", 198.12, 99.06, {}, {"1": "POT", "2": "GND"}),
        # GP14: push-button, pressed from 3 ms to 6 ms (diode + pulse source)
        ("Device:D", "D2", "DSIG", 60.96, 129.54,
         {"Sim.Device": "SUBCKT", "Sim.Library": LEDFILE, "Sim.Name": "DSIG",
          "Sim.Pins": "1=K 2=A"},
         {"2": "BTN", "1": "BTN_K"}),
        ("Simulation_SPICE:VPULSE", "V1", "BOUTON", 60.96, 147.32,
         {"Sim.Device": "V", "Sim.Type": "PULSE", "Sim.Pins": "1=+ 2=-",
          "Sim.Params": "y1=3.3 y2=0 td=3m tr=1u tf=1u tw=3m per=20m"},
         {"1": "BTN_K", "2": "GND"}),
        # USB-serial adapter (3.3 V) + PC terminal on UART0
        ("simAVR:USB_SERIE", "U2", "USB-serie 3,3 V + PC", 50.8, 83.82,
         {"Sim.Device": "SUBCKT", "Sim.Library": TERMFILE, "Sim.Name": "USB_SERIE",
          "Sim.Pins": "1=TXD 2=RXD 3=DTR 4=GND"},
         {"1": "RX", "2": "TX", "3": "NC", "4": "GND"}),
    ]
    PARTS, WIRES, LABELS, NO_CONNECTS = build_stubbed(PICO_PARTS)
    JUNCTIONS = []
    NO_CONNECT_REF = None
    TEXTS = [
        ("Raspberry Pi Pico 2 (RP2350, coeurs RISC-V) emule par rp2350js dans le plug-in "
         "(firmware pico2_demo_riscv.bin)" if PICO2 else
         "Raspberry Pi Pico (RP2040) emule par rp2040js dans le plug-in (firmware pico_demo.bin)",
         25.4, 30.48),
        ("GP25 : LED de la carte (dans le modele), GP15 : signal carre 1 kHz", 25.4, 34.29),
        ("GP14 : bouton (appuye de 3 a 6 ms) -> GP16 : LED D1", 25.4, 38.1),
        ("GP26 / ADC0 : pont R2 / R3 -> 1,06 V ; UART0 GP0 / GP1 a 115200 bauds", 25.4, 41.91),
        ("Terminal : terminal.txt -> tools/make_terminal_lib.py --vhigh 3.3", 25.4, 45.72),
    ]
    DIRECTIVES = [(".tran 0.5u 12m", 25.4, 185.42)]


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
    body = d["body"]
    if "raw" in d:
        rects = re.findall(r"\(rectangle\s*\(start (-?[0-9.]+) (-?[0-9.]+)\)\s*"
                           r"\(end (-?[0-9.]+) (-?[0-9.]+)\)", body)
        if rects:
            x1, y1, x2, y2 = (float(v) for v in rects[0])
            return min(x1, x2), min(y1, y2)
    nums = re.findall(r"\((?:xy|start|end) (-?[0-9.]+) (-?[0-9.]+)\)", body)
    xs = [float(a) for a, _ in nums]
    ys = [float(b) for _, b in nums]
    return min(xs), min(ys)


def is_ic(d):
    """Integrated circuits (MCU, logic, display...): reference "U" or "DS"."""
    return not d.get("power") and d["ref"] in ("U", "DS", "A")


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
        f'(property "Footprint" "{extra.pop("_fp", FOOTPRINTS.get(lib_id, d.get("footprint", "")))}" '
        f'(at {x} {y} 0) {font(hide=True)})',
        f'(property "Datasheet" "{extra.pop("_ds", DATASHEETS.get(lib_id, d.get("datasheet", "")))}" '
        f'(at {x} {y} 0) {font(hide=True)})',
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
        just = "right bottom" if angle in (180, 270) else "left bottom"
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


PICO_BOARD_MODELS = """\
* Models of the Raspberry Pi Pico board project (kisimavr), simplified.

* Schottky diode MBR120VLSFT1G (VBUS -> VSYS), about 0.3 V at 100 mA
.subckt MBR120 A K
D1 A K DMBR120
.model DMBR120 D(IS=50u RS=0.1 N=1.1 BV=20 IBV=1m CJO=100p)
.ends MBR120

* N-channel MOSFET DMG1012T (SOT-523), level 1: Vth ~ 0.7 V
.subckt DMG1012T D G S
M1 D G S S NDMG
.model NDMG NMOS(LEVEL=1 VTO=0.7 KP=0.5 LAMBDA=0.02)
.ends DMG1012T

* Green LED (pins: anode cathode), about 2.0 V at 3 mA
.subckt LEDVERTE A K
D1 A K DLEDV
.model DLEDV D(IS=1e-19 RS=10 N=2 BV=5 IBV=10u CJO=10p)
.ends LEDVERTE
"""

PICO2_BOARD_MODELS = """\
* Models of the Raspberry Pi Pico 2 board project (kisimavr), simplified.

* Schottky diode PMEG6010ELR (VBUS -> VSYS), about 0.3 V at 100 mA
.subckt PMEG6010 A K
D1 A K DPMEG
.model DPMEG D(IS=10u RS=0.08 N=1.05 BV=60 IBV=1m CJO=80p)
.ends PMEG6010

* N-channel MOSFET DMG1012T (SOT-523), level 1: Vth ~ 0.7 V
.subckt DMG1012T D G S
M1 D G S S NDMG
.model NDMG NMOS(LEVEL=1 VTO=0.7 KP=0.5 LAMBDA=0.02)
.ends DMG1012T

* Green LED (pins: anode cathode), about 2.0 V at 3 mA
.subckt LEDVERTE A K
D1 A K DLEDV
.model DLEDV D(IS=1e-19 RS=10 N=2 BV=5 IBV=10u CJO=10p)
.ends LEDVERTE
"""

PICO_TERMINAL = r"""# Text sent by the PC terminal to the Pico (UART0, 115200 bauds, 3.3 V).
# One line per send: <time> <text>   (escapes: \n \r \s \xHH)
# After a change: python3 tools/make_terminal_lib.py --vhigh 3.3 terminal.txt -o terminal.lib
2m Bonjour Pico\n
""".replace("\\\\", "\\")


def write_pico_terminal():
    """terminal.txt (kept if present) and terminal.lib, 3.3 V levels."""
    script = os.path.join(OUT, "terminal.txt")
    if not os.path.exists(script):
        with open(script, "w") as f:
            f.write(PICO_TERMINAL)
    import subprocess
    subprocess.run([sys.executable,
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_terminal_lib.py"),
                    "--vhigh", "3.3", script, "-o", os.path.join(OUT, "terminal.lib")], check=True)


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
    if HIER_SHEET is not None:
        # Board drawn as a sheet symbol on the root, schematic in a sub-sheet.
        global INSTANCE_PATH
        subfile, sheetname, root_writer = HIER_SHEET
        INSTANCE_PATH = f"/{ROOT_UUID}/{SHEET_UUID}"
        sub = schematic(SUB_UUID, root=False)
        assert balanced(sub), "unbalanced s-expression"
        with open(os.path.join(OUT, subfile), "w") as f:
            f.write(sub)
        sch = root_writer()
        sheets.append([SHEET_UUID, sheetname])
    else:
        sch = schematic()
    assert balanced(sch), "unbalanced s-expression"
    with open(os.path.join(OUT, PROJECT + ".kicad_sch"), "w") as f:
        f.write(sch)

    pro = {"meta": {"filename": PROJECT + ".kicad_pro", "version": 1},
           "sheets": sheets}
    if HIER_SHEET is not None:
        # The board block is a template: its pins wait for the user's parts,
        # so a label alone on a pin is a warning, not an error.
        pro["erc"] = {"rule_severities": {"label_dangling": "warning"}}
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
    if VARIANT in ("pico", "pico2"):
        write_pico_terminal()
    if VARIANT == "picoboard":
        with open(os.path.join(OUT, BOARDFILE), "w") as f:
            f.write(PICO_BOARD_MODELS)
    if VARIANT == "pico2board":
        with open(os.path.join(OUT, BOARDFILE), "w") as f:
            f.write(PICO2_BOARD_MODELS)
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
