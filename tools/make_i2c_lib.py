#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the SPICE model of I2C slaves on a bus: LM75, 24C02, LCD I2C.

Usage (KiCad project):
  python3 tools/make_i2c_lib.py --lm75 0x48 --lcd 0x27 -o ~/kicad/my_project/i2c.lib

The slaves are a d_cosim instance of mcu_cosim.so ("device" "i2c") wired on
SDA and SCL like the real modules: they decode the bus bit by bit and pull
SDA low (open drain) for their ACK bits and the bits they send, so any
master works: Raspberry Pi Pico / Pico 2, ATmega, or sources.
  - LM75 temperature sensor (--temp, --rate)
  - 24C02 EEPROM (256 bytes)
  - LCD 16x2 with a PCF8574 backpack (LiquidCrystal_I2C wiring): each screen
    is printed in the terminal (LCD> [...]) and in lcd_log.txt
Sub-circuit pins, as a 4-pin I2C module: 1 GND  2 VCC  3 SDA  4 SCL.
Input thresholds 30 % / 70 % of VCC (3.3 V or 5 V); no pull-up inside:
put the pull-ups (4.7 kohm) of the bus in the schematic.
"""

import argparse
import os
import sys

PINS = ["GND", "VCC", "SDA", "SCL"]


def wrap(items, indent="+ ", width=76):
    lines, line = [], ""
    for it in items:
        if line and len(line) + 1 + len(it) > width - len(indent):
            lines.append(line)
            line = it
        else:
            line = f"{line} {it}" if line else it
    lines.append(line)
    return f"\n{indent}".join(lines)


def address(text):
    value = int(text, 0)
    if not 0x08 <= value <= 0x77:
        raise argparse.ArgumentTypeError(f"invalid 7-bit address {text}")
    return value


def parse_args():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", help="output .lib (default: stdout)")
    ap.add_argument("--so", default=os.path.join(here, "..", "build", "mcu_cosim.so"),
                    help="mcu_cosim.so (default: ../build/mcu_cosim.so)")
    ap.add_argument("--lm75", type=address, action="append", default=[],
                    help="LM75 at this address (0x48..0x4F), may be repeated")
    ap.add_argument("--eeprom", type=address, action="append", default=[],
                    help="24C02 at this address (0x50..0x57), may be repeated")
    ap.add_argument("--lcd", type=address, action="append", default=[],
                    help="LCD with a PCF8574 backpack at this address (0x27, 0x3F...)")
    ap.add_argument("--temp", type=float, default=25.0, help="LM75 temperature (degC, 25)")
    ap.add_argument("--rate", type=float, default=0.0, help="LM75 slope (degC/s, 0)")
    ap.add_argument("--cols", type=int, default=16, help="LCD characters per row (16)")
    ap.add_argument("--rows", type=int, default=2, help="LCD rows (2)")
    ap.add_argument("--lcdlog", help="LCD screens file (default: lcd_log.txt next to the .lib)")
    ap.add_argument("--log", action="store_true", help="print every I2C transaction")
    ap.add_argument("--name", default="", help="suffix: I2C_<NAME>, for several buses")
    ap.add_argument("--irreversible", type=int, default=11,
                    help="d_cosim rank, distinct from every other co-simulated part (11)")
    ap.add_argument("--relative", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if not (args.lm75 or args.eeprom or args.lcd):
        ap.error("no slave: use --lm75, --eeprom and / or --lcd")

    if args.relative:    # Repository copy used by the ngspice/ tests.
        args.lcdlog = "../lcd_log.txt" if args.lcdlog is None else args.lcdlog
        return args
    base = os.path.dirname(os.path.abspath(args.output)) if args.output else os.getcwd()
    args.so = os.path.abspath(os.path.expanduser(args.so))
    if args.lcdlog is None:
        args.lcdlog = os.path.join(base, "lcd_log.txt")
    elif args.lcdlog:
        args.lcdlog = os.path.abspath(os.path.expanduser(args.lcdlog))
    if not os.path.isfile(args.so):
        print(f"warning: plug-in not found: {args.so}", file=sys.stderr)
    if args.so != args.so.lower():
        print("warning: the .so path contains upper case letters; KiCad lower-cases\n"
              "         it and ngspice will not find it (use e.g. a link in /opt).",
              file=sys.stderr)
    return args


def main():
    a = parse_args()
    suffix = "".join(ch for ch in a.name if ch.isalnum() or ch == "_").upper()
    sub = f"I2C_{suffix}" if suffix else "I2C_SLAVES"
    tag = sub.lower()
    sim_args = ['"device"', '"i2c"']
    for kind, addresses in (("lm75", a.lm75), ("eeprom", a.eeprom), ("lcd", a.lcd)):
        for addr in addresses:
            sim_args += [f'"{kind}"', f'"0x{addr:02X}"']
    if a.lm75:
        sim_args += ['"temp"', f'"{a.temp:g}"', '"rate"', f'"{a.rate:g}"']
    if a.lcd:
        sim_args += ['"cols"', f'"{a.cols}"', '"rows"', f'"{a.rows}"']
        if a.lcdlog:
            sim_args += ['"lcdlog"', f'"{a.lcdlog}"']
    if a.log:
        sim_args += ['"log"', '"1"']
    slaves = ([f"LM75 0x{x:02X}" for x in a.lm75] + [f"24C02 0x{x:02X}" for x in a.eeprom] +
              [f"LCD {a.cols}x{a.rows} PCF8574 0x{x:02X}" for x in a.lcd])

    out = []
    w = out.append
    w("* =============================================================================")
    w(f"* {sub} : I2C slaves (kisimavr): " + ", ".join(slaves))
    w("*   Decoded bit by bit in mcu_cosim.so (d_cosim, \"device\" \"i2c\"): ACK and")
    w("*   data bits pull SDA low (open drain, 50 ohm). Any master works.")
    w("*")
    w("* Pins: 1 GND  2 VCC  3 SDA  4 SCL   (pull-ups of the bus: in the schematic)")
    w("*   KiCad: a 4-pin connector symbol (Connector_Generic:Conn_01x04) or any")
    w(f"*   symbol; Sim.Device = SUBCKT, Sim.Name = {sub},")
    w("*   Sim.Pins = 1=GND 2=VCC 3=SDA 4=SCL")
    w("*")
    w(f"* Several co-simulated parts: each needs its own d_cosim rank (here {a.irreversible},")
    w("*   option --irreversible; the MCU keeps 1, an LCD 10).")
    w("* Generated by tools/make_i2c_lib.py")
    w("* =============================================================================")
    w("")
    w(f'.model {tag}_core d_cosim simulation="{a.so}"')
    w("+ sim_args=[" + wrap(sim_args) + "]")
    w(f"+ irreversible={a.irreversible}")
    w(f".model {tag}_in adc_bridge(in_low=1.5 in_high=3.5)")
    w(f".model {tag}_bit dac_bridge(out_low=0 out_high=1 out_undef=0 t_rise=10n t_fall=10n)")
    w(f".model {tag}_od aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e9 r_on=50 log=TRUE)")
    w("")
    w(f".subckt {sub} " + " ".join(PINS))
    w("* Levels scaled to a 5 V supply: thresholds 30 % / 70 % of VCC")
    w("Bvs vs 0 V = max(V(VCC,GND), 1)")
    w("Bsda n_sda 0 V = V(SDA,GND) * 5 / V(vs)")
    w("Bscl n_scl 0 V = V(SCL,GND) * 5 / V(vs)")
    w(f"aIn [n_sda n_scl] [d_sda d_scl] {tag}_in")
    w(f"aBus [d_sda d_scl] [d_pull] null {tag}_core")
    w("* SDA pulled low by the slaves (open drain)")
    w(f"aPull [d_pull] [pull] {tag}_bit")
    w(f"aOd pull (SDA GND) {tag}_od")
    w("* Inputs: high impedance; supply current of the modules (about 1 mA)")
    w("Rsda SDA GND 10meg")
    w("Rscl SCL GND 10meg")
    w("Rsupply VCC GND 3.3k")
    w(f".ends {sub}")
    text = "\n".join(out) + "\n"
    if a.output:
        with open(a.output, "w") as f:
            f.write(text)
        print(f"written {a.output}", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
