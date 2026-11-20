#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print symbols of the installed KiCad library, e.g. to embed them.

  python3 tools/dump_kicad_symbol.py MCU_RaspberryPi:RP2040 Device:Crystal_GND24
  python3 tools/dump_kicad_symbol.py --list Regulator_Switching | grep RT6150

Library folder: $KICAD_SYMBOL_DIR or /usr/share/kicad/symbols.
"""

import os
import sys

DIR = os.environ.get("KICAD_SYMBOL_DIR", "/usr/share/kicad/symbols")


def top_symbols(text):
    """(name, block) of the symbols at depth 1 of a .kicad_sym file."""
    depth, i, n = 0, 0, len(text)
    start = None
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "(":
            depth += 1
            if depth == 2 and text.startswith("(symbol ", i):
                start = i
        elif c == ")":
            if depth == 2 and start is not None:
                block = text[start:i + 1]
                name = block.split('"')[1]
                yield name, block
                start = None
            depth -= 1
        i += 1


def library(lib):
    path = os.path.join(DIR, lib + ".kicad_sym")
    with open(path, encoding="utf-8") as f:
        return dict(top_symbols(f.read()))


def main():
    args = sys.argv[1:]
    if args and args[0] == "--list":
        for name in library(args[1]):
            print(f"{args[1]}:{name}")
        return
    for item in args:
        lib, name = item.split(":", 1)
        try:
            symbols = library(lib)
        except OSError as err:
            print(f"### {item}: {err}", file=sys.stderr)
            continue
        if name not in symbols:
            print(f"### {item}: not found", file=sys.stderr)
            continue
        print(f"### {item}")
        print(symbols[name])


if __name__ == "__main__":
    main()
