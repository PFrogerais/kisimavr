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
