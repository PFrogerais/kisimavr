#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the SPICE model PICO: a Raspberry Pi Pico (RP2040) running a firmware.

Usage (KiCad project):
  python3 tools/make_pico_lib.py --fw ~/pico/blink/build/blink.uf2 -o ~/kicad/proj/pico.lib
  python3 tools/make_pico_lib.py --chip --fw prog.uf2 -o rp2040.lib   (RP2040 chip alone)
  python3 tools/make_pico_lib.py --mcu rp2350 --fw blink.uf2 -o pico2.lib   (Pico 2: PICO2)
  python3 tools/make_pico_lib.py --mcu rp2350 --chip --fw blink.uf2 -o rp2350.lib

The RP2040 is emulated by rp2040js (JavaScript, Wokwi), the RP2350 by
rp2350js (Hazard3 RISC-V or Cortex-M33 cores, from the firmware), run by
QuickJS inside mcu_cosim.so. Firmware: .uf2 (Pico SDK, Arduino-pico
"Export compiled binary"), .bin (flash image) or .hex.
The Pico 2 has the pinout of the Pico: same sub-circuit pins, sub-circuit
PICO2; the RP2350A chip alone is RP2350 (GPIO0..GPIO29).

Sub-circuit pins (3.3 V logic, ground = node 0 as for the ATMEGA328P):
  GP0..GP22 GP26 GP27 GP28  VBUS VSYS V3V3_EN V3V3 ADC_VREF RUN
Pin numbers of the Pico board (pins 3 8 13 18 23 28 33 38 = GND, not mapped):
  see PICO_PINS below; GP25 drives the on-board LED (inside the model).
Every GPIO is bidirectional:
  - the voltage is read back through an adc_bridge (thresholds 0.8 / 2.0 V);
  - "level" drives 0 / 3.3 V through 50 ohm when "oe" is set;
  - "pu" / "pd" close 50 kohm pull-up / pull-down resistors.
