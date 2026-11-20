#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the simulation models (.lib) of the KiCad demo projects.

The demo .lib files hold absolute paths (plug-in, firmware, logs), so they
are generated on each machine instead of being stored in git:

  python3 tools/setup_demos.py [--so /opt/kisimavr/mcu_cosim.so]
  # or: cmake --build build --target demos

Only the .lib model files are written: the schematics (.kicad_sch) are left
untouched, so edits made in KiCad are kept.

KiCad lower-cases the netlist: if the .so path contains upper case letters,
pass --so with a lower-case path (e.g. a link in /opt).
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PROJECTS = os.path.join(ROOT, "kicad", "projects")

# Projects using the small sub-circuits of kicad/atmega328p_cosim.lib.
COSIM_PROJECTS = ("simAVR_demo", "simAVR_adc", "simAVR_uart")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--so", default=os.path.join(ROOT, "build", "mcu_cosim.so"),
                    help="mcu_cosim.so (default: build/mcu_cosim.so)")
    args = ap.parse_args()
    so = os.path.abspath(os.path.expanduser(args.so))

    if not os.path.isfile(so):
        print(f"warning: plug-in not found: {so} (build it first)", file=sys.stderr)
    if not os.path.isfile(os.path.join(ROOT, "firmware", "full_demo.hex")):
        print("warning: firmwares not built: run 'make -C firmware'", file=sys.stderr)

    # Complete ATmega328P model (DIP-28 project): logs next to the project.
    full = os.path.join(PROJECTS, "simAVR_atmega328p", "atmega328p_full.lib")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_atmega328p_lib.py"),
                    "--hex", os.path.join(ROOT, "firmware", "full_demo.hex"),
                    "--so", so, "-o", full], check=True)

    # I2C demo (official symbol): LM75 at 20 degC rising by 1 degC per ms.
    i2c = os.path.join(PROJECTS, "simAVR_i2c", "atmega328p_full.lib")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_atmega328p_lib.py"),
                    "--hex", os.path.join(ROOT, "firmware", "i2c_lm75.hex"),
                    "--lm75temp", "20", "--lm75rate", "1000",
                    "--so", so, "-o", i2c], check=True)

    # SPI demo (official symbols): ATmega328P master + 74HC595, no I2C slave.
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_atmega328p_lib.py"),
                    "--hex", os.path.join(ROOT, "firmware", "spi_595.hex"), "--i2c=",
                    "--so", so, "-o", os.path.join(PROJECTS, "simAVR_spi", "atmega328p_full.lib")],
                   check=True)

    # Character LCD demo: ATmega328P + HD44780 display (second d_cosim part).
    lcd = os.path.join(PROJECTS, "simAVR_lcd")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_atmega328p_lib.py"),
                    "--hex", os.path.join(ROOT, "firmware", "lcd_demo.hex"), "--i2c=",
                    "--so", so, "-o", os.path.join(lcd, "atmega328p_full.lib")], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_lcd_lib.py"),
                    "--so", so, "-o", os.path.join(lcd, "lcd1602.lib")], check=True)

    # Raspberry Pi Pico (RP2040) demo: bare-metal firmware built by
    # "make -C firmware/pico", terminal at 3.3 V.
    pico = os.path.join(PROJECTS, "simPICO")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_pico_lib.py"),
                    "--fw", os.path.join(ROOT, "firmware", "pico", "pico_demo.bin"),
                    "--so", so, "-o", os.path.join(pico, "pico.lib")], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_terminal_lib.py"),
                    "--vhigh", "3.3", os.path.join(pico, "terminal.txt"),
                    "-o", os.path.join(pico, "terminal.lib")], check=True)

    # Raspberry Pi Pico board (official schematic): RP2040 chip model.
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_pico_lib.py"), "--chip",
                    "--fw", os.path.join(ROOT, "firmware", "pico", "pico_demo.bin"),
                    "--so", so, "-o", os.path.join(PROJECTS, "RaspberryPi_Pico", "rp2040.lib")],
                   check=True)

    # Pico I2C / SPI masters: bare-metal firmwares of firmware/pico; I2C
    # modules LM75 (25.5 degC) and LCD 16x2 I2C, one d_cosim rank each.
    tools = os.path.join(ROOT, "tools")
    for project, fw, name in (("simPICO_i2c", "pico_i2c.bin", "i2c"),
                              ("simPICO_spi", "pico_spi.bin", "spi")):
        subprocess.run([sys.executable, os.path.join(tools, "make_pico_lib.py"),
                        "--fw", os.path.join(ROOT, "firmware", "pico", fw), "--name", name,
                        "--so", so, "-o", os.path.join(PROJECTS, project, f"pico_{name}.lib")],
                       check=True)
    i2c = os.path.join(PROJECTS, "simPICO_i2c")
    subprocess.run([sys.executable, os.path.join(tools, "make_i2c_lib.py"), "--name", "lm75",
                    "--lm75", "0x48", "--temp", "25.5", "--log", "--irreversible", "11",
                    "--so", so, "-o", os.path.join(i2c, "i2c_lm75.lib")], check=True)
    subprocess.run([sys.executable, os.path.join(tools, "make_i2c_lib.py"), "--name", "lcd",
                    "--lcd", "0x27", "--irreversible", "12",
                    "--so", so, "-o", os.path.join(i2c, "i2c_lcd.lib")], check=True)

    # Sensors and actuators: radar (HC-SR04 + servo) and fan (NTC + L298N),
    # with an ATmega328P and with a Pico.
    for name in ("radar", "ventilo"):
        avr = os.path.join(PROJECTS, f"simAVR_{name}")
        subprocess.run([sys.executable, os.path.join(tools, "make_atmega328p_lib.py"),
                        "--hex", os.path.join(ROOT, "firmware", f"{name}.hex"), "--i2c=",
                        "--so", so, "-o", os.path.join(avr, f"atmega328p_{name}.lib")],
                       check=True)
        pico_dir = os.path.join(PROJECTS, f"simPICO_{name}")
        subprocess.run([sys.executable, os.path.join(tools, "make_pico_lib.py"),
                        "--fw", os.path.join(ROOT, "firmware", "pico", f"pico_{name}.bin"),
                        "--name", name, "--so", so,
                        "-o", os.path.join(pico_dir, f"pico_{name}.lib")], check=True)
    for project in ("simAVR_radar", "simPICO_radar"):
        out = os.path.join(PROJECTS, project)
        subprocess.run([sys.executable, os.path.join(tools, "make_module_lib.py"), "hcsr04",
                        "--dist", "40", "--rate", "-100", "--log", "--so", so,
                        "-o", os.path.join(out, "hcsr04.lib")], check=True)
        subprocess.run([sys.executable, os.path.join(tools, "make_module_lib.py"), "servo",
                        "--so", so, "-o", os.path.join(out, "servo.lib")], check=True)

    # Raspberry Pi Pico 2 (RP2350, RISC-V cores): firmware built by
    # "make -C firmware/pico2"; simPICO2 and the board project.
    pico2 = os.path.join(PROJECTS, "simPICO2")
    fw2 = os.path.join(ROOT, "firmware", "pico2", "pico2_demo_riscv.bin")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_pico_lib.py"),
                    "--mcu", "rp2350", "--fw", fw2,
                    "--so", so, "-o", os.path.join(pico2, "pico2.lib")], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_terminal_lib.py"),
                    "--vhigh", "3.3", os.path.join(pico2, "terminal.txt"),
                    "-o", os.path.join(pico2, "terminal.lib")], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_pico_lib.py"),
                    "--mcu", "rp2350", "--chip", "--fw", fw2, "--so", so,
                    "-o", os.path.join(PROJECTS, "RaspberryPi_Pico2", "rp2350.lib")],
                   check=True)

    # Arduino UNO board (official schematic): uno_demo firmware, and the
    # terminal model rebuilt from terminal.txt (the text sent to RX).
    uno = os.path.join(PROJECTS, "Arduino_UNO")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_atmega328p_lib.py"),
                    "--hex", os.path.join(ROOT, "firmware", "uno_demo.hex"),
                    "--so", so, "-o", os.path.join(uno, "atmega328p_full.lib")], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_terminal_lib.py"),
                    os.path.join(uno, "terminal.txt"), "-o", os.path.join(uno, "terminal.lib")],
                   check=True)

    # Small demos: repository model with its relative paths made absolute.
    with open(os.path.join(ROOT, "kicad", "atmega328p_cosim.lib")) as f:
        lib = f.read()
    lib = lib.replace('"../build/mcu_cosim.so"', f'"{so}"')
    lib = lib.replace('"../', f'"{ROOT}/')
    for project in COSIM_PROJECTS:
        path = os.path.join(PROJECTS, project, "atmega328p_cosim.lib")
        with open(path, "w") as f:
            f.write(lib)
        print(f"written {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
