#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the SPICE model PICO: a Raspberry Pi Pico (RP2040) running a firmware.

Usage (KiCad project):
  python3 tools/make_pico_lib.py --fw ~/pico/blink/build/blink.uf2 -o ~/kicad/proj/pico.lib

The RP2040 is emulated by rp2040js (JavaScript, Wokwi) run by QuickJS inside
mcu_cosim.so. Firmware: .uf2 (Pico SDK, Arduino-pico "Export compiled
binary"), .bin (flash image) or .hex.

Sub-circuit pins (3.3 V logic, ground = node 0 as for the ATMEGA328P):
  GP0..GP22 GP26 GP27 GP28  VBUS VSYS V3V3_EN V3V3 ADC_VREF RUN
Pin numbers of the Pico board (pins 3 8 13 18 23 28 33 38 = GND, not mapped):
  see PICO_PINS below; GP25 drives the on-board LED (inside the model).
Every GPIO is bidirectional:
  - the voltage is read back through an adc_bridge (thresholds 0.8 / 2.0 V);
  - "level" drives 0 / 3.3 V through 50 ohm when "oe" is set;
  - "pu" / "pd" close 50 kohm pull-up / pull-down resistors.
GP26..GP28 (ADC0..2) also feed the comparators of the SAR model
(12 bits, Vref = 3.3 V).
"""

import argparse
import os
import sys

GPIOS = [f"GP{i}" for i in range(23)] + ["GP26", "GP27", "GP28"]
POWER = ["VBUS", "VSYS", "V3V3_EN", "V3V3", "ADC_VREF", "RUN"]
ADC = [0, 1, 2]

# Pico board pin numbers (top view, USB at the top: 1..20 on the left).
PICO_PINS = {
    "GP0": 1, "GP1": 2, "GP2": 4, "GP3": 5, "GP4": 6, "GP5": 7, "GP6": 9, "GP7": 10,
    "GP8": 11, "GP9": 12, "GP10": 14, "GP11": 15, "GP12": 16, "GP13": 17, "GP14": 19,
    "GP15": 20, "GP16": 21, "GP17": 22, "GP18": 24, "GP19": 25, "GP20": 26, "GP21": 27,
    "GP22": 29, "RUN": 30, "GP26": 31, "GP27": 32, "GP28": 34, "ADC_VREF": 35,
    "V3V3": 36, "V3V3_EN": 37, "VSYS": 39, "VBUS": 40,
}


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


def parse_args():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--fw", required=True, help="firmware: .uf2, .bin or .hex")
    ap.add_argument("-o", "--output", help="output .lib (default: stdout)")
    ap.add_argument("--so", default=os.path.join(here, "..", "build", "mcu_cosim.so"),
                    help="mcu_cosim.so (default: ../build/mcu_cosim.so)")
    ap.add_argument("--uartlog", help="UART / USB text file (default: next to the .lib)")
    ap.add_argument("--name", default="", help="suffix: PICO_<NAME>, for several Picos")
    ap.add_argument("--irreversible", type=int, default=1,
                    help="d_cosim rank, distinct for each co-simulated part (default 1)")
    ap.add_argument("--relative", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.relative:    # Repository copy used by the ngspice/ tests.
        args.uartlog = args.uartlog or "../uart_log.txt"
        return args
    base = os.path.dirname(os.path.abspath(args.output)) if args.output else os.getcwd()
    args.fw = os.path.abspath(os.path.expanduser(args.fw))
    args.so = os.path.abspath(os.path.expanduser(args.so))
    args.uartlog = os.path.abspath(os.path.expanduser(
        args.uartlog or os.path.join(base, "uart_log.txt")))
    for path, what in ((args.fw, "firmware"), (args.so, "plug-in")):
        if not os.path.isfile(path):
            print(f"warning: {what} not found: {path}", file=sys.stderr)
    if args.so != args.so.lower():
        print("warning: the .so path contains upper case letters; KiCad lower-cases\n"
              "         it and ngspice will not find it (use e.g. a link in /opt).",
              file=sys.stderr)
    return args


def main():
    a = parse_args()
    ios = GPIOS + ["GP25"]                       # GP25: on-board LED.
    low = [p.lower() for p in ios]
    sim_args = (['"mcu"', '"rp2040"', '"fw"', f'"{a.fw}"', '"io"'] + [f'"{p}"' for p in ios] +
                ['"adc"'] + [f'"{c}"' for c in ADC] + ['"uartlog"', f'"{a.uartlog}"'])
    pins = GPIOS + POWER
    sim_pins = " ".join(f"{PICO_PINS[p]}={p}" for p in sorted(pins, key=lambda p: PICO_PINS[p]))

    out = []
    w = out.append
    w("* =============================================================================")
    w("* PICO : Raspberry Pi Pico (RP2040) co-simulation model (kisimavr)")
    w("*   rp2040js (Wokwi) runs the firmware inside mcu_cosim.so (QuickJS, d_cosim)")
    w("*")
    w("* GPIO: 3.3 V, bidirectional, direction and pulls set by the firmware.")
    w("*   UART0: TX = GP0 / GP12 / GP16, RX = GP1 / GP13 / GP17 (frames rebuilt by the")
    w("*   plug-in); UART1 on GP4 / GP5, GP8 / GP9; text in uart_log.txt. USB serial")
    w("*   (Serial of Arduino-pico): text only. ADC0..2 = GP26..GP28 (SAR, 12 bits).")
    w("*   GP25: on-board LED (green, 470 ohm), inside the model.")
    w("*   Ground = node 0 (pins 3 8 13 18 23 28 33 38 of the board).")
    w("*")
    w("* KiCad: Sim.Device = SUBCKT, Sim.Name = PICO, Sim.Pins =")
    w(f"*   {sim_pins}")
    w("*")
    w("* New firmware: rerun tools/make_pico_lib.py --fw <file> -o <lib>")
    w("* or edit the path after \"fw\". Keep the .so path in lower case.")
    w("* Generated by tools/make_pico_lib.py")
    w("* =============================================================================")
    w("")
    w(f'.model pico_core d_cosim simulation="{a.so}"')
    w("+ sim_args=[" + wrap(sim_args) + "]")
    w("+ delay=100n")
    if a.irreversible != 1:
        w(f"+ irreversible={a.irreversible}")
    w("")
    w(".model pico_in  adc_bridge(in_low=0.8 in_high=2.0)")
    w(".model pico_lvl dac_bridge(out_low=0 out_high=3.3 out_undef=0 t_rise=5n t_fall=5n)")
    w(".model pico_bit dac_bridge(out_low=0 out_high=1 out_undef=0 t_rise=5n t_fall=5n)")
    w(".model pico_drv aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e9 r_on=50 log=TRUE)")
    w(".model pico_pul aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e12 r_on=50k log=TRUE)")
    w(".model pico_cmp adc_bridge(in_low=-0.1m in_high=0.1m)")
    w(".model pico_led D(IS=1e-19 RS=10 N=2)")
    w("")
    w(".subckt PICO " + " ".join(pins))
    w("")
    w("* 3.3 V regulator output (ideal), ADC reference through 200 ohm (as on the board)")
    w("V33 v33 0 3.3")
    w("R33 v33 V3V3 0.1")
    w("Rvref V3V3 ADC_VREF 200")
    w("* Power and control inputs (not modelled): high impedance, RUN pulled up")
    w("Rvbus VBUS 0 1meg")
    w("Rvsys VSYS 0 1meg")
    w("Ren V3V3_EN 0 1meg")
    w("Rrun RUN V3V3 50k")
    w("* On-board LED on GP25")
    w("Rled GP25 led 470")
    w("Dled led 0 pico_led")
    w("")
    w("* Pin voltages read back by the RP2040")
    w("aIn [" + wrap(ios) + "]")
    w("+ [" + wrap([f"{p}_i" for p in low]) + "] pico_in")
    w("")
    w("* SAR ADC comparators: (GPn - vdac) > 0")
    for c in ADC:
        w(f"Ec{c} c{c} 0 GP{26 + c} vdac 1")
    w("aCmp [" + " ".join(f"c{c}" for c in ADC) + "] [" +
      " ".join(f"c{c}_d" for c in ADC) + "] pico_cmp")
    w("")
    w("* The RP2040: inputs = pin reads + comparators,")
    w("*             outputs = (level, oe, pu, pd) per pin + DAC bits b0..b11")
    ins = [f"{p}_i" for p in low] + [f"c{c}_d" for c in ADC]
    outs = [n for p in low for n in (f"{p}_l", f"{p}_e", f"{p}_u", f"{p}_d")] + \
           [f"b{k}" for k in range(12)]
    w("aMcu [" + wrap(ins) + "]")
    w("+ [" + wrap(outs) + "]")
    w("+ null pico_core")
    w("")
    w("* Pin drivers: 0 / 3.3 V level, output enable, pull-up, pull-down")
    w("aLvl [" + wrap([f"{p}_l" for p in low]) + "]")
    w("+ [" + wrap([f"{p}_o" for p in low]) + "] pico_lvl")
    ctl_in = [n for p in low for n in (f"{p}_e", f"{p}_u", f"{p}_d")]
    ctl_out = [n for p in low for n in (f"{p}_ea", f"{p}_ua", f"{p}_da")]
    w("aCtl [" + wrap(ctl_in) + "]")
    w("+ [" + wrap(ctl_out) + "] pico_bit")
    for p, lp in zip(ios, low):
        w(f"aD{lp} {lp}_ea ({lp}_o {p}) pico_drv")
        w(f"aU{lp} {lp}_ua (v33 {p}) pico_pul")
        w(f"aP{lp} {lp}_da ({p} 0) pico_pul")
    w("")
    w("* 12-bit DAC of the SAR converter: vdac = 3.3 V * code / 4096")
    w("aDac [" + " ".join(f"b{k}" for k in range(12)) + "] [" +
      " ".join(f"d{k}" for k in range(12)) + "] pico_bit")
    terms = " + ".join(f"{1 << k}*v(d{k})" for k in range(12))
    w("Bdac vdac 0 V = 3.3 * (" + wrap(terms.split(" "), width=70) + ") / 4096")
    w("")
    w(".ends PICO")
    text = "\n".join(out) + "\n"
    if a.name:
        n = "".join(ch for ch in a.name if ch.isalnum() or ch == "_")
        text = (text.replace(".subckt PICO ", f".subckt PICO_{n.upper()} ")
                    .replace(".ends PICO", f".ends PICO_{n.upper()}")
                    .replace("Sim.Name = PICO,", f"Sim.Name = PICO_{n.upper()},")
                    .replace("pico_", f"pico{n.lower()}_"))
    if a.output:
        with open(a.output, "w") as f:
            f.write(text)
        print(f"written {a.output}", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
