#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate kicad/atmega328p_full.lib: the complete ATmega328P model.

Usage (new KiCad project):
  python3 tools/make_atmega328p_lib.py --hex ~/Arduino/blink/blink.ino.hex \
      -o ~/kicad/my_project/atmega328p_full.lib

All paths are made absolute. By default the .so is the one built next to this
script (../build/mcu_cosim.so), and uart_log.txt / trace.vcd are written next
to the output .lib (i.e. in the KiCad project folder).

Every Arduino I/O (PD0-PD7, PB0-PB5, PC0-PC5) is a bidirectional pin:
  - the circuit voltage is read back through an adc_bridge (CMOS thresholds);
  - "level" (PORT bit) drives a 0/5 V source through an analog switch closed
    by "oe" (DDR bit): 25 ohm when driving, 1 Gohm when the pin is an input;
  - "pu" closes a 35 kohm pull-up to 5 V (input with PORT bit = 1).
PC0-PC5 (A0-A5) also feed the comparators of the SAR ADC model.
"""

import argparse
import os
import sys

PINS = ([f"PD{i}" for i in range(8)] + [f"PB{i}" for i in range(6)] +
        [f"PC{i}" for i in range(6)])
ADC = list(range(6))

# Pin numbers of the DIP-28 package (KiCad: MCU_Microchip_ATmega:ATmega328P-P).
DIP28 = {"PD0": 2, "PD1": 3, "PD2": 4, "PD3": 5, "PD4": 6, "PD5": 11, "PD6": 12,
         "PD7": 13, "PB0": 14, "PB1": 15, "PB2": 16, "PB3": 17, "PB4": 18,
         "PB5": 19, "PC0": 23, "PC1": 24, "PC2": 25, "PC3": 26, "PC4": 27,
         "PC5": 28}


def wrap(items, indent="+ ", width=76):
    """Split a long list of tokens over SPICE continuation lines."""
    lines, cur = [], ""
    for it in items:
        if len(cur) + len(it) + 1 > width:
            lines.append(cur)
            cur = ""
        cur += (" " if cur else "") + it
    lines.append(cur)
    return ("\n" + indent).join(lines)


def parse_args():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Generate the ATMEGA328P co-simulation .lib")
    ap.add_argument("--hex", required=True, help="firmware (Intel HEX, without bootloader)")
    ap.add_argument("-o", "--output", help="output .lib (default: stdout)")
    ap.add_argument("--so", default=os.path.join(here, "..", "build", "mcu_cosim.so"),
                    help="mcu_cosim.so (default: ../build/mcu_cosim.so)")
    ap.add_argument("--uartlog", help="UART text file (default: next to the .lib)")
    ap.add_argument("--vcd", help="VCD trace (default: next to the .lib, '' = none)")
    ap.add_argument("--freq", default="16000000", help="CPU clock in Hz")
    ap.add_argument("--i2c", default="lm75 eeprom",
                    help='virtual I2C slaves, e.g. "lm75 0x48 eeprom 0x50" ("" = none)')
    ap.add_argument("--lm75temp", default="25", help="LM75 temperature at t = 0 (degC)")
    ap.add_argument("--lm75rate", default="0", help="LM75 slope (degC per second)")
    ap.add_argument("--relative", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.relative:    # Repository copy used by the ngspice/ tests.
        args.uartlog = args.uartlog or "../uart_log.txt"
        args.vcd = "" if args.vcd is None else args.vcd
        return args

    base = os.path.dirname(os.path.abspath(args.output)) if args.output else os.getcwd()
    args.hex = os.path.abspath(os.path.expanduser(args.hex))
    args.so = os.path.abspath(os.path.expanduser(args.so))
    args.uartlog = os.path.abspath(os.path.expanduser(
        args.uartlog or os.path.join(base, "uart_log.txt")))
    if args.vcd is None:
        args.vcd = os.path.join(base, "trace.vcd")
    elif args.vcd:
        args.vcd = os.path.abspath(os.path.expanduser(args.vcd))

    for path, what in ((args.hex, "firmware"), (args.so, "plug-in")):
        if not os.path.isfile(path):
            print(f"warning: {what} not found: {path}", file=sys.stderr)
    if args.so != args.so.lower():
        print("warning: the .so path contains upper case letters; KiCad lower-cases\n"
              "         it and ngspice will not find it (use e.g. a link in /opt).",
              file=sys.stderr)
    if args.hex.endswith("with_bootloader.hex"):
        print("warning: use the .hex WITHOUT bootloader", file=sys.stderr)
    return args


def main():
    a = parse_args()
    hex_file, so_file = a.hex, a.so
    low = [p.lower() for p in PINS]

    args = (['"hex"', f'"{hex_file}"', '"mcu"', '"atmega328p"', '"freq"', f'"{a.freq}"',
             '"io"'] + [f'"{p}"' for p in PINS] +
            ['"adc"'] + [f'"{c}"' for c in ADC] +
            ['"vref"', '"5"', '"adcbit"', '"1e-6"', '"uartlog"', f'"{a.uartlog}"'] +
            ((['"i2c"'] + [f'"{t}"' for t in a.i2c.split()] +
              ['"lm75temp"', f'"{a.lm75temp}"', '"lm75rate"', f'"{a.lm75rate}"'])
             if a.i2c.split() else []) +
            (['"vcd"', f'"{a.vcd}"'] if a.vcd else []))

    sim_pins = " ".join(f"{DIP28[p]}={p}" for p in sorted(PINS, key=lambda p: DIP28[p]))

    out = []
    w = out.append
    w("* =============================================================================")
    w("* ATMEGA328P : complete ATmega328P (Arduino Uno) co-simulation model")
    w("*   simavr runs the .hex inside ngspice through d_cosim (mcu_cosim.so)")
    w("*")
    w("* Pins (all bidirectional, direction set by the firmware):")
    w("*   PD0..PD7 = D0..D7   PB0..PB5 = D8..D13   PC0..PC5 = A0..A5 (+ ADC0..5)")
    w("*   UART0: TXD = PD1 (frames rebuilt by the plug-in), text in uart_log.txt")
    w("*   I2C  : SDA = PC4 (A4), SCL = PC5 (A5), open drain: add pull-ups")
    w("*          (4.7k to 5 V) or enable the internal ones (Wire.begin does).")
    w(f"*          Virtual slaves: {a.i2c or 'none'}")
    w("*")
    w("* KiCad: works with the official symbol MCU_Microchip_ATmega:ATmega328P-P;")
    w("*   Sim.Device = SUBCKT, Sim.Name = ATMEGA328P, Sim.Pins =")
    w(f"*   {sim_pins}")
    w("*   (VCC, GND, AVCC, AREF, RESET, XTAL1/2 are not simulated)")
    w("*")
    w("* New firmware: rerun tools/make_atmega328p_lib.py --hex <file> -o <lib>")
    w("* or edit the path after \"hex\". Keep the .so path in lower case.")
    w("* Generated by tools/make_atmega328p_lib.py")
    w("* =============================================================================")
    w("")
    w(f'.model atmega328p_core d_cosim simulation="{so_file}"')
    w("+ sim_args=[" + wrap(args) + "]")
    w("+ delay=100n")
    w("")
    w(".model m328_in  adc_bridge(in_low=1.5 in_high=3.0)")
    w(".model m328_lvl dac_bridge(out_low=0 out_high=5 out_undef=0 t_rise=10n t_fall=10n)")
    w(".model m328_bit dac_bridge(out_low=0 out_high=1 out_undef=0 t_rise=10n t_fall=10n)")
    w(".model m328_drv aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e9 r_on=25 log=TRUE)")
    w(".model m328_pu  aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e12 r_on=35k log=TRUE)")
    w(".model m328_cmp adc_bridge(in_low=-0.1m in_high=0.1m)")
    w("")
    w(".subckt ATMEGA328P " + " ".join(PINS))
    w("")
    w("* Internal supply for the pull-ups")
    w("Vcc5 vcc5 0 5")
    w("")
    w("* Pin voltages read back by the MCU (CMOS thresholds 0.3 / 0.6 Vcc)")
    w("aIn [" + wrap(PINS) + "]")
    w("+ [" + wrap([f"{p}_i" for p in low]) + "] m328_in")
    w("")
    w("* SAR ADC comparators: (An - vdac) > 0")
    for c in ADC:
        w(f"Ec{c} c{c} 0 PC{c} vdac 1")
    w("aCmp [" + " ".join(f"c{c}" for c in ADC) + "] [" +
      " ".join(f"c{c}_d" for c in ADC) + "] m328_cmp")
    w("")
    w("* The MCU: inputs = pin reads + comparators,")
    w("*          outputs = (level, oe, pu) per pin + DAC bits b0..b9")
    ins = [f"{p}_i" for p in low] + [f"c{c}_d" for c in ADC]
    outs = [n for p in low for n in (f"{p}_l", f"{p}_e", f"{p}_p")] + \
           [f"b{k}" for k in range(10)]
    w("aMcu [" + wrap(ins) + "]")
    w("+ [" + wrap(outs) + "]")
    w("+ null atmega328p_core")
    w("")
    w("* Pin drivers: 0/5 V level, output enable and pull-up enable")
    w("aLvl [" + wrap([f"{p}_l" for p in low]) + "]")
    w("+ [" + wrap([f"{p}_o" for p in low]) + "] m328_lvl")
    ctl_in = [n for p in low for n in (f"{p}_e", f"{p}_p")]
    ctl_out = [n for p in low for n in (f"{p}_ea", f"{p}_pa")]
    w("aCtl [" + wrap(ctl_in) + "]")
    w("+ [" + wrap(ctl_out) + "] m328_bit")
    for p, lp in zip(PINS, low):
        w(f"aD{lp} {lp}_ea ({lp}_o {p}) m328_drv")
        w(f"aP{lp} {lp}_pa (vcc5 {p}) m328_pu")
    w("")
    w("* 10-bit DAC of the SAR converter: vdac = 5 V * code / 1024")
    w("aDac [b0 b1 b2 b3 b4 b5 b6 b7 b8 b9] [d0 d1 d2 d3 d4 d5 d6 d7 d8 d9] m328_bit")
    w("Bdac vdac 0 V = 5 * (v(d0) + 2*v(d1) + 4*v(d2) + 8*v(d3) + 16*v(d4) + 32*v(d5)")
    w("+ + 64*v(d6) + 128*v(d7) + 256*v(d8) + 512*v(d9)) / 1024")
    w("")
    w(".ends ATMEGA328P")
    text = "\n".join(out) + "\n"
    if a.output:
        with open(a.output, "w") as f:
            f.write(text)
        print(f"written {a.output}", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
