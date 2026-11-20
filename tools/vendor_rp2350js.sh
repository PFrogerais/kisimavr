#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Rebuild third_party/rp2350js: the rp2350js emulator (MIT, c1570, fork of
# Wokwi's rp2040js) compiled to plain JavaScript modules, run by QuickJS
# inside mcu_cosim.so for the RP2350 (Raspberry Pi Pico 2).
#   sh tools/vendor_rp2350js.sh [commit]      (needs git, tsc and node)
# Only the modules reachable from the ones the bridge imports are kept; the
# Node-only imports (fs, uf2) point to small shims that are never reached.
set -eu
REV=${1:-47bf55ddc590369e9c21cc003b0bf203dfb00cd8}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="$ROOT/third_party/rp2350js"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

git init -q "$WORK/src"
git -C "$WORK/src" fetch -q --depth 1 https://github.com/c1570/rp2350js.git "$REV"
git -C "$WORK/src" checkout -q FETCH_HEAD
# Type errors come from Node-only files (CLI, GDB, MCP): the JS is emitted anyway.
(cd "$WORK/src" && tsc -p tsconfig.esm.json --outDir "$WORK/out" --declaration false \
    --noEmitOnError false --target es2020) > /dev/null || true

rm -rf "$DEST"
node "$ROOT/tools/esm_vendor.mjs" "$WORK/out" "$DEST" \
    rp2350.js usb/cdc.js gpio-pin.js clock/simulation-clock.js utils/load-firmware.js

mkdir -p "$DEST/shims"
cat > "$DEST/shims/fs.js" <<'EOT'
// Node "fs" is not available in QuickJS: the bridge loads the firmware from bytes.
function unavailable() {
  throw new Error('fs is not available in the co-simulation');
}
export const existsSync = () => false;
export const readFileSync = unavailable;
export const writeFileSync = unavailable;
EOT
cat > "$DEST/shims/uf2.js" <<'EOT'
// The "uf2" npm package is not vendored: the bridge decodes UF2 files itself.
export function decodeBlock() {
  throw new Error('uf2 is not available in the co-simulation');
}
EOT
cp "$WORK/src/LICENSE" "$DEST/LICENSE"
cat > "$DEST/README.md" <<EOT
# rp2350js (vendored)

Raspberry Pi Pico / Pico 2 (RP2040, RP2350 with Hazard3 RISC-V or
Cortex-M33 cores) emulator by c1570, fork of Uri Shaked's rp2040js
(Wokwi), MIT licence (see LICENSE), https://github.com/c1570/rp2350js,
commit $REV, compiled from TypeScript to JavaScript modules by
tools/vendor_rp2350js.sh. Do not edit: rebuild with that script.

bootroms/: RP2040 B1 and RP2350 A2 bootroms
(https://github.com/raspberrypi/pico-bootrom-rp2350, BSD-3-Clause,
Copyright Raspberry Pi Ltd), as shipped by rp2350js.
shims/: stand-ins for the Node modules "fs" and "uf2" (never called).
EOT
echo "vendored rp2350js $REV into $DEST"
