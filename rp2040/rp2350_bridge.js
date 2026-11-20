// SPDX-License-Identifier: GPL-3.0-or-later
// kisimavr - RP2350 (Raspberry Pi Pico 2) co-simulation: the JavaScript
// side, on rp2350js (third_party/rp2350js). The interface with the C++ side
// is described in pico_common.js.
//
// The RP2350 boots its Hazard3 RISC-V or Cortex-M33 cores through the A2
// bootrom, which looks for the IMAGE_DEF block of the firmware, as on the
// chip. The core architecture is the one of the firmware: UF2 family ID,
// else the IMAGE_DEF block, else the "arch" key of the C++ side.

import { RP2350 } from '../third_party/rp2350js/rp2350.js';
import { GPIOPinState } from '../third_party/rp2350js/gpio-pin.js';
import { USBCDC } from '../third_party/rp2350js/usb/cdc.js';
import { setupVectoredRamBoot } from '../third_party/rp2350js/utils/load-firmware.js';
import { PicoBridge, bytesToText, forEachUf2Block, isUf2, makeLogger } from './pico_common.js';

const FLASH_BASE = 0x10000000;
const RAM_BASE = 0x20000000;
const RAM_WINDOW = 0x40000;          // SRAM searched for the IMAGE_DEF of a RAM image

const UF2_FAMILY_RP2040 = 0xe48bff56;
const UF2_FAMILY_ARM_S = 0xe48bff59;
const UF2_FAMILY_RISCV = 0xe48bff5a;
const UF2_FAMILY_ARM_NS = 0xe48bff5b;

const PICOBIN_MARKER_START = 0xffffded3;
const PICOBIN_ITEM_IMAGE_TYPE = 0x42;

/** Intel HEX -> [{ address, data }]. */
function hexSegments(text) {
  const out = [];
  let upper = 0;
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line.startsWith(':')) {
      continue;
    }
    const count = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);
    if (type === 0x04) {
      upper = parseInt(line.substr(9, 4), 16) << 16;
    } else if (type === 0x00) {
      const data = new Uint8Array(count);
      for (let i = 0; i < count; ++i) {
        data[i] = parseInt(line.substr(9 + 2 * i, 2), 16);
      }
      out.push({ address: (upper + addr) >>> 0, data });
    } else if (type === 0x01) {
      break;
    }
  }
  return out;
}

/** 'arm' or 'riscv' from the first IMAGE_TYPE item of the image, or ''. */
function archFromImageDef(segments) {
  for (const { data } of segments) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    for (let i = 0; i + 8 <= data.length; i += 4) {
      if (view.getUint32(i, true) === PICOBIN_MARKER_START && data[i + 4] === PICOBIN_ITEM_IMAGE_TYPE) {
        const flags = view.getUint16(i + 6, true);
        return ((flags >> 8) & 7) === 1 ? 'riscv' : 'arm';
      }
    }
  }
  return '';
}

export class Rp2350Bridge extends PicoBridge {
  /** @param {string} arch  'riscv', 'arm' or '' (from the firmware) */
  constructor(ios, adc, arch) {
    super(ios, adc);
    this.arch = arch || '';
  }

  load(bytes, isText) {
    // Firmware -> segments, then the core architecture.
    let segments = [];
    let family = 0;
    if (isText) {
      segments = hexSegments(bytesToText(bytes));
    } else if (isUf2(bytes)) {
      forEachUf2Block(bytes, (address, data, id) => {
        segments.push({ address, data });
        family = family || id;
      });
    } else {
      segments = [{ address: FLASH_BASE, data: bytes }];
    }
    if (family === UF2_FAMILY_RP2040) {
      throw new Error('this UF2 file is for the RP2040 (Pico), not the RP2350');
    }
    let arch = family === UF2_FAMILY_RISCV ? 'riscv' : '';
    if (family === UF2_FAMILY_ARM_S || family === UF2_FAMILY_ARM_NS) {
      arch = 'arm';
    }
    const imageArch = archFromImageDef(segments);
    if (!imageArch) {
      globalThis.kisimPrint?.('RP2350: no IMAGE_DEF block in the firmware: the bootrom will not ' +
        'start it (firmware for the RP2040?)');
    }
    arch = arch || imageArch || this.arch || 'riscv';
    if (this.arch && this.arch !== arch) {
      globalThis.kisimPrint?.(`RP2350: the firmware is built for ${arch}, not ${this.arch} (key "arch")`);
    }
    this.arch = arch;

    const mcu = new RP2350({ coreArch: arch });
    mcu.logger = makeLogger('RP2350');
    let ramBase = -1;
    let stored = 0;
    for (const { address, data } of segments) {
      if (address >= FLASH_BASE && address - FLASH_BASE + data.length <= mcu.flash.length) {
        mcu.flash.set(data, address - FLASH_BASE);
        ++stored;
      } else if (address >= RAM_BASE && address - RAM_BASE + data.length <= mcu.sram.length) {
        mcu.sram.set(data, address - RAM_BASE);
        ramBase = ramBase < 0 ? address : Math.min(ramBase, address);
        ++stored;
      }
    }
    if (stored === 0) {
      throw new Error('firmware without RP2350 flash or RAM data');
    }
    mcu.invalidateDecodeCache();
    if (ramBase >= 0) {
      setupVectoredRamBoot(mcu, ramBase, RAM_WINDOW);   // As after a UF2 copy to RAM.
    }
    mcu.reset();
    this.attach(mcu, mcu.clock, USBCDC, GPIOPinState);
  }

  advance(targetNs) {
    const { mcu, clock } = this;
    mcu.step();                     // Also wakes a sleeping core on an interrupt.
    const [c0, c1] = mcu.core;
    if (c0.waiting && c1.waiting && mcu.pioActiveSmCount === 0) {
      // Both cores asleep (wfi / wfe / h3.block): jump to the next alarm.
      const toAlarm = clock.nanosToNextAlarm;
      const left = targetNs - clock.nanos;
      if (left <= 0) {
        return;
      }
      const ns = toAlarm > 0 && toAlarm < left ? toAlarm : left;
      const cycles = Math.max(1, Math.ceil((ns * mcu.clkSys) / 1e9));
      c0.cycles += cycles;
      c1.cycles += cycles;
      mcu.stepThings(cycles);
    }
  }
}
