# rp2350js (vendored)

Raspberry Pi Pico / Pico 2 (RP2040, RP2350 with Hazard3 RISC-V or
Cortex-M33 cores) emulator by c1570, fork of Uri Shaked's rp2040js
(Wokwi), MIT licence (see LICENSE), https://github.com/c1570/rp2350js,
commit 47bf55ddc590369e9c21cc003b0bf203dfb00cd8, compiled from TypeScript to JavaScript modules by
tools/vendor_rp2350js.sh. Do not edit: rebuild with that script.

bootroms/: RP2040 B1 and RP2350 A2 bootroms
(https://github.com/raspberrypi/pico-bootrom-rp2350, BSD-3-Clause,
Copyright Raspberry Pi Ltd), as shipped by rp2350js.
shims/: stand-ins for the Node modules "fs" and "uf2" (never called).
