#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Rebuild third_party/rp2040js: the rp2040js emulator (MIT, Wokwi) compiled
# to plain JavaScript modules, run by QuickJS inside mcu_cosim.so.
#   sh tools/vendor_rp2040js.sh [commit]      (needs git and tsc)
set -eu
REV=${1:-cab5fae8d72f44dc300dc139fb6e600f95e8031e}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/third_party/rp2040js"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

git init -q "$WORK/src"
git -C "$WORK/src" fetch -q --depth 1 https://github.com/wokwi/rp2040js.git "$REV"
git -C "$WORK/src" checkout -q FETCH_HEAD
# Type errors come from Node-only files (gdb TCP server): the JS is emitted anyway.
(cd "$WORK/src" && tsc -p tsconfig.json --outDir "$WORK/out" --declaration false) || true

rm -rf "$DEST"
mkdir -p "$DEST"
(cd "$WORK/out" && find . -name '*.js' ! -name 'gdb-tcp-server.js' | while read -r f; do
    mkdir -p "$DEST/$(dirname "$f")"
    cp "$f" "$DEST/$f"
done)
# RP2040 bootrom B1 (Raspberry Pi, BSD-3-Clause), from the rp2040js demo.
sed -e 's/^export const bootromB1 = new Uint32Array(\[/export const bootromB1 = new Uint32Array([/' \
    "$WORK/src/demo/bootrom.ts" > "$DEST/bootrom.js"
cp "$WORK/src/LICENSE" "$DEST/LICENSE"
cat > "$DEST/README.md" <<EOT
# rp2040js (vendored)

Raspberry Pi Pico (RP2040) emulator by Uri Shaked / Wokwi, MIT licence
(see LICENSE), https://github.com/wokwi/rp2040js, commit $REV,
compiled from TypeScript to JavaScript modules by tools/vendor_rp2040js.sh.
Do not edit: rebuild with that script.

bootrom.js: RP2040 bootrom B1 (https://github.com/raspberrypi/pico-bootrom,
BSD-3-Clause, Copyright Raspberry Pi (Trading) Ltd.), from the rp2040js demo.
EOT
echo "vendored rp2040js $REV into $DEST"