GP26..GP29 (ADC0..3) also feed the comparators of the SAR model
(12 bits, Vref = 3.3 V); on the board GP29 reads VSYS / 3.
"""

import argparse
import os
import re
import sys

GPIOS = [f"GP{i}" for i in range(23)] + ["GP26", "GP27", "GP28"]
POWER = ["VBUS", "VSYS", "V3V3_EN", "V3V3", "ADC_VREF", "RUN"]
ADC = [0, 1, 2, 3]
# Inside the module: GP23 (SMPS mode), GP24 (VBUS sense), GP25 (LED), GP29 (VSYS / 3).
INTERNAL = ["GP23", "GP24", "GP25", "GP29"]
CHIP_GPIOS = [f"GPIO{i}" for i in range(30)]
# RP2040 chip (QFN-56) pin numbers of the GPIOs, symbol MCU_RaspberryPi:RP2040.
CHIP_PINS = dict(zip(CHIP_GPIOS, [2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18,
                                  27, 28, 29, 30, 31, 32, 34, 35, 36, 37, 38, 39, 40, 41]))
# RP2350A chip (QFN-60) pin numbers of the GPIOs, symbol MCU_RaspberryPi:RP2350A.
CHIP_PINS_RP2350 = dict(zip(CHIP_GPIOS, [2, 3, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18,
                                         19, 27, 28, 29, 31, 32, 33, 34, 35, 36, 37, 40, 41,
                                         42, 43]))

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
    ap.add_argument("--pty", nargs="?", const="~/kisimavr_uart0", default="",
                    help="UART0 on a pseudo-terminal for a terminal program (picocom, "
                         "CuteCom...), reached by this link (default ~/kisimavr_uart0)")
    ap.add_argument("--ptyusb", nargs="?", const="~/kisimavr_usb", default="",
                    help="USB serial port (Serial of Arduino-pico) on a pseudo-terminal "
                         "(default link ~/kisimavr_usb)")
    ap.add_argument("--name", default="", help="suffix: PICO_<NAME>, for several Picos")
    ap.add_argument("--irreversible", type=int, default=1,
                    help="d_cosim rank, distinct for each co-simulated part (default 1)")
    ap.add_argument("--mcu", choices=["rp2040", "rp2350"], default="rp2040",
                    help="rp2040 (Pico, default) or rp2350 (Pico 2)")
    ap.add_argument("--arch", choices=["riscv", "arm"],
                    help="RP2350 cores when the firmware does not tell (default: from "
                         "the UF2 family / IMAGE_DEF block)")
    ap.add_argument("--chip", action="store_true",
                    help="chip alone (sub-circuit RP2040 / RP2350, pins GPIO0..GPIO29) "
                         "instead of the Pico board")
    ap.add_argument("--relative", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.arch and args.mcu != "rp2350":
        ap.error("--arch is for --mcu rp2350")

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
    rp2350 = a.mcu == "rp2350"
    chip = "RP2350" if rp2350 else "RP2040"
    board = "Raspberry Pi Pico 2" if rp2350 else "Raspberry Pi Pico"
    emulator = ("rp2350js (c1570, fork of rp2040js)" if rp2350
                else "rp2040js (Wokwi)")
    if a.chip:
        # The chip alone: every GPIO is a pin of the sub-circuit.
        sub, tag = chip, chip.lower()
        names = CHIP_GPIOS
        ios = [f"GP{i}" for i in range(30)]
        pins = CHIP_GPIOS
        numbers = CHIP_PINS_RP2350 if rp2350 else CHIP_PINS
        sim_pins = " ".join(f"{numbers[p]}={p}" for p in CHIP_GPIOS)
    else:
        sub, tag = ("PICO2", "pico2") if rp2350 else ("PICO", "pico")
        names = GPIOS + INTERNAL
        ios = names
        pins = GPIOS + POWER
        sim_pins = " ".join(f"{PICO_PINS[p]}={p}"
                            for p in sorted(pins, key=lambda p: PICO_PINS[p]))
    nodes = {g: n for g, n in zip(ios, names)}      # GPIO -> node in the sub-circuit
    low = [g.lower() for g in ios]
    arch = ['"arch"', f'"{a.arch}"'] if a.arch else []
    sim_args = (['"mcu"', f'"{a.mcu}"', '"fw"', f'"{a.fw}"'] + arch + ['"io"'] +
                [f'"{g}"' for g in ios] +
                ['"adc"'] + [f'"{c}"' for c in ADC] + ['"uartlog"', f'"{a.uartlog}"'] +
                (['"pty"', f'"{a.pty}"'] if a.pty else []) +
                (['"ptyusb"', f'"{a.ptyusb}"'] if a.ptyusb else []))

    out = []
    w = out.append
    w("* =============================================================================")
    if a.chip:
        w(f"* {sub} : Raspberry Pi {chip} chip, co-simulation model (kisimavr)")
    else:
        w(f"* {sub} : {board} ({chip}) co-simulation model (kisimavr)")
    w(f"*   {emulator} runs the firmware inside mcu_cosim.so")
    w("*   (QuickJS, d_cosim)")
    w("*")
    w("* GPIO: 3.3 V, bidirectional, direction and pulls set by the firmware.")
    w("*   UART0: TX = GP0 / GP12 / GP16, RX = GP1 / GP13 / GP17 (frames rebuilt by the")
    w("*   plug-in); UART1 on GP4 / GP5, GP8 / GP9; text in uart_log.txt. USB serial")
    w("*   (Serial of Arduino-pico): text only. ADC0..3 = GP26..GP29 (SAR, 12 bits).")
    if a.chip:
        w("*   Supply pins (IOVDD, DVDD...), crystal, QSPI, USB, SWD: not simulated;")
        w("*   I/O levels from an internal 3.3 V, ground = node 0.")
    else:
        w("*   Inside the model, as on the board: GP25 = LED (470 ohm), GP24 = VBUS sense")
        w("*   (5.6k / 10k), GP29 = ADC3 = VSYS / 3 (200k / 100k, 1 nF), GP23 free.")
        w("*   Ground = node 0 (pins 3 8 13 18 23 28 33 38 of the board).")
    w("*")
    w(f"* KiCad: Sim.Device = SUBCKT, Sim.Name = {sub}, Sim.Pins =")
    w(f"*   {sim_pins}")
    w("*")
    opts = (" --mcu rp2350" if rp2350 else "") + (" --chip" if a.chip else "")
    w(f"* New firmware: rerun tools/make_pico_lib.py{opts} "
      "--fw <file> -o <lib>")
    w("* or edit the path after \"fw\". Keep the .so path in lower case.")
    w("* Generated by tools/make_pico_lib.py")
    w("* =============================================================================")
    w("")
    w(f'.model {tag}_core d_cosim simulation="{a.so}"')
    w("+ sim_args=[" + wrap(sim_args) + "]")
    w("+ delay=100n")
    if a.irreversible != 1:
        w(f"+ irreversible={a.irreversible}")
    w("")
    w(f".model {tag}_in  adc_bridge(in_low=0.8 in_high=2.0)")
    w(f".model {tag}_lvl dac_bridge(out_low=0 out_high=3.3 out_undef=0 t_rise=5n t_fall=5n)")
    w(f".model {tag}_bit dac_bridge(out_low=0 out_high=1 out_undef=0 t_rise=5n t_fall=5n)")
    w(f".model {tag}_drv aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e9 r_on=50 log=TRUE)")
    w(f".model {tag}_pul aswitch(cntl_off=0.1 cntl_on=0.9 r_off=1e12 r_on=50k log=TRUE)")
    w(f".model {tag}_cmp adc_bridge(in_low=-0.1m in_high=0.1m)")
    if not a.chip:
        w(f".model {tag}_led D(IS=1e-19 RS=10 N=2)")
    w("")
    w(f".subckt {sub} " + " ".join(pins))
    w("")
    w("* Internal 3.3 V of the I/O drivers")
    w("V33 v33 0 3.3")
    if not a.chip:
        w("* 3.3 V regulator output (ideal), ADC reference through 200 ohm (as on the board)")
        w("R33 v33 V3V3 0.1")
        w("Rvref V3V3 ADC_VREF 200")
        w("* Power and control inputs: VSYS / 3 divider on GP29, VBUS sense on GP24")
        w("Rvbus VBUS GP24 5.6k")
        w("Rsense GP24 0 10k")
        w("Rsys1 VSYS GP29 200k")
        w("Rsys2 GP29 0 100k")
        w("Csys GP29 0 1n")
        w("Ren V3V3_EN VSYS 100k")
        w("Rrun RUN V3V3 50k")
        w("* On-board LED on GP25; GP23 (SMPS mode) without load")
        w("Rled GP25 led 470")
        w(f"Dled led 0 {tag}_led")
        w("Rgp23 GP23 0 10meg")
    w("")
    w(f"* Pin voltages read back by the {chip}")
    w("aIn [" + wrap([nodes[g] for g in ios]) + "]")
    w("+ [" + wrap([f"{g}_i" for g in low]) + f"] {tag}_in")
    w("")
    w("* SAR ADC comparators: (GPn - vdac) > 0")
    for c in ADC:
        w(f"Ec{c} c{c} 0 {nodes[f'GP{26 + c}']} vdac 1")
    w("aCmp [" + " ".join(f"c{c}" for c in ADC) + "] [" +
      " ".join(f"c{c}_d" for c in ADC) + f"] {tag}_cmp")
    w("")
    w(f"* The {chip}: inputs = pin reads + comparators,")
    w("*             outputs = (level, oe, pu, pd) per pin + DAC bits b0..b11")
    ins = [f"{g}_i" for g in low] + [f"c{c}_d" for c in ADC]
    outs = [n for g in low for n in (f"{g}_l", f"{g}_e", f"{g}_u", f"{g}_d")] + \
           [f"b{k}" for k in range(12)]
    w("aMcu [" + wrap(ins) + "]")
    w("+ [" + wrap(outs) + "]")
    w(f"+ null {tag}_core")
    w("")
    w("* Pin drivers: 0 / 3.3 V level, output enable, pull-up, pull-down")
    w("aLvl [" + wrap([f"{g}_l" for g in low]) + "]")
    w("+ [" + wrap([f"{g}_o" for g in low]) + f"] {tag}_lvl")
    ctl_in = [n for g in low for n in (f"{g}_e", f"{g}_u", f"{g}_d")]
    ctl_out = [n for g in low for n in (f"{g}_ea", f"{g}_ua", f"{g}_da")]
    w("aCtl [" + wrap(ctl_in) + "]")
    w("+ [" + wrap(ctl_out) + f"] {tag}_bit")
    for g, lg in zip(ios, low):
        node = nodes[g]
        w(f"aD{lg} {lg}_ea ({lg}_o {node}) {tag}_drv")
        w(f"aU{lg} {lg}_ua (v33 {node}) {tag}_pul")
        w(f"aP{lg} {lg}_da ({node} 0) {tag}_pul")
    w("")
    w("* 12-bit DAC of the SAR converter: vdac = 3.3 V * code / 4096")
    w("aDac [" + " ".join(f"b{k}" for k in range(12)) + "] [" +
      " ".join(f"d{k}" for k in range(12)) + f"] {tag}_bit")
    terms = " + ".join(f"{1 << k}*v(d{k})" for k in range(12))
    w("Bdac vdac 0 V = 3.3 * (" + wrap(terms.split(" "), width=70) + ") / 4096")
    w("")
    w(f".ends {sub}")
    text = "\n".join(out) + "\n"
    if a.name:
        n = "".join(ch for ch in a.name if ch.isalnum() or ch == "_")
        text = (text.replace(f".subckt {sub} ", f".subckt {sub}_{n.upper()} ")
                    .replace(f".ends {sub}", f".ends {sub}_{n.upper()}")
                    .replace(f"Sim.Name = {sub},", f"Sim.Name = {sub}_{n.upper()},"))
        text = re.sub(rf"\b{tag}_(core|in|lvl|bit|drv|pul|cmp|led)\b",
                      lambda m: f"{tag}{n.lower()}_{m.group(1)}", text)
    if a.output:
        with open(a.output, "w") as f:
            f.write(text)
        print(f"written {a.output}", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
